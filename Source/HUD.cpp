//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "HUD.h"

#include "InputMap.h" // labels, profileStringToHotspot()
#include "Lookup.h"
#include "Menus.h" // activeSubMenu()
#include "OverlayWindow.h" // temp hack to force redraw()
#include "Profile.h"

namespace HUD
{

#ifdef _DEBUG
// Make the actual position of the overlay window obvious by drawing a frame
//#define DEBUG_DRAW_WINDOW_FRAME
#endif

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kNoticeStringDisplayTimePerChar = 60,
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
	u16 borderBrushID;
	u8 borderThickness;
	s8 alignmentX;
	s8 alignmentY;
};


//-----------------------------------------------------------------------------
// RenderData
//-----------------------------------------------------------------------------

struct RenderData
	: public ConstructFromZeroInitializedMemory<RenderData>
{
	std::wstring errorMessage;
	std::wstring noticeMessage;
	int errorMessageTimer;
	int noticeMessageTimer;
	bool initialized;
};


//-----------------------------------------------------------------------------
// Other Local Structures
//-----------------------------------------------------------------------------

struct HUDDrawData
{
	HDC hdc;
	POINT origin;
	POINT itemSize;
	POINT clientSize;
	u16 hudElementID;

	HUDDrawData(
		HDC hdc,
		POINT origin,
		POINT itemSize,
		POINT clientSize,
		u16 hudElementID)
		:
		hdc(hdc),
		origin(origin),
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
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<HFONT> sFonts;
static std::vector<HBRUSH> sBrushes;
static std::vector<HUDElementInfo> sHUDElementInfo;
static RenderData sRenderData;


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


static void realignOrigin(
	HUDDrawData& theDrawData,
	u16 theFullWidth, u16 theFullHeight)
{
	// -1 = left/top align, 0 = center, 1 = right/bottom align
	if( sHUDElementInfo[theDrawData.hudElementID].alignmentX == 0 )
		theDrawData.origin.x -= theFullWidth / 2;
	else if( sHUDElementInfo[theDrawData.hudElementID].alignmentX > 0 )
		theDrawData.origin.x -= theFullWidth;
	if( sHUDElementInfo[theDrawData.hudElementID].alignmentY == 0 )
		theDrawData.origin.y -= theFullHeight / 2;
	else if( sHUDElementInfo[theDrawData.hudElementID].alignmentY > 0 )
		theDrawData.origin.y -= theFullHeight;
}


static void drawMenuItem(
	const HUDDrawData& theDrawData,
	RECT theItemRect,
	const std::string& theLabel)
{
	const u16 aHUDElementID = theDrawData.hudElementID;
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
	HDC hdc = theDrawData.hdc;

	// Border
	FillRect(hdc, &theItemRect, sBrushes[aHUDInfo.borderBrushID]);

	// Interior background
	InflateRect(&theItemRect,
		-aHUDInfo.borderThickness,
		-aHUDInfo.borderThickness);
	FillRect(hdc, &theItemRect, sBrushes[aHUDInfo.itemBrushID]);

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

	SelectObject(hdc, sFonts[aHUDInfo.fontID]);
	SetBkColor(hdc, aHUDInfo.itemColor);
	SetTextColor(hdc, aHUDInfo.labelColor);

	realignOrigin(theDrawData,
		theDrawData.itemSize.x * 2,
		theDrawData.itemSize.y * 3);

	// Always 4 menu items, in order of L->R->U->D
	const u16 activeSubMenu = Menus::activeSubMenu(aHUDElementID);
	// Left
	RECT anItemRect;
	anItemRect.left = theDrawData.origin.x;
	anItemRect.top = theDrawData.origin.y + theDrawData.itemSize.y;
	anItemRect.right = anItemRect.left + theDrawData.itemSize.x;
	anItemRect.bottom = anItemRect.top + theDrawData.itemSize.y;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 0));
	// Right
	anItemRect.left += theDrawData.itemSize.x;
	anItemRect.right += theDrawData.itemSize.x;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 1));
	// Up
	anItemRect.left -= theDrawData.itemSize.x / 2;
	anItemRect.top = theDrawData.origin.y;
	anItemRect.right = anItemRect.left + theDrawData.itemSize.x;
	anItemRect.bottom = anItemRect.top + theDrawData.itemSize.y;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 2));
	// Down
	anItemRect.top += theDrawData.itemSize.y * 2;
	anItemRect.bottom += theDrawData.itemSize.y * 2;
	drawMenuItem(theDrawData, anItemRect,
		InputMap::menuItemLabel(activeSubMenu, 3));
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
			aTempHotspot.x.origin < 0x4000 ? -1 :
			aTempHotspot.x.origin > 0xC000 ? 1 : 0;
		aHUDInfo.alignmentY =
			aTempHotspot.y.origin < 0x4000 ? -1 :
			aTempHotspot.y.origin > 0xC000 ? 1 : 0;
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
		// aHUDInfo.borderBrushID = eHUDProp_BorderColor
		aHUDInfo.borderBrushID = getOrCreateBrushID(
			aHUDBuilder, strToRGB(aHUDBuilder,
			getHUDPropStr(aHUDName, eHUDProp_BorderColor)));
		// aHUDInfo.borderThickness = eHUDProp_BorderSize
		aHUDInfo.borderThickness = u8(u32FromString(
			getHUDPropStr(aHUDName, eHUDProp_BorderSize)));
	}

	sRenderData.initialized = true;
}


void cleanup()
{
	for(size_t i = 0; i < sFonts.size(); ++i)
		DeleteObject(sFonts[i]);
	for(size_t i = 0; i < sBrushes.size(); ++i)
		DeleteObject(sBrushes[i]);
	sFonts.clear();
	sBrushes.clear();
	sHUDElementInfo.clear();
	sRenderData = RenderData();
}


void update()
{
	if( sRenderData.errorMessageTimer > 0 )
	{
		sRenderData.errorMessageTimer -= gAppFrameTime;
		if( sRenderData.errorMessageTimer <= 0 )
		{
			sRenderData.errorMessageTimer = 0;
			sRenderData.errorMessage.clear();
			OverlayWindow::redraw();
		}
	}
	else if( !gErrorString.empty() && !hadFatalError() )
	{
		sRenderData.errorMessage =
			widen("MMOGO ERROR: ") +
			gErrorString +
			widen("\nCheck errorlog.txt for other possible errors!");
		sRenderData.errorMessageTimer = max(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sRenderData.errorMessage.size()));
		gErrorString.clear();
		OverlayWindow::redraw();
	}
	
	if( sRenderData.noticeMessageTimer > 0 )
	{
		sRenderData.noticeMessageTimer -= gAppFrameTime;
		if( sRenderData.noticeMessageTimer <= 0 )
		{
			sRenderData.noticeMessageTimer = 0;
			sRenderData.noticeMessage.clear();
			OverlayWindow::redraw();
		}
	}
	if( !gNoticeString.empty() )
	{
		sRenderData.noticeMessage = gNoticeString;
		sRenderData.noticeMessageTimer = max(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sRenderData.noticeMessage.size()));
		gNoticeString.clear();
		OverlayWindow::redraw();
	}

}


void render(HWND theWindow, const RECT& theClientRect)
{
	// Prepare to draw
	PAINTSTRUCT aPaintStruct;
	HDC hdc = BeginPaint(theWindow, &aPaintStruct);
	if( !sRenderData.initialized )
	{
		EndPaint(theWindow, &aPaintStruct);
		return;
	}

	HFONT anOldFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
	HBRUSH hOldBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
	HPEN hOldPen = (HPEN)GetCurrentObject(hdc, OBJ_PEN);

	#ifdef DEBUG_DRAW_WINDOW_FRAME
	{
		// Normally I'd prefer not to create and destroy these on-the-fly,
		// but since this is purely for debugging purposes...
		HBRUSH hBlackBrush = CreateSolidBrush(RGB(0, 0, 0));
		HPEN hRedPen = CreatePen(PS_INSIDEFRAME, 3, RGB(255, 0, 0));
		SelectObject(hdc, hRedPen);
		SelectObject(hdc, hBlackBrush);
		Rectangle(hdc,
			theClientRect.left, theClientRect.top,
			theClientRect.right, theClientRect.bottom);
		SelectObject(hdc, hOldPen);
		SelectObject(hdc, hOldBrush);
		DeleteObject(hRedPen);
		DeleteObject(hBlackBrush);
	}
	#endif

	POINT aClientSize = { theClientRect.right, theClientRect.bottom };

	// Draw each visible HUD Element
	for(int aHUDElementID = gVisibleHUD.firstSetBit();
		aHUDElementID < int(gVisibleHUD.size());
		aHUDElementID = gVisibleHUD.nextSetBit(aHUDElementID+1))
	{
		DBG_ASSERT(aHUDElementID < sHUDElementInfo.size());
		const HUDElementInfo& aHUDInfo = sHUDElementInfo[aHUDElementID];
		POINT anOriginPos;
		anOriginPos.x = hotspotCoordToClient(
			aHUDInfo.position.x, aClientSize.x);
		anOriginPos.y = hotspotCoordToClient(
			aHUDInfo.position.y, aClientSize.y);
		POINT anItemSize;
		anItemSize.x = hotspotCoordToClient(
			aHUDInfo.itemSize.x, aClientSize.x);
		anItemSize.y = hotspotCoordToClient(
			aHUDInfo.itemSize.y, aClientSize.y);
		HUDDrawData aDrawData(hdc, anOriginPos,
			anItemSize, aClientSize, aHUDElementID);
		switch(sHUDElementInfo[aHUDElementID].type)
		{
		case eMenuStyle_4Dir:	draw4DirMenu(aDrawData); break;
		}
	}

	// Draw error string
	if( sRenderData.errorMessageTimer > 0 )
	{
		SelectObject(hdc, anOldFont);
		RECT aTextRect = theClientRect;
		InflateRect(&aTextRect, -8, -8);
		SetBkColor(hdc, RGB(255, 0, 0));
		SetTextColor(hdc, RGB(255, 255, 0));
		DrawText(hdc, sRenderData.errorMessage.c_str(), -1, &aTextRect,
			DT_WORDBREAK);
	}

	// Draw notice string
	if( sRenderData.noticeMessageTimer > 0 )
	{
		SelectObject(hdc, anOldFont);
		RECT aTextRect = theClientRect;
		InflateRect(&aTextRect, -8, -8);
		SetBkColor(hdc, RGB(0, 0, 255));
		SetTextColor(hdc, RGB(255, 255, 255));
		DrawText(hdc, sRenderData.noticeMessage.c_str(), -1, &aTextRect,
			DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
	}

	// Clean up
	SelectObject(hdc, anOldFont);
	SelectObject(hdc, hOldBrush);
	SelectObject(hdc, hOldPen);
	EndPaint(theWindow, &aPaintStruct);		
}

} // HUD
