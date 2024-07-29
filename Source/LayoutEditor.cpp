//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "LayoutEditor.h"

#include "Dialogs.h"
#include "HotspotMap.h"
#include "HUD.h"
#include "InputMap.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "WindowManager.h"

namespace LayoutEditor
{

// Uncomment this to print status of target app/window tracking to debug window
//#define LAYOUT_EDITOR_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kActiveHotspotDrawSize = 5,
kChildHotspotDrawSize = 3,
};


const char* kHotspotsPrefix = "Hotspots";
const char* kIconsPrefix = "Icons";
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
const char* kDefHUDPrefx = "HUD";
const char* kDefMenuPrefix = "Menu";
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

struct LayoutEntry
{
	enum EType
	{
		eType_Root,
		eType_HotspotCategory,
		eType_CopyIconCategory,
		eType_MenuCategory,
		eType_HUDCategory,

		eType_Hotspot,
		eType_CopyIcon,
		eType_HUDElement,

		eType_Num,
		eType_CategoryNum = eType_Hotspot
	} type;
	Dialogs::TreeViewDialogItem item;

	struct Shape
	{
		std::string x, y, w, h;
		EAlignment alignment;
		Shape() : alignment(eAlignment_Num) {}
		bool operator==(const Shape& rhs) const
		{
			return
				x == rhs.x && y == rhs.y &&
				w == rhs.w && h == rhs.h &&
				alignment == rhs.alignment;
		}
		bool operator!=(const Shape& rhs) const
		{ return !(*this == rhs); }
	} shape;

	std::string posSect, sizeSect, alignSect, propName;
	std::vector<size_t> children;
	Hotspot drawHotspot;
	s16 drawOffX, drawOffY;
	u16 hudElementID;
	u16 rangeCount;

	LayoutEntry() :
		type(eType_Num),
		drawOffX(),
		drawOffY(),
		hudElementID(),
		rangeCount()
	{
		item.parentIndex = 0;
		item.isRootCategory = false;
	}
};


struct EditorState
{
	std::vector<LayoutEntry> entries;
	std::vector<Dialogs::TreeViewDialogItem*> dialogItems;
	StringToValueMap<u16> hotspotNameMapCache;
	StringToValueMap<u16> hotspotArrayNameMapCache;
	LayoutEntry::Shape entered, applied;
	size_t activeEntry;
	bool needsDrawPosUpdate;

	EditorState() : activeEntry(), needsDrawPosUpdate(true) {}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static EditorState* sState = null;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef LAYOUT_EDITOR_DEBUG_PRINT
#define layoutDebugPrint(...) debugPrint("LayoutEditor: " __VA_ARGS__)
#else
#define layoutDebugPrint(...) ((void)0)
#endif

// Forward declares
static void promptForEditEntry();

bool entryIncludesPosition(const LayoutEntry& theEntry)
{
	if( theEntry.type != LayoutEntry::eType_HUDElement )
		return true;
	switch(InputMap::hudElementType(theEntry.hudElementID))
	{
	case eMenuStyle_Hotspots:
		return false;
	}
	return true;
}


bool entryIncludesSize(const LayoutEntry& theEntry)
{
	switch(theEntry.type)
	{
	case LayoutEntry::eType_Hotspot:
		return false;
	case LayoutEntry::eType_CopyIcon:
		return theEntry.item.parentIndex < LayoutEntry::eType_CategoryNum;
	case LayoutEntry::eType_HUDElement:
		return true;
	}

	return false;
}


bool entryIncludesAlignment(const LayoutEntry& theEntry)
{
	return theEntry.type == LayoutEntry::eType_HUDElement;
}


bool entryIsAnOffset(const LayoutEntry& theEntry)
{
	if( theEntry.item.parentIndex >= LayoutEntry::eType_CategoryNum )
		return true;
	if( theEntry.type != LayoutEntry::eType_HUDElement )
		return false;
	switch(InputMap::hudElementType(theEntry.hudElementID))
	{
	case eHUDType_Hotspot:
	case eHUDType_KBArrayLast:
	case eHUDType_KBArrayDefault:
		return true;
	}
	return false;
}


static void applyNewPosition()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	const bool needNewPos =
		sState->entered.x != sState->applied.x ||
		sState->entered.y != sState->applied.y;
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
		layoutDebugPrint("Applying altered region to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(kIconsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y + ", " +
			sState->entered.w + ", " + sState->entered.h, false);
	}
	else if( needNewPos && anEntry.type == LayoutEntry::eType_CopyIcon )
	{
		layoutDebugPrint("Applying altered position to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(kIconsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y, false);
	}
	else if( needNewPos && anEntry.type == LayoutEntry::eType_Hotspot )
	{
		layoutDebugPrint("Applying altered position to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(kHotspotsPrefix, anEntry.propName,
			sState->entered.x + ", " + sState->entered.y, false);
	}
	else if( needNewPos && !anEntry.posSect.empty() )
	{
		layoutDebugPrint("Applying altered position to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.posSect, kPositionKey,
			sState->entered.x + ", " + sState->entered.y, false);
	}
	if( needNewSize && !anEntry.sizeSect.empty() )
	{
		layoutDebugPrint("Applying altered size to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.sizeSect, anEntry.propName,
			sState->entered.w + ", " + sState->entered.h, false);
	}
	if( needNewAlign && !anEntry.alignSect.empty() )
	{
		layoutDebugPrint("Applying altered alignment to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.alignSect, kAlignmentKey,
			kAlignmentStr[sState->entered.alignment][1], false);
	}
	sState->applied = sState->entered;

	switch(anEntry.type)
	{
	case LayoutEntry::eType_Hotspot:
		InputMap::reloadHotspotKey(anEntry.item.name,
			sState->hotspotNameMapCache,
			sState->hotspotArrayNameMapCache);
		HotspotMap::reloadPositions();
		for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
		{
			if( InputMap::hudElementType(i) == eMenuStyle_Hotspots ||
				InputMap::hudElementType(i) == eHUDType_Hotspot )
			{
				gReshapeHUD.set(i);
			}
		}
		break;
	case LayoutEntry::eType_CopyIcon:
		HUD::reloadCopyIconLabel(anEntry.item.name);
		break;
	case LayoutEntry::eType_HUDElement:
		HUD::reloadElementShape(anEntry.hudElementID);
		break;
	}
}


static void cancelRepositioning()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( !gShutdown && sState->applied != anEntry.shape )
	{
		layoutDebugPrint("Restoring previous position/size of '%s'\n",
			anEntry.item.name.c_str());
		sState->entered = anEntry.shape;
		applyNewPosition();
		Profile::saveChangesToFile();
	}
	//WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	HUD::setSystemOverlayDrawHook(NULL);
	WindowManager::destroyToolbarWindow();
}


static void saveNewPosition()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	applyNewPosition();
	if( anEntry.shape != sState->applied )
	{
		anEntry.shape = sState->applied;
		Profile::saveChangesToFile();
		layoutDebugPrint("New position saved to profile\n");
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
}


static HotspotMap::EHotspotNamingConvention formatForCoord(
	const LayoutEntry& theEntry, int theEditControlID)
{
	switch(theEditControlID)
	{
	case IDC_EDIT_X:
		return entryIsAnOffset(theEntry)
			? HotspotMap::eHNC_X_Off
			: HotspotMap::eHNC_X;
	case IDC_EDIT_Y:
		return entryIsAnOffset(theEntry)
			? HotspotMap::eHNC_Y_Off
			: HotspotMap::eHNC_Y;
	case IDC_EDIT_W:
		return HotspotMap::eHNC_W;
	case IDC_EDIT_H:
		return HotspotMap::eHNC_H;
	}

	return HotspotMap::eHNC_Num;
}


static void setInitialToolbarPos(HWND hDlg, const LayoutEntry& theEntry)
{
	if( !sState || !sState->activeEntry )
		return;

	// Position the tool bar as far as possible from the object to be moved,
	// so that it is less likely to end up overlapping the object
	POINT anEntryPos;
	if( theEntry.type == LayoutEntry::eType_HUDElement )
	{
		const RECT& anEntryRect =
			WindowManager::hudElementRect(theEntry.hudElementID);
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
	anOverlayRect = WindowManager::overlayTargetScreenRect();
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


static void processEditControlString(HWND hDlg, int theEditControlID)
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	HWND hEdit = GetDlgItem(hDlg, theEditControlID);
	DBG_ASSERT(hEdit);
	std::string aControlStr;
	if( int aStrLen = GetWindowTextLength(hEdit) )
	{
		std::vector<WCHAR> aStrBuf(aStrLen+1);
		GetDlgItemText(hDlg, theEditControlID, &aStrBuf[0], aStrLen+1);
		aControlStr = narrow(&aStrBuf[0]);
	}

	Hotspot::Coord tmp;
	std::string aValidatedStr = aControlStr;
	std::string aTestStr = aControlStr;
	HotspotMap::stringToCoord(aTestStr, tmp, &aValidatedStr);
	if( aValidatedStr.empty() )
	{
		switch(theEditControlID)
		{
		case IDC_EDIT_X:
			aValidatedStr = HotspotMap::coordToString(
				Hotspot::Coord(), formatForCoord(anEntry, IDC_EDIT_X));
			break;
		case IDC_EDIT_Y:
			aValidatedStr = HotspotMap::coordToString(
				Hotspot::Coord(), formatForCoord(anEntry, IDC_EDIT_Y));
			break;
		case IDC_EDIT_W:
		case IDC_EDIT_H:
			aValidatedStr = "4";
			break;
		}
	}
	DBG_ASSERT(!aValidatedStr.empty());

	if( aValidatedStr != aControlStr )
		SetWindowText(hEdit, widen(aValidatedStr).c_str());

	switch(theEditControlID)
	{
	case IDC_EDIT_X: sState->entered.x = aValidatedStr; break;
	case IDC_EDIT_Y: sState->entered.y = aValidatedStr; break;
	case IDC_EDIT_W: sState->entered.w = aValidatedStr; break;
	case IDC_EDIT_H: sState->entered.h = aValidatedStr; break;
	}
	sState->needsDrawPosUpdate = true;
	HUD::redrawSystemOverlay();

	applyNewPosition();
}


static INT_PTR CALLBACK editLayoutToolbarProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];

	switch(theMessage)
	{
	case WM_INITDIALOG:
		layoutDebugPrint("Initializing repositioning toolbar\n");
		// Set title to match the entry name
		SetWindowText(theDialog,
			(std::wstring(L"Repositioning: ") +
			 widen(anEntry.item.name)).c_str());
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
		}
		if( entryIncludesSize(anEntry) )
		{
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_W),
				widen(anEntry.shape.w).c_str());
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_H),
				widen(anEntry.shape.h).c_str());
		}
		if( entryIncludesAlignment(anEntry) )
		{
			HWND hDropList = GetDlgItem(theDialog, IDC_COMBO_ALIGN);
			for(size_t i = 0; i < eAlignment_Num; ++i)
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

		case IDC_EDIT_X: case IDC_EDIT_Y: case IDC_EDIT_W: case IDC_EDIT_H:
			if( HIWORD(wParam) == EN_KILLFOCUS )
				processEditControlString(theDialog, LOWORD(wParam));
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
	case WM_DESTROY:
		UnregisterHotKey(NULL, kCancelToolbarHotkeyID);
		break;
	}

	return (INT_PTR)FALSE;
}


static LRESULT CALLBACK layoutEditorWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	if( theMessage == WM_LBUTTONDOWN )
	{
		layoutDebugPrint("Mouse click detected in overlay!\n");
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
	std::string aHotspotStr = theShape.x + ", " + theShape.y;
	HotspotMap::stringToHotspot(aHotspotStr, theEntry.drawHotspot);
	theEntry.drawOffX = theEntry.drawOffY = 0;
	if( theEntry.rangeCount > 1 )
	{
		theEntry.drawOffX = theEntry.drawHotspot.x.offset;
		theEntry.drawOffY = theEntry.drawHotspot.y.offset;
	}
	if( theEntry.item.parentIndex >= LayoutEntry::eType_CategoryNum )
	{
		LayoutEntry& aParent = sState->entries[theEntry.item.parentIndex];
		DBG_ASSERT(aParent.type == theEntry.type);
		if( updateParentIfNeeded )
			updateDrawHotspot(aParent, aParent.shape, true, false);
		Hotspot anAnchor = aParent.drawHotspot;
		if( aParent.rangeCount > 1 )
		{
			anAnchor.x.offset += aParent.drawOffX * (aParent.rangeCount-1);
			anAnchor.y.offset += aParent.drawOffY * (aParent.rangeCount-1);
		}
		if( theEntry.rangeCount > 1 || theEntry.drawHotspot.x.anchor == 0 )
		{
			theEntry.drawHotspot.x.anchor = anAnchor.x.anchor;
			theEntry.drawHotspot.x.offset += anAnchor.x.offset;
		}
		if( theEntry.rangeCount > 1 || theEntry.drawHotspot.y.anchor == 0 )
		{
			theEntry.drawHotspot.y.anchor = anAnchor.y.anchor;
			theEntry.drawHotspot.y.offset += anAnchor.y.offset;
		}
	}
	if( updateChildren )
	{
		for(size_t i = 0; i < theEntry.children.size(); ++i )
		{
			LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
			updateDrawHotspot(aChildEntry, aChildEntry.shape, false, true);
		}
	}
}


static void drawHotspot(
	HDC hdc,
	POINT theCenterPoint,
	int theExtents,
	const RECT& theWindowRect,
	COLORREF theDotColor,
	bool isActiveHotspot)
{
	theCenterPoint.x -= theWindowRect.left;
	theCenterPoint.y -= theWindowRect.top;
	if( isActiveHotspot )
	{
		Rectangle(hdc,
			theCenterPoint.x - theExtents * 3,
			theCenterPoint.y - 1,
			theCenterPoint.x + theExtents * 3 + 1,
			theCenterPoint.y + 2);		
		Rectangle(hdc,
			theCenterPoint.x - 1,
			theCenterPoint.y - theExtents * 3,
			theCenterPoint.x + 2,
			theCenterPoint.y + theExtents * 3 + 1);
	}
	Rectangle(hdc,
		theCenterPoint.x - theExtents,
		theCenterPoint.y - theExtents,
		theCenterPoint.x + theExtents + 1,
		theCenterPoint.y + theExtents + 1);
	if( isActiveHotspot )
	{
		RECT aCenterDot;
		const int aDotExtents = 1;
		aCenterDot.left = theCenterPoint.x - aDotExtents;
		aCenterDot.top = theCenterPoint.y - aDotExtents;
		aCenterDot.right = theCenterPoint.x + aDotExtents + 1;
		aCenterDot.bottom = theCenterPoint.y + aDotExtents + 1;
		COLORREF oldColor = SetDCBrushColor(hdc, theDotColor);
		HBRUSH hBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
		FillRect(hdc, &aCenterDot, hBrush);
		SetDCBrushColor(hdc, oldColor);
	}
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
	{
		drawHotspot(hdc,
			WindowManager::hotspotToOverlayPos(aHotspot),
			kChildHotspotDrawSize,
			theWindowRect, theEraseColor, false);
	}
	for(size_t i = 0; i < theEntry.children.size(); ++i )
	{
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		drawEntry(aChildEntry, hdc, theWindowRect, RGB(255, 255, 255), false);
	}
	for(size_t i = 2; i <= theEntry.rangeCount; ++i)
	{
		aHotspot.x.offset += theEntry.drawOffX;
		aHotspot.y.offset += theEntry.drawOffY;
		drawHotspot(hdc,
			WindowManager::hotspotToOverlayPos(aHotspot),
			kChildHotspotDrawSize,
			theWindowRect, theEraseColor, false);
	}
	if( isActiveHotspot )
	{// Draw last so is over top of all children
		COLORREF oldColor = SetDCBrushColor(hdc, RGB(255, 255, 255));
		drawHotspot(hdc,
			WindowManager::hotspotToOverlayPos(theEntry.drawHotspot),
			kActiveHotspotDrawSize,
			theWindowRect, theEraseColor, true);
		SetDCBrushColor(hdc, oldColor);
	}
}


static void eraseRect(
	HDC hdc,
	POINT theCenterPoint,
	int theExtents,
	const RECT& theWindowRect)
{
	theCenterPoint.x -= theWindowRect.left;
	theCenterPoint.y -= theWindowRect.top;
	RECT aRect;
	aRect.left = theCenterPoint.x - theExtents;
	aRect.top = theCenterPoint.y - theExtents;
	aRect.right = theCenterPoint.x + theExtents + 1;
	aRect.bottom = theCenterPoint.y + theExtents + 1;
	HBRUSH hEraseBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
	FillRect(hdc, &aRect, hEraseBrush);
}


static void eraseDrawnEntry(
	LayoutEntry& theEntry,
	HDC hdc,
	const RECT& theWindowRect,
	bool isActiveHotspot = true)
{
	Hotspot aHotspot = theEntry.drawHotspot;
	eraseRect(hdc,
		WindowManager::hotspotToOverlayPos(aHotspot),
		isActiveHotspot ? kActiveHotspotDrawSize*3 : kChildHotspotDrawSize,
		theWindowRect);
	for(size_t i = 2; i <= theEntry.rangeCount; ++i)
	{
		aHotspot.x.offset += theEntry.drawOffX;
		aHotspot.y.offset += theEntry.drawOffY;
		eraseRect(hdc,
			WindowManager::hotspotToOverlayPos(aHotspot),
			kChildHotspotDrawSize,
			theWindowRect);
	}
	for(size_t i = 0; i < theEntry.children.size(); ++i )
	{
		LayoutEntry& aChildEntry = sState->entries[theEntry.children[i]];
		eraseDrawnEntry(
			aChildEntry, hdc, theWindowRect, false);
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
			eraseDrawnEntry(anEntry, hdc, theWindowRect);
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
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	if( sState->activeEntry )
	{
		LayoutEntry& anEntry = sState->entries[sState->activeEntry];
		sState->entered = sState->applied = anEntry.shape;
		sState->needsDrawPosUpdate = true;
		//WindowManager::setSystemOverlayCallbacks(
		//	layoutEditorWindowProc, layoutEditorPaintFunc);
		if( entryIncludesPosition(anEntry) )
			HUD::setSystemOverlayDrawHook(layoutEditorPaintFunc);
		WindowManager::createToolbarWindow(
			entryIncludesAlignment(anEntry)
				? entryIncludesPosition(anEntry)
					? IDD_DIALOG_LAYOUT_XYWHA_TOOLBAR
					: IDD_DIALOG_LAYOUT_WHA_TOOLBAR
				: entryIncludesSize(anEntry)
					? IDD_DIALOG_LAYOUT_XYWH_TOOLBAR
					: IDD_DIALOG_LAYOUT_XY_TOOLBAR,
			editLayoutToolbarProc,
			anEntry.type == LayoutEntry::eType_HUDElement
				? anEntry.hudElementID : -1);
	}
	else
	{
		cleanup();
	}
}


static void setEntryParent(
	LayoutEntry& theEntry,
	const StringToValueMap<u32>& theEntryNameMap)
{
	if( theEntry.rangeCount == 0 )
		return;
	std::string anArrayName = condense(theEntry.item.name);
	const int anArrayEndIdx = breakOffIntegerSuffix(anArrayName);
	DBG_ASSERT(anArrayEndIdx >= 0);
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
	StringToValueMap<u32>::IndexVector anIndexSet;
	theEntryNameMap.findAllWithPrefix(anArrayName, &anIndexSet);
	for(size_t i = 0; i < anIndexSet.size(); ++i)
	{
		std::string aHotspotName =
			theEntryNameMap.keys()[anIndexSet[i]];
		const int aHotspotEndIdx =
			breakOffIntegerSuffix(aHotspotName);
		if( aHotspotEndIdx == anArrayPrevIdx )
		{
			theEntry.item.parentIndex =
				theEntryNameMap.values()[anIndexSet[i]];
			return;
		}
	}
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
	const size_t aFirstNewEntryIdx = theEntryList.size();
	LayoutEntry aNewEntry;
	aNewEntry.type = theEntryType;
	aNewEntry.item.parentIndex = theCategoryType;

	Profile::KeyValuePairs aPropertySet;
	Profile::getAllKeys(
		theEntryList[theCategoryType].posSect + "/",
		aPropertySet);
	StringToValueMap<u32> anEntryNameToIdxMap;
	for(size_t i = 0; i < aPropertySet.size(); ++i)
	{
		aNewEntry.rangeCount = 0;
		aNewEntry.item.name = aPropertySet[i].first;
		aNewEntry.propName = aNewEntry.item.name;
		std::string aKeyName = condense(aNewEntry.item.name);
		anEntryNameToIdxMap.setValue(
			aKeyName, u32(theEntryList.size()));
		std::string aDesc = aPropertySet[i].second;
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
		theEntryList.push_back(aNewEntry);
	}
	aPropertySet.clear();

	// Link items in arrays with their direct parents
	for(size_t i = aFirstNewEntryIdx; i < theEntryList.size(); ++i)
		setEntryParent(theEntryList[i], anEntryNameToIdxMap);
}


static void tryFetchHUDHotspot(
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
			theEntry.shape.w = "4";
			theEntry.shape.h = "4";
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
		std::string aValStr = Profile::getStr(theReadSect + "/" + thePropName);
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

	// Gather information on elements that can be edited
	layoutDebugPrint("Initializing\n");
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
		aCatEntry.type = LayoutEntry::eType_HUDCategory;
		aCatEntry.item.name = "HUD Elements";
		aCatEntry.posSect = kHUDPrefix;
		sState->entries.push_back(aCatEntry);
		DBG_ASSERT(sState->entries.size() == LayoutEntry::eType_CategoryNum);
		for(size_t i = 0; i < sState->entries.size(); ++i)
			DBG_ASSERT(sState->entries[i].type == i);
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

	// Collect HUD Element / Menu position keys from InputMap
	for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
	{
		LayoutEntry aNewEntry;
		aNewEntry.item.isRootCategory = false;
		aNewEntry.hudElementID = i;
		aNewEntry.item.name = InputMap::hudElementKeyName(i);
		const bool isAMenu = InputMap::hudElementIsAMenu(i);
		aNewEntry.type = LayoutEntry::eType_HUDElement;
		aNewEntry.item.parentIndex = isAMenu
			? LayoutEntry::eType_MenuCategory
			: LayoutEntry::eType_HUDCategory;
		if( aNewEntry.item.name.empty() )
			continue;

		if( entryIncludesPosition(aNewEntry ) )
		{
			// Try read/write [Menu.Name]/Position
			tryFetchHUDHotspot(aNewEntry, kPositionKey,
				kMenuPrefix + aNewEntry.item.name,
				kMenuPrefix + aNewEntry.item.name);
			// Try read/write [HUD.Name]/Position
			tryFetchHUDHotspot(aNewEntry, kPositionKey,
				kHUDPrefix + aNewEntry.item.name,
				kHUDPrefix + aNewEntry.item.name);
			// Just write to [HUD/Menu.Name]/Position w/ default settings
			tryFetchHUDHotspot(aNewEntry, kPositionKey, "",
				(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		}

		if( isAMenu )
		{
			// Try read/write [Menu.Name]/ItemSize
			tryFetchHUDHotspot(aNewEntry, kItemSizeKey,
				kMenuPrefix + aNewEntry.item.name,
				kMenuPrefix + aNewEntry.item.name);
			// Try read/write [HUD.Name]/ItemSize
			tryFetchHUDHotspot(aNewEntry, kItemSizeKey,
				kHUDPrefix + aNewEntry.item.name,
				kHUDPrefix + aNewEntry.item.name);
			// Try read/write [Menu.Name]/Size
			tryFetchHUDHotspot(aNewEntry, kSizeKey,
				kMenuPrefix + aNewEntry.item.name,
				kMenuPrefix + aNewEntry.item.name);
			// Try read/write [HUD.Name]/Size
			tryFetchHUDHotspot(aNewEntry, kSizeKey,
				kHUDPrefix + aNewEntry.item.name,
				kHUDPrefix + aNewEntry.item.name);
		}
		else
		{
			// Try read/write [HUD.Name]/Size
			tryFetchHUDHotspot(aNewEntry, kSizeKey,
				kHUDPrefix + aNewEntry.item.name,
				kHUDPrefix + aNewEntry.item.name);
			// Try read/write [Menu.Name]/Size
			tryFetchHUDHotspot(aNewEntry, kSizeKey,
				kMenuPrefix + aNewEntry.item.name,
				kMenuPrefix + aNewEntry.item.name);
			// Try read/write [HUD.Name]/ItemSize
			tryFetchHUDHotspot(aNewEntry, kItemSizeKey,
				kHUDPrefix + aNewEntry.item.name,
				kHUDPrefix + aNewEntry.item.name);
			// Try read/write [Menu.Name]/ItemSize
			tryFetchHUDHotspot(aNewEntry, kItemSizeKey,
				kMenuPrefix + aNewEntry.item.name,
				kMenuPrefix + aNewEntry.item.name);
		}
		// Try read [HUD]/ItemSize and write to [HUD/Menu.Name]/ItemSize
		tryFetchHUDHotspot(aNewEntry, kItemSizeKey, kDefHUDPrefx,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Try read [Menu]/ItemSize and write to [HUD/Menu.Name]/ItemSize
		tryFetchHUDHotspot(aNewEntry, kItemSizeKey, kDefMenuPrefix,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Just write defaults to [Menu.Name]/ItemSize or [HUD.Name]/Size
		tryFetchHUDHotspot(aNewEntry, (isAMenu ? kItemSizeKey : kSizeKey), "",
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);

		// Try read/write [Menu.Name]/Alignment
		tryFetchHUDHotspot(aNewEntry, kAlignmentKey,
			kMenuPrefix + aNewEntry.item.name,
			kMenuPrefix + aNewEntry.item.name);
		// Try read/write [HUD.Name]/Alignment
		tryFetchHUDHotspot(aNewEntry, kAlignmentKey,
			kHUDPrefix + aNewEntry.item.name,
			kHUDPrefix + aNewEntry.item.name);
		// Try read [HUD]/Alignment and write to [HUD/Menu.Name]/Alignment
		tryFetchHUDHotspot(aNewEntry, kAlignmentKey, kDefHUDPrefx,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Try read [Menu]/Alignment and write to [HUD/Menu.Name]/Alignment
		tryFetchHUDHotspot(aNewEntry, kAlignmentKey, kDefMenuPrefix,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Just write to [HUD/Menu.Name]/Alignment w/ default settings
		tryFetchHUDHotspot(aNewEntry, kAlignmentKey, "",
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);

		sState->entries.push_back(aNewEntry);
	}

	// Prepare for prompt dialog
	if( sState->entries.size() <= LayoutEntry::eType_CategoryNum )
	{
		logError("Current profile has no positional items to edit!");
		return;
	}

	// Link parent entries to their children for drawing later
	for(size_t i = LayoutEntry::eType_CategoryNum; i < sState->entries.size(); ++i)
	{
		if( sState->entries[i].item.parentIndex >= LayoutEntry::eType_CategoryNum )
			sState->entries[sState->entries[i].item.parentIndex].children.push_back(i);
	}

	sState->dialogItems.reserve(sState->entries.size());
	for(size_t i = 0; i < sState->entries.size(); ++i)
		sState->dialogItems.push_back(&sState->entries[i].item);
	
	promptForEditEntry();
}


void cleanup()
{
	if( !sState )
		return;

	layoutDebugPrint("Shutting down and clearing memory\n");
	if( sState->activeEntry )
		cancelRepositioning();
	delete sState;
	sState = null;
}

#undef LAYOUT_EDITOR_DEBUG_PRINT
#undef layoutDebugPrint

} // LayoutEditor
