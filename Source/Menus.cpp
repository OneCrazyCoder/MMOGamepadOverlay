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
	u16 selected;
	s16 depth;
	SubMenuInfo(u16 id) : id(id), selected(), depth() {}
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
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(sMenuInfo.empty());
	for(u16 i = 0; i < InputMap::menuCount(); ++i)
	{
		const u16 aRootMenuID = InputMap::rootMenuOfMenu(i);
		sMenuInfo.addPair(aRootMenuID, MenuInfo());
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
	if( Profile::changedSections().containsPrefix("MENU.") )
	{
		// Clamp selection for all sub-menus in case an item was removed
		for(VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.begin();
			itr != sMenuInfo.end(); ++itr)
		{
			for(size_t i = 0; i < itr->second.subMenuStack.size(); ++i)
			{
				itr->second.subMenuStack[i].selected = clamp(
					itr->second.subMenuStack[i].selected, 0,
					InputMap::menuItemCount(itr->second.subMenuStack[i].id)-1);
			}
		}
	}
}


Command selectedMenuItemCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	const MenuInfo& aMenuInfo = itr->second;

	// Even if no actual change made, mark menu as having been interacted with
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	gActiveHUD.set(aHUDElementID);

	Command result;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const u16 aSelection = aMenuInfo.subMenuStack.back().selected;
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
		gConfirmedMenuItem[aHUDElementID] = aSelection;
	}

	return result;
}


Command selectMenuItem(
	u16 theMenuID,
	ECommandDir theDir,
	bool wrap,
	bool repeat)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	SubMenuInfo& aSubMenu = aMenuInfo.subMenuStack.back();
	const u16 aSubMenuID = aSubMenu.id;
	u16& aSelection = aSubMenu.selected;
	const u16 anItemCount = InputMap::menuItemCount(aSubMenuID);
	const u16 aPrevSel = aSelection;
	bool pushedPastEdge = false;

	Command aDirCmd = InputMap::commandForMenuDir(aSubMenuID, theDir);

	// Even if no actual change made, mark menu as having been interacted with
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	gActiveHUD.set(aHUDElementID);

	const EHUDType aMenuStyle = InputMap::menuStyle(theMenuID);
	const u16 aGridWidth = gridWidth(theMenuID);
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
			aSelection = min(aSelection, u16(aLinkMap.size()));
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
			gConfirmedMenuItem[aHUDElementID] = theDir;
		break;
	}

	aSelection = min(aSelection, anItemCount-1);
	if( aSelection != aPrevSel )
	{// Need to refresh to show selection differently
		DBG_ASSERT(aHUDElementID < gRefreshHUD.size());
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

	return aDirCmd;
}


Command openSubMenu(u16 theMenuID, u16 theSubMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theSubMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	const u16 oldMenuItemCount = itemCount(theMenuID);

	// Even if no actual change made, mark menu as having been interacted with
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	gActiveHUD.set(aHUDElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.back().id == theSubMenuID )
		return Command();

	// Push new menu on to the stack
	const s16 aStackDepth = aMenuInfo.subMenuStack.back().depth;
	aMenuInfo.subMenuStack.push_back(SubMenuInfo(theSubMenuID));
	aMenuInfo.subMenuStack.back().depth = aStackDepth + 1;

	// Need full redraw of new menu items
	DBG_ASSERT(aHUDElementID < gFullRedrawHUD.size());
	gFullRedrawHUD.set(aHUDElementID);

	// Might need to reshape menu for new menu item count
	DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
	if( oldMenuItemCount != itemCount(theMenuID) )
		gReshapeHUD.set(aHUDElementID);

	return InputMap::menuAutoCommand(theSubMenuID);
}


Command swapMenu(u16 theMenuID, u16 theAltMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	const u16 oldMenuItemCount = itemCount(theMenuID);

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
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	gActiveHUD.set(aHUDElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.back().id == theAltMenuID )
		return Command();

	// Going to change menus, will need a full redraw
	DBG_ASSERT(aHUDElementID < gFullRedrawHUD.size());
	gFullRedrawHUD.set(aHUDElementID);

	u16 aNextSel = aMenuInfo.subMenuStack.back().selected;
	const s16 aStackDepth = aMenuInfo.subMenuStack.back().depth;
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
				DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
				if( oldMenuItemCount != itemCount(theMenuID) )
					gReshapeHUD.set(aHUDElementID);
				return InputMap::menuAutoCommand(aSideMenuInfo.id);
			}
		}
		// If doesn't exist, push it on to the stack like a sub-menu,
		// so we retain the .selection value of the original menu.
		aMenuInfo.subMenuStack.push_back(
			SubMenuInfo(theAltMenuID));
		aMenuInfo.subMenuStack.back().depth = aStackDepth;
		DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
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
			const u8 anOldGridWidth = gridWidth(theMenuID);
			const u8 anOldX = aNextSel % anOldGridWidth;
			const u8 anOldY = aNextSel / anOldGridWidth;
			swap(aMenuInfo.subMenuStack.back().id, theAltMenuID);
			const u8 aNewGridWidth = gridWidth(theMenuID);
			const u8 aNewGridHeight = gridHeight(theMenuID);
			const u16 aNewItemCount = itemCount(theMenuID);
			swap(aMenuInfo.subMenuStack.back().id, theAltMenuID);
			u8 aNewX = min(anOldX, aNewGridWidth-1);
			u8 aNewY = min(anOldY, aNewGridHeight-1);
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
	aMenuInfo.subMenuStack.back().id = theAltMenuID;
	aMenuInfo.subMenuStack.back().selected = aNextSel;
	DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
	if( oldMenuItemCount != itemCount(theMenuID) )
		gReshapeHUD.set(aHUDElementID);

	return InputMap::menuAutoCommand(theAltMenuID);
}


Command closeLastSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	const u16 oldMenuItemCount = itemCount(theMenuID);
	Command result;

	// Even if no actual change made, mark menu as having been interacted with
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
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
			if( result.type == eCmdType_Empty )
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
	DBG_ASSERT(aHUDElementID < gFullRedrawHUD.size());
	gFullRedrawHUD.set(aHUDElementID);
	DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
	if( oldMenuItemCount != itemCount(theMenuID) )
		gReshapeHUD.set(aHUDElementID);
	result =
		InputMap::menuAutoCommand(aMenuInfo.subMenuStack.back().id);
	// Make sure even if command is empty caller knows change happened
	if( result.type == eCmdType_Empty )
		result.type = eCmdType_Unassigned;
	return result;
}


Command reset(u16 theMenuID, u16 toItemNo)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	const u16 oldMenuItemCount = itemCount(theMenuID);
	Command result;

	if( aMenuInfo.subMenuStack.size() > 1 ||
		aMenuInfo.subMenuStack.back().id != theMenuID ||
		aMenuInfo.subMenuStack.back().selected != toItemNo-1 )
	{
		aMenuInfo.subMenuStack.clear();
		aMenuInfo.subMenuStack.push_back(SubMenuInfo(theMenuID));
		if( toItemNo > 1 )
		{
			aMenuInfo.subMenuStack.back().selected =
				min(toItemNo-1, InputMap::menuItemCount(theMenuID)-1);
		}
		const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
		DBG_ASSERT(aHUDElementID < gFullRedrawHUD.size());
		gFullRedrawHUD.set(aHUDElementID);
		DBG_ASSERT(aHUDElementID < gReshapeHUD.size());
		if( oldMenuItemCount != itemCount(theMenuID) )
			gReshapeHUD.set(aHUDElementID);
		result =
			InputMap::menuAutoCommand(theMenuID);
		// Make sure even if command is empty caller knows change happened
		if( result.type == eCmdType_Empty )
			result.type = eCmdType_Unassigned;
	}

	return result;
}


Command autoCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	return InputMap::menuAutoCommand(aSubMenuID);
}


Command backCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	return InputMap::menuBackCommand(aSubMenuID);
}


Command closeCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	return InputMap::menuBackCommand(theMenuID);
}


void editMenuItem(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const u16 anItemIdx = selectedItem(theMenuID);
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
			const u16 aMenuItemCount = itemCount(theMenuID);
			for(u16 i = aMenuItemCount; i > anItemIdx+1; --i)
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
			const u16 aMenuItemCount = itemCount(theMenuID);
			for(u16 i = aMenuItemCount; i > anItemIdx; --i)
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
			const u16 aMenuItemCount = itemCount(theMenuID);
			for(u16 i = anItemIdx+1; i < aMenuItemCount; ++i)
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
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	gActiveHUD.set(aHUDElementID);
}


void editMenuItemDir(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
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
		DBG_ASSERT(aHUDElementID < gFullRedrawHUD.size());
		gFullRedrawHUD.set(aHUDElementID);
		// Should just save this out immediately
		Profile::saveChangesToFile();
	}
	DBG_ASSERT(aHUDElementID < gActiveHUD.size());
	gActiveHUD.set(aHUDElementID);
}


u16 activeSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	const MenuInfo& aMenuInfo = itr->second;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());

	return aMenuInfo.subMenuStack.back().id;
}


u16 selectedItem(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	const MenuInfo& aMenuInfo = itr->second;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());

	return aMenuInfo.subMenuStack.back().selected;
}


u16 itemCount(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	const MenuInfo& aMenuInfo = itr->second;
	if( InputMap::menuStyle(theMenuID) == eMenuStyle_4Dir )
		return eCmdDir_Num;

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	return InputMap::menuItemCount(
		aMenuInfo.subMenuStack.back().id);
}


u8 gridWidth(u16 theMenuID)
{
	return InputMap::menuGridWidth(theMenuID);
}


u8 gridHeight(u16 theMenuID)
{
	return InputMap::menuGridHeight(theMenuID);
}


} // Menus
