//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Reads and writes user configuration properties to/from a .ini file.
	Data is read in all at once and cached.

	Each property has a section name, property name, and value string. In the
	file, sections are designated by [SectionName] followed by each property
	in the section with PropertyName = PropertyValue.
*/

#include "Common.h"

namespace Profile
{

struct Property
{
	std::string str;
	std::string pattern;
};
typedef StringToValueMap<Property> PropertyMap;
typedef StringToValueMap<PropertyMap> SectionsMap;

// Load (and/or generate) .ini files for core profile data only
void loadCore();

// Load (and/or generate) .ini files for current profile
void load();

// Allow user to manually select which profile to load
// Returns true if user loaded a profile, false if they cancelled
bool queryUserForProfile();

// Get rtf string describing known issues with current game
const std::string& getKnownIssuesRTF();

// Access the profile properties
std::string getStr(const std::string& theSection,
				   const std::string& thePropertyName,
				   const std::string& theDefaultValue = "");
int getInt(const std::string&, const std::string&, int = 0);
bool getBool(const std::string&, const std::string&, bool = false);
double getFloat(const std::string&, const std::string&, double = 0);
int variableNameToID(const std::string& theVarName); // -1 if not found
std::string variableIDToName(int theVariableID);
const PropertyMap* getSection(const std::string& theSectionName);
const PropertyMap& getSectionProperties(const std::string& theSectionName);
const SectionsMap& allSections();

// Add or modify profile properties (does nothing if match prev value)
// Any changed values are added to changedSections() as well.
// Changes will not be saved to file until saveChangesToFile() is called.
// saveToFile = false means the new value can be applied but NOT saved to file
void setStr(const std::string& theSection,
			const std::string& thePropertyName,
			const std::string& theValue,
			bool saveToFile = true);
void setVariable(int theVarID, const std::string& theValue, bool temporary);
void setVariable(const std::string& theVarName, const std::string&, bool);
// Only sets the string if thePropertyName doesn't yet exist (or is empty val)
void setNewStr(const std::string&, const std::string&, const std::string&);
// Gets property changes requested by setStr() since last load() or clear
const SectionsMap& changedSections();
void clearChangedSections();
// Saves any setStr() changed requested with saveToFile to .ini file
void saveChangesToFile();

} // Profile
