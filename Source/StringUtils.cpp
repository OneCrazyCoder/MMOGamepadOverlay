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

	int aBufferSize = kInitialBufferSize;
	int aResult = vsnprintf(aCStringPtr, aBufferSize, fmt, argPtr);

	while((aResult < 0 || aResult >= aBufferSize) && aBufferSize < kMaxBufferSize)
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
			ch = ::toupper(ch);
		aString += ch;
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


std::string replaceAllStr(const std::string& theString, const char* oldStr, const char* newStr)
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

bool wildcardMatch(
	const wchar_t* theString,
	const wchar_t* thePattern,
	std::vector<std::wstring>* out)
{
	const wchar_t* aLastStar = NULL;
	const wchar_t* aLastMatch = NULL;

	bool newMatchStarted = false;
	while(*theString)
	{
		if( *thePattern == '*' )
		{
			aLastStar = thePattern++;
			aLastMatch = theString;
			newMatchStarted = true;
		}
		else if( towupper(*theString) == towupper(*thePattern) )
		{
			++theString;
			++thePattern;
		}
		else if( aLastStar )
		{
			if( out )
			{
				if( newMatchStarted )
					out->push_back(L"");
				out->back().push_back(towupper(*aLastMatch));
				newMatchStarted = false;
			}
			thePattern = aLastStar + 1;
			++aLastMatch;
			theString = aLastMatch;
		}
		else
		{
			return false;
		}
	}

	// Trailing *'s
	while(*thePattern == '*')
		++thePattern;

	return *thePattern == '\0';
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


std::string breakOffItemBeforeChar(std::string& theString, char theChar)
{
	std::string result;
	size_t aStripCount = 0;

	size_t aCharPos = theString.find(theChar);
	if( aCharPos != std::string::npos )
	{
		result = trim(theString.substr(0, aCharPos));
		// If result is non-empty, strip everything up to and include theChar
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


std::string breakOffNextItem(std::string& theString, char theChar)
{
	std::string result;
	size_t aStripCount = 0;

	bool inQuotes = false;
	size_t aCharPos;
	for(aCharPos = 0; aCharPos < theString.size(); ++aCharPos)
	{
		if( theString[aCharPos] == '"' )
			inQuotes = !inQuotes;
		if( theString[aCharPos] == theChar && !inQuotes )
			break;
	}

	result = trim(theString.substr(0, aCharPos));
	// Always strip theChar, even if result is empty
	aStripCount = aCharPos + 1;
	// Remove double quotes around result
	if( !result.empty() && result[0] == '"' )
		result = result.substr(1);
	if( !result.empty() && result[result.size()-1] == '"' )
		result.resize(result.size()-1);

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
	int aStrPos = int(theString.size())-1;
	while(
		aStrPos >= 0 &&
		theString[aStrPos] >= '0' &&
		theString[aStrPos] <= '9' )
	{
		result += int(theString[aStrPos] - '0') * multiplier;
		multiplier *= 10;
		--aStrPos;
	}

	// Don't chop off leading zeroes before the actual integer
	while(aStrPos < int(theString.size())-1 && theString[aStrPos+1] == '0' )
		++aStrPos;

	// If stayed at end of string or got all the way to start, invalid as a
	// string ending in (but not entirely being) an integer
	if( aStrPos >= int(theString.size())-1 || aStrPos < 0 )
		result = -1;
	else
		theString.resize(aStrPos+1);

	while(!theString.empty() && isspace(theString[int(theString.size())-1]))
		theString.resize(theString.size()-1);

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

		if( !(sc & 0x80) ) sc = ::toupper(sc);
		if( !(pc & 0x80) ) pc = ::toupper(pc);

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
findStringTag(const std::string& theString, std::string::size_type theStartPos)
{
	std::pair<std::string::size_type, std::string::size_type> result;
	result.second = 0;
	result.first = theString.find('<', theStartPos);
	if( result.first != std::string::npos )
	{
		// Find the closing '>' (or if none found, we're done)
		std::string::size_type anEndPos = theString.find('>', result.first);
		if( anEndPos != std::string::npos )
		{
			// Only use the last '<' found before the closing '>'
			result.first = theString.rfind('<', anEndPos);
			result.second = anEndPos - result.first + 1;
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
