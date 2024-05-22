//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HotspotMap.h"

#include "InputMap.h"
#include "WindowManager.h"

namespace HotspotMap
{

// Whether or not debug messages print depends on which line is commented out
//#define mapDebugPrint(...) debugPrint("HotspotMap: " __VA_ARGS__)
#define mapDebugPrint(...) ((void)0)

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
// This value won't overload a u32 when used with dist formula
kNormalizedTargetSize = 0x7FFF,
// Grid cells per axis, must be a power of 2
kGridSize = 4,
// >> amount to convert kNormalizedTargetSize to kGridSize
kNormalizedToGridShift = 13, 
// Must be <= kNormalizedTargetSize / kGridSize
kMaxJumpDist = 0x1FFF, 
kMaxJumpDistSquared = kMaxJumpDist * kMaxJumpDist,
// If point is too close, jump FROM it rather than to it
kMinJumpDist = 0x0100,
kMinJumpDistSquared = kMinJumpDist * kMinJumpDist,
};

enum ETask
{
	eTask_TargetSize,
	eTask_ActiveSets,
	eTask_AddToGrid,
	eTask_BeginSearch,
	eTask_FetchGrid0,
	eTask_FetchGrid1,
	eTask_FetchGrid2,
	eTask_FetchGrid3,
	eTask_NextLeft,
	eTask_NextRight,
	eTask_NextUp,
	eTask_NextDown,

	eTask_Num,
	eTask_None = eTask_Num
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct TrackedPoint
{
	// 0-32767 so can convert to dist squared without overflowing a u32
	u16 x, y;
	bool enabled;
};

struct GridPos
{
	u32 x, y;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static BitVector<> sRequestedSets;
static BitVector<> sActiveSets;
static std::vector<TrackedPoint> sPoints;
static std::vector<u16> sActiveGrid[kGridSize][kGridSize];
static std::vector<u16> sCandidates;
static u16 sNextHotspotInDir[eCmdDir_Num] = { 0 };
static SIZE sLastTargetSize;
static Hotspot sLastCursorPos;
static POINT sNormalizedCursorPos;
static GridPos sFetchGrid[4];
static BitArray<eTask_Num> sNewTasks;
static ETask sTaskInProgress = eTask_None;
static int sTaskProgress = 0;
static u32 sBestCandidateWeight = 0xFFFFFFFF;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

void processTargetSizeTask()
{
	if( sTaskProgress < sPoints.size() )
	{
		const u16 aHotspotID =
			u16(sTaskProgress) + InputMap::namedHotspotCount();
		const Hotspot& aHotspot =
			InputMap::getHotspot(aHotspotID);
		const POINT& anOverlayPos =
			WindowManager::hotspotToOverlayPos(aHotspot);
		const int aScaleFactor = max(sLastTargetSize.cx, sLastTargetSize.cy);
		if( aScaleFactor > 0 )
		{
			sPoints[sTaskProgress].x =
				(anOverlayPos.x + 1) * kNormalizedTargetSize / aScaleFactor;
			sPoints[sTaskProgress].y =
				(anOverlayPos.y + 1) * kNormalizedTargetSize / aScaleFactor;
		}
		mapDebugPrint(
			"Updating Hotspot #%d normalized position to %d x %d\n",
			sTaskProgress, sPoints[sTaskProgress].x, sPoints[sTaskProgress].y);
	}

	if( ++sTaskProgress >= sPoints.size() )
		sTaskInProgress = eTask_None;
}


void processActiveSetsTask()
{
	const size_t aHotspotSetCount = InputMap::hotspotSetCount();
	const size_t aNamedHotspotCount = InputMap::namedHotspotCount();
	while( sTaskProgress < aHotspotSetCount )
	{
		const u16 aSet = sTaskProgress++;
		bool needChangeMade = false;
		bool enableHotspots = false;
		if( sRequestedSets.test(aSet) && !sActiveSets.test(aSet) )
		{
			mapDebugPrint(
				"Enabling hotspots in Hotspot Set '%s'\n",
				InputMap::hotspotSetLabel(aSet).c_str());
			needChangeMade = true;
			enableHotspots = true;
		}
		else if( !sRequestedSets.test(aSet) && sActiveSets.test(aSet) )
		{
			mapDebugPrint(
				"Disabling hotspots in Hotspot Set '%s'\n",
				InputMap::hotspotSetLabel(aSet).c_str());
			needChangeMade = true;
			enableHotspots = false;
		}
		if( needChangeMade )
		{
			const size_t aFirstHotspot =
				InputMap::getFirstHotspotInSet(aSet) -
				aNamedHotspotCount;
			const size_t aLastHotspot =
				InputMap::getLastHotspotInSet(aSet) -
				aNamedHotspotCount;
			for(size_t i = aFirstHotspot; i <= aLastHotspot; ++i)
				sPoints[i].enabled = enableHotspots;
			sActiveSets.set(aSet, enableHotspots);
			sNewTasks.set(eTask_AddToGrid);
			break;
		}
	}

	if( sTaskProgress >= aHotspotSetCount )
		sTaskInProgress = eTask_None;
}


void processAddToGridTask()
{
	if( sTaskProgress == 0 )
	{
		if( sActiveSets.any() )
			mapDebugPrint("Adding enabled hotspots to grid...\n");
		for(size_t x = 0; x < kGridSize; ++x)
		{
			for(size_t y = 0; y < kGridSize; ++y)
				sActiveGrid[x][y].clear();
		}
	}

	while( sTaskProgress < sPoints.size() )
	{
		const u16 aPointIdx = sTaskProgress++;
		if( !sPoints[aPointIdx].enabled )
			continue;
		const u16 aGridX = sPoints[aPointIdx].x >> kNormalizedToGridShift;
		const u16 aGridY = sPoints[aPointIdx].y >> kNormalizedToGridShift;
		mapDebugPrint(
			"Adding Hotspot #%d to grid cell %d x %d\n",
			aPointIdx, aGridX, aGridY);
		sActiveGrid[aGridX][aGridY].push_back(aPointIdx);
		break;
	}

	if( sTaskProgress >= sPoints.size() )
		sTaskInProgress = eTask_None;
}


void processBeginSearchTask()
{
	if( sTaskProgress == 0 )
	{
		sCandidates.clear();
		sNextHotspotInDir[eCmdDir_L] = 0;
		sNextHotspotInDir[eCmdDir_R] = 0;
		sNextHotspotInDir[eCmdDir_U] = 0;
		sNextHotspotInDir[eCmdDir_D] = 0;
		++sTaskProgress;
		return;
	}

	const u32 kGridCellSize = kNormalizedTargetSize / kGridSize;
	const u32 aGridX = sNormalizedCursorPos.x - kGridCellSize / 2;
	const u32 aGridY = sNormalizedCursorPos.y - kGridCellSize / 2;
	sFetchGrid[0].x = aGridX >> kNormalizedToGridShift;
	sFetchGrid[0].y = aGridY >> kNormalizedToGridShift;
	sFetchGrid[1].x = (aGridX+kGridCellSize) >> kNormalizedToGridShift;
	sFetchGrid[1].y = aGridY >> kNormalizedToGridShift;
	sFetchGrid[2].x = aGridX >> kNormalizedToGridShift;
	sFetchGrid[2].y = (aGridY+kGridCellSize) >> kNormalizedToGridShift;
	sFetchGrid[3].x = (aGridX+kGridCellSize) >> kNormalizedToGridShift;
	sFetchGrid[3].y = (aGridY+kGridCellSize) >> kNormalizedToGridShift;
	if( sFetchGrid[0].x < kGridSize && sFetchGrid[0].y < kGridSize )
		sNewTasks.set(eTask_FetchGrid0);
	if( sFetchGrid[1].x < kGridSize && sFetchGrid[1].y < kGridSize )
		sNewTasks.set(eTask_FetchGrid1);
	if( sFetchGrid[2].x < kGridSize && sFetchGrid[2].y < kGridSize )
		sNewTasks.set(eTask_FetchGrid2);
	if( sFetchGrid[3].x < kGridSize && sFetchGrid[3].y < kGridSize )
		sNewTasks.set(eTask_FetchGrid3);
	sTaskInProgress = eTask_None;
}


void processFetchGridTask(u8 theFetchGridIdx)
{
	DBG_ASSERT(theFetchGridIdx < ARRAYSIZE(sFetchGrid));
	const u32 aGridX = sFetchGrid[theFetchGridIdx].x;
	const u32 aGridY = sFetchGrid[theFetchGridIdx].y;
	if( aGridX >= kGridSize || aGridY >= kGridSize ||
		sActiveGrid[aGridX][aGridY].empty() )
	{
		sTaskInProgress = eTask_None;
		return;
	}

	const u16 aPointIdx = sActiveGrid[aGridX][aGridY][sTaskProgress];
	const TrackedPoint& aPoint = sPoints[aPointIdx];
	const u32 aDeltaX = aPoint.x - sNormalizedCursorPos.x;
	const u32 aDeltaY = aPoint.y - sNormalizedCursorPos.y;
	const u32 aDist = (aDeltaX * aDeltaX) + (aDeltaY * aDeltaY);
	if( aDist > kMinJumpDistSquared && aDist < kMaxJumpDistSquared )
	{
		sCandidates.push_back(aPointIdx);
		sNewTasks.set(eTask_NextLeft);
		sNewTasks.set(eTask_NextRight);
		sNewTasks.set(eTask_NextUp);
		sNewTasks.set(eTask_NextDown);
	}

	if( ++sTaskProgress >= sActiveGrid[aGridX][aGridY].size() )
		sTaskInProgress = eTask_None;
}


void processNextLeftTask()
{
	if( sTaskProgress == 0 )
		sBestCandidateWeight = 0xFFFFFFFF;

	while( sTaskProgress < sCandidates.size() )
	{
		const u16 aPointIdx = sCandidates[sTaskProgress++];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		if( aPoint.x >= sNormalizedCursorPos.x )
			continue;
		u32 aWeight = sNormalizedCursorPos.x - aPoint.x;
		u32 anAltWeight = abs(aPoint.y - sNormalizedCursorPos.y);
		if( anAltWeight >= aWeight )
			continue;
		aWeight += anAltWeight / 2;
		if( aWeight < sBestCandidateWeight )
		{
			sNextHotspotInDir[eCmdDir_L] =
				aPointIdx + InputMap::namedHotspotCount();
			sBestCandidateWeight = aWeight;
		}
		break;
	}

	if( sTaskProgress >= sCandidates.size() )
	{
		sTaskInProgress = eTask_None;
		if( sNextHotspotInDir[eCmdDir_L] != 0 )
		{
			mapDebugPrint("Left hotspot chosen - #%d\n",
				sNextHotspotInDir[eCmdDir_L] -
				InputMap::namedHotspotCount());
		}
	}
}


void processNextRightTask()
{
	if( sTaskProgress == 0 )
		sBestCandidateWeight = 0xFFFFFFFF;

	while( sTaskProgress < sCandidates.size() )
	{
		const u16 aPointIdx = sCandidates[sTaskProgress++];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		if( aPoint.x <= sNormalizedCursorPos.x )
			continue;
		u32 aWeight = aPoint.x - sNormalizedCursorPos.x;
		u32 anAltWeight = abs(aPoint.y - sNormalizedCursorPos.y);
		if( anAltWeight >= aWeight )
			continue;
		aWeight += anAltWeight / 2;
		if( aWeight < sBestCandidateWeight )
		{
			sNextHotspotInDir[eCmdDir_R] =
				aPointIdx + InputMap::namedHotspotCount();
			sBestCandidateWeight = aWeight;
		}
		break;
	}

	if( sTaskProgress >= sCandidates.size() )
	{
		sTaskInProgress = eTask_None;
		if( sNextHotspotInDir[eCmdDir_R] != 0 )
		{
			mapDebugPrint("Right hotspot chosen - #%d\n",
				sNextHotspotInDir[eCmdDir_R] -
				InputMap::namedHotspotCount());
		}
	}
}


void processNextUpTask()
{
	if( sTaskProgress == 0 )
		sBestCandidateWeight = 0xFFFFFFFF;

	while( sTaskProgress < sCandidates.size() )
	{
		const u16 aPointIdx = sCandidates[sTaskProgress++];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		if( aPoint.y >= sNormalizedCursorPos.y )
			continue;
		u32 aWeight = sNormalizedCursorPos.y - aPoint.y;
		u32 anAltWeight = abs(aPoint.x - sNormalizedCursorPos.x);
		if( anAltWeight >= aWeight )
			continue;
		aWeight += anAltWeight / 2;
		if( aWeight < sBestCandidateWeight )
		{
			sNextHotspotInDir[eCmdDir_U] =
				aPointIdx + InputMap::namedHotspotCount();
			sBestCandidateWeight = aWeight;
		}
		break;
	}

	if( sTaskProgress >= sCandidates.size() )
	{
		sTaskInProgress = eTask_None;
		if( sNextHotspotInDir[eCmdDir_U] != 0 )
		{
			mapDebugPrint("Up hotspot chosen - #%d\n",
				sNextHotspotInDir[eCmdDir_U] -
				InputMap::namedHotspotCount());
		}
	}
}


void processNextDownTask()
{
	if( sTaskProgress == 0 )
		sBestCandidateWeight = 0xFFFFFFFF;

	while( sTaskProgress < sCandidates.size() )
	{
		const u16 aPointIdx = sCandidates[sTaskProgress++];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		if( aPoint.y <= sNormalizedCursorPos.y )
			continue;
		u32 aWeight = aPoint.y -sNormalizedCursorPos.y;
		u32 anAltWeight = abs(aPoint.x - sNormalizedCursorPos.x);
		if( anAltWeight >= aWeight )
			continue;
		aWeight += anAltWeight / 2;
		if( aWeight < sBestCandidateWeight )
		{
			sNextHotspotInDir[eCmdDir_D] =
				aPointIdx + InputMap::namedHotspotCount();
			sBestCandidateWeight = aWeight;
		}
		break;
	}

	if( sTaskProgress >= sCandidates.size() )
	{
		sTaskInProgress = eTask_None;
		if( sNextHotspotInDir[eCmdDir_D] != 0 )
		{
			mapDebugPrint("Down hotspot chosen - #%d\n",
				sNextHotspotInDir[eCmdDir_D] -
				InputMap::namedHotspotCount());
		}
	}
}


void processTasks()
{
	// Start new task or restart current if needed
	const int aNewTask = sNewTasks.firstSetBit();
	if( aNewTask <= sTaskInProgress && aNewTask < eTask_Num )
	{
		// Save incomplete task for later
		if( sTaskInProgress < eTask_Num )
			sNewTasks.set(sTaskInProgress);
		sTaskInProgress = ETask(aNewTask);
		sTaskProgress = 0;
		sNewTasks.reset(aNewTask);
	}

	switch(sTaskInProgress)
	{
	case eTask_TargetSize:	processTargetSizeTask();	break;
	case eTask_ActiveSets:	processActiveSetsTask();	break;
	case eTask_AddToGrid:	processAddToGridTask();		break;
	case eTask_BeginSearch:	processBeginSearchTask();	break;
	case eTask_FetchGrid0:	processFetchGridTask(0);	break;
	case eTask_FetchGrid1:	processFetchGridTask(1);	break;
	case eTask_FetchGrid2:	processFetchGridTask(2);	break;
	case eTask_FetchGrid3:	processFetchGridTask(3);	break;
	case eTask_NextLeft:	processNextLeftTask();		break;
	case eTask_NextRight:	processNextRightTask();		break;
	case eTask_NextUp:		processNextUpTask();		break;
	case eTask_NextDown:	processNextDownTask();		break;
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(sPoints.empty());
	const u16 aNamedHotspotCount = InputMap::namedHotspotCount();
	const u16 aHotspotsCount =
		InputMap::totalHotspotCount() - aNamedHotspotCount;
	sPoints.reserve(aHotspotsCount);
	sPoints.resize(aHotspotsCount);
	sRequestedSets.clearAndResize(InputMap::hotspotSetCount());
	sActiveSets.clearAndResize(InputMap::hotspotSetCount());
	sLastTargetSize.cx = 0;
	sLastTargetSize.cy = 0;
	sNewTasks.set();
}


void cleanup()
{
	sActiveSets.clear();
	sPoints.clear();
}


void update()
{
	// Check for state changes that could trigger new tasks
	const SIZE& aTargetSize = WindowManager::overlayTargetSize();
	if( aTargetSize.cx != sLastTargetSize.cx ||
		aTargetSize.cy != sLastTargetSize.cy )
	{
		sLastTargetSize.cx = aTargetSize.cx;
		sLastTargetSize.cy = aTargetSize.cy;
		sNewTasks.set(eTask_TargetSize);
		sNewTasks.set(eTask_AddToGrid);
		sNewTasks.set(eTask_BeginSearch);
	}
	const Hotspot& aCursorPos =
		InputMap::getHotspot(eSpecialHotspot_LastCursorPos);
	if( !(sLastCursorPos == aCursorPos) )
	{
		sLastCursorPos = aCursorPos;
		sNormalizedCursorPos = WindowManager::hotspotToOverlayPos(aCursorPos);
		const int aScaleFactor = max(sLastTargetSize.cx, sLastTargetSize.cy);
		if( aScaleFactor > 0 )
		{
			sNormalizedCursorPos.x =
				(sNormalizedCursorPos.x + 1) *
				kNormalizedTargetSize / aScaleFactor;
			sNormalizedCursorPos.y =
				(sNormalizedCursorPos.y + 1) *
				kNormalizedTargetSize / aScaleFactor;
		}
		sNewTasks.set(eTask_BeginSearch);
	}

	// Continue progress on any current tasks
	processTasks();
}


void setEnabledHotspotSets(const BitVector<>& theHotspotSets)
{
	if( sRequestedSets != theHotspotSets )
	{
		sRequestedSets = theHotspotSets;
		sNewTasks.set(eTask_ActiveSets);
	}
}


u16 getNextHotspotInDir(ECommandDir theDirection)
{
	// Abort "next hotspot" tasks in all directions besides requested
	BitArray<eTask_Num> abortedTasks; abortedTasks.reset();
	if( theDirection != eCmdDir_L && sNewTasks.test(eTask_NextLeft) )
	{
		sNewTasks.reset(eTask_NextLeft);
		abortedTasks.set(eTask_NextLeft);
	}
	if( theDirection != eCmdDir_R && sNewTasks.test(eTask_NextRight) )
	{
		sNewTasks.reset(eTask_NextRight);
		abortedTasks.set(eTask_NextRight);
	}
	if( theDirection != eCmdDir_U && sNewTasks.test(eTask_NextUp) )
	{
		sNewTasks.reset(eTask_NextUp);
		abortedTasks.set(eTask_NextUp);
	}
	if( theDirection != eCmdDir_D && sNewTasks.test(eTask_NextDown) )
	{
		sNewTasks.reset(eTask_NextDown);
		abortedTasks.set(eTask_NextDown);
	}

	// Complete all other tasks to get desired answer
	while(sTaskInProgress != eTask_None || sNewTasks.any())
		processTasks();

	// Restore aborted tasks
	sNewTasks |= abortedTasks;

	// Return found result
	return sNextHotspotInDir[theDirection];
}

} // HotspotMap
