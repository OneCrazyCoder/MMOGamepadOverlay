//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Renders Heads-Up Display (HUD) elements on the overlay window.
	Handles the visual representation of menus, button prompts,
	error messages, and other user interface components.
*/


#include "Common.h"

namespace HUD
{

void init();
void cleanup();

void update();
void render(HWND hWnd, RECT theClientRect);

} // HUD
