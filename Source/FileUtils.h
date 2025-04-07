//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

// Returns full path to folder containing this app's .exe (+ trailing slash)
// Also changes current working directory to become this same folder!
std::string getAppFolder();
std::wstring getAppFolderW();
// Expands environment variables and converts relative paths to absolute paths
// Assumes relative paths are relative to location of the application's .exe
// Also removes double quotes around paths (and params after the quotes).
std::string toAbsolutePath(const std::string& thePath, bool dir = false);
std::wstring toAbsolutePath(const std::wstring& thePath, bool dir = false);
// See StringUtils.h for many more functions related to file paths

bool isSamePath(const std::string& thePath1, const std::string& thePath2);
bool isSamePath(const std::wstring& thePath1, const std::wstring& thePath2);
bool isValidFilePath(const std::string& theFilePath);
bool isValidFilePath(const std::wstring& theFilePath);
bool isValidFolderPath(const std::string& thePath);
bool isValidFolderPath(const std::wstring& thePath);
// Gets a path to a temporary file that can work with ReplaceFile
std::wstring getTempFilePathFor(const std::wstring& theFileToReplace);

// Will overwrite any existing file at theDestFilePath, or return false
bool writeResourceToFile(
	WORD theResID,
	const wchar_t* theResType,
	const wchar_t* theDestFilePath);

// Deletes given file
bool deleteFile(const std::wstring& theFileToDelete);

// Returns architecture of given .exe file (or returns false if fails)
bool getExeArchitecture(const std::wstring& theExePath, bool& theBitWidthIs64);
