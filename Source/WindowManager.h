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

// Load profile changes that might affect windows (UIScale in particular!)
void loadProfileChanges();

// Update window contents as needed
void update();

// Disables main window and hides overlays until endDialogMode() or update()
void prepareForDialog();
void endDialogMode();

// Stops main window manually updating modules in modal mode (resizing window)
void stopModalModeUpdates();

// Checks if given window handle is owned by this app or not
bool isOwnedByThisApp(HWND theWindow);

// Gets handle to the main window (or NULL)
HWND mainHandle();

// Gets handle to active editor toolbar window (or NULL)
HWND toolbarHandle();

// Checks for situations where mouse must be visible and directly controlled
// (not hidden in the corner or attempting to use mouse look mode) due to
// current status of UI (i.e. not actively in-game at the moment).
bool requiresNormalCursorControl();

// Move/resize/hide/restore the overlay windows
void resize(RECT theNewTargetRect, bool isTargetAppWindow);
void resetOverlays();
void hideOverlays();
void showOverlays();
void setOverlaysToTopZ();
bool overlaysAreHidden();
SIZE overlayTargetSize();
RECT overlayClipRect();

// Updates gUIScale read in from Profile and possibly window size
//void updateUIScale();

// Displays a visual indicater that are tracking a target window now
void showTargetWindowFound();

// Creates a floating toolbar window placed on top of the overlays
// Can also specify a HUD element window to keep visible alognside it.
// Only one of these can be active at a time!
HWND createToolbarWindow(int theResID, DLGPROC, int theHUDElementID = -1);
void destroyToolbarWindow();

// Adds callback functions for drawing and getting messages for the System
// overlay window (the top-most, full-sized one) for editor functionality.
// Allows non-transparent pixels of the overlay to get mouse click messages.
// Said overlay will stay visible, but gRefreshHUD must be used for redraws.
// Set the callbacks to NULL to stop this behaviour and clear/hide the window.
typedef void (*SystemPaintFunc)(HDC, const RECT&, bool firstDraw);
void setSystemOverlayCallbacks(WNDPROC, SystemPaintFunc);

// Gets overlay-window-relative/clamped mouse position
POINT mouseToOverlayPos(bool clamped = true);
// Converts a hotspot into overlay-window-relative position
POINT hotspotToOverlayPos(const Hotspot& theHotspot);
// Converts overlay-window-relative position into a hotspot
Hotspot overlayPosToHotspot(POINT thePos);
// Converts overlay-window-relative mouse position into a
// virtual-desktop-relative 0-65535 normalized pos for SendInput
POINT overlayPosToNormalizedMousePos(POINT theMousePos);
// Converts above back to overlay-window-relative mouse position
POINT normalizedMouseToOverlayPos(POINT theSentMousePos);
// Calculates center-point pos of a menu item as a hotspot
Hotspot hotspotForMenuItem(u16 theMenuID, u16 theMenuItemIdx);
// Returns overlay-window-relative RECT of given HUD element
RECT hudElementRect(u16 theHUDElementID);

} // WindowManager
