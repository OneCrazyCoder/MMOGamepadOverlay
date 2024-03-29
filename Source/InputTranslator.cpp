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
	u16 autoRepeatDelay;
	u16 autoRepeatRate;

	void load()
	{
		shortHoldTime = Profile::getInt("System/ButtonShortHoldTime", 400);
		longHoldTime = Profile::getInt("System/ButtonLongHoldTime", 800);
		autoRepeatDelay = Profile::getInt("System/AutoRepeatDelay", 400);
		autoRepeatRate = Profile::getInt("System/AutoRepeatRate", 100);
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
	u16 layerWhenPressed;
	u16 layerHeld;
	u16 heldTime;
	s16 repeatDelay;
	u16 vKeyHeld;
	bool isAutoButton;
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
		const Command* oldCommands = this->commands;
		const u16 oldParentLayer = this->commandsLayer;
		const u16 oldChildLayer = this->layerHeld;
		const bool oldIsAutoButton = this->isAutoButton;
		clear();
		this->commands = oldCommands;
		this->commandsLayer = oldParentLayer;
		this->layerHeld = oldChildLayer;
		this->isAutoButton = oldIsAutoButton;
	}

	void swapHeldState(ButtonState& rhs)
	{
		std::swap(layerHeld, rhs.layerHeld);
		std::swap(heldTime, rhs.heldTime);
		std::swap(repeatDelay, rhs.repeatDelay);
		std::swap(vKeyHeld, rhs.vKeyHeld);
		std::swap(heldDown, rhs.heldDown);
		std::swap(shortHoldDone, rhs.shortHoldDone);
		std::swap(longHoldDone, rhs.longHoldDone);
		std::swap(usedInButtonCombo, rhs.usedInButtonCombo);
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
	bool heldActiveByButton;

	void clear()
	{
		autoButton.clear();
		autoButton.isAutoButton = true;
		parentLayerID = 0;
		active = false;
		newlyActive = false;
		ownedButtonHit = false;
		heldActiveByButton = false;
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
		sState.layers[i].autoButton.commandsLayer = i;
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
	if( !sState.layers[theLayerID].active )
		return;

	// Layer ID 0 can't be removed, but trying to removes everything else
	if( theLayerID == 0 )
		transDebugPrint(
			"Removing ALL Layers (besides [Scheme] & held by button)!\n");

	// Remove given layer ID and any "child" layers it spawned via 'Add'.
	// Child layers that are being held active by a held button are NOT
	// automatically removed along with their parent layer though!
	// Use reverse iteration so children are likely to be removed first.
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin(), next_itr = itr;
		itr != sState.layerOrder.rend(); itr = next_itr)
	{
		++next_itr;
		if( *itr == 0 )
			continue;
		LayerState& aLayer = sState.layers[*itr];
		if( *itr == theLayerID )
		{
			transDebugPrint("Removing Controls Layer: %s\n",
				InputMap::layerLabel(*itr).c_str());

			// Reset some layer properties
			aLayer.active = false;
			aLayer.ownedButtonHit = false;
			aLayer.newlyActive = false;
			aLayer.heldActiveByButton = false;
			sResults.layerChangeMade = true;

			// Remove from the layer order and recover iterator to continue
			next_itr = std::vector<u16>::reverse_iterator(
				sState.layerOrder.erase((itr+1).base()));
		}
		else if( aLayer.parentLayerID == theLayerID &&
				 !aLayer.heldActiveByButton &&
				 sState.layers[theLayerID].active )
		{
			// Need to use recursion so also remove grandchildren, etc
			DBG_ASSERT(sState.layers[*itr].active);
			removeControlsLayer(*itr);
			// Recursion will mess up our iterator beyond reasonable recovery,
			// so just start at the beginning (end) again
			next_itr = sState.layerOrder.rbegin();
		}
	}
}


static bool layerIsParentOfLayer(u16 theParentLayerID, u16 theCheckLayerID)
{
	std::vector<u16> aCheckedIDVec;
	aCheckedIDVec.reserve(4);
	while(
		std::find(aCheckedIDVec.begin(), aCheckedIDVec.end(),
			theCheckLayerID) == aCheckedIDVec.end() )
	{
		if( theCheckLayerID == theParentLayerID )
			return true;
		aCheckedIDVec.push_back(theCheckLayerID);
		theCheckLayerID = sState.layers[theCheckLayerID].parentLayerID;
	}
	return false;
}


static std::vector<u16>::iterator layerOrderInsertPos(u16 theParentLayerID)
{
	// New layers typically should be at the "top" (back() of the vector).
	// However, layers being held active by buttons should stay on top of
	// layers added by Add/Replace/Toggle (except for their own "children").
	std::vector<u16>::iterator result = sState.layerOrder.begin();

	// First, work backwards until find theParentLayerID's position in the
	// order to make sure insert after it.
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin();
		itr != sState.layerOrder.rend(); ++itr)
	{
		result = itr.base();
		if( *itr == theParentLayerID )
			break;
	}
	// Now move forward until run into top (back()) or hit a held layer,
	// in which case will insert below it.
	while( result != sState.layerOrder.end() &&
		   !sState.layers[*result].heldActiveByButton )
	{
		++result;
	}

	return result;
}


static void addControlsLayer(u16 theLayerID, u16 theSpawningLayerID)
{
	DBG_ASSERT(theLayerID < sState.layers.size());
	DBG_ASSERT(theSpawningLayerID < sState.layers.size());

	// Calculate new position in the layer order.
	
	// Check to see if layer is already active
	if( sState.layers[theLayerID].active )
	{
		// If request came from a child layer of the layer being added,
		// meaning could end up with a recursive child/parent relationship,
		// simply bump this layer to new position in layer order instead,
		// without adding/removing anything or changing parent relationships.
		if( layerIsParentOfLayer(theLayerID, theSpawningLayerID) )
		{
			// Remove from layer order
			sState.layerOrder.erase(std::find(
				sState.layerOrder.begin(),
				sState.layerOrder.end(),
				theLayerID));
			// Re-insert into layer order
			sState.layerOrder.insert(
				layerOrderInsertPos(theSpawningLayerID), theLayerID);
			transDebugPrint("Moving Controls Layer '%s' to top\n",
				InputMap::layerLabel(theLayerID).c_str());
			sResults.layerChangeMade = true;
			return;
		}

		// Otherwise, remove layer first then re-add it
		removeControlsLayer(theLayerID);
		DBG_ASSERT(!sState.layers[theLayerID].active);
	}

	if( theLayerID > 0 )
	{
		if( theSpawningLayerID > 0 )
		{
			transDebugPrint(
				"Adding Controls Layer '%s' as child of Layer '%s'\n",
				InputMap::layerLabel(theLayerID).c_str(),
				InputMap::layerLabel(theSpawningLayerID).c_str());
		}
		else
		{
			transDebugPrint(
				"Adding Controls Layer '%s'\n",
				InputMap::layerLabel(theLayerID).c_str());
		}
	}

	sState.layerOrder.insert(
		layerOrderInsertPos(theSpawningLayerID), theLayerID);
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
		aCmd.vKey = theBtnState.vKeyHeld;
		InputDispatcher::sendKeyCommand(aCmd);
		theBtnState.vKeyHeld = 0;
	}
}


static void processCommand(
	ButtonState& theBtnState,
	const Command& theCmd,
	u16 theLayerIdx,
	bool isAutoRepeated = false)
{
	Command aForwardCmd;
	switch(theCmd.type)
	{
	case eCmdType_Empty:
	case eCmdType_DoNothing:
		// Do nothing
		break;
	case eCmdType_PressAndHoldKey:
		// Release any previously-held key first!
		releaseKeyHeldByButton(theBtnState);
		// Send this right away since can often be instantly processed
		// instead of needing to wait for input queue
		InputDispatcher::sendKeyCommand(theCmd);
		// Make note that this button is holding this key
		theBtnState.vKeyHeld = theCmd.vKey;
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
	case eCmdType_ChangeProfile:
		Profile::queryUserForProfile();
		gReloadProfile = true;
		break;
	case eCmdType_QuitApp:
		gShutdown = true;
		break;
	case eCmdType_AddControlsLayer:
		// relativeLayer is how many parent layers up from current to attach to
		for(int i = 0; i < theCmd.relativeLayer && theLayerIdx != 0; ++i)
			theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
		addControlsLayer(theCmd.layerID, theLayerIdx);
		break;
	case eCmdType_RemoveControlsLayer:
		if( theCmd.layerID == 0 )
		{// 0 means to remove relative rather than direct layer ID
			if( theCmd.relativeLayer == kAllLayers )
			{
				removeControlsLayer(0);
			}
			else
			{
				// relativeLayer means how many parent layers up from calling
				for(int i = 0; i < theCmd.relativeLayer &&
					sState.layers[theLayerIdx].parentLayerID != 0; ++i)
				{
					theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				}
				removeControlsLayer(theLayerIdx);
			}
		}
		else
		{// Otherwise remove a specific layer ID specified
			removeControlsLayer(theCmd.layerID);
		}
		break;
	case eCmdType_HoldControlsLayer:
		// Special-case, handled elsewhere
		break;
	case eCmdType_ReplaceControlsLayer:
		// Replace can't name the layer to be replaced, only the one
		// to replace it with, so it uses relative layer for removal
		// in the same manner as _Remove does when .layerID == 0
		if( theCmd.relativeLayer == kAllLayers )
		{
			theLayerIdx = 0;
			removeControlsLayer(0);
		}
		else
		{
			// relativeLayer means how many parent layers up from calling
			u16 aParentIdx = sState.layers[theLayerIdx].parentLayerID;
			for(int i = 0; i < theCmd.relativeLayer && aParentIdx != 0; ++i)
			{
				theLayerIdx = aParentIdx;
				DBG_ASSERT(theLayerIdx < sState.layers.size());
				aParentIdx = sState.layers[theLayerIdx].parentLayerID;
			}
			removeControlsLayer(theLayerIdx);
			theLayerIdx = aParentIdx;
		}
		addControlsLayer(theCmd.layerID, theLayerIdx);
		break;
	case eCmdType_ToggleControlsLayer:
		aForwardCmd = theCmd;
		DBG_ASSERT(theCmd.layerID != 0);
		DBG_ASSERT(theCmd.layerID < sState.layers.size());
		aForwardCmd.type = sState.layers[theCmd.layerID].active
			? eCmdType_RemoveControlsLayer
			: eCmdType_AddControlsLayer;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_OpenSubMenu:
		Menus::openSubMenu(theCmd.menuID, theCmd.subMenuID);
		break;
	case eCmdType_ReplaceMenu:
		Menus::replaceMenu(theCmd.menuID, theCmd.subMenuID);
		break;
	case eCmdType_MenuReset:
		Menus::reset(theCmd.menuID);
		break;
	case eCmdType_MenuConfirm:
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.menuID);
		if( aForwardCmd.type != eCmdType_Empty )
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuConfirmAndClose:
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.menuID);
		if( aForwardCmd.type != eCmdType_Empty )
		{
			// Close menu first if this won't just switch to a sub-menu
			if( aForwardCmd.type != eCmdType_Empty &&
				aForwardCmd.type < eCmdType_FirstMenuControl &&
				theLayerIdx > 0 )
			{
				// Set theLayerIdx to parent layer first,
				// since this layer will be invalid for aForwardCmd
				const u16 aLayerToRemoveID = theLayerIdx;
				theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				// Assume removing calling layer "closes" the menu
				removeControlsLayer(aLayerToRemoveID);
			}
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		break;
	case eCmdType_MenuBack:
		Menus::closeLastSubMenu(theCmd.menuID);
		break;
	case eCmdType_MenuBackOrClose:
		// If at root, assume removing calling layer "closes" the menu
		if( !Menus::closeLastSubMenu(theCmd.menuID) )
			removeControlsLayer(theLayerIdx);
		break;
	case eCmdType_MenuEdit:
		Menus::editMenuItem(theCmd.menuID);
		break;
	case eCmdType_TargetGroupResetLast:
		gLastGroupTarget = gDefaultGroupTarget;
		transDebugPrint("Resetting 'last' Group Member to default #%d\n",
			gDefaultGroupTarget);
		break;
	case eCmdType_SetTargetGroupDefault:
		gDefaultGroupTarget = gLastGroupTarget;
		gDefaultGroupTargetUpdated = true;
		transDebugPrint("Setting Group Member #%d as default\n",
			gLastGroupTarget);
		break;
	case eCmdType_TargetGroupPrev:
		gLastGroupTargetUpdated = true;
		if( theCmd.wrap )
		{
			gLastGroupTarget =
				decWrap(gLastGroupTarget, InputMap::targetGroupSize());
			transDebugPrint("Targeting group member prev/up (wrap) (#%d)\n",
				gLastGroupTarget);
		}
		else
		{
			gLastGroupTarget =
				gLastGroupTarget == 0
					? 0 : gLastGroupTarget - 1;
			transDebugPrint("Targeting group member prev/up (clamp) (#%d)\n",
				gLastGroupTarget);
		}
		aForwardCmd.type = eCmdType_TapKey;
		aForwardCmd.vKey = InputMap::keyForSpecialAction(
			ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
		sResults.keys.push_back(aForwardCmd);
		break;
	case eCmdType_TargetGroupNext:
		gLastGroupTargetUpdated = true;
		if( theCmd.wrap )
		{
			gLastGroupTarget =
				incWrap(gLastGroupTarget, InputMap::targetGroupSize());
			transDebugPrint("Targeting group member next/down (wrap) (#%d)\n",
				gLastGroupTarget);
		}
		else
		{
			gLastGroupTarget =
				gLastGroupTarget >= InputMap::targetGroupSize() - 1
					? InputMap::targetGroupSize() - 1 : gLastGroupTarget + 1;
			transDebugPrint("Targeting group member next/down (clamp) (#%d)\n",
				gLastGroupTarget);
		}
		aForwardCmd.type = eCmdType_TapKey;
		aForwardCmd.vKey = InputMap::keyForSpecialAction(
			ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
		sResults.keys.push_back(aForwardCmd);
		break;
	case eCmdType_TargetGroupDefault:
		gLastGroupTarget = gDefaultGroupTarget;
		gLastGroupTargetUpdated = true;
		aForwardCmd.type = eCmdType_TapKey;
		aForwardCmd.vKey = InputMap::keyForSpecialAction(
			ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
		sResults.keys.push_back(aForwardCmd);
		transDebugPrint("Targeting default Group Member #%d\n",
			gLastGroupTarget);
		break;
	case eCmdType_TargetGroupPet:
		gLastGroupTargetUpdated = true;
		aForwardCmd.type = eCmdType_TapKey;
		aForwardCmd.vKey = InputMap::keyForSpecialAction(
			ESpecialKey(eSpecialKey_FirstGroupTarget + gLastGroupTarget));
		sResults.keys.push_back(aForwardCmd);
		transDebugPrint("Re-targeting last group member/pet (#%d) \n",
			gLastGroupTarget);
		break;
	case eCmdType_MenuSelect:
		aForwardCmd = Menus::selectMenuItem(
			theCmd.menuID, ECommandDir(theCmd.dir), isAutoRepeated);
		if( aForwardCmd.type != eCmdType_Empty )
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuSelectAndClose:
		aForwardCmd = Menus::selectMenuItem(
			theCmd.menuID, ECommandDir(theCmd.dir), isAutoRepeated);
		if( aForwardCmd.type != eCmdType_Empty )
		{
			// Close menu first if this won't just switch to a sub-menu
			if( aForwardCmd.type != eCmdType_Empty &&
				aForwardCmd.type < eCmdType_FirstMenuControl &&
				theLayerIdx > 0 )
			{
				// Set theLayerIdx to parent layer first,
				// since this layer will be invalid for aForwardCmd
				const u16 aLayerToRemoveID = theLayerIdx;
				theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				// Assume removing calling layer "closes" the menu
				removeControlsLayer(aLayerToRemoveID);
			}
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		break;
	case eCmdType_MenuEditDir:
		Menus::editMenuItemDir(theCmd.menuID, ECommandDir(theCmd.dir));
		break;
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
		if( theCmd.mouseWheelMotionType == eMouseWheelMotion_Once )
		{
			InputDispatcher::scrollMouseWheelOnce(ECommandDir(theCmd.dir));
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
	theBtnState.layerWhenPressed = theBtnState.commandsLayer;

	// Log that at least one button in the assigned layer has been pressed
	// (unless it is just the Auto button for the layer, which doesn't count)
	if( !theBtnState.isAutoButton )
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
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Once )
			return;
		break;
	}

	// _Press is processed before _Down since it is specifically called out
	// and the name implies it should be first action on button press.
	processCommand(theBtnState,
		theBtnState.commands[eBtnAct_Press],
		theBtnState.commandsLayer);
	processCommand(theBtnState, aCmd, theBtnState.commandsLayer);
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
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Once )
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

	switch(u32(aCmd.type << 16) | aCmd.dir)
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
		if( aCmd.mouseWheelMotionType == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY -= theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Down:
		if( aCmd.mouseWheelMotionType == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY += theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	default:
		DBG_ASSERT(false && "Command and direction combo invalid");
		break;
	}
}


static void processAutoRepeat(ButtonState& theBtnState)
{
	if( kConfig.autoRepeatRate == 0 )
		return;

	// Auto-repeat only commandsWhenPressed assigned to _Down
	Command aCmd;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Down];

	// Filter out which commands can use auto-repeat safely
	switch(aCmd.type)
	{
	case eCmdType_MenuSelect:
	case eCmdType_MenuSelectAndClose:
	case eCmdType_HotspotSelect:
	case eCmdType_TargetGroupPrev:
	case eCmdType_TargetGroupNext:
		// Continue to further checks below
		break;
	default:
		// Incompatible with this feature
		return;
	}
	
	// Don't auto-repeat when button has other conflicting actions assigned
	if( theBtnState.commandsWhenPressed[eBtnAct_Tap].type ||
		theBtnState.commandsWhenPressed[eBtnAct_ShortHold].type ||
		theBtnState.commandsWhenPressed[eBtnAct_LongHold].type )
		return;

	// Needs to be held for initial held time first before start repeating
	if( theBtnState.heldTime < kConfig.autoRepeatDelay )
		return;

	// Now can start using repeatDelayr to re-send command at autoRepeatRate
	if( theBtnState.repeatDelay <= 0 )
	{
		processCommand(theBtnState, aCmd, true);
		theBtnState.repeatDelay += kConfig.autoRepeatRate;
	}
	theBtnState.repeatDelay -= gAppFrameTime;
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
			theBtnState.commandsWhenPressed[eBtnAct_ShortHold],
			theBtnState.layerWhenPressed);
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
			theBtnState.commandsWhenPressed[eBtnAct_LongHold],
			theBtnState.layerWhenPressed);
	}
}


static void processButtonTap(ButtonState& theBtnState)
{
	// Only ever use commandsWhenPressed for tap action
	if( theBtnState.commandsWhenPressed )
	{
		processCommand(theBtnState,
			theBtnState.commandsWhenPressed[eBtnAct_Tap],
			theBtnState.layerWhenPressed);
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
	u16 aCmdLayer = theBtnState.layerWhenPressed;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Release];
	if( aCmd.type == eCmdType_Empty &&
		theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		theBtnState.commands[eBtnAct_Press].type == eCmdType_Empty )
	{
		aCmd = theBtnState.commands[eBtnAct_Release];
		aCmdLayer = theBtnState.commandsLayer;
	}
	processCommand(theBtnState, aCmd, aCmdLayer);

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

		// Process auto-repeat feature for certain commands
		processAutoRepeat(theBtnState);
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
	DBG_ASSERT(theLayerID < sState.layers.size());
	DBG_ASSERT(theLayerID > 0);
	releaseLayerHeldByButton(theBtnState);

	// Held layers are always independent (Layer 0 is their parent) and
	// placed on "top" of all other layers when they are added.
	transDebugPrint(
		"Holding Controls Layer '%s'\n",
		InputMap::layerLabel(theLayerID).c_str());
	sState.layerOrder.push_back(theLayerID);
	LayerState& aLayer = sState.layers[theLayerID];
	aLayer.parentLayerID = 0;
	aLayer.active = true;
	aLayer.newlyActive = true;
	aLayer.heldActiveByButton = true;
	sResults.layerChangeMade = true;
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
	// (in current 'commands', not necessarily 'commandsWhenPressed'!)
	if( !theBtnState.commands )
		return false;
	const Command& aDownCmd = theBtnState.commands[eBtnAct_Down];
	if( aDownCmd.type != eCmdType_HoldControlsLayer )
		return false;

	// Only concerned with layers that aren't already active
	const u16 aLayerID = aDownCmd.layerID;
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

	// Say want different actions for X, L2+X, R2+X, and L2+R2+X, so use
	// a Profile configuration such as:
	// [Layer.A]\n X = A\n L2 = Layer B\n R2 = Layer C\n 
	// [Layer.B]\n X = B\n R2 = Layer D\n 
	// [Layer.C]\n X = C\n L2 = Layer D\n 
	// [Layer.D]\n X = D

	// This will make 'X' press A, L2+X=B, R2+X=C, and L2+R2+X=D. However, if
	// hold L2 (layer B), then hold R2 (layer D), then release L2 but continue
	// to hold R2, then press X, would get 'D' even though L2 is no longer
	// held, and pressing L2 again would change X to be 'B' instead! User would
	// likely expect that letting go of L2 would instead revert to layer C,
	// then back to D again if press L2 again without ever releasing R2 at all.

	// The below code is exclusively here to handle this specific execption,
	// by allowing a direct swap of layer being held even when the button has
	// not been newly pressed, in the case where the button's _Down action was
	// set to _HoldControlsLayer both before and after configuration change.

	// If this exception is undesired, can avoid it by manually using Add and
	// Remove Layer on button press & release instead of using Hold layer.
	if( theBtnState.layerHeld &&
		theBtnState.layerHeld != aLayerID &&
		theBtnState.commandsWhenPressed &&
		theBtnState.commandsWhenPressed[eBtnAct_Down].type ==
			eCmdType_HoldControlsLayer )
	{
		// Both new and old layer must have matching Auto button
		// _Down and _Release commands to avoid side effects from the swap.
		ButtonState& pab = sState.layers[theBtnState.layerHeld].autoButton;
		ButtonState& nab = sState.layers[aLayerID].autoButton;
		if( pab.commands == nab.commands ||
			(pab.commands != null && nab.commands != null &&
			 pab.commands[eBtnAct_Down] == nab.commands[eBtnAct_Down] &&
			 pab.commands[eBtnAct_Release] == nab.commands[eBtnAct_Release]) )
		{
			nab.swapHeldState(pab);
			holdLayerByButton(theBtnState, aLayerID);
			sState.layers[aLayerID].newlyActive = false;
		}
		return true;
	}

	return false;
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
		// (removed layers & HUD will be handled later in main update)
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


static void updateHUDStateForCurrentLayers()
{
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

	// Also check to see if any Menus should be shown as disabled because
	// they don't have any commands assigned to them any more.
	// Start by assuming all visible menus are disabled...
	for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
	{
		if( InputMap::hudElementIsAMenu(i) && gVisibleHUD.test(i) )
			gDisabledHUD.set(i);
	}

	// Now re-enable any menus that have a command associated with them
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx)
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		if( aBtnState.commands )
		{
			for(size_t aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				u16 aHUDIdx = 0xFFFF;
				switch(aBtnState.commands[aBtnAct].type)
				{
				case eCmdType_MenuReset:
				case eCmdType_MenuConfirm:
				case eCmdType_MenuConfirmAndClose:
				case eCmdType_MenuBack:
				case eCmdType_MenuBackOrClose:
				case eCmdType_MenuEdit:
				case eCmdType_MenuSelect:
				case eCmdType_MenuSelectAndClose:
				case eCmdType_MenuEditDir:
					aHUDIdx = InputMap::hudElementForMenu(
						aBtnState.commands[aBtnAct].menuID);
					break;
				default:
					continue;
				}
				DBG_ASSERT(aHUDIdx < gDisabledHUD.size());
				gDisabledHUD.reset(aHUDIdx);
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
	addControlsLayer(0, 0);
	loadButtonCommandsForCurrentLayers();
	updateHUDStateForCurrentLayers();
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
		updateHUDStateForCurrentLayers();
		sState.mouseLookOn = false;
		#ifndef NDEBUG
		std::string aNewLayerOrder("Layers: ");
		for(std::vector<u16>::iterator itr =
			sState.layerOrder.begin();
			itr != sState.layerOrder.end(); ++itr)
		{
			aNewLayerOrder += InputMap::layerLabel(*itr);
			if( itr + 1 != sState.layerOrder.end() )
				aNewLayerOrder += " < ";
		}
		aNewLayerOrder += "\n";
		transDebugPrint("%s", aNewLayerOrder.c_str());
		#endif
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
