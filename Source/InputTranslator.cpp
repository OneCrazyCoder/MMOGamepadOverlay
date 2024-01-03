//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "Lookup.h"
#include "OverlayWindow.h" // to set alpha fade in/out
#include "Profile.h"

namespace InputTranslator
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

// Whether or not debug messages print depends on which line is commented out
#define transDebugPrint(...) debugPrint("InputTranslator: " __VA_ARGS__)
//#define transDebugPrint(...) ((void)0)

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
	u16 heldTime;
	u8 vKeyHeld;
	bool shortHoldDone;
	bool longHoldDone;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		ZeroMemory(this, sizeof(ButtonState));
	}

	ButtonState() : vKeyHeld() { clear(); }
	~ButtonState() { DBG_ASSERT(vKeyHeld == 0); }
};

typedef std::vector<std::pair<int, ButtonState> > ModeAutoButtons;
struct TranslatorState
{
	ButtonState btn[eBtn_Num];
	ModeAutoButtons modeAutoButtons;
	int mouseLookTime;

	void clear()
	{
		modeAutoButtons.clear();
		for(size_t i = 0; i < ARRAYSIZE(btn); ++i)
			btn[i].clear();
		mouseLookTime = 0;
	}
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
static TranslatorState sState;
static InputResults sResults;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void	loadButtonCommandsForCurrentMode()
{
	for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
	{
		sState.btn[i].commands =
			InputMap::commandsForButton(gControlsModeID, EButton(i));
	}

	// Begin tracking any "Auto" buttons for current and parent modes
	// Vector should be arranged with furthest ancestor first, current last.
	// Which means we first need to find inheritance chain and reverse it.
	static std::vector<int> sModeInheritanceChain;
	sModeInheritanceChain.clear();
	int aModeID = gControlsModeID;
	do {
		sModeInheritanceChain.push_back(aModeID);
		aModeID = InputMap::parentModeOf(aModeID);
	} while(aModeID != 0);
	std::reverse(sModeInheritanceChain.begin(), sModeInheritanceChain.end());
	for(size_t i = 0; i < sModeInheritanceChain.size(); ++i)
	{
		const int aModeID = sModeInheritanceChain[i];
		if( sState.modeAutoButtons.size() > i &&
			sState.modeAutoButtons[i].first == aModeID )
		{// Already has this button set up
			continue;
		}
		if( sState.modeAutoButtons.size() <= i )
			sState.modeAutoButtons.resize(i+1);
		sState.modeAutoButtons[i].first = aModeID;
		sState.modeAutoButtons[i].second.commands =
			InputMap::commandsForButton(aModeID, eBtn_None);
	}
	sState.modeAutoButtons.resize(sModeInheritanceChain.size());
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
	// by other button actions later. This makes sure if mode is changed
	// before the button is released, it will still behave as it would in
	// the previous mode, which could be important especially for cases
	// like _Release matching up with a particular _Press. This can also
	// be overridden for specific buttons by a mode using ForceReset,
	// causing the button to immediately switch to the new control mode
	// even if it was originally pressed while in another mode.
	theBtnState.commandsWhenPressed = theBtnState.commands;

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
			sResults.mouseWheelY -= theAnalogVal;
			sResults.mouseWheelDigital = isDigitalDown;
			break;
		case eCmdDir_Down:
			sResults.mouseWheelStepped = true;
			sResults.mouseWheelY += theAnalogVal;
			sResults.mouseMoveDigital = isDigitalDown;
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
	// First, release any key being held by this button
	releaseKeyHeldByButton(theBtnState);

	// If released quickly enough, process 'tap' event
	if( theBtnState.heldTime < kConfig.shortHoldTime )
		processButtonTap(theBtnState);

	// Use commandsWhenPressed if it has a _Release action.
	// If not, can use the _Release action for current mode instead,
	// but ONLY if current mode does NOT have an associated _Press
	// action along with it.
	// Idea here being that _Press and _Release commands may be
	// associated with each other, and so should avoid using
	// a _Release when the associated _Press was never used, but
	// we want to process a _Release in new mode in case it is,
	// in fact, the way the mode has been set to return to a former
	// mode for a "hold to change modes" style button assignment.
	Command aCmd;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Release];
	if( aCmd.type == eCmdType_Empty && theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		theBtnState.commands[eBtnAct_Press].type == eCmdType_Empty )
	{
		aCmd = theBtnState.commands[eBtnAct_Release];
	}

	processCommand(theBtnState, aCmd);

	// At this point no key should be held by this button - confirm that!
	releaseKeyHeldByButton(theBtnState);

	// Reset certain button states when it is released
	theBtnState.heldTime = 0;
	theBtnState.commandsWhenPressed = NULL;
	theBtnState.shortHoldDone = false;
	theBtnState.longHoldDone = false;
	DBG_ASSERT(theBtnState.vKeyHeld == 0);
}


static void processButtonState(
	ButtonState& theBtnState,
	bool isDown, bool wasHit, u8 theAnalogVal = 0)
{
	const bool wasDown = theBtnState.heldTime > 0;

	// If button was down previously and has either been hit again or
	// isn't down any more, then it must have been released, and we should
	// process that before any new hits since it may use a prior mode.
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

	// Update heldTime value and see if need to process a long hold
	if( isDown )
	{
		if( theBtnState.heldTime < (0xFFFF - gAppFrameTime) )
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


static void setModeTo(int theNewMode)
{
	const int anOldMode = gControlsModeID;

	// Each mode has an "Auto" button (assigned to eBtn_None in InputMap)
	// that is "pressed" whenever a mode becomes active, and "released"
	// when the mode exits. Beyond that, unlike regular buttons, each
	// mode's "Auto" button is separate from that of its child or parent
	// modes (rather than child modes just overriding parent mode button
	// assignments), meaning multiple "Auto" buttons can be "held" at once
	// when a mode has one or more parent modes. Finally, when changing
	// modes, if a mode will be active in both cases (because it is part
	// of the inheritance chain of both), its associated Auto button will
	// be considered still held down instead of re-hit.

	// Find each Auto button that will no longer be "held down" from this
	// change in modes by checking each tracked active Auto button to see
	// if it is NOT a part of the new mode's inheritance chain, and in that
	// case "releasing" it. We do this in reverse order though so furthest
	// changing ancestor is the last button to be released.
	for(ModeAutoButtons::reverse_iterator itr =
		sState.modeAutoButtons.rbegin();
		itr != sState.modeAutoButtons.rend(); ++itr)
	{
		if( InputMap::modeInheritsFrom(theNewMode, itr->first) )
			break;
		
		transDebugPrint("Releasing special 'Auto %s' button\n",
			InputMap::modeLabel(itr->first).c_str());
		processButtonState(itr->second, false, false);
	}

	// Change the current mode ID
	transDebugPrint("Swapping to Controls Mode: %s\n",
		InputMap::modeLabel(sResults.newMode).c_str());
	gControlsModeID = sResults.newMode;

	// Update default command assignments for all buttons
	// (including Auto buttons for current and parent modes)
	loadButtonCommandsForCurrentMode();

	// Now make sure all associated Auto mode buttons get "pressed", starting
	// with the furthest ancestor and ending with current mode
	for(ModeAutoButtons::iterator itr = sState.modeAutoButtons.begin();
		itr != sState.modeAutoButtons.end(); ++itr)
	{
		transDebugPrint("Pressing special 'Auto %s' button\n",
			InputMap::modeLabel(itr->first).c_str());
		processButtonState(itr->second, true, false);
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
	sState.clear();
	setModeTo(0);
	gMacroSetID = 0;
}


void cleanup()
{
	for(size_t i = 0; i < ARRAYSIZE(sState.btn); ++i)
		releaseKeyHeldByButton(sState.btn[i]);
	for(size_t i = 0; i < sState.modeAutoButtons.size(); ++i)
		releaseKeyHeldByButton(sState.modeAutoButtons[i].second);
}


void update()
{
	sResults.clear();

	// Set mouse look mode based on current controls mode
	const bool wantMouseLook = InputMap::mouseLookShouldBeOn(gControlsModeID);
	InputDispatcher::setMouseLookMode(wantMouseLook);
	if( !wantMouseLook ) sState.mouseLookTime = 0;

	// Auto buttons are just considered held down for now
	for(size_t i = 0; i < sState.modeAutoButtons.size(); ++i)
		processButtonState(sState.modeAutoButtons[i].second, true, false);

	// Process state changes of actual physical Gamepad buttons
	for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
	{
		processButtonState(
			sState.btn[i],
			Gamepad::buttonDown(EButton(i)),
			Gamepad::buttonHit(EButton(i)),
			Gamepad::buttonAnalogVal(EButton(i)));
	}

	// Swap controls modes if any of the above requested it
	if( sResults.newMode != gControlsModeID )
	{// Swap controls modes
		setModeTo(sResults.newMode);

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
		sState.mouseLookTime += gAppFrameTime;
}

} // InputTranslator
