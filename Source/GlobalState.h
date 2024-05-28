//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global variables tracking application's state,
	such as menu states and timers, that are needed by multiple modules.
*/

// App shutdown has been requested
extern bool gShutdown;

// Reload of current profile has been requested
extern bool gReloadProfile;

// Milliseconds since app started main loop
extern u32 gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// Which HUD elements should be visible
extern BitVector<> gVisibleHUD;

// Which HUD elements need to be re-drawn
extern BitVector<> gRedrawHUD;

// Which HUD elements have recently been interacted with (for inactive fade)
extern BitVector<> gActiveHUD;

// Which HUD elements are disabled (i.e. Menu w/o input assigned to control it)
extern BitVector<> gDisabledHUD;

// A menu item has been selected & confirmed and should show a confirmation flash
extern std::vector<u16> gConfirmedMenuItem;

// Last-used and default index for Key Bind Arrays, and flags for changing them
extern std::vector<u8> gKeyBindArrayLastIndex;
extern std::vector<u8> gKeyBindArrayDefaultIndex;
extern BitVector<> gKeyBindArrayLastIndexChanged;
extern BitVector<> gKeyBindArrayDefaultIndexChanged;

// All Hotspots/Positions/Sizes will be scaled by this values
extern double gUIScale;
