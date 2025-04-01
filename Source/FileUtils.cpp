//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Common.h"

#include <fstream>


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

std::string getAppFolder()
{
	return narrow(getAppFolderW());
}


std::wstring getAppFolderW()
{
	static std::wstring sAppFolder;
	if( sAppFolder.empty() )
	{
		WCHAR aBuffer[MAX_PATH];
		DWORD aLength = GetModuleFileName(NULL, aBuffer, MAX_PATH);
		if( aLength == 0 || aLength > MAX_PATH )
		{
			gHadFatalError = true;
			gErrorString = L"Failed to get access to own working directory!";
			return sAppFolder;
		}
		// End in trailing slash
		WCHAR* aSlashCharPtr = wcsrchr(aBuffer, L'\\');
		if( aSlashCharPtr )
			*(aSlashCharPtr+1) = L'\0';
		sAppFolder = aBuffer;
	}

	SetCurrentDirectory(sAppFolder.c_str());
	return sAppFolder;
}


std::string toAbsolutePath(const std::string& thePath, bool forcedAsDir)
{
	return narrow(toAbsolutePath(widen(thePath), forcedAsDir));
}


std::wstring toAbsolutePath(const std::wstring& thePath, bool forcedAsDir)
{
	WCHAR aBuffer[MAX_PATH];
	std::wstring aFullPath;

	// Expand environment variables in the path
	if( thePath.find(L'%') != std::wstring::npos)
	{
		DWORD aLength = ExpandEnvironmentStrings(
			thePath.c_str(), aBuffer, MAX_PATH);
		if( aLength == 0 || aLength > MAX_PATH )
			return L"";
		aFullPath.assign(aBuffer, aLength - 1);
	}
	else
	{
		aFullPath = thePath;
	}

	// Check if is a quoted path and strip any leading whitespace
	// Quoted paths have "" removed and any params after second " stripped
	bool isQuoted = false;
	std::wstring::size_type aPos;
	for(aPos = 0; aPos < aFullPath.size(); ++aPos)
	{
		if( aFullPath[aPos] == L'"' )
			isQuoted = true;
		else if( !iswspace(aFullPath[aPos]) )
			break;
	}
	if( aPos > 0 )
		aFullPath = aFullPath.substr(aPos);
	if( isQuoted )
		aFullPath = aFullPath.substr(0, aFullPath.find(L'"'));

	// Check if is a relative path (doesn't start with "C:\" or other drive)
	if( aFullPath.size() < 3 ||
		aFullPath[1] != L':' ||
		(aFullPath[2] != L'\\' && aFullPath[2] != L'/') ||
		!iswalpha(aFullPath[0]) )
	{
		// Set app directory as current for relative paths
		aFullPath = getAppFolderW() + aFullPath;		
	}

	// Always end in trailing slash when forcedAsDir
	// This may temporarily set it to end in two backslashes
	// but GetFullPathName will set it back to one after this
	if( forcedAsDir )
	{
		if( aFullPath[aFullPath.size()-1] == L':' )
			aFullPath.push_back('.');
		aFullPath.push_back('\\');
	}

	// Resolve things like .\ and ..\ and \\ and so on
	DWORD aLength = GetFullPathName(
		aFullPath.c_str(), MAX_PATH, aBuffer, NULL);
	if( aLength == 0 || aLength > MAX_PATH )
		return L"";

	return std::wstring(aBuffer, aLength);
}


bool isSamePath(const std::string& thePath1, const std::string& thePath2)
{
	return isSamePath(widen(thePath1), widen(thePath2));
}


bool isSamePath(const std::wstring& thePath1, const std::wstring& thePath2)
{
	// Consistently end in trailing slash, even if it is supposed to be
	// a file, to make sure matches if one ends in \ but the other doesn't
	const std::wstring& aCmpPath1 = toAbsolutePath(thePath1, true);
	const std::wstring& aCmpPath2 = toAbsolutePath(thePath2, true);
	return
		aCmpPath1.size() == aCmpPath2.size() &&
		CompareStringOrdinal(
			aCmpPath1.c_str(), int(aCmpPath1.length()),
			aCmpPath2.c_str(), int(aCmpPath2.length()),
			TRUE) == CSTR_EQUAL;
}


bool isValidFilePath(const std::string& theFilePath)
{
	return isValidFilePath(widen(theFilePath));
}


bool isValidFilePath(const std::wstring& theFilePath)
{
	const DWORD aFileAttributes = GetFileAttributes(theFilePath.c_str());

	return
		aFileAttributes != INVALID_FILE_ATTRIBUTES &&
		!(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}


bool isValidFolderPath(const std::string& thePath)
{
	return isValidFolderPath(widen(thePath));
}


bool isValidFolderPath(const std::wstring& thePath)
{
	const DWORD aFileAttributes = GetFileAttributes(thePath.c_str());

	return
		aFileAttributes != INVALID_FILE_ATTRIBUTES &&
		(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}


std::wstring getTempFilePathFor(const std::wstring& theFileToReplace)
{
	DBG_ASSERT(!theFileToReplace.empty());

	static std::wstring sSystemTempFolderPath;
	static std::wstring sSystemTempFolderVolume;
	if( sSystemTempFolderPath.empty() )
	{
		wchar_t aBuffer[MAX_PATH] = {0};
		DWORD aLength = GetTempPath(MAX_PATH, aBuffer);
		if( aLength > 0 && aLength <= MAX_PATH )
		{
			sSystemTempFolderPath = aBuffer;
			if( GetVolumePathName(
					sSystemTempFolderPath.c_str(), aBuffer, MAX_PATH) )
			{
				sSystemTempFolderVolume = aBuffer;
			}
		}
	}

	// Get volume path for the original file
	std::wstring aFileVolume;
	{
		wchar_t aBuffer[MAX_PATH] = {0};
		if( GetVolumePathName(theFileToReplace.c_str(), aBuffer, MAX_PATH) )
			aFileVolume = aBuffer;
	}

	// ReplaceFile requires both files be on the same volume
	// If system temp folder is on a different volume, use the
	// file's current folder instead
	std::wstring aTargetDir;
	const std::wstring::size_type aLastSlash =
		theFileToReplace.find_last_of(L"\\/");
	if( isSamePath(sSystemTempFolderVolume, aFileVolume) )
	{
		aTargetDir = sSystemTempFolderPath;
	}
	else
	{
		if( aLastSlash != std::wstring::npos )
			aTargetDir = theFileToReplace.substr(0, aLastSlash+1);
		else
			aTargetDir = L".\\";
	}
	DBG_ASSERT(!aTargetDir.empty());

	// Generate temporary file name
	std::wstring aTempFileName = L"~" +
		((aLastSlash != std::wstring::npos)
			? theFileToReplace.substr(aLastSlash + 1)
			: theFileToReplace);

	// Append temp file name to temp path
	return aTargetDir + aTempFileName;
}


bool writeResourceToFile(
	WORD theResID,
	const wchar_t* theResType,
	const wchar_t* theDestFilePath)
{
	HRSRC hResource = FindResource(NULL,
		MAKEINTRESOURCE(theResID), theResType);
	if( !hResource )
		return false;
	HGLOBAL hGlobal = LoadResource(NULL, hResource);
	if( !hGlobal )
		return false;

	void* aData = LockResource(hGlobal);
	DWORD aSize = SizeofResource(NULL, hResource);

	std::ofstream aFile(theDestFilePath, std::ios::binary | std::ios::trunc);
	if( !aFile.is_open() )
	{
		FreeResource(hGlobal);
		return false;
	}
	
	aFile.write(static_cast<char*>(aData), aSize);
	aFile.close();
	FreeResource(hGlobal);
	return true;
}


bool getExeArchitecture(const std::wstring& theExePath, bool& theBitWidthIs64)
{
	HANDLE hFile = CreateFile(
		theExePath.c_str(), GENERIC_READ,
		FILE_SHARE_READ, 
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if( hFile == INVALID_HANDLE_VALUE )
		return false;

	HANDLE hMapping =
		CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if( !hMapping )
	{
		CloseHandle(hFile);
		return false;
	}

	void* pBase = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
	if( !pBase )
	{
		CloseHandle(hMapping);
		CloseHandle(hFile);
		return false;
	}

	// Parse PE headers
	IMAGE_DOS_HEADER* pDosHeader = (IMAGE_DOS_HEADER*)pBase;
	IMAGE_NT_HEADERS* pNtHeaders = (IMAGE_NT_HEADERS*)
		((BYTE*)pBase + pDosHeader->e_lfanew);

	// Check bit width
	theBitWidthIs64 =
		pNtHeaders->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;

	UnmapViewOfFile(pBase);
	CloseHandle(hMapping);
	CloseHandle(hFile);

	return true;
}
