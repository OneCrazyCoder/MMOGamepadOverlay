//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#pragma once

#include <sstream>

std::string narrow(const wchar_t *s); // to utf8
std::wstring widen(const char *s); // from utf8
std::string toRTF(const wchar_t *s);
inline std::string toRTF(const char *s) { return toRTF(widen(s).c_str()); }
inline std::string narrow(const std::wstring &s) { return narrow(s.c_str()); }
inline std::wstring widen(const std::string &s) { return widen(s.c_str()); }
inline std::string toRTF(const std::wstring &s) { return toRTF(s.c_str()); }
inline std::string toRTF(const std::string &s) { return toRTF(s.c_str()); }

std::string vformat(const char* fmt, va_list argPtr);
std::string strFormat(const char* fmt ...);
// Remove leading and trailing whitespace
std::string trim(const std::string& theString);
// WARNING: Leaves all non-ASCII (0-127) characters as-is!
// Should only really be used for case-insensitive comparisons to ASCII strings
std::string upper(const std::string& theString);
std::string lower(const std::string& theString);
// Remove ALL whitespace (and '_'), set to all upper-case, and conjoins
// hyphenated words (L-Click to LClick), but number ranges like "8-12" are left
std::string condense(const std::string& theString);
// Replace all instances of a specific char with a different one
std::string replaceChar(const std::string& theString, char oldC, char newC);
// Replace all instances of a specific string with a different one
std::string replaceAllStr(
	const std::string& theString, const char* oldStr, const char* newStr);
// Checks if string matches a pattern using *'s as wildcards (case-insensitive)
// Result is empty if none found, or contains the sub-strings that matched with
// each '*' character (with a '*' in result for end of each matched sub-string).
std::wstring wildcardMatch(const wchar_t* theString, const wchar_t* thePattern);

// File/path utilities (works fine with UTF8-encoding strings)
std::string getFileName(const std::string& thePath);
std::string safeFileName(const std::string& theFileName);
std::string getFileDir(const std::string& thePath, bool withSlash = false);
std::string getExtension(const std::string& thePath);
std::string removeExtension(const std::string& thePath);
std::string withExtension(const std::string& thePath, const std::string& ext);
std::string getPathParams(const std::string& thePath);

// Returns string before first theChar, and removes it+theChar from theString
// Also trims whitespace around returned string and start of theString
// If theChar is not found or is the first non-whitespace character,
// returns empty string and only trims whitespace from beginning of theString.
std::string breakOffItemBeforeChar(std::string& theString, char theChar = ',');
// Always "breaks off" version, even if no/starting theChar (clears theString).
std::string breakOffNextItem(std::string& theString, char theChar = ',');
// Returns substring from thePosition to first theDelimiter (or whole string),
// updating thePosition to theDelimiter or '\0' pos. Returned string is trimmed.
// If entire string is quoted, theDelimiter is ignored inside quoted section,
// whitespace in the quotes is left as-is, and outer quote chars are removed.
// Supports SQL style for quote chars inside quoted strings ("" or '"' etc).
std::string fetchNextItem(
	const std::string&, size_t& thePosition, const char* theDelimiter = ",");
// If the string ends in a positive integer (and isn't entirely one),
// returns that integer and removes those chars. Otherwise returns -1.
// allowJustInt must be true to work for a string that is entirely an integer.
int breakOffIntegerSuffix(std::string& theString, bool allowJustInt = false);
// Converts an integer range suffix of positive integers into their components,
// such as "Name12-17" into "Name", 12, and 17 (or both 12 for just "Name12").
// Returns true if was a valid range with 2 values (even for i.e. "Name12-12").
bool fetchRangeSuffix(const std::string& theString, std::string& theRangeName,
					  int& theStart, int& theEnd, bool allowJustInt = false);
// Breaks the string into individual sub-strings of ASCII alphanumeric chars,
// and appends them to the passed-in vector of strings. All other characters are
// stripped and act as separators for each "word" (except for hyphenated or
// under-scored words which are conjoined (i.e. Left-Click becomes LeftClick).
void sanitizeSentence(const std::string&, std::vector<std::string>& out);
// Finds first character in theString that is after thePrefix, even if casing
// or whitespace (including '_' and '-') differs between them.
// Returns 0 if theString does not start with entire prefix or prefix is empty
size_t posAfterPrefix(const std::string&, const std::string& thePrefix);
bool hasPrefix(const std::string&, const std::string& thePrefix);
// Finds the first most-nested string "tag" (${TagContents}, <TagContents>, etc)
// and returns its start pos and length (which include theTagStart/End chars).
// If no tags are found, .first will be set to npos and .second to 0
std::pair<std::string::size_type, std::string::size_type>
findStringTag(const std::string&, std::string::size_type theStartPos = 0,
			  const char* theTagStart = "<", const char theTagEnd = '>');

// Conversion between numbers and (pure ASCII) strings
std::string commaSeparate(u32 theValue);
std::string toString(s32 theValue);
std::string toString(s16 theValue);
std::string toString(s8 theValue);
std::string toString(u32 theValue);
std::string toString(u16 theValue);
std::string toString(u8 theValue);
std::string toString(float theValue);
std::string toString(double theValue);
std::string toString(FILETIME theValue);

bool isAnInteger(const std::string& theString); // must be pre-trim()'d
bool isAnInteger(const char* theString); // must be pre-trim()'d
int intFromString(const std::string& theString);
int intFromString(const char* theString);
u32 u32FromString(const std::string& theString);
u32 u32FromString(const char* theString);
u32 u32FromHexString(const std::string& theString);
u32 u32FromHexString(const char* theString);
float floatFromString(const std::string& theString);
float floatFromString(const char* theString);
double doubleFromString(const std::string& theString);
double doubleFromString(const char* theString);
double doubleFromStringStrict(const std::string& theString); // returns NaN
double doubleFromStringStrict(const char* str); // instead of 0 if not valid
bool boolFromString(const std::string& theString);
bool boolFromString(const char* theString);
FILETIME fileTimeFromString(const std::string& theString);
FILETIME fileTimeFromString(const char* theString);

#include "StringUtils.inc"
