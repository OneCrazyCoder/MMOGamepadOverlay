//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "TargetConfigSync.h"

#include "HotspotMap.h"
#include "HUD.h"
#include "InputMap.h"
#include "Lookup.h"
#include "Profile.h"
#include "WindowManager.h"

#include <winioctl.h> // FSCTL_REQUEST_FILTER_OPLOCK

namespace TargetConfigSync
{

// Uncomment this to print details about config file syncing to debug window
//#define TARGET_CONFIG_SYNC_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

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

enum EPropertyType
{
	ePropertyType_Unknown,
	ePropertyType_UIScale,
	ePropertyType_Hotspot,
	ePropertyType_CopyIcon,
	ePropertyType_HUDElement,

	ePropertyType_Num
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

const char* kTargetConfigFilesSectionName = "TargetConfigFiles";
const char* kSyncPropertiesSectionName = "TargetSyncProperties";
const char* kValueFormatStrSectionName = "TargetConfigFileFormat";
const char* kValueFormatInvertPrefix = "Invert";
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

static const u16 kValueSetFirstIdx[] =
{
	eValueSetSubType_Base,				// eValueSetType_Single
	eValueSetSubType_UIWindow_First,	// eValueSetType_UIWindow
};
DBG_CTASSERT(ARRAYSIZE(kValueSetFirstIdx) == eValueSetType_Num);

static const u16 kValueSetLastIdx[] =
{
	eValueSetSubType_Base,				// eValueSetType_Single
	eValueSetSubType_UIWindow_Last,		// eValueSetType_UIWindow
};
DBG_CTASSERT(ARRAYSIZE(kValueSetLastIdx) == eValueSetType_Num);

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
				{ "BASE",		eValueFunc_Base		},
				{ "VALUE",		eValueFunc_Base		},
				{ "X",			eValueFunc_PosX		},
				{ "POSX",		eValueFunc_PosX		},
				{ "POSITIONX",	eValueFunc_PosX		},
				{ "XPOS",		eValueFunc_PosX		},
				{ "XPOSITION",	eValueFunc_PosX		},
				{ "XORIGIN",	eValueFunc_PosX		},
				{ "ORIGINX",	eValueFunc_PosX		},
				{ "Y",			eValueFunc_PosY		},
				{ "POSY",		eValueFunc_PosY		},
				{ "POSITIONY",	eValueFunc_PosY		},
				{ "YPOS",		eValueFunc_PosY		},
				{ "YPOSITION",	eValueFunc_PosY		},
				{ "YORIGIN",	eValueFunc_PosY		},
				{ "ORIGINY",	eValueFunc_PosY		},
				{ "LEFT",		eValueFunc_Left		},
				{ "L",			eValueFunc_Left		},
				{ "TOP",		eValueFunc_Top		},
				{ "T",			eValueFunc_Top		},
				{ "CX",			eValueFunc_CX		},
				{ "CENTERX",	eValueFunc_CX		},
				{ "CENTREX",	eValueFunc_CX		},
				{ "XCENTER",	eValueFunc_CX		},
				{ "XCENTRE",	eValueFunc_CX		},
				{ "CY",			eValueFunc_CY		},
				{ "CENTERY",	eValueFunc_CY		},
				{ "CENTREY",	eValueFunc_CY		},
				{ "YCENTER",	eValueFunc_CY		},
				{ "YCENTRE",	eValueFunc_CY		},
				{ "RIGHT",		eValueFunc_Right	},
				{ "R",			eValueFunc_Right	},
				{ "BOTTOM",		eValueFunc_Bottom	},
				{ "B",			eValueFunc_Bottom	},
				{ "WIDTH",		eValueFunc_Width	},
				{ "W",			eValueFunc_Width	},
				{ "XSIZE",		eValueFunc_Width	},
				{ "SIZEX",		eValueFunc_Width	},
				{ "HEIGHT",		eValueFunc_Height	},
				{ "H",			eValueFunc_Height	},
				{ "YSIZE",		eValueFunc_Height	},
				{ "SIZEY",		eValueFunc_Height	},
				{ "ALIGNX",		eValueFunc_AlignX	},
				{ "ALIGNMENTX",	eValueFunc_AlignX	},
				{ "ANCHORX",	eValueFunc_AlignX	},
				{ "XALIGNMENT",	eValueFunc_AlignX	},
				{ "XALIGN",		eValueFunc_AlignX	},
				{ "XANCHOR",	eValueFunc_AlignX	},
				{ "ALIGNY",		eValueFunc_AlignY	},
				{ "ALIGNMENTY",	eValueFunc_AlignY	},
				{ "ANCHORY",	eValueFunc_AlignY	},
				{ "YALIGN",		eValueFunc_AlignY	},
				{ "YALIGNMENT",	eValueFunc_AlignY	},
				{ "YANCHOR",	eValueFunc_AlignY	},
				{ "SCALE",		eValueFunc_Scale	},
				{ "SCALING",	eValueFunc_Scale	},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EValueFunction* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eValueFunc_Num;
}

#ifdef TARGET_CONFIG_SYNC_DEBUG_PRINT
#define syncDebugPrint(...) debugPrint("TargetConfigSync: " __VA_ARGS__)
#else
#define syncDebugPrint(...) ((void)0)
#endif


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ZERO_INIT(SyncProperty)
{
	std::string section, name, valueFormat;
	struct Segment
	{
		std::string::size_type insertPos;
		EValueFunction funcType;
		u16 valueSetID;
	};
	std::vector<Segment> valueInserts;
	BitVector<64> valueSetsUsed;
	EPropertyType type;
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
	union { u16 fileID; u16 regValID; };
};

struct ZERO_INIT(ConfigFile)
{
	std::wstring pathW;
	FILETIME lastModTime;
};

struct ZERO_INIT(ConfigFileFolder)
{
	HANDLE hChangedSignal;
	std::vector<u16> sourceIDs;
};

struct ZERO_INIT(SystemRegistryValue)
{
	HKEY hKey;
	std::wstring valueNameW;
};

struct ZERO_INIT(SystemRegistryKey)
{
	HKEY hKey;
	HANDLE hChangedSignal;
	std::vector<u16> sourceIDs;
};

// Data used during parsing/building the sync links but deleted once done
struct TargetConfigSyncBuilder
{
	std::vector<ValueLinkMap> valueLinkMaps;
	StringToValueMap<u16> nameToLinkMapID;
	StringToValueMap<u16> pathToLinkMapID;
	StringToValueMap<u16> valueSetNameToIDMap;
	std::string valueFormatStrings[eValueSetSubType_Num];
	std::string debugString;
};

// Forward declares
class ConfigDataReader;
class ConfigDataParser;


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<ConfigFileFolder> sFolders;
static std::vector<ConfigFile> sFiles;
static std::vector<SystemRegistryKey> sRegKeys;
static std::vector<SystemRegistryValue> sRegVals;
static std::vector<DataSource> sDataSources;
static std::vector<SyncProperty> sProperties;
static std::vector<double> sValues;
static std::vector<u16> sValueSets;
static std::vector<std::wstring> sCurrWildcardMatches;
static std::vector<std::wstring> sBestWildcardMatches;
static std::vector<std::wstring> sLastReadWildcardMatches;
static BitVector<32> sChangedDataSources;
static BitVector<64> sChangedValueSets;
static ConfigDataReader* sReader;
static ConfigDataParser* sParser;
static bool sInvertAxis[eValueSetSubType_Num];
static bool sInitialized = false;
static bool sPaused = false;


//-----------------------------------------------------------------------------
// ConfigDataParser
//-----------------------------------------------------------------------------

class ConfigDataParser
{
public:
	ConfigDataParser(size_t theDataSourceID) :
		mDataSourceID(theDataSourceID),
		mDoneParsing(false)
	{
		DBG_ASSERT(theDataSourceID < sDataSources.size());
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
	size_t dataSourceID() const { return mDataSourceID; }

protected:
	bool anyPathsUsePrefix(const std::string& thePrefix) const
	{
		return sDataSources[mDataSourceID].values.containsPrefix(thePrefix);
	}

	bool checkForFoundValue(
		const std::string& thePath,
		const std::string& theValue)
	{
		if( ValueLink* aValuePtr =
				sDataSources[mDataSourceID].values.find(thePath) )
		{
			sValues[aValuePtr->valueIdx] = doubleFromString(theValue);
			sChangedValueSets.set(aValuePtr->setIdx);
			mUnfound.reset(
				aValuePtr - &sDataSources[mDataSourceID].values.values()[0]);
			syncDebugPrint("Read path %s value as %f\n",
				thePath.c_str(), sValues[aValuePtr->valueIdx]);
		}
		return mUnfound.none();
	}

protected:
	const size_t mDataSourceID;
	BitVector<512> mUnfound;
	bool mDoneParsing;
};


//-----------------------------------------------------------------------------
// JSONParser
//-----------------------------------------------------------------------------

class JSONParser : public ConfigDataParser
{
public:
	JSONParser(size_t theDataSourceID) :
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
					mDoneParsing = true;
					return;
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
						mDoneParsing = true;
						return;
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
		EState type;
		u16 pathLen;
		u16 valueIdx;
		bool skip;
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
		aNewState.pathLen = u16(mPath.size());
		aNewState.skip = mState.empty() ? false : mState.back().skip;
		mState.push_back(aNewState);
	}

	void popState()
	{
		mState.pop_back();
		mPath.resize(mState.back().pathLen);
	}
};


//-----------------------------------------------------------------------------
// INIParser
//-----------------------------------------------------------------------------

class INIParser : public ConfigDataParser
{
public:
	INIParser(size_t theDataSourceID) :
		ConfigDataParser(theDataSourceID)
	{
	}

	virtual void parseNextChunk(const std::string& theReadChunk)
	{
		// TODO
		mDoneParsing = true;
		return;
	}
};


//-----------------------------------------------------------------------------
// ConfigDataReader
//-----------------------------------------------------------------------------

class ConfigDataReader
{
public:
	ConfigDataReader()
		:
		mDoneReading(false),
		mSourceWasBusy(false)
	{}
	virtual ~ConfigDataReader() {}

	virtual std::string readNextChunk() = 0;
	virtual void reportResults() = 0;

	bool done() const { return mDoneReading; }
	bool sourceWasBusy() const { return mSourceWasBusy; }

protected:
	bool mDoneReading;
	bool mSourceWasBusy;
};


//-----------------------------------------------------------------------------
// ConfigFileReader
//-----------------------------------------------------------------------------

class ConfigFileReader : public ConfigDataReader
{
public:
	ConfigFileReader(size_t theFileID) :
		ConfigDataReader(),
		mFileID(theFileID),
		mBytesRead(),
		mFileHandle(),
		mFileLockHandle(),
		mFilePointer(),
		mReadOverlapped(),
		mLockOverlapped(),
		mBuffer(),
		mBufferIdx()
	{
		DBG_ASSERT(theFileID < sFiles.size());
		ConfigFile& aFile = sFiles[theFileID];
		// Get a file handle that won't block other apps' access to the file
		mFileLockHandle = CreateFile(
			aFile.pathW.c_str(),
			0,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
		if( mFileLockHandle == INVALID_HANDLE_VALUE )
		{
			logToFile("Failed to find target config file %s",
				narrow(aFile.pathW).c_str());
			mDoneReading = true;
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
				getFileName(narrow(aFile.pathW)).c_str());
			mDoneReading = mSourceWasBusy = true;
			return;
		default:
			logToFile("Failed to get oplock read access to target config file %s",
				narrow(aFile.pathW).c_str());
			mDoneReading = true;
			return;
		}

		// Open the OpLock'd file for read
		mFileHandle = CreateFile(
			aFile.pathW.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
		if( mFileHandle == INVALID_HANDLE_VALUE )
		{
			logToFile("Failed to open target config file %s",
				narrow(aFile.pathW).c_str());
			mDoneReading = true;
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
				logToFile("Error reading target config file %s",
					narrow(aFile.pathW).c_str());
				mDoneReading = true;
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
		if( mDoneReading )
			return result;

		DBG_ASSERT(mFileID < sFiles.size());
		ConfigFile& aFile = sFiles[mFileID];

		// If got OpLock break request abort and try again later
		if( WaitForSingleObject(mLockOverlapped.hEvent, 0) == WAIT_OBJECT_0 )
		{
			syncDebugPrint("Another app needed file %s - delaying read!\n",
				getFileName(narrow(aFile.pathW)).c_str());
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
					logToFile("Error reading target config file %s",
						narrow(aFile.pathW).c_str());
					mDoneReading = true;
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
		syncDebugPrint("Finished parsing '%s'\n",
			getFileName(narrow(sFiles[mFileID].pathW)).c_str());
	}

private:

	const size_t mFileID;
	DWORD mBytesRead[2];
	HANDLE mFileHandle, mFileLockHandle;
	LARGE_INTEGER mFilePointer;
	OVERLAPPED mReadOverlapped, mLockOverlapped;
	u8 mBuffer[2][kConfigFileBufferSize];
	u8 mBufferIdx;
};


//-----------------------------------------------------------------------------
// SystemRegistryValueReader
//-----------------------------------------------------------------------------

class SystemRegistryValueReader : public ConfigDataReader
{
public:
	SystemRegistryValueReader(size_t theRegValID) :
		ConfigDataReader(),
		mRegValID(theRegValID),
		mParsePos()
	{
		const SystemRegistryValue& aRegVal = sRegVals[mRegValID];
		if( !aRegVal.hKey )
		{
			mDoneReading = true;
			return;
		}

		// If no wildcard in value name, use it directly
		if( aRegVal.valueNameW.find(L'*') == std::wstring::npos )
		{
			mResolvedValueName.reserve(aRegVal.valueNameW.size()+1);
			mResolvedValueName.assign(
				aRegVal.valueNameW.begin(),
				aRegVal.valueNameW.end());
			mResolvedValueName.push_back(L'\0');
			DWORD aDataSize = 0;
			RegGetValue(aRegVal.hKey, NULL, &mResolvedValueName[0],
				RRF_RT_REG_BINARY, NULL, NULL, &aDataSize);
			if( aDataSize == 0 )
			{
				mDoneReading = true;
				return;
			}

			mValueData.resize(aDataSize);
			if( RegGetValue(aRegVal.hKey, NULL, &mResolvedValueName[0],
					RRF_RT_REG_BINARY, NULL, &mValueData[0], &aDataSize)
						!= ERROR_SUCCESS)
			{
				mDoneReading = true;
				return;
			}

			return;
		}

		// Query the maximum value name length
		DWORD aMaxValueNameLen = 0;
		if( RegQueryInfoKey(
				aRegVal.hKey, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL, &aMaxValueNameLen, NULL, NULL, NULL)
					!= ERROR_SUCCESS )
		{
			mDoneReading = true;
			return;
		}
		++aMaxValueNameLen;

		// Seach for best candidate if multiple found that match
		DWORD aValueNameLen;
		std::vector<WCHAR> aValueName(aMaxValueNameLen);
		mResolvedValueName.resize(aMaxValueNameLen);
		std::vector<BYTE> aValueData;
		u64 aBestValueTimestamp = 0;
		sBestWildcardMatches.clear();
		bool madeChangesToRegistry = false;

		for(DWORD aValueIdx = 0; /*until break*/; ++aValueIdx)
		{
			aValueNameLen = aMaxValueNameLen;
			if( RegEnumValue(
					aRegVal.hKey, aValueIdx, &aValueName[0],
					&aValueNameLen, NULL, NULL,
					NULL, NULL) != ERROR_SUCCESS )
			{
				// End of enumeration
				break;
			}

			sCurrWildcardMatches.clear();
			if( !wildcardMatch(
					&aValueName[0],
					aRegVal.valueNameW.c_str(),
					&sCurrWildcardMatches) )
			{
				continue;
			}

			DWORD aDataSize = 0;
			RegGetValue(aRegVal.hKey, NULL, &aValueName[0],
				RRF_RT_REG_BINARY, NULL, NULL, &aDataSize);
			if( aDataSize == 0 )
				continue;

			aValueData.reserve(aDataSize + kTimestampSuffixSize);
			aValueData.resize(aDataSize);
			if( RegGetValue(aRegVal.hKey, NULL, &aValueName[0],
					RRF_RT_REG_BINARY, NULL, &aValueData[0], &aDataSize)
						!= ERROR_SUCCESS)
			{
				continue;
			}

			// When there's more than one match, the preference is the newest.
			// No way to check that though, so we add timestamps ourselves by
			// hiding them after the terminating null character. If a value
			// has no timestamp, it's assumed it was written out by the game
			// (stripping out our timestamp in the process) and thus is
			// treated as the newest via default timestamp of max u64 value.
			// Until that happens though, if more than one match without a
			// timestamp, our choice becomes essentially random.
			const u64 aValueTimestamp = extractTimestamp(aValueData);
			if( aValueTimestamp == 0xFFFFFFFFFFFFFFFFULL )
			{// No custom time stamp found
				if( appendTimestamp(aValueData) &&
					RegSetValueEx(aRegVal.hKey, &aValueName[0], NULL,
						REG_BINARY, &aValueData[0],
						DWORD(aValueData.size()))
							== ERROR_SUCCESS )
				{
					madeChangesToRegistry = true;
				}
			}
			bool isBetterMatch = aValueTimestamp > aBestValueTimestamp;

			if( aValueTimestamp == aBestValueTimestamp )
			{
				// For a tie breaker, if one of the substrings that matched 
				// with a * is the same as one used in the last-chosen value
				// name, prefer that one as they are likely related.
				for(size_t i = 0; i < sLastReadWildcardMatches.size(); ++i)
				{
					for(size_t j = 0; j < sCurrWildcardMatches.size(); ++j)
					{
						if( sLastReadWildcardMatches[i] == 
								sCurrWildcardMatches[j] )
						{
							isBetterMatch = true;
							break;
						}
					}
					if( isBetterMatch )
						break;
				}
			}

			if( isBetterMatch )
			{
				aBestValueTimestamp = aValueTimestamp;
				swap(aValueName, this->mResolvedValueName);
				swap(aValueData, this->mValueData);
				swap(sCurrWildcardMatches, sBestWildcardMatches);
			}
		}

		if( !mValueData.empty() && !sBestWildcardMatches.empty() )
			swap(sBestWildcardMatches, sLastReadWildcardMatches);

		// After initial load, don't actually parse the data if
		// wrote a timestamp, as that will trigger a re-parse itself,
		// so no sense doing it twice. Flag as "busy" to make sure
		// will parse later even if notification fails to trigger.
		if( sInitialized && madeChangesToRegistry )
		{
			mDoneReading = true;
			mSourceWasBusy = true;
		}
	}

	virtual std::string readNextChunk()
	{
		std::string result;
		if( mDoneReading )
			return result;

		DBG_ASSERT(mParsePos < mValueData.size());

		// Return the previously read-in data for parsing
		const size_t aBytesToRead = min(
			mValueData.size() - mParsePos,
			kConfigFileBufferSize);
		result.assign((char*)&mValueData[mParsePos], aBytesToRead);
		mParsePos += aBytesToRead;
		if( mParsePos >= mValueData.size() )
			mDoneReading = true;

		syncDebugPrint(
			"Read %d bytes from system registry data\n",
			aBytesToRead);

		return result;
	}

	virtual void reportResults()
	{
		if( !mResolvedValueName.empty() &&
			mResolvedValueName[0] != '\0' )
		{
			syncDebugPrint("Finished parsing '%s'\n",
				narrow(&mResolvedValueName[0]).c_str());
		}
	}

private:
	static const size_t kTimestampMarkerSize = 4;
	static const size_t kTimestampSize = sizeof(u64);
	static const size_t kTimestampSuffixSize =
		kTimestampMarkerSize + sizeof(u64) + 1 /* traling null */;

	u64 extractTimestamp(const std::vector<BYTE>& theData) const
	{
		u64 result = 0xFFFFFFFFFFFFFFFFULL;
		if( theData.size() <= 1 + kTimestampSuffixSize )
			return result;

		const size_t kTimestampOffset = theData.size() -
			(1 + kTimestampSize);
		const size_t kMarkerOffset = theData.size() -
			(1 + kTimestampSize + kTimestampMarkerSize);

		if( theData.back() != '\0' || theData[kMarkerOffset - 1] != '\0')
			return result;

		memcpy(&result, &theData[kTimestampOffset], sizeof(u64));
		return result;
	}

	bool appendTimestamp(std::vector<BYTE>& theData)
	{
		if( theData.empty() || theData.back() != '\0' )
			return false;

		const char* kTimestampMarker = "_TS_";
		theData.insert(theData.end(), kTimestampMarker,
			kTimestampMarker + kTimestampMarkerSize);

		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		ULARGE_INTEGER aTimestamp;
		aTimestamp.LowPart = ft.dwLowDateTime;
		aTimestamp.HighPart = ft.dwHighDateTime;
		const BYTE* aTimestampPtr = (const BYTE*)(&aTimestamp.QuadPart);
		theData.insert(theData.end(), aTimestampPtr,
			aTimestampPtr + kTimestampSize);
		theData.push_back('\0');

		return true;
	}

	const size_t mRegValID;
	size_t mParsePos;
	std::vector<BYTE> mValueData;
	std::vector<WCHAR> mResolvedValueName;
};


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static FILETIME getFileLastModTime(const ConfigFile& theFile)
{
	WIN32_FILE_ATTRIBUTE_DATA aFileAttr;
	if( GetFileAttributesEx(theFile.pathW.c_str(),
			GetFileExInfoStandard, &aFileAttr) )
	{
		return aFileAttr.ftLastWriteTime;
	}

	return FILETIME();
}


static bool isRegistryPath(const std::string thePath)
{
	return
		thePath.size() > 2 &&
		::toupper(thePath[0]) == 'H' &&
		::toupper(thePath[1]) == 'K';
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
			thePath = upper(thePath);
			thePath = replaceChar(thePath, '/', '\\');
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

	thePath = upper(toAbsolutePath(thePath));
	return thePath;
}


static HKEY getRootKeyHandle(const std::string& root)
{
	if( root == "HKEY_LOCAL_MACHINE" )	return HKEY_LOCAL_MACHINE;
	if( root == "HKEY_CURRENT_USER" )	return HKEY_CURRENT_USER;
	if( root == "HKEY_CLASSES_ROOT" )	return HKEY_CLASSES_ROOT;
	if( root == "HKEY_CURRENT_CONFIG" )	return HKEY_CURRENT_CONFIG;
	if( root == "HKEY_USERS" )			return HKEY_USERS;
	return null;
}


static bool setFetchValueFromDataSource(
	TargetConfigSyncBuilder& theBuilder,
	const std::string& theSubstituteStr,
	const u16 theDestValueSetID,
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
		while(aTagCoords.first != std::string::npos )
		{
			const std::string& aTag = condense(
				aConfigDataPath.substr(
					aTagCoords.first + 1, aTagCoords.second - 2));
			if( isAnInteger(aTag) || aTag == "NAME" )
			{
				// <name> can be used the same as <1>
				const u32 aTagNum = max(intFromString(aTag), 1) - 1;
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
			aTagCoords = findStringTag(
				aConfigDataPath,
				aTagCoords.first + aTagCoords.second);
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
	u16* const aValueLinkMapID =
		theBuilder.nameToLinkMapID.find(condense(aDataSourceKey));
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
	aDestValue.setIdx = theDestValueSetID;
	aDestValue.valueIdx =
		sValueSets[theDestValueSetID] +
		theDestValueSetSubType -
		kValueSetFirstIdx[theValueSetType];
	DBG_ASSERT(*aValueLinkMapID < theBuilder.valueLinkMaps.size());
	theBuilder.valueLinkMaps[*aValueLinkMapID].setValue(
		aConfigDataPath, aDestValue);
	return true;
}


static bool setConfigValueLinks(
	TargetConfigSyncBuilder& theBuilder,
	SyncProperty::Segment& thePropSegment,
	const std::string& theConfigFileValueName,
	EValueSetType theValueSetType)
{
	// Find or create value set for the value name given
	thePropSegment.valueSetID =
		theBuilder.valueSetNameToIDMap.findOrAdd(
			theConfigFileValueName,
			u16(sValueSets.size()));
	if( thePropSegment.valueSetID >= sValueSets.size() )
	{
		sValueSets.push_back(u16(sValues.size()));
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
				thePropSegment.valueSetID, theValueSetType, x)
	switch(thePropSegment.funcType)
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


static void parsePropertyValueTags(
	TargetConfigSyncBuilder& theBuilder,
	SyncProperty& theProperty,
	std::string theDesc)
{
	std::pair<std::string::size_type, std::string::size_type> aTagCoords =
		findStringTag(theDesc);
	while(aTagCoords.first != std::string::npos )
	{
		SyncProperty::Segment aSegment = SyncProperty::Segment();
		// Extract the tag contents
		std::string aTag = theDesc.substr(
			aTagCoords.first + 1, aTagCoords.second - 2);
		// Remove the tag from the description string
		theDesc.replace(aTagCoords.first, aTagCoords.second, "");
		// Note the insertion point for later replacement
		aSegment.insertPos = aTagCoords.first;
		// Get function identifier (empty is valid as "base" function)
		const std::string& aFuncName = breakOffItemBeforeChar(aTag, ':');
		aSegment.funcType = valueFuncNameToID(condense(aFuncName));
		if( aSegment.funcType == eValueFunc_Num )
		{
			logError("Unknown function name '%s' in sync property '%s'",
				aFuncName.c_str(), theBuilder.debugString.c_str());
			theProperty.valueSetsUsed.clear();
			return;
		}
		EValueSetType aValueSetType = funcToValueSetType(aSegment.funcType);
		// Set links from config data sources back to this segment
		if( !setConfigValueLinks(theBuilder, aSegment, aTag, aValueSetType) )
		{
			theProperty.valueSetsUsed.clear();
			return;
		}
		theProperty.valueInserts.push_back(aSegment);
		theProperty.valueSetsUsed.resize(sValueSets.size());
		theProperty.valueSetsUsed.set(aSegment.valueSetID);
		// Check for another tag after this one
		aTagCoords = findStringTag(theDesc, aTagCoords.first);
	}
	theProperty.valueFormat.reserve(theDesc.size());
	theProperty.valueFormat = theDesc;
	// trim to fit
	std::vector<SyncProperty::Segment>(theProperty.valueInserts)
		.swap(theProperty.valueInserts);
}


static EPropertyType extractPropertyType(const SyncProperty& theProperty)
{
	if( theProperty.section == "HOTSPOTS" )
		return ePropertyType_Hotspot;
	if( theProperty.section == "ICONS" )
		return ePropertyType_CopyIcon;
	if( hasPrefix(theProperty.section, "HUD") ||
		hasPrefix(theProperty.section, "MENU") )
		return ePropertyType_HUDElement;
	if( theProperty.section == "SYSTEM" && theProperty.name == "UISCALE" )
		return ePropertyType_UIScale;
	return ePropertyType_Unknown;
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
			result *= getSubTypeValue(
				theValArray,
				theSubType == eValueSetSubType_PivotX
					? eValueSetSubType_SizeX
					: eValueSetSubType_SizeY);
		}
		break;
	case eValueSetSubType_SizeX:
	case eValueSetSubType_SizeY:
		if( sInvertAxis[theSubType] )
			result = -result;
		result = max(0, result);
		break;
	case eValueSetSubType_Scale:
		if( result <= 0 )
			result = 1.0;
		break;
	}
	return result;
}


static std::string getValueInsertString(
	EValueFunction theFunction,
	u16 theValueSet)
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
				result = getValueInsertString(eValueFunc_Left, theValueSet);
			else if( anAlign > 0.6 )
				result = getValueInsertString(eValueFunc_Right, theValueSet);
			else
				result = getValueInsertString(eValueFunc_CX, theValueSet);
		}
		break;
	case eValueFunc_PosY:
		{
			const double anAlign = getSubTypeValue(v, eValueSetSubType_AlignY);
			if( anAlign < 0.4 )
				result = getValueInsertString(eValueFunc_Top, theValueSet);
			else if( anAlign > 0.6 )
				result = getValueInsertString(eValueFunc_Bottom, theValueSet);
			else
				result = getValueInsertString(eValueFunc_CY, theValueSet);
		}
		break;
	case eValueFunc_Left:
		result = getValueInsertString(eValueFunc_AlignX, theValueSet);
		{
			const int anOffset =
				getSubTypeValue(v, eValueSetSubType_PosX) -
				getSubTypeValue(v, eValueSetSubType_PivotX);
			if( anOffset )
			{
				if( anOffset > 0 ) result += "+";
				result += toString(anOffset);
			}
		}
		break;
	case eValueFunc_Top:
		result = getValueInsertString(eValueFunc_AlignY, theValueSet);
		{
			const int anOffset =
				getSubTypeValue(v, eValueSetSubType_PosY) -
				getSubTypeValue(v, eValueSetSubType_PivotY);
			if( anOffset )
			{
				if( anOffset > 0 ) result += "+";
				result += toString(anOffset);
			}
		}
		break;
	case eValueFunc_CX:
		result = getValueInsertString(eValueFunc_AlignX, theValueSet);
		{
			const int anOffset =
				getSubTypeValue(v, eValueSetSubType_PosX) -
				getSubTypeValue(v, eValueSetSubType_PivotX) +
				getSubTypeValue(v, eValueSetSubType_SizeX) * 0.5;
			if( anOffset )
			{
				if( anOffset > 0 ) result += "+";
				result += toString(anOffset);
			}
		}
		break;
	case eValueFunc_CY:
		result = getValueInsertString(eValueFunc_AlignY, theValueSet);
		{
			const int anOffset =
				getSubTypeValue(v, eValueSetSubType_PosY) -
				getSubTypeValue(v, eValueSetSubType_PivotY) +
				getSubTypeValue(v, eValueSetSubType_SizeY) * 0.5;
			if( anOffset )
			{
				if( anOffset > 0 ) result += "+";
				result += toString(anOffset);
			}
		}
		break;
	case eValueFunc_Right:
		result = getValueInsertString(eValueFunc_AlignX, theValueSet);
		{
			const int anOffset =
				getSubTypeValue(v, eValueSetSubType_PosX) -
				getSubTypeValue(v, eValueSetSubType_PivotX) +
				getSubTypeValue(v, eValueSetSubType_SizeX);
			if( anOffset )
			{
				if( anOffset > 0 ) result += "+";
				result += toString(anOffset);
			}
		}
		break;
	case eValueFunc_Bottom:
		result = getValueInsertString(eValueFunc_AlignY, theValueSet);
		{
			const int anOffset =
				getSubTypeValue(v, eValueSetSubType_PosY) -
				getSubTypeValue(v, eValueSetSubType_PivotY) +
				getSubTypeValue(v, eValueSetSubType_SizeY);
			if( anOffset )
			{
				if( anOffset > 0 ) result += "+";
				result += toString(anOffset);
			}
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
		result = toString(
			getSubTypeValue(v, eValueSetSubType_Scale) * 100.0) + "%";
		break;
	default:
		DBG_ASSERT(false && "Invalid EValueFunction");
		result = "0";
	}
	return result;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void load()
{
	if( sInitialized || sPaused )
		cleanup();
	TargetConfigSyncBuilder aBuilder;

	{// Fetch target config paths potentially containing sync properties
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kTargetConfigFilesSectionName);
		aBuilder.nameToLinkMapID.reserve(aPropMap.size());
		aBuilder.pathToLinkMapID.reserve(aPropMap.size());
		aBuilder.valueLinkMaps.reserve(aPropMap.size());
		for(size_t i = 0; i < aPropMap.size(); ++i)
		{
			const u16 aLinkMapID = aBuilder.pathToLinkMapID.findOrAdd(
				normalizedPath(aPropMap.vals()[i]),
				u16(aBuilder.valueLinkMaps.size()));
			if( aLinkMapID >= aBuilder.valueLinkMaps.size() )
				aBuilder.valueLinkMaps.push_back(ValueLinkMap());
			aBuilder.nameToLinkMapID.setValue(
				condense(aPropMap.keys()[i]), aLinkMapID);
		}
	}

	// Fetch value path formating data
	for(size_t i = 0; i < eValueSetSubType_Num; ++i)
	{
		aBuilder.valueFormatStrings[i] = Profile::getStr(
			kValueFormatStrSectionName,
			kValueFormatStringKeys[i]);
		sInvertAxis[i] = Profile::getBool(
			kValueFormatStrSectionName,
			std::string(kValueFormatInvertPrefix) +
			kValueFormatStringKeys[i]);
	}

	{// Fetch sync property values to read from the data sources
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(kSyncPropertiesSectionName);
		for(size_t i = 0; i < aPropMap.size(); ++i)
		{
			aBuilder.debugString = aPropMap.keys()[i];
			aBuilder.debugString += " = ";
			aBuilder.debugString += aPropMap.vals()[i];
			// Separate key into section and property name by > character
			SyncProperty aProperty;
			aProperty.section = aPropMap.keys()[i];
			size_t aPos = aProperty.section.find('>');
			if( aPos == std::string::npos )
			{
				logError("Missing '>' between section and property name "
					"for sync property '%s'",
					aPropMap.keys()[i].c_str());
				continue;
			}
			parsePropertyValueTags(
				aBuilder, aProperty,
				aPropMap.vals()[i]);
			if( aProperty.valueSetsUsed.none() )
				continue;
			aProperty.name = aProperty.section.substr(aPos+1);
			aProperty.section.resize(aPos);
			aProperty.type = extractPropertyType(aProperty);
			sProperties.push_back(aProperty);
		}
	}
	aBuilder.nameToLinkMapID.clear();

	// Prepare data sources for reading and monitoring for changes
	StringToValueMap<u16> aFolderPathToIdxMap;
	StringToValueMap<u16> aRegKeyPathToIdxMap;
	for(size_t i = 0; i < aBuilder.pathToLinkMapID.size(); ++i)
	{
		const size_t aValueLinkMapIdx = 
			aBuilder.pathToLinkMapID.values()[i];
		DBG_ASSERT(aValueLinkMapIdx < aBuilder.valueLinkMaps.size());
		ValueLinkMap& aValueLinkMap =
			aBuilder.valueLinkMaps[aValueLinkMapIdx];
		if( aValueLinkMap.empty() )
			continue;
		const std::string& aSourcePath = aBuilder.pathToLinkMapID.keys()[i];
		DataSource aNewDataSource;
		aNewDataSource.values = aValueLinkMap;
		const u16 aSourceID = u16(sDataSources.size());
		if( isRegistryPath(aSourcePath) )
		{
			aNewDataSource.type = eDataSourceType_RegVal;
			aNewDataSource.format = eConfigDataFormat_JSON;
			aNewDataSource.regValID = u16(sRegVals.size());
			SystemRegistryValue aNewRegVal;
			aNewRegVal.valueNameW = widen(getFileName(aSourcePath));
			std::string aRegKeyPath = getFileDir(aSourcePath);
			const u16 aRegKeyID = aRegKeyPathToIdxMap.findOrAdd(
				aRegKeyPath, u16(sRegKeys.size()));
			if( aRegKeyID >= sRegKeys.size() )
			{
				const HKEY aRootKey = getRootKeyHandle(
					breakOffNextItem(aRegKeyPath, '\\'));
				if( !aRootKey )
				{
					logError("Invalid root registry key name in path '%s'",
						getFileDir(aSourcePath).c_str());
					continue;
				}
				SystemRegistryKey aNewRegKey;
				aNewRegKey.hChangedSignal = aNewRegKey.hKey = NULL;
				if( RegOpenKeyEx(aRootKey, widen(aRegKeyPath).c_str(), 0,
						KEY_READ | KEY_WRITE | KEY_NOTIFY, &aNewRegKey.hKey)
							!= ERROR_SUCCESS )
				{
					// Try just read/notify access
					if( RegOpenKeyEx(aRootKey, widen(aRegKeyPath).c_str(), 0,
							KEY_READ | KEY_NOTIFY, &aNewRegKey.hKey)
								!= ERROR_SUCCESS )
					{
						logToFile(
							"Couldn't open System Registry key '%s' "
							"(does not exist yet?)",
							getFileDir(aSourcePath).c_str());
						continue;
					}
				}
				sRegKeys.push_back(aNewRegKey);
			}
			aNewRegVal.hKey = sRegKeys[aRegKeyID].hKey;
			sRegKeys[aRegKeyID].sourceIDs.push_back(aSourceID);
			sRegVals.push_back(aNewRegVal);
		}
		else
		{
			aNewDataSource.type = eDataSourceType_File;
			aNewDataSource.format = eConfigDataFormat_JSON; // TODO properly
			aNewDataSource.fileID = u16(sFiles.size());
			ConfigFile aNewConfigFile;
			aNewConfigFile.pathW = widen(aSourcePath);
			aNewConfigFile.lastModTime = getFileLastModTime(aNewConfigFile);
			const std::string& aFolderPath = getFileDir(aSourcePath);
			const u16 aFolderID = aFolderPathToIdxMap.findOrAdd(
				aFolderPath, u16(sFolders.size()));
			if( aFolderID >= sFolders.size() )
			{
				const std::wstring& aFolderPathW = widen(aFolderPath);
				if( !isValidFolderPath(aFolderPathW) )
				{
					logToFile("Config file folder %s does not exist (yet?)",
						aFolderPath.c_str());
					continue;
				}
				ConfigFileFolder aNewFolder;
				// Monitor for future changes to this folder
				aNewFolder.hChangedSignal = FindFirstChangeNotification(
					aFolderPathW.c_str(),
					FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
				sFolders.push_back(aNewFolder);
			}
			sFolders[aFolderID].sourceIDs.push_back(aSourceID);
			sFiles.push_back(aNewConfigFile);
		}
		sDataSources.push_back(aNewDataSource);
		sDataSources.back().values = aValueLinkMap;
	}
	aFolderPathToIdxMap.clear();
	aRegKeyPathToIdxMap.clear();
	aBuilder.pathToLinkMapID.clear();
	aBuilder.valueLinkMaps.clear();

	// Trim memory and resize structures
	if( sFolders.size() < sFolders.capacity() )
		std::vector<ConfigFileFolder>(sFolders).swap(sFolders);
	if( sFiles.size() < sFiles.capacity() )
		std::vector<ConfigFile>(sFiles).swap(sFiles);
	if( sRegKeys.size() < sRegKeys.capacity() )
		std::vector<SystemRegistryKey>(sRegKeys).swap(sRegKeys);
	if( sRegVals.size() < sRegVals.capacity() )
		std::vector<SystemRegistryValue>(sRegVals).swap(sRegVals);
	if( sDataSources.size() < sDataSources.capacity() )
		std::vector<DataSource>(sDataSources).swap(sDataSources);
	if( sProperties.size() < sProperties.capacity() )
		std::vector<SyncProperty>(sProperties).swap(sProperties);
	if( sValues.size() < sValues.capacity() )
		std::vector<double>(sValues).swap(sValues);
	if( sValueSets.size() < sValueSets.capacity() )
		std::vector<u16>(sValueSets).swap(sValueSets);
	sChangedValueSets.clearAndResize(sValueSets.size());

	// Load initial values and log file timestamps
	sChangedDataSources.clearAndResize(sDataSources.size());
	sChangedDataSources.set();
	if( !sFiles.empty() || !sRegVals.empty() )
		update();

	// Begin monitoring for registry changes only after first load
	for(size_t aRegKeyID = 0; aRegKeyID < sRegKeys.size(); ++aRegKeyID)
	{
		SystemRegistryKey& aRegKey = sRegKeys[aRegKeyID];
		aRegKey.hChangedSignal = CreateEvent(NULL, TRUE, FALSE, NULL);
		if( aRegKey.hChangedSignal )
		{
			RegNotifyChangeKeyValue(
				aRegKey.hKey,
				FALSE, REG_NOTIFY_CHANGE_LAST_SET,
				aRegKey.hChangedSignal, TRUE);
		}
	}

	sInitialized = true;
}


void loadProfileChanges()
{
	if( Profile::changedSections().contains(
			condense(kTargetConfigFilesSectionName)) ||
		Profile::changedSections().contains(
			condense(kSyncPropertiesSectionName)) ||
		Profile::changedSections().contains(
			condense(kValueFormatStrSectionName)) )
	{
		load();
	}
}


void cleanup()
{
	delete sReader; sReader = null;
	delete sParser; sParser = null;

	for(size_t i = 0; i < sFolders.size(); ++i)
		FindCloseChangeNotification(sFolders[i].hChangedSignal);
	sFolders.clear();
	sFiles.clear();

	for(size_t i = 0; i < sRegKeys.size(); ++i)
	{
		RegCloseKey(sRegKeys[i].hKey);
		CloseHandle(sRegKeys[i].hChangedSignal);
	}
	sRegKeys.clear();
	sRegVals.clear();

	sDataSources.clear();
	sProperties.clear();
	sValues.clear();
	sValueSets.clear();
	sChangedValueSets.clear();
	sChangedDataSources.clear();
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
		return;
	}

	// Continue any active reading & parsing already in progress
	if( sReader || sParser )
	{
		DBG_ASSERT(sReader && sParser);
		sParser->parseNextChunk(sReader->readNextChunk());
		if( sReader->done() || sParser->done() )
		{
			if( !sReader->sourceWasBusy() )
			{
				sReader->reportResults();
				sParser->reportResults();
				sChangedDataSources.reset(sParser->dataSourceID());
			}
			delete sReader; sReader = null;
			delete sParser; sParser = null;
		}
		return;
	}

	if( sInitialized )
	{// Check for any folder or registry key changes after initial load
		for(size_t aFolderID = 0; aFolderID < sFolders.size(); ++aFolderID)
		{
			ConfigFileFolder& aFolder = sFolders[aFolderID];
			if( WaitForSingleObject(aFolder.hChangedSignal, 0)
					== WAIT_OBJECT_0 )
			{
				// Re-arm the notification for next update
				FindNextChangeNotification(aFolder.hChangedSignal);

				// Use timestamps to check if any contained files are updated
				for( size_t i = 0; i < aFolder.sourceIDs.size(); ++i )
				{
					DataSource& aSource = sDataSources[aFolder.sourceIDs[i]];
					DBG_ASSERT(aSource.type == eDataSourceType_File);
					ConfigFile& aFile = sFiles[aSource.fileID];
					FILETIME aModTime = getFileLastModTime(aFile);
					if( CompareFileTime(&aModTime, &aFile.lastModTime) > 0 )
					{
						syncDebugPrint("Detected change in file %s\n",
							getFileName(narrow(aFile.pathW)).c_str());
						aFile.lastModTime = aModTime;
						sChangedDataSources.set(aFolder.sourceIDs[i]);
					}
				}
			}
		}

		for(size_t aRegKeyID = 0; aRegKeyID < sRegKeys.size(); ++aRegKeyID)
		{
			SystemRegistryKey& aRegKey = sRegKeys[aRegKeyID];
			if( WaitForSingleObject(aRegKey.hChangedSignal, 0)
					== WAIT_OBJECT_0)
			{
				// Rearm the notification for next update
				RegNotifyChangeKeyValue(
					aRegKey.hKey,
					FALSE, REG_NOTIFY_CHANGE_LAST_SET,
					aRegKey.hChangedSignal, TRUE);

				// Mark all data sources using this key as changed
				for(size_t i = 0; i < aRegKey.sourceIDs.size(); ++i)
				{
					if( sChangedDataSources.test(aRegKey.sourceIDs[i]) )
						continue;

					sChangedDataSources.set(aRegKey.sourceIDs[i]);
					syncDebugPrint(
						"Detected change in registry key value name %s\n",
						narrow(sRegVals[sDataSources[aRegKey.sourceIDs[i]].
							regValID].valueNameW).c_str());
				}
			}
		}
	}

	// Begin parsing any changed data sources
	for(int aDataSourceID = sChangedDataSources.firstSetBit();
		aDataSourceID < sChangedDataSources.size();
		aDataSourceID = sChangedDataSources.nextSetBit(aDataSourceID+1))
	{
		DBG_ASSERT(!sParser && !sReader);
		DataSource& aDataSource = sDataSources[aDataSourceID];
		switch(aDataSource.type)
		{
		case eDataSourceType_File:
			sReader = new ConfigFileReader(aDataSource.fileID);
			break;
		case eDataSourceType_RegVal:
			sReader = new SystemRegistryValueReader(aDataSource.regValID);
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
		// Attempt to complete parsing immediately when initializing
		if( !sInitialized )
		{
			while(!sParser->done() && !sReader->done())
				sParser->parseNextChunk(sReader->readNextChunk());
			if( !sReader->sourceWasBusy() )
			{
				sReader->reportResults();
				sParser->reportResults();
				sChangedDataSources.reset(sParser->dataSourceID());
			}
			delete sReader; sReader = null;
			delete sParser; sParser = null;
			continue;
		}
		// Otherwise only parse one file at a time
		return;
	}

	// Once done with all parsing, apply change values found
	if( !sParser && !sReader &&
		sChangedDataSources.none() && sChangedValueSets.any() )
	{
		bool propTypeChanged[ePropertyType_Num] = { };
		for(size_t aPropID = 0; aPropID < sProperties.size(); ++aPropID)
		{
			SyncProperty& aProp = sProperties[aPropID];
			if( (sChangedValueSets & aProp.valueSetsUsed).any() )
			{
				std::string aValueStr = aProp.valueFormat;
				// Work backwards so don't mess up insert positions
				for(int i = int(aProp.valueInserts.size()-1); i >= 0; --i)
				{
					aValueStr.insert(
						aProp.valueInserts[i].insertPos,
						getValueInsertString(
							aProp.valueInserts[i].funcType,
							aProp.valueInserts[i].valueSetID));
				}
				syncDebugPrint("Setting [%s] %s = %s\n",
					aProp.section.c_str(),
					aProp.name.c_str(),
					aValueStr.c_str());
				Profile::setStr(aProp.section, aProp.name, aValueStr, false);
				propTypeChanged[aProp.type] = true;
			}
		}
		if( sInitialized )
		{// After initial load, so need to let other modules know of changes
			if( propTypeChanged[ePropertyType_Unknown] )
				gLoadNewProfile = true;
			if( propTypeChanged[ePropertyType_CopyIcon] )
				HUD::reloadCopyIconLabel("");
			if( propTypeChanged[ePropertyType_HUDElement] )
			{
				for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
					HUD::reloadElementShape(i);
			}
		}
		sChangedValueSets.reset();
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

#undef syncDebugPrint
#undef TARGET_CONFIG_SYNC_DEBUG_PRINT

} // TargetConfigSync
