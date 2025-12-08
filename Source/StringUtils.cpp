//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "Common.h"

//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

std::string narrow(const wchar_t *s)
{
	std::string aResult;
	int aSize = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
	if( aSize < 256 )
	{
		char dest[255];
		aSize = WideCharToMultiByte(
			CP_UTF8, 0, s, -1, &dest[0], aSize, NULL, NULL);
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


std::string toRTF(const wchar_t* s)
{
	std::string rtf;
	if (!s || !*s)
		return rtf;

	rtf = "{";
	for(const wchar_t* p = s; *p; ++p)
	{
		if( *p > 0x7F )
		{// Needs unicode support
			rtf += "\\uc1 ";
			break;
		}
	}

	for (const wchar_t* p = s; *p; ++p)
	{
		switch (*p)
		{
		case L'\\':
			rtf += "\\\\";
			break;
		case L'{':
			rtf += "\\{";
			break;
		case L'}':
			rtf += "\\}";
			break;
		default:
			if (*p <= 127) // ascii
				rtf += char(*p);
			else // unicode
				rtf += "\\u" + toString(s16(*p)) + "?";
			break;
		}
	}

	rtf += "}";
	return rtf;
}


std::string vformat(const char* fmt, va_list argPtr)
{
	const int kInitialBufferSize = 260;
	// Allow doubling buffer size at most 12 times (~1MB string)
	const int kMaxBufferSize = kInitialBufferSize << 12;

	char aCharBuffer[kInitialBufferSize];
	char* aCStringPtr = &aCharBuffer[0];
	std::vector<char> aCharVector;

	int aBuffSize = kInitialBufferSize;
	int aResult = vsnprintf(aCStringPtr, aBuffSize, fmt, argPtr);

	while((aResult < 0 || aResult >= aBuffSize) && aBuffSize < kMaxBufferSize)
	{
		// Make buffer size needed, or if don't know just double size until do
		// Note that this doubling loop is only needed for older Visual Studio
		// which has a broken version of vsnprintf that doesn't return the
		// needed size but just returns -1 instead when the buffer is too small
		aBuffSize = max(aResult + 1, aBuffSize * 2);
		aCharVector.resize(aBuffSize);
		aCStringPtr = &aCharVector[0];
		aResult = vsnprintf(aCStringPtr, aBuffSize, fmt, argPtr);
	}
	DBG_ASSERT(aResult >= 0);

	return std::string(aCStringPtr);
}


std::string trim(const std::string& theString)
{
	const int kStringLen = intSize(theString.length());
	int aStartPos = 0;
	while(aStartPos < kStringLen && u8(theString[aStartPos]) <= ' ')
	{ ++aStartPos; }

	int anEndPos = kStringLen - 1;
	if( aStartPos < kStringLen )
	{
		while(anEndPos >= 0 && u8(theString[anEndPos]) <= ' ')
			--anEndPos;
	}

	return theString.substr(aStartPos, anEndPos - aStartPos + 1);
}


std::string upper(const std::string& theString)
{
	std::string aString;
	aString.reserve(theString.size());

	for(size_t i = 0; i < theString.length(); ++i)
	{
		aString += (theString[i] & 0x80)
			? theString[i]
			: char(::toupper(theString[i]));
	}

	return aString;
}


std::string lower(const std::string& theString)
{
	std::string aString;
	aString.reserve(theString.size());

	for(size_t i = 0; i < theString.length(); ++i)
	{
		aString += (theString[i] & 0x80)
			? theString[i]
			: char(::tolower(theString[i]));
	}

	return aString;
}



std::string condense(const std::string& theString)
{
	std::string aString;
	aString.reserve(theString.size());

	bool lastWasDigit = false;
	bool pendingDash = false;
	for(const char* c = theString.c_str(); *c; ++c)
	{
		u8 ch(*c);
		if( ch <= ' ' || ch == '_' )
			continue;
		if( ch == '-' )
		{
			if( lastWasDigit )
				pendingDash = true;
			lastWasDigit = false;
			continue;
		}
		const bool isDigit = (ch >= '0' && ch <= '9');
		if( pendingDash && isDigit )
			aString += '-';
		pendingDash = false;
		lastWasDigit = isDigit;
		if( (ch & 0x80) == 0 )
			ch = u8(::toupper(ch));
		aString += ch;
	}

	return aString;
}


std::string replaceChar(
	const std::string& theString, char oldChar, char newChar)
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


std::string replaceAllStr(
	const std::string& theString, const char* oldStr, const char* newStr)
{
	std::string result = theString;
	const size_t oldLen = std::strlen(oldStr);
	const size_t newLen = std::strlen(newStr);

	size_t aPos = 0;
	while ((aPos = result.find(oldStr, aPos)) != std::string::npos)
	{
		result.replace(aPos, oldLen, newStr);
		aPos += newLen;
	}

	return result;
}

std::wstring wildcardMatch(
	const wchar_t* theString,
	const wchar_t* thePattern)
{
	const wchar_t* aLastStar = NULL;
	const wchar_t* aLastMatch = NULL;
	std::wstring result;

	bool firstWildcardFound = false;
	while(*theString)
	{
		if( *thePattern == L'*' )
		{
			aLastStar = thePattern++;
			aLastMatch = theString;
			if( firstWildcardFound )
				result.push_back(L'*');
			else
				firstWildcardFound = true;
		}
		else if( ::towupper(*theString) == ::towupper(*thePattern) )
		{
			++theString;
			++thePattern;
		}
		else if( aLastStar )
		{
			result.push_back(*aLastMatch);
			thePattern = aLastStar + 1;
			++aLastMatch;
			theString = aLastMatch;
		}
		else
		{
			result.clear();
			return result;
		}
	}

	// Trailing *'s
	while(*thePattern == L'*')
	{
		if( firstWildcardFound )
			result.push_back(L'*');
		else
			firstWildcardFound = true;
		++thePattern;
	}

	if( *thePattern != L'\0' )
		result.clear();
	else if( firstWildcardFound )
		result.push_back(L'*');

	return result;
}


std::string getFileName(const std::string& thePath)
{
	std::string aResult;
	size_t aLastSlash = thePath.find_last_of("\\/");
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
	size_t aLastSlash = thePath.find_last_of("\\/");
	if( aLastSlash == std::string::npos )
		return aResult;

	if( withSlash )
		aResult = thePath.substr(0, aLastSlash+1);
	else
		aResult = thePath.substr(0, aLastSlash);
	return aResult;
}


std::string getExtension(const std::string& thePath)
{
	std::string aResult;
	size_t aLastSlash = thePath.find_last_of("\\/");
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
	size_t aLastSlash = thePath.find_last_of("\\/");
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
	// Paths containing parameters MUST have the path itself surrounded in
	// double quotes, otherwise it is impossible to know (through string
	// parsing alone) if a space separates file path from parameters vs
	// being spaces within file/folder names. Checking for an extension does
	// not help, because something like C:\Users\Jon.Doe Files\Apps\AnApp.exe
	// would assume .Doe was an extension and Files... as parameters.
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


std::string fetchNextItem(
	const std::string& theString,
	size_t& thePosition,
	const char* theDelimiter)
{
	#ifndef NDEBUG
	DBG_ASSERT(theDelimiter && *theDelimiter);
	for(const char* c = theDelimiter; *c; ++c)
	{
		DBG_ASSERT(*c != '\'');
		DBG_ASSERT(*c != '"');
		DBG_ASSERT(!(*c & 0x80));
	}
	#endif

	std::string result;

	// Skip leading whitespace
	while(thePosition < theString.size() && u8(theString[thePosition]) <= ' ' )
		++thePosition;

	const char aQuoteChar =
		thePosition >= theString.size() ? '\0' : theString[thePosition];
	bool isQuoted = aQuoteChar == '\'' || aQuoteChar == '"';
	std::string::size_type aPos = 0;
	if( isQuoted )
	{
		for(aPos = thePosition + 1; aPos < theString.size(); ++aPos)
		{
			if( theString[aPos] == aQuoteChar )
			{// End of quoted string or a literal if doubled ("" or '')
				if( aPos + 1 < theString.size() &&
					theString[aPos+1] == aQuoteChar )
				{
					++aPos;
					result += aQuoteChar;
					continue;
				}
				break;
			}
			result += theString[aPos];
		}
		// To be a proper quoted string, needs to end in the same quote char
		// started with, and then have only whitespace and delimiter after it
		if( aPos == theString.size() )
		{
			// Reached end without finding end quote character!
			// Re-parse as a non-quoted string
			isQuoted = false;
			result.clear();
		}
		else
		{
			// Make sure no trailing characters after end quote
			for(++aPos; aPos < theString.size(); ++aPos)
			{
				if( u8(theString[aPos]) <= ' ' )
					continue;
				if( strchr(theDelimiter, theString[aPos]) != null )
					break;
				isQuoted = false;
				result.clear();
				break;
			}
		}
	}

	if( !isQuoted )
	{
		aPos = theString.find_first_of(theDelimiter, thePosition);
		aPos = min(aPos, theString.size());
		if( aPos > thePosition )
		{
			size_t anItemLastChar = aPos - 1;
			while(anItemLastChar > thePosition &&
				  u8(theString[anItemLastChar]) <= ' ')
			{ --anItemLastChar; }
			result = theString.substr(
				thePosition,
				anItemLastChar + 1 - thePosition);
		}
	}

	// Report position of delimiter (or end of string if it wasn't found)
	thePosition = aPos;

	return result;
}


std::string breakOffItemBeforeChar(std::string& theString, char theChar)
{
	size_t aStrPos = 0;
	char aDelimiter[2] = { theChar, '\0' };
	std::string result = fetchNextItem(theString, aStrPos, &aDelimiter[0]);
	if( aStrPos >= theString.size() )
		result.clear();
	++aStrPos;
	if( result.empty() )
		aStrPos = 0;
	while(aStrPos < theString.size() && u8(theString[aStrPos]) <= ' ' )
		++aStrPos;
	if( aStrPos >= theString.size() )
		theString.clear();
	else if( aStrPos > 0 )
		theString = theString.substr(aStrPos);

	return result;
}


std::string breakOffNextItem(std::string& theString, char theChar)
{
	size_t aStrPos = 0;
	char aDelimiter[2] = { theChar, '\0' };
	std::string result = fetchNextItem(theString, aStrPos, &aDelimiter[0]);
	++aStrPos;
	while(aStrPos < theString.size() && u8(theString[aStrPos]) <= ' ' )
		++aStrPos;
	if( aStrPos >= theString.size() )
		theString.clear();
	else if( aStrPos > 0 )
		theString = theString.substr(aStrPos);

	return result;
}


int breakOffIntegerSuffix(std::string& theString, bool allowJustInt)
{
	int result = 0;
	int multiplier = 1;
	int aStrPos = intSize(theString.size())-1;
	while(
		aStrPos >= 0 &&
		theString[aStrPos] >= '0' &&
		theString[aStrPos] <= '9' )
	{
		result += int(theString[aStrPos] - '0') * multiplier;
		multiplier *= 10;
		--aStrPos;
	}

	// If stayed at end of string, invalid as a string ending in an integer
	// If got all the way to start, string is entirely an int, and being valid
	// or not depends on evenIfEntirelyAnInt
	if( aStrPos >= intSize(theString.size())-1 )
		result = -1;
	else if( aStrPos < 0 && !allowJustInt )
		result = -1;
	else
		theString.resize(aStrPos+1);

	// Trim spaces off end of theString
	while(!theString.empty() &&
		  u8(theString[theString.size()-1]) <= ' ' )
	{ theString.resize(theString.size()-1); }

	return result;
}


bool fetchRangeSuffix(
	const std::string& theString,
	std::string& theRangeName,
	int& theStart,
	int& theEnd,
	bool allowJustInt)
{
	// Break off integer suffix as end of range
	theRangeName = theString;
	theStart = theEnd = breakOffIntegerSuffix(theRangeName, allowJustInt);
	bool isInRangeFormat = false;

	if( theEnd >= 0 && theRangeName[theRangeName.size()-1] == '-' )
	{// Possibly in range format like 'Name3-5'
		isInRangeFormat = true;
		theRangeName.resize(theRangeName.size()-1);
		theStart = breakOffIntegerSuffix(theRangeName, allowJustInt);
		if( theStart < 0 )
		{
			// If no theStart value, it means suffix is a negative integer
			// (like 'Name-4'), which isn't valid, so revert to defaults
			theRangeName = theString;
			theStart = theEnd = -1;
			isInRangeFormat = false;
		}
		else if( theStart > theEnd )
		{
			swap(theStart, theEnd);
		}
	}

	return isInRangeFormat;
}


void sanitizeSentence(
	const std::string& theString, std::vector<std::string>& out)
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


size_t posAfterPrefix(
	const std::string& theString, const std::string& thePrefix)
{
	if( thePrefix.empty() )
		return 0;

	const char* str = theString.c_str();
	const char* pre = thePrefix.c_str();
	size_t i = 0, pi = 0;

	bool lastWasDigitStr = false, lastWasDigitPre = false;
	bool pendingDashStr = false, pendingDashPre = false;

	while(str[i] && pre[pi])
	{
		u8 sc = (u8)str[i];
		u8 pc = (u8)pre[pi];

		// Skip condensible characters in theString
		if( sc <= ' ' || sc == '_' ) { ++i; continue; }
		if( sc == '-' )
		{
			if( lastWasDigitStr )
				pendingDashStr = true;
			lastWasDigitStr = false;
			++i;
			continue;
		}

		// Skip condensible characters in thePrefix
		if( pc <= ' ' || pc == '_' ) { ++pi; continue; }
		if( pc == '-' )
		{
			if( lastWasDigitPre )
				pendingDashPre = true;
			lastWasDigitPre = false;
			++pi;
			continue;
		}

		bool isDigitStr = (sc >= '0' && sc <= '9');
		bool isDigitPre = (pc >= '0' && pc <= '9');

		if( pendingDashStr && isDigitStr )
		{
			if( !(pendingDashPre && isDigitPre) )
				return 0;
			pendingDashStr = pendingDashPre = false;
		}
		else if( pendingDashPre && isDigitPre )
		{
			return 0;
		}

		lastWasDigitStr = isDigitStr;
		lastWasDigitPre = isDigitPre;

		if( !(sc & 0x80) ) sc = u8(::toupper(sc));
		if( !(pc & 0x80) ) pc = u8(::toupper(pc));

		if( sc != pc )
			return 0;

		++i;
		++pi;
	}

	// If we didn't finish prefix, fail
	while (pre[pi])
	{
		u8 pc = (u8)pre[pi];
		if( pc > ' ' && pc != '_' && pc != '-' )
			return 0;
		++pi;
	}

	// Now skip trailing junk in theString
	while(str[i])
	{
		u8 c = (u8)str[i];
		if( c > ' ' && c != '-' && c != '_' )
			break;
		++i;
	}

	return i;
}


std::pair<std::string::size_type, std::string::size_type>
findStringTag(
	const std::string& theString,
	std::string::size_type theStartPos,
	const char* theTagStart, char theTagEnd)
{
	std::pair<std::string::size_type, std::string::size_type> result(
		std::string::npos, 0);
	std::string::size_type aTagStartPos =
		theString.find(theTagStart, theStartPos);
	if( aTagStartPos != std::string::npos )
	{
		// Find the closing theTagEnd (or if none found, not valid tag!)
		std::string::size_type aTagEndPos = theString.find(
			theTagEnd, aTagStartPos);
		if( aTagEndPos != std::string::npos )
		{
			// Only use the last theTagStart found before the closing theTagEnd
			result.first = theString.rfind(theTagStart, aTagEndPos);
			result.second = aTagEndPos - result.first + 1;
		}
	}

	return result;
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

	while(aCurValue > 0)
	{
		if( aPlace && aPlace % 3 == 0 )
			aResult = ',' + aResult;
		aResult = (char) ('0' + (aCurValue % 10)) + aResult;
		aCurValue /= 10;
		++aPlace;
	}

	return aResult;
}
