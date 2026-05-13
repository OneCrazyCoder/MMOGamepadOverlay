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
void logToConsole(const std::string&) {}
#else
void logToConsole(const std::string& theMsg)
{ OutputDebugString(widen(theMsg + "\n").c_str()); }
#endif


void logToFile(const std::string& theString)
{
	static std::wstring sLogFilePath;
	if( sLogFilePath.empty() )
	{
		const std::wstring& aFolderPath = getAppFolderW();
		if( aFolderPath.empty() )
			return;
		sLogFilePath = aFolderPath + L"MMOGO.log";
		// Does this log file already exist?
		if( isValidFilePath(sLogFilePath) )
		{
			// Change MMOGO.log with MMOGO-prev.log, overwriting any previous
			// instance of MMOGO-prev.log
			std::wstring aPrevLogFilePath = aFolderPath + L"MMOGO-prev.log";
			MoveFileEx(
				sLogFilePath.c_str(),
				aPrevLogFilePath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED
			);
		}
	}

	time_t now = time(0);
	::tm timeinfo;
	localtime_s(&timeinfo, &now);
	char aTimeStamp[32];
	strftime(aTimeStamp, sizeof(aTimeStamp), "<%Y-%m-%d %H:%M:%S> ", &timeinfo);
	std::ofstream aSessionLogFile(sLogFilePath.c_str(), std::ios_base::app);
	if( aSessionLogFile.is_open() )
	{
		aSessionLogFile << aTimeStamp << theString << std::endl;
		aSessionLogFile.close();
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

	OutputDebugString(widen(result).c_str());
}


void logInfo(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& aString = vformat(fmt, argList);
	va_end(argList);

	logToConsole("LOG: " + aString);
	logToFile(aString);
}


void logNotice(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& aNoticeString = vformat(fmt, argList);
	va_end(argList);

	// Store most recent notice, as past ones may no longer matter
	gNoticeString = widen(aNoticeString);

	logToConsole(aNoticeString);
	logToFile(aNoticeString);
}


void logError(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	const std::string& anErrorString = vformat(fmt, argList);
	va_end(argList);

	// Store just the *first* error logged, as later errors are likely
	// to have stemmed from the first error anyway.
	if( gErrorString.empty() )
		gErrorString = widen(anErrorString);

	logToConsole(std::string("ERROR: ") + anErrorString);
	logToFile(std::string("ERROR: ") + anErrorString);
}


void logFatalError(const char* fmt ...)
{

	va_list argList;
	va_start(argList, fmt);
	const std::string& anErrorString = vformat(fmt, argList);
	va_end(argList);

	// For first fatal error encountered, overwrite gErrorString if it
	// is from a non-fatal error, otherwise leave it as first error
	if( !gHadFatalError )
		gErrorString.clear();
	if( gErrorString.empty() )
		gErrorString = widen(anErrorString);

	logToConsole(std::string("!!! FATAL ERROR !!!: ") + anErrorString);
	logToFile(std::string("!!! FATAL ERROR !!!: ") + anErrorString);

	gHadFatalError = true;
}


bool hadFatalError()
{
	return gHadFatalError;
}
