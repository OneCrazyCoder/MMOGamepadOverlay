//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "WindowPainter.h"

#include "HotspotMap.h"
#include "InputMap.h"
#include "Menus.h"
#include "Profile.h"

namespace WindowPainter
{

// Make the region of each overlay window obvious by drawing a frame
//#define DEBUG_DRAW_OVERLAY_FRAMES
// Make the overall overlay region obvious by drawing a frame
//#define DEBUG_DRAW_TOTAL_OVERLAY_FRAME

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum {
kMinFontPixelHeight = 6,
kNoticeStringDisplayTimePerChar = 50,
kNoticeStringMinTime = 3000,
kSystemOverlayFlashFreq = 125,
kSystemOverlayFlashTime = kSystemOverlayFlashFreq * 8,
kSystemMenuID = 0,
kSystemOverlayID = 0,
kHotspotGuideMenuID = 1,
kHotspotGuideOverlayID = 1,
};

enum EAlignment
{
	eAlignment_Min,		// L or T
	eAlignment_Center,	// CX or CY
	eAlignment_Max,		// R or B
};

enum EMenuItemDrawState
{
	eMenuItemDrawState_Normal,
	eMenuItemDrawState_Selected,
	eMenuItemDrawState_Flash,
	eMenuItemDrawState_FlashSelected,

	eMenuItemDrawState_Num
};

enum EMenuItemLabelType
{
	eMenuItemLabelType_Unknown,
	eMenuItemLabelType_String,
	eMenuItemLabelType_Bitmap,
	eMenuItemLabelType_CopyRect,
};

const char* kDrawStatePrefix[] = { "", "Selected", "Flash", "FlashSelected" };
DBG_CTASSERT(ARRAYSIZE(kDrawStatePrefix) == eMenuItemDrawState_Num);

const char* kMenuDefaultSectionName = "Appearance";
const char* kMenuSectionPrefix = "Menu.";
const char* kBitmapsSectionName = "Bitmaps";
const char* kIconsSectionName = "LabelIcons";
const char* kHotspotSizesSectionName = "HotspotSizes";
const char* kAppVersionString = "Version: " __DATE__;
const char* kPositionPropName = "Position";
const char* kItemTypePropName = "ItemType";
const char* kItemSizePropName = "ItemSize";
const char* kSizePropName = "Size";
const char* kAlignmentPropName = "Alignment";
const char* kFontNamePropName = "Font";
const char* kFontSizePropName = "FontSize";
const char* kFontWeightPropName = "FontWeight";
const char* kFontColorPropName = "LabelRGB";
const char* kItemColorPropName = "ItemRGB";
const char* kBorderColorPropName = "BorderRGB";
const char* kBorderSizePropName = "BorderSize";
const char* kGapSizePropName = "GapSize";
const char* kTitleColorPropName = "TitleRGB";
const char* kTransColorPropName = "TransRGB";
const char* kMaxAlphaPropName = "MaxAlpha";
const char* kFadeInDelayPropName = "FadeInDelay";
const char* kFadeInTimePropName = "FadeInTime";
const char* kFadeOutDelayPropName = "FadeOutDelay";
const char* kFadeOutTimePropName = "FadeOutTime";
const char* kInactiveDelayPropName = "InactiveDelay";
const char* kInactiveAlphaPropName = "InactiveAlpha";
const char* kBitmapPropName = "Bitmap";
const char* kRadiusPropName = "Radius";
const char* kTitleHeightPropName = "TitleHeight";
const char* kAltLabelWidthPropName = "AltLabelWidth";
const char* kFlashTimePropName = "FlashTime";
const char* kDrawPriorityPropName = "Priority";


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct PropString
{
	std::string str;
	bool valid;

	typedef void (PropString::*boolType)() const;
	void btFunc() const {}
	operator boolType() const { return valid ? &PropString::btFunc : 0; }
};

struct ZERO_INIT(HotspotSizesRange)
{
	u16 firstHotspotID;
	u16 lastHotspotID;
	u16 width;
	u16 height;
};

struct ZERO_INIT(FontInfo)
{
	HFONT handle;
	std::string name;
	int size;
	int weight;

	bool operator==(const FontInfo& rhs) const
	{ return name == name && size == rhs.size && weight == rhs.weight; }
};

struct ZERO_INIT(PenInfo)
{
	HPEN handle;
	COLORREF color;
	int width;
};

struct ZERO_INIT(BitmapFileInfo)
{
	std::string path;
	HBITMAP image;
	COLORREF maskColor;
};

struct ZERO_INIT(BitmapIcon)
{
	HBITMAP image;
	HBITMAP mask;
	POINT fileTL;
	SIZE size;
	u16 bitmapFileID;
};

struct ZERO_INIT(LabelIcon)
{
	union
	{
		u16 bitmapIconID;
		u16 hotspotID;
	};
	bool copyFromScreen;
};

struct ZERO_INIT(MenuPosition)
{
	Hotspot base;
	u16 parentKBCycleID;
	s8 drawPriority;

	bool operator==(const MenuPosition& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(MenuPosition)) == 0; }
};

struct ZERO_INIT(MenuLayout)
{
	EMenuStyle style : 16;
	EAlignment alignmentX : 8;
	EAlignment alignmentY : 8;
	u16 sizeX;
	u16 sizeY;
	u16 titleHeight;
	u16 altLabelWidth;
	s8 gapSizeX;
	s8 gapSizeY;

	bool operator==(const MenuLayout& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(MenuLayout)) == 0; }
};

struct ZERO_INIT(MenuAppearance)
{
	EMenuItemType itemType;
	COLORREF transColor;
	COLORREF titleColor;
	u16 flashMaxTime;
	u16 fontID;
	u8 radius;
	u8 scaledRadius;

	bool operator==(const MenuAppearance& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(MenuAppearance)) == 0; }
};

struct ZERO_INIT(MenuItemAppearance)
{
	HPEN borderPen;
	COLORREF baseColor;
	COLORREF labelColor;
	COLORREF borderColor;
	u16 bitmapIconID;
	u8 borderSize;
	u8 baseBorderSize;

	bool operator==(const MenuItemAppearance& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(MenuItemAppearance)) == 0; }
};

// Actual struct defined in WindowPainter.h
WindowAlphaInfo::WindowAlphaInfo() :
	fadeInRate(255), fadeOutRate(255),
	fadeInTime(1), fadeOutTime(1),
	fadeInDelay(0), fadeOutDelay(0), inactiveFadeOutDelay(0),
	maxAlpha(255), inactiveAlpha(255)
	{}

bool WindowAlphaInfo::operator==(const WindowAlphaInfo& rhs) const
{
	// Don't check Rates since those are derived floating-point cache values
	return
		this->fadeInTime == rhs.fadeInTime &&
		this->fadeOutTime == rhs.fadeOutTime &&
		this->fadeInDelay == rhs.fadeInDelay &&
		this->fadeOutDelay == rhs.fadeOutDelay &&
		this->inactiveFadeOutDelay == rhs.inactiveFadeOutDelay &&
		this->maxAlpha == rhs.maxAlpha &&
		this->inactiveAlpha == rhs.inactiveAlpha;
}

struct ZERO_INIT(ResizedFontInfo)
{
	HFONT baseFontHandle, handle;
	int altSize;
};

struct StringScaleCacheEntry
{
	HFONT fontHandle;
	u16 width, height;
};

struct CopyRectCacheEntry
{
	POINT fromPos;
	SIZE fromSize;
};

struct ZERO_INIT(LabelDrawCacheEntry)
{
	EMenuItemLabelType type;
	union
	{
		StringScaleCacheEntry str;
		CopyRectCacheEntry copyRect;
		u16 bitmapIconID;
	};
};

struct MenuCacheEntry
{
	std::vector<LabelDrawCacheEntry> labelCache;
	u16 positionID;
	u16 layoutID;
	u16 appearanceID;
	u16 itemAppearanceID[eMenuItemDrawState_Num];
	u16 alphaInfoID;

	MenuCacheEntry()
	{
		positionID = layoutID = appearanceID = alphaInfoID = kInvalidID;
		for(int i = 0; i < eMenuItemDrawState_Num; ++i)
			itemAppearanceID[i] = kInvalidID;
	}
};

struct OverlayPaintState
{
	std::vector<RECT> rects;
	int flashStartTime;
	u16 selection;
	u16 flashing;
	u16 prevFlashing;
	u16 forcedRedrawItemID;

	OverlayPaintState() :
		flashStartTime(),
		selection(),
		flashing(kInvalidID),
		prevFlashing(kInvalidID),
		forcedRedrawItemID(kInvalidID)
	{}
};

struct CopyRectRefreshEntry
{
	u16 overlayID;
	u16 itemIdx;

	bool operator==(const CopyRectRefreshEntry& rhs) const
	{ return overlayID == rhs.overlayID && itemIdx == rhs.itemIdx; }
};

struct ZERO_INIT(DrawData)
{
	HDC hdc;
	HDC hCaptureDC;
	POINT captureOffset;
	SIZE targetSize;
	int overlayID;
	int menuID;
	EMenuItemDrawState itemDrawState;
	bool firstDraw;
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static std::vector<HotspotSizesRange> sHotspotSizes;
static std::vector<FontInfo> sFonts;
static std::vector<PenInfo> sPens;
static StringToValueMap<BitmapFileInfo> sBitmapFiles;
static std::vector<BitmapIcon> sBitmapIcons;
static StringToValueMap<LabelIcon> sLabelIcons;
static std::vector<MenuPosition> sMenuPositions;
static std::vector<MenuLayout> sMenuLayouts;
static std::vector<MenuAppearance> sMenuAppearances;
static std::vector<MenuItemAppearance> sMenuItemAppearances;
static std::vector<WindowAlphaInfo> sMenuAlphaInfo;
static std::vector<OverlayPaintState> sOverlayPaintStates;
static std::vector<MenuCacheEntry> sMenuDrawCache;
static std::vector<ResizedFontInfo> sAutoSizedFonts;
static std::vector<CopyRectRefreshEntry> sAutoRefreshCopyRectQueue;
static std::wstring sErrorMessage;
static std::wstring sNoticeMessage;
static HDC sBitmapDrawSrc = NULL;
static HRGN sClipRegion = NULL;
static SystemPaintFunc sSystemOverlayPaintFunc = NULL;
static int sErrorMessageTimer = 0;
static int sNoticeMessageTimer = 0;
static int sSystemBorderFlashTimer = 0;
static int sCopyRectUpdateRate = 100;
static int sAutoRefreshCopyRectTime = 0;

//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static void setHotspotSizes(
   const std::string& theHotspotSizesKey,
   const std::string& theSizeDesc)
{
	int aRangeStartIdx, aRangeEndIdx;
	std::string aHotspotArrayName;
	fetchRangeSuffix(
		theHotspotSizesKey, aHotspotArrayName,
		aRangeStartIdx, aRangeEndIdx);
	const int aHotspotArrayID =
		InputMap::hotspotArrayIDFromName(aHotspotArrayName);
	if( size_t(aHotspotArrayID) > size_t(InputMap::hotspotArrayCount()) )
	{
		// TODO - error unrecognized hotspot name
		return;
	}
	if( aRangeStartIdx <= 0 )
	{// Use entire array (or single solo hotspot)
		aRangeStartIdx = InputMap::firstHotspotInArray(aHotspotArrayID);
		aRangeEndIdx = InputMap::sizeOfHotspotArray(aHotspotArrayID);
		if( aRangeEndIdx && InputMap::hotspotArrayHasAnchor(aHotspotArrayID) )
		{// Include anchor hotspot in the range
			--aRangeStartIdx;
			aRangeEndIdx += aRangeStartIdx;
		}
		else
		{
			aRangeEndIdx += aRangeStartIdx - 1;
		}
	}
	else
	{// Use portion of the array
		const int anArrayFirstHotspotID =
			InputMap::firstHotspotInArray(aHotspotArrayID);
		const int anArrayLastHotspotID =
			InputMap::lastHotspotInArray(aHotspotArrayID);
		const int aRangeDelta = aRangeEndIdx - aRangeStartIdx;
		aRangeStartIdx += anArrayFirstHotspotID - 1;
		aRangeEndIdx = aRangeStartIdx + aRangeDelta;
		if( aRangeStartIdx < anArrayFirstHotspotID ||
			aRangeStartIdx > anArrayLastHotspotID ||
			aRangeEndIdx < anArrayFirstHotspotID ||
			aRangeEndIdx > anArrayLastHotspotID )
		{
			// TODO - Invalid range for hotspot array
			return;
		}
	}
	
	// Extract width and height from theSizeDesc
	size_t aPos = 0;
	u16 aNewWidth = u16(clamp(
		intFromString(fetchNextItem(theSizeDesc, aPos)), 0, 0xFFFF));
	u16 aNewHeight = u16(clamp(
		intFromString(fetchNextItem(theSizeDesc, aPos)), 0, 0xFFFF));

	// Insert into sorted vector, such that not only are earlier ranges
	// before later ones, but sub-ranges are sorted before their containing
	// larger ranges (like Name2-7 before just 'Name').
	for(int aCheckPos = 0, aLastPos = intSize(sHotspotSizes.size());
		aCheckPos <= aLastPos; ++aCheckPos)
	{
		const int aNextRangeEndIdx =
			aCheckPos >= aLastPos ? 0x10000
			: sHotspotSizes[aCheckPos].lastHotspotID;

		// If our range starts past the next range entirely, just continue
		if( aRangeStartIdx > aNextRangeEndIdx )
			continue;
		
		const int aNextRangeStartIdx =
			aCheckPos >= aLastPos ? 0x10000
			: sHotspotSizes[aCheckPos].firstHotspotID;

		// Check for being exact same range
		if( aRangeStartIdx == aNextRangeStartIdx &&
			aRangeEndIdx == aNextRangeEndIdx )
		{// Just replace existing range with new values
			sHotspotSizes[aCheckPos].width = aNewWidth;
			sHotspotSizes[aCheckPos].height = aNewHeight;
			return;
		}

		// Check for range being entirely before next range, or contained
		// entirely within it
		if( aRangeEndIdx < aNextRangeStartIdx ||
			(aRangeStartIdx >= aNextRangeStartIdx &&
			 aRangeEndIdx <= aNextRangeEndIdx) )
		{
			HotspotSizesRange aNewRange;
			aNewRange.firstHotspotID = dropTo<u16>(aRangeStartIdx);
			aNewRange.lastHotspotID = dropTo<u16>(aRangeEndIdx);
			aNewRange.width = aNewWidth;
			aNewRange.height = aNewHeight;
			sHotspotSizes.insert(
				sHotspotSizes.begin() + aCheckPos,
				aNewRange);
			return;
		}

		// Check for range entirely encompassing next range
		if( aRangeStartIdx <= aNextRangeStartIdx &&
			aRangeEndIdx >= aNextRangeEndIdx )
		{
			continue;
		}
	}

	// Anything else is a weird overlap like 1-5 and 2-7...
	// TODO - report invalid range overlap
}


static SIZE getHotspotSize(int theHotspotID)
{
	SIZE result = {0, 0};
	for(int i = 0, end = intSize(sHotspotSizes.size()); i < end; ++i)
	{
		if( i >= sHotspotSizes[i].firstHotspotID &&
			i <= sHotspotSizes[i].lastHotspotID )
		{
			result.cx = sHotspotSizes[i].width;
			result.cy = sHotspotSizes[i].height;
			return result;
		}
	}

	return result;
}


static void setLabelIcon(
	const std::string& theTextLabel,
	const std::string& theIconDesc,
	bool ignoreRangeSuffix = false)
{
	if( !ignoreRangeSuffix )
	{// Possibly a range of labels specified as "Name1-12 = Something#"
		int aRangeStartIdx, aRangeEndIdx;
		std::string aLabelBaseName;
		if( fetchRangeSuffix(
				theTextLabel, aLabelBaseName,
				aRangeStartIdx, aRangeEndIdx) )
		{
			for(int i = aRangeStartIdx; i <= aRangeEndIdx; ++i)
			{
				// Need local var because sending toString(i).c_str() is unsafe
				const std::string& anIntStr = toString(i);
				const std::string& aNumberedDesc = replaceAllStr(
					theIconDesc, "#", anIntStr.c_str());
				setLabelIcon(aLabelBaseName + anIntStr, aNumberedDesc, true);
			}
			return;
		}
	}

	LabelIcon& anIconEntry = sLabelIcons.setValue(theTextLabel, LabelIcon());
	
	// Is the label description a bitmap, a section of bitmap, a hotspot, or ?
	// Hotspot: HotSpotName
	// Bitmap: BitmapName
	// Bitmap Section: BitMapName: 0, 0, 50, 50
	size_t aPos = 0;
	const std::string& anAssetName = fetchNextItem(theIconDesc, aPos, ":");
	int aFoundIndex = sBitmapFiles.findIndex(anAssetName);
	if( aFoundIndex < sBitmapFiles.size() )
	{
		// TODO: Find/create bm icon entry that matches rest of description
		return;
	}

	aFoundIndex = InputMap::hotspotIDFromName(anAssetName);
	if( !aFoundIndex )
	{
		// TODO - error, can't find bitmap or hotspot being referenced
		return;
	}

	anIconEntry.copyFromScreen = true;
	anIconEntry.hotspotID = dropTo<u16>(aFoundIndex);
}


static int getOrCreateFontID(const FontInfo& theFontInfo)
{
	int anID = 0;
	for(int end = intSize(sFonts.size()); anID < end; ++anID)
	{
		if( sFonts[anID] == theFontInfo )
			return anID;
	}
	sFonts.push_back(theFontInfo);
	return anID;
}


static int getOrCreateMenuPositionID(
	const MenuPosition& theMenuPosInfo)
{
	int anID = 0;
	for(int end = intSize(sMenuPositions.size()); anID < end; ++anID)
	{
		if( sMenuPositions[anID] == theMenuPosInfo )
			return anID;
	}
	sMenuPositions.push_back(theMenuPosInfo);
	return anID;
}


static int getOrCreateMenuLayoutID(
	const MenuLayout& theLayoutInfo)
{
	int anID = 0;
	for(int end = intSize(sMenuLayouts.size()); anID < end; ++anID)
	{
		if( sMenuLayouts[anID] == theLayoutInfo )
			return anID;
	}
	sMenuLayouts.push_back(theLayoutInfo);
	return anID;
}


static int getOrCreateBaseAppearanceID(
	const MenuAppearance& theMenuAppInfo)
{
	int anID = 0;
	for(int end = intSize(sMenuAppearances.size()); anID < end; ++anID)
	{
		if( sMenuAppearances[anID] == theMenuAppInfo )
			return anID;
	}
	sMenuAppearances.push_back(theMenuAppInfo);
	return anID;
}


static int getOrCreateItemAppearanceID(
	const MenuItemAppearance& theItemAppInfo)
{
	int anID = 0;
	for(int end = intSize(sMenuItemAppearances.size()); anID < end; ++anID)
	{
		if( sMenuItemAppearances[anID] == theItemAppInfo )
			return anID;
	}
	sMenuItemAppearances.push_back(theItemAppInfo);
	return anID;
}


static int getOrCreateAlphaInfoID(
	const WindowAlphaInfo& theAlphaInfo)
{
	int anID = 0;
	for(int end = intSize(sMenuAlphaInfo.size()); anID < end; ++anID)
	{
		if( sMenuAlphaInfo[anID] == theAlphaInfo )
			return anID;
	}
	sMenuAlphaInfo.push_back(theAlphaInfo);
	return anID;
}


static inline PropString getPropString(
	const Profile::PropertyMap& thePropMap,
	const std::string& thePropName)
{
	PropString result;
	result.str = Profile::getStr(thePropMap, thePropName);
	result.valid = !isEffectivelyEmptyString(result.str);
	return result;
}


static COLORREF stringToRGB(const std::string& theColorString)
{
	// TODO: Error checking?
	size_t aPos = 0;
	const u32 r = clamp(intFromString(
		fetchNextItem(theColorString, aPos)), 0, 255);
	const u32 g = clamp(intFromString(
		fetchNextItem(theColorString, aPos)), 0, 255);
	const u32 b = clamp(intFromString(
		fetchNextItem(theColorString, aPos)), 0, 255);
	return RGB(r, g, b);
}


static void fetchMenuPositionProperties(
	const Profile::PropertyMap& thePropMap,
	MenuPosition& theDestPosition)
{
	if( PropString p = getPropString(thePropMap, kPositionPropName) )
	{
		HotspotMap::stringToHotspot(p.str, theDestPosition.base);
		// TODO - error checking - or use old strToHotspot?
	}

	if( PropString p = getPropString(thePropMap, kDrawPriorityPropName) )
	{
		theDestPosition.drawPriority =
			s8(clamp(intFromString(p.str), -100, 100));
	}
}


static void fetchMenuLayoutProperties(
	const Profile::PropertyMap& thePropMap,
	MenuLayout& theDestLayout)
{
	if( PropString p = getPropString(thePropMap, kAlignmentPropName) )
	{
		Hotspot aTempHotspot;
		HotspotMap::stringToHotspot(p.str, aTempHotspot);
		// TODO - error checking - or use old strToHotspot?
		theDestLayout.alignmentX =
			aTempHotspot.x.anchor < 0x4000	? eAlignment_Min :
			aTempHotspot.x.anchor > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		theDestLayout.alignmentY =
			aTempHotspot.y.anchor < 0x4000	? eAlignment_Min :
			aTempHotspot.y.anchor > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
	}

	if( PropString p = getPropString(thePropMap, kSizePropName) )
	{
		theDestLayout.sizeX = u16(clamp(intFromString(
			breakOffNextItem(p.str)), 0, 0xFFFF));
		if( !p.str.empty() )
			theDestLayout.sizeY = u16(clamp(intFromString(p.str), 0, 0xFFFF));
		else
			theDestLayout.sizeY = theDestLayout.sizeX;
	}

	if( PropString p = getPropString(thePropMap, kGapSizePropName) )
	{
		theDestLayout.gapSizeX = s8(clamp(intFromString(
			breakOffNextItem(p.str)), -128, 127));
		if( !p.str.empty() )
			theDestLayout.sizeY = s8(clamp(intFromString(p.str), -128, 127));
		else
			theDestLayout.gapSizeY = theDestLayout.gapSizeX;
	}

	if( PropString p = getPropString(thePropMap, kTitleHeightPropName) )
		theDestLayout.titleHeight = u8(clamp(intFromString(p.str), 0, 0xFF));

	if( theDestLayout.style == eMenuStyle_Slots )
	{
		if( PropString p = getPropString(thePropMap, kAltLabelWidthPropName) )
		{
			theDestLayout.altLabelWidth =
				u16(clamp(intFromString(p.str), 0, 0xFFFF));
		}
	}
}


static void fetchBaseAppearanceProperties(
	const Profile::PropertyMap& thePropMap,
	MenuAppearance& theDestAppearance)
{
	//EMenuItemType itemType;
	//COLORREF transColor;
	//COLORREF titleColor;
	//u16 flashMaxTime;
	//u16 fontID;
	//u8 radius;
	//u8 scaledRadius;

	if( PropString p = getPropString(thePropMap, kItemTypePropName) )
	{
		theDestAppearance.itemType = menuItemTypeNameToID(p.str);
		// TODO: Error if itemType >= eMenuItemType_Num (unknown type)
	}

	if( PropString p = getPropString(thePropMap, kTransColorPropName) )
		theDestAppearance.transColor = stringToRGB(p.str);
	if( PropString p = getPropString(thePropMap, kTitleColorPropName) )
		theDestAppearance.titleColor = stringToRGB(p.str);
	if( PropString p = getPropString(thePropMap, kFlashTimePropName) )
	{
		theDestAppearance.flashMaxTime = u16(clamp(
			intFromString(p.str), 0, 0xFFFF));
	}
	if( theDestAppearance.itemType == eMenuItemType_RndRect )
	{
		if( PropString p = getPropString(thePropMap, kRadiusPropName) )
			theDestAppearance.radius = u8(clamp(intFromString(p.str), 0, 0xFF));
		theDestAppearance.scaledRadius = theDestAppearance.radius;
	}
	
	PropString aFontNameProp = getPropString(thePropMap, kFontNamePropName);
	PropString aFontSizeProp = getPropString(thePropMap, kFontSizePropName);
	PropString aFontWeightProp = getPropString(thePropMap, kFontWeightPropName);
	if( aFontNameProp || aFontSizeProp || aFontWeightProp )
	{
		FontInfo aNewFont = sFonts[theDestAppearance.fontID];
		if( aFontNameProp ) aNewFont.name = aFontNameProp.str;
		if( aFontSizeProp ) aNewFont.size = intFromString(aFontSizeProp.str);
		if( aFontWeightProp )
			aNewFont.weight = intFromString(aFontWeightProp.str);
		theDestAppearance.fontID = dropTo<u16>(getOrCreateFontID(aNewFont));
	}
}


static void fetchItemAppearanceProperties(
	const Profile::PropertyMap& thePropMap,
	MenuItemAppearance& theDestAppearance,
	EMenuItemDrawState theDrawState)
{
	// TODO - including checking alternate draw states if property not found
}


static void fetchAlphaFadeProperties(
	const Profile::PropertyMap& thePropMap,
	WindowAlphaInfo& theDestAlpha)
{
	const WindowAlphaInfo anOldAlphaInfo = theDestAlpha;
	if( const Profile::Property* p = thePropMap.find(kMaxAlphaPropName) )
		theDestAlpha.maxAlpha = u8(clamp(intFromString(p->str), 0, 0xFF));
	if( const Profile::Property* p = thePropMap.find(kInactiveAlphaPropName) )
		theDestAlpha.inactiveAlpha = u8(clamp(intFromString(p->str), 0, 0xFF));
	if( const Profile::Property* p = thePropMap.find(kFadeInTimePropName) )
		theDestAlpha.fadeInTime = u16(clamp(intFromString(p->str), 1, 0xFFFF));
	if( const Profile::Property* p = thePropMap.find(kFadeOutTimePropName) )
		theDestAlpha.fadeOutTime = u16(clamp(intFromString(p->str), 1, 0xFFFF));
	if( const Profile::Property* p = thePropMap.find(kFadeInDelayPropName) )
		theDestAlpha.fadeInDelay = u16(clamp(intFromString(p->str), 0, 0xFFFF));
	if( const Profile::Property* p = thePropMap.find(kFadeOutDelayPropName) )
		theDestAlpha.fadeOutDelay =
			u16(clamp(intFromString(p->str), 0, 0xFFFF));
	if( const Profile::Property* p = thePropMap.find(kInactiveDelayPropName) )
		theDestAlpha.inactiveFadeOutDelay =
			u16(clamp(intFromString(p->str), 0, 0xFFFF));

	if( !(anOldAlphaInfo == theDestAlpha) )
	{// Cache fade rate to avoid doing this calculation every frame
		theDestAlpha.fadeInRate =
			double(theDestAlpha.maxAlpha) / double(theDestAlpha.fadeInTime);
		theDestAlpha.fadeOutRate =
			double(theDestAlpha.maxAlpha) / double(theDestAlpha.fadeOutTime);
	}
}


static MenuPosition& getMenuPosition(int theMenuID)
{
	// Check if already have index of valid cached info
	MenuCacheEntry& theMenuCacheEntry = sMenuDrawCache[theMenuID];
	if( theMenuCacheEntry.positionID >= sMenuPositions.size() )
	{
		// Collect default data from root menu and/or default settings
		const int theRootMenuID = InputMap::rootMenuOfMenu(theMenuID);
		MenuPosition aMenuPosition =
			(theRootMenuID != theMenuID)
				? getMenuPosition(theRootMenuID)
				: sMenuPositions[0];
		// Add overrides from this menu's profile data
		aMenuPosition.parentKBCycleID =
			dropTo<u16>(InputMap::menuKeyBindCycleID(theMenuID));
		fetchMenuPositionProperties(
			Profile::allSections().vals()[InputMap::menuSectionID(theMenuID)],
			aMenuPosition);
		// Add the data to the cache
		theMenuCacheEntry.positionID = dropTo<u16>(
			getOrCreateMenuPositionID(aMenuPosition));
		DBG_ASSERT(theMenuCacheEntry.positionID < sMenuPositions.size());
	}

	return sMenuPositions[theMenuCacheEntry.positionID];
}


static MenuLayout& getMenuLayout(int theMenuID)
{
	MenuCacheEntry& theMenuCacheEntry = sMenuDrawCache[theMenuID];
	if( theMenuCacheEntry.layoutID >= sMenuLayouts.size() )
	{
		const int theRootMenuID = InputMap::rootMenuOfMenu(theMenuID);
		MenuLayout aMenuLayout =
			(theRootMenuID != theMenuID)
				? getMenuLayout(theRootMenuID)
				: sMenuLayouts[0];
		aMenuLayout.style = InputMap::menuStyle(theMenuID);
		fetchMenuLayoutProperties(
			Profile::allSections().vals()[InputMap::menuSectionID(theMenuID)],
			aMenuLayout);
		theMenuCacheEntry.layoutID = dropTo<u16>(
			getOrCreateMenuLayoutID(aMenuLayout));
		DBG_ASSERT(theMenuCacheEntry.layoutID < sMenuLayouts.size());
	}

	return sMenuLayouts[theMenuCacheEntry.layoutID];
}


static MenuAppearance& getMenuAppearance(int theMenuID)
{
	MenuCacheEntry& theMenuCacheEntry = sMenuDrawCache[theMenuID];
	if( theMenuCacheEntry.appearanceID >= sMenuAppearances.size() )
	{
		const int theRootMenuID = InputMap::rootMenuOfMenu(theMenuID);
		MenuAppearance aMenuApp =
			(theRootMenuID != theMenuID)
				? getMenuAppearance(theRootMenuID)
				: sMenuAppearances[0];
		fetchBaseAppearanceProperties(
			Profile::allSections().vals()[InputMap::menuSectionID(theMenuID)],
			aMenuApp);
		theMenuCacheEntry.appearanceID = dropTo<u16>(
			getOrCreateBaseAppearanceID(aMenuApp));
		DBG_ASSERT(theMenuCacheEntry.appearanceID < sMenuAppearances.size());
	}

	return sMenuAppearances[theMenuCacheEntry.appearanceID];
}


static MenuItemAppearance& getMenuItemAppearance(
	int theMenuID,
	EMenuItemDrawState theDrawState)
{
	MenuCacheEntry& theMenuCacheEntry = sMenuDrawCache[theMenuID];
	int anAppID = theMenuCacheEntry.itemAppearanceID[theDrawState];
	if( size_t(anAppID) >= sMenuItemAppearances.size() )
	{
		const int theRootMenuID = InputMap::rootMenuOfMenu(theMenuID);
		MenuItemAppearance anItemApp =
			(theRootMenuID != theMenuID)
				? getMenuItemAppearance(theRootMenuID, theDrawState)
				: sMenuItemAppearances[theDrawState];
		fetchItemAppearanceProperties(
			Profile::allSections().vals()[InputMap::menuSectionID(theMenuID)],
			anItemApp, theDrawState);
		anAppID = getOrCreateItemAppearanceID(anItemApp);
		theMenuCacheEntry.itemAppearanceID[theDrawState] =
			dropTo<u16>(anAppID);
		DBG_ASSERT(size_t(anAppID) < sMenuItemAppearances.size());
	}

	return sMenuItemAppearances[anAppID];
}


static WindowAlphaInfo& getMenuAlphaInfo(int theMenuID)
{
	MenuCacheEntry& theMenuCacheEntry = sMenuDrawCache[theMenuID];
	if( theMenuCacheEntry.alphaInfoID >= sMenuAlphaInfo.size() )
	{
		const int theRootMenuID = InputMap::rootMenuOfMenu(theMenuID);
		WindowAlphaInfo anAlphaInfo =
			(theRootMenuID != theMenuID)
				? getMenuAlphaInfo(theRootMenuID)
				: sMenuAlphaInfo[0];
		fetchAlphaFadeProperties(
			Profile::allSections().vals()[InputMap::menuSectionID(theMenuID)],
			anAlphaInfo);
		theMenuCacheEntry.alphaInfoID = dropTo<u16>(
			getOrCreateAlphaInfoID(anAlphaInfo));
		DBG_ASSERT(theMenuCacheEntry.alphaInfoID < sMenuAlphaInfo.size());
	}

	return sMenuAlphaInfo[theMenuCacheEntry.alphaInfoID];
}


static void createBaseFontHandle(FontInfo& theFontInfo)
{
	// TEMP
	theFontInfo.handle = NULL;
}


static HPEN getBorderPenHandle(int theBorderWidth, COLORREF theBorderColor)
{
	// TEMP
	return HPEN();
}


#if 0

static std::string getNamedHUDPropStr(
	const std::string& theName,
	EHUDProperty theProperty,
	u32 theAppearanceMode = eAppearanceMode_Normal,
	bool recursive = false)
{
	DBG_ASSERT(theAppearanceMode < eAppearanceMode_Num);
	std::string result;

	if( theName.empty() )
		return result;

	const std::string& aPropName =
		std::string(kAppearancePrefix[theAppearanceMode]) +
		kHUDPropStr[theProperty];
	const std::string& aSectSuffix = "." + theName;

	// Try [Menu.Name] first since most HUD elements are likely Menus
	result = Profile::getStr(kMenuSectionName + aSectSuffix, aPropName);
	if( !result.empty() )
		return result;

	// Try [HUD.Name] next
	result = Profile::getStr(kHUDSectionName + aSectSuffix, aPropName);
	if( !result.empty() )
		return result;

	switch(theAppearanceMode)
	{
	case eAppearanceMode_FlashSelected:
		// Try getting "flash" appearance version of this property
		result = getNamedHUDPropStr(
			theName, theProperty, eAppearanceMode_Flash, true);
		if( !result.empty() )
			return result;
		// If that didn't exist, try getting "selected" version
		result = getNamedHUDPropStr(
			theName, theProperty, eAppearanceMode_Selected, true);
		if( !result.empty() )
			return result;
		// Fall through
	case eAppearanceMode_Selected:
	case eAppearanceMode_Flash:
		// Try getting normal appearance version of this property
		if( !recursive )
		{
			result = getNamedHUDPropStr(
				theName, theProperty, eAppearanceMode_Normal, true);
		}
		break;
	}

	return result;
}


static std::string getDefaultHUDPropStr(
	EHUDProperty theProperty,
	u32 theAppearanceMode = eAppearanceMode_Normal,
	bool recursive = false)
{
	DBG_ASSERT(theAppearanceMode < eAppearanceMode_Num);
	std::string result;
	const std::string& aPropName =
		std::string(kAppearancePrefix[theAppearanceMode]) +
		kHUDPropStr[theProperty];

	// Try just [HUD] for a default value
	result = Profile::getStr(kHUDSectionName, aPropName);
	if( !result.empty() )
		return result;

	// Maybe just [Menu] for default value?
	result = Profile::getStr(kMenuSectionName, aPropName);
	if( !result.empty() )
		return result;

	switch(theAppearanceMode)
	{
	case eAppearanceMode_FlashSelected:
		// Trying getting "flash" version of appearance
		result = getDefaultHUDPropStr(
			theProperty, eAppearanceMode_Flash, true);
		if( !result.empty() )
			return result;
		// If that didn't exist, try getting "selected" version
		result = getDefaultHUDPropStr(
			theProperty, eAppearanceMode_Selected, true);
		if( !result.empty() )
			return result;
		// Fall through
	case eAppearanceMode_Selected:
	case eAppearanceMode_Flash:
		// Get property use for normal appearance instead
		if( !recursive )
		{
			result = getDefaultHUDPropStr(
				theProperty, eAppearanceMode_Normal, true);
		}
		break;
	}

	return result;
}


static std::string getHUDPropStr(
	const std::string& theName,
	EHUDProperty theProperty,
	u32 theAppearanceMode = eAppearanceMode_Normal)
{
	DBG_ASSERT(theAppearanceMode < eAppearanceMode_Num);
	std::string result;

	// Try specific named HUD Element first
	result = getNamedHUDPropStr(theName, theProperty, theAppearanceMode);
	if( !result.empty() )
		return result;

	// Try default setting under just [HUD] or [Menu]
	result = getDefaultHUDPropStr(theProperty, theAppearanceMode);
	return result;
}


static Hotspot strToHotspot(
	const std::string& theString,
	const std::string& theHUDElementName,
	const EHUDProperty theHUDProperty)
{
	Hotspot result;
	if( !theString.empty() )
	{
		std::string aStr = theString;
		if( HotspotMap::stringToHotspot(aStr, result) != eResult_Ok )
		{
			logError(
				"Error parsing HUD Element '%s' %s property: '%s' "
				"- incorrect format?",
				theHUDElementName.c_str(),
				kHUDPropStr[theHUDProperty],
				theString.c_str());
		}
	}
	return result;
}


static void loadBitmapFile(
	HUDBuilder& theBuilder,
	const std::string& theBitmapName,
	std::string theBitmapPath)
{
	if( theBitmapPath.empty() )
		return;

	// Convert given path into a wstring absolute path
	theBitmapPath = removeExtension(toAbsolutePath(theBitmapPath)) + ".bmp";
	const std::wstring aFilePathW = widen(theBitmapPath);

	if( !isValidFilePath(aFilePathW) )
	{
		logError("Could not find requested bitmap file %s!",
			theBitmapPath.c_str());
		return;
	}

	HBITMAP aBitmapHandle = (HBITMAP)LoadImage(
		NULL, aFilePathW.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
	if( !aBitmapHandle )
	{
		logError("Could not load bitmap %s. Wrong file format?",
			theBitmapPath.c_str());
		return;
	}

	theBuilder.bitmapNameToHandleMap.setValue(theBitmapName, aBitmapHandle);
}
#endif


static POINT hotspotToPoint(
	const Hotspot& theHotspot,
	const SIZE& theTargetSize)
{
	POINT result;
	result.x =
		LONG(u16ToRangeVal(theHotspot.x.anchor, theTargetSize.cx)) +
		LONG(theHotspot.x.offset * gUIScale);
	result.y =
		LONG(u16ToRangeVal(theHotspot.y.anchor, theTargetSize.cy)) +
		LONG(theHotspot.y.offset * gUIScale);
	return result;
}


#if 0
static SIZE hotspotToSize(
	const Hotspot& theHotspot,
	const SIZE& theTargetSize)
{
	POINT aPoint = hotspotToPoint(theHotspot, theTargetSize);
	SIZE result;
	result.cx = max(1L, aPoint.x);
	result.cy = max(1L, aPoint.y);
	return result;
}


static LONG hotspotUnscaledValue(
	const Hotspot::Coord& theCoord,
	const LONG theMaxValue)
{
	return
		LONG(u16ToRangeVal(theCoord.anchor, theMaxValue)) +
		LONG(theCoord.offset);
}


static LONG hotspotAnchorValue(
	const Hotspot::Coord& theCoord,
	const LONG theMaxValue)
{
	return LONG(u16ToRangeVal(theCoord.anchor, theMaxValue));
}


static u16 getOrCreatePenID(
	HUDBuilder& theBuilder,
	COLORREF theColor,
	int theWidth)
{
	int theStyle = PS_INSIDEFRAME;
	if( theWidth <= 0 )
	{
		theWidth = 0;
		theStyle = PS_NULL;
		theColor = RGB(0, 0, 0);
	}
	// Check for and return existing pen
	HUDBuilder::PenDef aPenDef =
		std::make_pair(theColor, std::make_pair(theStyle, theWidth));
	u16 result = theBuilder.penDefToPenMap.findOrAdd(
		aPenDef, dropTo<u16>(sPens.size()));
	if( result < sPens.size() )
		return result;

	// Create new pen
	HPEN aPen = CreatePen(theStyle, theWidth, theColor);
	sPens.push_back(aPen);
	return result;
}


static u16 getOrCreateFontID(
	HUDBuilder& theBuilder,
	const std::string& theFontName,
	const std::string& theFontSize,
	const std::string& theFontWeight)
{
	// Check for and return existing font
	const std::string& aKeyStr =
		theFontName + "_" + theFontSize + "_" + theFontWeight;
	u16 result = theBuilder.fontInfoToFontIDMap.findOrAdd(
		aKeyStr, dropTo<u16>(sFonts.size()));
	if( result < sFonts.size() )
		return result;

	// Create new font
	const int aFontPointSize = gUIScale *
		intFromString(theFontSize);
	HDC hdc = GetDC(NULL);
	const int aFontHeight =
		-MulDiv(aFontPointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(NULL, hdc);
	HFONT aFont = CreateFont(
		aFontHeight, // Height of the font
		0, // Width of the font
		0, // Angle of escapement
		0, // Orientation angle
		intFromString(theFontWeight), // Font weight
		FALSE, // Italic
		FALSE, // Underline
		FALSE, // Strikeout
		DEFAULT_CHARSET, // Character set identifier
		OUT_DEFAULT_PRECIS, // Output precision
		CLIP_DEFAULT_PRECIS, // Clipping precision
		DEFAULT_QUALITY, // Output quality
		DEFAULT_PITCH | FF_DONTCARE, // Pitch and family
		widen(theFontName).c_str() // Font face name
	);
	sFonts.push_back(aFont);
	return result;
}


static u16 getOrCreateAppearanceID(const Appearance& theAppearance)
{
	for(u16 i = 0; i < sAppearances.size(); ++i)
	{
		if( sAppearances[i] == theAppearance )
			return i;
	}

	sAppearances.push_back(theAppearance);
	return dropTo<u16>(sAppearances.size()-1);
}


static size_t getOrCreateBuildIconEntry(
	HUDBuilder& theBuilder,
	const std::string& theIconDescription,
	bool allowCopyFromTarget)
{
	if( theIconDescription.empty() )
		return 0;

	// The description could contain just the bitmap name, just a rectangle
	// (to copy from target), or "bitmap : rect" format for both.
	// First try getting the bitmap name
	std::string aRectDesc = theIconDescription;
	std::string aBitmapName = breakOffItemBeforeChar(aRectDesc, ':');
	HBITMAP* hBitmapPtr = aBitmapName.empty()
		? theBuilder.bitmapNameToHandleMap.find(theIconDescription)
		: theBuilder.bitmapNameToHandleMap.find(aBitmapName);
	BuildIconEntry aBuildEntry = BuildIconEntry();
	aBuildEntry.srcFile = hBitmapPtr ? *hBitmapPtr : 0;

	// If aBuildEntry.srcFile != null, a bitmap name was found
	// If it wasn't separated by : (aBitmapName is empty), then full string
	// was used, meaning there must not be any included rect description
	if( aBuildEntry.srcFile && aBitmapName.empty() )
		aRectDesc.clear();

	// If separated by : (aBitmapName isn't empty) yet no bitmap found,
	// the given name must be incorrect. Also, a proper bitmap handle is
	// required if !allowCopyFromTarget.
	if( !aBuildEntry.srcFile &&
		(!aBitmapName.empty() || !allowCopyFromTarget) )
	{
		if( aBitmapName.empty() )
			aBitmapName = theIconDescription;
		logError("Could not find a [%s] entry '%s='!",
			kBitmapsSectionName,
			aBitmapName.c_str());
		return 0;
	}

	// aRectDesc should now be empty, or contain two hotspots
	EResult aHotspotParseResult = eResult_None;
	if( !aRectDesc.empty() )
	{
		HotspotMap::stringToHotspot(aRectDesc, aBuildEntry.pos);
		aHotspotParseResult =
			HotspotMap::stringToHotspot(aRectDesc, aBuildEntry.size);
	}

	// Check for malformed entry
	if( aBuildEntry.srcFile &&
		aHotspotParseResult != eResult_Ok &&
		aHotspotParseResult != eResult_None )
	{
		// Bitmap + rect specified yet rect didn't parse properly
		// aRectDesc is modified during parse, so re-create it for error
		aRectDesc = theIconDescription;
		breakOffItemBeforeChar(aRectDesc, ':');
		logError("Could not decipher bitmap source rect '%s'",
			aRectDesc.c_str());
		return 0;
	}
	if( !aBuildEntry.srcFile && aHotspotParseResult != eResult_Ok )
	{
		// No bitmap file, yet couldn't parse as a rect description
		logError("Could not decipher bitmap/icon description '%s'",
			theIconDescription.c_str());
		return 0;
	}

	// Check if a duplicate of aBuildEntry was already created
	for(size_t i = 0; i < theBuilder.iconBuilders.size(); ++i)
	{
		if( theBuilder.iconBuilders[i].srcFile == aBuildEntry.srcFile &&
			theBuilder.iconBuilders[i].pos == aBuildEntry.pos &&
			theBuilder.iconBuilders[i].size == aBuildEntry.size )
			return i;
	}

	theBuilder.iconBuilders.push_back(aBuildEntry);
	return theBuilder.iconBuilders.size()-1;
}


static void setBitmapIcon(IconEntry& theEntry, BuildIconEntry& theBuildEntry)
{
	if( !theBuildEntry.srcFile || theEntry.isUpToDate )
		return;

	// Create a bitmap in memory from portion of bitmap file
	BITMAP bm;
	GetObject(theBuildEntry.srcFile, sizeof(bm), &bm);
	LONG x = 0, y = 0, w = bm.bmWidth, h = bm.bmHeight;
	if( !(theBuildEntry.size == Hotspot()) )
	{
		// Hotspot to pixel conversions are endpoint-exclusive so add +1
		x = clamp(hotspotUnscaledValue(theBuildEntry.pos.x, w+1), 0, w);
		y = clamp(hotspotUnscaledValue(theBuildEntry.pos.y, h+1), 0, h);
		w = clamp(hotspotUnscaledValue(theBuildEntry.size.x, w+1), 0, w-x);
		h = clamp(hotspotUnscaledValue(theBuildEntry.size.y, h+1), 0, h-y);
	}

	// Copy over to the new bitmap
	HDC hdcSrc = CreateCompatibleDC(NULL);
	HBITMAP hOldSrcBitmap = (HBITMAP)
		SelectObject(hdcSrc, theBuildEntry.srcFile);
	HDC hdcDest = CreateCompatibleDC(NULL);
	BitmapIcon aBitmapIcon = BitmapIcon();
	aBitmapIcon.image = CreateCompatibleBitmap(hdcSrc, w, h);
	aBitmapIcon.size.cx = w;
	aBitmapIcon.size.cy = h;
	HBITMAP hOldDestBitmap = (HBITMAP)
		SelectObject(hdcDest, aBitmapIcon.image);

	BitBlt(hdcDest, 0, 0, w, h, hdcSrc, x, y, SRCCOPY);

	SelectObject(hdcSrc, hOldSrcBitmap);
	DeleteDC(hdcSrc);
	SelectObject(hdcDest, hOldDestBitmap);
	DeleteDC(hdcDest);

	if( theEntry.iconID == 0 )
	{
		sBitmapIcons.push_back(aBitmapIcon);
		theEntry.iconID = dropTo<u16>(sBitmapIcons.size()-1);
	}
	else
	{
		DeleteObject(sBitmapIcons[theEntry.iconID].image);
		DeleteObject(sBitmapIcons[theEntry.iconID].mask);
		sBitmapIcons[theEntry.iconID] = aBitmapIcon;
	}
	theEntry.copyFromTarget = false;
	theEntry.isUpToDate = true;
	theBuildEntry.result = theEntry;
}


static void generateBitmapIconMask(BitmapIcon& theIcon, COLORREF theTransColor)
{
	BITMAP bm;
	GetObject(theIcon.image, sizeof(BITMAP), &bm);
	theIcon.mask = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);

	HDC hdcImage = CreateCompatibleDC(NULL);
	HDC hdcMask = CreateCompatibleDC(NULL);
	SelectObject(hdcImage, theIcon.image);
	SelectObject(hdcMask, theIcon.mask);

	// Generate 1-bit - theTransColor areas become 1, opaque areas become 0
	SetBkColor(hdcImage, theTransColor);
	BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcImage, 0, 0, SRCCOPY);
	// Change theTransColor pixels in the original image to black
	BitBlt(hdcImage, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, SRCINVERT);

	DeleteDC(hdcImage);
	DeleteDC(hdcMask);
}


static void setCopyRect(IconEntry& theEntry, BuildIconEntry& theBuildEntry)
{
	if( theBuildEntry.srcFile || theEntry.isUpToDate )
		return;

	CopyRect aCopyRect = CopyRect();
	aCopyRect.pos = theBuildEntry.pos;
	aCopyRect.size = theBuildEntry.size;

	if( theEntry.iconID == 0 )
	{
		sCopyRects.push_back(aCopyRect);
		theEntry.iconID = dropTo<u16>(sCopyRects.size()-1);
	}
	else
	{
		sCopyRects[theEntry.iconID] = aCopyRect;
	}
	theEntry.copyFromTarget = true;
	theEntry.isUpToDate = true;
	theBuildEntry.result = theEntry;
}


static void setOffsetCopyRect(
	IconEntry& theEntry,
	const IconEntry& theBaseCopyRect,
	const Hotspot& theOffsetHotspot)
{
	if( theEntry.isUpToDate && theEntry.copyFromTarget )
		return;

	CopyRect aCopyRect = CopyRect();
	DBG_ASSERT(theBaseCopyRect.iconID < sCopyRects.size());
	aCopyRect.pos = sCopyRects[theBaseCopyRect.iconID].pos;
	aCopyRect.size = sCopyRects[theBaseCopyRect.iconID].size;
	aCopyRect.pos.x.offset += theOffsetHotspot.x.offset;
	aCopyRect.pos.y.offset += theOffsetHotspot.y.offset;

	if( theEntry.iconID == 0 )
	{
		sCopyRects.push_back(aCopyRect);
		theEntry.iconID = dropTo<u16>(sCopyRects.size()-1);
	}
	else
	{
		sCopyRects[theEntry.iconID] = aCopyRect;
	}
	theEntry.copyFromTarget = true;
	theEntry.isUpToDate = true;
}


static u16 getOrCreateBitmapIconID(
	HUDBuilder& theBuilder,
	const std::string& theIconDescription)
{
	size_t anIconBuildID = getOrCreateBuildIconEntry(
		theBuilder, theIconDescription, false);
	if( anIconBuildID == 0 )
		return 0;
	BuildIconEntry& aBuildEntry = theBuilder.iconBuilders[anIconBuildID];
	if( !aBuildEntry.srcFile )
		return 0;
	if( aBuildEntry.result.isUpToDate )
		return aBuildEntry.result.iconID;
	setBitmapIcon(aBuildEntry.result, aBuildEntry);
	return aBuildEntry.result.iconID;
}


static IconEntry getOrCreateLabelIcon(
	HUDBuilder& theBuilder,
	const std::string& theTextLabel,
	const std::string& theIconDescription,
	bool usePrevIdxAsBase = false)
{
	std::string aTextLabel = theTextLabel;

	// Check if might just be an offset from another copy icon
	int anArrayIdx = breakOffIntegerSuffix(aTextLabel);
	int aStartArrayIdx = anArrayIdx;
	if( anArrayIdx > 0 )
	{
		if( aTextLabel[aTextLabel.size()-1] == '-' )
		{
			aTextLabel.resize(aTextLabel.size()-1);
			aStartArrayIdx =
				breakOffIntegerSuffix(aTextLabel);
			usePrevIdxAsBase = true;
		}
		else if( IconEntry* anOldEntry =
					sLabelIcons.find(aTextLabel + toString(anArrayIdx)) )
		{
			if( anOldEntry->isUpToDate )
				return *anOldEntry;
		}
		Hotspot anOffset;
		std::string anIconDesc(theIconDescription);
		HotspotMap::stringToHotspot(anIconDesc, anOffset);
		if( anIconDesc.empty() &&
			anOffset.x.anchor == 0 && anOffset.y.anchor == 0 )
		{// Find icon to use as a base to offset from
			IconEntry aBaseIcon;
			if( usePrevIdxAsBase )
			{// Use aStartArrayIdx - 1 as base
				std::string aBaseLabel =
					aTextLabel + toString(aStartArrayIdx-1);
				aBaseIcon = getOrCreateLabelIcon(
					theBuilder, aBaseLabel,
					Profile::getStr(kIconsSectionName, aBaseLabel));
			}
			else
			{// Use un-numbered entry as base
				aBaseIcon = getOrCreateLabelIcon(
					theBuilder, aTextLabel,
					Profile::getStr(kIconsSectionName, aTextLabel));
			}
			if( aBaseIcon.isUpToDate && aBaseIcon.copyFromTarget )
			{
				for(int i = aStartArrayIdx; i <= anArrayIdx; ++i)
				{
					IconEntry& anEntry = sLabelIcons.findOrAdd(
						aTextLabel + toString(i), IconEntry());
					setOffsetCopyRect(anEntry, aBaseIcon, anOffset);
					aBaseIcon = anEntry;
				}
				return aBaseIcon;
			}
		}
		// Restore full label
		aTextLabel = theTextLabel;
	}

	IconEntry& anEntry = sLabelIcons.findOrAdd(aTextLabel, IconEntry());
	if( anEntry.isUpToDate )
		return anEntry;
	size_t anIconBuildID = getOrCreateBuildIconEntry(
		theBuilder, theIconDescription, true);
	if( anIconBuildID == 0 )
		return anEntry;
	BuildIconEntry& aBuildEntry = theBuilder.iconBuilders[anIconBuildID];
	if( aBuildEntry.result.isUpToDate )
	{
		anEntry = aBuildEntry.result;
		return anEntry;
	}
	if( aBuildEntry.srcFile )
	{
		setBitmapIcon(anEntry, aBuildEntry);
		return anEntry;
	}
	setCopyRect(anEntry, aBuildEntry);
	return anEntry;
}


static void eraseRect(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	COLORREF oldColor = SetDCBrushColor(dd.hdc, hi.transColor);
	HBRUSH hBrush = (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH);
	FillRect(dd.hdc, &theRect, hBrush);
	SetDCBrushColor(dd.hdc, oldColor);
}


static void drawHUDRect(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

	Rectangle(dd.hdc,
		theRect.left, theRect.top,
		theRect.right + (appearance.borderSize ? 0 : 1),
		theRect.bottom + (appearance.borderSize ? 0 : 1));
}


static void drawHUDRndRect(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

	int aRadius = hi.scaledRadius;
	aRadius = min<int>(aRadius, (theRect.right-theRect.left) * 3 / 4);
	aRadius = min<int>(aRadius, (theRect.bottom-theRect.top) * 3 / 4);

	RoundRect(dd.hdc,
		theRect.left, theRect.top,
		theRect.right + (appearance.borderSize ? 0 : 1),
		theRect.bottom + (appearance.borderSize ? 0 : 1),
		aRadius, aRadius);
}


static void drawHUDBitmap(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	const u16 anIconID = appearance.bitmapIconID;

	if( anIconID == 0 )
	{
		drawHUDRect(dd, theRect);
		return;
	}

	DBG_ASSERT(anIconID < sBitmapIcons.size());
	const BitmapIcon& anIcon = sBitmapIcons[anIconID];
	DBG_ASSERT(sBitmapDrawSrc);
	HBITMAP hOldBitmap = (HBITMAP)
		SelectObject(sBitmapDrawSrc, anIcon.image);

	StretchBlt(dd.hdc,
			   theRect.left, theRect.top,
			   theRect.right - theRect.left,
			   theRect.bottom - theRect.top,
			   sBitmapDrawSrc,
			   0, 0,
			   anIcon.size.cx,
			   anIcon.size.cy,
			   SRCCOPY);

	SelectObject(sBitmapDrawSrc, hOldBitmap);
}


static void drawHUDCircle(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

	Ellipse(dd.hdc,
		theRect.left, theRect.top,
		theRect.right, theRect.bottom);
}


static void drawHUDArrowL(HUDDrawData& dd, RECT theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);
	if( appearance.borderSize > 0 )
		InflateRect(&theRect,
			-appearance.borderSize / 2 - 1, -appearance.borderSize / 2 - 1);

	POINT points[3];
	points[0].x = theRect.right;
	points[0].y = theRect.top;
	points[1].x = theRect.right;
	points[1].y = theRect.bottom;
	points[2].x = theRect.left;
	points[2].y = (theRect.top + theRect.bottom) / 2;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDArrowR(HUDDrawData& dd, RECT theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);
	if( appearance.borderSize > 0 )
		InflateRect(&theRect,
			-appearance.borderSize / 2 - 1, -appearance.borderSize / 2 - 1);

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.top;
	points[1].x = theRect.left;
	points[1].y = theRect.bottom;
	points[2].x = theRect.right;
	points[2].y = (theRect.top + theRect.bottom) / 2;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDArrowU(HUDDrawData& dd, RECT theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);
	if( appearance.borderSize > 0 )
		InflateRect(&theRect,
			-appearance.borderSize / 2 - 1, -appearance.borderSize / 2 - 1);

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.bottom;
	points[1].x = theRect.right;
	points[1].y = theRect.bottom;
	points[2].x = (theRect.left + theRect.right) / 2;
	points[2].y = theRect.top;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDArrowD(HUDDrawData& dd, RECT theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);
	if( appearance.borderSize > 0 )
		InflateRect(&theRect,
			-appearance.borderSize / 2 - 1, -appearance.borderSize / 2 - 1);

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.top;
	points[1].x = theRect.right;
	points[1].y = theRect.top;
	points[2].x = (theRect.left + theRect.right) / 2;
	points[2].y = theRect.bottom;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDItem(HUDDrawData& dd, const RECT& theRect)
{
	switch(sHUDElementInfo[dd.hudElementID].itemType)
	{
	case eHUDItemType_Default: TODO - Rect unless overall menu is _Label, then _Label
	case eHUDItemType_Rect:		drawHUDRect(dd, theRect);		break;
	case eHUDItemType_RndRect:	drawHUDRndRect(dd, theRect);	break;
	case eHUDItemType_Bitmap:	drawHUDBitmap(dd, theRect);		break;
	case eHUDItemType_Circle:	drawHUDCircle(dd, theRect);		break;
	case eHUDItemType_ArrowL:	drawHUDArrowL(dd, theRect);		break;
	case eHUDItemType_ArrowR:	drawHUDArrowR(dd, theRect);		break;
	case eHUDItemType_ArrowU:	drawHUDArrowU(dd, theRect);		break;
	case eHUDItemType_ArrowD:	drawHUDArrowD(dd, theRect);		break;
	case eHUDItemType_Label:	drawHUDLabelOnly(dd, theRect);	break;
	default: DBG_ASSERT(false && "Invalid HUD ItemType!");
	}
}


static LONG getFontHeightToFit(
	HDC hdc,
	const std::wstring& theStr,
	const RECT& theClipRect,
	RECT* theTextRect,
	UINT theFormat)
{
	LONG result = 0; // 0 indicates base font is small enough

	// First check if base font will fit fine on its own
	RECT aCalcRect = theClipRect;
	// DT_SINGLELINE prevents DT_CALCRECT from calculating height
	theFormat &= ~DT_SINGLELINE;
	aCalcRect.bottom = aCalcRect.top;
	DrawText(hdc, theStr.c_str(), -1, &aCalcRect, theFormat | DT_CALCRECT);
	if(	aCalcRect.left >= theClipRect.left &&
		aCalcRect.right <= theClipRect.right &&
		aCalcRect.top >= theClipRect.top &&
		aCalcRect.bottom <= theClipRect.bottom )
	{// No need for further testing
		if( theTextRect )
			*theTextRect = aCalcRect;
		return result;
	}

	// Base font is too big, need a smaller font size!
	LOGFONT aBaseFont;
	GetObject(GetCurrentObject(hdc, OBJ_FONT), sizeof(LOGFONT), &aBaseFont);
	// Use this method instead of checking aBaseFont.lfHeight because
	// TEXTMETRIC.tmHeight is always a positive value in pixels but
	// aBaseFont.lfHeight can be a negative value of "logical units"
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);

	// Use binary search to find largest font that will still fit
	int low = kMinFontPixelHeight;
	int high = tm.tmHeight;
	while(low <= high)
	{
		int mid = low + (high - low) / 2;

		// Create font and measure text using test font size (mid)
		LOGFONT aFont = aBaseFont;
		aFont.lfWidth = 0;
		aFont.lfHeight = mid;
		HFONT hOldFont = (HFONT)SelectObject(hdc, CreateFontIndirect(&aFont));
		aCalcRect = theClipRect;
		aCalcRect.bottom = aCalcRect.top;
		DrawText(hdc, theStr.c_str(), -1, &aCalcRect, theFormat | DT_CALCRECT);
		DeleteObject(SelectObject(hdc, hOldFont));

		// Update result (aka best fit size so far) and search range
		if( mid == kMinFontPixelHeight ||
			(aCalcRect.left >= theClipRect.left &&
			 aCalcRect.right <= theClipRect.right &&
			 aCalcRect.top >= theClipRect.top &&
			 aCalcRect.bottom <= theClipRect.bottom) )
		{
			result = mid;
			low = mid + 1;
			if( theTextRect )
				*theTextRect = aCalcRect;
		}
		else
		{
			high = mid - 1;
		}
	}

	return result;
}


static void initStringCacheEntry(
	HUDDrawData& dd,
	const RECT& theRect,
	const std::wstring& theStr,
	UINT theFormat,
	StringScaleCacheEntry& theCacheEntry)
{
	if( theCacheEntry.width > 0 )
		return;

	// This will not only check if an alternate font size is needed, but
	// also calculate drawn width and height for given string, which
	// can be used to calculate offsets for things like vertical justify
	// when not using DT_SINGLELINE (meaning DT_VCENTER/_BOTTOM don't work).
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	theCacheEntry.fontID = hi.fontID;

	SelectObject(dd.hdc, sFonts[hi.fontID]);
	RECT aNeededRect = theRect;
	if( LONG aFontHeight = getFontHeightToFit(
			dd.hdc, theStr, theRect, &aNeededRect, theFormat) )
	{
		theCacheEntry.fontID = sResizedFontsMap.findOrAdd(
			std::make_pair(hi.fontID, aFontHeight),
			dropTo<u16>(sFonts.size()));
		if( theCacheEntry.fontID == sFonts.size() )
		{
			LOGFONT aFont;
			GetObject(sFonts[hi.fontID], sizeof(LOGFONT), &aFont);
			aFont.lfWidth = 0;
			aFont.lfHeight = aFontHeight;
			sFonts.push_back(CreateFontIndirect(&aFont));
		}
	}

	theCacheEntry.width = max(1L, aNeededRect.right - aNeededRect.left);
	theCacheEntry.height = max(1L, aNeededRect.bottom - aNeededRect.top);
}


static void drawLabelString(
	HUDDrawData& dd,
	RECT theRect,
	const std::wstring& theStr,
	UINT theFormat,
	StringScaleCacheEntry& theCacheEntry)
{
	if( theStr.empty() )
		return;

	theFormat |= DT_NOPREFIX;

	// Initialize cache entry to get font & height
	initStringCacheEntry(dd, theRect, theStr, theFormat, theCacheEntry);

	// Adjust vertial draw position manually when not using DT_SINGLELINE,
	// which is the only time DT_VCENTER and DT_BOTTOM work normally
	if( !(theFormat & DT_SINGLELINE) )
	{
		if( theFormat & DT_VCENTER )
		{
			theRect.top +=
				((theRect.bottom - theRect.top) -
				 theCacheEntry.height) / 2;
		}
		else if( theFormat & DT_BOTTOM )
		{
			theRect.top +=
				(theRect.bottom - theRect.top) - theCacheEntry.height;
		}
	}

	// Draw label based on cache entry's fields
	if( theCacheEntry.width > 0 )
	{
		SelectObject(dd.hdc, sFonts[theCacheEntry.fontID]);
		DrawText(dd.hdc, theStr.c_str(), -1, &theRect, theFormat);
	}
}


static void drawMenuItemLabel(
	HUDDrawData& dd,
	RECT theRect,
	u16 theItemIdx,
	std::string theLabel,
	const Appearance& theAppearance,
	u8 theCurrBorderSize, u8 theMaxBorderSize,
	MenuDrawCacheEntry& theCacheEntry)
{
	if( theCacheEntry.type == eMenuItemLabelType_Unknown )
	{// Initialize cache entry
		IconEntry* anIconEntry = sLabelIcons.find(theLabel);
		if( anIconEntry && anIconEntry->copyFromTarget )
		{
			theCacheEntry.type = eMenuItemLabelType_CopyRect;
			theCacheEntry.copyRect.fromPos = hotspotToPoint(
				sCopyRects[anIconEntry->iconID].pos,
				dd.targetSize);
			theCacheEntry.copyRect.fromSize = hotspotToSize(
				sCopyRects[anIconEntry->iconID].size,
				dd.targetSize);
		}
		else if( anIconEntry && !anIconEntry->copyFromTarget )
		{
			theCacheEntry.type = eMenuItemLabelType_Bitmap;
			theCacheEntry.bitmapIconID = anIconEntry->iconID;
			if( !sBitmapIcons[anIconEntry->iconID].mask )
			{
				generateBitmapIconMask(
					sBitmapIcons[anIconEntry->iconID],
					sHUDElementInfo[dd.hudElementID].transColor);
			}
		}
		else
		{
			theCacheEntry.type = eMenuItemLabelType_String;
			theCacheEntry.str = StringScaleCacheEntry();
		}
	}

	if( theCacheEntry.type == eMenuItemLabelType_String )
	{
		SetTextColor(dd.hdc, theAppearance.labelColor);
		SetBkColor(dd.hdc, theAppearance.itemColor);
		if( theMaxBorderSize > 0 )
			InflateRect(&theRect, -theMaxBorderSize, -theMaxBorderSize);
		drawLabelString(dd, theRect, widen(theLabel),
			DT_WORDBREAK | DT_CENTER | DT_VCENTER,
			theCacheEntry.str);
	}
	else if( theCacheEntry.type == eMenuItemLabelType_Bitmap )
	{
		DBG_ASSERT(theCacheEntry.bitmapIconID < sBitmapIcons.size());
		const BitmapIcon& anIcon = sBitmapIcons[theCacheEntry.bitmapIconID];
		DBG_ASSERT(sBitmapDrawSrc);
		if( theCurrBorderSize > 0 )
		{
			RECT aClipRect = theRect;
			InflateRect(&aClipRect, -theCurrBorderSize, -theCurrBorderSize);
			DBG_ASSERT(sClipRegion);
			SetRectRgn(sClipRegion,
				aClipRect.left, aClipRect.top,
				aClipRect.right, aClipRect.bottom);
			SelectClipRgn(dd.hdc, sClipRegion);
		}

		// Draw 1-bit mask with SRCAND, which will overwrite any pixels in
		// destination that are (0) in the mask to be all-black (0) while
		// leaving the other pixels (white (1) in the mask) in dest as-is
		HBITMAP hOldBitmap = (HBITMAP)
			SelectObject(sBitmapDrawSrc, anIcon.mask);
		StretchBlt(dd.hdc,
				   theRect.left, theRect.top,
				   theRect.right - theRect.left,
				   theRect.bottom - theRect.top,
				   sBitmapDrawSrc,
				   0, 0,
				   anIcon.size.cx,
				   anIcon.size.cy,
				   SRCAND);

		// Draw image using SRCPAINT (OR) to mask out transparent pixels
		// Black (0) source (was TransColor) | any dest = keep dest color
		// Color source | black dest (masked via above) = icon color
		SelectObject(sBitmapDrawSrc, anIcon.image);
		StretchBlt(dd.hdc,
				   theRect.left, theRect.top,
				   theRect.right - theRect.left,
				   theRect.bottom - theRect.top,
				   sBitmapDrawSrc,
				   0, 0,
				   anIcon.size.cx,
				   anIcon.size.cy,
				   SRCPAINT);

		SelectObject(sBitmapDrawSrc, hOldBitmap);
		if( theCurrBorderSize > 0 )
			SelectClipRgn(dd.hdc, NULL);
	}
	else if( theCacheEntry.type == eMenuItemLabelType_CopyRect )
	{
		int aDstL = theRect.left;
		int aDstT = theRect.top;
		int aDstW = theRect.right - theRect.left;
		int aDstH = theRect.bottom - theRect.top;
		int aSrcL = theCacheEntry.copyRect.fromPos.x + dd.captureOffset.x;
		int aSrcT = theCacheEntry.copyRect.fromPos.y + dd.captureOffset.y;
		int aSrcW = theCacheEntry.copyRect.fromSize.cx;
		int aSrcH = theCacheEntry.copyRect.fromSize.cy;
		if( theCurrBorderSize > 0 )
		{
			RECT aClipRect = theRect;
			InflateRect(&aClipRect, -theCurrBorderSize, -theCurrBorderSize);
			DBG_ASSERT(sClipRegion);
			SetRectRgn(sClipRegion,
				aClipRect.left, aClipRect.top,
				aClipRect.right, aClipRect.bottom);
			SelectClipRgn(dd.hdc, sClipRegion);
		}

		if( aDstW >= aSrcW && aDstH >= aSrcH )
		{// Just draw centered at destination
			BitBlt(dd.hdc,
				aDstL + (aDstW - aSrcW) / 2,
				aDstT + (aDstH - aSrcH) / 2,
				aSrcW, aSrcH,
				dd.hCaptureDC, aSrcL, aSrcT, SRCCOPY);
		}
		else
		{// Draw stretched, but maintain aspect ratio
			const double aSrcAspectRatio = double(aSrcW) / aSrcH;
			const double aDstAspectRatio = double(aDstW) / aDstH;

			if( aSrcAspectRatio > aDstAspectRatio )
			{
				// Fit height and center horizontally
				const int aPrevDestH = aDstH;
				aDstH = min(aDstH, int(aDstW / aSrcAspectRatio));
				aDstT += (aPrevDestH - aDstH) / 2;
			}
			else if( aSrcAspectRatio < aDstAspectRatio )
			{
				// Fit width and center vertically
				const int aPrevDestW = aDstW;
				aDstW = min(aDstW, int(aDstH * aSrcAspectRatio));
				aDstL += (aPrevDestW - aDstW) / 2;
			}

			StretchBlt(dd.hdc, aDstL, aDstT, aDstW, aDstH,
				dd.hCaptureDC, aSrcL, aSrcT, aSrcW, aSrcH, SRCCOPY);
		}
		// Queue auto-refresh this icon later if nothing else draws it
		CopyRectRefreshEntry anAutoRefreshEntry = { dd.overlayID, theItemIdx };
		// If it was already queued, bump it to the end
		sAutoRefreshCopyRectQueue.erase(std::remove(
			sAutoRefreshCopyRectQueue.begin(),
			sAutoRefreshCopyRectQueue.end(), anAutoRefreshEntry),
			sAutoRefreshCopyRectQueue.end());
		sAutoRefreshCopyRectQueue.push_back(anAutoRefreshEntry);
		if( theCurrBorderSize > 0 )
			SelectClipRgn(dd.hdc, NULL);
	}
}


static void drawMenuTitle(
	HUDDrawData& dd,
	u16 theSubMenuID,
	int theTitleBottomY,
	MenuDrawCacheEntry& theCacheEntry)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[eAppearanceMode_Normal]];
	DBG_ASSERT(dd.components.size() > 1);
	RECT aTitleRect = dd.components[0];
	if( hi.radius > 0 && hi.itemType == eMenuItemType_RndRect )
	{
		aTitleRect.left += hi.scaledRadius * 0.75;
		aTitleRect.right -= hi.scaledRadius * 0.75;
	}
	aTitleRect.bottom = theTitleBottomY;

	EAlignment alignment = EAlignment(hi.alignmentX);

	if( !dd.firstDraw )
		eraseRect(dd, aTitleRect);
	std::string aStr = InputMap::menuLabel(theSubMenuID);
	const std::wstring& aWStr = widen(aStr);
	if( theCacheEntry.type == eMenuItemLabelType_Unknown )
	{
		theCacheEntry.type = eMenuItemLabelType_String;
		theCacheEntry.str = StringScaleCacheEntry();
	}

	UINT aFormat = DT_WORDBREAK | DT_BOTTOM;
	switch(alignment)
	{
	case eAlignment_Min: aFormat |= DT_LEFT; break;
	case eAlignment_Center: aFormat |= DT_CENTER; break;
	case eAlignment_Max: aFormat |= DT_RIGHT; break;
	}
	const int aBorderSize = gUIScale > 1.0 ? 2 * gUIScale : 2;
	InflateRect(&aTitleRect, -aBorderSize, -aBorderSize);
	initStringCacheEntry(dd, aTitleRect, aWStr, aFormat, theCacheEntry.str);

	// Fill in 2px margin around text with titleBG (border) color
	RECT aBGRect = aTitleRect;
	InflateRect(&aBGRect, aBorderSize, aBorderSize);
	if( alignment == eAlignment_Center )
	{
		aBGRect.left =
			((dd.components[0].left + dd.components[0].right) / 2) -
			(theCacheEntry.str.width / 2) - aBorderSize;
	}
	else if( alignment == eAlignment_Max )
	{
		aBGRect.left =
			aTitleRect.right - theCacheEntry.str.width - aBorderSize;
	}
	aBGRect.right = aBGRect.left + theCacheEntry.str.width + aBorderSize * 2;
	aBGRect.top = aBGRect.bottom - theCacheEntry.str.height - aBorderSize * 2;
	COLORREF oldColor = SetDCBrushColor(dd.hdc, appearance.borderColor);
	FillRect(dd.hdc, &aBGRect, (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH));
	SetDCBrushColor(dd.hdc, oldColor);

	SetTextColor(dd.hdc, hi.titleColor);
	SetBkColor(dd.hdc, appearance.borderColor);
	drawLabelString(dd, aTitleRect, aWStr, aFormat, theCacheEntry.str);
}


static void drawMenuItem(
	HUDDrawData& dd,
	const RECT& theRect,
	u16 theItemIdx,
	const std::string& theLabel,
	MenuDrawCacheEntry& theCacheEntry)
{
	// Select appropriate appearance
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const bool selected = theItemIdx == hi.selection;
	const bool flashing = theItemIdx == hi.flashing;
	if( selected && flashing )
		dd.appearanceMode = eAppearanceMode_FlashSelected;
	else if( flashing )
		dd.appearanceMode = eAppearanceMode_Flash;
	else if( selected )
		dd.appearanceMode = eAppearanceMode_Selected;
	else
		dd.appearanceMode = eAppearanceMode_Normal;
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];

	// Background (usually a bordered rectangle)
	drawHUDItem(dd, theRect);

	// Label (usually word-wrapped and centered text)
	RECT aLabelRect = theRect;
	int aBorderSize = hi.scaledRadius / 4;
	int aMaxBorderSize = aBorderSize;
	aBorderSize = max(aBorderSize, int(appearance.borderSize));
	for(int i = 0; i < eAppearanceMode_Num; ++i)
	{
		aMaxBorderSize = max<int>(aMaxBorderSize,
			sAppearances[hi.appearanceID[i]].borderSize);
	}
	drawMenuItemLabel(
		dd, aLabelRect,
		theItemIdx,
		theLabel,
		appearance,
		aBorderSize, aMaxBorderSize,
		theCacheEntry);

	// Flag when successfully redrew forced-redraw item
	if( hi.forcedRedrawItemID == theItemIdx )
		hi.forcedRedrawItemID = kInvalidID;
}


static void drawBasicMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aPrevSelection = hi.selection;
	hi.selection = gDisabledHUD.test(dd.hudElementID)
		? kInvalidID : Menus::selectedItem(aMenuID);
	const u16 anItemCount = dropTo<u16>(dd.components.size() - 1);
	DBG_ASSERT(anItemCount == Menus::itemCount(aMenuID));
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	sMenuDrawCache.resize(max<size_t>(sMenuDrawCache.size(), aSubMenuID+1));
	sMenuDrawCache[aSubMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd, aSubMenuID, dd.components[1].top,
			sMenuDrawCache[aSubMenuID][0]);
	}

	const bool flashingChanged = hi.flashing != hi.prevFlashing;
	const bool selectionChanged = hi.selection != aPrevSelection;
	const bool shouldRedrawAll =
		dd.firstDraw ||
		((hi.gapSizeX < 0 || hi.gapSizeY < 0) &&
			(flashingChanged || selectionChanged));

	bool redrawSelectedItem = false;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( shouldRedrawAll ||
			hi.forcedRedrawItemID == itemIdx ||
			(selectionChanged &&
				(itemIdx == aPrevSelection || itemIdx == hi.selection)) ||
			(flashingChanged &&
				(itemIdx == hi.prevFlashing || itemIdx == hi.flashing)) )
		{
			// Non-rect shapes may not fully overwrite previous drawing
			if( !dd.firstDraw && hi.itemType != eHUDItemType_Rect )
				eraseRect(dd, dd.components[itemIdx+1]);
			if( itemIdx == hi.selection &&
				(hi.gapSizeX < 0 || hi.gapSizeY < 0) )
			{// Make sure selection is drawn on top of other items
				redrawSelectedItem = true;
			}
			else
			{
				drawMenuItem(dd, dd.components[itemIdx+1], itemIdx,
					InputMap::menuItemLabel(aSubMenuID, itemIdx),
					sMenuDrawCache[aSubMenuID][itemIdx + hasTitle]);
			}
		}
	}

	// Draw selected menu item last
	if( redrawSelectedItem )
	{
		drawMenuItem(dd, dd.components[hi.selection+1], hi.selection,
			InputMap::menuItemLabel(aSubMenuID, hi.selection),
			sMenuDrawCache[aSubMenuID][hi.selection + hasTitle]);
	}

	hi.prevFlashing = hi.flashing;
}


static void drawSlotsMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	u16 aPrevSelection = hi.selection;
	hi.selection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = dropTo<u16>(dd.components.size() -
		(hi.altLabelWidth ? 2 : 1));
	DBG_ASSERT(anItemCount == Menus::itemCount(aMenuID));
	DBG_ASSERT(hi.selection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	sMenuDrawCache.resize(max<size_t>(sMenuDrawCache.size(), aSubMenuID+1));
	sMenuDrawCache[aSubMenuID].resize(
		anItemCount + hasTitle + (hi.altLabelWidth ? anItemCount : 0));

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd, aSubMenuID, dd.components[1].top,
			sMenuDrawCache[aSubMenuID][0]);
	}

	const bool flashingChanged = hi.flashing != hi.prevFlashing;
	const bool selectionChanged = hi.selection != aPrevSelection;
	const bool shouldRedrawAll = dd.firstDraw || selectionChanged;

	// Draw alternate label for selected item off to the side
	if( hi.altLabelWidth &&
		(selectionChanged || shouldRedrawAll ||
		 hi.forcedRedrawItemID == hi.selection) )
	{
		const std::string& anAltLabel =
			InputMap::menuItemAltLabel(aSubMenuID, hi.selection);
		const std::string& aPrevAltLabel =
			InputMap::menuItemAltLabel(aSubMenuID, aPrevSelection);
		if( anAltLabel.empty() && !aPrevAltLabel.empty() )
			eraseRect(dd, dd.components.back());
		if( !anAltLabel.empty() )
		{
			dd.appearanceMode = eAppearanceMode_Normal;
			RECT anAltLabelRect = dd.components.back();
			drawHUDRect(dd, anAltLabelRect);
			const Appearance& appearance = sAppearances[
				hi.appearanceID[dd.appearanceMode]];
			drawMenuItemLabel(
				dd, anAltLabelRect,
				hi.selection,
				anAltLabel,
				appearance,
				appearance.borderSize, appearance.borderSize,
				sMenuDrawCache[aSubMenuID]
					[anItemCount+hi.selection+hasTitle]);
		}
	}

	// Make sure only flash top slot even if selection changes during flash
	if( hi.flashing != kInvalidID )
		hi.flashing = hi.selection;

	// Draw in a wrapping fashion, starting with hi.selection+1 being drawn
	// just below the top slot, and ending when draw hi.selection last at top
	for(u16 compIdx = (1 % anItemCount),
		itemIdx = (hi.selection + 1) % anItemCount; /*until break*/;
		itemIdx = (itemIdx + 1) % anItemCount,
		compIdx = (compIdx + 1) % anItemCount)
	{
		const bool isSelection = itemIdx == hi.selection;
		if( isSelection )
		{
			aPrevSelection = hi.selection;
			if( gDisabledHUD.test(dd.hudElementID) )
				hi.selection = kInvalidID;
		}
		if( shouldRedrawAll ||
			hi.forcedRedrawItemID == itemIdx ||
			(isSelection && flashingChanged) )
		{
			if( !dd.firstDraw && hi.itemType != eHUDItemType_Rect )
				eraseRect(dd, dd.components[compIdx+1]);
			drawMenuItem(dd, dd.components[compIdx+1], itemIdx,
				InputMap::menuItemLabel(aSubMenuID, itemIdx),
				sMenuDrawCache[aSubMenuID][itemIdx + hasTitle]);
		}
		if( isSelection )
		{
			hi.selection = aPrevSelection;
			break;
		}
	}

	hi.prevFlashing = hi.flashing;
}


static void draw4DirMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	hi.selection = kInvalidID;
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	sMenuDrawCache.resize(max<size_t>(sMenuDrawCache.size(), aSubMenuID+1));
	sMenuDrawCache[aSubMenuID].resize(eCmdDir_Num + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd,
			aSubMenuID, dd.components[1 + eCmdDir_Up].top,
			sMenuDrawCache[aSubMenuID][0]);
	}

	for(u16 itemIdx = 0; itemIdx < eCmdDir_Num; ++itemIdx)
	{
		const ECommandDir aDir = ECommandDir(itemIdx);
		if( dd.firstDraw ||
			hi.forcedRedrawItemID == itemIdx ||
			(hi.flashing != hi.prevFlashing &&
				(itemIdx == hi.prevFlashing || itemIdx == hi.flashing)) )
		{
			if( !dd.firstDraw && hi.itemType != eHUDItemType_Rect )
				eraseRect(dd, dd.components[itemIdx+1]);
			drawMenuItem(dd, dd.components[itemIdx+1], itemIdx,
				InputMap::menuDirLabel(aSubMenuID, aDir),
				sMenuDrawCache[aSubMenuID][aDir + hasTitle]);
		}
	}

	hi.prevFlashing = hi.flashing;
}


static void drawBasicHUD(HUDDrawData& dd)
{
	if( !dd.firstDraw )
		eraseRect(dd, dd.components[0]);

	drawHUDItem(dd, dd.components[0]);
}


static void drawHSGuide(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	DBG_ASSERT(hi.type == eHUDType_HotspotGuide);
	const BitVector<32>& arraysToShow = HotspotMap::getEnabledHotspotArrays();

	if( !dd.firstDraw )
		eraseRect(dd, dd.components[0]);

	const Appearance& appearance = sAppearances[
		hi.appearanceID[eAppearanceMode_Normal]];
	//SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);
	HBRUSH hBrush = (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH);

	for(int anArrayIdx = arraysToShow.firstSetBit();
		anArrayIdx < arraysToShow.size();
		anArrayIdx = arraysToShow.nextSetBit(anArrayIdx+1))
	{
		const u16 aHotspotCount = InputMap::sizeOfHotspotArray(anArrayIdx);
		const u16 aFirstHotspot = InputMap::firstHotspotInArray(anArrayIdx);
		for(u16 aHotspotID = aFirstHotspot;
			aHotspotID < aFirstHotspot + aHotspotCount;
			++aHotspotID)
		{
			const POINT& aHotspotPos = hotspotToPoint(
				InputMap::getHotspot(aHotspotID), dd.targetSize);
			const RECT aDrawRect = {
				aHotspotPos.x - hi.radius,
				aHotspotPos.y - hi.radius,
				aHotspotPos.x + hi.radius,
				aHotspotPos.y + hi.radius };
			FillRect(dd.hdc, &aDrawRect, hBrush);
		}
	}
}


static void drawSystemHUD(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	DBG_ASSERT(hi.type == eHUDType_System);

	if( ((sSystemBorderFlashTimer / kSystemHUDFlashFreq) & 0x01) != 0 &&
		sErrorMessage.empty() && sNoticeMessage.empty() )
	{
		COLORREF aFrameColor = RGB(0, 180, 0);
		HPEN hFramePen = CreatePen(PS_INSIDEFRAME, 4, aFrameColor);

		HPEN hOldPen = (HPEN)SelectObject(dd.hdc, hFramePen);
		Rectangle(dd.hdc, dd.components[0].left, dd.components[0].top,
			dd.components[0].right, dd.components[0].bottom);
		SelectObject(dd.hdc, hOldPen);

		DeleteObject(hFramePen);

		// This effectively also erased the dest region
		dd.firstDraw = true;
	}

	if( sSystemOverlayPaintFunc )
	{
		COLORREF oldColor = SetDCBrushColor(dd.hdc, hi.transColor);
		sSystemOverlayPaintFunc(dd.hdc, dd.components[0], dd.firstDraw);
		SetDCBrushColor(dd.hdc, oldColor);
	}

	RECT aTextRect = dd.components[1];
	InflateRect(&aTextRect, -8, -8);

	if( !sErrorMessage.empty() )
	{
		SetBkColor(dd.hdc, RGB(255, 0, 0));
		SetTextColor(dd.hdc, RGB(255, 255, 0));
		DrawText(dd.hdc, sErrorMessage.c_str(), -1, &aTextRect,
			DT_WORDBREAK | DT_NOPREFIX);
	}

	if( !sNoticeMessage.empty() )
	{
		SetBkColor(dd.hdc, RGB(0, 0, 255));
		SetTextColor(dd.hdc, RGB(255, 255, 255));
		DrawText(dd.hdc, sNoticeMessage.c_str(), -1, &aTextRect,
			DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
	}
}
#endif


static void updateHotspotsMenuLayout(
	int theOverlayID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect,
	POINT& theWindowPos,
	SIZE& theWindowSize)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	DBG_ASSERT(size_t(theOverlayID) < sOverlayPaintStates.size());
	OverlayPaintState& ps = sOverlayPaintStates[theOverlayID];
	const MenuLayout& theLayout = getMenuLayout(theMenuID);

	DBG_ASSERT(
		theLayout.style == eMenuStyle_Hotspots ||
		theLayout.style == eMenuStyle_SelectHotspot);
	const int anItemCount = InputMap::menuItemCount(theMenuID);
	ps.rects.reserve(anItemCount + 1);
	ps.rects.resize(1);
	RECT aWinRect = { 0 };
	aWinRect.left = theTargetSize.cx;
	aWinRect.top = theTargetSize.cy;
	const int anItemSizeX = int(max(0.0, ceil(theLayout.sizeX * gUIScale)));
	const int anItemSizeY = int(max(0.0, ceil(theLayout.sizeY * gUIScale)));
	const int anItemHalfSizeX = anItemSizeX / 2;
	const int anItemHalfSizeY = anItemSizeY / 2;
	for(int i = 0; i < anItemCount; ++i)
	{
		const int aHotspotID = InputMap::menuItemHotspotID(theMenuID, i);
		const POINT& anItemPos = hotspotToPoint(
			InputMap::getHotspot(aHotspotID), theTargetSize);

		RECT anItemRect;
		anItemRect.left = anItemPos.x;
		if( theLayout.alignmentX == eAlignment_Center )
			anItemRect.left -= anItemHalfSizeX;
		else if( theLayout.alignmentX == eAlignment_Max )
			anItemRect.left -= anItemSizeX - 1;
		anItemRect.right = anItemRect.left + anItemSizeX;
		anItemRect.top = anItemPos.y;
		if( theLayout.alignmentY == eAlignment_Center )
			anItemRect.top -= anItemHalfSizeY;
		else if( theLayout.alignmentY == eAlignment_Max )
			anItemRect.top -= anItemSizeY - 1;
		anItemRect.bottom = anItemRect.top + anItemSizeY;
		ps.rects.push_back(anItemRect);
		aWinRect.left = min(aWinRect.left, anItemRect.left);
		aWinRect.top = min(aWinRect.top, anItemRect.top);
		aWinRect.right = max(aWinRect.right, anItemRect.right);
		aWinRect.bottom = max(aWinRect.bottom, anItemRect.bottom);
	}
	if( theLayout.titleHeight > 0 )
		aWinRect.top -= LONG(ceil(theLayout.titleHeight * gUIScale));
	ps.rects[0] = aWinRect;

	// Clip actual window to target area
	aWinRect.left = max(aWinRect.left, theTargetClipRect.left);
	aWinRect.top = max(aWinRect.top, theTargetClipRect.top);
	aWinRect.right = min(aWinRect.right, theTargetClipRect.right);
	aWinRect.bottom = min(aWinRect.bottom, theTargetClipRect.bottom);
	theWindowPos.x = aWinRect.left;
	theWindowPos.y = aWinRect.top;
	theWindowSize.cx = aWinRect.right - aWinRect.left;
	theWindowSize.cy = aWinRect.bottom - aWinRect.top;

	// Make all rects be window-relative instead of target-relative
	for(std::vector<RECT>::iterator itr = ps.rects.begin();
		itr != ps.rects.end(); ++itr)
	{
		itr->left -= theWindowPos.x;
		itr->top -= theWindowPos.y;
		itr->right -= theWindowPos.x;
		itr->bottom -= theWindowPos.y;
	}
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void init()
{
	cleanup();

	sCopyRectUpdateRate =
		Profile::getInt("System", "CopyRectFrameTime", sCopyRectUpdateRate);

	sMenuDrawCache.resize(InputMap::menuCount());
	sOverlayPaintStates.reserve(InputMap::menuOverlayCount());
	sOverlayPaintStates.resize(InputMap::menuOverlayCount());

	// Add base/sentinel 0th entry to some vectors
	sFonts.resize(1);
	sBitmapIcons.resize(1);
	sMenuPositions.resize(1);
	sMenuLayouts.resize(1);
	sMenuAppearances.resize(1);
	sMenuAlphaInfo.resize(1);
	sMenuItemAppearances.resize(eMenuItemDrawState_Num);

	{// Get hotspot sizes
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kHotspotSizesSectionName);
		sHotspotSizes.reserve(aPropMap.size());
		for(int i = 0; i < aPropMap.size(); ++i)
		{
			setHotspotSizes(
				aPropMap.keys()[i],
				aPropMap.vals()[i].str);
		}
	}

	{// Prepare for loaded bitmap file handles
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kBitmapsSectionName);
		sBitmapFiles.reserve(aPropMap.size());
		for(int i = 0, end = aPropMap.size(); i < end; ++i)
			sBitmapFiles.setValue(aPropMap.keys()[i], BitmapFileInfo());
	}

	{// Set up converting text labels to icons
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kIconsSectionName);
		for(int i = 0; i < aPropMap.size(); ++i)
		{
			setLabelIcon(
				aPropMap.keys()[i],
				aPropMap.vals()[i].str);
		}
	}

	// Add data from base [Appearance] section for default look
	const Profile::PropertyMap& aDefaultProps =
		Profile::getSectionProperties(kMenuDefaultSectionName);
	fetchMenuLayoutProperties(aDefaultProps, sMenuLayouts[0]);
	fetchBaseAppearanceProperties(aDefaultProps, sMenuAppearances[0]);
	for(int i = 0; i < eMenuItemDrawState_Num; ++i)
	{
		fetchItemAppearanceProperties(aDefaultProps,
			sMenuItemAppearances[i], EMenuItemDrawState(i));
	}
	sMenuAlphaInfo[0] = WindowAlphaInfo();
	fetchAlphaFadeProperties(aDefaultProps, sMenuAlphaInfo[0]);

	{// Add data for the built-in _System menu
		DBG_ASSERT(InputMap::menuStyle(kSystemMenuID) == eMenuStyle_System);
		DBG_ASSERT(InputMap::menuOverlayID(kSystemMenuID) == kSystemOverlayID);
		MenuCacheEntry& theSystemMenu = sMenuDrawCache[kSystemMenuID];
		theSystemMenu.alphaInfoID = 0;
		theSystemMenu.appearanceID = 0;
		for(u16 i = 0; i < eMenuItemDrawState_Num; ++i)
			theSystemMenu.itemAppearanceID[i] = i;
		theSystemMenu.layoutID = 0;
		MenuPosition aMenuPos;
		// Set draw priority to higher than any can be manually set to
		aMenuPos.drawPriority = 127;
		theSystemMenu.positionID =
			dropTo<u16>(getOrCreateMenuPositionID(aMenuPos));
	}

	{// Add data for the  built-in _HotspotGuide menu
		DBG_ASSERT(InputMap::menuStyle(kHotspotGuideMenuID) ==
			eMenuStyle_HotspotGuide);
		DBG_ASSERT(InputMap::menuOverlayID(kHotspotGuideMenuID) ==
			kHotspotGuideOverlayID);
		MenuCacheEntry& theHSGuideMenu = sMenuDrawCache[kHotspotGuideMenuID];
		WindowAlphaInfo anAlphaInfo = sMenuAlphaInfo[0];
		anAlphaInfo.maxAlpha = u8(clamp(Profile::getInt(
			aDefaultProps, "HotspotGuideAlpha", 0), 0, 255));
		anAlphaInfo.inactiveFadeOutDelay = u16(clamp(Profile::getInt(
			aDefaultProps, "HotspotGuideDisplayTime", 1000), 0, 0xFFFF));
		anAlphaInfo.inactiveAlpha = 0;
		theHSGuideMenu.alphaInfoID =
			dropTo<u16>(getOrCreateAlphaInfoID(anAlphaInfo));
		MenuAppearance anAppearance = sMenuAppearances[0];
		anAppearance.itemType = eMenuItemType_Circle;
		anAppearance.radius = dropTo<u8>(clamp((Profile::getInt(
			aDefaultProps, "HotspotGuideSize", 6) / 2), 1, 255));
		theHSGuideMenu.appearanceID =
			dropTo<u16>(getOrCreateBaseAppearanceID(anAppearance));
		MenuItemAppearance anItemAppearance =
			sMenuItemAppearances[eMenuItemDrawState_Normal];
		anItemAppearance.baseColor = RGB(128, 128, 128);
		if( PropString p = getPropString(aDefaultProps, "HotspotGuideRGB") )
			anItemAppearance.baseColor = stringToRGB(p.str);
		theHSGuideMenu.itemAppearanceID[eMenuItemDrawState_Normal] =
			dropTo<u16>(getOrCreateItemAppearanceID(anItemAppearance));
		for(u16 i = 0; i < eMenuItemDrawState_Num; ++i)
			theHSGuideMenu.itemAppearanceID[i] = i;
		theHSGuideMenu.layoutID = 0;
		MenuPosition aMenuPos;
		// Lower than any can be manually set to
		aMenuPos.drawPriority = -127;
		theHSGuideMenu.positionID =
			dropTo<u16>(getOrCreateMenuPositionID(aMenuPos));
	}
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	// TODO - invalidate (mark dirty) cache entries that might have changed
	// Keep in mind that all menus can be affected by changes to anything in
	// kMenuDefaultSectionName, sub-menus can be affected by changes to their
	// root menu, and draw states like "flashing" within a particular menu
	// can be affected by "parent" draw states like "normal".
	// May also need some system to decide on complete reinitialization if there
	// are enough "orphaned" info structs from too many changes (though old
	// structs are re-used if the same settings are desired again, so it would
	// need to be a lot of completely unique changes for this to be a problem).
	// Make sure to inform WindowManager if any layout or position properties
	// changed so it can re-do updateWindowLayout(), as well as account for any
	// other changes that might affect WindowManager like sudden change in alpha
	// setting, transparency color, etc (perhaps in its own version of this func?)

	sCopyRectUpdateRate =
		Profile::getInt("System", "CopyRectFrameTime", sCopyRectUpdateRate);

	if( const Profile::PropertyMap* aPropMap =
			theProfileMap.find(kHotspotSizesSectionName) )
	{
		// TODO: mark dirty / update anything that uses these sizes as well!
		for(int i = 0; i < aPropMap->size(); ++i)
		{
			setHotspotSizes(
				aPropMap->keys()[i],
				aPropMap->vals()[i].str);
		}
	}

	// TODO: Also update hotspot size scaled value for any hotspots that might
	// have had their scale value updated within InputMap

	//if( const Profile::PropertyMap* aPropMap =
	//	theProfileMap.find(kIconsSectionName) )
	{
		//for(int i = 0; i < aPropMap.size(); ++i)
		//{
		//	setLabelIcon(
		//		aPropMap.keys()[i],
		//		aPropMap.vals()[i].str);
		//}
	}
}


void cleanup()
{
	for(int i = 0, end = intSize(sAutoSizedFonts.size()); i < end; ++i)
		DeleteObject(sAutoSizedFonts[i].handle);
	for(int i = 0, end = intSize(sFonts.size()); i < end; ++i)
		DeleteObject(sFonts[i].handle);
	for(int i = 0, end = intSize(sPens.size()); i < end; ++i)
		DeleteObject(sPens[i].handle);
	for(int i = 0, end = sBitmapFiles.size(); i < end; ++i)
		DeleteObject(sBitmapFiles.vals()[i].image);
	for(int i = 0, end = intSize(sBitmapIcons.size()); i < end; ++i)
	{
		DeleteObject(sBitmapIcons[i].image);
		DeleteObject(sBitmapIcons[i].mask);
	}

	sHotspotSizes.clear();
	sFonts.clear();
	sPens.clear();
	sBitmapFiles.clear();
	sBitmapIcons.clear();
	sLabelIcons.clear();
	sMenuPositions.clear();
	sMenuLayouts.clear();
	sMenuAppearances.clear();
	sMenuItemAppearances.clear();
	sMenuAlphaInfo.clear();
	sOverlayPaintStates.clear();
	sMenuDrawCache.clear();
	sAutoSizedFonts.clear();
	sAutoRefreshCopyRectQueue.clear();
	sErrorMessage.clear();
	sSystemOverlayPaintFunc = NULL;
	sErrorMessageTimer = 0;

	DeleteDC(sBitmapDrawSrc);
	sBitmapDrawSrc = NULL;
	DeleteObject(sClipRegion);
	sClipRegion = NULL;
}


void update()
{
	// Handle display of error messages and other notices via _System menu
	if( sErrorMessageTimer > 0 )
	{
		sErrorMessageTimer -= gAppFrameTime;
		if( sErrorMessageTimer <= 0 )
		{
			sErrorMessageTimer = 0;
			sErrorMessage.clear();
			gFullRedrawOverlays.set(kSystemOverlayID);
		}
	}
	else if( !gErrorString.empty() && !hadFatalError() )
	{
		sErrorMessage =
			widen("MMOGO ERROR: ") +
			gErrorString +
			widen("\nCheck MMOGO_ErrorLog.txt for other possible errors!");
		sErrorMessageTimer = max<int>(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sErrorMessage.size()));
		gErrorString.clear();
		gFullRedrawOverlays.set(kSystemOverlayID);
	}

	if( sNoticeMessageTimer > 0 )
	{
		sNoticeMessageTimer -= gAppFrameTime;
		if( sNoticeMessageTimer <= 0 )
		{
			sNoticeMessageTimer = 0;
			sNoticeMessage.clear();
			gFullRedrawOverlays.set(kSystemOverlayID);
		}
	}
	if( !gNoticeString.empty() )
	{
		sNoticeMessage = gNoticeString;
		sNoticeMessageTimer = max<int>(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sNoticeMessage.size()));
		gNoticeString.clear();
		gFullRedrawOverlays.set(kSystemOverlayID);
	}

	bool showSystemBorder = false;
	if( sSystemBorderFlashTimer > 0 &&
		sErrorMessage.empty() && sNoticeMessage.empty() )
	{
		showSystemBorder =
			((sSystemBorderFlashTimer / kSystemOverlayFlashFreq) & 0x01) != 0;
		sSystemBorderFlashTimer =
			max(0, sSystemBorderFlashTimer - gAppFrameTime);
		if( gVisibleOverlays.test(kSystemOverlayID) != showSystemBorder )
			gRefreshOverlays.set(kSystemOverlayID);
	}

	#ifdef DEBUG_DRAW_TOTAL_OVERLAY_FRAME
		showSystemBorder = true;
	#endif

	gVisibleOverlays.set(kSystemOverlayID,
		!sNoticeMessage.empty() ||
		!sErrorMessage.empty() ||
		sSystemOverlayPaintFunc ||
		showSystemBorder);

	if( gVisibleOverlays.test(kSystemOverlayID) )
		gActiveOverlays.set(kSystemOverlayID);

	// Check for updates to other HUD elements
	for(int i = 0, end = InputMap::menuOverlayCount(); i < end; ++i)
	{
		const int aMenuID = Menus::activeMenuForOverlayID(i);
		switch(InputMap::menuStyle(aMenuID))
		{
		case eMenuStyle_KBCycleLast:
			if( gKeyBindCycleLastIndexChanged.test(
					InputMap::menuKeyBindCycleID(aMenuID)) )
			{ gReshapeOverlays.set(i); }
			break;
		case eMenuStyle_KBCycleDefault:
			if( gKeyBindCycleLastIndexChanged.test(
					InputMap::menuKeyBindCycleID(aMenuID)) )
			{ gReshapeOverlays.set(i); }
			break;
		}

		// Update flash effect on confirmed menu item
		OverlayPaintState& aPaintState = sOverlayPaintStates[i];
		const MenuAppearance& aMenuApp = getMenuAppearance(aMenuID);
		if( gConfirmedMenuItem[i] != kInvalidID )
		{
			if( aMenuApp.flashMaxTime )
			{
				aPaintState.flashing = gConfirmedMenuItem[i];
				aPaintState.flashStartTime = gAppRunTime;
				gRefreshOverlays.set(i);
			}
			gConfirmedMenuItem[i] = kInvalidID;
		}
		else if( aPaintState.flashing != kInvalidID &&
				 (gAppRunTime - aPaintState.flashStartTime)
					> aMenuApp.flashMaxTime )
		{
			aPaintState.flashing = kInvalidID;
			gRefreshOverlays.set(i);
		}
	}

	switch(gHotspotsGuideMode)
	{
	case eHotspotGuideMode_Disabled:
		gVisibleOverlays.reset(kHotspotGuideOverlayID);
		break;
	case eHotspotGuideMode_Redraw:
		gRefreshOverlays.set(kHotspotGuideOverlayID);
		// fall through
	case eHotspotGuideMode_Redisplay:
		gActiveOverlays.set(kHotspotGuideOverlayID);
		gHotspotsGuideMode = eHotspotGuideMode_Showing;
		// fall through
	case eHotspotGuideMode_Showing:
		gVisibleOverlays.set(kHotspotGuideOverlayID);
		break;
	}

	// Update auto-refresh of idle copy-from-target icons
	// This system is set up to only force-redraw one icon per update,
	// but still have each icon individually only redraw at
	// sCopyRectUpdateRate, to spread the copy-paste operations out over
	// multiple frames and reduce chance of causing a framerate hitch.
	if( gAppRunTime >= sAutoRefreshCopyRectTime )
	{
		sAutoRefreshCopyRectTime = gAppRunTime;
		int validItemCount = 0;
		for(std::vector<CopyRectRefreshEntry>::iterator itr =
			sAutoRefreshCopyRectQueue.begin(), next_itr = itr;
			itr != sAutoRefreshCopyRectQueue.end(); itr = next_itr)
		{
			++next_itr;
			DBG_ASSERT(size_t(itr->overlayID) < sOverlayPaintStates.size());
			OverlayPaintState& aPaintState =
				sOverlayPaintStates[itr->overlayID];
			// Make sure this item is still valid
			const int aMaxItemID = InputMap::menuItemCount(
				Menus::activeMenuForOverlayID(itr->overlayID));
			if( itr->itemIdx > aMaxItemID )
			{// Invalid item ID, can't refresh, just remove it from queue
				next_itr = sAutoRefreshCopyRectQueue.erase(itr);
				continue;
			}
			// If .forcedRedrawItemID is already set as a valid entry, it's
			// still waiting for it to refresh (might be currently hidden), so
			// don't count any of this menu's items as valid until then
			if( aPaintState.forcedRedrawItemID <= aMaxItemID )
			{
				gRefreshOverlays.set(itr->overlayID);
				continue;
			}
			// Only first valid item found actually refreshes, and only once
			// have waited at least one time since found valid items
			if( validItemCount++ == 0 && sAutoRefreshCopyRectTime > 0 )
			{
				gRefreshOverlays.set(itr->overlayID);
				aPaintState.forcedRedrawItemID = itr->itemIdx;
				// Still need to continue to count valid items for refresh
				// timing, but this one no longer need be in the queue
				next_itr = sAutoRefreshCopyRectQueue.erase(itr);
			}
		}
		// Evenly space total refresh rate time by number of found items,
		// or if none found then set to 0 to keep checking every update but
		// also prevent instant-refresh of the first item that requests it
		if( validItemCount )
			sAutoRefreshCopyRectTime += sCopyRectUpdateRate / validItemCount;
		else
			sAutoRefreshCopyRectTime = 0;
	}

	gKeyBindCycleLastIndexChanged.reset();
	gKeyBindCycleDefaultIndexChanged.reset();
}


void updateScaling()
{
	if( sMenuDrawCache.empty() )
		return;

	// Clear fonts and border pens
	for(int i = 0, end = intSize(sAutoSizedFonts.size()); i < end; ++i)
		DeleteObject(sAutoSizedFonts[i].handle);
	for(int i = 0, end = intSize(sPens.size()); i < end; ++i)
		DeleteObject(sPens[i].handle);
	sAutoSizedFonts.clear();
	sPens.clear();

	// Clear label cache data for each menu as labels are affect by scale
	for(int i = 0, end = intSize(sMenuDrawCache.size()); i < end; ++i)
		sMenuDrawCache[i].labelCache.clear();

	// TODO: Update hotspot sizes

	// Update scaled Radius for each Menu Appearance
	for(int i = 0, end = intSize(sMenuAppearances.size()); i < end; ++i)
	{
		MenuAppearance& aMenuApp = sMenuAppearances[i];
		aMenuApp.scaledRadius = dropTo<u8>(
			aMenuApp.radius * gUIScale);
	}

	// Recreate base font handle for each base menu font
	for(int i = 0, end = intSize(sFonts.size()); i < end; ++i)
	{
		DeleteObject(sFonts[i].handle);
		sFonts[i].handle = NULL;
		createBaseFontHandle(sFonts[i]);
	}

	// Update border pen for each Menu Item Appearance
	for(int i = 0, end = intSize(sMenuItemAppearances.size()); i < end; ++i)
	{
		MenuItemAppearance& aMenuItemApp = sMenuItemAppearances[i];
		aMenuItemApp.borderSize = 0;
		if( aMenuItemApp.baseBorderSize > 0 )
		{
			aMenuItemApp.borderSize = dropTo<u8>(max(1.0,
				aMenuItemApp.baseBorderSize * gUIScale));
		}
		aMenuItemApp.borderPen = getBorderPenHandle(
			aMenuItemApp.borderSize,
			aMenuItemApp.borderColor);
	}
}


void paintWindowContents(
	HDC hdc,
	HDC hCaptureDC,
	const POINT& theCaptureOffset,
	const SIZE& theTargetSize,
	int theOverlayID,
	bool needsInitialErase)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	DBG_ASSERT(size_t(theOverlayID) < sOverlayPaintStates.size());
	OverlayPaintState& ps = sOverlayPaintStates[theOverlayID];

	DrawData dd;
	dd.hdc = hdc;
	dd.hCaptureDC = hCaptureDC;
	dd.menuID = theMenuID;
	dd.overlayID = theOverlayID;
	dd.captureOffset = theCaptureOffset;
	dd.targetSize = theTargetSize;
	dd.itemDrawState = eMenuItemDrawState_Normal;
	dd.firstDraw = needsInitialErase;

	const EMenuStyle aMenuStyle = getMenuLayout(theMenuID).style;
	DBG_ASSERT(!ps.rects.empty());
	if( !sBitmapDrawSrc )
		sBitmapDrawSrc = CreateCompatibleDC(dd.hdc);
	if( !sClipRegion )
		sClipRegion = CreateRectRgn(0, 0, 0, 0);

	// Select the transparent color (erase) brush by default first
	SelectObject(dd.hdc, GetStockObject(DC_BRUSH));
	SetDCBrushColor(dd.hdc, getMenuAppearance(theMenuID).transColor);

	#ifdef _DEBUG
		COLORREF aFrameColor = RGB(0, 0, 0);
		#ifdef DEBUG_DRAW_OVERLAY_FRAMES
			aFrameColor = RGB(0, 0, 200);
		#endif
	#ifdef DEBUG_DRAW_TOTAL_OVERLAY_FRAME
		if( aMenuStyle == eMenuStyle_System )
			aFrameColor = RGB(255, 0, 0);
	#endif
	if( aFrameColor != RGB(0, 0, 0) && needsInitialErase )
	{
		HPEN hFramePen = CreatePen(PS_INSIDEFRAME, 3, aFrameColor);

		HPEN hOldPen = (HPEN)SelectObject(dd.hdc, hFramePen);
		Rectangle(hdc,
			ps.rects[0].left, ps.rects[0].top,
			ps.rects[0].right, ps.rects[0].bottom);
		SelectObject(dd.hdc, hOldPen);

		DeleteObject(hFramePen);

		// This effectively also erased the dest region
		needsInitialErase = false;
	}
	#endif // _DEBUG

	//if( needsInitialErase )
	//	eraseRect(dd, ps.rects[0]);

	switch(aMenuStyle)
	{
	case eMenuStyle_List:
	case eMenuStyle_Bar:
	case eMenuStyle_Grid:			/*drawBasicMenu(dd);*/	break;
	case eMenuStyle_Slots:			/*drawSlotsMenu(dd);*/	break;
	case eMenuStyle_4Dir:			/*draw4DirMenu(dd);*/	break;
	case eMenuStlye_Ring:			/* TODO */				break;
	case eMenuStyle_Radial:			/* TODO */				break;
	case eMenuStyle_Hotspots:		/*drawBasicMenu(dd);*/	break;
	case eMenuStyle_SelectHotspot:	/* TODO */				break;
	case eMenuStyle_Visual:
	case eMenuStyle_Label:
	case eMenuStyle_KBCycleLast:
	case eMenuStyle_KBCycleDefault:	/*drawBasicHUD(dd);*/	break;
	case eMenuStyle_HotspotGuide:	/*drawHSGuide(dd);*/	break;
	case eMenuStyle_System:			/*drawSystemHUD(dd);*/	break;
	default:						DBG_ASSERT(false && "Invaild Menu Style");
	}
}


void updateWindowLayout(
	int theOverlayID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect,
	POINT& theWindowPos,
	SIZE& theWindowSize)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	DBG_ASSERT(size_t(theOverlayID) < sOverlayPaintStates.size());
	OverlayPaintState& ps = sOverlayPaintStates[theOverlayID];
	const MenuPosition& thePos = getMenuPosition(theMenuID);
	const MenuLayout& theLayout = getMenuLayout(theMenuID);

	// Some special element types have their own unique calculation method
	switch(theLayout.style)
	{
	case eMenuStyle_Hotspots:
	case eMenuStyle_SelectHotspot:
		updateHotspotsMenuLayout(
			theOverlayID, theTargetSize, theTargetClipRect,
			theWindowPos, theWindowSize);
		return;
	}

	// To prevent too many rounding errors, initially calculate everything
	// as if gUIScale has value 1.0, then apply gUIScale it in a later step.
	double aWinBaseSizeX = 0;
	double aWinBaseSizeY = 0;
	double aWinScalingSizeX = theLayout.sizeX;
	double aWinScalingSizeY = theLayout.sizeY;
	int aMenuItemCount = 0;
	int aMenuItemXCount = 1;
	int aMenuItemYCount = 1;
	switch(theLayout.style)
	{
	case eMenuStyle_Slots:
		aWinScalingSizeX += theLayout.altLabelWidth;
		// fall through
	case eMenuStyle_List:
		aMenuItemYCount = aMenuItemCount = InputMap::menuItemCount(theMenuID);
		ps.rects.reserve(1 + aMenuItemCount);
		aWinBaseSizeY *= aMenuItemYCount;
		aWinScalingSizeY *= aMenuItemYCount;
		if( aMenuItemYCount > 1 )
			aWinScalingSizeY += theLayout.gapSizeY * (aMenuItemYCount - 1);
		aWinScalingSizeY += theLayout.titleHeight;
		break;
	case eMenuStyle_Bar:
		aMenuItemXCount = aMenuItemCount = InputMap::menuItemCount(theMenuID);
		ps.rects.reserve(1 + aMenuItemCount);
		aWinBaseSizeX *= aMenuItemXCount;
		aWinScalingSizeX *= aMenuItemXCount;
		if( aMenuItemXCount > 1 )
			aWinScalingSizeX += theLayout.gapSizeX * (aMenuItemXCount - 1);
		aWinScalingSizeY += theLayout.titleHeight;
		break;
	case eMenuStyle_Grid:
		aMenuItemCount = InputMap::menuItemCount(theMenuID);
		aMenuItemXCount = InputMap::menuGridWidth(theMenuID);
		aMenuItemYCount = InputMap::menuGridHeight(theMenuID);
		ps.rects.reserve(1 + aMenuItemCount);
		aWinBaseSizeX *= aMenuItemXCount;
		aWinBaseSizeY *= aMenuItemYCount;
		aWinScalingSizeX *= aMenuItemXCount;
		aWinScalingSizeY *= aMenuItemYCount;
		if( aMenuItemXCount > 1 )
			aWinScalingSizeX += theLayout.gapSizeX * (aMenuItemXCount-1);
		if( aMenuItemYCount > 1 )
			aWinScalingSizeY += theLayout.gapSizeY * (aMenuItemYCount-1);
		aWinScalingSizeY += theLayout.titleHeight;
		break;
	case eMenuStyle_4Dir:
		ps.rects.reserve(1 + 4);
		aWinBaseSizeX *= 2;
		aWinBaseSizeY *= 3;
		aWinScalingSizeX = aWinScalingSizeX * 2 + theLayout.gapSizeX;
		aWinScalingSizeY = aWinScalingSizeY * 3 + theLayout.gapSizeY * 2;
		aWinScalingSizeY += theLayout.titleHeight;
		break;
	case eMenuStyle_HotspotGuide:
	case eMenuStyle_System:
		ps.rects.reserve(2);
		aWinBaseSizeX = theTargetSize.cx;
		aWinBaseSizeY = theTargetSize.cy;
		aWinScalingSizeX = 0;
		aWinScalingSizeY = 0;
		break;
	default:
		ps.rects.reserve(1);
		break;
	}

	// Get base window position (top-left corner) assuming top-left alignment
	double aWinBasePosX = 0;//hotspotAnchorValue(thePos.base.x, theTargetSize.cx);
	double aWinBasePosY = 0;//hotspotAnchorValue(thePos.base.y, theTargetSize.cy);
	double aWinScalingPosX = thePos.base.x.offset;
	double aWinScalingPosY = thePos.base.y.offset;

	//// TODO: change this to directly use key bind cycle data
	//case eHUDType_KBCycleLast:
	//case eHUDType_KBCycleDefault:
	//	if( const Hotspot* aHotspot =
	//			InputMap::KeyBindCycleHotspot(hi.kbArrayID, ???) )
	//	{
	//		result = *aHotspot;
	//	}
	//	break;

	// Adjust position according to size and alignment settings
	switch(theLayout.alignmentX)
	{
	case eAlignment_Min:
		// Do nothing
		break;
	case eAlignment_Center:
		aWinBasePosX -= aWinBaseSizeX * 0.5;
		aWinScalingPosX -= aWinScalingSizeX * 0.5;
		break;
	case eAlignment_Max:
		aWinBasePosX += 1;
		aWinBasePosX -= aWinBaseSizeX;
		aWinScalingPosX -= aWinScalingSizeX;
		break;
	}
	switch(theLayout.alignmentY)
	{
	case eAlignment_Min:
		// Do nothing
		break;
	case eAlignment_Center:
		aWinBasePosY -= aWinBaseSizeY * 0.5;
		aWinScalingPosY -= aWinScalingSizeY * 0.5;
		break;
	case eAlignment_Max:
		aWinBasePosY += 1;
		aWinBasePosY -= aWinBaseSizeY;
		aWinScalingPosY -= aWinScalingSizeY;
		break;
	}

	// Apply UI scale to scaling portions of each coordinate
	if( gUIScale != 1.0 )
	{
		aWinScalingSizeX *= gUIScale;
		aWinScalingSizeY *= gUIScale;
		aWinScalingPosX *= gUIScale;
		aWinScalingPosY *= gUIScale;
	}

	// Add together base and scaling portions and round off
	int aWinSizeX = int(max(0.0, ceil(aWinBaseSizeX + aWinScalingSizeX)));
	int aWinSizeY = int(max(0.0, ceil(aWinBaseSizeY + aWinScalingSizeY)));
	int aWinPosX = int(floor(aWinBasePosX + aWinScalingPosX));
	int aWinPosY = int(floor(aWinBasePosY + aWinScalingPosY));

	// Clip actual window to target area
	RECT aWinRect =
		{ aWinPosX, aWinPosY, aWinPosX + aWinSizeX, aWinPosY + aWinSizeY };
	RECT aWinClippedRect;
	aWinClippedRect.left = max(aWinRect.left, theTargetClipRect.left);
	aWinClippedRect.top = max(aWinRect.top, theTargetClipRect.top);
	aWinClippedRect.right = min(aWinRect.right, theTargetClipRect.right);
	aWinClippedRect.bottom = min(aWinRect.bottom, theTargetClipRect.bottom);
	theWindowPos.x = aWinClippedRect.left;
	theWindowPos.y = aWinClippedRect.top;
	theWindowSize.cx = aWinClippedRect.right - aWinClippedRect.left;
	theWindowSize.cy = aWinClippedRect.bottom - aWinClippedRect.top;

	// Calculate component rects based on menu type
	ps.rects.clear();
	POINT aCompTopLeft =
		{ aWinPosX - theWindowPos.x, aWinPosY - theWindowPos.y };
	POINT aCompBotRight =
		{ aCompTopLeft.x + aWinSizeX, aCompTopLeft.y + aWinSizeY };
	// Component 0 is the entire dest rect (before clipping)
	RECT anItemRect;
	anItemRect.left = aCompTopLeft.x;
	anItemRect.top = aCompTopLeft.y;
	anItemRect.right = aCompBotRight.x;
	anItemRect.bottom = aCompBotRight.y;
	ps.rects.push_back(anItemRect);
	double aCenterX;

	switch(theLayout.style)
	{
	case eMenuStyle_Slots:
		if( theLayout.altLabelWidth )
		{
			if( theLayout.alignmentX == eAlignment_Max )
			{// Align components to right edge of window
				aCompTopLeft.x = LONG(aCompBotRight.x -
					ceil(gUIScale * theLayout.sizeX));
			}
			else
			{// Restrict components to left edge of window
				aCompBotRight.x = LONG(aCompTopLeft.x +
					ceil(gUIScale * theLayout.sizeX));
			}
		}
		// fall through
	case eMenuStyle_List:
	case eMenuStyle_Bar:
	case eMenuStyle_Grid:
		for(int y = 0; y < aMenuItemYCount; ++y)
		{
			anItemRect.top = max<LONG>(aCompTopLeft.y,
				LONG(aCompTopLeft.y + gUIScale *
					(theLayout.titleHeight + y *
						(theLayout.sizeY + theLayout.gapSizeY))));
			anItemRect.bottom = aCompBotRight.y;
			if( y < aMenuItemYCount - 1 )
			{
				anItemRect.bottom = LONG(aCompTopLeft.y + gUIScale *
					(theLayout.titleHeight +
						theLayout.sizeY * (y+1) + theLayout.gapSizeY * y));
			}
			for(int x = 0; x < aMenuItemXCount; ++x)
			{
				if( dropTo<int>(ps.rects.size()) == aMenuItemCount + 1 )
					break;
				anItemRect.left = aCompTopLeft.x;
				anItemRect.right = aCompBotRight.x;
				if( x > 0 )
				{
					anItemRect.left += LONG(gUIScale *
						(theLayout.sizeX + theLayout.gapSizeX) * x);
				}
				if( x < aMenuItemXCount - 1 )
				{
					anItemRect.right = LONG(aCompTopLeft.x + gUIScale *
						(theLayout.sizeX * (x+1) + theLayout.gapSizeX * x));
				}
				ps.rects.push_back(anItemRect);
			}
		}
		break;
	case eMenuStyle_4Dir:
		aCenterX = (aCompTopLeft.x + aCompBotRight.x) * 0.5;
		for(u16 itemIdx = 0; itemIdx < eCmdDir_Num; ++itemIdx)
		{
			switch(itemIdx)
			{
			case eCmdDir_Left:
				anItemRect.left = aCompTopLeft.x;
				anItemRect.right =
					LONG(aCenterX - theLayout.gapSizeX * 0.5 * gUIScale);
				break;
			case eCmdDir_Right:
				anItemRect.left =
					LONG(aCenterX + theLayout.gapSizeX * 0.5 * gUIScale);
				anItemRect.right = aCompBotRight.x;
				break;
			case eCmdDir_Up:
			case eCmdDir_Down:
				anItemRect.left = LONG(floor(aCenterX -
					(gUIScale * theLayout.sizeX) * 0.5));
				anItemRect.right = LONG(ceil(aCenterX +
					(gUIScale * theLayout.sizeX) * 0.5));
				break;
			}

			switch(itemIdx)
			{
			case eCmdDir_Up:
				anItemRect.top = max<LONG>(aCompTopLeft.y,
					LONG(aCompTopLeft.y + gUIScale * theLayout.titleHeight));
				anItemRect.bottom =
					LONG(aCompTopLeft.y + gUIScale *
						(theLayout.titleHeight + theLayout.sizeY));
				break;
			case eCmdDir_Left:
			case eCmdDir_Right:
				anItemRect.top =
					LONG(aCompTopLeft.y + gUIScale *
						(theLayout.titleHeight +
							theLayout.sizeY + theLayout.gapSizeY));
				anItemRect.bottom =
					LONG(aCompTopLeft.y + gUIScale *
						(theLayout.titleHeight +
							theLayout.sizeY * 2 + theLayout.gapSizeY));
				break;
			case eCmdDir_Down:
				anItemRect.top =
					LONG(aCompTopLeft.y + gUIScale *
						(theLayout.titleHeight +
							(theLayout.sizeY + theLayout.gapSizeY) * 2));
				anItemRect.bottom = min<LONG>(aCompBotRight.y,
					LONG(aCompTopLeft.y + gUIScale *
						(theLayout.titleHeight +
							theLayout.sizeY * 3 + theLayout.gapSizeY * 2)));
				break;
			}
			ps.rects.push_back(anItemRect);
		}
		break;
	case eMenuStyle_HotspotGuide:
	case eMenuStyle_System:
		// These may reference clipped window rect as Component 1
		ps.rects.push_back(aWinClippedRect);
		break;
	}

	if( theLayout.style == eMenuStyle_Slots && theLayout.altLabelWidth )
	{// Add extra component for the alt label rect
		const u8 aBorderSize =
			getMenuItemAppearance(theMenuID, eMenuItemDrawState_Normal)
				.borderSize;
		const u8 aSelectedBorderSize =
			getMenuItemAppearance(theMenuID, eMenuItemDrawState_Selected)
				.borderSize;
		anItemRect.left = (theLayout.alignmentX == eAlignment_Max)
			? ps.rects[0].left
			: ps.rects[1].right - aBorderSize;
		anItemRect.right = (theLayout.alignmentX == eAlignment_Max)
			? ps.rects[1].left + aBorderSize
			: ps.rects[0].right;
		anItemRect.top = (theLayout.titleHeight > 0)
			? ps.rects[1].top
			: ps.rects[1].top + aSelectedBorderSize;
		anItemRect.bottom = (theLayout.titleHeight > 0)
			? ps.rects[1].bottom - aSelectedBorderSize * 2
			: ps.rects[1].bottom - aSelectedBorderSize;
		ps.rects.push_back(anItemRect);
	}
}


void paintMainWindowContents(HWND theWindow, bool asDisabled)
{
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(theWindow, &ps);

	RECT aRect;
	GetClientRect(theWindow, &aRect);

	// Swap to a recommended system font instead of default depracated version
	NONCLIENTMETRICS ncm;
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
		sizeof(NONCLIENTMETRICS), &ncm, 0);
	HFONT hOldFont = (HFONT)SelectObject(hdc,
		CreateFontIndirect(&ncm.lfMessageFont));

	const std::wstring& aWStr = widen(kAppVersionString);

	// Make sure string will actually fit with default system font
	//if( LONG aFontHeight = getFontHeightToFit(
	//		hdc, aWStr, aRect, NULL, DT_SINGLELINE) )
	//{// Need alternate font to actually fit string properly
	//	ncm.lfMessageFont.lfWidth = 0;
	//	ncm.lfMessageFont.lfHeight = aFontHeight;
	//	DeleteObject(SelectObject(hdc,
	//		CreateFontIndirect(&ncm.lfMessageFont)));
	//}

	// Set to appear grayed out while window is disabled/inactive
	COLORREF oldTextColor = 0;
	if( asDisabled )
		oldTextColor = SetTextColor(hdc, RGB(128, 128, 128));

	// Draw version string centered
	DrawText(hdc, aWStr.c_str(), -1, &aRect,
		DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);


	// Cleanup
	if( asDisabled )
		SetTextColor(hdc, oldTextColor);
	DeleteObject(SelectObject(hdc, hOldFont));

	EndPaint(theWindow, &ps);
}


void flashSystemOverlayBorder()
{
	sSystemBorderFlashTimer = kSystemOverlayFlashTime;
}


void setSystemOverlayDrawHook(SystemPaintFunc theFunc)
{
	sSystemOverlayPaintFunc = theFunc;
	if( kSystemOverlayID < gFullRedrawOverlays.size() )
		gFullRedrawOverlays.set(kSystemOverlayID);
}


void redrawSystemOverlay(bool fullRedraw)
{
	if( fullRedraw )
	{
		if( kSystemOverlayID < gFullRedrawOverlays.size() )
			gFullRedrawOverlays.set(kSystemOverlayID);
	}
	else
	{
		if( kSystemOverlayID < gRefreshOverlays.size() )
			gRefreshOverlays.set(kSystemOverlayID);
	}
}


const WindowAlphaInfo& alphaInfo(int theOverlayID)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	return getMenuAlphaInfo(theMenuID);
}


const std::vector<RECT>& windowLayoutRects(int theOverlayID)
{
	DBG_ASSERT(size_t(theOverlayID) < sOverlayPaintStates.size());
	return sOverlayPaintStates[theOverlayID].rects;
}


COLORREF transColor(int theOverlayID)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	return getMenuAppearance(theMenuID).transColor;
}


int drawPriority(int theOverlayID)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	return getMenuPosition(theMenuID).drawPriority;
}

} // WindowPainter
