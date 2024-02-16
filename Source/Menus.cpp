//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Menus.h"

#include "InputMap.h"

namespace Menus
{

//-----------------------------------------------------------------------------
// MenuInfo
//-----------------------------------------------------------------------------

struct SubMenuInfo
{
	u16 id;
	u16 selected;
	SubMenuInfo(u16 id) : id(id), selected() {}
};

struct MenuInfo
{
	EHUDType style;
	u16 hudElementID;
	std::vector<SubMenuInfo> subMenuStack;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<MenuInfo> sMenuInfo;
static Command sEmptyMenuCommand = Command();


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	const u16 kRootMenuCount = InputMap::rootMenuCount();
	sMenuInfo.reserve(kRootMenuCount);
	sMenuInfo.resize(kRootMenuCount);
	for(u16 i = 0; i < kRootMenuCount; ++i)
	{
		sMenuInfo[i].style = InputMap::menuStyle(i);
		sMenuInfo[i].hudElementID = InputMap::hudElementForMenu(i);
		sMenuInfo[i].subMenuStack.push_back(SubMenuInfo(i));
	}
}


void cleanup()
{
	sMenuInfo.clear();
}


const Command& selectedMenuItemCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gActiveHUD.size());
	gActiveHUD.set(sMenuInfo[theMenuID].hudElementID);

	const u16 aSubMenuID = activeSubMenu(theMenuID);
	const u16 aSelection = sMenuInfo[theMenuID].subMenuStack.back().selected;
	if( sMenuInfo[theMenuID].style == eMenuStyle_4Dir )
		return sEmptyMenuCommand;
	return InputMap::commandForMenuItem(aSubMenuID, aSelection);
}


const Command& selectMenuItem(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());

	SubMenuInfo& aSubMenu = sMenuInfo[theMenuID].subMenuStack.back();
	const u16 aSubMenuID = aSubMenu.id;
	u16& aSelection = aSubMenu.selected;
	const u16 aPrevSel = aSelection;
	const u16 anItemCount = InputMap::menuItemCount(aSubMenuID);
	const Command& aDirCmd = InputMap::commandForMenuDir(aSubMenuID, theDir);

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gActiveHUD.size());
	gActiveHUD.set(sMenuInfo[theMenuID].hudElementID);

	const EHUDType aMenuStyle = sMenuInfo[theMenuID].style;
	switch(aMenuStyle)
	{
	case eMenuStyle_List:
	case eMenuStyle_ListWrap:
	case eMenuStyle_Slots:
		switch(theDir)
		{
		case eCmdDir_L:
			if( aDirCmd.type == eCmdType_Empty )
				aSelection = 0;
			else
				return aDirCmd;
			break;
		case eCmdDir_R:
			if( aDirCmd.type == eCmdType_Empty )
				aSelection = anItemCount - 1;
			else
				return aDirCmd;
			break;
		case eCmdDir_U:
			aSelection =
				(aMenuStyle == eMenuStyle_List ||
				 (anItemCount <= 2 && aMenuStyle != eMenuStyle_Slots)) 
					? max(0, (signed)aSelection - 1)
					: decWrap(aSelection, anItemCount);
			break;
		case eCmdDir_D:
			aSelection =
				(aMenuStyle == eMenuStyle_List ||
				 (anItemCount <= 2 && aMenuStyle != eMenuStyle_Slots)) 
					? min(anItemCount - 1, aSelection + 1)
					: incWrap(aSelection, anItemCount);
			break;
		}
		break;
	case eMenuStyle_Bar:
	case eMenuStyle_BarWrap:
		switch(theDir)
		{
		case eCmdDir_L:
			aSelection =
				(aMenuStyle == eMenuStyle_Bar || anItemCount <= 2) 
					? max(0, (signed)aSelection - 1)
					: decWrap(aSelection, anItemCount);
			break;
		case eCmdDir_R:
			aSelection =
				(aMenuStyle == eMenuStyle_Bar || anItemCount <= 2) 
					? min(anItemCount - 1, aSelection + 1)
					: incWrap(aSelection, anItemCount);
			break;
		case eCmdDir_U:
			if( aDirCmd.type == eCmdType_Empty )
				aSelection = 0;
			else
				return aDirCmd;
			break;
		case eCmdDir_D:
			if( aDirCmd.type == eCmdType_Empty )
				aSelection = anItemCount - 1;
			else
				return aDirCmd;
			break;
		}
		break;
	case eMenuStyle_4Dir:
		return aDirCmd;
	}

	if( aSelection != aPrevSel )
	{// Need to redraw to show selection differently
		DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);
	}

	return sEmptyMenuCommand;
}


void openSubMenu(u16 theMenuID, u16 theSubMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	if( theMenuID != InputMap::rootMenuOfMenu(theSubMenuID) )
	{
		logError(
			"Attempted to open sub-menu '%s' from menu '%s', "
			"but it is not a sub-menu of this root menu!",
			InputMap::menuLabel(theSubMenuID).c_str(),
			InputMap::menuLabel(theMenuID).c_str());
		return;
	}

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gActiveHUD.size());
	gActiveHUD.set(sMenuInfo[theMenuID].hudElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	if( sMenuInfo[theMenuID].subMenuStack.back().id == theSubMenuID )
		return;

	// Push new menu on to the stack
	sMenuInfo[theMenuID].subMenuStack.push_back(SubMenuInfo(theSubMenuID));

	// Need to redraw new menu items
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
	gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);
}


void closeLastSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gActiveHUD.size());
	gActiveHUD.set(sMenuInfo[theMenuID].hudElementID);

	if( sMenuInfo[theMenuID].subMenuStack.size() > 1 )
	{
		sMenuInfo[theMenuID].subMenuStack.pop_back();
		DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);
	}
	else if( sMenuInfo[theMenuID].style == eMenuStyle_Slots &&
			 sMenuInfo[theMenuID].subMenuStack[0].id != theMenuID )
	{
		sMenuInfo[theMenuID].subMenuStack[0] = SubMenuInfo(theMenuID);
		DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);
	}
}


void replaceMenu(u16 theMenuID, u16 theReplacementMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	if( theMenuID != InputMap::rootMenuOfMenu(theReplacementMenuID) )
	{
		logError(
			"Attempted to open sub-menu '%s' from menu '%s', "
			"but it is not a sub-menu of this root menu!",
			InputMap::menuLabel(theReplacementMenuID).c_str(),
			InputMap::menuLabel(theMenuID).c_str());
		return;
	}

	// Even if no actual change made, mark menu as having been interacted with
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gActiveHUD.size());
	gActiveHUD.set(sMenuInfo[theMenuID].hudElementID);

	// If this is already the active sub-menu, do nothing else
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	if( sMenuInfo[theMenuID].subMenuStack.back().id == theReplacementMenuID )
		return;

	// Going to change menus, will need to redraw
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
	gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);

	if( sMenuInfo[theMenuID].style == eMenuStyle_Slots )
	{
		// Want to retain selection of any "side menus", so check
		// if the requested menu has already been opened and, if so,
		// just bump it as-is to the top of the stack.
		for(std::vector<SubMenuInfo>::iterator itr =
				sMenuInfo[theMenuID].subMenuStack.begin();
			itr != sMenuInfo[theMenuID].subMenuStack.end(); ++itr)
		{
			if( itr->id == theReplacementMenuID )
			{
				const SubMenuInfo aSideMenuInfo = *itr;
				sMenuInfo[theMenuID].subMenuStack.erase(itr);
				sMenuInfo[theMenuID].subMenuStack.push_back(aSideMenuInfo);
				return;
			}
		}
		// If doesn't exist, push it on to the stack like a sub-menu,
		// so we retain the .selection value of the original menu.
		// Note that this does cause closeLastSubMenu() to act a little
		// weird with this menu style, but its not really intended to use
		// a "back" button or have normal sub-menus anyway.
		sMenuInfo[theMenuID].subMenuStack.push_back(
			SubMenuInfo(theReplacementMenuID));
	}
	else
	{
		// Replace current sub-menu id with new sub-menu id directly
		DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
		SubMenuInfo& aSubMenu = sMenuInfo[theMenuID].subMenuStack.back();
		aSubMenu.id = theReplacementMenuID;
		// Clamp selection to new sub-menu's max
		const u16 anItemCount = InputMap::menuItemCount(theReplacementMenuID);
		if( aSubMenu.selected >= anItemCount )
			aSubMenu.selected = anItemCount - 1;
	}

	// Need to redraw new menu items
	DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
	gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);
}


void reset(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	if( sMenuInfo[theMenuID].subMenuStack.size() > 1 ||
		sMenuInfo[theMenuID].subMenuStack.back().id != theMenuID ||
		sMenuInfo[theMenuID].subMenuStack.back().selected != 0 )
	{
		sMenuInfo[theMenuID].subMenuStack.clear();
		sMenuInfo[theMenuID].subMenuStack.push_back(SubMenuInfo(theMenuID));
		DBG_ASSERT(sMenuInfo[theMenuID].hudElementID < gRedrawHUD.size());
		gRedrawHUD.set(sMenuInfo[theMenuID].hudElementID);
	}
}


u16 activeSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	return sMenuInfo[theMenuID].subMenuStack.back().id;
}


u16 selectedItem(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	return sMenuInfo[theMenuID].subMenuStack.back().selected;
}


u16 itemCount(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	return InputMap::menuItemCount(
		sMenuInfo[theMenuID].subMenuStack.back().id);
}

} // Menus
