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

// Checks if given controls layer should have mouse look turned on
bool mouseLookShouldBeOn(u16 theLayerID);

// Get commands to execute for given button in given layer, in the
// form of an array of 'Command' of size 'eBtnAct_Num', or NULL
// if no commands have been assigned to given layer & button at all
const Command* commandsForButton(u16 theLayerID, EButton theButton);

// Get command for executing given macro slot in given set
Command commandForMacro(u16 theMacroSetID, u8 theMacroSlotID);

// Get certain special-case keys directly
u8 keyForMoveTurn(ECommandDir);
u8 keyForMoveStrafe(ECommandDir);
u8 keyForMouseLookMoveTurn(ECommandDir);
u8 keyForMouseLookMoveStrafe(ECommandDir);

// Get controls layer name (label) for given ID
const std::string& layerLabel(u16 theLayerID); 

// Get macro name (label) for given macro set & slot
const std::string& macroLabel(u16 theMacroSetID, u8 theMacroSlotID);

} // InputMap
