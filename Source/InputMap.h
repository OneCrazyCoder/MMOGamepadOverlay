//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Loads and manages the configuration settings for mapping gamepad
	input to keyboard/mouse input, including stored macro sets.
	Also tracks some HUD-related information relevant to control schemes
	such as macro labels and which HUD elements need to be displayed.
*/

#include "Common.h"

namespace InputMap
{

// Load the input mappings and macro sets from current profile
void loadProfile();

// Checks if given controls mode should have mouse look turned on
bool mouseLookShouldBeOn(int theModeID);

// Get command to execute when given button performs given action
Command commandForButtonAction(
	int theModeID,
	EButton theButton,
	EButtonAction theAction);

// Get command for executing given macro slot in given set
Command commandForMacro(int theMacroSetID, u8 theMacroSlotID);

// Get certain special-case keys directly
u8 keyForMoveTurn(ECommandDir);
u8 keyForMoveStrafe(ECommandDir);
u8 keyForMouseLookMoveTurn(ECommandDir);
u8 keyForMouseLookMoveStrafe(ECommandDir);

// Get bitfield of EHudElements that should be shown
BitArray8<eHUDElement_Num> visibleHUDElements(int theMode);

// Get macro name (label) for given macro set & slot
const std::string& macroLabel(int theMacroSetID, u8 theMacroSlotID);

} // InputMap
