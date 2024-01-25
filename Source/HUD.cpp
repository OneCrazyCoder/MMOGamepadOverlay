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

#ifdef _DEBUG
// Make the region of each HUD element obvious by drawing a frame
//#define DEBUG_DRAW_HUD_ELEMENT_FRAME
#endif

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

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
	u16 borderPenID;
	s8 alignmentX;
	s8 alignmentY;
};


//-----------------------------------------------------------------------------
// Other Local Structures
//-----------------------------------------------------------------------------

struct HUDDrawData
{
	HDC hdc;
	POINT itemSize;
	POINT clientSize;
	u16 hudElementID;

	HUDDrawData(
		HDC hdc,
		POINT itemSize,
		POINT clientSize,
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

	SelectObject(hdc, sFonts[aHUDInfo.fontID]);
	SelectObject(hdc, sPens[aHUDInfo.borderPenID]);
	SelectObject(hdc, sBrushes[aHUDInfo.itemBrushID]);
	SetBkColor(hdc, aHUDInfo.itemColor);
	SetTextColor(hdc, aHUDInfo.labelColor);

	// Always 4 menu items, in order of L->R->U->D
	const u16 activeSubMenu = Menus::activeSubMenu(aHUDElementID);
	// Left
	RECT anItemRect;
	anItemRect.left = 0;
	anItemRect.top = theDrawData.itemSize.y;
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
	anItemRect.top = 0;
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
}


void drawElement(
	HWND theWindow,
	u16 theHUDElementID,
	const POINT& theItemSize,
	const POINT& theClientSize)
{
	PAINTSTRUCT aPaintStruct;
	HDC hdc = BeginPaint(theWindow, &aPaintStruct);
	RECT aClientRect;
	GetClientRect(theWindow, &aClientRect);

	HFONT anOldFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
	HBRUSH hOldBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
	HPEN hOldPen = (HPEN)GetCurrentObject(hdc, OBJ_PEN);

	#ifdef DEBUG_DRAW_HUD_ELEMENT_FRAME
	{
		// Normally I'd prefer not to create and destroy these on-the-fly,
		// but since this is purely for debugging purposes...
		HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
		HPEN hBluePen = CreatePen(PS_INSIDEFRAME, 3, RGB(0, 0, 200));
		SelectObject(hdc, hBluePen);
		SelectObject(hdc, hBlackBrush);
		Rectangle(hdc, 0, 0, theClientSize.x, theClientSize.y);
		SelectObject(hdc, hOldPen);
		SelectObject(hdc, hOldBrush);
		DeleteObject(hBluePen);
	}
	#endif

	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[theHUDElementID];
	HUDDrawData aDrawData(hdc, theItemSize, theClientSize, theHUDElementID);

	switch(sHUDElementInfo[theHUDElementID].type)
	{
	case eMenuStyle_4Dir:	draw4DirMenu(aDrawData); break;
	}

	// Clean up
	SelectObject(hdc, anOldFont);
	SelectObject(hdc, hOldBrush);
	SelectObject(hdc, hOldPen);
	EndPaint(theWindow, &aPaintStruct);
}


POINT menuItemSize(u16 theHUDElementID, const POINT& theClientSize)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[theHUDElementID];

	POINT result;
	result.x = hotspotCoordToClient(
		aHUDInfo.itemSize.x, theClientSize.x);
	result.y = hotspotCoordToClient(
		aHUDInfo.itemSize.y, theClientSize.y);

	return result;
}


RECT elementRectNeeded(
	u16 theHUDElementID,
	const POINT& theItemSize,
	const RECT& theClientRect)
{
	DBG_ASSERT(theHUDElementID < sHUDElementInfo.size());
	const HUDElementInfo& aHUDInfo = sHUDElementInfo[theHUDElementID];

	// Get origin point assuming top-left alignment
	POINT anOriginPos;
	anOriginPos.x = hotspotCoordToClient(
		aHUDInfo.position.x, theClientRect.right - theClientRect.left);
	anOriginPos.y = hotspotCoordToClient(
		aHUDInfo.position.y, theClientRect.bottom - theClientRect.top);

	// Calculate size needed
	u16 theFullWidth = theItemSize.x;
	u16 theFullHeight = theItemSize.y;
	switch(aHUDInfo.type)
	{
	case eMenuStyle_4Dir:
		theFullWidth *= 2;
		theFullHeight *= 3;
		break;
	}

	// Adjust origin according to alignment settings
	// -1 = left/top align, 0 = center, 1 = right/bottom align
	if( sHUDElementInfo[theHUDElementID].alignmentX == 0 )
		anOriginPos.x -= theFullWidth / 2;
	else if( sHUDElementInfo[theHUDElementID].alignmentX > 0 )
		anOriginPos.x -= theFullWidth;
	if( sHUDElementInfo[theHUDElementID].alignmentY == 0 )
		anOriginPos.y -= theFullHeight / 2;
	else if( sHUDElementInfo[theHUDElementID].alignmentY > 0 )
		anOriginPos.y -= theFullHeight;

	RECT result;
	result.left = anOriginPos.x + theClientRect.left;
	result.top = anOriginPos.y + theClientRect.top;
	result.right = result.left + theFullWidth;
	result.bottom = result.top + theFullHeight;

	return result;
}

} // HUD
