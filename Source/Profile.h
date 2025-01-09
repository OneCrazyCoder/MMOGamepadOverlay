//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Reads and writes user configuration settings to/from a .ini file.
	Data is read in all at once and cached, but changes are written out to
	disk immediately if they are different than cached data!

	Each setting can have a section name associated with it, separated by
	a forward slash character. For example: "System/FrameTime" would get the
	value for "FrameTime=" from the [System] section in the .ini file.
*/

#include "Common.h"

namespace Profile
{

// Load (and/or generate) .ini files for core profile data only
void loadCore();

// Load (and/or generate) .ini files for current profile
void load();

// Allow user to manually select which profile to load
// Returns true if user loaded a profile, false if they cancelled
bool queryUserForProfile();

// Access the profile properties
std::string getStr(const std::string& theKey, const std::string& theDefaultValue = "");
int getInt(const std::string& theKey, int theDefaultValue = 0);
bool getBool(const std::string& theKey, bool theDefaultValue = false);
float getFloat(const std::string& theKey, float theDefaultValue = 0);
// Directly returns all key/value pairs whose keys start with given prefix (section).
// Returned key/value pairs will be appended to any data already in 'out' vector.
// If 'trimKeys', thePrefix will be removed from returned key values.
// WARNING: Returned pointers may be invalidated with any modifications to profile!
typedef std::vector<std::pair<const char*, const char*> > KeyValuePairs;
void getAllKeys(const std::string& thePrefix, KeyValuePairs& out, bool trimKeys = true);

// Add or modify profile properties
// This requires section name be specified directly, in case there are any
// forward slash characters in the section or property name.
// Changes will not be saved to file until saveChangesToFile() is called,
// which happens when about to un-load a profile (reload/change) or exit app
void setStr(const std::string& theSection,
			const std::string& theProperty,
			const std::string& theValue);
// Saves any modifications made using setStr() to .ini file
void saveChangesToFile();

} // Profile
