//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Common.h"

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

bool gShutdown = false;
bool gReloadProfile = true;
u32 gAppRunTime = 0;
int gAppFrameTime = 0;
u32 gAppUpdateCount = 0;
BitVector<> gVisibleHUD;
BitVector<> gRedrawHUD;
BitVector<> gActiveHUD;
std::vector<MenuState> gMenuStates;
u8 gLastGroupTarget;
u8 gFavoriteGroupTarget;
