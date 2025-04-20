//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HotspotMap.h"

#include "InputMap.h"
#include "WindowManager.h"

namespace HotspotMap
{

// Uncomment this to print details about hotspot searches to debug window
//#define HOTSPOT_MAP_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
// This value won't overflow a u32 when used with dist formula
kNormalizedTargetSize = 0x7FFF,
// >> amount to convert kNormalizedTargetSize to kGridSize
kNormalizedToGridShift = 12, // 0x7FFF >> 12 = 7 = 8x8 grid
// Grid cells per axis
kGridSize = (kNormalizedTargetSize >> kNormalizedToGridShift) + 1,
// Size of each grid cell
kGridCellSize = (kNormalizedTargetSize + 1) / kGridSize,
// Maximum (normalized) distance cursor can jump from current position
kMaxJumpDist = 0x1400,
kMaxJumpDistSquared = kMaxJumpDist * kMaxJumpDist,
// Maximum grid squares in each axis to check
kMaxCheckGridCellsPerAxis = ((kMaxJumpDist * 2 - 1) / kGridCellSize) + 2,
kMaxCheckGridCells = kMaxCheckGridCellsPerAxis * kMaxCheckGridCellsPerAxis,
// If point is too close, jump FROM it rather than to it
kMinJumpDist = 0x0100,
kMinJumpDistSquared = kMinJumpDist * kMinJumpDist,
// If perpendicular weight exceeds this amount, don't jump to it
kMaxPerpendicularWeight = 0x07FF,
};

// Higher number = prioritize straighter lines over raw distance
const int /*float*/ kPerpendicularWeightMult = 2;

enum ETask
{
	eTask_TargetSize,
	eTask_ActiveArrays,
	eTask_AddToGrid,
	eTask_BeginSearch,
	eTask_FetchFromGrid,
	eTask_NextInDir,
	// Next 7 are remaining dirs in order from ECommandDir

	eTask_Num = eTask_NextInDir + eCmd8Dir_Num,
	eTask_None = eTask_Num,
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
static std::vector<GridPos> sFetchGrid(kMaxCheckGridCells);
static std::vector<u16> sCandidates;
static std::vector<Links> sLinkMaps;
static u16 sNextHotspotInDir[eCmd8Dir_Num] = { 0 };
static double sLastUIScale = 1.0;
static SIZE sLastTargetSize;
static Hotspot sLastCursorPos;
static POINT sNormalizedCursorPos;
static BitArray<eTask_Num> sNewTasks;
static ETask sCurrentTask = eTask_None;
static int sTaskProgress = 0;
static u32 sBestCandidateWeight = 0xFFFFFFFF;
static u16 sIgnorePointInSearch = 0;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef HOTSPOT_MAP_DEBUG_PRINT
#define mapDebugPrint(...) debugPrint("HotspotMap: " __VA_ARGS__)
#else
#define mapDebugPrint(...) ((void)0)
#endif

static void processTargetSizeTask()
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
		sCurrentTask = eTask_None;
}


static void processActiveArraysTask()
{
	const size_t aHotspotArrayCount = InputMap::hotspotArrayCount();
	while(sTaskProgress < aHotspotArrayCount)
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
			sNewTasks.set(eTask_BeginSearch);
			sNewTasks.set(eTask_FetchFromGrid);
			for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
				sNewTasks.set(eTask_NextInDir + aDir);
			break;
		}
	}

	if( sTaskProgress >= aHotspotArrayCount )
		sCurrentTask = eTask_None;
}


static void processAddToGridTask()
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

	while(sTaskProgress < sPoints.size())
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
		sCurrentTask = eTask_None;
}


static void processBeginSearchTask()
{
	if( sTaskProgress == 0 )
	{
		// This task will restart every time the cursor moves at all,
		// meaning could be repeated every frame for a while, so do the
		// bare minimum in this first step.
		sCandidates.clear();
		sFetchGrid.clear();
		for(int i = 0; i < eCmd8Dir_Num; ++i)
			sNextHotspotInDir[i] = 0;
		++sTaskProgress;
		return;
	}

	const u32 aMinGridX = u32(clamp(sNormalizedCursorPos.x - kMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	const u32 aMinGridY = u32(clamp(sNormalizedCursorPos.y - kMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	const u32 aMaxGridX = u32(clamp(sNormalizedCursorPos.x + kMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	const u32 aMaxGridY = u32(clamp(sNormalizedCursorPos.y + kMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	GridPos aGridPos = GridPos();
	for(aGridPos.x = aMinGridX; aGridPos.x <= aMaxGridX; ++aGridPos.x)
	{
		for(aGridPos.y = aMinGridY; aGridPos.y <= aMaxGridY; ++aGridPos.y)
			sFetchGrid.push_back(aGridPos);
	}
	sCurrentTask = eTask_None;
}


static void processFetchFromGridTask()
{
	if( sTaskProgress >= sFetchGrid.size() )
	{
		sCurrentTask = eTask_None;
		return;
	}
	const u32 aGridX = sFetchGrid[sTaskProgress].x;
	const u32 aGridY = sFetchGrid[sTaskProgress].y;
	DBG_ASSERT(aGridX < kGridSize);
	DBG_ASSERT(aGridY < kGridSize);
	mapDebugPrint(
		"Searching grid cell %d x %d for candidates\n",
		aGridX, aGridY);

	for(size_t i = 0; i < sActiveGrid[aGridX][aGridY].size(); ++i)
	{
		const u16 aPointIdx = sActiveGrid[aGridX][aGridY][i];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		const int aDeltaX = aPoint.x - sNormalizedCursorPos.x;
		const int aDeltaY = aPoint.y - sNormalizedCursorPos.y;
		const u32 aDist = (aDeltaX * aDeltaX) + (aDeltaY * aDeltaY);
		if( aDist > kMinJumpDistSquared && aDist < kMaxJumpDistSquared )
			sCandidates.push_back(aPointIdx);
	}

	if( ++sTaskProgress >= sFetchGrid.size() )
		sCurrentTask = eTask_None;
}


static void processNextInDirTask(ECommandDir theDir)
{
	if( sTaskProgress == 0 )
		sBestCandidateWeight = 0xFFFFFFFF;

	while(sTaskProgress < sCandidates.size())
	{
		const u16 aPointIdx = sCandidates[sTaskProgress++];
		if( aPointIdx == sIgnorePointInSearch )
			continue;

		const TrackedPoint& aPoint = sPoints[aPointIdx];
		const long dx = aPoint.x - sNormalizedCursorPos.x;
		const long dy = aPoint.y - sNormalizedCursorPos.y;
		bool inAllowedDir = false;
		switch(theDir)
		{
		case eCmd8Dir_L:  inAllowedDir = dx < 0;			break;
		case eCmd8Dir_R:  inAllowedDir = dx > 0;			break;
		case eCmd8Dir_U:  inAllowedDir = dy < 0;			break;
		case eCmd8Dir_D:  inAllowedDir = dy > 0;			break;
		case eCmd8Dir_UL: inAllowedDir = dx < 0 && dy < 0;	break;
		case eCmd8Dir_UR: inAllowedDir = dx > 0 && dy < 0;	break;
		case eCmd8Dir_DL: inAllowedDir = dx < 0 && dy > 0;	break;
		case eCmd8Dir_DR: inAllowedDir = dx > 0 && dy > 0;	break;
		}
		if( !inAllowedDir )
			continue;

		u32 aWeight;
		switch(theDir)
		{
		case eCmd8Dir_L:  aWeight = -dx;					break;
		case eCmd8Dir_R:  aWeight = dx;						break;
		case eCmd8Dir_U:  aWeight = -dy;					break;
		case eCmd8Dir_D:  aWeight = dy;						break;
		case eCmd8Dir_UL: aWeight = (-dx - dy) / 2;			break;
		case eCmd8Dir_UR: aWeight = (dx - dy) / 2;			break;
		case eCmd8Dir_DL: aWeight = (-dx + dy) / 2;			break;
		case eCmd8Dir_DR: aWeight = (dx + dy) / 2;			break;
		}
		if( aWeight <= 0 )
			continue;

		u32 anAltWeight;
		switch(theDir)
		{
		case eCmd8Dir_L:  anAltWeight = abs(dy);			break;
		case eCmd8Dir_R:  anAltWeight = abs(dy);			break;
		case eCmd8Dir_U:  anAltWeight = abs(dx);			break;
		case eCmd8Dir_D:  anAltWeight = abs(dx);			break;
		case eCmd8Dir_UL: anAltWeight = abs(dx - dy) / 2;	break;
		case eCmd8Dir_UR: anAltWeight = abs(dx + dy) / 2;	break;
		case eCmd8Dir_DL: anAltWeight = abs(-dx - dy) / 2;	break;
		case eCmd8Dir_DR: anAltWeight = abs(dy - dx) / 2;	break;
		}
		if( anAltWeight >= aWeight * 2 ||
			anAltWeight > kMaxPerpendicularWeight )
			continue;

		anAltWeight *= kPerpendicularWeightMult;
		aWeight += anAltWeight;
		if( aWeight < sBestCandidateWeight )
		{
			sNextHotspotInDir[theDir] = aPointIdx;
			sBestCandidateWeight = aWeight;
		}
		break;
	}

	if( sTaskProgress >= sCandidates.size() )
	{
		sCurrentTask = eTask_None;
		if( sNextHotspotInDir[theDir] != 0 && !sIgnorePointInSearch )
		{
			mapDebugPrint("%s hotspot chosen - #%d\n",
				theDir == eCmd8Dir_L	? "Left":
				theDir == eCmd8Dir_R	? "Right":
				theDir == eCmd8Dir_U	? "Up":
				theDir == eCmd8Dir_D	? "Down":
				theDir == eCmd8Dir_UL	? "UpLeft":
				theDir == eCmd8Dir_UR	? "UpRight":
				theDir == eCmd8Dir_DL	? "DownLeft":
				/*eCmd8Dir_DR*/			  "DownRight",
				sNextHotspotInDir[theDir]);
		}
	}
}


static void processTasks()
{
	// Start new task or restart current if needed
	const int aNewTask = sNewTasks.firstSetBit();
	if( aNewTask <= sCurrentTask && aNewTask < eTask_Num )
	{
		// Save incomplete task for later
		if( sCurrentTask < eTask_Num )
			sNewTasks.set(sCurrentTask);
		sCurrentTask = ETask(aNewTask);
		sTaskProgress = 0;
		sNewTasks.reset(aNewTask);
	}

	switch(sCurrentTask)
	{
	case eTask_None:			break;
	case eTask_TargetSize:		processTargetSizeTask();	break;
	case eTask_ActiveArrays:	processActiveArraysTask();	break;
	case eTask_AddToGrid:		processAddToGridTask();		break;
	case eTask_BeginSearch:		processBeginSearchTask();	break;
	case eTask_FetchFromGrid:	processFetchFromGridTask();	break;
	default:
		DBG_ASSERT(sCurrentTask >= eTask_NextInDir);
		DBG_ASSERT(sCurrentTask < eTask_Num);
		processNextInDirTask(ECommandDir(sCurrentTask - eTask_NextInDir));
		break;
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
	sLinkMaps.reserve(aHotspotArraysCount);
	sLinkMaps.resize(aHotspotArraysCount);
	sLastTargetSize = WindowManager::overlayTargetSize();
	sLastUIScale = gUIScale;
	sNewTasks.set();
}


void cleanup()
{
	sRequestedArrays.clear();
	sActiveArrays.clear();
	sPoints.clear();
	sLinkMaps.clear();
	sCandidates.clear();
	sNewTasks.reset();
	sCurrentTask = eTask_None;
	sTaskProgress = 0;
	for(size_t x = 0; x < kGridSize; ++x)
	{
		for(size_t y = 0; y < kGridSize; ++y)
			sActiveGrid[x][y].clear();
	}
}


void update()
{
	// Check for state changes that could trigger new tasks
	const SIZE& aTargetSize = WindowManager::overlayTargetSize();
	if( aTargetSize.cx != sLastTargetSize.cx ||
		aTargetSize.cy != sLastTargetSize.cy ||
		gUIScale != sLastUIScale )
	{
		sLastTargetSize.cx = aTargetSize.cx;
		sLastTargetSize.cy = aTargetSize.cy;
		sLastUIScale = gUIScale;
		sNewTasks.set(eTask_TargetSize);
		sNewTasks.set(eTask_AddToGrid);
		sNewTasks.set(eTask_BeginSearch);
		sNewTasks.set(eTask_FetchFromGrid);
		for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
			sNewTasks.set(eTask_NextInDir + aDir);
	}
	const Hotspot& aCursorPos =
		InputMap::getHotspot(eSpecialHotspot_LastCursorPos);
	if( !(sLastCursorPos == aCursorPos) || sNewTasks.test(eTask_TargetSize) )
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
		sNewTasks.set(eTask_FetchFromGrid);
		for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
			sNewTasks.set(eTask_NextInDir + aDir);
	}

	// Continue progress on any current tasks
	processTasks();
}


void reloadPositions()
{
	sNewTasks.set();
	for(size_t i = 0; i < sLinkMaps.size(); ++i)
		sLinkMaps[i].clear();
}


void setEnabledHotspotArrays(const BitVector<>& theHotspotArrays)
{
	if( sRequestedArrays != theHotspotArrays )
	{
		sRequestedArrays = theHotspotArrays;
		sNewTasks.set(eTask_ActiveArrays);
		if( gHotspotsGuideMode == eHotspotGuideMode_Showing )
			gHotspotsGuideMode = eHotspotGuideMode_Redraw;
	}
}


const BitVector<>& getEnabledHotspotArrays()
{
	return sRequestedArrays;
}


u16 getNextHotspotInDir(ECommandDir theDirection)
{
	if( sPoints.empty() || sRequestedArrays.none() )
		return 0;

	// Abort _nextInDir tasks in all directions besides requested
	BitArray<eTask_Num> abortedTasks; abortedTasks.reset();
	for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
	{
		if( theDirection != aDir &&
			sNewTasks.test(eTask_NextInDir + aDir) )
		{
			sNewTasks.reset(eTask_NextInDir + aDir);
			abortedTasks.set(eTask_NextInDir + aDir);
		}
	}

	// Complete all other tasks to get desired answer
	while(sCurrentTask != eTask_None || sNewTasks.any())
		processTasks();

	// Restore aborted tasks
	sNewTasks |= abortedTasks;

	// Return found result
	return sNextHotspotInDir[theDirection];
}


const Links& getLinks(u16 theArrayID)
{
	DBG_ASSERT(theArrayID < sLinkMaps.size());
	if( !sLinkMaps[theArrayID].empty() )
		return sLinkMaps[theArrayID];

	// Generate links
	mapDebugPrint("Generating links for hotspot array '%s'\n",
		InputMap::hotspotArrayLabel(theArrayID).c_str());
	const u16 aFirstHotspot = InputMap::firstHotspotInArray(theArrayID);
	const u16 aLastHotspot = InputMap::lastHotspotInArray(theArrayID);
	const u16 aNodeCount = aLastHotspot - aFirstHotspot + 1;
	sLinkMaps[theArrayID].resize(aNodeCount);

	// Backup some state of normal Hotspot Map search around cursor
	const BitVector<> oldRequestedArrays = sRequestedArrays;
	const POINT oldNormalizedCursorPos = sNormalizedCursorPos;

	// Setup overall hotspot map as having just this array active
	sRequestedArrays.reset(); sRequestedArrays.set(theArrayID);
	sNewTasks.set(eTask_ActiveArrays);
	while(sCurrentTask != eTask_None || sNewTasks.test(eTask_ActiveArrays))
		processTasks();

	// Don't use the grid since want full range - manually generate candidates
	sCandidates.clear();
	sCandidates.reserve(aNodeCount);
	sNewTasks.reset();
	for(u16 i = 0; i < sPoints.size(); ++i)
	{
		if( sPoints[i].enabled )
			sCandidates.push_back(i);
	}

	// Check each hotspot/node for where mouse would go next
	for(u16 aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
	{
		HotspotLinkNode& aNode = sLinkMaps[theArrayID][aNodeIdx];
		// Make sure not to return the origin hotspot itself
		sIgnorePointInSearch = aFirstHotspot + aNodeIdx;
		sNormalizedCursorPos.x = sPoints[sIgnorePointInSearch].x;
		sNormalizedCursorPos.y = sPoints[sIgnorePointInSearch].y;
		for(int aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
		{
			sNextHotspotInDir[aDir] = 0;
			sNewTasks.set(eTask_NextInDir + aDir);
		}
		while(sCurrentTask != eTask_None || sNewTasks.any())
			processTasks();
		for(int i = 0; i < eCmd8Dir_Num; ++i)
		{
			aNode.next[i] = aNodeIdx;
			if( sNextHotspotInDir[i] == 0 )
				aNode.edge[i] = true;
			else
				aNode.next[i] = sNextHotspotInDir[i] - aFirstHotspot;
		}
	}
	// Add wrap-around options for edge nodes
	for(u16 aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
	{
		HotspotLinkNode& aNode = sLinkMaps[theArrayID][aNodeIdx];
		for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
		{
			if( !aNode.edge[aDir] )
				continue;
			ECommandDir anOppDir =
				aDir == eCmd8Dir_L	? eCmd8Dir_R:
				aDir == eCmd8Dir_R	? eCmd8Dir_L:
				aDir == eCmd8Dir_U	? eCmd8Dir_D:
				aDir == eCmd8Dir_D	? eCmd8Dir_U:
				aDir == eCmd8Dir_UL	? eCmd8Dir_DR:
				aDir == eCmd8Dir_UR	? eCmd8Dir_DL:
				aDir == eCmd8Dir_DL	? eCmd8Dir_UR:
				/*eCmd8Dir_DR*/		  eCmd8Dir_UL;
			u16 aWrapNode = aNode.next[anOppDir];
			while(!sLinkMaps[theArrayID][aWrapNode].edge[anOppDir])
				aWrapNode = sLinkMaps[theArrayID][aWrapNode].next[anOppDir];
			aNode.next[aDir] = aWrapNode;
		}
	}

	// Restore normal functionality of searching around mouse cursor
	sRequestedArrays = oldRequestedArrays;
	sNewTasks.set(eTask_ActiveArrays);
	sNormalizedCursorPos = oldNormalizedCursorPos;
	sIgnorePointInSearch = 0;
	sNewTasks.set(eTask_BeginSearch);
	sNewTasks.set(eTask_FetchFromGrid);
	for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
		sNewTasks.set(eTask_NextInDir + aDir);

	return sLinkMaps[theArrayID];
}


EResult stringToHotspot(std::string& theString, Hotspot& out)
{
	EResult aResult = stringToCoord(theString, out.x);
	if( aResult != eResult_Ok )
		return aResult;
	aResult = stringToCoord(theString, out.y);
	if( aResult == eResult_Empty ) // has first but empty second == malformed
		aResult = eResult_Malformed;
	return aResult;
}


EResult stringToCoord(std::string& theString,
					  Hotspot::Coord& out,
					  std::string* theValidatedString)
{
	EResult result = eResult_Empty;
	// This function also removes the coordinate from start of string
	out = Hotspot::Coord();
	if( theValidatedString )
		theValidatedString->clear();
	if( theString.empty() )
		return result;

	enum EState
	{
		eState_Prefix,		// Checking for C/R/B in CX+10, R-8, B - 5, etc
		eState_PrefixEnd,	// Checking for X/Y in CX/CY or 'eft' in 'Left', etc
		eState_Numerator,	// Checking for 50%, 10. in 10.5%, 0. in 0.75, etc
		eState_Denominator,	// Checking for 5 in 0.5, 5% in 10.5%, etc
		eState_OffsetSign,	// Checking for -/+ in 50%+10, R-8, B - 5, etc
		eState_OffsetSpace,	// Checking for start of offset number after sign
		eState_OffsetNumber,// Checking for 10 in 50% + 10, CX+10, R-10, etc
		eState_TrailSpace,	// Check for final , or 'x' after end of coordinate
	} aState = eState_Prefix;

	u32 aNumerator = 0;
	u32 aDenominator = 0;
	u32 anOffset = 0;
	bool done = false;
	bool isOffsetNegative  = false;
	size_t aCharPos = 0;
	size_t aValidCharCount = 0;
	char c = theString[aCharPos];
	result = eResult_Ok;

	out.offset = 0;
	while(!done && result == eResult_Ok)
	{
		switch(c)
		{
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			switch(aState)
			{
			case eState_Prefix:
				aState = eState_Numerator;
				// fall through
			case eState_Numerator:
			case eState_Denominator:
				aDenominator *= 10;
				aNumerator *= 10;
				aNumerator += u32(c - '0');
				if( aNumerator > 0x7FFFFFFF )
					result = eResult_Overflow;
				else if( aDenominator > 0x7FFFFFFF )
					result = eResult_Overflow;
				else
					aValidCharCount = aCharPos + 1;
				break;
			case eState_OffsetSign:
				aState = EState(aState + 1);
				// fall through
			case eState_OffsetSpace:
				aState = EState(aState + 1);
				// fall through
			case eState_OffsetNumber:
				anOffset *= 10;
				anOffset += u32(c - '0');
				if( anOffset > 0x7FFF )
					result = eResult_Overflow;
				else if( anOffset || !isOffsetNegative )
					aValidCharCount = aCharPos + 1;
				break;
			default:
				return eResult_Malformed;
			}
			break;
		case '-':
		case '+':
			switch(aState)
			{
			case eState_Prefix:
				// Skipping directly to offset
				aDenominator = 1;
				// fall through
			case eState_PrefixEnd:
			case eState_Denominator:
			case eState_OffsetSign:
				isOffsetNegative = (c == '-');
				aState = eState_OffsetSpace;
				break;
			case eState_OffsetSpace:
				// Allow flipping the offset sign once by using a '-' just
				// before the actual number starts, like "+ -5" or "- -10"
				if( c == '-' &&
					aCharPos < theString.size()-1 &&
					theString[aCharPos+1] >= '0' &&
					theString[aCharPos+1] <= '9' )
				{
					isOffsetNegative = !isOffsetNegative;
					aState = eState_OffsetNumber;
				}
				else
				{
					result = eResult_Malformed;
				}
				break;
			case eState_Numerator:
				anOffset = aNumerator;
				aNumerator = 0;
				aDenominator = 1;
				// fall through
			case eState_OffsetNumber:
			case eState_TrailSpace:
				// Additional offset
				out.offset +=
					isOffsetNegative ? -s16(anOffset) : s16(anOffset);
				anOffset = 0;
				isOffsetNegative = (c == '-');
				aState = eState_OffsetSpace;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case '.':
			switch(aState)
			{
			case eState_Prefix:
			case eState_Numerator:
				aState = eState_Denominator;
				aDenominator = 1;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case '%':
		case 'p':
			switch(aState)
			{
			case eState_PrefixEnd:
				aValidCharCount = aCharPos + 1;
				break;
			case eState_Numerator:
			case eState_Denominator:
				if( !aDenominator ) aDenominator = 1;
				aDenominator *= 100; // Convert 50% to 0.5
				aState = eState_OffsetSign;
				aValidCharCount = aCharPos + 1;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case 'l': case 'L': // aka "Left"
		case 't': case 'T': // aka "Top"
			switch(aState)
			{
			case eState_PrefixEnd:
				aValidCharCount = aCharPos + 1;
				break;
			case eState_Prefix:
				aNumerator = 0;
				aDenominator = 1;
				aState = eState_PrefixEnd;
				aValidCharCount = aCharPos + 1;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case 'r': case 'R': case 'w': case 'W': // aka "Right" or "Width"
		case 'b': case 'B': case 'h': case 'H':// aka "Bottom" or "Height"
			switch(aState)
			{
			case eState_PrefixEnd:
				aValidCharCount = aCharPos + 1;
				break;
			case eState_Prefix:
				aNumerator = 1;
				aDenominator = 1;
				aState = eState_PrefixEnd;
				aValidCharCount = aCharPos + 1;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case 'c': case 'C': // aka "Center"
			switch(aState)
			{
			case eState_PrefixEnd:
				aValidCharCount = aCharPos + 1;
				break;
			case eState_Prefix:
				aNumerator = 1;
				aDenominator = 2;
				aState = eState_PrefixEnd;
				aValidCharCount = aCharPos + 1;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case 'x': case 'X': case ',':
			switch(aState)
			{
			case eState_PrefixEnd:
				if( c == ',' )
					done = true;
				else
					aValidCharCount = aCharPos + 1; // part of CX prefix
				break;
			case eState_Numerator:
				anOffset = aNumerator;
				aNumerator = 0;
				aDenominator = 1;
				done = true;
				break;
			case eState_Denominator:
			case eState_OffsetSign:
			case eState_OffsetNumber:
			case eState_TrailSpace:
				// Assume marks end of this coordinate
				done = true;
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case 'y': case 'Y':
			switch(aState)
			{
			case eState_PrefixEnd:
				aValidCharCount = aCharPos + 1; // part of CY prefix
				break;
			default:
				result = eResult_Malformed;
			}
			break;
		case ' ':
			switch(aState)
			{
			case eState_OffsetSign:
			case eState_OffsetSpace:
			case eState_Prefix:
			case eState_TrailSpace:
				// Allowed whitespace, ignore
				break;
			case eState_PrefixEnd:
				aState = eState_OffsetSign;
				break;
			case eState_Denominator:
				aState = eState_OffsetSign;
				break;
			case eState_Numerator:
				anOffset = aNumerator;
				aNumerator = 0;
				aDenominator = 1;
				// fall through
			case eState_OffsetNumber:
				aState = eState_TrailSpace;
				break;
			}
			break;
		case '*':
			// Offset scale indicator - treat as eol but return the '*'
			--aCharPos;
			done = true;
			break;
		default:
			if( aState == eState_PrefixEnd )
				aValidCharCount = aCharPos + 1;
			else
				result = eResult_Malformed;
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
		anOffset = aNumerator;
		aNumerator = 0;
		aDenominator = 1;
	}

	if( aNumerator > aDenominator )
		result = eResult_Overflow;

	if( theValidatedString )
		*theValidatedString = trim(theString.substr(0, aValidCharCount));

	if( result != eResult_Ok )
		return result;

	out.anchor = ratioToU16(aNumerator, aDenominator);
	out.offset +=
		isOffsetNegative ? -s16(anOffset) : s16(anOffset);

	// Remove processed section from start of string
	theString = theString.substr(aCharPos);

	return result;
}

std::string hotspotToString(
	const Hotspot& theHotspot,
	EHotspotNamingConvention theConvention)
{
	std::string result;
	EHotspotNamingConvention aCoordConvention;
	switch(theConvention)
	{
	case eHNC_XY:		aCoordConvention = eHNC_X; break;
	case eHNC_XY_Off:	aCoordConvention = eHNC_X_Off; break;
	case eHNC_WH:		aCoordConvention = eHNC_W; break;
	default: DBG_ASSERT(false);
	}
	result = coordToString(theHotspot.x, aCoordConvention) + ", ";
	switch(theConvention)
	{
	case eHNC_XY:		aCoordConvention = eHNC_Y; break;
	case eHNC_XY_Off:	aCoordConvention = eHNC_Y_Off; break;
	case eHNC_WH:		aCoordConvention = eHNC_H; break;
	default: DBG_ASSERT(false);
	}
	result += coordToString(theHotspot.x, aCoordConvention);
	return result;
}


std::string coordToString(
	const Hotspot::Coord& theCoord,
	EHotspotNamingConvention theConvention)
{
	const u16 kPercentResolution = 10000; // Must be evenly divisible by 10
	std::string result;
	switch(theCoord.anchor)
	{
	case 0:
		// Leave blank
		break;
	case 0x8000:
		switch(theConvention)
		{
		case eHNC_X: case eHNC_W: case eHNC_X_Off:
			result = "CX";
			break;
		case eHNC_Y: case eHNC_H: case eHNC_Y_Off:
			result = "CY";
			break;
		}
		break;
	case 0xFFFF:
		switch(theConvention)
		{
		case eHNC_X: case eHNC_W: case eHNC_X_Off:
			result = "R";
			break;
		case eHNC_Y: case eHNC_H: case eHNC_Y_Off:
			result = "B";
			break;
		}
		break;
	}

	if( result.empty() &&
		(theCoord.anchor != 0 || theConvention == eHNC_Num) )
	{// Convert to a XX.XXX% string
		u32 anInt = u16ToRangeVal(theCoord.anchor, kPercentResolution);
		if( ratioToU16(anInt, kPercentResolution) < theCoord.anchor )
			++anInt;
		result = toString(anInt * 100 / kPercentResolution);
		anInt = anInt * 100 % kPercentResolution;
		if( anInt )
		{
			result += ".";
			for(int i = 0; i < 3 && anInt != 0; ++i)
			{
				anInt *= 10;
				result += toString(anInt / kPercentResolution);
				anInt %= kPercentResolution;
			}
			while(result[result.size()-1] == '0')
				result.resize(result.size()-1);
			if( result[result.size()-1] == '.' )
				result.resize(result.size()-1);
		}
		result += "%";
	}

	if( theCoord.offset > 0 &&
		(!result.empty() ||
		 theConvention == eHNC_X_Off ||
		 theConvention == eHNC_Y_Off) )
	{
		result += "+";
	}
	if( theCoord.offset != 0 )
		result += toString(theCoord.offset);
	if( result.empty() )
	{
		switch(theConvention)
		{
		case eHNC_X:
			result = "L";
			break;
		case eHNC_Y:
			result = "T";
			break;
		case eHNC_W:
		case eHNC_H:
			result = "0";
			break;
		case eHNC_X_Off:
		case eHNC_Y_Off:
			result = "+0";
			break;
		}
	}
	return result;
}

#undef mapDebugPrint
#undef HOTSPOT_MAP_DEBUG_PRINT

} // HotspotMap
