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
	eHUDProp_GapSize,
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
	{	"GapSize",			"4"				},	// eHUDProp_GapSize
	{	"Radius",			"4"				},	// eHUDProp_Radius
};
DBG_CTASSERT(ARRAYSIZE(kHUDPropStr) == eHUDProp_Num);


//-----------------------------------------------------------------------------
// HUDElementInfo
//-----------------------------------------------------------------------------

struct HUDElementInfo
	: public ConstructFromZeroInitializedMemory<HUDElementInfo>
{
	EHUDType type;
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
	u16 fontID;
	u16 itemBrushID;
	u16 borderPenID;
	u16 eraseBrushID;
	u8 alignmentX;
	u8 alignmentY;
	u8 maxAlpha;
	u8 inactiveAlpha;

	union
	{
		int extraData;
		int gapSize;
		int radius;
	};

	HUDElementInfo() :
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
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<HFONT> sFonts;
static std::vector<HBRUSH> sBrushes;
static std::vector<HPEN> sPens;
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


LONG scaleHotspot(
	const Hotspot::Coord& theCoord,
	u16 theTargetSize)
{
	return
		LONG(theCoord.origin) *
		theTargetSize / 0x10000 +
		theCoord.offset;
}

	
static void drawMenuItem(
	const HUDDrawData& dd,
	RECT theItemRect,
	const std::string& theLabel)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;

	// Bordered rectangle
	Rectangle(hdc,
		theItemRect.left, theItemRect.top,
		theItemRect.right, theItemRect.bottom);

	if( !theLabel.empty() )
	{// Text - word-wrapped + horizontal & vertical center justification
		const std::wstring& aLabelW = widen(theLabel);
		RECT aTextRect = theItemRect;
		aTextRect.bottom = aTextRect.top;
		const int aTextHeight = DrawText(hdc, aLabelW.c_str(),
			-1, &aTextRect, DT_WORDBREAK | DT_CENTER | DT_CALCRECT);
		aTextRect.top += (theItemRect.bottom - theItemRect.top - aTextHeight) / 2;
		aTextRect.right = theItemRect.right;
		aTextRect.bottom = theItemRect.bottom;
		DrawText(hdc, aLabelW.c_str(), -1, &aTextRect, DT_WORDBREAK | DT_CENTER);
	}
}


static void draw4DirMenu(HUDDrawData& dd)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;
	DBG_ASSERT(hi.type == eMenuStyle_4Dir);

	// Always just draws all 4 items on top of previous,
	// so no need to erase anything first

	SelectObject(hdc, sFonts[hi.fontID]);
	SelectObject(hdc, sPens[hi.borderPenID]);
	SelectObject(hdc, sBrushes[hi.itemBrushID]);
	SetBkColor(hdc, hi.itemColor);
	SetTextColor(hdc, hi.labelColor);

	// Always 4 menu items, in order of L->R->U->D
	const u16 activeSubMenu = Menus::activeSubMenu(aHUDElementID);
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


static void drawRectHUD(HUDDrawData& dd)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;
	DBG_ASSERT(hi.type == eHUDType_Rectangle);

	SelectObject(hdc, sPens[hi.borderPenID]);
	SelectObject(hdc, sBrushes[hi.itemBrushID]);

	Rectangle(hdc, 0, 0, dd.targetSize.cx, dd.targetSize.cy);
}


static void drawRoundRectHUD(HUDDrawData& dd)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;
	DBG_ASSERT(hi.type == eHUDType_RoundRect);

	SelectObject(hdc, sPens[hi.borderPenID]);
	SelectObject(hdc, sBrushes[hi.itemBrushID]);

	RoundRect(hdc, 0, 0, dd.targetSize.cx, dd.targetSize.cy, hi.radius, hi.radius);
}


static void drawCircleHUD(HUDDrawData& dd)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;
	DBG_ASSERT(hi.type == eHUDType_Circle);

	SelectObject(hdc, sPens[hi.borderPenID]);
	SelectObject(hdc, sBrushes[hi.itemBrushID]);

	Ellipse(hdc, 0, 0, dd.targetSize.cx, dd.targetSize.cy);
}


static void drawCrossHUD(HUDDrawData& dd)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;
	DBG_ASSERT(hi.type == eHUDType_Crosshair);

	SelectObject(hdc, sPens[hi.borderPenID]);
	SelectObject(hdc, sBrushes[hi.itemBrushID]);

	const LONG aHalfGapSize = hi.gapSize / 2;
	const LONG aLength = dd.itemSize.cx;
	const LONG aThickness = dd.itemSize.cy;
	const LONG anOffset1 = aLength + aHalfGapSize;
	const LONG anOffset2 = anOffset1 + aThickness + aHalfGapSize;

	// Left hair
	Rectangle(hdc, 0, anOffset1, aLength + 1, anOffset1 + aThickness + 1);
	// Right hair
	Rectangle(hdc, anOffset2, anOffset1,
		anOffset2 + aLength + 1, anOffset1 + aThickness + 1);
	// Top hair
	Rectangle(hdc, anOffset1, 0, anOffset1 + aThickness + 1, aLength + 1);
	// Bottom hair
	Rectangle(hdc, anOffset1, anOffset2,
		anOffset1 + aThickness + 1, anOffset2 + aLength + 1);
}


static void drawSystemHUD(HUDDrawData& dd)
{
	const u16 aHUDElementID = dd.hudElementID;
	const HUDElementInfo& hi = sHUDElementInfo[aHUDElementID];
	HDC hdc = dd.hdc;
	DBG_ASSERT(hi.type == eHUDType_System);

	// Erase any previous strings
	if( !dd.firstDraw )
		FillRect(hdc, &dd.targetRect, sBrushes[hi.eraseBrushID]);

	RECT aTextRect = dd.targetRect;
	InflateRect(&aTextRect, -8, -8);

	if( !sErrorMessage.empty() )
	{
		SetBkColor(hdc, RGB(255, 0, 0));
		SetTextColor(hdc, RGB(255, 255, 0));
		DrawText(hdc, sErrorMessage.c_str(), -1, &aTextRect,
			DT_WORDBREAK);
	}

	if( !sNoticeMessage.empty() )
	{
		SetBkColor(hdc, RGB(0, 0, 255));
		SetTextColor(hdc, RGB(255, 255, 255));
		DrawText(hdc, sNoticeMessage.c_str(), -1, &aTextRect,
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
		hi.transColor = aDefaultTransColor;
		hi.eraseBrushID = aDefaultEraseBrush;
		if( hi.type == eHUDType_System )
			continue;
		const std::string& aHUDName = InputMap::hudElementLabel(aHUDElementID);
		// hi.position = eHUDProp_Position
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Position),
			hi.position);
		// hi.itemSize = eHUDProp_ItemSize (Menus) or eHUDProp_Size (HUD)
		std::string aStr;
		if( hi.type < eMenuStyle_Num )
			aStr = getHUDPropStr(aHUDName, eHUDProp_ItemSize, true);
		else
			aStr = getHUDPropStr(aHUDName, eHUDProp_Size, true);
		if( aStr.empty() && hi.type < eMenuStyle_Num )
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
		// hi.borderPenID = eHUDProp_BorderColor & _BorderSize
		u32 aVal = u32FromString(getHUDPropStr(aHUDName, eHUDProp_BorderSize));
		hi.borderPenID = getOrCreatePenID(aHUDBuilder,
			strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BorderColor)),
			aVal ? PS_INSIDEFRAME : PS_NULL, int(aVal));
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
		hi.inactiveAlpha = u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_InactiveAlpha)) & 0xFF;

		// Extra data values for specific types
		switch(hi.type)
		{
		case eHUDType_Crosshair:
			hi.gapSize = u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_GapSize));
			break;
		case eHUDType_RoundRect:
			hi.radius = u32FromString(
				getHUDPropStr(aHUDName, eHUDProp_Radius));
			break;
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
	sFonts.clear();
	sBrushes.clear();
	sPens.clear();
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

	switch(sHUDElementInfo[theHUDElementID].type)
	{
	case eMenuStyle_4Dir:		draw4DirMenu(aDrawData);		break;
	case eHUDType_Rectangle:	drawRectHUD(aDrawData);			break;
	case eHUDType_RoundRect:	drawRoundRectHUD(aDrawData);	break;
	case eHUDType_Circle:		drawCircleHUD(aDrawData);		break;
	case eHUDType_Crosshair:	drawCrossHUD(aDrawData);		break;
	case eHUDType_System:		drawSystemHUD(aDrawData);		break;
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

	// Calculate total window size needed based on type and component size
	theWindowSize = theComponentSize;
	switch(hi.type)
	{
	case eMenuStyle_4Dir:
		theWindowSize.cx *= 2;
		theWindowSize.cy *= 3;
		break;
	case eHUDType_Crosshair:
		// Treat X as length and Y as thickness of each "hair"
		theWindowSize.cx = theWindowSize.cy = 1 +
			theComponentSize.cx * 2 + theComponentSize.cy +
			(hi.gapSize / 2) * 2; // even number only gap size
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

} // HUD
