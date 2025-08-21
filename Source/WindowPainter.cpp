//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "WindowPainter.h"

#include "HotspotMap.h"
#include "InputMap.h"
#include "Menus.h"
#include "Profile.h"

namespace WindowPainter
{

//#pragma warning(disable:4244) // TODO - remove this and fix warnings here!

// Make the region of each overlay window obvious by drawing a frame
//#define DEBUG_DRAW_OVERLAY_FRAMES
// Make the overall overlay region obvious by drawing a frame
//#define DEBUG_DRAW_TOTAL_OVERLAY_FRAME

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

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


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ZERO_INIT(FontInfo)
{
	HFONT handle;
	std::string name;
	int size;
	int weight;
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
	u16 parentHotspotID;
	u16 parentKBCycleID;
	s8 drawPriority;

	bool operator==(const MenuPosition& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(MenuPosition)) == 0; }
};

struct ZERO_INIT(MenuLayout)
{
	EMenuStyle style;
	u16 sizeX;
	u16 sizeY;
	u16 titleHeight;
	u16 altLabelWidth;
	s8 gapSizeX;
	s8 gapSizeY;
	u8 alignmentX;
	u8 alignmentY;

	bool operator==(const MenuLayout& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(MenuLayout)) == 0; }
};

typedef std::vector<RECT> MenuLayoutRects;

struct ZERO_INIT(MenuAppearance)
{
	EMenuItemType itemType;
	COLORREF transColor;
	COLORREF titleColor;
	u32 flashMaxTime;
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
	u16 alphaInfoID;
	u16 appearanceID;
	u16 itemAppearanceID[eMenuItemDrawState_Num];
	
	MenuCacheEntry()
	{
		positionID = layoutID = alphaInfoID = appearanceID = kInvalidID;
		for(int i = 0; i < eMenuItemDrawState_Num; ++i)
			itemAppearanceID[i] = kInvalidID;
	}
};

struct OverlayPaintState
{
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
	const std::vector<RECT>& components;
	POINT captureOffset;
	SIZE targetSize;
	int overlayID;
	EMenuItemDrawState itemDrawState;
	bool firstDraw;

	DrawData(const std::vector<RECT>& theComponents)
		: components(theComponents) {}
};

//struct HUDBuilder
//{
//	std::vector<std::string> parsedString;
//	StringToValueMap<u16, u8> fontInfoToFontIDMap;
//	typedef std::pair< COLORREF, std::pair<int, int> > PenDef;
//	VectorMap<PenDef, u16> penDefToPenMap;
//	StringToValueMap<HBITMAP> bitmapNameToHandleMap;
//	std::vector<BuildIconEntry> iconBuilders;
//
//	~HUDBuilder() { DBG_ASSERT(bitmapNameToHandleMap.empty()); }
//};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<FontInfo> sFonts;
static std::vector<PenInfo> sPens;
static StringToValueMap<BitmapFileInfo> sBitmapFiles;
static std::vector<BitmapIcon> sBitmapIcons;
static StringToValueMap<LabelIcon> sLabelIcons;
static std::vector<MenuPosition> sMenuPositions;
static std::vector<MenuLayout> sMenuLayouts;
static std::vector<MenuLayoutRects> sMenuLayoutRects;
static std::vector<MenuAppearance> sMenuAppearances;
static std::vector<MenuItemAppearance> sMenuItemAppearances;
static std::vector<MenuAlphaInfo> sMenuAlphaInfo;
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

#if 0

//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

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


static COLORREF strToRGB(HUDBuilder& theBuilder, const std::string& theString)
{
	theBuilder.parsedString.clear();
	sanitizeSentence(theString, theBuilder.parsedString);
	theBuilder.parsedString.resize(3);
	const u8 r = u32FromString(theBuilder.parsedString[0]) & 0xFF;
	const u8 g = u32FromString(theBuilder.parsedString[1]) & 0xFF;
	const u8 b = u32FromString(theBuilder.parsedString[2]) & 0xFF;
	return RGB(r, g, b);
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
	if( hi.radius > 0 )
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


static void updateHotspotsMenuLayout(
	const HUDElementInfo& hi,
	const u16 theMenuID,
	const SIZE& theTargetSize,
	std::vector<RECT>& theComponents,
	POINT& theWindowPos,
	SIZE& theWindowSize,
	const RECT& theTargetClipRect)
{
	DBG_ASSERT(hi.type == eMenuStyle_Hotspots);
	const u16 aHotspotArrayID = InputMap::menuHotspotArray(theMenuID);
	const u16 anItemCount = InputMap::menuItemCount(theMenuID);
	const u16 aFirstHotspot = InputMap::firstHotspotInArray(aHotspotArrayID);
	DBG_ASSERT(anItemCount == InputMap::sizeOfHotspotArray(aHotspotArrayID));
	theComponents.reserve(anItemCount + 1);
	theComponents.resize(1);
	RECT aWinRect = { 0 };
	aWinRect.left = theTargetSize.cx;
	aWinRect.top = theTargetSize.cy;
	const SIZE& aCompSize = hotspotToSize(hi.itemSize, theTargetSize);
	const SIZE aCompHalfSize = { aCompSize.cx / 2, aCompSize.cy / 2 };
	for(u16 i = aFirstHotspot; i < aFirstHotspot + anItemCount; ++i)
	{
		const POINT& anItemPos = hotspotToPoint(
			InputMap::getHotspot(i), theTargetSize);

		RECT anItemRect;
		anItemRect.left = anItemPos.x;
		if( hi.alignmentX == eAlignment_Center )
			anItemRect.left -= aCompHalfSize.cx;
		else if( hi.alignmentX == eAlignment_Max )
			anItemRect.left -= aCompSize.cx - 1;
		anItemRect.right = anItemRect.left + aCompSize.cx;
		anItemRect.top = anItemPos.y;
		if( hi.alignmentY == eAlignment_Center )
			anItemRect.top -= aCompHalfSize.cy;
		else if( hi.alignmentY == eAlignment_Max )
			anItemRect.top -= aCompSize.cy - 1;
		anItemRect.bottom = anItemRect.top + aCompSize.cy;
		theComponents.push_back(anItemRect);
		aWinRect.left = min(aWinRect.left, anItemRect.left);
		aWinRect.top = min(aWinRect.top, anItemRect.top);
		aWinRect.right = max(aWinRect.right, anItemRect.right);
		aWinRect.bottom = max(aWinRect.bottom, anItemRect.bottom);
	}
	if( hi.titleHeight > 0 )
		aWinRect.top -= ceil(hi.titleHeight * gUIScale);
	theComponents[0] = aWinRect;

	// Clip actual window to target area
	aWinRect.left = max(aWinRect.left, theTargetClipRect.left);
	aWinRect.top = max(aWinRect.top, theTargetClipRect.top);
	aWinRect.right = min(aWinRect.right, theTargetClipRect.right);
	aWinRect.bottom = min(aWinRect.bottom, theTargetClipRect.bottom);
	theWindowPos.x = aWinRect.left;
	theWindowPos.y = aWinRect.top;
	theWindowSize.cx = aWinRect.right - aWinRect.left;
	theWindowSize.cy = aWinRect.bottom - aWinRect.top;

	// Make all components be window-relative instead of target-relative
	for(std::vector<RECT>::iterator itr = theComponents.begin();
		itr != theComponents.end(); ++itr)
	{
		itr->left -= theWindowPos.x;
		itr->top -= theWindowPos.y;
		itr->right -= theWindowPos.x;
		itr->bottom -= theWindowPos.y;
	}
}


void loadHUDElementShape(
	HUDElementInfo& hi,
	const std::string& theHUDName,
	bool isAMenu)
{
	// hi.position = eHUDProp_Position
	hi.position = strToHotspot(
		getNamedHUDPropStr(theHUDName, eHUDProp_Position),
		theHUDName, eHUDProp_Position);
	// hi.itemSize = eHUDProp_ItemSize (Menus) or eHUDProp_Size (HUD)
	std::string aStr;
	if( isAMenu )
		aStr = getNamedHUDPropStr(theHUDName, eHUDProp_ItemSize);
	else
		aStr = getNamedHUDPropStr(theHUDName, eHUDProp_Size);
	if( aStr.empty() && isAMenu )
		aStr = getNamedHUDPropStr(theHUDName, eHUDProp_Size);
	if( aStr.empty() )
		aStr = getHUDPropStr(theHUDName, eHUDProp_ItemSize);
	hi.itemSize = strToHotspot(aStr, theHUDName, eHUDProp_ItemSize);
	// hi.alignmentX/Y = eHUDProp_Alignment
	aStr = getNamedHUDPropStr(theHUDName, eHUDProp_Alignment);
	Hotspot aTempHotspot;
	if( !aStr.empty() )
		aTempHotspot = strToHotspot(aStr, theHUDName, eHUDProp_Alignment);
	hi.alignmentX =
		aTempHotspot.x.anchor < 0x4000	? eAlignment_Min :
		aTempHotspot.x.anchor > 0xC000	? eAlignment_Max :
		/*otherwise*/					  eAlignment_Center;
	hi.alignmentY =
		aTempHotspot.y.anchor < 0x4000	? eAlignment_Min :
		aTempHotspot.y.anchor > 0xC000	? eAlignment_Max :
		/*otherwise*/					  eAlignment_Center;
}
#endif


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	cleanup();

	sCopyRectUpdateRate =
		Profile::getInt("System", "CopyRectFrameTime", sCopyRectUpdateRate);

	sMenuDrawCache.resize(InputMap::menuCount());
	sOverlayPaintStates.reserve(InputMap::menuOverlayCount());
	sOverlayPaintStates.resize(InputMap::menuOverlayCount());

	// Add base/sentinel 0th entry to some vectors
	sBitmapIcons.resize(1);
	sMenuPositions.resize(1);
	sMenuLayouts.resize(1);
	sMenuLayoutRects.resize(1);
	sMenuAppearances.resize(1);
	sMenuAlphaInfo.resize(1);
	sMenuItemAppearances.resize(eMenuItemDrawState_Num);

	{// Prepare for loaded bitmap file handles
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kBitmapsSectionName);
		sBitmapFiles.reserve(aPropMap.size()+1);
		sBitmapFiles.setValue("", BitmapFileInfo()); // copy icon entry
	}

	{// Set up converting text labels to icons
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kIconsSectionName);
		//for(int i = 0; i < aPropMap.size(); ++i)
		//{
		//	getOrCreateLabelIcon(
		//		aPropMap.keys()[i],
		//		aPropMap.vals()[i].str);
		//}
	}

	// Add data from base [Appearance] section for default look
	// (use the full profile data set, not just theProfileMap)
	const Profile::PropertyMap& aDefaultProps =
		Profile::getSectionProperties(kMenuDefaultSectionName);
	//fetchMenuLayoutProperties(aDefaultProps, sMenuLayouts[0]);
	//fetchBaseAppearanceProperties(aDefaultProps, sMenuAppearances[0]);
	//for(int i = 0; i < eMenuItemDrawState_Num; ++i)
	//{
	//	fetchItemAppearanceProperties(aDefaultProps,
	//		sMenuItemAppearances[i], EMenuItemDrawState(i));
	//}
	sMenuAlphaInfo[0] = MenuAlphaInfo();
	sMenuAlphaInfo[0].fadeInRate = sMenuAlphaInfo[0].fadeOutRate = 255;
	sMenuAlphaInfo[0].maxAlpha = sMenuAlphaInfo[0].inactiveAlpha = 255;
	//fetchAlphaFadeProperties(aDefaultProps, sMenuAlphaInfo[0]);

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
		//theSystemMenu.positionID = getOrCreateMenuPositionID(aMenuPos);
	}

	{// Add data for the  built-in _HotspotGuide menu
		DBG_ASSERT(InputMap::menuStyle(kHotspotGuideMenuID) ==
			eMenuStyle_HotspotGuide);
		DBG_ASSERT(InputMap::menuOverlayID(kHotspotGuideMenuID) ==
			kHotspotGuideOverlayID);
		MenuCacheEntry& theHSGuideMenu = sMenuDrawCache[kHotspotGuideMenuID];
		MenuAlphaInfo anAlphaInfo = sMenuAlphaInfo[0];
		anAlphaInfo.maxAlpha = u8(clamp(Profile::getInt(
			aDefaultProps, "HotspotGuideAlpha", 0), 0, 255));
		anAlphaInfo.inactiveFadeOutDelay = max(0, Profile::getInt(
			aDefaultProps, "HotspotGuideDisplayTime", 1000));
		anAlphaInfo.inactiveAlpha = 0;
		//theHSGuideMenu.alphaInfoID = getOrCreateAlphaInfoID(anAlphaInfo);
		MenuAppearance anAppearance = sMenuAppearances[0];
		anAppearance.itemType = eMenuItemType_Circle;
		anAppearance.radius = dropTo<u8>(clamp((Profile::getInt(
			aDefaultProps, "HotspotGuideSize", 6) / 2), 1, 255));
		//theHSGuideMenu.appearanceID =
		//	getOrCreateBaseAppearanceID(anAppearance);
		MenuItemAppearance anItemAppearance =
			sMenuItemAppearances[eMenuItemDrawState_Normal];
		//anItemAppearance.baseColor = getRGBProp(
		//	aDefaultProps, "HotspotGuideRGB", "128, 128, 128");
		//theHSGuideMenu.itemAppearanceID[eMenuItemDrawState_Normal] =
		//	getOrCreateItemAppearanceID(anItemAppearance);
		for(u16 i = 0; i < eMenuItemDrawState_Num; ++i)
			theHSGuideMenu.itemAppearanceID[i] = i;
		theHSGuideMenu.layoutID = 0;
		MenuPosition aMenuPos;
		// Lower than any can be manually set to
		aMenuPos.drawPriority = -127;
		//theHSGuideMenu.positionID = getOrCreateMenuPositionID(aMenuPos);
	}
}


void loadProfileChanges()
{
	// TODO - invalidate (mark dirty) cache entries that might have changed
	// Just run init() if change kMenuDefaultSectionName section

	sCopyRectUpdateRate =
		Profile::getInt("System", "CopyRectFrameTime", sCopyRectUpdateRate);

	//if( const PropertyMap* aSection = theProfileMap.find(kIconsSectionName) )
	{// Set up converting text labels to icons
		//for(int i = 0; i < aSection.size(); ++i)
		//{
		//	getOrCreateLabelIcon(
		//		aSection.keys()[i],
		//		aSection.vals()[i].str);
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

	sFonts.clear();
	sPens.clear();
	sBitmapFiles.clear();
	sBitmapIcons.clear();
	sLabelIcons.clear();
	sMenuPositions.clear();
	sMenuLayouts.clear();
	sMenuLayoutRects.clear();
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
#if 0
	// Handle display of error messages and other notices via _System HUD
	DBG_ASSERT(sSystemHUDElementID < sHUDElementInfo.size());
	DBG_ASSERT(sHUDElementInfo[sSystemHUDElementID].type == eHUDType_System);

	if( sErrorMessageTimer > 0 )
	{
		sErrorMessageTimer -= gAppFrameTime;
		if( sErrorMessageTimer <= 0 )
		{
			sErrorMessageTimer = 0;
			sErrorMessage.clear();
			gFullRedrawHUD.set(sSystemHUDElementID);
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
		gFullRedrawHUD.set(sSystemHUDElementID);
	}

	if( sNoticeMessageTimer > 0 )
	{
		sNoticeMessageTimer -= gAppFrameTime;
		if( sNoticeMessageTimer <= 0 )
		{
			sNoticeMessageTimer = 0;
			sNoticeMessage.clear();
			gFullRedrawHUD.set(sSystemHUDElementID);
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
		gFullRedrawHUD.set(sSystemHUDElementID);
	}

	bool showSystemBorder = false;
	if( sSystemBorderFlashTimer > 0 &&
		sErrorMessage.empty() && sNoticeMessage.empty() )
	{
		showSystemBorder =
			((sSystemBorderFlashTimer / kSystemHUDFlashFreq) & 0x01) != 0;
		sSystemBorderFlashTimer = max(0, sSystemBorderFlashTimer - gAppFrameTime);
		if( gVisibleHUD.test(sSystemHUDElementID) != showSystemBorder )
			gRefreshHUD.set(sSystemHUDElementID);
	}

	#ifdef DEBUG_DRAW_TOTAL_OVERLAY_FRAME
		showSystemBorder = true;
	#endif

	gVisibleHUD.set(sSystemHUDElementID,
		!sNoticeMessage.empty() ||
		!sErrorMessage.empty() ||
		sSystemOverlayPaintFunc ||
		showSystemBorder);

	if( gVisibleHUD.test(sSystemHUDElementID) )
		gActiveHUD.set(sSystemHUDElementID);

	// Check for updates to other HUD elements
	for(u16 i = 0; i < sHUDElementInfo.size(); ++i)
	{
		HUDElementInfo& hi = sHUDElementInfo[i];
		switch(hi.type)
		{
		case eHUDType_KBCycleLast:
			TODO: Make it so if gKeyBindCycleLastIndex < 0 (use default next) that
			keep selection in last position and make this HUD element fade out to
			invisible even if it is set to still be visible in profile scheme
			if( gKeyBindCycleLastIndexChanged.test(hi.kbArrayID) )
			{
				gActiveHUD.set(i);
				gReshapeHUD.set(i);
				hi.selection = gKeyBindCycleLastIndex[hi.kbArrayID];
			}
			break;
		case eHUDType_KBCycleDefault:
			if( gKeyBindCycleDefaultIndexChanged.test(hi.kbArrayID) )
			{
				gActiveHUD.set(i);
				gReshapeHUD.set(i);
				hi.selection = gKeyBindCycleDefaultIndex[hi.kbArrayID];
			}
			break;
		}

		// Update flash effect on confirmed menu item
		if( gConfirmedMenuItem[i] != kInvalidID )
		{
			if( hi.flashMaxTime )
			{
				hi.flashing = gConfirmedMenuItem[i];
				hi.flashStartTime = gAppRunTime;
				gRefreshHUD.set(i);
			}
			gConfirmedMenuItem[i] = kInvalidID;
		}
		else if( hi.flashing != kInvalidID &&
				 (gAppRunTime - hi.flashStartTime) > hi.flashMaxTime )
		{
			hi.flashing = kInvalidID;
			gRefreshHUD.set(i);
		}
	}

	switch(gHotspotsGuideMode)
	{
	case eHotspotGuideMode_Disabled:
		gVisibleHUD.reset(sHotspotGuideHUDElementID);
		break;
	case eHotspotGuideMode_Redraw:
		gRefreshHUD.set(sHotspotGuideHUDElementID);
		// fall through
	case eHotspotGuideMode_Redisplay:
		gActiveHUD.set(sHotspotGuideHUDElementID);
		gHotspotsGuideMode = eHotspotGuideMode_Showing;
		// fall through
	case eHotspotGuideMode_Showing:
		gVisibleHUD.set(sHotspotGuideHUDElementID);
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
			// Make sure this item is still valid
			u16 aMaxItemID = 0;
			DBG_ASSERT(itr->hudElementID < sHUDElementInfo.size());
			HUDElementInfo& hi = sHUDElementInfo[itr->hudElementID];
			const bool isAMenu =
				hi.type >= eMenuStyle_Begin && hi.type < eMenuStyle_End;
			if( isAMenu )
			{
				aMaxItemID = Menus::itemCount(
					InputMap::menuForHUDElement(itr->hudElementID)) - 1;
			}
			if( itr->itemIdx > aMaxItemID )
			{// Invalid item ID, can't refresh, just remove it from queue
				next_itr = sAutoRefreshCopyRectQueue.erase(itr);
				continue;
			}
			// If hi.forcedRedrawItemID is already set as a valid entry, it's
			// still waiting for it to refresh (might be currently hidden), so
			// don't count any of this HUD Element's items as valid until then
			if( hi.forcedRedrawItemID <= aMaxItemID )
			{
				gRefreshHUD.set(itr->hudElementID);
				continue;
			}
			// Only first valid item found actually refreshes, and only once
			// have waited at least one time since found valid items
			if( validItemCount++ == 0 && sAutoRefreshCopyRectTime > 0 )
			{
				gRefreshHUD.set(itr->hudElementID);
				sHUDElementInfo[itr->hudElementID].
					forcedRedrawItemID = itr->itemIdx;
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
#endif
}


void updateScaling()
{
#if 0
	if( sHUDElementInfo.empty() )
		return;

	// Reset menu item cache entries
	for(size_t i = 0; i < sMenuDrawCache.size(); ++i)
	{
		for(size_t j = 0; j < sMenuDrawCache[i].size(); ++j)
			sMenuDrawCache[i][j] = MenuDrawCacheEntry();
	}

	// Clear fonts and border pens
	sResizedFontsMap.clear();
	for(size_t i = 0; i < sFonts.size(); ++i)
		DeleteObject(sFonts[i]);
	for(size_t i = 0; i < sPens.size(); ++i)
		DeleteObject(sPens[i]);
	sFonts.clear();
	sPens.clear();

	// Generate fonts, border pens, etc based on current gUIScale values
	HUDBuilder aHUDBuilder;
	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
		if( hi.type == eHUDType_System || hi.type == eHUDType_HotspotGuide )
			continue;
		const std::string& aHUDName =
			InputMap::hudElementKeyName(aHUDElementID);
		hi.fontID = getOrCreateFontID(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontName),
			getHUDPropStr(aHUDName, eHUDProp_FontSize),
			getHUDPropStr(aHUDName, eHUDProp_FontWeight));
		hi.scaledRadius = hi.radius * gUIScale;
	}
	for(u16 anAppearanceID = 0;
		anAppearanceID < sAppearances.size();
		++anAppearanceID)
	{
		Appearance& appearance = sAppearances[anAppearanceID];
		appearance.borderSize = 0;
		if( appearance.baseBorderSize > 0 )
		{
			appearance.borderSize =
				max(1.0, appearance.baseBorderSize * gUIScale);
		}
		appearance.borderPenID = getOrCreatePenID(aHUDBuilder,
			appearance.borderColor, appearance.borderSize);
	}
#endif
}


void paintWindowContents(
	HDC hdc,
	HDC hCaptureDC,
	const POINT& theCaptureOffset,
	const SIZE& theTargetSize,
	int theMenuID,
	bool needsInitialErase)
{
#if 0
	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	DBG_ASSERT(!theComponents.empty());
	HUDDrawData aDrawData(theComponents);
	aDrawData.hdc = hdc;
	aDrawData.hCaptureDC = hCaptureDC;
	aDrawData.captureOffset = theCaptureOffset;
	aDrawData.targetSize = theTargetSize;
	aDrawData.hudElementID = theHUDElementID;
	aDrawData.appearanceMode = eAppearanceMode_Normal;
	aDrawData.firstDraw = needsInitialErase;
	if( !sBitmapDrawSrc )
		sBitmapDrawSrc = CreateCompatibleDC(hdc);
	if( !sClipRegion )
		sClipRegion = CreateRectRgn(0, 0, 0, 0);

	// Select the transparent color (erase) brush by default first
	SelectObject(hdc, GetStockObject(DC_BRUSH));
	SetDCBrushColor(hdc, hi.transColor);

	#ifdef _DEBUG
		COLORREF aFrameColor = RGB(0, 0, 0);
		#ifdef DEBUG_DRAW_OVERLAY_FRAMES
			aFrameColor = RGB(0, 0, 200);
		#endif
	#ifdef DEBUG_DRAW_TOTAL_OVERLAY_FRAME
		if( hi.type == eHUDType_System )
			aFrameColor = RGB(255, 0, 0);
	#endif
	if( aFrameColor != RGB(0, 0, 0) && needsInitialErase )
	{
		HPEN hFramePen = CreatePen(PS_INSIDEFRAME, 3, aFrameColor);

		HPEN hOldPen = (HPEN)SelectObject(hdc, hFramePen);
		Rectangle(hdc, theComponents[0].left, theComponents[0].top,
			theComponents[0].right, theComponents[0].bottom);
		SelectObject(hdc, hOldPen);

		DeleteObject(hFramePen);

		// This effectively also erased the dest region
		needsInitialErase = false;
	}
	#endif // _DEBUG

	if( needsInitialErase )
		eraseRect(aDrawData, theComponents[0]);

	const EHUDType aHUDType = sHUDElementInfo[theHUDElementID].type;
	switch(aHUDType)
	{
	case eMenuStyle_List:
	case eMenuStyle_Bar:
	case eMenuStyle_Grid:
	case eMenuStyle_Hotspots:		drawBasicMenu(aDrawData);	break;
	case eMenuStyle_Slots:			drawSlotsMenu(aDrawData);	break;
	case eMenuStyle_4Dir:			draw4DirMenu(aDrawData);	break;
	case eMenuStlye_Ring:			/* TODO */					break;
	case eMenuStyle_Radial:			/* TODO */					break;
	case eHUDType_Hotspot:
	case eHUDType_KBCycleLast:
	case eHUDType_KBCycleDefault:	drawBasicHUD(aDrawData);	break;
	case eHUDType_HotspotGuide:		drawHSGuide(aDrawData);		break;
	case eHUDType_System:			drawSystemHUD(aDrawData);	break;
	default:
		if( aHUDType >= eHUDItemType_Begin && aHUDType < eHUDItemType_End )
			drawHUDItem(aDrawData, aDrawData.components[0]);
		else
			DBG_ASSERT(false && "Invaild HUD/Menu Type/Style!");
	}
#endif
}


void updateWindowLayout(
	int theMenuID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect,
	POINT& theWindowPos,
	SIZE& theWindowSize)
{
#if 0
	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	const HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];
	const int aMenuID = InputMap::menuForHUDElement(theHUDElementID);

	// Some special element types have their own unique calculation method
	switch(hi.type)
	{
	case eMenuStyle_Hotspots:
		updateHotspotsMenuLayout(
			hi, aMenuID, theTargetSize, theComponents,
			theWindowPos, theWindowSize, theTargetClipRect);
		return;
	}

	// To prevent too many rounding errors, initially calculate everything
	// as if gUIScale has value 1.0, then apply gUIScale it in a later step.
	// Start with component size since it affects several other properties
	int aCompBaseSizeX = hotspotAnchorValue(hi.itemSize.x, theTargetSize.cx);
	int aCompBaseSizeY = hotspotAnchorValue(hi.itemSize.x, theTargetSize.cx);
	double aCompScalingSizeX = hi.itemSize.x.offset;
	double aCompScalingSizeY = hi.itemSize.y.offset;

	// Calculate window size needed based on type and component size
	double aWinBaseSizeX = aCompBaseSizeX;
	double aWinBaseSizeY = aCompBaseSizeY;
	double aWinScalingSizeX = aCompScalingSizeX;
	double aWinScalingSizeY = aCompScalingSizeY;
	u16 aMenuItemCount = 0;
	u16 aMenuItemXCount = 1;
	u16 aMenuItemYCount = 1;
	switch(hi.type)
	{
	case eMenuStyle_Slots:
		aWinScalingSizeX += hi.altLabelWidth;
		// fall through
	case eMenuStyle_List:
		aMenuItemYCount = aMenuItemCount = Menus::itemCount(aMenuID);
		theComponents.reserve(1 + aMenuItemCount);
		aWinBaseSizeY *= aMenuItemYCount;
		aWinScalingSizeY *= aMenuItemYCount;
		if( aMenuItemYCount > 1 )
			aWinScalingSizeY += hi.gapSizeY * (aMenuItemYCount - 1);
		aWinScalingSizeY += hi.titleHeight;
		break;
	case eMenuStyle_Bar:
		aMenuItemXCount = aMenuItemCount = Menus::itemCount(aMenuID);
		theComponents.reserve(1 + aMenuItemCount);
		aWinBaseSizeX *= aMenuItemXCount;
		aWinScalingSizeX *= aMenuItemXCount;
		if( aMenuItemXCount > 1 )
			aWinScalingSizeX += hi.gapSizeX * (aMenuItemXCount - 1);
		aWinScalingSizeY += hi.titleHeight;
		break;
	case eMenuStyle_Grid:
		aMenuItemCount = Menus::itemCount(aMenuID);
		aMenuItemXCount = Menus::gridWidth(aMenuID);
		aMenuItemYCount = Menus::gridHeight(aMenuID);
		theComponents.reserve(1 + aMenuItemCount);
		aWinBaseSizeX *= aMenuItemXCount;
		aWinBaseSizeY *= aMenuItemYCount;
		aWinScalingSizeX *= aMenuItemXCount;
		aWinScalingSizeY *= aMenuItemYCount;
		if( aMenuItemXCount > 1 )
			aWinScalingSizeX += hi.gapSizeX * (aMenuItemXCount-1);
		if( aMenuItemYCount > 1 )
			aWinScalingSizeY += hi.gapSizeY * (aMenuItemYCount-1);
		aWinScalingSizeY += hi.titleHeight;
		break;
	case eMenuStyle_4Dir:
		theComponents.reserve(1 + 4);
		aWinBaseSizeX *= 2;
		aWinBaseSizeY *= 3;
		aWinScalingSizeX = aWinScalingSizeX * 2 + hi.gapSizeX;
		aWinScalingSizeY = aWinScalingSizeY * 3 + hi.gapSizeY * 2;
		aWinScalingSizeY += hi.titleHeight;
		break;
	case eHUDType_HotspotGuide:
	case eHUDType_System:
		theComponents.reserve(2);
		aWinBaseSizeX = theTargetSize.cx;
		aWinBaseSizeY = theTargetSize.cy;
		aWinScalingSizeX = 0;
		aWinScalingSizeY = 0;
		break;
	default:
		theComponents.reserve(1);
		break;
	}

	// Get base window position (top-left corner) assuming top-left alignment
	double aWinBasePosX = hotspotAnchorValue(hi.position.x, theTargetSize.cx);
	double aWinBasePosY = hotspotAnchorValue(hi.position.y, theTargetSize.cy);
	double aWinScalingPosX = hi.position.x.offset;
	double aWinScalingPosY = hi.position.y.offset;

	// Offset by parent hotspot, if have one
	const Hotspot& aHotspot = parentHotspot(theHUDElementID);
	aWinBasePosX += hotspotAnchorValue(aHotspot.x, theTargetSize.cx);
	aWinBasePosY += hotspotAnchorValue(aHotspot.y, theTargetSize.cy);
	aWinScalingPosX += aHotspot.x.offset;
	aWinScalingPosY += aHotspot.y.offset;

	// Adjust position according to size and alignment settings
	switch(hi.alignmentX)
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
	switch(hi.alignmentY)
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
	int aWinSizeX = max(0.0, ceil(aWinBaseSizeX + aWinScalingSizeX));
	int aWinSizeY = max(0.0, ceil(aWinBaseSizeY + aWinScalingSizeY));
	int aWinPosX = floor(aWinBasePosX + aWinScalingPosX);
	int aWinPosY = floor(aWinBasePosY + aWinScalingPosY);

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

	// Calculate component rect's based on menu type
	theComponents.clear();
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
	theComponents.push_back(anItemRect);
	double aCenterX;

	switch(hi.type)
	{
	case eMenuStyle_Slots:
		if( hi.altLabelWidth )
		{
			if( hi.alignmentX == eAlignment_Max )
			{// Align components to right edge of window
				aCompTopLeft.x = aCompBotRight.x -
					ceil(aCompBaseSizeX + gUIScale * aCompScalingSizeX);
			}
			else
			{// Restrict components to left edge of window
				aCompBotRight.x = aCompTopLeft.x +
					ceil(aCompBaseSizeX + gUIScale * aCompScalingSizeX);
			}
		}
		// fall through
	case eMenuStyle_List:
	case eMenuStyle_Bar:
	case eMenuStyle_Grid:
		for(int y = 0; y < aMenuItemYCount; ++y)
		{
			anItemRect.top = max<LONG>(aCompTopLeft.y,
				aCompTopLeft.y + (aCompBaseSizeY * y) + gUIScale *
				(hi.titleHeight + y * (aCompScalingSizeY + hi.gapSizeY)));
			anItemRect.bottom = aCompBotRight.y;
			if( y < aMenuItemYCount - 1 )
			{
				anItemRect.bottom = aCompTopLeft.y +
					(aCompBaseSizeY * (y+1)) + gUIScale *
					(hi.titleHeight +
						aCompScalingSizeY * (y+1) + hi.gapSizeY * y);
			}
			for(int x = 0; x < aMenuItemXCount; ++x)
			{
				if( int(theComponents.size()) == aMenuItemCount + 1 )
					break;
				anItemRect.left = aCompTopLeft.x;
				anItemRect.right = aCompBotRight.x;
				if( x > 0 )
				{
					anItemRect.left += (aCompBaseSizeX * x) + gUIScale *
						(aCompScalingSizeX + hi.gapSizeX) * x;
				}
				if( x < aMenuItemXCount - 1 )
				{
					anItemRect.right = aCompTopLeft.x +
						(aCompBaseSizeX * (x+1)) + gUIScale *
						(aCompScalingSizeX * (x+1) + hi.gapSizeX * x);
				}
				theComponents.push_back(anItemRect);
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
				anItemRect.right = aCenterX - hi.gapSizeX * 0.5 * gUIScale;
				break;
			case eCmdDir_Right:
				anItemRect.left =  aCenterX + hi.gapSizeX * 0.5 * gUIScale;
				anItemRect.right = aCompBotRight.x;
				break;
			case eCmdDir_Up:
			case eCmdDir_Down:
				anItemRect.left = floor(aCenterX -
					(aCompBaseSizeX + gUIScale * aCompScalingSizeX) * 0.5);
				anItemRect.right = ceil(aCenterX +
					(aCompBaseSizeX + gUIScale * aCompScalingSizeX) * 0.5);
				break;
			}

			switch(itemIdx)
			{
			case eCmdDir_Up:
				anItemRect.top = max<LONG>(aCompTopLeft.y,
					aCompTopLeft.y + gUIScale * hi.titleHeight);
				anItemRect.bottom =
					aCompTopLeft.y + aCompBaseSizeY + gUIScale *
					(hi.titleHeight + aCompScalingSizeY);
				break;
			case eCmdDir_Left:
			case eCmdDir_Right:
				anItemRect.top =
					aCompTopLeft.y + aCompBaseSizeY + gUIScale *
					(hi.titleHeight + aCompScalingSizeY + hi.gapSizeY);
				anItemRect.bottom =
					aCompTopLeft.y + (aCompBaseSizeY * 2) + gUIScale *
					(hi.titleHeight +
						aCompScalingSizeY * 2 + hi.gapSizeY);
				break;
			case eCmdDir_Down:
				anItemRect.top =
					aCompTopLeft.y + aCompBaseSizeY * 2 + gUIScale *
					(hi.titleHeight + (aCompScalingSizeY + hi.gapSizeY) * 2);
				anItemRect.bottom = min<LONG>(aCompBotRight.y,
					aCompTopLeft.y + (aCompBaseSizeY * 3) + gUIScale *
					(hi.titleHeight +
						aCompScalingSizeY * 3 + hi.gapSizeY * 2));
				break;
			}
			theComponents.push_back(anItemRect);
		}
		break;
	case eHUDType_HotspotGuide:
	case eHUDType_System:
		// These may reference clipped window rect as Component 1
		theComponents.push_back(aWinClippedRect);
		break;
	}

	if( hi.type == eMenuStyle_Slots && hi.altLabelWidth )
	{// Add extra component for the alt label rect
		const u8 aBorderSize =
			sAppearances[hi.appearanceID[eAppearanceMode_Normal]].borderSize;
		const u8 aSelectedBorderSize =
			sAppearances[hi.appearanceID[eAppearanceMode_Selected]].borderSize;
		anItemRect.left = (hi.alignmentX == eAlignment_Max)
			? theComponents[0].left
			: theComponents[1].right - aBorderSize;
		anItemRect.right = (hi.alignmentX == eAlignment_Max)
			? theComponents[1].left + aBorderSize
			: theComponents[0].right;
		anItemRect.top = (hi.titleHeight > 0)
			? theComponents[1].top
			: theComponents[1].top + aSelectedBorderSize;
		anItemRect.bottom = (hi.titleHeight > 0)
			? theComponents[1].bottom - aSelectedBorderSize * 2
			: theComponents[1].bottom - aSelectedBorderSize;
		theComponents.push_back(anItemRect);
	}
#endif
}


void paintMainWindowContents(HWND theWindow, bool asDisabled)
{
#if 0
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
	if( LONG aFontHeight = getFontHeightToFit(
			hdc, aWStr, aRect, NULL, DT_SINGLELINE) )
	{// Need alternate font to actually fit string properly
		ncm.lfMessageFont.lfWidth = 0;
		ncm.lfMessageFont.lfHeight = aFontHeight;
		DeleteObject(SelectObject(hdc,
			CreateFontIndirect(&ncm.lfMessageFont)));
	}

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
#endif
}


void flashSystemOverlayBorder()
{
	//sSystemBorderFlashTimer = kSystemHUDFlashTime;
}


void setSystemOverlayDrawHook(SystemPaintFunc theFunc)
{
	//sSystemOverlayPaintFunc = theFunc;
	//if( sSystemHUDElementID < gFullRedrawHUD.size() )
	//	gFullRedrawHUD.set(sSystemHUDElementID);
}


void redrawSystemOverlay(bool fullRedraw)
{
	//if( fullRedraw )
	//{
	//	if( sSystemHUDElementID < gFullRedrawHUD.size() )
	//		gFullRedrawHUD.set(sSystemHUDElementID);
	//}
	//else
	//{
	//	if( sSystemHUDElementID < gRefreshHUD.size() )
	//		gRefreshHUD.set(sSystemHUDElementID);
	//}
}


MenuAlphaInfo getMenuAlphaInfo(int theMenuID)
{
	//u8 maxAlpha(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].maxAlpha;
	//}
	//
	//
	//u8 inactiveAlpha(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].inactiveAlpha;
	//}
	//
	//
	//int alphaFadeInDelay(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].fadeInDelay;
	//}
	//
	//
	//double alphaFadeInRate(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].fadeInRate;
	//}
	//
	//
	//int alphaFadeOutDelay(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].fadeOutDelay;
	//}
	//
	//
	//double alphaFadeOutRate(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].fadeOutRate;
	//}
	//
	//
	//int inactiveFadeOutDelay(int theHUDElementID)
	//{
	//	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//	return sHUDElementInfo[theHUDElementID].delayUntilInactive;
	//}
	return MenuAlphaInfo();
}


const std::vector<RECT>& menuLayoutComponents(
	int theMenuID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect)
{
	static std::vector<RECT> aDummyResult;
	return aDummyResult;
}


COLORREF transColor(int theMenuID)
{
	//DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//return sHUDElementInfo[theHUDElementID].transColor;
	return COLORREF();
}


int drawPriority(int theMenuID)
{
	//DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	//return sHUDElementInfo[theHUDElementID].drawPriority;
	return 0;
}


Hotspot parentHotspot(int theMenuID)
{
	Hotspot result;
#if 0
	DBG_ASSERT(size_t(theHUDElementID) < sHUDElementInfo.size());
	const HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	switch(hi.type)
	{
	case eHUDType_Hotspot: // TODO - merge with eMenuStyle_Visual if hotspotID > 0
		result = InputMap::getHotspot(hi.hotspotID);
		break;
	case eHUDType_KBCycleLast:
	case eHUDType_KBCycleDefault:
		if( const Hotspot* aHotspot =
				InputMap::KeyBindCycleHotspot(hi.kbArrayID, hi.selection) )
		{
			result = *aHotspot;
		}
		break;
	}
#endif
	return result;
}

} // WindowPainter
