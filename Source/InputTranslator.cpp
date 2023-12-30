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

struct ButtonState : public ConstructFromZeroInitializedMemory<ButtonState>
{
	u16 heldTime;
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
	std::vector<Command> macros;

	void clear()
	{
		newMode = gControlsModeID;
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		macros.clear();
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static ButtonState sButtonStates[eBtn_Num];
static InputResults sResults;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void processCommand(const Command& theCmd)
{
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		// Do nothing
		break;
	case eCmdType_PressAndHoldKey:
	case eCmdType_ReleaseKey:
		// Send these right away since can often be instantly processed
		InputDispatcher::sendKeyCommand(theCmd);
		break;
	case eCmdType_VKeySequence:
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		// Queue these so they are sent last, since they can potentially
		// block other inputs from being processed temporarily.
		sResults.macros.push_back(theCmd);
		break;
	case eCmdType_ChangeMode:
		sResults.newMode = theCmd.data;
		break;
	case eCmdType_ChangeMacroSet:
		gMacroSetID = theCmd.data;
		// Temp hack
		OverlayWindow::redraw();
		break;
	case eCmdType_MoveCharacter:
		// TODO
		break;
	case eCmdType_SelectAbility:
		// TODO
		break;
	case eCmdType_SelectMacro:
		processCommand(InputMap::commandForMacro(
			gMacroSetID, theCmd.data));
		break;
	case eCmdType_SelectMenu:
		// TODO
		break;
	case eCmdType_RewriteMacro:
		// TODO
		break;
	case eCmdType_TargetGroup:
		// TODO
		break;
	case eCmdType_NextMouseHotspot:
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

	processCommand(InputMap::commandForButtonAction(
		gControlsModeID, theButton, eBtnAct_PressAndHold));
	processCommand(InputMap::commandForButtonAction(
		gControlsModeID, theButton, eBtnAct_Press));
}


static void processAnalogInput(EButton theButton)
{
	// Use modeWhenPressed if it has an analog action,
	// otherwise use current mode
	Command aCmd = InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_Analog);
	if( aCmd.type == eCmdType_Empty &&
		gControlsModeID != sButtonStates[theButton].modeWhenPressed )
	{
		aCmd = InputMap::commandForButtonAction(
			gControlsModeID, theButton, eBtnAct_Analog);
	}

	if( aCmd.type == eCmdType_MoveMouse )
	{// Update mouse motion in sResults
		u8 anAnalogVal = Gamepad::buttonAnalogVal(theButton);
		bool isDigitalPress = false;
		if( !anAnalogVal && Gamepad::buttonDown(theButton) )
		{
			anAnalogVal = 255;
			isDigitalPress = true;
		}
		if( !anAnalogVal )
			return;
		switch(aCmd.data)
		{
		case eCmdSubType_Left:
			sResults.mouseMoveX -= anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eCmdSubType_Right:
			sResults.mouseMoveX += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eCmdSubType_Up:
			sResults.mouseMoveY -= anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eCmdSubType_Down:
			sResults.mouseMoveY += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eCmdSubType_WheelUpStepped:
			sResults.mouseWheelStepped = true;
			// fall through
		case eCmdSubType_WheelUp:
			sResults.mouseWheelY -= anAnalogVal;
			sResults.mouseWheelDigital = isDigitalPress;
			break;
		case eCmdSubType_WheelDownStepped:
			sResults.mouseWheelStepped = true;
			// fall through
		case eCmdSubType_WheelDown:
			sResults.mouseWheelY += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		default:
			DBG_ASSERT(false && "Invalid sub-type set for MoveMouse command");
			break;
		}
	}
}


static void processButtonShortHold(EButton theButton)
{
	sButtonStates[theButton].shortHoldDone = true;
	// Only use modeWhenPressed for short hold action
	processCommand(InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_ShortHold));
}


static void processButtonLongHold(EButton theButton)
{
	sButtonStates[theButton].longHoldDone = true;
	// Only use modeWhenPressed for long hold action
	processCommand(InputMap::commandForButtonAction(
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
	
	processCommand(aCmd);
}


static void processButtonReleased(EButton theButton)
{
	// Only use modeWhenPressed for Hold Release action!
	processCommand(InputMap::commandForButtonAction(
		sButtonStates[theButton].modeWhenPressed,
		theButton, eBtnAct_HoldRelease));

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

	processCommand(aCmd);

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
	for(size_t i = 0; i < ARRAYSIZE(sButtonStates); ++i)
		sButtonStates[i] = ButtonState();
}


void update()
{
	sResults.clear();

	// Set mouse look mode based on current control scheme
	InputDispatcher::setMouseLookMode(
		InputMap::mouseLookShouldBeOn(gControlsModeID));

	for(EButton aBtn = EButton(1);
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

		// Check for analog axis values, which do not return on "hit" when
		// lightly pressed so must be checked continuously
		processAnalogInput(aBtn);

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

	InputDispatcher::moveMouse(
		sResults.mouseMoveX,
		sResults.mouseMoveY,
		sResults.mouseMoveDigital);
	InputDispatcher::scrollMouseWheel(
		sResults.mouseWheelY,
		sResults.mouseWheelDigital,
		sResults.mouseWheelStepped);
	for(size_t i = 0; i < sResults.macros.size(); ++i)
		InputDispatcher::sendKeyCommand(sResults.macros[i]);

	if( sResults.newMode != gControlsModeID )
	{// Swap controls modes
		gControlsModeID = sResults.newMode;
		// See if have an auto-input for initializing new mode
		// This is stored in the 'press' action for eBtn_None
		const Command& anAutoCmd = InputMap::commandForButtonAction(
			sResults.newMode,
			eBtn_None,
			eBtnAct_Press);
		processCommand(anAutoCmd);
		// Temp hack
		OverlayWindow::redraw();
	}
}

} // InputTranslator
