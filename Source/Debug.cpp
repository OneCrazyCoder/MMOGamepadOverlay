//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#include "Common.h"

#include <fstream>
#include <ctime>


//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

std::wstring gErrorString;


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

bool sHadFatalError = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

void logErrorInternal(const char* fmt, va_list argList)
{
	std::string anErrorStr = vformat(fmt, argList);

#ifndef NDEBUG
	{
		std::string aStr("<< ERROR LOG >>: ");
		aStr += anErrorStr + "\n";
		OutputDebugStringW(widen(aStr).c_str());
	}
#endif

	// Attempt to write to error log
    time_t now = time(0);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
	char aTimeStamp[32];
	strftime(aTimeStamp, sizeof(aTimeStamp), "<%Y-%m-%d %H:%M:%S> ", &timeinfo);
	std::ofstream errorFile("ErrorLog.txt", std::ios_base::app);
	if(errorFile.is_open())
	{
		errorFile << aTimeStamp << anErrorStr << std::endl;
		errorFile.close();
	}

	// Store the *first* error logged, as later errors are likely
	// to have stemmed from the first error anyway.
	if( gErrorString.empty() )
		gErrorString = widen(anErrorStr);
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void debugPrint(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	std::string result = vformat(fmt, argList);
	va_end(argList);

	OutputDebugStringW(widen(result).c_str());
}


void logError(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	logErrorInternal(fmt, argList);
	va_end(argList);
}


void logFatalError(const char* fmt ...)
{
	// For first fatal error encountered, overwrite sErrorString
	if( !sHadFatalError )
		gErrorString.clear();

	va_list argList;
	va_start(argList, fmt);
	logErrorInternal(fmt, argList);
	va_end(argList);

	sHadFatalError = true;
}


bool hadFatalError()
{
	return sHadFatalError;
}
