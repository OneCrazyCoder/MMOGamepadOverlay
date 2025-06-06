//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Menus.h"

#include "Dialogs.h" // editMenuCommand()
#include "HotspotMap.h"
#include "InputMap.h"
#include "Profile.h"

namespace Menus
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

struct SubMenuInfo
{
	u16 id;
	s8 selected;
	s8 depth;
	SubMenuInfo(u16 theMenuID) : id(theMenuID), selected(), depth() {}
};

struct ZERO_INIT(MenuInfo)
{
	std::vector<SubMenuInfo> subMenuStack;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static VectorMap<u16, MenuInfo> sMenuInfo;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static MenuInfo& infoForMenuID(int theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator it =
		sMenuInfo.find(dropTo<u16>(theMenuID));
	DBG_ASSERT(it != sMenuInfo.end());
	return it->second;
}


static int getMenuHUDElementID(int theMenuID)
{
	const int aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	DBG_ASSERT(aHUDElementID >= 0);
	DBG_ASSERT(aHUDElementID < gVisibleHUD.size());
	DBG_ASSERT(aHUDElementID < gRefreshHUD.size());
	DBG_ASSERT(aHUDElementID < gFullRedrawHUD.size());
	DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	DBG_ASSERT(aHUDElementID < gDisabledHUD.size());
	DBG_ASSERT(size_t(aHUDElementID) < gConfirmedMenuItem.size());
	return aHUDElementID;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(sMenuInfo.empty());
	sMenuInfo.reserve(InputMap::menuCount());
	for(int i = 0, end = InputMap::menuCount(); i < end; ++i)
	{
		sMenuInfo.addPair(
			dropTo<u16>(InputMap::rootMenuOfMenu(i)),
			MenuInfo());
	}
	sMenuInfo.sort();
	sMenuInfo.removeDuplicates();
	sMenuInfo.trim();
	for(VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.begin();
		itr != sMenuInfo.end(); ++itr)
	{
		const u16 aMenuID = itr->first;
		MenuInfo& aMenuInfo = itr->second;
		aMenuInfo.subMenuStack.push_back(SubMenuInfo(aMenuID));
		if( InputMap::menuStyle(aMenuID) == eMenuStyle_Hotspots )
			HotspotMap::getLinks(InputMap::menuHotspotArray(aMenuID));
	}
}


void cleanup()
{
	sMenuInfo.clear();
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.containsPrefix("Menu.") )
	{
		// Clamp selection for all sub-menus in case an item was removed
		for(VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.begin();
			itr != sMenuInfo.end(); ++itr)
		{
			std::vector<SubMenuInfo>& aSubMenuStack = itr->second.subMenuStack;
			for(int i = 0, end = intSize(aSubMenuStack.size()); i < end; ++i)
			{
				aSubMenuStack[i].selected = clamp(
					aSubMenuStack[i].selected, 0,
					InputMap::menuItemCount(aSubMenuStack[i].id)-1);
			}
		}
	}
}


Command selectedMenuItemCommand(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveHUD.set(aHUDElementID);

	Command result;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const int aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const int aSelection = aMenuInfo.subMenuStack.back().selected;
	if( InputMap::menuStyle(theMenuID) == eMenuStyle_4Dir )
		return result;

	// Have selected menu item show a confirmation flash if
	// this command won't change sub-menus
	result = InputMap::commandForMenuItem(aSubMenuID, aSelection);
	switch(result.type)
	{
	case eCmdType_OpenSubMenu:
	case eCmdType_SwapMenu:
	case eCmdType_MenuReset:
	case eCmdType_MenuBack:
		break;
	default:
		gConfirmedMenuItem[aHUDElementID] = dropTo<u16>(aSelection);
	}

	return result;
}


Command selectMenuItem(
	int theMenuID,
	ECommandDir theDir,
	bool wrap,
	bool repeat)
{
	MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);

	SubMenuInfo& aSubMenu = aMenuInfo.subMenuStack.back();
	const int aSubMenuID = aSubMenu.id;
	const int anItemCount = InputMap::menuItemCount(aSubMenuID);
	const int aPrevSel = aSubMenu.selected;
	int aSelection = aSubMenu.selected;
	bool pushedPastEdge = false;

	Command aDirCmd = InputMap::commandForMenuDir(aSubMenuID, theDir);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveHUD.set(aHUDElementID);

	const EHUDType aMenuStyle = InputMap::menuStyle(theMenuID);
	const int aGridWidth = gridWidth(theMenuID);
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
			else if( wrap && anItemCount > 2 )
				aSelection = anItemCount - 1;
			break;
		case eCmdDir_D:
			pushedPastEdge = aSelection >= anItemCount - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && anItemCount > 2 )
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
			else if( wrap && anItemCount > 2 )
				aSelection = anItemCount - 1;
			break;
		case eCmdDir_R:
			pushedPastEdge = aSelection >= anItemCount - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && anItemCount > 2 )
				aSelection = 0;
			break;
		case eCmdDir_U:
		case eCmdDir_D:
			pushedPastEdge = true;
			break;
		}
		break;
	case eMenuStyle_Grid:
		switch(theDir)
		{
		case eCmdDir_L:
			pushedPastEdge = aSelection % aGridWidth == 0;
			if( !pushedPastEdge )
				--aSelection;
			else if( wrap && anItemCount > 2 )
				aSelection = min(anItemCount - 1, aSelection + aGridWidth - 1);
			break;
		case eCmdDir_R:
			pushedPastEdge =
				aSelection >= anItemCount -1 ||
				aSelection % aGridWidth == aGridWidth - 1;
			if( !pushedPastEdge )
				++aSelection;
			else if( wrap && anItemCount > 2 )
				aSelection = (aSelection / aGridWidth) * aGridWidth;
			break;
		case eCmdDir_U:
			pushedPastEdge = aSelection < aGridWidth;
			if( !pushedPastEdge )
				aSelection -= aGridWidth;
			else if( wrap && anItemCount > 2 )
				aSelection += ((anItemCount-1) / aGridWidth) * aGridWidth;
			if( aSelection >= anItemCount )
				aSelection -= aGridWidth;
			break;
		case eCmdDir_D:
			pushedPastEdge = aSelection + aGridWidth >= anItemCount;
			if( !pushedPastEdge )
				aSelection += aGridWidth;
			else if( wrap && anItemCount > 2 )
				aSelection = aSelection % aGridWidth;
			else if( aDirCmd.type < eCmdType_FirstValid &&
					 aSelection < ((anItemCount-1) / aGridWidth) * aGridWidth )
				aSelection = anItemCount - 1;
			break;
		}
		break;
	case eMenuStyle_Hotspots:
		{
			const HotspotMap::Links& aLinkMap = HotspotMap::getLinks(
				InputMap::menuHotspotArray(theMenuID));
			aSelection = min(aSelection, intSize(aLinkMap.size()));
			pushedPastEdge = aLinkMap[aSelection].edge[theDir];
			if( !pushedPastEdge || wrap )
				aSelection = aLinkMap[aSelection].next[theDir];
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
			aSelection = decWrap(aSelection, anItemCount);
			break;
		case eCmdDir_D:
			aSelection = incWrap(aSelection, anItemCount);
			break;
		}
		break;
	case eMenuStyle_4Dir:
		pushedPastEdge = !repeat;
		if( pushedPastEdge )
			gConfirmedMenuItem[aHUDElementID] = dropTo<u16>(theDir);
		break;
	}

	aSelection = min(aSelection, anItemCount-1);
	if( aSelection != aPrevSel )
	{// Need to refresh to show selection differently
		gRefreshHUD.set(aHUDElementID);
	}

	if( !pushedPastEdge )
		aDirCmd = Command();

	if( aDirCmd.type == eCmdType_OpenSubMenu && aMenuStyle != eMenuStyle_4Dir )
	{
		// Requests to open sub-menus with directionals in most menu styles
		// should use swap menu instead, meaning they'll stay on the same
		// "level" as the previous menu instead of being a "child" menu.
		aDirCmd.type = eCmdType_SwapMenu;
		aDirCmd.swapDir = theDir;
	}

	aSubMenu.selected = dropTo<s8>(aSelection);

	return aDirCmd;
}


Command openSubMenu(int theMenuID, int theSubMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theSubMenuID));
	MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);
	const int oldMenuItemCount = itemCount(theMenuID);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveHUD.set(aHUDElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.back().id == theSubMenuID )
		return Command();

	// Push new menu on to the stack
	const int aStackDepth = aMenuInfo.subMenuStack.back().depth;
	aMenuInfo.subMenuStack.push_back(SubMenuInfo(dropTo<u16>(theSubMenuID)));
	aMenuInfo.subMenuStack.back().depth = dropTo<s8>(aStackDepth + 1);

	// Need full redraw of new menu items
	gFullRedrawHUD.set(aHUDElementID);

	// Might need to reshape menu for new menu item count
	if( oldMenuItemCount != itemCount(theMenuID) )
		gReshapeHUD.set(aHUDElementID);

	return InputMap::menuAutoCommand(theSubMenuID);
}


Command swapMenu(int theMenuID, int theAltMenuID, ECommandDir theDir)
{
	MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);
	const int oldMenuItemCount = itemCount(theMenuID);

	if( theMenuID != InputMap::rootMenuOfMenu(theAltMenuID) )
	{
		logError(
			"Attempted to open sub-menu '%s' from menu '%s', "
			"but it is not a sub-menu of this root menu!",
			InputMap::menuLabel(theAltMenuID).c_str(),
			InputMap::menuLabel(theMenuID).c_str());
		return Command();
	}

	// Even if no actual change made, mark menu as having been interacted with
	gActiveHUD.set(aHUDElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.back().id == theAltMenuID )
		return Command();

	// Going to change menus, will need a full redraw
	gFullRedrawHUD.set(aHUDElementID);

	int aNextSel = aMenuInfo.subMenuStack.back().selected;
	const int aStackDepth = aMenuInfo.subMenuStack.back().depth;
	switch(InputMap::menuStyle(theMenuID))
	{
	case eMenuStyle_Slots:
		// Want to retain selection of any "side menus", so check
		// if the requested menu has already been opened and, if so,
		// just bump it as-is to the top of the stack.
		for(std::vector<SubMenuInfo>::iterator itr =
				aMenuInfo.subMenuStack.begin();
			itr != aMenuInfo.subMenuStack.end(); ++itr)
		{
			if( itr->id == theAltMenuID )
			{
				const SubMenuInfo aSideMenuInfo = *itr;
				aMenuInfo.subMenuStack.erase(itr);
				aMenuInfo.subMenuStack.push_back(aSideMenuInfo);
				if( oldMenuItemCount != itemCount(theMenuID) )
					gReshapeHUD.set(aHUDElementID);
				return InputMap::menuAutoCommand(aSideMenuInfo.id);
			}
		}
		// If doesn't exist, push it on to the stack like a sub-menu,
		// so we retain the .selection value of the original menu.
		aMenuInfo.subMenuStack.push_back(
			SubMenuInfo(dropTo<u16>(theAltMenuID)));
		aMenuInfo.subMenuStack.back().depth = dropTo<s8>(aStackDepth);
		if( oldMenuItemCount != itemCount(theMenuID) )
			gReshapeHUD.set(aHUDElementID);
		return InputMap::menuAutoCommand(theAltMenuID);

	case eMenuStyle_List:
		switch(theDir)
		{
		case eCmdDir_U:
			aNextSel = u16(-1);
			break;
		case eCmdDir_D:
			aNextSel = 0;
			break;
		}
		break;

	case eMenuStyle_Bar:
		switch(theDir)
		{
		case eCmdDir_L:
			aNextSel = u16(-1);
			break;
		case eCmdDir_R:
			aNextSel = 0;
			break;
		}
		break;

	case eMenuStyle_Grid:
		{
			const int anOldGridWidth = gridWidth(theMenuID);
			const int anOldX = aNextSel % anOldGridWidth;
			const int anOldY = aNextSel / anOldGridWidth;
			const u16 anOldMenuID = aMenuInfo.subMenuStack.back().id;
			aMenuInfo.subMenuStack.back().id = dropTo<u16>(theAltMenuID);
			const int aNewGridWidth = gridWidth(theMenuID);
			const int aNewGridHeight = gridHeight(theMenuID);
			const int aNewItemCount = itemCount(theMenuID);
			aMenuInfo.subMenuStack.back().id = anOldMenuID;
			int aNewX = min(anOldX, aNewGridWidth-1);
			int aNewY = min(anOldY, aNewGridHeight-1);
			switch(theDir)
			{
			case eCmdDir_L: aNewX = aNewGridWidth-1; break;
			case eCmdDir_R: aNewX = 0; break;
			case eCmdDir_U: aNewY = aNewGridHeight-1; break;
			case eCmdDir_D: aNewY = 0; break;
			}
			aNextSel = aNewY * aNewGridWidth + aNewX;
			if( aNextSel >= aNewItemCount )
			{
				if( theDir == eCmdDir_L )
					aNextSel = aNewItemCount-1;
				else
					aNextSel -= aNewGridWidth;
				DBG_ASSERT(aNextSel < aNewItemCount);
			}
		}
		break;

	default:
		aNextSel = 0;
		break;
	}

	// Directly replace the current sub-menu with the alt in the stack
	aNextSel = min(aNextSel, InputMap::menuItemCount(theAltMenuID)-1);
	aMenuInfo.subMenuStack.back().id = dropTo<u16>(theAltMenuID);
	aMenuInfo.subMenuStack.back().selected = dropTo<s8>(aNextSel);
	if( oldMenuItemCount != itemCount(theMenuID) )
		gReshapeHUD.set(aHUDElementID);

	return InputMap::menuAutoCommand(theAltMenuID);
}


Command closeLastSubMenu(int theMenuID)
{
	MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);
	const int oldMenuItemCount = itemCount(theMenuID);
	Command result;

	// Even if no actual change made, mark menu as having been interacted with
	gActiveHUD.set(aHUDElementID);

	// If already at root, do nothing
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.size() == 1 &&
		aMenuInfo.subMenuStack[0].id == theMenuID )
		return result;

	const s16 aStackDepth = aMenuInfo.subMenuStack.back().depth;
	if( aStackDepth == 0 )
	{
		if( InputMap::menuStyle(theMenuID) == eMenuStyle_Slots )
		{// Swap back to root menu
			if( aMenuInfo.subMenuStack.back().id == theMenuID )
				return result;
			swapMenu(theMenuID, theMenuID, eCmdDir_None);
			result =
				InputMap::menuAutoCommand(aMenuInfo.subMenuStack.back().id);
			// Make sure even if command is empty caller knows change happened
			if( result.type == eCmdType_Invalid )
				result.type = eCmdType_Unassigned;
			return result;
		}
		// Reset back to root menu
		result = reset(theMenuID);
		return result;
	}

	// Remove all sub menus at same depth as current to pop back to parent
	while(aMenuInfo.subMenuStack.back().depth == aStackDepth)
		aMenuInfo.subMenuStack.pop_back();
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	gFullRedrawHUD.set(aHUDElementID);
	if( oldMenuItemCount != itemCount(theMenuID) )
		gReshapeHUD.set(aHUDElementID);
	result =
		InputMap::menuAutoCommand(aMenuInfo.subMenuStack.back().id);
	// Make sure even if command is empty caller knows change happened
	if( result.type == eCmdType_Invalid )
		result.type = eCmdType_Unassigned;
	return result;
}


Command reset(int theMenuID, int toItemNo)
{
	MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);
	const int oldMenuItemCount = itemCount(theMenuID);
	Command result;

	if( aMenuInfo.subMenuStack.size() > 1 ||
		aMenuInfo.subMenuStack.back().id != theMenuID ||
		aMenuInfo.subMenuStack.back().selected != toItemNo-1 )
	{
		aMenuInfo.subMenuStack.clear();
		aMenuInfo.subMenuStack.push_back(SubMenuInfo(dropTo<u16>(theMenuID)));
		if( toItemNo > 1 )
		{
			aMenuInfo.subMenuStack.back().selected = dropTo<s8>(
				min(toItemNo-1, InputMap::menuItemCount(theMenuID)-1));
		}
		gFullRedrawHUD.set(aHUDElementID);
		if( oldMenuItemCount != itemCount(theMenuID) )
			gReshapeHUD.set(aHUDElementID);
		result =
			InputMap::menuAutoCommand(theMenuID);
		// Make sure even if command is empty caller knows change happened
		if( result.type == eCmdType_Invalid )
			result.type = eCmdType_Unassigned;
	}

	return result;
}


Command autoCommand(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const int aSubMenuID = aMenuInfo.subMenuStack.back().id;
	return InputMap::menuAutoCommand(aSubMenuID);
}


Command backCommand(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const int aSubMenuID = aMenuInfo.subMenuStack.back().id;
	return InputMap::menuBackCommand(aSubMenuID);
}


Command closeCommand(int theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	return InputMap::menuBackCommand(theMenuID);
}


void editMenuItem(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const int aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const int anItemIdx = selectedItem(theMenuID);
	const std::string& aMenuProfileName =
		InputMap::menuSectionName(aSubMenuID);
	std::string aMenuItemCmd = Profile::getStr(
		aMenuProfileName, InputMap::menuItemKeyName(anItemIdx));
	if( Dialogs::editMenuCommand(aMenuItemCmd) == eResult_Ok )
	{
		if( aMenuItemCmd[0] == '+' )
		{// Insert as new menu item after current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			for(int i = itemCount(theMenuID); i > anItemIdx+1; --i)
			{
				Profile::setStr(
					aMenuProfileName,
					InputMap::menuItemKeyName(i),
					Profile::getStr(aMenuProfileName,
						InputMap::menuItemKeyName(i-1)));
			}
			Profile::setStr(
				aMenuProfileName,
				InputMap::menuItemKeyName(anItemIdx+1),
				aMenuItemCmd);
		}
		else if( aMenuItemCmd[0] == '-' )
		{// Insert as new menu item before current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			for(int i = itemCount(theMenuID); i > anItemIdx; --i)
			{
				Profile::setStr(
					aMenuProfileName,
					InputMap::menuItemKeyName(i),
					Profile::getStr(aMenuProfileName,
						InputMap::menuItemKeyName(i-1)));
			}
			Profile::setStr(
				aMenuProfileName,
				InputMap::menuItemKeyName(anItemIdx),
				aMenuItemCmd);
		}
		else if( aMenuItemCmd.empty() )
		{// Remove menu item
			const int aMenuItemCount = itemCount(theMenuID);
			for(int i = anItemIdx+1; i < aMenuItemCount; ++i)
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
				InputMap::menuItemKeyName(anItemIdx), aMenuItemCmd);
		}
		// See if created a sub-menu, and if so add a dummy item for it
		InputMap::menuItemStringToSubMenuName(aMenuItemCmd);
		if( !aMenuItemCmd.empty() )
			Profile::setNewStr(aMenuProfileName+"."+aMenuItemCmd, "1", ": ..");
		// Should just save this out immediately
		Profile::saveChangesToFile();
	}
	gActiveHUD.set(aHUDElementID);
}


void editMenuItemDir(int theMenuID, ECommandDir theDir)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	const int aHUDElementID = getMenuHUDElementID(theMenuID);

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const int aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const std::string& aMenuProfileName =
		InputMap::menuSectionName(aSubMenuID);
	std::string aMenuItemCmd = Profile::getStr(
		aMenuProfileName, InputMap::menuItemDirKeyName(theDir));
	if( Dialogs::editMenuCommand(aMenuItemCmd, true) == eResult_Ok )
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
		gFullRedrawHUD.set(aHUDElementID);
		// Should just save this out immediately
		Profile::saveChangesToFile();
	}
	gActiveHUD.set(aHUDElementID);
}


int activeSubMenu(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());

	return aMenuInfo.subMenuStack.back().id;
}


int selectedItem(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());

	return aMenuInfo.subMenuStack.back().selected;
}


int itemCount(int theMenuID)
{
	if( InputMap::menuStyle(theMenuID) == eMenuStyle_4Dir )
		return eCmdDir_Num;

	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	return InputMap::menuItemCount(
		aMenuInfo.subMenuStack.back().id);
}


int gridWidth(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	return InputMap::menuGridWidth(
		aMenuInfo.subMenuStack.back().id);
}


int gridHeight(int theMenuID)
{
	const MenuInfo& aMenuInfo = infoForMenuID(theMenuID);
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	return InputMap::menuGridHeight(
		aMenuInfo.subMenuStack.back().id);
}

} // Menus
