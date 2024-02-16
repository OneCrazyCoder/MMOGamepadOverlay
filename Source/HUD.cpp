//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "InputMap.h" // labels, profileStringToHotspot(), menuForHUDElement()
#include "Lookup.h"
#include "Menus.h" // activeSubMenu()
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
const char* kAppVersionString = "Version: " __DATE__;

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
	eHUDProp_SFontColor,
	eHUDProp_SItemColor,
	eHUDProp_SBorderColor,
	eHUDProp_TransColor,
	eHUDProp_MaxAlpha,
	eHUDProp_FadeInDelay,
	eHUDProp_FadeInTime,
	eHUDProp_FadeOutDelay,
	eHUDProp_FadeOutTime,
	eHUDProp_InactiveDelay,
	eHUDProp_InactiveAlpha,
	eHUDProp_BitmapPath,
	eHUDProp_SBitmapPath,
	eHUDProp_Radius,

	eHUDProp_Num
};

enum EAlignment
{
	eAlignment_Min,		// L or T
	eAlignment_Center,	// CX or CY
	eAlignment_Max,		// R or B
};

const char* kHUDPropStr[] =
{
	"Position",				// eHUDProp_Position
	"ItemType",				// eHUDProp_ItemType
	"ItemSize",				// eHUDProp_ItemSize
	"Size",					// eHUDProp_Size
	"Alignment",			// eHUDProp_Alignment
	"Font",					// eHUDProp_FontName
	"FontSize",				// eHUDProp_FontSize
	"FontWeight",			// eHUDProp_FontWeight
	"LabelRGB",				// eHUDProp_FontColor
	"ItemRGB",				// eHUDProp_ItemColor
	"BorderRGB",			// eHUDProp_BorderColor
	"BorderSize",			// eHUDProp_BorderSize
	"SelectedLabelRGB",		// eHUDProp_SFontColor
	"SelectedItemRGB",		// eHUDProp_SItemColor
	"SelectedBorderRGB",	// eHUDProp_SBorderColor
	"TransRGB",				// eHUDProp_TransColor
	"MaxAlpha",				// eHUDProp_MaxAlpha
	"FadeInDelay",			// eHUDProp_FadeInDelay
	"FadeInTime",			// eHUDProp_FadeInTime
	"FadeOutDelay",			// eHUDProp_FadeOutDelay
	"FadeOutTime",			// eHUDProp_FadeOutTime
	"InactiveDelay",		// eHUDProp_InactiveDelay
	"InactiveAlpha",		// eHUDProp_InactiveAlpha
	"BitmapPath",			// eHUDProp_BitmapPath
	"SelectedBitmapPath",	// eHUDProp_SBitmapPath
	"Radius",				// eHUDProp_Radius
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
	COLORREF labelColor;
	COLORREF labelBGColor;
	COLORREF selLabelColor;
	COLORREF selLabelBGColor;
	COLORREF transColor;
	Hotspot position;
	Hotspot itemSize;
	float fadeInRate;
	float fadeOutRate;
	int fadeInDelay;
	int fadeOutDelay;
	int delayUntilInactive;
	int selection;
	u16 subMenuID;
	u16 fontID;
	u16 itemBrushID;
	u16 borderPenID;
	u16 borderSize;
	u16 selItemBrushID;
	u16 selBorderPenID;
	u16 eraseBrushID;
	u16 bitmapID;
	u16 selBitmapID;
	u16 radius;
	u8 alignmentX;
	u8 alignmentY;
	u8 maxAlpha;
	u8 inactiveAlpha;

	HUDElementInfo() :
		itemType(eHUDItemType_Rect),
		fadeInRate(255),
		fadeOutRate(255),
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
	SIZE itemSize;
	SIZE targetSize;
	RECT targetRect;
	u16 hudElementID;
	bool firstDraw;
};

struct HUDDrawCacheEntry
{
	LONG yOff;
	u16 fontID;
	bool initialized;
	HUDDrawCacheEntry() : initialized(false) {}
};

struct HUDBuilder
{
	std::vector<std::string> parsedString;
	StringToValueMap<u16> fontInfoToFontIDMap;
	VectorMap<COLORREF, u16> colorToBrushMap;
	typedef std::pair< COLORREF, std::pair<int, int> > PenDef;
	VectorMap<PenDef, u16> penDefToPenMap;
	VectorMap<DWORD, u16> fileToBitmapMap;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<HFONT> sFonts;
static std::vector<HBRUSH> sBrushes;
static std::vector<HPEN> sPens;
static std::vector<HBITMAP> sBitmaps;
static std::vector<HUDElementInfo> sHUDElementInfo;
static std::vector< std::vector<HUDDrawCacheEntry> > sMenuDrawCache;
static VectorMap<std::pair<u16, LONG>, u16> sResizedFontsMap;
static std::wstring sErrorMessage;
static std::wstring sNoticeMessage;
static int sErrorMessageTimer = 0;
static int sNoticeMessageTimer = 0;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static std::string getHUDPropStr(
	const std::string& theName,
	EHUDProperty theProperty,
	bool namedOnly = false)
{
	std::string result;
	std::string aKey;

	if( !theName.empty() )
	{
		// Try [Menu.Name] first since most HUD elements are likely Menus
		aKey = kMenuPrefix;
		aKey += "."; aKey += theName + "/";
		aKey += kHUDPropStr[theProperty];
		result = Profile::getStr(aKey);
		if( !result.empty() )
			return result;

		// Try [HUD.Name] next
		aKey = kHUDPrefix;
		aKey += "."; aKey += theName + "/";
		aKey += kHUDPropStr[theProperty];
		result = Profile::getStr(aKey);
		if( !result.empty() )
			return result;
	}

	if( namedOnly )
		return result;

	// Try just [HUD] for a default value
	aKey = kHUDPrefix;
	aKey += "/";
	aKey += kHUDPropStr[theProperty];
	result = Profile::getStr(aKey);
	if( !result.empty() )
		return result;

	// Maybe just [Menu] for default value?
	aKey = kMenuPrefix;
	aKey += "/";
	aKey += kHUDPropStr[theProperty];
	result = Profile::getStr(aKey);
	if( !result.empty() )
		return result;

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


static u16 getOrCreateBrushID(HUDBuilder& theBuilder, COLORREF theColor)
{
	// Check for and return existing brush
	u16 result = theBuilder.colorToBrushMap.findOrAdd(
		theColor, u16(sBrushes.size()));
	if( result < sBrushes.size() )
		return result;

	// Create new brush
	HBRUSH aBrush = CreateSolidBrush(theColor);
	sBrushes.push_back(aBrush);
	return result;
}


static u16 getOrCreatePenID(
	HUDBuilder& theBuilder,
	COLORREF theColor,
	int theStyle,
	int theWidth)
{
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
	const int aFontPointSize = intFromString(theFontSize);
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


static u16 getOrCreateBitmapID(
	HUDBuilder& theBuilder,
	const std::string& theBitmapPath)
{
	u16 result = 0;
	if( theBitmapPath.empty() )
		return result;

	const std::wstring& aFilePathW =
		getExtension(theBitmapPath).empty()
			? widen(removeExtension(theBitmapPath) + ".bmp")
			: widen(theBitmapPath);
	const DWORD aFileAttributes = GetFileAttributes(aFilePathW.c_str());
	if( aFileAttributes == INVALID_FILE_ATTRIBUTES ||
		(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
	{
		logError("Could not find requested bitmap file %s!",
			theBitmapPath.c_str());
		return result;
	}

	DWORD aFileID = 0;
	HANDLE hFile = CreateFile(aFilePathW.c_str(),
		GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if( hFile != INVALID_HANDLE_VALUE )
	{
		BY_HANDLE_FILE_INFORMATION aFileInfo;
		if( GetFileInformationByHandle(hFile, &aFileInfo) )
		{
			aFileID = aFileInfo.dwVolumeSerialNumber ^
				((aFileInfo.nFileIndexHigh << 16) | aFileInfo.nFileIndexLow) ^
				aFileInfo.ftCreationTime.dwHighDateTime ^
				aFileInfo.ftCreationTime.dwLowDateTime;
		}
		CloseHandle(hFile);
	}

	if( !aFileID )
	{
		logError("Could not identify requested bitmap file %s!",
			theBitmapPath.c_str());
		return result;
	}

	result = theBuilder.fileToBitmapMap.findOrAdd(
		aFileID, u16(sBitmaps.size()));
	if( result < sBitmaps.size() )
		return result;

	sBitmaps.push_back((HBITMAP)
		LoadImage(NULL, aFilePathW.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE));
	if( sBitmaps.back() == NULL )
	{
		logError("Could not load bitmap %s. Wrong bitmap format?",
			theBitmapPath.c_str());
	}

	return result;
}


LONG scaleHotspot(
	const Hotspot::Coord& theCoord,
	u16 theTargetSize)
{
	return
		LONG(theCoord.origin) *
		theTargetSize / 0x10000 +
		theCoord.offset;
}


static void drawHUDRect(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

	Rectangle(dd.hdc,
		theRect.left, theRect.top,
		theRect.right, theRect.bottom);
}


static void drawHUDRndRect(HUDDrawData& dd, const RECT& theRect)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

	RoundRect(dd.hdc,
		theRect.left, theRect.top,
		theRect.right, theRect.bottom,
		hi.radius, hi.radius);
}


static void drawHUDBitmap(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	HBITMAP aBitmap = sBitmaps[hi.bitmapID];
	if( !aBitmap )
	{
		drawHUDRect(dd, theRect);
		return;
	}

	BITMAP bm;
	GetObject(aBitmap, sizeof(bm), &bm);
	HDC hdcMem = CreateCompatibleDC(dd.hdc);
	HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, aBitmap);

	StretchBlt(dd.hdc,
			   theRect.left, theRect.top,
			   theRect.right - theRect.left, theRect.bottom - theRect.top,
			   hdcMem,
			   0, 0,
			   bm.bmWidth, bm.bmHeight,
			   SRCCOPY);

	SelectObject(hdcMem, hOldBitmap);
	DeleteDC(hdcMem);
}


static void drawHUDCircle(HUDDrawData& dd, const RECT& theRect)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

	Ellipse(dd.hdc,
		theRect.left, theRect.top,
		theRect.right, theRect.bottom);
}


static void drawHUDArrowL(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

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

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

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

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

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

	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);

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


static void drawMenuItem(
	HUDDrawData& dd,
	const RECT& theRect,
	const std::string& theLabel,
	HUDDrawCacheEntry& theCacheEntry,
	bool selected = false)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	if( selected )
	{
		swap(hi.itemBrushID, hi.selItemBrushID);
		swap(hi.borderPenID, hi.selBorderPenID);
		swap(hi.bitmapID, hi.selBitmapID);
	}
	drawHUDItem(dd, theRect);
	if( selected )
	{
		swap(hi.itemBrushID, hi.selItemBrushID);
		swap(hi.borderPenID, hi.selBorderPenID);
		swap(hi.bitmapID, hi.selBitmapID);
	}

	if( theLabel.empty() )
		return;

	// Label (usually word-wrapped and centered text)
	const std::wstring& aLabelW = widen(theLabel);
	RECT aTextRect = theRect;
	InflateRect(&aTextRect, -hi.borderSize - 1, -hi.borderSize - 1);

	// Get draw details from cache entry, or initialize it now
	const UINT aFormat = DT_WORDBREAK | DT_CENTER;
	if( !theCacheEntry.initialized )
	{
		theCacheEntry.fontID = hi.fontID;

		// This will not only check if an alternate font size is needed, but
		// also calculate what drawn rect will be with it (aNeededRect) which
		// can be used to calculate an offset for vertical center justify
		// when using DT_WORDBREAK (which means DT_VCENTER doesn't work).
		SelectObject(dd.hdc, sFonts[hi.fontID]);
		RECT aNeededRect = aTextRect;
		if( LONG aFontHeight = getFontHeightToFit(
				dd.hdc, aLabelW, aTextRect, &aNeededRect, aFormat) )
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

		// Center the text vertically using modified aNeededRect
		theCacheEntry.yOff =
			((aTextRect.bottom - aTextRect.top) -
			 (aNeededRect.bottom - aNeededRect.top)) / 2;
		theCacheEntry.initialized = true;
	}

	// Draw label based on cache entry's fields
	DBG_ASSERT(theCacheEntry.initialized);
	aTextRect.top += theCacheEntry.yOff;
	SelectObject(dd.hdc, sFonts[theCacheEntry.fontID]);
	SetTextColor(dd.hdc, selected ? hi.selLabelColor : hi.labelColor);
	SetBkColor(dd.hdc, selected ? hi.selLabelBGColor : hi.labelBGColor);
	DrawText(dd.hdc, aLabelW.c_str(), -1, &aTextRect, aFormat);
}


static void drawListMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aPrevSubMenuID = hi.subMenuID;
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	DBG_ASSERT(aSubMenuID < sMenuDrawCache.size());
	const u16 aPrevSelection = hi.selection;
	const u16 aSelection = Menus::selectedItem(dd.hudElementID);
	const u16 anItemCount = Menus::itemCount(dd.hudElementID);
	DBG_ASSERT(aSelection < anItemCount);
	if( sMenuDrawCache[aSubMenuID].size() < anItemCount )
		sMenuDrawCache[aSubMenuID].resize(anItemCount);

	RECT anItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.bottom = dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		// Don't need to re-draw menu items that haven't changed their
		// "selected" status or label (sub-menu) after firstDraw done
		if( dd.firstDraw || aSubMenuID != aPrevSubMenuID ||
			itemIdx == aPrevSelection || itemIdx == aSelection )
		{
			if( !dd.firstDraw &&
				aSubMenuID != aPrevSubMenuID &&
				hi.itemType != eHUDItemType_Rect )
			{
				// Non-_Rect menu items need to erase the full rect when
				// the label (sub-menu) changes (except for firstDraw),
				// in case old label poked out of the background shape.
				FillRect(dd.hdc, &anItemRect, sBrushes[hi.eraseBrushID]);
			}
			drawMenuItem(dd, anItemRect,
				InputMap::menuItemLabel(aSubMenuID, itemIdx),
				sMenuDrawCache[aSubMenuID][itemIdx],
				itemIdx == aSelection);
		}
		anItemRect.top = anItemRect.bottom;
		anItemRect.bottom += dd.itemSize.cy;
	}

	hi.subMenuID = aSubMenuID;
	hi.selection = aSelection;
}


static void drawSlotsMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aPrevSubMenuID = hi.subMenuID;
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	DBG_ASSERT(aSubMenuID < sMenuDrawCache.size());
	const u16 aPrevSelection = hi.selection;
	const u16 aSelection = Menus::selectedItem(dd.hudElementID);
	const u16 anItemCount = Menus::itemCount(dd.hudElementID);
	DBG_ASSERT(aSelection < anItemCount);
	if( sMenuDrawCache[aSubMenuID].size() < anItemCount )
		sMenuDrawCache[aSubMenuID].resize(anItemCount);

	RECT anItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.bottom = dd.itemSize.cy;
	// Draw selected item on top
	for(u16 itemIdx = aSelection; itemIdx < anItemCount; ++itemIdx)
	{
		if( !dd.firstDraw &&
			aSubMenuID != aPrevSubMenuID &&
			hi.itemType != eHUDItemType_Rect )
		{
			// Non-_Rect menu items need to erase the full rect when
			// the label (sub-menu) changes (except for firstDraw),
			// in case old label poked out of the background shape.
			FillRect(dd.hdc, &anItemRect, sBrushes[hi.eraseBrushID]);
		}
		drawMenuItem(dd, anItemRect,
			InputMap::menuItemLabel(aSubMenuID, itemIdx),
			sMenuDrawCache[aSubMenuID][itemIdx],
			itemIdx == aSelection);
		anItemRect.top = anItemRect.bottom;
		anItemRect.bottom += dd.itemSize.cy;
	}
	// Draw rest of the items below
	for(u16 itemIdx = 0; itemIdx < aSelection; ++itemIdx)
	{
		if( !dd.firstDraw &&
			aSubMenuID != aPrevSubMenuID &&
			hi.itemType != eHUDItemType_Rect )
		{
			FillRect(dd.hdc, &anItemRect, sBrushes[hi.eraseBrushID]);
		}
		drawMenuItem(dd, anItemRect,
			InputMap::menuItemLabel(aSubMenuID, itemIdx),
			sMenuDrawCache[aSubMenuID][itemIdx],
			itemIdx == aSelection);
		anItemRect.top = anItemRect.bottom;
		anItemRect.bottom += dd.itemSize.cy;
	}

	hi.subMenuID = aSubMenuID;
	hi.selection = aSelection;
}


static void draw4DirMenu(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	DBG_ASSERT(aSubMenuID < sMenuDrawCache.size());

	if( !dd.firstDraw && hi.itemType != eHUDItemType_Rect )
		FillRect(dd.hdc, &dd.targetRect, sBrushes[hi.eraseBrushID]);
	if( sMenuDrawCache[aSubMenuID].size() < 4 )
		sMenuDrawCache[aSubMenuID].resize(4);

	// Left
	RECT anItemRect;
	anItemRect.left = 0;
	anItemRect.top = dd.itemSize.cy;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Left),
		sMenuDrawCache[aSubMenuID][eCmdDir_Left]);
	// Right
	anItemRect.left += dd.itemSize.cx;
	anItemRect.right += dd.itemSize.cx;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Right),
		sMenuDrawCache[aSubMenuID][eCmdDir_Right]);
	// Up
	anItemRect.left -= dd.itemSize.cx / 2;
	anItemRect.top = 0;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Up),
		sMenuDrawCache[aSubMenuID][eCmdDir_Up]);
	// Down
	anItemRect.top += dd.itemSize.cy * 2;
	anItemRect.bottom += dd.itemSize.cy * 2;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Down),
		sMenuDrawCache[aSubMenuID][eCmdDir_Down]);
}


static void drawBasicHUD(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];

	if( !dd.firstDraw )
		FillRect(dd.hdc, &dd.targetRect, sBrushes[hi.eraseBrushID]);

	drawHUDItem(dd, dd.targetRect);
}


static void drawSystemHUD(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	DBG_ASSERT(hi.type == eHUDType_System);

	// Erase any previous strings
	if( !dd.firstDraw )
		FillRect(dd.hdc, &dd.targetRect, sBrushes[hi.eraseBrushID]);

	RECT aTextRect = dd.targetRect;
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

	sBitmaps.push_back(NULL);
	aHUDBuilder.fileToBitmapMap.addPair(0, NULL);

	// Get default erase (transparent) color value
	const COLORREF aDefaultTransColor = strToRGB(aHUDBuilder,
					getHUDPropStr("", eHUDProp_TransColor));
	const u16 aDefaultEraseBrush =
		getOrCreateBrushID(aHUDBuilder, aDefaultTransColor);

	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
		hi.type = InputMap::hudElementType(aHUDElementID);
		if( hi.type >= eHUDItemType_Begin && hi.type < eHUDItemType_End )
			hi.itemType = hi.type;
		hi.transColor = aDefaultTransColor;
		hi.eraseBrushID = aDefaultEraseBrush;
		if( hi.type == eHUDType_System )
			continue;
		const std::string& aHUDName = InputMap::hudElementLabel(aHUDElementID);
		// hi.itemType = eHUDProp_ItemType
		std::string aStr = getHUDPropStr(aHUDName, eHUDProp_ItemType);
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
		// hi.position = eHUDProp_Position
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Position),
			hi.position);
		// hi.itemSize = eHUDProp_ItemSize (Menus) or eHUDProp_Size (HUD)
		if( hi.type < eMenuStyle_End )
			aStr = getHUDPropStr(aHUDName, eHUDProp_ItemSize, true);
		else
			aStr = getHUDPropStr(aHUDName, eHUDProp_Size, true);
		if( aStr.empty() && hi.type < eMenuStyle_End )
			aStr = getHUDPropStr(aHUDName, eHUDProp_Size, true);
		if( aStr.empty() )
			aStr = getHUDPropStr(aHUDName, eHUDProp_ItemSize);
		InputMap::profileStringToHotspot(aStr, hi.itemSize);
		// hi.alignmentX/Y = eHUDProp_Alignment
		Hotspot aTempHotspot;
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Alignment),
			aTempHotspot);
		hi.alignmentX =
			aTempHotspot.x.origin < 0x4000	? eAlignment_Min :
			aTempHotspot.x.origin > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		hi.alignmentY =
			aTempHotspot.y.origin < 0x4000	? eAlignment_Min :
			aTempHotspot.y.origin > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		// hi.fontID = eHUDProp_FontName & _FontSize & _FontWeight
		hi.fontID = getOrCreateFontID(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontName),
			getHUDPropStr(aHUDName, eHUDProp_FontSize),
			getHUDPropStr(aHUDName, eHUDProp_FontWeight));
		// hi.labelColor = eHUDProp_FontColor
		hi.labelColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontColor));
		// hi.itemBrushID & .labelBGColor = eHUDProp_ItemColor
		hi.labelBGColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_ItemColor));
		hi.itemBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.labelBGColor);
		// hi.borderPenID & .borderSize = eHUDProp_BorderColor & _BorderSize
		hi.borderSize =
			u32FromString(getHUDPropStr(aHUDName, eHUDProp_BorderSize));
		hi.borderPenID = getOrCreatePenID(aHUDBuilder,
			strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BorderColor)),
			hi.borderSize ? PS_INSIDEFRAME : PS_NULL, int(hi.borderSize));
		// hi.selLabelColor = eHUDProp_SFontColor
		hi.selLabelColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_SFontColor));
		// hi.selItemBrushID & .setLabelBGColor = eHUDProp_SItemColor
		hi.selLabelBGColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_SItemColor));
		hi.selItemBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.selLabelBGColor);
		// hi.selBorderPenID = eHUDProp_SBorderColor (and +1 to size)
		hi.selBorderPenID = getOrCreatePenID(aHUDBuilder,
			strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_SBorderColor)),
			PS_INSIDEFRAME, int(hi.borderSize + 1));
		// hi.transColor & .eraseBrushID = eHUDProp_TransColor
		hi.transColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_TransColor));
		hi.eraseBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.transColor);
		// hi.maxAlpha = eHUDProp_MaxAlpha
		hi.maxAlpha = u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_MaxAlpha)) & 0xFF;
		// hi.fadeInDelay = eHUDProp_FadeInDelay
		hi.fadeInDelay = max(0, intFromString(
			getHUDPropStr(aHUDName, eHUDProp_FadeInDelay)));
		// hi.fadeInRate = eHUDProp_FadeInTime
		u32 aVal = max(1, u32FromString(
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
		hi.inactiveAlpha = u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_InactiveAlpha)) & 0xFF;

		// Extra data values for specific types
		if( hi.type == eHUDItemType_RndRect ||
			hi.itemType == eHUDItemType_RndRect )
		{
			hi.radius = u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_Radius));
		}
		if( hi.type == eHUDItemType_Bitmap ||
			hi.itemType == eHUDItemType_Bitmap )
		{
			hi.bitmapID = getOrCreateBitmapID(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BitmapPath));
			aStr = getHUDPropStr(aHUDName, eHUDProp_SBitmapPath);
			if( aStr.empty() )
				hi.selBitmapID = hi.bitmapID;
			else
				hi.selBitmapID = getOrCreateBitmapID(aHUDBuilder, aStr);
		}
	}
}


void cleanup()
{
	for(size_t i = 0; i < sFonts.size(); ++i)
		DeleteObject(sFonts[i]);
	for(size_t i = 0; i < sBrushes.size(); ++i)
		DeleteObject(sBrushes[i]);
	for(size_t i = 0; i < sPens.size(); ++i)
		DeleteObject(sPens[i]);
	for(size_t i = 0; i < sBitmaps.size(); ++i)
		DeleteObject(sBitmaps[i]);
	sMenuDrawCache.clear();
	sResizedFontsMap.clear();
	sFonts.clear();
	sBrushes.clear();
	sPens.clear();
	sBitmaps.clear();
	sHUDElementInfo.clear();
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
		case eHUDType_GroupTarget:
			if( gLastGroupTargetUpdated )
			{
				gActiveHUD.set(i);
				hi.selection = gLastGroupTarget;
			}
			break;
		case eHUDType_DefaultTarget:
			if( gDefaultGroupTargetUpdated )
			{
				gActiveHUD.set(i);
				hi.selection = gDefaultGroupTarget;
			}
			break;
		}
	}
	gLastGroupTargetUpdated = false;
	gDefaultGroupTargetUpdated = false;
}


void clearCache()
{
	for(size_t i = 0; i < sMenuDrawCache.size(); ++i)
	{
		for(size_t j = 0; j < sMenuDrawCache[i].size(); ++j)
			sMenuDrawCache[i][j].initialized = false;
	}
}


void drawElement(
	HDC hdc,
	u16 theHUDElementID,
	const SIZE& theComponentSize,
	const SIZE& theDestSize,
	bool needsInitialErase)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	HUDDrawData aDrawData = { 0 };
	aDrawData.hdc = hdc;
	aDrawData.itemSize = theComponentSize;
	aDrawData.targetSize = theDestSize;
	SetRect(&aDrawData.targetRect, 0, 0, theDestSize.cx, theDestSize.cy);
	aDrawData.hudElementID = theHUDElementID;
	aDrawData.firstDraw = needsInitialErase;

	// Select the eraseBrush (transparent color) by default first
	SelectObject(hdc, sBrushes[hi.eraseBrushID]);

	#ifdef _DEBUG
	COLORREF aFrameColor = RGB(0, 0, 0);
	#ifdef DEBUG_DRAW_HUD_ELEMENT_FRAME
	aFrameColor = RGB(0, 0, 200);
	#endif
	#ifdef DEBUG_DRAW_OVERLAY_FRAME
	if( hi.type == eHUDType_System )
		aFrameColor = RGB(255, 0, 0);
	#endif
	if( aFrameColor != RGB(0, 0, 0) )
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
		RECT aTargetRect;
		SetRect(&aTargetRect, 0, 0, theDestSize.cx, theDestSize.cy);
		FillRect(hdc, &aTargetRect, sBrushes[hi.eraseBrushID]);
	}

	const EHUDType aHUDType = sHUDElementInfo[theHUDElementID].type;
	switch(aHUDType)
	{
	case eMenuStyle_List:
	case eMenuStyle_ListWrap:		drawListMenu(aDrawData);	break;
	case eMenuStyle_Slots:			drawSlotsMenu(aDrawData);	break;
	case eMenuStyle_Bar:			/* TODO */					break;
	case eMenuStyle_4Dir:			draw4DirMenu(aDrawData);	break;
	case eMenuStyle_GridX:			/* TODO */					break;
	case eMenuStyle_GridY:			/* TODO */					break;
	case eMenuStlye_Ring:			/* TODO */					break;
	case eMenuStyle_Radial:			/* TODO */					break;
	case eHUDType_GroupTarget:		drawBasicHUD(aDrawData);	break;
	case eHUDType_DefaultTarget:	drawBasicHUD(aDrawData);	break;
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
	theComponentSize.cx = scaleHotspot(hi.itemSize.x, theTargetSize.cx);
	theComponentSize.cy = scaleHotspot(hi.itemSize.y, theTargetSize.cy);

	// Get window position (top-left corner) assuming top-left alignment
	theWindowPos.x = scaleHotspot(hi.position.x, theTargetSize.cx);
	theWindowPos.y = scaleHotspot(hi.position.y, theTargetSize.cy);

	// Apply special-case position offsets
	switch(hi.type)
	{
	case eHUDType_GroupTarget:
		theWindowPos.x += scaleHotspot(InputMap::getHotspot(
			eSpecialHotspot_TargetSelf + hi.selection).x,
			theTargetSize.cx);
		theWindowPos.y += scaleHotspot(InputMap::getHotspot(
			eSpecialHotspot_TargetSelf + hi.selection).y,
			theTargetSize.cy);
		break;
	case eHUDType_DefaultTarget:
		theWindowPos.x += scaleHotspot(InputMap::getHotspot(
			eSpecialHotspot_TargetSelf + hi.selection).x,
			theTargetSize.cx);
		theWindowPos.y += scaleHotspot(InputMap::getHotspot(
			eSpecialHotspot_TargetSelf + hi.selection).y,
			theTargetSize.cy);
		break;
	}

	// Calculate total window size needed based on type and component size
	theWindowSize = theComponentSize;
	switch(hi.type)
	{
	case eMenuStyle_List:
	case eMenuStyle_ListWrap:
	case eMenuStyle_Slots:
		theWindowSize.cy *= Menus::itemCount(theHUDElementID);
		break;
	case eMenuStyle_4Dir:
		theWindowSize.cx *= 2;
		theWindowSize.cy *= 3;
		break;
	case eHUDType_System:
		theWindowSize.cx = theTargetSize.cx;
		theWindowSize.cy = theTargetSize.cy;
		break;
	}

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


bool shouldStartHidden(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	switch(sHUDElementInfo[theHUDElementID].type)
	{
	case eHUDType_GroupTarget:
		return true;
	}

	return false;
}

} // HUD
