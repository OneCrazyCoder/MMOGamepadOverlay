//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

#include "Debug.h"

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>

std::string gLastError;
bool gHadFatalError = false;

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
	gLastError = vformat(fmt, argList);
	va_end(argList);

#ifndef NDEBUG
	{
		std::string aStr("<< ERROR LOG >>: ");
		aStr += gLastError + "\n";
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
		errorFile << aTimeStamp << gLastError << std::endl;
		errorFile.close();
	}
}
