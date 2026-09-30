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

// Load configuration settings from current profile
void loadProfile();
void loadProfileChanges();

// Remove/disable all hotspots for app shutdown or profile change
void cleanup();

// Re-evaluate hotspot positions due to target/overlay position/size change
void resize();

// Updates hotspot tracking from changes to cursor, target size, etc.
void update();

// Access basic hotspot data
const Hotspot& getHotspot(int theHotspotID);
const BitVector<512>& getHotspotSet(int theHotspotSetID);
int hotspotCount();
int hotspotSetCount();
int hotspotIDFromName(const std::string& theHotspotName); // or 0
int hotspotSetIDFromName(const std::string& theHotspotSetName); // or 0
bool isValidHotspotID(int theHotspotID);
float hotspotScale(int theHotspotID);
const char* hotspotLabel(int theHotspotID);
const char* hotspotSetLabel(int theHotspotSetID);

// Check which hotspots changed from last loadProfileChanges() call
const BitVector<512>& changedHotspots();
void resetChangedHotspots();

// Updates both eSpecialHotspot_LastCursorPos and gLastCursorPos
bool setLastCursorPos(POINT theNewCursorPos); // true if changed

// Set which hotspot sets should be active
void setEnabledHotspotSets(const BitVector<32>& theHotspotsets);
const BitVector<512>& enabledHotspots();

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
