//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Profile.h"

#include "Dialogs.h"
#include "Lookup.h"
#include "Resources/resource.h"
#include "StringUtils.h"

#include <fstream>

namespace Profile
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kINIFileBufferSize = 256, // How many characters to process at a time from .ini
};

struct ResourceProfile
{
	const char* name;
	const WORD resID;
};

struct ProfileEntry
{
	std::string name;
	std::string path;
	DWORD id;
};

const char* kProfilePrefix = "MMOGO_";
const char* kProfileSuffix = ".ini";
const char* kCoreProfileName = "MMOGO_Core.ini";
const char* kAutoLaunchAppKey = "SYSTEM/AUTOLAUNCHAPP";
const char* kAutoLaunchAppParamsKey = "SYSTEM/AUTOLAUNCHAPPPARAMS";
const std::string kEndOfLine = "\r\n";

const ResourceProfile kResTemplateCore =
	{	"Core",			IDR_TEXT_INI_CORE		};

const ResourceProfile kResTemplateBase[] =
{//		name			resID
	{	"MnM Base",		IDR_TEXT_INI_BASE_MNM	},
	{	"P99 Base",		IDR_TEXT_INI_BASE_P99	},
	{	"PQ Base",		IDR_TEXT_INI_BASE_PQ	},
};

const ResourceProfile kResTemplateDefault[] =
{//		name			resID
	{	"MnM Default",	IDR_TEXT_INI_DEF_MNM	},
	{	"P99 Default",	IDR_TEXT_INI_DEF_P99	},
	{	"PQ Default",	IDR_TEXT_INI_DEF_PQ		},
};

enum EParseMode
{
	eParseMode_Header,
	eParseMode_Categories,
};

typedef void (*ParseINICallback)(
	const std::string& theKey,
	const std::string& theValue,
	void* theUserData);
typedef StringToValueMap<std::string> StringsMap;
typedef std::vector<std::string> StringsVec;


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static StringsMap sSettingsMap;
static std::vector<ProfileEntry> sKnownProfiles;
static std::vector< std::vector<int> > sProfilesCanLoad;
static std::string sLoadedProfileName;
static int sAutoProfileIdx = 0;
static int sNewBaseProfileIdx = -1;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static const std::string& iniFolderPath()
{
	static std::string sFolderPath;
	if( sFolderPath.empty() )
	{
		// Just use our application folder (for now)
		TCHAR aPath[_MAX_PATH];
		GetModuleFileName(NULL, aPath, _MAX_PATH);
		sFolderPath = getFileDir(narrow(aPath), true);
		_wchdir(widen(sFolderPath).c_str());

		// Make sure it actually exists
		// (obviously it has to, but leaving this here for possibly
		// later changing the .ini folder to AppData or something)
		size_t aCurPos = 0;
		for (;;)
		{
			size_t aSlashPos = sFolderPath.find_first_of("\\/", aCurPos);
			if (aSlashPos == -1)
			{
				_wmkdir(widen(sFolderPath).c_str());
				break;
			}

			aCurPos = aSlashPos+1;

			std::string aCurPath = sFolderPath.substr(0, aSlashPos);
			_wmkdir(widen(aCurPath).c_str());
		}
	}

	return sFolderPath;
}


static std::string extractProfileName(const std::string& theString)
{
	std::string result = removeExtension(getFileName(theString));
	const std::string prefix = kProfilePrefix;
	if( upper(result).compare(0, prefix.length(), upper(prefix)) == 0 )
		result = result.substr(prefix.length());
	return replaceChar(result, '_', ' ');
}


static std::string profileNameToFilePath(const std::string& theName)
{
	DBG_ASSERT(theName == extractProfileName(theName));
	std::string result = iniFolderPath();
	result += kProfilePrefix;
	result += replaceChar(theName, ' ', '_');
	result += kProfileSuffix;
	return result;
}


static DWORD uniqueFileIdentifier(const std::string& theFilePath)
{
	DWORD result = 0; // 0 == invalid file
	if( theFilePath.empty() )
		return result;

	const std::wstring& aFilePathW = widen(theFilePath);
	const DWORD aFileAttributes = GetFileAttributes(aFilePathW.c_str());
	if( aFileAttributes == INVALID_FILE_ATTRIBUTES ||
		(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
		return result;

	HANDLE hFile = CreateFile(aFilePathW.c_str(),
		GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if( hFile != INVALID_HANDLE_VALUE )
	{
		BY_HANDLE_FILE_INFORMATION aFileInfo;
		if( GetFileInformationByHandle(hFile, &aFileInfo) )
		{
			result = aFileInfo.dwVolumeSerialNumber ^
				((aFileInfo.nFileIndexHigh << 16) | aFileInfo.nFileIndexLow) ^
				aFileInfo.ftCreationTime.dwHighDateTime ^
				aFileInfo.ftCreationTime.dwLowDateTime;
		}
		CloseHandle(hFile);
	}

	return result;
}


static ProfileEntry profileNameToEntry(const std::string& theProfileName)
{
	ProfileEntry anEntry;
	anEntry.name = extractProfileName(theProfileName);
	anEntry.path = profileNameToFilePath(anEntry.name);
	anEntry.id = uniqueFileIdentifier(anEntry.path);
	return anEntry;
}


static int getOrAddProfileIdx(const ProfileEntry& theProfileEntry)
{
	// A map would be faster, but requires a separate Map structure
	// to ensure stable indexes (or using pointers instead), and
	// there's unlikely to be a whole lot of these anyway.
	for(int i = 0; i < sKnownProfiles.size(); ++i)
	{
		if( sKnownProfiles[i].id == theProfileEntry.id )
			return i;
	}

	sKnownProfiles.push_back(theProfileEntry);
	return int(sKnownProfiles.size()) - 1;
}


static bool profileExists(const std::string& theFilePath)
{
	const DWORD aFileAttributes = GetFileAttributesW(
		widen(theFilePath).c_str());

	return
		aFileAttributes != INVALID_FILE_ATTRIBUTES &&
		!(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}


static void parseINI(
	ParseINICallback theCallbackFunc,
	const std::string& theFilePath,
	EParseMode theParseMode,
	void* theUserData = NULL)
{
	std::ifstream aFile(widen(theFilePath).c_str(), std::ios::binary);
	if( !aFile.is_open() )
	{
		logFatalError("Could not open file %s", theFilePath.c_str());
		return;
	}

	// Prepare input buffer
	// +1 on size is to accommodate an extra newline character when
	// reach end of file
	char aBuffer[kINIFileBufferSize+1];
	std::streamsize aBufferSize = kINIFileBufferSize;
	std::string aNewCategory;
	std::string aCategory;
	std::string aKey;
	std::string aValue;

	enum
	{
		ePIState_Whitespace,
		ePIState_Category,
		ePIState_Key,
		ePIState_Value,
		ePIState_Comment,
	} aState = ePIState_Whitespace;

	while(aFile.good())
	{
		// Read next chunk from file
		aFile.read(aBuffer, aBufferSize);
		if( aFile.eof() || aFile.gcount() < aBufferSize )
		{// Put eol at end of file in case there wasn't one already
			aBufferSize = aFile.gcount() + 1;
			aBuffer[aBufferSize-1] = '\n';
			DBG_ASSERT(aBufferSize <= ARRAYSIZE(aBuffer));
		}
		else if( aFile.bad() )
		{
			logFatalError("Unknown error reading %s", theFilePath.c_str());
			aFile.close();
			return;
		}

		for(int i = 0; i < aBufferSize; ++i)
		{// Step through buffer character-by-character
			const char c = aBuffer[i];
			switch(aState)
			{
			case ePIState_Whitespace:
				// Look for a category, key, or comment
				if( c == '[' )
				{
					if( theParseMode == eParseMode_Header )
					{// Stop parsing once hit [ in this mode
						aFile.close();
						return;
					}
					aState = ePIState_Category;
					aNewCategory.clear();
				}
				else if( c == '#' || c == ';' )
				{
					aState = ePIState_Comment;
				}
				else if( isalnum(c) )
				{
					if( theParseMode == eParseMode_Categories &&
						aCategory.empty() )
					{// Treat anything before first category as a comment
						aState = ePIState_Comment;
					}
					else
					{
						aState = ePIState_Key;
						aKey = aCategory;
						if( !aKey.empty() )
							aKey.push_back('/');
						aKey.push_back(toupper(c));
					}
				}
				break;

			case ePIState_Category:
				// Look for ']' to end category
				switch(c)
				{
				case ']':
					aNewCategory = trim(aNewCategory);
					if( !aNewCategory.empty() )
						aCategory.swap(aNewCategory);
					// fall through
				case '\r': case '\n': case '\0':
					aState = ePIState_Whitespace;
					break;
				default:
					// Categories are all upper-case and no spaces/etc
					if( c > ' ' && c != '-' && c != '_' && c != '/' )
						aNewCategory.push_back(toupper(c));
				}
				break;

			case ePIState_Key:
				// Look for '=' to end key
				if( c == '=' )
				{// Switch from parsing key to value
					aKey = trim(aKey);
					aState = ePIState_Value;
					aValue.clear();
				}
				else if( c == '\r' || c == '\n' || c == '\0' )
				{// Abort - invalid key
					aState = ePIState_Whitespace;
				}
				else if( c > ' ' && c != '-' && c != '_' && c != '/' )
				{// Keys are all upper-case and no spaces/etc
					aKey.push_back(toupper(c));
				}
				break;

			case ePIState_Value:
				// Look for eol to end value
				if( c == '\r' || c == '\n' || c == '\0' )
				{// Value string complete, time to process it!
					aValue = trim(aValue);
					if( !aValue.empty() )
						theCallbackFunc(aKey, aValue, theUserData);
					aState = ePIState_Whitespace;
				}
				else
				{
					aValue.push_back(c);
				}
				break;

			case ePIState_Comment:
				// Look for eol to end comment
				if( c == '\r' || c == '\n' || c == '\0' )
					aState = ePIState_Whitespace;
				break;
			}
		}
	}

	aFile.close();
}


static bool areOnSameVolume(
	const std::string& thePath1,
	const std::string& thePath2)
{
	DWORD aPath1VolumeSerialNumber;
	if(!GetVolumeInformation(widen(getRootDir(thePath1)).c_str(), 0, 0,
							  &aPath1VolumeSerialNumber, 0, 0, 0, 0))
	{
		return false;
	}
	DWORD aPath2VolumeSerialNumber;
	if(!GetVolumeInformation(widen(getRootDir(thePath2)).c_str(), 0, 0,
							  &aPath2VolumeSerialNumber, 0, 0, 0, 0))
	{
		return false;
	}

	return aPath1VolumeSerialNumber == aPath2VolumeSerialNumber;
}


static void setKeyValueInINI(
	const std::string& theFilePath,
	const std::string& theCategory,
	const std::string& theKey,
	const std::string& theValue)
{
	// Open source file
	std::ifstream aFile(widen(theFilePath).c_str(), std::ios::binary);
	if( !aFile.is_open() )
	{
		logError("Could not open file %s", theFilePath.c_str());
		return;
	}

	// Create temp output file
	std::string aTmpPath;
	{// Prefer to use temp folder if can, to reduce risk of leaving a mess
		WCHAR aPathBuffer[MAX_PATH];
		DWORD aLen = GetTempPath(MAX_PATH, aPathBuffer);
		if( aLen > 0 && aLen <= MAX_PATH )
			aTmpPath = narrow(aPathBuffer);
	}
	// ReplaceFile() requires both files be on the same drive...
	if( aTmpPath.empty() || !areOnSameVolume(aTmpPath, theFilePath) )
		aTmpPath = getFileDir(theFilePath, true);
	aTmpPath += "~"; aTmpPath += getFileName(theFilePath);
	std::ofstream aTmpFile(
		widen(aTmpPath).c_str(),
		std::ios::binary | std::ios::trunc);
	if( !aTmpFile.is_open() )
	{
		logError("Could not create temp file %s", aTmpPath.c_str());
		aFile.close();
		return;
	}

	// Prepare buffer
	// +1 on size is to accommodate an extra newline character when
	// reach end of file
	char aBuffer[kINIFileBufferSize+1];
	std::streamsize aBufferSize = kINIFileBufferSize;
	std::string aCheckStr;
	const std::string& aCmpCategory = condense(theCategory);
	const std::string& aCmpKey = condense(theKey);

	enum
	{
		eSKVState_FindCategory,
		eSKVState_CheckCategory,
		eSKVState_SkipCategoryLine,
		eSKVState_FindKey,
		eSKVState_CheckKey,
		eSKVState_SkipKeyLine,
		eSKVState_ValueLine,
		eSKVState_Finished,
	} aState = eSKVState_FindCategory;
	if( theCategory.empty() )
		aState = eSKVState_FindKey;

	// Parse the INI file to find start and end of segment to replace
	const std::fstream::pos_type kInvalidFilePos = -1;
	std::fstream::pos_type aReplaceStartPos = kInvalidFilePos;
	std::fstream::pos_type aReplaceEndPos = kInvalidFilePos;
	std::fstream::pos_type anEndOfValidCategory = kInvalidFilePos;
	std::fstream::pos_type aCurrFilePos = aFile.tellg();
	std::fstream::pos_type aLastNonWhitespace = aCurrFilePos;

	while(aFile.good() && aState != eSKVState_Finished)
	{
		// Read next chunk from file
		aFile.read(aBuffer, aBufferSize);
		if( aFile.eof() || aFile.gcount() < aBufferSize )
		{// Put eol at end of file in case there wasn't one already
			aBufferSize = aFile.gcount() + 1;
			aBuffer[aBufferSize-1] = '\n';
			DBG_ASSERT(aBufferSize <= ARRAYSIZE(aBuffer));
		}
		else if( aFile.bad() )
		{
			logError("Unknown error reading %s", theFilePath.c_str());
			aFile.close();
			return;
		}

		for(int i = 0; i < aBufferSize && aState != eSKVState_Finished; ++i)
		{// Step through buffer character-by-character
			const char c = aBuffer[i];
			if( !isspace(c) )
				aLastNonWhitespace = aCurrFilePos;
			switch(aState)
			{
			case eSKVState_FindCategory:
				// Look for a category
				if( c == '[' )
				{
					aState = eSKVState_CheckCategory;
					aCheckStr.clear();
				}
				else if( isalnum(c) )
				{
					aState = eSKVState_SkipCategoryLine;
				}
				break;

			case eSKVState_CheckCategory:
				// Look for ']' to end category
				switch(c)
				{
				case ']':
					aCheckStr = trim(aCheckStr);
					if( aCheckStr == aCmpCategory )
						aState = eSKVState_FindKey;
					else if( aCmpCategory.empty() )
						aState = eSKVState_Finished;
					else
						aState = eSKVState_FindCategory;
					break;
				case '\r': case '\n': case '\0':
					aState = eSKVState_FindCategory;
					break;
				default:
					// Categories are all upper-case and no spaces/etc
					if( c > ' ' && c != '-' && c != '_' && c != '/' )
						aCheckStr.push_back(toupper(c));
					break;
				}
				break;

			case eSKVState_SkipCategoryLine:
				// Look for end of line than resume category search
				if( c == '\r' || c == '\n' || c == '\0' )
					aState = eSKVState_FindCategory;
				break;

			case eSKVState_FindKey:
				// Look for start of a key
				if( c == '[' )
				{
					aState = eSKVState_CheckCategory;
					aCheckStr.clear();
				}
				else if( c == '#' || c == ';' )
				{
					aState = eSKVState_SkipKeyLine;
				}
				else if( isalnum(c) )
				{
					aState = eSKVState_CheckKey;
					aCheckStr.clear();
					aCheckStr.push_back(toupper(c));
				}
				break;

			case eSKVState_CheckKey:
				// Look for '=' to end key
				if( c == '=' )
				{// End of the key - check if this is the one being looked for
					if( trim(aCheckStr) == aCmpKey )
					{// Found starting position to replace with new characters!
						aReplaceStartPos = aCurrFilePos;
						aReplaceStartPos += std::fstream::off_type(1);
						aState = eSKVState_ValueLine;
					}
					else
					{// Not the right key, skip rest of this line
						aState = eSKVState_SkipKeyLine;
					}
				}
				else if( c == '\r' || c == '\n' || c == '\0' )
				{// Abort - invalid key
					aState = eSKVState_FindKey;
				}
				else if( c > ' ' && c != '-' && c != '_' && c != '/' )
				{// Keys are all upper-case and no spaces/etc
					aCheckStr.push_back(toupper(c));
				}
				break;

			case eSKVState_SkipKeyLine:
				// Look for end of line then resume key search
				if( c == '\r' || c == '\n' || c == '\0' )
				{
					aState = eSKVState_FindKey;
					anEndOfValidCategory = aCurrFilePos;
				}
				break;

			case eSKVState_ValueLine:
				// Look for eol to end section to replace
				if( c == '\r' || c == '\n' || c == '\0' )
				{
					aReplaceEndPos = aCurrFilePos;
					aState = eSKVState_Finished;
				}
				break;
			}
			aCurrFilePos += std::fstream::off_type(1);
		}
	}

	// Clear eof flag
	aFile.clear();

	// Decide what to write and where exactly from info gathered above
	std::string aWriteString;
	if( aReplaceStartPos != kInvalidFilePos )
	{
		// Found the existing key, just write new value
		DBG_ASSERT(aReplaceEndPos != kInvalidFilePos);
		aWriteString = std::string(" ") + trim(theValue);
	}
	else if( anEndOfValidCategory != kInvalidFilePos )
	{
		// Found category, write key and value
		aReplaceStartPos = aReplaceEndPos = anEndOfValidCategory;
		aWriteString = kEndOfLine + trim(theKey) + " = " + trim(theValue);
	}
	else
	{
		// Found nothing, write category, key, and value
		aReplaceStartPos = aLastNonWhitespace;
		aReplaceStartPos += std::fstream::off_type(1);
		aFile.seekg(0, std::ios::end);
		aReplaceEndPos = aFile.tellg();
		aWriteString = kEndOfLine + kEndOfLine + "[";
		aWriteString += trim(theCategory) + "]" + kEndOfLine;
		aWriteString += trim(theKey) + " = " + trim(theValue);
		aWriteString += kEndOfLine;
	}

	// Write aFile 0->aReplaceStartPos to temp file
	aFile.seekg(0, std::ios::beg);
	aCurrFilePos = aFile.tellg();
	while(aCurrFilePos < aReplaceStartPos && aFile.good())
	{
		const std::streamsize aBytesToRead =
			min(kINIFileBufferSize, aReplaceStartPos - aCurrFilePos);
		aFile.read(aBuffer, aBytesToRead);
		aTmpFile.write(aBuffer, aFile.gcount());
		aCurrFilePos += aFile.gcount();
	}

	// Write new data
	aTmpFile << aWriteString;

	// Write aReplaceEndPos->eof to temp file
	aFile.seekg(aReplaceEndPos, std::ios::beg);
	while(aFile.read(aBuffer, kINIFileBufferSize))
		aTmpFile.write(aBuffer, aFile.gcount());
	aTmpFile.write(aBuffer, aFile.gcount());

	// Close both files
	aFile.close();
	aTmpFile.close();

	// Replace original file with new temp file (and delete temp file)
	if( !ReplaceFile(widen(theFilePath).c_str(), widen(aTmpPath).c_str(),
			NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL) )
	{
		logError("Failed to modify file %s!", theFilePath.c_str());
	}
}


static void generateResourceProfile(const ResourceProfile& theResProfile)
{
	HMODULE hModule = GetModuleHandle(NULL);

	HRSRC hResource = FindResource(
		hModule,
		MAKEINTRESOURCE(theResProfile.resID), L"TEXT");

	if( hResource != NULL )
	{
		HGLOBAL hGlobal = LoadResource(hModule, hResource);

		if( hGlobal != NULL )
		{
			void* aData = LockResource(hGlobal);
			DWORD aSize = SizeofResource(hModule, hResource);

			const std::string& aFilePath =
				profileNameToFilePath(theResProfile.name);
			std::ofstream aFile(
				widen(aFilePath).c_str(),
				std::ios::binary | std::ios::trunc);
			if( aFile.is_open() )
			{
				aFile.write(static_cast<char*>(aData), aSize);
				aFile.close();
			}
			else
			{
				logFatalError("Unable to write Profile data to file %s\n",
					aFilePath.c_str());
			}
			FreeResource(hGlobal);
		}
	}
}


static void getProfileListCallback(
   const std::string& theKey,
   const std::string& theValue,
   void*)
{
	const std::string kProfilePrefix = "PROFILE";
	if( theKey.compare(0, kProfilePrefix.length(), kProfilePrefix) == 0 )
	{
		const int aProfileNum =
			intFromString(theKey.substr(kProfilePrefix.length()));
		if( aProfileNum > 0 && !theValue.empty() )
		{
			const ProfileEntry& aProfileEntry = profileNameToEntry(theValue);
			if( aProfileEntry.id != 0 )
			{
				if( aProfileNum >= sProfilesCanLoad.size() )
					sProfilesCanLoad.resize((size_t)aProfileNum + 1);
				sProfilesCanLoad[aProfileNum].push_back(
					getOrAddProfileIdx(aProfileEntry));
			}
			else
			{
				logError("Could not find/open profile '%s' (%s) listed in %s",
					theValue.c_str(),
					aProfileEntry.path.c_str(),
					kCoreProfileName);
			}
		}
	}
	else if( theKey == "AUTOLOADPROFILE" )
	{
		if( theValue.empty() )
			return;

		int anAutoProfileNum = intFromString(theValue);
		if( anAutoProfileNum > 0 &&
			toString(anAutoProfileNum).length() == theValue.length() )
		{// Specified by number
			sAutoProfileIdx = anAutoProfileNum;
			return;
		}

		// Specified by name
		const ProfileEntry& aProfileEntry = profileNameToEntry(theValue);
		if( aProfileEntry.id != 0 )
		{
			DBG_ASSERT(!sProfilesCanLoad.empty());
			sProfilesCanLoad[0].push_back(
				getOrAddProfileIdx(aProfileEntry));
		}
		else
		{
			logError("Could not find/open auto-load profile %s (%s)",
				theValue.c_str(),
				aProfileEntry.path.c_str());
		}
	}
}


static void addParentCallback(
   const std::string& theKey,
   const std::string& theValue,
   void* theLoadList)
{
	if( !theLoadList )
		return;

	std::vector<int>* aLoadPriorityList = (std::vector<int>*)(theLoadList);

	if( theKey == "PARENTPROFILE" || theKey == "PARENT" )
	{
		const std::string& aProfileName = extractProfileName(theValue);
		ProfileEntry aProfileEntry = profileNameToEntry(aProfileName);
		if( aProfileEntry.id == 0 )
		{// File not found - but maybe referencing a resource base file?
			const std::string& aCmpName = condense(aProfileName);
			for(size_t i = 0; i < ARRAYSIZE(kResTemplateBase); ++i)
			{
				if( condense(kResTemplateBase[i].name) == aCmpName )
				{
					generateResourceProfile(kResTemplateBase[i]);
					aProfileEntry = profileNameToEntry(aProfileName);
					sNewBaseProfileIdx = getOrAddProfileIdx(aProfileEntry);
					break;
				}
			}
		}
		if( aProfileEntry.id != 0 )
		{
			const int aParentProfileIdx = getOrAddProfileIdx(aProfileEntry);
			// Don't add Core as parent if specified (it's added automatically)
			if( aParentProfileIdx == 0 )
				return;
			// Don't add entries already added (or can get infinite loop)!
			if( std::find(aLoadPriorityList->begin(), aLoadPriorityList->end(),
					aParentProfileIdx) != aLoadPriorityList->end() )
				return;
			aLoadPriorityList->push_back(aParentProfileIdx);
		}
		else
		{
			logError("Could not find/open '%s' Profile's parent: '%s' (%s)",
				sKnownProfiles[*(aLoadPriorityList->begin())].name.c_str(),
				theValue.c_str(), aProfileEntry.path.c_str());
		}
	}
}


static void generateProfileLoadPriorityList(int theProfileListIdx)
{
	DBG_ASSERT(theProfileListIdx >= 0);
	DBG_ASSERT(theProfileListIdx < sProfilesCanLoad.size());
	std::vector<int>& aList = sProfilesCanLoad[theProfileListIdx];
	if( aList.empty() )
		return;

	// Should only have the main file as first priority at this point
	DBG_ASSERT(aList.size() == 1);
	DBG_ASSERT(aList[0] >= 0 && aList[0] < sKnownProfiles.size());

	// Parse main profile and add parent, parent of parent, etc.
	size_t aLoadListSize = 0;
	while(aList.size() > aLoadListSize)
	{
		parseINI(
			addParentCallback,
			sKnownProfiles[aList[aLoadListSize++]].path,
			eParseMode_Header,
			&aList);
	}

	// Finally add core .ini as lowest-priority settings
	aList.push_back(0);
}


static void parseProfilesCanLoad()
{
	sKnownProfiles.clear();
	sProfilesCanLoad.clear();
	sAutoProfileIdx = 0;

	// Initially core .ini is only profile known, with others added
	// via parsing core (and other profiles for checking for parents)
	ProfileEntry aCoreProfile = profileNameToEntry(kCoreProfileName);
	if( aCoreProfile.id == 0 )
	{
		logFatalError("Could not find %s (%s)",
			kCoreProfileName, aCoreProfile.path.c_str());
		return;
	}

	{// Add Core as first known profile (profile 0)
		int aCoreID = getOrAddProfileIdx(aCoreProfile);
		DBG_ASSERT(aCoreID == 0);
	}

	// Get list of load-able profiles and auto-load profile from core
	// Profile '0' reserved for auto-load profile if specified by name
	sProfilesCanLoad.resize(1);
	parseINI(
		getProfileListCallback,
		aCoreProfile.path,
		eParseMode_Header);
}


static void tryAddAutoLaunchApp(
	int theKnownProfileIdx,
	std::string& theDefaultParams)
{
	std::string aPath = Dialogs::targetAppPath(theDefaultParams);
	if( !aPath.empty() )
	{
		setKeyValueInINI(
			sKnownProfiles[theKnownProfileIdx].path,
			"System", "AutoLaunchApp",
			aPath);
		sSettingsMap.setValue(kAutoLaunchAppKey, aPath);
		setKeyValueInINI(
			sKnownProfiles[theKnownProfileIdx].path,
			"System", "AutoLaunchAppParams",
			theDefaultParams);
	}
}


static void readProfileCallback(
   const std::string& theKey,
   const std::string& theValue,
   void*)
{
	sSettingsMap.setValue(theKey, theValue);
}


static void loadProfile(int theProfilesCanLoadIdx)
{
	sSettingsMap.clear();
	sSettingsMap.trim();

	DBG_ASSERT(theProfilesCanLoadIdx >= 0);
	DBG_ASSERT(theProfilesCanLoadIdx < sProfilesCanLoad.size());
	const std::vector<int>& aList = sProfilesCanLoad[theProfilesCanLoadIdx];
	if( aList.empty() )
		return;

	// Now load each .ini's values 1-by-1 in reverse order.
	// Any setting in later files overrides previous setting,
	// which is why we load them in reverse of priority order
	for(std::vector<int>::const_reverse_iterator itr = aList.rbegin();
		itr != aList.rend(); ++itr)
	{
		DBG_ASSERT(*itr >= 0);
		DBG_ASSERT(*itr < sKnownProfiles.size());
		const ProfileEntry& anEntry = sKnownProfiles[*itr];
		parseINI(
			readProfileCallback,
			anEntry.path,
			eParseMode_Categories);
	}

	if( sNewBaseProfileIdx >= 0 )
	{// Generated a new kResTemplateBase profile
		// Prompt if want to have it set to Auto-launch target app
		std::string& aDefaultParams =
			sSettingsMap.findOrAdd(kAutoLaunchAppParamsKey);
		tryAddAutoLaunchApp(sNewBaseProfileIdx, aDefaultParams);
		sNewBaseProfileIdx = -1;
	}

	// Now that have loaded a profile, only need to remember the main
	// profile's name and can forget gathered data about other profiles
	sLoadedProfileName = sKnownProfiles[aList[0]].name;
	sKnownProfiles.clear();
	sProfilesCanLoad.clear();
	sAutoProfileIdx = 0;
	sNewBaseProfileIdx = -1;
}


void setAutoLoadProfile(int theProfilesCanLoadIdx)
{
	if( theProfilesCanLoadIdx == sAutoProfileIdx )
		return;

	DBG_ASSERT(!sKnownProfiles.empty());

	if( theProfilesCanLoadIdx > 0 )
	{
		setKeyValueInINI(
			sKnownProfiles[0].path,
			"", "AutoLoadProfile",
			toString(theProfilesCanLoadIdx));
	}
	else
	{
		setKeyValueInINI(
			sKnownProfiles[0].path,
			"", "AutoLoadProfile", "");
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadCore()
{
	// Should only be run once at app startup, otherwise core will be
	// loaded alongside normal ::load()
	DBG_ASSERT(sSettingsMap.empty());

	ProfileEntry aCoreProfile = profileNameToEntry(kCoreProfileName);
	if( aCoreProfile.id == 0 )
	{// Core .ini file not found!
		// Assume this means first run, which means show license agreement
		if( Dialogs::showLicenseAgreement() == eResult_Declined )
		{// Declined license agreement - just exit
			gShutdown = true;
		}
		else
		{// Generate Core.ini which will prevent future license agreement
			generateResourceProfile(kResTemplateCore);
			aCoreProfile.id = uniqueFileIdentifier(aCoreProfile.path);
			if( aCoreProfile.id == 0 )
			{
				logFatalError("Unable to find/write %s (%s)",
					kCoreProfileName, aCoreProfile.path.c_str());
			}
		}
	}
	if( hadFatalError() || gShutdown )
		return;
	
	parseINI(
		readProfileCallback,
		aCoreProfile.path,
		eParseMode_Categories);
}


void load()
{
	parseProfilesCanLoad();
	if( hadFatalError() || gShutdown )
		return;

	// Make sure sAutoProfileIdx is a valid (even if empty) index
	if( sAutoProfileIdx < 0 ||
		sAutoProfileIdx >= sProfilesCanLoad.size() ||
		sProfilesCanLoad[sAutoProfileIdx].empty() )
	{
		if( sAutoProfileIdx > 0 )
		{
			logError(
				"AutoLoadProfile = %d but no profile #%d found!",
				sAutoProfileIdx, sAutoProfileIdx);
		}
		sAutoProfileIdx = 0;
	}

	// If already loaded a profile, override sAutoProfileIdx with it
	if( !sLoadedProfileName.empty() )
	{
		const int aProfileIdx =
			getOrAddProfileIdx(profileNameToEntry(sLoadedProfileName));
		if( aProfileIdx >= 0 )
		{
			sAutoProfileIdx = -1;
			for(size_t i = 0; i < sProfilesCanLoad.size(); ++i)
			{
				if( !sProfilesCanLoad[i].empty() &&
					sProfilesCanLoad[i][0] == aProfileIdx )
				{
					sAutoProfileIdx = int(i);
					break;
				}
			}
			if( sAutoProfileIdx == -1 )
			{
				sProfilesCanLoad.push_back(std::vector<int>());
				sProfilesCanLoad.back().push_back(aProfileIdx);
				sAutoProfileIdx = int(sProfilesCanLoad.size())-1;
			}
		}
	}

	// Fill out rest of the sProfilesCanLoad vectors
	for(int i = 0; i < sProfilesCanLoad.size(); ++i)
		generateProfileLoadPriorityList(i);

	#ifdef _DEBUG
	if( sLoadedProfileName.empty() )
	{
		// In Debug builds, update any generated built-in resource .ini
		// files to reflect any changes made to the resource versions.
		generateResourceProfile(kResTemplateCore);
		for(size_t i = 1; i < sKnownProfiles.size(); ++i)
		{
			for(size_t j = 0; j < ARRAYSIZE(kResTemplateBase); ++j)
			{
				if( sKnownProfiles[i].name == kResTemplateBase[j].name )
					generateResourceProfile(kResTemplateBase[j]);
			}
			for(size_t j = 0; j < ARRAYSIZE(kResTemplateDefault); ++j)
			{
				if( sKnownProfiles[i].name == kResTemplateDefault[j].name )
					generateResourceProfile(kResTemplateDefault[j]);
			}
		}
		// Restore profile load settings to regenerated Core
		int autoProfileIdxTmp = 0;
		swap(autoProfileIdxTmp, sAutoProfileIdx);
		setAutoLoadProfile(autoProfileIdxTmp);
		swap(autoProfileIdxTmp, sAutoProfileIdx);
		for(size_t i = 1; i < sProfilesCanLoad.size(); ++i)
		{
			if( !sProfilesCanLoad[i].empty() )
			{
				setKeyValueInINI(
					sKnownProfiles[0].path, "",
					std::string("Profile") + toString(int(i)),
					sKnownProfiles[sProfilesCanLoad[i][0]].name);
			}
		}
	}
	#endif

	// See if need to query user for profile to load
	if( sProfilesCanLoad[sAutoProfileIdx].empty() )
		queryUserForProfile();
	else
		loadProfile(sAutoProfileIdx);
}


bool queryUserForProfile()
{
	if( sKnownProfiles.empty() )
	{
		parseProfilesCanLoad();
		for(int i = 0; i < sProfilesCanLoad.size(); ++i)
			generateProfileLoadPriorityList(i);
	}
	if( hadFatalError() )
		return false;

	// Create lists needed for dialog
	enum EType
	{
		eType_CopyFile,			// index into sKnownProfiles
		eType_CopyResDefault,	// index into kResTemplateDefault
		eType_ParentResBase,	// index into kResTemplateBase
		eType_ParentFile,		// index into sKnownProfiles
	};
	std::vector<std::string> aLoadableProfileNames;
	std::vector<int> aLoadableProfileIndices;
	std::vector<std::string> aTemplateProfileNames;
	std::vector<int> aTemplateProfileIndex;
	std::vector<EType> aTemplateProfileType;
	const std::string kCopyFilePrefix = "Copy \"";
	const std::string kCopyResPrefix = "Copy generated \"";
	const std::string kParentPrefix = "Empty w/ parent \"";

	for(int i = 0; i < sProfilesCanLoad.size(); ++i)
	{
		if( !sProfilesCanLoad[i].empty() )
		{
			const std::string& aName =
				sKnownProfiles[sProfilesCanLoad[i][0]].name;
			aLoadableProfileNames.push_back(aName);
			aLoadableProfileIndices.push_back(i);
			aTemplateProfileNames.push_back(
				kCopyFilePrefix + aName + '"');
			aTemplateProfileIndex.push_back(
				sProfilesCanLoad[i][0]);
			aTemplateProfileType.push_back(eType_CopyFile);
		}
	}

	const bool needFirstProfile = aLoadableProfileNames.empty();
	for(int i = 0; i < ARRAYSIZE(kResTemplateDefault); ++i)
	{
		if( needFirstProfile )
		{
			aLoadableProfileNames.push_back(kResTemplateDefault[i].name);
			aLoadableProfileIndices.push_back(i);
		}
		aTemplateProfileNames.push_back(
			kCopyResPrefix + kResTemplateDefault[i].name + '"');
		aTemplateProfileIndex.push_back(i);
		aTemplateProfileType.push_back(eType_CopyResDefault);
	}

	for(int i = 0; i < sKnownProfiles.size(); ++i)
	{
		aTemplateProfileNames.push_back(
			kParentPrefix + sKnownProfiles[i].name + '"');
		aTemplateProfileIndex.push_back(i);
		aTemplateProfileType.push_back(eType_ParentFile);
	}

	for(int i = 0; i < ARRAYSIZE(kResTemplateBase); ++i)
	{
		// Only add ones that don't already exist in sKnownProfiles
		bool alreadyExists = false;
		const ProfileEntry& aTestEntry =
			profileNameToEntry(kResTemplateBase[i].name);
		if( aTestEntry.id != 0 )
		{
			for(int j = 0; j < sKnownProfiles.size(); ++j)
			{
				if( sKnownProfiles[j].id == aTestEntry.id )
				{
					alreadyExists = true;
					break;
				}
			}
		}
		if( alreadyExists )
			continue;
		aTemplateProfileNames.push_back(
			kParentPrefix + kResTemplateBase[i].name + '"');
		aTemplateProfileIndex.push_back(i);
		aTemplateProfileType.push_back(eType_ParentResBase);
	}

	// Use Dialog to ask the user what they want to do
	int aDefaultSelectedIdx = -1;
	if( !sLoadedProfileName.empty() )
	{
		for(int i = 0; i < aLoadableProfileNames.size(); ++i)
		{
			if( aLoadableProfileNames[i] == sLoadedProfileName )
				aDefaultSelectedIdx = i;
		}
	}
	Dialogs::ProfileSelectResult aDialogResult = Dialogs::profileSelect(
		aLoadableProfileNames,
		aTemplateProfileNames,
		aDefaultSelectedIdx,
		sAutoProfileIdx >= 0 &&
		sAutoProfileIdx < sProfilesCanLoad.size() &&
		!sProfilesCanLoad[sAutoProfileIdx].empty());

	if( aDialogResult.cancelled )
	{// User declined to select anything
		// If have no profile loaded already, quit app entirely
		if( sLoadedProfileName.empty() )
			gShutdown = true;
		return false;
	}

	// For first profile, if no name given then use built-in name
	// NOTE: This only works out because set the first entries of
	// aTemplateProfile's to match aLoadableProfile's which both
	// match the entries in kResTemplateDefault. So need to change
	// this if rearrange the order of any of these above!
	if( needFirstProfile && aDialogResult.newName.empty() )
		aDialogResult.newName =
			kResTemplateDefault[aDialogResult.selectedIndex].name;

	if( aDialogResult.newName.empty() )
	{// User must have requested to load an existing profile
		DBG_ASSERT(aDialogResult.selectedIndex < aLoadableProfileNames.size());
		const int aProfileCanLoadIdx =
			aLoadableProfileIndices[aDialogResult.selectedIndex];
		setAutoLoadProfile(
			aDialogResult.autoLoadRequested ? aProfileCanLoadIdx : 0);
		loadProfile(aProfileCanLoadIdx);
		return true;
	}

	// User must have requested creating a new profile
	const std::string& aNewName = extractProfileName(aDialogResult.newName);
	ProfileEntry aNewEntry = profileNameToEntry(aNewName);
	size_t aSrcIdx = (unsigned)aDialogResult.selectedIndex;
	DBG_ASSERT(aSrcIdx < aTemplateProfileIndex.size());

	if( aNewEntry.id == sKnownProfiles[0].id )
	{
		logFatalError("Can not create custom Profile with the name 'Core'!");
		return false;
	}

	// If profile already exists, make sure it's not in sProfilesCanLoad
	if( aNewEntry.id != 0 )
	{
		for(size_t i = 0; i < sProfilesCanLoad.size(); ++i)
		{
			if( !sProfilesCanLoad[i].empty() &&
				sKnownProfiles[sProfilesCanLoad[i][0]].id == aNewEntry.id )
			{
				sProfilesCanLoad[i].clear();
			}
		}
	}

	// Find slot in sProfilesCanLoad to add profile to
	int aProfileCanLoadIdx = int(sProfilesCanLoad.size());
	for(int i = 1; i < sProfilesCanLoad.size(); ++i)
	{
		if( sProfilesCanLoad[i].empty() )
		{
			aProfileCanLoadIdx = i;
			break;
		}
	}
	if( aProfileCanLoadIdx >= sProfilesCanLoad.size() )
		sProfilesCanLoad.resize(aProfileCanLoadIdx+1);

	DBG_ASSERT(aDialogResult.selectedIndex < aTemplateProfileType.size());
	switch(aTemplateProfileType[aDialogResult.selectedIndex])
	{
	case eType_CopyFile:
		{// Create copy of source file
			const size_t aKnownProfileIdx = aTemplateProfileIndex[aSrcIdx];
			DBG_ASSERT(aKnownProfileIdx < sKnownProfiles.size());
			const ProfileEntry& aSrcEntry = sKnownProfiles[aKnownProfileIdx];
			// Don't actually do anything if source and dest are the same file
			if( aNewEntry.id != aSrcEntry.id )
			{
				CopyFile(
					widen(aSrcEntry.path).c_str(),
					widen(aNewEntry.path).c_str(),
					FALSE);
			}
		}
		break;
	case eType_CopyResDefault:
		{// Generate resource profile but with new file name
			const size_t aResTemplateIdx = aTemplateProfileIndex[aSrcIdx];
			DBG_ASSERT(aResTemplateIdx < ARRAYSIZE(kResTemplateDefault));
			ResourceProfile aResProfile = kResTemplateDefault[aResTemplateIdx];
			aResProfile.name = aNewName.c_str();
			generateResourceProfile(aResProfile);
		}
		break;
	case eType_ParentResBase:
		{// Generate resource profile to act as parent
			const size_t aResBaseIdx = aTemplateProfileIndex[aSrcIdx];
			DBG_ASSERT(aResBaseIdx < ARRAYSIZE(kResTemplateBase));
			generateResourceProfile(kResTemplateBase[aResBaseIdx]);
			sNewBaseProfileIdx = -1;
			sNewBaseProfileIdx = getOrAddProfileIdx(
				profileNameToEntry(kResTemplateBase[aResBaseIdx].name));
			if( sNewBaseProfileIdx < 0 )
				break;
			// Treat as _ParentFile type now that resource exists as a file
			aTemplateProfileNames.push_back("");
			aTemplateProfileIndex.push_back(sNewBaseProfileIdx);
			aTemplateProfileType.push_back(eType_ParentFile);
			aSrcIdx = aTemplateProfileType.size() - 1;
		}
		// fall through
	case eType_ParentFile:
		{// Create new file that sets source file as its parent profile
			const size_t aKnownProfileIdx = aTemplateProfileIndex[aSrcIdx];
			DBG_ASSERT(aKnownProfileIdx < sKnownProfiles.size());
			const ProfileEntry& aSrcEntry = sKnownProfiles[aKnownProfileIdx];
			// Don't actually do anything if source and dest are the same file
			if( aNewEntry.id != aSrcEntry.id )
			{
				std::ofstream aFile(
					widen(aNewEntry.path).c_str(),
					std::ios::out | std::ios::trunc);
				if( aFile.is_open() )
				{
					if( aKnownProfileIdx > 0 )
					{// Don't need to specify if Core is the parent
						aFile << "ParentProfile = ";
						aFile << aSrcEntry.name << std::endl << std::endl;
					}
					aFile << "[Scheme]" << std::endl;
					aFile.close();
				}
				else
				{
					logFatalError("Unable to write Profile data to file %s\n",
						aNewEntry.path.c_str());
				}
			}
		}
		break;
	}

	if( hadFatalError() )
		return false;

	if( aNewEntry.id == 0 )
		aNewEntry.id = uniqueFileIdentifier(aNewEntry.path);
	if( aNewEntry.id == 0 )
	{
		logFatalError("Failure creating new Profile %s (%s)!",
			aNewEntry.name.c_str(), aNewEntry.path.c_str());
		return false;
	}

	setKeyValueInINI(
		sKnownProfiles[0].path, "",
		std::string("Profile") + toString(aProfileCanLoadIdx),
		aNewEntry.name);
	sProfilesCanLoad[aProfileCanLoadIdx].push_back(
		getOrAddProfileIdx(aNewEntry));
	size_t aKnownProfilesCount = sKnownProfiles.size();
	generateProfileLoadPriorityList(aProfileCanLoadIdx);
	setAutoLoadProfile(
		aDialogResult.autoLoadRequested ? aProfileCanLoadIdx : 0);
	loadProfile(aProfileCanLoadIdx);

	return true;
}


std::string getStr(const std::string& theKey, const std::string& theDefaultValue)
{
	if( std::string* aString = sSettingsMap.find(upper(theKey)) )
	{
		if( !aString->empty() )
			return *aString;
	}

	return theDefaultValue;
}


int getInt(const std::string& theKey, int theDefaultValue)
{
	return intFromString(
		getStr(theKey, toString(theDefaultValue)));
}


bool getBool(const std::string& theKey, bool theDefaultValue)
{
	const std::string& aString =
		getStr(theKey, (theDefaultValue ? "1" : "0"));

	if( aString.empty() ||
		aString[0] == '0' ||
		aString[0] == 'n' || // no
		aString[0] == 'N' || // No
		aString[0] == 'f' || // false
		aString[0] == 'F' || // False
		(aString[0] == 'O' && aString[1] == 'F') || // OFF
		(aString[0] == 'O' && aString[1] == 'f') || // Off
		(aString[0] == 'o' && aString[1] == 'f') || // off
		aString[0] == '\0' )
	{
		return false;
	}
	return true;
}


void getAllKeys(const std::string& thePrefix, KeyValuePairs& out)
{
	const size_t aPrefixLength = thePrefix.length();
	StringsMap::IndexVector anIndexSet;
	sSettingsMap.findAllWithPrefix(upper(thePrefix), &anIndexSet);

	#ifndef NDEBUG
	// Unnecessary but nice for debug output - sort to match order added to map
	std::sort(anIndexSet.begin(), anIndexSet.end());
	#endif

	for(size_t i = 0; i < anIndexSet.size(); ++i)
	{
		if( sSettingsMap.values()[anIndexSet[i]].empty() )
			continue;
		out.push_back(std::make_pair(
			sSettingsMap.keys()[anIndexSet[i]].c_str() + aPrefixLength,
			sSettingsMap.values()[anIndexSet[i]].c_str()));
	}
}


void setStr(const std::string& theKey, const std::string& theString)
{
	std::string& aString =
		sSettingsMap.findOrAdd(upper(theKey), std::string(""));

	// Only change map and write to file if new string is actually different
	if( aString == theString )
		return;
	aString = theString;

	const std::string& aSectionName = getFileDir(theKey, false);
	const std::string& aKeyOnly = getFileName(theKey);

	if( !sLoadedProfileName.empty() )
	{
		setKeyValueInINI(
			profileNameToFilePath(sLoadedProfileName),
			aSectionName, aKeyOnly, theString);
	}
}


void setInt(const std::string& theKey, int theValue)
{
	setStr(theKey, toString(theValue));
}


void setBool(const std::string& theKey, bool theValue)
{
	if( getBool(theKey) != theValue )
		setStr(theKey, theValue ? "Yes" : "No");
}

} // Profile
