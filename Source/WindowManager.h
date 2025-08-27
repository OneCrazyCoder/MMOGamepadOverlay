//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Creates and manages the main app window and the transparent overlays.
	Handles basic window functionality such as event dispatch, paint messaging
	(to WindowPainter), and alpha fading of overlay windows in/out as needed.

	Also handles window sizing and positioning relative to the target game
	window, and coordinate system conversions (e.g., window-relative to
	virtual-desktop-relative for hotspots and mouse cursor positions).

	Note: There is no single full-screen overlay. Instead, each (root) menu has
	its own individual overlay window, along with the small (or hidden) app
	window for user interaction, taskbar visibility, and compatibility with
	tools like Discord that detect running applications.
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
// Can also specify a specific overlay window to keep visible alognside it.
// Only one of these can be active at a time!
HWND createToolbarWindow(int theResID, DLGPROC, int theOverlayID = -1);
void destroyToolbarWindow();

// Adds callback functions for drawing and getting messages for the System
// overlay window (the top-most, full-sized one) for editor functionality.
// Allows non-transparent pixels of the overlay to get mouse click messages.
// Said overlay will stay visible, but gRefreshHUD must be used for redraws.
// Set the callbacks to NULL to stop this behaviour and clear/hide the window.
typedef void (*SystemPaintFunc)(HDC, const RECT&, bool firstDraw);
void setSystemOverlayCallbacks(WNDPROC, SystemPaintFunc);

// Gets overlay-relative/clamped mouse position
POINT mouseToOverlayPos(bool clamped = true);
// Converts a hotspot into overlay-relative position
POINT hotspotToOverlayPos(const Hotspot& theHotspot);
// Converts overlay-relative position into a hotspot
Hotspot overlayPosToHotspot(POINT thePos);
// Converts overlay-relative mouse position into a
// virtual-desktop-relative 0-65535 normalized pos for SendInput
POINT overlayPosToNormalizedMousePos(POINT theMousePos);
// Converts above back to overlay-relative mouse position
POINT normalizedMouseToOverlayPos(POINT theSentMousePos);
// Calculates center-point pos of a menu item as a hotspot
Hotspot hotspotForMenuItem(int theRootMenuID, int theMenuItemIdx);
// Returns overlay-relative RECT of given individual overlay
RECT overlayRect(int theOverlayID);

} // WindowManager
