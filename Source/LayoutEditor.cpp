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
const char* kMenuDefaultSectionName = "Appearance";
const char* kPositionPropName = "Position";
const char* kItemSizePropName = "ItemSize";
const char* kAlignmentPropName = "Alignment";
const WCHAR* kAnchorOnlyLabel = L"Scaling:";
const WCHAR* kOffsetOnlyLabel = L"Offset:";
const WCHAR* kValueOnlyLabel = L"Value:";
const WCHAR* kUseDefaultLabel = L"Default:";

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

	eAlignment_Num
};

const char* kAlignmentStr[][2] =
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
		eType_Root,
		eType_HotspotCategory,
		eType_MenuCategory,

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
			Component() : offset() {}
			bool operator==(const Component& rhs) const
			{ return base == rhs.base && offset == rhs.offset; }
			bool operator!=(const Component& rhs) const
			{ return !(*this == rhs); }
		};
		Component x, y, w, h;
		std::string scale;
		EAlignment alignment;
		Shape() : alignment(eAlignment_Num) {}
		bool operator==(const Shape& rhs) const
		{
			return
				x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h &&
				scale == rhs.scale && alignment == rhs.alignment;
		}
		bool operator!=(const Shape& rhs) const
		{ return !(*this == rhs); }
	} shape;

	std::string posPropName, sizePropName;
	int posSectID, sizeSectID;
	std::vector<size_t> children;
	RECT drawnRect;
	Hotspot drawHotspot;
	float drawOffScale;
	int drawOffX, drawOffY;
	int rangeCount; // -1 = anchor, 0 = single, 1+ = actual range
	bool sizeIsARange;

	LayoutEntry() : type(eType_Num), rangeCount(-1) {}
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

static inline bool entryHasParent(const LayoutEntry& theEntry)
{
	return theEntry.item.parentIndex >= LayoutEntry::eType_CategoryNum;
}

static inline bool entryIsAnchorHotspot(const LayoutEntry& theEntry)
{
	return
		theEntry.type == LayoutEntry::eType_Hotspot &&
		!entryHasParent(theEntry) &&
		!theEntry.children.empty(); // no children == independent hotspot
}


static double entryScaleFactor(const LayoutEntry& theEntry)
{
	double result = 1.0;

	if( theEntry.type == LayoutEntry::eType_Hotspot &&
		entryHasParent(theEntry) )
	{
		// Find anchor hotspot
		LayoutEntry* anAnchorEntry =
			&sState->entries[theEntry.item.parentIndex];
		while(entryHasParent(*anAnchorEntry))
			anAnchorEntry = &sState->entries[anAnchorEntry->item.parentIndex];
		result = stringToDouble(
			Profile::expandVars(anAnchorEntry->shape.scale));
		if( result == 0 )
			result = 1.0;
	}

	return result * gUIScale;
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
		return result;

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

	if( !result.base.empty() )
	{// Check if base string is actually valid (after var expansion)
		const std::string& aCheckStr = Profile::expandVars(result.base);
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

	// Advance thePos to end of component section for next component or end
	// (done here because no longer need to know start point of component and
	// anEndPos might be used as a dummy var after this)
	thePos = anEndPos;

	// See if component string ends in a non-variable non-decimal offset value,
	// and if so bump that to the offset int and trim it off the base string
	if( !result.base.empty() )
	{
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


static std::string shapeComponentToProfileString(
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
	result.w = u16(clamp(
		stringToDoubleSum(Profile::expandVars(theShape.w.base), aStrPos) +
		theShape.w.offset,
		0, 0xFFFF));

	aStrPos = 0;
	result.h = u16(clamp(
		stringToDoubleSum(Profile::expandVars(theShape.h.base), aStrPos) +
		theShape.h.offset,
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
	theEntry.drawOffScale = 0;
	if( entryIsAnchorHotspot(theEntry) )
		theEntry.drawOffScale = stringToFloat(theShape.scale);
	if( theEntry.drawOffScale == 0 )
		theEntry.drawOffScale = 1.0f;
	if( theEntry.rangeCount > 0 )
	{
		theEntry.drawOffX = theEntry.drawHotspot.x.offset;
		theEntry.drawOffY = theEntry.drawHotspot.y.offset;
	}
	if( entryHasParent(theEntry) )
	{
		LayoutEntry& aParent = sState->entries[theEntry.item.parentIndex];
		DBG_ASSERT(aParent.type == theEntry.type);
		if( updateParentIfNeeded )
			updateDrawHotspot(aParent, aParent.shape, true, false);
		theEntry.drawOffScale = aParent.drawOffScale;
		Hotspot anAnchor = aParent.drawHotspot;
		if( aParent.rangeCount > 0 )
		{// Rare case where the parent is itself a range of hotspots
			anAnchor.x.offset = s16(clamp(int(
				anAnchor.x.offset +
				aParent.drawOffX *
				(aParent.rangeCount-1) *
				aParent.drawOffScale), -0x8000, 0x7FFF));
			anAnchor.y.offset = s16(clamp(int(
				anAnchor.y.offset +
				aParent.drawOffY *
				(aParent.rangeCount-1) *
				aParent.drawOffScale), -0x8000, 0x7FFF));
		}
		if( theEntry.rangeCount > 0 || theEntry.drawHotspot.x.anchor == 0 )
		{
			theEntry.drawHotspot.x.anchor = anAnchor.x.anchor;
			theEntry.drawHotspot.x.offset = s16(clamp(int(
				theEntry.drawHotspot.x.offset * theEntry.drawOffScale) +
				anAnchor.x.offset, -0x8000, 0x7FFF));
		}
		if( theEntry.rangeCount > 0 || theEntry.drawHotspot.y.anchor == 0 )
		{
			theEntry.drawHotspot.y.anchor = anAnchor.y.anchor;
			theEntry.drawHotspot.y.offset = s16(clamp(int(
				theEntry.drawHotspot.y.offset * theEntry.drawOffScale) +
				anAnchor.y.offset, -0x8000, 0x7FFF));
		}
		if( theEntry.drawHotspot.w == 0 && theEntry.drawHotspot.h == 0 )
		{
			theEntry.drawHotspot.w = anAnchor.w;
			theEntry.drawHotspot.h = anAnchor.h;
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


static void applyNewLayoutProperties()
{
	/*
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	const bool needNewPos =
		sState->entered.x != sState->applied.x ||
		sState->entered.y != sState->applied.y ||
		sState->entered.scale != sState->applied.scale;
	const bool needNewSize =
		(sState->entered.w != sState->applied.w ||
		 sState->entered.h != sState->applied.h);
	const bool needNewAlign =
		sState->entered.alignment != sState->applied.alignment;
	if( !needNewPos && !needNewSize && !needNewAlign )
		return;

	if( needNewPos && anEntry.type == LayoutEntry::eType_Hotspot &&
		!sState->entered.scale.empty() &&
		stringToFloat(sState->entered.scale) >= 0 &&
		stringToFloat(sState->entered.scale) != 1 )
	{
		Profile::setStr(kHotspotsSectionName, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y +
			" * " + sState->entered.scale);
	}
	else if( needNewPos && anEntry.type == LayoutEntry::eType_Hotspot )
	{
		Profile::setStr(kHotspotsSectionName, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y);
	}
	else if( needNewPos && !anEntry.posSect.empty() )
	{
		Profile::setStr(anEntry.posSect, kPositionPropName,
			sState->entered.x + ", " + sState->entered.y);
	}
	if( needNewSize && !anEntry.sizeSect.empty() )
	{
		Profile::setStr(anEntry.sizeSect, kItemSizePropName,
			sState->entered.w + ", " + sState->entered.h);
	}
	if( needNewAlign && !anEntry.alignSect.empty() )
	{
		Profile::setStr(anEntry.alignSect, kAlignmentPropName,
			kAlignmentStr[sState->entered.alignment][1]);
	}
	*/
	sState->applied = sState->entered;
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
		Profile::saveChangesToFile();
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static void saveNewPosition()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	applyNewLayoutProperties();
	if( anEntry.shape != sState->applied )
	{
		anEntry.shape = sState->applied;
		Profile::saveChangesToFile();
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
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


static void setInitialToolbarPos(HWND hDlg, LayoutEntry& theEntry)
{
	if( !sState || !sState->activeEntry )
		return;

	// Position the tool bar as far as possible from the object to be moved,
	// so that it is less likely to end up overlapping the object
	POINT anEntryPos;
	//if( theEntry.type == LayoutEntry::eType_Menu )
	//{
	//	const RECT& anEntryRect =
	//		WindowManager::overlayRect(theEntry.menuOverlayID);
	//	anEntryPos.x = (anEntryRect.left + anEntryRect.right) / 2;
	//	anEntryPos.y = (anEntryRect.top + anEntryRect.bottom) / 2;
	//}
	//else
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
	GetWindowRect(hDlg, &aDialogRect);
	const int aDialogWidth = aDialogRect.right - aDialogRect.left;
	const int aDialogHeight = aDialogRect.bottom - aDialogRect.top;

	SetWindowPos(hDlg, NULL,
		useRightEdge
			? anOverlayRect.right - aDialogWidth
			: anOverlayRect.left,
		useBottomEdge
			? anOverlayRect.bottom - aDialogHeight
			: anOverlayRect.top,
		0, 0, SWP_NOZORDER | SWP_NOSIZE);
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


static void processCoordFieldChange(
	HWND hDlg,
	int theControlID,
	int theDelta = 0,
	bool theDeltaSetByMouse = false)
{
	if( !hDlg ) return;

	const int theBaseControlID = IDC_COMBO_X_BASE + (theControlID - IDC_EDIT_X);
	const std::string& aBaseControlStr = getControlText(hDlg, theBaseControlID);
	const bool isAnchorOnly = aBaseControlStr == narrow(kAnchorOnlyLabel);
	const bool isOffset = !isAnchorOnly &&
		aBaseControlStr != narrow(kValueOnlyLabel) &&
		aBaseControlStr != narrow(kUseDefaultLabel);
	const std::string& aControlStr = getControlText(hDlg, theControlID);
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
					HWND hComboBox = GetDlgItem(hDlg, theBaseControlID);
					SendMessage(hComboBox, CB_SETCURSEL, aNewAnchorType, 0);
					SendMessage(hDlg, WM_COMMAND,
						MAKEWPARAM(theBaseControlID, CBN_SELCHANGE),
						(LPARAM)hComboBox);
					// Above will change offset, base string, etc, so need to
					// restart processing of coordinate string from the top
					// (treating theDelta as direct rather than mouse-based
					// since have already adjusted it for that here).
					processCoordFieldChange(hDlg, theControlID, theDelta);
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
		if( isInPercent )
			aFormattedString = strFormat("%.4g%%", aNewValue * 100.0);
		else
			aFormattedString = strFormat("%.5g", aNewValue);
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
			GetDlgItem(hDlg, theControlID),
			widen(aFormattedString).c_str());
	}

	// Now actually apply the string to the component
	LayoutEntry::Shape::Component& aComp =
		theControlID == IDC_EDIT_X	? sState->entered.x :
		theControlID == IDC_EDIT_Y	? sState->entered.y :
		theControlID == IDC_EDIT_W	? sState->entered.w :
		/*theControlID == IDC_EDIT_H*/sState->entered.h;

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


static void adjustComboBoxDroppedWidth(HWND hCombo)
{
	DBG_ASSERT(IsWindow(hCombo));

	// Get current dropped width so we don't shrink it
	const int aCurrWidth = SendMessage(hCombo, CB_GETDROPPEDWIDTH, 0, 0);

	// Get number of items
	const int anItemCount = SendMessage(hCombo, CB_GETCOUNT, 0, 0);
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
		int aTextLen = SendMessage(hCombo, CB_GETLBTEXTLEN, i, 0);
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
	aMaxTextWidth += /*GetSystemMetrics(SM_CXVSCROLL) +*/ 8;

	// Only expand, never shrink
	if( aMaxTextWidth > aCurrWidth )
		SendMessage(hCombo, CB_SETDROPPEDWIDTH, aMaxTextWidth, 0);
}


static INT_PTR CALLBACK editLayoutToolbarProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Set title to match the entry name and display scaling being applied
		{
			const double aScaling = entryScaleFactor(anEntry);
			if( aScaling != 1 )
			{
				SetWindowText(theDialog,
					(L"Repositioning: " +
					 widen(anEntry.item.name) +
					 L" (at " +
					 widen(strFormat("%.4g", aScaling * 100.0) + "%") +
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
		HWND hCntrl;
		for(int isY = 0; isY < 2; ++isY)
		{// X & Y
			const LayoutEntry::Shape::Component& aComp =
				isY ? anEntry.shape.y : anEntry.shape.x;
			hCntrl = GetDlgItem(theDialog,
				isY ? IDC_COMBO_Y_BASE : IDC_COMBO_X_BASE);
			int aDropListSize = 0;
			if( anEntry.rangeCount <= 0 )
			{
				SendMessage(hCntrl,CB_ADDSTRING, 0,
					(LPARAM)(isY ? L"T" : L"L"));
				SendMessage(hCntrl, CB_ADDSTRING, 0,
					(LPARAM)(isY ? L"CY" : L"CX"));
				SendMessage(hCntrl, CB_ADDSTRING, 0,
					(LPARAM)(isY ? L"B" : L"R"));
				SendMessage(hCntrl, CB_ADDSTRING, 0, (LPARAM)kAnchorOnlyLabel);
				aDropListSize = 4;
			}
			const bool canBeOffsetOnly = entryHasParent(anEntry);
			// Set initial selection to one of the above if appropriate
			const double anAnchorVal = aComp.offset
					? 0 : stringToDouble(aComp.base, true);
			const bool isCustomAnchor = anEntry.rangeCount <= 0 &&
				!_isnan(anAnchorVal) && anAnchorVal != 0 && anAnchorVal < 1.0;
			if( canBeOffsetOnly )
			{
				SendMessage(hCntrl, CB_ADDSTRING, 0, (LPARAM)kOffsetOnlyLabel);
				++aDropListSize;
			}
			if( isCustomAnchor )
			{
				// Entry is anchor-only (percent of screen area)
				SendMessage(hCntrl, CB_SETCURSEL, (WPARAM)3, 0);
				SetWindowText(
					GetDlgItem(theDialog, (isY ? IDC_EDIT_Y : IDC_EDIT_X)),
					widen(aComp.base).c_str());
			}
			else if( canBeOffsetOnly && aComp.base.empty() )
			{
				SendMessage(hCntrl, CB_SETCURSEL, WPARAM(aDropListSize-1), 0);
			}
			else
			{
				// Check what hotspot coord would be if read ONLY base string
				size_t aPos = 0;
				const Hotspot::Coord& aCoord = stringToCoord(
					Profile::expandVars(aComp.base), aPos);
				// If string contained an offset or unkwnon characters
				// (likely ${} variables), or has an anchor even though
				// it is a range, then need to include custom string option
				bool useStringBase = aCoord.offset != 0 ||
					(!aComp.base.empty() && aPos < aComp.base.size()) ||
					(anEntry.rangeCount > 0 && aCoord.anchor != 0);
				if( !useStringBase )
				{// Check if string is simply a known basic anchor type
					switch(aCoord.anchor)
					{
					case 0:
					case 0x0001: // L or T
						SendMessage(hCntrl, CB_SETCURSEL, 0, 0);
						break;
					case 0x8000: // CX or CY
						SendMessage(hCntrl, CB_SETCURSEL, 1, 0);
						break;
					case 0xFFFF: // R or B
						SendMessage(hCntrl, CB_SETCURSEL, 2, 0);
						break;
					default: // non-standard anchor value
						useStringBase = true;
						break;
					}
				}
				if( useStringBase )
				{
					SendMessage(hCntrl, CB_ADDSTRING, 0,
						(LPARAM)widen(aComp.base).c_str());
					SendMessage(hCntrl, CB_SETCURSEL, WPARAM(aDropListSize), 0);
				}
			}
			if( !isCustomAnchor )
			{// Set offset field
				SetWindowText(
					GetDlgItem(theDialog, (isY ? IDC_EDIT_Y : IDC_EDIT_X)),
					((aComp.offset >= 0 ? L"+" : L"") +
					 widen(toString(aComp.offset))).c_str());
			}
			adjustComboBoxDroppedWidth(hCntrl);
		}

		for(int isH = 0; isH < 2; ++isH)
		{// W & H
			const LayoutEntry::Shape::Component& aComp =
				isH ? anEntry.shape.h : anEntry.shape.w;
			hCntrl = GetDlgItem(theDialog,
				isH ? IDC_COMBO_H_BASE : IDC_COMBO_W_BASE);
			SendMessage(hCntrl, CB_ADDSTRING, 0, (LPARAM)kUseDefaultLabel);
			SendMessage(hCntrl, CB_ADDSTRING, 0, (LPARAM)kValueOnlyLabel);
			int aSelection = 0; // default/parent action
			if( !aComp.base.empty() )
			{// Use option of custom base string + offset
				SendMessage(hCntrl, CB_ADDSTRING, 0,
					(LPARAM)widen(aComp.base).c_str());
				aSelection = 2;
			}
			else if( !anEntry.shape.w.base.empty() ||
					 anEntry.shape.w.offset > 0 ||
					 !anEntry.shape.h.base.empty() ||
					 anEntry.shape.h.offset > 0 )
			{// Use custom value option if either axis is non-default
				aSelection = 1;
			}
			adjustComboBoxDroppedWidth(hCntrl);
			SendMessage(hCntrl, CB_SETCURSEL, (WPARAM)aSelection, 0);
		}
		// Send selection changed message to W & H combo boxes to complete setup
		SendMessage(theDialog, WM_COMMAND,
			MAKEWPARAM(IDC_COMBO_W_BASE, CBN_SELCHANGE),
			(LPARAM)(GetDlgItem(theDialog, IDC_COMBO_W_BASE)));
		SendMessage(theDialog, WM_COMMAND,
			MAKEWPARAM(IDC_COMBO_H_BASE, CBN_SELCHANGE),
			(LPARAM)(GetDlgItem(theDialog, IDC_COMBO_H_BASE)));


		//if( anEntry.type == LayoutEntry::eType_Menu )
		//{// Alignment
		//	HWND hDropList = GetDlgItem(theDialog, IDC_COMBO_ALIGN);
		//	for(int i = 0; i < eAlignment_Num; ++i)
		//	{
		//		SendMessage(hDropList, CB_ADDSTRING, 0,
		//			(LPARAM)widen(kAlignmentStr[i][0]).c_str());
		//	}
		//	if( anEntry.shape.alignment < eAlignment_Num )
		//	{
		//		SendMessage(hDropList, CB_SETCURSEL,
		//			(WPARAM)anEntry.shape.alignment, 0);
		//	}
		//}

		if( entryIsAnchorHotspot(anEntry) )
		{// Scale
			SendDlgItemMessage(theDialog, IDC_SLIDER_S,
				TBM_SETRANGE, TRUE,
				MAKELPARAM(kMinOffsetScale, kMaxOffsetScale));
			const std::string& aFinalScaleString =
				anEntry.shape.scale.empty() ? "1" :
				toString(stringToDouble(
					Profile::expandVars(anEntry.shape.scale)));
			if( aFinalScaleString != anEntry.shape.scale )
			{
				if( !anEntry.shape.scale.empty() )
				{
					SetWindowText(
						GetDlgItem(theDialog, IDC_EDIT_S_VAR),
						widen(anEntry.shape.scale).c_str());
				}
				CheckDlgButton(theDialog, IDC_CHECK_SCALE, false);
			}
			else if( !anEntry.shape.scale.empty() )
			{
				CheckDlgButton(theDialog, IDC_CHECK_SCALE, true);
				SetWindowText(
					GetDlgItem(theDialog, IDC_EDIT_S),
					widen(aFinalScaleString).c_str());
				SendDlgItemMessage(theDialog, IDC_SLIDER_S,
					TBM_SETPOS, TRUE,
					LPARAM(stringToDouble(aFinalScaleString) * 100));
			}
			else
			{
				CheckDlgButton(theDialog, IDC_CHECK_SCALE, false);
			}
			// Send initial check/un-check message for rest of setup
			SendMessage(theDialog, WM_COMMAND,
				MAKEWPARAM(IDC_CHECK_SCALE, BN_CLICKED),
				(LPARAM)GetDlgItem(theDialog, IDC_CHECK_SCALE));
		}

		{// Adjust spinner control positioning/size slightly
			hCntrl = GetDlgItem(theDialog, IDC_SPIN_X);
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
					sState->entered.scale = isInPercent
						? strFormat("%.4g%%", aNewValue * 100.0)
						: strFormat("%.5g", aNewValue);
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

		case IDC_CHECK_SCALE:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				const bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_SCALE)
						== BST_CHECKED;
				SendMessage(GetDlgItem(theDialog, IDC_EDIT_S),
					EM_SETREADONLY, !isChecked, 0);
				EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), isChecked);
				if( isChecked )
				{
					ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), true);
					ShowWindow(GetDlgItem(theDialog, IDC_EDIT_S_VAR), false);
					sState->entered.scale =
						getControlText(theDialog, IDC_EDIT_S);
					applyNewLayoutProperties();
					break;
				}
				const std::string& aScaleVarStr =
					getControlText(theDialog, IDC_EDIT_S_VAR);
				if( aScaleVarStr.empty() )
				{// Reset to default scale if have no default variables
					sState->entered.scale = "1";
					SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S), L"1");
					SendDlgItemMessage(theDialog, IDC_SLIDER_S,
						TBM_SETPOS, TRUE, LPARAM(100));
					sState->needsDrawPosUpdate = true;
					gRefreshOverlays.set(kSystemOverlayID);
					applyNewLayoutProperties();
					break;
				}
				// Use scale derived from variable string
				ShowWindow(GetDlgItem(theDialog, IDC_SLIDER_S), false);
				ShowWindow(GetDlgItem(theDialog, IDC_EDIT_S_VAR), true);
				sState->entered.scale = aScaleVarStr;
				const std::string& aCalculatedScaleString =
					Profile::expandVars(aScaleVarStr);
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
					widen(aCalculatedScaleString).c_str());
				SendDlgItemMessage(theDialog, IDC_SLIDER_S,
					TBM_SETPOS, TRUE,
					LPARAM(stringToDouble(aCalculatedScaleString) * 100));
				sState->needsDrawPosUpdate = true;
				gRefreshOverlays.set(kSystemOverlayID);
				applyNewLayoutProperties();
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
						clamp(aDrawnPos * 100.0 / double(aWinMax), 0, 100.0);
					aComp.base = strFormat("%.4g%%", aPercent);
					aComp.offset = 0;
					SetWindowText(
						GetDlgItem(theDialog, isY ? IDC_EDIT_Y : IDC_EDIT_X),
						widen(aComp.base).c_str());
				}
				else
				{
					if( aSelectedText == narrow(kOffsetOnlyLabel) )
						aComp.base.clear();
					else
						aComp.base = aSelectedText;
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
				const int altAxisControlID =
					isH ? IDC_COMBO_W_BASE : IDC_COMBO_H_BASE;
				LayoutEntry::Shape::Component& aComp =
					isH ? sState->entered.h : sState->entered.w;
				const std::string& aSelectedText =
					getControlText(theDialog, LOWORD(wParam));
				const std::string& anAltAxisText =
					getControlText(theDialog, altAxisControlID);
				const std::string& anOffsetText =
					getControlText(theDialog, isH ? IDC_EDIT_H : IDC_EDIT_W);
				const bool useDefault =
					aSelectedText == narrow(kUseDefaultLabel);
				const bool useDirectValue = !useDefault &&
					aSelectedText == narrow(kValueOnlyLabel);
				const bool useStringBase = !useDefault && !useDirectValue;
				const bool altAxisSetToDefault =
					anAltAxisText == narrow(kUseDefaultLabel);

				if( useDefault != altAxisSetToDefault )
				{// Both axis must be default, or both NOT default
					HWND hAltAxis =
						GetDlgItem(theDialog, altAxisControlID);
					int aNewSel = min(
						SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0),
						SendMessage(hAltAxis, CB_GETCOUNT, 0, 0));
					SendMessage(hAltAxis, CB_SETCURSEL, aNewSel, 0);
					SendMessage(theDialog, WM_COMMAND,
						MAKEWPARAM(altAxisControlID, CBN_SELCHANGE),
						(LPARAM)hAltAxis);
				}

				// Disable/enable modification based on useDefault
				SendMessage(
					GetDlgItem(theDialog, isH ? IDC_EDIT_H : IDC_EDIT_W),
					EM_SETREADONLY, useDefault, 0);
				EnableWindow(
					GetDlgItem(theDialog, isH ? IDC_SPIN_H : IDC_SPIN_W),
					!useDefault);

				if( useDefault )
				{
					sState->entered.w.base.clear();
					sState->entered.w.offset = 0;
					sState->entered.h.base.clear();
					sState->entered.h.offset = 0;
					// Borrow draw hotspot to display inherited value
					updateDrawHotspot(anEntry, sState->entered, true, false);
					if( anEntry.drawHotspot.w == 0 &&
						anEntry.drawHotspot.h == 0 )
					{
						SetWindowText(GetDlgItem(theDialog,
							isH ? IDC_EDIT_H : IDC_EDIT_W),
							L"");
					}
					else
					{
						SetWindowText(GetDlgItem(theDialog,
							isH ? IDC_EDIT_H : IDC_EDIT_W),
							widen(isH
								? toString(anEntry.drawHotspot.h)
								: toString(anEntry.drawHotspot.w)).c_str());
					}
				}
				else
				{
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
				}
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
			if( !sState->entered.scale.empty() &&
				sState->entered.scale
					[sState->entered.scale.size()-1] == '%' )
			{
				sState->entered.scale = strFormat("%d%%", aScaleVal);
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
					widen(sState->entered.scale).c_str());
			}
			else
			{
				sState->entered.scale = strFormat("%.5g", aScaleVal / 100.0);
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
					widen(sState->entered.scale).c_str());
			}
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
				processCoordFieldChange(
					WindowManager::toolbarHandle(),
					IDC_EDIT_X,
					aMousePos.x - sState->lastMouseDragPos.x,
					true);
			}
			if( aMousePos.y != sState->lastMouseDragPos.y )
			{
				processCoordFieldChange(
					WindowManager::toolbarHandle(),
					IDC_EDIT_Y,
					aMousePos.y - sState->lastMouseDragPos.y,
					true);
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


//static RECT drawBoundBox(
//	HDC hdc,
//	const POINT& theOrigin,
//	const SIZE& theBoxSize,
//	COLORREF theEraseColor,
//	bool isActiveBox)
//{
//	RECT aRect = {
//		theOrigin.x, theOrigin.y,
//		theOrigin.x + theBoxSize.cx, theOrigin.y + theBoxSize.cy };
//	RECT aFullDrawnRect = { 0 };
//
//	// Draw outer-most black border
//	COLORREF oldBrushColor = SetDCBrushColor(hdc, theEraseColor);
//	HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
//	InflateRect(&aRect, 2, 2);
//	Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
//	UnionRect(&aFullDrawnRect, &aFullDrawnRect, &aRect);
//
//	// For active box, draw anchor in top-left corner representing base pos
//	if( isActiveBox )
//	{
//		RECT anAnchorRect = { 0 };
//		anAnchorRect.left = theOrigin.x - kBoundBoxAnchorDrawSize;
//		anAnchorRect.top = theOrigin.y - kBoundBoxAnchorDrawSize;
//		anAnchorRect.right = theOrigin.x + 3;
//		anAnchorRect.bottom = theOrigin.y + 3;
//		SetDCBrushColor(hdc, oldBrushColor);
//		SelectObject(hdc, hOldBrush);
//		Rectangle(hdc,
//			anAnchorRect.left, anAnchorRect.top,
//			anAnchorRect.right, anAnchorRect.bottom);
//		UnionRect(&aFullDrawnRect, &aFullDrawnRect, &anAnchorRect);
//		SetDCBrushColor(hdc, theEraseColor);
//		SelectObject(hdc, GetStockObject(NULL_BRUSH));
//	}
//
//	// Draw white/grey border just outside of inner rect
//	InflateRect(&aRect, -1, -1);
//	HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
//	COLORREF oldPenColor = SetDCPenColor(hdc, oldBrushColor);
//	Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
//	SetDCPenColor(hdc, oldPenColor);
//	SelectObject(hdc, hOldPen);
//	SelectObject(hdc, hOldBrush);
//	InflateRect(&aRect, -1, -1);
//
//	// Erase contents of inner rect if this is the active box
//	if( isActiveBox )
//		FillRect(hdc, &aRect, hOldBrush);
//
//	SetDCBrushColor(hdc, oldBrushColor);
//	return aFullDrawnRect;
//}


static RECT drawHotspot(
	HDC hdc,
	const Hotspot& theHotspot,
	int theExtents,
	const RECT& theWindowRect,
	COLORREF theEraseColor,
	bool isActiveHotspot)
{
	const SIZE& aTargetSize = WindowManager::overlayTargetSize();
	const POINT aCenterPoint = {
		LONG(u16ToRangeVal(theHotspot.x.anchor, aTargetSize.cx)) +
			LONG(theHotspot.x.offset * gUIScale) + theWindowRect.left,
		LONG(u16ToRangeVal(theHotspot.y.anchor, aTargetSize.cy)) +
			LONG(theHotspot.y.offset * gUIScale) + theWindowRect.top };

	//if( theHotspot.w > 0 && theHotspot.w > 0 )
	//{
	//	return drawBoundBox(
	//		hdc, aCenterPoint, theBoxSize, theEraseColor, isActiveHotspot);
	//}

	RECT aFullDrawnRect = { 0 };
	aFullDrawnRect.left = aCenterPoint.x - theExtents;
	aFullDrawnRect.top = aCenterPoint.y - theExtents;
	aFullDrawnRect.right = aCenterPoint.x + theExtents + 1;
	aFullDrawnRect.bottom = aCenterPoint.y + theExtents + 1;
	if( isActiveHotspot )
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
	Rectangle(hdc,
		aFullDrawnRect.left, aFullDrawnRect.top,
		aFullDrawnRect.right, aFullDrawnRect.bottom);
	if( isActiveHotspot )
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
		// Extend returned rect by earlier crosshair size
		InflateRect(&aFullDrawnRect, theExtents * 2, theExtents * 2);
	}
	return aFullDrawnRect;
}


static void drawEntry(
	LayoutEntry& theEntry,
	HDC hdc,
	const RECT& theWindowRect,
	COLORREF theEraseColor,
	bool isActiveHotspot)
{
	Hotspot aHotspot = theEntry.drawHotspot;
	if( !isActiveHotspot )
	{// Draw basic hotspot
		theEntry.drawnRect = drawHotspot(hdc, aHotspot,
			kChildHotspotDrawSize, theWindowRect, theEraseColor, false);
	}
	for(int i = 0, end = intSize(theEntry.children.size()); i < end; ++i )
	{// Draw child hotspots
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		drawEntry(aChildEntry, hdc, theWindowRect, theEraseColor, false);
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
				kChildHotspotDrawSize, theWindowRect, theEraseColor, false);
			UnionRect(&aRangeDrawnRect, &aRangeDrawnRect, &aDrawnRect);
		}
	}
	if( isActiveHotspot )
	{// Draw primary hotspot last so is over top of all children/range
		COLORREF oldColor = SetDCBrushColor(hdc, RGB(255, 255, 255));
		theEntry.drawnRect = drawHotspot(hdc, theEntry.drawHotspot,
			kActiveHotspotDrawSize, theWindowRect, theEraseColor, true);
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

	COLORREF anEraseColor = SetDCBrushColor(hdc, RGB(128, 128, 128));
	HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(BLACK_PEN));
	drawEntry(anEntry, hdc, theWindowRect, anEraseColor, true);
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
			anEntry.type == LayoutEntry::eType_Menu
				? IDD_DIALOG_LAYOUT_MENU
				: entryIsAnchorHotspot(anEntry)
					? IDD_DIALOG_LAYOUT_ANCHOR
					: IDD_DIALOG_LAYOUT_HOTSPOT,
			editLayoutToolbarProc,
			-1 /*TODO*/);
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
		int aFirstNewEntryIdx = intSize(sState->entries.size());

		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(kHotspotsSectionID);
		StringToValueMap<u32> anEntryNameToIdxMap;
		for(int i = 0, end = intSize(aPropMap->size()); i < end; ++i)
		{
			LayoutEntry aNewEntry;
			aNewEntry.type = LayoutEntry::eType_Hotspot;
			aNewEntry.item.parentIndex = LayoutEntry::eType_HotspotCategory;
			aNewEntry.item.name = aPropMap->keys()[i];
			aNewEntry.posPropName = aNewEntry.item.name;
			std::string aKeyName = aNewEntry.item.name;
			anEntryNameToIdxMap.setValue(
				aKeyName, u32(sState->entries.size()));
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
			// This might read in a scale for hotspots that aren't allowed to
			// have one, but it will just be ignored in those cases.
			if( aPos < aDesc.size() && aDesc[aPos] == '*' )
				aNewEntry.shape.scale = fetchShapeScale(aDesc, ++aPos);
			sState->entries.push_back(aNewEntry);
		}

		// Link hotspots in arrays with their direct parents
		for(int i = aFirstNewEntryIdx,
			end = intSize(sState->entries.size()); i < end; ++i)
		{
			setHotspotEntryParent(sState->entries[i], anEntryNameToIdxMap);
		}
	}

	// TODO - Collect menu layouts


	// Prepare for prompt dialog
	if( sState->entries.size() <= LayoutEntry::eType_CategoryNum )
	{
		logError("Current profile has no edit-able layout items!");
		return;
	}

	// Link parent entries to their children for drawing later
	for(int i = LayoutEntry::eType_CategoryNum,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		if( entryHasParent(sState->entries[i]) )
		{
			sState->entries[sState->entries[i].item.parentIndex]
				.children.push_back(i);
		}
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
