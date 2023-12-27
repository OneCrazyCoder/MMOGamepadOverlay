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

struct Scheme
{
	static const int kButtonsChecked = 25; // Everything before Home
	struct Commands
	{
		// Triggered when button first pressed
		// (and continiously while held in cases like mouse movement)
		std::string press;
		// Triggered once when button released after short time
		std::string tap;
		// Triggered once when button held past "tap" time
		std::string held;
		// Triggered once when button released (regardless of hold time)
		std::string release;
	};
	Commands cmd[kButtonsChecked];
	bool mouseLookOn;

	Scheme() : mouseLookOn() {}
};

// Load the input mappings and macro sets from current profile
void loadProfile();

// Get selected mode's control scheme
const Scheme& controlScheme(int theModeID);

// Get bitfield of EHudElements that should be shown
BitArray8<eHUDElement_Num> visibleHUDElements(int theMode);

// Get command for executing given macro slot in given set
const std::string& macroOutput(int theMacroSetID, int theMacroSlotID);

// Get macro name (label) for given macro set & slot
const std::string& macroLabel(int theMacroSetID, int theMacroSlotID);

} // InputMap
