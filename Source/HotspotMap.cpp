//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "HotspotMap.h"

#include "InputMap.h"
#include "Profile.h"
#include "WindowManager.h"

namespace HotspotMap
{

// Uncomment this to print details about hotspot searches to debug window
//#define HOTSPOT_MAP_DEBUG_PRINT

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum {
// >> amount to convert 0xFFFF (normalized desktop max pos) to kGridSize
kNormalizedToGridShift = 13, // (0x10000 >> 13 = 8x8 grid)
// Grid cells per axis
kGridSize = 0x10000 >> kNormalizedToGridShift,
// If point is too close, jump FROM it rather than to it
kDefaultMinJumpDist = 0x0200,
// Max leeway in perpindicular direction to still count as "straight"
kMaxPerpDistForStraightLine = 0x0110,
// Max leeway for final "columns" step when making link maps
kMaxLinkMapColumnXDist = 0x0A00,
};

const char* kHotspotSectName = "Hotspots";
const char* kBaseJumpDistSectName = "Mouse";
const char* kBaseJumpDistPropName = "DefaultHotspotDistance";
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
	eTask_SetDistances,
	eTask_Normalize,
	eTask_AddToGrid,
	eTask_BeginSearch,
	eTask_FetchFromGrid, // auto-set by _BeginSearch
	eTask_NextInDir, // auto-set (all 8 versions) by _BeginSearch
	// Next 7 are remaining dirs in order from ECommandDir

	eTask_Num = eTask_NextInDir + eCmd8Dir_Num,
	eTask_None = eTask_Num,
};

const char* const kSpecialHotspotNames[] =
{
	"<None>",				// eSpecialHotspot_None
	"LastCursorPos",		// eSpecialHotspot_LastCursorPos
	"MouseLookStart",		// eSpecialHotspot_MouseLookStart
	"MouseHidden",			// eSpecialHotspot_MouseHidden
};
DBG_CTASSERT(ARRAYSIZE(kSpecialHotspotNames) == eSpecialHotspot_Num);


//------------------------------------------------------------------------------
// Debugging
//------------------------------------------------------------------------------

#ifdef HOTSPOT_MAP_DEBUG_PRINT
#define mapDebugPrint(...) debugPrint("HotspotMap: " __VA_ARGS__)
#else
#define mapDebugPrint(...) ((void)0)
#endif


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct ZERO_INIT(HotspotData)
{
	Hotspot hs;
	u16 nx, ny; // normalized desktop pos
	s16 ox, oy; // offset from anchor hotspot
	u16 anchorHotspotID; // other hotspot to offset from
	u16 valid : 1; // isn't just set to none/skip/blank
	u16 directAssigned : 1; // i.e. not part of a range
	u16 hasOwnXAnchor : 1; // has anchor specified - ignore anchor hotspot
	u16 hasOwnYAnchor : 1; // has anchor specified - ignore anchor hotspot
	u16 hasOwnSize : 1; // has width/height specified - ignore anchor hotspot
	u16 setID : 11; // which hotspot set belongs to
};

struct HotspotSet
{
	BitVector<512> included;
	float scale;
	u16 anchorHotspotID;
	HotspotSet() : scale(1.0f), anchorHotspotID() {}
};

struct ZERO_INIT(GridPos)
{
	int x, y;
};

typedef std::pair<u16, u16> MenuEdgeNode;
typedef std::vector<MenuEdgeNode> MenuEdge;
struct ZERO_INIT(MenuEdgeMap)
{
	MenuEdge edge[eCmdDir_Num];
	int itemCount;
};

typedef std::vector<HotspotLinkNode> MenuLinks;


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static StringToValueMap<HotspotData> sHotspots;
static StringToValueMap<HotspotSet> sHotspotSets;
static BitVector<32> sActiveHotspotSets;
static BitVector<512> sActiveHotspots;
static BitVector<512> sChangedHotspots;
static BitVector<512> sHotspotsToNormalize;
static std::vector<u16> sActiveGrid[kGridSize][kGridSize];
static std::vector<GridPos> sFetchGrid;
static std::vector<int> sCandidates;
static VectorMap<u16, MenuLinks> sLinkMaps;
static VectorMap<u16, MenuEdgeMap> sEdgeMaps;
static int sNextHotspotInDir[eCmd8Dir_Num] = { 0 };
static int sMaxDesktopAxis = 0;
static POINT sNormalizedCursorPos = { 0 };
static BitArray<eTask_Num> sNewTasks;
static ETask sCurrentTask = eTask_None;
static int sTaskProgress = 0;
static int sBaseJumpDist = 0;
static int sMaxJumpDist = 0;
static u32 sMaxJumpDistSquared = 0;
static u32 sMaxDeviationRadiusSquared = 0;
static u32 sMinJumpDistSquared = kDefaultMinJumpDist * kDefaultMinJumpDist;
static u32 sBestCandidateDistPenalty = 0xFFFFFFFF;


//------------------------------------------------------------------------------
// Row class - helper class for generating hotspot link maps
//------------------------------------------------------------------------------

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
		int hotspotID, x, y, vertLink[eVDir_Num];
		Dot(int theHotspotID = 0) :
			hotspotID(theHotspotID),
			x(sHotspots.vals()[theHotspotID].nx),
			y(sHotspots.vals()[theHotspotID].ny),
			vertLink()
		{}

		bool operator<(const Dot& rhs) const
		{ return x <rhs.x; }
	};

	// MUTATORS
	void addDot(int theHotspotID)
	{
		DBG_ASSERT(theHotspotID > 0 && theHotspotID < sHotspots.size());
		mDots.push_back(Dot(theHotspotID));
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


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static bool finalizeHotspot(int theHotspotID, Hotspot theNewHotspot)
{
	DBG_ASSERT(theHotspotID > 0 && theHotspotID < sHotspots.size());
	HotspotData& aHotspot = sHotspots.vals()[theHotspotID];
	if( !aHotspot.valid )
		return false;

	if( aHotspot.anchorHotspotID > 0 )
	{
		// Apply (scaled) offsets from anchor hotspot
		const HotspotData& anAnchorHotspot =
			sHotspots.vals()[aHotspot.anchorHotspotID];
		if( anAnchorHotspot.valid )
		{
			float anOffsetScale = 1.0f;
			if( aHotspot.setID > 0 )
			{
				DBG_ASSERT(aHotspot.setID < sHotspotSets.size());
				const HotspotSet& aHotspotSet =
					sHotspotSets.vals()[aHotspot.setID];
				if( aHotspotSet.anchorHotspotID != theHotspotID )
					anOffsetScale = aHotspotSet.scale;
			}
			if( !aHotspot.hasOwnXAnchor )
			{
				theNewHotspot.x = anAnchorHotspot.hs.x;
				theNewHotspot.x.offset = s16(clamp(
					theNewHotspot.x.offset + aHotspot.ox * anOffsetScale,
					-0x8000, 0x7FFF));
			}
			if( !aHotspot.hasOwnYAnchor )
			{
				theNewHotspot.y = anAnchorHotspot.hs.y;
				theNewHotspot.y.offset = s16(clamp(
					theNewHotspot.y.offset + aHotspot.oy * anOffsetScale,
					-0x8000, 0x7FFF));
			}
			if( !aHotspot.hasOwnSize )
			{
				theNewHotspot.w = anAnchorHotspot.hs.w;
				theNewHotspot.h = anAnchorHotspot.hs.h;
			}
		}
	}

	if( aHotspot.hs != theNewHotspot )
	{
		aHotspot.hs = theNewHotspot;
		sChangedHotspots.set(theHotspotID);
		return true;
	}

	return false;
}


static void applyHotspotProperty(
	const std::string& theKey,
	std::string theDesc,
	int theParentSet,
	bool fullParse,
	bool autoAssigned = false)
{
	// Check theKey suffix to see if single hotspot or a range of hotspots
	int aRangeStartIdx, aRangeEndIdx;
	std::string anArrayKey;
	if( !autoAssigned &&
		fetchRangeSuffix(theKey, anArrayKey, aRangeStartIdx, aRangeEndIdx) )
	{
		int aStepX = 0;
		int aStepY = 0;
		u32 applyXEvery = 0;
		u32 applyYEvery = 0;
		const bool calculateOffsets =
			fullParse && !isEffectivelyEmptyString(theDesc);
		if( calculateOffsets )
		{
			const size_t aRangeInfoStrPos = theDesc.rfind(':');
			if( aRangeInfoStrPos != std::string::npos )
			{
				size_t aStrPos = aRangeInfoStrPos;
				// X step
				aStepX = int(floor(
					stringToDoubleSum(theDesc, ++aStrPos) + 0.5));
				bool valid = aStrPos < theDesc.size() &&
					(theDesc[aStrPos] == ',' ||
					 theDesc[aStrPos] == 'x' ||
					 theDesc[aStrPos] == 'X' ||
					 theDesc[aStrPos] == '@');
				// X wrap
				if( valid && theDesc[aStrPos] == '@' )
				{
					const std::string& aWrapPointStr =
						fetchNextItem(theDesc, ++aStrPos, ",xX");
					if( aWrapPointStr[0] == '-' ||
						!isAnInteger(aWrapPointStr) )
					{
						logError(
							"Hotspot Range '%s': "
							"Value of wrap point (after @ symbol) "
							"must be a positive integer value!",
							theKey.c_str());
						valid = false;
					}
					else
					{
						applyXEvery = stringToU32(aWrapPointStr);
						valid = aStrPos < theDesc.size() &&
							(theDesc[aStrPos] == ',' ||
							 theDesc[aStrPos] == 'x' ||
							 theDesc[aStrPos] == 'X');
					}
				}
				// Y step
				if( valid )
				{
					aStepY = int(floor(
						stringToDoubleSum(theDesc, ++aStrPos) + 0.5));
					valid =
						aStrPos == theDesc.size() ||
						theDesc[aStrPos] == '@';
				}
				// Y wrap
				if( valid && theDesc[aStrPos] == '@' )
				{
					if( applyXEvery )
					{
						logError(
							"Hotspot Range '%s': "
							"Only one axis (X or Y) can have a "
							"wrapping point set (using '@' character)!",
							theKey.c_str());
					}
					else
					{
						const std::string& aWrapPointStr =
							fetchNextItem(theDesc, ++aStrPos, ",xX");
						if( aWrapPointStr[0] == '-' ||
							!isAnInteger(aWrapPointStr) )
						{
							logError(
								"Hotspot Range '%s': "
								"Value of wrap point (after @ symbol) "
								"must be a positive integer value!",
								theKey.c_str());
							valid = false;
						}
						else
						{
							applyYEvery = stringToU32(aWrapPointStr);
							valid = aStrPos == theDesc.size();
						}
					}
				}
				if( !valid )
				{
					logError("Hotspot Range '%s' expected per-hotspot offsets "
						"but unable to interpret '%s' as such!",
						theKey.c_str(),
						theDesc.substr(aRangeInfoStrPos+1).c_str());
				}
				// Cut range description off the end of theDesc
				theDesc = theDesc.substr(0, aRangeInfoStrPos);
			}
			else
			{
				logError("Hotspot Range '%s' expected per-hotspot offsets "
					"(after ':') but found none in '%s'!",
					theKey.c_str(), theDesc.c_str());
			}
		}

		// Set first hotspot directly as a normal hotspot at start pos
		applyHotspotProperty(
			anArrayKey + toString(aRangeStartIdx),
			theDesc, theParentSet, fullParse, true);

		// Remaining hotspots in the range use previous hotspots as their
		// anchor and apply as offsets only
		for(int i = 1, end = aRangeEndIdx - aRangeStartIdx + 1; i < end; ++i)
		{
			if( calculateOffsets )
			{
				int aParentIdx = aRangeStartIdx + i - 1;
				int anOffsetX = applyXEvery ? 0 : aStepX;
				int anOffsetY = applyYEvery ? 0 : aStepY;
				if( applyXEvery > 0 && i % applyXEvery == 0 )
				{
					aParentIdx = aRangeStartIdx + i - applyXEvery;
					anOffsetX = aStepX;
					anOffsetY = 0;
				}
				if( applyYEvery > 0 && i % applyYEvery == 0 )
				{
					aParentIdx = aRangeStartIdx + i - applyYEvery;
					anOffsetX = 0;
					anOffsetY = aStepY;
				}
				theDesc = anArrayKey + toString(aParentIdx) + ":" +
					toString(anOffsetX) + ", " +
					toString(anOffsetY);
			}
			applyHotspotProperty(
				anArrayKey + toString(aRangeStartIdx + i),
				theDesc, theParentSet, fullParse, true);
		}
		return;
	}

	int aHotspotID = sHotspots.findOrAddIndex(theKey);
	if( aHotspotID == sHotspots.size()-1 )
	{
		sActiveHotspots.resize(sHotspots.size());
		sChangedHotspots.resize(sHotspots.size());
		sChangedHotspots.set(sHotspots.size()-1);
		sHotspotsToNormalize.resize(sHotspots.size());
		for(int i = 0, end = sHotspotSets.size(); i < end; ++i)
			sHotspotSets.vals()[i].included.resize(sHotspots.size());
	}

	// If only collecting hotspot names for the map, stop here for now
	if( !fullParse )
		return;

	HotspotData& aHotspot = sHotspots.vals()[aHotspotID];

	// Don't allow an auto assignment to override a direct one
	if( !autoAssigned )
		aHotspot.directAssigned = true;
	else if( aHotspot.directAssigned )
		return;

	// Assign a hotspot to offset from
	aHotspot.anchorHotspotID = 0; // default "None" hotspot
	const std::string& anAnchorName = breakOffItemBeforeChar(theDesc, ':');
	if( theParentSet > 0 )
	{// Add set name to beginning of anchor name (or entirely as anchor name)
		DBG_ASSERT(theParentSet < sHotspotSets.size());
		const std::string& aSetAnchorName =
			sHotspotSets.keys()[theParentSet] + anAnchorName;
		aHotspot.anchorHotspotID = dropTo<u16>(
			sHotspots.findIndex(aSetAnchorName));
		if( aHotspot.anchorHotspotID >= sHotspots.size() )
			aHotspot.anchorHotspotID = 0;
	}
	if( !anAnchorName.empty() && aHotspot.anchorHotspotID == 0 )
	{
		aHotspot.anchorHotspotID = dropTo<u16>(
			sHotspots.findIndex(anAnchorName));
		if( aHotspot.anchorHotspotID >= sHotspots.size() )
		{
			aHotspot.anchorHotspotID = 0;
			logError("Hotspot %s: Could not find anchor hotspot named '%s'",
				theKey.c_str(),
				anAnchorName.c_str());
		}
	}
	if( aHotspot.anchorHotspotID )
	{// Confirm didn't just make an infinite parenting loop
		int aParentID = aHotspot.anchorHotspotID;
		while(aParentID != 0)
		{
			if( aParentID == aHotspotID )
			{// Infinite loop found!
				logError("Hotspot %s ends up with itself as a parent/anchor!",
					theKey.c_str());
				aHotspot.anchorHotspotID = 0;
				break;
			}
			aParentID = sHotspots.vals()[aParentID].anchorHotspotID;
		}
	}
	if( theDesc[0] == ':' )
		theDesc = theDesc.substr(1);
	
	// Add to parent set if haven't alreday done so
	// It is intentional that there is no way to remove a hotspot from a set,
	// even if it is also defined as a non-set hotspot using the same name.
	if( theParentSet && !aHotspot.setID )
	{
		aHotspot.setID = dropTo<u16>(theParentSet);
		sHotspotSets.vals()[theParentSet].included.set(aHotspotID);
	}

	if( isEffectivelyEmptyString(theDesc) )
	{// Mark as invalid hotspot (and changed if was previously valid)
		if( aHotspot.valid )
		{
			aHotspot.valid = false;
			sChangedHotspots.set(aHotspotID);
		}
		return;
	}

	Hotspot aNewHotspot;
	if( !theDesc.empty() )
	{
		// X
		size_t aStrPos = 0;
		aNewHotspot.x = stringToCoord(theDesc, aStrPos);
		bool valid = aStrPos < theDesc.size() &&
			(theDesc[aStrPos] == ',' ||
			 theDesc[aStrPos] == 'x' ||
			 theDesc[aStrPos] == 'X');
		// Y
		if( valid )
		{
			aNewHotspot.y = stringToCoord(theDesc, ++aStrPos);
			valid = aStrPos == theDesc.size() ||
				theDesc[aStrPos] == ',';
		}
		// W
		aHotspot.hasOwnSize =
			valid && aStrPos < theDesc.size() && theDesc[aStrPos] == ',';
		if( aHotspot.hasOwnSize )
		{
			const double aWidth = stringToDoubleSum(theDesc, ++aStrPos);
			aNewHotspot.w = u16(clamp(floor(aWidth + 0.5), 0, 0xFFFF));
			valid = aStrPos < theDesc.size() &&
				(theDesc[aStrPos] == ',' ||
				 theDesc[aStrPos] == 'x' ||
				 theDesc[aStrPos] == 'X');
			// H
			if( valid )
			{
				const double aHeight = stringToDoubleSum(theDesc, ++aStrPos);
				aNewHotspot.h = u16(clamp(floor(aHeight + 0.5), 0, 0xFFFF));
				valid = aStrPos == theDesc.size();
			}
		}
		if( valid )
		{
			if( !aHotspot.valid )
			{
				aHotspot.valid = true;
				sChangedHotspots.set(aHotspotID);
			}
		}
		else
		{
			logError("Hotspot %s: Error parsing hotspot description '%s'",
				theKey.c_str(), theDesc.c_str());
			aNewHotspot = Hotspot();
			aHotspot.valid = false;
		}
	}

	aHotspot.hasOwnXAnchor = aNewHotspot.x.anchor != 0;
	aHotspot.hasOwnYAnchor = aNewHotspot.y.anchor != 0;
	if( !aHotspot.hasOwnXAnchor )
		aHotspot.ox = aNewHotspot.x.offset;
	if( !aHotspot.hasOwnYAnchor )
		aHotspot.oy = aNewHotspot.y.offset;
	finalizeHotspot(aHotspotID, aNewHotspot);
}


static void processSetDistancesTask()
{
	const u64 aJumpDist = u32(max(0.0,
		Profile::getFloat(kBaseJumpDistSectName, kBaseJumpDistPropName) *
		gUIScale * 0x10000));
	const u64 aScaleFactor = sMaxDesktopAxis;
	u64 aMaxDeviationRadius = u64(aJumpDist * kDeviationRadiusMult);
	
	if( aScaleFactor > 0 )
	{
		sBaseJumpDist = dropTo<int>(
			(aJumpDist + 1) / aScaleFactor);
		sMaxJumpDist = dropTo<int>(
			(aJumpDist + aMaxDeviationRadius + 1) / aScaleFactor);
		aMaxDeviationRadius = (aMaxDeviationRadius + 1) / aScaleFactor;
		sMaxDeviationRadiusSquared = dropTo<u32>(min<u64>(
			0xFFFFFFFF,
			aMaxDeviationRadius * aMaxDeviationRadius));
		sMaxJumpDistSquared = dropTo<u32>(min<u64>(
			0xFFFFFFFF,
			u64(sMaxJumpDist) * u64(sMaxJumpDist)));

		mapDebugPrint("Set base jump distance to %d (normalized)\n", sBaseJumpDist);
		sCurrentTask = eTask_None;
	}
}


static void processNormalizeTask()
{
	if( sTaskProgress == 0 )
		sTaskProgress = sHotspotsToNormalize.firstSetBit();

	while(sTaskProgress < sHotspotsToNormalize.size() &&
		  !HotspotMap::isValidHotspotID(sTaskProgress) )
	{
		sHotspotsToNormalize.reset(sTaskProgress);
		sTaskProgress = sHotspotsToNormalize.nextSetBit(sTaskProgress);
		if( sTaskProgress >= sHotspotsToNormalize.size() )
		{
			sCurrentTask = eTask_None;
			return;
		}
	}

	if( sTaskProgress < intSize(sHotspots.size()) )
	{
		sHotspotsToNormalize.reset(sTaskProgress);
		const Hotspot& aHotspot = HotspotMap::getHotspot(sTaskProgress);
		const POINT& aDesktopPos = WindowManager::overlayPosToDesktopPos(
			WindowManager::hotspotToOverlayPos(aHotspot));
		if( sMaxDesktopAxis > 0 )
		{
			sHotspots.vals()[sTaskProgress].nx =
				ratioToU16(aDesktopPos.x, sMaxDesktopAxis);
			sHotspots.vals()[sTaskProgress].ny =
				ratioToU16(aDesktopPos.y, sMaxDesktopAxis);
		}
		mapDebugPrint(
			"Normalizing Hotspot '%s' (%d x %d) position to %d x %d \n",
			hotspotLabel(sTaskProgress),
			aDesktopPos.x, aDesktopPos.y,
			sHotspots.vals()[sTaskProgress].nx,
			sHotspots.vals()[sTaskProgress].ny);
	}

	sTaskProgress = sHotspotsToNormalize.nextSetBit(sTaskProgress);
	if( sTaskProgress >= sHotspotsToNormalize.size() )
		sCurrentTask = eTask_None;
}


static void processAddToGridTask()
{
	if( sTaskProgress == 0 )
	{
		if( sActiveHotspotSets.any() )
			mapDebugPrint("Adding enabled hotspots to grid...\n");
		for(int x = 0; x < kGridSize; ++x)
		{
			for(int y = 0; y < kGridSize; ++y)
				sActiveGrid[x][y].clear();
		}
	}

	for(int anAddedCount = 0; anAddedCount < 16; ++anAddedCount)
	{
		sTaskProgress = sActiveHotspots.nextSetBit(sTaskProgress);
		while(sTaskProgress < sActiveHotspots.size() &&
			  !sHotspots.vals()[sTaskProgress].valid)
		{
			sTaskProgress = sActiveHotspots.nextSetBit(sTaskProgress+1);
		}
		if( sTaskProgress >= sActiveHotspots.size() )
		{
			sCurrentTask = eTask_None;
			return;
		}
		const int aGridX =
			sHotspots.vals()[sTaskProgress].nx >> kNormalizedToGridShift;
		const int aGridY =
			sHotspots.vals()[sTaskProgress].ny >> kNormalizedToGridShift;
		DBG_ASSERT(aGridX >= 0 && aGridX < kGridSize);
		DBG_ASSERT(aGridY >= 0 && aGridY < kGridSize);
		mapDebugPrint(
			"Adding Hotspot '%s' to grid cell %d x %d\n",
			hotspotLabel(sTaskProgress),
			aGridX, aGridY);
		sActiveGrid[aGridX][aGridY].push_back(dropTo<u16>(sTaskProgress));
		++sTaskProgress;
	}
}


static void processBeginSearchTask()
{
	switch(sTaskProgress)
	{
	case 0:
		// Calculate normalized mouse position
		if( sMaxDesktopAxis > 0 )
		{
			sNormalizedCursorPos =
				WindowManager::overlayPosToDesktopPos(gLastCursorPos);
			sNormalizedCursorPos.x =
				ratioToU16(sNormalizedCursorPos.x, sMaxDesktopAxis);
			sNormalizedCursorPos.y =
				ratioToU16(sNormalizedCursorPos.y, sMaxDesktopAxis);
			++sTaskProgress;
		}
		break;
	case 1:
		// Reset search parameters
		sCandidates.clear();
		sFetchGrid.clear();
		for(int i = 0; i < eCmd8Dir_Num; ++i)
			sNextHotspotInDir[i] = 0;
		++sTaskProgress;
		break;
	case 2:
	default:
		{// Determine grid sections that must be searched
			const int aMinGridX(u32(clamp(sNormalizedCursorPos.x - sMaxJumpDist,
				0, 0xFFFF)) >> kNormalizedToGridShift);
			const int aMinGridY(u32(clamp(sNormalizedCursorPos.y - sMaxJumpDist,
				0, 0xFFFF)) >> kNormalizedToGridShift);
			const int aMaxGridX(u32(clamp(sNormalizedCursorPos.x + sMaxJumpDist,
				0, 0xFFFF)) >> kNormalizedToGridShift);
			const int aMaxGridY(u32(clamp(sNormalizedCursorPos.y + sMaxJumpDist,
				0, 0xFFFF)) >> kNormalizedToGridShift);
			GridPos aPos = GridPos();
			for(aPos.x = aMinGridX; aPos.x <= aMaxGridX; ++aPos.x)
			{
				for(aPos.y = aMinGridY; aPos.y <= aMaxGridY; ++aPos.y)
					sFetchGrid.push_back(aPos);
			}
		}

		// Start up fetch grid tasks
		sCurrentTask = eTask_None;
		sNewTasks.set(eTask_FetchFromGrid);
		for(u8 aDir = 0; aDir < eCmd8Dir_Num; ++aDir)
			sNewTasks.set(eTask_NextInDir + aDir);

		mapDebugPrint(
			"Beginning new search starting from %d x %d (normalized)\n",
			sNormalizedCursorPos.x, sNormalizedCursorPos.y);
		break;
	}
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
		const int aHotspotID = sActiveGrid[aGridX][aGridY][i];
		const HotspotData& aHotspot = sHotspots.vals()[aHotspotID];
		const u32 aDeltaX = abs(aHotspot.nx - sNormalizedCursorPos.x);
		const u32 aDeltaY = abs(aHotspot.ny - sNormalizedCursorPos.y);
		const u32 aDistSq = (aDeltaX * aDeltaX) + (aDeltaY * aDeltaY);
		if( aDistSq >= sMinJumpDistSquared && aDistSq < sMaxJumpDistSquared )
			sCandidates.push_back(aHotspotID);
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
		const int aHotspotID = sCandidates[sTaskProgress++];
		const HotspotData& aHotspot = sHotspots.vals()[aHotspotID];
		int dx = aHotspot.nx - sNormalizedCursorPos.x;
		int dy = aHotspot.ny - sNormalizedCursorPos.y;
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
				sNextHotspotInDir[theDir] = aHotspotID;
				sBestCandidateDistPenalty = u32(aDirDist);
			}
			continue;
		}

		// All others have at least as much penalty as full straight line,
		// plus their distance from the default no-hotspot-found jump dest.
		dx = min(aDirDist - sBaseJumpDist, 0xFFFF);
		dy = min(int(aPerpDist * kPerpPenaltyMult), 0xFFFF);
		const u32 aDistSqFromBaseDest = (dx * dx) + (dy * dy);
		if( aDistSqFromBaseDest > sMaxDeviationRadiusSquared )
			continue;

		const u32 aDistPenalty = u32(sMaxJumpDist) + aDistSqFromBaseDest;
		if( aDistPenalty < sBestCandidateDistPenalty )
		{
			sNextHotspotInDir[theDir] = aHotspotID;
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
				hotspotLabel(sNextHotspotInDir[theDir]));
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
	case eTask_SetDistances:	processSetDistancesTask();	break;
	case eTask_Normalize:		processNormalizeTask();		break;
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
								aRow[i].vertLink[aVDir] = aLinkDot.hotspotID;
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
							aNextRow.closestTo(aRow[i].x).hotspotID;
					}
				}
				break;
			case Row::eConnectMethod_OffLeftEdge:
				// Link the opposite end points
				if( aRow.leftEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_L] == 0 )
				{
					aRow.leftEdgeDot().vertLink[aVDir] =
						aNextRow.rightEdgeDot().hotspotID;
				}
				break;
			case Row::eConnectMethod_OffRightEdge:
				// Link the opposite end points
				if( aRow.rightEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_R] == 0 )
				{
					aRow.rightEdgeDot().vertLink[aVDir] =
						aNextRow.leftEdgeDot().hotspotID;
				}
				break;
			case Row::eConnectMethod_SplitOut:
				// Link end points to points just past them on other row
				if( aRow.leftEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_L] == 0 )
				{
					aRow.leftEdgeDot().vertLink[aVDir] =
						aNextRow.nextLeft(aRow.minX()).hotspotID;
				}
				if( aRow.rightEdgeDot().vertLink[aVDir] == 0 &&
					aRow.outsideLink[eHDir_R] == 0 )
				{
					aRow.rightEdgeDot().vertLink[aVDir] =
						aNextRow.nextRight(aRow.maxX()).hotspotID;
				}
				break;
			case Row::eConnectMethod_SplitIn:
				// Link points just outside other row's end points to it
				if( aRow.nextLeft(aNextRow.minX()).vertLink[aVDir] == 0 &&
					aRow.insideLink[eHDir_R] == 0 )
				{
					aRow.nextLeft(aNextRow.minX()).vertLink[aVDir] =
						aNextRow.leftEdgeDot().hotspotID;
				}
				if( aRow.nextRight(aNextRow.minX()).vertLink[aVDir] == 0 &&
					aRow.insideLink[eHDir_L] == 0 )
				{
					aRow.nextRight(aNextRow.minX()).vertLink[aVDir] =
						aNextRow.rightEdgeDot().hotspotID;
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

			const EVDir anOppVDir = oppositeDir(aVDir);
			const int aNextRowIdx = aRowIdx + dirDelta(aVDir);
			DBG_ASSERT(aNextRowIdx >= 0 && aNextRowIdx < aRowCount);
			// If same method in both v directions, can only process one of them
			const bool aVDirMethodIsOutward =
				aRow.method[aVDir] == Row::eConnectMethod_OffLeftEdge ||
				aRow.method[aVDir] == Row::eConnectMethod_OffRightEdge ||
				aRow.method[aVDir] == Row::eConnectMethod_SplitOut;
			const bool anOppVDirMethodIsOutward =
				aRow.method[anOppVDir] == Row::eConnectMethod_OffLeftEdge ||
				aRow.method[anOppVDir] == Row::eConnectMethod_OffRightEdge ||
				aRow.method[anOppVDir] == Row::eConnectMethod_SplitOut;
			const bool isBidirectional =
				aVDirMethodIsOutward == anOppVDirMethodIsOutward;
			if( isBidirectional && aVDir == 0 )
			{
				const bool aVDirMethodIsOneWay =
					aRow.method[aVDir] == Row::eConnectMethod_OffLeftEdge ||
					aRow.method[aVDir] == Row::eConnectMethod_OffRightEdge;
				const bool anOppVDirMethodIsOneWay =
					aRow.method[anOppVDir] == Row::eConnectMethod_OffLeftEdge ||
					aRow.method[anOppVDir] == Row::eConnectMethod_OffRightEdge;
				// Split-out should take priority over off-edge
				if( aVDirMethodIsOneWay &&
					aVDirMethodIsOneWay != anOppVDirMethodIsOneWay )
				{ continue; } // allow loop to process other dir instead
					
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
						aNextRow.edgeDot(oppositeDir(aHDir)).hotspotID;
					aRow.edgeDot(aHDir).vertLink[aVDir] = 0;
					if( isBidirectional )
						aRow.edgeDot(aHDir).vertLink[anOppVDir] = 0;
				}
				break;
			case Row::eConnectMethod_SplitOut:
				if( aRow.outsideLink[eHDir_L] ||
					aRow.outsideLink[eHDir_R] )
				{ break; }

				// Convert to horizontal links in both directions
				aRow.outsideLink[eHDir_L] =
					aNextRow.nextLeft(aRow.minX()).hotspotID;
				aRow.outsideLink[eHDir_R] =
					aNextRow.nextRight(aRow.minX()).hotspotID;
				aRow.leftEdgeDot().vertLink[aVDir] = 0;
				aRow.rightEdgeDot().vertLink[aVDir] = 0;
				if( isBidirectional )
				{
					aRow.leftEdgeDot().vertLink[anOppVDir] = 0;
					aRow.rightEdgeDot().vertLink[anOppVDir] = 0;
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
					aNextRow.rightEdgeDot().hotspotID;
				aRow[aRow.insideLinkDotIdx[eHDir_L]].vertLink[aVDir] = 0;

				// Right to smaller row's left-most dot
				aRow.insideLinkDotIdx[eHDir_R] =
					aRow.nextLeftIdx(aNextRow.minX());
				aRow.insideLink[eHDir_R] =
					aNextRow.leftEdgeDot().hotspotID;
				aRow[aRow.insideLinkDotIdx[eHDir_R]].vertLink[aVDir] = 0;

				if( isBidirectional )
				{
					aRow[aRow.insideLinkDotIdx[eHDir_L]]
						.vertLink[anOppVDir] = 0;
					aRow[aRow.insideLinkDotIdx[eHDir_R]]
						.vertLink[anOppVDir] = 0;
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


static bool addHotspotSet(
	const Profile::SectionsMap& theSectionsMap,
	int theSectionID, const std::string& thePrefix, void*)
{
	const std::string& aSectionName =
		theSectionsMap.keys()[theSectionID];
	const std::string& aHotspotSetName =
		aSectionName.substr(posAfterPrefix(aSectionName, thePrefix));
	if( !aHotspotSetName.empty() )
		sHotspotSets.findOrAdd(aHotspotSetName);
	return true;
}


static void loadHotspotDataFromProfile(
	const Profile::SectionsMap& theProfileMap)
{
	// Need to parse properties twice - once to add hotspot names for
	// possible reference by other hotspots, and then the full parse
	for(int aParseMode = 0; aParseMode < 2; ++aParseMode)
	{
		// Parse non-set Hotspots
		if( Profile::PropertyMapPtr aPropMap =
				theProfileMap.find(kHotspotSectName) )
		{
			for(int aPropIdx = 0; aPropIdx < aPropMap->size(); ++aPropIdx)
			{
				applyHotspotProperty(
					aPropMap->keys()[aPropIdx],
					aPropMap->vals()[aPropIdx].str,
					0, aParseMode != 0);
			}
		}

		// Parse hotspots in sets
		for(int i = 1, end = sHotspotSets.size(); i < end; ++i)
		{
			Profile::PropertyMapPtr aPropMap = theProfileMap.find(
				kHotspotSectName + std::string(".") +
				sHotspotSets.keys()[i]);
			if( !aPropMap )
				continue;
			if( !sHotspotSets.vals()[i].anchorHotspotID )
			{// Make sure has an anchor hotspot assigned (even if empty)
				applyHotspotProperty(
					sHotspotSets.keys()[i], "", 0, false, true);
				const int anAnchorIdx =
					sHotspots.findIndex(sHotspotSets.keys()[i]);
				DBG_ASSERT(anAnchorIdx != 0);
				DBG_ASSERT(anAnchorIdx < sHotspots.size());
				sHotspotSets.vals()[i].anchorHotspotID =
					dropTo<u16>(anAnchorIdx);
			}
			for(int aPropIdx = 0; aPropIdx < aPropMap->size(); ++aPropIdx)
			{
				const std::string& aCondensedKey =
					condense(aPropMap->keys()[aPropIdx]);
				if( aCondensedKey == "ANCHOR" ||
					aCondensedKey == "BASE" )
				{
					// Anchor hotspots aren't considered actually IN the set,
					// so still send in a setID of 0
					applyHotspotProperty(
						sHotspotSets.keys()[i],
						aPropMap->vals()[aPropIdx].str,
						0, aParseMode != 0);
				}
				else if( aCondensedKey == "SCALE" )
				{
					if( aParseMode != 0 )
					{
						const float aNewScale =
							stringToFloat(aPropMap->vals()[aPropIdx].str);
						if( aNewScale != sHotspotSets.vals()[i].scale )
						{
							sHotspotSets.vals()[i].scale = aNewScale;
							// Mark anchor hotspot as changded so re-apply
							// scale to all hotspot offsets in this set
							const int anAnchorID =
								sHotspotSets.vals()[i].anchorHotspotID;
							DBG_ASSERT(anAnchorID != 0);
							sChangedHotspots.set(anAnchorID);
						}
					}
				}
				else
				{
					// Parse hotspot assigned to this set
					applyHotspotProperty(
						sHotspotSets.keys()[i] + aPropMap->keys()[aPropIdx],
						aPropMap->vals()[aPropIdx].str,
						i, aParseMode != 0);
				}
			}
		}
	}

	// Changes to anchor hotspots need to be applied to offet hotspots
	// Continue updating until a full pass happens with no new changes
	bool needApplyOffsets = sChangedHotspots.any();
	while(needApplyOffsets)
	{
		needApplyOffsets = false;
		for(int i = 1, end = sHotspots.size(); i < end; ++i)
		{
			HotspotData& aHotspot = sHotspots.vals()[i];
			if( aHotspot.anchorHotspotID > 0 &&
				sChangedHotspots.test(aHotspot.anchorHotspotID) &&
				finalizeHotspot(i, sHotspots.vals()[i].hs) )
			{
				needApplyOffsets = true;
			}
		}
	}

	if( sTaskProgress > 0 )
		sCurrentTask = eTask_None;

	// Report changed hotspots (done after the fact since a single hotspot
	// property can change multiple hotspots at once due to ranges).
	#ifdef HOTSPOT_MAP_DEBUG_PRINT
	for(int aHotspotID = sChangedHotspots.firstSetBit();
		aHotspotID < sChangedHotspots.size();
		aHotspotID = sChangedHotspots.nextSetBit(aHotspotID+1))
	{
		if( !sHotspots.vals()[aHotspotID].valid )
		{
			mapDebugPrint("Assigned '%s' to EMPTY (invalid)\n",
				hotspotLabel(aHotspotID));
		}
		else
		{
			const Hotspot& aHotspot = sHotspots.vals()[aHotspotID].hs;
			const float aScale = hotspotScale(aHotspotID);
			mapDebugPrint("Assigned '%s' to %d%s%dx, %d%s%dy, %dw, %dh\n",
				hotspotLabel(aHotspotID),
				int(aHotspot.x.anchor / 655.36 + 0.5),
				aHotspot.x.offset >= 0 ? "%+" : "%",
				aHotspot.x.offset,
				int(aHotspot.y.anchor / 655.36 + 0.5),
				aHotspot.y.offset >= 0 ? "%+" : "%",
				aHotspot.y.offset,
				int(aHotspot.w * aScale),
				int(aHotspot.h * aScale));
		}
	}
	#endif
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void loadProfile()
{
	DBG_ASSERT(sHotspots.empty());
	DBG_ASSERT(sHotspotSets.empty());
	sFetchGrid.reserve(kGridSize * kGridSize);

	// Create hotspot sets (starting with default unnamed set)
	sHotspotSets.setValue(kSpecialHotspotNames[0], HotspotSet());
	Profile::allSections().findAllWithPrefix(
		kHotspotSectName + std::string("."), addHotspotSet);

	// Create the special hotspots first so they get correct IDs
	for(int i = 0; i < eSpecialHotspot_Num; ++i)
		sHotspots.setValue(kSpecialHotspotNames[i], HotspotData());
	sChangedHotspots.clearAndResize(eSpecialHotspot_Num);
	sChangedHotspots.set();
	sChangedHotspots.reset(eSpecialHotspot_None);
	sChangedHotspots.reset(eSpecialHotspot_LastCursorPos);

	// Fetch and apply hotspot properties
	loadHotspotDataFromProfile(Profile::allSections());
	sChangedHotspots.reset();

	// Queue ALL tasks initially
	sNewTasks.set();

	// Set initial desktop size for normalizing
	resize();
}


void loadProfileChanges()
{
	// Load any changes to hotspots themselves first (to flag changed ones)
	loadHotspotDataFromProfile(Profile::changedSections());

	// Unlikely, but did base jump distance change?
	if( Profile::PropertyMapPtr aPropMap =
			Profile::changedSections().find(kBaseJumpDistSectName) )
	{
		if( aPropMap->find(kBaseJumpDistPropName) )
		{
			sNewTasks.set(eTask_SetDistances);
			sNewTasks.set(eTask_BeginSearch);
		}
	}

	// Check for hotspot changes for any menu link maps
	for(int i = 0, end = intSize(sLinkMaps.size()); i < end; ++i)
	{
		MenuLinks& aLinks = sLinkMaps[i].second;
		const int aMenuID = sLinkMaps[i].first;
		if( InputMap::menuItemCount(aMenuID) != intSize(aLinks.size()) ||
			InputMap::menuHotspotsChanged(aMenuID) )
		{
			aLinks.clear();
		}
	}

	// Check for hotspot changes in any menu edge maps
	for(int i = 0, end = intSize(sEdgeMaps.size()); i < end; ++i)
	{
		MenuEdgeMap& anEdgeMap = sEdgeMaps[i].second;
		const int aMenuID = sEdgeMaps[i].first;
		const int anItemCount = InputMap::menuItemCount(aMenuID);
		if( anItemCount != anEdgeMap.itemCount ||
			InputMap::menuHotspotsChanged(aMenuID) )
		{
			anEdgeMap.itemCount = anItemCount;
			anEdgeMap.edge[eCmdDir_L].clear();
			anEdgeMap.edge[eCmdDir_R].clear();
			anEdgeMap.edge[eCmdDir_U].clear();
			anEdgeMap.edge[eCmdDir_D].clear();
		}
	}

	// Check for any hotspots need to re-normalize and thus re-process grid
	sHotspotsToNormalize |= sChangedHotspots;
	sHotspotsToNormalize.reset(eSpecialHotspot_None);
	sHotspotsToNormalize.reset(eSpecialHotspot_LastCursorPos);
	if( sHotspotsToNormalize.any() )
	{
		sNewTasks.set(eTask_Normalize);
		sNewTasks.set(eTask_AddToGrid);
		sNewTasks.set(eTask_BeginSearch);
		if( gHotspotsGuideMode == eHotspotGuideMode_Showing )
			gHotspotsGuideMode = eHotspotGuideMode_Redraw;
	}
}


void cleanup()
{
	sHotspots.clear();
	sHotspotSets.clear();
	sActiveHotspotSets.clear();
	sActiveHotspots.clear();
	sChangedHotspots.clear();
	sHotspotsToNormalize.clear();
	sFetchGrid.clear();
	sCandidates.clear();
	sLinkMaps.clear();
	sEdgeMaps.clear();
	sNewTasks.reset();
	sCurrentTask = eTask_None;
	sTaskProgress = 0;
	for(int x = 0; x < kGridSize; ++x)
	{
		for(int y = 0; y < kGridSize; ++y)
			sActiveGrid[x][y].clear();
	}
}


void resize()
{
	if( sHotspots.empty() )
		return;

	// Recalculate where points are on the desktop and normalize them
	sHotspotsToNormalize.set();
	sHotspotsToNormalize.reset(eSpecialHotspot_None);
	sHotspotsToNormalize.reset(eSpecialHotspot_LastCursorPos);
	sNewTasks.set(eTask_SetDistances);
	sNewTasks.set(eTask_Normalize);
	sNewTasks.set(eTask_AddToGrid);
	sNewTasks.set(eTask_BeginSearch);
	sMaxDesktopAxis = max(
		GetSystemMetrics(SM_CXVIRTUALSCREEN),
		GetSystemMetrics(SM_CYVIRTUALSCREEN));
}


void update()
{
	processTasks();
}


const Hotspot& getHotspot(int theHotspotID)
{
	DBG_ASSERT(size_t(theHotspotID) < size_t(sHotspots.size()));
	return sHotspots.vals()[theHotspotID].hs;
}


const BitVector<512>& getHotspotSet(int theHotspotSetID)
{
	DBG_ASSERT(size_t(theHotspotSetID) < size_t(sHotspotSets.size()));
	return sHotspotSets.vals()[theHotspotSetID].included;
}


int hotspotCount()
{
	return sHotspots.size();
}


int hotspotSetCount()
{
	return sHotspotSets.size();
}


int hotspotIDFromName(const std::string& theHotspotName)
{
	int result = sHotspots.findIndex(theHotspotName);
	if( result >= sHotspots.size() )
		result = eSpecialHotspot_None;
	return result;
}


int hotspotSetIDFromName(const std::string& theHotspotSetName)
{
	int result = sHotspotSets.findIndex(theHotspotSetName);
	if( result >= sHotspotSets.size() )
		result = 0;
	return result;
}


bool isValidHotspotID(int theHotspotID)
{
	return
		theHotspotID > 0 &&
		theHotspotID < sHotspots.size() &&
		sHotspots.vals()[theHotspotID].valid;
}


float hotspotScale(int theHotspotID)
{
	DBG_ASSERT(size_t(theHotspotID) < size_t(sHotspots.size()));
	return sHotspotSets.vals()[sHotspots.vals()[theHotspotID].setID].scale;
}


const char* hotspotLabel(int theHotspotID)
{
	DBG_ASSERT(theHotspotID >= 0);
	DBG_ASSERT(theHotspotID < sHotspots.size());
	return sHotspots.keys()[theHotspotID].c_str();
}


const char* hotspotSetLabel(int theHotspotSetID)
{
	DBG_ASSERT(theHotspotSetID >= 0);
	DBG_ASSERT(theHotspotSetID < sHotspotSets.size());
	return sHotspotSets.keys()[theHotspotSetID].c_str();
}


const BitVector<512>& changedHotspots()
{
	return sChangedHotspots;
}


void resetChangedHotspots()
{
	sChangedHotspots.reset();
}


bool setLastCursorPos(POINT theNewPos)
{
	theNewPos = WindowManager::overlayPosValidated(theNewPos);
	if( theNewPos.x != gLastCursorPos.x || theNewPos.y != gLastCursorPos.y )
	{
		gLastCursorPos = theNewPos;
		sChangedHotspots.set(eSpecialHotspot_LastCursorPos);
		sHotspots.vals()[eSpecialHotspot_LastCursorPos].hs =
			WindowManager::overlayPosToHotspot(theNewPos);
		sNewTasks.set(eTask_BeginSearch);
		return true;
	}

	return false;
}


void setEnabledHotspotSets(const BitVector<32>& theHotspotSets)
{
	if( sActiveHotspotSets != theHotspotSets )
	{
		#ifdef HOTSPOT_MAP_DEBUG_PRINT
		for(int i = 0, end = sActiveHotspotSets.size(); i < end; ++i)
		{
			if( theHotspotSets.test(i) && !sActiveHotspotSets.test(i) )
			{
				mapDebugPrint(
					"Enabling hotspots in Hotspot Set '%s'\n",
					hotspotSetLabel(i));
			}
			else if( !theHotspotSets.test(i) && sActiveHotspotSets.test(i) )
			{
				mapDebugPrint(
					"Disabling hotspots in Hotspot Set '%s'\n",
					hotspotSetLabel(i));
			}
		}
		#endif
		sActiveHotspotSets = theHotspotSets;
		sActiveHotspots.reset();
		for(int aHotspotSet = sActiveHotspotSets.firstSetBit();
			aHotspotSet < sActiveHotspotSets.size();
			aHotspotSet = sActiveHotspotSets.nextSetBit(aHotspotSet+1))
		{
			sActiveHotspots |= sHotspotSets.vals()[aHotspotSet].included;
		}
		sNewTasks.set(eTask_AddToGrid);
		sNewTasks.set(eTask_BeginSearch);
		if( gHotspotsGuideMode == eHotspotGuideMode_Showing )
			gHotspotsGuideMode = eHotspotGuideMode_Redraw;
	}
}


const BitVector<512>& enabledHotspots()
{
	return sActiveHotspots;
}


int getNextHotspotInDir(ECommandDir theDirection)
{
	if( sHotspots.empty() || sActiveHotspotSets.none() )
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


HotspotLinkNode getMenuHotspotsLink(int theMenuID, int theMenuItemIdx)
{
	MenuLinks& aLinkVec = sLinkMaps.findOrAdd(dropTo<u16>(theMenuID));
	if( !aLinkVec.empty() )
		return aLinkVec[min(theMenuItemIdx, intSize(aLinkVec.size())-1)];

	// Generate links
	mapDebugPrint("Generating hotspot links for menu '%s'\n",
		InputMap::menuLabel(theMenuID));
	const int aNodeCount = max(1, InputMap::menuItemCount(theMenuID));
	aLinkVec.resize(aNodeCount);
	if( aNodeCount == 1 )
		return aLinkVec[min(theMenuItemIdx, intSize(aLinkVec.size())-1)];

	// Make sure hotspots' normalized positions have been assigned
	while(sNewTasks.test(eTask_Normalize) ||
		  sCurrentTask == eTask_Normalize)
	{ processTasks(); }

	// Assign the hotspots to "dots" in "rows" (nearly-matching Y values)
	// Also create map for converting hotspots back into menu items indexes
	VectorMap<u16, u8> aHotspotToMenuIdxMap;
	aHotspotToMenuIdxMap.reserve(aNodeCount);
	std::vector<Row> aRowVec; aRowVec.reserve(aNodeCount);
	for(int aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
	{
		const int aHotspotID =
			InputMap::menuItemHotspotID(theMenuID, aNodeIdx);
		aHotspotToMenuIdxMap.addPair(
			dropTo<u16>(aHotspotID), dropTo<u8>(aNodeIdx));
		if( !isValidHotspotID(aHotspotID) )
		{
			const int aDefaultItemIdx = InputMap::menuDefaultItemIdx(theMenuID);
			for(int aDir = 0; aDir < eCmdDir_Num; ++aDir)
			{
				aLinkVec[aNodeIdx].next[aDir] = dropTo<u8>(aDefaultItemIdx);
				aLinkVec[aNodeIdx].edge[aDir] = false;
			}
			continue;
		}
		HotspotData& aHotspot = sHotspots.vals()[aHotspotID];
		bool addedToExistingRow = false;
		for(int i = 0, end = intSize(aRowVec.size()); i < end; ++i)
		{
			Row& aRow = aRowVec[i];
			const int aYDist = abs(aRow.avgY - signed(aHotspot.ny));
			if( aYDist <= kMaxPerpDistForStraightLine )
			{
				aRow.addDot(aHotspotID);
				addedToExistingRow = true;
				break;
			}
		}
		if( !addedToExistingRow )
		{
			aRowVec.push_back(Row());
			aRowVec.back().addDot(aHotspotID);
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
						aFromDot.vertLink[aVDir] = aToDot.hotspotID;
						aBestCandidateDistPenalty = aDistPenalty;
					}
				}
			}
		}
	}

	// Up to this point, have restricted some links that could logically be
	// made in order to allow for wrapping to possibly be a better choice
	// (cause a more straight line of position change) when wrapping is
	// enabled. However, in cases where both directions in an axis have no
	// link for a given dot, even with wrapping on those directions will do
	// nothing. For those cases, enabled a "lenient" link to nearby dots.
	for(int aRowIdx = 0; aRowIdx < aRowCount; ++aRowIdx)
	{
		Row& aRow = aRowVec[aRowIdx];
		if( aRow.size() == 1 &&
			aRow.outsideLink[eHDir_L] == 0 &&
			aRow.outsideLink[eHDir_R] == 0 )
		{// Row with 1 dot and no horizontal links - find some if can!
			const Row::Dot& aRefDot = aRow[0];
			// Find best candidates above and below to either side
			Row::Dot* aBestAboveLeft = null;
			Row::Dot* aBestAboveRight = null;
			Row::Dot* aBestBelowLeft = null;
			Row::Dot* aBestBelowRight = null;
			for(int aNextRowIdx = aRowIdx - 1;
				aNextRowIdx >= 0 && (!aBestAboveLeft || !aBestAboveRight);
				--aNextRowIdx)
			{
				Row& aNextRow = aRowVec[aNextRowIdx];
				int aClosestDotIdx = aNextRow.closestIdxTo(aRefDot.x);
				if( !aBestAboveLeft )
				{
					int aDotIdx = aClosestDotIdx;
					while(aDotIdx >= 0 &&
						  aNextRow[aDotIdx].x >=
							aRefDot.x - kMaxPerpDistForStraightLine)
					{ --aDotIdx; }
					if( aDotIdx >= 0 )
						aBestAboveLeft = &aNextRow[aDotIdx];
				}
				if( !aBestAboveLeft )
				{
					int aDotIdx = aClosestDotIdx;
					while(aDotIdx < aNextRow.size() &&
						  aNextRow[aDotIdx].x <=
							aRefDot.x + kMaxPerpDistForStraightLine)
					{ ++aDotIdx; }
					if( aDotIdx < aNextRow.size() )
						aBestAboveRight = &aNextRow[aDotIdx];
				}
			}
			for(int aNextRowIdx = aRowIdx + 1, end = intSize(aRowVec.size());
				aNextRowIdx < end && (!aBestBelowLeft || !aBestBelowRight);
				++aNextRowIdx)
			{
				Row& aNextRow = aRowVec[aNextRowIdx];
				int aClosestDotIdx = aNextRow.closestIdxTo(aRefDot.x);
				if( !aBestAboveLeft )
				{
					int aDotIdx = aClosestDotIdx;
					while(aDotIdx >= 0 &&
						  aNextRow[aDotIdx].x >=
							aRefDot.x - kMaxPerpDistForStraightLine)
					{ --aDotIdx; }
					if( aDotIdx >= 0 )
						aBestBelowLeft = &aNextRow[aDotIdx];
				}
				if( !aBestAboveLeft )
				{
					int aDotIdx = aClosestDotIdx;
					while(aDotIdx < aNextRow.size() &&
						  aNextRow[aDotIdx].x <=
							aRefDot.x + kMaxPerpDistForStraightLine)
					{ ++aDotIdx; }
					if( aDotIdx < aNextRow.size() )
						aBestBelowRight = &aNextRow[aDotIdx];
				}
			}
			Row::Dot* aBestDot = aBestBelowLeft;
			if( aBestAboveLeft )
			{
				if( !aBestDot ||
					abs(aBestAboveLeft->y - aRefDot.y) <
						abs(aBestDot->y - aRefDot.y) )
				{
					aBestDot = aBestAboveLeft;
				}
			}
			if( aBestDot )
				aRow.outsideLink[eHDir_L] = aBestDot->hotspotID;

			aBestDot = aBestBelowRight;
			if( aBestAboveRight )
			{
				if( !aBestDot ||
					abs(aBestAboveRight->y - aRefDot.y) <
						abs(aBestDot->y - aRefDot.y) )
				{
					aBestDot = aBestAboveRight;
				}
			}
			if( aBestDot )
				aRow.outsideLink[eHDir_R] = aBestDot->hotspotID;
		}
		for(int aDotIdx = 0, aDotsEnd = intSize(aRow.size());
			aDotIdx < aDotsEnd; ++aDotIdx )
		{
			Row::Dot& aDot = aRow[aDotIdx];
			if( aDot.vertLink[eVDir_U] == 0 &&
				aDot.vertLink[eVDir_D] == 0 )
			{
				if( aRowIdx > 0 )
				{
					aDot.vertLink[eVDir_U] =
						aRowVec[aRowIdx-1].closestTo(aDot.x).hotspotID;
				}
				if( aRowIdx < intSize(aRowVec.size())-1 )
				{
					aDot.vertLink[eVDir_D] =
						aRowVec[aRowIdx+1].closestTo(aDot.x).hotspotID;
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
				{ aPointInDir[eCmdDir_L] = aRow[aDotIdx-1].hotspotID; }

			if( aDotIdx == aDotsEnd - 1 )
				{ aPointInDir[eCmdDir_R] = aRow.outsideLink[eHDir_R]; }
			else if( aRow.insideLink[eHDir_R] != 0 &&
					 aRow.insideLinkDotIdx[eHDir_R] == aDotIdx )
				{ aPointInDir[eCmdDir_R] = aRow.insideLink[eHDir_R]; }
			else
				{ aPointInDir[eCmdDir_R] = aRow[aDotIdx+1].hotspotID; }

			const u8 aNodeIdx = aHotspotToMenuIdxMap.find(
				dropTo<u16>(aDot.hotspotID))->second;
			HotspotLinkNode& aNode = aLinkVec[aNodeIdx];
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
		HotspotLinkNode& aNode = aLinkVec[aNodeIdx];
		for(u8 aDir = 0; aDir < eCmdDir_Num; ++aDir)
		{
			if( !aNode.edge[aDir] )
				continue;
			const ECommandDir anOppDir = oppositeDir(ECommandDir(aDir));
			u8 aWrapNode = aNode.next[anOppDir];
			while(!aLinkVec[aWrapNode].edge[anOppDir])
				aWrapNode = aLinkVec[aWrapNode].next[anOppDir];
			aNode.next[aDir] = aWrapNode;
		}
	}

	return aLinkVec[min(theMenuItemIdx, intSize(aLinkVec.size())-1)];
}


int getEdgeMenuItem(int theMenuID, ECommandDir theDir, int theDefault)
{
	DBG_ASSERT(theDir < eCmdDir_Num);
	const int aNodeCount = InputMap::menuItemCount(theMenuID);
	if( aNodeCount <= 1 )
		return 0;
	MenuEdgeMap& anEdgeMap = sEdgeMaps.findOrAdd(dropTo<u16>(theMenuID));
	MenuEdge& anEdge = anEdgeMap.edge[theDir];
	if( anEdge.empty() )
	{
		// Generate sorted edge nodes list
		mapDebugPrint("Generating hotspot edge %d for menu '%s'\n",
			theDir, InputMap::menuLabel(theMenuID));

		// Track total count so if it changes can clear and rebuild this data
		anEdgeMap.itemCount = aNodeCount;

		// Make sure hotspots' normalized positions have been assigned
		while(sNewTasks.test(eTask_Normalize) ||
			  sCurrentTask == eTask_Normalize)
		{ processTasks(); }

		int anEdgeAvgPos = -1;
		int anEdgeTotalPos = 0;
		for(int aNodeIdx = 0; aNodeIdx < aNodeCount; ++aNodeIdx)
		{
			const HotspotData& aHotspot = sHotspots.vals()[
				InputMap::menuItemHotspotID(theMenuID, aNodeIdx)];
			int posInDir = -1, posInPerpDir = 0;
			switch(theDir)
			{
			case eCmdDir_L:
				posInDir = 0xFFFF - aHotspot.nx;
				posInPerpDir = aHotspot.ny;
				break;
			case eCmdDir_R:
				posInDir = aHotspot.nx;
				posInPerpDir = aHotspot.ny;
				break;
			case eCmdDir_U:
				posInDir = 0xFFFF - aHotspot.ny;
				posInPerpDir = aHotspot.nx;
				break;
			case eCmdDir_D:
				posInDir = aHotspot.ny;
				posInPerpDir = aHotspot.nx;
				break;
			}
			if( posInDir + kMaxPerpDistForStraightLine < anEdgeAvgPos )
				continue;
			if( anEdgeTotalPos == 0 ||
				posInDir - kMaxPerpDistForStraightLine > anEdgeAvgPos )
			{
				anEdge.clear();
				anEdgeTotalPos = 0;
			}
			anEdge.push_back(MenuEdgeNode(
				dropTo<u16>(posInPerpDir),
				dropTo<u16>(aNodeIdx)));
			anEdgeTotalPos += posInDir;
			anEdgeAvgPos = anEdgeTotalPos / intSize(anEdge.size());
		}
		std::sort(anEdge.begin(), anEdge.end());
		if( anEdge.size() < anEdge.capacity() )
			MenuEdge(anEdge).swap(anEdge);
	}
	DBG_ASSERT(!anEdge.empty());

	if( anEdge.size() == 1 )
		return anEdge[0].second;

	const HotspotData& aDefaultHotspot =
		sHotspots.vals()[InputMap::menuItemHotspotID(theMenuID, theDefault)];

	const MenuEdgeNode aSearchNode(
		(theDir == eCmdDir_L || theDir == eCmdDir_R)
			? aDefaultHotspot.ny : aDefaultHotspot.nx,
		0);
	MenuEdge::const_iterator aNextNode = std::lower_bound(
		anEdge.begin(), anEdge.end(), aSearchNode);
	if( aNextNode == anEdge.begin() )
		return aNextNode->second;

	MenuEdge::const_iterator aPrevNode = aNextNode - 1;
	if( aNextNode == anEdge.end() )
		return aPrevNode->second;

	const int aPrevDist = aSearchNode.first - aPrevNode->first;
	const int aNextDist = aNextNode->first - aSearchNode.first;

	return (aPrevDist <= aNextDist) ? aPrevNode->second : aNextNode->second;
}

#undef mapDebugPrint
#undef HOTSPOT_MAP_DEBUG_PRINT

} // HotspotMap
