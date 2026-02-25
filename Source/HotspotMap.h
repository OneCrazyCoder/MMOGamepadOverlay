//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

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

// Update map to reflect changes to any hotspots
void loadProfileChanges();

// Deactivate all hotspots and free memory
void cleanup();

// Updates hotspot tracking from changes to cursor, target size, etc.
void update();

// Set which hotspot arrays should be active
void setEnabledHotspotArrays(const BitVector<32>& theHotspotArrays);
const BitVector<32>& getEnabledHotspotArrays();

// Returns which hotspot to jump mouse cursor to in given direction (or 0)
int getNextHotspotInDir(ECommandDir theDirection);

// Get Link in a pre-generated map linking menu hotspot with cardinal directions
struct ZERO_INIT(HotspotLinkNode)
{ u8 next[eCmdDir_Num]; bool edge[eCmdDir_Num]; };
HotspotLinkNode getMenuHotspotsLink(int theMenuID, int theMenuItemIdx);
// Returns the edge-most hotspot menu item in desired cardinal direction
// If edge has multiple hotspots in a row/column, returns closest to theDefault
int getEdgeMenuItem(int theMenuID, ECommandDir theDir, int theDefault);

} // HotspotMap
