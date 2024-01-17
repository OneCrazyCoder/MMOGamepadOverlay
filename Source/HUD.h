//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Renders Heads-Up Display (HUD) elements on the overlay window.
	Handles the visual representation of status indicators, button prompts,
	and other user interface components.
*/


#include "Common.h"

namespace HUD
{

void init();
void cleanup();

void update();
void render(HWND hWnd, RECT theClientRect);

} // HUD
