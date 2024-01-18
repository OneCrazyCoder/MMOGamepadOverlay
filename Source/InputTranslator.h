//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Translates gamepad input into corresponding keyboard/mouse inputs.
	Converts gamepad input into keyboard and mouse inputs to be sent to the
	target game by the InputDispatcher module, based on user configurations
	and current global state such as the current "mode" of input.
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
