//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Translates gamepad input into corresponding keyboard/mouse inputs.
	Uses the Gamepad module to read input, translates that input into desired
	keyboard and mouse input using the data in InputMap and tracked state of
	active Controls Layers and Menus, and then passes that along to be sent
	to the target game by the InputDispatcher module.
*/

#include "Common.h"

namespace InputTranslator
{

// Load configuration settings from current profile
void loadProfile();

// Clean up any state and pointers (be sure to use before reloading profile)
void cleanup();

// Call once per frame to translate GamePad into InputDispatcher w/ InputMap
void update();

} // InputTranslator
