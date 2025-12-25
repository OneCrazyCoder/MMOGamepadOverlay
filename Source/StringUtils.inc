//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

inline std::string strFormat(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	std::string result = vformat(fmt, argList);
	va_end(argList);

	return result;
}


inline bool hasPrefix(
	const std::string& theString, const std::string& thePrefix)
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


inline std::string toString(FILETIME theValue)
{
	char aBuf[24];
	SYSTEMTIME aSystemTime;
	FileTimeToSystemTime(&theValue, &aSystemTime);
	sprintf(aBuf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
		aSystemTime.wYear, aSystemTime.wMonth, aSystemTime.wDay,
		aSystemTime.wHour, aSystemTime.wMinute, aSystemTime.wSecond,
		aSystemTime.wMilliseconds);
	return std::string(aBuf);
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


inline int stringToInt(const std::string& theString)
{
	return atoi(theString.c_str());
}


inline int stringToInt(const char* theString)
{
	return atoi(theString);
}


inline u32 stringToU32(const std::string& theString)
{
	return u32(strtoul(theString.c_str(), NULL, 10));
}


inline u32 stringToU32(const char* theString)
{
	return u32(strtoul(theString, NULL, 10));
}


inline float stringToFloat(const std::string& theString)
{
	double result = stringToDoubleStrict(theString.c_str());
	if( _isnan(result) )
		result = 0;
	return float(result);
}


inline float stringToFloat(const char* theString)
{
	double result = stringToDoubleStrict(theString);
	if( _isnan(result) )
		result = 0;
	return float(result);
}


inline double stringToDouble(const std::string& theString)
{
	double result = stringToDoubleStrict(theString.c_str());
	if( _isnan(result) )
		result = 0;
	return result;
}


inline double stringToDouble(const char* theString)
{
	double result = stringToDoubleStrict(theString);
	if( _isnan(result) )
		result = 0;
	return result;
}


inline double stringToDoubleStrict(const char* theString)
{
	if( !theString || *theString == '\0' )
		return std::numeric_limits<double>::quiet_NaN();

	char* endptr = null;
	double result = strtod(theString, &endptr);
	if( *endptr == '\0' )
		return result;

	if( endptr != theString && *(endptr + 1) == '\0' && *endptr == '%' )
		return result / 100.0;

	// Replace various boolean representations with 0.0 or 1.0
	switch(theString[0])
	{
	case 't': case 'T': case 'f': case 'F':
	case 'y': case 'Y': case 'n': case 'N':
	case 'o': case 'O': case 'd': case 'D':
	case 'e': case 'E': case 'a': case 'A':
		{
			const std::string& str = lower(theString);
			if( str == "true" || str == "yes" || str == "on" ||
				str == "enabled" || str == "ok" || str == "active" )
			{ return 1.0; }

			if( str == "false" || str == "no" || str == "off" ||
				str == "nothing" || str == "null" || str == "nil" ||
				str == "disabled" || str == "empty" )
			{ return 0; }
		}
		break;
	}

	return std::numeric_limits<double>::quiet_NaN();
}


inline double stringToDoubleStrict(const std::string& theString)
{
	return stringToDoubleStrict(theString.c_str());
}


inline bool stringToBool(const std::string& theString)
{
	return !theString.empty() &&
		stringToDoubleStrict(theString) != 0.0;
}


inline bool stringToBool(const char* theString)
{
	return theString != null && theString[0] != '\0' &&
		stringToDoubleStrict(theString) != 0.0;
}


inline FILETIME stringToFileTime(const std::string& theString)
{
	return stringToFileTime(theString.c_str());
}


inline FILETIME stringToFileTime(const char* theString)
{
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, ms = 0;
	std::sscanf(theString, "%d-%d-%dT%d:%d:%d.%d",
		&year, &month, &day, &hour, &minute, &second, &ms);
	SYSTEMTIME aSystemTime;
	aSystemTime.wYear = dropTo<WORD>(year);
	aSystemTime.wMonth = dropTo<WORD>(month);
	aSystemTime.wDay = dropTo<WORD>(day);
	aSystemTime.wHour = dropTo<WORD>(hour);
	aSystemTime.wMinute = dropTo<WORD>(minute);
	aSystemTime.wSecond = dropTo<WORD>(second);
	aSystemTime.wMilliseconds = dropTo<WORD>(ms);

	FILETIME result = FILETIME();
	SystemTimeToFileTime(&aSystemTime, &result);
	return result;
}
