//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#pragma once

/*
	Tracks and updates the current state of all active menus based on input
	from InputTranslator and configuration from InputMap/Profile.

	Each menu has a Style that defines both its behavior (how menu-related
	commands are interpreted) and its visual layout (used by WindowPainter).

	Commands only reference the root menu ID's directly, so this module uses
	root menu ID's externally and tracks the currently active sub-menu for
	each root menu's stack stored internally.
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

// Returns Command to execute for the selected menu item of given menu
Command selectedMenuItemCommand(int theRootMenuID);

// Changes currently selected menu item.
// In some menu styles, the act of selecting a menu item should also
// trigger an associated Command to execute immediately, which is returned.
Command selectMenuItem(int theRootMenuID, ECommandDir, bool wrap, bool repeat);

// Opens a sub-menu, meaning all references to the menu ID will actually refer
// to the sub-menu's data instead untli it is closed. Returns its Auto command.
Command openSubMenu(int theRootMenuID, int theSubMenuID);

// Similar result to openSubMenu but replaces current menu entirely instead
// of adding it to menu stack, changing how closeLastSubMenu() below behaves.
// Note that this does NOT trigger the "Back" command of replaced menu!
Command swapMenu(int theRootMenuID, int theAltMenuID, ECommandDir theDir);

// Removes the most recently-added sub-menu (via openSubMenu), returning to
// whichever sub-menu (or the root menu) was active before then.
// Returned command will be _Empty if were already at the root menu and thus
// did nothing, otherwise will be the Auto command of the now-active parent
// menu (which will at least be of type _Unassigned rather than _Empty)
Command closeLastSubMenu(int theRootMenuID);

// Resets menu to its default state (closes all sub-menus & resets selection),
// but does NOT trigger this as "activating" the menu in terms of alpha fade.
// Return value works like closeLastSubMenu (returns _Empty if did nothing).
// If toItemNo > 0, selects that item (1-based index) instead of default.
Command reset(int theRootMenuID, int toItemNo = 0);

// Returns Auto command of menu's currently-active sub-menu
Command autoCommand(int theRootMenuID);

// Returns Back command of menu's currently-active sub-menu
Command backCommand(int theRootMenuID);

// Returns Back command of root menu, which should "close" the menu by
// removing whatever layer controls it (that's the intent anyway)
Command closeCommand(int theRootMenuID);

// Prompts user for new label & command for currently selected menu item
void editMenuItem(int theRootMenuID);

// Prompts user for new label & command for directional menu item in theDir
void editMenuItemDir(int theRootMenuID, ECommandDir theDir);

// Returns current active (displayed) menuID for given overlay (window) ID
int activeMenuForOverlayID(int theOverlayID);

// Returns if active menu for given overlay ID actually accepts menu commands
bool overlayMenuAcceptsCommands(int theOverlayID);

// Returns selected item index for given menu
int selectedItem(int theRootMenuID);

// Returns grid width and height for grid-style menus
int gridWidth(int theRootMenuID);
int gridHeight(int theRootMenuID);

} // Menus
