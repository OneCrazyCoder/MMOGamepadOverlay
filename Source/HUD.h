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

// Update visual element timers (animations etc)
void update();

// Draws given HUD element to given Device Context (bitmap), starting at 0,0
void drawElement(
	HDC hdc,
	u16 theHUDElementID,
	const SIZE& theComponentSize,
	const SIZE& theDestSize,
	bool needsInitialErase);

// Updates layout properties needed for each HUD element's overlay Window
void updateWindowLayout(
	u16 theHUDElementID,
	const SIZE& theTargetSize,
	SIZE& theComponentSize,
	POINT& theWindowPos,
	SIZE& theWindowSize);

// Returns background color to become fully transparent
COLORREF transColor(u16 theHUDElementID);

} // HUD
