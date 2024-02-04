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

struct MenuInfo
{
	u16 selected;
	EHUDType style;
	std::vector<u16> subMenuStack;

	MenuInfo() : selected() {}
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
	const u16 kMenuCount = InputMap::menuCount();
	sMenuInfo.reserve(kMenuCount);
	sMenuInfo.resize(kMenuCount);
	for(u16 i = 0; i < kMenuCount; ++i)
	{
		sMenuInfo[i].style = InputMap::menuStyle(i);
		sMenuInfo[i].subMenuStack.push_back(i);
	}
}


void cleanup()
{
	sMenuInfo.clear();
}


const Command& selectedMenuItemCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	gActiveHUD.set(theMenuID);
	switch(sMenuInfo[theMenuID].style)
	{
	case eMenuStyle_4Dir:
		return sEmptyMenuCommand;
	}
	return sEmptyMenuCommand;
}


const Command& selectMenuItem(u16 theMenuID, ECommandDir theDir)
{
	const u16 aSubMenuID = activeSubMenu(theMenuID);
	gActiveHUD.set(theMenuID);
	switch(sMenuInfo[theMenuID].style)
	{
	case eMenuStyle_4Dir:
		return InputMap::commandForMenuItem(aSubMenuID, theDir);
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
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	if( sMenuInfo[theMenuID].subMenuStack.back() != theSubMenuID )
	{
		sMenuInfo[theMenuID].subMenuStack.push_back(theSubMenuID);
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
}


u16 activeSubMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID == InputMap::rootMenuOfMenu(theMenuID));
	DBG_ASSERT(theMenuID < sMenuInfo.size());
	DBG_ASSERT(!sMenuInfo[theMenuID].subMenuStack.empty());
	return sMenuInfo[theMenuID].subMenuStack.back();
}

} // Menus
