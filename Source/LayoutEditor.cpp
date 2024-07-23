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

const char* kHotspotsPrefix = "Hotspots/";
const char* kIconsPrefix = "Icons/";
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
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
	std::string keyName;
	std::string sizeKeyName;
	Hotspot position;
	Hotspot size;
	Dialogs::TreeViewDialogItem item;
	u16 rangeCount;
};


struct EditorState
{
	std::vector<LayoutEntry> entries;
	std::vector<Dialogs::TreeViewDialogItem*> dialogItems;
	size_t activeEntry;
	Hotspot undoPos, undoSize, newPos, newSize, appliedPos, appliedSize;

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
		return !theEntry.sizeKeyName.empty();
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
	if( sState->appliedPos != sState->newPos ||
		sState->appliedSize != sState->newSize )
	{
		layoutDebugPrint("Applying altered position/size to '%s'\n",
			anEntry.item.name.c_str());
		sState->appliedPos = sState->newPos;
		sState->appliedSize = sState->newSize;
		// TODO - save to Profile cache
	}
}


static void cancelRepositioning()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	LayoutEntry& anEntry = sState->entries[sState->activeEntry];
	if( !gShutdown &&
		(sState->appliedPos != sState->undoPos ||
		 sState->appliedSize != sState->undoSize) )
	{
		layoutDebugPrint("Restoring previous position/size of '%s'\n",
			anEntry.item.name.c_str());
		sState->newPos = sState->undoPos;
		sState->newSize = sState->undoSize;
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
	if( anEntry.position != sState->appliedPos ||
		anEntry.size != sState->appliedSize )
	{
		anEntry.position = sState->appliedPos;
		anEntry.size = sState->appliedSize;
		// TODO - save to Profile .ini file
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
	POINT anEntryPos = WindowManager::hotspotToOverlayPos(theEntry.position);
	POINT anEntrySize = WindowManager::hotspotToOverlayPos(theEntry.size);
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

	Hotspot::Coord aNewCoord;
	std::string aCoordStr = trim(aControlStr);
	if( !aCoordStr.empty() )
	{
		std::string aCheckedStr = aCoordStr;
		EResult aResult = HotspotMap::stringToCoord(aCheckedStr, aNewCoord);
		while(!aCoordStr.empty() &&
			  (aResult != eResult_Ok || !aCheckedStr.empty()))
		{// Try just trimming extra characters off the end to salvage it
			if( aResult == eResult_Ok )
				aCoordStr.resize(aCoordStr.size() - aCheckedStr.size());
			else
				aCoordStr.resize(aCoordStr.size() - 1);
			aCheckedStr = aCoordStr;
			aResult = HotspotMap::stringToCoord(aCheckedStr, aNewCoord);
		}
	}

	if( aCoordStr.empty() )
	{
		switch(theEditControlID)
		{
		case IDC_EDIT_X: aCoordStr = "L"; break;
		case IDC_EDIT_Y: aCoordStr = "T"; break;
		case IDC_EDIT_W: aCoordStr = "4"; break;
		case IDC_EDIT_H: aCoordStr = "4"; break;
		}
		std::string aCheckedStr = aCoordStr;
		HotspotMap::stringToCoord(aCheckedStr, aNewCoord);
	}
	DBG_ASSERT(!aCoordStr.empty());

	if( aCoordStr[aCoordStr.size()-1] != '%' &&
		!isdigit(aCoordStr[aCoordStr.size()-1]) )
	{// String may have extra characters at end - fix by re-converting it
		LayoutEntry& anEntry = sState->entries[sState->activeEntry];
		aCoordStr = HotspotMap::coordToString(
			aNewCoord, formatForCoord(anEntry, theEditControlID));
	}

	if( aCoordStr != aControlStr )
		SetWindowText(hEdit, widen(aCoordStr).c_str());

	switch(theEditControlID)
	{
	case IDC_EDIT_X: sState->newPos.x = aNewCoord; break;
	case IDC_EDIT_Y: sState->newPos.y = aNewCoord; break;
	case IDC_EDIT_W: sState->newSize.x = aNewCoord; break;
	case IDC_EDIT_H: sState->newSize.y = aNewCoord; break;
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
			widen(HotspotMap::coordToString(
				anEntry.position.x,
				formatForCoord(anEntry, IDC_EDIT_X))).c_str());
		SetWindowText(
			GetDlgItem(theDialog, IDC_EDIT_Y),
			widen(HotspotMap::coordToString(
				anEntry.position.y,
				formatForCoord(anEntry, IDC_EDIT_Y))).c_str());
		if( entryIncludesSize(anEntry) )
		{
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_W),
				widen(HotspotMap::coordToString(
					anEntry.size.x,
					formatForCoord(anEntry, IDC_EDIT_W))).c_str());
			SetWindowText(
				GetDlgItem(theDialog, IDC_EDIT_H),
				widen(HotspotMap::coordToString(
					anEntry.size.y,
					formatForCoord(anEntry, IDC_EDIT_H))).c_str());
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
		sState->appliedPos = anEntry.position;
		sState->appliedSize = anEntry.size;
		sState->undoPos = sState->newPos = sState->appliedPos;
		sState->undoSize = sState->newSize = sState->appliedSize;
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
		theEntryList[theCategoryType].keyName,
		aPropertySet);
	StringToValueMap<u32> anEntryNameToIdxMap;
	for(size_t i = 0; i < aPropertySet.size(); ++i)
	{
		aNewEntry.rangeCount = 0;
		aNewEntry.item.name = aPropertySet[i].first;
		aNewEntry.keyName = theEntryList[theCategoryType].keyName;
		aNewEntry.keyName += aNewEntry.item.name;
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
		HotspotMap::stringToHotspot(aHotspotDesc, aNewEntry.position);
		HotspotMap::stringToHotspot(aHotspotDesc, aNewEntry.size);
		theEntryList.push_back(aNewEntry);
	}
	aPropertySet.clear();

	// Link items in arrays with their direct parents
	for(size_t i = aFirstNewEntryIdx; i < theEntryList.size(); ++i)
		setEntryParent(theEntryList[i], anEntryNameToIdxMap);
}


static std::pair<std::string, std::string> tryGetHUDHotspot(
	const std::string& thePrefix,
	const std::string& theElementName,
	const char* thePropKey)
{
	std::pair<std::string, std::string> result;
	result.first = thePrefix + theElementName + "/";
	result.first += thePropKey;
	result.second = Profile::getStr(result.first);
	return result;
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
	LayoutEntry aNewEntry;
	aNewEntry.type = LayoutEntry::eType_Root;
	aNewEntry.item.parentIndex = 0;
	aNewEntry.item.isRootCategory = true;
	aNewEntry.rangeCount = 0;
	sState->entries.push_back(aNewEntry);
	aNewEntry.type = LayoutEntry::eType_HotspotCategory;
	aNewEntry.item.name = "Hotspots";
	aNewEntry.keyName = kHotspotsPrefix;
	sState->entries.push_back(aNewEntry);
	aNewEntry.type = LayoutEntry::eType_CopyIconCategory;
	aNewEntry.item.name = "Copy Icons";
	aNewEntry.keyName = kIconsPrefix;
	sState->entries.push_back(aNewEntry);
	aNewEntry.type = LayoutEntry::eType_MenuCategory;
	aNewEntry.item.name = "Menus";
	aNewEntry.keyName = kMenuPrefix;
	sState->entries.push_back(aNewEntry);
	aNewEntry.type = LayoutEntry::eType_HUDCategory;
	aNewEntry.item.name = "HUD Elements";
	aNewEntry.keyName = kHUDPrefix;
	sState->entries.push_back(aNewEntry);
	DBG_ASSERT(sState->entries.size() == LayoutEntry::eType_CategoryNum);
	for(size_t i = 0; i < sState->entries.size(); ++i)
		DBG_ASSERT(sState->entries[i].type == i);
	aNewEntry.item.isRootCategory = false;

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
		aNewEntry.rangeCount = 0;
		aNewEntry.item.name = InputMap::hudElementKeyName(i);
		if( aNewEntry.item.name.empty() )
			continue;
		std::pair<std::string, std::string> aHotspotStr;
		for(LayoutEntry::EType aCatType = LayoutEntry::eType_MenuCategory,
			anEntryType = LayoutEntry::eType_Menu;
			aCatType < LayoutEntry::eType_CategoryNum;
			aCatType = LayoutEntry::EType(aCatType+1),
			anEntryType = LayoutEntry::EType(anEntryType+1))
		{
			const std::string& aPrefix = sState->entries[aCatType].keyName;
			aHotspotStr = tryGetHUDHotspot(
				aPrefix, aNewEntry.item.name, kPositionKey);
			if( !aHotspotStr.second.empty() )
			{
				aNewEntry.type = anEntryType;
				aNewEntry.keyName = aHotspotStr.first;
				aNewEntry.item.parentIndex = aCatType;
				aNewEntry.sizeKeyName.clear();
				HotspotMap::stringToHotspot(
					aHotspotStr.second, aNewEntry.position);
				aHotspotStr = tryGetHUDHotspot(
					aPrefix, aNewEntry.item.name, kSizeKey);
				if( !aHotspotStr.second.empty() )
				{
					aNewEntry.sizeKeyName = aHotspotStr.first;
					HotspotMap::stringToHotspot(
						aHotspotStr.second, aNewEntry.size);
				}
				else
				{
					aHotspotStr = tryGetHUDHotspot(
						aPrefix, aNewEntry.item.name, kItemSizeKey);
					if( !aHotspotStr.second.empty() )
					{
						aNewEntry.sizeKeyName = aHotspotStr.first;
						HotspotMap::stringToHotspot(
							aHotspotStr.second, aNewEntry.size);
					}
				}
				sState->entries.push_back(aNewEntry);
				break;
			}
		}
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
