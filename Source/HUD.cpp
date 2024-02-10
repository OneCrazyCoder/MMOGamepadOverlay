//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "InputMap.h" // labels, profileStringToHotspot()
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
kNoticeStringDisplayTimePerChar = 50,
kNoticeStringMinTime = 3000,
};

const char* kMenuPrefix = "Menu";
const char* kHUDPrefix = "HUD";

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
	eHUDProp_TransColor,
	eHUDProp_MaxAlpha,
	eHUDProp_FadeInDelay,
	eHUDProp_FadeInTime,
	eHUDProp_FadeOutDelay,
	eHUDProp_FadeOutTime,
	eHUDProp_InactiveDelay,
	eHUDProp_InactiveAlpha,
	eHUDProp_BitmapPath,
	eHUDProp_Radius,

	eHUDProp_Num
};

enum EAlignment
{
	eAlignment_Min,		// L or T
	eAlignment_Center,	// CX or CY
	eAlignment_Max,		// R or B
};

const char* kHUDPropStr[][2] =
{//		Key					Default value
	{	"Position",			"0, 0"			},	// eHUDProp_Position
	{	"ItemType",			""				},	// eHUDProp_ItemType
	{	"ItemSize",			"55, 55"		},	// eHUDProp_ItemSize
	{	"Size",				""				},	// eHUDProp_Size
	{	"Alignment",		"L, T"			},	// eHUDProp_Alignment
	{	"Font",				"Verdana"		},	// eHUDProp_FontName
	{	"FontSize",			"13"			},	// eHUDProp_FontSize
	{	"FontWeight",		"400"			},	// eHUDProp_FontWeight
	{	"LabelRGB",			"10, 10, 10"	},	// eHUDProp_FontColor
	{	"ItemRGB",			"150, 150, 150"	},	// eHUDProp_ItemColor
	{	"BorderRGB",		"100, 100, 100"	},	// eHUDProp_BorderColor
	{	"BorderSize",		"1"				},	// eHUDProp_BorderSize
	{	"TransRGB",			"255, 0, 255"	},	// eHUDProp_TransColor
	{	"MaxAlpha",			"255"			},	// eHUDProp_MaxAlpha
	{	"FadeInDelay",		"0"				},	// eHUDProp_FadeInDelay
	{	"FadeInTime",		"0"				},	// eHUDProp_FadeInTime
	{	"FadeOutDelay",		"0"				},	// eHUDProp_FadeOutDelay
	{	"FadeOutTime",		"0"				},	// eHUDProp_FadeOutTime
	{	"InactiveDelay",	"0"				},	// eHUDProp_InactiveDelay
	{	"InactiveAlpha",	"150"			},	// eHUDProp_InactiveAlpha
	{	"BitmapPath",		""				},	// eHUDProp_BitmapPath
	{	"Radius",			"20"			},	// eHUDProp_Radius
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
	COLORREF itemColor;
	COLORREF transColor;
	Hotspot position;
	Hotspot itemSize;
	float fadeInRate;
	float fadeOutRate;
	int fadeInDelay;
	int fadeOutDelay;
	int delayUntilInactive;
	int selection;
	u16 fontID;
	u16 itemBrushID;
	u16 borderPenID;
	u16 eraseBrushID;
	u16 bitmapID;
	u16 borderSize;
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

	HUDDrawData(
		HDC hdc,
		SIZE itemSize,
		SIZE targetSize,
		u16 hudElementID,
		bool firstDraw)
		:
		hdc(hdc),
		itemSize(itemSize),
		targetSize(targetSize),
		hudElementID(hudElementID),
		firstDraw(firstDraw)
	{
		SetRect(&targetRect, 0, 0, targetSize.cx, targetSize.cy);
	}
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
		aKey += kHUDPropStr[theProperty][0];
		result = Profile::getStr(aKey);
		if( !result.empty() )
			return result;

		// Try [HUD.Name] next
		aKey = kHUDPrefix;
		aKey += "."; aKey += theName + "/";
		aKey += kHUDPropStr[theProperty][0];
		result = Profile::getStr(aKey);
		if( !result.empty() )
			return result;
	}

	if( namedOnly )
		return result;

	// Try just [HUD] for a default value
	aKey = kHUDPrefix;
	aKey += "/";
	aKey += kHUDPropStr[theProperty][0];
	result = Profile::getStr(aKey);
	if( !result.empty() )
		return result;

	// Maybe just [Menu] for default value?
	aKey = kMenuPrefix;
	aKey += "/";
	aKey += kHUDPropStr[theProperty][0];
	result = Profile::getStr(aKey);
	if( !result.empty() )
		return result;

	// Return hard-coded default value
	result = kHUDPropStr[theProperty][1];
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
	HFONT aFont = CreateFont(
		intFromString(theFontSize), // Height of the font
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


static void drawMenuItem(
	HUDDrawData& dd,
	const RECT& theRect,
	const std::string& theLabel)
{
	// Backdrop (usually bordered rectangle)
	drawHUDItem(dd, theRect);

	if( theLabel.empty() )
		return;

	// Text - word-wrapped + horizontal & vertical center justification
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	const std::wstring& aLabelW = widen(theLabel);
	RECT aClipRect = theRect;
	InflateRect(&aClipRect, -hi.borderSize, -hi.borderSize);
	RECT aTextRect = aClipRect;
	aTextRect.bottom = aTextRect.top;
	const int aTextHeight = DrawText(dd.hdc, aLabelW.c_str(),
		-1, &aTextRect, DT_WORDBREAK | DT_CENTER | DT_CALCRECT);
	aTextRect.top += (aClipRect.bottom - aClipRect.top - aTextHeight) / 2;
	aTextRect.right = aClipRect.right;
	aTextRect.bottom = aClipRect.bottom;
	DrawText(dd.hdc, aLabelW.c_str(), -1,
		&aTextRect, DT_WORDBREAK | DT_CENTER);
}


static void draw4DirMenu(HUDDrawData& dd)
{
	const HUDElementInfo& hi = sHUDElementInfo[dd.hudElementID];
	DBG_ASSERT(hi.type == eMenuStyle_4Dir);

	if( !dd.firstDraw )
		FillRect(dd.hdc, &dd.targetRect, sBrushes[hi.eraseBrushID]);

	SelectObject(dd.hdc, sFonts[hi.fontID]);
	SelectObject(dd.hdc, sPens[hi.borderPenID]);
	SelectObject(dd.hdc, sBrushes[hi.itemBrushID]);
	SetBkColor(dd.hdc, hi.itemColor);
	SetTextColor(dd.hdc, hi.labelColor);

	// Always 4 menu items, in order of L->R->U->D
	const u16 activeSubMenu = Menus::activeSubMenu(dd.hudElementID);
	// Left
	RECT anItemRect;
	anItemRect.left = 0;
	anItemRect.top = dd.itemSize.cy;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 0));
	// Right
	anItemRect.left += dd.itemSize.cx;
	anItemRect.right += dd.itemSize.cx;
	drawMenuItem(dd, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 1));
	// Up
	anItemRect.left -= dd.itemSize.cx / 2;
	anItemRect.top = 0;
	anItemRect.right = anItemRect.left + dd.itemSize.cx;
	anItemRect.bottom = anItemRect.top + dd.itemSize.cy;
	drawMenuItem(dd, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 2));
	// Down
	anItemRect.top += dd.itemSize.cy * 2;
	anItemRect.bottom += dd.itemSize.cy * 2;
	drawMenuItem(dd, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 3));
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
		// hi.itemColor & .itemBrushID = eHUDProp_ItemColor
		hi.itemColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_ItemColor));
		hi.itemBrushID = getOrCreateBrushID(
			aHUDBuilder, hi.itemColor);
		// hi.borderPenID & .borderSize = eHUDProp_BorderColor & _BorderSize
		hi.borderSize =
			u32FromString(getHUDPropStr(aHUDName, eHUDProp_BorderSize));
		hi.borderPenID = getOrCreatePenID(aHUDBuilder,
			strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BorderColor)),
			hi.borderSize ? PS_INSIDEFRAME : PS_NULL, int(hi.borderSize));
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


void drawElement(
	HDC hdc,
	u16 theHUDElementID,
	const SIZE& theComponentSize,
	const SIZE& theDestSize,
	bool needsInitialErase)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& hi = sHUDElementInfo[theHUDElementID];

	HUDDrawData aDrawData(
		hdc,
		theComponentSize, theDestSize,
		theHUDElementID,
		needsInitialErase);

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
	case eMenuStyle_List:			/* TODO */					break;
	case eMenuStyle_4Dir:			draw4DirMenu(aDrawData);	break;
	case eMenuStyle_Grid:			/* TODO */					break;
	case eMenuStyle_Pillar:			/* TODO */					break;
	case eMenuStyle_Bar:			/* TODO */					break;
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
