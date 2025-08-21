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

// Which transparent overlay windows (root menus) should be visible
extern BitVector<32> gVisibleOverlays;

// Which overlays need to redraw selection, confirmation, or disabled state
extern BitVector<32> gRefreshOverlays;

// Which overlays need complete erase and re-draw from scratch
extern BitVector<32> gFullRedrawOverlays;

// Which overlays need to be moved/resized
extern BitVector<32> gReshapeOverlays;

// Which overlays have recently been interacted with (for inactive fade)
extern BitVector<32> gActiveOverlays;

// Which overlays are disabled (i.e. Menu w/o input assigned to control it)
extern BitVector<32> gDisabledOverlays;

// Displays hotspots mouse cursor can jump to with Select Hotspot command
extern EHotspotGuideMode gHotspotsGuideMode;

// A menu item has been selected & confirmed and should show a confirmation flash
extern std::vector<u16> gConfirmedMenuItem;

// Last-used and default index for Key Bind Cycles, and flags for changing them
extern std::vector<int> gKeyBindCycleLastIndex; // -1 = use default index next
extern std::vector<int> gKeyBindCycleDefaultIndex;
extern BitVector<32> gKeyBindCycleLastIndexChanged;
extern BitVector<32> gKeyBindCycleDefaultIndexChanged;

// All Hotspots/Positions/Sizes will be scaled by gUIScale
extern double gUIScale, gWindowUIScale;

