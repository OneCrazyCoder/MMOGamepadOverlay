//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Renders Heads-Up Display (HUD) elements to overlay windows.
	Handles the visual representation of menus, button prompts, etc.
	according to preferences set in Profile.
*/


#include "Common.h"

namespace HUD
{

// Load Profile data and create brushes, fonts, etc.
void init();

// Free resources such as brushes and fonts
void cleanup();

// Draws given HUD element to given Window
// Assumes the Window is already positioned correct so can draw at 0,0
void drawElement(
	HWND theWindow,
	u16 theHUDElementID,
	const POINT& theItemSize,
	const POINT& theClientSize);

// Get size of each menu item based on full target client size
POINT menuItemSize(
	u16 theHUDElementID,
	const POINT& theClientSize);

// Gets the region position/size for drawing HUD element
RECT elementRectNeeded(
	u16 theHUDElementID,
	const POINT& theItemSize,
	const RECT& theClientRect);

} // HUD
