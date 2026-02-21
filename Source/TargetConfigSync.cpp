//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "TargetConfigSync.h"

#include "Dialogs.h"
#include "Profile.h"

#include <winioctl.h> // FSCTL_REQUEST_FILTER_OPLOCK

namespace TargetConfigSync
{

// Uncomment this to print details about config file syncing to debug window
//#define TARGET_CONFIG_SYNC_DEBUG_PRINT

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum {
kConfigFileBufferSize = 4096 // How many chars to stream from file per read
};

enum EConfigDataFormat
{
	eConfigDataFormat_JSON,
	eConfigDataFormat_INI,

	eConfigDataFormat_Num
};

enum EDataSourceType
{
	eDataSourceType_File,
	eDataSourceType_RegVal,

	eDataSourceType_Num
};

enum EValueSetType
{
	eValueSetType_Single,
	eValueSetType_UIWindow,

	eValueSetType_Num
};

enum EValueSetSubType
{
	// eValueSetType_Single
	eValueSetSubType_Base,

	// eValueSetType_UIWindow
	eValueSetSubType_PosX,
	eValueSetSubType_PosY,
	eValueSetSubType_AlignX,
	eValueSetSubType_AlignY,
	eValueSetSubType_AnchorTypeA,
	eValueSetSubType_PivotX,
	eValueSetSubType_PivotY,
	eValueSetSubType_SizeX,
	eValueSetSubType_SizeY,
	eValueSetSubType_Scale,

	eValueSetSubType_Num,
	eValueSetSubType_UIWindow_First = eValueSetSubType_PosX,
	eValueSetSubType_UIWindow_Last = eValueSetSubType_Scale,
};

enum EValueFunction
{
	eValueFunc_Base, // just return eValueSetSubType_Base
	eValueFunc_PosX,
	eValueFunc_PosY,
	eValueFunc_Left,
	eValueFunc_Top,
	eValueFunc_CX,
	eValueFunc_CY,
	eValueFunc_Right,
	eValueFunc_Bottom,
	eValueFunc_Width,
	eValueFunc_Height,
	eValueFunc_AlignX,
	eValueFunc_AlignY,
	eValueFunc_Scale,

	eValueFunc_Num
};

const char* kTargetConfigSettingsSectionName = "System";
const char* kTargetConfigFilesSectionName = "TargetConfigFiles";
const char* kTargetConfigVarsSectionName = "TargetConfigVariables";
const char* kValueFormatStrSectionName = "TargetConfigFormat";
const char* kValueFormatInvertPrefix = "Invert";
const char* kPromptForFilesPropertyName =
	"PromptForTargetConfigSyncFiles";
const char* kLastFileSelectedPropertyName =
	"LastTargetConfigSyncFileSelected";
const char* kLastTimeFileSelectedPropertyName =
	"LastTimeTargetConfigSyncFileSelected";
const char* kValueFormatStringKeys[] =
{
	"Value",		// eValueSetSubType_Base
	"PositionX",	// eValueSetSubType_PosX
	"PositionY",	// eValueSetSubType_PosY
	"AlignmentX",	// eValueSetSubType_AlignX
	"AlignmentY",	// eValueSetSubType_AlignY
	"AnchorTypeA",	// eValueSetSubType_AnchorTypeA - as in AOA
	"PivotX",		// eValueSetSubType_PivotX
	"PivotY",		// eValueSetSubType_PivotY
	"Width",		// eValueSetSubType_Width
	"Height",		// eValueSetSubType_Height
	"Scale",		// eValueSetSubType_Scale
};
DBG_CTASSERT(ARRAYSIZE(kValueFormatStringKeys) == eValueSetSubType_Num);

static const int kValueSetFirstIdx[] =
{
	eValueSetSubType_Base,				// eValueSetType_Single
	eValueSetSubType_UIWindow_First,	// eValueSetType_UIWindow
};
DBG_CTASSERT(ARRAYSIZE(kValueSetFirstIdx) == eValueSetType_Num);

static const int kValueSetLastIdx[] =
{
	eValueSetSubType_Base,				// eValueSetType_Single
	eValueSetSubType_UIWindow_Last,		// eValueSetType_UIWindow
};
DBG_CTASSERT(ARRAYSIZE(kValueSetLastIdx) == eValueSetType_Num);

static HKEY getRootKeyHandle(const std::string& root)
{
	if( root == "HKEY_LOCAL_MACHINE" )	return HKEY_LOCAL_MACHINE;
	if( root == "HKEY_CURRENT_USER" )	return HKEY_CURRENT_USER;
	if( root == "HKEY_CLASSES_ROOT" )	return HKEY_CLASSES_ROOT;
	if( root == "HKEY_CURRENT_CONFIG" )	return HKEY_CURRENT_CONFIG;
	if( root == "HKEY_USERS" )			return HKEY_USERS;
	return null;
}

static EValueFunction valueFuncNameToID(const std::string& theName)
{
	struct NameToEnumMapper
	{
		typedef StringToValueMap<EValueFunction, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			struct { const char* str; EValueFunction val; } kEntries[] = {
				{ "",			eValueFunc_Base		},
				{ "Base",		eValueFunc_Base		},
				{ "Value",		eValueFunc_Base		},
				{ "X",			eValueFunc_PosX		},
				{ "PosX",		eValueFunc_PosX		},
				{ "PositionX",	eValueFunc_PosX		},
				{ "XPos",		eValueFunc_PosX		},
				{ "XPosition",	eValueFunc_PosX		},
				{ "XOrigin",	eValueFunc_PosX		},
				{ "OriginX",	eValueFunc_PosX		},
				{ "Y",			eValueFunc_PosY		},
				{ "PosY",		eValueFunc_PosY		},
				{ "PositionY",	eValueFunc_PosY		},
				{ "YPos",		eValueFunc_PosY		},
				{ "YPosition",	eValueFunc_PosY		},
				{ "YOrigin",	eValueFunc_PosY		},
				{ "OriginY",	eValueFunc_PosY		},
				{ "Left",		eValueFunc_Left		},
				{ "L",			eValueFunc_Left		},
				{ "Top",		eValueFunc_Top		},
				{ "T",			eValueFunc_Top		},
				{ "CX",			eValueFunc_CX		},
				{ "CenterX",	eValueFunc_CX		},
				{ "CentreX",	eValueFunc_CX		},
				{ "XCenter",	eValueFunc_CX		},
				{ "XCentre",	eValueFunc_CX		},
				{ "CY",			eValueFunc_CY		},
				{ "CenterY",	eValueFunc_CY		},
				{ "CentreY",	eValueFunc_CY		},
				{ "YCenter",	eValueFunc_CY		},
				{ "YCentre",	eValueFunc_CY		},
				{ "Right",		eValueFunc_Right	},
				{ "R",			eValueFunc_Right	},
				{ "Bottom",		eValueFunc_Bottom	},
				{ "B",			eValueFunc_Bottom	},
				{ "Width",		eValueFunc_Width	},
				{ "W",			eValueFunc_Width	},
				{ "XSize",		eValueFunc_Width	},
				{ "SizeX",		eValueFunc_Width	},
				{ "Height",		eValueFunc_Height	},
				{ "H",			eValueFunc_Height	},
				{ "YSize",		eValueFunc_Height	},
				{ "SizeY",		eValueFunc_Height	},
				{ "AlignX",		eValueFunc_AlignX	},
				{ "AlignmentX",	eValueFunc_AlignX	},
				{ "AnchorX",	eValueFunc_AlignX	},
				{ "XAlignment",	eValueFunc_AlignX	},
				{ "XAlign",		eValueFunc_AlignX	},
				{ "XAnchor",	eValueFunc_AlignX	},
				{ "AlignY",		eValueFunc_AlignY	},
				{ "AlignmentY",	eValueFunc_AlignY	},
				{ "AnchorY",	eValueFunc_AlignY	},
				{ "YAlign",		eValueFunc_AlignY	},
				{ "YAlignment",	eValueFunc_AlignY	},
				{ "YAnchor",	eValueFunc_AlignY	},
				{ "Scale",		eValueFunc_Scale	},
				{ "Scaling",	eValueFunc_Scale	},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(int i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EValueFunction* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eValueFunc_Num;
}


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct ZERO_INIT(SyncVariable)
{
	int variableID;
	EValueFunction funcType;
	int valueSetID;
};

struct ZERO_INIT(ValueLink)
{
	u16 setIdx;
	u16 valueIdx;
};
typedef StringToValueMap<ValueLink> ValueLinkMap;

struct ZERO_INIT(DataSource)
{
	ValueLinkMap values;
	EConfigDataFormat format;
	EDataSourceType type;
	std::string pathPattern;
	FILETIME lastModTime;
	std::wstring pathToRead;
	std::vector<BYTE> dataCache;
	bool usesWildcards;
};

struct ZERO_INIT(ConfigFileFolder)
{
	HANDLE hChangedSignal;
	std::vector<u16> dataSourceIDs;
	bool recursiveCheckNeeded;
};

struct ZERO_INIT(SystemRegistryKey)
{
	HKEY hKey;
	HANDLE hChangedSignal;
	std::vector<u16> dataSourceIDs;
	bool recursiveCheckNeeded;
};

// Data used during parsing/building the sync links but deleted once done
struct TargetConfigSyncBuilder
{
	std::vector<ValueLinkMap> valueLinkMaps;
	StringToValueMap<int> nameToLinkMapID;
	StringToValueMap<int, u16, true> pathToLinkMapID;
	StringToValueMap<int> valueSetNameToIDMap;
	std::string valueFormatStrings[eValueSetSubType_Num];
	std::string debugString;
};

// Forward declares
class ConfigDataFinder;
class ConfigDataReader;
class ConfigDataParser;


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static std::vector<DataSource> sDataSources;
static std::vector<ConfigFileFolder> sFolders;
static std::vector<SystemRegistryKey> sRegKeys;
static std::vector<SyncVariable> sVariables;
static std::vector<double> sValues;
static std::vector<u16> sValueSets;
static BitVector<32> sDataSourcesToCheck;
static BitVector<32> sDataSourcesToRead;
static BitVector<32> sDataSourcesToReCheck;
static BitVector<64> sValueSetsChanged;
static ConfigDataFinder* sFinder;
static ConfigDataReader* sReader;
static ConfigDataParser* sParser;
static std::wstring sLastWildcardFileSelected;
static FILETIME sLastChangeDetectedTime;
static FILETIME sLastTimeWildcardFileSelected;
static bool sInvertAxis[eValueSetSubType_Num];
static bool sInitialized = false;
static bool sPaused = false;
static bool sPromptForWildcardFiles = false;
static bool sForcePromptForWildcardFiles = false;


//------------------------------------------------------------------------------
// Debugging
//------------------------------------------------------------------------------

#ifdef TARGET_CONFIG_SYNC_DEBUG_PRINT
#define syncDebugPrint(...) debugPrint("TargetConfigSync: " __VA_ARGS__)
#else
#define syncDebugPrint(...) ((void)0)
#endif


//------------------------------------------------------------------------------
// ConfigDataParser
//------------------------------------------------------------------------------

class ConfigDataParser
{
public:
	ConfigDataParser(int theDataSourceID) :
		mDataSourceID(theDataSourceID),
		mDoneParsing(false)
	{
		DBG_ASSERT(size_t(theDataSourceID) < sDataSources.size());
		mUnfound.clearAndResize(sDataSources[theDataSourceID].values.size());
		mUnfound.set();
	}
	virtual ~ConfigDataParser() {}

	virtual void parseNextChunk(const std::string&) = 0;

	void reportResults()
	{
		#ifdef TARGET_CONFIG_SYNC_DEBUG_PRINT
		if( mUnfound.any() )
		{
			syncDebugPrint("%d values not found:\n", mUnfound.count());
			for(int i = mUnfound.firstSetBit();
				i < mUnfound.size(); i = mUnfound.nextSetBit(i+1))
			{
				syncDebugPrint("  * %s\n",
					sDataSources[mDataSourceID].values.keys()[i].c_str());
			}
		}
		else
		{
			syncDebugPrint("All values found!\n");
		}
		#endif
	}

	bool done() const { return mDoneParsing; }
	int dataSourceID() const { return mDataSourceID; }

protected:
	bool anyPathsUsePrefix(const std::string& thePrefix) const
	{
		return sDataSources[mDataSourceID].values.containsPrefix(thePrefix);
	}

	bool checkForFoundValue(
		const std::string& thePath,
		const std::string& theValue)
	{
		const int aValueLinkID =
			sDataSources[mDataSourceID].values.findIndex(thePath);
		if( aValueLinkID < sDataSources[mDataSourceID].values.size() )
		{
			const ValueLink& aValueLink =
				sDataSources[mDataSourceID].values.vals()[aValueLinkID];
			sValues[aValueLink.valueIdx] = stringToDouble(theValue);
			sValueSetsChanged.set(aValueLink.setIdx);
			mUnfound.reset(aValueLinkID);
			syncDebugPrint("Read path %s value as %f\n",
				thePath.c_str(), sValues[aValueLink.valueIdx]);
		}
		return mUnfound.none();
	}

protected:
	const int mDataSourceID;
	BitVector<512> mUnfound;
	bool mDoneParsing;
};


//------------------------------------------------------------------------------
// JSONParser
//------------------------------------------------------------------------------

class JSONParser : public ConfigDataParser
{
public:
	JSONParser(int theDataSourceID) :
		ConfigDataParser(theDataSourceID),
		mPathsFoundCount()
	{
		mPath.reserve(256);
		mReadStr.reserve(32);
		mState.reserve(16);
		pushState(eState_Init);
	}

	virtual void parseNextChunk(const std::string& theReadChunk)
	{
		DBG_ASSERT(!mState.empty());
		for(const char* c = theReadChunk.c_str(); *c; ++c)
		{
			const char ch = *c;

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
					mDoneParsing = true;
					return;
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
					mDoneParsing = true;
					return;
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
					mDoneParsing = true;
					return;
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
					--c; // re-read char in _ValueType state
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
					mDoneParsing = true;
					return;
				}
				break;

			case eState_ValueType:
				// Waiting for char indicating type of value
				if( u8(ch) > ' ' )
				{
					mState.back().type = eState_Value;
					mReadStr.clear();
					switch(ch)
					{
					case '}': case ']': case ',': case ':':
						// Invalid in this state
						mDoneParsing = true;
						return;
					case '{':
						pushState(eState_Object);
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
					--c;
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
					else if( mState[mState.size()-2].type == eState_Object &&
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
									dropTo<u16>(mPath.size());
						}
						// Can now check if this path even matters
						if( !anyPathsUsePrefix(mPath) )
							mState.back().skip = true;
					}
					else
					{
						if( !mReadStr.empty() )
						{
							if( checkForFoundValue(mPath, mReadStr) )
							{// All values found!
								mDoneParsing = true;
								return;
							}
						}
						popState();
					}
					mReadStr.clear();
					++mState.back().valueIdx;
					if( (mState.back().type == eState_Object && ch == ']') ||
						(mState.back().type == eState_Array && ch == '}') )
					{// Incorrect ending character
						mDoneParsing = true;
						return;
					}
					break;
				case ' ': case '\t': case '\n':
				case '\r': case '\f': case '\v':
					// Ignored
					break;
				default:
					// Invalid character
					mDoneParsing = true;
					return;
				}
				break;

			case eState_Primitive:
				// Reading a sequence of characters forming a number/bool/null
				switch(ch)
				{
				case '{': case '[': case ':':
					// Invalid in this state
					mDoneParsing = true;
					return;
				case ',': case '}': case ']':
					popState();
					// Need to have pop'd state process this char
					--c;
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
				mDoneParsing = true;
				return;
			}
		}
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
		EState type : 4;
		u32 skip : 1;
		u32 pathLen : 13;
		u32 valueIdx : 14;
	};
	std::vector<State> mState;
	std::string mReadStr; // Key or value currently being read
	std::string mPath; // Object/array path to current value
	int mPathsFoundCount;

	void pushState(EState theState)
	{
		State aNewState;
		aNewState.type = theState;
		aNewState.valueIdx = 0;
		aNewState.pathLen = intSize(mPath.size());
		aNewState.skip = mState.empty() ? false : mState.back().skip;
		mState.push_back(aNewState);
	}

	void popState()
	{
		mState.pop_back();
		mPath.resize(mState.back().pathLen);
	}
};


//------------------------------------------------------------------------------
// INIParser
//------------------------------------------------------------------------------

class INIParser : public ConfigDataParser
{
public:
	INIParser(int theDataSourceID) :
		ConfigDataParser(theDataSourceID)
	{
	}

	virtual void parseNextChunk(const std::string& /*theReadChunk*/)
	{
		// TODO
		mDoneParsing = true;
		return;
	}
};


//------------------------------------------------------------------------------
// ConfigDataReader
//------------------------------------------------------------------------------

class ConfigDataReader
{
public:
	ConfigDataReader(int theDataSourceID)
		:
		mDataSourceID(theDataSourceID),
		mDoneReading(false),
		mSourceWasBusy(false),
		mErrorEncountered(false)
	{
	}

	virtual ~ConfigDataReader() {}

	virtual std::string readNextChunk() = 0;
	virtual void reportResults() = 0;

	bool done() const { return mDoneReading || mErrorEncountered; }
	bool sourceWasBusy() const { return mSourceWasBusy; }
	bool errorEncountered() const { return mErrorEncountered; }

protected:

	const int mDataSourceID;
	bool mDoneReading;
	bool mSourceWasBusy;
	bool mErrorEncountered;
};


//------------------------------------------------------------------------------
// ConfigFileReader
//------------------------------------------------------------------------------

class ConfigFileReader : public ConfigDataReader
{
public:
	ConfigFileReader(int theDataSourceID) :
		ConfigDataReader(theDataSourceID),
		mBytesRead(),
		mFileHandle(),
		mFileLockHandle(),
		mFilePointer(),
		mReadOverlapped(),
		mLockOverlapped(),
		mBuffer(),
		mBufferIdx()
	{
		DBG_ASSERT(size_t(theDataSourceID) < sDataSources.size());
		const DataSource& aDataSource = sDataSources[theDataSourceID];

		// Get a file handle that won't block other apps' access to the file
		mFileLockHandle = CreateFile(
			aDataSource.pathToRead.c_str(),
			0,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
		if( mFileLockHandle == INVALID_HANDLE_VALUE )
		{
			logToFile("Failed to find target config file %ls",
				aDataSource.pathToRead.c_str());
			mErrorEncountered = true;
			return;
		}

		// Request a filter OpLock to the file so can get out of the way if
		// target app needs the file - without causing it any sharing errors
		mLockOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		DBG_ASSERT(mLockOverlapped.hEvent);
		DeviceIoControl(mFileLockHandle, FSCTL_REQUEST_FILTER_OPLOCK,
			NULL, 0, NULL, 0, NULL, &mLockOverlapped);
		switch (GetLastError())
		{
		case ERROR_IO_PENDING:
			// Expected result for successful lock
			break;
		case ERROR_OPLOCK_NOT_GRANTED:
		case ERROR_CANNOT_GRANT_REQUESTED_OPLOCK:
			// File in use - try again later
			syncDebugPrint("File %s in use - trying again later!\n",
				getFileName(narrow(aDataSource.pathToRead)).c_str());
			mDoneReading = mSourceWasBusy = true;
			return;
		default:
			logToFile(
				"Failed to get oplock read access to target config file %ls",
				aDataSource.pathToRead.c_str());
			mErrorEncountered = true;
			return;
		}

		// Open the OpLock'd file for read
		mFileHandle = CreateFile(
			aDataSource.pathToRead.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
		if( mFileHandle == INVALID_HANDLE_VALUE )
		{
			logToFile("Failed to open target config file %ls",
				aDataSource.pathToRead.c_str());
			mErrorEncountered = true;
			return;
		}

		// Begin first read
		mReadOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		DBG_ASSERT(mReadOverlapped.hEvent);
		if( !ReadFile(
				mFileHandle,
				mBuffer[mBufferIdx],
				kConfigFileBufferSize,
				&mBytesRead[mBufferIdx],
				&mReadOverlapped) )
		{
			if( GetLastError() != ERROR_IO_PENDING )
			{
				logToFile("Error reading target config file %ls",
					aDataSource.pathToRead.c_str());
				mErrorEncountered = true;
			}
		}
	}

	virtual ~ConfigFileReader()
	{
		CloseHandle(mReadOverlapped.hEvent);
		CloseHandle(mLockOverlapped.hEvent);
		CloseHandle(mFileHandle);
		CloseHandle(mFileLockHandle);
	}

	virtual std::string readNextChunk()
	{
		std::string result;
		if( done() )
			return result;

		const DataSource& aDataSource = sDataSources[mDataSourceID];

		// If got OpLock break request abort and try again later
		if( WaitForSingleObject(mLockOverlapped.hEvent, 0) == WAIT_OBJECT_0 )
		{
			syncDebugPrint("Another app needed file %ls - delaying read!\n",
				aDataSource.pathToRead.c_str());
			mDoneReading = mSourceWasBusy = true;
			return result;
		}

		// Wait for last async read request to complete
		if( !GetOverlappedResult(
				mFileHandle,
				&mReadOverlapped,
				&mBytesRead[mBufferIdx],
				TRUE) )
		{
			mDoneReading = true;
			return result;
		}

		if( mBytesRead[mBufferIdx] < kConfigFileBufferSize )
			mDoneReading = true;

		// Start reading next chunk while parsing this one
		if( !mDoneReading )
		{
			mFilePointer.QuadPart += mBytesRead[mBufferIdx];
			mReadOverlapped.Offset = mFilePointer.LowPart;
			mReadOverlapped.OffsetHigh = mFilePointer.HighPart;

			// Use overlapped async file read to begin reading to other buffer
			if( !ReadFile(
					mFileHandle,
					mBuffer[mBufferIdx ? 0 : 1],
					kConfigFileBufferSize,
					&mBytesRead[mBufferIdx ? 0 : 1],
					&mReadOverlapped) )
			{
				if( GetLastError() != ERROR_IO_PENDING )
				{
					logToFile("Error reading target config file %ls",
						aDataSource.pathToRead.c_str());
					mErrorEncountered = true;
					return result;
				}
			}
		}

		// Return the previously read-in data for parsing
		syncDebugPrint(
			"Read %d bytes from config file\n",
			mBytesRead[mBufferIdx]);
		result.assign((char*)&mBuffer[mBufferIdx], mBytesRead[mBufferIdx]);

		// Swap buffer to check for next read
		mBufferIdx = mBufferIdx ? 0 : 1;

		return result;
	}

	virtual void reportResults()
	{
		syncDebugPrint("Finished parsing '%s'\n", getFileName(narrow(
			sDataSources[mDataSourceID].pathToRead)).c_str());
	}

private:

	DWORD mBytesRead[2];
	HANDLE mFileHandle, mFileLockHandle;
	LARGE_INTEGER mFilePointer;
	OVERLAPPED mReadOverlapped, mLockOverlapped;
	int mBufferIdx;
	u8 mBuffer[2][kConfigFileBufferSize];
};


//------------------------------------------------------------------------------
// SystemRegistryValueReader
//------------------------------------------------------------------------------

class SystemRegistryValueReader : public ConfigDataReader
{
public:
	SystemRegistryValueReader(int theDataSourceID) :
		ConfigDataReader(theDataSourceID),
		mParsePos()
	{
		DBG_ASSERT(size_t(theDataSourceID) < sDataSources.size());
		const DataSource& aDataSource = sDataSources[theDataSourceID];
		if( aDataSource.dataCache.empty() )
			mDoneReading = true;
	}

	virtual std::string readNextChunk()
	{
		std::string result;
		if( done() )
			return result;

		const DataSource& aDataSource = sDataSources[mDataSourceID];
		DBG_ASSERT(size_t(mParsePos) < aDataSource.dataCache.size());

		// Return the previously read-in data for parsing
		const int aBytesToRead = min(
			intSize(aDataSource.dataCache.size()) - mParsePos,
			int(kConfigFileBufferSize));
		result.assign((char*)&aDataSource.dataCache[mParsePos], aBytesToRead);
		mParsePos += aBytesToRead;
		if( mParsePos >= intSize(aDataSource.dataCache.size()) )
			mDoneReading = true;

		syncDebugPrint(
			"Read %d bytes from system registry data\n",
			aBytesToRead);

		return result;
	}

	virtual void reportResults()
	{
		syncDebugPrint("Finished parsing '%ls'\n",
			sDataSources[mDataSourceID].pathToRead.c_str());
	}

private:

	int mParsePos;
};


//------------------------------------------------------------------------------
// ConfigDataFinder
//------------------------------------------------------------------------------

class ConfigDataFinder
{
public:
	ConfigDataFinder(int theDataSourceID)
		:
		mDataSourceID(theDataSourceID),
		mDoneSearching(false),
		mFoundSourceToRead(false),
		mSearchTriggeredChange(false)
	{
	}

	virtual ~ConfigDataFinder() {}
	virtual void checkNextLocation() = 0;

	bool done() const { return mDoneSearching; }
	bool foundSourceToRead() const { return mFoundSourceToRead; }
	bool searchTriggeredChange() const { return mSearchTriggeredChange; }
	int dataSourceID() const { return mDataSourceID; }

protected:

	void compareBestSource(
		const std::wstring& thePath,
		const std::wstring& theMatchedString,
		FILETIME theModTime) // also matching mValueBuf set for these
	{
		DBG_ASSERT(size_t(mDataSourceID) < sDataSources.size());
		DataSource& aDataSource = sDataSources[mDataSourceID];
		if( CompareFileTime(&theModTime, &aDataSource.lastModTime) < 0 )
			return;

		mCandidateNames.push_back(theMatchedString);
		mCandidates.push_back(DataSourceCandidate());
		mCandidates.back().lastModTime = theModTime;
		mCandidates.back().pathToRead = thePath;
		swap(mCandidates.back().dataCache, mValueBuf);
	}

	void finalizeBestSourceFound()
	{
		DBG_ASSERT(size_t(mDataSourceID) < sDataSources.size());
		DataSource& aDataSource = sDataSources[mDataSourceID];

		mDoneSearching = true;
		if( mCandidates.empty() )
			return;

		int aBestIdx = 0;
		DBG_ASSERT(mCandidates.size() == mCandidateNames.size());
		if( mCandidates.size() > 1 )
		{
			// Check if one of the candidates was previously selected already,
			// determined by at least one matched sub-string fully matching
			// from sLastWildcardFileSelected, and if that applies to more
			// than one candidate, select the one that most closely matches
			int aLastSelCandidateIdx = -1;
			if( !sLastWildcardFileSelected.empty() )
			{
				int aBestMatchCount = 0;
				for(int i = 0; i < intSize(mCandidateNames.size()); ++i)
				{
					int aMatchCount = 0;

					const std::wstring& a = sLastWildcardFileSelected;
					const std::wstring& b = mCandidateNames[i];

					const size_t aLen = a.length(), bLen = b.length();
					size_t aPos = 0, bPos = 0;

					while(aPos < aLen && bPos < bLen)
					{
						// Find next asterisk or end of string
						size_t aNext = a.find(L'*', aPos);
						size_t bNext = b.find(L'*', bPos);
						if( aNext == std::wstring::npos ) aNext = aLen;
						if( bNext == std::wstring::npos ) bNext = bLen;

						// Calculate lengths
						const int aSubLen = intSize(aNext) - intSize(aPos);
						const int bSubLen = intSize(bNext) - intSize(bPos);

						// Compare directly (case-insensitive)
						if( CompareStringOrdinal(
								&a[aPos], aSubLen,
								&b[bPos], bSubLen,
								TRUE) == CSTR_EQUAL )
						{
							++aMatchCount;
						}

						// Move past the '*'
						aPos = (aNext < aLen) ? aNext + 1 : aLen + 1;
						bPos = (bNext < bLen) ? bNext + 1 : bLen + 1;
					}
					if( aMatchCount > aBestMatchCount )
					{
						aBestMatchCount = aMatchCount;
						aLastSelCandidateIdx = i;
					}
				}
			}

			// Treat last selected candidate as having last mod time of
			// at least sLastTimeWildcardFileSelected time, so it will
			// be preferred over candidates that may have been modified
			// after it yet not modified since a selection was made.
			if( aLastSelCandidateIdx >= 0 &&
				CompareFileTime(
					&mCandidates[aLastSelCandidateIdx].lastModTime,
					&sLastTimeWildcardFileSelected) < 0 )
			{
				mCandidates[aLastSelCandidateIdx].lastModTime =
					sLastTimeWildcardFileSelected;
			}

			// Find candidate with most recent modification time
			for(int i = 1, end = intSize(mCandidates.size()); i < end; ++i)
			{
				FILETIME aTestTime = mCandidates[i].lastModTime;
				FILETIME aBestTime = mCandidates[aBestIdx].lastModTime;
				LONG aComp = CompareFileTime(&aTestTime, &aBestTime);
				if( aComp > 0 )
					aBestIdx = i;
				else if( aComp == 0 && i == aLastSelCandidateIdx )
					aBestIdx = i; // last selected candidate is tie-breaker
			}

			// If requested prompting for multiple found, and best candidate
			// isn't the last one selected, and are in initialization phase,
			// then bring up the dialog. Or if directly requested dialog.
			if( sForcePromptForWildcardFiles ||
				(sPromptForWildcardFiles &&
				 aBestIdx != aLastSelCandidateIdx &&
				 !sInitialized) )
			{
				sForcePromptForWildcardFiles = false;
				Dialogs::CharacterSelectResult aDialogResult =
					Dialogs::characterSelect(
						mCandidateNames, aBestIdx,
						!sPromptForWildcardFiles);
				if( !aDialogResult.cancelled )
				{
					aBestIdx = aDialogResult.selectedIndex;
					GetSystemTimeAsFileTime(&sLastTimeWildcardFileSelected);
					Profile::setStr(
						kTargetConfigSettingsSectionName,
						kLastFileSelectedPropertyName,
						narrow(mCandidateNames[aBestIdx]));
					Profile::setStr(
						kTargetConfigSettingsSectionName,
						kLastTimeFileSelectedPropertyName,
						toString(sLastTimeWildcardFileSelected));
					sPromptForWildcardFiles =
						!aDialogResult.autoSelectRequested;
					Profile::setStr(
						kTargetConfigSettingsSectionName,
						kPromptForFilesPropertyName,
						sPromptForWildcardFiles ? "Yes" : "No");
				}
			}

			sLastWildcardFileSelected = mCandidateNames[aBestIdx];
			syncDebugPrint(
				"Selected source for multi-source wildcard match: %s\n",
				narrow(sLastWildcardFileSelected).c_str());
		}

		aDataSource.pathToRead = mCandidates[aBestIdx].pathToRead;
		aDataSource.lastModTime = mCandidates[aBestIdx].lastModTime;
		swap(aDataSource.dataCache, mCandidates[aBestIdx].dataCache);

		mFoundSourceToRead = true;
	}

	struct ZERO_INIT(DataSourceCandidate)
	{
		FILETIME lastModTime;
		std::wstring pathToRead;
		std::vector<BYTE> dataCache;
	};
	std::vector<DataSourceCandidate> mCandidates;
	std::vector<std::wstring> mCandidateNames;
	std::vector<BYTE> mValueBuf;
	const int mDataSourceID;
	bool mDoneSearching;
	bool mFoundSourceToRead;
	bool mSearchTriggeredChange;
};


//------------------------------------------------------------------------------
// ConfigFileFinder
//------------------------------------------------------------------------------

class ConfigFileFinder : public ConfigDataFinder
{
public:
	ConfigFileFinder(int theDataSourceID) :
		ConfigDataFinder(theDataSourceID),
		mEntriesCheckedThisUpdate()
	{
		DBG_ASSERT(size_t(mDataSourceID) < sDataSources.size());
		DataSource& aDataSource = sDataSources[mDataSourceID];
		DBG_ASSERT(aDataSource.type == eDataSourceType_File);

		// Can check for the file right away if don't need wildcard search
		if( !aDataSource.usesWildcards )
		{
			if( aDataSource.pathToRead.empty() )
				aDataSource.pathToRead = widen(aDataSource.pathPattern);
			if( !isValidFilePath(aDataSource.pathToRead) )
			{
				logToFile("Failed to find target config file '%s'",
					aDataSource.pathPattern.c_str());
				mDoneSearching = true;
				return;
			}
			FILETIME aModTime = getFileLastModTime(aDataSource.pathToRead);
			if( CompareFileTime(&aModTime, &aDataSource.lastModTime) > 0 )
			{
				if( aDataSource.lastModTime.dwHighDateTime ||
					aDataSource.lastModTime.dwLowDateTime )
				{
					syncDebugPrint("Detected change in file %s\n",
						getFileName(aDataSource.pathPattern).c_str());
				}
				aDataSource.lastModTime = aModTime;
				mFoundSourceToRead = true;
			}
			mDoneSearching = true;
			return;
		}

		// Set up multi-file search with wildcard
		mRootPath = widen(getFileDir(aDataSource.pathPattern.substr(
				0, aDataSource.pathPattern.find('*')), true));
		if( !isValidFolderPath(mRootPath) )
		{
			logToFile("Root folder '%ls\' missing for pattern '%s'",
				mRootPath.c_str(),
				aDataSource.pathPattern.c_str());
			mDoneSearching = true;
			return;
		}

		mFolderStack.reserve(kMaxFolderStackSize);
		mFolderStack.resize(1);
		mPathPattern = widen(aDataSource.pathPattern).substr(mRootPath.size());
	}

	virtual ~ConfigFileFinder()
	{
		for(int i = 0, end = intSize(mFolderStack.size()); i < end; ++i)
		{
			if( mFolderStack[i].hFind )
				FindClose(mFolderStack[i].hFind);
		}
	}

	virtual void checkNextLocation()
	{
		if( done() )
			return;

		mEntriesCheckedThisUpdate = 0;

		while(!mFolderStack.empty())
		{
			FolderState& aFolderState = mFolderStack.back();
			if( !aFolderState.traverseFilesStarted )
			{
				aFolderState.hFind = FindFirstFile(
					(mRootPath + aFolderState.path + L'*').c_str(),
					&aFolderState.data);
				if( aFolderState.hFind == INVALID_HANDLE_VALUE )
				{
					mFolderStack.pop_back();
					continue;
				}
				aFolderState.traverseFilesStarted = true;
			}
			else if( !FindNextFileW(aFolderState.hFind, &aFolderState.data) )
			{
				FindClose(aFolderState.hFind);
				mFolderStack.pop_back();
				// Finishing a directory is enough work for one update
				return;
			}

			const WIN32_FIND_DATAW& ffd = aFolderState.data;
			std::wstring aFileName(ffd.cFileName);
			if( aFileName == L"." || aFileName == L".." )
				continue;

			if( ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				if( mFolderStack.size() < kMaxFolderStackSize )
				{
					aFileName = aFolderState.path + aFileName + L'\\';
					// Warning: invalidates current aFolderState reference!
					mFolderStack.push_back(FolderState());
					mFolderStack.back().path = aFileName;
				}
			}
			else
			{
				aFileName = aFolderState.path + aFileName;
				const std::wstring& aMatchStr =
					wildcardMatch(aFileName.c_str(), mPathPattern.c_str());
				if( !aMatchStr.empty() )
				{
					compareBestSource(
						mRootPath + aFileName,
						aMatchStr,
						ffd.ftLastWriteTime);
					// Finding a match is enough work for one update
					return;
				}
			}

			// Avoid long stalls from large directories
			if( ++mEntriesCheckedThisUpdate >= kMaxEntriesCheckedPerUpdate )
				return;
		}

		// Once mFolderStack is empty we are done searching
		finalizeBestSourceFound();
	}

private:
	static const int kMaxFolderStackSize = 16;
	static const int kMaxEntriesCheckedPerUpdate = 64;

	struct ZERO_INIT(FolderState)
	{
		std::wstring path;
		bool traverseFilesStarted;
		HANDLE hFind;
		WIN32_FIND_DATAW data;
	};

	std::vector<FolderState> mFolderStack;
	std::wstring mPathPattern;
	std::wstring mRootPath;
	int mEntriesCheckedThisUpdate;
};


//------------------------------------------------------------------------------
// SystemRegistryValueFinder
//------------------------------------------------------------------------------

class SystemRegistryValueFinder : public ConfigDataFinder
{
public:
	SystemRegistryValueFinder(int theDataSourceID) :
		ConfigDataFinder(theDataSourceID),
		mEntriesCheckedThisUpdate()
	{
		DBG_ASSERT(size_t(theDataSourceID) < sDataSources.size());
		DataSource& aDataSource = sDataSources[theDataSourceID];
		DBG_ASSERT(aDataSource.type == eDataSourceType_RegVal);

		std::string aSearchStartPath = aDataSource.pathPattern;
		if( aDataSource.usesWildcards )
		{
			aSearchStartPath = aSearchStartPath.substr(
				0, aDataSource.pathPattern.find('*'));
		}
		aSearchStartPath = getFileDir(aSearchStartPath);
		const std::string& aRootKeyName =
			breakOffNextItem(aSearchStartPath, '\\');
		HKEY aRootKey = getRootKeyHandle(aRootKeyName);
		if( !aRootKey )
		{
			logError("Invalid root registry key name in path '%s'",
				aDataSource.pathPattern.c_str());
			mDoneSearching = true;
			return;
		}
		mRootPath = widen(aSearchStartPath);
		HKEY aSearchStartHKey = NULL;
		if( RegOpenKeyEx(aRootKey, mRootPath.c_str(), 0,
				KEY_READ | KEY_WRITE, &aSearchStartHKey) != ERROR_SUCCESS )
		{
			logToFile(
				"Couldn't open System Registry key '%s\\%s'",
				aRootKeyName.c_str(),
				aSearchStartPath.c_str());
			RegCloseKey(aSearchStartHKey);
			mDoneSearching = true;
			return;
		}

		// Can check for the value right away if don't need wildcard search
		if( !aDataSource.usesWildcards )
		{
			aDataSource.pathToRead =
				widen(getFileName(aDataSource.pathPattern));
			DWORD aDataSize = 0;
			RegGetValue(aSearchStartHKey, NULL,
				aDataSource.pathToRead.c_str(),
				RRF_RT_REG_BINARY, NULL, NULL, &aDataSize);
			if( aDataSize == 0 )
			{
				logToFile(
					"Failed to read registry value '%ls' in '%s\\%s'",
					aDataSource.pathToRead.c_str(),
					aRootKeyName.c_str(),
					aSearchStartPath.c_str());
				RegCloseKey(aSearchStartHKey);
				mDoneSearching = true;
				return;
			}

			mValueBuf.reserve(aDataSize + kTimestampSuffixSize);
			mValueBuf.resize(aDataSize);
			if( RegGetValue(aSearchStartHKey, NULL,
					aDataSource.pathToRead.c_str(),
					RRF_RT_REG_BINARY, NULL,
					&mValueBuf[0], &aDataSize)
						!= ERROR_SUCCESS)
			{
				logToFile(
					"Failed to read registry value '%ls' in '%s\\%s' "
					"(does not exist yet? wrong format?)",
					aDataSource.pathToRead.c_str(),
					aRootKeyName.c_str(),
					aSearchStartPath.c_str());
				RegCloseKey(aSearchStartHKey);
				mDoneSearching = true;
				return;
			}

			FILETIME aModTime = extractTimestamp(
				mValueBuf,
				aSearchStartHKey,
				aDataSource.pathToRead);
			if( CompareFileTime(&aModTime, &aDataSource.lastModTime) > 0 )
			{
				if( aDataSource.lastModTime.dwHighDateTime ||
					aDataSource.lastModTime.dwLowDateTime )
				{
					syncDebugPrint(
						"Detected change in registry key value name %ls\n",
						aDataSource.pathToRead.c_str());
				}
				aDataSource.lastModTime = aModTime;
				swap(aDataSource.dataCache, mValueBuf);
				mFoundSourceToRead = true;
			}
			mDoneSearching = true;
			RegCloseKey(aSearchStartHKey);
			return;
		}

		// Set up multi-value search with wildcard
		mRootPath += '\\';
		mKeyStack.reserve(kMaxSubkeyStackSize);
		mKeyStack.resize(1);
		mKeyStack[0].hKey = aSearchStartHKey;
		mPathPattern = widen(aDataSource.pathPattern)
			.substr(mRootPath.size() + aRootKeyName.size() + 1);
	}

	virtual ~SystemRegistryValueFinder()
	{
		for(int i = 0, end = intSize(mKeyStack.size()); i < end; ++i)
		{
			if( mKeyStack[i].hKey )
				RegCloseKey(mKeyStack[i].hKey);
		}
	}

	virtual void checkNextLocation()
	{
		if( done() )
			return;

		DataSource& aDataSource = sDataSources[mDataSourceID];
		mEntriesCheckedThisUpdate = 0;

		while(!mKeyStack.empty())
		{
			SubkeyState& aKeyState = mKeyStack.back();

			if( !aKeyState.traverseSubKeysStarted )
			{
				// Allocate name buffer
				DWORD aMaxValueNameLen, aMaxSubkeyNameLen;
				if( RegQueryInfoKey(
						aKeyState.hKey, NULL, NULL, NULL, NULL,
						&aMaxSubkeyNameLen, NULL, NULL,
						&aMaxValueNameLen, NULL, NULL, NULL)
							!= ERROR_SUCCESS )
				{
					RegCloseKey(aKeyState.hKey);
					mKeyStack.pop_back();
					continue;
				}
				aKeyState.maxNameLen =
					max(aMaxSubkeyNameLen, aMaxValueNameLen)+1;
				aKeyState.traverseSubKeysStarted = true;
			}

			DWORD aNameSize = aKeyState.maxNameLen;
			mNameBuf.resize(aNameSize);
			if( !aKeyState.traverseValuesStarted )
			{
				FILETIME aModTime = FILETIME();
				if( RegEnumKeyExW(
						aKeyState.hKey, aKeyState.index++,
						&mNameBuf[0], &aNameSize,
						NULL, NULL, NULL,
						&aModTime) != ERROR_SUCCESS )
				{
					// Must be done checking for sub-keys
					aKeyState.traverseValuesStarted = true;
					aKeyState.index = 0;
					continue;
				}

				if( mKeyStack.size() < kMaxSubkeyStackSize &&
					CompareFileTime(&aModTime, &aDataSource.lastModTime) > 0 )
				{
					SubkeyState aSubkeyState = SubkeyState();
					if( RegOpenKeyEx(aKeyState.hKey, &mNameBuf[0], 0,
							KEY_READ | KEY_WRITE,
							&aSubkeyState.hKey) == ERROR_SUCCESS )
					{
						aSubkeyState.path =
							aKeyState.path + &mNameBuf[0] + L'\\';
						// Warning: invalidates current aKeyState reference!
						mKeyStack.push_back(aSubkeyState);
					}
				}
			}

			if( aKeyState.traverseValuesStarted )
			{
				DWORD aType;
				DWORD aDataSize = 0;
				if( RegEnumValue(
						aKeyState.hKey, aKeyState.index++,
						&mNameBuf[0], &aNameSize,
						NULL, &aType, NULL, &aDataSize) != ERROR_SUCCESS )
				{
					// Must be done checking for values
					RegCloseKey(aKeyState.hKey);
					mKeyStack.pop_back();
					// Finishing a key is enough work for one update
					return;
				}

				if( aType == REG_BINARY && aDataSize > 0 )
				{
					const std::wstring& aValuePath =
						aKeyState.path + &mNameBuf[0];
					const std::wstring& aMatchStr =
						wildcardMatch(aValuePath.c_str(), mPathPattern.c_str());
					if( !aMatchStr.empty() )
					{
						mValueBuf.reserve(
							aDataSize + kTimestampSuffixSize);
						mValueBuf.resize(aDataSize);
						if( RegGetValue(aKeyState.hKey, NULL,
								&mNameBuf[0],
								RRF_RT_REG_BINARY, NULL,
								&mValueBuf[0],
								&aDataSize) == ERROR_SUCCESS )
						{
							const FILETIME aModTime = extractTimestamp(
								mValueBuf, aKeyState.hKey, &mNameBuf[0]);
							compareBestSource(
								&mNameBuf[0], aMatchStr, aModTime);
							// Finding a match is enough work for one update
							return;
						}
					}
				}
			}

			// Avoid long stalls from large keys
			if( ++mEntriesCheckedThisUpdate >= kMaxEntriesCheckedPerUpdate )
				return;
		}

		// Once mKeyStack is empty we are done searching
		finalizeBestSourceFound();
	}

private:
	static const int kMaxSubkeyStackSize = 8;
	static const int kMaxEntriesCheckedPerUpdate = 64;
	static const int kTimestampMarkerSize = 4;
	static const int kTimestampSize = sizeof(ULONGLONG);
	static const int kTimestampSuffixSize =
		kTimestampMarkerSize + kTimestampSize + 1 /* traling null */;

	const char* timeStampMarker() const
	{
		return "_TS_";
	}

	// Individual registry key values don't have last-modified time stamps
	// (just the keys as a whole) so instead we add timestamps ourselves by
	// hiding them after the terminating null character of the text-as-binary
	// values being read. If a value has no timestamp, it's assumed that value
	// was written out by the game (stripping out our timestamp in the process)
	// and thus is treated as being newly modified (current system time).
	FILETIME extractTimestamp(
		std::vector<BYTE> theData,
		HKEY theRegKey,
		const std::wstring& theRegValName)
	{
		if( theData.size() <= 1 + kTimestampSuffixSize )
		{
			appendTimestamp(theData, theRegKey, theRegValName);
			return extractTimestamp(theData, theRegKey, theRegValName);
		}

		const int kTimestampOffset = intSize(theData.size()) -
			(1 + kTimestampSize);
		const int kMarkerOffset = intSize(theData.size()) -
			(1 + kTimestampSize + kTimestampMarkerSize);

		if( theData.back() != '\0' || theData[kMarkerOffset - 1] != '\0')
		{
			appendTimestamp(theData, theRegKey, theRegValName);
			return extractTimestamp(theData, theRegKey, theRegValName);
		}

		if( memcmp(&theData[kMarkerOffset],
				timeStampMarker(), kTimestampMarkerSize) != 0 )
		{
			appendTimestamp(theData, theRegKey, theRegValName);
			return extractTimestamp(theData, theRegKey, theRegValName);
		}

		ULARGE_INTEGER timeUnion;
		memcpy(
			&timeUnion.QuadPart,
			&theData[kTimestampOffset],
			kTimestampSize);
		FILETIME result;
		result.dwLowDateTime = timeUnion.LowPart;
		result.dwHighDateTime = timeUnion.HighPart;
		return result;
	}

	void appendTimestamp(
		std::vector<BYTE>& theData,
		HKEY theRegKey,
		const std::wstring& theRegValName)
	{
		if( theData.empty() || theData.back() != '\0' )
			theData.push_back('\0');

		theData.insert(theData.end(), timeStampMarker(),
			timeStampMarker() + kTimestampMarkerSize);

		ULARGE_INTEGER aTimestamp;
		aTimestamp.LowPart = sLastChangeDetectedTime.dwLowDateTime;
		aTimestamp.HighPart = sLastChangeDetectedTime.dwHighDateTime;
		const BYTE* aTimestampPtr = (const BYTE*)(&aTimestamp.QuadPart);
		theData.insert(theData.end(), aTimestampPtr,
			aTimestampPtr + kTimestampSize);
		theData.push_back('\0');

		RegSetValueEx(theRegKey, theRegValName.c_str(), NULL,
			REG_BINARY, &theData[0],
			DWORD(theData.size()));

		// Writing to the registry will cause our own notification to fire.
		// Setting this informs other logic of this happening, in an effort
		// to prevent duplicating work every time the time stamp is lost
		mSearchTriggeredChange = true;
	}

	struct ZERO_INIT(SubkeyState)
	{
		HKEY hKey;
		std::wstring path;
		DWORD index;
		DWORD maxNameLen;
		bool traverseSubKeysStarted;
		bool traverseSubKeysEnded;
		bool traverseValuesStarted;
	};

	std::vector<SubkeyState> mKeyStack;
	std::vector<WCHAR> mNameBuf;
	std::wstring mPathPattern;
	std::wstring mRootPath;
	int mEntriesCheckedThisUpdate;
};


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static bool isRegistryPath(const std::string thePath)
{
	return
		thePath.size() > 4 &&
		::toupper(thePath[0]) == 'H' &&
		::toupper(thePath[1]) == 'K';
}


static bool monitoredPathExists(const std::wstring& thePath)
{
	if( thePath.size() > 4 &&
		::towupper(thePath[0]) == L'H' &&
		::towupper(thePath[1]) == L'K' )
	{// Assume a system registry key path starting with 'HKEY_'
		const std::wstring& aRootKeyName =
			thePath.substr(0, thePath.find(L'\\'));
		HKEY aRootKey = getRootKeyHandle(narrow(aRootKeyName));
		if( !aRootKey )
			return false;
		std::wstring aKeyPath = thePath.substr(aRootKeyName.size() + 1);

		HKEY hTemp;
		LONG res = RegOpenKeyExW(
			aRootKey, aKeyPath.c_str(),
			0, KEY_READ, &hTemp);

		if( res == ERROR_SUCCESS )
		{
			RegCloseKey(hTemp);
			return true;
		}

		return false;
	}

	return isValidFolderPath(thePath);
}


static std::string normalizedPath(std::string thePath)
{
	if( thePath.size() >= 4 )
	{
		// Remove quotes
		while(
			thePath.size() >= 4 &&
			thePath[0] == '\"' &&
			thePath[thePath.size()-1] == '\"')
		{
			thePath = thePath.substr(1, thePath.size() - 2);
		}

		if( isRegistryPath(thePath) )
		{// Assume a registry path rather than file path
			thePath = replaceChar(thePath, '/', '\\');
			const std::string& theRootStr = breakOffNextItem(thePath, '\\');
			thePath = upper(theRootStr) + "\\" + thePath;
			// Expand known shorthands for root key
			if( thePath.compare(0, 4, "HKLM") == 0 )
				thePath.replace(0, 4, "HKEY_LOCAL_MACHINE");
			else if( thePath.compare(0, 4, "HKCU") == 0 )
				thePath.replace(0, 4, "HKEY_CURRENT_USER");
			else if( thePath.compare(0, 4, "HKCR") == 0 )
				thePath.replace(0, 4, "HKEY_CLASSES_ROOT");
			else if( thePath.compare(0, 4, "HKCC") == 0 )
				thePath.replace(0, 4, "HKEY_CURRENT_CONFIG");
			else if( thePath.compare(0, 5, "HKU") == 0 )
				thePath.replace(0, 3, "HKEY_USERS");
			return thePath;
		}
	}

	thePath = toAbsolutePath(thePath);
	return thePath;
}


static bool isPathAncestorOf(const std::wstring& theAncestor,
							 const std::wstring& theDescendant)
{
	if( theAncestor.empty() || theDescendant.empty() )
		return false;
	if( theDescendant.size() < theAncestor.size() )
		return false;
	if( _wcsnicmp(theAncestor.c_str(),
			theDescendant.c_str(),
			theAncestor.size()) != 0 )
	{ return false; }
	if( theDescendant.size() == theAncestor.size() )
		return true;
	return
		theAncestor[theAncestor.size()-1] == L'\\' ||
		theDescendant[theAncestor.size()] == L'\\';
}


static bool setFetchValueFromDataSource(
	TargetConfigSyncBuilder& theBuilder,
	const std::string& theSubstituteStr,
	const int theDestValueSetID,
	const EValueSetType theValueSetType,
	const EValueSetSubType theDestValueSetSubType)
{
	// Generate path from format for given sub-type
	std::string aConfigDataPath;
	aConfigDataPath.reserve(256);
	if( !theBuilder.valueFormatStrings[theDestValueSetSubType].empty() )
	{
		// Parse format string for string replacement tags
		std::vector<std::string> aReplacementStrings;
		aReplacementStrings.reserve(4);
		{// Extract individual parameters
			std::string aStr = theSubstituteStr;
			while(!aStr.empty())
				aReplacementStrings.push_back(breakOffNextItem(aStr));
		}
		aConfigDataPath =
			theBuilder.valueFormatStrings[theDestValueSetSubType];
		std::pair<std::string::size_type, std::string::size_type> aTagCoords =
			findStringTag(aConfigDataPath);
		while(aTagCoords.first != std::string::npos)
		{
			const std::string& aTag = condense(
				aConfigDataPath.substr(
					aTagCoords.first + 1, aTagCoords.second - 2));
			if( isAnInteger(aTag) || aTag == "NAME" )
			{
				// <name> can be used the same as <1>
				const u32 aTagNum = max(stringToInt(aTag), 1) - 1;
				if( aTagNum < aReplacementStrings.size() )
				{
					aConfigDataPath.replace(
						aTagCoords.first,
						aTagCoords.second,
						aReplacementStrings[aTagNum]);
				}
				else
				{
					logError(
						"No parameter for tag <%s> in <%s> for '%s = %s'",
						theSubstituteStr.c_str(),
						aTag.c_str(),
						kValueFormatStringKeys[theDestValueSetSubType],
						theBuilder.valueFormatStrings[
							theDestValueSetSubType].c_str());
					return false;
				}
			}
			else
			{
				logError(
					"Unrecognized tag %s '%s = %s'",
					aConfigDataPath.substr(
					aTagCoords.first, aTagCoords.second).c_str(),
					kValueFormatStringKeys[theDestValueSetSubType],
					theBuilder.valueFormatStrings[
						theDestValueSetSubType].c_str());
				// Prevent spamming this error
				theBuilder.valueFormatStrings[
					theDestValueSetSubType].clear();
				return false;
			}
			aTagCoords = findStringTag(aConfigDataPath, aTagCoords.first + 1);
		}
	}
	else if( theValueSetType != eValueSetType_Single )
	{
		// For single values in a value set, if no format string is
		// specified, this particular value should be left as 0.
		return true;
	}
	else
	{
		// Must be a single-value direct path
		aConfigDataPath = theSubstituteStr;
	}

	// Extract data source key from beginning of path up to first '.'
	const std::string& aDataSourceKey =
		breakOffItemBeforeChar(aConfigDataPath, '.');
	if( aDataSourceKey.empty() )
	{
		logError("Missing config file ID for path '%s' in sync property '%s'",
			aConfigDataPath.c_str(), theBuilder.debugString.c_str());
		return false;
	}
	int* const aValueLinkMapID =
		theBuilder.nameToLinkMapID.find(aDataSourceKey);
	if( !aValueLinkMapID )
	{
		// It may be intentional that syncing was disabled by not defining the
		// data path to sync from, so report this only in the log file
		logToFile("Config file ID '%s' referenced in '%s' not found",
			aDataSourceKey.c_str(), theBuilder.debugString.c_str());
		return false;
	}

	// Set this key's path as a value to look for when parsing this data
	ValueLink aDestValue;
	aDestValue.setIdx = dropTo<u16>(theDestValueSetID);
	aDestValue.valueIdx = dropTo<u16>(
		sValueSets[theDestValueSetID] +
		theDestValueSetSubType -
		kValueSetFirstIdx[theValueSetType]);
	DBG_ASSERT(size_t(*aValueLinkMapID) < theBuilder.valueLinkMaps.size());
	theBuilder.valueLinkMaps[*aValueLinkMapID].setValue(
		aConfigDataPath, aDestValue);
	return true;
}


static bool setConfigValueLinks(
	TargetConfigSyncBuilder& theBuilder,
	SyncVariable& theSyncVar,
	const std::string& theConfigFileValueName,
	EValueSetType theValueSetType)
{
	// Find or create value set for the value name given
	theSyncVar.valueSetID =
		theBuilder.valueSetNameToIDMap.findOrAdd(
			theConfigFileValueName,
			intSize(sValueSets.size()));
	if( theSyncVar.valueSetID >= intSize(sValueSets.size()) )
	{
		sValueSets.push_back(dropTo<u16>(sValues.size()));
		sValues.resize(sValues.size() +
			kValueSetLastIdx[theValueSetType] -
			kValueSetFirstIdx[theValueSetType] + 1,
			std::numeric_limits<double>::quiet_NaN());
	}
	// Request fetch all related values for given function & value set
	bool isValidResult = true;
	#define fetchVal(x) \
		isValidResult = isValidResult && \
			setFetchValueFromDataSource( \
				theBuilder, theConfigFileValueName, \
				theSyncVar.valueSetID, theValueSetType, x)
	switch(theSyncVar.funcType)
	{
	case eValueFunc_Base:
		fetchVal(eValueSetSubType_Base);
		break;
	case eValueFunc_PosX:
	case eValueFunc_Left:
	case eValueFunc_CX:
	case eValueFunc_Right:
		fetchVal(eValueSetSubType_PosX);
		fetchVal(eValueSetSubType_AlignX);
		fetchVal(eValueSetSubType_AnchorTypeA);
		fetchVal(eValueSetSubType_PivotX);
		fetchVal(eValueSetSubType_SizeX);
		break;
	case eValueFunc_PosY:
	case eValueFunc_Top:
	case eValueFunc_CY:
	case eValueFunc_Bottom:
		fetchVal(eValueSetSubType_PosY);
		fetchVal(eValueSetSubType_AlignY);
		fetchVal(eValueSetSubType_AnchorTypeA);
		fetchVal(eValueSetSubType_PivotY);
		fetchVal(eValueSetSubType_SizeY);
		break;
	case eValueFunc_Width:
		fetchVal(eValueSetSubType_SizeX);
		break;
	case eValueFunc_Height:
		fetchVal(eValueSetSubType_SizeY);
		break;
	case eValueFunc_AlignX:
		fetchVal(eValueSetSubType_AlignX);
		fetchVal(eValueSetSubType_AnchorTypeA);
		break;
	case eValueFunc_AlignY:
		fetchVal(eValueSetSubType_AlignY);
		fetchVal(eValueSetSubType_AnchorTypeA);
		break;
	case eValueFunc_Scale:
		fetchVal(eValueSetSubType_Scale);
		break;
	}
	#undef fetchVal

	return isValidResult;
}


static EValueSetType funcToValueSetType(EValueFunction theFunc)
{
	return theFunc == eValueFunc_Base
		? eValueSetType_Single
		: eValueSetType_UIWindow;
}


static SyncVariable parseSyncVariableFunc(
	TargetConfigSyncBuilder& theBuilder,
	std::string theDesc)
{
	SyncVariable result;
	result.valueSetID = -1;
	const std::string& aFuncName = breakOffItemBeforeChar(theDesc, ':');
	result.funcType = valueFuncNameToID(aFuncName);
	if( result.funcType == eValueFunc_Num )
	{
		logError("Unknown function name '%s' for target sync variable '%s'",
			aFuncName.c_str(), theBuilder.debugString.c_str());
		return result;
	}
	EValueSetType aValueSetType = funcToValueSetType(result.funcType);
	// Set links from config data sources back to this variable
	if( !setConfigValueLinks(theBuilder, result, theDesc, aValueSetType) )
	{
		result.valueSetID = -1;
		return result;
	}
	return result;
}


static double anchorTypeToSubTypeValue(
	const double* theValArray,
	EValueSetSubType theSubType)
{
	// Anchor Type A - used by AOA
	// This type goes clockwise from TL in a spiral ending at center,
	// and it affects both alignment and pivot point
	if( !_isnan(theValArray[eValueSetSubType_AnchorTypeA]) )
	{
		//	  L-T  C-T  R-T  R-C  R-B  C-B  L-B  L-C  C-C
		static const double kTypeAAlignX[9] =
			{ 0.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.0, 0.0, 0.5 };
		static const double kTypeAAlignY[9] =
			{ 0.0, 0.0, 0.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.5 };
		const u32 anAnchorType =
			u32(int(theValArray[eValueSetSubType_AnchorTypeA]));
		if( anAnchorType < 9 )
		{
			switch(theSubType)
			{
			case eValueSetSubType_AlignX:
			case eValueSetSubType_PivotX:
				return kTypeAAlignX[anAnchorType];
			case eValueSetSubType_AlignY:
			case eValueSetSubType_PivotY:
				return kTypeAAlignY[anAnchorType];
			}
		}
		// Invalid value for this anchor type enum
		return 0;
	}

	// No anchor type values were read in
	return 0;
}


static double getSubTypeValue(
	const double* theValArray,
	EValueSetSubType theSubType)
{
	double result = theValArray[theSubType];
	const bool wasFound = !_isnan(result);
	if( !wasFound ) result = 0;

	switch(theSubType)
	{
	case eValueSetSubType_PosX:
	case eValueSetSubType_PosY:
		if( sInvertAxis[theSubType] )
			result = -result;
		break;
	case eValueSetSubType_AlignX:
	case eValueSetSubType_AlignY:
		if( !wasFound )
			result = anchorTypeToSubTypeValue(theValArray, theSubType);
		result = clamp(result, 0, 1.0);
		if( sInvertAxis[theSubType] )
			result = 1.0 - result;
		break;
	case eValueSetSubType_PivotX:
	case eValueSetSubType_PivotY:
		// For these what we really want is the offset needed to compensate
		// for the pivot's effect rather than the actual pivot value itself.
		if( !wasFound )
			result = anchorTypeToSubTypeValue(theValArray, theSubType);
		result = clamp(result, 0, 1.0);
		if( sInvertAxis[theSubType] )
			result = 1.0 - result;
		if( result != 0 )
		{
			result *=
				getSubTypeValue(
					theValArray,
					theSubType == eValueSetSubType_PivotX
						? eValueSetSubType_SizeX
						: eValueSetSubType_SizeY);
		}
		break;
	case eValueSetSubType_SizeX:
	case eValueSetSubType_SizeY:
		result *= getSubTypeValue(theValArray, eValueSetSubType_Scale);
		result = floor(result + 0.5);
		if( sInvertAxis[theSubType] )
			result = -result;
		result = max(0.0, result);
		break;
	case eValueSetSubType_Scale:
		if( result <= 0 )
			result = 1.0;
		break;
	}
	return result;
}


static std::string getValueString(
	EValueFunction theFunction,
	int theValueSet)
{
	// Get an array pointer to the value that EValueSetSubType(0) would return
	// (note this means v could end up pointing before the beginning of the
	// array, but rest of logic should not be dereferencing v[0] in that case!)
	double* v = &sValues[sValueSets[theValueSet]];
	v -= kValueSetFirstIdx[funcToValueSetType(theFunction)];
	std::string result;
	result.reserve(32);
	switch(theFunction)
	{
	case eValueFunc_Base:
		result = toString(getSubTypeValue(v, eValueSetSubType_Base));
		break;
	case eValueFunc_PosX:
		{
			const double anAlign = getSubTypeValue(v, eValueSetSubType_AlignX);
			if( anAlign < 0.4 )
				result = getValueString(eValueFunc_Left, theValueSet);
			else if( anAlign > 0.6 )
				result = getValueString(eValueFunc_Right, theValueSet);
			else
				result = getValueString(eValueFunc_CX, theValueSet);
		}
		break;
	case eValueFunc_PosY:
		{
			const double anAlign = getSubTypeValue(v, eValueSetSubType_AlignY);
			if( anAlign < 0.4 )
				result = getValueString(eValueFunc_Top, theValueSet);
			else if( anAlign > 0.6 )
				result = getValueString(eValueFunc_Bottom, theValueSet);
			else
				result = getValueString(eValueFunc_CY, theValueSet);
		}
		break;
	case eValueFunc_Left:
		result = getValueString(eValueFunc_AlignX, theValueSet);
		{
			const int anOffset = static_cast<int>(
				getSubTypeValue(v, eValueSetSubType_PosX) -
				getSubTypeValue(v, eValueSetSubType_PivotX));
			if( anOffset >= 0 ) result += "+";
			result += toString(anOffset);
		}
		break;
	case eValueFunc_Top:
		result = getValueString(eValueFunc_AlignY, theValueSet);
		{
			const int anOffset = static_cast<int>(
				getSubTypeValue(v, eValueSetSubType_PosY) -
				getSubTypeValue(v, eValueSetSubType_PivotY));
			if( anOffset >= 0 ) result += "+";
			result += toString(anOffset);
		}
		break;
	case eValueFunc_CX:
		result = getValueString(eValueFunc_AlignX, theValueSet);
		{
			const int anOffset = static_cast<int>(
				getSubTypeValue(v, eValueSetSubType_PosX) -
				getSubTypeValue(v, eValueSetSubType_PivotX) +
				getSubTypeValue(v, eValueSetSubType_SizeX) * 0.5);
			if( anOffset >= 0 ) result += "+";
			result += toString(anOffset);
		}
		break;
	case eValueFunc_CY:
		result = getValueString(eValueFunc_AlignY, theValueSet);
		{
			const int anOffset = static_cast<int>(
				getSubTypeValue(v, eValueSetSubType_PosY) -
				getSubTypeValue(v, eValueSetSubType_PivotY) +
				getSubTypeValue(v, eValueSetSubType_SizeY) * 0.5);
			if( anOffset >= 0 ) result += "+";
			result += toString(anOffset);
		}
		break;
	case eValueFunc_Right:
		result = getValueString(eValueFunc_AlignX, theValueSet);
		{
			const int anOffset = static_cast<int>(
				getSubTypeValue(v, eValueSetSubType_PosX) -
				getSubTypeValue(v, eValueSetSubType_PivotX) +
				getSubTypeValue(v, eValueSetSubType_SizeX));
			if( anOffset >= 0 ) result += "+";
			result += toString(anOffset);
		}
		break;
	case eValueFunc_Bottom:
		result = getValueString(eValueFunc_AlignY, theValueSet);
		{
			const int anOffset = static_cast<int>(
				getSubTypeValue(v, eValueSetSubType_PosY) -
				getSubTypeValue(v, eValueSetSubType_PivotY) +
				getSubTypeValue(v, eValueSetSubType_SizeY));
			if( anOffset >= 0 ) result += "+";
			result += toString(anOffset);
		}
		break;
	case eValueFunc_Width:
		result = toString(getSubTypeValue(v, eValueSetSubType_SizeX));
		break;
	case eValueFunc_Height:
		result = toString(getSubTypeValue(v, eValueSetSubType_SizeY));
		break;
	case eValueFunc_AlignX:
		{
			const double anAlign = getSubTypeValue(v, eValueSetSubType_AlignX);
			if( anAlign <= 0 )
				result = "L";
			else if( anAlign >= 1.0 )
				result = "R";
			else if( anAlign == 0.5 )
				result = "CX";
			else
				result = toString(anAlign * 100.0) + "%";
		}
		break;
	case eValueFunc_AlignY:
		{
			const double anAlign = getSubTypeValue(v, eValueSetSubType_AlignY);
			if( anAlign <= 0 )
				result = "T";
			else if( anAlign >= 1.0 )
				result = "B";
			else if( anAlign == 0.5 )
				result = "CY";
			else
				result = toString(anAlign * 100.0) + "%";
		}
		break;
	case eValueFunc_Scale:
		result = toString(getSubTypeValue(v, eValueSetSubType_Scale));
		break;
	default:
		DBG_ASSERT(false && "Invalid EValueFunction");
		result = "0";
	}
	return result;
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void load()
{
	if( sInitialized || sPaused )
		cleanup();

	GetSystemTimeAsFileTime(&sLastChangeDetectedTime);
	TargetConfigSyncBuilder aBuilder;
	sPromptForWildcardFiles = Profile::getBool(
		kTargetConfigSettingsSectionName,
		kPromptForFilesPropertyName);
	sLastWildcardFileSelected = widen(Profile::getStr(
		kTargetConfigSettingsSectionName,
		kLastFileSelectedPropertyName));
	sLastTimeWildcardFileSelected = stringToFileTime(Profile::getStr(
		kTargetConfigSettingsSectionName,
		kLastTimeFileSelectedPropertyName));

	{// Fetch target config paths potentially containing sync properties
		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(kTargetConfigFilesSectionName);
		aBuilder.nameToLinkMapID.reserve(aPropMap->size());
		aBuilder.pathToLinkMapID.reserve(aPropMap->size());
		aBuilder.valueLinkMaps.reserve(aPropMap->size());
		for(int i = 0; i < aPropMap->size(); ++i)
		{
			const int aLinkMapID = aBuilder.pathToLinkMapID.findOrAdd(
				normalizedPath(aPropMap->vals()[i].str),
				intSize(aBuilder.valueLinkMaps.size()));
			if( aLinkMapID >= intSize(aBuilder.valueLinkMaps.size()) )
				aBuilder.valueLinkMaps.push_back(ValueLinkMap());
			aBuilder.nameToLinkMapID.setValue(
				aPropMap->keys()[i], aLinkMapID);
		}
	}

	// Fetch value path formating data
	for(int i = 0; i < eValueSetSubType_Num; ++i)
	{
		aBuilder.valueFormatStrings[i] = Profile::getStr(
			kValueFormatStrSectionName,
			kValueFormatStringKeys[i]);
		sInvertAxis[i] = Profile::getBool(
			kValueFormatStrSectionName,
			std::string(kValueFormatInvertPrefix) +
			kValueFormatStringKeys[i]);
	}

	{// Fetch sync variable values to read from the data sources
		Profile::PropertyMapPtr aPropMap =
			Profile::getSectionProperties(kTargetConfigVarsSectionName);
		for(int i = 0; i < aPropMap->size(); ++i)
		{
			aBuilder.debugString = aPropMap->keys()[i];
			aBuilder.debugString += " = ";
			aBuilder.debugString += aPropMap->vals()[i].str;
			SyncVariable aSyncVar = parseSyncVariableFunc(
				aBuilder,
				aPropMap->vals()[i].str);
			if( aSyncVar.valueSetID < 0 )
				continue;
			aSyncVar.variableID =
				Profile::variableNameToID(aPropMap->keys()[i]);
			if( aSyncVar.variableID < 0 )
			{
				logError("Unknown variable name '%s' in %s.",
					aPropMap->keys()[i].c_str(),
					aBuilder.debugString.c_str());
				continue;
			}
			sVariables.push_back(aSyncVar);
		}
	}
	aBuilder.nameToLinkMapID.clear();

	// Prepare data sources for reading and monitoring for changes
	struct MonitoredFolder
	{
		std::wstring path;
		std::vector<u16> dataSourceIDs;
		EDataSourceType type;
		bool recursiveCheckNeeded;
	};
	std::vector<MonitoredFolder> aMonitoredFolderSet;
	for(int i = 0; i < aBuilder.pathToLinkMapID.size(); ++i)
	{
		const int aValueLinkMapIdx =
			aBuilder.pathToLinkMapID.values()[i];
		DBG_ASSERT(size_t(aValueLinkMapIdx) < aBuilder.valueLinkMaps.size());
		ValueLinkMap& aValueLinkMap =
			aBuilder.valueLinkMaps[aValueLinkMapIdx];
		if( aValueLinkMap.empty() )
			continue;
		const std::string& aSourcePath = aBuilder.pathToLinkMapID.keys()[i];
		const int aSourceID = intSize(sDataSources.size());
		sDataSources.push_back(DataSource());
		sDataSources.back().values = aValueLinkMap;
		sDataSources.back().pathPattern = aSourcePath;
		if( isRegistryPath(aSourcePath) )
			sDataSources.back().type = eDataSourceType_RegVal;
		else
			sDataSources.back().type = eDataSourceType_File;
		sDataSources.back().format = eConfigDataFormat_JSON; // TODO properly
		std::string::size_type aFirstWildcardPos =
			aSourcePath.find('*');
		sDataSources.back().usesWildcards =
			aFirstWildcardPos != std::string::npos;
		aMonitoredFolderSet.push_back(MonitoredFolder());
		aMonitoredFolderSet.back().type = sDataSources.back().type;
		aMonitoredFolderSet.back().path =
			widen(getFileDir(aSourcePath.substr(0, aFirstWildcardPos), true));
		aMonitoredFolderSet.back().dataSourceIDs.push_back(
			dropTo<u16>(aSourceID));
		aMonitoredFolderSet.back().recursiveCheckNeeded =
			sDataSources.back().usesWildcards;
	}
	aBuilder.pathToLinkMapID.clear();
	aBuilder.valueLinkMaps.clear();
	sValueSetsChanged.clearAndResize(sValueSets.size());
	sDataSourcesToCheck.clearAndResize(sDataSources.size());
	sDataSourcesToRead.clearAndResize(sDataSources.size());
	sDataSourcesToReCheck.clearAndResize(sDataSources.size());

	// Merge duplicate monitored folders
	for(int i = 0; i < intSize(aMonitoredFolderSet.size())-1; ++i)
	{
		for(int j = i+1; j < intSize(aMonitoredFolderSet.size());)
		{
			if( isPathAncestorOf(aMonitoredFolderSet[i].path,
					aMonitoredFolderSet[j].path) )
			{
				aMonitoredFolderSet[i].dataSourceIDs.insert(
					aMonitoredFolderSet[i].dataSourceIDs.end(),
					aMonitoredFolderSet[j].dataSourceIDs.begin(),
					aMonitoredFolderSet[j].dataSourceIDs.end());
				aMonitoredFolderSet[i].recursiveCheckNeeded = true;
				aMonitoredFolderSet.erase(aMonitoredFolderSet.begin() + j);
			}
			else if( isPathAncestorOf(aMonitoredFolderSet[j].path,
						aMonitoredFolderSet[i].path) )
			{
				aMonitoredFolderSet[j].dataSourceIDs.insert(
					aMonitoredFolderSet[j].dataSourceIDs.end(),
					aMonitoredFolderSet[i].dataSourceIDs.begin(),
					aMonitoredFolderSet[i].dataSourceIDs.end());
				aMonitoredFolderSet[j].recursiveCheckNeeded = true;
				aMonitoredFolderSet.erase(aMonitoredFolderSet.begin() + i);
				--i;
				break;
			}
			else
			{
				++j;
			}
		}
	}

	// Report and remove missing monitored folders, and prepare to load
	// data sources from any that do exist
	for(int i = 0; i < intSize(aMonitoredFolderSet.size()); ++i)
	{
		if( !monitoredPathExists(aMonitoredFolderSet[i].path) )
		{
			logToFile("Config data base path '%ls' does not exist (yet?)",
					aMonitoredFolderSet[i].path.c_str());
			aMonitoredFolderSet.erase(aMonitoredFolderSet.begin() + i);
			--i;
			continue;
		}
		for(int j = 0;
			j < intSize(aMonitoredFolderSet[i].dataSourceIDs.size());
			++j)
		{
			sDataSourcesToCheck.set(
				aMonitoredFolderSet[i].dataSourceIDs[j]);
		}
	}

	// Load initial values and log timestamps
	while(!sPaused && !gShutdown &&
		  (sDataSourcesToCheck.any() ||
		   sDataSourcesToRead.any() ||
		   sValueSetsChanged.any() ||
		   sFinder || sReader || sParser))
	{// Keep updating until done for this initial pass
		update();
	}

	// Begin monitoring for folder and registry key changes
	for(int i = 0, end = intSize(aMonitoredFolderSet.size()); i < end; ++i)
	{
		if( aMonitoredFolderSet[i].type == eDataSourceType_File )
		{
			sFolders.push_back(ConfigFileFolder());
			sFolders.back().dataSourceIDs =
				aMonitoredFolderSet[i].dataSourceIDs;
			sFolders.back().recursiveCheckNeeded =
				aMonitoredFolderSet[i].recursiveCheckNeeded;
			sFolders.back().hChangedSignal = FindFirstChangeNotification(
				aMonitoredFolderSet[i].path.c_str(),
				sFolders.back().recursiveCheckNeeded,
				FILE_NOTIFY_CHANGE_LAST_WRITE);
		}
		else if( aMonitoredFolderSet[i].type == eDataSourceType_RegVal )
		{
			sRegKeys.push_back(SystemRegistryKey());
			sRegKeys.back().dataSourceIDs =
				aMonitoredFolderSet[i].dataSourceIDs;
			sRegKeys.back().recursiveCheckNeeded =
				aMonitoredFolderSet[i].recursiveCheckNeeded;
			std::string aRegKeyPath = narrow(aMonitoredFolderSet[i].path);
			const HKEY aRootKey = getRootKeyHandle(
				breakOffNextItem(aRegKeyPath, '\\'));
			if( !aRootKey )
				continue;
			if( RegOpenKeyEx(aRootKey, widen(aRegKeyPath).c_str(), 0,
					KEY_NOTIFY, &sRegKeys.back().hKey) != ERROR_SUCCESS )
			{
				continue;
			}
			sRegKeys.back().hChangedSignal =
				CreateEvent(NULL, TRUE, FALSE, NULL);
			if( sRegKeys.back().hChangedSignal )
			{
				RegNotifyChangeKeyValue(
					sRegKeys.back().hKey,
					sRegKeys.back().recursiveCheckNeeded,
					REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
					sRegKeys.back().hChangedSignal, TRUE);
			}
		}
	}

	// Trim memory
	if( sDataSources.size() < sDataSources.capacity() )
		std::vector<DataSource>(sDataSources).swap(sDataSources);
	if( sFolders.size() < sFolders.capacity() )
		std::vector<ConfigFileFolder>(sFolders).swap(sFolders);
	if( sRegKeys.size() < sRegKeys.capacity() )
		std::vector<SystemRegistryKey>(sRegKeys).swap(sRegKeys);
	if( sVariables.size() < sVariables.capacity() )
		std::vector<SyncVariable>(sVariables).swap(sVariables);
	if( sValues.size() < sValues.capacity() )
		std::vector<double>(sValues).swap(sValues);
	if( sValueSets.size() < sValueSets.capacity() )
		std::vector<u16>(sValueSets).swap(sValueSets);

	sInitialized = true;
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.contains(kTargetConfigSettingsSectionName) ||
		theProfileMap.contains(kTargetConfigFilesSectionName) ||
		theProfileMap.contains(kTargetConfigVarsSectionName) ||
		theProfileMap.contains(kValueFormatStrSectionName) )
	{
		load();
	}
}


void cleanup()
{
	delete sReader; sReader = null;
	delete sParser; sParser = null;

	for(int i = 0, end = intSize(sFolders.size()); i < end; ++i)
		FindCloseChangeNotification(sFolders[i].hChangedSignal);
	sFolders.clear();

	for(int i = 0, end = intSize(sRegKeys.size()); i < end; ++i)
	{
		RegCloseKey(sRegKeys[i].hKey);
		CloseHandle(sRegKeys[i].hChangedSignal);
	}
	sRegKeys.clear();

	sDataSources.clear();
	sVariables.clear();
	sValues.clear();
	sValueSets.clear();
	sValueSetsChanged.clear();
	sDataSourcesToCheck.clear();
	sDataSourcesToRead.clear();
	sDataSourcesToReCheck.clear();
	sInitialized = false;
	sPaused = false;
}


void update()
{
	if( sPaused || gShutdown )
	{
		// Cancel any parsing in-progress
		delete sReader; sReader = null;
		delete sParser; sParser = null;
		delete sFinder; sFinder = null;
		return;
	}

	// Continue any active parsing already in progress
	if( sFinder )
	{
		sFinder->checkNextLocation();
		if( sFinder->searchTriggeredChange() && sInitialized )
		{
			// Change will trigger our own notification system,
			// so just continue after that happens
			sDataSourcesToReCheck.set(sFinder->dataSourceID());
			delete sFinder; sFinder = null;
		}
		else if( sFinder->done() )
		{
			sDataSourcesToCheck.reset(sFinder->dataSourceID());
			if( sFinder->foundSourceToRead() )
				sDataSourcesToRead.set(sFinder->dataSourceID());
			delete sFinder; sFinder = null;
		}
		return;
	}

	if( sParser )
	{
		DBG_ASSERT(sReader && sParser);
		sParser->parseNextChunk(sReader->readNextChunk());
		if( sReader->sourceWasBusy() )
		{
			// Try processing this source again later instead
			sDataSourcesToReCheck.set(sParser->dataSourceID());
			sDataSourcesToRead.reset(sParser->dataSourceID());
			sDataSources[sParser->dataSourceID()].dataCache.clear();
			delete sReader; sReader = null;
			delete sParser; sParser = null;
		}
		else if( sReader->done() || sParser->done() )
		{
			if( sReader->errorEncountered() )
			{
				syncDebugPrint("No values read - error encountered!\n");
			}
			else
			{
				sReader->reportResults();
				sParser->reportResults();
			}
			sDataSourcesToRead.reset(sParser->dataSourceID());
			sDataSources[sParser->dataSourceID()].dataCache.clear();
			delete sReader; sReader = null;
			delete sParser; sParser = null;
		}
		return;
	}

	if( sInitialized )
	{// Check for any folder or registry key changes after initial load
		for(int aFolderID = 0, aFoldersEnd = intSize(sFolders.size());
			aFolderID < aFoldersEnd; ++aFolderID)
		{
			ConfigFileFolder& aFolder = sFolders[aFolderID];
			if( WaitForSingleObject(aFolder.hChangedSignal, 0)
					== WAIT_OBJECT_0 )
			{
				// Re-arm the notification for next update
				FindNextChangeNotification(aFolder.hChangedSignal);

				// Note time of change detected
				GetSystemTimeAsFileTime(&sLastChangeDetectedTime);

				// Flag to check for changes to any related data sources
				for(int i = 0, end = intSize(aFolder.dataSourceIDs.size());
					i < end; ++i )
				{
					sDataSourcesToCheck.set(aFolder.dataSourceIDs[i]);
				}
			}
		}

		for(int aRegKeyID = 0, aRegKeyEnd = intSize(sRegKeys.size());
			aRegKeyID < aRegKeyEnd; ++aRegKeyID)
		{
			SystemRegistryKey& aRegKey = sRegKeys[aRegKeyID];
			if( WaitForSingleObject(aRegKey.hChangedSignal, 0)
					== WAIT_OBJECT_0)
			{
				// Rearm the notification for next update
				RegNotifyChangeKeyValue(
					aRegKey.hKey,
					aRegKey.recursiveCheckNeeded,
					REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
					aRegKey.hChangedSignal, TRUE);

				// Note time of change detected
				GetSystemTimeAsFileTime(&sLastChangeDetectedTime);

				// Flag to check for changes to any related data sources
				for(int i = 0, end = intSize(aRegKey.dataSourceIDs.size());
					i < end; ++i )
				{
					sDataSourcesToCheck.set(aRegKey.dataSourceIDs[i]);
				}
			}
		}
	}

	// Investigate any data sources that might have changed
	if( sDataSourcesToCheck.any() )
	{
		const int aDataSourceID = sDataSourcesToCheck.firstSetBit();
		DataSource& aDataSource = sDataSources[aDataSourceID];
		aDataSource.dataCache.clear();
		sDataSourcesToReCheck.reset(aDataSourceID);
		DBG_ASSERT(!sFinder);
		switch(aDataSource.type)
		{
		case eDataSourceType_File:
			sFinder = new ConfigFileFinder(aDataSourceID);
			break;
		case eDataSourceType_RegVal:
			sFinder = new SystemRegistryValueFinder(aDataSourceID);
			break;
		default:
			DBG_ASSERT(false && "Unknown config source data type");
		}
		// Search (for file with newer timestamp) will continue next update()
		return;
	}

	// Begin reading any new/changed data sources found
	if( sDataSourcesToRead.any() )
	{
		const int aDataSourceID = sDataSourcesToRead.firstSetBit();
		DataSource& aDataSource = sDataSources[aDataSourceID];
		DBG_ASSERT(!sParser && !sReader);
		switch(aDataSource.type)
		{
		case eDataSourceType_File:
			sReader = new ConfigFileReader(aDataSourceID);
			break;
		case eDataSourceType_RegVal:
			sReader = new SystemRegistryValueReader(aDataSourceID);
			break;
		default:
			DBG_ASSERT(false && "Unknown config source data type");
		}
		switch(aDataSource.format)
		{
		case eConfigDataFormat_JSON:
			sParser = new JSONParser(aDataSourceID);
			break;
		case eConfigDataFormat_INI:
			sParser = new INIParser(aDataSourceID);
			break;
		default:
			DBG_ASSERT(false && "Unknown config data format");
		}
		DBG_ASSERT(sReader && sParser);
		// Parsing will continue next update()
		return;
	}

	if( sDataSourcesToReCheck.any() )
	{
		// Need to attempt again to read these data sources
		// Not during initialization though, or may get stuck
		// in update loop waiting for them to become available
		if( sInitialized )
		{
			sDataSourcesToCheck = sDataSourcesToReCheck;
			sDataSourcesToReCheck.reset();
		}
		return;
	}

	// Once done with all parsing, apply change values found
	if( !sParser && !sReader && sValueSetsChanged.any() )
	{
		for(int aVarID = 0, aVariablesEnd = intSize(sVariables.size());
			aVarID < aVariablesEnd; ++aVarID)
		{
			SyncVariable& aSyncVar = sVariables[aVarID];
			if( sValueSetsChanged.test(aSyncVar.valueSetID) )
			{
				const std::string& aValueStr = getValueString(
					aSyncVar.funcType, aSyncVar.valueSetID);
				syncDebugPrint("Setting variable %s = %s\n",
					Profile::variableIDToName(aSyncVar.variableID).c_str(),
					aValueStr.c_str());
				Profile::setVariable(aSyncVar.variableID, aValueStr, true);
			}
		}
		sValueSetsChanged.reset();
		syncDebugPrint("All read properties now being applied!\n");
	}
}


void pauseMonitoring()
{
	sPaused = true;
}


void resumeMonitoring()
{
	sPaused = false;
}


void promptUserForSyncFileToUse()
{
	sForcePromptForWildcardFiles = true;

	load();

	if( sForcePromptForWildcardFiles )
	{
		Dialogs::showNotice(
			"No selection necessary - all target config file path "
			"patterns have one or less matching files anyway.",
			"No action needed");
	}

	sForcePromptForWildcardFiles = false;
}

#undef syncDebugPrint
#undef TARGET_CONFIG_SYNC_DEBUG_PRINT

} // TargetConfigSync
