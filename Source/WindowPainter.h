//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Paints the contents of overlay windows (and the main app window) using
	generated and stored GDI resources such as pens, brushes, and fonts.

	Translates current menu state and appearance preferences set in Profile
	into the final visual representation for each visible menu.
*/


#include "Common.h"

namespace WindowPainter
{

// Load Profile data and create fonts, bitmaps, etc.
void init();

// Load profile changes and update displayed graphics to match
void loadProfileChanges();

// Free resources such as pens and fonts
void cleanup();

// Update visual element timers (animations etc)
void update();

// Refreshes cached drawing data related to target window size
void updateScaling();

// Draws given menu to given Device Context (bitmap), starting at 0,0
void paintWindowContents(
	HDC hdc,
	HDC hCaptureDC,
	const POINT& theCaptureOffset,
	const SIZE& theTargetSize,
	int theMenuID,
	bool needsInitialErase);

// Updates layout properties needed for a menu's overlay window
void updateWindowLayout(
	int theMenuID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect,
	POINT& theWindowPos,
	SIZE& theWindowSize);

// Draws contents of main window (version string)
void paintMainWindowContents(HWND theWindow, bool asDisabled);

// Causes border of eMenuStyle_System to flash for a bit to show region
void flashSystemOverlayBorder();

// Sets a draw hook for eMenuStyle_System and (keeps it visible if != NULL)
typedef void (*SystemPaintFunc)(HDC, const RECT&, bool firstDraw);
void setSystemOverlayDrawHook(SystemPaintFunc);
// Requests redraw eMenuStyle_System overlay contents
void redrawSystemOverlay(bool fullRedraw = false);

// Get alpha fade in/out information
struct MenuAlphaInfo
{
	double fadeInRate, fadeOutRate;
	int fadeInDelay, fadeOutDelay, inactiveFadeOutDelay;
	u8 maxAlpha, inactiveAlpha;
};
MenuAlphaInfo getMenuAlphaInfo(int theMenuID);

const std::vector<RECT>& menuLayoutComponents(
	int theMenuID,
	const SIZE& theTargetSize,
	const RECT& theTargetClipRect);

// Returns background color to become fully transparent
COLORREF transColor(int theMenuID);

// Returns draw priority (which are on top of which)
int drawPriority(int theMenuID);

// Returns associated parent hotspot if have one
Hotspot parentHotspot(int theMenuID);

} // WindowPainter
