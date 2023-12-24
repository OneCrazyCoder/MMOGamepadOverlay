//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Loads and manages the configuration settings for mapping gamepad
	input to keyboard/mouse input, including stored macro sets.
*/

#include "Common.h"

namespace InputMap
{

// Load the input mappings and macro sets from current profile
void loadProfile();

std::string getMacroOutput(int theMacroSetID, int theMacroSlotID);
std::string getMacroLabel(int theMacroSetID, int theMacroSlotID);

} // InputMap
