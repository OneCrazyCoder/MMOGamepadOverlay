//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Creates and manages the app main window & transparent overlay windows.
	Handles basic window functionality such as event processing (including the
	paint events sent to the HUD module), positioning to match the target game
	window, and alpha fading HUD overlay windows in and out when requested.
	Also converts between different coordinate systems like window-relative to
	virtual-desktop-relative for hotspots and mouse cursor positions.

	Note that there is no single "Overlay Window" overlapping target game.
	Instead, each HUD Element has its own small overlay window, managed by this
	module, alongside the small (or invisible) normal app window just for
	allowing the user to close this app (or to have it recognized by other
	programs like Discord to make it think this is a game being played, in place
	of older target clients like EverQuest that aren't recognized by Discord).
*/

#include "Common.h"

namespace WindowManager
{

// Create the main window and begin window management
void createMain(HINSTANCE);

// Create the overlay windows needed based on number of HUD elements defined
void createOverlays(HINSTANCE);

// Destroy all windows
void destroyAll(HINSTANCE);

// Update window contents as needed
void update();

// Checks if given window handle is owned by this app or not
bool isOwnedByThisApp(HWND theWindow);

// Gets handle to the main window (or NULL)
HWND mainHandle();

// Move/resize/hide/restore the overlay windows
void resize(RECT theNewTargetRect);
void hideOverlays();
void showOverlays();
void setOverlaysToTopZ();
bool overlaysAreHidden();

// Gets overlay-window-relative/clamped mouse position
POINT mouseToOverlayPos(bool clamped = true);
// Converts a hotspot into overlay-window-relative position
POINT hotspotToOverlayPos(const Hotspot& theHotspot);
// Converts overlay-window-relative position into a hotspot
Hotspot overlayPosToHotspot(POINT theMousePos);
// Converts overlay-window-relative mouse position into a
// virtual-desktop-relative 0-65535 normalized pos for SendInput
POINT overlayPosToNormalizedMousePos(POINT theMousePos);
// Converts above back to overlay-window-relative mouse position
POINT normalizedMouseToOverlayPos(POINT theSentMousePos);
// Calculates center-point pos of a menu item as a hotspot
Hotspot hotspotForMenuItem(u16 theMenuID, u16 theMenuItemIdx);

} // WindowManager
