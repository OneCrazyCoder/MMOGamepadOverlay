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
	eHUDProp_Alignment,
	eHUDProp_FontName,
	eHUDProp_FontSize,
	eHUDProp_FontWeight,
	eHUDProp_FontColor,
	eHUDProp_ItemColor,
	eHUDProp_BorderColor,
	eHUDProp_BorderSize,

	eHUDProp_Num
};

enum EAlignment
{
	eAlignment_Min,		// L or T
	eAlignment_Center,	// CX or CY
	eAlignment_Max,		// R or B
};

const char* kHUDPropStr[][2] =
{//		Key				Default value
	{	"Position",		"0, 0"			},	// eHUDProp_Position
	{	"ItemSize",		"55, 55"		},	// eHUDProp_ItemSize
	{	"Alignment",	"L, T"			},	// eHUDProp_Alignment
	{	"Font",			"Verdana"		},	// eHUDProp_FontName
	{	"FontSize",		"13"			},	// eHUDProp_FontSize
	{	"FontWeight",	"400"			},	// eHUDProp_FontWeight
	{	"LabelRGB",		"10, 10, 10"	},	// eHUDProp_FontColor
	{	"ItemRGB",		"150, 150, 150"	},	// eHUDProp_ItemColor
	{	"BorderRGB",	"100, 100, 100"	},	// eHUDProp_BorderColor
	{	"BorderSize",	"1"				},	// eHUDProp_BorderSize
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
	Hotspot position;
	Hotspot itemSize;
	u16 fontID;
	u16 itemBrushID;
	u16 borderPenID;
	u8 alignmentX;
	u8 alignmentY;
};


//-----------------------------------------------------------------------------
// Other Local Structures
//-----------------------------------------------------------------------------

struct HUDDrawData
{
	HDC hdc;
	SIZE itemSize;
	SIZE clientSize;
	u16 hudElementID;

	HUDDrawData(
		HDC hdc,
		SIZE itemSize,
		SIZE clientSize,
		u16 hudElementID)
		:
		hdc(hdc),
		itemSize(itemSize),
		clientSize(clientSize),
		hudElementID(hudElementID)
	{
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
	EHUDProperty theProperty)
{
	std::string result;
	// Try [Menu.Name] first since most HUD elements are likely Menus
	std::string aKey = kMenuPrefix;
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


static LONG hotspotCoordToClient(
	const Hotspot::Coord& theCoord,
	u16 theClientSize)
{
	return
		LONG(theCoord.origin) *
		theClientSize / 0x10000 +
		theCoord.offset;
}


static void drawMenuItem(
	const HUDDrawData& theDrawData,
	RECT theItemRect,
	const std::string& theLabel)
{
	const u16 aHUDElementID = theDrawData.hudElementID;
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
	HDC hdc = theDrawData.hdc;

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


static void draw4DirMenu(HUDDrawData& theDrawData)
{
	const u16 aHUDElementID = theDrawData.hudElementID;
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
	HDC hdc = theDrawData.hdc;
	DBG_ASSERT(aHUDInfo.type == eMenuStyle_4Dir);

	HFONT hOldFont = (HFONT)SelectObject(hdc, sFonts[aHUDInfo.fontID]);
	HPEN hOldPen = (HPEN)SelectObject(hdc, sPens[aHUDInfo.borderPenID]);
	HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, sBrushes[aHUDInfo.itemBrushID]);
	SetBkColor(hdc, aHUDInfo.itemColor);
	SetTextColor(hdc, aHUDInfo.labelColor);

	// Always 4 menu items, in order of L->R->U->D
	const u16 activeSubMenu = Menus::activeSubMenu(aHUDElementID);
	// Left
	RECT anItemRect;
	anItemRect.left = 0;
	anItemRect.top = theDrawData.itemSize.cy;
	anItemRect.right = anItemRect.left + theDrawData.itemSize.cx;
	anItemRect.bottom = anItemRect.top + theDrawData.itemSize.cy;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 0));
	// Right
	anItemRect.left += theDrawData.itemSize.cx;
	anItemRect.right += theDrawData.itemSize.cx;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 1));
	// Up
	anItemRect.left -= theDrawData.itemSize.cx / 2;
	anItemRect.top = 0;
	anItemRect.right = anItemRect.left + theDrawData.itemSize.cx;
	anItemRect.bottom = anItemRect.top + theDrawData.itemSize.cy;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 2));
	// Down
	anItemRect.top += theDrawData.itemSize.cy * 2;
	anItemRect.bottom += theDrawData.itemSize.cy * 2;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 3));

	// Cleanup
	SelectObject(hdc, hOldFont);
	SelectObject(hdc, hOldPen);
	SelectObject(hdc, hOldBrush);
}


static void drawRectHUD(HUDDrawData& theDrawData)
{
	const u16 aHUDElementID = theDrawData.hudElementID;
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
	HDC hdc = theDrawData.hdc;
	DBG_ASSERT(aHUDInfo.type == eHUDType_Rectangle);

	HPEN hOldPen = (HPEN)SelectObject(hdc, sPens[aHUDInfo.borderPenID]);
	HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, sBrushes[aHUDInfo.itemBrushID]);

	SetBkColor(hdc, aHUDInfo.itemColor);
	SetTextColor(hdc, aHUDInfo.labelColor);

	Rectangle(hdc, 0, 0, theDrawData.clientSize.cx, theDrawData.clientSize.cy);

	// Cleanup
	SelectObject(hdc, hOldPen);
	SelectObject(hdc, hOldBrush);
}


static void drawSystemHUD(HUDDrawData& theDrawData)
{
	const u16 aHUDElementID = theDrawData.hudElementID;
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
	HDC hdc = theDrawData.hdc;
	DBG_ASSERT(aHUDInfo.type == eHUDType_System);

	RECT aTextRect = { 8, 8,
		theDrawData.clientSize.cx - 8,
		theDrawData.clientSize.cy - 8 };

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
	for(u16 aHUDElementID = 0;
		aHUDElementID < sHUDElementInfo.size();
		++aHUDElementID)
	{
		HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
		aHUDInfo.type = InputMap::hudElementType(aHUDElementID);
		if( aHUDInfo.type == eHUDType_System )
			continue;
		const std::string& aHUDName = InputMap::hudElementLabel(aHUDElementID);
		// aHUDInfo.position = eHUDProp_Position
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Position),
			aHUDInfo.position);
		// aHUDInfo.itemSize = eHUDProp_ItemSize
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_ItemSize),
			aHUDInfo.itemSize);
		// aHUDInfo.alignmentX/Y = eHUDProp_Alignment
		Hotspot aTempHotspot;
		InputMap::profileStringToHotspot(
			getHUDPropStr(aHUDName, eHUDProp_Alignment),
			aTempHotspot);
		aHUDInfo.alignmentX =
			aTempHotspot.x.origin < 0x4000	? eAlignment_Min :
			aTempHotspot.x.origin > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		aHUDInfo.alignmentY =
			aTempHotspot.y.origin < 0x4000	? eAlignment_Min :
			aTempHotspot.y.origin > 0xC000	? eAlignment_Max :
			/*otherwise*/					  eAlignment_Center;
		// aHUDInfo.fontID = eHUDProp_FontName & _FontSize & _FontWeight
		aHUDInfo.fontID = getOrCreateFontID(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontName),
			getHUDPropStr(aHUDName, eHUDProp_FontSize),
			getHUDPropStr(aHUDName, eHUDProp_FontWeight));
		// aHUDInfo.labelColor = eHUDProp_FontColor
		aHUDInfo.labelColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_FontColor));
		// aHUDInfo.itemColor & .itemBrushID = eHUDProp_ItemColor
		aHUDInfo.itemColor = strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_ItemColor));
		aHUDInfo.itemBrushID = getOrCreateBrushID(
			aHUDBuilder, aHUDInfo.itemColor);
		// aHUDInfo.borderPenID = eHUDProp_BorderColor & _BorderSize
		aHUDInfo.borderPenID = getOrCreatePenID(aHUDBuilder,
			strToRGB(aHUDBuilder,
				getHUDPropStr(aHUDName, eHUDProp_BorderColor)),
			PS_INSIDEFRAME, intFromString(
				getHUDPropStr(aHUDName, eHUDProp_BorderSize)));
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
}


void drawElement(
	HDC hdc,
	u16 theHUDElementID,
	const SIZE& theComponentSize,
	const SIZE& theClientSize)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[theHUDElementID];

	#ifdef _DEBUG
	COLORREF aFrameColor = RGB(0, 0, 0);
	#ifdef DEBUG_DRAW_HUD_ELEMENT_FRAME
	aFrameColor = RGB(0, 0, 200);
	#endif
	#ifdef DEBUG_DRAW_OVERLAY_FRAME
	if( aHUDInfo.type == eHUDType_System )
		aFrameColor = RGB(255, 0, 0);
	#endif
	if( aFrameColor != RGB(0, 0, 0) )
	{
		// Normally I'd prefer not to create and destroy these on-the-fly,
		// but since this is purely for debugging purposes...
		HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
		HPEN hBluePen = CreatePen(PS_INSIDEFRAME, 3, aFrameColor);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hBluePen);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBlackBrush);

		Rectangle(hdc, 0, 0, theClientSize.cx, theClientSize.cy);

		SelectObject(hdc, hOldPen);
		SelectObject(hdc, hOldBrush);
		DeleteObject(hBluePen);
		DeleteObject(hBlackBrush);
	}
	#endif // _DEBUG

	HUDDrawData aDrawData(hdc, theComponentSize, theClientSize, theHUDElementID);
	switch(sHUDElementInfo[theHUDElementID].type)
	{
	case eMenuStyle_4Dir:		draw4DirMenu(aDrawData);	break;
	case eHUDType_Rectangle:	drawRectHUD(aDrawData);		break;
	case eHUDType_System:		drawSystemHUD(aDrawData);	break;
	}
}


SIZE componentSize(u16 theHUDElementID, const SIZE& theClientSize)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[theHUDElementID];

	SIZE result;
	result.cx = hotspotCoordToClient(aHUDInfo.itemSize.x, theClientSize.cx);
	result.cy = hotspotCoordToClient(aHUDInfo.itemSize.y, theClientSize.cy);

	return result;
}


RECT elementRectNeeded(
	u16 theHUDElementID,
	const SIZE& theItemSize,
	const RECT& theClientRect)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[theHUDElementID];

	SIZE aClientSize;
	aClientSize.cx = theClientRect.right - theClientRect.left;
	aClientSize.cy = theClientRect.bottom - theClientRect.top;

	// Get origin point assuming top-left alignment
	POINT anOriginPos;
	anOriginPos.x = hotspotCoordToClient(aHUDInfo.position.x, aClientSize.cx);
	anOriginPos.y = hotspotCoordToClient(aHUDInfo.position.y, aClientSize.cy);

	// Calculate size needed
	SIZE aFullSize = theItemSize;
	switch(aHUDInfo.type)
	{
	case eMenuStyle_4Dir:
		aFullSize.cx *= 2;
		aFullSize.cy *= 3;
		break;
	case eHUDType_System:
		aFullSize.cx = aClientSize.cx;
		aFullSize.cy = aClientSize.cy;
		break;
	}

	// Adjust origin according to alignment settings
	switch(sHUDElementInfo[theHUDElementID].alignmentX)
	{
	case eAlignment_Min:
		// Do nothig
		break;
	case eAlignment_Center:
		anOriginPos.x -= aFullSize.cx / 2;
		break;
	case eAlignment_Max:
		anOriginPos.x -= aFullSize.cx;
		break;
	}
	switch(sHUDElementInfo[theHUDElementID].alignmentY)
	{
	case eAlignment_Min:
		// Do nothig
		break;
	case eAlignment_Center:
		anOriginPos.y -= aFullSize.cy / 2;
		break;
	case eAlignment_Max:
		anOriginPos.y -= aFullSize.cy;
		break;
	}

	RECT result;
	result.left = anOriginPos.x + theClientRect.left;
	result.top = anOriginPos.y + theClientRect.top;
	result.right = result.left + aFullSize.cx;
	result.bottom = result.top + aFullSize.cy;

	return result;
}

} // HUD
