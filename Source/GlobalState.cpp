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
int gAppTargetFrameTime = 14; // allows >= 60 fps without taxing CPU
BitVector<> gFiredSignals;
BitVector<> gVisibleHUD;
BitVector<> gRedrawHUD;
BitVector<> gFullRedrawHUD;
BitVector<> gReshapeHUD;
BitVector<> gActiveHUD;
BitVector<> gDisabledHUD;
EHotspotGuideMode gHotspotsGuideMode = eHotspotGuideMode_Disabled;
std::vector<u16> gConfirmedMenuItem;
std::vector<u8> gKeyBindArrayLastIndex;
std::vector<u8> gKeyBindArrayDefaultIndex;
BitVector<> gKeyBindArrayLastIndexChanged;
BitVector<> gKeyBindArrayDefaultIndexChanged;
double gUIScale = 1.0f;
