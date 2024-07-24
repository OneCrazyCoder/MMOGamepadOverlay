//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "LayoutEditor.h"

#include "Dialogs.h"
#include "HotspotMap.h"
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

const char* kHotspotsPrefix = "Hotspots";
const char* kIconsPrefix = "Icons";
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
const char* kDefHUDPrefx = "HUD";
const char* kDefMenuPrefix = "Menu";
const char* kPositionKey = "Position";
const char* kSizeKey = "Size";
const char* kItemSizeKey = "ItemSize";


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
		eType_Menu,
		eType_HUD,

		eType_Num,
		eType_CategoryNum = eType_Hotspot
	} type;
	Dialogs::TreeViewDialogItem item;

	struct Shape
	{
		std::string x, y, w, h;
		bool operator==(const Shape& rhs) const
		{ return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h; }
		bool operator!=(const Shape& rhs) const
		{ return !(*this == rhs); }
		
	} shape;

	std::string posSect, posProp, sizeSect, sizeProp;
	u16 rangeCount;
};


struct EditorState
{
	std::vector<LayoutEntry> entries;
	std::vector<Dialogs::TreeViewDialogItem*> dialogItems;
	StringToValueMap<u16> hotspotNameMapCache;
	StringToValueMap<u16> hotspotArrayNameMapCache;
	size_t activeEntry;
	LayoutEntry::Shape undo, entered, applied;

	EditorState() : activeEntry() {}
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

bool entryIncludesSize(const LayoutEntry& theEntry)
{
	switch(theEntry.type)
	{
	case LayoutEntry::eType_Hotspot:
		return false;
	case LayoutEntry::eType_CopyIcon:
		return theEntry.item.parentIndex < LayoutEntry::eType_CategoryNum;
	case LayoutEntry::eType_Menu:
	case LayoutEntry::eType_HUD:
		return !theEntry.sizeProp.empty();
	}

	return false;
}


bool entryIsAnOffset(const LayoutEntry& theEntry)
{
	return theEntry.item.parentIndex >= LayoutEntry::eType_CategoryNum;
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


static void layoutEditorPaintFunc(
	HDC hdc, const RECT& theDrawRect, bool firstDraw)
{
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
	const bool needCheckSize = entryIncludesSize(anEntry);
	const bool needNewSize = needCheckSize &&
		(sState->entered.w != sState->applied.w ||
		 sState->entered.h != sState->applied.h);
	if( !needNewPos && !needNewSize )
		return;

	const bool needNewCombo = (needNewPos || needNewSize) &&
		needCheckSize && anEntry.type == LayoutEntry::eType_CopyIcon;
	if( needNewCombo )
	{
		layoutDebugPrint("Applying altered position & size to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.posSect, anEntry.posProp,
			sState->entered.x + ", " + sState->entered.y + ", " +
			sState->entered.w + ", " + sState->entered.h, false);
	}
	else if( needNewPos && needNewSize )
	{
		layoutDebugPrint("Applying altered position & size to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.posSect, anEntry.posProp,
			sState->entered.x + ", " + sState->entered.y, false);
		Profile::setStr(anEntry.sizeSect, anEntry.sizeProp,
			sState->entered.w + ", " + sState->entered.h, false);
	}
	else if( needNewPos )
	{
		layoutDebugPrint("Applying altered position to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.posSect, anEntry.posProp,
			sState->entered.x + ", " + sState->entered.y, false);
	}
	else if( needNewSize )
	{
		layoutDebugPrint("Applying altered size to '%s'\n",
			anEntry.item.name.c_str());
		Profile::setStr(anEntry.sizeSect, anEntry.sizeProp,
			sState->entered.w + ", " + sState->entered.h, false);
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
	}
}


static void cancelRepositioning()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( !gShutdown && sState->applied != sState->undo )
	{
		layoutDebugPrint("Restoring previous position/size of '%s'\n",
			anEntry.item.name.c_str());
		sState->entered = sState->undo;
		applyNewPosition();
	}
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
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
	Hotspot anEntryHSPos, anEntryHSSize;
	std::string aStr;
	aStr = theEntry.shape.x; HotspotMap::stringToCoord(aStr, anEntryHSPos.x);
	aStr = theEntry.shape.y; HotspotMap::stringToCoord(aStr, anEntryHSPos.y);
	aStr = theEntry.shape.w; HotspotMap::stringToCoord(aStr, anEntryHSSize.x);
	aStr = theEntry.shape.h; HotspotMap::stringToCoord(aStr, anEntryHSSize.x);
	POINT anEntryPos = WindowManager::hotspotToOverlayPos(anEntryHSPos);
	POINT anEntrySize = WindowManager::hotspotToOverlayPos(anEntryHSSize);
	anEntryPos.x = anEntryPos.x + anEntrySize.x / 2;
	anEntryPos.y = anEntryPos.y + anEntrySize.y / 2;
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
		case IDC_EDIT_X: aValidatedStr = "L"; break;
		case IDC_EDIT_Y: aValidatedStr = "T"; break;
		case IDC_EDIT_W: aValidatedStr = "4"; break;
		case IDC_EDIT_H: aValidatedStr = "4"; break;
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
		// Fill in initial values in each of the edit fields
		SetWindowText(
			GetDlgItem(theDialog, IDC_EDIT_X),
			widen(anEntry.shape.x).c_str());
		SetWindowText(
			GetDlgItem(theDialog, IDC_EDIT_Y),
			widen(anEntry.shape.y).c_str());
		if( entryIncludesSize(anEntry) )
		{
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_W),
				widen(anEntry.shape.w).c_str());
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_H),
				widen(anEntry.shape.h).c_str());
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
		}
		break;
	case WM_DESTROY:
		UnregisterHotKey(NULL, kCancelToolbarHotkeyID);
		break;
	}

	return (INT_PTR)FALSE;
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
		sState->undo = sState->entered = sState->applied = anEntry.shape;
		WindowManager::setSystemOverlayCallbacks(
			layoutEditorWindowProc, layoutEditorPaintFunc);
		WindowManager::createToolbarWindow(
			entryIncludesSize(sState->entries[sState->activeEntry])
				? IDD_DIALOG_LAYOUT_XYWH_TOOLBAR
				: IDD_DIALOG_LAYOUT_XY_TOOLBAR,
			editLayoutToolbarProc);
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
	{// Search for an anchor hotspot to act as parent
		const u32* aParentIdx = theEntryNameMap.find(anArrayName);
		if( aParentIdx )
			theEntry.item.parentIndex = *aParentIdx;
		return;
	}

	// Search for previous hotspot in the hotspot array
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
	aNewEntry.item.isRootCategory = false;

	Profile::KeyValuePairs aPropertySet;
	Profile::getAllKeys(
		theEntryList[theCategoryType].posSect + "/",
		aPropertySet);
	StringToValueMap<u32> anEntryNameToIdxMap;
	for(size_t i = 0; i < aPropertySet.size(); ++i)
	{
		aNewEntry.rangeCount = 0;
		aNewEntry.item.name = aPropertySet[i].first;
		aNewEntry.posSect = theEntryList[theCategoryType].posSect;
		aNewEntry.posProp = aNewEntry.item.name;
		std::string aKeyName = condense(aNewEntry.item.name);
		anEntryNameToIdxMap.setValue(
			aKeyName, u32(theEntryList.size()));
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
		std::string aHotspotDesc = aPropertySet[i].second;
		Hotspot::Coord tmp;
		HotspotMap::stringToCoord(aHotspotDesc, tmp, &aNewEntry.shape.x);
		HotspotMap::stringToCoord(aHotspotDesc, tmp, &aNewEntry.shape.y);
		HotspotMap::stringToCoord(aHotspotDesc, tmp, &aNewEntry.shape.w);
		HotspotMap::stringToCoord(aHotspotDesc, tmp, &aNewEntry.shape.h);
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
	std::string* aDestSectStr =
		isPosition ? &theEntry.posSect : &theEntry.sizeSect;
	if( !aDestSectStr->empty() )
		return;
	std::string* aDestProptr =
		isPosition ? &theEntry.posProp : &theEntry.sizeProp;
	if( theReadSect.empty() )
	{
		*aDestSectStr = theWriteSect;
		*aDestProptr = thePropName;
		if( isPosition )
		{
			theEntry.shape.x = "L";
			theEntry.shape.y = "T";
		}
		else
		{
			theEntry.shape.w = "4";
			theEntry.shape.h = "4";
		}
	}
	else
	{
		std::string aValStr = Profile::getStr(theReadSect + "/" + thePropName);
		if( !aValStr.empty() )
		{
			*aDestSectStr = theWriteSect;
			*aDestProptr = thePropName;
			Hotspot::Coord tmp;
			if( isPosition )
			{
				HotspotMap::stringToCoord(aValStr, tmp, &theEntry.shape.x);
				HotspotMap::stringToCoord(aValStr, tmp, &theEntry.shape.y);
			}
			else
			{
				HotspotMap::stringToCoord(aValStr, tmp, &theEntry.shape.w);
				HotspotMap::stringToCoord(aValStr, tmp, &theEntry.shape.h);
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
		aCatEntry.item.parentIndex = 0;
		aCatEntry.item.isRootCategory = true;
		aCatEntry.rangeCount = 0;
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
		aNewEntry.rangeCount = 0;
		aNewEntry.item.name = InputMap::hudElementKeyName(i);
		const bool isAMenu = InputMap::hudElementIsAMenu(i);
		aNewEntry.type = isAMenu
			? LayoutEntry::eType_Menu
			: LayoutEntry::eType_HUD;
		aNewEntry.item.parentIndex = isAMenu
			? LayoutEntry::eType_MenuCategory
			: LayoutEntry::eType_HUDCategory;
		if( aNewEntry.item.name.empty() )
			continue;

		// Try read/write [Menu.Name]/Position
		tryFetchHUDHotspot(aNewEntry, kPositionKey,
			kMenuPrefix + aNewEntry.item.name,
			kMenuPrefix + aNewEntry.item.name);
		// Try read/write [HUD.Name]/Position
		tryFetchHUDHotspot(aNewEntry, kPositionKey,
			kHUDPrefix + aNewEntry.item.name,
			kHUDPrefix + aNewEntry.item.name);
		// Try read [HUD]/Position and write to [HUD/Menu.Name]/Position
		tryFetchHUDHotspot(aNewEntry, kPositionKey, kDefHUDPrefx,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Try read [Menu]/Position and write to [HUD/Menu.Name]/Position
		tryFetchHUDHotspot(aNewEntry, kPositionKey, kDefMenuPrefix,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Just write to [HUD/Menu.Name]/Position w/ default settings
		tryFetchHUDHotspot(aNewEntry, kPositionKey, "",
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);

		// Try read/write [Menu.Name]/Size
		tryFetchHUDHotspot(aNewEntry, kSizeKey,
			kMenuPrefix + aNewEntry.item.name,
			kMenuPrefix + aNewEntry.item.name);
		// Try read/write [Menu.Name]/ItemSize
		tryFetchHUDHotspot(aNewEntry, kItemSizeKey,
			kMenuPrefix + aNewEntry.item.name,
			kMenuPrefix + aNewEntry.item.name);
		// Try read/write [HUD.Name]/Size
		tryFetchHUDHotspot(aNewEntry, kSizeKey,
			kHUDPrefix + aNewEntry.item.name,
			kHUDPrefix + aNewEntry.item.name);
		// Try read/write [HUD.Name]/ItemSize
		tryFetchHUDHotspot(aNewEntry, kItemSizeKey,
			kHUDPrefix + aNewEntry.item.name,
			kHUDPrefix + aNewEntry.item.name);
		// Try read [HUD]/ItemSize and write to [HUD/Menu.Name]/ItemSize
		tryFetchHUDHotspot(aNewEntry, kItemSizeKey, kDefHUDPrefx,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Try read [HUD]/Size and write to [HUD/Menu.Name]/Size
		tryFetchHUDHotspot(aNewEntry, kSizeKey, kDefHUDPrefx,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Try read [Menu]/ItemSize and write to [HUD/Menu.Name]/ItemSize
		tryFetchHUDHotspot(aNewEntry, kItemSizeKey, kDefMenuPrefix,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Try read [Menu]/Size and write to [HUD/Menu.Name]/Size
		tryFetchHUDHotspot(aNewEntry, kSizeKey, kDefMenuPrefix,
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);
		// Just write defaults to [Menu.Name]/ItemSize or [HUD.Name]/Size
		tryFetchHUDHotspot(aNewEntry, (isAMenu ? kItemSizeKey : kSizeKey), "",
			(isAMenu ? kMenuPrefix : kHUDPrefix) + aNewEntry.item.name);

		sState->entries.push_back(aNewEntry);
	}

	// Prepare for prompt dialog
	if( sState->entries.empty() )
	{
		logError("Current profile has no positional items to edit!");
		return;
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
