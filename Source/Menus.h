//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Updates the MenuState of any active menus via InputTranslator, according
	to the menu configuration set up by InputMap reading current Profile.
	Each Menu has a Style which determines how the variosu menu-related
	commands should be interpreted (as well as how the menu will be displayed
	as a HUD element).
*/

#include "Common.h"

namespace Menus
{

// Initialize menus based on data already parsed by InputMap
void init();

// Resets all menu's states
void cleanup();

// Returns Command to execute for the selected menu item of given menu
const Command& selectedMenuItemCommand(u16 theMenuID);

// Changes currently selected menu item.
// In some menu styles, the act of selecting a menu item should also
// trigger an associated Command to execute immediately, which is returned.
const Command& selectMenuItem(u16 theMenuID, ECommandDir theDir);

// Opens a sub-menu, meaning all references to the menu ID will actually refer
// to the sub-menu's data instead untli it is closed.
void openSubMenu(u16 theMenuID, u16 theSubMenuID);

// Removes the most recently-added sub-menu from openSubMenu, returning to
// whichever sub-menu (or the root menu) was active before then.
void closeLastSubMenu(u16 theMenuID);

// Returns menu to its default state (closes all sub-menus & resets selection)
void reset(u16 theMenuID);

// Returns current active sub-menu of given root menu
u16 activeSubMenu(u16 theMenuID);

} // Menus
