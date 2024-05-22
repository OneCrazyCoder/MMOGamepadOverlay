//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Menus.h"

#include "Dialogs.h" // editMenuCommand()
#include "Lookup.h"
#include "InputMap.h"
#include "Profile.h"

namespace Menus
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

const Command kEmptyMenuCommand = Command();

struct SubMenuInfo
{
	u16 id;
	u16 selected;
	SubMenuInfo(u16 id) : id(id), selected() {}
};

struct MenuInfo
{
	std::vector<SubMenuInfo> subMenuStack;
	EHUDType style;
	u16 hudElementID;
	u8 gridWidth;
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
		aMenuInfo.style = InputMap::menuStyle(aMenuID);
		aMenuInfo.hudElementID = InputMap::hudElementForMenu(aMenuID);
		aMenuInfo.gridWidth = 1;
		if( aMenuInfo.style == eMenuStyle_Grid )
		{
			std::string aKey = "Menu.";
			aKey += InputMap::hudElementKeyName(aMenuInfo.hudElementID);
			aKey += "/GridWidth";
			aMenuInfo.gridWidth =
				u8(max(0, intFromString(Profile::getStr(aKey))) & 0xFF);
			if( aMenuInfo.gridWidth == 0 )
			{// Auto-calculate grid width based on item count
				aMenuInfo.gridWidth =
					u8(u32(ceil(sqrt(double(itemCount(aMenuID))))) & 0xFF);
			}
		}
	}
}


void cleanup()
{
	sMenuInfo.clear();
}


const Command& selectedMenuItemCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	const MenuInfo& aMenuInfo = itr->second;

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const u16 aSelection = aMenuInfo.subMenuStack.back().selected;
	if( aMenuInfo.style == eMenuStyle_4Dir )
		return kEmptyMenuCommand;

	// Have selected menu item show a confirmation flash if
	// this command won't change sub-menus
	const Command& result =
		InputMap::commandForMenuItem(aSubMenuID, aSelection);
	switch(result.type)
	{
	case eCmdType_OpenSubMenu:
	case eCmdType_ReplaceMenu:
	case eCmdType_MenuReset:
	case eCmdType_MenuBack:
	case eCmdType_MenuBackOrClose:
		break;
	default:
		gConfirmedMenuItem[aMenuInfo.hudElementID] = aSelection;
	}

	return InputMap::commandForMenuItem(aSubMenuID, aSelection);
}


const Command& selectMenuItem(
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
	const u16 aPrevSel = aSelection;
	const u16 anItemCount = InputMap::menuItemCount(aSubMenuID);
	bool pushedPastEdge = false;
	
	const Command& aDirCmd = InputMap::commandForMenuDir(aSubMenuID, theDir);

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);

	const EHUDType aMenuStyle = aMenuInfo.style;
	const u16 aGridWidth = gridWidth(theMenuID);
	switch(aMenuStyle)
	{
	case eMenuStyle_List:
		switch(theDir)
		{
		case eCmdDir_L:
			pushedPastEdge = !repeat;
			break;
		case eCmdDir_R:
			pushedPastEdge = !repeat;
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
			pushedPastEdge = !repeat;
			break;
		case eCmdDir_D:
			pushedPastEdge = !repeat;
			break;
		}
		break;
	case eMenuStyle_4Dir:
		pushedPastEdge = !repeat;
		if( pushedPastEdge )
			gConfirmedMenuItem[aMenuInfo.hudElementID] = theDir;
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
			pushedPastEdge =
				aSelection >= (anItemCount / aGridWidth) * aGridWidth;
			if( aSelection + aGridWidth < anItemCount )
				aSelection = min(anItemCount - 1, aSelection + aGridWidth);
			else if( wrap && anItemCount > 2 )
				aSelection = aSelection % aGridWidth;
			else if( aSelection < ((anItemCount-1) / aGridWidth) * aGridWidth )
				aSelection = anItemCount - 1;
			break;
		}
		break;
	}

	if( aSelection != aPrevSel )
	{// Need to redraw to show selection differently
		DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(aMenuInfo.hudElementID);
	}

	if( pushedPastEdge && aDirCmd.type != eCmdType_Empty )
		return aDirCmd;
	return kEmptyMenuCommand;
}


const Command& openSubMenu(u16 theMenuID, u16 theSubMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theSubMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.back().id == theSubMenuID )
		return kEmptyMenuCommand;

	// Push new menu on to the stack
	aMenuInfo.subMenuStack.push_back(SubMenuInfo(theSubMenuID));

	// Need to redraw new menu items
	DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
	gRedrawHUD.set(aMenuInfo.hudElementID);

	return InputMap::menuAutoCommand(theSubMenuID);
}


const Command& replaceMenu(u16 theMenuID, u16 theReplacementSubMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	if( theMenuID != InputMap::rootMenuOfMenu(theReplacementSubMenuID) )
	{
		logError(
			"Attempted to open sub-menu '%s' from menu '%s', "
			"but it is not a sub-menu of this root menu!",
			InputMap::menuLabel(theReplacementSubMenuID).c_str(),
			InputMap::menuLabel(theMenuID).c_str());
		return kEmptyMenuCommand;
	}

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	if( aMenuInfo.subMenuStack.back().id == theReplacementSubMenuID )
		return kEmptyMenuCommand;

	// Going to change menus, will need to redraw
	DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
	gRedrawHUD.set(aMenuInfo.hudElementID);

	if( aMenuInfo.style == eMenuStyle_Slots )
	{
		// Want to retain selection of any "side menus", so check
		// if the requested menu has already been opened and, if so,
		// just bump it as-is to the top of the stack.
		for(std::vector<SubMenuInfo>::iterator itr =
				aMenuInfo.subMenuStack.begin();
			itr != aMenuInfo.subMenuStack.end(); ++itr)
		{
			if( itr->id == theReplacementSubMenuID )
			{
				const SubMenuInfo aSideMenuInfo = *itr;
				aMenuInfo.subMenuStack.erase(itr);
				aMenuInfo.subMenuStack.push_back(aSideMenuInfo);
				return InputMap::menuAutoCommand(aSideMenuInfo.id);
			}
		}
		// If doesn't exist, push it on to the stack like a sub-menu,
		// so we retain the .selection value of the original menu.
		// Note that this does cause closeLastSubMenu() to act a little
		// weird with this menu style, but its not really intended to use
		// a "back" button or have normal sub-menus anyway.
		aMenuInfo.subMenuStack.push_back(
			SubMenuInfo(theReplacementSubMenuID));
	}
	else
	{
		// Replace current sub-menu id with new sub-menu id directly
		DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
		SubMenuInfo& aSubMenu = aMenuInfo.subMenuStack.back();
		aSubMenu.id = theReplacementSubMenuID;
		// Clamp selection to new sub-menu's max
		aSubMenu.selected = max(aSubMenu.selected,
			InputMap::menuItemCount(theReplacementSubMenuID) - 1);
	}

	// Need to redraw new menu items
	DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
	gRedrawHUD.set(aMenuInfo.hudElementID);

	return InputMap::menuAutoCommand(theReplacementSubMenuID);
}


const Command* closeLastSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);

	if( aMenuInfo.subMenuStack.size() > 1 )
	{
		aMenuInfo.subMenuStack.pop_back();
		DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(aMenuInfo.hudElementID);
		const Command& aCmd =
			InputMap::menuAutoCommand(aMenuInfo.subMenuStack.back().id);
		return &aCmd;
	}
	
	// For Slots-style menus, "side" menus (other slots) can replace
	// root menu in 0th position, and this can be used to restore it
	if( aMenuInfo.style == eMenuStyle_Slots &&
		aMenuInfo.subMenuStack[0].id != theMenuID )
	{
		aMenuInfo.subMenuStack[0] = SubMenuInfo(theMenuID);
		DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(aMenuInfo.hudElementID);
		const Command& aCmd =
			InputMap::menuAutoCommand(aMenuInfo.subMenuStack.back().id);
		return &aCmd;
	}

	return null;
}


const Command* reset(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	if( aMenuInfo.subMenuStack.size() > 1 ||
		aMenuInfo.subMenuStack.back().id != theMenuID ||
		aMenuInfo.subMenuStack.back().selected != 0 )
	{
		aMenuInfo.subMenuStack.clear();
		aMenuInfo.subMenuStack.push_back(SubMenuInfo(theMenuID));
		DBG_ASSERT(aMenuInfo.hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(aMenuInfo.hudElementID);
		const Command& aCmd =
			InputMap::menuAutoCommand(theMenuID);
		return &aCmd;
	}
	
	return null;
}


const Command& autoCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	return InputMap::menuAutoCommand(aSubMenuID);
}


void editMenuItem(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const u16 anItemIdx = selectedItem(theMenuID);
	const std::string& aMenuItemKey =
		InputMap::menuItemKey(aSubMenuID, anItemIdx);
	std::string aMenuItemCmd = Profile::getStr(aMenuItemKey);
	if( Dialogs::editMenuCommand(aMenuItemCmd) == eResult_Ok )
	{
		gReloadProfile = true;
		if( aMenuItemCmd[0] == '+' )
		{// Insert as new menu item after current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			const u16 aMenuItemCount = itemCount(theMenuID);
			for(u16 i = aMenuItemCount; i > anItemIdx+1; --i)
			{
				Profile::setStr(
					InputMap::menuItemKey(aSubMenuID, i),
					Profile::getStr(
						InputMap::menuItemKey(aSubMenuID, i-1)));
			}
			Profile::setStr(
				InputMap::menuItemKey(aSubMenuID, anItemIdx+1),
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
					InputMap::menuItemKey(aSubMenuID, i),
					Profile::getStr(
						InputMap::menuItemKey(aSubMenuID, i-1)));
			}
			Profile::setStr(
				InputMap::menuItemKey(aSubMenuID, anItemIdx),
				aMenuItemCmd);
		}
		else if( aMenuItemCmd.empty() )
		{// Remove menu item
			const u16 aMenuItemCount = itemCount(theMenuID);
			for(u16 i = anItemIdx+1; i < aMenuItemCount; ++i)
			{
				Profile::setStr(
					InputMap::menuItemKey(aSubMenuID, i-1),
					Profile::getStr(
						InputMap::menuItemKey(aSubMenuID, i)));
			}
			Profile::setStr(
				InputMap::menuItemKey(aSubMenuID, aMenuItemCount-1),
				"");
		}
		else
		{// Modify menu item
			Profile::setStr(aMenuItemKey, aMenuItemCmd);
		}
	}
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);
}


void editMenuItemDir(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	MenuInfo& aMenuInfo = itr->second;

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	const u16 aSubMenuID = aMenuInfo.subMenuStack.back().id;
	const std::string& aMenuItemKey =
		InputMap::menuItemDirKey(aSubMenuID, theDir);
	std::string aMenuItemCmd = Profile::getStr(aMenuItemKey);
	if( Dialogs::editMenuCommand(aMenuItemCmd, true) == eResult_Ok )
	{
		if( !aMenuItemCmd.empty() &&
			(aMenuItemCmd[0] == '+' || aMenuItemCmd[0] == '-') )
		{
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
		}
		gReloadProfile = true;
		Profile::setStr(aMenuItemKey, aMenuItemCmd);
	}
	DBG_ASSERT(aMenuInfo.hudElementID < gActiveHUD.size());
	gActiveHUD.set(aMenuInfo.hudElementID);
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
	if( aMenuInfo.style == eMenuStyle_4Dir )
		return eCmdDir_Num;

	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());
	return InputMap::menuItemCount(
		aMenuInfo.subMenuStack.back().id);
}


u8 gridWidth(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	VectorMap<u16, MenuInfo>::iterator itr = sMenuInfo.find(theMenuID);
	DBG_ASSERT(itr != sMenuInfo.end());
	const MenuInfo& aMenuInfo = itr->second;
	DBG_ASSERT(!aMenuInfo.subMenuStack.empty());

	return min(itemCount(theMenuID),
		aMenuInfo.gridWidth);
}


u8 gridHeight(u16 theMenuID)
{
	const u16 anItemCount = InputMap::menuItemCount(theMenuID);
	const u16 aGridWidth = gridWidth(theMenuID);
	return (anItemCount + aGridWidth - 1) / aGridWidth;
}

} // Menus
