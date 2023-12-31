//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "OverlayWindow.h" // to set alpha fade in/out
#include "Profile.h"

namespace InputTranslator
{

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

struct ModeState : public ConstructFromZeroInitializedMemory<ModeState>
{
	int mouseLookTime;
};

struct ButtonState : public ConstructFromZeroInitializedMemory<ButtonState>
{
	u16 heldTime;
	u8 vKeyHeld;
	u8 modeWhenPressed;
	bool shortHoldDone;
	bool longHoldDone;
};

struct InputResults
{
	int newMode;
	s16 mouseMoveX;
	s16 mouseMoveY;
	s16 mouseWheelY;
	bool mouseMoveDigital;
	bool mouseWheelDigital;
	bool mouseWheelStepped;
	std::vector<Command> keys;
	std::vector<Command> macros;

	void clear()
	{
		newMode = gControlsModeID;
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		mouseWheelStepped = false;
		keys.clear();
		macros.clear();
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static ModeState sModeState;
static ButtonState sButtonStates[eBtn_Num];
static InputResults sResults;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void releaseKeyHeldByButton(EButton theButton)
{
	if( sButtonStates[theButton].vKeyHeld )
	{
		Command aCmd;
		aCmd.type = eCmdType_ReleaseKey;
		aCmd.data = sButtonStates[theButton].vKeyHeld;
		InputDispatcher::sendKeyCommand(aCmd);
		sButtonStates[theButton].vKeyHeld = 0;
	}
}


static void processCommand(EButton theButton, const Command& theCmd)
{
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		// Do nothing
		break;
	case eCmdType_PressAndHoldKey:
		// Release any previously-held key first!
		releaseKeyHeldByButton(theButton);
		// Send this right away since can often be instantly processed
		// instead of needing to wait for input queue
		InputDispatcher::sendKeyCommand(theCmd);
		// Make note that this button is holding this key
		sButtonStates[theButton].vKeyHeld = theCmd.data;
		break;
	case eCmdType_VKeySequence:
		// Queue to send after press/release commands but before macros
		sResults.keys.push_back(theCmd);
		break;
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		// Queue to send last, since can block other input the longest
		sResults.macros.push_back(theCmd);
		break;
	case eCmdType_ChangeMode:
		sResults.newMode = theCmd.data;
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
		processCommand(theButton, InputMap::commandForMacro(
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


static void processButtonPress(EButton theButton)
{
	// Store mode when pressed to use by other button actions.
	// This ensures that if control scheme is changed before this
	// button is released, the original release event is still used,
	// and don't end up in a situation where a keyboard key gets
	// "stuck" held down forever, nor do we have to immediately force
	// all keys to be released every time a control scheme changes.
	sButtonStates[theButton].modeWhenPressed = gControlsModeID;

	// Get default command for pressing this button (_Down action)
	const Command& aCmd = InputMap::commandForButtonAction(
		gControlsModeID, theButton, eBtnAct_Down);

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

	processCommand(theButton, aCmd);
	processCommand(theButton, InputMap::commandForButtonAction(
		gControlsModeID, theButton, eBtnAct_Press));
}


static void processContinuousInput(EButton theButton, bool isDigitalDown)
{
	// Use modeWhenPressed if it has a continuous action, otherwise current
	// These inputs should always be assigned to the _Down button action
	Command aCmd = InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_Down);
	if( aCmd.type == eCmdType_Empty &&
		gControlsModeID != sButtonStates[theButton].modeWhenPressed )
	{
		aCmd = InputMap::commandForButtonAction(
			gControlsModeID, theButton, eBtnAct_Down);
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
			if( sButtonStates[theButton].heldTime < sModeState.mouseLookTime ||
				sModeState.mouseLookTime > kMinMouseLookTimeForAltMove )
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
			if( aCmd.data != sButtonStates[theButton].vKeyHeld )
				processCommand(theButton, aCmd);
		}
		else
		{
			releaseKeyHeldByButton(theButton);
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

	u8 anAnalogVal = Gamepad::buttonAnalogVal(theButton);
	if( anAnalogVal )
		isDigitalDown = false;
	else if( isDigitalDown )
		anAnalogVal = 255;
	else // if( anAnalogVal == 0 && !isDigitalDown )
		return;

	if( aCmd.type == eCmdType_MoveMouse )
	{
		switch(aCmd.data)
		{
		case eCmdDir_Left:
			sResults.mouseMoveX -= anAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		case eCmdDir_Right:
			sResults.mouseMoveX += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		case eCmdDir_Up:
			sResults.mouseMoveY -= anAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseMoveY += anAnalogVal;
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
			sResults.mouseWheelY -= anAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseWheelY += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
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
			sResults.mouseWheelY -= anAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseWheelStepped = true;
			sResults.mouseWheelY += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
			break;
		default:
			DBG_ASSERT(false && "Mouse wheel only supports up and down dir");
			break;
		}
	}
}


static void processButtonShortHold(EButton theButton)
{
	sButtonStates[theButton].shortHoldDone = true;
	// Only use modeWhenPressed for short hold action
	processCommand(theButton, InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_ShortHold));
}


static void processButtonLongHold(EButton theButton)
{
	sButtonStates[theButton].longHoldDone = true;
	// Only use modeWhenPressed for long hold action
	processCommand(theButton, InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_LongHold));
}


static void processButtonTap(EButton theButton)
{
	// Use modeWhenPressed if it has a tap action,
	// otherwise use current mode
	Command aCmd = InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_Tap);
	if( aCmd.type == eCmdType_Empty &&
		gControlsModeID != sButtonStates[theButton].modeWhenPressed )
	{
		aCmd = InputMap::commandForButtonAction(
			gControlsModeID, theButton, eBtnAct_Tap);
	}
	
	processCommand(theButton, aCmd);
}


static void processButtonReleased(EButton theButton)
{
	// First, release any key being held by this button
	releaseKeyHeldByButton(theButton);

	// If released quickly enough, process 'tap' event
	if( sButtonStates[theButton].heldTime < kConfig.shortHoldTime )
		processButtonTap(theButton);

	// Use modeWhenPressed if it has a normal release action,
	// otherwise use current mode.
	Command aCmd = InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_Release);
	if( aCmd.type == eCmdType_Empty &&
		gControlsModeID != sButtonStates[theButton].modeWhenPressed )
	{
		aCmd = InputMap::commandForButtonAction(
			gControlsModeID, theButton, eBtnAct_Release);
	}

	processCommand(theButton, aCmd);

	// At this point no key should be held by this button - confirm that!
	releaseKeyHeldByButton(theButton);

	// Reset button state now that it is released (sets heldTime to 0)
	sButtonStates[theButton] = ButtonState();
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
}


void cleanup()
{
	sResults.clear();
	sModeState = ModeState();
	for(size_t i = 0; i < ARRAYSIZE(sButtonStates); ++i)
	{
		releaseKeyHeldByButton(EButton(i));
		sButtonStates[i] = ButtonState();
	}
}


void update()
{
	sResults.clear();

	// Set mouse look mode based on current control scheme
	const bool wantMouseLook = InputMap::mouseLookShouldBeOn(gControlsModeID);
	InputDispatcher::setMouseLookMode(wantMouseLook);
	if( !wantMouseLook ) sModeState.mouseLookTime = 0;

	for(EButton aBtn = EButton(1); // skip button 0 (eBtn_None)
		aBtn < eBtn_Num;
		aBtn = EButton(aBtn+1))
	{
		const bool wasDown = sButtonStates[aBtn].heldTime > 0;
		const bool isDown = Gamepad::buttonDown(aBtn);
		const bool wasHit = Gamepad::buttonHit(aBtn);

		// If button was down previously and has either been hit again or
		// isn't down any more, then it must have been released, and we should
		// process that before any new hits since it may use a prior scheme.
		if( wasDown && (!isDown || wasHit) )
			processButtonReleased(aBtn);

		// If was hit (or somehow is down but wasn't before), process a press
		if( wasHit || (isDown && !wasDown) )
		{
			processButtonPress(aBtn);
			// Now that have processed a press, if it's not down any more,
			// it must have already been released, so process that as well.
			if( !isDown )
				processButtonReleased(aBtn);
		}

		// Process continuous input, such as for analog axis values which
		// which do not necessarily return hit/release when lightly pressed
		processContinuousInput(aBtn, isDown);

		// Update heldTime value and see if need to process a long hold
		if( isDown )
		{
			if( sButtonStates[aBtn].heldTime < (0xFFFF - gAppFrameTime) )
				sButtonStates[aBtn].heldTime += gAppFrameTime;
			if( sButtonStates[aBtn].heldTime >= kConfig.shortHoldTime &&
				!sButtonStates[aBtn].shortHoldDone )
			{
				processButtonShortHold(aBtn);
			}
			if( sButtonStates[aBtn].heldTime >= kConfig.longHoldTime &&
				!sButtonStates[aBtn].longHoldDone )
			{
				processButtonLongHold(aBtn);
			}
		}
	}

	if( sResults.newMode != gControlsModeID )
	{// Swap controls modes
		// In Profile .ini file, eBtn_None is referenced as "Auto" and
		// can be assigned like any other button to commands. But it
		// is "pressed" when the mode starts, and "released" when the
		// mode stops. Actions like _ShortHold will never trigger though.
		processButtonReleased(eBtn_None);

		gControlsModeID = sResults.newMode;

		// Now press the "Auto=" button for the newly started mode
		processButtonPress(eBtn_None);

		// TODO: Stop using this temp hack
		OverlayWindow::redraw();
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
	for(size_t i = 0; i < sResults.macros.size(); ++i)
		InputDispatcher::sendKeyCommand(sResults.macros[i]);

	// Track time spent in mouselook mode
	if( wantMouseLook )
		sModeState.mouseLookTime += gAppFrameTime;
}

} // InputTranslator
