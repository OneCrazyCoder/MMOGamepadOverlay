//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "TargetConfigSync.h"

#include "HUD.h" // updateScaling()
#include "Lookup.h"
#include "Profile.h"

#include <winioctl.h> // FSCTL_REQUEST_FILTER_OPLOCK

namespace TargetConfigSync
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

const char* kTargetConfigFilesPrefix = "TargetConfigFiles/";
const char* kSyncPropertiesPrefix = "TargetSyncProperties/";

enum EPropType
{
	ePropType_Scale,
	ePropType_Hotspot,
};

enum EPropParamType
{
	ePropParam_Scale,
	//ePropParam_OffsetX,
	//ePropParam_OffsetY,
	//ePropParam_AnchorX,
	//ePropParam_AnchorY,
	//ePropParam_MinAnchorX,
	//ePropParam_MinAnchorY,
	//ePropParam_MaxAnchorX,
	//ePropParam_MaxAnchorY,
	//ePropParam_PivotX,
	//ePropParam_PivotY,
	//ePropParam_Width,
	//ePropParam_Height,

	ePropParam_Num
};

enum EConfigFileFormat
{
	eConfigFileFormat_JSON,
	eConfigFileFormat_INI,
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct TargetSyncProperty :
	public ConstructFromZeroInitializedMemory<TargetSyncProperty>
{
	std::string section, name;
	EPropType type;
	double values[ePropParam_Num];
	bool newValueFound;
};

struct TargetConfigValue
{
	u16 syncPropID;
	u16 propParamType; // EPropParamType
};

struct TargetConfigFile
{
	EConfigFileFormat format;
	std::wstring pathW;
	FILETIME lastModTime;
	StringToValueMap<TargetConfigValue> values;
};

struct TargetConfigFolder
{
	HANDLE hChangedSignal;
	std::vector<u16> fileIDs;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<TargetConfigFolder> sFolders;
static std::vector<TargetConfigFile> sFiles;
static std::vector<TargetSyncProperty> sProperties;
static BitVector<> sChangedFiles;
bool sInitialized = false;


//-----------------------------------------------------------------------------
// TargetConfigFileParser
//-----------------------------------------------------------------------------

class TargetConfigFileParser
{
public:
	TargetConfigFileParser(size_t theFileID) :
		mFileID(theFileID),
		mUnfoundCount(int(sFiles[theFileID].values.size()))
	{}
	virtual ~TargetConfigFileParser() {}
	virtual bool parse(std::string&) = 0;
protected:
	bool anyPathsUsePrefix(const std::string& thePrefix) const
	{
		return sFiles[mFileID].values.containsPrefix(thePrefix);
	}

	void checkForFoundValue(
		const std::string& thePath,
		const std::string& theValue)
	{
		TargetConfigValue* aValuePtr = sFiles[mFileID].values.find(thePath);
		if( !aValuePtr )
			return;

		TargetSyncProperty& aProperty = sProperties[aValuePtr->syncPropID];
		aProperty.values[aValuePtr->propParamType] =
			doubleFromString(theValue);		
		aProperty.newValueFound = true;
		--mUnfoundCount;
	}

	const size_t mFileID;
	int mUnfoundCount;
};


//-----------------------------------------------------------------------------
// TargetConfigJSONParser
//-----------------------------------------------------------------------------

class TargetConfigJSONParser : public TargetConfigFileParser
{
public:
	TargetConfigJSONParser(size_t theFileID) :
		TargetConfigFileParser(theFileID),
		mPathsFoundCount()
	{
		mPath.reserve(256);
		mReadStr.reserve(32);
		mState.reserve(16);
		pushState(eState_Init);
	}
	virtual ~TargetConfigJSONParser() {}

	virtual bool parse(std::string& theReadChunk)
	{
		std::string::size_type aReadPos = 0;
		DBG_ASSERT(!mState.empty());
		while(aReadPos < theReadChunk.size())
		{
			const char ch = theReadChunk[aReadPos++];

			switch(mState.back().type)
			{
			case eState_Init:
				// Waiting for root object or array
				switch(ch)
				{
				case '{':
					mState.back().type = eState_Finish;
					pushState(eState_Object);
					break;
				case '[':
					mState.back().type = eState_Finish;
					pushState(eState_Array);
					break;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Ignored
					break;
				default:
					// Invalid character
					return false;
				}
				break;

			case eState_Object:
				// Waiting for next key or end of object
				switch(ch)
				{
				case '"':
					mReadStr.clear();
					pushState(eState_Key);
					pushState(eState_String);
					break;
				case '}':
					popState();
					break;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Ignored
					break;
				default:
					// Invalid character
					return false;
				}
				break;

			case eState_Array:
				// Waiting for next value type or end of array
				switch(ch)
				{
				case ']':
					popState();
					break;
				case '}':
					return false;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Ignored
					break;
				default:
					// First character of new value type
					mPath.resize(mState.back().pathLen);
					mPath.push_back('[');
					mPath += toString(mState.back().valueIdx);
					mPath.push_back(']');
					pushState(eState_ValueType);
					--aReadPos; // re-read char in _ValueType state
					break;
				}
				break;

			case eState_Key:
				// Waiting for transition to value
				// Reached via popState() from Key->String
				switch(ch)
				{
				case ':':
					popState();
					if( !mPath.empty() ) mPath.push_back('.');
					mPath += mReadStr;
					pushState(eState_ValueType);
					break;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Ignored
					break;
				default:
					// Invalid character
					return false;
				}
				break;

			case eState_ValueType:
				// Waiting for char indicating type of value
				if( !isspace(ch) )
				{
					mState.back().type = eState_Value;
					mReadStr.clear();
					switch(ch)
					{
					case '}': case ']': case ',': case ':':
						// Invalid in this state
						return false;
					case '{':
						pushState(eState_Object);
						// Special case: Do not skip objects as array elements
						// until have checked the first key
						if( !mState.back().skip )
						{// See if should skip ahead due to no matching prefix
							// Special case: If are an array element, do not
							// skip ahead until check first key's value
							DBG_ASSERT(mState.size() >= 3);
							if( mState[mState.size()-3].type != eState_Array &&
								!anyPathsUsePrefix(mPath) )
							{
								mState.back().skip = true;
							}
						}
						break;
					case '[':
						pushState(eState_Array);
						if( !mState.back().skip && !anyPathsUsePrefix(mPath) )
							mState.back().skip = true;
						break;
					case '"':
						pushState(eState_String);
						break;
					default:
						mReadStr.push_back(ch);
						pushState(eState_Primitive);
						break;
					}
				}
				break;

			case eState_Value:
				// Waiting for end-of-value indicator (like comma)
				// Reached via popState() from Value->String/Primitive
				switch(ch)
				{
				case '}':
				case ']':
					// Re-read this char in parent state later
					--aReadPos;
					// fall through
				case ',':
					// Special case: For array elements that are objects, if
					// the object has at least one key-value pair and the first
					// key's value is a primitive (not an object or array),
					// replace the array index in the path with the value of
					// the first key. This allows paths like
					// "arrayOfObjects.Object1.name" instead of
					// "arrayOfObjects[0].name", and assumes that the first
					// key in an array element object is an "id" key.
					if( mState.back().skip )
					{
						popState();
					}
					else if( !mState.back().skip &&
							 mState[mState.size()-2].type == eState_Object &&
							 mState[mState.size()-3].type == eState_Value &&
							 mState[mState.size()-4].type == eState_Array &&
							 mState[mState.size()-2].valueIdx == 0 )
					{
						popState();
						// First value of an object as an array element
						if( !mReadStr.empty() )
						{
							mPath.resize(
								mState[mState.size()-3].pathLen);
							if( !mPath.empty() ) mPath.push_back('.');
							mPath += mReadStr;
							mState[mState.size()-1].pathLen =
								mState[mState.size()-2].pathLen =
								u32(mPath.size());
						}
						// Can now check if this path even matters
						if( !anyPathsUsePrefix(mPath) )
							mState.back().skip = true;
					}
					else
					{
						if( !mReadStr.empty() )
						{
							checkForFoundValue(mPath, mReadStr);
							if( mUnfoundCount <= 0 )
								return false;
						}
						popState();
					}					
					mReadStr.clear();
					++mState.back().valueIdx;
					if( mState.back().type == eState_Object && ch == ']' )
						return false;
					if( mState.back().type == eState_Array && ch == '}' )
						return false;
					break;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Ignored
					break;
				default:
					// Invalid character
					return false;
				}
				break;

			case eState_Primitive:
				// Reading a sequence of characters forming a number/bool/null
				switch(ch)
				{
				case '{': case '[': case ':':
					// Invalid in this state
					return false;
				case ',': case '}': case ']':
					popState();
					// Need to have pop'd state process this char
					--aReadPos;
					break;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Finished with reading primitive
					popState();
					break;
				default:
					// Add to primitive string
					mReadStr.push_back(ch);
					break;
				}
				break;

			case eState_String:
				// Reading a string (char sequence contained in "")
				switch(ch)
				{
				case '"': popState(); break;
				case '\\': pushState(eState_StringEsc); break;
				case '\t': mReadStr += "\\t"; break;
				case '\n': mReadStr += "\\n"; break;
				case '\r': mReadStr += "\\r"; break;
				default: mReadStr.push_back(ch); break;
				}
				break;

			case eState_StringEsc:
				// Checking for next character after \ in a string
				if( ch == '"' || ch == '\\' )
				{
					mReadStr.push_back(ch);
				}
				else
				{
					mReadStr.push_back('\\');
					mReadStr.push_back(ch);					
				}
				popState();
				break;

			case eState_Finish:
				// No more to parse
				return false;
			}
		}

		return true;
	}

private:
	enum EState
	{
		eState_Init,
		eState_Object,
		eState_Array,
		eState_Key,
		eState_ValueType,
		eState_Value,
		eState_Primitive,
		eState_String,
		eState_StringEsc,
		eState_Finish,
	};
	struct State
	{
		EState type;
		u16 pathLen;
		u16 valueIdx;
		bool skip;
	};
	std::vector<State> mState;
	std::string mReadStr; // Key or value currently being read
	std::string mPath; // Object/array path to current value
	int mPathsFoundCount;

	inline void pushState(EState theState)
	{
		State aNewState;
		aNewState.type = theState;
		aNewState.valueIdx = 0;
		aNewState.pathLen = u16(mPath.size());
		aNewState.skip = mState.empty() ? false : mState.back().skip;
		mState.push_back(aNewState);
	}
	inline void popState()
	{
		mState.pop_back();
		mPath.resize(mState.back().pathLen);
	}
};


//-----------------------------------------------------------------------------
// TargetConfigINIParser
//-----------------------------------------------------------------------------

class TargetConfigINIParser : public TargetConfigFileParser
{
public:
	TargetConfigINIParser(size_t theFileID) :
		TargetConfigFileParser(theFileID)
	{
	}

	virtual ~TargetConfigINIParser() {}
	virtual bool parse(std::string& theReadChunk)
	{
		// TODO
		return false;
	}
};


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

FILETIME getFileLastModTime(const TargetConfigFile& theFile)
{
	WIN32_FILE_ATTRIBUTE_DATA aFileAttr;
	if( GetFileAttributesEx(theFile.pathW.c_str(),
			GetFileExInfoStandard, &aFileAttr) )
	{
		return aFileAttr.ftLastWriteTime;
	}

	return FILETIME();
}


bool parseTargetConfigFile(size_t theFileID)
{
	DBG_ASSERT(theFileID < sFiles.size());
	TargetConfigFile& theFile = sFiles[theFileID];
	struct AutoHandle
	{
		AutoHandle(HANDLE theHandle = NULL) : mHandle(theHandle) {}
		~AutoHandle() { CloseHandle(mHandle); }
		operator HANDLE() const { return mHandle; }
		HANDLE mHandle;
	};

	// Get a file handle that won't block other apps' access to the file
	AutoHandle hLockFile = CreateFile(
		theFile.pathW.c_str(),
		0,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL);
	if( hLockFile == INVALID_HANDLE_VALUE )
	{
		logToFile("Failed to find target config file %s",
			narrow(theFile.pathW).c_str());
		return true;
	}

	// Request a filter OpLock to the file so can get out of the way if
	// target app needs the file - without causing it any sharing errors
	OVERLAPPED aLockOverlapped = {};
	aLockOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	DBG_ASSERT(aLockOverlapped.hEvent);
	AutoHandle aLockOverlappedEvent(aLockOverlapped.hEvent);
	DeviceIoControl(hLockFile, FSCTL_REQUEST_FILTER_OPLOCK,
		NULL, 0, NULL, 0, NULL, &aLockOverlapped);
	switch (GetLastError())
	{
	case ERROR_IO_PENDING:
		// Expected result for successful lock
		break;
	case ERROR_OPLOCK_NOT_GRANTED:
	case ERROR_CANNOT_GRANT_REQUESTED_OPLOCK:
		// File in use - try again next update
		return false;
	default:
		logToFile("Failed to get oplock read access to target config file %s",
			narrow(theFile.pathW).c_str());
		return true;
	}

	// Open the OpLock'd file for read
	AutoHandle hFile = CreateFile(
		theFile.pathW.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL);
	if( hFile == INVALID_HANDLE_VALUE )
	{
		logToFile("Failed to open target config file %s",
			narrow(theFile.pathW).c_str());
		return true;
	}

	OVERLAPPED aReadOverlapped = {};
	aReadOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	DBG_ASSERT(aReadOverlapped.hEvent);
	AutoHandle aReadOverlappedEvent(aReadOverlapped.hEvent);

	// Decide which parser to use for this file
	struct ParserPtr
	{
		ParserPtr(EConfigFileFormat theFormat, size_t theFileID)
			: mParser(NULL)
		{
			switch(theFormat)
			{
			case eConfigFileFormat_INI:
				mParser = new TargetConfigINIParser(theFileID);
				break;
			case eConfigFileFormat_JSON:
				mParser = new TargetConfigJSONParser(theFileID);
				break;
			}
		}
		~ParserPtr() { delete mParser; }
		TargetConfigFileParser* operator->() { return mParser; }
		TargetConfigFileParser* mParser;
	} aParser(theFile.format, theFileID);

	// Begin reading file
	char aBuffer[1024];
	DWORD aBytesRead = 0;
	LARGE_INTEGER aFilePointer = {};
	while(true)
	{
		// Set the file pointer for the next read
		aReadOverlapped.Offset = aFilePointer.LowPart;
		aReadOverlapped.OffsetHigh = aFilePointer.HighPart;

		// Use overlapped async file read
		if( !ReadFile(hFile, aBuffer, sizeof(aBuffer),
				&aBytesRead, &aReadOverlapped) )
		{
			if( GetLastError() != ERROR_IO_PENDING )
				return true;
		}

		// Get read result
		if( !GetOverlappedResult(hFile, &aReadOverlapped, &aBytesRead, TRUE) )
			return true;

		// If got OpLock break request abort and try again later
		if( WaitForSingleObject(aLockOverlapped.hEvent, 0) == WAIT_OBJECT_0 )
			return false;

		// Update the file pointer
		aFilePointer.QuadPart += aBytesRead;
		
		// Parse the read-in data
		if( !aParser->parse(std::string(aBuffer, aBytesRead)) )
			return true;

		// Alternate eof check
		if( aBytesRead < sizeof(aBuffer) )
			return true;
	}

	return true;
}


std::string hotspotValuesToStr(const TargetSyncProperty& theProp)
{
	std::string result;
	return result;
}


void applyReadTargetConfigValues()
{
	const double oldScale = gUIScale;
	double aUIScale = Profile::getFloat("System/UIScale", 1.0f);
	if( aUIScale <= 0 )
		aUIScale = 1.0;
	gUIScale = aUIScale * gWindowUIScale;
	if( gUIScale != oldScale )
		HUD::updateScaling();
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void load()
{
	sFolders.clear();
	sFiles.clear();
	sProperties.clear();
	StringToValueMap<u16, u8> aFilePathMap;
	StringToValueMap<u16, u8> aConfigFileIDMap;

	// Fetch target config files potentially containing sync properties
	Profile::KeyValuePairs aKeyValueList;
	Profile::getAllKeys(kTargetConfigFilesPrefix, aKeyValueList);
	for(size_t i = 0; i < aKeyValueList.size(); ++i)
	{
		const std::string& aFilePath =
			upper(removePathParams(expandPathVars(aKeyValueList[i].second)));
		const u16 aFileID = aFilePathMap.findOrAdd(
			aFilePath, u16(sFiles.size()));
		if( aFileID >= sFiles.size() )
		{
			TargetConfigFile aNewConfigFile;
			aNewConfigFile.pathW = widen(aFilePath);
			aNewConfigFile.format = eConfigFileFormat_JSON; // TODO properly
			aNewConfigFile.lastModTime = FILETIME();
			sFiles.push_back(aNewConfigFile);
		}
		aConfigFileIDMap.setValue(upper(aKeyValueList[i].first), aFileID);
	}

	// Fetch sync property values to read from the config files
	// Format of property names:
	//	SectionName.PropertyName =
	// Format of property values:
	//	ConfigFileID.RootKey.NestedKey.NestedKey...FinalKey
	//	For arrays in .json files, the "key" for an array element object
	//	is assumed to be the value for that object's first key. Other
	//	array elements can be specified with [index] syntax.
	//	For .ini files format is ConfigFileID.Section.ValueName
	aKeyValueList.clear();
	Profile::getAllKeys(kSyncPropertiesPrefix, aKeyValueList);
	for(size_t i = 0; i < aKeyValueList.size(); ++i)
	{
		// Replace first . in key with / for the property
		TargetSyncProperty aProperty;
		aProperty.type = ePropType_Scale; // TODO - actually calculate this
		aProperty.section = upper(aKeyValueList[i].first);
		size_t aPos = aProperty.section.find('.');
		if( aPos == std::string::npos )
			continue;
		aProperty.name = aProperty.section.substr(aPos+1);
		aProperty.section.resize(aPos);
		std::string aPropDesc = aKeyValueList[i].second;
		if( aPropDesc.empty() )
			continue;
		std::string aFileKey = upper(breakOffItemBeforeChar(aPropDesc, '.'));
		if( aFileKey.empty() )
			continue;
		if( const u16* aFileIDPtr = aConfigFileIDMap.find(aFileKey) )
		{
			const u16 aPropertyID = u16(sProperties.size());
			sProperties.push_back(aProperty);
			TargetConfigValue aConfigValue;
			aConfigValue.propParamType = ePropParam_Scale;
			aConfigValue.syncPropID = aPropertyID;
			sFiles[*aFileIDPtr].values.setValue(aPropDesc, aConfigValue);
		}
	}

	aKeyValueList.clear();
	aFilePathMap.clear();
	aConfigFileIDMap.clear();
	sChangedFiles.clearAndResize(sFiles.size());

	// Begin monitoring folders for changes to contained files w/ properites
	for(u16 i = 0; i < sFiles.size(); ++i)
	{
		if( sFiles[i].values.empty() )
			continue;
		sFiles[i].values.trim();
		const std::string& aFolderPath = getFileDir(narrow(sFiles[i].pathW));
		const u16 aFolderID = aFilePathMap.findOrAdd(
			aFolderPath, u16(sFolders.size()));
		if( aFolderID >= sFolders.size() )
		{
			TargetConfigFolder aNewConfigFolder;
			aNewConfigFolder.hChangedSignal = FindFirstChangeNotification(
				widen(aFolderPath).c_str(),
				FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
			sFolders.push_back(aNewConfigFolder);
		}
		sFolders[aFolderID].fileIDs.push_back(i);
	}

	// Load initial values and log file timestamps
	sInitialized = true;
	refresh();
}


void refresh()
{
	if( !sInitialized )
		return;

	for(size_t i = 0; i < sFiles.size(); ++i)
	{
		if( sFiles[i].values.empty() )
			continue;
		sFiles[i].lastModTime = getFileLastModTime(sFiles[i]);
		sChangedFiles.set(i);
	}
	if( sChangedFiles.any() )
		update();
	else
		applyReadTargetConfigValues();
}


void stop()
{
	for(size_t i = 0; i < sFolders.size(); ++i)
		FindCloseChangeNotification(sFolders[i].hChangedSignal);
	sFolders.clear();
	sFiles.clear();
	sProperties.clear();
	sChangedFiles.clear();
	sInitialized = false;
}


void update()
{
	if( !sInitialized )
		return;

	for(size_t aFolderID = 0; aFolderID < sFolders.size(); ++aFolderID)
	{
		TargetConfigFolder& aFolder = sFolders[aFolderID];
		if( WaitForSingleObject(aFolder.hChangedSignal, 0)
				== WAIT_OBJECT_0 )
		{
			// Re-arm the notification for next update
			FindNextChangeNotification(aFolder.hChangedSignal);

			// Check if any contained files are updated (by checking timestamp)
			for( size_t i = 0; i < aFolder.fileIDs.size(); ++i )
			{
				TargetConfigFile& aFile = sFiles[aFolder.fileIDs[i]];
				FILETIME aFileModTime = getFileLastModTime(aFile);
				if( CompareFileTime(&aFileModTime, &aFile.lastModTime) > 0 )
				{
					aFile.lastModTime = aFileModTime;
					sChangedFiles.set(aFolder.fileIDs[i]);
				}
			}
		}
	}

	const bool needApplyValues = sChangedFiles.any();

	for(int aFileID = sChangedFiles.firstSetBit();
		aFileID < sChangedFiles.size();
		aFileID = sChangedFiles.nextSetBit(aFileID+1))
	{
		if( parseTargetConfigFile(aFileID) )
			sChangedFiles.reset(aFileID);
	}

	if( needApplyValues )
	{
		for(size_t aPropID = 0; aPropID < sProperties.size(); ++aPropID)
		{
			TargetSyncProperty& aProp = sProperties[aPropID];
			if( !aProp.newValueFound )
				continue;
			aProp.newValueFound = false;
			std::string aValueStr;
			switch(aProp.type)
			{
			case ePropType_Scale:
				aValueStr = toString(aProp.values[ePropParam_Scale]);
				break;
			case ePropType_Hotspot:
				aValueStr = hotspotValuesToStr(aProp);
				break;
			}
			Profile::setStr(aProp.section, aProp.name, aValueStr, false);
		}

		applyReadTargetConfigValues();
	}
}

} // TargetConfigSync
