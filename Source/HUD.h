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

// Load Profile data and create fonts, bitmaps, etc.
void init();

// Load profile changes and update HUD visuals to match
void loadProfileChanges();

// Free resources such as pens and fonts
void cleanup();

// Update visual element timers (animations etc)
void update();

// Refreshes cached drawing data related to target window size
void updateScaling();

// Re-loads copy-from-target-region icon data from Profile for given label
void reloadCopyIconLabel(const std::string& theCopyIconLabel);

// Re-loads given HUD element's default position and size from Profile
void reloadElementShape(int theHUDElementID);

// Draws given HUD element to given Device Context (bitmap), starting at 0,0
void drawElement(
	HDC hdc,
	HDC hCaptureDC,
	const POINT& theCaptureOffset,
	const SIZE& theTargetSize,
	int theHUDElementID,
	const std::vector<RECT>& theComponents,
	bool needsInitialErase);

// Updates layout properties needed for each HUD element's overlay Window
void updateWindowLayout(
	int theHUDElementID,
	const SIZE& theTargetSize,
	std::vector<RECT>& theComponents,
	POINT& theWindowPos,
	SIZE& theWindowSize,
	const RECT& theTargetClipRect);

// Draws contents of main window (version string)
void drawMainWindowContents(HWND theWindow, bool asDisabled);

// Causes border of eHUDType_System to flash for a bit to show region
void flashSystemWindowBorder();

// Sets a draw hook for eHUDType_System and (keeps it visible if != NULL)
typedef void (*SystemPaintFunc)(HDC, const RECT&, bool firstDraw);
void setSystemOverlayDrawHook(SystemPaintFunc);
// Requests redraw eHUDType_System element
void redrawSystemOverlay(bool fullRedraw = false);

// Get alpha fade in/out information
u8 maxAlpha(int theHUDElementID);
u8 inactiveAlpha(int theHUDElementID);
int alphaFadeInDelay(int theHUDElementID);
double alphaFadeInRate(int theHUDElementID);
int alphaFadeOutDelay(int theHUDElementID);
double alphaFadeOutRate(int theHUDElementID);
int inactiveFadeOutDelay(int theHUDElementID);

// Returns background color to become fully transparent
COLORREF transColor(int theHUDElementID);

// Returns draw priority (which are on top of which)
int drawPriority(int theHUDElementID);

// Returns associated parent hotspot if have one
Hotspot parentHotspot(int theHUDElementID);

} // HUD
