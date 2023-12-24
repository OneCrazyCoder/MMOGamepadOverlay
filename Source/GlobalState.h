//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global variables tracking application's state,
	such as modes and timers, that are needed by multiple modules.
*/

#include "Common.h"

// Milliseconds since app started main loop
extern u32 gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// How many updates the main loop has gone through since starting
extern u32 gAppUpdateCount;

// Macro Set currently selected by user (index into array of sets)
extern u16 gMacroSetID;

// User has requested reload their profile
extern bool gReloadProfile;
