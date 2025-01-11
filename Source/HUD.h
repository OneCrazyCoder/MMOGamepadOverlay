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

// Free resources such as pens and fonts
void cleanup();

// Update visual element timers (animations etc)
void update();

// Refreshes cached drawing data related to target window size
void updateScaling();

// Re-loads copy-from-target-region icon data from Profile for given label
void reloadCopyIconLabel(const std::string& theCopyIconLabel);

// Re-loads given HUD element's default position and size from Profile 
void reloadElementShape(u16 theHUDElementID);

// Draws given HUD element to given Device Context (bitmap), starting at 0,0
void drawElement(
	HDC hdc,
	HDC hCaptureDC,
	const POINT& theCaptureOffset,
	const SIZE& theTargetSize,
	u16 theHUDElementID,
	const std::vector<RECT>& theComponents,
	bool needsInitialErase);

// Updates layout properties needed for each HUD element's overlay Window
void updateWindowLayout(
	u16 theHUDElementID,
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
u8 maxAlpha(u16 theHUDElementID);
u8 inactiveAlpha(u16 theHUDElementID);
int alphaFadeInDelay(u16 theHUDElementID);
float alphaFadeInRate(u16 theHUDElementID);
int alphaFadeOutDelay(u16 theHUDElementID);
float alphaFadeOutRate(u16 theHUDElementID);
int inactiveFadeOutDelay(u16 theHUDElementID);

// Returns background color to become fully transparent
COLORREF transColor(u16 theHUDElementID);

// Returns draw priority (which are on top of which)
s8 drawPriority(u16 theHUDElementID);

// Returns associated parent hotspot if have one
Hotspot parentHotspot(u16 theHUDElementID);

} // HUD
