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

// Loading a different profile has been requested
extern bool gLoadNewProfile;

// Milliseconds since app started main loop
extern int gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// Target value of gAppFrameTime (determines sleep() time)
extern int gAppTargetFrameTime;

// Signals sent out to trigger signal commands
extern BitVector<256> gFiredSignals;

// Which HUD elements should be visible
extern BitVector<32> gVisibleHUD;

// Which HUD elements need to redraw selection, confirmation, or disabled state
extern BitVector<32> gRefreshHUD;

// Which HUD elements need complete erase and re-draw from scratch
extern BitVector<32> gFullRedrawHUD;

// Which HUD elements need to be moved/resized
extern BitVector<32> gReshapeHUD;

// Which HUD elements have recently been interacted with (for inactive fade)
extern BitVector<32> gActiveHUD;

// Which HUD elements are disabled (i.e. Menu w/o input assigned to control it)
extern BitVector<32> gDisabledHUD;

// Flag that all HUD elements containing dynamic strings need redraw
extern bool gRedrawDynamicHUDStrings;

// Displays hotspots mouse cursor can jump to with Select Hotspot command
extern EHotspotGuideMode gHotspotsGuideMode;

// A menu item has been selected & confirmed and should show a confirmation flash
extern std::vector<u16> gConfirmedMenuItem;

// Last-used and default index for Key Bind Arrays, and flags for changing them
extern std::vector<int> gKeyBindArrayLastIndex;
extern std::vector<int> gKeyBindArrayDefaultIndex;
extern BitVector<32> gKeyBindArrayLastIndexChanged;
extern BitVector<32> gKeyBindArrayDefaultIndexChanged;

// All Hotspots/Positions/Sizes will be scaled by gUIScale
extern double gUIScale, gWindowUIScale;

