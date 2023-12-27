//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Creates and manages the transparent overlay window.
	Handles basic window functionality such as event processing (including the
	paint event sent to the HUD module), positioning to match the target game
	window, and alpha fading the window in and out when requested.
*/

#include "Common.h"

namespace OverlayWindow
{

// Create the window
void create(HINSTANCE);

// Destroy the window
void destroy();

// Load configuration settings from current profile
void loadProfile();

// Update contents that aren't updated by Windows messages
void update();

// Marks the window as needing to be redrawn
void redraw();

extern HWND gHandle;

} // OverlayWindow