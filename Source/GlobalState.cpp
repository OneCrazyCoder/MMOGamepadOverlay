//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "Common.h"

//------------------------------------------------------------------------------
// Global Variables
//------------------------------------------------------------------------------

bool gShutdown = false;
bool gLoadNewProfile = true;
int gAppRunTime = 0;
int gAppFrameTime = 0;
int gAppTargetFrameTime = 14; // allows >= 60 fps without taxing CPU
BitVector<256> gFiredSignals;
BitVector<32> gVisibleOverlays;
BitVector<32> gRefreshOverlays;
BitVector<32> gFullRedrawOverlays;
BitVector<32> gReshapeOverlays;
BitVector<32> gActiveOverlays;
BitVector<32> gDisabledOverlays;
EHotspotGuideMode gHotspotsGuideMode = eHotspotGuideMode_Disabled;
std::vector<u16> gConfirmedMenuItem;
std::vector<int> gKeyBindCycleLastIndex;
std::vector<int> gKeyBindCycleDefaultIndex;
BitVector<32> gKeyBindCycleLastIndexChanged;
BitVector<32> gKeyBindCycleDefaultIndexChanged;
double gUIScale = 1.0;
double gWindowUIScale = 1.0;
