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
BitVector<> gVisibleHUD;
BitVector<> gRedrawHUD;
BitVector<> gActiveHUD;
u8 gLastGroupTarget = 0;
u8 gGroupTargetOrigin = 0;
u8 gDefaultGroupTarget = 0;
bool gLastGroupTargetUpdated = false;
bool gDefaultGroupTargetUpdated = false;

