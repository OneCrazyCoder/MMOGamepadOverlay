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

struct ButtonState
{
	const InputMap::Scheme* schemeWhenPressed;
	u16 heldTime;
};


struct InputResults
{
	int newMode;
	int newMacroSet;
	s16 mouseMoveX;
	s16 mouseMoveY;
	s16 mouseWheelY;
	bool mouseMoveDigital;
	bool mouseWheelDigital;
	bool mouseWheelStepped;
	std::vector<u8> heldKeys;
	std::vector<u8> releasedKeys;
	std::vector<const char*> keySequences;

	void clear()
	{
		newMode = gControlsModeID;
		newMacroSet = gMacroSetID;
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		heldKeys.clear();
		releasedKeys.clear();
		keySequences.clear();
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static ButtonState sButtonStates[InputMap::Scheme::kButtonsChecked] = { 0 };
static InputResults sResults;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void processCommand(const std::string& theCommandString)
{
	DBG_ASSERT(theCommandString.size() > 1);

	// Check command char to know how to process the string
	switch(theCommandString[0])
	{
	case eCmdChar_ChangeMode:
		sResults.newMode = theCommandString[1];
		break;
	case eCmdChar_PressAndHoldKey:
		sResults.heldKeys.push_back(theCommandString[1]);
		break;
	case eCmdChar_ReleaseKey:
		sResults.releasedKeys.push_back(theCommandString[1]);
		break;
	case eCmdChar_Mouse:
		// Processed elsewhere
		break;
	case eCmdChar_MoveCharacter:
		// TODO
		break;
	case eCmdChar_SelectAbility:
		// TODO
		break;
	case eCmdChar_SelectMacro:
		// TODO
		break;
	case eCmdChar_SelectMenu:
		// TODO
		break;
	case eCmdChar_ChangeMacro:
		// TODO
		break;
	case eCmdChar_TargetGroup:
		// TODO
		break;
	case eCmdChar_NextMouseHotspot:
		// TODO
		break;
	case eCmdChar_ChangeMacroSet:
		// Should never reach here with these commands
		DBG_ASSERT(false);
		break;
	case eCmdChar_SlashCommand:
	case eCmdChar_SayString:
	default:
		sResults.keySequences.push_back(theCommandString.c_str());
		break;
	}
}


static void processButtonPress(
	Gamepad::EButton theButton,
	const InputMap::Scheme& theScheme)
{
	const InputMap::Scheme::Commands& aCmd = theScheme.cmd[theButton];
	
	// Remember current scheme for when this button is released later.
	// This ensures that if control scheme is changed before this
	// button is released, the original release event is still used,
	// and don't end up in a situation where a keyboard key gets
	// "stuck" held down forever, nor do we have to immediately force
	// all keys to be released every time a control scheme changes.
	sButtonStates[theButton].schemeWhenPressed = &theScheme;

	if( aCmd.press.empty() )
		return;

	processCommand(aCmd.press);
}


static void processAnalogInput(
	Gamepad::EButton theButton,
	const InputMap::Scheme& theScheme)
{
	// Use stored scheme unless it doesn't exist or has no 'press' entry
	const InputMap::Scheme* aScheme =
		sButtonStates[theButton].schemeWhenPressed;
	if( !aScheme || aScheme->cmd[theButton].press.empty() )
		aScheme = &theScheme;
	const InputMap::Scheme::Commands& aCmd = aScheme->cmd[theButton];
	if( aCmd.press.empty() )
		return;

	if( aCmd.press[0] == eCmdChar_Mouse && aCmd.press.size() > 0 )
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
		switch(aCmd.press[1])
		{
		case eSubCmdChar_Left:
			sResults.mouseMoveX -= anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eSubCmdChar_Right:
			sResults.mouseMoveX += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eSubCmdChar_Up:
			sResults.mouseMoveY -= anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eSubCmdChar_Down:
			sResults.mouseMoveY += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		case eSubCmdChar_WheelUpStepped:
			sResults.mouseWheelStepped = true;
			// fall through
		case eSubCmdChar_WheelUp:
			sResults.mouseWheelY -= anAnalogVal;
			sResults.mouseWheelDigital = isDigitalPress;
			break;
		case eSubCmdChar_WheelDownStepped:
			sResults.mouseWheelStepped = true;
			// fall through
		case eSubCmdChar_WheelDown:
			sResults.mouseWheelY += anAnalogVal;
			sResults.mouseMoveDigital = isDigitalPress;
			break;
		}
	}
}


static void processButtonLongHold(
	Gamepad::EButton theButton)
{
	// Always use stored scheme for long hold events
	const InputMap::Scheme* aScheme =
		sButtonStates[theButton].schemeWhenPressed;
	DBG_ASSERT(aScheme);
	const InputMap::Scheme::Commands& aCmd = aScheme->cmd[theButton];
	if( aCmd.held.empty() )
		return;

	processCommand(aCmd.held);
}


static void processButtonTap(
	Gamepad::EButton theButton,
	const InputMap::Scheme& theScheme)
{
	// Use stored scheme by default, but if it has no entry, use current
	const InputMap::Scheme* aScheme =
		sButtonStates[theButton].schemeWhenPressed;
	DBG_ASSERT(aScheme);
	if( aScheme->cmd[theButton].tap.empty() )
		aScheme = &theScheme;
	const InputMap::Scheme::Commands& aCmd = aScheme->cmd[theButton];
	if( aCmd.tap.empty() )
		return;
	
	processCommand(aCmd.tap);
}


static void processButtonReleased(
	Gamepad::EButton theButton,
	const InputMap::Scheme& theScheme)
{
	// If released quickly enough, process 'tap' event
	if( sButtonStates[theButton].heldTime <= kConfig.maxTapHoldTime )
		processButtonTap(theButton, theScheme);

	// Reset held time to 0
	sButtonStates[theButton].heldTime = 0;

	// Use stored scheme by default, but if it has no entry for 'release'
	// and current scheme has no entry for 'press', use current scheme
	// (assumed it is the case that the prior scheme switched to this
	// scheme on press, and this scheme returns to prior on release,
	// and shouldn't have any problems with stuck buttons or the like if
	// prior is missing 'release' and current is missing 'press').
	const InputMap::Scheme* aScheme =
		sButtonStates[theButton].schemeWhenPressed;
	DBG_ASSERT(aScheme);
	if( aScheme->cmd[theButton].release.empty() )
	{
		if( theScheme.cmd[theButton].press.empty() )
			aScheme = &theScheme;
		else
			return;
	}
	const InputMap::Scheme::Commands& aCmd = aScheme->cmd[theButton];
	if( aCmd.release.empty() )
		return;

	processCommand(aCmd.release);
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
	ZeroMemory(&sButtonStates, sizeof(sButtonStates));
	sResults.clear();
}


void update()
{
	sResults.clear();

	// Get current active control scheme
	const InputMap::Scheme& aScheme =
		InputMap::controlScheme(gControlsModeID);

	// Set mouse look mode based on current control scheme
	InputDispatcher::setMouseLookMode(aScheme.mouseLookOn);

	for(Gamepad::EButton aBtn = Gamepad::EButton(1);
		aBtn < InputMap::Scheme::kButtonsChecked;
		aBtn = Gamepad::EButton(aBtn+1))
	{
		const bool wasDown = sButtonStates[aBtn].heldTime > 0;
		const bool isDown = Gamepad::buttonDown(aBtn);
		const bool wasHit = Gamepad::buttonHit(aBtn);

		// If button was down previously and has either been hit again or
		// isn't down any more, then it must have been released, and we should
		// process that before any new hits since it may use a prior scheme.
		if( wasDown && (!isDown || wasHit) )
			processButtonReleased(aBtn, aScheme);

		// If was hit (or somehow is down but wasn't before), process a press
		if( wasHit || (isDown && !wasDown) )
		{
			processButtonPress(aBtn, aScheme);
			// Now that have processed a press, if it's not down any more,
			// it must have already been released, so process that as well.
			if( !isDown )
				processButtonReleased(aBtn, aScheme);
		}

		// Check for analog axis values, which do not return on "hit" when
		// lightly pressed so must be checked continuously
		processAnalogInput(aBtn, aScheme);

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
}

} // InputTranslator
