//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "Menus.h"
#include "Profile.h"

namespace InputTranslator
{

// Whether or not debug messages print depends on which line is commented out
//#define transDebugPrint(...) debugPrint("InputTranslator: " __VA_ARGS__)
#define transDebugPrint(...) ((void)0)

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kMaxLayerChangesPerUpdate = 20,
};


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	u16 shortHoldTime;
	u16 longHoldTime;

	void load()
	{
		shortHoldTime = Profile::getInt("System/ButtonShortHoldTime", 400);
		longHoldTime = Profile::getInt("System/ButtonLongHoldTime", 1200);
	}
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ButtonState
{
	const Command* commands;
	const Command* commandsWhenPressed;
	u16 commandsLayer;
	u16 layerHeld;
	u16 heldTime;
	u16 vKeyHeld;
	bool heldDown;
	bool shortHoldDone;
	bool longHoldDone;
	bool usedInButtonCombo;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		ZeroMemory(this, sizeof(ButtonState));
	}

	void resetWhenReleased()
	{
		const Command* backupCommands = this->commands;
		const u16 backupParentLayer = this->commandsLayer;
		const u16 backupChildLayer = this->layerHeld;
		clear();
		this->commands = backupCommands;
		this->commandsLayer = backupParentLayer;
		this->layerHeld = backupChildLayer;
	}

	ButtonState() : vKeyHeld() { clear(); }
};

struct LayerState
{
	ButtonState autoButton;
	u16 parentLayerID;
	bool active;
	bool newlyActive;
	bool ownedButtonHit;

	void clear()
	{
		autoButton.clear();
		parentLayerID = 0;
		active = false;
		newlyActive = false;
		ownedButtonHit = false;
	}
};

struct TranslatorState
{
	ButtonState gamepadButtons[eBtn_Num];
	std::vector<LayerState> layers;
	std::vector<u16> layerOrder;
	int mouseLookTime;
	bool mouseLookOn;

	void clear()
	{
		for(size_t i = 0; i < ARRAYSIZE(gamepadButtons); ++i)
			gamepadButtons[i].clear();
		for(size_t i = 0; i < layers.size(); ++i)
			layers[i].autoButton.clear();
		layers.clear();
		layerOrder.clear();
		mouseLookTime = 0;
		mouseLookOn = false;
	}
};

struct InputResults
{
	std::vector<Command> keys;
	std::vector<Command> strings;
	s16 charMove;
	s16 charTurn;
	s16 charStrafe;
	s16 mouseMoveX;
	s16 mouseMoveY;
	s16 mouseWheelY;
	bool mouseMoveDigital;
	bool mouseWheelDigital;
	bool mouseWheelStepped;
	bool layerChangeMade;

	void clear()
	{
		keys.clear();
		strings.clear();
		charMove = 0;
		charTurn = 0;
		charStrafe = 0;
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		mouseWheelStepped = false;
		layerChangeMade = false;
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static TranslatorState sState;
static InputResults sResults;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void loadLayerData()
{
	DBG_ASSERT(sState.layers.empty());

	sState.layers.reserve(InputMap::controlsLayerCount());
	sState.layers.resize(InputMap::controlsLayerCount());
	for(u16 i = 0; i < sState.layers.size(); ++i)
	{
		sState.layers[i].clear();
		// Assign auto-button commands for this layer ID
		// Note that these leave .commandsLayer as default 0 value,
		// to prevent them from setting ownedButtonHit flags,
		// which should only be set by actual gamepad button presses.
		sState.layers[i].autoButton.commands =
			InputMap::commandsForButton(i, eBtn_None);
	}
}


static void	loadButtonCommandsForCurrentLayers()
{
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx) // skip eBtn_None
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		// Start with the front-most layer, and stop once get a non-null
		for(std::vector<u16>::const_reverse_iterator itr =
			sState.layerOrder.rbegin();
			itr != sState.layerOrder.rend(); ++itr)
		{
			aBtnState.commands =
				InputMap::commandsForButton(*itr, EButton(aBtnIdx));
			if( aBtnState.commands )
			{
				aBtnState.commandsLayer = *itr;
				break;
			}
		}
	}
}


static void removeControlsLayer(u16 theLayerID)
{
	if( theLayerID == 0 || !sState.layers[theLayerID].active )
		return;

	// Remove given layer ID and any other layers it spawned via 'Add'
	// (reverse iteration so children likely to be removed first)
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin(), next_itr = itr;
		itr != sState.layerOrder.rend(); itr = next_itr)
	{
		++next_itr;
		LayerState& aLayer = sState.layers[*itr];
		if( aLayer.parentLayerID == theLayerID )
		{
			// Need to use recursion so also remove grandchildren, etc
			removeControlsLayer(*itr);
			// Recursion will mess up our iterator beyond reasonable recovery,
			// so just start at the beginning (end) again
			next_itr = sState.layerOrder.rbegin();
		}
		else if( *itr == theLayerID )
		{
			transDebugPrint("Removing Controls Layer: %s\n",
				InputMap::layerLabel(*itr).c_str());

			// Reset some layer properties
			aLayer.active = false;
			aLayer.ownedButtonHit = false;
			aLayer.newlyActive = false;
			sResults.layerChangeMade = true;

			// Remove from the layer order and recover iterator to continue
			next_itr = std::vector<u16>::reverse_iterator(
				sState.layerOrder.erase((itr+1).base()));
		}
	}
}


static void addControlsLayer(u16 theLayerID, u16 theSpawningLayerID = 0)
{
	DBG_ASSERT(theLayerID < sState.layers.size());
	DBG_ASSERT(theSpawningLayerID < sState.layers.size());

	// Remove the layer first if it is already active
	removeControlsLayer(theLayerID);
	DBG_ASSERT(!sState.layers[theLayerID].active);

	transDebugPrint("Adding Controls Layer: %s\n",
		InputMap::layerLabel(theLayerID).c_str());

	sState.layerOrder.push_back(theLayerID);
	LayerState& aLayer = sState.layers[theLayerID];
	aLayer.parentLayerID = theSpawningLayerID;
	aLayer.active = true;
	aLayer.newlyActive = true;
	sResults.layerChangeMade = true;
}


static void releaseKeyHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.vKeyHeld )
	{
		Command aCmd;
		aCmd.type = eCmdType_ReleaseKey;
		aCmd.data = theBtnState.vKeyHeld;
		InputDispatcher::sendKeyCommand(aCmd);
		theBtnState.vKeyHeld = 0;
	}
}


static void processCommand(ButtonState& theBtnState, const Command& theCmd)
{
	Command aForwardCmd;
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		// Do nothing
		break;
	case eCmdType_PressAndHoldKey:
		// Release any previously-held key first!
		releaseKeyHeldByButton(theBtnState);
		// Send this right away since can often be instantly processed
		// instead of needing to wait for input queue
		InputDispatcher::sendKeyCommand(theCmd);
		// Make note that this button is holding this key
		theBtnState.vKeyHeld = theCmd.data;
		break;
	case eCmdType_ReleaseKey:
		// Handled by releaseKeyHeldByButton() instead
		DBG_ASSERT(false && "_ReleaseKey should not be directly assigned!");
		break;
	case eCmdType_TapKey:
	case eCmdType_VKeySequence:
		// Queue to send after press/release commands but before strings
		sResults.keys.push_back(theCmd);
		break;
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		// Queue to send last, since can block other input the longest
		sResults.strings.push_back(theCmd);
		break;
	case eCmdType_AddControlsLayer:
		addControlsLayer(theCmd.data, theCmd.data2);
		break;
	case eCmdType_HoldControlsLayer:
		// Special-case, handled elsewhere
		break;
	case eCmdType_RemoveControlsLayer:
		DBG_ASSERT(theCmd.data != 0);
		removeControlsLayer(theCmd.data);
		break;
	case eCmdType_OpenSubMenu:
		Menus::openSubMenu(theCmd.data2, theCmd.data);
		break;
	case eCmdType_ReplaceMenu:
		Menus::replaceMenu(theCmd.data2, theCmd.data);
		break;
	case eCmdType_MenuReset:
		Menus::reset(theCmd.data);
		break;
	case eCmdType_MenuConfirm:
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.data);
		if( aForwardCmd.type != eCmdType_Empty )
			processCommand(theBtnState, aForwardCmd);
		break;
	case eCmdType_MenuConfirmAndClose:
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.data);
		if( aForwardCmd.type != eCmdType_Empty )
		{
			processCommand(theBtnState, aForwardCmd);
			// Close menu as well if this didn't just switch to a sub-menu
			if( aForwardCmd.type != eCmdType_Empty &&
				aForwardCmd.type < eCmdType_FirstMenuControl &&
				theBtnState.commandsLayer > 0 )
			{// Assume removing this layer "closes" the menu
				removeControlsLayer(theBtnState.commandsLayer);
			}
		}
		break;
	case eCmdType_MenuBack:
		Menus::closeLastSubMenu(theCmd.data);
		break;
	case eCmdType_MenuReassign:
		// TODO
		break;
	case eCmdType_TargetGroup:
		DBG_ASSERT(theCmd.data < eTargetGroupType_Num);
		switch(theCmd.data)
		{
		case eTargetGroupType_Reset:
			gGroupTargetOrigin = gDefaultGroupTarget;
			transDebugPrint("Resetting origin Group Member to default #%d\n",
				gDefaultGroupTarget);
			break;
		case eTargetGroupType_SetDefault:
			gDefaultGroupTarget = gLastGroupTarget;
			gDefaultGroupTargetUpdated = true;
			transDebugPrint("Setting Group Member #%d as default\n",
				gLastGroupTarget);
			break;
		case eTargetGroupType_Default:
			gLastGroupTarget = gGroupTargetOrigin = gDefaultGroupTarget;
			gLastGroupTargetUpdated = true;
			aForwardCmd.type = eCmdType_TapKey;
			aForwardCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
			sResults.keys.push_back(aForwardCmd);
			transDebugPrint("Targeting default Group Member #%d\n",
				gLastGroupTarget);
			break;
		case eTargetGroupType_Last:
			gLastGroupTargetUpdated = true;
			gGroupTargetOrigin = gLastGroupTarget;
			aForwardCmd.type = eCmdType_TapKey;
			aForwardCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
			sResults.keys.push_back(aForwardCmd);
			transDebugPrint("Re-targeting last group member/pet (#%d) \n",
				gLastGroupTarget);
			break;
		case eTargetGroupType_Prev:
			gLastGroupTargetUpdated = true;
			gGroupTargetOrigin =
				gGroupTargetOrigin == 0
					? 0 : gGroupTargetOrigin - 1;
			gLastGroupTarget = gGroupTargetOrigin;
			aForwardCmd.type = eCmdType_TapKey;
			aForwardCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
			sResults.keys.push_back(aForwardCmd);
			transDebugPrint("Targeting group member prev/up no-wrap (#%d)\n",
				gLastGroupTarget);
			break;
		case eTargetGroupType_Next:
			gLastGroupTargetUpdated = true;
			gGroupTargetOrigin =
				gGroupTargetOrigin >= InputMap::targetGroupSize() - 1
					? InputMap::targetGroupSize() - 1 : gGroupTargetOrigin + 1;
			gLastGroupTarget = gGroupTargetOrigin;
			aForwardCmd.type = eCmdType_TapKey;
			aForwardCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
			sResults.keys.push_back(aForwardCmd);
			transDebugPrint("Targeting group member next/down no-wrap (#%d)\n",
				gLastGroupTarget);
			break;
		case eTargetGroupType_PrevWrap:
			gLastGroupTargetUpdated = true;
			gGroupTargetOrigin =
				decWrap(gGroupTargetOrigin, InputMap::targetGroupSize());
			gLastGroupTarget = gGroupTargetOrigin;
			aForwardCmd.type = eCmdType_TapKey;
			aForwardCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
			sResults.keys.push_back(aForwardCmd);
			transDebugPrint("Targeting group member prev/up (#%d)\n",
				gLastGroupTarget);
			break;
		case eTargetGroupType_NextWrap:
			gLastGroupTargetUpdated = true;
			gGroupTargetOrigin =
				incWrap(gGroupTargetOrigin, InputMap::targetGroupSize());
			gLastGroupTarget = gGroupTargetOrigin;
			aForwardCmd.type = eCmdType_TapKey;
			aForwardCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
			sResults.keys.push_back(aForwardCmd);
			transDebugPrint("Targeting group member next/down (#%d)\n",
				gLastGroupTarget);
			break;
		}
		break;
	case eCmdType_MenuSelect:
		aForwardCmd =
			Menus::selectMenuItem(theCmd.data2, ECommandDir(theCmd.data));
		if( aForwardCmd.type != eCmdType_Empty )
			processCommand(theBtnState, aForwardCmd);
		break;
	case eCmdType_MenuSelectAndClose:
		aForwardCmd =
			Menus::selectMenuItem(theCmd.data2, ECommandDir(theCmd.data));
		if( aForwardCmd.type != eCmdType_Empty )
		{
			processCommand(theBtnState, aForwardCmd);
			// Close menu as well if this didn't just switch to a sub-menu
			if( aForwardCmd.type != eCmdType_Empty &&
				aForwardCmd.type < eCmdType_FirstMenuControl &&
				theBtnState.commandsLayer > 0 )
			{// Assume removing this layer "closes" the menu
				removeControlsLayer(theBtnState.commandsLayer);
			}
		}
		break;
	case eCmdType_MenuReassignDir:
	case eCmdType_HotspotSelect:
		// TODO
		break;
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Invalid command for this function!
		DBG_ASSERT(false && "Invalid command sent to processCommand()");
		break;
	case eCmdType_MouseWheel:
		if( theCmd.data2 == eMouseWheelMotion_Once )
		{
			InputDispatcher::scrollMouseWheelOnce(ECommandDir(theCmd.data));
			break;
		}
		// fall through
	default:
		// Invalid command for this function!
		DBG_ASSERT(false && "Invalid command sent to processCommand()");
		break;
	}
}


static void processButtonPress(ButtonState& theBtnState)
{
	// When first pressed, back up copy of current commands to be referenced
	// by other button actions later. This makes sure if layers change
	// before the button is released, it will still behave as it would
	// previously, which could be important - especially for cases like
	// _Release matching up with a particular _Press.
	theBtnState.commandsWhenPressed = theBtnState.commands;

	// Log that at least one button in the assigned layer has been pressed
	sState.layers[theBtnState.commandsLayer].ownedButtonHit = true;

	if( !theBtnState.commands )
		return;

	// Get default command for pressing this button (_Down action)
	const Command& aCmd = theBtnState.commands[eBtnAct_Down];

	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Handled in processContinuousInput instead
		return;
	case eCmdType_MouseWheel:
		// Handled in processContinuousInput instead unless set to _Once
		if( aCmd.data2 != eMouseWheelMotion_Once )
			return;
		break;
	}

	// _Press is processed before _Down since it is specifically called out
	// and the name implies it should be first action on button press.
	processCommand(theBtnState, theBtnState.commands[eBtnAct_Press]);
	processCommand(theBtnState, aCmd);
}


static void processContinuousInput(
	ButtonState& theBtnState,
	u8 theAnalogVal,
	bool isDigitalDown)
{
	// Use commandsWhenPressed if has a continuous action, otherwise current
	// These inputs should always be assigned to the _Down button action
	Command aCmd;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Down];
	if( aCmd.type == eCmdType_Empty && theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed )
	{
		aCmd = theBtnState.commands[eBtnAct_Down];
	}

	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Continue to analog checks below
		break;
	case eCmdType_MouseWheel:
		// Continue to analog checks below unless set to _Once
		if( aCmd.data2 != eMouseWheelMotion_Once )
			break;
		return;
	default:
		// Handled elsewhere
		return;
	}

	if( theAnalogVal )
		isDigitalDown = false;
	else if( isDigitalDown )
		theAnalogVal = 255;
	else // if( theAnalogVal == 0 && !isDigitalDown )
		return;

	switch(u32(aCmd.type << 16) | aCmd.data)
	{
	case (eCmdType_MoveTurn << 16) | eCmdDir_Forward:
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Forward:
		sResults.charMove += theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Back:
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Back:
		sResults.charMove -= theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Left:
		sResults.charTurn -= theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Right:
		sResults.charTurn += theAnalogVal;
		break;
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Left:
		sResults.charStrafe -= theAnalogVal;
		break;
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Right:
		sResults.charStrafe += theAnalogVal;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Left:
		sResults.mouseMoveX -= theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Right:
		sResults.mouseMoveX += theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Up:
		sResults.mouseMoveY -= theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Down:
		sResults.mouseMoveY += theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Up:
		if( aCmd.data2 == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY -= theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Down:
		if( aCmd.data2 == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY += theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	default:
		DBG_ASSERT(false && "Command and direction combo invalid");
		break;
	}
}


static void processButtonShortHold(ButtonState& theBtnState)
{
	theBtnState.shortHoldDone = true;

	// Skip if this button was used as a modifier to execute a button
	// combination command (i.e. is L2 for the combo L2+X).
	if( theBtnState.usedInButtonCombo )
		return;

	// Only ever use commandsWhenPressed for short hold action
	if( theBtnState.commandsWhenPressed )
	{
		processCommand(theBtnState,
			theBtnState.commandsWhenPressed[eBtnAct_ShortHold]);
	}
}


static void processButtonLongHold(ButtonState& theBtnState)
{
	theBtnState.longHoldDone = true;

	// Skip if this button was used as a modifier to execute a button
	// combination command (i.e. is L2 for the combo L2+X).
	if( theBtnState.usedInButtonCombo )
		return;

	// Only ever use commandsWhenPressed for long hold action
	if( theBtnState.commandsWhenPressed )
	{
		processCommand(theBtnState,
			theBtnState.commandsWhenPressed[eBtnAct_LongHold]);
	}
}


static void processButtonTap(ButtonState& theBtnState)
{
	// Only ever use commandsWhenPressed for tap action
	if( theBtnState.commandsWhenPressed )
	{
		processCommand(theBtnState,
			theBtnState.commandsWhenPressed[eBtnAct_Tap]);
	}
}


static void processButtonReleased(ButtonState& theBtnState)
{
	// Always release any key being held by this button when button is released
	releaseKeyHeldByButton(theBtnState);

	// If this button was used as a modifier to execute a button combination
	// command (i.e. is L2 for the combo L2+X), then do not perform any other
	// actions for this button besides releasing held key and resetting state.
	if( theBtnState.usedInButtonCombo )
	{
		theBtnState.resetWhenReleased();
		return;
	}

	// If released quickly enough, process 'tap' event
	if( !theBtnState.shortHoldDone )
	{// If released before .shortHoldDone, definitely a tap
		processButtonTap(theBtnState);
	}
	else if( !theBtnState.longHoldDone )
	{// May still be a tap before .longHoldDone
		// This is only allowed if has a valid _LongHold command type,
		// and does NOT have a valid _ShortHold command type.
		const ECommandType shortHoldCommandType =
			!theBtnState.commandsWhenPressed ? eCmdType_Empty :
			theBtnState.commandsWhenPressed[eBtnAct_ShortHold].type;
		const ECommandType longHoldCommandType =
			!theBtnState.commandsWhenPressed ? eCmdType_Empty :
			theBtnState.commandsWhenPressed[eBtnAct_LongHold].type;
		if( shortHoldCommandType == eCmdType_Empty &&
			longHoldCommandType != eCmdType_Empty )
		{
			processButtonTap(theBtnState);
		}
	}

	// Use commandsWhenPressed if it has a _Release action.
	// If not, can use the _Release action for current setup instead,
	// but ONLY if current commands do NOT have an associated _Press
	// action along with the _Release. This exception is to handle a
	// case where a layer was added that intends to be removed by
	// releasing a held button, but for some reason didn't use the
	// standard _HoldControlsLayer method that does it automatically.
	Command aCmd;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Release];
	if( aCmd.type == eCmdType_Empty &&
		theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		theBtnState.commands[eBtnAct_Press].type == eCmdType_Empty )
	{
		aCmd = theBtnState.commands[eBtnAct_Release];
	}
	processCommand(theBtnState, aCmd);

	// At this point no keys should be held by this button,
	// but maybe something weird happened with release/tap commands,
	// so check again to make sure
	releaseKeyHeldByButton(theBtnState);

	// Reset most of the button state when button is released
	theBtnState.resetWhenReleased();
}


static void processButtonState(
	ButtonState& theBtnState,
	bool isDown, bool wasHit, u8 theAnalogVal = 0)
{
	const bool wasDown = theBtnState.heldDown;

	// If button was down previously and has either been hit again or
	// isn't down any more, then it must have been released, and we should
	// process that before any new hits so use commandsWhenPressed.
	if( wasDown && (!isDown || wasHit) )
		processButtonReleased(theBtnState);

	// If was hit (or somehow is down but wasn't before), process a press
	if( wasHit || (isDown && !wasDown) )
	{
		processButtonPress(theBtnState);
		// Now that have processed a press, if it's not down any more,
		// it must have already been released, so process that as well.
		if( !isDown )
			processButtonReleased(theBtnState);
	}

	// Process continuous input, such as for analog axis values which
	// which do not necessarily return hit/release when lightly pressed
	processContinuousInput(theBtnState, theAnalogVal, isDown);

	// Update heldTime value and see if need to process a 'hold' event
	if( isDown )
	{
		theBtnState.heldDown = true;

		// If this button is holding a layer active and another button that
		// is assigned a command from that layer was pressed, then flag
		// this button as being used as the modifier key in a button combo.
		if( theBtnState.layerHeld &&
			sState.layers[theBtnState.layerHeld].ownedButtonHit )
		{
			theBtnState.usedInButtonCombo = true;
		}

		if( wasDown && theBtnState.heldTime < (0xFFFF - gAppFrameTime) )
			theBtnState.heldTime += gAppFrameTime;
		if( theBtnState.heldTime >= kConfig.shortHoldTime &&
			!theBtnState.shortHoldDone )
		{
			processButtonShortHold(theBtnState);
		}
		if( theBtnState.heldTime >= kConfig.longHoldTime &&
			!theBtnState.longHoldDone )
		{
			processButtonLongHold(theBtnState);
		}
	}
}


static void releaseLayerHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.layerHeld )
	{
		// Flag if used in a button combo, like L2 in L2+X
		if( sState.layers[theBtnState.layerHeld].ownedButtonHit )
			theBtnState.usedInButtonCombo = true;
		removeControlsLayer(theBtnState.layerHeld);
		theBtnState.layerHeld = 0;
	}
}


static void holdLayerByButton(ButtonState& theBtnState, u16 theLayerID)
{
	releaseLayerHeldByButton(theBtnState);
	addControlsLayer(theLayerID);
	theBtnState.layerHeld = theLayerID;
}


static bool tryAddLayerFromButton(
	ButtonState& theBtnState, bool isDown, bool wasHit)
{
	// A hit and quick release again doesn't matter for holding layers active,
	// so if the button isn't down right now then just release held layer.
	if( !isDown )
	{
		releaseLayerHeldByButton(theBtnState);
		return false;
	}

	// Only concerned with buttons that have _HoldControlsLayer for _Down
	if( !theBtnState.commands )
		return false;
	const Command& aDownCmd = theBtnState.commands[eBtnAct_Down];
	if( aDownCmd.type != eCmdType_HoldControlsLayer )
		return false;

	// Only concerned with layers that aren't already active
	const u16 aLayerID = aDownCmd.data;
	if( sState.layers[aLayerID].active )
		return false;

	// If newly hit, replace any past layer with new layer
	if( wasHit )
	{
		holdLayerByButton(theBtnState, aLayerID);
		return true;
	}

	// Generally if the button is just being held down and wasn't newly hit,
	// nothing should change and we're done, but there is a special exception.
	// Say want different actions for X, L2+X, R2+X, and L2+R2+X. Could use
	// a Profile configuration such as (; = new line in actual .ini file):
	// [Layer.A] L2 = Layer B; R2 = Layer C; X = A
	// [Layer.B] R2 = Layer D; X = B
	// [Layer.C] L2 = Layer D; X = C
	// [Layer.D] X = D
	// This will make 'X' press A, L2+X=B, R2+X=C, and L2+R2+X=D. However, if
	// hold L2 (layer B), then hold R2 (layer D), then release L2 but continue
	// to hold R2, then press X, would get 'D' even though L2 is no longer held,
	// and pressing L2 again would change it to now be 'B' instead! User would
	// likely expect that letting go of L2 would instead revert to layer C,
	// then back to D again if press L2 again without ever releasing R2 at all.
	// The below code is exclusively here to handle this specific execption.
	// This altered behaviour should only occur in cases where the button sets
	// a layer in both configurations and isn't doing anything else via the
	// old layer's Auto button or having other commands besides holding layers.
	if( theBtnState.layerHeld == 0 )
		return false;

	const Command* aCmdSet = theBtnState.commandsWhenPressed;
	if( aCmdSet != null )
	{
		for( size_t i = 0; i < eBtnAct_Num; ++i)
		{
			if( i == eBtnAct_Down )
				continue;
			if( aCmdSet[i].type != eCmdType_Empty )
				return false;
		}
	}

	aCmdSet = sState.layers[aLayerID].autoButton.commands;
	if( aCmdSet != null )
	{
		for( size_t i = 0; i < eBtnAct_Num; ++i)
		{
			if( aCmdSet[i].type != eCmdType_Empty &&
				aCmdSet[i].type != eCmdType_HoldControlsLayer &&
				aCmdSet[i].type != eCmdType_AddControlsLayer &&
				aCmdSet[i].type != eCmdType_RemoveControlsLayer )
				return false;
		}
	}

	// Should be safe to directly transition from prior layer held to new one
	holdLayerByButton(theBtnState, aLayerID);

	return true;
}


static void processLayerHoldButtons()
{
	int aLoopCount = 0;
	bool aLayerWasAdded = true;
	while(aLayerWasAdded)
	{
		aLayerWasAdded = false;
		for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
		{
			aLayerWasAdded = aLayerWasAdded ||
				tryAddLayerFromButton(
					sState.gamepadButtons[i],
					Gamepad::buttonDown(EButton(i)),
					Gamepad::buttonHit(EButton(i)));
		}
		if( !aLayerWasAdded )
		{
			for(size_t i = 0; i < sState.layers.size(); ++i)
			{
				LayerState& aLayer = sState.layers[i];
				aLayerWasAdded = aLayerWasAdded ||
					tryAddLayerFromButton(
						aLayer.autoButton,
						aLayer.active,
						aLayer.newlyActive);
			}
		}

		// Update button commands for newly added layers
		// (removed layers will be handled later in main update)
		if( aLayerWasAdded )
		{
			loadButtonCommandsForCurrentLayers();
			if( ++aLoopCount > kMaxLayerChangesPerUpdate )
			{
				logFatalError(
					"Infinite loop of Controls Layer changes detected!");
				break;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
	loadLayerData();
	addControlsLayer(0);
	loadButtonCommandsForCurrentLayers();
}


void cleanup()
{
	for(size_t i = 0; i < ARRAYSIZE(sState.gamepadButtons); ++i)
		releaseKeyHeldByButton(sState.gamepadButtons[i]);
	for(size_t i = 0; i < sState.layers.size(); ++i)
		releaseKeyHeldByButton(sState.layers[i].autoButton);
	sState.clear();
	sResults.clear();
	Menus::cleanup();
}


void update()
{
	// Buttons that 'hold' layers need to be checked first, in order to make
	// sure that combinations enabled through layers like 'L2+X' work when
	// both buttons are pressed at exactly the same time.
	processLayerHoldButtons();

	// Treat each layer as also being a virtual button ("Auto" button)
	for(size_t i = 0; i < sState.layers.size(); ++i)
	{
		LayerState& aLayer = sState.layers[i];
		processButtonState(
			aLayer.autoButton,
			aLayer.active,
			aLayer.newlyActive);
		aLayer.newlyActive = false;
	}

	// Process state changes of actual physical Gamepad buttons
	for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
	{
		processButtonState(
			sState.gamepadButtons[i],
			Gamepad::buttonDown(EButton(i)),
			Gamepad::buttonHit(EButton(i)),
			Gamepad::buttonAnalogVal(EButton(i)));
	}

	// Process any virtual "auto" buttons for layers just added by above
	for(size_t i = 0; i < sState.layers.size(); ++i)
	{
		LayerState& aLayer = sState.layers[i];
		if( aLayer.newlyActive )
		{
			processButtonState(
				aLayer.autoButton,
				aLayer.active,
				aLayer.newlyActive);
			aLayer.newlyActive = false;
		}
	}

	// Update button commands, mouselook mode, HUD, etc for new layer order
	if( sResults.layerChangeMade )
	{
		loadButtonCommandsForCurrentLayers();
		sState.mouseLookOn = false;
		gVisibleHUD.reset();
		for(u16 i = 0; i < sState.layerOrder.size(); ++i)
		{
			sState.mouseLookOn = sState.mouseLookOn ||
				InputMap::mouseLookShouldBeOn(sState.layerOrder[i]);
			gVisibleHUD |=
				InputMap::hudElementsToShow(sState.layerOrder[i]);
			gVisibleHUD &=
				~InputMap::hudElementsToHide(sState.layerOrder[i]);
		}
	}

	// Update mouselook status continuously
	InputDispatcher::setMouseLookMode(sState.mouseLookOn);
	if( sState.mouseLookOn )
		sState.mouseLookTime += gAppFrameTime;
	else
		sState.mouseLookTime = 0;

	// Send input that was queued up by any of the above
	InputDispatcher::moveCharacter(
		sResults.charMove,
		sResults.charTurn,
		sResults.charStrafe);
	InputDispatcher::moveMouse(
		sResults.mouseMoveX,
		sResults.mouseMoveY,
		sResults.mouseMoveDigital);
	InputDispatcher::scrollMouseWheel(
		sResults.mouseWheelY,
		sResults.mouseWheelDigital,
		sResults.mouseWheelStepped);
	for(size_t i = 0; i < sResults.keys.size(); ++i)
		InputDispatcher::sendKeyCommand(sResults.keys[i]);
	for(size_t i = 0; i < sResults.strings.size(); ++i)
		InputDispatcher::sendKeyCommand(sResults.strings[i]);

	// Clear results for next update
	sResults.clear();
}

} // InputTranslator
