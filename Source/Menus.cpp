//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "Menus.h"

#include "Dialogs.h" // editMenuCommand()
#include "HotspotMap.h"
#include "InputMap.h"
#include "Profile.h"

namespace Menus
{

//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

std::vector<s16> sSelectedItem;
std::vector<u16> sActiveSubMenu;
int sOverrideActiveMenuBackup = -1;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static inline int getMenuOverlayID(int theRootMenuID)
{
	const int anOverlayID = InputMap::menuOverlayID(theRootMenuID);
	DBG_ASSERT(anOverlayID >= 0);
	DBG_ASSERT(anOverlayID < gVisibleOverlays.size());
	DBG_ASSERT(anOverlayID < gRefreshOverlays.size());
	DBG_ASSERT(anOverlayID < gFullRedrawOverlays.size());
	DBG_ASSERT(anOverlayID < gReshapeOverlays.size());
	DBG_ASSERT(anOverlayID < gActiveOverlays.size());
	DBG_ASSERT(anOverlayID < gDisabledOverlays.size());
	DBG_ASSERT(size_t(anOverlayID) < gConfirmedMenuItem.size());
	return anOverlayID;
}


static inline int activeSubMenu(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	DBG_ASSERT(size_t(anOverlayID) < sActiveSubMenu.size());
	return sActiveSubMenu[anOverlayID];
}


static inline int activeMenuItemCount(int theRootMenuID)
{
	return InputMap::menuItemCount(activeSubMenu(theRootMenuID));
}


static inline int selectedXPos(int theMenuID)
{
	switch(InputMap::menuStyle(theMenuID))
	{
	case eMenuStyle_Bar:
		return sSelectedItem[theMenuID];
	case eMenuStyle_Grid:
		return sSelectedItem[theMenuID] % InputMap::menuGridWidth(theMenuID);
	case eMenuStyle_Columns:
		return sSelectedItem[theMenuID] / InputMap::menuGridHeight(theMenuID);
		break;
	default:
		return 0;
	}
}


static inline int selectedYPos(int theMenuID)
{
	switch(InputMap::menuStyle(theMenuID))
	{
	case eMenuStyle_List:
	case eMenuStyle_Slots:
		return sSelectedItem[theMenuID];
	case eMenuStyle_Grid:
		return sSelectedItem[theMenuID] / InputMap::menuGridWidth(theMenuID);
	case eMenuStyle_Columns:
		return sSelectedItem[theMenuID] % InputMap::menuGridHeight(theMenuID);
		break;
	default:
		return 0;
	}
}


static inline int xyToSelectionIndex(int theMenuID, int theX, int theY)
{
	int aStrideLen;
	switch(InputMap::menuStyle(theMenuID))
	{
	case eMenuStyle_List:
	case eMenuStyle_Slots:
		aStrideLen = InputMap::menuItemCount(theMenuID);
		return clamp(theY, 0, aStrideLen-1);
	case eMenuStyle_Bar:
		aStrideLen = InputMap::menuItemCount(theMenuID);
		return clamp(theX, 0, aStrideLen-1);
	case eMenuStyle_Grid:
		aStrideLen = InputMap::menuGridWidth(theMenuID);
		theX = clamp(theX, 0, aStrideLen-1);
		theY = clamp(theY, 0, InputMap::menuGridHeight(theMenuID)-1);
		return theY * aStrideLen + theX;
	case eMenuStyle_Columns:
		aStrideLen = InputMap::menuGridHeight(theMenuID);
		theX = clamp(theX, 0, InputMap::menuGridWidth(theMenuID)-1);
		theY = clamp(theY, 0, aStrideLen-1);
		return theX * aStrideLen + theY;
	default:
		return 0;
	}
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(sActiveSubMenu.empty());
	const int kMenuStackCount = InputMap::menuOverlayCount();
	sActiveSubMenu.reserve(kMenuStackCount);
	sActiveSubMenu.resize(kMenuStackCount);
	for(int anOverlayID = 0; anOverlayID < kMenuStackCount; ++anOverlayID)
	{
		sActiveSubMenu[anOverlayID] = dropTo<u16>(
			InputMap::overlayRootMenuID(anOverlayID));
	}

	DBG_ASSERT(sSelectedItem.empty());
	const int kMenuCount = InputMap::menuCount();
	sSelectedItem.resize(kMenuCount);
	for(int aMenuID = 0; aMenuID < kMenuCount; ++aMenuID)
	{
		sSelectedItem[aMenuID] = dropTo<u16>(
			InputMap::menuDefaultItemIdx(aMenuID));
	}
}


void cleanup()
{
	sActiveSubMenu.clear();
	sSelectedItem.clear();
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.containsPrefix("Menu.") )
	{
		const int oldMenuCount = intSize(sSelectedItem.size());
		sSelectedItem.resize(InputMap::menuCount());
		for(int i = 0, end = intSize(sSelectedItem.size()); i < end; ++i)
		{
			// Select default selection for newly-added sub-menus
			if( i >= oldMenuCount )
				sSelectedItem[i] = dropTo<u16>(InputMap::menuDefaultItemIdx(i));
			// Clamp current selection to current menu item count
			sSelectedItem[i] = clamp(
				sSelectedItem[i], 0, InputMap::menuItemCount(i)-1);
		}
	}
}


Command selectedMenuItemCommand(int theRootMenuID)
{
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theSubMenuID = sActiveSubMenu[theOverlayID];
	const int theSelection = sSelectedItem[theSubMenuID];

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(theOverlayID);

	Command result;
	if( InputMap::menuStyle(theSubMenuID) == eMenuStyle_4Dir )
		return result;

	// Have selected menu item show a confirmation flash if
	// this command won't change sub-menus
	result = InputMap::commandForMenuItem(theSubMenuID, theSelection);
	switch(result.type)
	{
	case eCmdType_OpenSubMenu:
	case eCmdType_MenuReset:
	case eCmdType_MenuBack:
		break;
	default:
		gConfirmedMenuItem[theOverlayID] = dropTo<u16>(theSelection);
	}

	return result;
}


Command selectMenuItem(
	int theRootMenuID,
	ECommandDir theDir,
	bool wrap,
	bool repeat)
{
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theSubMenuID = sActiveSubMenu[theOverlayID];
	const int theItemCount = InputMap::menuItemCount(theSubMenuID);
	int aSelection = sSelectedItem[theSubMenuID];
	bool pushedPastEdge = false;

	Command aDirCmd = InputMap::commandForMenuDir(theSubMenuID, theDir);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(theOverlayID);

	const EMenuStyle aMenuStyle = InputMap::menuStyle(theSubMenuID);
	int aGridSize;
	switch(aMenuStyle)
	{
	case eMenuStyle_List:
		switch(theDir)
		{
		case eCmdDir_L:
		case eCmdDir_R:
			pushedPastEdge = true;
			break;
		case eCmdDir_U:
			pushedPastEdge = aSelection == 0;
			if( !pushedPastEdge )
				--aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = theItemCount - 1;
			break;
		case eCmdDir_D:
			pushedPastEdge = aSelection >= theItemCount - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = 0;
			break;
		}
		break;
	case eMenuStyle_Bar:
		switch(theDir)
		{
		case eCmdDir_L:
			pushedPastEdge = aSelection == 0;
			if( !pushedPastEdge )
				--aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = theItemCount - 1;
			break;
		case eCmdDir_R:
			pushedPastEdge = aSelection >= theItemCount - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = 0;
			break;
		case eCmdDir_U:
		case eCmdDir_D:
			pushedPastEdge = true;
			break;
		}
		break;
	case eMenuStyle_Grid:
		aGridSize = InputMap::menuGridWidth(theSubMenuID);
		switch(theDir)
		{
		case eCmdDir_L:
			pushedPastEdge = aSelection % aGridSize == 0;
			if( !pushedPastEdge )
				--aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = min(theItemCount - 1, aSelection + aGridSize - 1);
			break;
		case eCmdDir_R:
			pushedPastEdge =
				aSelection >= theItemCount -1 ||
				aSelection % aGridSize == aGridSize - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = (aSelection / aGridSize) * aGridSize;
			break;
		case eCmdDir_U:
			pushedPastEdge = aSelection < aGridSize;
			if( !pushedPastEdge )
				aSelection -= aGridSize;
			else if( wrap && theItemCount > 2 )
				aSelection += ((theItemCount-1) / aGridSize) * aGridSize;
			if( aSelection >= theItemCount )
				aSelection -= aGridSize;
			break;
		case eCmdDir_D:
			pushedPastEdge = aSelection + aGridSize >= theItemCount;
			if( !pushedPastEdge )
				aSelection += aGridSize;
			else if( wrap && theItemCount > 2 )
				aSelection = aSelection % aGridSize;
			else if( aDirCmd.type < eCmdType_FirstValid &&
					 aSelection < ((theItemCount-1) / aGridSize) * aGridSize )
				aSelection = theItemCount - 1;
			break;
		}
		break;
	case eMenuStyle_Columns:
		aGridSize = InputMap::menuGridHeight(theSubMenuID);
		switch(theDir)
		{
		case eCmdDir_L:
			pushedPastEdge = aSelection < aGridSize;
			if( !pushedPastEdge )
				aSelection -= aGridSize;
			else if( wrap && theItemCount > 2 )
				aSelection += ((theItemCount-1) / aGridSize) * aGridSize;
			if( aSelection >= theItemCount )
				aSelection -= aGridSize;
			break;
		case eCmdDir_R:
			pushedPastEdge = aSelection + aGridSize >= theItemCount;
			if( !pushedPastEdge )
				aSelection += aGridSize;
			else if( wrap && theItemCount > 2 )
				aSelection = aSelection % aGridSize;
			else if( aDirCmd.type < eCmdType_FirstValid &&
					 aSelection < ((theItemCount-1) / aGridSize) * aGridSize )
				aSelection = theItemCount - 1;
			break;
		case eCmdDir_U:
			pushedPastEdge = aSelection % aGridSize == 0;
			if( !pushedPastEdge )
				--aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = min(theItemCount - 1, aSelection + aGridSize - 1);
			break;
		case eCmdDir_D:
			pushedPastEdge =
				aSelection >= theItemCount -1 ||
				aSelection % aGridSize == aGridSize - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && theItemCount > 2 )
				aSelection = (aSelection / aGridSize) * aGridSize;
			break;
		}
		break;
	case eMenuStyle_Hotspots:
	case eMenuStyle_Highlight:
		{
			HotspotMap::HotspotLinkNode aLink =
				HotspotMap::getMenuHotspotsLink(theSubMenuID, aSelection);
			pushedPastEdge = aLink.edge[theDir];
			if( !pushedPastEdge || wrap )
				aSelection = aLink.next[theDir];
		}
		break;
	case eMenuStyle_Slots:
		switch(theDir)
		{
		case eCmdDir_L:
			pushedPastEdge = !repeat;
			break;
		case eCmdDir_R:
			pushedPastEdge = !repeat;
			break;
		case eCmdDir_U:
			aSelection = decWrap(aSelection, theItemCount);
			break;
		case eCmdDir_D:
			aSelection = incWrap(aSelection, theItemCount);
			break;
		}
		break;
	case eMenuStyle_4Dir:
		pushedPastEdge = !repeat;
		if( pushedPastEdge )
			gConfirmedMenuItem[theOverlayID] = dropTo<u16>(theDir);
		break;
	}

	aSelection = min(aSelection, theItemCount-1);
	if( !pushedPastEdge )
		aDirCmd = Command();

	if( aDirCmd.type == eCmdType_OpenSubMenu &&
		aDirCmd.menuItemID == 0 &&
		aMenuStyle != eMenuStyle_4Dir )
	{
		// Requests to open sub-menus with directionals in most menu styles
		// should keep previous selection in menu being left, and may change
		// the initial selection of the new menu via openSideMenu().
		aSelection = sSelectedItem[theSubMenuID];
		aDirCmd.type = eCmdType_OpenSideMenu;
		aDirCmd.sideMenuDir = theDir;
	}

	if( aSelection != sSelectedItem[theSubMenuID] )
	{
		sSelectedItem[theSubMenuID] = dropTo<s16>(aSelection);
		// Need to refresh to show selection differently
		gRefreshOverlays.set(theOverlayID);
	}

	return aDirCmd;
}


Command openSubMenu(int theRootMenuID, int theSubMenuID, int theMenuItem)
{
	DBG_ASSERT(theRootMenuID == InputMap::rootMenuOfMenu(theSubMenuID));
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theOldSubMenuID = sActiveSubMenu[theOverlayID];
	const int oldMenuItemCount = activeMenuItemCount(theRootMenuID);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(theOverlayID);

	if( theOldSubMenuID != theSubMenuID )
	{
		sActiveSubMenu[theOverlayID] = dropTo<u16>(theSubMenuID);

		// Need full redraw of new menu items
		gFullRedrawOverlays.set(theOverlayID);

		// Might need to reshape menu for new menu item count
		if( oldMenuItemCount != InputMap::menuItemCount(theSubMenuID) )
			gReshapeOverlays.set(theOverlayID);
	}

	// If theMenuItem <= 0, select default menu item, otherwise select
	// requested menu item - either way previously-selected item is ignored
	if( theMenuItem <= 0 )
		theMenuItem = InputMap::menuDefaultItemIdx(theSubMenuID);
	else
		--theMenuItem; // convert from 1-based to 0-based index
	theMenuItem = min(theMenuItem, InputMap::menuItemCount(theSubMenuID) - 1);
	theMenuItem = max(theMenuItem, 0);
	if( sSelectedItem[theSubMenuID] != theMenuItem )
	{
		sSelectedItem[theSubMenuID] = dropTo<u16>(theMenuItem);
		// Refresh to show selection changed, just in case went to same menu ID
		gRefreshOverlays.set(theOverlayID);
	}

	return InputMap::menuAutoCommand(theSubMenuID);
}


Command openSideMenu(int theRootMenuID, int theSideMenuID, ECommandDir theDir)
{
	// Remember that openSubMenu takes 1-based index, or 0 == use default,
	// so remaining code needs to add 1 to whatever selection is desired
	int aNextSel = 0;
	const EMenuStyle aNewMenuStyle = InputMap::menuStyle(theSideMenuID);
	const int aNewItemCount = InputMap::menuItemCount(theSideMenuID);
	const int anOldMenuID = activeSubMenu(theRootMenuID);
	switch(aNewMenuStyle)
	{
	case eMenuStyle_Slots:
	case eMenuStyle_Hotspots:
	case eMenuStyle_Highlight:
		// Retain whatever selection the side menu had previously
		// rather than resetting to the menu's default
		aNextSel = sSelectedItem[theSideMenuID] + 1;
		break;
	case eMenuStyle_List:
		// Wrap to other side if changed menus with up/down
		if( theDir == eCmdDir_U )
			aNextSel = aNewItemCount;
		else if( theDir == eCmdDir_D )
			aNextSel = 1;
		else
			aNextSel = min(selectedYPos(anOldMenuID)+1, aNewItemCount);
		break;
	case eMenuStyle_Bar:
		// Wrap to other side if changed menus with left/right
		if( theDir == eCmdDir_L )
			aNextSel = aNewItemCount;
		else if( theDir == eCmdDir_R )
			aNextSel = 1;
		else
			aNextSel = min(selectedXPos(anOldMenuID)+1, aNewItemCount);
		break;
	case eMenuStyle_Grid:
	case eMenuStyle_Columns:
		{
			int aNewX = selectedXPos(anOldMenuID);
			int aNewY = selectedYPos(anOldMenuID);
			switch(theDir)
			{
			case eCmdDir_L: aNewX = INT_MAX;	break;
			case eCmdDir_R: aNewX = 0;			break;
			case eCmdDir_U: aNewY = INT_MAX;	break;
			case eCmdDir_D: aNewY = 0;			break;
			}
			aNextSel = xyToSelectionIndex(theSideMenuID, aNewX, aNewY);
			if( aNextSel >= aNewItemCount )
			{
				switch(aNewMenuStyle)
				{
				case eMenuStyle_Grid:
					if( theDir == eCmdDir_L )
						aNextSel = aNewItemCount-1;
					else
						aNextSel -= InputMap::menuGridWidth(theSideMenuID);
					break;
				case eMenuStyle_Columns:
					if( theDir == eCmdDir_U )
						aNextSel = aNewItemCount-1;
					else
						aNextSel -= InputMap::menuGridHeight(theSideMenuID);
					break;
				default:
					aNextSel = clamp(aNextSel,
						0, InputMap::menuItemCount(theSideMenuID));
					break;
				}
			}
		}
		break;
	}

	return openSubMenu(theRootMenuID, theSideMenuID, aNextSel);
}


Command closeActiveSubMenu(int theRootMenuID)
{
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theOldSubMenuID = sActiveSubMenu[theOverlayID];

	Command result;
	if( theOldSubMenuID == theRootMenuID )
	{// Can't close root menu, but mark as attempted a change still
		gActiveOverlays.set(theOverlayID);
		return result;
	}

	// Retain selected item of parent menu rather than reset to default
	// (remember that openSubMenu() expects a 1-based index for this!)
	const int aParentMenuID = InputMap::parentMenuOfMenu(theOldSubMenuID);
	const int aParentMenuSelection = sSelectedItem[aParentMenuID];
	result = openSubMenu(theRootMenuID, aParentMenuID, aParentMenuSelection+1);

	// Make sure even if command is empty caller knows when active menu changed
	const int aNewSubMenuID = sActiveSubMenu[theOverlayID];
	if( result.type == eCmdType_Invalid && aNewSubMenuID != theOldSubMenuID )
		result.type = eCmdType_Unassigned;

	return result;
}


Command reset(int theRootMenuID)
{
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theOldSubMenuID = sActiveSubMenu[theOverlayID];

	// Borrow openSubMenu() even if already at root to reset default selection
	const bool wasActivatedThisUpdate = gActiveOverlays.test(theOverlayID);
	Command result = openSubMenu(theRootMenuID, theRootMenuID);
	DBG_ASSERT(activeSubMenu(theRootMenuID) == theRootMenuID);

	// UN-mark menu as being activated by this action (if nothing else did)
	gActiveOverlays.set(theOverlayID, wasActivatedThisUpdate);

	// Make sure even if command is empty caller knows when active menu changed
	if( result.type == eCmdType_Invalid && theRootMenuID != theOldSubMenuID )
		result.type = eCmdType_Unassigned;

	return result;
}


Command autoCommand(int theRootMenuID)
{
	return InputMap::menuAutoCommand(activeSubMenu(theRootMenuID));
}


Command backCommand(int theRootMenuID)
{
	return InputMap::menuBackCommand(activeSubMenu(theRootMenuID));
}


Command closeCommand(int theRootMenuID)
{
	return InputMap::menuBackCommand(theRootMenuID);
}


void editMenuItem(int theRootMenuID)
{
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theSubMenuID = sActiveSubMenu[theOverlayID];
	const int theItemIdx = sSelectedItem[theSubMenuID];

	const std::string& aMenuProfileName =
		InputMap::menuSectionName(theSubMenuID);
	const std::string& aMenuItemProfileName =
		InputMap::menuItemKeyName(theItemIdx);
	std::string aMenuItemCmd = Profile::getStr(
		aMenuProfileName, aMenuItemProfileName);
	bool allowInsert = true;
	switch(InputMap::menuStyle(theSubMenuID))
	{
	case eMenuStyle_4Dir:
	case eMenuStyle_Hotspots:
	case eMenuStyle_Highlight:
	case eMenuStyle_HUD:
	case eMenuStyle_KBCycleLast:
	case eMenuStyle_KBCycleDefault:
		allowInsert = false;
		break;
	}
	const std::string& aMenuItemLabel = "Edit [" +
		aMenuProfileName + "] item " + aMenuItemProfileName;
	if( Dialogs::editMenuCommand(
			aMenuItemLabel, aMenuItemCmd, true) == eResult_Ok )
	{
		if( allowInsert && aMenuItemCmd[0] == '+' )
		{// Insert as new menu item after current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			for(int i = activeMenuItemCount(theRootMenuID);
				i > theItemIdx+1; --i)
			{
				Profile::setStr(
					aMenuProfileName,
					InputMap::menuItemKeyName(i),
					Profile::getStr(aMenuProfileName,
						InputMap::menuItemKeyName(i-1)));
			}
			Profile::setStr(
				aMenuProfileName,
				InputMap::menuItemKeyName(theItemIdx+1),
				aMenuItemCmd);
		}
		else if( allowInsert && aMenuItemCmd[0] == '-' )
		{// Insert as new menu item before current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			for(int i = activeMenuItemCount(theRootMenuID);
				i > theItemIdx; --i)
			{
				Profile::setStr(
					aMenuProfileName,
					InputMap::menuItemKeyName(i),
					Profile::getStr(aMenuProfileName,
						InputMap::menuItemKeyName(i-1)));
			}
			Profile::setStr(
				aMenuProfileName,
				InputMap::menuItemKeyName(theItemIdx),
				aMenuItemCmd);
		}
		else if( allowInsert && aMenuItemCmd.empty() )
		{// Remove menu item
			const int aMenuItemCount = activeMenuItemCount(theRootMenuID);
			for(int i = theItemIdx+1; i < aMenuItemCount; ++i)
			{
				Profile::setStr(
					aMenuProfileName,
					InputMap::menuItemKeyName(i-1),
					Profile::getStr(aMenuProfileName,
						InputMap::menuItemKeyName(i)));
			}
			Profile::setStr(
				aMenuProfileName,
				InputMap::menuItemKeyName(aMenuItemCount-1),
				"");
		}
		else
		{// Modify menu item
			Profile::setStr(aMenuProfileName,
				InputMap::menuItemKeyName(theItemIdx), aMenuItemCmd);
		}
		// See if created a sub-menu, and if so add a dummy item for it
		InputMap::menuItemStringToSubMenuName(aMenuItemCmd);
		if( !aMenuItemCmd.empty() )
		{
			if( allowInsert )
			{
				Profile::setNewStr(
					aMenuProfileName+"."+aMenuItemCmd, "1", ": ..");
			}
			else
			{
				Profile::setNewStr(
					aMenuProfileName+"."+aMenuItemCmd, "Default", "");
			}
		}
		// Should just save this out immediately
		Profile::saveChangesToFile();
	}
	gActiveOverlays.set(theOverlayID);
}


void editMenuItemDir(int theRootMenuID, ECommandDir theDir)
{
	const int theOverlayID = getMenuOverlayID(theRootMenuID);
	const int theSubMenuID = sActiveSubMenu[theOverlayID];

	const std::string& aMenuProfileName =
		InputMap::menuSectionName(theSubMenuID);
	const std::string& aMenuItemProfileName =
		InputMap::menuItemDirKeyName(theDir);
	std::string aMenuItemCmd = Profile::getStr(
		aMenuProfileName, aMenuItemProfileName);
	const std::string& aMenuItemLabel = "Edit [" +
		aMenuProfileName + "] item " + aMenuItemProfileName;
	if( Dialogs::editMenuCommand(
			aMenuItemLabel, aMenuItemCmd, false) == eResult_Ok )
	{
		if( !aMenuItemCmd.empty() &&
			(aMenuItemCmd[0] == '+' || aMenuItemCmd[0] == '-') )
		{
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
		}
		Profile::setStr(aMenuProfileName,
			InputMap::menuItemDirKeyName(theDir), aMenuItemCmd);
		// See if created a sub-menu, and if so add dummy items for it
		InputMap::menuItemStringToSubMenuName(aMenuItemCmd);
		if( !aMenuItemCmd.empty() )
		{
			Profile::setNewStr(aMenuProfileName+"."+aMenuItemCmd, "U", ":");
			Profile::setNewStr(aMenuProfileName+"."+aMenuItemCmd, "L", ":");
			Profile::setNewStr(aMenuProfileName+"."+aMenuItemCmd, "R", ":");
			Profile::setNewStr(aMenuProfileName+"."+aMenuItemCmd, "D", ":");
		}
		gFullRedrawOverlays.set(theOverlayID);
		// Should just save this out immediately
		Profile::saveChangesToFile();
	}
	gActiveOverlays.set(theOverlayID);
}


void tempForceShowSubMenu(int theMenuID)
{
	if( theMenuID < 0 )
	{
		if( sOverrideActiveMenuBackup >= 0 )
		{// Restore active menu/ from backup
			const int aBackupOverlayID =
				InputMap::menuOverlayID(sOverrideActiveMenuBackup);
			if( sActiveSubMenu[aBackupOverlayID] != sOverrideActiveMenuBackup )
			{
				const int oldMenuItemCount =
					InputMap::menuItemCount(sActiveSubMenu[aBackupOverlayID]);
				sActiveSubMenu[aBackupOverlayID] =
					dropTo<u16>(sOverrideActiveMenuBackup);
				gFullRedrawOverlays.set(aBackupOverlayID);
				const int newMenuItemCount =
					InputMap::menuItemCount(sActiveSubMenu[aBackupOverlayID]);
				if( oldMenuItemCount != newMenuItemCount )
					gReshapeOverlays.set(aBackupOverlayID);
			}
			sOverrideActiveMenuBackup = -1;
		}
		return;
	}

	const int theOverlayID = InputMap::menuOverlayID(theMenuID);

	if( sOverrideActiveMenuBackup >= 0 )
		tempForceShowSubMenu(-1);

	if( sActiveSubMenu[theOverlayID] == theMenuID )
		return;

	sOverrideActiveMenuBackup = sActiveSubMenu[theOverlayID];
	const int oldMenuItemCount =
		InputMap::menuItemCount(sActiveSubMenu[theOverlayID]);
	sActiveSubMenu[theOverlayID] = dropTo<u16>(theMenuID);
	gFullRedrawOverlays.set(theOverlayID);
	const int newMenuItemCount =
		InputMap::menuItemCount(sActiveSubMenu[theOverlayID]);
	if( oldMenuItemCount != newMenuItemCount )
		gReshapeOverlays.set(theOverlayID);
}


int activeMenuForOverlayID(int theOverlayID)
{
	DBG_ASSERT(size_t(theOverlayID) < sActiveSubMenu.size());
	return sActiveSubMenu[theOverlayID];
}


bool overlayMenuAcceptsCommands(int theOverlayID)
{
	switch(InputMap::menuStyle(activeMenuForOverlayID(theOverlayID)))
	{
	case eMenuStyle_HUD:
	case eMenuStyle_KBCycleLast:
	case eMenuStyle_KBCycleDefault:
	case eMenuStyle_HotspotGuide:
	case eMenuStyle_System:
		return false;
	}

	return true;
}


int selectedItem(int theRootMenuID)
{
	return sSelectedItem[activeSubMenu(theRootMenuID)];
}


EMenuMouseMode menuMouseMode(int theRootMenuID)
{
	return InputMap::menuMouseMode(activeSubMenu(theRootMenuID));
}


int gridWidth(int theRootMenuID)
{
	return InputMap::menuGridWidth(activeSubMenu(theRootMenuID));
}


int gridHeight(int theRootMenuID)
{
	return InputMap::menuGridHeight(activeSubMenu(theRootMenuID));
}

} // Menus
