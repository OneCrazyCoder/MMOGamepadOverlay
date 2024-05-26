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

// Set which hotspot arrays should be active
void setEnabledHotspotArrays(const BitVector<>& theHotspotArrays);

// Returns which hotspot to jump to in given direction (or 0)
u16 getNextHotspotInDir(ECommandDir theDirection);

// Converts a Profile string into a Hotspot (and removes the hotspot
// from the start of the string in case there are multiple included).
EResult stringToHotspot(std::string& theString, Hotspot& out);
// Same thing but for only a single coordinate of a hotspot
EResult stringToCoord(std::string& theString, Hotspot::Coord& out);

} // HotspotMap
