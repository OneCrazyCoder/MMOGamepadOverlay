//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Allows for setting hotspot positions, copy-from-screen icon regions, and
	HUD element positions at runtime. Changes are saved to current Profile.
*/

#include "Common.h"

namespace LayoutEditor
{

// Launches the editor (displays dialog to select what to change)
void init();

// Halts editor mode if it is running and cleans up memory
void cleanup();

} // LayoutEditor
