//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "OverlayWindow.h" // temp hack to force redraw()
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
kMinMouseLookTimeForAltMove = 50,
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
	u16 heldTime;
	u16 vKeyHeld;
	u16 layerHeld;
	bool heldDown;
	bool shortHoldDone;
	bool longHoldDone;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		ZeroMemory(this, sizeof(ButtonState));
	}

	ButtonState() : vKeyHeld(), layerHeld() { clear(); }
};

struct LayerData
{
	u16 layerID;
	u16 parentLayerID;
	ButtonState autoButton;

	LayerData() : layerID(), parentLayerID() {}
};

struct TranslatorState
{
	ButtonState gamepadButtons[eBtn_Num];
	std::vector<LayerData> activeLayers;
	BitVector<64> buttonFromLayerWasHit;
	int mouseLookTime;

	void clear()
	{
		for(size_t i = 0; i < ARRAYSIZE(gamepadButtons); ++i)
			gamepadButtons[i].clear();
		for(size_t i = 0; i < activeLayers.size(); ++i)
			activeLayers[i].autoButton.clear();
		activeLayers.clear();
		mouseLookTime = 0;
		buttonFromLayerWasHit.reset();
	}
};

struct InputResults
{
	std::vector< std::pair<u16, u16> > layersToAdd;
	std::vector<u16> layersToRemove;
	std::vector<Command> keys;
	std::vector<Command> strings;
	s16 mouseMoveX;
	s16 mouseMoveY;
	s16 mouseWheelY;
	bool mouseMoveDigital;
	bool mouseWheelDigital;
	bool mouseWheelStepped;

	void clear()
	{
		layersToAdd.clear();
		layersToRemove.clear();
		keys.clear();
		strings.clear();
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		mouseWheelStepped = false;
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

static void	loadButtonCommandsForCurrentLayers()
{
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx) // skip eBtn_None
	{
		// Start with the front-most layer, and stop once get a non-null
		for(std::vector<LayerData>::const_reverse_iterator itr =
			sState.activeLayers.rbegin();
			itr != sState.activeLayers.rend(); ++itr)
		{
			sState.gamepadButtons[aBtnIdx].commands =
				InputMap::commandsForButton(itr->layerID, EButton(aBtnIdx));
			if( sState.gamepadButtons[aBtnIdx].commands )
			{
				sState.gamepadButtons[aBtnIdx].commandsLayer = itr->layerID;
				break;
			}
		}
	}
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


static void releaseLayerHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.layerHeld )
	{
		sResults.layersToRemove.push_back(theBtnState.layerHeld);
		theBtnState.layerHeld = 0;
	}
}


static void releaseAllHeldByButton(ButtonState& theBtnState)
{
	releaseKeyHeldByButton(theBtnState);
	releaseLayerHeldByButton(theBtnState);
}


static void processCommand(ButtonState& theBtnState, const Command& theCmd)
{
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
	case eCmdType_VKeySequence:
		// Queue to send after press/release commands but before strings
		sResults.keys.push_back(theCmd);
		break;
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		// Queue to send last, since can block other input the longest
		sResults.strings.push_back(theCmd);
		break;
	case eCmdType_HoldControlsLayer:
		if( theBtnState.layerHeld )
			releaseLayerHeldByButton(theBtnState);
		theBtnState.layerHeld = theCmd.data;
		// fall through
	case eCmdType_AddControlsLayer:
		sResults.layersToAdd.push_back(
			std::make_pair(theCmd.data, theCmd.data2));
		break;
	case eCmdType_RemoveControlsLayer:
		DBG_ASSERT(theCmd.data != 0);
		sResults.layersToRemove.push_back(theCmd.data);
		break;
	case eCmdType_ChangeMacroSet:
		gMacroSetID = theCmd.data;
		// TODO: Stop using this temp hack
		OverlayWindow::redraw();
		break;
	case eCmdType_UseAbility:
	case eCmdType_ConfirmMenu:
	case eCmdType_CancelMenu:
	case eCmdType_SelectAbility:
	case eCmdType_SelectMenu:
	case eCmdType_SelectHotspot:
		// TODO
		break;
	case eCmdType_SelectMacro:
		processCommand(theBtnState, InputMap::commandForMacro(
			gMacroSetID, theCmd.data));
		break;
	case eCmdType_RewriteMacro:
		// TODO
		break;
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
	// the previously, which could be important especially for cases
	// like _Release matching up with a particular _Press.
	theBtnState.commandsWhenPressed = theBtnState.commands;

	// Log that at least one button in the assigned layer has been pressed
	sState.buttonFromLayerWasHit.set(theBtnState.commandsLayer);

	if( !theBtnState.commands )
		return;

	// Get default command for pressing this button (_Down action)
	const Command& aCmd = theBtnState.commands[eBtnAct_Down];

	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
	case eCmdType_SmoothMouseWheel:
	case eCmdType_StepMouseWheel:
		// Handled in processContinuousInput instead
		return;
	}

	processCommand(theBtnState, aCmd);
	processCommand(theBtnState, theBtnState.commands[eBtnAct_Press]);
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
		// Translate into key held/release commands
		if( isDigitalDown )
		{
			aCmd.type = eCmdType_PressAndHoldKey;
			const ECommandDir aDir = ECommandDir(aCmd.data);
			// Use mouse look movement controls (i.e. left-click for forward)
			// only if were already in mouse look mode before pressed the
			// button, or have been in mouselook mode a minimum time
			// (instant switching when start the mode while already holding
			// the button may not register with target application).
			if( theBtnState.heldTime < sState.mouseLookTime ||
				sState.mouseLookTime > kMinMouseLookTimeForAltMove )
			{
				if( aCmd.type == eCmdType_MoveStrafe )
					aCmd.data = InputMap::keyForMouseLookMoveStrafe(aDir);
				else
					aCmd.data = InputMap::keyForMouseLookMoveTurn(aDir);
			}
			else
			{
				if( aCmd.type == eCmdType_MoveStrafe )
					aCmd.data = InputMap::keyForMoveStrafe(aDir);
				else
					aCmd.data = InputMap::keyForMoveTurn(aDir);
			}
			if( aCmd.data != theBtnState.vKeyHeld )
				processCommand(theBtnState, aCmd);
		}
		else
		{
			releaseKeyHeldByButton(theBtnState);
		}
		break;
	case eCmdType_MoveMouse:
	case eCmdType_SmoothMouseWheel:
	case eCmdType_StepMouseWheel:
		// Continue to analog checks below
		break;
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

	if( aCmd.type == eCmdType_MoveMouse )
	{
		switch(aCmd.data)
		{
		case eCmdDir_Left:
			sResults.mouseMoveX -= theAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		case eCmdDir_Right:
			sResults.mouseMoveX += theAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		case eCmdDir_Up:
			sResults.mouseMoveY -= theAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseMoveY += theAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		default:
			DBG_ASSERT(false && "Invalid dir for eCmdType_MoveMouse");
			break;
		}
	}
	else if( aCmd.type == eCmdType_SmoothMouseWheel )
	{
		switch(aCmd.data)
		{
		case eCmdDir_Up:
			sResults.mouseWheelY -= theAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseWheelY += theAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		default:
			DBG_ASSERT(false && "Mouse wheel only supports up and down dir");
			break;
		}
	}
	else if( aCmd.type == eCmdType_StepMouseWheel )
	{
		switch(aCmd.data)
		{
		case eCmdDir_Up:
			sResults.mouseWheelStepped = true;
			sResults.mouseWheelY -= theAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseWheelStepped = true;
			sResults.mouseWheelY += theAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		default:
			DBG_ASSERT(false && "Mouse wheel only supports up and down dir");
			break;
		}
	}
}


static void processButtonShortHold(ButtonState& theBtnState)
{
	theBtnState.shortHoldDone = true;

	// Skip if this button is holding a layer active and one of the other
	// buttons from the layer has been pressed.
	if( theBtnState.layerHeld > 0 &&
		sState.buttonFromLayerWasHit.test(theBtnState.layerHeld) )
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

	// Skip if this button is holding a layer active and one of the other
	// buttons from the layer has been pressed.
	if( theBtnState.layerHeld > 0 &&
		sState.buttonFromLayerWasHit.test(theBtnState.layerHeld) )
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
	// If this button is holding a layer active and one of the other
	// buttons from the layer has been pressed, then do not perform
	// any other actions for this button short of releasing the held
	// layer (or any keys being held).
	const bool skipReleaseCommands =
		theBtnState.layerHeld > 0 &&
		sState.buttonFromLayerWasHit.test(theBtnState.layerHeld);

	// Release anything being held by this button
	releaseAllHeldByButton(theBtnState);

	// If released quickly enough, process 'tap' event
	if( !skipReleaseCommands && theBtnState.heldTime < kConfig.shortHoldTime )
		processButtonTap(theBtnState);

	// Use commandsWhenPressed if it has a _Release action.
	// If not, can use the _Release action for current setup instead,
	// but ONLY if current commands do NOT have an associated _Press
	// action along with the _Release. This exception is to handle a
	// case where a layer was added that intends to be removed by
	// releasing a held button, but for some reason didn't use the
	// standard _HoldControlsLayer method that does it automatically.
	Command aCmd;
	if( !skipReleaseCommands && theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Release];
	if( !skipReleaseCommands &&
		aCmd.type == eCmdType_Empty &&
		theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		theBtnState.commands[eBtnAct_Press].type == eCmdType_Empty )
	{
		aCmd = theBtnState.commands[eBtnAct_Release];
	}
	processCommand(theBtnState, aCmd);

	// At this point nothing should be held by this button,
	// but maybe something weird happened with release/tap commands,
	// so check again to make sure everything is released.
	releaseAllHeldByButton(theBtnState);

	// Reset certain button states when button is released
	theBtnState.heldTime = 0;
	theBtnState.commandsWhenPressed = NULL;
	theBtnState.heldDown = false;
	theBtnState.shortHoldDone = false;
	theBtnState.longHoldDone = false;
	DBG_ASSERT(theBtnState.vKeyHeld == 0);
	DBG_ASSERT(theBtnState.layerHeld == 0);
}


static void processButtonState(
	ButtonState& theBtnState, bool isDown,
	bool wasHit = false, u8 theAnalogVal = 0)
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


static void removeControlsLayer(u16 theLayerID)
{
	if( theLayerID == 0 )
		return;

	// Remove given layer ID and any other layers it spawned
	// (reverse iteration so children likely to be removed first)
	for(std::vector<LayerData>::reverse_iterator itr =
		sState.activeLayers.rbegin(), next_itr = itr;
		itr != sState.activeLayers.rend(); itr = next_itr)
	{
		++next_itr;
		if( itr->parentLayerID == theLayerID )
		{
			// Need to use recursion so also remove grandchildren, etc
			removeControlsLayer(itr->layerID);
			// Recursion will mess up our iterator beyond reasonable recovery,
			// so just start at the beginning (end) again
			next_itr = sState.activeLayers.rbegin();
		}	
		else if( itr->layerID == theLayerID )
		{
			transDebugPrint("Removing Controls Layer: %s\n",
				InputMap::layerLabel(itr->layerID).c_str());

			// Release the special "Auto" button (eBtn_None) for this layer
			processButtonState(itr->autoButton, false);

			// Erase the layer and recover iterator to continue
			next_itr = std::vector<LayerData>::reverse_iterator(
				sState.activeLayers.erase((itr+1).base()));
		}
	}
}


static void addControlsLayer(std::pair<u16, u16> theLayerToAdd)
{
	const u16 aNewLayerID = theLayerToAdd.first;
	const u16 aParentLayerID = theLayerToAdd.second;

	while(sState.buttonFromLayerWasHit.size() < aNewLayerID+1)
		sState.buttonFromLayerWasHit.push_back(false);
	sState.buttonFromLayerWasHit.reset(aNewLayerID);

	// Remove the layer first if it is already active
	removeControlsLayer(aNewLayerID);

	transDebugPrint("Adding Controls Layer: %s\n",
		InputMap::layerLabel(aNewLayerID).c_str());

	sState.activeLayers.push_back(LayerData());
	sState.activeLayers.back().layerID = aNewLayerID;
	sState.activeLayers.back().parentLayerID = aParentLayerID;

	// Assign and "press" the special "Auto" button for this layer
	// Note that these leave .commandsLayer as default 0 value,
	// to prevent them from setting buttonFromLayerWasHit flags,
	// which should only be set by actual gamepad button presses.
	sState.activeLayers.back().autoButton.commands =
		InputMap::commandsForButton(aNewLayerID, eBtn_None);
	processButtonState(sState.activeLayers.back().autoButton, true, true);
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
	addControlsLayer(std::make_pair(0, 0));
	loadButtonCommandsForCurrentLayers();
}


void cleanup()
{
	for(size_t i = 0; i < ARRAYSIZE(sState.gamepadButtons); ++i)
		releaseAllHeldByButton(sState.gamepadButtons[i]);
	for(size_t i = 0; i < sState.activeLayers.size(); ++i)
		releaseAllHeldByButton(sState.activeLayers[i].autoButton);
	sState.clear();
	sResults.clear();
	gMacroSetID = 0;
}


void update()
{
	// Every active layer's "auto" button is always considered "held down"
	for(size_t i = 0; i < sState.activeLayers.size(); ++i)
		processButtonState(sState.activeLayers[i].autoButton, true);

	// Process state changes of actual physical Gamepad buttons
	for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
	{
		processButtonState(
			sState.gamepadButtons[i],
			Gamepad::buttonDown(EButton(i)),
			Gamepad::buttonHit(EButton(i)),
			Gamepad::buttonAnalogVal(EButton(i)));
	}

	// Remove layers to be removed (and "release" their Auto buttons)
	for(size_t i = 0; i < sResults.layersToRemove.size(); ++i)
		removeControlsLayer(sResults.layersToRemove[i]);

	// Add newly requested layers
	for(size_t i = 0; i < sResults.layersToAdd.size(); ++i)
		addControlsLayer(sResults.layersToAdd[i]);

	// Update button commands to match layer changes
	if( !sResults.layersToRemove.empty() || !sResults.layersToAdd.empty() )
		loadButtonCommandsForCurrentLayers();

	// Update mouselook mode, HUD, etc for current (new) layer configuration
	{
		bool wantMouseLook = false;
		BitArray<eHUDElement_Num> wantedHUDElements;
		wantedHUDElements.reset();
		for(u16 i = 0; i < sState.activeLayers.size(); ++i)
		{
			wantMouseLook = wantMouseLook ||
				InputMap::mouseLookShouldBeOn(sState.activeLayers[i].layerID);
			wantedHUDElements |=
				InputMap::hudElementsToShow(sState.activeLayers[i].layerID);
			wantedHUDElements &=
				~InputMap::hudElementsToHide(sState.activeLayers[i].layerID);
		}
		InputDispatcher::setMouseLookMode(wantMouseLook);
		if( wantMouseLook )
			sState.mouseLookTime += gAppFrameTime;
		else
			sState.mouseLookTime = 0;
		if( gVisibleHUD != wantedHUDElements )
		{
			gVisibleHUD = wantedHUDElements;
			// TODO: Stop using this temp hack
			OverlayWindow::redraw();
		}
	}

	// Send input that was queued up by any of the above
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
