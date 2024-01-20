//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Tracks (and possibly launches) target app (game) for things like window
	region and whether or not it is active, minimized, etc., so can sync up
	the Overlay window appropriately. May even trigger this app to quit when
	the target app does, if user desires.
*/

#include "Common.h"

namespace TargetApp
{

// Load configuration settings from current profile
void loadProfile();

// Attempt to launch the target application if one was specified
void autoLaunch();

// Clean up any state and handles
void cleanup();

// Call once per frame to check for changes to target app
void update();

// Call when get WM_HOTKEY with kFullScreenHotkeyID
void toggleFullScreenMode();

} // TargetApp
