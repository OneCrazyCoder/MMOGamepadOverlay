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

// User has requested reload their profile
extern bool gReloadProfile;

// Milliseconds since app started main loop
extern u32 gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// How many updates the main loop has gone through since starting
extern u32 gAppUpdateCount;

// Which HUD elements should be visible
extern BitVector<> gVisibleHUD;

// State of each active Menu (sub-menu, selected item, etc)
extern std::vector<MenuState> gMenuStates;

// Which group member (self being #0) last targeted with relative targeting
extern u8 gLastGroupTarget;

//Which group member should be recalled as favorite for quick access
extern u8 gFavoriteGroupTarget;
