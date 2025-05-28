//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HotspotMap.h"

#include "InputMap.h"
#include "Profile.h"
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
//kDefaultMaxJumpDist = 0x0200,//0x1400,
// If point is too close, jump FROM it rather than to it
kDefaultMinJumpDist = 0x0100,
// Max leeway in perpindicular direction to still count as "straight"
kMaxPerpDistForStraightLine = 0x0088,
// Max leeway for final "columns" step when making link maps
kMaxLinkMapColumnXDist = 0x0500,
};

// How much past base jump dest to search for a hotspot to jump to,
// as a multiplier of Mouse/DefaultHotspotDistance property
const float kDeviationRadiusMult = 0.75;
// Higher number = prioritize straighter lines over shorter distances
const float kPerpPenaltyMult = 1.25;
// These are used only for generating hotspot link maps, which must
// guarantee all points can be reached regardless of distance without
// interim hops so uses a different algorithm than basic jumps
// Higher number = must be further in X to make a left/right link
const float kMinSlopeForHorizLink = 0.9f;
// Higher number = allows for up/down link even when far in X
const float kMaxSlopeForVertLink = 1.2f;
// Higher number = prioritize straighter columns over Y distance
const int /*float*/ kColumnXDistPenaltyMult = 2;

enum ETask
{
	eTask_TargetSize,
	eTask_ActiveArrays,
	eTask_AddToGrid,
	eTask_BeginSearch,
	eTask_FetchFromGrid, // auto-set by _BeginSearch
	eTask_NextInDir,
	// Next 7 are remaining dirs in order from ECommandDir

	eTask_Num = eTask_NextInDir + eCmd8Dir_Num,
	eTask_None = eTask_Num,
};

#ifdef HOTSPOT_MAP_DEBUG_PRINT
#define mapDebugPrint(...) debugPrint("HotspotMap: " __VA_ARGS__)
#else
#define mapDebugPrint(...) ((void)0)
#endif


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ZERO_INIT(TrackedPoint)
{
	// 0-32767 so can convert to dist squared without overflowing a u32
	u16 x, y;
	bool enabled;
};

struct ZERO_INIT(GridPos)
{
	u32 x, y;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static BitVector<32> sRequestedArrays;
static BitVector<32> sActiveArrays;
static std::vector<TrackedPoint> sPoints;
static std::vector<u16> sActiveGrid[kGridSize][kGridSize];
static std::vector<GridPos> sFetchGrid;
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
static s32 sBaseJumpDist = 0;
static s32 sMaxJumpDist = 0;
static u32 sMaxJumpDistSquared = 0;
static u32 sMaxDeviationRadiusSquared = 0;
static u32 sMinJumpDistSquared = kDefaultMinJumpDist * kDefaultMinJumpDist;
static u32 sBestCandidateDistPenalty = 0xFFFFFFFF;


//-----------------------------------------------------------------------------
// Row class - helper class for generating hotspot link maps
//-----------------------------------------------------------------------------

enum EHDir { eHDir_L, eHDir_R, eHDir_Num };
enum EVDir { eVDir_U, eVDir_D, eVDir_Num };
static EHDir oppositeDir(EHDir theDir)
{ return theDir == eHDir_L ? eHDir_R : eHDir_L; }
static EVDir oppositeDir(EVDir theDir)
{ return theDir == eVDir_U ? eVDir_D : eVDir_U; }
static int dirDelta(EVDir theDir)
{ return theDir == eVDir_U ? -1 : 1; }

class ZERO_INIT(Row)
{
public:
	// TYPES & CONSTANTS
	enum EConnectMethod
	{
		eConnectMethod_None,
		eConnectMethod_Basic,
		eConnectMethod_Full,
		eConnectMethod_OffLeftEdge,
		eConnectMethod_OffRightEdge,
		eConnectMethod_SplitOut,
		eConnectMethod_SplitIn,
	} method[eVDir_Num];

	struct ZERO_INIT(Dot)
	{
		u16 pointID, x, y, vertLink[eVDir_Num];
		Dot(u16 thePointID = 0) :
			pointID(thePointID),
			x(sPoints[thePointID].x),
			y(sPoints[thePointID].y),
			vertLink()
		{}

		bool operator<(const Dot& rhs) const
		{ return x <rhs.x; }
	};

	// MUTATORS
	void addDot(u16 thePointID)
	{
		DBG_ASSERT(thePointID < sPoints.size());
		mDots.push_back(Dot(thePointID));
		this->totalY += mDots.back().y;
		this->avgY = this->totalY / int(mDots.size());
	}
	void sortDots() { std::sort(mDots.begin(), mDots.end()); }


	// ACCESSORS
	bool operator<(const Row& rhs) const { return avgY < rhs.avgY; }
	const Dot& operator[](size_t idx) const { return mDots[idx]; }
	Dot& operator[](size_t idx) { return mDots[idx]; }
	bool empty() const { return mDots.empty(); }
	size_t size() const { return mDots.size(); }
	const Dot& leftEdgeDot() const { return mDots.front(); }
	Dot& leftEdgeDot() { return mDots.front(); }
	const Dot& rightEdgeDot() const { return mDots.back(); }
	Dot& rightEdgeDot() { return mDots.back(); }
	Dot& edgeDot(EHDir theDir)
	{ return theDir == eHDir_L ? leftEdgeDot() : rightEdgeDot(); }
	const Dot& edgeDot(EHDir theDir) const
	{ return theDir == eHDir_L ? leftEdgeDot() : rightEdgeDot(); }

	int minX() const { return leftEdgeDot().x; }
	int maxX() const { return rightEdgeDot().x; }
	int minXy() const { return leftEdgeDot().y; }
	int maxXy() const { return rightEdgeDot().y; }
	int minXp() const { return minX() - kMaxPerpDistForStraightLine; }
	int maxXp() const { return maxX() + kMaxPerpDistForStraightLine; }

	const size_t closestIdxTo(int theX) const
	{
		DBG_ASSERT(!empty());
		int idx = 0;
		for(; idx < mDots.size(); ++idx)
		{
			if( mDots[idx].x == theX )
				break;
			if( mDots[idx].x > theX )
			{
				if( idx > 0 && (mDots[idx].x - theX > theX - mDots[idx-1].x) )
					--idx;
				break;
			}
		}
		if( idx == mDots.size() )
			--idx;
		return idx;
	}

	size_t nextLeftIdx(int theX) const
	{
		DBG_ASSERT(!empty());
		int idx = int(mDots.size())-1;
		while(mDots[idx].x > theX)
			--idx;
		return max(0, idx);
	}

	size_t nextRightIdx(int theX) const
	{
		DBG_ASSERT(!empty());
		size_t idx = 0;
		while(mDots[idx].x < theX)
			++idx;
		return min(mDots.size()-1, idx);
	}
	const Dot& nextLeft(int theX) const
	{ return mDots[nextLeftIdx(theX)]; }
	const Dot& nextRight(int theX) const
	{ return mDots[nextRightIdx(theX)]; }
	Dot& nextLeft(int theX)
	{ return mDots[nextLeftIdx(theX)]; }
	Dot& nextRight(int theX)
	{ return mDots[nextRightIdx(theX)]; }
	const Dot& closestTo(int theX) const
	{ return mDots[closestIdxTo(theX)]; }
	Dot& closestTo(int theX)
	{ return mDots[closestIdxTo(theX)]; }

	EConnectMethod findConnectMethod(const Row& rhs) const
	{
		if( empty() )
			return eConnectMethod_None;

		if( minX() - rhs.maxX() >
				abs(minXy() - rhs.maxXy()) * kMinSlopeForHorizLink )
		{
			return eConnectMethod_OffLeftEdge;
		}

		if( rhs.minX() - maxX() >
				abs(minXy() - rhs.maxXy()) * kMinSlopeForHorizLink )
		{
			return eConnectMethod_OffRightEdge;
		}

		if( minX() >= rhs.minX() && maxX() <= rhs.maxX() )
		{
			const size_t aNextR = rhs.nextRightIdx(maxX());
			const size_t aNextL = rhs.nextLeftIdx(minX());
			if( aNextR == aNextL + 1 )
			{
				if( rhs[aNextR].x - maxX() >
						abs(rhs[aNextR].y - maxXy()) * kMinSlopeForHorizLink &&
					minX() - rhs[aNextL].x >
						abs(rhs[aNextR].y - maxXy()) * kMinSlopeForHorizLink )
				{ return eConnectMethod_SplitOut; }
				// Even if not true split out, don't count as "full" either
				return eConnectMethod_Basic;
			}
		}

		if( minX() < rhs.minX() && maxX() > rhs.maxX() )
		{
			const size_t aNextR = nextRightIdx(rhs.maxX());
			const size_t aNextL = nextLeftIdx(rhs.minX());
			if( aNextR == aNextL + 1 )
			{
				if( aNextR - rhs.maxX() >
						abs(mDots[aNextR].y - rhs.maxXy()) *
							kMinSlopeForHorizLink &&
					rhs.minX() - aNextL >
						abs(mDots[aNextL].y - rhs.minXy()) *
							kMinSlopeForHorizLink )
				{ return eConnectMethod_SplitIn; }
				return eConnectMethod_Basic;
			}
		}

		if( min(maxX(), rhs.maxX()) - max(minX(), rhs.minX()) >
				(maxX() - minX()) * 0.7 )
		{
			return eConnectMethod_Full;
		}

		return eConnectMethod_Basic;
	}

	// PUBLIC DATA
	int avgY, totalY;
	size_t insideLinkDotIdx[eHDir_Num];
	u16 insideLink[eHDir_Num];
	u16 outsideLink[eHDir_Num];

private:
	// PRIVATE DATA
	std::vector<Dot> mDots;
};


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

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
			sPoints[sTaskProgress].x = min<double>(kNormalizedTargetSize,
				(anOverlayPos.x + 1) * kNormalizedTargetSize / aScaleFactor);
			sPoints[sTaskProgress].y = min<double>(kNormalizedTargetSize,
				(anOverlayPos.y + 1) * kNormalizedTargetSize / aScaleFactor);
			DBG_ASSERT(sPoints[sTaskProgress].y <= kNormalizedTargetSize);
		}
		mapDebugPrint(
			"Normalizing Hotspot '%s' (%d x %d) position to %d x %d \n",
			InputMap::hotspotLabel(aHotspotID).c_str(),
			anOverlayPos.x, anOverlayPos.y,
			sPoints[sTaskProgress].x, sPoints[sTaskProgress].y);
	}
	else if( sTaskProgress == sPoints.size() )
	{
		// Calculate jump ranges
		sBaseJumpDist = max(0.0,
			Profile::getInt("Mouse", "DefaultHotspotDistance") *
			sLastUIScale / gWindowUIScale * kNormalizedTargetSize);

		u32 aMaxDeviationRadius = sBaseJumpDist * kDeviationRadiusMult;
		sMaxJumpDist = sBaseJumpDist + aMaxDeviationRadius;

		const int aScaleFactor = max(sLastTargetSize.cx, sLastTargetSize.cy);
		sBaseJumpDist /= aScaleFactor;
		sMaxJumpDist /= aScaleFactor;
		aMaxDeviationRadius /= aScaleFactor;
		sMaxDeviationRadiusSquared = aMaxDeviationRadius * aMaxDeviationRadius;
		sMaxJumpDistSquared = sMaxJumpDist * sMaxJumpDist;
	}

	if( ++sTaskProgress > sPoints.size() )
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
			const int aFirstHotspot = InputMap::firstHotspotInArray(anArray);
			const int aHotspotCount = InputMap::sizeOfHotspotArray(anArray);
			for(int i = aFirstHotspot; i < aFirstHotspot + aHotspotCount; ++i)
				sPoints[i].enabled = enableHotspots;
			sActiveArrays.set(anArray, enableHotspots);
			sNewTasks.set(eTask_AddToGrid);
			sNewTasks.set(eTask_BeginSearch);
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
			"Adding Hotspot '%s' to grid cell %d x %d\n",
			InputMap::hotspotLabel(aPointIdx).c_str(),
			aGridX, aGridY);
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

	const u32 aMinGridX = u32(clamp(sNormalizedCursorPos.x - sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	const u32 aMinGridY = u32(clamp(sNormalizedCursorPos.y - sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	const u32 aMaxGridX = u32(clamp(sNormalizedCursorPos.x + sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	const u32 aMaxGridY = u32(clamp(sNormalizedCursorPos.y + sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift;
	GridPos aGridPos = GridPos();
	for(aGridPos.x = aMinGridX; aGridPos.x <= aMaxGridX; ++aGridPos.x)
	{
		for(aGridPos.y = aMinGridY; aGridPos.y <= aMaxGridY; ++aGridPos.y)
			sFetchGrid.push_back(aGridPos);
	}
	sCurrentTask = eTask_None;
	sNewTasks.set(eTask_FetchFromGrid);
	mapDebugPrint(
		"Beginning new search starting from %d x %d (normalized)\n",
		sNormalizedCursorPos.x, sNormalizedCursorPos.y);
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
		if( aDist >= sMinJumpDistSquared && aDist < sMaxJumpDistSquared )
			sCandidates.push_back(aPointIdx);
	}

	if( ++sTaskProgress >= sFetchGrid.size() )
		sCurrentTask = eTask_None;
}


static void processNextInDirTask(ECommandDir theDir)
{
	if( sTaskProgress == 0 )
		sBestCandidateDistPenalty = 0xFFFFFFFF;

	while(sTaskProgress < sCandidates.size())
	{
		const u16 aPointIdx = sCandidates[sTaskProgress++];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		long dx = aPoint.x - sNormalizedCursorPos.x;
		long dy = aPoint.y - sNormalizedCursorPos.y;
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

		// Below purposefully skews distances such that x=10, y=10 is just
		// a distance of 10 instead of the true distance of 14.14~, to act
		// like a chess board where movement of 1 unit slant-wise is 1x+1y.
		long aDirDist;
		switch(theDir)
		{
		case eCmd8Dir_L:  aDirDist = -dx;					break;
		case eCmd8Dir_R:  aDirDist = dx;					break;
		case eCmd8Dir_U:  aDirDist = -dy;					break;
		case eCmd8Dir_D:  aDirDist = dy;					break;
		case eCmd8Dir_UL: aDirDist = (-dx - dy) / 2;		break;
		case eCmd8Dir_UR: aDirDist = (dx - dy) / 2;			break;
		case eCmd8Dir_DL: aDirDist = (-dx + dy) / 2;		break;
		case eCmd8Dir_DR: aDirDist = (dx + dy) / 2;			break;
		}
		if( aDirDist <= 0 )
			continue;

		long aPerpDist;
		switch(theDir)
		{
		case eCmd8Dir_L:  aPerpDist = abs(dy);				break;
		case eCmd8Dir_R:  aPerpDist = abs(dy);				break;
		case eCmd8Dir_U:  aPerpDist = abs(dx);				break;
		case eCmd8Dir_D:  aPerpDist = abs(dx);				break;
		case eCmd8Dir_UL: aPerpDist = abs(dx - dy) / 2;		break;
		case eCmd8Dir_UR: aPerpDist = abs(dx + dy) / 2;		break;
		case eCmd8Dir_DL: aPerpDist = abs(-dx - dy) / 2;	break;
		case eCmd8Dir_DR: aPerpDist = abs(dy - dx) / 2;		break;
		}

		// First check if counts as being straight in desired direction,
		// which gives highest priority (lowest weight), based on dist.
		if( aPerpDist <= kMaxPerpDistForStraightLine )
		{
			if( aDirDist < sBestCandidateDistPenalty )
			{
				sNextHotspotInDir[theDir] = aPointIdx;
				sBestCandidateDistPenalty = aDirDist;
			}
			continue;
		}

		// All others have at least as much penalty as full straight line,
		// plus their distance from the default no-hotspot-found jump dest.
		dx = abs(aDirDist - sBaseJumpDist);
		dy = aPerpDist * kPerpPenaltyMult;
		const u32 aDistSqFromBaseDest = (dx * dx) + (dy * dy);
		if( aDistSqFromBaseDest > sMaxDeviationRadiusSquared )
			continue;

		const u32 aDistPenalty = u32(sMaxJumpDist) + aDistSqFromBaseDest;
		if( aDistPenalty < sBestCandidateDistPenalty )
		{
			sNextHotspotInDir[theDir] = aPointIdx;
			sBestCandidateDistPenalty = aDistPenalty;
		}
		break;
	}

	if( sTaskProgress >= sCandidates.size() )
	{
		sCurrentTask = eTask_None;
		if( sNextHotspotInDir[theDir] != 0 )
		{
			mapDebugPrint("%s hotspot chosen - '%s'\n",
				theDir == eCmd8Dir_L	? "Left":
				theDir == eCmd8Dir_R	? "Right":
				theDir == eCmd8Dir_U	? "Up":
				theDir == eCmd8Dir_D	? "Down":
				theDir == eCmd8Dir_UL	? "UpLeft":
				theDir == eCmd8Dir_UR	? "UpRight":
				theDir == eCmd8Dir_DL	? "DownLeft":
				/*eCmd8Dir_DR*/			  "DownRight",
				InputMap::hotspotLabel(sNextHotspotInDir[theDir]).c_str());
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


static void safeLinkHotspotRows(
	std::vector<Row>& theRows,
	int theRowRangeBegin,
	int theRowRangeEnd)
{
	// Helper function for generating hotspot link map
	// Guarantees each row has at least one link to adjacent row, so all
	// points have at least one path to be connected to all others
	for(int aRowIdx = theRowRangeBegin; aRowIdx < theRowRangeEnd; ++aRowIdx)
	{
		Row& aRow = theRows[aRowIdx];
		for(EVDir aVDir = EVDir(0);
			aVDir < eVDir_Num; aVDir = EVDir(aVDir+1) )
		{
			int aNextRowIdx = aRowIdx + dirDelta(aVDir);
			if( aNextRowIdx < 0 || aNextRowIdx >= theRows.size() )
				continue; // leave method as _None
			Row& aNextRow = theRows[aNextRowIdx];
			aRow.method[aVDir] = aRow.findConnectMethod(aNextRow);

			switch(aRow.method[aVDir])
			{
			case Row::eConnectMethod_None:
				break;
			case Row::eConnectMethod_Basic:
				{// Link points within intersecting X range
					size_t aFirstDotIdx = 0;
					size_t aLastDotIdx = aRow.size() - 1;
					while(aFirstDotIdx < aRow.size() - 1 &&
						  aRow[aFirstDotIdx].x < aNextRow.minXp())
					{ ++aFirstDotIdx; }
					while(aLastDotIdx > 0 &&
						  aRow[aLastDotIdx].x > aNextRow.maxXp())
					{ --aLastDotIdx; }
					if( aFirstDotIdx > aLastDotIdx )
					{// No dots within intersection area - pick closest
						const size_t aDotLIdx =
							aRow.closestIdxTo(aNextRow.minX());
						const size_t aDotRIdx =
							aRow.closestIdxTo(aNextRow.maxX());
						if( abs(aNextRow.maxX() - aRow[aDotRIdx].x) <
								abs(aNextRow.minX() - aRow[aDotLIdx].x) )
							{ aFirstDotIdx = aLastDotIdx = aDotRIdx; }
						else
							{ aFirstDotIdx = aLastDotIdx = aDotLIdx; }
					}
					for(size_t i = aFirstDotIdx; i <= aLastDotIdx; ++i)
					{
						if( aRow[i].vertLink[aVDir] == 0 )
						{
							const Row::Dot& aLinkDot =
								aNextRow.closestTo(aRow[i].x);
							// Don't connect center dots if another is closer
							if( i <= 0 || i == aRow.size() - 1 ||
								(aLinkDot.x > aRow[i-1].x &&
								 aLinkDot.x < aRow[i+1].x) )
							{
								aRow[i].vertLink[aVDir] = aLinkDot.pointID;
							}
						}
					}
				}
				break;
			case Row::eConnectMethod_Full:
				// Link ALL points
				for(size_t i = 0; i < aRow.size(); ++i)
				{
					if( aRow[i].vertLink[aVDir] == 0 )
					{
						aRow[i].vertLink[aVDir] =
							aNextRow.closestTo(aRow[i].x).pointID;
					}
				}
				break;
			case Row::eConnectMethod_OffLeftEdge:
				// Link the opposite end points
				if( aRow.leftEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_L] == 0 )
				{
					aRow.leftEdgeDot().vertLink[aVDir] =
						aNextRow.rightEdgeDot().pointID;
				}
				break;
			case Row::eConnectMethod_OffRightEdge:
				// Link the opposite end points
				if( aRow.rightEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_R] == 0 )
				{
					aRow.rightEdgeDot().vertLink[aVDir] =
						aNextRow.leftEdgeDot().pointID;
				}
				break;
			case Row::eConnectMethod_SplitOut:
				// Link end points to points just past them on other row
				if( aRow.leftEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_L] == 0 )
				{
					aRow.leftEdgeDot().vertLink[aVDir] =
						aNextRow.nextLeft(aRow.minX()).pointID;
				}
				if( aRow.rightEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_R] == 0 )
				{
					aRow.rightEdgeDot().vertLink[aVDir] =
						aNextRow.nextRight(aRow.maxX()).pointID;
				}
				break;
			case Row::eConnectMethod_SplitIn:
				// Link points just outside other row's end points to it
				if( aRow.nextLeft(aNextRow.minX()).vertLink[aVDir] == 0 &&
					aRow.insideLink[eHDir_R] == 0 )
				{
					aRow.nextLeft(aNextRow.minX()).vertLink[aVDir] =
						aNextRow.leftEdgeDot().pointID;
				}
				if( aRow.nextRight(aNextRow.minX()).vertLink[aVDir] == 0 &&
					aRow.insideLink[eHDir_L] == 0 )
				{
					aRow.nextRight(aNextRow.minX()).vertLink[aVDir] =
						aNextRow.rightEdgeDot().pointID;
				}
				break;
			}
		}
	}

	// Replace certain vertical links with horizontal links instead
	// This will temporarily break the all-rows-linked guarantee above,
	// which will then be repaired using the skip row list after this
	std::vector<int> aSkipRowList;
	for(int aRowIdx = theRowRangeBegin; aRowIdx < theRowRangeEnd; ++aRowIdx)
	{
		Row& aRow = theRows[aRowIdx];
		for(EVDir aVDir = EVDir(0);
			aVDir < eVDir_Num; aVDir = EVDir(aVDir+1))
		{
			if( aRow.method[aVDir] != Row::eConnectMethod_OffLeftEdge &&
				aRow.method[aVDir] != Row::eConnectMethod_OffRightEdge &&
				aRow.method[aVDir] != Row::eConnectMethod_SplitOut &&
				aRow.method[aVDir] != Row::eConnectMethod_SplitIn )
			{ continue; }

			const int aNextRowIdx = aRowIdx + dirDelta(aVDir);
			DBG_ASSERT(aNextRowIdx >= 0 && aNextRowIdx < theRows.size());
			// If same method in both directions, only process closer in Y
			const bool isBidirectional =
				aRow.method[aVDir] == aRow.method[oppositeDir(aVDir)];
			if( isBidirectional && aVDir == 0 )
			{
				const int aPrevRowIdx = aRowIdx - dirDelta(aVDir);
				DBG_ASSERT(aPrevRowIdx >= 0 && aPrevRowIdx < theRows.size());
				const int aNextYDist =
					abs(aRow.avgY - theRows[aNextRowIdx].avgY);
				const int aPrevYDist =
					abs(aRow.avgY - theRows[aPrevRowIdx].avgY);
				if( aPrevYDist < aNextYDist )
					continue; // allow loop to process other dir instead 
			}
			Row& aNextRow = theRows[aRowIdx + dirDelta(aVDir)];

			switch(aRow.method[aVDir])
			{
			case Row::eConnectMethod_OffLeftEdge:
			case Row::eConnectMethod_OffRightEdge:
				{// Horizontally link theRows separated far in X
					const EHDir aHDir =
						aRow.method[aVDir] == Row::eConnectMethod_OffLeftEdge
							? eHDir_L : eHDir_R;
					if( aRow.outsideLink[aHDir] )
						break;
					aRow.outsideLink[aHDir] =
						aNextRow.edgeDot(oppositeDir(aHDir)).pointID;
					aRow.edgeDot(aHDir).vertLink[aVDir] = 0;
					if( isBidirectional )
						aRow.edgeDot(aHDir).vertLink[oppositeDir(aVDir)] = 0;
				}
				break;
			case Row::eConnectMethod_SplitOut:
				if( aRow.outsideLink[eHDir_L] ||
					aRow.outsideLink[eHDir_R] )
				{ break; }

				// Convert to horizontal links in both directions
				aRow.outsideLink[eHDir_L] =
					aNextRow.nextLeft(aRow.minX()).pointID;
				aRow.outsideLink[eHDir_R] =
					aNextRow.nextRight(aRow.minX()).pointID;
				aRow.leftEdgeDot().vertLink[aVDir] = 0;
				aRow.rightEdgeDot().vertLink[aVDir] = 0;
				if( isBidirectional )
				{
					aRow.leftEdgeDot().vertLink[oppositeDir(aVDir)] = 0;
					aRow.rightEdgeDot().vertLink[oppositeDir(aVDir)] = 0;
				}
				break;
			case Row::eConnectMethod_SplitIn:
				if( aRow.insideLink[eHDir_L] ||
					aRow.insideLink[eHDir_R] )
				{ break; }

				// Left to smaller row's right-most dot
				aRow.insideLinkDotIdx[eHDir_L] =
					aRow.nextRightIdx(aNextRow.maxX());
				aRow.insideLink[eHDir_L] =
					aNextRow.rightEdgeDot().pointID;
				aRow[aRow.insideLinkDotIdx[eHDir_L]].vertLink[aVDir] = 0;

				// Right to smaller row's left-most dot
				aRow.insideLinkDotIdx[eHDir_R] =
					aRow.nextLeftIdx(aNextRow.minX());
				aRow.insideLink[eHDir_R] =
					aNextRow.leftEdgeDot().pointID;
				aRow[aRow.insideLinkDotIdx[eHDir_R]].vertLink[aVDir] = 0;

				if( isBidirectional )
				{
					aRow[aRow.insideLinkDotIdx[eHDir_L]]
						.vertLink[oppositeDir(aVDir)] = 0;
					aRow[aRow.insideLinkDotIdx[eHDir_R]]
						.vertLink[oppositeDir(aVDir)] = 0;
				}
				break;
			}

			if( isBidirectional )
			{
				aSkipRowList.push_back(aRowIdx);
				break;
			}
		}
	}

	for(size_t i = 0; i < aSkipRowList.size(); ++i)
	{
		// Recursively use this function as if this row didn't exist,
		// allowing rows to link to other rows by skipping over
		// problematic ones (bi-directional splits and offsets).
		Row aTmpRow = theRows[aSkipRowList[i]];
		theRows.erase(theRows.begin() + aSkipRowList[i]);
		safeLinkHotspotRows(theRows,
			max(0, aSkipRowList[i]-1),
			min(int(theRows.size()), aSkipRowList[i]+1));
		theRows.insert(theRows.begin() + aSkipRowList[i], aTmpRow);
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
	sFetchGrid.reserve(kGridSize * kGridSize);
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


void loadProfileChanges()
{
	if( Profile::changedSections().contains("HOTSPOTS") ||
		Profile::changedSections().contains("MOUSE") )
	{
		sNewTasks.set();
		for(size_t i = 0; i < sLinkMaps.size(); ++i)
			sLinkMaps[i].clear();
	}
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
		for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
			sNewTasks.set(eTask_NextInDir + aDir);
	}

	// Continue progress on any current tasks
	processTasks();
}


void setEnabledHotspotArrays(const BitVector<32>& theHotspotArrays)
{
	if( sRequestedArrays != theHotspotArrays )
	{
		sRequestedArrays = theHotspotArrays;
		sNewTasks.set(eTask_ActiveArrays);
		if( gHotspotsGuideMode == eHotspotGuideMode_Showing )
			gHotspotsGuideMode = eHotspotGuideMode_Redraw;
	}
}


const BitVector<32>& getEnabledHotspotArrays()
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
	const u16 aNodeCount = InputMap::sizeOfHotspotArray(theArrayID);
	sLinkMaps[theArrayID].resize(max<size_t>(1, aNodeCount));
	if( aNodeCount <= 1 ) return sLinkMaps[theArrayID];

	// Make sure hotspots' normalized positions have been assigned
	while(sNewTasks.test(eTask_TargetSize) ||
		  sCurrentTask == eTask_TargetSize)
	{ processTasks(); }

	// Assign the hotspots to "dots" in "rows" (nearly-matching Y values)
	std::vector<Row> aRowVec; aRowVec.reserve(aNodeCount);
	for(u16 aPointIdx = aFirstHotspot;
		aPointIdx < aFirstHotspot + aNodeCount; ++aPointIdx)
	{
		TrackedPoint& aPoint = sPoints[aPointIdx];
		bool addedToExistingRow = false;
		for(size_t aRowIdx = 0; aRowIdx < aRowVec.size(); ++aRowIdx)
		{
			Row& aRow = aRowVec[aRowIdx];
			const int aYDist = abs(aRow.avgY - signed(aPoint.y));
			if( aYDist <= kMaxPerpDistForStraightLine )
			{
				aRow.addDot(aPointIdx);
				addedToExistingRow = true;
			}
		}
		if( !addedToExistingRow )
		{
			aRowVec.push_back(Row());
			aRowVec.back().addDot(aPointIdx);
		}
	}

	// Sort the dots horizontally in each row
	for(size_t aRowIdx = 0; aRowIdx < aRowVec.size(); ++aRowIdx)
		aRowVec[aRowIdx].sortDots();

	// Sort the rows from top to bottom
	std::sort(aRowVec.begin(), aRowVec.end());
	
	// Generate vertical links with guarantee all points can be reached
	// (even if not always by the most convenient route)
	safeLinkHotspotRows(aRowVec, 0, int(aRowVec.size()));

	// Add extra vertical links for columns by allowing row skips
	for(int aRowIdx = 0; aRowIdx < aRowVec.size(); ++aRowIdx)
	{
		Row& aRow = aRowVec[aRowIdx];
		for(EVDir aVDir = EVDir(0);
			aVDir < eVDir_Num; aVDir = EVDir(aVDir+1))
		{
			for(size_t aDotIdx = 0; aDotIdx < aRow.size(); ++aDotIdx)
			{
				Row::Dot& aFromDot = aRow[aDotIdx];
				u32 aBestCandidateDistPenalty = 0xFFFFFFFF;
				if( aFromDot.vertLink[aVDir] != 0 )
					continue;

				for(int aNextRowIdx = aRowIdx + dirDelta(aVDir);
					aNextRowIdx >= 0 && aNextRowIdx < aRowVec.size();
					aNextRowIdx += dirDelta(aVDir))
				{
					Row::Dot& aToDot =
						aRowVec[aNextRowIdx].closestTo(aFromDot.x);
					const u32 aDistX = abs(aFromDot.x - aToDot.x);
					const u32 aDistY = abs(aFromDot.y - aToDot.y);
					if( aDistX > kMaxLinkMapColumnXDist )
						continue;
					if( aDistX > aDistY * kMaxSlopeForVertLink )
						continue;
					const u32 aDistPenalty =
						aDistY + aDistX * kColumnXDistPenaltyMult;
					if( aDistPenalty < aBestCandidateDistPenalty ) 
					{
						aFromDot.vertLink[aVDir] = aToDot.pointID;
						aBestCandidateDistPenalty = aDistPenalty;
					}
				}
			}
		}
	}

	// Convert finalized Row data into HotspotLinkNode data
	for(int aRowIdx = 0; aRowIdx < aRowVec.size(); ++aRowIdx)
	{
		Row& aRow = aRowVec[aRowIdx];
		for(size_t aDotIdx = 0; aDotIdx < aRow.size(); ++aDotIdx )
		{
			Row::Dot& aDot = aRow[aDotIdx];
			u16 aPointInDir[eCmdDir_Num];
			aPointInDir[eCmdDir_U] = aDot.vertLink[eVDir_U];
			aPointInDir[eCmdDir_D] = aDot.vertLink[eVDir_D];
			if( aDotIdx == 0 )
				{ aPointInDir[eCmdDir_L] = aRow.outsideLink[eHDir_L]; }
			else if( aRow.insideLink[eHDir_L] != 0 &&
					 aRow.insideLinkDotIdx[eHDir_L] == aDotIdx )
				{ aPointInDir[eCmdDir_L] = aRow.insideLink[eHDir_L]; }
			else
				{ aPointInDir[eCmdDir_L] = aRow[aDotIdx-1].pointID; }

			if( aDotIdx == aRow.size() - 1 )
				{ aPointInDir[eCmdDir_R] = aRow.outsideLink[eHDir_R]; }
			else if( aRow.insideLink[eHDir_R] != 0 &&
					 aRow.insideLinkDotIdx[eHDir_R] == aDotIdx )
				{ aPointInDir[eCmdDir_R] = aRow.insideLink[eHDir_R]; }
			else
				{ aPointInDir[eCmdDir_R] = aRow[aDotIdx+1].pointID; }

			const u16 aNodeIdx = aDot.pointID - aFirstHotspot;
			HotspotLinkNode& aNode = sLinkMaps[theArrayID][aNodeIdx];
			for(u8 aDir = 0; aDir < eCmdDir_Num; ++aDir)
			{
				if( aPointInDir[aDir] == 0 )
				{
					aNode.next[aDir] = aNodeIdx;
					aNode.edge[aDir] = true;
				}
				else
				{
					DBG_ASSERT(aPointInDir[aDir] >= aFirstHotspot);
					aNode.next[aDir] = aPointInDir[aDir] - aFirstHotspot;
					aNode.edge[aDir] = false;
				}
			}
		}
	}

	// Add wrap-around options for edge nodes
	for(u16 aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
	{
		HotspotLinkNode& aNode = sLinkMaps[theArrayID][aNodeIdx];
		for(u8 aDir = 0; aDir < eCmdDir_Num; ++aDir)
		{
			if( !aNode.edge[aDir] )
				continue;
			const ECommandDir anOppDir = opposite8Dir(ECommandDir(aDir));
			u16 aWrapNode = aNode.next[anOppDir];
			while(!sLinkMaps[theArrayID][aWrapNode].edge[anOppDir])
				aWrapNode = sLinkMaps[theArrayID][aWrapNode].next[anOppDir];
			aNode.next[aDir] = aWrapNode;
		}
	}

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
	EHotspotNamingStyle theStyle)
{
	std::string result;
	EHotspotNamingStyle aCoordStyle;
	switch(theStyle)
	{
	case eHNS_XY:		aCoordStyle = eHNS_X;		break;
	case eHNS_XY_Off:	aCoordStyle = eHNS_X_Off;	break;
	case eHNS_WH:		aCoordStyle = eHNS_W;		break;
	default: DBG_ASSERT(false);
	}
	result = coordToString(theHotspot.x, aCoordStyle) + ", ";
	switch(theStyle)
	{
	case eHNS_XY:		aCoordStyle = eHNS_Y;		break;
	case eHNS_XY_Off:	aCoordStyle = eHNS_Y_Off;	break;
	case eHNS_WH:		aCoordStyle = eHNS_H;		break;
	default: DBG_ASSERT(false);
	}
	result += coordToString(theHotspot.y, aCoordStyle);
	return result;
}


std::string coordToString(
	const Hotspot::Coord& theCoord,
	EHotspotNamingStyle theStyle)
{
	const u16 kPercentResolution = 10000; // Must be evenly divisible by 10
	std::string result;
	switch(theCoord.anchor)
	{
	case 0:
		// Leave blank
		break;
	case 0x8000:
		switch(theStyle)
		{
		case eHNS_X: case eHNS_W: case eHNS_X_Off:
			result = "CX";
			break;
		case eHNS_Y: case eHNS_H: case eHNS_Y_Off:
			result = "CY";
			break;
		}
		break;
	case 0xFFFF:
		switch(theStyle)
		{
		case eHNS_X: case eHNS_W: case eHNS_X_Off:
			result = "R";
			break;
		case eHNS_Y: case eHNS_H: case eHNS_Y_Off:
			result = "B";
			break;
		}
		break;
	}

	if( result.empty() &&
		(theCoord.anchor != 0 || theStyle == eHNS_Num) )
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
		 theStyle == eHNS_X_Off ||
		 theStyle == eHNS_Y_Off) )
	{
		result += "+";
	}
	if( theCoord.offset != 0 )
		result += toString(theCoord.offset);
	if( result.empty() )
	{
		switch(theStyle)
		{
		case eHNS_X:
			result = "L";
			break;
		case eHNS_Y:
			result = "T";
			break;
		case eHNS_W:
		case eHNS_H:
			result = "0";
			break;
		case eHNS_X_Off:
		case eHNS_Y_Off:
			result = "+0";
			break;
		}
	}
	return result;
}

#undef mapDebugPrint
#undef HOTSPOT_MAP_DEBUG_PRINT

} // HotspotMap
