//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Creates and manages the transparent overlay window.
	Handles basic window functionality such as event processing (including the
	paint event sent to the HUD module), positioning to match the target game
	window, and alpha fading the window in and out when requested. Also
	converts between different coordinate systems like windows-relative to
	virtual-desktop-relative for hotspots and such.
*/

#include "Common.h"

namespace OverlayWindow
{

// Create the window
void create(HINSTANCE);

// Destroy the window
void destroy();

// Update contents that aren't updated by Windows messages
void update();

// Move/resize/minimize/restore the window
void resize(RECT theNewWindowRect); // undoes minimize() as well
void minimize();
void moveToTopZ();  // undoes minimize() as well
bool isMinimized();

// Marks the window as needing to be redrawn
void redraw();

// Gets virtual-desktop-relative mouse coordinates normalized to 0-65535 range
u16 hotspotMousePosX(const Hotspot& theHotspot);
u16 hotspotMousePosY(const Hotspot& theHotspot);

extern HWND gHandle;

} // OverlayWindow