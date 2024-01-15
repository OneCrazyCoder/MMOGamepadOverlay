//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Common.h"

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

u32 gAppRunTime = 0;
int gAppFrameTime = 0;
u32 gAppUpdateCount = 0;
BitArray<eHUDElement_Num> gVisibleHUD;
u16 gMacroSetID = 0;
u8 gLastGroupTarget;
u8 gFavoriteGroupTarget;
bool gReloadProfile = true;
