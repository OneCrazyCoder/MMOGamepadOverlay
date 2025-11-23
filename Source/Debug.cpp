//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#pragma once

#include "Common.h"

#include <fstream>
#include <ctime>


//------------------------------------------------------------------------------
// Global Variables
//------------------------------------------------------------------------------

std::wstring gErrorString;
std::wstring gNoticeString;
bool gHadFatalError = false;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

#ifdef NDEBUG
void logToConsole(const std::string&, const std::string&) {}
#else
void logToConsole(const std::string& thePrefix, const std::string& theMsg)
{
	const std::string& aStr = thePrefix + theMsg + "\n";
	OutputDebugStringW(widen(aStr).c_str());
}
#endif


void logToErrorFile(const std::string& theErrorString)
{
	static std::wstring sErrorLogFilePath;
	if( sErrorLogFilePath.empty() )
	{
		sErrorLogFilePath = getAppFolderW();
		if( sErrorLogFilePath.empty() )
			return;
		sErrorLogFilePath += L"MMOGO_ErrorLog.txt";
	}

	time_t now = time(0);
	::tm timeinfo;
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


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void debugPrint(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	std::string result = vformat(fmt, argList);
	va_end(argList);

	OutputDebugStringW(widen(result).c_str());
}


void logToFile(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& anErrorString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("Logged to error file: ", anErrorString);
	logToErrorFile(anErrorString);
}


void logNotice(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& aNoticeString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("", aNoticeString);
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
	if( !gHadFatalError )
		gErrorString.clear();

	va_list argList;
	va_start(argList, fmt);
	const std::string& anErrorString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("!!! FATAL ERROR !!!: ", anErrorString);
	logToErrorFile(anErrorString);
	if( gErrorString.empty() )
		gErrorString = widen(anErrorString);

	gHadFatalError = true;
}


bool hadFatalError()
{
	return gHadFatalError;
}
