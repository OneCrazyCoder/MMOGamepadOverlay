//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Reads and writes user configuration settings to/from a .ini file.
	Data is read in all at once and cached, but changes are written out to
	disk immediately if they are different than cached data!

	Each setting can have a category name associated with it, separated by
	a forward slash character. For example: "System/FrameTime" would get the
	value for "FrameTime=" from the category [System] in the .ini file.
*/

#include "Common.h"

namespace Profile
{

// Load (and/or generate) .ini files
void load();

// Allow user to manually select which profile to load
void queryUserForProfile();

// Access the profile settings
std::string getStr(const std::string& theKey, const std::string& theDefaultValue = "");
int getInt(const std::string& theKey, int theDefaultValue = 0);
bool getBool(const std::string& theKey, bool theDefaultValue = false);
// If 'out' already has more values than settings file includes, the extras are left as-is
void getIntArray(const std::string& theKey, std::vector<int>& out);
// Directly returns all key/value pairs whose keys start with given prefix (category).
// Returned key names will be in all-caps, no spaces, and with given prefix removed.
// WARNING: Returned pointers may be invalidated with any modifications to profile!
typedef std::vector<std::pair<const char*, const char*> > KeyValuePairs;
void getAllKeys(const std::string& thePrefix, KeyValuePairs& out);

// Add or modify profile settings
void setStr(const std::string& theKey, const std::string& theString);
void setInt(const std::string& theKey, int theValue);
void setBoolean(const std::string& theKey, bool theValue);

} // Profile
