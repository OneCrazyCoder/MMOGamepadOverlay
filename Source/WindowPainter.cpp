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
	{ return name == rhs.name && size == rhs.size && weight == rhs.weight; }
};

struct ZERO_INIT(PenInfo)
{
	HPEN handle;
	COLORREF color;
	int width;

	bool operator==(const PenInfo& rhs) const
	{ return color == rhs.color && width == rhs.width; }
};

struct ZERO_INIT(BitmapFileInfo)
{
	HBITMAP handle;
	SIZE size;
	COLORREF maskColor;
	bool useMaskColor;
};

struct ZERO_INIT(BitmapIcon)
{
	HBITMAP image;
	HBITMAP mask;
	POINT fileTL;
	SIZE size;
	u16 fileID;

	bool operator==(const BitmapIcon& rhs) const
	{
		return
			fileID == rhs.fileID &&
			fileTL.x == rhs.fileTL.x && fileTL.y == rhs.fileTL.y &&
			size.cx == rhs.size.cx && size.cy == rhs.size.cy;
	}
};

struct ZERO_INIT(LabelIcon)
{
	union
	{
		u16 bitmapIconID;
		u16 hotspotID;
	};
	bool copyFromTarget;
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
	u8 baseRadius;

	bool operator==(const MenuAppearance& rhs) const
	{
		return
			itemType == rhs.itemType &&
			transColor == rhs.transColor &&
			titleColor == rhs.titleColor &&
			flashMaxTime == rhs.flashMaxTime &&
			fontID == rhs.fontID &&
			baseRadius == rhs.baseRadius;
	}
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
	{
		return
			baseColor == rhs.baseColor &&
			labelColor == rhs.labelColor &&
			borderColor == rhs.borderColor &&
			bitmapIconID == rhs.bitmapIconID &&
			baseBorderSize == rhs.baseBorderSize;
	}
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
	LONG altSize;
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
	s16 maxBorderSize;

	MenuCacheEntry()
	{
		maxBorderSize = -1;
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
		selection(kInvalidID),
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
static std::vector<BitmapFileInfo> sBitmapFiles;
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
static int sBitmapsProfileSectionID = 0;
static int sErrorMessageTimer = 0;
static int sNoticeMessageTimer = 0;
static int sSystemBorderFlashTimer = 0;
static int sCopyRectUpdateRate = 100;
static int sAutoRefreshCopyRectTime = 0;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static COLORREF stringToRGB(const std::string& theString, size_t thePos = 0)
{
	// TODO: Error checking?
	const u32 r = clamp(stringToInt(
		fetchNextItem(theString, thePos)), 0, 255);
	const u32 g = clamp(stringToInt(
		fetchNextItem(theString, ++thePos)), 0, 255);
	const u32 b = clamp(stringToInt(
		fetchNextItem(theString, ++thePos)), 0, 255);
	return RGB(r, g, b);
}


static LONG stringToWidth(const std::string& theString, size_t& thePos)
{
	// Calling code will have to do error checking (thePos != theString.size())
	// since this may be part of a larger string like "width, height"
	const double aSum = stringToDoubleSum(theString, thePos);
	return LONG(clamp(floor(aSum + 0.5), 0.0, double(LONG_MAX)));
}


static SIZE stringToSize(const std::string& theString, size_t thePos = 0)
{
	// Calling code should check for cx < 0 for invalid results
	SIZE result = { -1, -1 };
	result.cx = stringToWidth(theString, thePos);
	if( thePos >= theString.size() )
	{// Only width was specified - use same value for both
		result.cy = result.cx;
		return result;
	}
	if( theString[thePos] != ',' &&
		theString[thePos] != 'x' &&
		theString[thePos] != 'X' )
	{// Appropriate character separating width from height not found!
		result.cx = -1;
		return result;
	}
	result.cy = stringToWidth(theString, ++thePos);
	if( thePos != theString.size() )
		result.cx = result.cy = -1;
	return result;
}


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
	const SIZE& aNewSize = stringToSize(theSizeDesc);

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
			sHotspotSizes[aCheckPos].width = u16(min(aNewSize.cx, 0xFFFFL));
			sHotspotSizes[aCheckPos].height = u16(min(aNewSize.cy, 0xFFFFL));
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
			aNewRange.width = u16(min(aNewSize.cx, 0xFFFFL));
			aNewRange.height = u16(min(aNewSize.cy, 0xFFFFL));
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


static SIZE getHotspotUnscaledSize(int theHotspotID)
{
	SIZE result = {0, 0};
	for(int i = 0, end = intSize(sHotspotSizes.size()); i < end; ++i)
	{
		if( theHotspotID >= sHotspotSizes[i].firstHotspotID &&
			theHotspotID <= sHotspotSizes[i].lastHotspotID )
		{
			result.cx = sHotspotSizes[i].width;
			result.cy = sHotspotSizes[i].height;
			return result;
		}
	}

	return result;
}


static void createBaseFontHandle(FontInfo& theFontInfo)
{
	DBG_ASSERT(theFontInfo.handle == NULL);
	const int aFontPointSize = int(gUIScale * theFontInfo.size);
	HDC hdc = GetDC(NULL);
	const int aFontHeight =
		-MulDiv(aFontPointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(NULL, hdc);
	// Below always succeeds with fallbacks even if the font name was
	// typed in wrong, so don't need to report error back to user here
	// (they'll just have to notice the font looks off and check the name)
	theFontInfo.handle = CreateFont(
		aFontHeight, // Height of the font
		0, // Width of the font
		0, // Angle of escapement
		0, // Orientation angle
		theFontInfo.weight, // Font weight
		FALSE, // Italic
		FALSE, // Underline
		FALSE, // Strikeout
		DEFAULT_CHARSET, // Character set identifier
		OUT_DEFAULT_PRECIS, // Output precision
		CLIP_DEFAULT_PRECIS, // Clipping precision
		DEFAULT_QUALITY, // Output quality
		DEFAULT_PITCH | FF_DONTCARE, // Pitch and family
		widen(theFontInfo.name).c_str() // Font face name
	);
}


static HPEN getBorderPenHandle(int theBorderWidth, COLORREF theBorderColor)
{
	PenInfo aPenInfo;
	if( theBorderWidth <= 0 )
	{
		aPenInfo.width = 0;
		aPenInfo.color = RGB(0, 0, 0);
	}
	else
	{
		aPenInfo.width = theBorderWidth;
		aPenInfo.color = theBorderColor;
	}
	for(int i = 0, end = intSize(sPens.size()); i < end; ++i)
	{
		if( sPens[i] == aPenInfo )
			return sPens[i].handle;
	}
	aPenInfo.handle = CreatePen(
		aPenInfo.width ? PS_INSIDEFRAME : PS_NULL,
		aPenInfo.width,
		aPenInfo.color);
	sPens.push_back(aPenInfo);
	return sPens.back().handle;
}


static BitmapFileInfo loadBitmapFile(
	Profile::PropertyMapPtr theBitmapsPropMap,
	int theBitmapPropID)
{
	DBG_ASSERT(size_t(theBitmapPropID) < sBitmapFiles.size());
	BitmapFileInfo& theSrcFileInfo = sBitmapFiles[theBitmapPropID];
	if( theSrcFileInfo.handle )
		return theSrcFileInfo;

	const std::string& theBitmapDesc =
		theBitmapsPropMap->vals()[theBitmapPropID].str;
	size_t aStrPos = 0;
	const std::string& aFilePath = fetchNextItem(theBitmapDesc, aStrPos);
	if( aFilePath.empty() )
		return theSrcFileInfo;

	// Convert given path into a wstring absolute path w/ .bmp extension
	const std::wstring aFilePathW =
		widen(removeExtension(toAbsolutePath(aFilePath)) + ".bmp");

	if( !isValidFilePath(aFilePathW) )
	{
		logError("Could not find requested bitmap file %s!",
			aFilePath.c_str());
		return theSrcFileInfo;
	}

	theSrcFileInfo.handle = (HBITMAP)LoadImage(
		NULL, aFilePathW.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
	if( !theSrcFileInfo.handle )
	{
		logError("Could not load bitmap %s. Wrong file format?",
			aFilePath.c_str());
		return theSrcFileInfo;
	}

	BITMAP aBitmapStruct;
	GetObject(theSrcFileInfo.handle, sizeof(aBitmapStruct), &aBitmapStruct);
	theSrcFileInfo.size.cx = aBitmapStruct.bmWidth;
	theSrcFileInfo.size.cy = aBitmapStruct.bmHeight;

	if( aStrPos < theBitmapDesc.size() )
	{
		theSrcFileInfo.useMaskColor = true;
		theSrcFileInfo.maskColor = stringToRGB(theBitmapDesc, ++aStrPos);
	}

	return theSrcFileInfo;
}


static void createBitmapIconImage(int theIconID)
{
	DBG_ASSERT(size_t(theIconID) < sBitmapIcons.size());
	BitmapIcon& theIcon = sBitmapIcons[theIconID];
	DBG_ASSERT(theIcon.image == NULL);
	DBG_ASSERT(theIcon.mask == NULL);
	DBG_ASSERT(theIcon.fileID < sBitmapFiles.size());
	const BitmapFileInfo& theSrcBitmapFileInfo = sBitmapFiles[theIcon.fileID];

	HDC hdcSrc = CreateCompatibleDC(NULL);
	HBITMAP hOldSrcBitmap = (HBITMAP)
		SelectObject(hdcSrc, theSrcBitmapFileInfo.handle);
	HDC hdcImage = CreateCompatibleDC(NULL);
	theIcon.image = CreateCompatibleBitmap(
		hdcSrc, theIcon.size.cx, theIcon.size.cy);
	HBITMAP hOldImageBitmap = (HBITMAP)
		SelectObject(hdcImage, theIcon.image);

	// Copy source to image bitmap
	BitBlt(
		hdcImage, 0, 0, theIcon.size.cx, theIcon.size.cy,
		hdcSrc, theIcon.fileTL.x, theIcon.fileTL.y, SRCCOPY);

	if( sBitmapFiles[theIcon.fileID].useMaskColor )
	{
		HDC hdcMask = CreateCompatibleDC(NULL);
		theIcon.mask = CreateBitmap(
			theIcon.size.cx, theIcon.size.cy, 1, 1, NULL);
		HBITMAP hOldMaskBitmap = (HBITMAP)
			SelectObject(hdcMask, theIcon.mask);

		// Generate 1-bit - maskColor areas become 1, opaque areas become 0
		SetBkColor(hdcImage, sBitmapFiles[theIcon.fileID].maskColor);
		BitBlt(
			hdcMask, 0, 0, theIcon.size.cx, theIcon.size.cy,
			hdcImage, 0, 0, SRCCOPY);

		// Change maskColor pixels in the image bitmap to black
		BitBlt(
			hdcImage, 0, 0, theIcon.size.cx, theIcon.size.cy,
			hdcMask, 0, 0, SRCINVERT);

		SelectObject(hdcMask, hOldMaskBitmap);
		DeleteDC(hdcMask);
	}

	SelectObject(hdcImage, hOldImageBitmap);
	DeleteDC(hdcImage);
	SelectObject(hdcSrc, hOldSrcBitmap);
	DeleteDC(hdcSrc);
}


static int getOrCreateBitmapIconID(const std::string& theIconDesc)
{
	int anID = 0;
	size_t aStrPos = 0;
	const std::string& theBitmapKey = fetchNextItem(theIconDesc, aStrPos, ":");
	Profile::PropertyMapPtr aBitmapFilesSection =
		Profile::getSectionProperties(sBitmapsProfileSectionID);
	const int aBitmapFileID = aBitmapFilesSection->findIndex(theBitmapKey);

	// Bitmap name not found, but might be a hotspot, so don't give error here!
	if( aBitmapFileID >= aBitmapFilesSection->size() )
		return anID;

	// Get/load source bitmap file
	const BitmapFileInfo& theSrcBitmapFileInfo =
		loadBitmapFile(aBitmapFilesSection, aBitmapFileID);
	if( !theSrcBitmapFileInfo.handle )
		return anID;

	BitmapIcon anIcon;
	anIcon.fileID = dropTo<u16>(aBitmapFileID);
	if( aStrPos >= theIconDesc.size() - 1 )
	{// Use entire bitmap
		anIcon.fileTL.x = 0;
		anIcon.fileTL.y = 0;
		anIcon.size = theSrcBitmapFileInfo.size;
	}
	else
	{// Use region of bitmap
		anIcon.fileTL.x = dropTo<LONG>(stringToInt(
			fetchNextItem(theIconDesc, ++aStrPos)));
		anIcon.fileTL.y = dropTo<LONG>(stringToInt(
			fetchNextItem(theIconDesc, ++aStrPos)));
		anIcon.size.cx = dropTo<LONG>(stringToInt(
			fetchNextItem(theIconDesc, ++aStrPos)));
		anIcon.size.cy = dropTo<LONG>(stringToInt(
			fetchNextItem(theIconDesc, ++aStrPos)));
		// TODO - report error if above numbers are invalid somehow
	}

	// Check to see if already have created an icon with these parameters
	for(int end = intSize(sBitmapIcons.size()); anID < end; ++anID)
	{
		if( sBitmapIcons[anID] == anIcon )
			return anID;
	}

	sBitmapIcons.push_back(anIcon);
	createBitmapIconImage(anID);
	return anID;
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
	createBaseFontHandle(sFonts.back());
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

	// Check if is a bitmap icon description
	LabelIcon aNewIcon;
	aNewIcon.bitmapIconID =
		dropTo<u16>(getOrCreateBitmapIconID(theIconDesc));
	if( !aNewIcon.bitmapIconID )
	{// Nope! Copy-from-screen hotspot instead?
		aNewIcon.hotspotID =
			dropTo<u16>(InputMap::hotspotIDFromName(theIconDesc));
		if( !aNewIcon.hotspotID )
		{
			// TODO - error, can't find bitmap or hotspot being referenced
			return;
		}
		aNewIcon.copyFromTarget = true;
	}

	sLabelIcons.setValue(theTextLabel, aNewIcon);
}


static inline PropString getPropString(
	Profile::PropertyMapPtr thePropMap,
	const std::string& thePropName)
{
	PropString result;
	result.str = Profile::getStr(thePropMap, thePropName);
	result.valid = !isEffectivelyEmptyString(result.str);
	return result;
}


static void fetchMenuPositionProperties(
	Profile::PropertyMapPtr thePropMap,
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
			s8(clamp(stringToInt(p.str), -100, 100));
	}
}


static void fetchMenuLayoutProperties(
	Profile::PropertyMapPtr thePropMap,
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

	if( PropString p = getPropString(thePropMap, kItemSizePropName) )
	{
		const SIZE& aSize = stringToSize(p.str);
		if( aSize.cx < 0 )
		{
			// TODO - error invalid size
		}
		else
		{
			theDestLayout.sizeX = u16(min(aSize.cx, 0xFFFFL));
			theDestLayout.sizeY = u16(min(aSize.cy, 0xFFFFL));
		}
	}

	if( PropString p = getPropString(thePropMap, kGapSizePropName) )
	{
		const SIZE& aSize = stringToSize(p.str);
		if( aSize.cx < 0 )
		{
			// TODO - error invalid size
		}
		else
		{
			theDestLayout.gapSizeX = s8(clamp(aSize.cx, -127, 128));
			theDestLayout.gapSizeY = s8(clamp(aSize.cy, -127, 128));
		}
	}

	if( PropString p = getPropString(thePropMap, kTitleHeightPropName) )
	{
		size_t aPos = 0;
		theDestLayout.titleHeight =
			u8(clamp(stringToWidth(p.str, aPos), 0, 0xFF));
		// TODO - error if aPos != p.str.size()
	}

	if( PropString p = getPropString(thePropMap, kAltLabelWidthPropName) )
	{
		size_t aPos = 0;
		theDestLayout.altLabelWidth =
			u16(clamp(stringToWidth(p.str, aPos), 0, 0xFFFF));
		// TODO - error if aPos != p.str.size()
	}
}


static void fetchBaseAppearanceProperties(
	Profile::PropertyMapPtr thePropMap,
	MenuAppearance& theDestAppearance)
{
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
			stringToInt(p.str), 0, 0xFFFF));
	}
	if( theDestAppearance.itemType == eMenuItemType_RndRect )
	{
		if( PropString p = getPropString(thePropMap, kRadiusPropName) )
		{
			size_t aPos = 0;
			theDestAppearance.baseRadius =
				u8(clamp(stringToWidth(p.str, aPos), 0, 0xFF));
			// TODO - error if aPos != p.str.size()
			theDestAppearance.radius = u8(clamp(int(
				theDestAppearance.baseRadius * gUIScale), 0, 0xFF));
		}
	}
	
	PropString aFontNameProp = getPropString(thePropMap, kFontNamePropName);
	PropString aFontSizeProp = getPropString(thePropMap, kFontSizePropName);
	PropString aFontWeightProp = getPropString(thePropMap, kFontWeightPropName);
	if( aFontNameProp || aFontSizeProp || aFontWeightProp )
	{
		FontInfo aNewFont = sFonts[theDestAppearance.fontID];
		aNewFont.handle = NULL;
		if( aFontNameProp ) aNewFont.name = aFontNameProp.str;
		if( aFontWeightProp )
			aNewFont.weight = stringToInt(aFontWeightProp.str);
		if( aFontSizeProp )
		{
			size_t aPos = 0;
			aNewFont.size = int(stringToWidth(aFontSizeProp.str, aPos));
			// TODO - error if aPos != p.str.size()
		}
		theDestAppearance.fontID = dropTo<u16>(getOrCreateFontID(aNewFont));
	}
}


static PropString getPropStringForDrawState(
	Profile::PropertyMapPtr thePropMap,
	const std::string& thePropName,
	EMenuItemDrawState theDrawState)
{
	PropString result = getPropString(thePropMap,
		kDrawStatePrefix[theDrawState] + thePropName);
	if( !result.valid )
	{
		switch(theDrawState)
		{
		case eMenuItemDrawState_FlashSelected:
			// Try getting "flash" appearance version of this property
			result = getPropString(thePropMap,
				kDrawStatePrefix[eMenuItemDrawState_Flash] + thePropName);
			if( result.valid )
				break;
			// If that didn't exist, try getting "selected" version
			result = getPropString(thePropMap,
				kDrawStatePrefix[eMenuItemDrawState_Selected] + thePropName);
			if( result.valid )
				break;
			// Fall through
		case eMenuItemDrawState_Selected:
		case eMenuItemDrawState_Flash:
			// Try getting normal appearance version of this property
			result = getPropString(thePropMap, thePropName);
			break;
		}
	}

	return result;
}


static void fetchItemAppearanceProperties(
	Profile::PropertyMapPtr thePropMap,
	MenuItemAppearance& theDestAppearance,
	EMenuItemDrawState theDrawState)
{
	if( PropString p = getPropStringForDrawState(
			thePropMap, kItemColorPropName, theDrawState) )
	{
		theDestAppearance.baseColor = stringToRGB(p.str);
	}
	if( PropString p = getPropStringForDrawState(
			thePropMap, kFontColorPropName, theDrawState) )
	{
		theDestAppearance.labelColor = stringToRGB(p.str);
	}
	if( PropString p = getPropStringForDrawState(
			thePropMap, kBorderColorPropName, theDrawState) )
	{
		theDestAppearance.borderColor = stringToRGB(p.str);
		theDestAppearance.borderPen = NULL;
	}
	if( PropString p = getPropStringForDrawState(
			thePropMap, kBorderSizePropName, theDrawState) )
	{
		size_t aPos = 0;
		theDestAppearance.baseBorderSize =
			u8(clamp(stringToWidth(p.str, aPos), 0, 0xFF));
		// TODO - error if aPos != p.str.size()
		theDestAppearance.borderSize = 0;
		theDestAppearance.borderPen = NULL;
		if( theDestAppearance.baseBorderSize > 0 )
		{
			theDestAppearance.borderSize = u8(clamp(int(
				theDestAppearance.baseBorderSize * gUIScale), 1, 0xFF));
		}
	}
	if( PropString p = getPropStringForDrawState(
			thePropMap, kBitmapPropName, theDrawState) )
	{
		theDestAppearance.bitmapIconID =
			dropTo<u16>(getOrCreateBitmapIconID(p.str));
		// TODO - report error if get 0 back from above
	}

	if( !theDestAppearance.borderPen )
	{
		theDestAppearance.borderPen = getBorderPenHandle(
			theDestAppearance.borderSize,
			theDestAppearance.borderColor);
	}
}


static void fetchAlphaFadeProperties(
	Profile::PropertyMapPtr thePropMap,
	WindowAlphaInfo& theDestAlpha)
{
	const WindowAlphaInfo anOldAlphaInfo = theDestAlpha;
	if( PropString p = getPropString(thePropMap, kMaxAlphaPropName) )
		theDestAlpha.maxAlpha = u8(clamp(stringToInt(p.str), 0, 0xFF));
	if( PropString p = getPropString(thePropMap, kInactiveAlphaPropName) )
		theDestAlpha.inactiveAlpha = u8(clamp(stringToInt(p.str), 0, 0xFF));
	if( PropString p = getPropString(thePropMap, kFadeInTimePropName) )
		theDestAlpha.fadeInTime = u16(clamp(stringToInt(p.str), 1, 0xFFFF));
	if( PropString p = getPropString(thePropMap, kFadeOutTimePropName) )
		theDestAlpha.fadeOutTime = u16(clamp(stringToInt(p.str), 1, 0xFFFF));
	if( PropString p = getPropString(thePropMap, kFadeInDelayPropName) )
		theDestAlpha.fadeInDelay = u16(clamp(stringToInt(p.str), 0, 0xFFFF));
	if( PropString p = getPropString(thePropMap, kFadeOutDelayPropName) )
		theDestAlpha.fadeOutDelay =
			u16(clamp(stringToInt(p.str), 0, 0xFFFF));
	if( PropString p = getPropString(thePropMap, kInactiveDelayPropName) )
		theDestAlpha.inactiveFadeOutDelay =
			u16(clamp(stringToInt(p.str), 0, 0xFFFF));

	if( !(anOldAlphaInfo == theDestAlpha) )
	{// Cache fade rate to avoid doing this calculation every frame
		theDestAlpha.fadeInRate =
			double(theDestAlpha.maxAlpha) / double(theDestAlpha.fadeInTime);
		theDestAlpha.fadeOutRate =
			double(theDestAlpha.maxAlpha) / double(theDestAlpha.fadeOutTime);
	}
}


static MenuPosition getMenuPosition(int theMenuID)
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
			Profile::getSectionProperties(InputMap::menuSectionID(theMenuID)),
			aMenuPosition);
		// Add the data to the cache
		theMenuCacheEntry.positionID = dropTo<u16>(
			getOrCreateMenuPositionID(aMenuPosition));
		DBG_ASSERT(theMenuCacheEntry.positionID < sMenuPositions.size());
	}

	return sMenuPositions[theMenuCacheEntry.positionID];
}


static MenuLayout getMenuLayout(int theMenuID)
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
			Profile::getSectionProperties(InputMap::menuSectionID(theMenuID)),
			aMenuLayout);
		theMenuCacheEntry.layoutID = dropTo<u16>(
			getOrCreateMenuLayoutID(aMenuLayout));
		DBG_ASSERT(theMenuCacheEntry.layoutID < sMenuLayouts.size());
	}

	return sMenuLayouts[theMenuCacheEntry.layoutID];
}


static MenuAppearance getMenuAppearance(int theMenuID)
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
			Profile::getSectionProperties(InputMap::menuSectionID(theMenuID)),
			aMenuApp);
		theMenuCacheEntry.appearanceID = dropTo<u16>(
			getOrCreateBaseAppearanceID(aMenuApp));
		DBG_ASSERT(theMenuCacheEntry.appearanceID < sMenuAppearances.size());
	}

	return sMenuAppearances[theMenuCacheEntry.appearanceID];
}


static MenuItemAppearance getMenuItemAppearance(
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
			Profile::getSectionProperties(InputMap::menuSectionID(theMenuID)),
			anItemApp, theDrawState);
		anAppID = getOrCreateItemAppearanceID(anItemApp);
		theMenuCacheEntry.itemAppearanceID[theDrawState] =
			dropTo<u16>(anAppID);
		DBG_ASSERT(size_t(anAppID) < sMenuItemAppearances.size());
	}

	return sMenuItemAppearances[anAppID];
}


static WindowAlphaInfo getMenuAlphaInfo(int theMenuID)
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
			Profile::getSectionProperties(InputMap::menuSectionID(theMenuID)),
			anAlphaInfo);
		theMenuCacheEntry.alphaInfoID = dropTo<u16>(
			getOrCreateAlphaInfoID(anAlphaInfo));
		DBG_ASSERT(theMenuCacheEntry.alphaInfoID < sMenuAlphaInfo.size());
	}

	return sMenuAlphaInfo[theMenuCacheEntry.alphaInfoID];
}


static POINT hotspotToPoint(
	const Hotspot& theHotspot, const SIZE& theTargetSize)
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


static void eraseRect(DrawData& dd, const RECT& theRect)
{
	const MenuAppearance& app = getMenuAppearance(dd.menuID);
	COLORREF oldColor = SetDCBrushColor(dd.hdc, app.transColor);
	HBRUSH hBrush = (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH);
	FillRect(dd.hdc, &theRect, hBrush);
	SetDCBrushColor(dd.hdc, oldColor);
}


static void drawBitmapIcon(
	DrawData& dd, const BitmapIcon& theIcon, const RECT& theRect)
{
	DBG_ASSERT(theIcon.image && sBitmapDrawSrc);
	HBITMAP hOldBitmap = NULL;

	if( !theIcon.mask )
	{
		hOldBitmap = (HBITMAP)
			SelectObject(sBitmapDrawSrc, theIcon.image);
		StretchBlt(dd.hdc,
				   theRect.left, theRect.top,
				   theRect.right - theRect.left,
				   theRect.bottom - theRect.top,
				   sBitmapDrawSrc,
				   0, 0, theIcon.size.cx, theIcon.size.cy,
				   SRCCOPY);
	}
	else
	{
		// Draw 1-bit mask with SRCAND, which will overwrite any pixels in
		// destination that are (0) in the mask to be all-black (0) while
		// leaving the other pixels (white (1) in the mask) in dest as-is.
		// Need to make sure 0 and 1 in the 1-bit image map to literal black
		// and white first, which map to dest DC's background and text colors.
		hOldBitmap = (HBITMAP)
			SelectObject(sBitmapDrawSrc, theIcon.mask);
		SetBkColor(dd.hdc, RGB(255, 255, 255));
		SetTextColor(dd.hdc, RGB(0, 0, 0));
		StretchBlt(dd.hdc,
				   theRect.left, theRect.top,
				   theRect.right - theRect.left,
				   theRect.bottom - theRect.top,
				   sBitmapDrawSrc,
				   0, 0, theIcon.size.cx, theIcon.size.cy,
				   SRCAND);

		// Draw image using SRCPAINT (OR) to mask out transparent pixels
		// Black (0) source (was maskColor) | any dest = keep dest color
		// Color source | black (0) dest (masked via above) = icon color
		SelectObject(sBitmapDrawSrc, theIcon.image);
		StretchBlt(dd.hdc,
				   theRect.left, theRect.top,
				   theRect.right - theRect.left,
				   theRect.bottom - theRect.top,
				   sBitmapDrawSrc,
				   0, 0, theIcon.size.cx, theIcon.size.cy,
				   SRCPAINT);
	}

	// Cleanup
	SelectObject(sBitmapDrawSrc, hOldBitmap);
}


static void drawBGRect(
	DrawData& dd, const MenuItemAppearance& theApp, const RECT& theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);

	Rectangle(dd.hdc,
		theRect.left, theRect.top,
		theRect.right + (theApp.borderSize ? 0 : 1),
		theRect.bottom + (theApp.borderSize ? 0 : 1));
}


static void drawBGRndRect(
	DrawData& dd, const MenuItemAppearance& theApp, const RECT& theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);

	int aRadius = getMenuAppearance(dd.menuID).radius;
	aRadius = min<int>(aRadius, (theRect.right - theRect.left) * 3 / 4);
	aRadius = min<int>(aRadius, (theRect.bottom - theRect.top) * 3 / 4);

	RoundRect(dd.hdc,
		theRect.left, theRect.top,
		theRect.right + (theApp.borderSize ? 0 : 1),
		theRect.bottom + (theApp.borderSize ? 0 : 1),
		aRadius, aRadius);
}


static void drawBGBitmap(
	DrawData& dd, const MenuItemAppearance& theApp, const RECT& theRect)
{
	if( theApp.bitmapIconID == 0 )
	{
		drawBGRect(dd, theApp, theRect);
		return;
	}

	DBG_ASSERT(theApp.bitmapIconID < sBitmapIcons.size());
	drawBitmapIcon(dd, sBitmapIcons[theApp.bitmapIconID], theRect);
}


static void drawBGCircle(
	DrawData& dd, const MenuItemAppearance& theApp, const RECT& theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);

	Ellipse(dd.hdc,
		theRect.left, theRect.top,
		theRect.right, theRect.bottom);
}


static void drawBGArrowL(
	DrawData& dd, const MenuItemAppearance& theApp, RECT theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);
	if( theApp.borderSize > 0 )
	{
		InflateRect(&theRect,
			-theApp.borderSize / 2 - 1, -theApp.borderSize / 2 - 1);
	}

	POINT points[3];
	points[0].x = theRect.right;
	points[0].y = theRect.top;
	points[1].x = theRect.right;
	points[1].y = theRect.bottom;
	points[2].x = theRect.left;
	points[2].y = (theRect.top + theRect.bottom) / 2;
	Polygon(dd.hdc, points, 3);
}


static void drawBGArrowR(
	DrawData& dd, const MenuItemAppearance& theApp, RECT theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);
	if( theApp.borderSize > 0 )
	{
		InflateRect(&theRect,
			-theApp.borderSize / 2 - 1, -theApp.borderSize / 2 - 1);
	}

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.top;
	points[1].x = theRect.left;
	points[1].y = theRect.bottom;
	points[2].x = theRect.right;
	points[2].y = (theRect.top + theRect.bottom) / 2;
	Polygon(dd.hdc, points, 3);
}


static void drawBGArrowU(
	DrawData& dd, const MenuItemAppearance& theApp, RECT theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);
	if( theApp.borderSize > 0 )
	{
		InflateRect(&theRect,
			-theApp.borderSize / 2 - 1, -theApp.borderSize / 2 - 1);
	}

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.bottom;
	points[1].x = theRect.right;
	points[1].y = theRect.bottom;
	points[2].x = (theRect.left + theRect.right) / 2;
	points[2].y = theRect.top;
	Polygon(dd.hdc, points, 3);
}


static void drawBGArrowD(
	DrawData& dd, const MenuItemAppearance& theApp, RECT theRect)
{
	DBG_ASSERT(theApp.borderPen);
	SelectObject(dd.hdc, theApp.borderPen);
	SetDCBrushColor(dd.hdc, theApp.baseColor);
	if( theApp.borderSize > 0 )
	{
		InflateRect(&theRect,
			-theApp.borderSize / 2 - 1, -theApp.borderSize / 2 - 1);
	}

	POINT points[3];
	points[0].x = theRect.left;
	points[0].y = theRect.top;
	points[1].x = theRect.right;
	points[1].y = theRect.top;
	points[2].x = (theRect.left + theRect.right) / 2;
	points[2].y = theRect.bottom;
	Polygon(dd.hdc, points, 3);
}


static void drawMenuItemBG(
	DrawData& dd,
	const MenuAppearance& theMenuApp,
	const MenuItemAppearance& theItemApp,
	const RECT& theRect)
{
	switch(theMenuApp.itemType)
	{
	case eMenuItemType_Rect:	drawBGRect(dd, theItemApp, theRect);	break;
	case eMenuItemType_RndRect:	drawBGRndRect(dd, theItemApp, theRect);	break;
	case eMenuItemType_Bitmap:	drawBGBitmap(dd, theItemApp, theRect);	break;
	case eMenuItemType_Circle:	drawBGCircle(dd, theItemApp, theRect);	break;
	case eMenuItemType_ArrowL:	drawBGArrowL(dd, theItemApp, theRect);	break;
	case eMenuItemType_ArrowR:	drawBGArrowR(dd, theItemApp, theRect);	break;
	case eMenuItemType_ArrowU:	drawBGArrowU(dd, theItemApp, theRect);	break;
	case eMenuItemType_ArrowD:	drawBGArrowD(dd, theItemApp, theRect);	break;
	case eMenuItemType_Label:	/* no background drawn */				break;
	default: DBG_ASSERT(false && "Invalid Menu Item Type!");
	}
}


static LONG getMaxBorderSize(int theMenuID)
{
	int result = sMenuDrawCache[theMenuID].maxBorderSize;
	if( result < 0 )
	{
		result = getMenuAppearance(theMenuID).radius / 4;
		for(int i = 0; i < eMenuItemDrawState_Num; ++i)
		{
			result = max<int>(result,
				getMenuItemAppearance(theMenuID, EMenuItemDrawState(i))
					.borderSize);
		}
		sMenuDrawCache[theMenuID].maxBorderSize = dropTo<s16>(result);
	}

	return result;
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
	DrawData& dd,
	const RECT& theRect,
	const std::wstring& theStr,
	const HFONT theBaseFontHandle,
	UINT theFormat,
	StringScaleCacheEntry& theCacheEntry)
{
	if( theCacheEntry.width > 0 )
		return; // already initialized!

	// This will not only check if an alternate font size is needed, but
	// also calculate drawn width and height for given string, which
	// can be used to calculate offsets for things like vertical justify
	// when not using DT_SINGLELINE (meaning DT_VCENTER/_BOTTOM don't work).
	RECT aNeededRect = theRect;
	SelectObject(dd.hdc, theBaseFontHandle);
	HFONT aChosenFontHandle = NULL;
	if( LONG aFontHeight = getFontHeightToFit(
			dd.hdc, theStr, theRect, &aNeededRect, theFormat) )
	{// Use an alternate size of the font from sAutoSizedFonts

		// First see if already created an auto-sized font for this setup
		for(int i = 0, end = intSize(sAutoSizedFonts.size()); i < end; ++i)
		{
			if( sAutoSizedFonts[i].baseFontHandle == theBaseFontHandle &&
				sAutoSizedFonts[i].altSize == aFontHeight )
			{
				aChosenFontHandle = sAutoSizedFonts[i].handle;
				break;
			}
		}

		// If no font chosen yet, create a new indirect resized font
		if( !aChosenFontHandle )
		{
			sAutoSizedFonts.push_back(ResizedFontInfo());
			sAutoSizedFonts.back().baseFontHandle = theBaseFontHandle;
			sAutoSizedFonts.back().altSize = aFontHeight;
			LOGFONT aNewLogFont;
			GetObject(theBaseFontHandle, sizeof(LOGFONT), &aNewLogFont);
			aNewLogFont.lfWidth = 0;
			aNewLogFont.lfHeight = aFontHeight;
			aChosenFontHandle = CreateFontIndirect(&aNewLogFont);
			sAutoSizedFonts.back().handle = aChosenFontHandle;
		}
	}
	else
	{// The base font is fine to use directly
		aChosenFontHandle = theBaseFontHandle;
	}

	DBG_ASSERT(aChosenFontHandle);
	theCacheEntry.fontHandle = aChosenFontHandle;
	theCacheEntry.width = u16(clamp(
		aNeededRect.right - aNeededRect.left, 1, 0xFFFF));
	theCacheEntry.height = u16(clamp(
		aNeededRect.bottom - aNeededRect.top, 1, 0xFFFF));
}


static void drawLabelString(
	DrawData& dd,
	RECT theRect,
	const std::wstring& theStr,
	const HFONT theBaseFontHandle,
	UINT theFormat,
	StringScaleCacheEntry& theCacheEntry)
{
	if( theStr.empty() )
		return;

	theFormat |= DT_NOPREFIX;

	// Initialize cache entry to get font & height
	initStringCacheEntry(
		dd, theRect, theStr, theBaseFontHandle, theFormat, theCacheEntry);

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
		SelectObject(dd.hdc, theCacheEntry.fontHandle);
		DrawText(dd.hdc, theStr.c_str(), -1, &theRect, theFormat);
	}
}


static void drawMenuItemLabel(
	DrawData& dd,
	RECT theRect,
	int theItemIdx,
	const std::string& theLabel,
	const MenuAppearance& theMenuApp,
	const MenuItemAppearance& theItemApp,
	int theCurrBorderSize, int theMaxBorderSize,
	LabelDrawCacheEntry& theCacheEntry)
{
	if( theCacheEntry.type == eMenuItemLabelType_Unknown )
	{// Initialize cache entry
		LabelIcon* aLabelIcon = sLabelIcons.find(theLabel);
		if( aLabelIcon && aLabelIcon->copyFromTarget )
		{
			theCacheEntry.type = eMenuItemLabelType_CopyRect;
			const double aHotspotScale =
				InputMap::hotspotScale(aLabelIcon->hotspotID);
			Hotspot aCopySrcHotspot =
				InputMap::getHotspot(aLabelIcon->hotspotID);
			const SIZE& aHotspotSize =
				getHotspotUnscaledSize(aLabelIcon->hotspotID);
			aCopySrcHotspot.x.offset =
				aCopySrcHotspot.x.offset - dropTo<s16>(aHotspotSize.cx / 2);
			aCopySrcHotspot.y.offset =
				aCopySrcHotspot.y.offset - dropTo<s16>(aHotspotSize.cy / 2);
			theCacheEntry.copyRect.fromPos =
				hotspotToPoint(aCopySrcHotspot, dd.targetSize);
			// TODO: Give warning that size 0x or 0y hotspot won't actually
			// copy anything to screen, and maybe switch to string type
			// of label instead in that case?
			theCacheEntry.copyRect.fromSize.cx = LONG(
				aHotspotSize.cx * aHotspotScale * gUIScale);
			theCacheEntry.copyRect.fromSize.cy = LONG(
				aHotspotSize.cy * aHotspotScale * gUIScale);
		}
		else if( aLabelIcon && !aLabelIcon->copyFromTarget )
		{
			theCacheEntry.type = eMenuItemLabelType_Bitmap;
			theCacheEntry.bitmapIconID = aLabelIcon->bitmapIconID;
		}
		else
		{
			theCacheEntry.type = eMenuItemLabelType_String;
			theCacheEntry.str = StringScaleCacheEntry();
		}
	}

	if( theCacheEntry.type == eMenuItemLabelType_String )
	{
		SetTextColor(dd.hdc, theItemApp.labelColor);
		SetBkColor(dd.hdc, theItemApp.baseColor);
		if( theMaxBorderSize > 0 )
			InflateRect(&theRect, -theMaxBorderSize, -theMaxBorderSize);
		drawLabelString(dd, theRect, widen(theLabel),
			sFonts[theMenuApp.fontID].handle,
			DT_WORDBREAK | DT_CENTER | DT_VCENTER,
			theCacheEntry.str);
	}
	else if( theCacheEntry.type == eMenuItemLabelType_Bitmap )
	{
		DBG_ASSERT(theCacheEntry.bitmapIconID < sBitmapIcons.size());
		BitmapIcon& anIcon = sBitmapIcons[theCacheEntry.bitmapIconID];
		DBG_ASSERT(anIcon.image);

		const bool shouldClipOutBorder =
			theCurrBorderSize > 0 &&
			theMenuApp.itemType != eMenuItemType_Bitmap;
		
		if( shouldClipOutBorder )
		{// Clip to within border
			RECT aClipRect = theRect;
			InflateRect(&aClipRect, -theCurrBorderSize, -theCurrBorderSize);
			DBG_ASSERT(sClipRegion);
			SetRectRgn(sClipRegion,
				aClipRect.left, aClipRect.top,
				aClipRect.right, aClipRect.bottom);
			SelectClipRgn(dd.hdc, sClipRegion);
		}

		// Draw the bitmap icon itself
		drawBitmapIcon(dd, anIcon, theRect);

		// Cleanup
		if( shouldClipOutBorder )
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
		{// Clip to within border
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
		CopyRectRefreshEntry anAutoRefreshEntry;
		anAutoRefreshEntry.overlayID = dropTo<u16>(dd.overlayID);
		anAutoRefreshEntry.itemIdx = dropTo<u16>(theItemIdx);
		// If it was already queued, bump it to the end
		sAutoRefreshCopyRectQueue.erase(std::remove(
			sAutoRefreshCopyRectQueue.begin(),
			sAutoRefreshCopyRectQueue.end(), anAutoRefreshEntry),
			sAutoRefreshCopyRectQueue.end());
		sAutoRefreshCopyRectQueue.push_back(anAutoRefreshEntry);
		// Reset clipping
		if( theCurrBorderSize > 0 )
			SelectClipRgn(dd.hdc, NULL);
	}
}


static void drawMenuTitle(
	DrawData& dd,
	const MenuAppearance& theMenuApp,
	const MenuLayout& theLayout,
	LabelDrawCacheEntry& theCacheEntry,
	int theTitleBottomY)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	RECT aRect = ps.rects[0];
	if( theMenuApp.radius )
	{
		aRect.left += LONG(theMenuApp.radius * 0.75);
		aRect.right -= LONG(theMenuApp.radius * 0.75);
	}
	aRect.bottom = theTitleBottomY;

	if( !dd.firstDraw )
		eraseRect(dd, aRect);
	const std::wstring& aWStr = widen(InputMap::menuLabel(dd.menuID));
	if( theCacheEntry.type == eMenuItemLabelType_Unknown )
	{
		theCacheEntry.type = eMenuItemLabelType_String;
		theCacheEntry.str = StringScaleCacheEntry();
	}

	UINT aFormat = DT_WORDBREAK | DT_BOTTOM;
	const EAlignment theAlignment = theLayout.alignmentX;
	switch(theAlignment)
	{
	case eAlignment_Min: aFormat |= DT_LEFT; break;
	case eAlignment_Center: aFormat |= DT_CENTER; break;
	case eAlignment_Max: aFormat |= DT_RIGHT; break;
	}
	const int aBorderSize = gUIScale > 1.0 ? int(2 * gUIScale) : 2;
	InflateRect(&aRect, -aBorderSize, -aBorderSize);

	const HFONT aFont = sFonts[theMenuApp.fontID].handle;
	initStringCacheEntry(dd, aRect, aWStr, aFont, aFormat, theCacheEntry.str);

	// Fill in 2px margin around text with titleBG (border) color
	RECT aBGRect = aRect;
	InflateRect(&aBGRect, aBorderSize, aBorderSize);
	if( theAlignment == eAlignment_Center )
	{
		aBGRect.left =
			((ps.rects[0].left + ps.rects[0].right) / 2) -
			(theCacheEntry.str.width / 2) - aBorderSize;
	}
	else if( theAlignment == eAlignment_Max )
	{
		aBGRect.left =
			aRect.right - theCacheEntry.str.width - aBorderSize;
	}
	aBGRect.right = aBGRect.left + theCacheEntry.str.width + aBorderSize * 2;
	aBGRect.top = aBGRect.bottom - theCacheEntry.str.height - aBorderSize * 2;

	const MenuItemAppearance& theTitleApp =
		getMenuItemAppearance(dd.menuID, eMenuItemDrawState_Normal);
	COLORREF oldColor = SetDCBrushColor(dd.hdc, theTitleApp.borderColor);
	FillRect(dd.hdc, &aBGRect, (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH));
	SetDCBrushColor(dd.hdc, oldColor);

	SetTextColor(dd.hdc, theMenuApp.titleColor);
	SetBkColor(dd.hdc, theTitleApp.borderColor);
	drawLabelString(dd, aRect, aWStr, aFont, aFormat, theCacheEntry.str);
}


static void drawMenuItem(
	DrawData& dd,
	const RECT& theRect,
	int theItemIdx,
	const std::string& theLabel,
	LabelDrawCacheEntry& theLabelCacheEntry)
{
	// Select appropriate appearance
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	const bool selected = theItemIdx == ps.selection;
	const bool flashing = theItemIdx == ps.flashing;
	if( selected && flashing )
		dd.itemDrawState = eMenuItemDrawState_FlashSelected;
	else if( flashing )
		dd.itemDrawState = eMenuItemDrawState_Flash;
	else if( selected )
		dd.itemDrawState = eMenuItemDrawState_Selected;
	else
		dd.itemDrawState = eMenuItemDrawState_Normal;
	const MenuAppearance& menuApp = getMenuAppearance(dd.menuID);
	const MenuItemAppearance& itemApp =
		getMenuItemAppearance(dd.menuID, dd.itemDrawState);

	// Background (usually a bordered rectangle)
	drawMenuItemBG(dd, menuApp, itemApp, theRect);

	// Label (usually word-wrapped and centered text)
	if( !theLabel.empty() )
	{
		RECT aLabelRect = theRect;
		drawMenuItemLabel(
			dd, aLabelRect,
			theItemIdx,
			theLabel,
			menuApp, itemApp,
			max(menuApp.radius / 4, int(itemApp.borderSize)),
			getMaxBorderSize(dd.menuID),
			theLabelCacheEntry);
	}

	// Flag when successfully redrew forced-redraw item
	if( ps.forcedRedrawItemID == theItemIdx )
		ps.forcedRedrawItemID = kInvalidID;
}


static void drawBasicMenu(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	const MenuLayout& layout = getMenuLayout(dd.menuID);
	const MenuAppearance& menuApp = getMenuAppearance(dd.menuID);
	const int aPrevSelection = ps.selection;
	ps.selection = dropTo<u16>(gDisabledOverlays.test(dd.overlayID)
		? kInvalidID : Menus::selectedItem(dd.menuID));
	const int anItemCount = intSize(ps.rects.size()) - 1;
	DBG_ASSERT(anItemCount == InputMap::menuItemCount(dd.menuID));
	const u8 hasTitle = layout.titleHeight > 0 ? 1 : 0;
	std::vector<LabelDrawCacheEntry>& labelCache =
		sMenuDrawCache[dd.menuID].labelCache;
	labelCache.resize(anItemCount + hasTitle);

	if( hasTitle && dd.firstDraw )
		drawMenuTitle(dd, menuApp, layout, labelCache[0], ps.rects[1].top);

	const bool flashingChanged = ps.flashing != ps.prevFlashing;
	const bool selectionChanged = ps.selection != aPrevSelection;
	const bool shouldRedrawAll =
		dd.firstDraw ||
		((layout.gapSizeX < 0 || layout.gapSizeY < 0) &&
			(flashingChanged || selectionChanged));

	bool redrawSelectedItem = false;
	for(int itemIdx = 0; itemIdx < anItemCount; ++itemIdx)
	{
		if( shouldRedrawAll ||
			ps.forcedRedrawItemID == itemIdx ||
			(selectionChanged &&
				(itemIdx == aPrevSelection || itemIdx == ps.selection)) ||
			(flashingChanged &&
				(itemIdx == ps.prevFlashing || itemIdx == ps.flashing)) )
		{
			// Non-rect shapes may not fully overwrite previous drawing,
			if( !dd.firstDraw && menuApp.itemType != eMenuItemType_Rect )
				eraseRect(dd, ps.rects[itemIdx+1]);
			if( itemIdx == ps.selection &&
				(layout.gapSizeX < 0 || layout.gapSizeY < 0) )
			{// Make sure selection is drawn on top of other items
				redrawSelectedItem = true;
			}
			else
			{
				drawMenuItem(dd, ps.rects[itemIdx+1], itemIdx,
					InputMap::menuItemLabel(dd.menuID, itemIdx),
					labelCache[itemIdx + hasTitle]);
			}
		}
	}

	// Draw selected menu item last
	if( redrawSelectedItem )
	{
		drawMenuItem(dd, ps.rects[ps.selection+1], ps.selection,
			InputMap::menuItemLabel(dd.menuID, ps.selection),
			labelCache[ps.selection + hasTitle]);
	}

	ps.prevFlashing = ps.flashing;
}


static void drawSlotsMenu(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	const MenuLayout& layout = getMenuLayout(dd.menuID);
	const MenuAppearance& menuApp = getMenuAppearance(dd.menuID);
	int aPrevSelection = ps.selection;
	ps.selection = dropTo<u16>(Menus::selectedItem(dd.menuID));
	const int anItemCount = intSize(ps.rects.size()) -
		(layout.altLabelWidth ? 2 : 1);
	DBG_ASSERT(anItemCount == InputMap::menuItemCount(dd.menuID));
	DBG_ASSERT(ps.selection < anItemCount);
	const u8 hasTitle = layout.titleHeight > 0 ? 1 : 0;
	std::vector<LabelDrawCacheEntry>& labelCache =
		sMenuDrawCache[dd.menuID].labelCache;
	labelCache.resize(anItemCount + hasTitle +
		(layout.altLabelWidth ? anItemCount : 0));

	if( hasTitle && dd.firstDraw )
		drawMenuTitle(dd, menuApp, layout, labelCache[0], ps.rects[1].top);

	const bool flashingChanged = ps.flashing != ps.prevFlashing;
	const bool selectionChanged = ps.selection != aPrevSelection;
	const bool shouldRedrawAll = dd.firstDraw || selectionChanged;

	// Draw alternate label for selected item off to the side
	if( layout.altLabelWidth &&
		(selectionChanged || shouldRedrawAll ||
		 ps.forcedRedrawItemID == ps.selection) )
	{
		std::string anAltLabel =
			InputMap::menuItemAltLabel(dd.menuID, ps.selection);
		std::string aPrevAltLabel =
			InputMap::menuItemAltLabel(dd.menuID, aPrevSelection);
		if( anAltLabel.empty() && !aPrevAltLabel.empty() )
			eraseRect(dd, ps.rects.back());
		if( !anAltLabel.empty() )
		{
			dd.itemDrawState = eMenuItemDrawState_Normal;
			RECT anAltLabelRect = ps.rects.back();
			const MenuItemAppearance& itemApp =
				getMenuItemAppearance(dd.menuID, dd.itemDrawState);
			drawBGRect(dd, itemApp, anAltLabelRect);
			drawMenuItemLabel(
				dd, anAltLabelRect,
				ps.selection,
				anAltLabel,
				menuApp, itemApp,
				itemApp.borderSize,
				getMaxBorderSize(dd.menuID),
				labelCache[anItemCount+ps.selection+hasTitle]);
		}
	}

	// Make sure only flash top slot even if selection changes during flash
	if( ps.flashing != kInvalidID )
		ps.flashing = ps.selection;

	// Draw in a wrapping fashion, starting with ps.selection+1 being drawn
	// just below the top slot, and ending when draw ps.selection last at top
	for(int compIdx = (1 % anItemCount),
		itemIdx = (ps.selection + 1) % anItemCount; /*until break*/;
		itemIdx = (itemIdx + 1) % anItemCount,
		compIdx = (compIdx + 1) % anItemCount)
	{
		const bool isSelection = itemIdx == ps.selection;
		if( isSelection )
		{
			aPrevSelection = ps.selection;
			if( gDisabledOverlays.test(dd.overlayID) )
				ps.selection = kInvalidID;
		}
		if( shouldRedrawAll ||
			ps.forcedRedrawItemID == itemIdx ||
			(isSelection && flashingChanged) )
		{
			if( !dd.firstDraw && menuApp.itemType != eMenuItemType_Rect )
				eraseRect(dd, ps.rects[compIdx+1]);
			drawMenuItem(dd, ps.rects[compIdx+1], itemIdx,
				InputMap::menuItemLabel(dd.menuID, itemIdx),
				labelCache[itemIdx + hasTitle]);
		}
		if( isSelection )
		{
			ps.selection = dropTo<u16>(aPrevSelection);
			break;
		}
	}

	ps.prevFlashing = ps.flashing;
}


static void draw4DirMenu(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	const MenuLayout& layout = getMenuLayout(dd.menuID);
	const MenuAppearance& menuApp = getMenuAppearance(dd.menuID);
	ps.selection = kInvalidID;
	const u8 hasTitle = layout.titleHeight > 0 ? 1 : 0;
	std::vector<LabelDrawCacheEntry>& labelCache =
		sMenuDrawCache[dd.menuID].labelCache;
	labelCache.resize(eCmdDir_Num + hasTitle);

	if( hasTitle && dd.firstDraw )
	{
		drawMenuTitle(dd,
			menuApp, layout, labelCache[0],
			ps.rects[1 + eCmdDir_Up].top);
	}

	for(int itemIdx = 0; itemIdx < eCmdDir_Num; ++itemIdx)
	{
		const ECommandDir aDir = ECommandDir(itemIdx);
		if( dd.firstDraw ||
			ps.forcedRedrawItemID == itemIdx ||
			(ps.flashing != ps.prevFlashing &&
				(itemIdx == ps.prevFlashing || itemIdx == ps.flashing)) )
		{
			if( !dd.firstDraw && menuApp.itemType != eMenuItemType_Rect )
				eraseRect(dd, ps.rects[itemIdx+1]);
			drawMenuItem(dd, ps.rects[itemIdx+1], itemIdx,
				InputMap::menuDirLabel(dd.menuID, aDir),
				labelCache[aDir + hasTitle]);
		}
	}

	ps.prevFlashing = ps.flashing;
}


static void drawHighlightMenu(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];

	// Title is not drawn for this menu style

	const MenuAppearance& menuApp = getMenuAppearance(dd.menuID);
	// Always draw as the selected item
	dd.itemDrawState = eMenuItemDrawState_Selected;
	MenuItemAppearance itemApp =
		getMenuItemAppearance(dd.menuID, dd.itemDrawState);
	if( !dd.firstDraw || itemApp.borderSize == 0 )
		eraseRect(dd, ps.rects[0]);

	if( itemApp.borderSize > 0 )
	{
		// Don't draw entire BG shape - just its border
		itemApp.baseColor = menuApp.transColor;
		drawMenuItemBG(dd, menuApp, itemApp, ps.rects[0]);
	}

	// Label is not drawn for this menu style
}


static void drawHUDElement(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];

	// Title is not drawn for this menu style

	if( !dd.firstDraw )
		eraseRect(dd, ps.rects[0]);

	sMenuDrawCache[dd.menuID].labelCache.resize(1);
	drawMenuItem(dd, ps.rects[0], 0,
		InputMap::menuItemLabel(dd.menuID, 0),
		sMenuDrawCache[dd.menuID].labelCache[0]);
}


static void drawHSGuide(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	const MenuLayout& layout = getMenuLayout(dd.menuID);
	DBG_ASSERT(layout.style == eMenuStyle_HotspotGuide);
	const BitVector<32>& arraysToShow = HotspotMap::getEnabledHotspotArrays();

	if( !dd.firstDraw )
		eraseRect(dd, ps.rects[0]);

	const MenuAppearance& menuApp = getMenuAppearance(dd.menuID);
	const MenuItemAppearance& itemApp =
		getMenuItemAppearance(dd.menuID, eMenuItemDrawState_Normal);
	DBG_ASSERT(itemApp.borderPen);
	SelectObject(dd.hdc, itemApp.borderPen);
	SetDCBrushColor(dd.hdc, itemApp.baseColor);
	HBRUSH hBrush = (HBRUSH)GetCurrentObject(dd.hdc, OBJ_BRUSH);

	for(int anArrayIdx = arraysToShow.firstSetBit();
		anArrayIdx < arraysToShow.size();
		anArrayIdx = arraysToShow.nextSetBit(anArrayIdx+1))
	{
		const int aHotspotCount = InputMap::sizeOfHotspotArray(anArrayIdx);
		const int aFirstHotspot = InputMap::firstHotspotInArray(anArrayIdx);
		for(int i = aFirstHotspot, end = aFirstHotspot + aHotspotCount;
			i < end; ++i)
		{
			const POINT& aHotspotPos = hotspotToPoint(
				InputMap::getHotspot(i), dd.targetSize);
			const RECT aDrawRect = {
				aHotspotPos.x - menuApp.baseRadius,
				aHotspotPos.y - menuApp.baseRadius,
				aHotspotPos.x + menuApp.baseRadius,
				aHotspotPos.y + menuApp.baseRadius };
			FillRect(dd.hdc, &aDrawRect, hBrush);
		}
	}
}


static void drawSystemOverlay(DrawData& dd)
{
	OverlayPaintState& ps = sOverlayPaintStates[dd.overlayID];
	const MenuLayout& layout = getMenuLayout(dd.menuID);
	DBG_ASSERT(layout.style == eMenuStyle_System);

	if( ((sSystemBorderFlashTimer / kSystemOverlayFlashFreq) & 0x01) != 0 &&
		sErrorMessage.empty() && sNoticeMessage.empty() )
	{
		COLORREF aFrameColor = RGB(0, 180, 0);
		HPEN hFramePen = CreatePen(PS_INSIDEFRAME, 4, aFrameColor);

		HPEN hOldPen = (HPEN)SelectObject(dd.hdc, hFramePen);
		Rectangle(dd.hdc, ps.rects[0].left, ps.rects[0].top,
			ps.rects[0].right, ps.rects[0].bottom);
		SelectObject(dd.hdc, hOldPen);

		DeleteObject(hFramePen);

		// This effectively also erased the dest region
		dd.firstDraw = true;
	}

	if( sSystemOverlayPaintFunc )
	{
		COLORREF oldColor = SetDCBrushColor(dd.hdc, transColor(dd.overlayID));
		sSystemOverlayPaintFunc(dd.hdc, ps.rects[0], dd.firstDraw);
		SetDCBrushColor(dd.hdc, oldColor);
	}

	RECT aTextRect = ps.rects[1];
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
	int theMenuID,
	OverlayPaintState& ps,
	const MenuLayout& theLayout,
	int theMenuItemCount,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect,
	POINT& theWinPos,
	SIZE& theWinSize)
{
	DBG_ASSERT(theLayout.style == eMenuStyle_Hotspots);
	ps.rects.reserve(theMenuItemCount + 1);
	ps.rects.resize(1);
	RECT aWinRect = { 0 };
	aWinRect.left = theTargetSize.cx;
	aWinRect.top = theTargetSize.cy;
	for(int i = 0; i < theMenuItemCount; ++i)
	{
		const int aHotspotID = InputMap::menuItemHotspotID(theMenuID, i);
		const POINT& anItemPos = hotspotToPoint(
			InputMap::getHotspot(aHotspotID), theTargetSize);
		SIZE anItemSize = { theLayout.sizeX, theLayout.sizeY };
		const SIZE& aHotspotSize = getHotspotUnscaledSize(aHotspotID);
		if( aHotspotSize.cx > 0 )
		{
			const double aHotspotScale = InputMap::hotspotScale(aHotspotID);
			anItemSize.cx = LONG(
				aHotspotSize.cx * aHotspotScale * gUIScale);
			anItemSize.cy = LONG(
				aHotspotSize.cy * aHotspotScale * gUIScale);
		}
		RECT anItemRect;
		anItemRect.left = anItemPos.x - anItemSize.cx / 2;
		anItemRect.right = anItemRect.left + anItemSize.cx;
		anItemRect.top = anItemPos.y - anItemSize.cy / 2;
		anItemRect.bottom = anItemRect.top + anItemSize.cy;
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
	theWinPos.x = aWinRect.left;
	theWinPos.y = aWinRect.top;
	theWinSize.cx = aWinRect.right - aWinRect.left;
	theWinSize.cy = aWinRect.bottom - aWinRect.top;

	// Make all rects be window-relative instead of target-relative
	for(std::vector<RECT>::iterator itr = ps.rects.begin();
		itr != ps.rects.end(); ++itr)
	{
		itr->left -= theWinPos.x;
		itr->top -= theWinPos.y;
		itr->right -= theWinPos.x;
		itr->bottom -= theWinPos.y;
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
		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(kHotspotSizesSectionName);
		sHotspotSizes.reserve(aPropMap->size());
		for(int i = 0; i < aPropMap->size(); ++i)
		{
			setHotspotSizes(
				aPropMap->keys()[i],
				aPropMap->vals()[i].str);
		}
	}

	{// Get bitmaps Profile Section info
		sBitmapsProfileSectionID = Profile::getSectionID(kBitmapsSectionName);
		sBitmapFiles.resize(
			Profile::getSectionProperties(sBitmapsProfileSectionID)->size());
	}

	{// Set up converting text labels to icons
		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(kIconsSectionName);
		for(int i = 0; i < aPropMap->size(); ++i)
		{
			setLabelIcon(
				aPropMap->keys()[i],
				aPropMap->vals()[i].str);
		}
	}

	// Add data from base [Appearance] section for default look
	Profile::PropertyMapPtr aDefaultProps =
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
		MenuLayout aMenuLayout;
		aMenuLayout.style = eMenuStyle_System;
		theSystemMenu.layoutID =
			dropTo<u16>(getOrCreateMenuLayoutID(aMenuLayout));
		MenuPosition aMenuPos;
		// Set draw priority to higher than any can be manually set to
		aMenuPos.drawPriority = 127;
		theSystemMenu.positionID =
			dropTo<u16>(getOrCreateMenuPositionID(aMenuPos));
	}

	{// Add data for the built-in _HotspotGuide menu
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
		anAppearance.baseRadius = dropTo<u8>(clamp((Profile::getInt(
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
		MenuLayout aMenuLayout;
		aMenuLayout.style = eMenuStyle_HotspotGuide;
		theHSGuideMenu.layoutID =
			dropTo<u16>(getOrCreateMenuLayoutID(aMenuLayout));
		MenuPosition aMenuPos;
		// Lower than any can be manually set to
		aMenuPos.drawPriority = -127;
		theHSGuideMenu.positionID =
			dropTo<u16>(getOrCreateMenuPositionID(aMenuPos));
	}

	// Bitmap source files are only needed temporarily to make
	// BitmapIcons out of, so can be freed when done creating icons
	for(int i = 0, end = sBitmapFiles.size(); i < end; ++i)
	{
		DeleteObject(sBitmapFiles[i].handle);
		sBitmapFiles[i].handle = NULL;
	}

	// Make sure paint states rects get initialized via updateWindowLayout
	gReshapeOverlays.set();
}


void loadProfileChanges()
{
	// TEMP
	init();
	updateScaling();
	gFullRedrawOverlays.set();
	// TEMP

#if 0
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

	for(int i = 0, end = sBitmapFiles.size(); i < end; ++i)
	{
		DeleteObject(sBitmapFiles[i].handle);
		sBitmapFiles[i].handle = NULL;
	}
#endif
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
		DeleteObject(sBitmapFiles[i].handle);
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

	// Check for updates to other menus
	for(int i = 0, end = InputMap::menuOverlayCount(); i < end; ++i)
	{
		const int aMenuID = Menus::activeMenuForOverlayID(i);
		switch(InputMap::menuStyle(aMenuID))
		{
		case eMenuStyle_KBCycleLast:
			if( gKeyBindCycleLastIndexChanged.test(
					InputMap::menuKeyBindCycleID(aMenuID)) )
			{
				gActiveOverlays.set(i);
				gReshapeOverlays.set(i);
			}
			break;
		case eMenuStyle_KBCycleDefault:
			if( gKeyBindCycleDefaultIndexChanged.test(
					InputMap::menuKeyBindCycleID(aMenuID)) )
			{
				gActiveOverlays.set(i);
				gReshapeOverlays.set(i);
			}
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

	// Clear auto-sized fonts and border pens so they'll regenerate at new scale
	for(int i = 0, end = intSize(sAutoSizedFonts.size()); i < end; ++i)
		DeleteObject(sAutoSizedFonts[i].handle);
	sAutoSizedFonts.clear();
	for(int i = 0, end = intSize(sPens.size()); i < end; ++i)
		DeleteObject(sPens[i].handle);
	sPens.clear();

	// Clear label cache data for each menu since they are affected by scale
	for(int i = 0, end = intSize(sMenuDrawCache.size()); i < end; ++i)
	{
		sMenuDrawCache[i].labelCache.clear();
		// Also reset max border size
		sMenuDrawCache[i].maxBorderSize = -1;
	}

	// TODO: Update things affected by hotspot sizes

	// Update radius scaling for each MenuAppearance
	for(int i = 0, end = intSize(sMenuAppearances.size()); i < end; ++i)
	{
		MenuAppearance& aMenuApp = sMenuAppearances[i];
		if( aMenuApp.baseRadius || aMenuApp.radius )
		{
			aMenuApp.radius = u8(clamp(int(
				aMenuApp.baseRadius * gUIScale), 0, 0xFF));
		}
	}

	// Re-create base font handle for each base menu font (skip dummy 0th font)
	for(int i = 1, end = intSize(sFonts.size()); i < end; ++i)
	{
		DeleteObject(sFonts[i].handle);
		sFonts[i].handle = NULL;
		createBaseFontHandle(sFonts[i]);
	}

	// Update border pen for each MenuItemAppearance
	for(int i = 0, end = intSize(sMenuItemAppearances.size()); i < end; ++i)
	{
		MenuItemAppearance& aMenuItemApp = sMenuItemAppearances[i];
		aMenuItemApp.borderSize = 0;
		if( aMenuItemApp.baseBorderSize > 0 )
		{
			aMenuItemApp.borderSize = u8(clamp(int(
				aMenuItemApp.baseBorderSize * gUIScale), 1, 0xFF));
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

	// Bitmap BG's have unpredictable shape so should always fully re-draw
	if( getMenuAppearance(theMenuID).itemType == eMenuItemType_Bitmap )
		needsInitialErase = true;

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

	if( needsInitialErase )
		eraseRect(dd, ps.rects[0]);

	switch(aMenuStyle)
	{
	case eMenuStyle_List:
	case eMenuStyle_Bar:
	case eMenuStyle_Grid:
	case eMenuStyle_Columns:		drawBasicMenu(dd);		break;
	case eMenuStyle_Slots:			drawSlotsMenu(dd);		break;
	case eMenuStyle_4Dir:			draw4DirMenu(dd);		break;
	case eMenuStlye_Ring:			/* TODO */				break;
	case eMenuStyle_Radial:			/* TODO */				break;
	case eMenuStyle_Hotspots:		drawBasicMenu(dd);		break;
	case eMenuStyle_Highlight:		drawHighlightMenu(dd);	break;
	case eMenuStyle_HUD:
	case eMenuStyle_KBCycleLast:
	case eMenuStyle_KBCycleDefault:	drawHUDElement(dd);		break;
	case eMenuStyle_HotspotGuide:	drawHSGuide(dd);		break;
	case eMenuStyle_System:			drawSystemOverlay(dd);	break;
	default:						DBG_ASSERT(false && "Invaild Menu Style");
	}
}


void updateWindowLayout(
	int theOverlayID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect,
	POINT& theWinPos,
	SIZE& theWinSize)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	DBG_ASSERT(size_t(theOverlayID) < sOverlayPaintStates.size());
	OverlayPaintState& ps = sOverlayPaintStates[theOverlayID];
	const MenuPosition& thePos = getMenuPosition(theMenuID);
	const MenuLayout& theLayout = getMenuLayout(theMenuID);
	EAlignment theAlignmentX = theLayout.alignmentX;
	EAlignment theAlignmentY = theLayout.alignmentY;

	// To prevent too many rounding errors, initially calculate everything
	// as if gUIScale has value 1.0, then apply gUIScale it in a later step.
	double aWinBasePosX = 0;
	double aWinBasePosY = 0;
	double aWinScalingPosX = 0;
	double aWinScalingPosY = 0;
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
		aWinScalingSizeX *= aMenuItemXCount;
		if( aMenuItemXCount > 1 )
			aWinScalingSizeX += theLayout.gapSizeX * (aMenuItemXCount - 1);
		aWinScalingSizeY += theLayout.titleHeight;
		break;
	case eMenuStyle_Grid:
	case eMenuStyle_Columns:
		aMenuItemCount = InputMap::menuItemCount(theMenuID);
		aMenuItemXCount = InputMap::menuGridWidth(theMenuID);
		aMenuItemYCount = InputMap::menuGridHeight(theMenuID);
		ps.rects.reserve(1 + aMenuItemCount);
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
		aWinScalingSizeX = aWinScalingSizeX * 2 + theLayout.gapSizeX;
		aWinScalingSizeY = aWinScalingSizeY * 3 + theLayout.gapSizeY * 2;
		aWinScalingSizeY += theLayout.titleHeight;
		break;
	case eMenuStyle_Hotspots:
		// Whole different function for this style
		updateHotspotsMenuLayout(
			theMenuID, ps, theLayout, aMenuItemCount,
			theTargetSize, theTargetClipRect,
			theWinPos, theWinSize);
		return;
	case eMenuStyle_HUD:
		ps.rects.reserve(1);
		break;
	case eMenuStyle_Highlight:
		ps.rects.reserve(1);
		theAlignmentX = eAlignment_Center;
		theAlignmentY = eAlignment_Center;
		{
			const int aHotspotID = InputMap::menuItemHotspotID(
					theMenuID, Menus::selectedItem(theMenuID));
			const Hotspot& aHotspot = InputMap::getHotspot(aHotspotID);
			aWinBasePosX = LONG(u16ToRangeVal(
				aHotspot.x.anchor, theTargetSize.cx));
			aWinBasePosY = LONG(u16ToRangeVal(
				aHotspot.y.anchor, theTargetSize.cy));
			aWinScalingPosX += aHotspot.x.offset;
			aWinScalingPosY += aHotspot.y.offset;
			const SIZE& aHotspotSize = getHotspotUnscaledSize(aHotspotID);
			if( aHotspotSize.cx != 0 || aHotspotSize.cy != 0 )
			{
				// Border size is added to the hotspot size in this case,
				// since hotspot size is potentially the size of an icon
				// (for copy-icon-from-dest) and border ought to be drawn
				// around the icon rather than overlapping it
				const int aSelectedBorderSize = getMenuItemAppearance(
					theMenuID, eMenuItemDrawState_Selected).borderSize;
				aWinScalingSizeX = aHotspotSize.cx + aSelectedBorderSize * 2;
				aWinScalingSizeY = aHotspotSize.cy + aSelectedBorderSize * 2;
				const double aHotspotScale = InputMap::hotspotScale(aHotspotID);
				aWinScalingSizeX *= aHotspotScale;
				aWinScalingSizeY *= aHotspotScale;
			}
		}
		break;
	case eMenuStyle_KBCycleLast:
	case eMenuStyle_KBCycleDefault:
		ps.rects.reserve(1);
		theAlignmentX = eAlignment_Center;
		theAlignmentY = eAlignment_Center;
		if( const Hotspot* aHotspot = InputMap::KeyBindCycleHotspot(
				thePos.parentKBCycleID,
				theLayout.style == eMenuStyle_KBCycleLast &&
				gKeyBindCycleLastIndex[thePos.parentKBCycleID] >= 0
					? gKeyBindCycleLastIndex[thePos.parentKBCycleID]
					: gKeyBindCycleDefaultIndex[thePos.parentKBCycleID]) )
		{
			aWinBasePosX = LONG(u16ToRangeVal(
				aHotspot->x.anchor, theTargetSize.cx));
			aWinBasePosY = LONG(u16ToRangeVal(
				aHotspot->y.anchor, theTargetSize.cy));
			aWinScalingPosX += aHotspot->x.offset;
			aWinScalingPosY += aHotspot->y.offset;
		}		
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
		DBG_ASSERT(false && "Unhandled menu style in updateWindowLayout()!");
		break;
	}

	// Get base window position (top-left corner) assuming top-left alignment
	aWinBasePosX += LONG(u16ToRangeVal(
		thePos.base.x.anchor, theTargetSize.cx));
	aWinBasePosY += LONG(u16ToRangeVal(
		thePos.base.y.anchor, theTargetSize.cy));
	aWinScalingPosX += thePos.base.x.offset;
	aWinScalingPosY += thePos.base.y.offset;

	// Adjust position according to size and alignment settings
	switch(theAlignmentX)
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
	switch(theAlignmentY)
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
	theWinPos.x = aWinClippedRect.left;
	theWinPos.y = aWinClippedRect.top;
	theWinSize.cx = max(0, int(aWinClippedRect.right - aWinClippedRect.left));
	theWinSize.cy = max(0, int(aWinClippedRect.bottom - aWinClippedRect.top));

	// Calculate component rects based on menu type
	ps.rects.clear();
	POINT aCompTopLeft =
		{ aWinPosX - theWinPos.x, aWinPosY - theWinPos.y };
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
	bool inHorizOrder = true;

	switch(theLayout.style)
	{
	case eMenuStyle_Slots:
		if( theLayout.altLabelWidth )
		{
			if( theAlignmentX == eAlignment_Max )
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
	case eMenuStyle_Columns:
		inHorizOrder = false;
		// fall through
	case eMenuStyle_Bar:
	case eMenuStyle_Grid:
		ps.rects.resize(aMenuItemCount + 1);
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
				const int aMenuItemIdx = inHorizOrder
					? y * aMenuItemXCount + x
					: x * aMenuItemYCount + y;
				if( aMenuItemIdx >= aMenuItemCount )
					continue;
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
				ps.rects[aMenuItemIdx + 1] = anItemRect;
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
		anItemRect.left = (theAlignmentX == eAlignment_Max)
			? ps.rects[0].left
			: ps.rects[1].right - aBorderSize;
		anItemRect.right = (theAlignmentX == eAlignment_Max)
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


WindowAlphaInfo alphaInfo(int theOverlayID)
{
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);
	return getMenuAlphaInfo(theMenuID);
}


RECT windowLayoutRect(int theOverlayID, int theRectIndex)
{
	DBG_ASSERT(size_t(theOverlayID) < sOverlayPaintStates.size());
	DBG_ASSERT(!sOverlayPaintStates[theOverlayID].rects.empty());
	theRectIndex = min(theRectIndex,
		intSize(sOverlayPaintStates[theOverlayID].rects.size())-1);
	return sOverlayPaintStates[theOverlayID].rects[theRectIndex];
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
