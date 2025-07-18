//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Updates the MenuState of any active menus via InputTranslator, according
	to the menu configuration set up by InputMap reading current Profile.
	Each Menu has a Style which determines how the various menu-related
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

// Update current menu status if profile changes at runtime
void loadProfileChanges();

// NOTE: theMenuID should refer to a ROOT menu in all of the below,
// meaning the menu ID used in any commands that act on menus, not
// on the currently-active sub-menu ID.

// Returns Command to execute for the selected menu item of given menu
Command selectedMenuItemCommand(int theMenuID);

// Changes currently selected menu item.
// In some menu styles, the act of selecting a menu item should also
// trigger an associated Command to execute immediately, which is returned.
Command selectMenuItem(int theMenuID, ECommandDir, bool wrap, bool repeat);

// Opens a sub-menu, meaning all references to the menu ID will actually refer
// to the sub-menu's data instead untli it is close. Returns its Auto command.
Command openSubMenu(int theMenuID, int theSubMenuID);

// Similar result to openSubMenu but replaces current menu entirely instead
// of adding it to menu stack, changing how closeLastSubMenu() below behaves.
// Note that this does NOT trigger the "Back" command of replaced menu!
Command swapMenu(int theMenuID, int theAltMenuID, ECommandDir theDir);

// Removes the most recently-added sub-menu (via openSubMenu), returning to
// whichever sub-menu (or the root menu) was active before then.
// Returned command will be _Empty if were already at the root menu and thus
// did nothing, otherwise will be the Auto command of the now-active parent
// menu (which will at least be of type _Unassigned rather than _Empty)
Command closeLastSubMenu(int theMenuID);

// Resets menu to its default state (closes all sub-menus & resets selection),
// but does NOT trigger this as "activating" the menu in terms of alpha fade.
// Return value works like closeLastSubMenu (returns _Empty if did nothing).
Command reset(int theMenuID, int toItemNo = 1);

// Returns Auto command of menu's currently-active sub-menu
Command autoCommand(int theMenuID);

// Returns Back command of menu's currently-active sub-menu
Command backCommand(int theMenuID);

// Returns Back command of root menu, which should "close" the menu by
// removing whatever layer controls it (that's the intent anyway)
Command closeCommand(int theMenuID);

// Prompts user for new label & command for currently selected menu item
void editMenuItem(int theMenuID);

// Prompts user for new label & command for directional menu item in theDir
void editMenuItemDir(int theMenuID, ECommandDir theDir);

// Returns current active sub-menu of given root menu
int activeSubMenu(int theMenuID);

// Returns selected item of currently-active sub-menu of given root menu
int selectedItem(int theMenuID);

// Returns item count for currently-active sub-menu of given root menu
int itemCount(int theMenuID);

// Returns grid width and height for grid-style menus
int gridWidth(int theMenuID);
int gridHeight(int theMenuID);

} // Menus
