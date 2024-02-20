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

// Show dialog to change profile has been requested
extern bool gChangeProfile;

// Milliseconds since app started main loop
extern u32 gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// How many updates the main loop has gone through since starting
extern u32 gAppUpdateCount;

// Which HUD elements should be visible
extern BitVector<> gVisibleHUD;

// Which HUD elements need to be re-drawn
extern BitVector<> gRedrawHUD;

// Which HUD elements have recently been interacted with (for inactive fade)
extern BitVector<> gActiveHUD;

// State of each active Menu (sub-menu, selected item, etc)
extern std::vector<MenuState> gMenuStates;

// Which group member (self being #0) last targeted with relative targeting
extern u8 gLastGroupTarget;

// Which group member to go to prev/next from using relative targeting
extern u8 gGroupTargetOrigin;

// Which group member to target initially when begin targeting
extern u8 gDefaultGroupTarget;

// Flag that used group target feature and needs to display that in HUD
extern bool gLastGroupTargetUpdated;
extern bool gDefaultGroupTargetUpdated;
