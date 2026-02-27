//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "LayoutEditor.h"
#include "Dialogs.h"
#include "InputMap.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "TargetConfigSync.h"
#include "WindowManager.h"

#include <CommCtrl.h>

namespace LayoutEditor
{

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum {
kBoundBoxAnchorDrawSize = 6,
kActiveHotspotDrawSize = 5,
kChildHotspotDrawSize = 3,
kMinOffsetScale = 25,
kMaxOffsetScale = 400,
};

const char* kHotspotsSectionName = "Hotspots";
const char* kMenuSectionPrefix = "Menu.";
const char* kPositionPropName = "Position";
const char* kItemSizePropName = "ItemSize";
const char* kAlignmentPropName = "Alignment";
const WCHAR* kAnchorOnlyLabel = L"Scaling:";
const WCHAR* kOffsetOnlyLabel = L"Offset:";
const WCHAR* kValueOnlyLabel = L"Value:";

enum EAlignment
{
	eAlignment_L_T,
	eAlignment_CX_T,
	eAlignment_R_T,
	eAlignment_L_CY,
	eAlignment_CX_CY,
	eAlignment_R_CY,
	eAlignment_L_B,
	eAlignment_CX_B,
	eAlignment_R_B,

	eAlignment_Num,
	eAlignment_VarString = eAlignment_Num,
	eAlignment_UseDefault,
};

const char* const kAlignmentStr[][2] =
{//		Droplist			Profile
	{	"Top Left",			"L, T"		}, // eAlignment_L_T
	{	"Top Center",		"CX, T"		}, // eAlignment_CX_T
	{	"Top Right",		"R, T"		}, // eAlignment_R_T
	{ 	"Center Left",		"L, CY"		}, // eAlignment_L_CY
	{ 	"Center",			"CX, CY"	}, // eAlignment_CX_CY
	{ 	"Center Right",		"R, CY"		}, // eAlignment_R_CY
	{ 	"Bottom Left",		"L, B"		}, // eAlignment_L_B
	{ 	"Bottom Center",	"CX, B"		}, // eAlignment_CX_B
	{ 	"Bottom Right",		"R, B"		}, // eAlignment_R_B
};


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct ZERO_INIT(LayoutEntry)
{
	enum EType
	{
		// These first type values are also the literal index of the
		// only object of each of these types in the entries vector
		eType_Root,
		eType_HotspotCategory,
		eType_MenuCategory,

		// These types should not be used as indexes as count can vary
		eType_Hotspot,
		eType_Menu,

		eType_Num,
		eType_CategoryNum = eType_Hotspot
	} type;
	Dialogs::TreeViewDialogItem item;

	struct Shape
	{
		struct Component
		{
			std::string base;
			int offset;
			bool useDefault;
			Component() : offset(0), useDefault(true) {}
			bool operator==(const Component& rhs) const
			{
				return
					(useDefault && rhs.useDefault) ||
					(base == rhs.base && offset == rhs.offset &&
					 !useDefault && !rhs.useDefault);
			}
			bool operator!=(const Component& rhs) const
			{ return !(*this == rhs); }
		};
		Component x, y, w, h;
		std::string scale;
		EAlignment alignment;
		std::string alignVarString;
		Shape() : alignment(eAlignment_UseDefault) {}
		bool operator==(const Shape& rhs) const
		{
			return
				x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h &&
				scale == rhs.scale && alignment == rhs.alignment;
		}
		bool operator!=(const Shape& rhs) const
		{ return !(*this == rhs); }
	} shape;

	std::vector<size_t> children;
	RECT drawnRect;
	Hotspot drawHotspot;
	float drawOffScale;
	int propSectID, menuID;
	int drawOffX, drawOffY;
	// -1 = independent/anchor/menu, 0 = range element, 1+ = full range
	int rangeCount;
	bool sizeIsARange;

	LayoutEntry() : type(eType_Num), menuID(-1), rangeCount(-1) {}
};


struct ZERO_INIT(EditorState)
{
	std::vector<LayoutEntry> entries;
	std::vector<Dialogs::TreeViewDialogItem*> dialogItems;
	LayoutEntry::Shape entered, applied;
	int activeEntry;
	POINT lastMouseDragPos;
	double unappliedDeltaX, unappliedDeltaY;
	bool needsDrawPosUpdate;
	bool draggingWithMouse;
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static EditorState* sState = null;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

// Forward declares
static void promptForEditEntry();

static inline std::string numToStringDecimal(double theNum, bool asPercent)
{
	std::string result;
	if( asPercent )
	{
		result = strFormat("%.4g%%", theNum * 100.0);
		return result;
	}

	DBG_ASSERT(theNum >= 0.0);
	result = strFormat("%.4f", theNum);
	while(
		(result.size() > 6 || result[result.size()-1] == '0') &&
		result[result.size()-2] != '.')
	{
		result.resize(result.size()-1);
	}

	return result;
}


static inline std::string numToStringDecimal(float theNum, bool asPercent)
{
	return numToStringDecimal(double(theNum), asPercent);
}


static inline std::string numToStringDecimal(int theNum, bool asPercent)
{
	if( asPercent )
		return strFormat("%d%%", theNum);
	return numToStringDecimal(theNum / 100.0, false);
}


static inline bool entryHasParent(const LayoutEntry& theEntry)
{
	// _MenuCategory acts as a "parent" (has default values),
	// but _Root and _HotspotCategory do not
	return
		theEntry.item.parentIndex != LayoutEntry::eType_Root &&
		theEntry.item.parentIndex != LayoutEntry::eType_HotspotCategory;
}


static inline bool entryIsAnchorHotspot(const LayoutEntry& theEntry)
{
	// Must be a hotspot with no parent AND have childern
	// (no children just means independent hotspot, not an anchor, thus
	// can't have an offset scale value)
	return
		theEntry.type == LayoutEntry::eType_Hotspot &&
		!entryHasParent(theEntry) &&
		!theEntry.children.empty();
}


static inline bool entryIsMenu(const LayoutEntry& theEntry)
{
	return theEntry.type == LayoutEntry::eType_Menu;
}


static inline LayoutEntry* getParentEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	DBG_ASSERT(entryHasParent(*theEntry));
	LayoutEntry* aParent = &sState->entries[theEntry->item.parentIndex];
	if( entryIsMenu(*theEntry) )
	{
		// In the context of inheritence, the "parent" menu is the root menu,
		// not the actual immediate parent used for sorting the tree view
		int aRootMenuID = InputMap::rootMenuOfMenu(theEntry->menuID);
		while(aParent->menuID != aRootMenuID && entryHasParent(*aParent))
			aParent = &sState->entries[aParent->item.parentIndex];
		return aParent;
	}
	return aParent;
}


static const LayoutEntry* getPosSourceEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	const LayoutEntry* result = theEntry;
	while(result->shape.x.useDefault && entryHasParent(*result))
		result = getParentEntry(result);
	return result;
}


static const LayoutEntry* getSizeSourceEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	const LayoutEntry* result = theEntry;
	while(result->shape.w.useDefault && entryHasParent(*result))
		result = getParentEntry(result);
	return result;
}


static const LayoutEntry* getAlignmentSourceEntry(const LayoutEntry* theEntry)
{
	DBG_ASSERT(theEntry);
	const LayoutEntry* result = theEntry;
	while(
		result->shape.alignment == eAlignment_UseDefault &&
		entryHasParent(*result))
	{
		result = getParentEntry(result);
	}
	return result;
}


static double entryScaleFactor(const LayoutEntry& theEntry)
{
	double result = 1.0;

	if( !entryIsMenu(theEntry) && entryHasParent(theEntry) )
	{
		// Find anchor hotspot
		LayoutEntry* anAnchorEntry =
			&sState->entries[theEntry.item.parentIndex];
		while(entryHasParent(*anAnchorEntry))
			anAnchorEntry = getParentEntry(anAnchorEntry);
		DBG_ASSERT(sState->activeEntry > 0);
		DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
		result = anAnchorEntry == &sState->entries[sState->activeEntry]
			? stringToFloat(Profile::expandVars(sState->entered.scale))
			: stringToFloat(Profile::expandVars(anAnchorEntry->shape.scale));
		if( result == 0 )
			result = 1.0;
	}

	return result;
}


static std::string fetchShapeScale(const std::string& theString, size_t thePos)
{
	std::string result;
	if( thePos >= theString.size() )
		return result;
	result = trim(theString.substr(thePos));
	// Verify result
	std::string aCheckStr = Profile::expandVars(result);
	char* end = null;
	const char* aCStr = aCheckStr.c_str();
	if( double aResultVal = strtod(aCStr, &end) )
	{
		// Allow for a % at end of the number
		if( *end == '%' )
		{
			aResultVal /= 100.0;
			++end;
		}
		// If haven't reached the end of the post-var-expanded string
		// when read in as a float or percent, then generate an 
		// appropriate string ourselves out of whatever we did get
		if( *end != '\0' )
			result = toString(aResultVal * 100.0) + '%';
	}
	else
	{
		// No valid numberals at all (or 0), treat as no scale specified
		result.clear();
	}

	return result;
}


static LayoutEntry::Shape::Component fetchShapeComponent(
	const std::string& theString, size_t& thePos, bool allowAnchor)
{
	LayoutEntry::Shape::Component result;
	if( thePos >= theString.size() )
		return result; // useDefault == true

	// First need to find the end of this component
	const char* aCStr = theString.c_str();
	int tagDepth = 0;
	size_t anEndPos = thePos;
	for(bool done = false; !done;)
	{
		switch(aCStr[anEndPos])
		{
		case '$':
			if( aCStr[anEndPos+1] == '{' )
			{
				anEndPos += 2;
				++tagDepth;
			}
			break;
		case '}':
			if( tagDepth )
				--tagDepth;
			++anEndPos;
			break;
		case 'x': case 'X':
			// Try to differentiate between '5 x 7' and 'CX', for example
			if( anEndPos > 0 && !tagDepth &&
				(aCStr[anEndPos-1] <= ' ' ||
				 (aCStr[anEndPos-1] >= '0' && aCStr[anEndPos-1] <= '9') ||
				 aCStr[anEndPos-1] == '%' || aCStr[anEndPos-1] == '.' ||
				 aCStr[anEndPos-1] == '}') )
			{
				done = true;
			}
			else
			{
				++anEndPos;
			}
			break;
		case ',': case '*':
			if( !tagDepth )
				done = true;
			else
				++anEndPos;
			break;
		case '\0':
			done = true;
			break;
		default:
			++anEndPos;
			break;
		}
	}

	// Extract the section of the string representing this component
	result.base = trim(theString.substr(thePos, anEndPos-thePos));

	// Advance thePos to end of component section for next component or end
	// (done here because no longer need to know start point of component and
	// anEndPos might be used as a dummy var after this)
	thePos = anEndPos;

	if( !result.base.empty() )
	{// Check if base string is actually valid (after var expansion)
		const std::string& aCheckStr = Profile::expandVars(result.base);
		if( isEffectivelyEmptyString(aCheckStr) )
			return result; // useDefault == true
		size_t aCheckPos = 0;
		if( allowAnchor )
			stringToCoord(aCheckStr, aCheckPos);
		else
			stringToDoubleSum(aCheckStr, aCheckPos);
		// If didn't make it to end of component string, its invalid
		// Directly use (post-var-expansion) whatever chars were valid
		if( aCheckPos < aCheckStr.size() )
			result.base = result.base.substr(0, aCheckPos);
	}

	// See if component string ends in a non-variable non-decimal offset value,
	// and if so bump that to the offset int and trim it off the base string
	if( !result.base.empty() )
	{
		result.useDefault = false;
		// Step back from end until encounter anything that isn't a numeral
		// or a + or - operator.
		int anOffStartPos = intSize(result.base.size()-1);
		while(anOffStartPos >= 0 &&
			  ((result.base[anOffStartPos] >= '0' &&
				result.base[anOffStartPos] <= '9') ||
			   result.base[anOffStartPos] == '-' ||
			   result.base[anOffStartPos] == '+' ||
			   result.base[anOffStartPos] <= ' '))
		{ --anOffStartPos; }
		// If reached a decimal point, step forward again to get
		// past decimal number to next int
		if( anOffStartPos >= 0 && result.base[anOffStartPos] == '.' )
		{
			while(++anOffStartPos < intSize(result.base.size()) &&
				  (result.base[anOffStartPos] >= '0' &&
				   result.base[anOffStartPos] <= '9'))
			{ ++anOffStartPos; }
		}
		if( ++anOffStartPos < intSize(result.base.size()) )
		{
			anEndPos = 0;
			result.offset = int(stringToDoubleSum(
				result.base.substr(anOffStartPos), anEndPos));
			result.base = result.base.substr(0, anOffStartPos);
		}
	}

	return result;
}


static std::string shapeCompToProfString(
	const LayoutEntry::Shape::Component& theComponent,
	bool asOffset = false)
{
	std::string result = theComponent.base;
	if( result.empty() )
	{
		if( asOffset && theComponent.offset > 0 )
			result = "+";
		result += toString(theComponent.offset);
	}
	else if( theComponent.offset )
	{
		if( theComponent.offset > 0 )
			result += std::string(" + ") + toString(theComponent.offset);
		else if( theComponent.offset < 0 )
			result += std::string(" - ") + toString(-theComponent.offset);
	}

	return result;
}


static Hotspot shapeToHotspot(const LayoutEntry::Shape& theShape)
{
	Hotspot result;
	size_t aStrPos = 0;
	result.x = stringToCoord(Profile::expandVars(theShape.x.base), aStrPos);
	result.x.offset = s16(clamp(
		result.x.offset + theShape.x.offset,
		-0x8000, 0x7FFF));

	aStrPos = 0;
	result.y = stringToCoord(Profile::expandVars(theShape.y.base), aStrPos);
	result.y.offset = s16(clamp(
		result.y.offset + theShape.y.offset,
		-0x8000, 0x7FFF));

	aStrPos = 0;
	result.w = u16(clamp(floor(
		stringToDoubleSum(Profile::expandVars(theShape.w.base), aStrPos) +
		theShape.w.offset + 0.5),
		0, 0xFFFF));

	aStrPos = 0;
	result.h = u16(clamp(floor(
		stringToDoubleSum(Profile::expandVars(theShape.h.base), aStrPos) +
		theShape.h.offset + 0.5),
		0, 0xFFFF));

	return result;
}


static void updateDrawHotspot(
	LayoutEntry& theEntry,
	const LayoutEntry::Shape& theShape,
	bool updateParentIfNeeded = true,
	bool updateChildren = true)
{
	theEntry.drawHotspot = shapeToHotspot(theShape);

	theEntry.drawOffX = theEntry.drawOffY = 0;
	theEntry.drawOffScale = entryIsAnchorHotspot(theEntry)
		? stringToFloat(Profile::expandVars(theShape.scale))
		: 1.0f;
	if( theEntry.drawOffScale <= 0 )
		theEntry.drawOffScale = 1.0f;
	if( theEntry.rangeCount > 0 )
	{
		theEntry.drawOffX = theEntry.drawHotspot.x.offset;
		theEntry.drawOffY = theEntry.drawHotspot.y.offset;
	}
	if( entryHasParent(theEntry) )
	{
		LayoutEntry* aParent = getParentEntry(&theEntry);
		if( updateParentIfNeeded )
			updateDrawHotspot(*aParent, aParent->shape, true, false);
		theEntry.drawOffScale = aParent->drawOffScale;
		if( aParent->rangeCount > 0 )
		{// Rare case where the parent is itself a range of hotspots
			aParent->drawHotspot.x.offset = s16(clamp(int(
				aParent->drawHotspot.x.offset +
				aParent->drawOffX *
				(aParent->rangeCount-1) *
				aParent->drawOffScale), -0x8000, 0x7FFF));
			aParent->drawHotspot.y.offset = s16(clamp(int(
				aParent->drawHotspot.y.offset +
				aParent->drawOffY *
				(aParent->rangeCount-1) *
				aParent->drawOffScale), -0x8000, 0x7FFF));
		}
		if( theEntry.rangeCount >= 0 )
		{// Offset from parent position
			if( theEntry.rangeCount > 0 || theEntry.drawHotspot.x.anchor == 0 )
			{
				theEntry.drawHotspot.x.anchor = aParent->drawHotspot.x.anchor;
				theEntry.drawHotspot.x.offset = s16(clamp(int(
					theEntry.drawHotspot.x.offset * theEntry.drawOffScale) +
					aParent->drawHotspot.x.offset, -0x8000, 0x7FFF));
			}
			if( theEntry.rangeCount > 0 || theEntry.drawHotspot.y.anchor == 0 )
			{
				theEntry.drawHotspot.y.anchor = aParent->drawHotspot.y.anchor;
				theEntry.drawHotspot.y.offset = s16(clamp(int(
					theEntry.drawHotspot.y.offset * theEntry.drawOffScale) +
					aParent->drawHotspot.y.offset, -0x8000, 0x7FFF));
			}
		}
		else if( theShape.x.useDefault )
		{// Inherit parent position
			DBG_ASSERT(entryIsMenu(theEntry));
			theEntry.drawHotspot.x = aParent->drawHotspot.x;
			theEntry.drawHotspot.y = aParent->drawHotspot.y;
		}
		if( theShape.w.useDefault ||
			theEntry.drawHotspot.w == 0 && theEntry.drawHotspot.h == 0 )
		{// Inherit parent size
			theEntry.drawHotspot.w = aParent->drawHotspot.w;
			theEntry.drawHotspot.h = aParent->drawHotspot.h;
		}
	}
	if( updateChildren )
	{
		for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i )
		{
			LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
			updateDrawHotspot(aChildEntry, aChildEntry.shape, false, true);
		}
	}
}


static void applyNewLayoutProperties(bool toFile = false)
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& theEntry = sState->entries[sState->activeEntry];
	LayoutEntry::Shape* aNewShape = &sState->entered;
	LayoutEntry::Shape* anOldShape1 = &sState->applied;
	LayoutEntry::Shape* anOldShape2 =
		toFile ? &theEntry.shape : &sState->applied;
	const bool needNewPos =
		aNewShape->x != anOldShape1->x || aNewShape->x != anOldShape2->x ||
		aNewShape->y != anOldShape1->y || aNewShape->y != anOldShape2->y;
	const bool needNewScale =
		aNewShape->scale != anOldShape1->scale ||
		aNewShape->scale != anOldShape2->scale;
	const bool needNewSize =
		aNewShape->w != anOldShape1->w || aNewShape->w != anOldShape2->w ||
		aNewShape->h != anOldShape1->h || aNewShape->h != anOldShape2->h;
	const bool needNewAlign =
		aNewShape->alignment != anOldShape1->alignment ||
		aNewShape->alignment != anOldShape2->alignment;
	if( !needNewPos && !needNewSize && !needNewScale && !needNewAlign )
		return;

	const bool isHotspot = theEntry.type == LayoutEntry::eType_Hotspot;
	std::string aPosStr;
	if( isHotspot || (needNewPos && !aNewShape->x.useDefault) )
	{
		const bool asOffset = theEntry.rangeCount >= 0;
		aPosStr =
			shapeCompToProfString(aNewShape->x, asOffset) + ", " +
			shapeCompToProfString(aNewShape->y, asOffset);
	}
	std::string aSizeStr;
	if( !aNewShape->w.useDefault && (needNewSize || isHotspot) )
	{
		aSizeStr =
			shapeCompToProfString(aNewShape->w) + ", " +
			shapeCompToProfString(aNewShape->h);
	}

	if( isHotspot )
	{// Combine everything into a single property string for hotspot/range
		std::string aScaleStr;
		if( entryIsAnchorHotspot(theEntry) &&
			stringToFloat(aNewShape->scale) >= 0 )
		{
			aScaleStr = aNewShape->scale;
		}
		std::string aProfileStr = aPosStr;
		if( !aSizeStr.empty() )
			aProfileStr += ", " + aSizeStr;
		if( !aScaleStr.empty() )
			aProfileStr += " * " + aScaleStr;
		Profile::setStr(
			kHotspotsSectionName, theEntry.item.name,
			aProfileStr, toFile);
	}
	else // if( entryIsMenu(theEntry) )
	{// Each string is saved to a separate property string within menu section
		const int aProfileSect = InputMap::menuSectionID(theEntry.menuID);
		if( needNewPos )
			Profile::setStr(aProfileSect, kPositionPropName, aPosStr, toFile);
		if( needNewSize )
			Profile::setStr(aProfileSect, kItemSizePropName, aSizeStr, toFile);
		if( needNewAlign )
		{
			if( aNewShape->alignment == eAlignment_UseDefault )
			{
				Profile::setStr(aProfileSect, kAlignmentPropName,
					"", toFile);
			}
			else if( aNewShape->alignment == eAlignment_VarString )
			{
				Profile::setStr(aProfileSect, kAlignmentPropName,
					aNewShape->alignVarString, toFile);
			}
			else
			{
				Profile::setStr(aProfileSect, kAlignmentPropName,
					kAlignmentStr[aNewShape->alignment][1], toFile);
			}
		}
	}

	sState->applied = sState->entered;
	if( toFile )
	{
		theEntry.shape = sState->entered;
		Profile::saveChangesToFile();
	}
}


static void cancelRepositioning()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( !gShutdown && sState->applied != anEntry.shape )
	{
		sState->entered = anEntry.shape;
		applyNewLayoutProperties();
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static void saveNewPosition()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	applyNewLayoutProperties(true);
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static void setInitialToolbarPos(HWND theDialog, LayoutEntry& theEntry)
{
	if( !sState || !sState->activeEntry )
		return;

	// Position the tool bar as far as possible from the object to be moved,
	// so that it is less likely to end up overlapping the object
	POINT anEntryPos;
	if( entryIsMenu(theEntry) )
	{
		const RECT& anEntryRect = WindowManager::overlayRect(
			InputMap::menuOverlayID(theEntry.menuID));
		anEntryPos.x = (anEntryRect.left + anEntryRect.right) / 2;
		anEntryPos.y = (anEntryRect.top + anEntryRect.bottom) / 2;
	}
	else
	{
		updateDrawHotspot(theEntry, theEntry.shape);
		anEntryPos = WindowManager::hotspotToOverlayPos(theEntry.drawHotspot);
	}
	RECT anOverlayRect;
	anOverlayRect = WindowManager::overlayClipRect();
	POINT anOverlayCenter;
	anOverlayCenter.x = (anOverlayRect.left + anOverlayRect.right) / 2;
	anOverlayCenter.y = (anOverlayRect.top + anOverlayRect.bottom) / 2;

	const bool useRightEdge = anEntryPos.x < anOverlayCenter.x;
	const bool useBottomEdge = anEntryPos.y < anOverlayCenter.y;

	RECT aDialogRect;
	GetWindowRect(theDialog, &aDialogRect);
	const int aDialogWidth = aDialogRect.right - aDialogRect.left;
	const int aDialogHeight = aDialogRect.bottom - aDialogRect.top;

	SetWindowPos(theDialog, NULL,
		useRightEdge
			? anOverlayRect.right - aDialogWidth
			: anOverlayRect.left,
		useBottomEdge
			? anOverlayRect.bottom - aDialogHeight
			: anOverlayRect.top,
		0, 0, SWP_NOZORDER | SWP_NOSIZE);
}


static std::pair<POINT, SIZE> getControlPos(HWND theDialog, HWND theControl)
{
	DBG_ASSERT(theDialog && theControl);
	RECT aRect;
	GetWindowRect(theControl, &aRect);
	POINT aTopLeftPt = { aRect.left, aRect.top };
	POINT aBottomRightPt = { aRect.right, aRect.bottom };
	ScreenToClient(theDialog, &aTopLeftPt);
	ScreenToClient(theDialog, &aBottomRightPt);
	SIZE aControlSize = {
		aBottomRightPt.x - aTopLeftPt.x,
		aBottomRightPt.y - aTopLeftPt.y };
	return std::make_pair(aTopLeftPt, aControlSize);
}


static std::string getControlText(HWND theDialog, int theControlID)
{
	std::string result;
	HWND hCntrl = GetDlgItem(theDialog, theControlID);
	DBG_ASSERT(hCntrl);
	if( int aStrLen = GetWindowTextLength(hCntrl) )
	{
		std::vector<WCHAR> aStrBuf(aStrLen+1);
		GetDlgItemText(theDialog, theControlID, &aStrBuf[0], aStrLen+1);
		result = narrow(&aStrBuf[0]);
	}
	return result;
}


static void adjustComboBoxDroppedWidth(HWND hCombo)
{
	DBG_ASSERT(IsWindow(hCombo));

	// Get current dropped width so we don't shrink it
	const int aCurrWidth = int(SendMessage(hCombo, CB_GETDROPPEDWIDTH, 0, 0));

	// Get number of items
	const int anItemCount = int(SendMessage(hCombo, CB_GETCOUNT, 0, 0));
	if( anItemCount <= 0 )
		return;

	HDC hdc = GetDC(hCombo);
	if( !hdc )
		return;

	// Select combo box font into DC
	HFONT hFont = (HFONT)SendMessage(hCombo, WM_GETFONT, 0, 0);
	HFONT hOldFont = NULL;
	if( hFont )
		hOldFont = (HFONT)SelectObject(hdc, hFont);

	LONG aMaxTextWidth = 0;

	for(int i = 0; i < anItemCount; ++i)
	{
		int aTextLen = int(SendMessage(hCombo, CB_GETLBTEXTLEN, i, 0));
		if( aTextLen <= 0 )
			continue;

		// +1 for null terminator
		std::vector<WCHAR> aBuffer(aTextLen + 1);
		aBuffer[0] = L'\0';

		SendMessage(hCombo, CB_GETLBTEXT, i, (LPARAM)&aBuffer[0]);

		SIZE aSize = { 0, 0 };
		if( GetTextExtentPoint32(hdc, &aBuffer[0], aTextLen, &aSize) )
			aMaxTextWidth = max(aMaxTextWidth, aSize.cx);
	}

	// Restore font and DC
	if( hOldFont )
		SelectObject(hdc, hOldFont);
	ReleaseDC(hCombo, hdc);

	// Add some padding
	aMaxTextWidth += 8;

	// Only expand, never shrink
	if( aMaxTextWidth > aCurrWidth )
		SendMessage(hCombo, CB_SETDROPPEDWIDTH, aMaxTextWidth, 0);
}


static void setupPositionControls(
	HWND theDialog,
	const LayoutEntry& theEntry,
	const LayoutEntry::Shape::Component& theComponent,
	bool isY)
{
	HWND hComboBox = GetDlgItem(theDialog,
		isY ? IDC_COMBO_Y_BASE : IDC_COMBO_X_BASE);
	HWND hEditBox = GetDlgItem(theDialog, (isY ? IDC_EDIT_Y : IDC_EDIT_X));
	SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
	if( theComponent.useDefault )
	{
		SetWindowText(hEditBox, L"");
		return;
	}

	int aDropListSize = 0;
	if( theEntry.rangeCount <= 0 )
	{
		SendMessage(hComboBox,CB_ADDSTRING, 0,
			(LPARAM)(isY ? L"T" : L"L"));
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)(isY ? L"CY" : L"CX"));
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)(isY ? L"B" : L"R"));
		SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)kAnchorOnlyLabel);
		aDropListSize = 4;
	}
	const bool canBeOffsetOnly = theEntry.rangeCount >= 0;
	// Set initial selection to one of the above if appropriate
	const double anAnchorVal = theComponent.offset
			? 0 : stringToDouble(theComponent.base, true);
	const bool isCustomAnchor = theEntry.rangeCount <= 0 &&
		!_isnan(anAnchorVal) && anAnchorVal != 0 && anAnchorVal < 1.0;
	if( canBeOffsetOnly )
	{
		SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)kOffsetOnlyLabel);
		++aDropListSize;
	}
	if( isCustomAnchor )
	{
		// Entry is anchor-only (percent of screen area)
		SendMessage(hComboBox, CB_SETCURSEL, (WPARAM)3, 0);
		SetWindowText(hEditBox, widen(theComponent.base).c_str());
	}
	else if( canBeOffsetOnly && theComponent.base.empty() )
	{
		SendMessage(hComboBox, CB_SETCURSEL, WPARAM(aDropListSize-1), 0);
	}
	else
	{
		// Check what hotspot coord would be if read ONLY base string
		size_t aPos = 0;
		const Hotspot::Coord& aCoord = stringToCoord(
			Profile::expandVars(theComponent.base), aPos);
		// If string contained an offset or unkwnon characters
		// (likely ${} variables), or has an anchor even though
		// it is a range, then need to include custom string option
		bool useStringBase = aCoord.offset != 0 ||
			(!theComponent.base.empty() && aPos < theComponent.base.size()) ||
			(theEntry.rangeCount > 0 && aCoord.anchor != 0);
		if( !useStringBase )
		{// Check if string is simply a known basic anchor type
			switch(aCoord.anchor)
			{
			case 0:
			case 0x0001: // L or T
				SendMessage(hComboBox, CB_SETCURSEL, 0, 0);
				break;
			case 0x8000: // CX or CY
				SendMessage(hComboBox, CB_SETCURSEL, 1, 0);
				break;
			case 0xFFFF: // R or B
				SendMessage(hComboBox, CB_SETCURSEL, 2, 0);
				break;
			default: // non-standard anchor value
				useStringBase = true;
				break;
			}
		}
		if( useStringBase )
		{
			SendMessage(hComboBox, CB_ADDSTRING, 0,
				(LPARAM)widen(theComponent.base).c_str());
			SendMessage(hComboBox, CB_SETCURSEL, WPARAM(aDropListSize), 0);
		}
	}
	adjustComboBoxDroppedWidth(hComboBox);
	if( !isCustomAnchor )
	{// Set offset field
		SetWindowText(
			hEditBox,
			((theComponent.offset >= 0 ? L"+" : L"") +
			 widen(toString(theComponent.offset))).c_str());
	}
}


static void setupSizeControls(
	HWND theDialog,
	const LayoutEntry::Shape::Component& theComponent,
	bool isH)
{
	HWND hComboBox = GetDlgItem(theDialog,
		isH ? IDC_COMBO_H_BASE : IDC_COMBO_W_BASE);
	HWND hEditBox = GetDlgItem(theDialog, (isH ? IDC_EDIT_H : IDC_EDIT_W));
	SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
	if( theComponent.useDefault )
	{
		SetWindowText(hEditBox, L"");
		return;
	}

	const bool useStringBase = !theComponent.base.empty();
	int aSelection = 0;
	SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)kValueOnlyLabel);
	if( useStringBase )
	{// Use option of custom base string + offset
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)widen(theComponent.base).c_str());
		aSelection = 1;
	}
	adjustComboBoxDroppedWidth(hComboBox);
	SendMessage(hComboBox, CB_SETCURSEL, (WPARAM)aSelection, 0);

	SetWindowText(hEditBox,
		((useStringBase && theComponent.offset >= 0 ? L"+" : L"") +
		 widen(toString(theComponent.offset))).c_str());
}


static bool setupScaleControls(
	HWND theDialog, const std::string& theScaleString)
{
	enum EScaleType
	{
		eScaleType_None,
		eScaleType_Number,
		eScaleType_Var,
	} aScaleType = eScaleType_None;

	float aCalculatedScale = 0;
	std::string aCalculatedScaleString;
	if( !theScaleString.empty() )
	{
		const float aSimpleScale = stringToFloat(theScaleString);
		aCalculatedScale = stringToFloat(Profile::expandVars(theScaleString));
		aCalculatedScaleString = numToStringDecimal(aCalculatedScale, false);
		if( !aCalculatedScale )
			aScaleType = eScaleType_None;
		else if( aCalculatedScale == aSimpleScale )
			aScaleType = eScaleType_Number;
		else
			aScaleType = eScaleType_Var;
	}

	switch(aScaleType)
	{
	case eScaleType_None:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
			EM_SETREADONLY, true, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S), L"");
		ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
		ShowWindow(GetDlgItem(theDialog, IDC_EDIT_S_VAR), false);
		EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), false);
		SendDlgItemMessage(theDialog, IDC_SLIDER_S,
			TBM_SETPOS, TRUE, LPARAM(100));
		break;

	case eScaleType_Number:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
			EM_SETREADONLY, false, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
			widen(theScaleString).c_str());
		ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
		ShowWindow(GetDlgItem(theDialog, IDC_EDIT_S_VAR), false);
		EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
		SendDlgItemMessage(theDialog, IDC_SLIDER_S,
			TBM_SETPOS, TRUE, LPARAM(100 * aCalculatedScale));
		break;

	case eScaleType_Var:
		SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
			EM_SETREADONLY, true, 0);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
			widen(aCalculatedScaleString).c_str());
		ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), false);
		SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S_VAR),
			widen(theScaleString).c_str());
		ShowWindow(GetDlgItem(theDialog, IDC_EDIT_S_VAR), true);
		break;
	}

	return aScaleType == eScaleType_Number;
}


static void setupAlignmentControls(
	HWND theDialog, const LayoutEntry::Shape& theShape)
{
	HWND hComboBox = GetDlgItem(theDialog, IDC_COMBO_ALIGN);
	SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
	if( theShape.alignment == eAlignment_UseDefault )
		return;

	for(int i = 0; i < eAlignment_Num; ++i)
	{
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)widen(kAlignmentStr[i][0]).c_str());
	}
	if( theShape.alignment == eAlignment_VarString )
	{
		DBG_ASSERT(!theShape.alignVarString.empty());
		SendMessage(hComboBox, CB_ADDSTRING, 0,
			(LPARAM)widen(theShape.alignVarString).c_str());
	}
	adjustComboBoxDroppedWidth(hComboBox);
	SendMessage(hComboBox, CB_SETCURSEL,
		(WPARAM)theShape.alignment, 0);
}


static void processCoordFieldChange(
	HWND theDialog,
	int theControlID,
	int theDelta = 0,
	bool theDeltaSetByMouse = false)
{
	if( !theDialog )
		return;

	LayoutEntry::Shape::Component& aComp =
		theControlID == IDC_EDIT_X	? sState->entered.x :
		theControlID == IDC_EDIT_Y	? sState->entered.y :
		theControlID == IDC_EDIT_W	? sState->entered.w :
		/*theControlID == IDC_EDIT_H*/sState->entered.h;

	if( aComp.useDefault )
		return;

	const int theBaseControlID = IDC_COMBO_X_BASE + (theControlID - IDC_EDIT_X);
	const std::string& aBaseControlStr =
		getControlText(theDialog, theBaseControlID);
	const bool isAnchorOnly = aBaseControlStr == narrow(kAnchorOnlyLabel);
	const bool isOffset = !isAnchorOnly &&
		aBaseControlStr != narrow(kValueOnlyLabel);
	const std::string& aControlStr = getControlText(theDialog, theControlID);
	size_t aStrPos = 0;
	double aNewValue = stringToDoubleSum(aControlStr, aStrPos);
	const bool isInPercent =
		isAnchorOnly &&
		aStrPos < aControlStr.size() &&
		aControlStr[aStrPos] == '%';
	if( isInPercent )
		aNewValue /= 100.0;

	if( theDelta != 0 )
	{
		const bool isY = theControlID == IDC_EDIT_Y;
		const double aWinMax = isY
			? WindowManager::overlayTargetSize().cy
			: WindowManager::overlayTargetSize().cx;
		if( isAnchorOnly )
		{// Have each point of delta count as 1 pixel of anchor
			aNewValue = (aNewValue * aWinMax + theDelta) / aWinMax;
		}
		else if( theDeltaSetByMouse )
		{// Adjust for scale and possibly auto-swap anchor type
			if( gUIScale != 1.0 )
			{
				double* aDeltaFP = isY
					? &sState->unappliedDeltaY
					: &sState->unappliedDeltaX;
				*aDeltaFP += double(theDelta) / gUIScale;
				theDelta = int(*aDeltaFP);
				*aDeltaFP -= theDelta;
			}
			aNewValue += theDelta;
			// Auto-swap basic basic named anchor type if new position is
			// very clase to a different basic named anchor point (edge/center)
			const double anAnchorPercent =
				isY && aBaseControlStr == "T"	? 0.0 :
				isY && aBaseControlStr == "CY"	? 0.5 :
				isY && aBaseControlStr == "B"	? 1.0 :
				!isY && aBaseControlStr == "L"	? 0.0 :
				!isY && aBaseControlStr == "CX"	? 0.5 :
				!isY && aBaseControlStr == "R"	? 1.0 :
				/*otherwise*/					  -1.0;
			if( anAnchorPercent >= 0.0 )
			{// Must be one of the named anchor points, can auto-swap
				const double aWinPos = clamp(
					floor(aWinMax * anAnchorPercent + 0.5) + aNewValue,
					0, aWinMax-1.0);
				double aNewAnchorPercent = anAnchorPercent;
				if( anAnchorPercent > 0.0 && aWinPos <= aWinMax * 0.02 )
				{// Swap to left/top anchor
					aNewAnchorPercent = 0;
				}
				else if( anAnchorPercent < 1.0 && aWinPos >= aWinMax * 0.98 )
				{// Swap to right/bottom anchor
					aNewAnchorPercent = 1.0;
				}
				// Could also swap to center anchor, but perhaps only if both
				// X and Y are near center point, and past testing showed this
				// to be more annoying than helpful, so left out for now.
				if( aNewAnchorPercent != anAnchorPercent )
				{
					const int aNewAnchorType =
						aNewAnchorPercent == 0.0 ? 0 :
						aNewAnchorPercent == 0.5 ? 1 :
						/*aNewAnchorPercent == 1.0*/2;		
					HWND hComboBox = GetDlgItem(theDialog, theBaseControlID);
					SendMessage(hComboBox, CB_SETCURSEL, aNewAnchorType, 0);
					SendMessage(theDialog, WM_COMMAND,
						MAKEWPARAM(theBaseControlID, CBN_SELCHANGE),
						(LPARAM)hComboBox);
					// Above will change offset, base string, etc, so need to
					// restart processing of coordinate string from the top
					// (treating theDelta as direct rather than mouse-based
					// since have already adjusted it for that here).
					processCoordFieldChange(theDialog, theControlID, theDelta);
					return;
				}
			}
		}
		else
		{// Just directly shift the offset by requested amount (spinner)
			aNewValue += theDelta;
		}
	}

	if( theControlID == IDC_EDIT_W || theControlID == IDC_EDIT_H )
	{// Do not allow total value of the component to be < 1
		aStrPos = 0;
		const double aBaseValue = !isOffset ? 0.0 :
			stringToDoubleSum(Profile::expandVars(aBaseControlStr), aStrPos);
		if( floor(aBaseValue) + floor(aNewValue) < 1.0 )
			aNewValue = ceil(1.0 - floor(aBaseValue));
	}

	std::string aFormattedString;
	if( isAnchorOnly )
	{// Allow decimals/percent in final string
		DBG_ASSERT(theControlID == IDC_EDIT_X || theControlID == IDC_EDIT_Y);
		aNewValue = clamp(aNewValue, 0.0, 1.0);
		aFormattedString = numToStringDecimal(aNewValue, isInPercent);
	}
	else
	{// Only allow integer values in final string
		aNewValue = clamp(floor(aNewValue + 0.5), -0x8000, 0x7FFF);
		aFormattedString =
			(aNewValue >= 0 && isOffset ? "+" : "") +
			toString(int(aNewValue));
	}
	if( aFormattedString != aControlStr )
	{
		SetWindowText(
			GetDlgItem(theDialog, theControlID),
			widen(aFormattedString).c_str());
	}

	// Now actually apply the string to the component
	// For anchor-only apply to .base string, otherwise offset int
	if( isAnchorOnly )
	{
		if( aComp.base != aFormattedString )
		{
			aComp.base = aFormattedString;
			sState->needsDrawPosUpdate = true;
			gRefreshOverlays.set(kSystemOverlayID);
		}
	}
	else if( aComp.offset != int(aNewValue) )
	{
		aComp.offset = int(aNewValue);
		sState->needsDrawPosUpdate = true;
		gRefreshOverlays.set(kSystemOverlayID);
	}
}


static INT_PTR CALLBACK editLayoutToolbarProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	const LayoutEntry* aSrcEntry = null;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Set title to match the entry name and display scaling being applied
		{
			const double aScaling = entryScaleFactor(anEntry) * gUIScale;
			if( aScaling != 1 )
			{
				SetWindowText(theDialog,
					(L"Repositioning: " +
					 widen(anEntry.item.name) +
					 L" (at " +
					 widen(numToStringDecimal(aScaling, true)) +
					 L" scale)").c_str());
			}
			else
			{
				SetWindowText(theDialog,
					(L"Repositioning: " +
					 widen(anEntry.item.name)).c_str());
			}
		}
		// Allow Esc to cancel even when not the active window
		RegisterHotKey(NULL, kCancelToolbarHotkeyID, 0, VK_ESCAPE);
		
		// Populate controls with initial values from active entry
		// Position (X & Y)
		aSrcEntry = getPosSourceEntry(&anEntry);
		setupPositionControls(theDialog, *aSrcEntry, aSrcEntry->shape.x, false);
		setupPositionControls(theDialog, *aSrcEntry, aSrcEntry->shape.y, true);
		if( aSrcEntry != &anEntry )
		{
			DBG_ASSERT(entryIsMenu(anEntry));
			SendMessage(GetDlgItem(theDialog, IDC_EDIT_X),
				EM_SETREADONLY, true, 0);
			EnableWindow(GetDlgItem(theDialog, IDC_COMBO_X_BASE), false);
			EnableWindow(GetDlgItem(theDialog, IDC_SPIN_X), false);
			SendMessage(GetDlgItem(theDialog, IDC_EDIT_Y),
				EM_SETREADONLY, true, 0);
			EnableWindow(GetDlgItem(theDialog, IDC_COMBO_Y_BASE), false);
			EnableWindow(GetDlgItem(theDialog, IDC_SPIN_Y), false);
		}
		else if( entryIsMenu(anEntry) )
		{
			CheckDlgButton(theDialog, IDC_CHECK_POSITION, true);
		}
		
		// Size (W & H)
		aSrcEntry = getSizeSourceEntry(&anEntry);
		setupSizeControls(theDialog, aSrcEntry->shape.w, false);
		setupSizeControls(theDialog, aSrcEntry->shape.h, true);
		if( aSrcEntry != &anEntry || aSrcEntry->shape.w.useDefault )
		{
			SendMessage(GetDlgItem(theDialog, IDC_EDIT_W),
				EM_SETREADONLY, true, 0);
			EnableWindow(GetDlgItem(theDialog, IDC_COMBO_W_BASE), false);
			EnableWindow(GetDlgItem(theDialog, IDC_SPIN_W), false);
			SendMessage(GetDlgItem(theDialog, IDC_EDIT_H),
				EM_SETREADONLY, true, 0);
			EnableWindow(GetDlgItem(theDialog, IDC_COMBO_H_BASE), false);
			EnableWindow(GetDlgItem(theDialog, IDC_SPIN_H), false);
		}
		else
		{
			CheckDlgButton(theDialog, IDC_CHECK_SIZE, true);
		}

		if( entryIsMenu(anEntry) )
		{// Alignment
			aSrcEntry = getAlignmentSourceEntry(&anEntry);
			if( aSrcEntry != &anEntry ||
				aSrcEntry->shape.alignment == eAlignment_UseDefault )
			{
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_ALIGN), false);
			}
			else
			{
				CheckDlgButton(theDialog, IDC_CHECK_ALIGNMENT, true);
			}
			setupAlignmentControls(theDialog, aSrcEntry->shape);
		}

		if( entryIsAnchorHotspot(anEntry) )
		{// Scale
			SendDlgItemMessage(theDialog, IDC_SLIDER_S,
				TBM_SETRANGE, TRUE,
				MAKELPARAM(kMinOffsetScale, kMaxOffsetScale));
			if( setupScaleControls(theDialog, anEntry.shape.scale) )
				CheckDlgButton(theDialog, IDC_CHECK_SCALE, true);
		}

		{// Adjust spinner control positioning/size slightly
			HWND hCntrl = GetDlgItem(theDialog, IDC_SPIN_X);
			std::pair<POINT, SIZE> aCtrlPos = getControlPos(theDialog, hCntrl);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hCntrl, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
			hCntrl = GetDlgItem(theDialog, IDC_SPIN_Y);
			aCtrlPos = getControlPos(theDialog, hCntrl);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hCntrl, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
			hCntrl = GetDlgItem(theDialog, IDC_SPIN_W);
			aCtrlPos = getControlPos(theDialog, hCntrl);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hCntrl, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
			hCntrl = GetDlgItem(theDialog, IDC_SPIN_H);
			aCtrlPos = getControlPos(theDialog, hCntrl);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hCntrl, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
		}

		setInitialToolbarPos(theDialog, anEntry);
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				if( HWND hFocus = GetFocus() )
				{
					switch(GetDlgCtrlID(hFocus))
					{
					case IDC_EDIT_X: case IDC_EDIT_Y:
					case IDC_EDIT_W: case IDC_EDIT_H: case IDC_EDIT_S:
						// Set focus to OK button but don't click it yet
						// (so EN_KILLFOCUS applies changes in the field)
						PostMessage(theDialog, WM_NEXTDLGCTL,
							(WPARAM)GetDlgItem(theDialog, IDOK), TRUE);
						return (INT_PTR)TRUE;
					default:
						saveNewPosition();
						promptForEditEntry();
						return (INT_PTR)TRUE;
					}
				}
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				cancelRepositioning();
				promptForEditEntry();
				return (INT_PTR)TRUE;
			}
			break;

		case IDC_EDIT_X: case IDC_EDIT_Y:
		case IDC_EDIT_W: case IDC_EDIT_H:
			if( HIWORD(wParam) == EN_KILLFOCUS )
			{
				processCoordFieldChange(theDialog, LOWORD(wParam));
				applyNewLayoutProperties();
			}
			break;

		case IDC_EDIT_S:
			if( HIWORD(wParam) == EN_KILLFOCUS &&
				IsDlgButtonChecked(theDialog, IDC_CHECK_SCALE) == BST_CHECKED )
			{
				const std::string& aControlStr =
					getControlText(theDialog, IDC_EDIT_S);
				size_t aStrPos = 0;
				double aNewValue = stringToDoubleSum(aControlStr, aStrPos);
				const bool isInPercent =
					aStrPos < aControlStr.size() && aControlStr[aStrPos] == '%';
				if( isInPercent )
					aNewValue /= 100.0;
				if( aNewValue < 0.01 )
					aNewValue = 0.01;
				if( aControlStr != sState->entered.scale )
				{
					sState->entered.scale =
						numToStringDecimal(aNewValue, isInPercent);
					sState->needsDrawPosUpdate = true;
					gRefreshOverlays.set(kSystemOverlayID);
					applyNewLayoutProperties();
					// Update slider to match
					SendDlgItemMessage(theDialog, IDC_SLIDER_S,
						TBM_SETPOS, TRUE,
						LPARAM(aNewValue * 100.0));
					if( aControlStr != sState->entered.scale )
					{
						SetWindowText(
							GetDlgItem(theDialog, IDC_EDIT_S),
							widen(sState->entered.scale).c_str());
					}
				}
			}
			break;

		case IDC_CHECK_POSITION:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_POSITION)
						== BST_CHECKED;
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_X),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_X_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_X), isChecked);
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_Y),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_Y_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_Y), isChecked);
				const LayoutEntry* aSrcEntry;
				if( isChecked )
				{// Begin using own custom values (or duplicate of parent's)
					aSrcEntry = getPosSourceEntry(&anEntry);
					sState->entered.x = aSrcEntry->shape.x;
					sState->entered.y = aSrcEntry->shape.y;
					if( sState->entered.x.useDefault ||
						sState->entered.y.useDefault )
					{// Don't defer to defaults while this is checked!
						sState->entered.x.useDefault = false;
						sState->entered.y.useDefault = false;
					}
				}
				else
				{// Set to defer to default values
					sState->entered.x = LayoutEntry::Shape::Component();
					sState->entered.y = LayoutEntry::Shape::Component();
					aSrcEntry = getPosSourceEntry(
						entryHasParent(anEntry)
							? getParentEntry(&anEntry)
							: &anEntry);
				}
				DBG_ASSERT(aSrcEntry);
				setupPositionControls(theDialog, *aSrcEntry,
					sState->entered.x.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.x : sState->entered.x,
						false);
				setupPositionControls(theDialog, *aSrcEntry,
					sState->entered.y.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.y : sState->entered.y,
						true);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_CHECK_SIZE:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_SIZE)
						== BST_CHECKED;
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_W),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_W_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_W), isChecked);
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_H),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_H_BASE),isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_SPIN_H), isChecked);
				const LayoutEntry* aSrcEntry;
				if( isChecked )
				{// Begin using own custom values (or duplicate of parent's)
					aSrcEntry = getSizeSourceEntry(&anEntry);
					sState->entered.w = aSrcEntry->shape.w;
					sState->entered.h = aSrcEntry->shape.h;
					if( sState->entered.w.useDefault ||
						sState->entered.h.useDefault )
					{// Don't defer to defaults while this is checked!
						sState->entered.w.useDefault = false;
						sState->entered.h.useDefault = false;
						sState->entered.w.offset = 1;
						sState->entered.h.offset = 1;
					}
				}
				else
				{// Set to defer to default values
					sState->entered.w = LayoutEntry::Shape::Component();
					sState->entered.h = LayoutEntry::Shape::Component();
					aSrcEntry = getSizeSourceEntry(
						entryHasParent(anEntry)
							? getParentEntry(&anEntry)
							: &anEntry);
				}
				DBG_ASSERT(aSrcEntry);
				setupSizeControls(theDialog,
					sState->entered.w.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.w : sState->entered.w,
						false);
				setupSizeControls(theDialog,
					sState->entered.h.useDefault && aSrcEntry != &anEntry
						? aSrcEntry->shape.h : sState->entered.h,
						true);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_CHECK_ALIGNMENT:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_ALIGNMENT)
						== BST_CHECKED;
				EnableWindow(GetDlgItem(theDialog, IDC_COMBO_ALIGN), isChecked);
				const LayoutEntry* aSrcEntry;
				if( isChecked )
				{// Begin using own custom values (or duplicate of parent's)
					aSrcEntry = getAlignmentSourceEntry(&anEntry);
					sState->entered.alignment = aSrcEntry->shape.alignment;
					sState->entered.alignVarString =
						aSrcEntry->shape.alignVarString;
					if( sState->entered.alignment == eAlignment_UseDefault )
					{// Don't defer to defaults while this is checked!
						sState->entered.alignment = eAlignment_L_T;
					}
				}
				else
				{// Set to defer to default values
					sState->entered.alignment = eAlignment_UseDefault;
					sState->entered.alignVarString.clear();
					aSrcEntry = getAlignmentSourceEntry(
						entryHasParent(anEntry)
							? getParentEntry(&anEntry)
							: &anEntry);
				}
				DBG_ASSERT(aSrcEntry);
				setupAlignmentControls(theDialog,
					sState->entered.alignment == eAlignment_UseDefault &&
					aSrcEntry != &anEntry ? aSrcEntry->shape : sState->entered);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_CHECK_SCALE:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_SCALE)
						== BST_CHECKED;
				if( isChecked )
				{
					const std::string& aSrcStr = anEntry.shape.scale;
					const bool isInPercent =
						!aSrcStr.empty() &&
						aSrcStr[aSrcStr.size()-1] == '%';
					float aNewScaleVal = stringToFloat(
						Profile::expandVars(aSrcStr));
					if( aNewScaleVal <= 0 )
						aNewScaleVal = 1.0f;
					sState->entered.scale =
						numToStringDecimal(aNewScaleVal, isInPercent);
				}
				else
				{
					sState->entered.scale =
						getControlText(theDialog, IDC_EDIT_S_VAR);
				}
				setupScaleControls(theDialog, sState->entered.scale);
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
				break;
			}
			break;

		case IDC_COMBO_ALIGN:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				sState->entered.alignment = EAlignment(SendMessage(
					GetDlgItem(theDialog, IDC_COMBO_ALIGN),
					CB_GETCURSEL, 0, 0));
				applyNewLayoutProperties();
			}
			break;

		case IDC_COMBO_X_BASE:
		case IDC_COMBO_Y_BASE:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				const bool isY = LOWORD(wParam) == IDC_COMBO_Y_BASE;
				LayoutEntry::Shape::Component& aComp =
					isY ? sState->entered.y : sState->entered.x;
				const std::string& aSelectedText =
					getControlText(theDialog, LOWORD(wParam));
				// Borrow the draw hotspot so actual on-screen position is
				// maintained as swap to a different base (anchor) position
 				updateDrawHotspot(anEntry, sState->entered, false, false);
				const int aWinMax = isY
					? WindowManager::overlayTargetSize().cy
					: WindowManager::overlayTargetSize().cx;
				const int aDrawnPos = (isY
					? u16ToRangeVal(anEntry.drawHotspot.y.anchor, aWinMax)
					: u16ToRangeVal(anEntry.drawHotspot.x.anchor, aWinMax))
					+ (isY	? int(anEntry.drawHotspot.y.offset * gUIScale)
							: int(anEntry.drawHotspot.x.offset * gUIScale));
				if( aSelectedText == narrow(kAnchorOnlyLabel) )
				{
					// Convert drawn pos to a percentage of window size
					const double aPercent =
						clamp(aDrawnPos / double(aWinMax), 0, 1.0);
					aComp.base = numToStringDecimal(aPercent, true);
					aComp.offset = 0;
					SetWindowText(
						GetDlgItem(theDialog, isY ? IDC_EDIT_Y : IDC_EDIT_X),
						widen(aComp.base).c_str());
				}
				else
				{
					aComp.base = aSelectedText;
					// Treat as having no base if just acting as an offset, or
					// for menus or no-parent hotspots, don't bother having a
					// base string for L/T as those are implied (for hotspots
					// with parents, L/T being included indicates parent should
					// be ignored and to not act as an offset, so keep them).
					if( aSelectedText == narrow(kOffsetOnlyLabel) ||
						(anEntry.rangeCount < 0 &&
						 (aSelectedText == "L" || aSelectedText == "T")) )
					{
						aComp.base.clear();
					}
					aComp.offset = 0;
					// Set offset to delta from old drawn pos to new base
					updateDrawHotspot(anEntry, sState->entered, false, false);
					const int aBaseDrawnPos = (isY
						? u16ToRangeVal(anEntry.drawHotspot.y.anchor, aWinMax)
						: u16ToRangeVal(anEntry.drawHotspot.x.anchor, aWinMax))
						+ (isY	? int(anEntry.drawHotspot.y.offset * gUIScale)
								: int(anEntry.drawHotspot.x.offset * gUIScale));
					aComp.offset = aDrawnPos - aBaseDrawnPos;
					if( gUIScale != 1.0 )
					{
						aComp.offset = aComp.offset >= 0
							? int(ceil(aComp.offset / gUIScale))
							: int(floor(aComp.offset / gUIScale));
					}
					SetWindowText(
						GetDlgItem(theDialog, isY ? IDC_EDIT_Y : IDC_EDIT_X),
						((aComp.offset >= 0 ? L"+" : L"") +
						 widen(toString(aComp.offset))).c_str());
				}
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;

		case IDC_COMBO_W_BASE:
		case IDC_COMBO_H_BASE:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				const bool isH = LOWORD(wParam) == IDC_COMBO_H_BASE;
				LayoutEntry::Shape::Component& aComp =
					isH ? sState->entered.h : sState->entered.w;
				const std::string& aSelectedText =
					getControlText(theDialog, LOWORD(wParam));
				const std::string& anOffsetText =
					getControlText(theDialog, isH ? IDC_EDIT_H : IDC_EDIT_W);
				const bool useDirectValue =
					aSelectedText == narrow(kValueOnlyLabel);
				const bool useStringBase = !useDirectValue;
				const int aPrevOffset = anOffsetText.empty()
					? aComp.offset : stringToInt(anOffsetText);
				const int aPrevFullValue = max(1, aPrevOffset +
					stringToInt(Profile::expandVars(aComp.base)));
				const int aNewBaseValue = useStringBase
					? stringToInt(Profile::expandVars(aSelectedText)) : 0;
				if( useStringBase )
					aComp.base = aSelectedText;
				else
					aComp.base.clear();
				aComp.offset = aPrevFullValue - aNewBaseValue;
				SetWindowText(
					GetDlgItem(theDialog, isH ? IDC_EDIT_H : IDC_EDIT_W),
					((useStringBase && aComp.offset >= 0 ? L"+" : L"") +
					widen(toString(aComp.offset))).c_str());
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
			}
			break;
		}
		break;

	case WM_HSCROLL:
		if( (HWND)lParam == GetDlgItem(theDialog, IDC_SLIDER_S) )
		{
			const int aScaleVal = dropTo<int>(
				SendDlgItemMessage(theDialog, IDC_SLIDER_S, TBM_GETPOS, 0, 0));
			const bool isInPercent =
				!sState->entered.scale.empty() &&
				sState->entered.scale[sState->entered.scale.size()-1] == '%';
			sState->entered.scale = numToStringDecimal(aScaleVal, isInPercent);
			SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
				widen(sState->entered.scale).c_str());
			sState->needsDrawPosUpdate = true;
			gRefreshOverlays.set(kSystemOverlayID);
			applyNewLayoutProperties();
		}
		break;

	case WM_NOTIFY:
		switch(((LPNMHDR)lParam)->idFrom)
		{
		case IDC_SPIN_X: case IDC_SPIN_W: case IDC_SPIN_H:
			processCoordFieldChange(theDialog,
				IDC_EDIT_X +
				dropTo<int>(((LPNMHDR)lParam)->idFrom) - IDC_SPIN_X,
				-((LPNMUPDOWN)lParam)->iDelta);
			applyNewLayoutProperties();
			break;
		case IDC_SPIN_Y:
			processCoordFieldChange(theDialog,
				IDC_EDIT_X +
				dropTo<int>(((LPNMHDR)lParam)->idFrom) - IDC_SPIN_X,
				((LPNMUPDOWN)lParam)->iDelta);
			applyNewLayoutProperties();
			break;
		}
		break;

	case WM_DESTROY:
		UnregisterHotKey(NULL, kCancelToolbarHotkeyID);
		break;
	}

	return (INT_PTR)FALSE;
}


static LRESULT CALLBACK layoutEditorWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	if( !sState )
		return DefWindowProc(theWindow, theMessage, wParam, lParam);

	bool stopDragging = false;

	switch(theMessage)
	{
	case WM_LBUTTONDOWN:
		sState->draggingWithMouse = true;
		sState->lastMouseDragPos.x = (short)LOWORD(lParam);
		sState->lastMouseDragPos.y = (short)HIWORD(lParam);
		SetCapture(theWindow);
		ShowCursor(FALSE);
		sState->needsDrawPosUpdate = true;
		gRefreshOverlays.set(kSystemOverlayID);
		if( sState->entered.x.useDefault ||
			sState->entered.y.useDefault )
		{// Unlock position controls and set to match mouse pos
			updateDrawHotspot(
				sState->entries[sState->activeEntry],
				sState->entered);
			const POINT& aDesiredPos = WindowManager::hotspotToOverlayPos(
				sState->entries[sState->activeEntry].drawHotspot);
			CheckDlgButton(
				WindowManager::toolbarHandle(), IDC_CHECK_POSITION, true);
			SendMessage(WindowManager::toolbarHandle(), WM_COMMAND,
				MAKEWPARAM(IDC_CHECK_POSITION, BN_CLICKED),
				(LPARAM)GetDlgItem(
					WindowManager::toolbarHandle(), IDC_CHECK_POSITION));
			updateDrawHotspot(
				sState->entries[sState->activeEntry],
				sState->entered);
			const POINT& aCurrPos = WindowManager::hotspotToOverlayPos(
				sState->entries[sState->activeEntry].drawHotspot);
			if( aCurrPos.x != aDesiredPos.x || aCurrPos.y != aDesiredPos.y )
			{
				processCoordFieldChange(WindowManager::toolbarHandle(),
					IDC_EDIT_X, aDesiredPos.x - aCurrPos.x, true);
				processCoordFieldChange(WindowManager::toolbarHandle(),
					IDC_EDIT_Y, aDesiredPos.y - aCurrPos.y, true);
			}
		}
		return 0;
	case WM_LBUTTONUP:
		stopDragging = true;
		// fall through to process final position change
	case WM_MOUSEMOVE:
		if( sState->draggingWithMouse )
		{
			POINT aMousePos;
			aMousePos.x = (short)LOWORD(lParam);
			aMousePos.y = (short)HIWORD(lParam);

			if( aMousePos.x != sState->lastMouseDragPos.x )
			{
				processCoordFieldChange( WindowManager::toolbarHandle(),
					IDC_EDIT_X, aMousePos.x - sState->lastMouseDragPos.x, true);
			}
			if( aMousePos.y != sState->lastMouseDragPos.y )
			{
				processCoordFieldChange(WindowManager::toolbarHandle(),
					IDC_EDIT_Y, aMousePos.y - sState->lastMouseDragPos.y, true);
			}

			sState->lastMouseDragPos = aMousePos;
			if( stopDragging )
			{
				ShowCursor(TRUE);
				ReleaseCapture();
				sState->draggingWithMouse = false;
				sState->unappliedDeltaX = sState->unappliedDeltaY = 0;
				applyNewLayoutProperties();
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
			}
		}
		return 0;
	case WM_SETCURSOR:
		if( !sState->draggingWithMouse )
		{
			SetCursor(LoadCursor(NULL, IDC_SIZEALL));
			processCoordFieldChange(WindowManager::toolbarHandle(), IDC_EDIT_X);
			processCoordFieldChange(WindowManager::toolbarHandle(), IDC_EDIT_Y);
		}
		return 0;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static RECT drawHotspot(
	HDC hdc,
	const Hotspot& theHotspot,
	int theExtents,
	double theHotspotSizeScale,
	const POINT& theBaseOffset,
	COLORREF theEraseColor,
	bool asActiveHotspot,
	bool includeSizeBox)
{
	const SIZE& aTargetSize = WindowManager::overlayTargetSize();
	RECT aFullDrawnRect = { 0 };

	if( includeSizeBox &&
		(theHotspot.w > theExtents * 2 || theHotspot.h > theExtents * 2) )
	{// Draw bounding box to show hotspot size
		const SIZE aBoxSize = {
			LONG(theHotspot.w * theHotspotSizeScale * gUIScale + 0.5),
			LONG(theHotspot.h * theHotspotSizeScale * gUIScale + 0.5) };
		RECT aRect;
		aRect.left = LONG(u16ToRangeVal(theHotspot.x.anchor, aTargetSize.cx)) +
			LONG(theHotspot.x.offset * gUIScale) +
				theBaseOffset.x - aBoxSize.cx / 2;
		aRect.right = aRect.left + aBoxSize.cx;
		aRect.top = LONG(u16ToRangeVal(theHotspot.y.anchor, aTargetSize.cy)) +
			LONG(theHotspot.y.offset * gUIScale) +
				theBaseOffset.y - aBoxSize.cy / 2;
		aRect.bottom = aRect.top + aBoxSize.cy;

		// Draw outer-most black border
		COLORREF oldBrushColor = SetDCBrushColor(hdc, theEraseColor);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(
			hdc, GetStockObject(NULL_BRUSH));
		InflateRect(&aRect, 2, 2);
		Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
		UnionRect(&aFullDrawnRect, &aFullDrawnRect, &aRect);
		
		// Draw white/grey border just outside of (undrawn) inner rect
		InflateRect(&aRect, -1, -1);
		HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
		COLORREF oldPenColor = SetDCPenColor(hdc, oldBrushColor);
		Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
		SetDCPenColor(hdc, oldPenColor);
		SelectObject(hdc, hOldPen);
		SelectObject(hdc, hOldBrush);
		InflateRect(&aRect, -1, -1);

		// Erase contents of inner rect for active hotspot, for further clarity
		if( asActiveHotspot )
			FillRect(hdc, &aRect, hOldBrush);
		SetDCBrushColor(hdc, oldBrushColor);

		// For non-active hotspots, don't draw the point itself, just the box
		if( !asActiveHotspot )
			return aFullDrawnRect;
	}

	const POINT aCenterPoint = {
		LONG(u16ToRangeVal(theHotspot.x.anchor, aTargetSize.cx)) +
			LONG(theHotspot.x.offset * gUIScale) + theBaseOffset.x,
		LONG(u16ToRangeVal(theHotspot.y.anchor, aTargetSize.cy)) +
			LONG(theHotspot.y.offset * gUIScale) + theBaseOffset.y };
	if( asActiveHotspot )
	{// Draw extended crosshair arms
		Rectangle(hdc,
			aCenterPoint.x - theExtents * 3,
			aCenterPoint.y - 1,
			aCenterPoint.x + theExtents * 3 + 1,
			aCenterPoint.y + 2);
		Rectangle(hdc,
			aCenterPoint.x - 1,
			aCenterPoint.y - theExtents * 3,
			aCenterPoint.x + 2,
			aCenterPoint.y + theExtents * 3 + 1);
	}
	// Draw main rectangle
	RECT aRect = {
		aCenterPoint.x - theExtents,
		aCenterPoint.y - theExtents,
		aCenterPoint.x + theExtents + 1,
		aCenterPoint.y + theExtents + 1 };
	Rectangle(hdc,
		aRect.left, aRect.top,
		aRect.right, aRect.bottom);
	if( asActiveHotspot )
	{
		if( sState && sState->draggingWithMouse )
		{// Erase center dot for extra help with positioning
			RECT aCenterDot;
			const int aDotExtents = 1;
			aCenterDot.left = aCenterPoint.x - aDotExtents;
			aCenterDot.top = aCenterPoint.y - aDotExtents;
			aCenterDot.right = aCenterPoint.x + aDotExtents + 1;
			aCenterDot.bottom = aCenterPoint.y + aDotExtents + 1;
			COLORREF oldColor = SetDCBrushColor(hdc, theEraseColor);
			HBRUSH hBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
			FillRect(hdc, &aCenterDot, hBrush);
			SetDCBrushColor(hdc, oldColor);
		}
		// Extend known drawn rect by earlier crosshair size
		InflateRect(&aRect, theExtents * 2, theExtents * 2);
	}

	UnionRect(&aFullDrawnRect, &aFullDrawnRect, &aRect);
	return aFullDrawnRect;
}


static void drawEntry(
	LayoutEntry& theEntry,
	HDC hdc,
	const POINT& theBaseOffset,
	COLORREF theEraseColor,
	bool isActiveHotspot)
{
	Hotspot aHotspot = theEntry.drawHotspot;
	if( !isActiveHotspot )
	{// Draw basic hotspot
		theEntry.drawnRect = drawHotspot(hdc, aHotspot,
			kChildHotspotDrawSize, entryScaleFactor(theEntry),
			theBaseOffset, theEraseColor, false, true);
	}
	for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i )
	{// Draw child hotspots
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		drawEntry(aChildEntry, hdc, theBaseOffset, theEraseColor, false);
	}
	// Draw built-in range of hotspots and track their combined drawn rect
	RECT aRangeDrawnRect = { 0 };
	if( theEntry.rangeCount > 1 )
	{
		const Hotspot aRangeAnchor = aHotspot;
		for(int i = 2; i <= theEntry.rangeCount; ++i)
		{
			aHotspot.x.offset = s16(clamp(int(
				theEntry.drawOffX * (i-1) * theEntry.drawOffScale) +
				aRangeAnchor.x.offset, -0x8000, 0x7FFF));
			aHotspot.y.offset = s16(clamp(int(
				theEntry.drawOffY * (i-1) * theEntry.drawOffScale) +
				aRangeAnchor.y.offset, -0x8000, 0x7FFF));
			RECT aDrawnRect = drawHotspot(hdc, aHotspot,
				kChildHotspotDrawSize, entryScaleFactor(theEntry),
				theBaseOffset, theEraseColor, false, true);
			UnionRect(&aRangeDrawnRect, &aRangeDrawnRect, &aDrawnRect);
		}
	}
	if( isActiveHotspot )
	{// Draw primary hotspot last so is over top of all children/range
		// Only draw size box for active entry for normal hotspots, not
		// anchor hotspots (size is only specified for those to pass on to
		// the rest of the range really) or menus (size is displayed by menu
		// itself being drawn).
		COLORREF oldColor = SetDCBrushColor(hdc, RGB(255, 255, 255));
		theEntry.drawnRect = drawHotspot(hdc, theEntry.drawHotspot,
			kActiveHotspotDrawSize, entryScaleFactor(theEntry),
			theBaseOffset, theEraseColor, true,
			!entryIsAnchorHotspot(theEntry) && !entryIsMenu(theEntry));
		SetDCBrushColor(hdc, oldColor);
	}
	// Have final drawn rect include range
	if( theEntry.rangeCount > 1 )
		UnionRect(&theEntry.drawnRect, &theEntry.drawnRect, &aRangeDrawnRect);
}


static void eraseDrawnEntry(LayoutEntry& theEntry, HDC hdc)
{
	HBRUSH hEraseBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
	RECT aDrawnRect = theEntry.drawnRect;
	FillRect(hdc, &aDrawnRect, hEraseBrush);
	for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i )
	{
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		eraseDrawnEntry(aChildEntry, hdc);
	}
}


static void layoutEditorPaintFunc(
	HDC hdc, const RECT& theWindowRect, bool firstDraw)
{
	if( !sState )
		return;

	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( sState->needsDrawPosUpdate )
	{// Need to erase previous positions and update to new ones
		if( !firstDraw )
			eraseDrawnEntry(anEntry, hdc);
		updateDrawHotspot(anEntry, sState->entered, firstDraw);
	}

	POINT aBaseOffset = { theWindowRect.left, theWindowRect.top };
	COLORREF anEraseColor = SetDCBrushColor(hdc, RGB(128, 128, 128));
	HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(BLACK_PEN));
	drawEntry(anEntry, hdc, aBaseOffset, anEraseColor, true);
	SelectObject(hdc, hOldPen);

	// Cleanup
	sState->needsDrawPosUpdate = false;
}


static void promptForEditEntry()
{
	DBG_ASSERT(sState);
	// Set root's parent to be last-selected item if there was one
	// This signals to the dialog which item to start out already selected
	sState->entries[0].item.parentIndex = sState->activeEntry;
	sState->activeEntry = Dialogs::layoutItemSelect(sState->dialogItems);
	DBG_ASSERT(sState->activeEntry >= 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	if( sState->activeEntry )
	{
		LayoutEntry& anEntry = sState->entries[sState->activeEntry];
		sState->entered = sState->applied = anEntry.shape;
		sState->needsDrawPosUpdate = true;
		WindowManager::setSystemOverlayCallbacks(
			layoutEditorWindowProc, layoutEditorPaintFunc);
		WindowManager::createToolbarWindow(
			entryIsMenu(anEntry)
				? IDD_DIALOG_LAYOUT_MENU
				: entryIsAnchorHotspot(anEntry)
					? IDD_DIALOG_LAYOUT_ANCHOR
					: IDD_DIALOG_LAYOUT_HOTSPOT,
			editLayoutToolbarProc,
			anEntry.menuID);
	}
	else
	{
		cleanup();
	}
}


static bool setEntryParentFromMap(
	const StringToValueMap<u32>& theEntryNameMap,
	int theIndex, const std::string& /*thePrefix*/, void* theEntryPtr)
{
	LayoutEntry& theEntry = *((LayoutEntry*)theEntryPtr);
	std::string aHotspotPropertyName =
		theEntryNameMap.keys()[theIndex];
	const int aHotspotEndIdx =
		breakOffIntegerSuffix(aHotspotPropertyName);
	const int anArrayPrevIdx = -theEntry.item.parentIndex;
	if( aHotspotEndIdx == anArrayPrevIdx )
	{
		theEntry.item.parentIndex = theEntryNameMap.values()[theIndex];
		return false;
	}

	return true;
}


static void setHotspotEntryParent(
	LayoutEntry& theEntry,
	const StringToValueMap<u32>& theEntryNameMap)
{
	// Check if might have a parent/anchor (ends in a number or range)
	theEntry.rangeCount = -1;
	int aRangeStartIdx, aRangeEndIdx;
	std::string anArrayName;
	const bool usePrevIndexAsParent = fetchRangeSuffix(
		theEntry.item.name, anArrayName,
		aRangeStartIdx, aRangeEndIdx);

	// If didn't end in a number, must be an anchor or independent hotspot
	// Leave rangeCount as -1 and exit now
	if( aRangeEndIdx <= 0 )
		return;

	// For "ArrayName#" entries (!usePrevIndexAsParent), set rangeCount = 0 and
	// only let the overall array anchor hotspot be a parent, if there is one
	const u32* aParentIdx;
	if( !usePrevIndexAsParent )
	{
		theEntry.rangeCount = 0;
		aParentIdx = theEntryNameMap.find(anArrayName);
		if( aParentIdx )
			theEntry.item.parentIndex = *aParentIdx;
		return;
	}

	// Must be "ArrayName#-#" (though both #'s might be the same value),
	// and thus usePrevIndexAsParent is true. Set .rangeCount appropriately.
	theEntry.rangeCount = 1 + aRangeEndIdx - aRangeStartIdx;

	// Need to find the hotspot in the array with starting index - 1
	const int anArrayPrevIdx = aRangeStartIdx - 1;

	// Directly check for an entry named "ArrayName#" (with # == prev index)
	aParentIdx = theEntryNameMap.find(anArrayName + toString(anArrayPrevIdx));
	if( aParentIdx )
	{
		theEntry.item.parentIndex = *aParentIdx;
		return;
	}

	// Previous index may be itself in a range like "ArrayName3-#", in which
	// case need to search through all hotspots in the named array for any
	// ranges that end in "-#" by using findAllWithPrefix
	if( anArrayPrevIdx > 0 )
	{
		// Store the value we are looking for temporarily in theEntry
		// for use in the setEntryParentFromMap function, and set it negative
		// so can check if it was re-set to an actual parent (became positive).
		const int oldParentIndex = theEntry.item.parentIndex;
		theEntry.item.parentIndex = -anArrayPrevIdx;
		theEntryNameMap.findAllWithPrefix(
			anArrayName, setEntryParentFromMap, &theEntry);
		if( theEntry.item.parentIndex >= 0 )
			return;
		theEntry.item.parentIndex = oldParentIndex;
	}

	// All else failed, so try using the overall anchor hotspot for the array.
	// One case where this might happen is if the first hotspot is a range,
	// such as "ArrayName1-3", so previous index (0) is just "ArrayName".
	aParentIdx = theEntryNameMap.find(anArrayName);
	if( aParentIdx )
	{
		theEntry.item.parentIndex = *aParentIdx;
		return;
	}

	// Very unlikely but possible to be a range with no parent (offset from 0x0)
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void init()
{
	if( sState )
	{
		cleanup();
		return;
	}

	TargetConfigSync::pauseMonitoring();

	// Gather information on elements that can be edited
	DBG_ASSERT(sState == null);
	sState = new EditorState();

	// Start with all the categories
	{
		LayoutEntry aCatEntry;
		aCatEntry.type = LayoutEntry::eType_Root;
		aCatEntry.item.isRootCategory = true;
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_HotspotCategory;
		aCatEntry.item.name = "Hotspots";
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_MenuCategory;
		aCatEntry.item.name = "Menus";
		sState->entries.push_back(aCatEntry);
		DBG_ASSERT(sState->entries.size() == LayoutEntry::eType_CategoryNum);
		#ifndef NDEBUG
		for(int i = 0; i < LayoutEntry::eType_CategoryNum; ++i)
			DBG_ASSERT(sState->entries[i].type == i);
		#endif
	}

	// Collect hotspot data entries
	const int kHotspotsSectionID = Profile::getSectionID(kHotspotsSectionName);
	if( kHotspotsSectionID >= 0 )
	{
		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(kHotspotsSectionID);
		StringToValueMap<u32> anEntryNameToIdxMap;
		for(int i = 0, end = intSize(aPropMap->size()); i < end; ++i)
		{
			LayoutEntry aNewEntry;
			aNewEntry.type = LayoutEntry::eType_Hotspot;
			aNewEntry.item.parentIndex = LayoutEntry::eType_HotspotCategory;
			aNewEntry.item.name = aPropMap->keys()[i];
			aNewEntry.propSectID = kHotspotsSectionID;
			anEntryNameToIdxMap.setValue(
				aNewEntry.item.name, u32(sState->entries.size()));
			const std::string& aDesc =
				aPropMap->vals()[i].pattern.empty()
					? aPropMap->vals()[i].str
					: aPropMap->vals()[i].pattern;
			size_t aPos = 0;
			aNewEntry.shape.x = fetchShapeComponent(aDesc, aPos, true);
			if( aPos < aDesc.size() && aDesc[aPos] != '*' )
				aNewEntry.shape.y = fetchShapeComponent(aDesc, ++aPos, true);
			if( aPos < aDesc.size() && aDesc[aPos] != '*' )
				aNewEntry.shape.w = fetchShapeComponent(aDesc, ++aPos, false);
			if( aPos < aDesc.size() && aDesc[aPos] != '*' )
				aNewEntry.shape.h = fetchShapeComponent(aDesc, ++aPos, false);
			// useDefault flag must match on each component pair
			aNewEntry.shape.x.useDefault = aNewEntry.shape.y.useDefault = false;
			if( aNewEntry.shape.w.base.empty() && !aNewEntry.shape.w.offset &&
				aNewEntry.shape.w.base.empty() && !aNewEntry.shape.w.offset )
			{// Width & height being 0 is equivalent to useDefault for hotspots
				aNewEntry.shape.w.useDefault = true;
				aNewEntry.shape.h.useDefault = true;
			}
			else if( !aNewEntry.shape.w.useDefault ||
					 !aNewEntry.shape.h.useDefault )
			{// If either W or H was set, make sure both are set to >= 1
				aNewEntry.shape.w.useDefault = false;
				aNewEntry.shape.h.useDefault = false;
				if( aNewEntry.shape.w.base.empty() )
					aNewEntry.shape.w.offset = max(1, aNewEntry.shape.w.offset);
				if( aNewEntry.shape.h.base.empty() )
					aNewEntry.shape.h.offset = max(1, aNewEntry.shape.h.offset);
			}
			// This might read in a scale for hotspots that aren't allowed to
			// have one, but it will just be ignored in those cases.
			if( aPos < aDesc.size() && aDesc[aPos] == '*' )
				aNewEntry.shape.scale = fetchShapeScale(aDesc, ++aPos);
			sState->entries.push_back(aNewEntry);
		}

		// Link hotspots in arrays with their direct parents
		for(int i = LayoutEntry::eType_CategoryNum,
			end = intSize(sState->entries.size()); i < end; ++i)
		{
			setHotspotEntryParent(sState->entries[i], anEntryNameToIdxMap);
		}
	}

	// Link parent hotspot entries to their children for drawing later
	for(int i = LayoutEntry::eType_CategoryNum,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		if( entryHasParent(sState->entries[i]) )
			getParentEntry(&sState->entries[i])->children.push_back(i);
	}

	// Collect menu layout entries
	int aFirstMenuEntryIdx = intSize(sState->entries.size());
	for(int aMenuID = 0, aMenuCnt = InputMap::menuCount();
		aMenuID < aMenuCnt; ++aMenuID)
	{
		bool isMenuDefaults = false;
		switch(InputMap::menuStyle(aMenuID))
		{
		case eMenuStyle_Hotspots:
		case eMenuStyle_Highlight:
		case eMenuStyle_KBCycleLast:
		case eMenuStyle_KBCycleDefault:
		case eMenuStyle_HotspotGuide:
			// These menus are not edit-able (use hotspot data instead)
			continue; // to next menu from for loop above
		case eMenuStyle_System:
			// System menu acts as a stand-in for default menu values
			isMenuDefaults = true;
			break;
		default:
			// Don't try editing menus that use a hotspot for their position
			if( InputMap::menuOriginHotspotID(aMenuID) )
				continue;
			// Add a new entry for this menu ID
			sState->entries.push_back(LayoutEntry());
			break;
		}
		Profile::PropertyMapPtr aPropMap = null;
		LayoutEntry& aNewEntry = isMenuDefaults
			? sState->entries[LayoutEntry::eType_MenuCategory]
			: sState->entries.back();
		aNewEntry.propSectID = InputMap::menuSectionID(aMenuID);
		if( aNewEntry.propSectID == kInvalidID && isMenuDefaults )
			continue;
		if( !isMenuDefaults )
		{
			aNewEntry.type = LayoutEntry::eType_Menu;
			aNewEntry.item.parentIndex = LayoutEntry::eType_MenuCategory;
			aNewEntry.item.name = InputMap::menuLabel(aMenuID);
		}
		aNewEntry.menuID = aMenuID;
		aPropMap = Profile::getSectionProperties(aNewEntry.propSectID);
		if( const Profile::Property* aPosProp =
				aPropMap->find(kPositionPropName) )
		{
			const std::string& aDesc = aPosProp->pattern.empty()
				? aPosProp->str : aPosProp->pattern;
			size_t aPos = 0;
			aNewEntry.shape.x = fetchShapeComponent(aDesc, aPos, true);
			if( aPos < aDesc.size() )
				aNewEntry.shape.y = fetchShapeComponent(aDesc, ++aPos, true);
		}
		if( const Profile::Property* aSizeProp =
				aPropMap->find(kItemSizePropName) )
		{
			const std::string& aDesc = aSizeProp->pattern.empty()
				? aSizeProp->str : aSizeProp->pattern;
			size_t aPos = 0;
			aNewEntry.shape.w = fetchShapeComponent(aDesc, aPos, false);
			if( aPos < aDesc.size() )
				aNewEntry.shape.h = fetchShapeComponent(aDesc, ++aPos, false);
		}
		if( const Profile::Property* anAlignProp =
				aPropMap->find(kAlignmentPropName) )
		{
			if( isEffectivelyEmptyString(anAlignProp->str) )
			{// No alignment specified - use default
				aNewEntry.shape.alignment = eAlignment_UseDefault;
			}
			else if( anAlignProp->pattern.empty() )
			{// No pattern vars used - parse alignment string
				size_t aPos = 0;
				Hotspot::Coord aCoord = stringToCoord(anAlignProp->str, aPos);
				int anAlignVal =
					aCoord.anchor < 0x4000	? eAlignment_L_T :
					aCoord.anchor > 0xC000	? eAlignment_R_T :
					/*otherwise*/			  eAlignment_CX_T;
				aCoord = stringToCoord(anAlignProp->str, ++aPos);
				anAlignVal +=
					aCoord.anchor < 0x4000	? eAlignment_L_T :
					aCoord.anchor > 0xC000	? eAlignment_L_B :
					/*otherwise*/			  eAlignment_L_CY;
				aNewEntry.shape.alignment = EAlignment(anAlignVal);
			}
			else
			{// Vars used - directly use pattern string
				aNewEntry.shape.alignment = eAlignment_VarString;
				aNewEntry.shape.alignVarString = anAlignProp->pattern;
			}
		}

		if( !aNewEntry.shape.x.useDefault ||
			!aNewEntry.shape.y.useDefault ||
			isMenuDefaults )
		{
			aNewEntry.shape.x.useDefault = false;
			aNewEntry.shape.y.useDefault = false;
		}
		if( !aNewEntry.shape.w.useDefault ||
			!aNewEntry.shape.h.useDefault ||
			isMenuDefaults )
		{// If either W or H was set, make sure both are set to >= 1
			aNewEntry.shape.w.useDefault = false;
			aNewEntry.shape.h.useDefault = false;
			if( aNewEntry.shape.w.base.empty() )
				aNewEntry.shape.w.offset = max(1, aNewEntry.shape.w.offset);
			if( aNewEntry.shape.h.base.empty() )
				aNewEntry.shape.h.offset = max(1, aNewEntry.shape.h.offset);
		}
		if( aNewEntry.shape.alignment == eAlignment_UseDefault &&
			isMenuDefaults )
		{// Default alignment if unspecified at allshould be TL
			aNewEntry.shape.alignment = eAlignment_L_T;
		}
	}

	// Link menus with their parent menus
	for(int i = aFirstMenuEntryIdx,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		LayoutEntry& anEntry = sState->entries[i];
		int aParentMenuID = InputMap::parentMenuOfMenu(anEntry.menuID);
		if( aParentMenuID == anEntry.menuID )
			continue;
		for(int j = aFirstMenuEntryIdx,
			end = intSize(sState->entries.size()); j < end; ++j)
		{
			if( sState->entries[j].menuID == aParentMenuID )
			{
				anEntry.item.parentIndex = j;
				break;
			}
		}
	}

	// Prepare for prompt dialog
	if( sState->entries.size() <= LayoutEntry::eType_CategoryNum )
	{
		logError("Current profile has no edit-able layout items!");
		return;
	}

	sState->dialogItems.reserve(sState->entries.size());
	for(int i = 0, end = intSize(sState->entries.size()); i < end; ++i)
		sState->dialogItems.push_back(&sState->entries[i].item);

	promptForEditEntry();
}


void cleanup()
{
	if( !sState )
		return;

	if( sState->activeEntry )
		cancelRepositioning();
	delete sState;
	sState = null;

	TargetConfigSync::resumeMonitoring();
}

} // LayoutEditor
