//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Common.h"

//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

std::string narrow(const wchar_t *s)
{
	std::string aResult;
	int aSize = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
	if( aSize < 256 )
	{
		char dest[255];
		aSize = WideCharToMultiByte(CP_UTF8, 0, s, -1, &dest[0], aSize, NULL, NULL);
		DBG_ASSERT(aSize > 0);
		aResult = &dest[0];
	}
	else
	{
		char* dest = new char[aSize];
		aSize = WideCharToMultiByte(CP_UTF8, 0, s, -1, dest, aSize, NULL, NULL);
		DBG_ASSERT(aSize > 0);
		aResult = dest;
		delete [] dest;
	}
	return aResult;
}


std::wstring widen(const char *s)
{
	std::wstring aResult;
	int aSize = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if( aSize < 256 )
	{
		wchar_t dest[255];
		aSize = MultiByteToWideChar(CP_UTF8, 0, s, -1, &dest[0], aSize);
		DBG_ASSERT(aSize > 0);
		aResult = &dest[0];
	}
	else
	{
		wchar_t* dest = new wchar_t[aSize];
		aSize = MultiByteToWideChar(CP_UTF8, 0, s, -1, dest, aSize);
		DBG_ASSERT(aSize > 0);
		aResult = dest;
		delete [] dest;
	}
	return aResult;
}


std::string vformat(const char* fmt, va_list argPtr)
{
	const int kInitialBufferSize = 260;
	// Allow doubling buffer size at most 12 times (~1MB string)
	const int kMaxBufferSize = kInitialBufferSize << 12;

	char aCharBuffer[kInitialBufferSize];
	char* aCStringPtr = &aCharBuffer[0];
	std::vector<char> aCharVector;

	int aBufferSize = kInitialBufferSize;
	int aResult = vsnprintf(aCStringPtr, aBufferSize, fmt, argPtr);

	while( (aResult < 0 || aResult >= aBufferSize) && aBufferSize < kMaxBufferSize )
	{
		// Make buffer size needed, or if don't know just double size until do
		// Note that this doubling loop is only needed for older Visual Studio
		// which has a broken version of vsnprintf that doesn't return the
		// needed size but just returns -1 instead when the buffer is too small
		aBufferSize = max(aResult + 1, aBufferSize * 2);
		aCharVector.resize(aBufferSize);
		aCStringPtr = &aCharVector[0];
		aResult = vsnprintf(aCStringPtr, aBufferSize, fmt, argPtr);
	}
	DBG_ASSERT(aResult >= 0);

	return std::string(aCStringPtr);
}


std::string trim(const std::string& theString)
{
	int aStartPos = 0;
	while(aStartPos < theString.length() && isspace(theString[aStartPos]))
		++aStartPos;

	int anEndPos = int(theString.length()) - 1;
	if( aStartPos < theString.length() )
	{
		while(anEndPos >= 0 && isspace(theString[anEndPos]))
			--anEndPos;
	}

	return theString.substr(aStartPos, anEndPos - aStartPos + 1);
}


std::string upper(const std::string& theString)
{
	std::string aString;
	aString.reserve(theString.size());

	for(size_t i = 0; i < theString.length(); ++i)
		aString += (theString[i] & 0x80) ? theString[i] : ::toupper(theString[i]);

	return aString;
}


std::string lower(const std::string& theString)
{
	std::string aString;
	aString.reserve(theString.size());

	for(size_t i = 0; i < theString.length(); ++i)
		aString += (theString[i] & 0x80) ? theString[i] : ::tolower(theString[i]);

	return aString;
}


std::string condense(const std::string& theString)
{
	std::string aString;
	aString.reserve(theString.size());

	bool allowDash = true;
	for(size_t i = 0; i < theString.length(); ++i)
	{
		if( (unsigned)theString[i] > ' ' && theString[i] != '_' &&
			(theString[i] != '-' || allowDash) )
		{
			aString += (theString[i] & 0x80) ? theString[i] : ::toupper(theString[i]);
			allowDash = theString[i] >= '0' && theString[i] <= '9';
		}
	}

	return aString;
}


std::string replaceChar(const std::string& theString, char oldChar, char newChar)
{
	// Only safe with ANSI chars so don't mess up UTF-8 strings
	DBG_ASSERT(u8(oldChar) <= 0x7F);
	DBG_ASSERT(u8(newChar) <= 0x7F);

	std::string aResult;
	aResult.reserve(theString.size());
	for(size_t i = 0; i < theString.length(); ++i)
	{
		if( theString[i] == oldChar )
			aResult.push_back(newChar);
		else
			aResult.push_back(theString[i]);
	}

	return aResult;
}

std::string getFileName(const std::string& thePath)
{
	std::string aResult;
	size_t aLastSlash = thePath.rfind('\\');
	if( aLastSlash == std::string::npos )
		aLastSlash = thePath.rfind('/');

	if( aLastSlash == std::string::npos )
		aResult = thePath;
	else
		aResult = thePath.substr(aLastSlash + 1);
	return aResult;
}


std::string safeFileName(const std::string& theFileName)
{
	std::string result;

	// Remove illegal characters: <>:"/\|?* and control characters
	for(size_t i = 0; i < theFileName.size(); ++i)
	{
		switch(theFileName[i])
		{
		case '\x7f': case '<': case '>': case ':': case '"':
		case '/': case '\\': case '|': case '?': case '*':
			break;
		default:
			if( theFileName[i] >= ' ' )
				result.push_back(theFileName[i]);
			break;
		}
	}
	if( result.empty() )
		return result;

	// Remove all leading whitespace
	size_t aStartPos = 0;
	while(aStartPos < result.length() && result[aStartPos] == ' ')
		++aStartPos;
	result = result.substr(aStartPos);
	if( result.empty() )
		return result;

	// Remove all trailing spaces and make sure can't end in '.'
	while(!result.empty() &&
			(result[result.length()-1] == ' ' ||
			 result[result.length()-1] == '.') )
	{
		result.resize(result.length()-1);
	}

	// Check if final name is equal to a reserved file name
	static const char* kReservedNames[] =
	{
		"CON", "PRN", "AUX", "NUL","COM1", "COM2", "COM3", "COM4",
		"COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
		"LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
	};
	for(size_t i = 0; i < ARRAYSIZE(kReservedNames); ++i)
	{
		if( result == kReservedNames[i] )
		{
			result.clear();
			break;
		}
	}

	return result;
}


std::string getFileDir(const std::string& thePath, bool withSlash)
{
	std::string aResult;
	size_t aLastSlash = thePath.rfind('\\');
	if( aLastSlash == std::string::npos )
		aLastSlash = thePath.rfind('/');
	if( aLastSlash == std::string::npos )
		return aResult;

	if( withSlash )
		aResult = thePath.substr(0, aLastSlash+1);
	else
		aResult = thePath.substr(0, aLastSlash);
	return aResult;
}


std::string getRootDir(const std::string& thePath)
{
	std::string result;
	size_t aSearchPos = 0;
	if( thePath.empty() )
		return result;

	while(thePath[aSearchPos] == ' ')
		++aSearchPos;
	if( thePath[aSearchPos] == '\0' )
		return result;
	if( thePath[aSearchPos] == '"' )
		++aSearchPos;
	while(thePath[aSearchPos] == ' ')
		++aSearchPos;
	if( thePath[aSearchPos] == '\0' )
		return result;
	size_t aStartPos = aSearchPos;
	while((thePath[aSearchPos] >= 'A' && thePath[aSearchPos] <= 'Z') ||
		  (thePath[aSearchPos] >= 'a' && thePath[aSearchPos] <= 'z'))
		++aSearchPos;
	if( aStartPos == aSearchPos )
		return result;
	if( thePath[aSearchPos++] != ':' )
		return result;
	if( thePath[aSearchPos] != '\\' && thePath[aSearchPos] != '/' )
		return result;
	result = thePath.substr(aStartPos, aSearchPos-aStartPos);
	result.push_back('\\');

	return result;
}


std::string getExtension(const std::string& thePath)
{
	std::string aResult;
	size_t aLastSlash = thePath.rfind('\\');
	if( aLastSlash == std::string::npos )
		aLastSlash = thePath.rfind('/');
	if( aLastSlash == std::string::npos )
		aLastSlash = 0;
	const size_t aLastDot = thePath.rfind('.');
	if( aLastDot != std::string::npos && aLastDot > aLastSlash )
		aResult = thePath.substr(aLastDot);
	return aResult;
}


std::string removeExtension(const std::string& thePath)
{
	std::string aResult;
	size_t aLastSlash = thePath.rfind('\\');
	if( aLastSlash == std::string::npos )
		aLastSlash = thePath.rfind('/');
	if( aLastSlash == std::string::npos )
		aLastSlash = 0;
	const size_t aLastDot = thePath.rfind('.');
	if( aLastDot != std::string::npos && aLastDot > aLastSlash )
		aResult = thePath.substr(0, aLastDot);
	else
		aResult = thePath;
	return aResult;
}


std::string withExtension(const std::string& thePath, const std::string& theExt)
{
	if( getExtension(thePath) == theExt )
		return thePath;

	return removeExtension(thePath) + theExt;
}


std::string getPathParams(const std::string& thePath)
{
	bool inQuotes = false;
	bool atStart = true;
	for(size_t aPos = 0; aPos < thePath.size(); ++aPos)
	{
		if( thePath[aPos] == '"' )
			inQuotes = !inQuotes;
		if( thePath[aPos] == ' ' )
		{
			if( !atStart && !inQuotes )
				return thePath.substr(aPos + 1);
		}
		else
		{
			atStart = false;
		}
	}

	return std::string();
}


std::string removePathParams(const std::string& thePath)
{
	std::string aResult;

	bool inQuotes = false;
	bool atStart = true;
	for(size_t aPos = 0; aPos < thePath.size(); ++aPos)
	{
		if( thePath[aPos] == '"' )
		{
			inQuotes = !inQuotes;
		}
		else if( thePath[aPos] == ' ' )
		{
			if( !atStart && !inQuotes )
				break;
			aResult.push_back(thePath[aPos]);
		}
		else
		{
			atStart = false;
			aResult.push_back(thePath[aPos]);
		}
	}

	return aResult;
}


std::string removeTrailingSlash(const std::string& theDirectory)
{
	std::string aResult;
	const size_t aLen = theDirectory.length();

	if( aLen > 0 && (theDirectory[aLen-1] == '\\' || theDirectory[aLen-1] == '/') )
		aResult = theDirectory.substr(0, aLen - 1);
	else
		aResult = theDirectory;
	return aResult;
}


std::string	addTrailingSlash(const std::string& theDirectory, bool backSlash)
{
	std::string aResult;
	if( !theDirectory.empty() )
	{
		char aChar = theDirectory[theDirectory.length()-1];
		if( aChar != '\\' && aChar != '/' )
			aResult = theDirectory + (backSlash ? '\\' : '/');
		else
			aResult = theDirectory;
	}

	return aResult;
}


bool isAbsolutePath(const std::string& thePath)
{
	return !getRootDir(thePath).empty();
}


std::string breakOffItemBeforeChar(std::string& theString, char theChar)
{
	std::string result;
	size_t aStripCount = 0;

	size_t aCharPos = theString.find(theChar);
	if( aCharPos != std::string::npos )
	{
		result = trim(theString.substr(0, aCharPos));
		// If result is non-empty, everything up to and include theChar
		if( !result.empty() )
			aStripCount = aCharPos + 1;
	}

	// Whether or not stripping anything else from above, additionally strip
	// any whitespace characters that would otherwise be at start of theString
	for(; aStripCount < theString.length(); ++aStripCount)
	{
		if( theString[aStripCount] > ' ' )
			break;
	}
	theString.erase(0, aStripCount);

	return result;
}


int breakOffIntegerSuffix(std::string& theString)
{
	int result = 0;
	int multiplier = 1;
	size_t aStrPos = theString.size()-1;
	while(
		aStrPos > 0 &&
		theString[aStrPos] >= '0' &&
		theString[aStrPos] <= '9' )
	{
		result += int(theString[aStrPos] - '0') * multiplier;
		multiplier *= 10;
		--aStrPos;
	}

	// If stayed at end of string or got all the way to start, invalid as a
	// string ending in (but not entirely being) an integer
	if( aStrPos >= theString.size()-1 || aStrPos <= 0 )
		result = -1;
	else
		theString.resize(aStrPos+1);

	return result;
}


void sanitizeSentence(const std::string& theString, std::vector<std::string>& out)
{
	std::string word;
	for(size_t i = 0; i < theString.length(); ++i)
	{
		const char c = theString[i];
		if( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') )
		{
			// Only negative integers can start with '-'
			if( !word.empty() && word[0] == '-' )
				word = word.substr(1);
			word += c;
			continue;
		}

		if( (c >= '0' && c <= '9') || (c == '-' && word.empty()) )
		{
			word += c;
			continue;
		}
				
		if( c != '-' && c != '_' && c != '\'' && !word.empty() )
		{
			out.push_back(word);
			word.clear();
		}
	}

	if( !word.empty() && word != "-" )
		out.push_back(word);
}


std::string commaSeparate(u32 theValue)
{
	std::string aResult;
	if( theValue == 0 )
	{
		aResult = "0";
		return aResult;
	}

	int aPlace = 0;
	int aCurValue = theValue;

	while( aCurValue > 0 )
	{
		if( aPlace && aPlace % 3 == 0 )
			aResult = ',' + aResult;
		aResult = (char) ('0' + (aCurValue % 10)) + aResult;
		aCurValue /= 10;
		++aPlace;
	}

	return aResult;
}


std::vector<u32> UTF8ToUTF32(const char* s)
{
	std::vector<u32> aResult;
	u32 c = 0;
	for(u32 prev = 0, curr = 0; *s; prev = curr, ++s)
	{
		switch(decodeUTF8(&curr, &c, *s))
		{
		case eUTF8DecodeResult_Error:
			// The byte is invalid, replace it and restart.
			DBG_LOG("U+FFFD (Bad UTF-8 sequence)\n");
			curr = eUTF8DecodeResult_Ready;
			if( prev != eUTF8DecodeResult_Ready )
				--s;
			break;;

		case eUTF8DecodeResult_Ready:
			// A properly encoded character has been found.
			aResult.push_back(c);
			break;
		}
	}

	return aResult;
}


std::string substrUTF8(const char* s, size_t theFirstCodePointPos, size_t theCodePointLength)
{
	std::string aResult;
	u32 c = 0;
	for(u32 prev = 0, curr = 0; *s && theCodePointLength > 0; prev = curr, ++s)
	{
		if( !theFirstCodePointPos )
			aResult += *s;
		switch(decodeUTF8(&curr, &c, *s))
		{
		case eUTF8DecodeResult_Error:
			// The byte is invalid, replace it and restart.
			DBG_LOG("U+FFFD (Bad UTF-8 sequence)\n");
			curr = eUTF8DecodeResult_Ready;
			if( prev != eUTF8DecodeResult_Ready )
			{
				--s;
				if( !theFirstCodePointPos )
					aResult.erase(aResult.size() - 1);
			}
			break;;

		case eUTF8DecodeResult_Ready:
			// A properly encoded character has been found.
			if( theFirstCodePointPos )
				--theFirstCodePointPos;
			else
				--theCodePointLength;
			break;
		}
	}

	return aResult;
}

