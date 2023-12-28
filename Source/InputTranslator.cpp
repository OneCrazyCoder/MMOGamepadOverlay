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
kHoldEventDisabled = 0xFFFF,
};


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	u16 maxTapHoldTime;

	void load()
	{
		maxTapHoldTime = Profile::getInt("System/MaxTapHoldTime", 40);		
	}
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ButtonState : public ConstructFromZeroInitializedMemory<ButtonState>
{
	Command tap;
	Command onceHeld;
	Command release;
	Command analog;
	u16 heldTime;
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
static ButtonState sButtonStates[Gamepad::eBtn_Num];
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


static void processButtonPress(Gamepad::EButton theButton)
{
	const Command& aCmd = InputMap::commandForButton(
		gControlsModeID, theButton, eButtonAction_Press);

	// Store commands for other actions assigned to this button.
	// This ensures that if control scheme is changed before this
	// button is released, the original release event is still used,
	// and don't end up in a situation where a keyboard key gets
	// "stuck" held down forever, nor do we have to immediately force
	// all keys to be released every time a control scheme changes.
	sButtonStates[theButton].tap = InputMap::commandForButton(
		gControlsModeID, theButton, eButtonAction_Tap);
	sButtonStates[theButton].onceHeld = InputMap::commandForButton(
		gControlsModeID, theButton, eButtonAction_OnceHeld);
	sButtonStates[theButton].release = InputMap::commandForButton(
		gControlsModeID, theButton, eButtonAction_Release);
	sButtonStates[theButton].analog = InputMap::commandForButton(
		gControlsModeID, theButton, eButtonAction_Analog);

	processCommand(aCmd);
}


static void processAnalogInput(Gamepad::EButton theButton)
{
	// Use stored command if one exists, otherwise use current mode's
	Command aCmd = sButtonStates[theButton].analog;
	if( aCmd.type == eCmdType_Empty )
	{
		aCmd = InputMap::commandForButton(
			gControlsModeID, theButton, eButtonAction_Analog);
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


static void processButtonLongHold(Gamepad::EButton theButton)
{
	// Only used stored command for long hold action
	processCommand(sButtonStates[theButton].onceHeld);
}


static void processButtonTap(Gamepad::EButton theButton)
{
	// Use stored command if one exists, otherwise use current mode's
	Command aCmd = sButtonStates[theButton].tap;
	if( aCmd.type == eCmdType_Empty )
	{
		aCmd = InputMap::commandForButton(
			gControlsModeID, theButton, eButtonAction_Tap);
	}
	
	processCommand(aCmd);
}


static void processButtonReleased(Gamepad::EButton theButton)
{
	// If released quickly enough, process 'tap' event
	if( sButtonStates[theButton].heldTime <= kConfig.maxTapHoldTime )
		processButtonTap(theButton);

	// Reset held time to 0
	sButtonStates[theButton].heldTime = 0;

	// Use stored command for release by default, but if it is empty and
	// and current mode has no command for 'press', use current mode's
	// (assumed it is the case that the prior mode switched to this
	// mode on press, and this mode returns to prior on release,
	// and shouldn't have any problems with stuck buttons or the like if
	// prior is missing 'release' and current is missing 'press').
	Command aCmd = sButtonStates[theButton].release;
	if( aCmd.type == eCmdType_Empty )
	{
		aCmd = InputMap::commandForButton(
			gControlsModeID, theButton, eButtonAction_Press);
		if( aCmd.type == eCmdType_Empty )
		{
			aCmd = InputMap::commandForButton(
				gControlsModeID, theButton, eButtonAction_Release);
		}
	}

	processCommand(aCmd);

	// Now that release has been processed, forget stored commands
	sButtonStates[theButton].tap = Command();
	sButtonStates[theButton].onceHeld = Command();
	sButtonStates[theButton].release = Command();
	sButtonStates[theButton].analog = Command();
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

	for(Gamepad::EButton aBtn = Gamepad::EButton(1);
		aBtn < Gamepad::eBtn_Num;
		aBtn = Gamepad::EButton(aBtn+1))
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
			if( sButtonStates[aBtn].heldTime != kHoldEventDisabled )
				sButtonStates[aBtn].heldTime += gAppFrameTime;
			if( sButtonStates[aBtn].heldTime > kConfig.maxTapHoldTime &&
				sButtonStates[aBtn].heldTime != kHoldEventDisabled )
			{
				processButtonLongHold(aBtn);
				sButtonStates[aBtn].heldTime = kHoldEventDisabled;
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
		const Command& anAutoCmd = InputMap::commandForButton(
			sResults.newMode,
			Gamepad::eBtn_None,
			eButtonAction_Press);
		processCommand(anAutoCmd);
		// Temp hack
		OverlayWindow::redraw();
	}
}

} // InputTranslator
