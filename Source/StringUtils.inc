//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

inline std::string strFormat(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	std::string result = vformat(fmt, argList);
	va_end(argList);

	return result;
}

inline std::string toString(s32 theValue)
{
	char aBuf[12];
	sprintf(aBuf, "%d", theValue);
	return std::string(aBuf);
}

inline std::string toString(s16 theValue)
{
	char aBuf[7];
	sprintf(aBuf, "%d", theValue);
	return std::string(aBuf);
}


inline std::string toString(s8 theValue)
{
	char aBuf[4];
	sprintf(aBuf, "%d", theValue);
	return std::string(aBuf);
}


inline std::string toString(u32 theValue)
{
	char aBuf[11];
	sprintf(aBuf, "%u", theValue);
	return std::string(aBuf);
}


inline std::string toString(u16 theValue)
{
	char aBuf[6];
	sprintf(aBuf, "%u", theValue);
	return std::string(aBuf);
}


inline std::string toString(u8 theValue)
{
	char aBuf[3];
	sprintf(aBuf, "%u", theValue);
	return std::string(aBuf);
}


inline std::string toString(float theValue)
{
	// This is slower than sprintf but has nicer output because it cuts off
	// trailing 0's and removes decimals entirely for integer values. Also
	// easier since the string buffer size for floats is hard to nail down.
	std::ostringstream stream;
	stream << theValue;
	return stream.str();
}


inline std::string toString(double theValue)
{
	// Same reasoning as for float version here
	std::ostringstream stream;
	stream << theValue;
	return stream.str();
}


inline bool isAnInteger(const std::string& theString)
{
	return isAnInteger(theString.c_str());
}


inline bool isAnInteger(const char* theString)
{
	if( !theString || *theString == '\0' || *theString == ',' )
		return false;
	if( *theString == '-' )
		++theString;

	bool foundNumeral = false;
	while(*theString != '\0')
	{
		if( *theString >= '0' && *theString <= '9' )
			foundNumeral = true;
		else if( *theString != ',' )
			return false;
		++theString;
	}

	return foundNumeral;
}


inline int intFromString(const std::string& theString)
{
	return atoi(theString.c_str());
}


inline int intFromString(const char* theString)
{
	return atoi(theString);
}


inline u32 u32FromString(const std::string& theString)
{
	return u32(strtoul(theString.c_str(), NULL, 10));
}


inline u32 u32FromString(const char* theString)
{
	return u32(strtoul(theString, NULL, 10));
}


inline u32 u32FromHexString(const std::string& theString)
{
	return u32(strtoul(theString.c_str(), NULL, 16));
}


inline u32 u32FromHexString(const char* theString)
{
	return u32(strtoul(theString, NULL, 16));
}


inline s64 s64FromString(const std::string& theString)
{
	return strtol(theString.c_str(), NULL, 10);
}


inline s64 s64FromString(const char* theString)
{
	return strtol(theString, NULL, 10);
}


inline u64 u64FromString(const std::string& theString)
{
	return strtoul(theString.c_str(), NULL, 10);
}


inline u64 u64FromString(const char* theString)
{
	return strtoul(theString, NULL, 10);
}


inline float floatFromString(const std::string& theString)
{
	float result = float(atof(theString.c_str()));
	if( theString[theString.size()-1] == '%' )
		result /= 100.0;
	return result;
}


inline float floatFromString(const char* theString)
{
	return floatFromString(std::string(theString));
}


inline double doubleFromString(const std::string& theString)
{
	double result = double(atof(theString.c_str()));
	if( theString[theString.size()-1] == '%' )
		result /= 100.0;
	return result;
}


inline double doubleFromString(const char* theString)
{
	return doubleFromString(std::string(theString));
}


inline std::vector<u32> UTF8ToUTF32(const std::string& theString)
{
	return UTF8ToUTF32(theString.c_str());
}


inline bool boolFromString(const std::string& theString)
{
	return boolFromString(theString.c_str());
}


inline bool boolFromString(const char* theString)
{
	if( !theString ||
		theString[0] == '0' ||
		theString[0] == 'n' || // no
		theString[0] == 'N' || // No
		theString[0] == 'f' || // false
		theString[0] == 'F' || // False
		(theString[0] == 'O' && theString[1] == 'F') || // OFF
		(theString[0] == 'O' && theString[1] == 'f') || // Off
		(theString[0] == 'o' && theString[1] == 'f') || // off
		theString[0] == '\0' )
	{
		return false;
	}
	return true;
}


inline std::string UTF32ToUTF8(const std::vector<u32>& theVector)
{
	std::string aResult;
	for(size_t i = 0; i < theVector.size(); ++i)
		appendUTF8(theVector[i], aResult);
	return aResult;
}


inline std::string substrUTF8(const std::string& theString, size_t theFirstCodePointPos, size_t theCodePointLength)
{
	return substrUTF8(theString.c_str(), theFirstCodePointPos, theCodePointLength);
}


inline EUTF8DecodeResult decodeUTF8(u32* theState, u32* theCodePoint, unsigned char theByte)
{
	//-------------------------------------------------------------------------
	// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
	// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
	//-------------------------------------------------------------------------
	static const u8 utf8d[] = {
		// The first part of the table maps bytes to character classes
		// to reduce the size of the transition table and create bitmasks.
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
		10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

		// The second part is a transition table that maps a combination
		// of a state of the automaton and a character class to a state.
		0, 12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
		12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
		12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
		12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
		12,36,12,12,12,12,12,12,12,12,12,12,
	};

	const u32 aByteType = utf8d[theByte];

	*theCodePoint = (*theState) ?
		((*theCodePoint << 6) | (theByte & 0x3F)) :
		((0xFF >> aByteType) & theByte);

	*theState = utf8d[256 + *theState + aByteType];
	return EUTF8DecodeResult(*theState);
}


inline void appendUTF8(u32 theCodePoint, std::string& theDestString)
{
	if( theCodePoint < 0x80 )
	{
		theDestString += char(theCodePoint);
	}
	else if( theCodePoint < 0x800 )
	{
		theDestString += char(192 + theCodePoint / 64);
		theDestString += char(128 + theCodePoint % 64);
	}
	else if( theCodePoint < 0x10000 )
	{
		theDestString += char(224 + theCodePoint / 4096);
		theDestString += char(128 + theCodePoint / 64%64);
		theDestString += char(128 + theCodePoint % 64);
	}
	else
	{
		theDestString += char(240 + theCodePoint / 262144);
		theDestString += char(128 + theCodePoint / 4096 % 64);
		theDestString += char(128 + theCodePoint / 64 % 64);
		theDestString += char(128 + theCodePoint % 64);
	}
}
