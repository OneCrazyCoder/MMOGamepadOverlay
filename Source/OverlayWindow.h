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

// Fades in to max alpha value
void fadeFullyIn();

// Fades out to min alpha value
void beginFadeOut();

// Fades out to min alpha value after a set time from this call
// Resets the delay from any previous calls of this function,
// meaning calling it repeatedly prevents the fade out
void startAutoFadeOutTimer();

// Halts delay timer to trigger fading out set by setAutoFadeOutTime
void abortAutoFadeOut();

// Returns true if waiting for delay to end to start fading out
bool isAutoFadeOutTimeSet();

extern HWND gHandle;

} // OverlayWindow