//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "HotspotMap.h"
#include "InputMap.h"
#include "Lookup.h"
#include "Menus.h" // activeSubMenu(), itemCount()
#include "Profile.h"

namespace HUD
{

// Make the region of each HUD element obvious by drawing a frame
//#define DEBUG_DRAW_HUD_ELEMENT_FRAME
// Make the overall overlay region obvious by drawing a frame
//#define DEBUG_DRAW_OVERLAY_FRAME

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kMinFontPixelHeight = 6,
kNoticeStringDisplayTimePerChar = 50,
kNoticeStringMinTime = 3000,
};

const char* kMenuPrefix = "Menu";
const char* kHUDPrefix = "HUD";
const char* kBitmapsPrefix = "Bitmaps";
const char* kIconsPrefix = "Icons/";
const char* kAppVersionString = "Version: " __DATE__;
const char* kCopyIconRateKey = "System/CopyIconFrameTime";

enum EHUDProperty
{
	eHUDProp_Position,
	eHUDProp_ItemType,
	eHUDProp_ItemSize,
	eHUDProp_Size,
	eHUDProp_Alignment,
	eHUDProp_FontName,
	eHUDProp_FontSize,
	eHUDProp_FontWeight,
	eHUDProp_FontColor,
	eHUDProp_ItemColor,
	eHUDProp_BorderColor,
	eHUDProp_BorderSize,
	eHUDProp_GapSize,
	eHUDProp_TitleColor,
	eHUDProp_TransColor,
	eHUDProp_MaxAlpha,
	eHUDProp_FadeInDelay,
	eHUDProp_FadeInTime,
	eHUDProp_FadeOutDelay,
	eHUDProp_FadeOutTime,
	eHUDProp_InactiveDelay,
	eHUDProp_InactiveAlpha,
	eHUDProp_Bitmap,
	eHUDProp_Radius,
	eHUDProp_TitleHeight,
	eHUDProp_AltLabelWidth,
	eHUDProp_FlashTime,
	eHUDProp_Priority,

	eHUDProp_Num
};

enum EAlignment
{
	eAlignment_Min,		// L or T
	eAlignment_Center,	// CX or CY
	eAlignment_Max,		// R or B
};

enum EAppearanceMode
{
	eAppearanceMode_Normal,
	eAppearanceMode_Selected,
	eAppearanceMode_Flash,
	eAppearanceMode_FlashSelected,

	eAppearanceMode_Num
};

enum EMenuItemLabelType
{
	eMenuItemLabelType_Unknown,
	eMenuItemLabelType_String,
	eMenuItemLabelType_Bitmap,
	eMenuItemLabelType_CopyRect,
};

const char* kAppearancePrefix[] = { "", "Selected", "Flash", "FlashSelected" };
DBG_CTASSERT(ARRAYSIZE(kAppearancePrefix) == eAppearanceMode_Num);

const char* kHUDPropStr[] =
{
	"Position",			// eHUDProp_Position
	"ItemType",			// eHUDProp_ItemType
	"ItemSize",			// eHUDProp_ItemSize
	"Size",				// eHUDProp_Size
	"Alignment",		// eHUDProp_Alignment
	"Font",				// eHUDProp_FontName
	"FontSize",			// eHUDProp_FontSize
	"FontWeight",		// eHUDProp_FontWeight
	"LabelRGB",			// eHUDProp_FontColor
	"ItemRGB",			// eHUDProp_ItemColor
	"BorderRGB",		// eHUDProp_BorderColor
	"BorderSize",		// eHUDProp_BorderSize
	"GapSize",			// eHUDProp_GapSize
	"TitleRGB",			// eHUDProp_TitleColor
	"TransRGB",			// eHUDProp_TransColor
	"MaxAlpha",			// eHUDProp_MaxAlpha
	"FadeInDelay",		// eHUDProp_FadeInDelay
	"FadeInTime",		// eHUDProp_FadeInTime
	"FadeOutDelay",		// eHUDProp_FadeOutDelay
	"FadeOutTime",		// eHUDProp_FadeOutTime
	"InactiveDelay",	// eHUDProp_InactiveDelay
	"InactiveAlpha",	// eHUDProp_InactiveAlpha
	"Bitmap",			// eHUDProp_Bitmap
	"Radius",			// eHUDProp_Radius
	"TitleHeight",		// eHUDProp_TitleHeight
	"AltLabelWidth",	// eHUDProp_AltLabelWidth
	"FlashTime",		// eHUDProp_FlashTime
	"Priority",			// eHUDProp_Priority
};
DBG_CTASSERT(ARRAYSIZE(kHUDPropStr) == eHUDProp_Num);


//-----------------------------------------------------------------------------
// HUDElementInfo
//-----------------------------------------------------------------------------

struct HUDElementInfo
	: public ConstructFromZeroInitializedMemory<HUDElementInfo>
{
	EHUDType type;
	EHUDType itemType;
	COLORREF transColor;
	COLORREF titleColor;
	Hotspot position;
	Hotspot itemSize;
	float fadeInRate;
	float fadeOutRate;
	int fadeInDelay;
	int fadeOutDelay;
	int delayUntilInactive;
	u32 flashMaxTime;
	u32 flashStartTime;
	u16 appearanceID[eAppearanceMode_Num];
	union { u16 subMenuID; u16 arrayID; };
	u16 selection;
	u16 flashing;
	u16 prevFlashing;
	u16 fontID;
	u16 forcedRedrawItemID;
	u16 titleHeight;
	u16 altLabelWidth;
	s8 gapSizeX;
	s8 gapSizeY;
	u8 radius;
	u8 alignmentX;
	u8 alignmentY;
	u8 maxAlpha;
	u8 inactiveAlpha;
	s8 drawPriority;

	HUDElementInfo() :
		itemType(eHUDItemType_Rect),
		fadeInRate(255),
		fadeOutRate(255),
		flashing(kInvalidItem),
		prevFlashing(kInvalidItem),
		forcedRedrawItemID(kInvalidItem),
		maxAlpha(255),
		inactiveAlpha(255)
	{}
};


//-----------------------------------------------------------------------------
// Other Local Structures
//-----------------------------------------------------------------------------

struct HUDDrawData
{
	HDC hdc;
	HDC hTargetDC;
	SIZE targetSize;
	SIZE itemSize;
	SIZE destSize;
	RECT destRect;
	u16 hudElementID;
	EAppearanceMode appearanceMode;
	bool firstDraw;
};

struct Appearance
{
	COLORREF itemColor;
	COLORREF labelColor;
	COLORREF borderColor;
	u16 bitmapIconID;
	u16 borderPenID;
	u8 borderSize;
	u8 baseBorderSize;

	bool operator==(const Appearance& rhs) const
	{ return std::memcmp(this, &rhs, sizeof(Appearance)) == 0; }
};

struct BitmapIcon
{
	HBITMAP image;
	HBITMAP mask;
	SIZE size;
};

struct CopyIcon
{
	Hotspot pos;
	Hotspot size;
};

struct IconEntry
{
	u16 iconID;
	bool copyFromTarget;
};

struct BuildIconEntry
{
	HBITMAP srcFile; // 0 == copy from target window
	Hotspot pos;
	Hotspot size;
	IconEntry result;

	BuildIconEntry() : srcFile(), result() {}
};

struct CopyRectCacheEntry
{
	POINT fromPos;
	SIZE fromSize;
};

struct StringScaleCacheEntry
{
	u16 width, height, fontID;
};

struct MenuDrawCacheEntry
	: public ConstructFromZeroInitializedMemory<MenuDrawCacheEntry>
{
	EMenuItemLabelType type;
	union
	{
		u16 bitmapIconID;
		CopyRectCacheEntry copyRect;
		StringScaleCacheEntry str;
	};
};

struct AutoRefreshLabelEntry
{
	u16 hudElementID;
	u16 itemIdx;
};

struct HUDBuilder
{
	std::vector<std::string> parsedString;
	StringToValueMap<u16> fontInfoToFontIDMap;
	typedef std::pair< COLORREF, std::pair<int, int> > PenDef;
	VectorMap<PenDef, u16> penDefToPenMap;
	StringToValueMap<HBITMAP> bitmapNameToHandleMap;
	std::vector<BuildIconEntry> iconBuilders;

	~HUDBuilder() { DBG_ASSERT(bitmapNameToHandleMap.empty()); }
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<HFONT> sFonts;
static std::vector<HPEN> sPens;
static std::vector<Appearance> sAppearances;
static std::vector<BitmapIcon> sBitmapIcons;
static std::vector<CopyIcon> sCopyIcons;
static StringToValueMap<IconEntry> sLabelIcons;
static std::vector<HUDElementInfo> sHUDElementInfo;
static std::vector< std::vector<MenuDrawCacheEntry> > sMenuDrawCache;
static VectorMap<std::pair<u16, LONG>, u16> sResizedFontsMap;
static std::vector<AutoRefreshLabelEntry> sAutoRefreshLabels;
static std::wstring sErrorMessage;
static std::wstring sNoticeMessage;
static HDC sBitmapDrawSrc = NULL;
static int sErrorMessageTimer = 0;
static int sNoticeMessageTimer = 0;
static u32 sCopyIconUpdateRate = 100;
static u32 sNextAutoRefreshTime = 0;


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
	std::string aKey;

	if( theName.empty() )
		return result;

	// Try [Menu.Name] first since most HUD elements are likely Menus
	aKey = kMenuPrefix;
	aKey += "."; aKey += theName + "/";
	aKey += kAppearancePrefix[theAppearanceMode];
	aKey += kHUDPropStr[theProperty];
	result = Profile::getStr(aKey);
	if( !result.empty() )
		return result;

	// Try [HUD.Name] next
	aKey = kHUDPrefix;
	aKey += "."; aKey += theName + "/";
	aKey += kAppearancePrefix[theAppearanceMode];
	aKey += kHUDPropStr[theProperty];
	result = Profile::getStr(aKey);
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
	std::string aKey;

	// Try just [HUD] for a default value
	aKey = kHUDPrefix;
	aKey += "/";
	aKey += kAppearancePrefix[theAppearanceMode];
	aKey += kHUDPropStr[theProperty];
	result = Profile::getStr(aKey);
	if( !result.empty() )
		return result;

	// Maybe just [Menu] for default value?
	aKey = kMenuPrefix;
	aKey += "/";
	aKey += kAppearancePrefix[theAppearanceMode];
	aKey += kHUDPropStr[theProperty];
	result = Profile::getStr(aKey);
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


static void loadBitmapFile(
	HUDBuilder& theBuilder,
	const std::string& theBitmapName,
	std::string theBitmapPath)
{
	if( theBitmapPath.empty() )
		return;

	// Convert given path into a wstring absolute path
	theBitmapPath = removeExtension(removePathParams(theBitmapPath)) + ".bmp";

	if( !isAbsolutePath(theBitmapPath) )
	{
		WCHAR anAppPath[MAX_PATH];
		GetModuleFileName(NULL, anAppPath, MAX_PATH);
		theBitmapPath = getFileDir(narrow(anAppPath), true) + theBitmapPath;
	}
	const std::wstring aFilePathW = widen(theBitmapPath);

	const DWORD aFileAttributes = GetFileAttributes(aFilePathW.c_str());
	if( aFileAttributes == INVALID_FILE_ATTRIBUTES ||
		(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
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

	theBuilder.bitmapNameToHandleMap.setValue(
		theBitmapName, aBitmapHandle);
}


static inline POINT hotspotToPoint(
	const Hotspot& theHotspot,
	const SIZE& theTargetSize)
{
	POINT result;
	result.x =
		LONG(theHotspot.x.anchor) * theTargetSize.cx / 0x10000 +
		LONG(theHotspot.x.offset) +
		LONG(theHotspot.x.scaled * gUIScaleX);
	result.y =
		LONG(theHotspot.y.anchor) * theTargetSize.cy / 0x10000 +
		LONG(theHotspot.y.offset) +
		LONG(theHotspot.y.scaled * gUIScaleY);
	return result;
}


static inline SIZE hotspotToSize(
	const Hotspot& theHotspot,
	const SIZE& theTargetSize)
{
	POINT aPoint = hotspotToPoint(theHotspot, theTargetSize);
	SIZE result;
	result.cx = aPoint.x;
	result.cy = aPoint.y;
	return result;
}


static LONG hotspotUnscaledValue(
	Hotspot::Coord& theCoord,
	const LONG theMaxValue)
{
	return
		LONG(theCoord.anchor) * theMaxValue / 0x10000 +
		LONG(theCoord.offset) + LONG(theCoord.scaled);
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
		aPenDef, u16(sPens.size()));
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
		upper(theFontName) + "_" + theFontSize + "_" + theFontWeight;
	u16 result = theBuilder.fontInfoToFontIDMap.findOrAdd(
		aKeyStr, u16(sFonts.size()));
	if( result < sFonts.size() )
		return result;

	// Create new font
	const int aFontPointSize = (gUIScaleX + gUIScaleY) * 0.5 *
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
	return u16(sAppearances.size()-1);
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
		? theBuilder.bitmapNameToHandleMap.find(condense(theIconDescription))
		: theBuilder.bitmapNameToHandleMap.find(condense(aBitmapName));
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
			kBitmapsPrefix,
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


static void createBitmapIcon(BuildIconEntry& theBuildEntry)
{
	if( !theBuildEntry.srcFile || theBuildEntry.result.iconID > 0 )
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

	sBitmapIcons.push_back(aBitmapIcon);
	theBuildEntry.result.copyFromTarget = false;
	theBuildEntry.result.iconID = u16(sBitmapIcons.size()-1);
}


void generateBitmapIconMask(BitmapIcon& theIcon, COLORREF theTransColor)
{
	BITMAP bm;
	GetObject(theIcon.image, sizeof(BITMAP), &bm);
	theIcon.mask = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);

	HDC hdcImage = CreateCompatibleDC(NULL);
	HDC hdcMask = CreateCompatibleDC(NULL);
	SelectObject(hdcImage, theIcon.image);
	SelectObject(hdcMask, theIcon.mask);

	SetBkColor(hdcImage, theTransColor);
	BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcImage, 0, 0, SRCCOPY);
	BitBlt(hdcImage, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, SRCINVERT);

	DeleteDC(hdcImage);
	DeleteDC(hdcMask);
}


static void createCopyIcon(BuildIconEntry& theBuildEntry)
{
	if( theBuildEntry.srcFile || theBuildEntry.result.iconID > 0 )
		return;

	CopyIcon aCopyIcon = CopyIcon();
	aCopyIcon.pos = theBuildEntry.pos;
	aCopyIcon.size = theBuildEntry.size;
	sCopyIcons.push_back(aCopyIcon);
	theBuildEntry.result.copyFromTarget = true;
	theBuildEntry.result.iconID = u16(sCopyIcons.size()-1);
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
	if( aBuildEntry.result.iconID > 0 )
		return aBuildEntry.result.iconID;
	createBitmapIcon(aBuildEntry);
	return aBuildEntry.result.iconID;
}


static void createLabelIcon(
	HUDBuilder& theBuilder,
	const std::string& theTextLabel,
	const std::string& theIconDescription)
{
	size_t anIconBuildID = getOrCreateBuildIconEntry(
		theBuilder, theIconDescription, true);
	if( anIconBuildID == 0 )
		return;
	BuildIconEntry& aBuildEntry = theBuilder.iconBuilders[anIconBuildID];
	if( aBuildEntry.result.iconID > 0 )
	{
		sLabelIcons.setValue(theTextLabel, aBuildEntry.result);
		return;
	}
	if( aBuildEntry.srcFile )
	{
		createBitmapIcon(aBuildEntry);
		sLabelIcons.setValue(theTextLabel, aBuildEntry.result);
		return;
	}
	createCopyIcon(aBuildEntry);
	sLabelIcons.setValue(theTextLabel, aBuildEntry.result);	
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

	int aRadius = hi.radius;
	aRadius = min(aRadius, (theRect.right-theRect.left) * 3 / 4);
	aRadius = min(aRadius, (theRect.bottom-theRect.top) * 3 / 4);

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


static void drawHUDArrowL(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

	POINT points[3];
	points[0].x = theRect.right;
	points[0].y = theRect.top;
	points[1].x = theRect.right;
	points[1].y = theRect.bottom;
	points[2].x = theRect.left;
	points[2].y = (theRect.top + theRect.bottom) / 2;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDArrowR(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.top;
	points[1].x = theRect.left;
	points[1].y = theRect.bottom;
	points[2].x = theRect.right;
	points[2].y = (theRect.top + theRect.bottom) / 2;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDArrowU(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.bottom;
	points[1].x = theRect.right;
	points[1].y = theRect.bottom;
	points[2].x = (theRect.left + theRect.right) / 2;
	points[2].y = theRect.top;
	Polygon(dd.hdc, points, 3);
}


static void drawHUDArrowD(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[dd.appearanceMode]];
	SelectObject(dd.hdc, sPens[appearance.borderPenID]);
	SetDCBrushColor(dd.hdc, appearance.itemColor);

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
	case eHUDItemType_Rect:		drawHUDRect(dd, theRect);		break;
	case eHUDItemType_RndRect:	drawHUDRndRect(dd, theRect);	break;
	case eHUDItemType_Bitmap:	drawHUDBitmap(dd, theRect);		break;
	case eHUDItemType_Circle:	drawHUDCircle(dd, theRect);		break;
	case eHUDItemType_ArrowL:	drawHUDArrowL(dd, theRect);		break;
	case eHUDItemType_ArrowR:	drawHUDArrowR(dd, theRect);		break;
	case eHUDItemType_ArrowU:	drawHUDArrowU(dd, theRect);		break;
	case eHUDItemType_ArrowD:	drawHUDArrowD(dd, theRect);		break;
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
			u16(sFonts.size()));
		if( theCacheEntry.fontID == sFonts.size() )
		{
			LOGFONT aFont;
			GetObject(sFonts[hi.fontID], sizeof(LOGFONT), &aFont);
			aFont.lfWidth = 0;
			aFont.lfHeight = aFontHeight;
			sFonts.push_back(CreateFontIndirect(&aFont));
		}
	}

	theCacheEntry.width = aNeededRect.right - aNeededRect.left;
	theCacheEntry.height = aNeededRect.bottom - aNeededRect.top;
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

	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];

	// Initialize cache entry to get font & height
	if( theCacheEntry.width == 0 )
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
	const RECT& theRect,
	u16 theItemIdx,
	const std::string& theLabel,
	const Appearance& theAppearance,
	MenuDrawCacheEntry& theCacheEntry)	
{
	// Remove auto-refresh draw entry if have one now that are being drawn
	for(std::vector<AutoRefreshLabelEntry>::iterator itr =
		sAutoRefreshLabels.begin(); itr != sAutoRefreshLabels.end(); ++itr )
	{
		if( itr->hudElementID == dd.hudElementID &&
			itr->itemIdx == theItemIdx )
		{
			sAutoRefreshLabels.erase(itr);
			break;
		}
	}

	if( theCacheEntry.type == eMenuItemLabelType_Unknown )
	{// Initialize cache entry
		IconEntry* anIconEntry = sLabelIcons.find(condense(theLabel));
		if( anIconEntry && anIconEntry->copyFromTarget )
		{
			theCacheEntry.type = eMenuItemLabelType_CopyRect;
			theCacheEntry.copyRect.fromPos = hotspotToPoint(
				sCopyIcons[anIconEntry->iconID].pos,
				dd.targetSize);
			theCacheEntry.copyRect.fromSize = hotspotToSize(
				sCopyIcons[anIconEntry->iconID].size,
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
		drawLabelString(dd, theRect, widen(theLabel),
			DT_WORDBREAK | DT_CENTER | DT_VCENTER,
			theCacheEntry.str);
	}
	else if( theCacheEntry.type == eMenuItemLabelType_Bitmap )
	{
		DBG_ASSERT(theCacheEntry.bitmapIconID < sBitmapIcons.size());
		const BitmapIcon& anIcon = sBitmapIcons[theCacheEntry.bitmapIconID];
		DBG_ASSERT(sBitmapDrawSrc);
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
	}
	else if( theCacheEntry.type == eMenuItemLabelType_CopyRect )
	{
		int aDstL = theRect.left;
		int aDstT = theRect.top;
		int aDstW = theRect.right - theRect.left;
		int aDstH = theRect.bottom - theRect.top;
		int aSrcL = theCacheEntry.copyRect.fromPos.x;
		int aSrcT = theCacheEntry.copyRect.fromPos.y;
		int aSrcW = theCacheEntry.copyRect.fromSize.cx;
		int aSrcH = theCacheEntry.copyRect.fromSize.cy;

		if( aDstW >= aSrcW && aDstH >= aSrcH )
		{// Just draw centered at destination
			BitBlt(dd.hdc,
				aDstL + (aDstW - aSrcW) / 2,
				aDstT + (aDstH - aSrcH) / 2,
				aSrcW, aSrcH,
				dd.hTargetDC, aSrcL, aSrcT, SRCCOPY);
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
				dd.hTargetDC, aSrcL, aSrcT, aSrcW, aSrcH, SRCCOPY);
		}
		// Queue auto-refresh of this label later if nothing else draws it
		sAutoRefreshLabels.push_back(AutoRefreshLabelEntry());
		sAutoRefreshLabels.back().hudElementID = dd.hudElementID;
		sAutoRefreshLabels.back().itemIdx = theItemIdx;
	}
}


static void drawMenuTitle(
	HUDDrawData& dd,
	u16 theSubMenuID,
	StringScaleCacheEntry& theCacheEntry)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const Appearance& appearance = sAppearances[
		hi.appearanceID[eAppearanceMode_Normal]];
	RECT aTitleRect = {
		hi.radius / 2, 0,
		dd.destSize.cx - hi.radius / 2,
		hi.titleHeight };

	EAlignment alignment = EAlignment(hi.alignmentX);
	if( hi.itemType != eHUDItemType_Rect )
		alignment = eAlignment_Center;

	if( !dd.firstDraw )
		eraseRect(dd, aTitleRect);
	InflateRect(&aTitleRect, -2, -2);
	const std::wstring& aStr = widen(InputMap::menuLabel(theSubMenuID));
	UINT aFormat = DT_WORDBREAK | DT_BOTTOM;
	switch(alignment)
	{
	case eAlignment_Min: aFormat |= DT_LEFT; break;
	case eAlignment_Center: aFormat |= DT_CENTER; break;
	case eAlignment_Max: aFormat |= DT_RIGHT; break;
	}
	if( theCacheEntry.width == 0 )
		initStringCacheEntry(dd, aTitleRect, aStr, aFormat, theCacheEntry);

	// Fill in 2px margin around text with titleBG (border) color
	RECT aBGRect;
	LONG aSize = theCacheEntry.width + 4;
	switch(alignment)
	{
	case eAlignment_Min:
		aBGRect.left = 0;
		break;
	case eAlignment_Center:
		aBGRect.left = ((dd.destSize.cx - aSize) / 2);
		break;
	case eAlignment_Max:
		aBGRect.left = dd.destSize.cx - aSize;
		break;
	}
	aBGRect.right = aBGRect.left + aSize;
	aSize = theCacheEntry.height + 4;
	aBGRect.bottom = hi.titleHeight;
	aBGRect.top = aBGRect.bottom - aSize;
	COLORREF oldColor = SetDCBrushColor(dd.hdc, appearance.borderColor);
	FillRect(dd.hdc, &aBGRect, (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH));
	SetDCBrushColor(dd.hdc, oldColor);

	SetTextColor(dd.hdc, hi.titleColor);
	SetBkColor(dd.hdc, appearance.borderColor);
	drawLabelString(dd, aTitleRect, aStr, aFormat, theCacheEntry);	
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
	int aMaxBorderSize = hi.radius / 4;
	for(int i = 0; i < eAppearanceMode_Num; ++i)
	{
		aMaxBorderSize = max(aMaxBorderSize,
			sAppearances[hi.appearanceID[i]].borderSize);
	}
	InflateRect(&aLabelRect, -aMaxBorderSize - 1, -aMaxBorderSize - 1);
	drawMenuItemLabel(
		dd, aLabelRect,
		theItemIdx,
		theLabel,
		appearance,
		theCacheEntry);

	// Flag when successfully redrew forced-redraw item
	if( hi.forcedRedrawItemID == theItemIdx )
		hi.forcedRedrawItemID = kInvalidItem;
}


static void drawListMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aPrevSelection = hi.selection;
	hi.selection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(hi.selection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[hi.subMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd, hi.subMenuID,
			sMenuDrawCache[hi.subMenuID][0].str);
	}

	const bool flashingChanged = hi.flashing != hi.prevFlashing;
	const bool selectionChanged = hi.selection != aPrevSelection;
	const bool shouldRedrawAll =
		dd.firstDraw ||
		(hi.gapSizeY < 0 && (flashingChanged || selectionChanged));

	RECT anItemRect = { 0 };
	RECT aSelectedItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight;
	anItemRect.bottom = hi.titleHeight + dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( shouldRedrawAll ||
			hi.forcedRedrawItemID == itemIdx ||
			(selectionChanged &&
				(itemIdx == aPrevSelection || itemIdx == hi.selection)) ||
			(flashingChanged &&
				(itemIdx == hi.prevFlashing || itemIdx == hi.flashing)) )
		{
			if( itemIdx == hi.selection && hi.gapSizeY < 0 )
			{// Make sure selection is drawn on top of other items
				aSelectedItemRect = anItemRect;
			}
			else
			{
				drawMenuItem(dd, anItemRect, itemIdx,
					InputMap::menuItemLabel(hi.subMenuID, itemIdx),
					sMenuDrawCache[hi.subMenuID][itemIdx + hasTitle]);
			}
		}
		anItemRect.top = anItemRect.bottom + hi.gapSizeY;
		anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	}

	// Draw selected menu item last
	if( aSelectedItemRect.right > aSelectedItemRect.left )
	{
		drawMenuItem(dd, aSelectedItemRect, hi.selection,
			InputMap::menuItemLabel(hi.subMenuID, hi.selection),
			sMenuDrawCache[hi.subMenuID][hi.selection + hasTitle]);
	}

	hi.prevFlashing = hi.flashing;
}


static void drawSlotsMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aPrevSelection = hi.selection;
	hi.selection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(hi.selection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[hi.subMenuID].resize(
		anItemCount + hasTitle + (hi.altLabelWidth ? anItemCount : 0));

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd, hi.subMenuID,
			sMenuDrawCache[hi.subMenuID][0].str);
	}

	const bool flashingChanged = hi.flashing != hi.prevFlashing;
	const bool selectionChanged = hi.selection != aPrevSelection;
	const bool shouldRedrawAll = dd.firstDraw || selectionChanged;

	// Draw alternate label for selected item off to the side
	if( hi.altLabelWidth > 0 &&
		(selectionChanged || shouldRedrawAll ||
		 hi.forcedRedrawItemID == hi.selection) )
	{
		const u8 aBorderSize = 
			sAppearances[hi.appearanceID[eAppearanceMode_Normal]].borderSize;
		const u8 aSelectedBorderSize = 
			sAppearances[hi.appearanceID[eAppearanceMode_Selected]].borderSize;
		RECT anAltLabelRect = { 0 };
		anAltLabelRect.left = 0;
		if( hi.alignmentX != eAlignment_Max )
			anAltLabelRect.left = dd.itemSize.cx - aBorderSize;
		anAltLabelRect.right =
			anAltLabelRect.left + hi.altLabelWidth + aBorderSize;
		anAltLabelRect.top =
			hi.titleHeight ? hi.titleHeight : aSelectedBorderSize;
		anAltLabelRect.bottom = anAltLabelRect.top +
			dd.itemSize.cy - aSelectedBorderSize * 2;
		const std::string& anAltLabel =
			InputMap::menuItemAltLabel(hi.subMenuID, hi.selection);
		const std::string& aPrevAltLabel =
			InputMap::menuItemAltLabel(hi.subMenuID, aPrevSelection);
		if( anAltLabel.empty() && !aPrevAltLabel.empty() )
			eraseRect(dd, anAltLabelRect);
		if( !anAltLabel.empty() )
		{
			dd.appearanceMode = eAppearanceMode_Normal;
			drawHUDRect(dd, anAltLabelRect);
			InflateRect(&anAltLabelRect, -aBorderSize-1, -aBorderSize-1);
			drawMenuItemLabel(
				dd, anAltLabelRect,
				hi.selection,
				anAltLabel,
				sAppearances[hi.appearanceID[eAppearanceMode_Normal]],
				sMenuDrawCache[hi.subMenuID]
					[anItemCount+hi.selection+hasTitle]);
		}
	}

	// Make sure only flash top slot even if selection changes during flash
	if( hi.flashing != kInvalidItem )
		hi.flashing = hi.selection;

	// Draw in a wrapping fashion, starting with hi.selection+1 being drawn
	// just below the top slot, and ending when draw hi.selection last at top
	RECT anItemRect = { 0 };
	if( hi.alignmentX == eAlignment_Max )
		anItemRect.left = dd.destSize.cx - dd.itemSize.cx;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.top = hi.titleHeight + dd.itemSize.cy + hi.gapSizeY;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	for(u16 itemIdx = (hi.selection + 1) % anItemCount;
		true; itemIdx = (itemIdx + 1) % anItemCount)
	{
		const bool isSelection = itemIdx == hi.selection;
		if( isSelection )
		{
			anItemRect.top = hi.titleHeight;
			anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
		}
		if( shouldRedrawAll ||
			hi.forcedRedrawItemID == itemIdx ||
			(isSelection && flashingChanged) )
		{
			drawMenuItem(dd, anItemRect, itemIdx,
				InputMap::menuItemLabel(hi.subMenuID, itemIdx),
				sMenuDrawCache[hi.subMenuID][itemIdx + hasTitle]);
		}
		if( isSelection )
			break;
		anItemRect.top = anItemRect.bottom + hi.gapSizeY;
		anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	}

	hi.prevFlashing = hi.flashing;
}


static void drawBarMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aPrevSelection = hi.selection;
	hi.selection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(hi.selection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[hi.subMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd, hi.subMenuID,
			sMenuDrawCache[hi.subMenuID][0].str);
	}

	const bool flashingChanged = hi.flashing != hi.prevFlashing;
	const bool selectionChanged = hi.selection != aPrevSelection;
	const bool shouldRedrawAll =
		dd.firstDraw ||
		(hi.gapSizeX < 0 && (flashingChanged || selectionChanged));

	RECT anItemRect = { 0 };
	RECT aSelectedItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight;
	anItemRect.bottom = hi.titleHeight + dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( shouldRedrawAll ||
			hi.forcedRedrawItemID == itemIdx ||
			(selectionChanged &&
				(itemIdx == aPrevSelection || itemIdx == hi.selection)) ||
			(flashingChanged &&
				(itemIdx == hi.prevFlashing || itemIdx == hi.flashing)) )
		{
			if( itemIdx == hi.selection && hi.gapSizeX < 0 )
			{// Make sure selection is drawn on top of other items
				aSelectedItemRect = anItemRect;
			}
			else
			{
				drawMenuItem(dd, anItemRect, itemIdx,
					InputMap::menuItemLabel(hi.subMenuID, itemIdx),
					sMenuDrawCache[hi.subMenuID][itemIdx + hasTitle]);
			}
		}
		anItemRect.left = anItemRect.right + hi.gapSizeX;
		anItemRect.right = anItemRect.left + dd.itemSize.cx;
	}

	// Draw selected menu item last
	if( aSelectedItemRect.right > aSelectedItemRect.left )
	{
		drawMenuItem(dd, aSelectedItemRect, hi.selection,
			InputMap::menuItemLabel(hi.subMenuID, hi.selection),
			sMenuDrawCache[hi.subMenuID][hi.selection + hasTitle]);
	}

	hi.prevFlashing = hi.flashing;
}


static void draw4DirMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	hi.selection = kInvalidItem;
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[hi.subMenuID].resize(eCmdDir_Num + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd,
			hi.subMenuID,
			sMenuDrawCache[hi.subMenuID][0].str);
	}

	RECT anItemRect = { 0 };
	for(u16 itemIdx = 0; itemIdx < eCmdDir_Num; ++itemIdx)
	{
		const ECommandDir aDir = ECommandDir(itemIdx);
		if( dd.firstDraw ||
			hi.forcedRedrawItemID == itemIdx ||
			(hi.flashing != hi.prevFlashing &&
				(itemIdx == hi.prevFlashing || itemIdx == hi.flashing)) )
		{
			switch(itemIdx)
			{
			case eCmdDir_Left:
				anItemRect.left = 0;
				anItemRect.top = hi.titleHeight + dd.itemSize.cy + hi.gapSizeY;
				break;
			case eCmdDir_Right:
				anItemRect.left = dd.itemSize.cx + hi.gapSizeX;
				anItemRect.top = hi.titleHeight + dd.itemSize.cy + hi.gapSizeY;
				break;
			case eCmdDir_Up:
				anItemRect.left = dd.itemSize.cx / 2 + hi.gapSizeX / 2;
				anItemRect.top = hi.titleHeight;
				break;
			case eCmdDir_Down:
				anItemRect.left = dd.itemSize.cx / 2 + hi.gapSizeX / 2;
				anItemRect.top =
					hi.titleHeight + dd.itemSize.cy * 2 + hi.gapSizeY * 2;
				break;
			}
			anItemRect.right = anItemRect.left + dd.itemSize.cx;
			anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
			drawMenuItem(dd, anItemRect, itemIdx,
				InputMap::menuDirLabel(hi.subMenuID, aDir),
				sMenuDrawCache[hi.subMenuID][aDir + hasTitle]);
		}
	}

	hi.prevFlashing = hi.flashing;
}


static void drawGridMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aPrevSelection = hi.selection;
	hi.selection = Menus::selectedItem(aMenuID);
	const u16 aGridWidth = Menus::gridWidth(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(hi.selection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[hi.subMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd, hi.subMenuID,
			sMenuDrawCache[hi.subMenuID][0].str);
	}

	const bool flashingChanged = hi.flashing != hi.prevFlashing;
	const bool selectionChanged = hi.selection != aPrevSelection;
	const bool shouldRedrawAll =
		dd.firstDraw ||
		((hi.gapSizeX < 0 || hi.gapSizeY < 0) &&
			(flashingChanged || selectionChanged));

	RECT anItemRect = { 0 };
	RECT aSelectedItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight;
	anItemRect.bottom = hi.titleHeight + dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( shouldRedrawAll ||
			hi.forcedRedrawItemID == itemIdx ||
			(selectionChanged &&
				(itemIdx == aPrevSelection || itemIdx == hi.selection)) ||
			(flashingChanged &&
				(itemIdx == hi.prevFlashing || itemIdx == hi.flashing)) )
		{
			if( itemIdx == hi.selection &&
				(hi.gapSizeX < 0 || hi.gapSizeY < 0) )
			{// Make sure selection is drawn on top of other items
				aSelectedItemRect = anItemRect;
			}
			else
			{
				drawMenuItem(dd, anItemRect, itemIdx,
					InputMap::menuItemLabel(hi.subMenuID, itemIdx),
					sMenuDrawCache[hi.subMenuID][itemIdx + hasTitle]);
			}
		}
		if( itemIdx % aGridWidth == aGridWidth - 1 )
		{// Next menu item is left edge and one down
			anItemRect.left = 0;
			anItemRect.right = dd.itemSize.cx;
			anItemRect.top = anItemRect.bottom + hi.gapSizeY;
			anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
		}
		else
		{// Next menu item is to the right
			anItemRect.left = anItemRect.right + hi.gapSizeX;
			anItemRect.right = anItemRect.left + dd.itemSize.cx;
		}
	}

	// Draw selected menu item last
	if( aSelectedItemRect.right > aSelectedItemRect.left )
	{
		drawMenuItem(dd, aSelectedItemRect, hi.selection,
			InputMap::menuItemLabel(hi.subMenuID, hi.selection),
			sMenuDrawCache[hi.subMenuID][hi.selection + hasTitle]);
	}

	hi.prevFlashing = hi.flashing;
}


static void drawBasicHUD(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];

	if( !dd.firstDraw )
		eraseRect(dd, dd.destRect);

	drawHUDItem(dd, dd.destRect);
}


static void drawSystemHUD(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	DBG_ASSERT(hi.type == eHUDType_System);

	// Erase any previous strings
	if( !dd.firstDraw )
		eraseRect(dd, dd.destRect);

	RECT aTextRect = dd.destRect;
	InflateRect(&aTextRect, -8, -8);

	if( !sErrorMessage.empty() )
	{
		SetBkColor(dd.hdc, RGB(255, 0, 0));
		SetTextColor(dd.hdc, RGB(255, 255, 0));
		DrawText(dd.hdc, sErrorMessage.c_str(), -1, &aTextRect,
			DT_WORDBREAK);
	}

	if( !sNoticeMessage.empty() )
	{
		SetBkColor(dd.hdc, RGB(0, 0, 255));
		SetTextColor(dd.hdc, RGB(255, 255, 255));
		DrawText(dd.hdc, sNoticeMessage.c_str(), -1, &aTextRect,
			DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	cleanup();

	HUDBuilder aHUDBuilder;
	sHUDElementInfo.resize(InputMap::hudElementCount());
	sMenuDrawCache.resize(InputMap::menuCount());
	sCopyIconUpdateRate =
		Profile::getInt(kCopyIconRateKey, sCopyIconUpdateRate);

	// Add dummy 0th entries to some vectors
	aHUDBuilder.bitmapNameToHandleMap.setValue("", NULL);
	aHUDBuilder.iconBuilders.push_back(BuildIconEntry());
	sBitmapIcons.push_back(BitmapIcon());
	sCopyIcons.push_back(CopyIcon());

	// Generate default appearances as entries 0 to eAppearanceMode_Num
	sAppearances.resize(eAppearanceMode_Num);
	const COLORREF aDefaultTransColor = strToRGB(aHUDBuilder,
					getDefaultHUDPropStr(eHUDProp_TransColor));
	for(u32 i = 0; i < eAppearanceMode_Num; ++i)
	{
		sAppearances[i].itemColor = strToRGB(aHUDBuilder,
			getDefaultHUDPropStr(eHUDProp_ItemColor, i));
		sAppearances[i].labelColor = strToRGB(aHUDBuilder,
			getDefaultHUDPropStr(eHUDProp_FontColor, i));
		sAppearances[i].borderColor = strToRGB(aHUDBuilder,
			getDefaultHUDPropStr(eHUDProp_BorderColor, i));
		sAppearances[i].baseBorderSize = u32FromString(
			getDefaultHUDPropStr(eHUDProp_BorderSize, i));
		sAppearances[i].bitmapIconID = getOrCreateBitmapIconID(
			aHUDBuilder, getDefaultHUDPropStr(eHUDProp_Bitmap, i));
	}

	// Load bitmap files
	Profile::KeyValuePairs aKeyValueList;
	Profile::getAllKeys(std::string(kBitmapsPrefix) + "/", aKeyValueList);
	for(size_t i = 0; i < aKeyValueList.size(); ++i)
	{
		std::string aBitmapName = aKeyValueList[i].first;
		std::string aBitmapPath = aKeyValueList[i].second;
		loadBitmapFile(aHUDBuilder, aBitmapName, aBitmapPath);
	}

	// Generate text label to icon label map
	aKeyValueList.clear();
	Profile::getAllKeys(kIconsPrefix, aKeyValueList);
	for(size_t i = 0; i < aKeyValueList.size(); ++i)
	{
		std::string aTextLabel = aKeyValueList[i].first;
		std::string anIconDesc = aKeyValueList[i].second;
		createLabelIcon(aHUDBuilder, aTextLabel, anIconDesc);
	}

	// Get information for each HUD Element from Profile
	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
		hi.type = InputMap::hudElementType(aHUDElementID);
		hi.transColor = aDefaultTransColor;
		for(u16 i = 0; i < eAppearanceMode_Num; ++i)
			hi.appearanceID[i] = i;
		if( hi.type == eHUDType_System )
		{
			hi.drawPriority = 127; // Higher than any can be manually set to
			continue;
		}
		const std::string& aHUDName =
			InputMap::hudElementKeyName(aHUDElementID);
		std::string aStr;
		u32 aVal;
		// hi.itemType = eHUDProp_ItemType
		if( hi.type >= eHUDItemType_Begin && hi.type < eHUDItemType_End )
		{
			hi.itemType = hi.type;
		}
		else
		{
			aStr = getHUDPropStr(aHUDName, eHUDProp_ItemType);
			if( !aStr.empty() )
				hi.itemType = hudTypeNameToID(upper(aStr));
			if( hi.itemType < eHUDItemType_Begin ||
				hi.itemType >= eHUDItemType_End )
			{
				logError("Invalid ItemType (%s) for HUD Element %s! "
					"Defaulting to 'Rectangle'!",
					aStr.c_str(), aHUDName.c_str());
				hi.itemType = eHUDItemType_Rect;
			}
		}
		// hi.position = eHUDProp_Position
		HotspotMap::stringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Position),
			hi.position);
		// hi.itemSize = eHUDProp_ItemSize (Menus) or eHUDProp_Size (HUD)
		const bool isAMenu =
			hi.type >= eMenuStyle_Begin && hi.type < eMenuStyle_End;
		if( isAMenu )
			aStr = getNamedHUDPropStr(aHUDName, eHUDProp_ItemSize);
		else
			aStr = getNamedHUDPropStr(aHUDName, eHUDProp_Size);
		if( aStr.empty() && isAMenu )
			aStr = getNamedHUDPropStr(aHUDName, eHUDProp_Size);
		if( aStr.empty() )
			aStr = getHUDPropStr(aHUDName, eHUDProp_ItemSize);
		HotspotMap::stringToHotspot(aStr, hi.itemSize);
		// hi.alignmentX/Y = eHUDProp_Alignment
		Hotspot aTempHotspot;
		HotspotMap::stringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Alignment),
			aTempHotspot);
		hi.alignmentX =
			aTempHotspot.x.anchor < 0x4000	? eAlignment_Min :
			aTempHotspot.x.anchor > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		hi.alignmentY =
			aTempHotspot.y.anchor < 0x4000	? eAlignment_Min :
			aTempHotspot.y.anchor > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		// hi.transColor = eHUDProp_TransColor
		hi.transColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_TransColor));
		// hi.titleColor = eHUDProp_TitleColor
		hi.titleColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_TitleColor));
		// hi.maxAlpha = eHUDProp_MaxAlpha
		hi.maxAlpha = u8(u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_MaxAlpha)) & 0xFF);
		// hi.fadeInDelay = eHUDProp_FadeInDelay
		hi.fadeInDelay = max(0, intFromString(
			getHUDPropStr(aHUDName, eHUDProp_FadeInDelay)));
		// hi.fadeInRate = eHUDProp_FadeInTime
		aVal = max(1, u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_FadeInTime)));
		hi.fadeInRate = float(hi.maxAlpha) / float(aVal);
		// hi.fadeOutDelay = eHUDProp_FadeOutDelay
		hi.fadeOutDelay = max(0, intFromString(
			getHUDPropStr(aHUDName, eHUDProp_FadeOutDelay)));
		// hi.fadeOutRate = eHUDProp_FadeOutTime
		aVal = max(1, u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_FadeOutTime)));
		hi.fadeOutRate = float(hi.maxAlpha) / float(aVal);
		// hi.delayUntilInactive = eHUDProp_InactiveDelay
		hi.delayUntilInactive = intFromString(
			getHUDPropStr(aHUDName, eHUDProp_InactiveDelay));
		// hi.inactiveAlpha = eHUDProp_InactiveAlpha
		hi.inactiveAlpha = u8(u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_InactiveAlpha)) & 0xFF);
		// hi.flashMaxTime = eHUDProp_FlashTime
		hi.flashMaxTime = u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_FlashTime));
		// hi.drawPriority = eHUDProp_Priority
		hi.drawPriority = clamp(intFromString(
			getNamedHUDPropStr(aHUDName, eHUDProp_Priority)),
			-100, 100);
		
		// Generate custom appearances if have any custom properties
		for(u32 i = 0; i < eAppearanceMode_Num; ++i)
		{
			Appearance anAppearance = Appearance();
			anAppearance.itemColor = strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_ItemColor, i));
			anAppearance.labelColor = strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_FontColor, i));
			anAppearance.borderColor = strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BorderColor, i));
			anAppearance.baseBorderSize = u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_BorderSize, i));
			if( hi.type == eHUDItemType_Bitmap ||
				hi.itemType == eHUDItemType_Bitmap )
			{
				anAppearance.bitmapIconID = getOrCreateBitmapIconID(
					aHUDBuilder, getHUDPropStr(aHUDName, eHUDProp_Bitmap, i));
			}
			hi.appearanceID[i] = getOrCreateAppearanceID(anAppearance);
		}

		if( hi.type == eHUDType_KBArrayLast ||
			hi.type == eHUDType_KBArrayDefault )
		{
			hi.arrayID = InputMap::keyBindArrayForHUDElement(aHUDElementID);
			DBG_ASSERT(hi.arrayID < InputMap::keyBindArrayCount());
		}
	}

	updateScaling();

	// Free loaded bitmap files now that have copied them to local versions
	for(size_t i = 0; i < aHUDBuilder.bitmapNameToHandleMap.size(); ++i)
		DeleteObject(aHUDBuilder.bitmapNameToHandleMap.values()[i]);
	aHUDBuilder.bitmapNameToHandleMap.clear();

	// Trim unused memory
	if( sFonts.size() < sFonts.capacity() )
		std::vector<HFONT>(sFonts).swap(sFonts);
	if( sPens.size() < sPens.capacity() )
		std::vector<HPEN>(sPens).swap(sPens);
	if( sAppearances.size() < sAppearances.capacity() )
		std::vector<Appearance>(sAppearances).swap(sAppearances);
	if( sBitmapIcons.size() < sBitmapIcons.capacity() )
		std::vector<BitmapIcon>(sBitmapIcons).swap(sBitmapIcons);
	if( sCopyIcons.size() < sCopyIcons.capacity() )
		std::vector<CopyIcon>(sCopyIcons).swap(sCopyIcons);
	sLabelIcons.trim();
	if( sHUDElementInfo.size() < sHUDElementInfo.capacity() )
		std::vector<HUDElementInfo>(sHUDElementInfo).swap(sHUDElementInfo);
}


void cleanup()
{
	for(size_t i = 0; i < sFonts.size(); ++i)
		DeleteObject(sFonts[i]);
	for(size_t i = 0; i < sPens.size(); ++i)
		DeleteObject(sPens[i]);
	for(size_t i = 0; i < sBitmapIcons.size(); ++i)
	{
		DeleteObject(sBitmapIcons[i].image);
		DeleteObject(sBitmapIcons[i].mask);
	}

	sMenuDrawCache.clear();
	sResizedFontsMap.clear();
	sAutoRefreshLabels.clear();
	sFonts.clear();
	sPens.clear();
	sAppearances.clear();
	sBitmapIcons.clear();
	sCopyIcons.clear();
	sLabelIcons.clear();
	sHUDElementInfo.clear();

	DeleteDC(sBitmapDrawSrc);
	sBitmapDrawSrc = NULL;
}


void update()
{
	// Hande display of error messages and other notices
	// eHUDType_System should always be the last HUD element in the list
	const u16 aSystemElementID = u16(sHUDElementInfo.size() - 1);
	DBG_ASSERT(sHUDElementInfo[aSystemElementID].type == eHUDType_System);

	if( sErrorMessageTimer > 0 )
	{
		sErrorMessageTimer -= gAppFrameTime;
		if( sErrorMessageTimer <= 0 )
		{
			sErrorMessageTimer = 0;
			sErrorMessage.clear();
			gRedrawHUD.set(aSystemElementID);
		}
	}
	else if( !gErrorString.empty() && !hadFatalError() )
	{
		sErrorMessage =
			widen("MMOGO ERROR: ") +
			gErrorString +
			widen("\nCheck errorlog.txt for other possible errors!");
		sErrorMessageTimer = max(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sErrorMessage.size()));
		gErrorString.clear();
		gRedrawHUD.set(aSystemElementID);
	}

	if( sNoticeMessageTimer > 0 )
	{
		sNoticeMessageTimer -= gAppFrameTime;
		if( sNoticeMessageTimer <= 0 )
		{
			sNoticeMessageTimer = 0;
			sNoticeMessage.clear();
			gRedrawHUD.set(aSystemElementID);
		}
	}
	if( !gNoticeString.empty() )
	{
		sNoticeMessage = gNoticeString;
		sNoticeMessageTimer = max(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sNoticeMessage.size()));
		gNoticeString.clear();
		gRedrawHUD.set(aSystemElementID);
	}

	gVisibleHUD.set(aSystemElementID,
		!sNoticeMessage.empty() ||
		!sErrorMessage.empty());

	#ifdef DEBUG_DRAW_OVERLAY_FRAME
		gVisibleHUD.set(aSystemElementID);
	#endif

	if( gVisibleHUD.test(aSystemElementID) )
		gActiveHUD.set(aSystemElementID);

	// Check for updates to other HUD elements
	for(size_t i = 0; i < sHUDElementInfo.size(); ++i)
	{
		HUDElementInfo& hi = sHUDElementInfo[i];
		switch(hi.type)
		{
		case eHUDType_KBArrayLast:
			if( gKeyBindArrayLastIndexChanged.test(hi.arrayID) )
			{
				gActiveHUD.set(i);
				hi.selection = gKeyBindArrayLastIndex[hi.arrayID];
			}
			break;
		case eHUDType_KBArrayDefault:
			if( gKeyBindArrayDefaultIndexChanged.test(hi.arrayID) )
			{
				gActiveHUD.set(i);
				hi.selection = gKeyBindArrayDefaultIndex[hi.arrayID];
			}
			break;
		}

		// Update flash effect on confirmed menu item
		if( gConfirmedMenuItem[i] != kInvalidItem )
		{
			if( hi.flashMaxTime )
			{
				hi.flashing = gConfirmedMenuItem[i];
				hi.flashStartTime = gAppRunTime;
				gRedrawHUD.set(i);
			}
			gConfirmedMenuItem[i] = kInvalidItem;
		}
		else if( hi.flashing != kInvalidItem &&
				 (gAppRunTime - hi.flashStartTime) > hi.flashMaxTime )
		{
			hi.flashing = kInvalidItem;
			gRedrawHUD.set(i);
		}
	}

	// Update auto-refresh of idle copy-from-target icons
	// This system is set up to only force-redraw one icon per update,
	// but still have each icon individually only redraw at
	// sCopyIconUpdateRate, to spread the copy-paste operations out over
	// multiple frames and reduce chance of causing a framerate hitch.
	if( gAppRunTime >= sNextAutoRefreshTime )
	{
		sNextAutoRefreshTime = gAppRunTime;
		u32 validItemCount = 0;
		u16 aHUDElementToRefresh = kInvalidItem;
		u16 aMenuItemToRefresh = kInvalidItem;
		for(std::vector<AutoRefreshLabelEntry>::iterator itr =
			sAutoRefreshLabels.begin(), next_itr = itr;
			itr != sAutoRefreshLabels.end(); itr = next_itr)
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
				next_itr = sAutoRefreshLabels.erase(itr);
				continue;
			}
			gRedrawHUD.set(itr->hudElementID);
			u16& aDestItemID = hi.forcedRedrawItemID;
			// If aDestItemID is already set as a valid entry, it means still
			// waiting for it to refresh (might be currently hidden), so don't
			// count any of this HUD Element's items as valid until then
			if( aDestItemID != kInvalidItem && aDestItemID <= aMaxItemID )
				continue;
			if( validItemCount++ == 0 )
			{// Only first valid item found actually refreshes
				aHUDElementToRefresh = itr->hudElementID;
				aMenuItemToRefresh = itr->itemIdx;
				next_itr = sAutoRefreshLabels.erase(itr);
			}
		}
		if( validItemCount )
		{
			sHUDElementInfo[aHUDElementToRefresh]
				.forcedRedrawItemID = aMenuItemToRefresh;
			sNextAutoRefreshTime +=
				sCopyIconUpdateRate / validItemCount;
		}
	}

	gKeyBindArrayLastIndexChanged.reset();
	gKeyBindArrayDefaultIndexChanged.reset();
}


void updateScaling()
{
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

	// Generate fonts, border pens, etc based on current gScale values
	HUDBuilder aHUDBuilder;
	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
		if( hi.type == eHUDType_System )
			continue;
		const std::string& aHUDName =
			InputMap::hudElementKeyName(aHUDElementID);
		const bool isAMenu =
			hi.type >= eMenuStyle_Begin && hi.type < eMenuStyle_End;
		// hi.fontID = eHUDProp_FontName & _FontSize & _FontWeight
		hi.fontID = getOrCreateFontID(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontName),
			getHUDPropStr(aHUDName, eHUDProp_FontSize),
			getHUDPropStr(aHUDName, eHUDProp_FontWeight));
		if( isAMenu )
		{
			// hi.gapSizeX/Y = eHUDProp_GapSize
			aHUDBuilder.parsedString.clear();
			sanitizeSentence(
				getHUDPropStr(aHUDName, eHUDProp_GapSize),
				aHUDBuilder.parsedString);
			hi.gapSizeX = hi.gapSizeY = 0;
			if( !aHUDBuilder.parsedString.empty() )
				hi.gapSizeX = intFromString(aHUDBuilder.parsedString[0]);
			if( aHUDBuilder.parsedString.size() > 1 )
				hi.gapSizeY = intFromString(aHUDBuilder.parsedString[1]);
			else
				hi.gapSizeY = hi.gapSizeX;
			hi.gapSizeX *= gUIScaleX;
			hi.gapSizeY *= gUIScaleY;
			// hi.titleHeight = eHUDProp_TitleHeight
			hi.titleHeight = u8(u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_TitleHeight)) & 0xFF);
			if( hi.titleHeight )
				hi.titleHeight = max(8, hi.titleHeight * gUIScaleY);
		}

		// Extra data values for specific types
		if( hi.type == eHUDItemType_RndRect ||
			hi.itemType == eHUDItemType_RndRect )
		{
			hi.radius = (gUIScaleX + gUIScaleY) * 0.5 * u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_Radius));
		}

		if( hi.type == eMenuStyle_Slots )
		{
			hi.altLabelWidth = gUIScaleX * u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_AltLabelWidth));
		}
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
				max(1, appearance.baseBorderSize * gUIScaleY);
		}
		appearance.borderPenID = getOrCreatePenID(aHUDBuilder,
			appearance.borderColor, appearance.borderSize);
	}
}


void drawElement(
	HDC hdc,
	HDC hTargetDC,
	const SIZE& theTargetSize,
	u16 theHUDElementID,
	const SIZE& theComponentSize,
	const SIZE& theDestSize,
	bool needsInitialErase)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	HUDDrawData aDrawData = { 0 };
	aDrawData.hdc = hdc;
	aDrawData.hTargetDC = hTargetDC;
	aDrawData.targetSize = theTargetSize;
	aDrawData.itemSize = theComponentSize;
	aDrawData.destSize = theDestSize;
	SetRect(&aDrawData.destRect, 0, 0, theDestSize.cx, theDestSize.cy);
	aDrawData.hudElementID = theHUDElementID;
	aDrawData.appearanceMode = eAppearanceMode_Normal;
	aDrawData.firstDraw = needsInitialErase;
	if( !sBitmapDrawSrc )
		sBitmapDrawSrc = CreateCompatibleDC(hdc);

	if( hi.type >= eMenuStyle_Begin && hi.type < eMenuStyle_End )
	{// Check for complete redraw from sub-menu change
		u16 aNewSubMenu = Menus::activeSubMenu(
			InputMap::menuForHUDElement(theHUDElementID));
		if( aNewSubMenu != hi.subMenuID )
		{
			needsInitialErase = aDrawData.firstDraw = true;
			hi.subMenuID = aNewSubMenu;
		}
	}

	// Select the transparent color (erase) brush by default first
	SelectObject(hdc, GetStockObject(DC_BRUSH));
	SetDCBrushColor(hdc, hi.transColor);

	#ifdef _DEBUG
		COLORREF aFrameColor = RGB(0, 0, 0);
		#ifdef DEBUG_DRAW_HUD_ELEMENT_FRAME
			aFrameColor = RGB(0, 0, 200);
		#endif
	#ifdef DEBUG_DRAW_OVERLAY_FRAME
		if( hi.type == eHUDType_System )
			aFrameColor = RGB(255, 0, 0);
	#endif
	if( aFrameColor != RGB(0, 0, 0) && needsInitialErase )
	{
		HPEN hFramePen = CreatePen(PS_INSIDEFRAME, 3, aFrameColor);

		HPEN hOldPen = (HPEN)SelectObject(hdc, hFramePen);
		Rectangle(hdc, 0, 0, theDestSize.cx, theDestSize.cy);
		SelectObject(hdc, hOldPen);

		DeleteObject(hFramePen);

		// This effectively also erased the dest region
		needsInitialErase = false;
	}
	#endif // _DEBUG

	if( needsInitialErase )
	{
		RECT aDestRect;
		SetRect(&aDestRect, 0, 0, theDestSize.cx, theDestSize.cy);
		eraseRect(aDrawData, aDestRect);
	}

	const EHUDType aHUDType = sHUDElementInfo[theHUDElementID].type;
	switch(aHUDType)
	{
	case eMenuStyle_List:			drawListMenu(aDrawData);	break;
	case eMenuStyle_Slots:			drawSlotsMenu(aDrawData);	break;
	case eMenuStyle_Bar:			drawBarMenu(aDrawData);		break;
	case eMenuStyle_4Dir:			draw4DirMenu(aDrawData);	break;
	case eMenuStyle_Grid:			drawGridMenu(aDrawData);	break;
	case eMenuStlye_Ring:			/* TODO */					break;
	case eMenuStyle_Radial:			/* TODO */					break;
	case eHUDType_KBArrayLast:		drawBasicHUD(aDrawData);	break;
	case eHUDType_KBArrayDefault:	drawBasicHUD(aDrawData);	break;
	case eHUDType_System:			drawSystemHUD(aDrawData);	break;
	default:
		if( aHUDType >= eHUDItemType_Begin && aHUDType < eHUDItemType_End )
		{
			RECT r; SetRect(&r, 0, 0, theDestSize.cx, theDestSize.cy);
			drawHUDItem(aDrawData, r);
		}
		else
		{
			DBG_ASSERT(false && "Invaild HUD/Menu Type/Style!");
		}
	}
}


void updateWindowLayout(
	u16 theHUDElementID,
	const SIZE& theTargetSize,
	SIZE& theComponentSize,
	POINT& theWindowPos,
	SIZE& theWindowSize)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	// Calculate component size first since it affects other properties
	theComponentSize = hotspotToSize(hi.itemSize, theTargetSize);

	// Get window position (top-left corner) assuming top-left alignment
	theWindowPos = hotspotToPoint(hi.position, theTargetSize);

	// Apply special-case position offsets
	switch(hi.type)
	{
	case eHUDType_KBArrayLast:
	case eHUDType_KBArrayDefault:
		if( const Hotspot* aHotspot =
				InputMap::keyBindArrayHotspot(hi.arrayID, hi.selection) )
		{
			const POINT& anOffset = hotspotToPoint(*aHotspot, theTargetSize);
			theWindowPos.x += anOffset.x;
			theWindowPos.y += anOffset.y;
		}
		break;
	}

	// Calculate total window size needed based on type and component size
	theWindowSize = theComponentSize;
	const u16 aMenuID = InputMap::menuForHUDElement(theHUDElementID);
	switch(hi.type)
	{
	case eMenuStyle_List:
	case eMenuStyle_Slots:
		{
			const u16 aMenuItemCount = Menus::itemCount(aMenuID);
			theWindowSize.cy *= aMenuItemCount;
			if( aMenuItemCount > 1 )
				theWindowSize.cy += hi.gapSizeY * (aMenuItemCount - 1);
			theWindowSize.cx += hi.altLabelWidth;
		}
		break;
	case eMenuStyle_Bar:
		{
			const u16 aMenuItemCount = Menus::itemCount(aMenuID);
			theWindowSize.cx *= aMenuItemCount;
			if( aMenuItemCount > 1 )
				theWindowSize.cx += hi.gapSizeX * (aMenuItemCount - 1);
		}
		break;
	case eMenuStyle_4Dir:
		theWindowSize.cx = theWindowSize.cx * 2 + hi.gapSizeX;
		theWindowSize.cy = theWindowSize.cy * 3 + hi.gapSizeY * 2;
		break;
	case eMenuStyle_Grid:
		{
			const u8 aMenuItemXCount = Menus::gridWidth(aMenuID);
			const u8 aMenuItemYCount = Menus::gridHeight(aMenuID);
			theWindowSize.cx *= aMenuItemXCount;
			theWindowSize.cy *= aMenuItemYCount;
			if( aMenuItemXCount > 1 )
				theWindowSize.cx += hi.gapSizeX * (aMenuItemXCount-1);
			if( aMenuItemYCount > 1 )
				theWindowSize.cy += hi.gapSizeY * (aMenuItemYCount-1);
		}
		break;
	case eHUDType_System:
		theWindowSize.cx = theTargetSize.cx;
		theWindowSize.cy = theTargetSize.cy;
		break;
	}
	theWindowSize.cy += hi.titleHeight;

	// Adjust position according to size and alignment settings
	switch(sHUDElementInfo[theHUDElementID].alignmentX)
	{
	case eAlignment_Min:
		// Do nothing
		break;
	case eAlignment_Center:
		theWindowPos.x -= theWindowSize.cx / 2;
		break;
	case eAlignment_Max:
		theWindowPos.x -= theWindowSize.cx;
		break;
	}
	switch(sHUDElementInfo[theHUDElementID].alignmentY)
	{
	case eAlignment_Min:
		// Do nothing
		break;
	case eAlignment_Center:
		theWindowPos.y -= theWindowSize.cy / 2;
		break;
	case eAlignment_Max:
		theWindowPos.y -= theWindowSize.cy;
		break;
	}

	// Clamp to target area
	theWindowPos.x = max(0, theWindowPos.x);
	theWindowPos.y = max(0, theWindowPos.y);
	theWindowSize.cx = min(theWindowSize.cx, theTargetSize.cx - theWindowPos.x);
	theWindowSize.cy = min(theWindowSize.cy, theTargetSize.cy - theWindowPos.y);
}


POINT componentOffsetPos(
	u16 theHUDElementID,
	u16 theComponentIdx,
	const SIZE& theComponentSize)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	POINT result = { 0, 0 };
	switch(hi.type)
	{
	case eMenuStyle_List:
		result.y = (theComponentSize.cy + hi.gapSizeY) * theComponentIdx;
		break;
	case eMenuStyle_Bar:
		result.x = (theComponentSize.cx + hi.gapSizeX) * theComponentIdx;
		break;
	case eMenuStyle_Grid:
		{
			const u16 aMenuID = InputMap::menuForHUDElement(theHUDElementID);
			const u16 aGridWidth = Menus::gridWidth(aMenuID);
			const u16 aXOffset = theComponentIdx % aGridWidth;
			const u16 aYOffset = theComponentIdx / aGridWidth;
			result.x = (theComponentSize.cx + hi.gapSizeX) * aXOffset;
			result.y = (theComponentSize.cy + hi.gapSizeY) * aYOffset;
		}
		break;
	}

	result.y += hi.titleHeight;
	return result;
}


void drawMainWindowContents(HWND theWindow)
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
	if( LONG aFontHeight = getFontHeightToFit(
			hdc, aWStr, aRect, NULL, DT_SINGLELINE) )
	{// Need alternate font to actually fit string properly
		ncm.lfMessageFont.lfWidth = 0;
		ncm.lfMessageFont.lfHeight = aFontHeight;
		DeleteObject(SelectObject(hdc,
			CreateFontIndirect(&ncm.lfMessageFont)));
	}

	// Draw version string centered
	DrawText(hdc, aWStr.c_str(), -1, &aRect,
		DT_SINGLELINE | DT_CENTER | DT_VCENTER);

	// Swap back to default font and delete temp font
	DeleteObject(SelectObject(hdc, hOldFont));

	EndPaint(theWindow, &ps);
}


u8 maxAlpha(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].maxAlpha;
}


u8 inactiveAlpha(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].inactiveAlpha;
}


int alphaFadeInDelay(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].fadeInDelay;
}


float alphaFadeInRate(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].fadeInRate;
}


int alphaFadeOutDelay(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].fadeOutDelay;
}


float alphaFadeOutRate(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].fadeOutRate;
}


int inactiveFadeOutDelay(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].delayUntilInactive;
}


COLORREF transColor(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].transColor;
}


s8 drawPriority(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	return sHUDElementInfo[theHUDElementID].drawPriority;
}


bool shouldStartHidden(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	switch(sHUDElementInfo[theHUDElementID].type)
	{
	case eHUDType_KBArrayLast:
		return true;
	}

	return false;
}

} // HUD
