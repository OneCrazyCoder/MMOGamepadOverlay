//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Profile.h"

#include "Lookup.h"
#include "StringUtils.h"
#include "Resources/resource.h"

#include <iostream>
#include <fstream>

namespace Profile
{

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

const ResourceProfile kResourceProfiles[] =
{//		name						resID
	{	"MMOGO_Core.ini",			IDR_TEXT_INI_CORE		},
	{	"MMOGO_MnM_Base.ini",		IDR_TEXT_INI_BASE_MNM	},
	{	"MMOGO_P99_Base.ini",		IDR_TEXT_INI_BASE_P99	},
	{	"MMOGO_PQ_Base.ini",		IDR_TEXT_INI_BASE_PQ	},
	{	"MMOGO_Profile.ini",		IDR_TEXT_INI_DEF_MNM	},
	{	"MMOGO_Profile.ini",		IDR_TEXT_INI_DEF_P99	},
	{	"MMOGO_Profile.ini",		IDR_TEXT_INI_DEF_PQ		},
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
static StringsVec sAvailableProfiles;
static size_t sAutoProfileIdx = 0;
static std::string sActiveProfileName;


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
		GetModuleFileNameW(NULL, aPath, _MAX_PATH);
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


static void parseINI(
	ParseINICallback theCallbackFunc,
	const std::string& theFileName,
	EParseMode theParseMode,
	void* theUserData = NULL)
{
	const std::string& aFullPath = iniFolderPath() + theFileName;
	std::ifstream aFile(widen(aFullPath).c_str(), std::ios::binary);
	if( !aFile.is_open() )
	{
		logError("Could not open file %s", aFullPath);
		gHadFatalError = true;
	}

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
			logError("Unknown error reading %s", aFullPath);
			gHadFatalError = true;
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
				else
				{
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


static bool profileExists(const std::string& theFileName)
{
	const DWORD aFileAttributes = GetFileAttributesW(
		widen(iniFolderPath() + theFileName).c_str());

	return
		aFileAttributes != INVALID_FILE_ATTRIBUTES &&
		!(aFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}


static void generateResourceProfile(size_t theProfileID)
{
	DBG_ASSERT(theProfileID < ARRAYSIZE(kResourceProfiles));

	HMODULE hModule = GetModuleHandle(NULL);

	HRSRC hResource = FindResource(
		hModule,
		MAKEINTRESOURCE(kResourceProfiles[theProfileID].resID), L"TEXT");

	const std::string aFileName(kResourceProfiles[theProfileID].name);

	if( hResource != NULL )
	{
		HGLOBAL hGlobal = LoadResource(hModule, hResource);

		if( hGlobal != NULL )
		{
			void* data = LockResource(hGlobal);
			DWORD size = SizeofResource(hModule, hResource);

			std::ofstream aFile(
				widen(iniFolderPath() + aFileName).c_str(),
				std::ios::binary);
			if( aFile.is_open() )
			{
				aFile << static_cast<char*>(data);
				aFile.close();
			}
			else
			{
				logError("Unable to save default settings file to %s\n",
					(iniFolderPath() + aFileName).c_str());
				gHadFatalError = true;
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
	if( theKey.compare(0, kProfilePrefix.length(), kProfilePrefix) == 0)
	{
		const int aProfileNum =
			intFromString(theKey.substr(kProfilePrefix.length()));
		if( aProfileNum > 0 )
		{
			std::string aProfileName(theValue);
			if( !aProfileName.empty() )
			{
				if( getExtension(aProfileName) != ".ini" )
					aProfileName = removeExtension(aProfileName) + ".ini";
				if( profileExists(aProfileName) )
				{
					if( aProfileNum >= sAvailableProfiles.size() )
						sAvailableProfiles.resize((size_t)aProfileNum + 1);
					sAvailableProfiles[aProfileNum] = aProfileName;
				}
				else
				{
					logError("Could not find profile %s listed in %s",
						(iniFolderPath() + aProfileName).c_str(),
						kResourceProfiles[0].name);
				}
			}
		}
	}
	else if( theKey == "AUTOLOADPROFILE" )
	{
		std::string anAutoProfileName(theValue);
		if( anAutoProfileName.empty() )
			return;

		int anAutoProfileNum = intFromString(anAutoProfileName);
		if( anAutoProfileNum > 0 &&
			toString(anAutoProfileNum).length() == anAutoProfileName.length() )
		{// Specified by number
			sAutoProfileIdx = anAutoProfileNum;
			return;
		}

		// Specified by name
		anAutoProfileName = withExtension(anAutoProfileName, ".ini");
		if( profileExists(anAutoProfileName) )
			sAvailableProfiles[0] = anAutoProfileName;
		else
			logError("Could not find auto-load profile %s",
				(iniFolderPath() + anAutoProfileName).c_str());
	}
}


static void addParentCallback(
   const std::string& theKey,
   const std::string& theValue,
   void* theLoadList)
{
	if( !theLoadList )
		return;

	StringsVec* aLoadPriorityList = (StringsVec*)(theLoadList);

	if( theKey == "PARENTPROFILE" || theKey == "PARENT" )
	{
		std::string aParentProfileName(theValue);
		if( aParentProfileName.empty() )
			return;

		aParentProfileName = withExtension(aParentProfileName, ".ini");
		// Don't add entries already added before (or can get inifnite loop)
		if( std::find(
				aLoadPriorityList->begin(),
				aLoadPriorityList->end(),
				aParentProfileName) == aLoadPriorityList->end() )
		{
			if( profileExists(aParentProfileName) )
				aLoadPriorityList->push_back(aParentProfileName);
			else
				logError("Could not find parent profile %s",
					(iniFolderPath() + aParentProfileName).c_str());
		}
	}
}


static void readProfileCallback(
   const std::string& theKey,
   const std::string& theValue,
   void*)
{
	sSettingsMap.setValue(theKey, theValue);
}


static void loadProfile(const std::string& theProfileName)
{
	sSettingsMap.clear();
	sSettingsMap.trim();

	// Build list of .ini's to load in order of their priority
	StringsVec aLoadPriorityList;

	// Active profile is first priority (settings override all others)
	aLoadPriorityList.push_back(theProfileName);

	// Parse and add parent, parent of parent, etc.
	size_t aLoadListSize = 0;
	while(aLoadPriorityList.size() > aLoadListSize)
	{
		parseINI(
			addParentCallback,
			aLoadPriorityList[aLoadListSize++],
			eParseMode_Header,
			&aLoadPriorityList);
	}

	// Finally the core .ini as lowest-priority settings
	aLoadPriorityList.push_back(std::string(kResourceProfiles[0].name));

	// Now load each .ini's values 1-by-1 in reverse order
	// Any setting in later files overrides previous setting,
	// which is why we load them in reverse of priority order
	for(StringsVec::reverse_iterator itr = aLoadPriorityList.rbegin();
		itr != aLoadPriorityList.rend(); ++itr)
	{
		parseINI(
			readProfileCallback,
			*itr,
			eParseMode_Categories);
	}

	// Set active profile name for future writes
	sActiveProfileName = theProfileName;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void load()
{
	#ifdef _DEBUG
	// In debug builds, always re-generate base .ini files
	for(size_t i = 0; i < 4; ++i)
		generateResourceProfile(i);
	#endif

	// Make sure core .ini file exists
	if( !profileExists(kResourceProfiles[0].name) )
		generateResourceProfile(0);
	if( gHadFatalError )
		return;
	
	// Get list of profiles and auto-load profile from core
	sActiveProfileName.clear();
	sAvailableProfiles.clear();
	sAvailableProfiles.push_back(std::string(""));
	sAutoProfileIdx = 0;
	parseINI(
		getProfileListCallback,
		kResourceProfiles[0].name,
		eParseMode_Header);
	if( gHadFatalError )
		return;

	// See if need to query user for profile to load
	if( sAutoProfileIdx < 0 ||
		sAutoProfileIdx >= sAvailableProfiles.size() ||
		sAvailableProfiles[sAutoProfileIdx].empty() )
	{
		if( sAutoProfileIdx > 0 )
		{
			logError(
				"AutoLoadProfile = %d but no profile #%d found!",
				sAutoProfileIdx, sAutoProfileIdx);
		}
		sAutoProfileIdx = 0;
	}

	if( sAvailableProfiles[sAutoProfileIdx].empty() )
		queryUserForProfile();
	else
		loadProfile(sAvailableProfiles[sAutoProfileIdx]);
}


void queryUserForProfile()
{
	// TODO
	// TEMP - just generate and select a default profile
	static const size_t kDefaultProfileID = 5; // PQ
	//if( !profileExists(kResourceProfiles[kDefaultProfileID].name) )
		generateResourceProfile(kDefaultProfileID);
	sAvailableProfiles.push_back(kResourceProfiles[kDefaultProfileID].name);
	sAutoProfileIdx = sAvailableProfiles.size()-1;
	loadProfile(sAvailableProfiles[sAutoProfileIdx]);
	// END TEMP
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


void getIntArray(const std::string& theKey, std::vector<int>* out)
{
	const std::string& aString = getStr(theKey);	
	StringsVec aParsedString;
	sanitizeSentence(aString, &aParsedString);
	out->resize(max(out->size(), aParsedString.size()));
	for(size_t i = 0; i < aParsedString.size(); ++i)
		(*out)[i] = intFromString(aParsedString[i]);
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
