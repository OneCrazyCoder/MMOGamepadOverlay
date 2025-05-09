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
extern u32 gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// Target value of gAppFrameTime (determines sleep() time)
extern int gAppTargetFrameTime;

// Signals sent out to trigger signal commands
extern BitVector<> gFiredSignals;

// Which HUD elements should be visible
extern BitVector<> gVisibleHUD;

// Which HUD elements need to be re-drawn to reflect changes (like selection)
extern BitVector<> gRedrawHUD;

// Which HUD elements need complete erase and re-draw from scratch
extern BitVector<> gFullRedrawHUD;

// Which HUD elements need to be moved/resized
extern BitVector<> gReshapeHUD;

// Which HUD elements have recently been interacted with (for inactive fade)
extern BitVector<> gActiveHUD;

// Which HUD elements are disabled (i.e. Menu w/o input assigned to control it)
extern BitVector<> gDisabledHUD;

// Flag that all HUD elements containing dynamic strings need redraw
extern bool gRedrawDynamicHUDStrings;

// Displays hotspots mouse cursor can jump to with Select Hotspot command
extern EHotspotGuideMode gHotspotsGuideMode;

// A menu item has been selected & confirmed and should show a confirmation flash
extern std::vector<u16> gConfirmedMenuItem;

// Last-used and default index for Key Bind Arrays, and flags for changing them
extern std::vector<u8> gKeyBindArrayLastIndex;
extern std::vector<u8> gKeyBindArrayDefaultIndex;
extern BitVector<> gKeyBindArrayLastIndexChanged;
extern BitVector<> gKeyBindArrayDefaultIndexChanged;

// All Hotspots/Positions/Sizes will be scaled by gUIScale
extern double gUIScale, gWindowUIScale;

