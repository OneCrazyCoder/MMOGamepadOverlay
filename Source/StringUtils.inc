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


inline bool hasPrefix(const std::string& theString, const std::string& thePrefix)
{
	return posAfterPrefix(theString, thePrefix) > 0;
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
	if( !theString.empty() && theString[theString.size()-1] == '%' )
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
	if( !theString.empty() && theString[theString.size()-1] == '%' )
		result /= 100.0;
	if( result == 0 )
	{
		switch(theString[0])
		{
		case 't': case 'T':
			if( lower(theString) == "true" )
				result = 1.0;
			break;
		case 'Y': case 'y':
			if( lower(theString) == "yes" )
				result = 1.0;
			break;
		case 'O': case 'o':
			if( lower(theString) == "on" )
				result = 1.0;
			break;
		}
	}
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
