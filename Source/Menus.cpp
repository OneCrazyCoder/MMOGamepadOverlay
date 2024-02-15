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
	SubMenuInfo(u16 id = 0, u16 sel = 0) : id(id), selected(sel) {}
};

struct MenuInfo
{
	EHUDType style;
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
		sMenuInfo[i].subMenuStack.push_back(SubMenuInfo(i));
	}
}


void cleanup()
{
	sMenuInfo.clear();
}


const Command& selectedMenuItemCommand(u16 theMenuID)
{
	const u16 aSubMenuID = activeSubMenu(theMenuID);
	const u16 aSelection = sMenuInfo[theMenuID].subMenuStack.back().selected;
	gActiveHUD.set(theMenuID);
	if( sMenuInfo[theMenuID].style == eMenuStyle_4Dir )
		return sEmptyMenuCommand;
	return InputMap::commandForMenuItem(aSubMenuID, aSelection);
}


const Command& selectMenuItem(u16 theMenuID, ECommandDir theDir)
{
	SubMenuInfo& aSubMenu = sMenuInfo[theMenuID].subMenuStack.back();
	const u16 aSubMenuID = aSubMenu.id;
	u16& aSelection = aSubMenu.selected;
	const u16 aPrevSel = aSelection;
	const u16 anItemCount = InputMap::menuItemCount(aSubMenuID);
	gActiveHUD.set(theMenuID);
	switch(sMenuInfo[theMenuID].style)
	{
	case eMenuStyle_List:
		switch(theDir)
		{
		case eCmdDir_L:
			aSelection = 0;
			break;
		case eCmdDir_R:
			aSelection = anItemCount - 1;
			break;
		case eCmdDir_U:
			if( anItemCount > 2 )
				aSelection = decWrap(aSelection, anItemCount);
			else // wrapping can be confusing when only have 2 menu items...
				aSelection = 0;
			break;
		case eCmdDir_D:
			if( anItemCount > 2 )
				aSelection = incWrap(aSelection, anItemCount);
			else
				aSelection = anItemCount - 1;
			break;
		}
		break;
	case eMenuStyle_4Dir:
		return InputMap::commandForMenuItem(aSubMenuID, theDir);
	}

	if( aSelection != aPrevSel )
		gRedrawHUD.set(theMenuID);
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
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	if( sMenuInfo[theMenuID].subMenuStack.back().id != theSubMenuID )
	{
		sMenuInfo[theMenuID].subMenuStack.push_back(SubMenuInfo(theSubMenuID));
		DBG_ASSERT(theMenuID < gRedrawHUD.size());
		gRedrawHUD.set(theMenuID);
		gActiveHUD.set(theMenuID);
	}
}


void closeLastSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	if( sMenuInfo[theMenuID].subMenuStack.size() > 1 )
	{
		sMenuInfo[theMenuID].subMenuStack.pop_back();
		DBG_ASSERT(theMenuID < gRedrawHUD.size());
		gRedrawHUD.set(theMenuID);
		gActiveHUD.set(theMenuID);
	}
}


void reset(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	if( sMenuInfo[theMenuID].subMenuStack.size() > 1 )
	{
		sMenuInfo[theMenuID].subMenuStack.resize(1);
		DBG_ASSERT(theMenuID < gRedrawHUD.size());
		gRedrawHUD.set(theMenuID);
	}
	if( sMenuInfo[theMenuID].subMenuStack[0].selected != 0 )
	{
		sMenuInfo[theMenuID].subMenuStack[0].selected = 0;
		DBG_ASSERT(theMenuID < gRedrawHUD.size());
		gRedrawHUD.set(theMenuID);
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
