//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "TargetConfigSync.h"

#include "HotspotMap.h" // reloadPositions()
#include "HUD.h" // reloadLayout()
#include "InputMap.h" //reloadAllHotspots()
#include "Lookup.h"
#include "Profile.h"
#include "WindowManager.h" // updateUIScale()

#include <winioctl.h> // FSCTL_REQUEST_FILTER_OPLOCK

namespace TargetConfigSync
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kConfigFileBufferSize = 256, // How many chars to stream from file per read
};

enum EConfigFileFormat
{
	eConfigFileFormat_JSON,
	eConfigFileFormat_INI,
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

const char* kTargetConfigFilesPrefix = "TargetConfigFiles/";
const char* kSyncPropertiesPrefix = "TargetSyncProperties/";
const char* kValueFormatNameTag = "NAME";
const char* kValueFormatStrPrefix = "TargetConfigFileFormat/";
const char* kValueFormatInvertPrefix = "TargetConfigFileFormat/Invert";
const char* kValueFormatStringKeys[] =
{
	"Value",		// eValueSetSubType_Base
	"PositionX",	// eValueSetSubType_PosX
	"PositionY",	// eValueSetSubType_PosY
	"AlignmentX",	// eValueSetSubType_AlignX
	"AlignmentY",	// eValueSetSubType_AlignY
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
				{ "POSX",		eValueFunc_PosX		},
				{ "POSITIONX",	eValueFunc_PosX		},
				{ "XPOS",		eValueFunc_PosX		},
				{ "XPOSITION",	eValueFunc_PosX		},
				{ "XORIGIN",	eValueFunc_PosX		},
				{ "ORIGINX",	eValueFunc_PosX		},
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
				{ "ANCHORX",	eValueFunc_AlignX	},
				{ "XALIGN",		eValueFunc_AlignX	},
				{ "XANCHOR",	eValueFunc_AlignX	},
				{ "ALIGNY",		eValueFunc_AlignY	},
				{ "ANCHORY",	eValueFunc_AlignY	},
				{ "YALIGN",		eValueFunc_AlignY	},
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


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct TargetSyncProperty :
	public ConstructFromZeroInitializedMemory<TargetSyncProperty>
{
	std::string section, name, valueFormat;
	struct Segment
	{
		std::string::size_type insertPos;
		EValueFunction funcType;
		u16 valueSetID;
	};
	std::vector<Segment> valueInserts;
	BitVector<> valueSetsUsed;
	EPropertyType type;
};

struct TargetConfigFile
{
	EConfigFileFormat format;
	std::wstring pathW;
	FILETIME lastModTime;
	struct Value { u16 setIdx, valueIdx; };
	StringToValueMap<Value> values;
};

struct TargetConfigFolder
{
	HANDLE hChangedSignal;
	std::vector<u16> fileIDs;
};

// Data used during parsing/building the sync links but deleted once done
struct TargetConfigSyncBuilder
{
	StringToValueMap<u16> fileKeyToIDMap;
	StringToValueMap<u16> valueSetNameToIDMap;
	std::string valueFormatStrings[eValueSetSubType_Num];
	std::string debugString;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<TargetConfigFolder> sFolders;
static std::vector<TargetConfigFile> sFiles;
static std::vector<TargetSyncProperty> sProperties;
static std::vector<double> sValues;
static std::vector<size_t> sValueSets;
static BitVector<> sChangedFiles;
static BitVector<> sChangedValueSets;
static bool sInvertAxis[eValueSetSubType_Num];
static bool sInitialized = false;


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
		if( TargetConfigFile::Value* aValuePtr =
				sFiles[mFileID].values.find(thePath) )
		{
			sValues[aValuePtr->valueIdx] = doubleFromString(theValue);
			sChangedValueSets.set(aValuePtr->setIdx);
			--mUnfoundCount;
		}
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

static FILETIME getFileLastModTime(const TargetConfigFile& theFile)
{
	WIN32_FILE_ATTRIBUTE_DATA aFileAttr;
	if( GetFileAttributesEx(theFile.pathW.c_str(),
			GetFileExInfoStandard, &aFileAttr) )
	{
		return aFileAttr.ftLastWriteTime;
	}

	return FILETIME();
}


static bool setFetchValueFromFile(
	TargetConfigSyncBuilder& theBuilder,
	const std::string& theValueName,
	const u16 theDestValueSetID,
	const EValueSetType theValueSetType,
	const EValueSetSubType theDestValueSetSubType)
{
	// Generate path from format for given sub-type
	std::string aConfigFilePath;
	aConfigFilePath.reserve(256);
	aConfigFilePath = theValueName;
	if( !theBuilder.valueFormatStrings[theDestValueSetSubType].empty() )
	{
		// Parse format string for <name> tag
		bool nameTagFound = false;
		aConfigFilePath =
			theBuilder.valueFormatStrings[theDestValueSetSubType];
		std::pair<std::string::size_type, std::string::size_type> aTagCoords =
			findStringTag(aConfigFilePath);
		while(aTagCoords.first != std::string::npos )
		{
			std::string aTag = aConfigFilePath.substr(
				aTagCoords.first + 1, aTagCoords.second - 2);
			if( condense(aTag) == kValueFormatNameTag )
			{
				aConfigFilePath.replace(
					aTagCoords.first,
					aTagCoords.second,
					theValueName);
				aTagCoords.second = theValueName.size();
				nameTagFound = true;
			}
			aTagCoords = findStringTag(
				aConfigFilePath,
				aTagCoords.first + aTagCoords.second);
		}
		if( !nameTagFound )
		{
			logError("Missing <%s> tag for format string '%s = %s'",
				lower(kValueFormatNameTag).c_str(),
				kValueFormatStringKeys[theDestValueSetSubType],
				aConfigFilePath.c_str());
			return false;
		}
	}
	// Extract file key from beginning of path up to first '.'
	const std::string& aFileKey = breakOffItemBeforeChar(aConfigFilePath, '.');
	if( aFileKey.empty() )
	{
		logError("Missing config file ID for path '%s' in sync property '%s'",
			aConfigFilePath.c_str(), theBuilder.debugString.c_str());
		return false;
	}
	const u16* aFileIDPtr = theBuilder.fileKeyToIDMap.find(condense(aFileKey));
	if( !aFileIDPtr )
	{
		// It may be intentional that syncing was disabled by not defining the
		// file to sync from, so report this only in the log file
		logToFile("Config file ID '%s' referenced in '%s' not found",
			aFileKey.c_str(), theBuilder.debugString.c_str());
		return false;
	}
	// Set this keys path as a value to look for when parsing this file
	TargetConfigFile::Value aDestValue;
	aDestValue.setIdx = theDestValueSetID;
	aDestValue.valueIdx =
		sValueSets[theDestValueSetID] +
		theDestValueSetSubType -
		kValueSetFirstIdx[theValueSetType];
	sFiles[*aFileIDPtr].values.setValue(aConfigFilePath, aDestValue);
	return true;
}


static bool setConfigFileLinks(
	TargetConfigSyncBuilder& theBuilder,
	TargetSyncProperty::Segment& thePropSegment,
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
			kValueSetFirstIdx[theValueSetType] + 1);
	}
	// Request fetch all related values for given function & value set
	bool isValidResult = true;
	#define fetchVal(x) \
		isValidResult = isValidResult && \
			setFetchValueFromFile( \
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
		fetchVal(eValueSetSubType_AlignX);
		fetchVal(eValueSetSubType_PosX);
		fetchVal(eValueSetSubType_SizeX);
		fetchVal(eValueSetSubType_PivotX);
		break;
	case eValueFunc_PosY:
	case eValueFunc_Top:
	case eValueFunc_CY:
	case eValueFunc_Bottom:
		fetchVal(eValueSetSubType_AlignY);
		fetchVal(eValueSetSubType_PosY);
		fetchVal(eValueSetSubType_SizeY);
		fetchVal(eValueSetSubType_PivotY);
		break;
	case eValueFunc_Width:
		fetchVal(eValueSetSubType_SizeX);
		break;
	case eValueFunc_Height:
		fetchVal(eValueSetSubType_SizeY);
		break;
	case eValueFunc_AlignX:
		fetchVal(eValueSetSubType_AlignX);
		break;
	case eValueFunc_AlignY:
		fetchVal(eValueSetSubType_AlignY);
		break;
	case eValueFunc_Scale:
		fetchVal(eValueSetSubType_Scale);
		break;
	}
	#undef fetchVal

	return isValidResult;
}


static inline EValueSetType funcToValueSetType(EValueFunction theFunc)
{
	return theFunc == eValueFunc_Base
		? eValueSetType_Single
		: eValueSetType_UIWindow;
}


static void parsePropertyValueTags(
	TargetConfigSyncBuilder& theBuilder,
	TargetSyncProperty& theProperty,
	std::string theDesc)
{
	std::pair<std::string::size_type, std::string::size_type> aTagCoords =
		findStringTag(theDesc);
	while(aTagCoords.first != std::string::npos )
	{
		TargetSyncProperty::Segment aSegment = TargetSyncProperty::Segment();
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
		// Set links from config files back to this segment
		if( !setConfigFileLinks(theBuilder, aSegment, aTag, aValueSetType) )
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
	std::vector<TargetSyncProperty::Segment>(theProperty.valueInserts)
		.swap(theProperty.valueInserts);
}


static EPropertyType extractPropertyType(const TargetSyncProperty& theProperty)
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


static bool parseTargetConfigFile(size_t theFileID)
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
	char aBuffer[kConfigFileBufferSize];
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


static inline double getSubTypeValue(
	const double* theValArray,
	EValueSetSubType theSubType)
{
	double result = theValArray[theSubType];
	switch(theSubType)
	{
	case eValueSetSubType_AlignX:
	case eValueSetSubType_AlignY:
		result = clamp(result, 0, 1.0);
		if( sInvertAxis[theSubType] )
			result = 1.0 - result;
		break;
	case eValueSetSubType_PivotX:
	case eValueSetSubType_PivotY:
		// For these what we really want is the offset needed to compensate
		// for the pivot's effect rather than the actual pivot value itself
		result = clamp(result, 0, 1.0);
		if( sInvertAxis[theSubType] )
			result = 1.0 - result;
		if( result )
		{
			result *= getSubTypeValue(
				theValArray,
				theSubType == eValueSetSubType_PivotX
					? eValueSetSubType_SizeX
					: eValueSetSubType_SizeY);
		}
		break;
	case eValueSetSubType_PosX:
	case eValueSetSubType_PosY:
		if( sInvertAxis[theSubType] )
			result = -result;
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
	sFolders.clear();
	sFiles.clear();
	sProperties.clear();
	TargetConfigSyncBuilder aBuilder;
	StringToValueMap<u16> aPathToIdxMap;

	// Fetch target config files potentially containing sync properties
	Profile::KeyValuePairs aKeyValueList;
	Profile::getAllKeys(kTargetConfigFilesPrefix, aKeyValueList);
	for(size_t i = 0; i < aKeyValueList.size(); ++i)
	{
		const std::string& aFilePath =
			upper(removePathParams(expandPathVars(
				aKeyValueList[i].second)));
		const u16 aFileID = aPathToIdxMap.findOrAdd(
			aFilePath, u16(sFiles.size()));
		if( aFileID >= sFiles.size() )
		{
			TargetConfigFile aNewConfigFile;
			aNewConfigFile.pathW = widen(aFilePath);
			aNewConfigFile.format = eConfigFileFormat_JSON; // TODO properly
			aNewConfigFile.lastModTime = FILETIME();
			sFiles.push_back(aNewConfigFile);
		}
		aBuilder.fileKeyToIDMap.setValue(
			condense(aKeyValueList[i].first), aFileID);
	}

	// Fetch value path formating data
	for(size_t i = 0; i < eValueSetSubType_Num; ++i)
	{
		aBuilder.valueFormatStrings[i] = Profile::getStr(
			std::string(kValueFormatStrPrefix) +
			kValueFormatStringKeys[i]);
		sInvertAxis[i] = Profile::getBool(
			std::string(kValueFormatInvertPrefix) +
			kValueFormatStringKeys[i]);
	}

	// Fetch sync property values to read from the config files
	aKeyValueList.clear();
	Profile::getAllKeys(kSyncPropertiesPrefix, aKeyValueList);
	for(size_t i = 0; i < aKeyValueList.size(); ++i)
	{
		aBuilder.debugString = aKeyValueList[i].first;
		aBuilder.debugString += " = ";
		aBuilder.debugString += aKeyValueList[i].second;
		// Separate key into section and property name by > character
		TargetSyncProperty aProperty;
		aProperty.section = condense(aKeyValueList[i].first);
		size_t aPos = aProperty.section.find('>');
		if( aPos == std::string::npos )
		{
			logError("Missing '>' between section and property name "
				"for sync property '%s'",
				aKeyValueList[i].first);
			continue;
		}
		parsePropertyValueTags(aBuilder, aProperty, aKeyValueList[i].second);
		if( aProperty.valueSetsUsed.none() )
			continue;
		aProperty.name = aProperty.section.substr(aPos+1);
		aProperty.section.resize(aPos);
		aProperty.type = extractPropertyType(aProperty);
		const u16 aPropertyID = u16(sProperties.size());
		sProperties.push_back(aProperty);
	}

	// Begin monitoring folders for changes to contained files w/ properites
	aPathToIdxMap.clear();
	for(u16 i = 0; i < sFiles.size(); ++i)
	{
		if( sFiles[i].values.empty() )
			continue;
		sFiles[i].values.trim();
		const std::string& aFolderPath = getFileDir(narrow(sFiles[i].pathW));
		const u16 aFolderID = aPathToIdxMap.findOrAdd(
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

	// Trim memory and resize structures
	for(u16 i = 0; i < sFolders.size(); ++i)
	{
		if( sFolders[i].fileIDs.size() < sFolders[i].fileIDs.capacity() )
			std::vector<u16>(sFolders[i].fileIDs).swap(sFolders[i].fileIDs);	
	}
	if( sFolders.size() < sFolders.capacity() )
		std::vector<TargetConfigFolder>(sFolders).swap(sFolders);
	if( sFiles.size() < sFiles.capacity() )
		std::vector<TargetConfigFile>(sFiles).swap(sFiles);
	if( sProperties.size() < sProperties.capacity() )
		std::vector<TargetSyncProperty>(sProperties).swap(sProperties);
	if( sValues.size() < sValues.capacity() )
		std::vector<double>(sValues).swap(sValues);
	if( sValueSets.size() < sValueSets.capacity() )
		std::vector<size_t>(sValueSets).swap(sValueSets);
	sChangedFiles.clearAndResize(sFiles.size());
	sChangedValueSets.clearAndResize(sValueSets.size());

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
	if( !sInitialized || gShutdown )
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

	for(int aFileID = sChangedFiles.firstSetBit();
		aFileID < sChangedFiles.size();
		aFileID = sChangedFiles.nextSetBit(aFileID+1))
	{
		if( parseTargetConfigFile(aFileID) )
			sChangedFiles.reset(aFileID);
	}

	if( sChangedFiles.none() && sChangedValueSets.any() )
	{
		bool propTypeChanged[ePropertyType_Num] = { };
		for(size_t aPropID = 0; aPropID < sProperties.size(); ++aPropID)
		{
			TargetSyncProperty& aProp = sProperties[aPropID];
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
				Profile::setStr(aProp.section, aProp.name, aValueStr, false);
				propTypeChanged[aProp.type] = true;
			}
		}
		if( propTypeChanged[ePropertyType_Unknown] ||
			propTypeChanged[ePropertyType_Hotspot] )
		{
			// TODO
			//InputMap::reloadAllHotspots();
			HotspotMap::reloadPositions();
		}
		if( propTypeChanged[ePropertyType_Unknown] ||
			propTypeChanged[ePropertyType_CopyIcon] ||
			propTypeChanged[ePropertyType_HUDElement] )
		{
			// TODO
			//HUD::reloadLayout();
		}
		if( propTypeChanged[ePropertyType_Unknown] ||
			propTypeChanged[ePropertyType_UIScale] )
		{
			WindowManager::updateUIScale();
		}
		sChangedValueSets.reset();
	}
}

} // TargetConfigSync
