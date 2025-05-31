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

// Update map to reflect changes to any hotspots
void loadProfileChanges();

// Deactivate all hotspots and free memory
void cleanup();

// Updates hotspot tracking from changes to cursor, target size, etc.
void update();

// Set which hotspot arrays should be active
void setEnabledHotspotArrays(const BitVector<32>& theHotspotArrays);
const BitVector<32>& getEnabledHotspotArrays();

// Returns which hotspot to jump to in given direction (or 0)
int getNextHotspotInDir(ECommandDir theDirection);

// Get a pre-generated map linking hotspots in a specific array
// (with values relative to first hotspot in that array)
struct ZERO_INIT(HotspotLinkNode)
{ u16 next[eCmdDir_Num]; bool edge[eCmdDir_Num]; };
typedef std::vector<HotspotLinkNode> Links;
const Links& getLinks(int theArrayID);

// Converts a Profile string into a Hotspot (and removes the hotspot
// from the start of the string in case there are multiple included).
EResult stringToHotspot(std::string& theString, Hotspot& out);
// Same thing but for only a single coordinate of a hotspot. Can also
// return the portion of the string that determined this coordinate.
EResult stringToCoord(std::string& theString,
					  Hotspot::Coord& out,
					  std::string* theValidatedString = null);
// Reverse of above - converts a hotspot into a Profile string
enum EHotspotNamingStyle
{	eHNS_XY, eHNS_XY_Off, eHNS_WH, eHNS_X, eHNS_Y,
	eHNS_W, eHNS_H, eHNS_X_Off, eHNS_Y_Off, eHNS_Num };
std::string hotspotToString(const Hotspot&, EHotspotNamingStyle = eHNS_XY);
std::string coordToString(const Hotspot::Coord&, EHotspotNamingStyle);

} // HotspotMap
