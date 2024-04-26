//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "InputMap.h" // labels, profileStringToHotspot(), menuForHUDElement()
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
	eHUDProp_GapSize,
	eHUDProp_SFontColor,
	eHUDProp_SItemColor,
	eHUDProp_SBorderColor,
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
	eHUDProp_SelBitmap,
	eHUDProp_Radius,
	eHUDProp_TitleHeight,

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
	"GapSize",				// eHUDProp_GapSize
	"SelectedLabelRGB",		// eHUDProp_SFontColor
	"SelectedItemRGB",		// eHUDProp_SItemColor
	"SelectedBorderRGB",	// eHUDProp_SBorderColor
	"TitleRGB",				// eHUDProp_TitleColor
	"TransRGB",				// eHUDProp_TransColor
	"MaxAlpha",				// eHUDProp_MaxAlpha
	"FadeInDelay",			// eHUDProp_FadeInDelay
	"FadeInTime",			// eHUDProp_FadeInTime
	"FadeOutDelay",			// eHUDProp_FadeOutDelay
	"FadeOutTime",			// eHUDProp_FadeOutTime
	"InactiveDelay",		// eHUDProp_InactiveDelay
	"InactiveAlpha",		// eHUDProp_InactiveAlpha
	"Bitmap",				// eHUDProp_Bitmap
	"SelectedBitmap",		// eHUDProp_SelBitmap
	"Radius",				// eHUDProp_Radius
	"TitleHeight",			// eHUDProp_TitleHeight
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
	COLORREF titleColor;
	COLORREF titleBGColor;
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
	union { u16 subMenuID; u16 arrayID; };
	u16 fontID;
	u16 itemBrushID;
	u16 borderPenID;
	u16 selItemBrushID;
	u16 selBorderPenID;
	u16 eraseBrushID;
	u16 iconID;
	u16 selIconID;
	u16 titleBrushID;
	u8 borderSize;
	s8 gapSize;
	u8 radius;
	u8 titleHeight;
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

struct IconEntry
	: public ConstructFromZeroInitializedMemory<IconEntry>
{
	u16 bitmapID; // 0 == copy from target window
	Hotspot pos;
	Hotspot size;
};

struct StringScaleCacheEntry
{
	u16 width, height, fontID;
};

struct MenuDrawCacheEntry
	: public ConstructFromZeroInitializedMemory<MenuDrawCacheEntry>
{
	u16 iconID;
	StringScaleCacheEntry str;
};

struct HUDBuilder
{
	std::vector<std::string> parsedString;
	StringToValueMap<u16> fontInfoToFontIDMap;
	VectorMap<COLORREF, u16> colorToBrushMap;
	typedef std::pair< COLORREF, std::pair<int, int> > PenDef;
	VectorMap<PenDef, u16> penDefToPenMap;
	StringToValueMap<u16> bitmapNameToIdxMap;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<HFONT> sFonts;
static std::vector<HBRUSH> sBrushes;
static std::vector<HPEN> sPens;
static std::vector<HBITMAP> sBitmaps;
static std::vector<IconEntry> sIcons;
static std::vector<HUDElementInfo> sHUDElementInfo;
static std::vector< std::vector<MenuDrawCacheEntry> > sMenuDrawCache;
static VectorMap<std::pair<u16, LONG>, u16> sResizedFontsMap;
static std::wstring sErrorMessage;
static std::wstring sNoticeMessage;
static HDC sBitmapDrawSrc = NULL;
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
	const int aFontPointSize = intFromString(theFontSize) * gUIScaleY;
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


static void loadBitmap(
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

	sBitmaps.push_back(aBitmapHandle);
	theBuilder.bitmapNameToIdxMap.setValue(
		theBitmapName, u16(sBitmaps.size()-1));
}


static inline POINT hotspotToPoint(
	const Hotspot& theHotspot,
	const SIZE& theTargetSize)
{
	POINT result;
	result.x =
		LONG(theHotspot.x.origin) * theTargetSize.cx / 0x10000 +
		LONG(theHotspot.x.offset * gUIScaleX);
	result.y =
		LONG(theHotspot.y.origin) * theTargetSize.cy / 0x10000 +
		LONG(theHotspot.y.offset * gUIScaleY);
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


static void hotspotToPixelsOnly(
	Hotspot& theHotspot,
	const SIZE theTargetSize)
{
	theHotspot.x.offset +=
		u32(theHotspot.x.origin) * theTargetSize.cx / 0x10000;
	theHotspot.y.offset +=
		u32(theHotspot.y.origin) * theTargetSize.cy / 0x10000;
	theHotspot.x.origin = 0;
	theHotspot.y.origin = 0;
}


static u16 createIconID(
	HUDBuilder& theBuilder,
	const std::string& theIconDescription)
{
	// The description could contain just the bitmap name, just a rectangle
	// (to copy from target), or "bitmap : rect" format for both.
	// First try getting the bitmap name
	std::string aRectDesc = theIconDescription;
	std::string aBitmapName = breakOffItemBeforeChar(aRectDesc, ':');
	u16* aBitmapIDPtr = aBitmapName.empty()
		? theBuilder.bitmapNameToIdxMap.find(condense(theIconDescription))
		: theBuilder.bitmapNameToIdxMap.find(condense(aBitmapName));
	IconEntry anEntry;
	anEntry.bitmapID = aBitmapIDPtr ? *aBitmapIDPtr : 0;

	// If anEntry.bitmapID > 0, a bitmap name was found
	// If it wasn't separated by :, then full description was used, meaning
	// there must not be a rect description included
	if( aBitmapIDPtr && aBitmapName.empty() )
		aRectDesc.clear();

	// If separated by : (has aBitmapName) yet none found, something is wrong
	if( anEntry.bitmapID == 0 && !aBitmapName.empty() )
	{
		logError("Could not find a [%s] entry named '%s'!",
			kBitmapsPrefix,
			aBitmapName.c_str());
		return 0;
	}

	// aRectDesc should now be empty, or contain two hotspots
	EResult aHotspotParseResult = eResult_NotFound;
	if( !aRectDesc.empty() )
	{
		InputMap::profileStringToHotspot(aRectDesc, anEntry.pos);
		aHotspotParseResult =
			InputMap::profileStringToHotspot(aRectDesc, anEntry.size);
	}

	if( anEntry.bitmapID > 0 )
	{
		BITMAP bm;
		DBG_ASSERT(anEntry.bitmapID < sBitmaps.size());
		GetObject(sBitmaps[anEntry.bitmapID], sizeof(bm), &bm);
		if( aHotspotParseResult == eResult_Ok )
		{// When using a bitmap, pre-convert relative coordinates to pixels
			SIZE aBitmapSize;
			// Hotspot to pixel conversions are endpoint-exclusive so add +1
			aBitmapSize.cx = bm.bmWidth + 1;
			aBitmapSize.cy = bm.bmHeight + 1;
			hotspotToPixelsOnly(anEntry.pos, aBitmapSize);
			hotspotToPixelsOnly(anEntry.size, aBitmapSize);
		}
		else if( aHotspotParseResult == eResult_NotFound )
		{
			// Just bitmap specified, assume draw entire size
			anEntry.pos = Hotspot();
			anEntry.size = Hotspot();
			anEntry.size.x.offset = bm.bmWidth;
			anEntry.size.y.offset = bm.bmHeight;
		}
		else
		{
			// Bitmap + rect specified yet rect didn't parse properly
			// aRectDesc is modified during parse, so re-create it for error
			aRectDesc = theIconDescription;
			breakOffItemBeforeChar(aRectDesc, ':');
			logError("Could not decipher bitmap source rect '%s'",
				aRectDesc.c_str());
			return 0;
		}
	}
	else if( aHotspotParseResult != eResult_Ok )
	{
		logError("Could not decipher bitmap/icon description '%s'",
			theIconDescription.c_str());
		return 0;
	}
	// else if( anEntry.bitmapID == 0 && aHotspotParseResult == eResult_Ok )
	// .bitmapID == 0 flags to copy region of target game window at runtime,
	// and region stays in the form of Hotspot::Coord for now to retain
	// possible target-size-relative coordinates.

	// Check if is a duplicate icon
	for(u16 i = 0; i < sIcons.size(); ++i)
	{
		if( sIcons[i].bitmapID == anEntry.bitmapID &&
			sIcons[i].pos == anEntry.pos &&
			sIcons[i].size == anEntry.size )
		{
			return i;
		}
	}

	sIcons.push_back(anEntry);
	return u16(sIcons.size()-1);
}


static void drawHUDIcon(HUDDrawData& dd, u16 theIconID, const RECT& theRect)
{
	DBG_ASSERT(theIconID < sIcons.size());
	const IconEntry& anIcon = sIcons[theIconID];
	const u16 aBitmapID = anIcon.bitmapID;
	if( aBitmapID > 0 )
	{
		// Draw bitmap read in from file
		DBG_ASSERT(aBitmapID < sBitmaps.size());
		HBITMAP aBitmap = sBitmaps[aBitmapID];
		DBG_ASSERT(sBitmapDrawSrc);
		HBITMAP hOldBitmap = (HBITMAP)SelectObject(sBitmapDrawSrc, aBitmap);

		StretchBlt(dd.hdc,
				   theRect.left, theRect.top,
				   theRect.right - theRect.left,
				   theRect.bottom - theRect.top,
				   sBitmapDrawSrc,
				   anIcon.pos.x.offset, anIcon.pos.y.offset,
				   anIcon.size.x.offset, anIcon.size.y.offset,
				   SRCCOPY);

		SelectObject(sBitmapDrawSrc, hOldBitmap);
	}
	else
	{
		// Copy region of target app window
		// TODO
	}
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

	int aRadius = hi.radius;
	aRadius = min(aRadius, (theRect.right-theRect.left) * 3 / 4);
	aRadius = min(aRadius, (theRect.bottom-theRect.top) * 3 / 4);

	RoundRect(dd.hdc,
		theRect.left, theRect.top,
		theRect.right, theRect.bottom,
		aRadius, aRadius);
}


static void drawHUDBitmap(HUDDrawData& dd, const RECT& theRect)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	if( hi.iconID == 0 )
	{
		drawHUDRect(dd, theRect);
		return;
	}
	drawHUDIcon(dd, hi.iconID, theRect);
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


static void drawMenuTitle(
	HUDDrawData& dd,
	u16 theSubMenuID,
	StringScaleCacheEntry& theCacheEntry,
	bool centered = false)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	RECT aTitleRect = { 0, 0, dd.targetSize.cx, hi.titleHeight };

	if( !dd.firstDraw )
		FillRect(dd.hdc, &aTitleRect, sBrushes[hi.eraseBrushID]);
	InflateRect(&aTitleRect, -2, -2);
	const std::wstring& aStr = widen(InputMap::menuLabel(theSubMenuID));
	UINT aFormat = DT_WORDBREAK | DT_BOTTOM;
	if( centered) aFormat |= DT_CENTER;
	if( theCacheEntry.width == 0 )
		initStringCacheEntry(dd, aTitleRect, aStr, aFormat, theCacheEntry);

	// Fill in 2px margin around text with titleBG (border) color
	RECT aBGRect;
	LONG aSize = theCacheEntry.width + 4;
	aBGRect.left = centered ? ((dd.targetSize.cx - aSize) / 2) : 0;
	aBGRect.right = aBGRect.left + aSize;
	aSize = theCacheEntry.height + 4;
	aBGRect.bottom = hi.titleHeight;
	aBGRect.top = aBGRect.bottom - aSize;
	FillRect(dd.hdc, &aBGRect, sBrushes[hi.titleBrushID]);

	SetTextColor(dd.hdc, hi.titleColor);
	SetBkColor(dd.hdc, hi.titleBGColor);
	drawLabelString(dd, aTitleRect, aStr, aFormat, theCacheEntry);	
}


static void drawMenuItem(
	HUDDrawData& dd,
	const RECT& theRect,
	const std::string& theLabel,
	MenuDrawCacheEntry& theCacheEntry,
	bool selected = false)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];

	// Background (usually a bordered rectangle)
	if( selected )
	{
		swap(hi.itemBrushID, hi.selItemBrushID);
		swap(hi.borderPenID, hi.selBorderPenID);
		swap(hi.iconID, hi.selIconID);
	}
	drawHUDItem(dd, theRect);
	if( selected )
	{
		swap(hi.itemBrushID, hi.selItemBrushID);
		swap(hi.borderPenID, hi.selBorderPenID);
		swap(hi.iconID, hi.selIconID);
	}

	// Label (usually word-wrapped and centered text)
	RECT aLabelRect = theRect;
	InflateRect(&aLabelRect, -hi.borderSize - 1, -hi.borderSize - 1);
	SetTextColor(dd.hdc, selected ? hi.selLabelColor : hi.labelColor);
	SetBkColor(dd.hdc, selected ? hi.selLabelBGColor : hi.labelBGColor);
	drawLabelString(dd, aLabelRect, widen(theLabel),
		DT_WORDBREAK | DT_CENTER | DT_VCENTER,
		theCacheEntry.str);
}


static void drawListMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aPrevSubMenuID = hi.subMenuID;
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	DBG_ASSERT(aSubMenuID < sMenuDrawCache.size());
	const u16 aPrevSelection = hi.selection;
	const u16 aSelection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(aSelection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[aSubMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && (dd.firstDraw || aSubMenuID != aPrevSubMenuID) )
		drawMenuTitle(dd, aSubMenuID, sMenuDrawCache[aSubMenuID][0].str);

	RECT anItemRect = { 0 };
	RECT aSelectedItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight;
	anItemRect.bottom = hi.titleHeight + dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		// Don't need to re-draw menu items that haven't changed their
		// "selected" status or label (sub-menu) after firstDraw done,
		// unless they overlap each other due to negative gapSize
		if( dd.firstDraw || aSubMenuID != aPrevSubMenuID || hi.gapSize < 0 ||
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
			if( itemIdx == aSelection && hi.gapSize < 0 )
			{// Make sure selection is drawn on top of other items
				aSelectedItemRect = anItemRect;
			}
			else
			{
				drawMenuItem(dd, anItemRect,
					InputMap::menuItemLabel(aSubMenuID, itemIdx),
					sMenuDrawCache[aSubMenuID][itemIdx + hasTitle],
					itemIdx == aSelection);
			}
		}
		anItemRect.top = anItemRect.bottom + hi.gapSize;
		anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	}

	// Draw selected menu item last
	if( aSelectedItemRect.right > aSelectedItemRect.left )
	{
		drawMenuItem(dd, aSelectedItemRect,
			InputMap::menuItemLabel(aSubMenuID, aSelection),
			sMenuDrawCache[aSubMenuID][aSelection + hasTitle], true);
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
	const u16 aSelection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(aSelection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[aSubMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && (dd.firstDraw || aSubMenuID != aPrevSubMenuID) )
		drawMenuTitle(dd, aSubMenuID, sMenuDrawCache[aSubMenuID][0].str);

	// Draw in a wrapping fashion, starting with aSelection+1 being drawn just
	// below the top slot, and ending when draw aSelection last at the top
	RECT anItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight + dd.itemSize.cy + hi.gapSize;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	for(u16 itemIdx = (aSelection + 1) % anItemCount;
		true; itemIdx = (itemIdx + 1) % anItemCount)
	{
		const bool isSelection = itemIdx == aSelection;
		if( isSelection )
		{
			anItemRect.top = hi.titleHeight;
			anItemRect.bottom = dd.itemSize.cy;
		}
		if( !dd.firstDraw &&
			aSubMenuID != aPrevSubMenuID &&
			hi.itemType != eHUDItemType_Rect )
		{
			FillRect(dd.hdc, &anItemRect, sBrushes[hi.eraseBrushID]);
		}
		drawMenuItem(dd, anItemRect,
			InputMap::menuItemLabel(aSubMenuID, itemIdx),
			sMenuDrawCache[aSubMenuID][itemIdx + hasTitle],
			isSelection);
		if( isSelection )
			break;
		anItemRect.top = anItemRect.bottom + hi.gapSize;
		anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	}

	hi.subMenuID = aSubMenuID;
	hi.selection = aSelection;
}


static void drawBarMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aPrevSubMenuID = hi.subMenuID;
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	DBG_ASSERT(aSubMenuID < sMenuDrawCache.size());
	const u16 aPrevSelection = hi.selection;
	const u16 aSelection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	DBG_ASSERT(aSelection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[aSubMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && (dd.firstDraw || aSubMenuID != aPrevSubMenuID) )
		drawMenuTitle(dd, aSubMenuID, sMenuDrawCache[aSubMenuID][0].str);

	RECT anItemRect = { 0 };
	RECT aSelectedItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight;
	anItemRect.bottom = hi.titleHeight + dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( dd.firstDraw || aSubMenuID != aPrevSubMenuID || hi.gapSize < 0 ||
			itemIdx == aPrevSelection || itemIdx == aSelection )
		{
			if( !dd.firstDraw &&
				aSubMenuID != aPrevSubMenuID &&
				hi.itemType != eHUDItemType_Rect )
			{
				FillRect(dd.hdc, &anItemRect, sBrushes[hi.eraseBrushID]);
			}
			if( itemIdx == aSelection && hi.gapSize < 0 )
			{// Make sure selection is drawn on top of other items
				aSelectedItemRect = anItemRect;
			}
			else
			{
				drawMenuItem(dd, anItemRect,
					InputMap::menuItemLabel(aSubMenuID, itemIdx),
					sMenuDrawCache[aSubMenuID][itemIdx + hasTitle],
					itemIdx == aSelection);
			}
		}
		anItemRect.left = anItemRect.right + hi.gapSize;
		anItemRect.right = anItemRect.left + dd.itemSize.cx;
	}

	// Draw selected menu item last
	if( aSelectedItemRect.right > aSelectedItemRect.left )
	{
		drawMenuItem(dd, aSelectedItemRect,
			InputMap::menuItemLabel(aSubMenuID, aSelection),
			sMenuDrawCache[aSubMenuID][aSelection + hasTitle], true);
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
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[aSubMenuID].resize(eCmdDir_Num + hasTitle);

	if( !dd.firstDraw && (hi.itemType != eHUDItemType_Rect || hi.gapSize < 0) )
	{
		FillRect(dd.hdc, &dd.targetRect, sBrushes[hi.eraseBrushID]);
		// Since erased entire thing now, treat as firstDraw from now on
		dd.firstDraw = true;
	}

	if( hasTitle )
	{
		drawMenuTitle(dd, aSubMenuID,
			sMenuDrawCache[aSubMenuID][eCmdDir_Num].str, true);
	}

	// Left
	RECT anItemRect;
	anItemRect.left = 0;
	anItemRect.top = hi.titleHeight + dd.itemSize.cy + hi.gapSize;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Left),
		sMenuDrawCache[aSubMenuID][eCmdDir_Left]);
	// Right
	anItemRect.left += dd.itemSize.cx + hi.gapSize;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Right),
		sMenuDrawCache[aSubMenuID][eCmdDir_Right]);
	// Up
	anItemRect.left -= dd.itemSize.cx / 2 + hi.gapSize / 2;
	anItemRect.top = hi.titleHeight;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Up),
		sMenuDrawCache[aSubMenuID][eCmdDir_Up]);
	// Down
	anItemRect.top += dd.itemSize.cy * 2 + hi.gapSize * 2;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuDirLabel(aSubMenuID, eCmdDir_Down),
		sMenuDrawCache[aSubMenuID][eCmdDir_Down]);
}


static void drawGridMenu(HUDDrawData& dd)
{
	HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const u16 aPrevSubMenuID = hi.subMenuID;
	const u16 aMenuID = InputMap::menuForHUDElement(dd.hudElementID);
	const u16 aSubMenuID = Menus::activeSubMenu(aMenuID);
	DBG_ASSERT(aSubMenuID < sMenuDrawCache.size());
	const u16 aPrevSelection = hi.selection;
	const u16 aSelection = Menus::selectedItem(aMenuID);
	const u16 anItemCount = Menus::itemCount(aMenuID);
	const u16 aGridWidth = Menus::gridWidth(aMenuID);
	DBG_ASSERT(aSelection < anItemCount);
	const u8 hasTitle = hi.titleHeight > 0 ? 1 : 0;
	sMenuDrawCache[aSubMenuID].resize(anItemCount + hasTitle);

	if( hasTitle && (dd.firstDraw || aSubMenuID != aPrevSubMenuID) )
		drawMenuTitle(dd, aSubMenuID, sMenuDrawCache[aSubMenuID][0].str);

	RECT anItemRect = { 0 };
	RECT aSelectedItemRect = { 0 };
	anItemRect.right = dd.itemSize.cx;
	anItemRect.top = hi.titleHeight;
	anItemRect.bottom = hi.titleHeight + dd.itemSize.cy;
	for(u16 itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( dd.firstDraw || aSubMenuID != aPrevSubMenuID ||
			itemIdx == aPrevSelection || itemIdx == aSelection )
		{
			if( !dd.firstDraw &&
				aSubMenuID != aPrevSubMenuID &&
				hi.itemType != eHUDItemType_Rect )
			{
				FillRect(dd.hdc, &anItemRect, sBrushes[hi.eraseBrushID]);
			}
			if( itemIdx == aSelection && hi.gapSize < 0 )
			{// Make sure selection is drawn on top of other items
				aSelectedItemRect = anItemRect;
			}
			else
			{
				drawMenuItem(dd, anItemRect,
					InputMap::menuItemLabel(aSubMenuID, itemIdx),
					sMenuDrawCache[aSubMenuID][itemIdx + hasTitle],
					itemIdx == aSelection);
			}
		}
		if( itemIdx % aGridWidth == aGridWidth - 1 )
		{// Next menu item is left edge and one down
			anItemRect.left = 0;
			anItemRect.right = dd.itemSize.cx;
			anItemRect.top = anItemRect.bottom + hi.gapSize;
			anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
		}
		else
		{// Next menu item is to the right
			anItemRect.left = anItemRect.right + hi.gapSize;
			anItemRect.right = anItemRect.left + dd.itemSize.cx;
		}
	}

	// Draw selected menu item last
	if( aSelectedItemRect.right > aSelectedItemRect.left )
	{
		drawMenuItem(dd, aSelectedItemRect,
			InputMap::menuItemLabel(aSubMenuID, aSelection),
			sMenuDrawCache[aSubMenuID][aSelection + hasTitle], true);
	}

	hi.subMenuID = aSubMenuID;
	hi.selection = aSelection;
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
	aHUDBuilder.bitmapNameToIdxMap.setValue("", 0);
	sIcons.push_back(IconEntry());

	// Get default erase (transparent) color value
	const COLORREF aDefaultTransColor = strToRGB(aHUDBuilder,
					getHUDPropStr("", eHUDProp_TransColor));
	const u16 aDefaultEraseBrush =
		getOrCreateBrushID(aHUDBuilder, aDefaultTransColor);

	// Create Bitmaps and load bitmap images
	Profile::KeyValuePairs aBitmapRequests;
	Profile::getAllKeys(std::string(kBitmapsPrefix) + "/", aBitmapRequests);
	for(size_t i = 0; i < aBitmapRequests.size(); ++i)
	{
		std::string aBitmapName = aBitmapRequests[i].first;
		std::string aBitmapPath = aBitmapRequests[i].second;
		loadBitmap(aHUDBuilder, aBitmapName, aBitmapPath);
	}

	// Get information for each HUD Element from Profile
	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
		hi.type = InputMap::hudElementType(aHUDElementID);
		hi.transColor = aDefaultTransColor;
		hi.eraseBrushID = aDefaultEraseBrush;
		if( hi.type == eHUDType_System )
			continue;
		const std::string& aHUDName = InputMap::hudElementLabel(aHUDElementID);
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
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Position),
			hi.position);
		// hi.itemSize = eHUDProp_ItemSize (Menus) or eHUDProp_Size (HUD)
		const bool isAMenu =
			hi.type >= eMenuStyle_Begin && hi.type < eMenuStyle_End;
		if( isAMenu )
			aStr = getHUDPropStr(aHUDName, eHUDProp_ItemSize, true);
		else
			aStr = getHUDPropStr(aHUDName, eHUDProp_Size, true);
		if( aStr.empty() && isAMenu )
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
		// hi.labelColor = eHUDProp_FontColor
		hi.labelColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontColor));
		// hi.itemBrushID & .labelBGColor = eHUDProp_ItemColor
		hi.labelBGColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_ItemColor));
		hi.itemBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.labelBGColor);
		// hi.titleBGColor/BrushID = eHUDProp_BorderColor
		hi.titleBGColor = strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BorderColor));
		hi.titleBrushID = getOrCreateBrushID(aHUDBuilder, hi.titleBGColor);
		// hi.selLabelColor = eHUDProp_SFontColor
		hi.selLabelColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_SFontColor));
		// hi.selItemBrushID & .setLabelBGColor = eHUDProp_SItemColor
		hi.selLabelBGColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_SItemColor));
		hi.selItemBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.selLabelBGColor);
		// hi.titleColor = eHUDProp_TitleColor
		hi.titleColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_TitleColor));
		// hi.transColor & .eraseBrushID = eHUDProp_TransColor
		hi.transColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_TransColor));
		hi.eraseBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.transColor);
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

		// Extra data values for specific types
		if( hi.type == eHUDItemType_Bitmap ||
			hi.itemType == eHUDItemType_Bitmap )
		{
			hi.iconID = createIconID(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_Bitmap));
			aStr = getHUDPropStr(aHUDName, eHUDProp_SelBitmap);
			if( aStr.empty() )
				hi.selIconID = hi.iconID;
			else
				hi.selIconID = createIconID(aHUDBuilder, aStr);
		}
		if( hi.type == eHUDType_KBArrayLast ||
			hi.type == eHUDType_KBArrayDefault )
		{
			hi.arrayID = InputMap::keyBindArrayForHUDElement(aHUDElementID);
			DBG_ASSERT(hi.arrayID < InputMap::keyBindArrayCount());
		}
	}

	updateScaling();

	// Trim unused memory
	if( sFonts.size() < sFonts.capacity() )
		std::vector<HFONT>(sFonts).swap(sFonts);
	if( sBrushes.size() < sBrushes.capacity() )
		std::vector<HBRUSH>(sBrushes).swap(sBrushes);
	if( sPens.size() < sPens.capacity() )
		std::vector<HPEN>(sPens).swap(sPens);
	if( sBitmaps.size() < sBitmaps.capacity() )
		std::vector<HBITMAP>(sBitmaps).swap(sBitmaps);
	if( sIcons.size() < sIcons.capacity() )
		std::vector<IconEntry>(sIcons).swap(sIcons);
	if( sHUDElementInfo.size() < sHUDElementInfo.capacity() )
		std::vector<HUDElementInfo>(sHUDElementInfo).swap(sHUDElementInfo);
}


void cleanup()
{
	DeleteDC(sBitmapDrawSrc);
	sBitmapDrawSrc = NULL;
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
	sIcons.clear();
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
	}
	gKeyBindArrayLastIndexChanged.reset();
	gKeyBindArrayDefaultIndexChanged.reset();
}


void updateScaling()
{
	if( sHUDElementInfo.empty() )
		return;

	// Reset string auto-sizing cache entries
	for(size_t i = 0; i < sMenuDrawCache.size(); ++i)
	{
		for(size_t j = 0; j < sMenuDrawCache[i].size(); ++j)
			sMenuDrawCache[i][j].str = StringScaleCacheEntry();
	}
	sResizedFontsMap.clear();

	// Clear fonts and border pens
	for(size_t i = 0; i < sFonts.size(); ++i)
		DeleteObject(sFonts[i]);
	for(size_t i = 0; i < sPens.size(); ++i)
		DeleteObject(sPens[i]);
	sFonts.clear();
	sPens.clear();

	// Generate fonts and border pens based on current gScale values
	HUDBuilder aHUDBuilder;
	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
		if( hi.type == eHUDType_System )
			continue;
		const std::string& aHUDName = InputMap::hudElementLabel(aHUDElementID);
		const bool isAMenu =
			hi.type >= eMenuStyle_Begin && hi.type < eMenuStyle_End;
		// hi.fontID = eHUDProp_FontName & _FontSize & _FontWeight
		hi.fontID = getOrCreateFontID(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontName),
			getHUDPropStr(aHUDName, eHUDProp_FontSize),
			getHUDPropStr(aHUDName, eHUDProp_FontWeight));
		// hi.borderSize = eHUDProp_BorderSize
		hi.borderSize =
			u32FromString(getHUDPropStr(aHUDName, eHUDProp_BorderSize));
		if( hi.borderSize > 0 )
			hi.borderSize = max(1, hi.borderSize * gUIScaleY);
		// hi.borderPenID
		hi.borderPenID = getOrCreatePenID(aHUDBuilder,
			hi.titleBGColor,
			hi.borderSize ? PS_INSIDEFRAME : PS_NULL, int(hi.borderSize));
		// hi.selBorderPenID = eHUDProp_SBorderColor (and +1 to size)
		hi.selBorderPenID = getOrCreatePenID(aHUDBuilder,
			strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_SBorderColor)),
			PS_INSIDEFRAME, int(hi.borderSize + max(1, gUIScaleY)));
		if( isAMenu )
		{
			// hi.gapSize = eHUDProp_GapSize
			hi.gapSize = gUIScaleY *
				intFromString(getHUDPropStr(aHUDName, eHUDProp_GapSize));
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
			hi.radius = gUIScaleY * u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_Radius));
		}
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
	if( !sBitmapDrawSrc )
		sBitmapDrawSrc = CreateCompatibleDC(hdc);

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
		if( Hotspot* aHotspot =
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
				theWindowSize.cy += hi.gapSize * (aMenuItemCount - 1);
		}
		break;
	case eMenuStyle_Bar:
		{
			const u16 aMenuItemCount = Menus::itemCount(aMenuID);
			theWindowSize.cx *= aMenuItemCount;
			if( aMenuItemCount > 1 )
				theWindowSize.cx += hi.gapSize * (aMenuItemCount - 1);
		}
		break;
	case eMenuStyle_4Dir:
		theWindowSize.cx = theWindowSize.cx * 2 + hi.gapSize;
		theWindowSize.cy = theWindowSize.cy * 3 + hi.gapSize * 2;
		break;
	case eMenuStyle_Grid:
		{
			const u8 aMenuItemXCount = Menus::gridWidth(aMenuID);
			const u8 aMenuItemYCount = Menus::gridHeight(aMenuID);
			theWindowSize.cx *= aMenuItemXCount;
			theWindowSize.cy *= aMenuItemYCount;
			if( aMenuItemXCount > 1 )
				theWindowSize.cx += hi.gapSize * (aMenuItemXCount-1);
			if( aMenuItemYCount > 1 )
				theWindowSize.cy += hi.gapSize * (aMenuItemYCount-1);
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
	case eHUDType_KBArrayLast:
		return true;
	}

	return false;
}

} // HUD
