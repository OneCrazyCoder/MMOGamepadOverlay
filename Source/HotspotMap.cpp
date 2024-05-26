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
	eTask_ActiveArrays,
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

static BitVector<> sRequestedArrays;
static BitVector<> sActiveArrays;
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
		const u16 aHotspotID = u16(sTaskProgress);
		const Hotspot& aHotspot = InputMap::getHotspot(aHotspotID);
		const POINT& anOverlayPos =
			WindowManager::hotspotToOverlayPos(aHotspot);
		const int aScaleFactor = max(sLastTargetSize.cx, sLastTargetSize.cy);
		if( aScaleFactor > 0 )
		{
			sPoints[sTaskProgress].x = min(kNormalizedTargetSize, 
				(anOverlayPos.x + 1) * kNormalizedTargetSize / aScaleFactor);
			sPoints[sTaskProgress].y = min(kNormalizedTargetSize,
				(anOverlayPos.y + 1) * kNormalizedTargetSize / aScaleFactor);
			DBG_ASSERT(sPoints[sTaskProgress].y <= kNormalizedTargetSize);
		}
		mapDebugPrint(
			"Updating Hotspot #%d normalized position to %d x %d\n",
			sTaskProgress, sPoints[sTaskProgress].x, sPoints[sTaskProgress].y);
	}

	if( ++sTaskProgress >= sPoints.size() )
		sTaskInProgress = eTask_None;
}


void processActiveArraysTask()
{
	const size_t aHotspotArrayCount = InputMap::hotspotArrayCount();
	while( sTaskProgress < aHotspotArrayCount )
	{
		const u16 anArray = sTaskProgress++;
		bool needChangeMade = false;
		bool enableHotspots = false;
		if( sRequestedArrays.test(anArray) && !sActiveArrays.test(anArray) )
		{
			mapDebugPrint(
				"Enabling hotspots in Hotspot Array '%s'\n",
				InputMap::hotspotArrayLabel(anArray).c_str());
			needChangeMade = true;
			enableHotspots = true;
		}
		else if( !sRequestedArrays.test(anArray) &&
				 sActiveArrays.test(anArray) )
		{
			mapDebugPrint(
				"Disabling hotspots in Hotspot Array '%s'\n",
				InputMap::hotspotArrayLabel(anArray).c_str());
			needChangeMade = true;
			enableHotspots = false;
		}
		if( needChangeMade )
		{
			const size_t aFirstHotspot =
				InputMap::firstHotspotInArray(anArray);
			const size_t aLastHotspot =
				InputMap::lastHotspotInArray(anArray);
			for(size_t i = aFirstHotspot; i <= aLastHotspot; ++i)
				sPoints[i].enabled = enableHotspots;
			sActiveArrays.set(anArray, enableHotspots);
			sNewTasks.set(eTask_AddToGrid);
			break;
		}
	}

	if( sTaskProgress >= aHotspotArrayCount )
		sTaskInProgress = eTask_None;
}


void processAddToGridTask()
{
	if( sTaskProgress == 0 )
	{
		if( sActiveArrays.any() )
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
		DBG_ASSERT(aGridX < kGridSize);
		DBG_ASSERT(aGridY < kGridSize);
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
			sNextHotspotInDir[eCmdDir_L] = aPointIdx;
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
				sNextHotspotInDir[eCmdDir_L]);
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
			sNextHotspotInDir[eCmdDir_R] = aPointIdx;
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
				sNextHotspotInDir[eCmdDir_R]);
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
			sNextHotspotInDir[eCmdDir_U] = aPointIdx;
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
				sNextHotspotInDir[eCmdDir_U]);
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
			sNextHotspotInDir[eCmdDir_D] = aPointIdx;
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
				sNextHotspotInDir[eCmdDir_D]);
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
	case eTask_ActiveArrays:	processActiveArraysTask();	break;
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
	const u16 aHotspotsCount = InputMap::hotspotCount();
	const u16 aHotspotArraysCount = InputMap::hotspotArrayCount();
	sPoints.reserve(aHotspotsCount);
	sPoints.resize(aHotspotsCount);
	sRequestedArrays.clearAndResize(aHotspotArraysCount);
	sActiveArrays.clearAndResize(aHotspotArraysCount);
	sLastTargetSize.cx = 0;
	sLastTargetSize.cy = 0;
	sNewTasks.set();
}


void cleanup()
{
	sActiveArrays.clear();
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


void setEnabledHotspotArrays(const BitVector<>& theHotspotArrays)
{
	if( sRequestedArrays != theHotspotArrays )
	{
		sRequestedArrays = theHotspotArrays;
		sNewTasks.set(eTask_ActiveArrays);
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


EResult stringToHotspot(std::string& theString, Hotspot& out)
{
	EResult aResult = stringToCoord(theString, out.x);
	if( aResult != eResult_Ok )
		return aResult;
	aResult = stringToCoord(theString, out.y);
	return aResult;
}


EResult stringToCoord(std::string& theString, Hotspot::Coord& out)
{
	// This function also removes the coordinate from start of string
	out = Hotspot::Coord();
	if( theString.empty() )
		return eResult_Empty;

	enum EMode
	{
		eMode_Prefix,		// Checking for C/R/B in CX+10, R-8, B - 5, etc
		eMode_PrefixEnd,	// Checking for X/Y in CX/CY or 'eft' in 'Left', etc
		eMode_Numerator,	// Checking for 50%, 10. in 10.5%, 0. in 0.75, etc
		eMode_Denominator,	// Checking for 5 in 0.5, 5% in 10.5%, etc
		eMode_OffsetSign,	// Checking for -/+ in 50%+10, R-8, B - 5, etc
		eMode_OffsetSpace,	// Optional space between -/+ and offset number
		eMode_OffsetNumber, // Checking for 10 in 50% + 10, CX+10, R-10, etc
		eMode_ScaledSign,	// Checking for -/+ in 50%+10-8, etc
		eMode_ScaledSpace,	// Optional space between -/+ and scaled number
		eMode_ScaledNumber, // Checking for 8 in 50% + 10 - 8, etc
	} aMode = eMode_Prefix;

	u32 aNumerator = 0;
	u32 aDenominator = 0;
	u32 aScaledOffset = 0;
	u32 aFixedOffset = 0;
	bool done = false;
	bool isScaledNegative  = false;
	bool isFixedNegative  = false;
	size_t aCharPos = 0;
	char c = theString[aCharPos];

	while(!done)
	{
		switch(c)
		{
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			switch(aMode)
			{
			case eMode_Prefix:
				aMode = eMode_Numerator;
				// fall through
			case eMode_Numerator:
			case eMode_Denominator:
				aDenominator *= 10;
				aNumerator *= 10;
				aNumerator += u32(c - '0');
				if( aNumerator > 0x7FFF )
					return eResult_Overflow;
				if( aDenominator > 0x7FFF )
					return eResult_Overflow;
				break;
			case eMode_OffsetSign:
			case eMode_ScaledSign:
				aMode = EMode(aMode + 1);
				// fall through
			case eMode_OffsetSpace:
			case eMode_ScaledSpace:
				aMode = EMode(aMode + 1);
				// fall through
			case eMode_OffsetNumber:
			case eMode_ScaledNumber:
				aScaledOffset *= 10;
				aScaledOffset += u32(c - '0');
				if( aScaledOffset > 0x7FFF )
					return eResult_Overflow;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case '-':
		case '+':
			switch(aMode)
			{
			case eMode_Prefix:
				// Skipping directly to offset
				aDenominator = 1;
				// fall through
			case eMode_PrefixEnd:
			case eMode_Denominator:
			case eMode_OffsetSign:
				isScaledNegative = (c == '-');
				aMode = eMode_OffsetSpace;
				break;
			case eMode_OffsetNumber:
			case eMode_ScaledSign:
				// Second offset specified
				isFixedNegative = isScaledNegative;
				isScaledNegative = (c == '-');
				aFixedOffset = aScaledOffset;
				aScaledOffset = 0;
				aMode = eMode_ScaledSpace;
				break;
			case eMode_Numerator:
				isScaledNegative = (c == '-');
				aFixedOffset = aNumerator;
				aNumerator = 0;
				aDenominator = 1;
				aMode = eMode_ScaledSpace;
			default:
				return eResult_Malformed;
			}
			break;
		case '.':
			switch(aMode)
			{
			case eMode_Prefix:
			case eMode_Numerator:
				aMode = eMode_Denominator;
				aDenominator = 1;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case '%':
		case 'p':
			switch(aMode)
			{
			case eMode_PrefixEnd:
				break;
			case eMode_Numerator:
			case eMode_Denominator:
				if( !aDenominator ) aDenominator = 1;
				aDenominator *= 100; // Convert 50% to 0.5
				aMode = eMode_OffsetSign;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case 'l': case 'L': // aka "Left"
		case 't': case 'T': // aka "Top"
			switch(aMode)
			{
			case eMode_PrefixEnd:
				break;
			case eMode_Prefix:
				aNumerator = 0;
				aDenominator = 1;
				aMode = eMode_PrefixEnd;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case 'r': case 'R': case 'w': case 'W': // aka "Right" or "Width"
		case 'b': case 'B': case 'h': case 'H':// aka "Bottom" or "Height"
			switch(aMode)
			{
			case eMode_PrefixEnd:
				break;
			case eMode_Prefix:
				aNumerator = 1;
				aDenominator = 1;
				aMode = eMode_PrefixEnd;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case 'c': case 'C': // aka "Center"
			switch(aMode)
			{
			case eMode_PrefixEnd:
				break;
			case eMode_Prefix:
				aNumerator = 1;
				aDenominator = 2;
				aMode = eMode_PrefixEnd;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case 'x': case 'X':
		case 'y': case 'Y':
		case ',':
			switch(aMode)
			{
			case eMode_PrefixEnd:
				if( c == ',' )
					done = true;
				// Ignore as part of prefix like CX or CY
				break;
			case eMode_Numerator:
				aScaledOffset = aNumerator;
				aNumerator = 0;
				aDenominator = 1;
				done = true;
				break;
			case eMode_Denominator:
			case eMode_OffsetSign:
			case eMode_OffsetNumber:
			case eMode_ScaledSign:
			case eMode_ScaledNumber:
				// Assume marks end of this coordinate
				done = true;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case ' ':
			switch(aMode)
			{
			case eMode_Prefix:
			case eMode_OffsetSign:
			case eMode_OffsetSpace:
			case eMode_ScaledSign:
			case eMode_ScaledSpace:
				// Allowed whitespace, ignore
				break;
			case eMode_Denominator:
				aMode = eMode_OffsetSign;
				break;
			case eMode_Numerator:
				aScaledOffset = aNumerator;
				aNumerator = 0;
				aDenominator = 1;
				// fall through
			case eMode_OffsetNumber:
				aMode = eMode_ScaledSign;
				break;
			case eMode_ScaledNumber:
				done = true;
				break;
			}
			break;
		default:
			if( aMode != eMode_PrefixEnd )
				return eResult_Malformed;
			break;
		}

		++aCharPos;
		if( !done )
		{
			if( aCharPos >= theString.size() )
				done = true;
			else
				c = theString[aCharPos];
		}
	}

	if( aDenominator == 0 )
	{
		aScaledOffset = aNumerator;
		aNumerator = 0;
		aDenominator = 1;
	}

	if( aNumerator >= aDenominator )
		out.anchor = 0xFFFF;
	else
		out.anchor = u16((aNumerator * 0x10000) / aDenominator);
	out.offset = s16(aFixedOffset);
	if( isFixedNegative )
		out.offset = -out.offset;
	out.scaled = s16(aScaledOffset);
	if( isScaledNegative )
		out.scaled = -out.scaled;

	// Remove processed section from start of string
	theString = theString.substr(aCharPos);

	return eResult_Ok;
}

} // HotspotMap
