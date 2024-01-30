//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Profile.h"

#include "Dialogs.h"
#include "Lookup.h"
#include "StringUtils.h"
#include "Resources/resource.h"

#include <fstream>

namespace Profile
{

#ifdef _DEBUG
#define USE_DEFAULT_PROFILE_INDEX 0 // MnM Default
#endif

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kINIReadBufferSize = 256, // How many characters to read at a time from .ini
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

	HANDLE hFile = CreateFile(widen(theFilePath).c_str(),
		GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if( hFile != INVALID_HANDLE_VALUE )
	{
		BY_HANDLE_FILE_INFORMATION aFileInfo;
		if( GetFileInformationByHandle(hFile, &aFileInfo) )
		{
			result = aFileInfo.dwVolumeSerialNumber ^ 
				((aFileInfo.nFileIndexHigh << 16) | aFileInfo.nFileIndexLow);
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
		logFatalError("Could not open file %s", theFilePath.c_str());

	// Prepare input buffer
	// +1 on size is to accomodate an extra newline character when
	// reach end of file
	char aBuffer[kINIReadBufferSize+1];
	int aBufferSize = kINIReadBufferSize;
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
					if( c > ' ' && c != '-' && c != '_' )
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
				else if( c > ' ' && c != '-' && c != '_' )
				{// Keys are all upper-case and no spaces/etc
					aKey.push_back(toupper(c));
				}
				break;

			case ePIState_Value:
				// Look for eol to end value
				if( c == '\r' || c == '\n' || c == '\0' )
				{// Value string complete, time to process it!
					aValue = trim(aValue);
					// An empty value may still have some meaning...
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
	DBG_ASSERT(sKnownProfiles.empty());
	DBG_ASSERT(sProfilesCanLoad.empty());
	sAutoProfileIdx = 0;

	// Initially core .ini is only profile known, with others added
	// via parsing core (and other profiles for checking for parents)
	ProfileEntry aCoreProfile = profileNameToEntry(kCoreProfileName);
	if( aCoreProfile.id == 0 )
	{
		// Core .ini not found - generate it!
		generateResourceProfile(kResTemplateCore);
		aCoreProfile.id = uniqueFileIdentifier(aCoreProfile.path);
		if( aCoreProfile.id == 0 )
		{
			logFatalError("Unable to find/write %s (%s)",
				kCoreProfileName, aCoreProfile.path.c_str());		
		}
	}
	if( hadFatalError() )
		return;

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

	// Now that have loaded a profile, only need to remember the main
	// profile's name and can forget gathered data about other profiles
	sLoadedProfileName = sKnownProfiles[aList[0]].name;
	sKnownProfiles.clear();
	sProfilesCanLoad.clear();
	sAutoProfileIdx = 0;
}


void setAutoLoadProfile(int theProfilesCanLoadIdx)
{
	// TODO: Write the value to Core.ini AutoLoadProfile key
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void load()
{
	#ifdef _DEBUG
	// In debug builds, always re-generate built-in resource .ini files,
	// to always reflect changes made to them while working on the code
	generateResourceProfile(kResTemplateCore);
	for(size_t i = 0; i < ARRAYSIZE(kResTemplateBase); ++i)
		generateResourceProfile(kResTemplateBase[i]);
	for(size_t i = 0; i < ARRAYSIZE(kResTemplateDefault); ++i)
		generateResourceProfile(kResTemplateDefault[i]);
	#ifdef USE_DEFAULT_PROFILE_INDEX
	sLoadedProfileName = kResTemplateDefault[USE_DEFAULT_PROFILE_INDEX].name;
	#endif
	#endif

	parseProfilesCanLoad();
	if( hadFatalError() )
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

	// See if need to query user for profile to load
	if( sProfilesCanLoad[sAutoProfileIdx].empty() )
		queryUserForProfile();
	else
		loadProfile(sAutoProfileIdx);
}


void queryUserForProfile()
{
	if( sKnownProfiles.empty() )
	{
		parseProfilesCanLoad();
		for(int i = 0; i < sProfilesCanLoad.size(); ++i)
			generateProfileLoadPriorityList(i);
	}
	if( hadFatalError() )
		return;

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
	const std::string kCopyPrefix = "Copy of \"";
	const std::string kParentPrefix = "Based on \"";

	for(int i = 0; i < sProfilesCanLoad.size(); ++i)
	{
		if( !sProfilesCanLoad[i].empty() )
		{
			const std::string& aName =
				sKnownProfiles[sProfilesCanLoad[i][0]].name;
			aLoadableProfileNames.push_back(aName);
			aLoadableProfileIndices.push_back(i);
			aTemplateProfileNames.push_back(
				kCopyPrefix + aName + '"');
			aTemplateProfileIndex.push_back(
				sProfilesCanLoad[i][0]);
			aTemplateProfileType.push_back(eType_CopyFile);
		}
	}

	for(int i = 0; i < ARRAYSIZE(kResTemplateDefault); ++i)
	{
		aTemplateProfileNames.push_back(
			kCopyPrefix + kResTemplateDefault[i].name + '"');
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
			for(int ii = 0; ii < sKnownProfiles.size(); ++ii)
			{
				if( sKnownProfiles[ii].id == aTestEntry.id )
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
	const Dialogs::ProfileSelectResult& aDialogResult =
		Dialogs::profileSelect(aLoadableProfileNames, aTemplateProfileNames);

	if( aDialogResult.cancelled )
	{// User declined to select anything
		// If have no profile loaded already, quit app entirely
		if( sLoadedProfileName.empty() )
			gShutdown = true;
		return;
	}

	if( aDialogResult.newName.empty() )
	{// User must have requested to load an existing profile
		DBG_ASSERT(aDialogResult.selectedIndex < aLoadableProfileNames.size());
		const int aProfileCanLoadIdx =
			aLoadableProfileIndices[aDialogResult.selectedIndex];
		loadProfile(aProfileCanLoadIdx);
		setAutoLoadProfile(
			aDialogResult.autoLoadRequested ? aProfileCanLoadIdx : 0);
		return;
	}

	// User must have requested creating a new profile
	const std::string& aNewName = extractProfileName(aDialogResult.newName);
	ProfileEntry aNewEntry = profileNameToEntry(aNewName);
	size_t aSrcIdx = (unsigned)aDialogResult.selectedIndex;
	DBG_ASSERT(aSrcIdx < aTemplateProfileIndex.size());

	if( aNewEntry.id == sKnownProfiles[0].id )
	{
		logFatalError("Can not create custom Profile with the name 'Core'!");
		return;
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
			const int aKnownProfileIdx = getOrAddProfileIdx(
				profileNameToEntry(kResTemplateBase[aResBaseIdx].name));
			if( aKnownProfileIdx < 0 )
				break;
			// Treat as _ParentFile type now that resource exists as a file
			aTemplateProfileNames.push_back("");
			aTemplateProfileIndex.push_back(aKnownProfileIdx);
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
		return;

	if( aNewEntry.id == 0 )
		aNewEntry.id = uniqueFileIdentifier(aNewEntry.path);
	if( aNewEntry.id == 0 )
	{
		logFatalError("Failure creating new Profile %s (%s)!",
			aNewEntry.name.c_str(), aNewEntry.path.c_str());
		return;
	}

	// TODO: Write new profile name to Core.ini Profile# key
	sProfilesCanLoad[aProfileCanLoadIdx].push_back(
		getOrAddProfileIdx(aNewEntry));
	generateProfileLoadPriorityList(aProfileCanLoadIdx);
	loadProfile(aProfileCanLoadIdx);
	setAutoLoadProfile(
		aDialogResult.autoLoadRequested ? aProfileCanLoadIdx : 0);
}


std::string getStr(const std::string& theKey, const std::string& theDefaultValue)
{
	if( std::string* aString = sSettingsMap.find(upper(theKey)) )
		return *aString;

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
		out.push_back(std::make_pair(
			sSettingsMap.keys()[anIndexSet[i]].c_str() + aPrefixLength,
			sSettingsMap.values()[anIndexSet[i]].c_str()));
	}
}


void setStr(const std::string& theKey, const std::string& theString)
{
	if( theString.empty() )
		return;

	std::string& aString =
		sSettingsMap.findOrAdd(upper(theKey), std::string(""));

	// Only change map and write to file if new string is actually different
	if( aString == theString )
		return;
	aString = theString;

	std::string aSectionName = getFileDir(theKey, false);
	std::string aKeyOnly = getFileName(theKey);

	// TODO: Write the change out to sActiveProfileName.ini file
}


void setInt(const std::string& theKey, int theValue)
{
	setStr(theKey, toString(theValue));
}


void setBool(const std::string& theKey, bool theValue)
{
	setStr(theKey, theValue ? "Yes" : "No");
}

} // Profile
