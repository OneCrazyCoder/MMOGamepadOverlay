//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global variables tracking application's state,
	such as modes and timers, that are needed by multiple modules.
*/

// Milliseconds since app started main loop
extern u32 gAppRunTime;

// Milliseconds that have passed since last update of main loop
extern int gAppFrameTime;

// How many updates the main loop has gone through since starting
extern u32 gAppUpdateCount;

// Which HUD elements should be visible
extern BitArray<eHUDElement_Num> gVisibleHUD;

// Macro Set currently selected by user (index into array of sets)
extern u16 gMacroSetID;

// Which group member (self being #0) last targeted with relative targeting
extern u8 gLastGroupTarget;

//Which group member should be recalled as favorite for quick access
extern u8 gFavoriteGroupTarget;

// User has requested reload their profile
extern bool gReloadProfile;
