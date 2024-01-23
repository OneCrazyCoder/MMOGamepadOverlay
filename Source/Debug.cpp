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
std::wstring gNoticeString;


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

bool sHadFatalError = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

void logToConsole(const std::string& thePrefix, const std::string& theMsg)
{
	#ifndef NDEBUG
	const std::string& aStr = thePrefix + theMsg + "\n";
	OutputDebugStringW(widen(aStr).c_str());
	#endif
}


void logToErrorFile(const std::string& theErrorString)
{
	static std::wstring sErrorLogFilePath;
	if( sErrorLogFilePath.empty() )
	{
		WCHAR aPathW[MAX_PATH];
		GetModuleFileName(NULL, aPathW, MAX_PATH);
		sErrorLogFilePath =
			widen(getFileDir(narrow(aPathW), true) + "ErrorLog.txt");
	}

	time_t now = time(0);
	struct tm timeinfo;
	localtime_s(&timeinfo, &now);
	char aTimeStamp[32];
	strftime(aTimeStamp, sizeof(aTimeStamp), "<%Y-%m-%d %H:%M:%S> ", &timeinfo);
	std::ofstream errorFile(sErrorLogFilePath.c_str(), std::ios_base::app);
	if( errorFile.is_open() )
	{
		errorFile << aTimeStamp << theErrorString << std::endl;
		errorFile.close();
	}
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


void logNotice(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& aNoticeString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("", aNoticeString);
	logToErrorFile(aNoticeString);
	// Store most recent notice, as past ones may no longer matter
	gNoticeString = widen(aNoticeString);
}


void logError(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& anErrorString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("<< ERROR LOG >>: ", anErrorString);
	logToErrorFile(anErrorString);
	// Store the *first* error logged, as later errors are likely
	// to have stemmed from the first error anyway.
	if( gErrorString.empty() )
		gErrorString = widen(anErrorString);
}


void logFatalError(const char* fmt ...)
{
	// For first fatal error encountered, overwrite sErrorString
	if( !sHadFatalError )
		gErrorString.clear();

	va_list argList;
	va_start(argList, fmt);
	const std::string& anErrorString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("!!! FATAL ERROR !!!: ", anErrorString);
	logToErrorFile(anErrorString);
	if( gErrorString.empty() )
		gErrorString = widen(anErrorString);

	sHadFatalError = true;
}


bool hadFatalError()
{
	return sHadFatalError;
}
