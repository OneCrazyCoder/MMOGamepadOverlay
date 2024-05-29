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
BitVector<> gReshapeHUD;
BitVector<> gActiveHUD;
BitVector<> gDisabledHUD;
std::vector<u16> gConfirmedMenuItem;
std::vector<u8> gKeyBindArrayLastIndex;
std::vector<u8> gKeyBindArrayDefaultIndex;
BitVector<> gKeyBindArrayLastIndexChanged;
BitVector<> gKeyBindArrayDefaultIndexChanged;
double gUIScale = 1.0f;
