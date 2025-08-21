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
const double kDeviationRadiusMult = 0.75;
// Higher number = prioritize straighter lines over shorter distances
const double kPerpPenaltyMult = 1.25;
// These are used only for generating hotspot link maps, which must
// guarantee all points can be reached regardless of distance without
// interim hops so uses a different algorithm than basic jumps
// Higher number = must be further in X to make a left/right link
const double kMinSlopeForHorizLink = 0.9f;
// Higher number = allows for up/down link even when far in X
const double kMaxSlopeForVertLink = 1.2f;
// Higher number = prioritize straighter columns over Y distance
const int /*double*/ kColumnXDistPenaltyMult = 2;

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
	int x, y;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static BitVector<32> sRequestedArrays;
static BitVector<32> sActiveArrays;
static std::vector<TrackedPoint> sPoints;
static std::vector<u16> sActiveGrid[kGridSize][kGridSize];
static std::vector<GridPos> sFetchGrid;
static std::vector<int> sCandidates;
static VectorMap<u16, Links> sLinkMaps;
static int sNextHotspotInDir[eCmd8Dir_Num] = { 0 };
static double sLastUIScale = 1.0;
static SIZE sLastTargetSize;
static Hotspot sLastCursorPos;
static POINT sNormalizedCursorPos;
static BitArray<eTask_Num> sNewTasks;
static ETask sCurrentTask = eTask_None;
static int sTaskProgress = 0;
static int sBaseJumpDist = 0;
static int sMaxJumpDist = 0;
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
		int pointID, x, y, vertLink[eVDir_Num];
		Dot(int thePointID = 0) :
			pointID(thePointID),
			x(sPoints[thePointID].x),
			y(sPoints[thePointID].y),
			vertLink()
		{}

		bool operator<(const Dot& rhs) const
		{ return x <rhs.x; }
	};

	// MUTATORS
	void addDot(int thePointID)
	{
		DBG_ASSERT(size_t(thePointID) < sPoints.size());
		mDots.push_back(Dot(thePointID));
		this->totalY += mDots.back().y;
		this->avgY = this->totalY / intSize(mDots.size());
	}
	void sortDots() { std::sort(mDots.begin(), mDots.end()); }


	// ACCESSORS
	bool operator<(const Row& rhs) const { return avgY < rhs.avgY; }
	const Dot& operator[](size_t idx) const { return mDots[idx]; }
	Dot& operator[](size_t idx) { return mDots[idx]; }
	bool empty() const { return mDots.empty(); }
	int size() const { return intSize(mDots.size()); }
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

	const int closestIdxTo(int theX) const
	{
		DBG_ASSERT(!empty());
		int idx = 0;
		for(int end = intSize(mDots.size()); idx < end; ++idx)
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
		if( size_t(idx) == mDots.size() )
			--idx;
		return idx;
	}

	int nextLeftIdx(int theX) const
	{
		DBG_ASSERT(!empty());
		int idx = intSize(mDots.size())-1;
		while(mDots[idx].x > theX)
			--idx;
		return max(0, idx);
	}

	int nextRightIdx(int theX) const
	{
		DBG_ASSERT(!empty());
		int idx = 0;
		while(mDots[idx].x < theX)
			++idx;
		return min(intSize(mDots.size())-1, idx);
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
			const int aNextR = rhs.nextRightIdx(maxX());
			const int aNextL = rhs.nextLeftIdx(minX());
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
			const int aNextR = nextRightIdx(rhs.maxX());
			const int aNextL = nextLeftIdx(rhs.minX());
			if( aNextR == aNextL + 1 )
			{
				if( mDots[aNextR].x - rhs.maxX() >
						abs(mDots[aNextR].y - rhs.maxXy()) *
							kMinSlopeForHorizLink &&
					rhs.minX() -  mDots[aNextL].x >
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
	int insideLinkDotIdx[eHDir_Num];
	int insideLink[eHDir_Num];
	int outsideLink[eHDir_Num];

private:
	// PRIVATE DATA
	std::vector<Dot> mDots;
};


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void processTargetSizeTask()
{
	const int kPointsCount = intSize(sPoints.size());
	if( sTaskProgress < kPointsCount )
	{
		const int aHotspotID = sTaskProgress;
		const Hotspot& aHotspot = InputMap::getHotspot(aHotspotID);
		const POINT& anOverlayPos =
			WindowManager::hotspotToOverlayPos(aHotspot);
		const int aScaleFactor = max(sLastTargetSize.cx, sLastTargetSize.cy);
		if( aScaleFactor > 0 )
		{
			sPoints[sTaskProgress].x = u16(min<double>(kNormalizedTargetSize,
				(anOverlayPos.x + 1) * kNormalizedTargetSize / aScaleFactor));
			sPoints[sTaskProgress].y = u16(min<double>(kNormalizedTargetSize,
				(anOverlayPos.y + 1) * kNormalizedTargetSize / aScaleFactor));
			DBG_ASSERT(sPoints[sTaskProgress].y <= kNormalizedTargetSize);
		}
		mapDebugPrint(
			"Normalizing Hotspot '%s' (%d x %d) position to %d x %d \n",
			InputMap::hotspotLabel(aHotspotID).c_str(),
			anOverlayPos.x, anOverlayPos.y,
			sPoints[sTaskProgress].x, sPoints[sTaskProgress].y);
	}
	else if( sTaskProgress == kPointsCount )
	{
		// Calculate jump ranges
		sBaseJumpDist = int(max(0.0,
			Profile::getInt("Mouse", "DefaultHotspotDistance") *
			sLastUIScale / gWindowUIScale * kNormalizedTargetSize));

		int aMaxDeviationRadius = int(sBaseJumpDist * kDeviationRadiusMult);
		sMaxJumpDist = sBaseJumpDist + aMaxDeviationRadius;

		const int aScaleFactor = max(sLastTargetSize.cx, sLastTargetSize.cy);
		sBaseJumpDist /= aScaleFactor;
		sMaxJumpDist /= aScaleFactor;
		aMaxDeviationRadius /= aScaleFactor;
		sMaxDeviationRadiusSquared =
			u32(min<s64>(0xFFFFFFFF,
			s64(aMaxDeviationRadius) * aMaxDeviationRadius));
		sMaxJumpDistSquared =
			u32(min<s64>(0xFFFFFFFF, s64(sMaxJumpDist) * sMaxJumpDist));
	}

	if( ++sTaskProgress > kPointsCount )
		sCurrentTask = eTask_None;
}


static void processActiveArraysTask()
{
	const int kHotspotArrayCount = InputMap::hotspotArrayCount();
	while(sTaskProgress < kHotspotArrayCount)
	{
		const int anArray = sTaskProgress++;
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

	if( sTaskProgress >= kHotspotArrayCount )
		sCurrentTask = eTask_None;
}


static void processAddToGridTask()
{
	const int kPointsCount = intSize(sPoints.size());
	if( sTaskProgress == 0 )
	{
		if( sActiveArrays.any() )
			mapDebugPrint("Adding enabled hotspots to grid...\n");
		for(int x = 0; x < kGridSize; ++x)
		{
			for(int y = 0; y < kGridSize; ++y)
				sActiveGrid[x][y].clear();
		}
	}

	while(sTaskProgress < kPointsCount)
	{
		const int aPointIdx = sTaskProgress++;
		if( !sPoints[aPointIdx].enabled )
			continue;
		const int aGridX = sPoints[aPointIdx].x >> kNormalizedToGridShift;
		const int aGridY = sPoints[aPointIdx].y >> kNormalizedToGridShift;
		DBG_ASSERT(aGridX >= 0 && aGridX < kGridSize);
		DBG_ASSERT(aGridY >= 0 && aGridY < kGridSize);
		mapDebugPrint(
			"Adding Hotspot '%s' to grid cell %d x %d\n",
			InputMap::hotspotLabel(aPointIdx).c_str(),
			aGridX, aGridY);
		sActiveGrid[aGridX][aGridY].push_back(dropTo<u16>(aPointIdx));
		break;
	}

	if( sTaskProgress >= kPointsCount )
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

	const int aMinGridX(u32(clamp(sNormalizedCursorPos.x - sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift);
	const int aMinGridY(u32(clamp(sNormalizedCursorPos.y - sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift);
	const int aMaxGridX(u32(clamp(sNormalizedCursorPos.x + sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift);
	const int aMaxGridY(u32(clamp(sNormalizedCursorPos.y + sMaxJumpDist,
		0, kNormalizedTargetSize)) >> kNormalizedToGridShift);
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
	const int kFetchGridSize = intSize(sFetchGrid.size());
	if( sTaskProgress >= kFetchGridSize )
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
		const int aPointIdx = sActiveGrid[aGridX][aGridY][i];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		const u32 aDeltaX = abs(aPoint.x - sNormalizedCursorPos.x);
		const u32 aDeltaY = abs(aPoint.y - sNormalizedCursorPos.y);
		const u32 aDistSq = (aDeltaX * aDeltaX) + (aDeltaY * aDeltaY);
		if( aDistSq >= sMinJumpDistSquared && aDistSq < sMaxJumpDistSquared )
			sCandidates.push_back(aPointIdx);
	}

	if( ++sTaskProgress >= kFetchGridSize )
		sCurrentTask = eTask_None;
}


static void processNextInDirTask(ECommandDir theDir)
{
	const int kCandidateCount = intSize(sCandidates.size());
	if( sTaskProgress == 0 )
		sBestCandidateDistPenalty = 0xFFFFFFFF;

	while(sTaskProgress < kCandidateCount)
	{
		const int aPointIdx = sCandidates[sTaskProgress++];
		const TrackedPoint& aPoint = sPoints[aPointIdx];
		int dx = aPoint.x - sNormalizedCursorPos.x;
		int dy = aPoint.y - sNormalizedCursorPos.y;
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
		int aDirDist;
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
		default: DBG_ASSERT(false); aDirDist = 0;			break;
		}
		if( aDirDist <= 0 )
			continue;

		int aPerpDist;
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
		default: DBG_ASSERT(false); aPerpDist = 0;			break;
		}

		// First check if counts as being straight in desired direction,
		// which gives highest priority (lowest weight), based on dist.
		if( aPerpDist <= kMaxPerpDistForStraightLine )
		{
			if( u32(aDirDist) < sBestCandidateDistPenalty )
			{
				sNextHotspotInDir[theDir] = aPointIdx;
				sBestCandidateDistPenalty = u32(aDirDist);
			}
			continue;
		}

		// All others have at least as much penalty as full straight line,
		// plus their distance from the default no-hotspot-found jump dest.
		dx = abs(aDirDist - sBaseJumpDist);
		dy = int(aPerpDist * kPerpPenaltyMult);
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

	if( sTaskProgress >= kCandidateCount )
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
	const int aRowCount = intSize(theRows.size());
	for(int aRowIdx = theRowRangeBegin; aRowIdx < theRowRangeEnd; ++aRowIdx)
	{
		Row& aRow = theRows[aRowIdx];
		for(EVDir aVDir = EVDir(0);
			aVDir < eVDir_Num; aVDir = EVDir(aVDir+1) )
		{
			int aNextRowIdx = aRowIdx + dirDelta(aVDir);
			if( aNextRowIdx < 0 || aNextRowIdx >= aRowCount )
				continue; // leave method as _None
			Row& aNextRow = theRows[aNextRowIdx];
			aRow.method[aVDir] = aRow.findConnectMethod(aNextRow);

			switch(aRow.method[aVDir])
			{
			case Row::eConnectMethod_None:
				break;
			case Row::eConnectMethod_Basic:
				{// Link points within intersecting X range
					int aFirstDotIdx = 0;
					int aLastDotIdx = aRow.size() - 1;
					while(aFirstDotIdx < aRow.size() - 1 &&
						  aRow[aFirstDotIdx].x < aNextRow.minXp())
					{ ++aFirstDotIdx; }
					while(aLastDotIdx > 0 &&
						  aRow[aLastDotIdx].x > aNextRow.maxXp())
					{ --aLastDotIdx; }
					if( aFirstDotIdx > aLastDotIdx )
					{// No dots within intersection area - pick closest
						const int aDotLIdx =
							aRow.closestIdxTo(aNextRow.minX());
						const int aDotRIdx =
							aRow.closestIdxTo(aNextRow.maxX());
						if( abs(aNextRow.maxX() - aRow[aDotRIdx].x) <
								abs(aNextRow.minX() - aRow[aDotLIdx].x) )
							{ aFirstDotIdx = aLastDotIdx = aDotRIdx; }
						else
							{ aFirstDotIdx = aLastDotIdx = aDotLIdx; }
					}
					for(int i = aFirstDotIdx; i <= aLastDotIdx; ++i)
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
				for(int i = 0, end = aRow.size(); i < end; ++i)
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
			DBG_ASSERT(aNextRowIdx >= 0 && aNextRowIdx < aRowCount);
			// If same method in both directions, only process closer in Y
			const bool isBidirectional =
				aRow.method[aVDir] == aRow.method[oppositeDir(aVDir)];
			if( isBidirectional && aVDir == 0 )
			{
				const int aPrevRowIdx = aRowIdx - dirDelta(aVDir);
				DBG_ASSERT(aPrevRowIdx >= 0 && aPrevRowIdx < aRowCount);
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

	for(int i = 0, end = intSize(aSkipRowList.size()); i < end; ++i)
	{
		// Recursively use this function as if this row didn't exist,
		// allowing rows to link to other rows by skipping over
		// problematic ones (bi-directional splits and offsets).
		Row aTmpRow = theRows[aSkipRowList[i]];
		theRows.erase(theRows.begin() + aSkipRowList[i]);
		safeLinkHotspotRows(theRows,
			max(0, aSkipRowList[i]-1),
			min(aRowCount, aSkipRowList[i]+1));
		theRows.insert(theRows.begin() + aSkipRowList[i], aTmpRow);
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(sPoints.empty());
	const int aHotspotsCount = InputMap::hotspotCount();
	const int aHotspotArraysCount = InputMap::hotspotArrayCount();
	sFetchGrid.reserve(kGridSize * kGridSize);
	sPoints.reserve(aHotspotsCount);
	sPoints.resize(aHotspotsCount);
	sRequestedArrays.clearAndResize(aHotspotArraysCount);
	sActiveArrays.clearAndResize(aHotspotArraysCount);
	sLinkMaps.clear();
	sLastTargetSize = WindowManager::overlayTargetSize();
	sLastUIScale = gUIScale;
	sNewTasks.set();
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.contains("Hotspots") || theProfileMap.contains("Mouse") )
	{
		sNewTasks.set();
		sLinkMaps.clear();
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
	for(int x = 0; x < kGridSize; ++x)
	{
		for(int y = 0; y < kGridSize; ++y)
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


int getNextHotspotInDir(ECommandDir theDirection)
{
	if( sPoints.empty() || sRequestedArrays.none() )
		return 0;

	// Abort _nextInDir tasks in all directions besides requested
	BitArray<eTask_Num> abortedTasks; abortedTasks.reset();
	for(int aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
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


const Links& getLinks(int theMenuID)
{
	Links& result = sLinkMaps.findOrAdd(dropTo<u16>(theMenuID));
	if( !result.empty() )
		return result;

	// Generate links
	mapDebugPrint("Generating hotspot links for menu '%s'\n",
		InputMap::menuLabel(theMenuID).c_str());
	const int aNodeCount = max(1, InputMap::menuItemCount(theMenuID));
	result.resize(aNodeCount);
	if( aNodeCount == 1 ) return result;

	// Make sure hotspots' normalized positions have been assigned
	while(sNewTasks.test(eTask_TargetSize) ||
		  sCurrentTask == eTask_TargetSize)
	{ processTasks(); }

	// Assign the hotspots to "dots" in "rows" (nearly-matching Y values)
	// Also create map for converting hotspots back into menu items indexes
	VectorMap<u16, u8> aHotspotToMenuIdxMap;
	aHotspotToMenuIdxMap.reserve(aNodeCount);
	std::vector<Row> aRowVec; aRowVec.reserve(aNodeCount);
	for(int aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
	{
		const int aPointIdx =
			InputMap::menuItemHotspotID(theMenuID, aNodeIdx);
		aHotspotToMenuIdxMap.addPair(
			dropTo<u16>(aPointIdx), dropTo<u8>(aNodeIdx));
		TrackedPoint& aPoint = sPoints[aPointIdx];
		bool addedToExistingRow = false;
		for(int i = 0, end = intSize(aRowVec.size()); i < end; ++i)
		{
			Row& aRow = aRowVec[i];
			const int aYDist = abs(aRow.avgY - signed(aPoint.y));
			if( aYDist <= kMaxPerpDistForStraightLine )
			{
				aRow.addDot(aPointIdx);
				addedToExistingRow = true;
				break;
			}
		}
		if( !addedToExistingRow )
		{
			aRowVec.push_back(Row());
			aRowVec.back().addDot(aPointIdx);
		}
	}
	aHotspotToMenuIdxMap.sort();
	const int aRowCount = intSize(aRowVec.size());

	// Sort the dots horizontally in each row
	for(int i = 0; i < aRowCount; ++i)
		aRowVec[i].sortDots();

	// Sort the rows from top to bottom
	std::sort(aRowVec.begin(), aRowVec.end());
	
	// Generate vertical links with guarantee all points can be reached
	// (even if not always by the most convenient route)
	safeLinkHotspotRows(aRowVec, 0, aRowCount);

	// Add extra vertical links for columns by allowing row skips
	for(int aRowIdx = 0; aRowIdx < aRowCount; ++aRowIdx)
	{
		Row& aRow = aRowVec[aRowIdx];
		for(EVDir aVDir = EVDir(0);
			aVDir < eVDir_Num; aVDir = EVDir(aVDir+1))
		{
			for(int aDotIdx = 0, aDotsEnd = aRow.size();
				aDotIdx < aDotsEnd; ++aDotIdx)
			{
				Row::Dot& aFromDot = aRow[aDotIdx];
				u32 aBestCandidateDistPenalty = 0xFFFFFFFF;
				if( aFromDot.vertLink[aVDir] != 0 )
					continue;

				for(int aNextRowIdx = aRowIdx + dirDelta(aVDir);
					aNextRowIdx >= 0 && aNextRowIdx < aRowCount;
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
	for(int aRowIdx = 0; aRowIdx < aRowCount; ++aRowIdx)
	{
		Row& aRow = aRowVec[aRowIdx];
		for(int aDotIdx = 0, aDotsEnd = intSize(aRow.size());
			aDotIdx < aDotsEnd; ++aDotIdx )
		{
			Row::Dot& aDot = aRow[aDotIdx];
			int aPointInDir[eCmdDir_Num];
			aPointInDir[eCmdDir_U] = aDot.vertLink[eVDir_U];
			aPointInDir[eCmdDir_D] = aDot.vertLink[eVDir_D];
			if( aDotIdx == 0 )
				{ aPointInDir[eCmdDir_L] = aRow.outsideLink[eHDir_L]; }
			else if( aRow.insideLink[eHDir_L] != 0 &&
					 aRow.insideLinkDotIdx[eHDir_L] == aDotIdx )
				{ aPointInDir[eCmdDir_L] = aRow.insideLink[eHDir_L]; }
			else
				{ aPointInDir[eCmdDir_L] = aRow[aDotIdx-1].pointID; }

			if( aDotIdx == aDotsEnd - 1 )
				{ aPointInDir[eCmdDir_R] = aRow.outsideLink[eHDir_R]; }
			else if( aRow.insideLink[eHDir_R] != 0 &&
					 aRow.insideLinkDotIdx[eHDir_R] == aDotIdx )
				{ aPointInDir[eCmdDir_R] = aRow.insideLink[eHDir_R]; }
			else
				{ aPointInDir[eCmdDir_R] = aRow[aDotIdx+1].pointID; }

			const u8 aNodeIdx = aHotspotToMenuIdxMap.find(
				dropTo<u16>(aDot.pointID))->second;
			HotspotLinkNode& aNode = result[aNodeIdx];
			for(int aDir = 0; aDir < eCmdDir_Num; ++aDir)
			{
				if( aPointInDir[aDir] == 0 )
				{
					aNode.edge[aDir] = true;
					aNode.next[aDir] = aNodeIdx;
				}
				else
				{
					aNode.edge[aDir] = false;
					aNode.next[aDir] = aHotspotToMenuIdxMap.find(
						dropTo<u16>(aPointInDir[aDir]))->second;
				}
			}
		}
	}

	// Add wrap-around options for edge nodes
	for(int aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
	{
		HotspotLinkNode& aNode = result[aNodeIdx];
		for(u8 aDir = 0; aDir < eCmdDir_Num; ++aDir)
		{
			if( !aNode.edge[aDir] )
				continue;
			const ECommandDir anOppDir = opposite8Dir(ECommandDir(aDir));
			u8 aWrapNode = aNode.next[anOppDir];
			while(!result[aWrapNode].edge[anOppDir])
				aWrapNode = result[aWrapNode].next[anOppDir];
			aNode.next[aDir] = aWrapNode;
		}
	}

	return result;
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
		eState_AnchorNum,	// Checking for 50%, 10. in 10.5%, 0. in 0.75, etc
		eState_AnchorDenom,	// Checking for 5 in 0.5, 5% in 10.5%, etc
		eState_OffsetSign,	// Checking for -/+ in 50%+10, R-8, B - 5, etc
		eState_OffsetSpace,	// Checking for start of offset number after sign
		eState_OffsetNum,	// Checking for 10 in 50% + 10, CX+10, R-10, etc
		eState_OffsetDenom,	// Checking for post-decimal digits in an offset
		eState_TrailSpace,	// Check for final , or 'x' after end of coordinate
	} aState = eState_Prefix;

	u32 aNumerator = 0;
	u32 aDenominator = 0;
	u32 anAnchorNum = 0;
	u32 anAnchorDenom = 0;
	double aTotalOffset = 0;
	bool done = false;
	bool isNegativeNum = false;
	int aCharPos = 0;
	int aValidCharCount = 0;
	char c = theString[aCharPos];
	result = eResult_Ok;

	while(!done && result == eResult_Ok)
	{
		switch(c)
		{
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			switch(aState)
			{
			case eState_Prefix: // +2 = eState_AnchorNum
			case eState_OffsetSign: // +2 = eState_OffsetNum
				aState = EState(aState + 1);
				// fall through
			case eState_OffsetSpace: // +1 = eState_OffsetNum
				aState = EState(aState + 1);
				// fall through
			case eState_AnchorNum:
			case eState_AnchorDenom:
			case eState_OffsetNum:
			case eState_OffsetDenom:
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
			default:
				result = eResult_Malformed;
			}
			break;
		case '-':
		case '+':
			switch(aState)
			{
			case eState_AnchorDenom:
				anAnchorNum = aNumerator;
				anAnchorDenom = aDenominator;
				// fall through
			case eState_Prefix:
			case eState_PrefixEnd:
			case eState_OffsetSign:
				aNumerator = 0;
				aDenominator = 0;
				isNegativeNum = (c == '-');
				aState = eState_OffsetSpace;
				break;
			case eState_OffsetSpace:
				// Allow flipping the offset sign once by using a '-' just
				// before the actual number starts, like "+ -5" or "- -10"
				if( c == '-' &&
					aCharPos < intSize(theString.size()) - 1 &&
					theString[aCharPos+1] >= '0' &&
					theString[aCharPos+1] <= '9' )
				{
					isNegativeNum = !isNegativeNum;
					aState = eState_OffsetNum;
				}
				else
				{
					result = eResult_Malformed;
				}
				break;
			case eState_AnchorNum: // must actually be an offset
			case eState_OffsetNum:
			case eState_OffsetDenom:
			case eState_TrailSpace:
				// Calculate current and start new offset parsing
				if( !aDenominator ) aDenominator = 1;
				aTotalOffset +=
					(isNegativeNum ? -double(aNumerator) : double(aNumerator))
					/ double(aDenominator);
				aNumerator = 0;
				aDenominator = 0;
				isNegativeNum = (c == '-');
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
				aState = eState_AnchorNum;
				// fall through
			case eState_AnchorNum:
			case eState_OffsetNum:
				aState = EState(aState+1); // Denominator state
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
			case eState_AnchorNum:
			case eState_AnchorDenom:
				anAnchorNum = aNumerator;
				anAnchorDenom = aDenominator;
				aNumerator = 0;
				aDenominator = 0;
				if( !anAnchorDenom ) anAnchorDenom = 1;
				anAnchorDenom *= 100; // Convert 50% to 0.5
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
				anAnchorNum = 0;
				anAnchorDenom = 1;
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
				anAnchorNum = 1;
				anAnchorDenom = 1;
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
				anAnchorNum = 1;
				anAnchorDenom = 2;
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
			case eState_AnchorNum:
			case eState_AnchorDenom:
			case eState_OffsetSign:
			case eState_OffsetNum:
			case eState_OffsetDenom:
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
			case eState_AnchorDenom:
				anAnchorNum = aNumerator;
				anAnchorDenom = aDenominator;
				aNumerator = 0;
				aDenominator = 0;
				aState = eState_OffsetSign;
				break;
			case eState_AnchorNum:
			case eState_OffsetNum:
			case eState_OffsetDenom:
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
			if( aCharPos >= intSize(theString.size()) )
				done = true;
			else
				c = theString[aCharPos];
		}
	}

	if( aNumerator > 0 )
	{
		// Finalize last number read
		if( aState == eState_AnchorDenom )
		{// Just an anchor with no offset found
			DBG_ASSERT(anAnchorNum == 0);
			DBG_ASSERT(aDenominator != 0);
			anAnchorNum = aNumerator;
			anAnchorDenom = aDenominator;
		}
		else
		{// Use as last offset
			if( aDenominator == 0 )
				aDenominator = 1;
			aTotalOffset +=
				(isNegativeNum ? -double(aNumerator) : double(aNumerator))
				/ double(aDenominator);
		}
	}
	if( anAnchorDenom == 0 )
		anAnchorDenom = 1;

	// If anchor ratio invalid (> 1.0) treat as extra offset
	if( anAnchorNum > anAnchorDenom )
	{
		aTotalOffset += double(anAnchorNum) / double(anAnchorDenom);
		anAnchorNum = 0; anAnchorDenom = 1;
	}

	if( theValidatedString )
		*theValidatedString = trim(theString.substr(0, aValidCharCount));

	if( result != eResult_Ok )
		return result;

	out.anchor = ratioToU16(anAnchorNum, anAnchorDenom);

	if( aTotalOffset != 0 )
	{
		isNegativeNum = aTotalOffset < 0;
		if( isNegativeNum ) aTotalOffset = -aTotalOffset;

		double anIntPart;
		double aFracPart = std::modf(aTotalOffset, &anIntPart);
		const int aRoundedOffset = isNegativeNum
			? ((aFracPart > 0.5f) ? -int(anIntPart + 1.0f) : -int(anIntPart))
			: ((aFracPart >= 0.5f) ? int(anIntPart + 1.0f) : int(anIntPart));

		out.offset = dropTo<s16>(clamp(aRoundedOffset, -32768, 32767));
	}

	// Remove processed section from start of string
	theString = theString.substr(aCharPos);

	return result;
}

std::string hotspotToString(
	const Hotspot& theHotspot,
	EHotspotNamingStyle theStyle)
{
	std::string result;
	EHotspotNamingStyle aCoordStyle = EHotspotNamingStyle(0);
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
	const u32 kPercentResolution = 10000; // Must be evenly divisible by 10
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
