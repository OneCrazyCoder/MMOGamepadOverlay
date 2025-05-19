//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Common.h"

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

bool gShutdown = false;
bool gLoadNewProfile = true;
u32 gAppRunTime = 0;
int gAppFrameTime = 0;
int gAppTargetFrameTime = 14; // allows >= 60 fps without taxing CPU
BitVector<256> gFiredSignals;
BitVector<32> gVisibleHUD;
BitVector<32> gRefreshHUD;
BitVector<32> gFullRedrawHUD;
BitVector<32> gReshapeHUD;
BitVector<32> gActiveHUD;
BitVector<32> gDisabledHUD;
bool gRedrawDynamicHUDStrings = false;
EHotspotGuideMode gHotspotsGuideMode = eHotspotGuideMode_Disabled;
std::vector<u16> gConfirmedMenuItem;
std::vector<u8> gKeyBindArrayLastIndex;
std::vector<u8> gKeyBindArrayDefaultIndex;
BitVector<32> gKeyBindArrayLastIndexChanged;
BitVector<32> gKeyBindArrayDefaultIndexChanged;
double gUIScale = 1.0;
double gWindowUIScale = 1.0;
