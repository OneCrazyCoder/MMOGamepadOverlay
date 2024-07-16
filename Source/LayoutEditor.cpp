//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "LayoutEditor.h"

#include "Dialogs.h"
#include "HotspotMap.h"
#include "Profile.h"

namespace LayoutEditor
{

// Uncomment this to print status of target app/window tracking to debug window
#define LAYOUT_EDITOR_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------


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

		eType_HotspotSingle,
		eType_HotspotArray,
		eType_HotspotRange,
		eType_HotspotArrayElement,
		eType_CopyIcon,
		eType_CopyIconRange,
		eType_Menu,
		eType_HUD,

		eType_Num,
		eType_FirstMoveable = eType_HotspotSingle
	} type;
	Hotspot value;
	Dialogs::TreeViewDialogItem item;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef LAYOUT_EDITOR_DEBUG_PRINT
#define layoutDebugPrint(...) debugPrint("LayoutEditor: " __VA_ARGS__)
#else
#define layoutDebugPrint(...) ((void)0)
#endif


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void launch()
{
	logError("Layout editor functionality is not yet implemented!");
	return;

	// Gather information on elements that can be edited
	layoutDebugPrint("Initializing layout editor\n");
	std::vector<LayoutEntry> aLayoutEntriesList;
	LayoutEntry anEntry;
	anEntry.type = LayoutEntry::eType_Root;
	anEntry.item.parentIndex = 0;
	anEntry.item.allowedAsResult = false;
	aLayoutEntriesList.push_back(anEntry);

	anEntry.type = LayoutEntry::eType_HotspotCategory;
	anEntry.item.name = "Hotspots";
	aLayoutEntriesList.push_back(anEntry);

	// Gather all the elements that can be customized from current Profile
	Profile::KeyValuePairs aPropertySet;
	Profile::getAllKeys("Hotspots/", aPropertySet);
	for(size_t i = 0; i < aPropertySet.size(); ++i)
	{
		anEntry.type = LayoutEntry::eType_HotspotSingle;
		anEntry.item.name = aPropertySet[i].first;
		anEntry.item.parentIndex = 1;
		anEntry.item.allowedAsResult =
			anEntry.type >= LayoutEntry::eType_FirstMoveable;
		std::string aHotspotDesc = aPropertySet[i].second;
		HotspotMap::stringToHotspot(aHotspotDesc, anEntry.value);
		aLayoutEntriesList.push_back(anEntry);
	}
	if( aLayoutEntriesList.empty() )
	{
		logError("Current profile has no positional items to edit!");
		return;
	}

	std::vector<Dialogs::TreeViewDialogItem*> aDialogList;
	aDialogList.reserve(aLayoutEntriesList.size());
	for(size_t i = 0; i < aLayoutEntriesList.size(); ++i)
		aDialogList.push_back(&aLayoutEntriesList[i].item);
	
	size_t anEntryToEdit = Dialogs::layoutItemSelect(aDialogList);
	layoutDebugPrint("Returned selection value = %d (%s)\n",
		anEntryToEdit,
		aLayoutEntriesList[anEntryToEdit].item.name.c_str());
}

#undef LAYOUT_EDITOR_DEBUG_PRINT
#undef layoutDebugPrint

} // LayoutEditor
