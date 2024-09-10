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

// Reload hotspot positions from InputMap
void reloadPositions();

// Set which hotspot arrays should be active
void setEnabledHotspotArrays(const BitVector<>& theHotspotArrays);
const BitVector<>& getEnabledHotspotArrays();

// Returns which hotspot to jump to in given direction (or 0)
u16 getNextHotspotInDir(ECommandDir theDirection);

// Get a pre-generated map linking hotspots in a specific array
// (with values relative to first hotspot in that array)
struct HotspotLinkNode
{ u16 next[eCmdDir_Num]; bool edge[eCmdDir_Num]; };
typedef std::vector<HotspotLinkNode> Links;
const Links& getLinks(u16 theArrayID);

// Converts a Profile string into a Hotspot (and removes the hotspot
// from the start of the string in case there are multiple included).
EResult stringToHotspot(std::string& theString, Hotspot& out);
// Same thing but for only a single coordinate of a hotspot. Can also
// return the portion of the string that determined this coordinate.
EResult stringToCoord(std::string& theString,
					  Hotspot::Coord& out,
					  std::string* theValidatedString = null);
// Reverse of above - converts a hotspot into a Profile string
enum EHotspotNamingConvention
{	eHNC_XY, eHNC_XY_Off, eHNC_WH, eHNC_X, eHNC_Y,
	eHNC_W, eHNC_H, eHNC_X_Off, eHNC_Y_Off, eHNC_Num };
std::string hotspotToString(const Hotspot&, EHotspotNamingConvention);
std::string coordToString(const Hotspot::Coord&, EHotspotNamingConvention);

} // HotspotMap
