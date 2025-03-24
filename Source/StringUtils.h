//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#include <sstream>

#pragma warning(disable: 4996)

std::string narrow(const wchar_t *s); // to utf8
std::wstring widen(const char *s); // from utf8
inline std::string narrow(const std::wstring &s) { return narrow(s.c_str()); }
inline std::wstring widen(const std::string &s) { return widen(s.c_str()); }

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
std::string replaceChar(const std::string& theString, char oldChar, char newChar);
// Replace all instances of a specific string with a different one
std::string replaceAllStr(const std::string& theString, const char* oldStr, const char* newStr);

// File/path utilities (works fine with UTF8-encoding strings)
std::string getFileName(const std::string& thePath);
std::string safeFileName(const std::string& theFileName);
std::string getFileDir(const std::string& thePath, bool withSlash = false);
std::string getRootDir(const std::string& thePath);
std::string getExtension(const std::string& thePath);
std::string removeExtension(const std::string& thePath);
std::string withExtension(const std::string& thePath, const std::string& theExt);
std::string getPathParams(const std::string& thePath);
std::string removePathParams(const std::string& thePath); // also removes double quotes
std::string expandPathVars(const std::string& thePath); // such as %USERPROFILE%
std::string removeTrailingSlash(const std::string& theDirectory);
std::string addTrailingSlash(const std::string& theDirectory, bool backSlash = false);
bool isAbsolutePath(const std::string& thePath);

// Returns string before first theChar, and removes it+theChar from theString
// Also trims whitespace around returned string and start of theString
// If theChar is not found or is the first non-whitespace character,
// returns empty string and only trims whitespace from beginning of theString.
std::string breakOffItemBeforeChar(std::string& theString, char theChar = ',');
// If the string ends in a positive integer (but isn't entirely a number), returns
// that integer and removes those chars (except leading 0's). Otherwise returns -1.
int breakOffIntegerSuffix(std::string& theString);
// Breaks the string into individual sub-strings of ASCII alphanumeric characters,
// and appends them to the passed-in vector of strings. All other characters are
// stripped and act as separators for each "word" except for hyphenated or under-
// scored words which are conjoined (Left-Click or Left_Click becomes LeftClick).
void sanitizeSentence(const std::string& theString, std::vector<std::string>& out);
// Finds first character in theString that is after thePrefix, even if upper/lower
// case doesn't match or string has whitespace in its copy of the prefix.
// Returns 0 if theString does not start with entire prefix or the prefix is empty
size_t posAfterPrefix(const std::string& theString, const std::string& thePrefix);
bool hasPrefix(const std::string& theString, const std::string& thePrefix);
// Finds next string "tag" in format <tagName> and returns its start pos and length
// (which include the '<' and '>' chars). .subStr(.first+1, .second-2) == tagName.
// If no tags are found, .first will be set to std::string::npos and .second to 0
std::pair<std::string::size_type, std::string::size_type>
findStringTag(const std::string& theString, std::string::size_type theStartPos = 0);

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

bool isAnInteger(const std::string& theString); // must be pre-trim()'d
bool isAnInteger(const char* theString); // must be pre-trim()'d
int intFromString(const std::string& theString);
int intFromString(const char* theString);
u32 u32FromString(const std::string& theString);
u32 u32FromString(const char* theString);
u32 u32FromHexString(const std::string& theString);
u32 u32FromHexString(const char* theString);
s64 s64FromString(const std::string& theString);
s64 s64FromString(const char* theString);
u64 u64FromString(const std::string& theString);
u64 u64FromString(const char* theString);
float floatFromString(const std::string& theString);
float floatFromString(const char* theString);
double doubleFromString(const std::string& theString);
double doubleFromString(const char* theString);
bool boolFromString(const std::string& theString);
bool boolFromString(const char* theString);

// UTF8-encoded string conversions
std::vector<u32> UTF8ToUTF32(const std::string& theString);
std::vector<u32> UTF8ToUTF32(const char*);
std::string UTF32ToUTF8(const std::vector<u32>& theVector);

// Returns a utf8-encoded sub-string of theString, but is NOT the same as if
// called theString.substr() directly because the passed-in positions are in
// terms of code points rather than bytes.
std::string substrUTF8(const std::string& theString, size_t theFirstCodePointPos, size_t theCodePointLength = size_t(-1));
std::string substrUTF8(const char* theString, size_t theFirstCodePointPos, size_t theCodePointLength = size_t(-1));

enum EUTF8DecodeResult
{
	eUTF8DecodeResult_Ready = 0, // theCodePoint is now a valid Unicode value
	eUTF8DecodeResult_Error = 12, // Bad UT8 sequence
	// Any other value == need to run decodeUTF8() again with next string byte!
};
// Decodes one byte of a UTF8-encoded string.
// theState and theCodePoint should initially be 0 to begin decoding. They are
// updated and need to be passed back in again along with next byte of the
// string if the result is not _Ready or _Error in order to fully decode a
// single Unicode code point that uses multiple bytes of encoding. After get
// _Ready result, do NOT need to reset theState or theCodePoint, but can just
// pass them back in with the next byte of the string to continue decoding.
EUTF8DecodeResult decodeUTF8(u32* theState, u32* theCodePoint, unsigned char theByte);

// Converts a unicode code point to a utf8 character or sequence of charaters
// (depending on the code point) and appends it/them to given std::string
void appendUTF8(u32 theCodePoint, std::string& theDestString);

#include "StringUtils.inc"
