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
#define LAYOUT_EDITOR_DEBUG_PRINT

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
static void cancelRepositioning();
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


static INT_PTR CALLBACK editLayoutToolbarProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_INITDIALOG:
		layoutDebugPrint("Initializing repositioning toolbar\n");
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Cancel button clicked
				cancelRepositioning();
				promptForEditEntry();
				return (INT_PTR)TRUE;
			}
			break;
		}
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
	layoutDebugPrint("Returned selection value = %d (%s)\n",
		sState->activeEntry,
		sState->entries[sState->activeEntry].item.name.c_str());
	DBG_ASSERT(sState->activeEntry < sState->entries.size());
	if( sState->activeEntry )
	{
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


static void cancelRepositioning()
{
	DBG_ASSERT(sState);
	DBG_ASSERT(sState->activeEntry != 0);
	layoutDebugPrint("Reposition cancelled\n");
	WindowManager::setSystemOverlayCallbacks(NULL, NULL);
	WindowManager::destroyToolbarWindow();
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
		if( sState->activeEntry )
		{
			cancelRepositioning();
			return;
		}
		cleanup();
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
