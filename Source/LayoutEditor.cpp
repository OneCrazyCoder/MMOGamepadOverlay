//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "LayoutEditor.h"
#include "Dialogs.h"
#include "HotspotMap.h"
#include "InputMap.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "TargetConfigSync.h"
#include "WindowManager.h"
#include "WindowPainter.h"

#include <CommCtrl.h>

namespace LayoutEditor
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kBoundBoxAnchorDrawSize = 6,
kActiveHotspotDrawSize = 5,
kChildHotspotDrawSize = 3,
kMinOffsetScale = 25,
kMaxOffsetScale = 400,
};


const char* kHotspotsPrefix = "Hotspots";
const char* kIconsPrefix = "Icons";
const char* kMenuPrefix = "Menu.";
const char* kDefMenuSectName = "Appearance";
const char* kPositionKey = "Position";
const char* kSizeKey = "Size";
const char* kItemSizeKey = "ItemSize";
const char* kAlignmentKey = "Alignment";

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


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ZERO_INIT(LayoutEntry)
{
	enum EType
	{
		eType_Root,
		eType_HotspotCategory,
		eType_CopyIconCategory,
		eType_MenuCategory,

		eType_Hotspot,
		eType_CopyIcon,
		eType_MenuOverlay,

		eType_Num,
		eType_CategoryNum = eType_Hotspot
	} type;
	Dialogs::TreeViewDialogItem item;

	struct Shape
	{
		std::string x, y, w, h;
		std::string offsetScale;
		EAlignment alignment;
		Shape() : alignment(eAlignment_Num) {}
		bool operator==(const Shape& rhs) const
		{
			return
				x == rhs.x && y == rhs.y &&
				w == rhs.w && h == rhs.h &&
				offsetScale == rhs.offsetScale &&
				alignment == rhs.alignment;
		}
		bool operator!=(const Shape& rhs) const
		{ return !(*this == rhs); }
	} shape;

	std::string posSect, sizeSect, alignSect, propName;
	std::vector<size_t> children;
	RECT drawnRect;
	Hotspot drawHotspot, drawSize;
	float drawOffScale;
	int drawOffX, drawOffY;
	int menuOverlayID;
	int rangeCount;

	LayoutEntry() : type(eType_Num) {}
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


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static EditorState* sState = null;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

// Forward declares
static void promptForEditEntry();

static bool entryIncludesPosition(const LayoutEntry& theEntry)
{
	if( theEntry.type != LayoutEntry::eType_MenuOverlay )
		return true;
	const int aRootMenuID =
		InputMap::overlayRootMenuID(theEntry.menuOverlayID);
	switch(InputMap::menuStyle(aRootMenuID))
	{
	case eMenuStyle_Hotspots:
		return false;
	}
	return true;
}


static bool entryIncludesSize(const LayoutEntry& theEntry)
{
	switch(theEntry.type)
	{
	case LayoutEntry::eType_Hotspot:
		return false;
	case LayoutEntry::eType_CopyIcon:
		return theEntry.item.parentIndex < LayoutEntry::eType_CategoryNum;
	case LayoutEntry::eType_MenuOverlay:
		return true;
	}

	return false;
}


static bool entryIncludesAlignment(const LayoutEntry& theEntry)
{
	return theEntry.type == LayoutEntry::eType_MenuOverlay;
}


static bool entryIncludesScale(const LayoutEntry& theEntry)
{
	return
		theEntry.type == LayoutEntry::eType_Hotspot &&
		theEntry.item.parentIndex < LayoutEntry::eType_CategoryNum &&
		!theEntry.children.empty();
}


static bool entryIsAnOffset(const LayoutEntry& theEntry)
{
	if( theEntry.item.parentIndex >= LayoutEntry::eType_CategoryNum )
		return true;
	return false;
}


static double entryScaleFactor(const LayoutEntry& theEntry)
{
	double result = 1.0;

	if( theEntry.type == LayoutEntry::eType_Hotspot &&
		entryIsAnOffset(theEntry) )
	{
		// Find anchor hotspot
		LayoutEntry* anAnchorEntry =
			&sState->entries[theEntry.item.parentIndex];
		while(entryIsAnOffset(*anAnchorEntry))
			anAnchorEntry = &sState->entries[anAnchorEntry->item.parentIndex];
		result = doubleFromString(anAnchorEntry->shape.offsetScale);
		if( result == 0 )
			result = 1.0;
	}

	return result * gUIScale;
}


static void applyNewPosition()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	const bool needNewPos =
		sState->entered.x != sState->applied.x ||
		sState->entered.y != sState->applied.y ||
		sState->entered.offsetScale != sState->applied.offsetScale;
	const bool needNewSize =
		(sState->entered.w != sState->applied.w ||
		 sState->entered.h != sState->applied.h);
	const bool needNewAlign =
		sState->entered.alignment != sState->applied.alignment;
	if( !needNewPos && !needNewSize && !needNewAlign )
		return;

	if( (needNewPos || needNewSize) &&
		anEntry.type == LayoutEntry::eType_CopyIcon &&
		entryIncludesSize(anEntry) )
	{
		Profile::setStr(kIconsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y + ", " +
			sState->entered.w + ", " + sState->entered.h);
	}
	else if( needNewPos && anEntry.type == LayoutEntry::eType_CopyIcon )
	{
		Profile::setStr(kIconsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y);
	}
	else if( needNewPos && anEntry.type == LayoutEntry::eType_Hotspot &&
			 !sState->entered.offsetScale.empty() &&
			 floatFromString(sState->entered.offsetScale) >= 0 &&
			 floatFromString(sState->entered.offsetScale) != 1 )
	{
		Profile::setStr(kHotspotsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y +
			" * " + sState->entered.offsetScale);
	}
	else if( needNewPos && anEntry.type == LayoutEntry::eType_Hotspot )
	{
		Profile::setStr(kHotspotsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y);
	}
	else if( needNewPos && !anEntry.posSect.empty() )
	{
		Profile::setStr(anEntry.posSect, kPositionKey,
			sState->entered.x + ", " + sState->entered.y);
	}
	if( needNewSize && !anEntry.sizeSect.empty() )
	{
		Profile::setStr(anEntry.sizeSect, anEntry.propName,
			sState->entered.w + ", " + sState->entered.h);
	}
	if( needNewAlign && !anEntry.alignSect.empty() )
	{
		Profile::setStr(anEntry.alignSect, kAlignmentKey,
			kAlignmentStr[sState->entered.alignment][1]);
	}
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
		applyNewPosition();
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
	applyNewPosition();
	if( anEntry.shape != sState->applied )
	{
		anEntry.shape = sState->applied;
		Profile::saveChangesToFile();
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static HotspotMap::EHotspotNamingStyle formatForCoord(
	const LayoutEntry& theEntry, int theEditControlID)
{
	switch(theEditControlID)
	{
	case IDC_EDIT_X:
		return entryIsAnOffset(theEntry)
			? HotspotMap::eHNS_X_Off
			: HotspotMap::eHNS_X;
	case IDC_EDIT_Y:
		return entryIsAnOffset(theEntry)
			? HotspotMap::eHNS_Y_Off
			: HotspotMap::eHNS_Y;
	case IDC_EDIT_W:
		return HotspotMap::eHNS_W;
	case IDC_EDIT_H:
		return HotspotMap::eHNS_H;
	}

	return HotspotMap::eHNS_Num;
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


static void setInitialToolbarPos(HWND hDlg, const LayoutEntry& theEntry)
{
	if( !sState || !sState->activeEntry )
		return;

	// Position the tool bar as far as possible from the object to be moved,
	// so that it is less likely to end up overlapping the object
	POINT anEntryPos;
	if( theEntry.type == LayoutEntry::eType_MenuOverlay )
	{
		const RECT& anEntryRect =
			WindowManager::overlayRect(theEntry.menuOverlayID);
		anEntryPos.x = (anEntryRect.left + anEntryRect.right) / 2;
		anEntryPos.y = (anEntryRect.top + anEntryRect.bottom) / 2;
	}
	else
	{
		Hotspot anEntryHSPos, anEntryHSSize;
		std::string aStr;
		aStr = theEntry.shape.x; HotspotMap::stringToCoord(aStr, anEntryHSPos.x);
		aStr = theEntry.shape.y; HotspotMap::stringToCoord(aStr, anEntryHSPos.y);
		aStr = theEntry.shape.w; HotspotMap::stringToCoord(aStr, anEntryHSSize.x);
		aStr = theEntry.shape.h; HotspotMap::stringToCoord(aStr, anEntryHSSize.x);
		anEntryPos = WindowManager::hotspotToOverlayPos(anEntryHSPos);
		POINT anEntrySize = WindowManager::hotspotToOverlayPos(anEntryHSSize);
		anEntryPos.x = anEntryPos.x + anEntrySize.x / 2;
		anEntryPos.y = anEntryPos.y + anEntrySize.y / 2;
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


static void autoSwapHotspotAnchor(Hotspot& theHotspot, bool forXAxis)
{
	// This function changes a hotspot's anchor if it is very close to a
	// different anchor point (considering min, max, or center points only).
	Hotspot::Coord& out = forXAxis ? theHotspot.x : theHotspot.y;
	if( out.anchor != 0x0000 && out.anchor != 0xFFFF && out.anchor != 0x8000 )
		return;
	const POINT& aWinPos = WindowManager::hotspotToOverlayPos(theHotspot);
	const SIZE& aMaxWinPos = WindowManager::overlayTargetSize();
	const LONG aMaxWinPosOnAxis = forXAxis ? aMaxWinPos.cx : aMaxWinPos.cy;
	const LONG aWinPosOnAxis = forXAxis ? aWinPos.x : aWinPos.y;
	if( out.anchor != 0 && aWinPosOnAxis <= aMaxWinPosOnAxis * 0.02 )
	{
		out.anchor = 0;
		out.offset = dropTo<s16>(aWinPosOnAxis);
		if( gUIScale != 1.0 ) out.offset = s16(out.offset / gUIScale);
		return;
	}
	if( out.anchor != 0xFFFF && aWinPosOnAxis >= aMaxWinPosOnAxis * 0.98 )
	{
		out.anchor = 0xFFFF;
		out.offset = dropTo<s16>(aWinPosOnAxis - (aMaxWinPosOnAxis-1));
		if( gUIScale != 1.0 ) out.offset = s16(out.offset / gUIScale);
		return;
	}
	/* I'm not sure auto-swapping to center anchor is a good idea...
	if( out.anchor != 0x8000 &&
		aWinPos.x >= aMaxWinPos.cx * 0.47 &&
		aWinPos.x <= aMaxWinPos.cx * 0.53 &&
		aWinPos.y >= aMaxWinPos.cy * 0.47 &&
		aWinPos.y <= aMaxWinPos.cy * 0.53 )
	{
		out.anchor = 0x8000;
		out.offset = (aWinPosOnAxis - (aMaxWinPosOnAxis/2));
		if( gUIScale != 1.0 ) out.offset /= gUIScale;
		return;
	}
	*/
}


static void processCoordString(
	HWND hDlg,
	int theControlID,
	int theDelta = 0,
	bool theDeltaSetByMouse = false)
{
	if( !hDlg ) return;

	std::string* aDestStr = null;
	switch(theControlID)
	{
	case IDC_EDIT_X: aDestStr = &sState->entered.x; break;
	case IDC_EDIT_Y: aDestStr = &sState->entered.y; break;
	case IDC_EDIT_W: aDestStr = &sState->entered.w; break;
	case IDC_EDIT_H: aDestStr = &sState->entered.h; break;
	case IDC_EDIT_S: aDestStr = &sState->entered.offsetScale; break;
	}
	DBG_ASSERT(aDestStr);

	HWND hEdit = GetDlgItem(hDlg, theControlID);
	DBG_ASSERT(hEdit);
	std::string aControlStr;
	if( int aStrLen = GetWindowTextLength(hEdit) )
	{
		std::vector<WCHAR> aStrBuf(aStrLen+1);
		GetDlgItemText(hDlg, theControlID, &aStrBuf[0], aStrLen+1);
		aControlStr = narrow(&aStrBuf[0]);
	}
	if( !theDelta && aControlStr == *aDestStr )
		return;

	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry > 0);
	DBG_ASSERT(size_t(sState->activeEntry) < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];

	std::string result = aControlStr;
	std::string aTempStr = aControlStr;
	Hotspot::Coord aCoord = {};
	if( theControlID != IDC_EDIT_S )
		HotspotMap::stringToCoord(aTempStr, aCoord, &result);
	else if( floatFromString(aControlStr) <= 0 )
		result = "100%";
	if( result.empty() )
	{
		switch(theControlID)
		{
		case IDC_EDIT_X:
			result = HotspotMap::coordToString(
				Hotspot::Coord(), formatForCoord(anEntry, IDC_EDIT_X));
			break;
		case IDC_EDIT_Y:
			result = HotspotMap::coordToString(
				Hotspot::Coord(), formatForCoord(anEntry, IDC_EDIT_Y));
			break;
		case IDC_EDIT_W:
		case IDC_EDIT_H:
			result = "0";
			break;
		}
	}
	DBG_ASSERT(!result.empty());

	if( theDelta )
	{// Adjust the value while trying to maintain current formatting
		// Check if only specifying a non-standard anchor, meaning not CX, etc,
		// so must contain a decimal or a % symbol, and no offset specified
		// (including no "+0" for offset), and not trying to push past edges.
		// If it is a non-standard anchor, shift the anchor instead of offset.
		if( aCoord.offset == 0 &&
			(aCoord.anchor < 0xFFFF || theDelta <= 0 || theDeltaSetByMouse) &&
			(aCoord.anchor > 0 || theDelta >= 0 || theDeltaSetByMouse) &&
			result.find('+') == std::string::npos &&
			(result[result.size()-1] == '%' ||
			 result.find('.') != std::string::npos) )
		{
			Hotspot aHotspot; aHotspot.x = aHotspot.y = aCoord;
			POINT aWinPos = WindowManager::hotspotToOverlayPos(aHotspot);
			aWinPos.x += theDelta; aWinPos.y += theDelta;
			aHotspot = WindowManager::overlayPosToHotspot(aWinPos);
			switch(theControlID)
			{
			case IDC_EDIT_X: case IDC_EDIT_W:
				if( aWinPos.x >= WindowManager::overlayTargetSize().cx-1 )
					result = "100%";
				else if( aWinPos.x <= 0 )
					result = "0%";
				else
					result = HotspotMap::coordToString(
						aHotspot.x, HotspotMap::eHNS_Num);
				break;
			case IDC_EDIT_Y: case IDC_EDIT_H:
				if( aWinPos.y >= WindowManager::overlayTargetSize().cy-1 )
					result = "100%";
				else if( aWinPos.y <= 0 )
					result = "0%";
				else
					result = HotspotMap::coordToString(
						aHotspot.y, HotspotMap::eHNS_Num);
				break;
			}
		}
		else
		{
			if( theDeltaSetByMouse )
			{
				if( gUIScale != 1.0 )
				{
					double* aDeltaFP =
						theControlID == IDC_EDIT_X
							? &sState->unappliedDeltaX
							: &sState->unappliedDeltaY;
					*aDeltaFP += double(theDelta) / gUIScale;
					theDelta = int(*aDeltaFP);
					*aDeltaFP -= theDelta;
				}
				if( theControlID == IDC_EDIT_X )
				{
					Hotspot aHotspot; aHotspot.x = aCoord;
					aHotspot.x.offset =
						dropTo<s16>(aHotspot.x.offset + theDelta);
					aTempStr = sState->entered.y;
					HotspotMap::stringToCoord(aTempStr, aHotspot.y);
					autoSwapHotspotAnchor(aHotspot, true);
					aCoord.anchor = aHotspot.x.anchor;
					theDelta = aHotspot.x.offset - aCoord.offset;
				}
				if( theControlID == IDC_EDIT_Y )
				{
					Hotspot aHotspot; aHotspot.y = aCoord;
					aHotspot.y.offset =
						dropTo<s16>(aHotspot.y.offset + theDelta);
					aTempStr = sState->entered.x;
					HotspotMap::stringToCoord(aTempStr, aHotspot.x);
					autoSwapHotspotAnchor(aHotspot, false);
					aCoord.anchor = aHotspot.y.anchor;
					theDelta = aHotspot.y.offset - aCoord.offset;
				}
			}
			if( theDelta )
			{
				aCoord.offset = dropTo<s16>(aCoord.offset + theDelta);
				result = HotspotMap::coordToString(
					aCoord, formatForCoord(anEntry, theControlID));
				if( result[result.size()-1] == '%' )
					result += "+0";
			}
		}
	}

	if( aControlStr != result )
		SetWindowText(hEdit, widen(result).c_str());

	if( *aDestStr != result )
	{
		*aDestStr = result;
		sState->needsDrawPosUpdate = true;
		WindowPainter::redrawSystemOverlay();
	}
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
					(std::wstring(L"Repositioning: ") +
					 widen(anEntry.item.name) +
					 std::wstring(L" (at ") +
					 widen(strFormat("%.4g", aScaling * 100.0) + "%") +
					 std::wstring(L" scale)")).c_str());
			}
			else
			{
				SetWindowText(theDialog,
					(std::wstring(L"Repositioning: ") +
					 widen(anEntry.item.name)).c_str());
			}
		}
		// Allow Esc to cancel even when not the active window
		RegisterHotKey(NULL, kCancelToolbarHotkeyID, 0, VK_ESCAPE);
		// Populate controls with initial values from active entry
		if( entryIncludesPosition(anEntry) )
		{
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_X),
				widen(anEntry.shape.x).c_str());
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_Y),
				widen(anEntry.shape.y).c_str());
			HWND hSpin = GetDlgItem(theDialog, IDC_SPIN_X);
			std::pair<POINT, SIZE> aCtrlPos = getControlPos(theDialog, hSpin);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hSpin, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
			hSpin = GetDlgItem(theDialog, IDC_SPIN_Y);
			aCtrlPos = getControlPos(theDialog, hSpin);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hSpin, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
		}
		if( entryIncludesSize(anEntry) )
		{
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_W),
				widen(anEntry.shape.w).c_str());
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_H),
				widen(anEntry.shape.h).c_str());
			HWND hSpin = GetDlgItem(theDialog, IDC_SPIN_W);
			std::pair<POINT, SIZE> aCtrlPos = getControlPos(theDialog, hSpin);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hSpin, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
			hSpin = GetDlgItem(theDialog, IDC_SPIN_H);
			aCtrlPos = getControlPos(theDialog, hSpin);
			aCtrlPos.first.y += 2;
			aCtrlPos.second.cy -= 2;
			SetWindowPos(hSpin, NULL, aCtrlPos.first.x, aCtrlPos.first.y,
				aCtrlPos.second.cx, aCtrlPos.second.cy, SWP_NOZORDER);
		}
		if( entryIncludesAlignment(anEntry) )
		{
			HWND hDropList = GetDlgItem(theDialog, IDC_COMBO_ALIGN);
			for(int i = 0; i < eAlignment_Num; ++i)
			{
				SendMessage(hDropList, CB_ADDSTRING, 0,
					(LPARAM)widen(kAlignmentStr[i][0]).c_str());
			}
			if( anEntry.shape.alignment < eAlignment_Num )
			{
				SendMessage(hDropList, CB_SETCURSEL,
					(WPARAM)anEntry.shape.alignment, 0);
			}
		}
		if( entryIncludesScale(anEntry) )
		{
			SendDlgItemMessage(theDialog, IDC_SLIDER_S,
				TBM_SETRANGE, TRUE,
				MAKELPARAM(kMinOffsetScale, kMaxOffsetScale));
			const float aScale = floatFromString(sState->entered.offsetScale);
			if( anEntry.shape.offsetScale.empty() ||
				aScale <= 0 || aScale == 1 )
			{
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S), L"100%");
				EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), false);
				EnableWindow(GetDlgItem(theDialog, IDC_EDIT_S), false);
				SendDlgItemMessage(theDialog, IDC_SLIDER_S,
					TBM_SETPOS, TRUE, 100);
			}
			else
			{
				CheckDlgButton(theDialog, IDC_CHECK_SCALE, true);
				SetWindowText(
					GetDlgItem(theDialog, IDC_EDIT_S),
					widen(anEntry.shape.offsetScale).c_str());
				SendDlgItemMessage(theDialog, IDC_SLIDER_S,
					TBM_SETPOS, TRUE, LPARAM(aScale * 100));
			}
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
					case IDC_EDIT_W: case IDC_EDIT_H:
						// Set focus to OK button but don't click it yet
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
		case IDC_EDIT_S:
			if( HIWORD(wParam) == EN_KILLFOCUS )
			{
				processCoordString(theDialog, LOWORD(wParam));
				applyNewPosition();
				if( LOWORD(wParam) == IDC_EDIT_S )
				{// Update slider to match
					const float aScale =
						floatFromString(sState->entered.offsetScale);
					SendDlgItemMessage(theDialog, IDC_SLIDER_S,
						TBM_SETPOS, TRUE, LPARAM(aScale * 100));
				}
			}
			break;

		case IDC_CHECK_SCALE:
			if( HIWORD(wParam) == BN_CLICKED )
			{
				bool isChecked =
					IsDlgButtonChecked(theDialog, IDC_CHECK_SCALE)
						== BST_CHECKED;
				EnableWindow(GetDlgItem(theDialog, IDC_SLIDER_S), isChecked);
				EnableWindow(GetDlgItem(theDialog, IDC_EDIT_S), isChecked);
				if( isChecked )
				{
					processCoordString(theDialog, IDC_EDIT_S);
					applyNewPosition();
				}
				else if( !sState->entered.offsetScale.empty() )
				{
					sState->entered.offsetScale.clear();
					sState->needsDrawPosUpdate = true;
					WindowPainter::redrawSystemOverlay();
					applyNewPosition();
				}
			}
			break;

		case IDC_COMBO_ALIGN:
			if( HIWORD(wParam) == CBN_SELCHANGE && sState )
			{
				sState->entered.alignment = EAlignment(SendMessage(
					GetDlgItem(theDialog, IDC_COMBO_ALIGN),
					CB_GETCURSEL, 0, 0));
				applyNewPosition();
			}
			break;
		}
		break;

	case WM_HSCROLL:
		if( (HWND)lParam == GetDlgItem(theDialog, IDC_SLIDER_S) )
		{
			const int aScaleVal = dropTo<int>(
				SendDlgItemMessage(theDialog, IDC_SLIDER_S, TBM_GETPOS, 0, 0));
			if( !sState->entered.offsetScale.empty() &&
				sState->entered.offsetScale
					[sState->entered.offsetScale.size()-1] == '%' )
			{
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
					widen(strFormat("%d%%", aScaleVal)).c_str());
			}
			else
			{
				SetWindowText(GetDlgItem(theDialog, IDC_EDIT_S),
					widen(strFormat("%.4g", aScaleVal / 100.0)).c_str());
			}
			processCoordString(theDialog, IDC_EDIT_S);
			applyNewPosition();
		}
		break;

	case WM_NOTIFY:
		switch(((LPNMHDR)lParam)->idFrom)
		{
		case IDC_SPIN_X: case IDC_SPIN_W:
			processCoordString(theDialog,
				IDC_EDIT_X +
				dropTo<int>(((LPNMHDR)lParam)->idFrom) - IDC_SPIN_X,
				-((LPNMUPDOWN)lParam)->iDelta);
			applyNewPosition();
			break;
		case IDC_SPIN_Y: case IDC_SPIN_H:
			processCoordString(theDialog,
				IDC_EDIT_X +
				dropTo<int>(((LPNMHDR)lParam)->idFrom) - IDC_SPIN_X,
				((LPNMUPDOWN)lParam)->iDelta);
			applyNewPosition();
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
		WindowPainter::redrawSystemOverlay();
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
				processCoordString(
					WindowManager::toolbarHandle(),
					IDC_EDIT_X,
					aMousePos.x - sState->lastMouseDragPos.x,
					true);
			}
			if( aMousePos.y != sState->lastMouseDragPos.y )
			{
				processCoordString(
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
				applyNewPosition();
				sState->needsDrawPosUpdate = true;
				WindowPainter::redrawSystemOverlay();
			}
		}
		return 0;
	case WM_SETCURSOR:
		if( !sState->draggingWithMouse )
		{
			SetCursor(LoadCursor(NULL, IDC_SIZEALL));
			processCoordString(WindowManager::toolbarHandle(), IDC_EDIT_X);
			processCoordString(WindowManager::toolbarHandle(), IDC_EDIT_Y);
		}
		return 0;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static void updateDrawHotspot(
	LayoutEntry& theEntry,
	const LayoutEntry::Shape& theShape,
	bool updateParentIfNeeded = true,
	bool updateChildren = true)
{
	DBG_ASSERT(entryIncludesPosition(theEntry));
	std::string aHotspotStr = theShape.x + ", " + theShape.y;
	HotspotMap::stringToHotspot(aHotspotStr, theEntry.drawHotspot);
	theEntry.drawOffX = theEntry.drawOffY = 0;
	theEntry.drawSize = Hotspot();
	theEntry.drawOffScale = 0;
	theEntry.drawOffScale = floatFromString(theShape.offsetScale);
	if( theEntry.drawOffScale == 0 )
		theEntry.drawOffScale = 1.0f;
	if( theEntry.rangeCount > 1 )
	{
		theEntry.drawOffX = theEntry.drawHotspot.x.offset;
		theEntry.drawOffY = theEntry.drawHotspot.y.offset;
	}
	if( theEntry.type == LayoutEntry::eType_CopyIcon &&
		entryIncludesSize(theEntry) )
	{
		aHotspotStr = theShape.w + ", " + theShape.h;
		HotspotMap::stringToHotspot(aHotspotStr, theEntry.drawSize);
	}
	if( entryIsAnOffset(theEntry) )
	{
		LayoutEntry& aParent = sState->entries[theEntry.item.parentIndex];
		DBG_ASSERT(aParent.type == theEntry.type);
		if( updateParentIfNeeded )
			updateDrawHotspot(aParent, aParent.shape, true, false);
		theEntry.drawOffScale = aParent.drawOffScale;
		Hotspot anAnchor = aParent.drawHotspot;
		if( aParent.rangeCount > 1 )
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
		if( theEntry.rangeCount > 1 || theEntry.drawHotspot.x.anchor == 0 )
		{
			theEntry.drawHotspot.x.anchor = anAnchor.x.anchor;
			theEntry.drawHotspot.x.offset = s16(clamp(int(
				theEntry.drawHotspot.x.offset * theEntry.drawOffScale) +
				anAnchor.x.offset, -0x8000, 0x7FFF));
		}
		if( theEntry.rangeCount > 1 || theEntry.drawHotspot.y.anchor == 0 )
		{
			theEntry.drawHotspot.y.anchor = anAnchor.y.anchor;
			theEntry.drawHotspot.y.offset = s16(clamp(int(
				theEntry.drawHotspot.y.offset * theEntry.drawOffScale) +
				anAnchor.y.offset, -0x8000, 0x7FFF));
		}
		if( theEntry.type == LayoutEntry::eType_CopyIcon )
			theEntry.drawSize = aParent.drawSize;
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


static RECT drawBoundBox(
	HDC hdc,
	const POINT& theOrigin,
	const SIZE& theBoxSize,
	COLORREF theEraseColor,
	bool isActiveBox)
{
	RECT aRect = {
		theOrigin.x, theOrigin.y,
		theOrigin.x + theBoxSize.cx, theOrigin.y + theBoxSize.cy };
	RECT aFullDrawnRect = { 0 };

	// Draw outer-most black border
	COLORREF oldBrushColor = SetDCBrushColor(hdc, theEraseColor);
	HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
	InflateRect(&aRect, 2, 2);
	Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
	UnionRect(&aFullDrawnRect, &aFullDrawnRect, &aRect);

	// For active box, draw anchor in top-left corner representing base pos
	if( isActiveBox )
	{
		RECT anAnchorRect = { 0 };
		anAnchorRect.left = theOrigin.x - kBoundBoxAnchorDrawSize;
		anAnchorRect.top = theOrigin.y - kBoundBoxAnchorDrawSize;
		anAnchorRect.right = theOrigin.x + 3;
		anAnchorRect.bottom = theOrigin.y + 3;
		SetDCBrushColor(hdc, oldBrushColor);
		SelectObject(hdc, hOldBrush);
		Rectangle(hdc,
			anAnchorRect.left, anAnchorRect.top,
			anAnchorRect.right, anAnchorRect.bottom);
		UnionRect(&aFullDrawnRect, &aFullDrawnRect, &anAnchorRect);
		SetDCBrushColor(hdc, theEraseColor);
		SelectObject(hdc, GetStockObject(NULL_BRUSH));
	}

	// Draw white/grey border just outside of inner rect
	InflateRect(&aRect, -1, -1);
	HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
	COLORREF oldPenColor = SetDCPenColor(hdc, oldBrushColor);
	Rectangle(hdc, aRect.left, aRect.top, aRect.right, aRect.bottom);
	SetDCPenColor(hdc, oldPenColor);
	SelectObject(hdc, hOldPen);
	SelectObject(hdc, hOldBrush);
	InflateRect(&aRect, -1, -1);

	// Erase contents of inner rect if this is the active box
	if( isActiveBox )
		FillRect(hdc, &aRect, hOldBrush);

	SetDCBrushColor(hdc, oldBrushColor);
	return aFullDrawnRect;
}


static RECT drawHotspot(
	HDC hdc,
	const Hotspot& theHotspot,
	const Hotspot* theBoxSize,
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

	if( theBoxSize )
	{
		const SIZE aBoxWH = {
			max(1L, LONG(u16ToRangeVal(theBoxSize->x.anchor, aTargetSize.cx)) +
				LONG(theBoxSize->x.offset * gUIScale)),
			max(1L, LONG(u16ToRangeVal(theBoxSize->y.anchor, aTargetSize.cy)) +
				LONG(theBoxSize->y.offset * gUIScale)) };
		return drawBoundBox(
			hdc, aCenterPoint, aBoxWH, theEraseColor, isActiveHotspot);
	}

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
	Hotspot* aDrawBoxSize =
		theEntry.type == LayoutEntry::eType_CopyIcon
			? &theEntry.drawSize : null;
	Hotspot aHotspot = theEntry.drawHotspot;
	if( !isActiveHotspot )
	{// Draw basic hotspot
		theEntry.drawnRect = drawHotspot(hdc, aHotspot, aDrawBoxSize,
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
			RECT aDrawnRect = drawHotspot(hdc, aHotspot, aDrawBoxSize,
				kChildHotspotDrawSize, theWindowRect, theEraseColor, false);
			UnionRect(&aRangeDrawnRect, &aRangeDrawnRect, &aDrawnRect);
		}
	}
	if( isActiveHotspot )
	{// Draw primary hotspot last so is over top of all children/range
		COLORREF oldColor = SetDCBrushColor(hdc, RGB(255, 255, 255));
		theEntry.drawnRect = drawHotspot(hdc, theEntry.drawHotspot,
			aDrawBoxSize, kActiveHotspotDrawSize, theWindowRect,
			theEraseColor, true);
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
		if( entryIncludesPosition(anEntry) )
		{
			WindowManager::setSystemOverlayCallbacks(
				layoutEditorWindowProc, layoutEditorPaintFunc);
		}
		WindowManager::createToolbarWindow(
			entryIncludesAlignment(anEntry)
				? entryIncludesPosition(anEntry)
					? IDD_DIALOG_LAYOUT_XYWHA_TOOLBAR
					: IDD_DIALOG_LAYOUT_WHA_TOOLBAR
				: entryIncludesSize(anEntry)
					? IDD_DIALOG_LAYOUT_XYWH_TOOLBAR
				: entryIncludesScale(anEntry)
					? IDD_DIALOG_LAYOUT_XYS_TOOLBAR
					: IDD_DIALOG_LAYOUT_XY_TOOLBAR,
			editLayoutToolbarProc,
			anEntry.type == LayoutEntry::eType_MenuOverlay
				? anEntry.menuOverlayID : -1);
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
	std::string aHotspotName =
		theEntryNameMap.keys()[theIndex];
	const int aHotspotEndIdx =
		breakOffIntegerSuffix(aHotspotName);
	const int anArrayPrevIdx = -theEntry.item.parentIndex;
	if( aHotspotEndIdx == anArrayPrevIdx )
	{
		theEntry.item.parentIndex = theEntryNameMap.values()[theIndex];
		return false;
	}

	return true;
}


static void setEntryParent(
	LayoutEntry& theEntry,
	const StringToValueMap<u32>& theEntryNameMap)
{
	if( theEntry.rangeCount == 0 )
		return;
	std::string anArrayName = theEntry.item.name;
	breakOffIntegerSuffix(anArrayName);
	if( theEntry.rangeCount == 1 )
	{// Search for an anchor to act as parent
		const u32* aParentIdx = theEntryNameMap.find(anArrayName);
		if( aParentIdx )
			theEntry.item.parentIndex = *aParentIdx;
		return;
	}

	// Search for previous index in the array
	DBG_ASSERT(anArrayName[anArrayName.size()-1] == '-');
	anArrayName.resize(anArrayName.size()-1);
	const int anArrayStartIdx = breakOffIntegerSuffix(anArrayName);
	DBG_ASSERT(anArrayStartIdx >= 0);
	const int anArrayPrevIdx = anArrayStartIdx - 1;
	const u32* aParentIdx = theEntryNameMap.find(
		anArrayName + toString(anArrayPrevIdx));
	if( aParentIdx )
	{
		theEntry.item.parentIndex = *aParentIdx;
		return;
	}
	const int oldParentIndex = theEntry.item.parentIndex;
	theEntry.item.parentIndex = -anArrayPrevIdx;
	theEntryNameMap.findAllWithPrefix(anArrayName, setEntryParentFromMap);
	if( theEntry.item.parentIndex < 0 )
		theEntry.item.parentIndex = oldParentIndex;
	else
		return;
	aParentIdx = theEntryNameMap.find(anArrayName);
	if( aParentIdx )
		theEntry.item.parentIndex = *aParentIdx;
	return;
}


static void addArrayEntries(
	std::vector<LayoutEntry>& theEntryList,
	LayoutEntry::EType theCategoryType,
	LayoutEntry::EType theEntryType)
{
	const int aFirstNewEntryIdx = intSize(theEntryList.size());
	LayoutEntry aNewEntry;
	aNewEntry.type = theEntryType;
	aNewEntry.item.parentIndex = theCategoryType;

	const Profile::PropertyMap& aPropertyMap =
		Profile::getSectionProperties(theEntryList[theCategoryType].posSect);
	StringToValueMap<u32> anEntryNameToIdxMap;
	for(int i = 0, end = intSize(aPropertyMap.size()); i < end; ++i)
	{
		aNewEntry.rangeCount = 0;
		aNewEntry.item.name = aPropertyMap.keys()[i];
		aNewEntry.propName = aNewEntry.item.name;
		std::string aKeyName = aNewEntry.item.name;
		anEntryNameToIdxMap.setValue(
			aKeyName, u32(theEntryList.size()));
		std::string aDesc = aPropertyMap.vals()[i].str;
		Hotspot aHotspot;
		HotspotMap::stringToCoord(aDesc, aHotspot.x, &aNewEntry.shape.x);
		HotspotMap::stringToCoord(aDesc, aHotspot.y, &aNewEntry.shape.y);
		if( theEntryType == LayoutEntry::eType_CopyIcon )
		{
			Hotspot::Coord tmp;
			HotspotMap::stringToCoord(aDesc, tmp, &aNewEntry.shape.w);
			HotspotMap::stringToCoord(aDesc, tmp, &aNewEntry.shape.h);
		}
		if( aHotspot.x.anchor == 0 && aHotspot.y.anchor == 0 &&
			aNewEntry.shape.h.empty() )
		{// Possibly has a parent to offset from
			aNewEntry.shape.w.clear();
			const int anArrayEndIdx = breakOffIntegerSuffix(aKeyName);
			if( anArrayEndIdx > 0 )
			{
				aNewEntry.rangeCount = 1;
				int anArrayStartIdx = anArrayEndIdx;
				if( aKeyName[aKeyName.size()-1] == '-' )
				{// Part of a range of values, like Name2-8
					aKeyName.resize(aKeyName.size()-1);
					anArrayStartIdx = breakOffIntegerSuffix(aKeyName);
				}
				aNewEntry.rangeCount += anArrayEndIdx - anArrayStartIdx;
			}
		}
		if( aNewEntry.rangeCount == 0 && !aDesc.empty() && aDesc[0] == '*' )
			aNewEntry.shape.offsetScale = trim(aDesc.substr(1));
		else
			aNewEntry.shape.offsetScale.clear();
		theEntryList.push_back(aNewEntry);
	}

	// Link items in arrays with their direct parents
	for(int i = aFirstNewEntryIdx,
		end = intSize(theEntryList.size()); i < end; ++i)
	{
		setEntryParent(theEntryList[i], anEntryNameToIdxMap);
	}
}


static void tryFetchMenuHotspot(
	LayoutEntry& theEntry,
	const std::string& thePropName,
	const std::string& theReadSect,
	const std::string& theWriteSect)
{
	const bool isPosition = thePropName == kPositionKey;
	const bool isAlignment = !isPosition && thePropName == kAlignmentKey;
	const bool isSize = !isPosition && !isAlignment;

	std::string* aDestSectStr =
		isPosition ? &theEntry.posSect :
		isSize ? &theEntry.sizeSect :
		/*isAlignment ?*/ &theEntry.alignSect;
	if( !aDestSectStr->empty() )
		return;
	if( theReadSect.empty() )
	{
		*aDestSectStr = theWriteSect;
		if( isPosition )
		{
			theEntry.shape.x = HotspotMap::coordToString(
				Hotspot::Coord(), formatForCoord(theEntry, IDC_EDIT_X));
			theEntry.shape.y = HotspotMap::coordToString(
				Hotspot::Coord(), formatForCoord(theEntry, IDC_EDIT_Y));
		}
		else if( isSize )
		{
			theEntry.shape.w = theEntry.shape.h = "0";
			theEntry.propName = thePropName;
		}
		else// if( isAlignment )
		{
			theEntry.shape.alignment =
				entryIncludesPosition(theEntry)
					? eAlignment_L_T
					: eAlignment_CX_CY;
		}
	}
	else
	{
		std::string aValStr = Profile::getStr(theReadSect, thePropName);
		if( !aValStr.empty() )
		{
			*aDestSectStr = theWriteSect;
			Hotspot tmp;
			if( isPosition )
			{
				HotspotMap::stringToCoord(aValStr, tmp.x, &theEntry.shape.x);
				HotspotMap::stringToCoord(aValStr, tmp.y, &theEntry.shape.y);
			}
			else if( isSize )
			{
				HotspotMap::stringToCoord(aValStr, tmp.x, &theEntry.shape.w);
				HotspotMap::stringToCoord(aValStr, tmp.y, &theEntry.shape.h);
				theEntry.propName = thePropName;
			}
			else //if( isAlignment )
			{
				HotspotMap::stringToHotspot(aValStr, tmp);
				int anAlignVal =
					tmp.x.anchor < 0x4000	? eAlignment_L_T :
					tmp.x.anchor > 0xC000	? eAlignment_R_T :
					/*otherwise*/			  eAlignment_CX_T;
				anAlignVal +=
					tmp.y.anchor < 0x4000	? eAlignment_L_T :
					tmp.y.anchor > 0xC000	? eAlignment_L_B :
					/*otherwise*/			  eAlignment_L_CY;
				theEntry.shape.alignment = EAlignment(anAlignVal);
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

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
		aCatEntry.posSect = kHotspotsPrefix;
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_CopyIconCategory;
		aCatEntry.item.name = "Copy Icons";
		aCatEntry.posSect = kIconsPrefix;
		sState->entries.push_back(aCatEntry);
		aCatEntry.type = LayoutEntry::eType_MenuCategory;
		aCatEntry.item.name = "Menus";
		aCatEntry.posSect = kMenuPrefix;
		sState->entries.push_back(aCatEntry);
		DBG_ASSERT(sState->entries.size() == LayoutEntry::eType_CategoryNum);
		#ifndef NDEBUG
		for(int i = 0; i < LayoutEntry::eType_CategoryNum; ++i)
			DBG_ASSERT(sState->entries[i].type == i);
		#endif
	}

	// Collect the hotspots
	addArrayEntries(
		sState->entries,
		LayoutEntry::eType_HotspotCategory,
		LayoutEntry::eType_Hotspot);

	// Collect the copy icon regions
	addArrayEntries(
		sState->entries,
		LayoutEntry::eType_CopyIconCategory,
		LayoutEntry::eType_CopyIcon);

	// Collect menu position keys from InputMap
	for(int i = 0, end = intSize(InputMap::menuOverlayCount()); i < end; ++i)
	{
		const int aRootMenuID = InputMap::overlayRootMenuID(i);
		LayoutEntry aNewEntry;
		aNewEntry.item.isRootCategory = false;
		aNewEntry.menuOverlayID = i;
		aNewEntry.item.name = InputMap::menuKeyName(aRootMenuID);
		aNewEntry.type = LayoutEntry::eType_MenuOverlay;
		aNewEntry.item.parentIndex = LayoutEntry::eType_MenuCategory;
		if( aNewEntry.item.name.empty() )
			continue;

		const std::string& aMenuSectName = kMenuPrefix + aNewEntry.item.name;
		if( entryIncludesPosition(aNewEntry ) )
		{
			// Try read/write [Menu.Name]/Position
			tryFetchMenuHotspot(aNewEntry, kPositionKey,
				aMenuSectName, aMenuSectName);
			// Just write to [Menu.Name]/Position w/ default settings
			tryFetchMenuHotspot(aNewEntry, kPositionKey, "", aMenuSectName);
		}

		// Try read/write [Menu.Name]/ItemSize
		tryFetchMenuHotspot(aNewEntry, kItemSizeKey,
			aMenuSectName, aMenuSectName);
		// Try read/write [Menu.Name]/Size
		tryFetchMenuHotspot(aNewEntry, kSizeKey,
			aMenuSectName, aMenuSectName);

		// Try read [Appearance]/ItemSize and write to [Menu.Name]/ItemSize
		tryFetchMenuHotspot(aNewEntry, kItemSizeKey, kDefMenuSectName,
			aMenuSectName);
		// Just write defaults to [Menu.Name]/ItemSize
		tryFetchMenuHotspot(aNewEntry, kItemSizeKey, "", aMenuSectName);

		// Try read/write [Menu.Name]/Alignment
		tryFetchMenuHotspot(aNewEntry, kAlignmentKey,
			aMenuSectName, aMenuSectName);
		// Try read [Appearance]/Alignment and write to [enu.Name]/Alignment
		tryFetchMenuHotspot(aNewEntry, kAlignmentKey, kDefMenuSectName,
			aMenuSectName);
		// Just write to [Menu.Name]/Alignment w/ default settings
		tryFetchMenuHotspot(aNewEntry, kAlignmentKey, "",
			aMenuSectName);

		sState->entries.push_back(aNewEntry);
	}

	// Prepare for prompt dialog
	if( sState->entries.size() <= LayoutEntry::eType_CategoryNum )
	{
		logError("Current profile has no positional items to edit!");
		return;
	}

	// Link parent entries to their children for drawing later
	for(int i = LayoutEntry::eType_CategoryNum,
		end = intSize(sState->entries.size()); i < end; ++i)
	{
		if( sState->entries[i].item.parentIndex >= LayoutEntry::eType_CategoryNum )
			sState->entries[sState->entries[i].item.parentIndex].children.push_back(i);
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
