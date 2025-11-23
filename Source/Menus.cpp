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
// Const Data
//------------------------------------------------------------------------------

struct SubMenuState
{
	u16 id;
	s8 selected;
	s8 depth;
	SubMenuState(int theRootMenuID)
		: id(dropTo<u16>(theRootMenuID)), depth()
	{
		selected = dropTo<s8>(InputMap::menuDefaultItemIdx(theRootMenuID));
	}
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

typedef std::vector<SubMenuState> SubMenuStack;
std::vector<SubMenuStack> sMenuStacks;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static int getMenuOverlayID(int theRootMenuID)
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
	DBG_ASSERT(!sMenuStacks[anOverlayID].empty());
	return anOverlayID;
}


static int activeMenuItemCount(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	return InputMap::menuItemCount(aMenuStack.back().id);
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(sMenuStacks.empty());
	const int kMenuStackCount = InputMap::menuOverlayCount();
	sMenuStacks.reserve(kMenuStackCount);
	sMenuStacks.resize(kMenuStackCount);
	for(int anOverlayID = 0; anOverlayID < kMenuStackCount; ++anOverlayID)
	{
		const int aRootMenuID = InputMap::overlayRootMenuID(anOverlayID);
		sMenuStacks[anOverlayID].push_back(SubMenuState(aRootMenuID));
	}
}


void cleanup()
{
	sMenuStacks.clear();
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.containsPrefix("Menu.") )
	{
		// Clamp selection for all sub-menus in case an item was removed
		for(int aMenuStackID = 0, end = InputMap::menuOverlayCount();
			aMenuStackID < end; ++aMenuStackID)
		{
			SubMenuStack& aSubMenuStack = sMenuStacks[aMenuStackID];
			for(int i = 0, end = intSize(aSubMenuStack.size()); i < end; ++i)
			{
				aSubMenuStack[i].selected = clamp(
					aSubMenuStack[i].selected, 0,
					InputMap::menuItemCount(aSubMenuStack[i].id)-1);
			}
		}
	}
}


Command selectedMenuItemCommand(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(anOverlayID);

	Command result;
	const int aSubMenuID = aMenuStack.back().id;
	const int aSelection = aMenuStack.back().selected;
	if( InputMap::menuStyle(aSubMenuID) == eMenuStyle_4Dir )
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
		gConfirmedMenuItem[anOverlayID] = dropTo<u16>(aSelection);
	}

	return result;
}


Command selectMenuItem(
	int theRootMenuID,
	ECommandDir theDir,
	bool wrap,
	bool repeat)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];

	SubMenuState& aSubMenu = aMenuStack.back();
	const int aSubMenuID = aSubMenu.id;
	const int anItemCount = InputMap::menuItemCount(aSubMenuID);
	int aSelection = aSubMenu.selected;
	bool pushedPastEdge = false;

	Command aDirCmd = InputMap::commandForMenuDir(aSubMenuID, theDir);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(anOverlayID);

	const EMenuStyle aMenuStyle = InputMap::menuStyle(aSubMenuID);
	const int aGridWidth = InputMap::menuGridWidth(aSubMenuID);
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
	case eMenuStyle_SelectHotspot:
		{
			const HotspotMap::Links& aLinkMap =
				HotspotMap::getLinks(aSubMenuID);
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
			gConfirmedMenuItem[anOverlayID] = dropTo<u16>(theDir);
		break;
	}

	aSelection = min(aSelection, anItemCount-1);
	if( !pushedPastEdge )
		aDirCmd = Command();

	if( aDirCmd.type == eCmdType_OpenSubMenu && aMenuStyle != eMenuStyle_4Dir )
	{
		// Requests to open sub-menus with directionals in most menu styles
		// should use swap menu instead, meaning they'll stay on the same
		// "level" as the previous menu instead of being a "child" menu.
		// They also should ignore wrapping and keep previous selection
		aSelection = aSubMenu.selected;
		aDirCmd.type = eCmdType_SwapMenu;
		aDirCmd.swapDir = theDir;
	}

	if( aSelection != aSubMenu.selected )
	{
		aSubMenu.selected = dropTo<s8>(aSelection);
		// Need to refresh to show selection differently
		gRefreshOverlays.set(anOverlayID);
	}

	return aDirCmd;
}


Command openSubMenu(int theRootMenuID, int theSubMenuID)
{
	DBG_ASSERT(theRootMenuID == InputMap::rootMenuOfMenu(theSubMenuID));
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	const int oldMenuItemCount = activeMenuItemCount(theRootMenuID);

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(anOverlayID);

	// If this is already the active sub-menu, do nothing else
	if( aMenuStack.back().id == theSubMenuID )
		return Command();

	// Push new menu on to the stack
	const int aStackDepth = aMenuStack.back().depth;
	aMenuStack.push_back(SubMenuState(theSubMenuID));
	aMenuStack.back().depth = dropTo<s8>(aStackDepth + 1);

	// Need full redraw of new menu items
	gFullRedrawOverlays.set(anOverlayID);

	// Might need to reshape menu for new menu item count
	if( oldMenuItemCount != InputMap::menuItemCount(theSubMenuID) )
		gReshapeOverlays.set(anOverlayID);

	return InputMap::menuAutoCommand(theSubMenuID);
}


Command swapMenu(int theRootMenuID, int theAltMenuID, ECommandDir theDir)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	const int oldMenuItemCount = activeMenuItemCount(theRootMenuID);

	if( theRootMenuID != InputMap::rootMenuOfMenu(theAltMenuID) )
	{
		logError(
			"Attempted to open sub-menu '%s' from menu '%s', "
			"but it is not a sub-menu of this root menu!",
			InputMap::menuLabel(theAltMenuID).c_str(),
			InputMap::menuLabel(theRootMenuID).c_str());
		return Command();
	}

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(anOverlayID);

	// If this is already the active sub-menu, do nothing else
	if( aMenuStack.back().id == theAltMenuID )
		return Command();

	// Going to change menus, will need a full redraw
	gFullRedrawOverlays.set(anOverlayID);

	int aNextSel = aMenuStack.back().selected;
	const int aStackDepth = aMenuStack.back().depth;
	switch(InputMap::menuStyle(aMenuStack.back().id))
	{
	case eMenuStyle_Slots:
		// Want to retain selection of any "side menus", so check
		// if the requested menu has already been opened and, if so,
		// just bump it as-is to the top of the stack.
		for(SubMenuStack::iterator itr = aMenuStack.begin();
			itr != aMenuStack.end(); ++itr)
		{
			if( itr->id == theAltMenuID )
			{
				const SubMenuState aSideMenuState = *itr;
				aMenuStack.erase(itr);
				aMenuStack.push_back(aSideMenuState);
				if( oldMenuItemCount != InputMap::menuItemCount(theAltMenuID) )
					gReshapeOverlays.set(anOverlayID);
				return InputMap::menuAutoCommand(aSideMenuState.id);
			}
		}
		// If doesn't exist, push it on to the stack like a sub-menu,
		// so we retain the .selection value of the original menu.
		aMenuStack.push_back(
			SubMenuState(theAltMenuID));
		aMenuStack.back().depth = dropTo<s8>(aStackDepth);
		if( oldMenuItemCount != InputMap::menuItemCount(theAltMenuID) )
			gReshapeOverlays.set(anOverlayID);
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
			const int anOldGridWidth = gridWidth(theRootMenuID);
			const int anOldX = aNextSel % anOldGridWidth;
			const int anOldY = aNextSel / anOldGridWidth;
			const u16 anOldMenuID = aMenuStack.back().id;
			aMenuStack.back().id = dropTo<u16>(theAltMenuID);
			const int aNewGridWidth = gridWidth(theRootMenuID);
			const int aNewGridHeight = gridHeight(theRootMenuID);
			const int aNewItemCount = InputMap::menuItemCount(theAltMenuID);
			aMenuStack.back().id = anOldMenuID;
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
	aMenuStack.back().id = dropTo<u16>(theAltMenuID);
	aMenuStack.back().selected = dropTo<s8>(aNextSel);
	if( oldMenuItemCount != InputMap::menuItemCount(theAltMenuID) )
		gReshapeOverlays.set(anOverlayID);

	return InputMap::menuAutoCommand(theAltMenuID);
}


Command closeLastSubMenu(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	const int oldMenuItemCount = activeMenuItemCount(theRootMenuID);
	Command result;

	// Even if no actual change made, mark menu as having been interacted with
	gActiveOverlays.set(anOverlayID);

	// If already at root, do nothing
	if( aMenuStack.size() == 1 &&
		aMenuStack[0].id == theRootMenuID )
		return result;

	const s16 aStackDepth = aMenuStack.back().depth;
	if( aStackDepth == 0 )
	{
		if( InputMap::menuStyle(aMenuStack.back().id) == eMenuStyle_Slots )
		{// Swap back to root menu
			if( aMenuStack.back().id == theRootMenuID )
				return result;
			swapMenu(theRootMenuID, theRootMenuID, eCmdDir_None);
			result =
				InputMap::menuAutoCommand(aMenuStack.back().id);
			// Make sure even if command is empty caller knows change happened
			if( result.type == eCmdType_Invalid )
				result.type = eCmdType_Unassigned;
			return result;
		}
		// Reset back to root menu
		result = reset(theRootMenuID);
		return result;
	}

	// Remove all sub menus at same depth as current to pop back to parent
	while(aMenuStack.back().depth == aStackDepth)
		aMenuStack.pop_back();
	gFullRedrawOverlays.set(anOverlayID);
	if( oldMenuItemCount != InputMap::menuItemCount(aMenuStack.back().id) )
		gReshapeOverlays.set(anOverlayID);
	result =
		InputMap::menuAutoCommand(aMenuStack.back().id);
	// Make sure even if command is empty caller knows change happened
	if( result.type == eCmdType_Invalid )
		result.type = eCmdType_Unassigned;
	return result;
}


Command reset(int theRootMenuID, int toItemNo)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	const int oldMenuItemCount = activeMenuItemCount(theRootMenuID);
	Command result;

	if( toItemNo > 0 )
		toItemNo -= 1;
	else
		toItemNo = InputMap::menuDefaultItemIdx(theRootMenuID);
	if( aMenuStack.size() > 1 ||
		aMenuStack.back().id != theRootMenuID ||
		aMenuStack.back().selected != toItemNo )
	{
		aMenuStack.clear();
		aMenuStack.push_back(SubMenuState(theRootMenuID));
		aMenuStack.back().selected = dropTo<s8>(
			min(toItemNo, InputMap::menuItemCount(theRootMenuID)-1));
		gFullRedrawOverlays.set(anOverlayID);
		if( oldMenuItemCount != InputMap::menuItemCount(aMenuStack.back().id) )
			gReshapeOverlays.set(anOverlayID);
		result =
			InputMap::menuAutoCommand(theRootMenuID);
		// Make sure even if command is empty caller knows change happened
		if( result.type == eCmdType_Invalid )
			result.type = eCmdType_Unassigned;
	}

	return result;
}


Command autoCommand(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	return InputMap::menuAutoCommand(aMenuStack.back().id);
}


Command backCommand(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	return InputMap::menuBackCommand(aMenuStack.back().id);
}


Command closeCommand(int theRootMenuID)
{
	DBG_ASSERT(theRootMenuID == InputMap::rootMenuOfMenu(theRootMenuID));
	return InputMap::menuBackCommand(theRootMenuID);
}


void editMenuItem(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];

	const int aSubMenuID = aMenuStack.back().id;
	const int anItemIdx = selectedItem(theRootMenuID);
	const std::string& aMenuProfileName =
		InputMap::menuSectionName(aSubMenuID);
	std::string aMenuItemCmd = Profile::getStr(
		aMenuProfileName, InputMap::menuItemKeyName(anItemIdx));
	bool allowInsert = true;
	switch(InputMap::menuStyle(aSubMenuID))
	{
	case eMenuStyle_4Dir:
	case eMenuStyle_Hotspots:
	case eMenuStyle_SelectHotspot:
	case eMenuStyle_Visual:
	case eMenuStyle_Label:
	case eMenuStyle_KBCycleLast:
	case eMenuStyle_KBCycleDefault:
		allowInsert = false;
		break;
	}
	if( Dialogs::editMenuCommand(aMenuItemCmd, true) == eResult_Ok )
	{
		if( allowInsert && aMenuItemCmd[0] == '+' )
		{// Insert as new menu item after current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			for(int i = activeMenuItemCount(theRootMenuID);
				i > anItemIdx+1; --i)
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
		else if( allowInsert && aMenuItemCmd[0] == '-' )
		{// Insert as new menu item before current
			aMenuItemCmd = trim(&aMenuItemCmd[1]);
			if( aMenuItemCmd.empty() ) aMenuItemCmd = ":";
			for(int i = activeMenuItemCount(theRootMenuID);
				i > anItemIdx; --i)
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
		else if( allowInsert && aMenuItemCmd.empty() )
		{// Remove menu item
			const int aMenuItemCount = activeMenuItemCount(theRootMenuID);
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
	gActiveOverlays.set(anOverlayID);
}


void editMenuItemDir(int theRootMenuID, ECommandDir theDir)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];

	const int aSubMenuID = aMenuStack.back().id;
	const std::string& aMenuProfileName =
		InputMap::menuSectionName(aSubMenuID);
	std::string aMenuItemCmd = Profile::getStr(
		aMenuProfileName, InputMap::menuItemDirKeyName(theDir));
	if( Dialogs::editMenuCommand(aMenuItemCmd, false) == eResult_Ok )
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
		gFullRedrawOverlays.set(anOverlayID);
		// Should just save this out immediately
		Profile::saveChangesToFile();
	}
	gActiveOverlays.set(anOverlayID);
}


int activeMenuForOverlayID(int theOverlayID)
{
	DBG_ASSERT(size_t(theOverlayID) < sMenuStacks.size());
	return sMenuStacks[theOverlayID].back().id;
}


int selectedItem(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];

	return aMenuStack.back().selected;
}


int gridWidth(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	return InputMap::menuGridWidth(
		aMenuStack.back().id);
}


int gridHeight(int theRootMenuID)
{
	const int anOverlayID = getMenuOverlayID(theRootMenuID);
	const SubMenuStack& aMenuStack = sMenuStacks[anOverlayID];
	return InputMap::menuGridHeight(
		aMenuStack.back().id);
}

} // Menus
