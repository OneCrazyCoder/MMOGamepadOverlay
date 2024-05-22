//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Tracks enabled "hotspots" (positions of interest in the target game window)
	to find which one mouse cursor should next jump to in a cardinal direction.
*/

#include "Common.h"

namespace HotspotMap
{

// Initialize hotspot tracking based on data parsed by InputMap
void init();

// Deactivate all hotspots and free memory
void cleanup();

// Updates hotspot tracking from changes to cursor, target size, etc.
void update();

// Set which hotspot sets should be active
void setEnabledHotspotSets(const BitVector<>& theHotspotSets);

// Returns which hotspot to jump to in given direction (or 0)
u16 getNextHotspotInDir(ECommandDir theDirection);

} // HotspotMap
