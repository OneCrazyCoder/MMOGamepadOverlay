//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputMap.h"

#include "HotspotMap.h" // stringToHotspot()
#include "Profile.h"

namespace InputMap
{

// Uncomment this to print command assignments to debug window
//#define INPUT_MAP_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kInvalidID = 0xFFFF,
};

const char* kMainLayerSectionName = "Scheme";
const char* kLayerPrefix = "Layer.";
const char kComboLayerDeliminator = '+';
const char kSubMenuDeliminator = '.';
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
const char* kKeyBindsSectionName = "KeyBinds";
const char* kHotspotsSectionName = "Hotspots";
const char* k4DirCmdSuffix[] = { " Left", " Right", " Up", " Down" };
const std::string kActionOnlyPrefix = "Just";
const std::string kSignalCommandPrefix = "When";

const char* kSpecialNamedHotspots[] =
{
	"MouseLookStart",		// eSpecialHotspot_MouseLookStart
	"MouseHidden",			// eSpecialHotspot_MouseHidden
};
DBG_CTASSERT(ARRAYSIZE(kSpecialNamedHotspots) ==
			 eSpecialHotspot_Num - eSpecialHotspot_FirstNamed);

const char* kButtonActionPrefx[] =
{
	"",						// eBtnAct_Down
	"Press",				// eBtnAct_Press
	"Hold",					// eBtnAct_Hold
	"Tap",					// eBtnAct_Tap
	"Release",				// eBtnAct_Release
};
DBG_CTASSERT(ARRAYSIZE(kButtonActionPrefx) == eBtnAct_Num);

const char* kSpecialKeyBindNames[] =
{
	"",						// eSpecialKey_None
	"SwapWindowMode",		// eSpecialKey_SwapWindowMode
	"PasteText",			// eSpecialKey_PasteText
	"AutoRun",				// eSpecialKey_AutoRun
	"MoveForward",			// eSpecialKey_MoveF
	"MoveBack",				// eSpecialKey_MoveB
	"TurnLeft",				// eSpecialKey_TurnL
	"TurnRight",			// eSpecialKey_TurnR
	"StrafeLeft",			// eSpecialKey_StrafeL
	"StrafeRight",			// eSpecialKey_StrafeR
};
DBG_CTASSERT(ARRAYSIZE(kSpecialKeyBindNames) == eSpecialKey_Num);

enum EPropertyType
{
	// These 4 must be first so exactly match ECommandDir
	ePropType_MenuItemLeft,		// eCmdDir_L
	ePropType_MenuItemRight,	// eCmdDir_R
	ePropType_MenuItemUp,		// eCmdDir_U
	ePropType_MenuItemDown,		// eCmdDir_D

	// These are specifically section names
	ePropType_Hotspots,
	ePropType_KeyBinds,
	ePropType_Scheme,
	ePropType_Layer,
	ePropType_Menu,
	ePropType_HUD,

	// Normal properties
	ePropType_Mouse,
	ePropType_Hotspot,
	ePropType_Parent,
	ePropType_Priority,
	ePropType_Auto,
	ePropType_Back,
	ePropType_Type,
	ePropType_KBArray,
	ePropType_GridWidth,
	ePropType_Label,

	// Special-case property groups
	ePropType_MenuItemNumber,
	ePropType_Filler,

	ePropType_Num
};

EPropertyType propKeyToType(const std::string& theName)
{
	struct NameToEnumMapper
	{
		typedef StringToValueMap<EPropertyType, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			struct { const char* str; EPropertyType val; } kEntries[] = {
				{ "L",				ePropType_MenuItemLeft	},
				{ "R",				ePropType_MenuItemRight	},
				{ "U",				ePropType_MenuItemUp	},
				{ "D",				ePropType_MenuItemDown	},
				{ "Hotspots",		ePropType_Hotspots		},
				{ "Keybinds",		ePropType_KeyBinds		},
				{ "Scheme",			ePropType_Scheme		},
				{ "Layer",			ePropType_Layer			},
				{ "Menu",			ePropType_Menu			},
				{ "HUD",			ePropType_HUD			},
				{ "Mouse",			ePropType_Mouse			},
				{ "Cursor",			ePropType_Mouse			},
				{ "Hotspot",		ePropType_Hotspot		},
				{ "Hot",			ePropType_Hotspot		},
				{ "Spot",			ePropType_Hotspot		},
				{ "Point",			ePropType_Hotspot		},
				{ "ParenT",			ePropType_Parent		},
				{ "Priority",		ePropType_Priority		},
				{ "Auto",			ePropType_Auto			},
				{ "Back",			ePropType_Back			},
				{ "Type",			ePropType_Type			},
				{ "Style",			ePropType_Type			},
				{ "KeyBindArray",	ePropType_KBArray		},
				{ "KBArray",		ePropType_KBArray		},
				{ "Array",			ePropType_KBArray		},
				{ "GridWidth",		ePropType_GridWidth		},
				{ "Label",			ePropType_Label			},
				{ "Title",			ePropType_Label			},
				{ "Name",			ePropType_Label			},
				{ "String",			ePropType_Label			},
				{ "to",				ePropType_Filler		},
				{ "at",				ePropType_Filler		},
				{ "on",				ePropType_Filler		},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EPropertyType* result = sNameToEnumMapper.map.find(theName);
	return
		result ?				*result :
		isAnInteger(theName) ?	ePropType_MenuItemNumber :
		/*otherwise*/			ePropType_Num;
}


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ZERO_INIT(KeyBindArrayEntry)
{
	u16 keyBindID;
	u16 hotspotID;
};
typedef std::vector<KeyBindArrayEntry> KeyBindArray;

struct MenuItem
{
	std::string label;
	std::string altLabel;
	Command cmd;

	std::string debugLabel()
	{
		if( label.empty() ) return "<unnamed>";
		if( altLabel.empty() ) return label;
		return label + " (" + altLabel + ")";
	}
};

struct Menu
{
	std::string name;
	std::string label;
	std::vector<MenuItem> items;
	MenuItem dirItems[eCmdDir_Num];
	Command autoCommand;
	Command backCommand;
	u16 parentMenuID;
	u16 rootMenuID;
	u16 hudElementID;
	u16 hotspotArrayID;
	u8 gridWidth;

	Menu() :
		parentMenuID(kInvalidID),
		rootMenuID(kInvalidID),
		hudElementID(kInvalidID),
		hotspotArrayID(kInvalidID),
		gridWidth(0)
	{}
};

struct ZERO_INIT(HUDElement)
{
	std::string name; // TODO - Remove?
	EHUDType type;
	u16 menuID;
	u16 hotspotID;
	u16 keyBindArrayID;
	// Visual details will be parsed by HUD module

	HUDElement() :
		type(eHUDType_Num),
		menuID(kInvalidID),
		keyBindArrayID(kInvalidID)
	{}
};

struct ButtonActions
{
	//std::string label[eBtnAct_Num]; // TODO
	Command cmd[eBtnAct_Num];
	void initIfEmpty()
	{
		for(size_t i = 0; i < eBtnAct_Num; ++i)
		{
			if( cmd[i].type == eCmdType_Empty )
				cmd[i].type = eCmdType_Unassigned;
		}
	}
};
typedef VectorMap<EButton, ButtonActions> ButtonActionsMap;

struct ZERO_INIT(ControlsLayer)
{
	std::string name;
	ButtonActionsMap buttonMap;
	VectorMap<u16, Command> signalCommands;
	BitVector<32> showHUD;
	BitVector<32> hideHUD;
	BitVector<32> enableHotspots;
	BitVector<32> disableHotspots;
	EMouseMode mouseMode;
	u16 parentLayer;
	s8 priority;
	bool isComboLayer;
};

struct ZERO_INIT(HotspotRange)
{
	s16 xOffset, yOffset;
	u16 firstIdx;
	u16 count : 12;
	u16 hasOwnXAnchor : 1;
	u16 hasOwnYAnchor : 1;
	u16 offsetFromPrev : 1;
	u16 removed : 1;

	u16 lastIdx() const { return firstIdx + count - 1; }
	bool operator<(const HotspotRange& rhs) const
	{ return firstIdx < rhs.firstIdx; }
};

struct ZERO_INIT(HotspotArray)
{
	std::string name; // TODO - remove (just use key)?
	std::vector<HotspotRange> ranges;
	float offsetScale;
	u16 anchorIdx; // set to first hotspot idx - 1 if !hasAnchor
	u16 hasAnchor : 1;
	u16 size : 15; // not including anchor

	HotspotArray() : offsetScale(1.0) {}
	bool operator<(const HotspotArray& rhs) const
	{ return (anchorIdx + (hasAnchor ? 0 : 1)) <
		(rhs.anchorIdx + (rhs.hasAnchor ? 0 : 1)); }
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<Hotspot> sHotspots;
static StringToValueMap<HotspotArray> sHotspotArrays;
static StringToValueMap<bool, u16, true> sCmdStrings;
static StringToValueMap<Command> sKeyBinds;
static StringToValueMap<KeyBindArray> sKeyBindArrays;
static StringToValueMap<ControlsLayer> sLayers;
static VectorMap<std::pair<u16, u16>, u16> sComboLayers;
static StringToValueMap<Menu> sMenus;
static StringToValueMap<HUDElement> sHUDElements;
static VectorMap<std::pair<u16, EButton>, u32> sButtonHoldTimes;
static u32 sDefaultButtonHoldTime = 400;
static std::string sSectionPrintName;
static std::string sPropertyPrintName;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef INPUT_MAP_DEBUG_PRINT
#define mapDebugPrint(...) debugPrint("InputMap: " __VA_ARGS__)
#else
#define mapDebugPrint(...) ((void)0)
#endif

static u16 checkForComboVKeyName(std::string theKeyName)
{
	u16 result = 0;
	std::string aModKeyName;
	aModKeyName.push_back(theKeyName[0]);
	theKeyName = theKeyName.substr(1);
	while(theKeyName.size() > 1)
	{
		aModKeyName.push_back(theKeyName[0]);
		theKeyName = theKeyName.substr(1);
		const u8 aModKey = keyNameToVirtualKey(aModKeyName);
		if( aModKey != 0 &&
			(aModKey == VK_SHIFT ||
			 aModKey == VK_CONTROL ||
			 aModKey == VK_MENU ||
			 aModKey == VK_LWIN ||
			 aModKey == kVKeyForceRelease) )
		{// Found a valid modifier key
			switch(aModKey)
			{
			case VK_SHIFT:
				result |= kVKeyShiftFlag;
				break;
			case VK_CONTROL:
				result |= kVKeyCtrlFlag;
				break;
			case VK_MENU:
				result |= kVKeyAltFlag;
				break;
			case VK_LWIN:
				result |= kVKeyWinFlag;
				break;
			}
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(theKeyName))
			{// We have a valid key combo!
				result |= aMainKey;
				return result;
			}
			// Perhaps remainder is another mod+key, like ShiftCtrlA?
			if( u16 aPartialVKey = checkForComboVKeyName(theKeyName) )
			{
				result |= aPartialVKey;
				return eResult_Ok;
			}
			// No main key found to go with modifier key
			result = 0;
		}
	}

	// No valid modifier key found
	return 0;
}


static EResult checkForVKeySeqPause(
	const std::string& theKeyName,
	std::string& out,
	bool timeOnly = false)
{
	size_t aStrPos = 0;
	if( !timeOnly )
	{
		if( theKeyName.size() <= 1 )
			return eResult_NotFound;

		// See if starts with P(ause), W(ait), or D(elay)
		switch(theKeyName[0])
		{
		case 'P': case 'D': case 'W': aStrPos = 1; break;
		default: return eResult_NotFound;
		}

		if( theKeyName.compare(0, 5, "PAUSE") == 0 )
			aStrPos = 5;
		else if( theKeyName.compare(0, 5, "DELAY") == 0 )
			aStrPos = 5;
		else if( theKeyName.compare(0, 4, "WAIT") == 0 )
			aStrPos = 4;
		// If whole word is a valid name but nothing else remains,
		// then it is valid but we need a second word for timeOnly
		if( aStrPos == theKeyName.size() )
			return eResult_Incomplete;
	}

	typedef unsigned int u32;
	u32 aTime = 0;
	for(; aStrPos < theKeyName.size(); ++aStrPos)
	{
		const u8 c = theKeyName[aStrPos];
		// Check if is a valid integer digit
		if( c < '0' || c > '9' )
			return eResult_NotFound;
		const u32 aDigit = c - '0';
		aTime *= 10;
		aTime += aDigit;
		if( aTime > 0x3FFF )
		{
			aTime = 0x3FFF;
			logError("Pause time in a key sequence "
				"can not exceed 16 seconds (from %s/%s = )",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str());
			break;
		}
	}

	// Delay of 0 is technically valid but doesn't add to the sequence
	if( aTime == 0 )
		return eResult_Ok;

	// Encode the special-case VK_PAUSE key and the delay amount as a 14-bit
	// number using the 2 bytes after it in the string (making sure each byte
	// has highest bit set so can't end up with either being 0 and acting as
	// a terminator for the string).
	out.push_back(VK_PAUSE);
	out.push_back(u8(((aTime >> 7) & 0x7F) | 0x80));
	out.push_back(u8((aTime & 0x7F) | 0x80));

	// Success!
	return eResult_Ok;
}


static u16 getHotspotID(const std::string& theName)
{
	std::string anArrayName = theName;
	const int anArrayIdx = breakOffIntegerSuffix(anArrayName);
	if( anArrayIdx <= 0 )
	{// Get the anchor hotspot of the array with full matching name
		if( HotspotArray* aHotspotArray = sHotspotArrays.find(theName) )
			return aHotspotArray->anchorIdx;
	}
	if( HotspotArray* aHotspotArray = sHotspotArrays.find(anArrayName) )
		return aHotspotArray->anchorIdx + anArrayIdx;
	return 0;
}


static EResult checkForVKeyHotspotPos(
	const std::string& theKeyName,
	std::string& out,
	bool afterClickCommand)
{
	switch(propKeyToType(theKeyName))
	{
	case ePropType_Mouse:
	case ePropType_Hotspot:
	case ePropType_Filler:
		// Keep checking past these for an actual hotspot name
		return eResult_Incomplete;
	}

	u16 aHotspotIdx = getHotspotID(theKeyName);
	if( !aHotspotIdx )
		return eResult_NotFound;

	std::string suffix;
	if( afterClickCommand )
	{// Need to inject the jump command to before the click
		suffix.insert(suffix.begin(), out[out.size()-1]);
		out.erase(out.size()-1);
		while(!out.empty() &&
			(out[out.size()-1] == VK_SHIFT ||
			 out[out.size()-1] == VK_CONTROL ||
			 out[out.size()-1] == VK_MENU ||
			 out[out.size()-1] == VK_LWIN ||
			 out[out.size()-1] == kVKeyForceRelease))
		{
			suffix.insert(suffix.begin(), out[out.size()-1]);
			out.erase(out.size()-1);
		}
		out.push_back(kVKeyMouseJump);
	}

	// Encode the hotspot ID into 14-bit as in checkForVKeySeqPause()
	out.push_back(u8(((aHotspotIdx >> 7) & 0x7F) | 0x80));
	out.push_back(u8((aHotspotIdx & 0x7F) | 0x80));

	// Add back in the actual click if had to filter it out
	out += suffix;

	return eResult_Ok;
}


static u16 namesToVKey(const std::vector<std::string>& theNames)
{
	u16 result = 0;

	for(int aNameIdx = 0; aNameIdx < theNames.size(); ++aNameIdx)
	{
		if( result & kVKeyMask )
		{// Nothing else should be found after first non-modifier key
			result = 0;
			break;
		}
		const std::string& aName = upper(theNames[aNameIdx]);
		DBG_ASSERT(!aName.empty());
		const u8 aVKey = keyNameToVirtualKey(aName);
		switch(aVKey)
		{
		case 0:
			// Check if it's a modifier+key in one word like Shift2 or Alt1
			result = checkForComboVKeyName(aName);
			if( !result )
				return result;
			break;
		case VK_SHIFT:
			result |= kVKeyShiftFlag;
			break;
		case VK_CONTROL:
			result |= kVKeyCtrlFlag;
			break;
		case VK_MENU:
			result |= kVKeyAltFlag;
			break;
		case VK_LWIN:
			result |= kVKeyWinFlag;
			break;
		case kVKeyForceRelease:
		case kVKeyMouseJump:
			result = 0;
			return result;
		default:
			result |= aVKey;
			break;
		}
	}

	// If purely mod keys, add dummy base key
	if( result && !(result & kVKeyMask) )
		result |= kVKeyModKeyOnlyBase;

	return result;
}


static std::string namesToVKeySequence(
	const std::vector<std::string>& theNames)
{
	std::string result;

	if( theNames.empty() )
		return result;

	bool expectingWaitTime = false;
	bool expectingJumpPos = false;
	for(int aNameIdx = 0; aNameIdx < theNames.size(); ++aNameIdx)
	{
		const std::string& aName = upper(theNames[aNameIdx]);
		DBG_ASSERT(!aName.empty());
		if( expectingWaitTime )
		{
			if( checkForVKeySeqPause(aName, result, true) != eResult_Ok )
			{// Didn't get wait time as expected - abort!
				result.clear();
				return result;
			}
			expectingWaitTime = false;
			continue;
		}
		else if( expectingJumpPos )
		{
			const EResult aResult = checkForVKeyHotspotPos(
				aName, result, false);
			if( aResult == eResult_Incomplete )
				continue;
			if( aResult == eResult_Ok )
			{
				expectingJumpPos = false;
				continue;
			}
			// Didn't get jump pos as expected - abort!
			result.clear();
			return result;
		}
		const u8 aVKey = keyNameToVirtualKey(aName);
		if( aVKey == 0 )
		{
			// If previous key was a mouse button, check for follow-up hotspot
			EResult aResult;
			if( !result.empty() &&
				(result[result.size()-1] == VK_LBUTTON ||
				 result[result.size()-1] == VK_MBUTTON ||
				 result[result.size()-1] == VK_RBUTTON)  )
			{
				aResult = checkForVKeyHotspotPos(
					aName, result, true);
				if( aResult != eResult_NotFound )
					continue;
			}

			// Check if it's a pause/delay/wait command
			aResult = checkForVKeySeqPause(aName, result);
			// Incomplete result means it WAS a wait, now need the time
			if( aResult == eResult_Incomplete )
				expectingWaitTime = true;
			if( aResult != eResult_NotFound )
				continue;

			// Check if it is the name of a key bind
			const u16 aKeyBindID = sKeyBinds.findIndex(aName);
			if( aKeyBindID < sKeyBinds.size() )
			{
				// Encode the ID into 14-bit as in checkForVKeySeqPause()
				result.push_back(kVKeyTriggerKeyBind);
				result.push_back(u8(((aKeyBindID >> 7) & 0x7F) | 0x80));
				result.push_back(u8((aKeyBindID & 0x7F) | 0x80));
				continue;
			}

			// Check if it's a modifier+key in one word like Shift2 or Alt1
			if( u16 aComboVKey = checkForComboVKeyName(aName) )
			{
				if( aComboVKey & kVKeyShiftFlag )
					result.push_back(VK_SHIFT);
				if( aComboVKey & kVKeyCtrlFlag )
					result.push_back(VK_CONTROL);
				if( aComboVKey & kVKeyAltFlag )
					result.push_back(VK_MENU);
				if( aComboVKey & kVKeyWinFlag )
					result.push_back(VK_LWIN);
				result.push_back(u8(aComboVKey & kVKeyMask));
				continue;
			}

			// Can't figure this word out at all, abort!
			result.clear();
			return result;
		}
		else if( aVKey == kVKeyMouseJump )
		{
			// Get name of hotspot to jump cursor to next
			expectingJumpPos = true;
			result.push_back(aVKey);
		}
		else
		{
			result.push_back(aVKey);
		}
	}

	return result;
}


static Command parseChatBoxMacro(std::string theString)
{
	DBG_ASSERT(!theString.empty());
	Command result;

	if( theString[0] != '/' && theString[0] != '>' )
		return result;

	// Treat as Do Nothing command if nothing after the / or >
	if( theString.length() <= 1 )
	{
		result.type = eCmdType_DoNothing;
		return result;
	}

	// Handle multi-line macros such as ">Hello!\n /wave"
	std::string aTmpStr;
	for(size_t i = 1; i < theString.size()-1; ++i)
	{
		// Only care about character sequence '\' followed by 'n'
		if( theString[i] != '\\' || ::tolower(theString[i+1]) != 'n' )
			continue;
		// Chop into two strings temporarily, before and after "\\n",
		// and trim any whitespace that was surrounding it
		aTmpStr = trim(theString.substr(i+2));
		theString.resize(i); theString = trim(theString);
		// Strip extra redundant "\\n" from start of second half
		while(aTmpStr.size() >= 2 && aTmpStr[0] == '\\' && aTmpStr[1] == 'n' )
			aTmpStr = trim(aTmpStr.substr(2));
		if( aTmpStr.empty() )
			break;

		// Add '\r' to have macro send carriage return for this line
		theString.push_back('\r');
		// If second segment is missing '>' (or '/'), add it now
		if( aTmpStr[0] != '/' && aTmpStr[0] != '>' )
			theString.push_back('>');
		// Make sure loop continues from correct position after trims/adds
		i = theString.size();
		// Join the two string segments back together
		theString += aTmpStr;
	}

	// Add final '\r' to send last line of chat box text
	theString.push_back('\r');
	result.type = eCmdType_ChatBoxString;
	result.stringID = sCmdStrings.findOrAddIndex(theString);

	return result;
}


static bool createEmptyLayer(
	const Profile::SectionsMap& theSectionsMap,
	u16 theSectionID, const std::string& thePrefix, void*)
{
	const std::string& aSectionName =
		theSectionsMap.keys()[theSectionID];
	const size_t aPostPrefixPos = posAfterPrefix(aSectionName, thePrefix);
	const std::string& aLayerName = aSectionName.substr(aPostPrefixPos);
	DBG_ASSERT(!aLayerName.empty());
	ControlsLayer& aLayer = sLayers.findOrAdd(aLayerName);
	if( aLayer.name.empty() )
		aLayer.name = aLayerName;
	aLayer.isComboLayer =
		aLayerName.find(kComboLayerDeliminator) != std::string::npos;
	return true;
}


static void linkComboLayers(u16 theLayerID)
{
	ControlsLayer& theLayer = sLayers.vals()[theLayerID];
	if( !theLayer.isComboLayer )
		return;

	std::string aSecondLayerName = sLayers.vals()[theLayerID].name;
	std::string aFirstLayerName = breakOffItemBeforeChar(
		aSecondLayerName, kComboLayerDeliminator);
	if( aFirstLayerName.empty() || aSecondLayerName.empty() )
		return;

	const u16 aFirstLayerID = sLayers.findIndex(aFirstLayerName);
	if( aFirstLayerID >= sLayers.size() )
	{
		logError("Base layer [%s] not found for combo layer [%s]",
			(kLayerPrefix + aFirstLayerName).c_str(),
			(kLayerPrefix + sLayers.vals()[theLayerID].name).c_str());
		return;
	}

	u16 aSecondLayerID = sLayers.findIndex(aSecondLayerName);
	if( aSecondLayerID >= sLayers.size() )
	{
		// Second layer may actually be a combo layer itself, including
		// possibly a combo layer that is not explicitly declared in
		// the profile but needs to be created purely for creating combo
		// layers of any arbitrary number of base layers.
		const bool secondLayerIsComboLayer =
			aSecondLayerName.find(kComboLayerDeliminator) != std::string::npos;
		if( !secondLayerIsComboLayer )
		{
			logError("Base layer [%s] not found for combo layer [%s]",
				(kLayerPrefix + aSecondLayerName).c_str(),
				(kLayerPrefix + sLayers.vals()[theLayerID].name).c_str());
			return;
		}
		// Create placeholder second combo layer
		ControlsLayer& aSecondLayer =
			sLayers.setValue(aSecondLayerName, ControlsLayer());
		aSecondLayer.name = aSecondLayerName;
		aSecondLayer.isComboLayer = true;
	}

	if( aFirstLayerID == aSecondLayerID )
	{
		logError("Specified same layer twice in combo layer name '%s'!",
			sLayers.vals()[theLayerID].name.c_str());
		return;
	}

	// Link combo layer to its base layers
	std::pair<u16, u16> aComboLayerKey(aFirstLayerID, aSecondLayerID);
	sComboLayers.addPair(aComboLayerKey, theLayerID);
}


static bool createEmptyHUDElement(
	const Profile::SectionsMap& theSectionsMap,
	u16 theSectionID, const std::string& thePrefix, void*)
{
	const std::string& aSectionName =
		theSectionsMap.keys()[theSectionID];
	const size_t aPostPrefixPos = posAfterPrefix(aSectionName, thePrefix);
	const std::string& aHUDElementName = aSectionName.substr(aPostPrefixPos);
	DBG_ASSERT(!aHUDElementName.empty());
	HUDElement& aHUDElement =
		sHUDElements.findOrAdd(aHUDElementName);
	if( aHUDElement.name.empty() )
		aHUDElement.name = aHUDElementName;
	return true;
}


static bool createEmptyMenu(
	const Profile::SectionsMap& theSectionsMap,
	u16 theSectionID, const std::string& thePrefix, void*)
{
	const std::string& aSectionName =
		theSectionsMap.keys()[theSectionID];
	const size_t aPostPrefixPos = posAfterPrefix(aSectionName, thePrefix);
	const std::string& aMenuName = aSectionName.substr(aPostPrefixPos);
	DBG_ASSERT(!aMenuName.empty());
	Menu& aMenu = sMenus.findOrAdd(aMenuName);
	if( aMenu.name.empty() )
		aMenu.name = aMenuName;
	return true;
}


static bool setMenuAsChildOf(
	const StringToValueMap<Menu>&,
	u16 theChildMenuID, const std::string& thePrefix, void* theParentMenuIDPtr)
{
	Menu& aSubMenu = sMenus.vals()[theChildMenuID];
	const std::string& aPotentialLabel =
		aSubMenu.name.substr(posAfterPrefix(aSubMenu.name, thePrefix));
	if( aPotentialLabel.empty() )
		return true;
	if( aSubMenu.label.empty() ||
		aSubMenu.label.size() > aPotentialLabel.size() )
	{
		aSubMenu.parentMenuID = *((u16*)theParentMenuIDPtr);
		aSubMenu.label = aPotentialLabel;
	}

	return true;
}


static void linkMenuToSubMenus(u16 theMenuID)
{
	// Find all menus whose key starts with this menu's key (and . at end)
	const std::string& aPrefix =
		sMenus.keys()[theMenuID] + kSubMenuDeliminator;
	sMenus.findAllWithPrefix(aPrefix, setMenuAsChildOf, &theMenuID);
}


static void setupRootMenu(u16 theMenuID)
{
	Menu& theMenu = sMenus.vals()[theMenuID];
	if( theMenu.parentMenuID == kInvalidID )
	{
		// Is a root menu
		theMenu.rootMenuID = theMenuID;		
		// Create as a HUD element as well
		const u16 aHUDElementID = sHUDElements.findOrAddIndex(
			sMenus.keys()[theMenuID]);
		theMenu.hudElementID = aHUDElementID;
		sHUDElements.vals()[aHUDElementID].name =
			theMenu.name;
		sHUDElements.vals()[aHUDElementID].menuID = theMenuID;
	}
	else
	{
		// Identify root menu
		u16 aRootMenuID = theMenu.parentMenuID;
		while(sMenus.vals()[aRootMenuID].parentMenuID < sMenus.size())
			aRootMenuID = sMenus.vals()[aRootMenuID].parentMenuID;
		theMenu.rootMenuID = aRootMenuID;
	}
}


static u16 getRootMenuID(const std::string& theName)
{
	u16 result = sMenus.findIndex(theName);
	if( result < sMenus.size() &&
		sMenus.vals()[result].parentMenuID < sMenus.size() )
	{// Found a menu but it's a sub-menu
		result = kInvalidID;
	}
	return result;
}


static u16 getMenuID(std::string theMenuName, u16 theParentMenuID)
{
	DBG_ASSERT(!theMenuName.empty());
	DBG_ASSERT(theParentMenuID < sMenus.size());

	std::string aParentPath = sMenus.keys()[theParentMenuID];
	if( theMenuName[0] == '.' )
	{// Starting with '.' signals want to treat "grandparent" as the parent
		theParentMenuID = sMenus.vals()[theParentMenuID].parentMenuID;
		if( theParentMenuID >= sMenus.size() )
			return kInvalidID;
		aParentPath = sMenus.keys()[theParentMenuID];
		// Name being ".." means treat this as direct alias to grandparent menu
		if( theMenuName == ".." )
			return theParentMenuID;
		// Remove leading '.'
		theMenuName = theMenuName.substr(1);
	}

	return sMenus.findIndex(aParentPath + "." + theMenuName);
}


static Command wordsToSpecialCommand(
	const std::vector<std::string>& theWords,
	bool allowButtonActions = false,
	bool allowHoldActions = false)
{
	// Can't allow hold actions if don't also allow button actions
	DBG_ASSERT(!allowHoldActions || allowButtonActions);
	Command result;
	if( theWords.empty() )
		return result;

	// Almost all commands require more than one "word", even if only one of
	// words is actually a command key word (thus can force a keybind to be
	// used instead of a command by specifying the keybind as a single word).
	// The exception are the "nothing" and "unassigned" key words.
	if( theWords.size() <= 1 )
	{
		ECommandKeyWord aKeyWordID = commandWordToID(theWords[0]);
		if( aKeyWordID != eCmdWord_Nothing )
			return result;
	}

	// Find all key words that are actually included and their positions
	static VectorMap<ECommandKeyWord, size_t> sKeyWordMap;
	sKeyWordMap.reserve(8);
	sKeyWordMap.clear();
	BitArray<eCmdWord_Num> keyWordsFound = { 0 };
	BitArray<eCmdWord_Num> allowedKeyWords = { 0 };
	const std::string* anIgnoredWord = null;
	const std::string* anIntegerWord = null;
	const std::string* aSecondLayerName = null;
	result.wrap = false;
	result.count = 1;
	for(size_t i = 0; i < theWords.size(); ++i)
	{
		ECommandKeyWord aKeyWordID = commandWordToID(theWords[i]);
		// Convert LeftWrap/NextNoWrap/etc into just dir & wrap flag
		switch(aKeyWordID)
		{
		case eCmdWord_LeftWrap:
			aKeyWordID = eCmdWord_Left;
			result.wrap = true;
			break;
		case eCmdWord_LeftNoWrap:
			aKeyWordID = eCmdWord_Left;
			result.wrap = false;
			break;
		case eCmdWord_RightWrap:
			aKeyWordID = eCmdWord_Right;
			result.wrap = true;
			break;
		case eCmdWord_RightNoWrap:
			aKeyWordID = eCmdWord_Right;
			result.wrap = false;
			break;
		case eCmdWord_UpWrap:
			aKeyWordID = eCmdWord_Up;
			result.wrap = true;
			break;
		case eCmdWord_UpNoWrap:
			aKeyWordID = eCmdWord_Up;
			result.wrap = false;
			break;
		case eCmdWord_DownWrap:
			aKeyWordID = eCmdWord_Down;
			result.wrap = true;
			break;
		case eCmdWord_DownNoWrap:
			aKeyWordID = eCmdWord_Down;
			result.wrap = false;
			break;
		case eCmdWord_With:
			// Special case for "Replace <name> with <name>"
			if( i < theWords.size() - 1 &&
				keyWordsFound.test(eCmdWord_Replace) )
			{
				aSecondLayerName = &theWords[i+1];
				allowedKeyWords = keyWordsFound;
			}
			aKeyWordID = eCmdWord_Filler;
			break;
		}
		switch(aKeyWordID)
		{
		case eCmdWord_Filler:
			break;
		case eCmdWord_Ignored:
			anIgnoredWord = &theWords[i];
			break;
		case eCmdWord_Wrap:
			result.wrap = true;
			anIgnoredWord = &theWords[i];
			break;
		case eCmdWord_NoWrap:
			result.wrap = false;
			anIgnoredWord = &theWords[i];
			break;
		case eCmdWord_Integer:
			anIntegerWord = &theWords[i];
			result.count = s16(clamp(
				intFromString(theWords[i]),
				result.count, 0x7FFF));
			// fall through
		case eCmdWord_Unknown:
			// Not allowed more than once per command, since
			// these might actually be different values
			if( keyWordsFound.test(aKeyWordID) )
			{
				// Exception: single allowed duplicate
				if( allowedKeyWords.test(aKeyWordID) )
					allowedKeyWords.reset();
				else
					return result;
			}
			// fall through
		default:
			// Don't add duplicate keys to the map
			if( !keyWordsFound.test(aKeyWordID) )
			{
				keyWordsFound.set(aKeyWordID);
				sKeyWordMap.addPair(aKeyWordID, i);
			}
			break;
		}
	}
	if( sKeyWordMap.empty() )
		return result;
	sKeyWordMap.sort();
	// If have no "unknown" word (layer name/etc), use "ignored" word as one
	// This is the only difference between "ignored" and "filler" words
	if( !keyWordsFound.test(eCmdWord_Unknown) &&
		keyWordsFound.test(eCmdWord_Ignored) )
	{
		keyWordsFound.set(eCmdWord_Unknown);
		sKeyWordMap.setValue(eCmdWord_Unknown,
			sKeyWordMap.findOrAdd(eCmdWord_Ignored));
	}
	keyWordsFound.reset(eCmdWord_Ignored);

	// Find a command by checking for specific key words + allowed related
	// words (but no extra words beyond that, besides fillers)

	// "= [Do] Nothing"
	if( keyWordsFound.test(eCmdWord_Nothing) && keyWordsFound.count() == 1)
	{
		result.type = eCmdType_DoNothing;
		return result;
	}

	// "= [Change] Profile"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Change);
	allowedKeyWords.set(eCmdWord_Replace);
	allowedKeyWords.set(eCmdWord_Profile);
	if( keyWordsFound.test(eCmdWord_Profile) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_ChangeProfile;
		return result;
	}

	// "= [Edit] Layout"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Change);
	allowedKeyWords.set(eCmdWord_Edit);
	allowedKeyWords.set(eCmdWord_Layout);
	if( keyWordsFound.test(eCmdWord_Layout) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_EditLayout;
		return result;
	}

	// "= Close App"
	if( keyWordsFound.test(eCmdWord_Close) &&
		keyWordsFound.test(eCmdWord_App) &&
		keyWordsFound.count() == 2 )
	{
		result.type = eCmdType_QuitApp;
		return result;
	}

	// "= Lock Movement"
	if( keyWordsFound.test(eCmdWord_Lock) &&
		keyWordsFound.test(eCmdWord_Move) &&
		keyWordsFound.count() == 2 )
	{
		result.type = eCmdType_StartAutoRun;
		result.multiDirAutoRun = true;
		return result;
	}

	// "= Move 'Mouse|Cursor' to <aHotspotName>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Move);
	allowedKeyWords.set(eCmdWord_Mouse);
	if( keyWordsFound.test(eCmdWord_Move) &&
		keyWordsFound.test(eCmdWord_Mouse) &&
		!keyWordsFound.test(eCmdWord_Left) &&
		!keyWordsFound.test(eCmdWord_Right) &&
		!keyWordsFound.test(eCmdWord_Up) &&
		!keyWordsFound.test(eCmdWord_Down) &&
		(keyWordsFound & ~allowedKeyWords).count() == 1 )
	{
		allowedKeyWords = keyWordsFound;
		allowedKeyWords.reset(eCmdWord_Move);
		allowedKeyWords.reset(eCmdWord_Mouse);
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			sKeyWordMap.find(ECommandKeyWord(
				allowedKeyWords.firstSetBit()));
		if( itr != sKeyWordMap.end() )
		{
			u16 aHotspotID = getHotspotID(theWords[itr->second]);
			if( aHotspotID )
			{
				result.type = eCmdType_MoveMouseToHotspot;
				result.hotspotID = aHotspotID;
				return result;
			}
		}
	}

	// "= Remove [this] Layer"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Layer);
	allowedKeyWords.set(eCmdWord_Remove);
	if( allowButtonActions &&
		keyWordsFound.test(eCmdWord_Remove) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_RemoveControlsLayer;
		// Since can't remove layer 0 (main scheme), 0 acts as a flag
		// meaning to remove calling layer instead
		result.layerID = 0;
		return result;
	}

	// The remainng Layer-related commands need one extra word that is
	// not a key word related to layers, which will be the name of the
	// layer (likely, but not always, the eCmdWord_Unknown entry).
	// Start by defining what key words are related to layer commands
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Layer);
	allowedKeyWords.set(eCmdWord_Add);
	allowedKeyWords.set(eCmdWord_Remove);
	allowedKeyWords.set(eCmdWord_Hold);
	allowedKeyWords.set(eCmdWord_Replace);
	allowedKeyWords.set(eCmdWord_Toggle);
	// Directionals aren't layer-related but also not allowed as layer names
	allowedKeyWords.set(eCmdWord_Left);
	allowedKeyWords.set(eCmdWord_Right);
	allowedKeyWords.set(eCmdWord_Up);
	allowedKeyWords.set(eCmdWord_Down);
	// If have aSecondLayerName, make sure it isn't one of the above
	if( aSecondLayerName )
	{
		allowedKeyWords.set(eCmdWord_Filler);
		ECommandKeyWord aKeyWordID = commandWordToID(*aSecondLayerName);
		while(aSecondLayerName && allowedKeyWords.test(aKeyWordID))
		{
			if( aSecondLayerName == &theWords.back() )
			{
				aSecondLayerName = null;
				break;
			}
			++aSecondLayerName;
			aKeyWordID = commandWordToID(*aSecondLayerName);
		}
		allowedKeyWords.reset(eCmdWord_Filler);
	}
	// Convert allowedKeyWords into all non-layer-related words found
	allowedKeyWords = keyWordsFound & (~allowedKeyWords);
	// If no extra words found, default to anIgnoredWord
	const std::string* aLayerName = anIgnoredWord;
	// If no ignored word either, default to anIntegerWord
	if( !aLayerName ) aLayerName = anIntegerWord;
	if( allowedKeyWords.count() == 1 ||
		(aSecondLayerName && allowedKeyWords.count() == 2) )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			sKeyWordMap.find(ECommandKeyWord(
				allowedKeyWords.firstSetBit()));
		if( itr != sKeyWordMap.end() )
			aLayerName = &theWords[itr->second];
	}
	if( aSecondLayerName &&
		aLayerName == aSecondLayerName &&
		allowedKeyWords.count() == 2 )
	{
		aLayerName = anIgnoredWord;
		if( !aLayerName ) aLayerName = anIntegerWord;
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			sKeyWordMap.find(ECommandKeyWord(
				allowedKeyWords.nextSetBit(allowedKeyWords.firstSetBit())));
		if( itr != sKeyWordMap.end() )
			aLayerName = &theWords[itr->second];
	}
	u16 aLayerID = kInvalidID;
	if( aLayerName )
		aLayerID = sLayers.findIndex(*aLayerName);
	u16 aSecondLayerID = kInvalidID;
	if( aSecondLayerName )
		aSecondLayerID = sLayers.findIndex(*aSecondLayerName);
	allowedKeyWords.reset();

	if( aLayerID < sLayers.size() )
	{
		// "= Replace [Layer] <aLayerName> with <aSecondLayerName>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Layer);
		allowedKeyWords.set(eCmdWord_Replace);
		if( keyWordsFound.test(eCmdWord_Replace) &&
			aSecondLayerID < sLayers.size() &&
			(keyWordsFound & ~allowedKeyWords).count() <= 2 )
		{
			result.type = eCmdType_ReplaceControlsLayer;
			result.layerID = aLayerID;
			result.replacementLayer = aSecondLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Replace);

		// "= Add [Layer] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Add);
		if( keyWordsFound.test(eCmdWord_Add) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_AddControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Add);

		// "= Toggle [Layer] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Toggle);
		if( keyWordsFound.test(eCmdWord_Toggle) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_ToggleControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Toggle);

		// "= Replace [this layer with] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Replace);
		if( allowButtonActions &&
			keyWordsFound.test(eCmdWord_Replace) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_ReplaceControlsLayer;
			// Since can't remove layer 0 (main scheme), 0 acts as a flag
			// meaning to remove calling layer instead
			result.layerID = 0;
			result.replacementLayer = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Replace);

		// "= 'Hold'|'Layer'|'Hold Layer' <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Hold);
		if( allowHoldActions &&
			(keyWordsFound.test(eCmdWord_Hold) ||
			 keyWordsFound.test(eCmdWord_Layer)) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_HoldControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Hold);

		// "= Remove [Layer] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Remove);
		if( keyWordsFound.test(eCmdWord_Remove) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_RemoveControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		//allowedKeyWords.reset(eCmdWord_Remove);
	}

	// Same deal as aLayerName for the Menu-related commands needing a name
	// of the menu in question as the one otherwise-unrelated word.
	const std::string* aMenuName = anIgnoredWord;
	allowedKeyWords = keyWordsFound;
	allowedKeyWords.reset(eCmdWord_Menu);
	allowedKeyWords.reset(eCmdWord_Reset);
	allowedKeyWords.reset(eCmdWord_Select);
	allowedKeyWords.reset(eCmdWord_Confirm);
	allowedKeyWords.reset(eCmdWord_Close);
	allowedKeyWords.reset(eCmdWord_Edit);
	allowedKeyWords.reset(eCmdWord_Left);
	allowedKeyWords.reset(eCmdWord_Right);
	allowedKeyWords.reset(eCmdWord_Up);
	allowedKeyWords.reset(eCmdWord_Down);
	allowedKeyWords.reset(eCmdWord_Hotspot);
	allowedKeyWords.reset(eCmdWord_Default);
	allowedKeyWords.reset(eCmdWord_Integer);
	allowedKeyWords.reset(eCmdWord_Back);
	allowedKeyWords.reset(eCmdWord_Mouse);
	allowedKeyWords.reset(eCmdWord_Click);
	if( allowedKeyWords.count() == 1 )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			sKeyWordMap.find(
				ECommandKeyWord(allowedKeyWords.firstSetBit()));
		if( itr != sKeyWordMap.end() )
			aMenuName = &theWords[itr->second];
	}
	u16 aMenuID = kInvalidID;
	if( aMenuName )
		aMenuID = getRootMenuID(*aMenuName);

	if( aMenuID < sMenus.size() )
	{
		// If add "[with] mouse" to menu commands, causes actual mouse
		// cursor to move and point at currently selected item, and
		// possibly left-click there as well if also use "mouse click".
		// This is used for menus that directly overlay actual in-game
		// menus to save on needing a bunch of hotspots and extra
		// jump/click commands (or alongside eMenuStyle_Hotspots menus).
		result.andClick = keyWordsFound.test(eCmdWord_Click);
		result.withMouse = result.andClick ||
			keyWordsFound.test(eCmdWord_Mouse);

		// "= Reset <aMenuName> [Menu] [to Default] [to #] [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Default);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		allowedKeyWords.set(eCmdWord_Integer);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuReset;
			result.menuID = aMenuID;
			result.menuItemID = result.count;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Reset);
		allowedKeyWords.reset(eCmdWord_Default);
	}

	if( allowButtonActions && aMenuID < sMenus.size() )
	{
		// "= Confirm <aMenuName> [Menu] [with mouse click]"
		// allowedKeyWords = Menu & Mouse & Click
		allowedKeyWords.set(eCmdWord_Confirm);
		if( keyWordsFound.test(eCmdWord_Confirm) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuConfirm;
			result.menuID = aMenuID;
			return result;
		}

		// "= Confirm <aMenuName> [Menu] and Close [with mouse click]"
		// allowedKeyWords = Menu & Confirm & Mouse & Click
		allowedKeyWords.set(eCmdWord_Close);
		if( keyWordsFound.test(eCmdWord_Confirm) &&
			keyWordsFound.test(eCmdWord_Close) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuConfirmAndClose;
			result.menuID = aMenuID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Confirm);
		allowedKeyWords.reset(eCmdWord_Mouse);
		allowedKeyWords.reset(eCmdWord_Click);

		// "= Edit <aMenuName> [Menu]"
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Edit);
		if( keyWordsFound.test(eCmdWord_Edit) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuEdit;
			result.menuID = aMenuID;
			return result;
		}
	}

	// Now once again same deal for the name of a Key Bind Array
	const std::string* aKeyBindArrayName = anIgnoredWord;
	allowedKeyWords = keyWordsFound;
	allowedKeyWords.reset(eCmdWord_Reset);
	allowedKeyWords.reset(eCmdWord_Last);
	allowedKeyWords.reset(eCmdWord_Default);
	allowedKeyWords.reset(eCmdWord_Load);
	allowedKeyWords.reset(eCmdWord_Set);
	allowedKeyWords.reset(eCmdWord_Prev);
	allowedKeyWords.reset(eCmdWord_Next);
	allowedKeyWords.reset(eCmdWord_Integer);
	if( allowedKeyWords.count() == 1 )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			sKeyWordMap.find(
				ECommandKeyWord(allowedKeyWords.firstSetBit()));
		if( itr != sKeyWordMap.end() )
			aKeyBindArrayName = &theWords[itr->second];
	}
	// In this case, need to confirm key bind name is valid and get ID from it
	u16 aKeyBindArrayID = sKeyBindArrays.size();
	if( aKeyBindArrayName )
		aKeyBindArrayID = sKeyBindArrays.findIndex(*aKeyBindArrayName);

	if( aKeyBindArrayID < sKeyBindArrays.size() )
	{
		// "= Reset <aKeyBindArrayID> [Last] [to Default]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Last);
		allowedKeyWords.set(eCmdWord_Default);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayResetLast;
			result.keybindArrayID = aKeyBindArrayID;
			return result;
		}
		// "= <aKeyBindArrayID> [Load] Default"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Load);
		allowedKeyWords.set(eCmdWord_Default);
		if( keyWordsFound.test(eCmdWord_Default) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayDefault;
			result.keybindArrayID = aKeyBindArrayID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Load);
		// "= <aKeyBindArrayID> 'Set [Default] [to Last]"
		// allowedKeyWords = Default
		allowedKeyWords.set(eCmdWord_Set);
		allowedKeyWords.set(eCmdWord_Last);
		if( keyWordsFound.test(eCmdWord_Set) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArraySetDefault;
			result.keybindArrayID = aKeyBindArrayID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Set);
		allowedKeyWords.reset(eCmdWord_Default);
		// "= <aKeyBindArrayID> Last"
		// allowedKeyWords = Last
		if( keyWordsFound.test(eCmdWord_Last) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayLast;
			result.keybindArrayID = aKeyBindArrayID;
			return result;
		}
		// "= <aKeyBindArrayID> Prev [No/Wrap] [#]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Prev);
		allowedKeyWords.set(eCmdWord_Integer);
		if( keyWordsFound.test(eCmdWord_Prev) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayPrev;
			result.keybindArrayID = aKeyBindArrayID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Prev);
		// "= <aKeyBindArrayID> Next [No/Wrap] [#]"
		// allowedKeyWords = Integer
		allowedKeyWords.set(eCmdWord_Next);
		if( keyWordsFound.test(eCmdWord_Next) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayNext;
			result.keybindArrayID = aKeyBindArrayID;
			return result;
		}
	}

	// Get ECmdDir from key words for remaining commands
	DBG_ASSERT(result.type == eCmdType_Empty);
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Up);
	allowedKeyWords.set(eCmdWord_Down);
	allowedKeyWords.set(eCmdWord_Left);
	allowedKeyWords.set(eCmdWord_Right);
	allowedKeyWords.set(eCmdWord_Back);
	if( (keyWordsFound & allowedKeyWords).count() != 1 )
		return result;

	ECommandDir aCmdDir = eCmdDir_None;
	if( keyWordsFound.test(eCmdWord_Up) ) aCmdDir = eCmdDir_Up;
	else if( keyWordsFound.test(eCmdWord_Down) ) aCmdDir = eCmdDir_Down;
	else if( keyWordsFound.test(eCmdWord_Left) ) aCmdDir = eCmdDir_Left;
	else if( keyWordsFound.test(eCmdWord_Right) ) aCmdDir = eCmdDir_Right;
	result.dir = aCmdDir;
	// Remove direction-related bits from keyWordsFound
	allowedKeyWords.reset(eCmdWord_Back);
	keyWordsFound &= ~allowedKeyWords;

	if( allowButtonActions && aMenuID < sMenus.size() &&
		result.dir != eCmdDir_None )
	{
		// "= 'Select'|'Menu'|'Select Menu'
		// <aMenuName> <aCmdDir> [No/Wrap] [#] [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Integer);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		if( (keyWordsFound.test(eCmdWord_Select) ||
			 keyWordsFound.test(eCmdWord_Menu)) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuSelect;
			result.menuID = aMenuID;
			return result;
		}

		// "= 'Select'|'Menu'|'Select Menu' and Close
		// <aMenuName> <aCmdDir> [No/Wrap] [#] [with mouse click]"
		// allowedKeyWords = Menu & Select & Mouse & Click
		allowedKeyWords.set(eCmdWord_Close);
		if( (keyWordsFound.test(eCmdWord_Select) ||
			 keyWordsFound.test(eCmdWord_Menu)) &&
			keyWordsFound.test(eCmdWord_Close) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuSelectAndClose;
			result.menuID = aMenuID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Select);
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Mouse);
		allowedKeyWords.reset(eCmdWord_Click);

		// "= Edit [Menu] <aMenuName> <aCmdDir>"
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Edit);
		if( keyWordsFound.test(eCmdWord_Edit) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuEditDir;
			result.menuID = aMenuID;
			return result;
		}
	}

	if( keyWordsFound.test(eCmdWord_Back) )
	{
		result.dir = eCmdDir_Back;
		keyWordsFound.reset(eCmdWord_Back);
	}

	if( allowButtonActions )
	{
		// "= [Select] [Mouse] Hotspot <aCmdDir> [#]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Hotspot);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Integer);
		if( keyWordsFound.test(eCmdWord_Hotspot) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_HotspotSelect;
			return result;
		}
	}

	if( allowHoldActions )
	{
		// "= 'Move'|'Turn'|'MoveTurn' <aCmdDir>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Move);
		allowedKeyWords.set(eCmdWord_Turn);
		if( (keyWordsFound & allowedKeyWords).any() &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveTurn;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Turn);

		// "= 'Strafe|MoveStrafe' <aCmdDir>"
		// allowedKeyWords = Move
		allowedKeyWords.set(eCmdWord_Strafe);
		if( keyWordsFound.test(eCmdWord_Strafe) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveStrafe;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Strafe);

		// "= 'Look|MoveLook' <aCmdDir>"
		// allowedKeyWords = Move
		allowedKeyWords.set(eCmdWord_Look);
		if( keyWordsFound.test(eCmdWord_Look) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveLook;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Look);

		// "= [Move] Mouse <aCmdDir>"
		// allowedKeyWords = Move
		allowedKeyWords.set(eCmdWord_Mouse);
		if( keyWordsFound.test(eCmdWord_Mouse) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveMouse;
			return result;
		}

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Stepped] <aCmdDir>"
		// allowedKeyWords = Move & Mouse
		allowedKeyWords.set(eCmdWord_MouseWheel);
		allowedKeyWords.set(eCmdWord_Stepped);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.mouseWheelMotionType = eMouseWheelMotion_Stepped;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Stepped);

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' Smooth <aCmdDir>"
		// allowedKeyWords = Move & Mouse & Wheel
		allowedKeyWords.set(eCmdWord_Smooth);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			keyWordsFound.test(eCmdWord_Smooth) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.mouseWheelMotionType = eMouseWheelMotion_Smooth;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Smooth);
	}

	// "= [Move] [Mouse] 'Wheel'|'MouseWheel' # <aCmdDir>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Move);
	allowedKeyWords.set(eCmdWord_Mouse);
	allowedKeyWords.set(eCmdWord_MouseWheel);
	allowedKeyWords.set(eCmdWord_Integer);
	if( keyWordsFound.test(eCmdWord_MouseWheel) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MouseWheel;
		result.mouseWheelMotionType = eMouseWheelMotion_Jump;
		return result;
	}

	if( allowButtonActions && aMenuID < sMenus.size() )
	{
		// This is all the way down here because "back" could be a direction
		// for another command, OR mean backing out of a sub-menu, and want
		// to first make sure <aMenuName> isn't another command's key word
		// "= Menu <aMenuName> Back [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		if( result.dir == eCmdDir_Back &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuBack;
			result.dir = eCmdDir_None;
			result.menuID = aMenuID;
			return result;
		}
	}

	DBG_ASSERT(result.type == eCmdType_Empty);
	return result;
}


static Command* specialKeyBindNameToCommand(const std::string& theName)
{
	// This intercepts attempts to set a command to certain specific key binds
	// directly and instead uses the special command associated with it.
	// For example, using "= TurnLeft" will return eCmdType_MoveTurn w/ _Left
	// instead of becoming _TapKey with whatever TurnLeft is bound to.
	// This makes sure special code for some of these commands is utilized
	// when assign to the key bind name directly intead of the command name.

	static StringToValueMap<Command, u8> sSpecialKeyBindMap;
	if( sSpecialKeyBindMap.empty() )
	{
		sSpecialKeyBindMap.reserve(7); // <-- update this if add more!
		Command aCmd;
		aCmd.type = eCmdType_StartAutoRun;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_AutoRun], aCmd);
		aCmd.type = eCmdType_MoveTurn;
		aCmd.dir = eCmdDir_Forward;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_MoveF], aCmd);
		aCmd.dir = eCmdDir_Back;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_MoveB], aCmd);
		aCmd.dir = eCmdDir_Left;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_TurnL], aCmd);
		aCmd.dir = eCmdDir_Right;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_TurnR], aCmd);
		aCmd.type = eCmdType_MoveStrafe;
		aCmd.dir = eCmdDir_Left;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_StrafeL], aCmd);
		aCmd.dir = eCmdDir_Right;
		sSpecialKeyBindMap.setValue(
			kSpecialKeyBindNames[eSpecialKey_StrafeR], aCmd);
	}

	return sSpecialKeyBindMap.find(theName);
}


static Command stringToCommand(
	const std::string& theString,
	bool allowButtonActions = false,
	bool allowHoldActions = false)
{
	Command result;

	if( theString.empty() )
		return result;

	// Check for a chat box macro (message or slash command)
	result = parseChatBoxMacro(theString);
	if( result.type != eCmdType_Empty )
		return result;

	// Check for a simple key assignment
	std::vector<std::string> aParsedString; aParsedString.reserve(16);
	sanitizeSentence(theString, aParsedString);
	if( u16 aVKey = namesToVKey(aParsedString) )
	{
		result.type = eCmdType_TapKey;
		result.vKey = aVKey;
		// _PressAndHoldKey (and indirectly its associated _ReleaseKey)
		// only works properly with a single key (+ mods), just like _TapKey,
		// and only when allowHoldActions is true (_Down button action),
		// so in that case change _TapKey to _PressAndHoldKey instead.
		// This does mean there isn't currently a way to keep this button
		// action assigned to only _TapKey, and that a _Down button action
		// will just act the same as _Press for any other commmands
		// (which can be used intentionally to have 2 commands for button
		// initial press, thus can be useful even if a bit unintuitive).
		result.type = eCmdType_PressAndHoldKey;
		return result;
	}

	// Check for special command
	result = wordsToSpecialCommand(
		aParsedString,
		allowButtonActions,
		allowHoldActions);
	if( result.type != eCmdType_Empty )
		return result;

	// Check for alias to a keybind
	if( Command* aSpecialKeyBindCommand =
			specialKeyBindNameToCommand(theString) )
	{
		if( allowHoldActions ||
			aSpecialKeyBindCommand->type < eCmdType_FirstContinuous )
		{
			result = *aSpecialKeyBindCommand;
			return result;
		}
	}

	const u16 aKeyBindID = sKeyBinds.findIndex(theString);
	if( aKeyBindID < sKeyBinds.size() )
	{
		// Check if this keybind is part of an array, and if so, use
		// eCmdType_KeyBindArrayIndex instead so "last" will update
		std::string aKeyBindName = theString;
		const int anArrayIdx = breakOffIntegerSuffix(aKeyBindName);
		if( anArrayIdx >= 0 )
		{
			u16 aKeyBindArrayID = sKeyBindArrays.findIndex(aKeyBindName);
			if( aKeyBindArrayID < sKeyBindArrays.size() &&
				sKeyBindArrays.vals()[aKeyBindArrayID][anArrayIdx].
					keyBindID == aKeyBindID )
			{
				// Comment on _PressAndHoldKey above explains this
				if( result.type == eCmdType_TapKey && allowHoldActions )
					result.type = eCmdType_KeyBindArrayHoldIndex;
				else
					result.type = eCmdType_KeyBindArrayIndex;
				result.keybindArrayID = aKeyBindArrayID;
				result.arrayIdx = anArrayIdx;
				return result;
			}
		}
		result.type = eCmdType_TriggerKeyBind;
		result.keyBindID = aKeyBindID;
		return result;
	}

	// Check for Virtual-Key Code sequence
	const std::string& aVKeySeq = namesToVKeySequence(aParsedString);
	if( !aVKeySeq.empty() )
	{
		result.type = eCmdType_VKeySequence;
		result.vKeySeqID = sCmdStrings.findOrAddIndex(aVKeySeq);
	}

	return result;
}


static void createEmptyHotspotArray(const std::string& theName)
{
	// Check if name ends in a number and thus could be part of an array
	std::string anArrayKey = theName;
	int aRangeEndIdx = breakOffIntegerSuffix(anArrayKey);
	int aRangeStartIdx = aRangeEndIdx;
	bool isAnchorHotspot = aRangeEndIdx <= 0;

	// Check for range syntax like HotspotName7-10
	bool isRange = false;
	if( !isAnchorHotspot )
	{
		isRange = anArrayKey[anArrayKey.size()-1] == '-';
		if( isRange )
		{
			anArrayKey.resize(anArrayKey.size()-1);
			aRangeStartIdx = breakOffIntegerSuffix(anArrayKey);
			if( aRangeStartIdx <= 0 || aRangeStartIdx > aRangeEndIdx )
			{
				logError("Invalid range in '%s', treating as non-array hotspot",
					theName.c_str());
				isAnchorHotspot = true;
			}
		}
	}

	// Restore full key string for an anchor name that may have been modified
	if( isAnchorHotspot && aRangeEndIdx >= 0 )
		anArrayKey = theName;

	// Create hotspot array object
	HotspotArray& anArray = sHotspotArrays.findOrAdd(anArrayKey);
	if( anArray.name.empty() )
	{
		anArray.name = theName;
		if( !isAnchorHotspot )
		{// Strip number(s) off end of name
			anArray.name.resize(posAfterPrefix(
				anArray.name, anArrayKey));
		}
	}
	if( isAnchorHotspot )
	{
		anArray.hasAnchor = true;
		return;
	}

	// Now create the range to add
	HotspotRange aNewRange;
	aNewRange.firstIdx = u16(aRangeStartIdx);
	aNewRange.count = aRangeEndIdx - aRangeStartIdx + 1;
	aNewRange.offsetFromPrev = isRange;
	aNewRange.hasOwnXAnchor = !isRange;
	aNewRange.hasOwnYAnchor = !isRange;

	// Insert into the ranges vector in sorted order and check for overlap
	std::vector<HotspotRange>::iterator itr = std::lower_bound(
		anArray.ranges.begin(), anArray.ranges.end(), aNewRange);

	// Check for overlap with previous range (if any)
	if( itr != anArray.ranges.begin() )
	{
		const HotspotRange& aPrevRange = *(itr - 1);
		if( aPrevRange.lastIdx() >= aNewRange.firstIdx )
		{
			logError("%s overlaps with another hotspot/range!",
				theName.c_str());
			return; // skip adding
		}
	}

	// Check for overlap with next range (if any)
	if( itr != anArray.ranges.end() )
	{
		const HotspotRange& aNextRange = *itr;
		if( aNewRange.lastIdx() >= aNextRange.firstIdx )
		{
			logError("%s overlaps with another hotspot/range!",
				theName.c_str());
			return; // skip adding
		}
	}

	anArray.ranges.insert(itr, aNewRange);

	// Update total size (excluding anchor)
	anArray.size = anArray.ranges.back().lastIdx();
}


static void createEmptyHotspotsForArray(u16 theArrayID)
{
	HotspotArray& theArray = sHotspotArrays.vals()[theArrayID];

	// Check for missing entries
	u16 anExpectedIdx = 1;
	for(size_t i = 0; i < theArray.ranges.size(); ++i)
	{
		if( theArray.ranges[i].firstIdx != anExpectedIdx )
		{
			logError("Hotspot Array '%s' appears to be missing '%s%d'!",
				theArray.name.c_str(), theArray.name.c_str(), anExpectedIdx);
			theArray.size = anExpectedIdx - 1;
			theArray.ranges.resize(i);
			break;
		}
		anExpectedIdx = theArray.ranges[i].lastIdx() + 1;
	}
	DBG_ASSERT(anExpectedIdx == theArray.size + 1);

	// Trim ranges vector
	if( theArray.ranges.size() < theArray.ranges.capacity() )
		std::vector<HotspotRange>(theArray.ranges).swap(theArray.ranges);

	// Create contiguous hotspots in sHotspots for this array, and
	// update this array to know where its hotspots are located
	theArray.anchorIdx = u16(sHotspots.size());
	if( theArray.hasAnchor )
		sHotspots.push_back(Hotspot());
	else
		--theArray.anchorIdx;
	if( theArray.size > 0 )
		sHotspots.resize(sHotspots.size() + theArray.size);
}


static void applyHotspotProperty(
	const std::string& theKey,
	const std::string& theDesc,
	BitVector<32>& theUpdatedHotspotArrays,
	BitVector<512>& theUpdatedHotspots)
{
	// Determine if this is a single hotspot or part of a range
	std::string anArrayKey = theKey;
	int aRangeEndIdx = breakOffIntegerSuffix(anArrayKey);
	int aRangeStartIdx = aRangeEndIdx;
	bool isAnchorHotspot = aRangeEndIdx <= 0;

	// Check for range syntax like HotspotName7-10
	if( !isAnchorHotspot )
	{
		if( anArrayKey[anArrayKey.size()-1] == '-' )
		{
			anArrayKey.resize(anArrayKey.size()-1);
			aRangeStartIdx = breakOffIntegerSuffix(anArrayKey);
			// Treat malformed same as before, but no warning any more
			if( aRangeStartIdx <= 0 || aRangeStartIdx > aRangeEndIdx )
				isAnchorHotspot = true;
		}
	}

	// If anchor name was modified during parsing, restore full form
	if( isAnchorHotspot && aRangeEndIdx >= 0 )
		anArrayKey = theKey;

	// Look up hotspot metadata using array key
	u16 aHotspotArrayID = sHotspotArrays.findIndex(anArrayKey);
	if( aHotspotArrayID >= sHotspots.size() )
		return;
	HotspotArray& anArray = sHotspotArrays.vals()[aHotspotArrayID];
	DBG_ASSERT(!isAnchorHotspot || anArray.hasAnchor);

	// Parse hotspot data from the description string
	Hotspot aHotspot;
	float anOffsetScale = 0;
	if( !theDesc.empty() )
	{
		std::string aHotspotDesc = theDesc;
		EResult aResult = HotspotMap::stringToHotspot(aHotspotDesc, aHotspot);
		if( aResult == eResult_Overflow )
		{
			logError("Hotspot %s: Invalid coordinate in '%s' "
				"(anchor must be in 0-100% range and limited decimal places)",
				sPropertyPrintName.c_str(), theDesc.c_str());
			aHotspot = Hotspot();
		}
		else if( aResult == eResult_Malformed )
		{
			logError("Hotspot %s: Could not decipher hotspot position '%s'",
				sPropertyPrintName.c_str(), theDesc.c_str());
			aHotspot = Hotspot();
		}
		else if( !aHotspotDesc.empty() && aHotspotDesc[0] == '*' )
		{
			if( !isAnchorHotspot || anArray.ranges.empty() )
			{
				logError(
					"Hotspot %s: Only array anchor hotspots can specify "
					"an offset scale factor (using '*')!",
					sPropertyPrintName.c_str());
			}
			else
			{
				anOffsetScale = floatFromString(aHotspotDesc.substr(1));
				if( anOffsetScale == 0 )
				{
					logError("Hotspot %s: Invalid offset scale factor '%s'",
						sPropertyPrintName.c_str(), aHotspotDesc.c_str());
				}
			}
		}
	}

	std::vector<HotspotRange>::iterator aRange;
	if( isAnchorHotspot )
	{
		// Skip any further work if no different than previous setting
		if( sHotspots[anArray.anchorIdx] == aHotspot &&
			(anOffsetScale == 0 || anOffsetScale == anArray.offsetScale) )
		{ return; }

		sHotspots[anArray.anchorIdx] = aHotspot;
		theUpdatedHotspotArrays.set(aHotspotArrayID);
		theUpdatedHotspots.set(anArray.anchorIdx);
		if( anOffsetScale )
			anArray.offsetScale = anOffsetScale;

		// Start scanning at first range for dependent hotspots
		aRange = anArray.ranges.begin();
	}
	else
	{
		// Find matching range entry to apply the property to
		HotspotRange aCmpRange;
		aCmpRange.firstIdx = aRangeStartIdx;
		aCmpRange.count = aRangeEndIdx - aRangeStartIdx + 1;
		std::vector<HotspotRange>::iterator itr = std::lower_bound(
			anArray.ranges.begin(), anArray.ranges.end(), aCmpRange);

		// Exit if none match exactly (should already have warned)
		if( itr == anArray.ranges.end() ||
			itr->firstIdx != aCmpRange.firstIdx ||
			itr->lastIdx() != aCmpRange.lastIdx() )
		{ return; }

		if( itr->count == 1 && !itr->offsetFromPrev )
		{// Might have own anchors - set hotspot directly for now
			itr->hasOwnXAnchor = aHotspot.x.anchor != 0 || !anArray.hasAnchor;
			itr->hasOwnYAnchor = aHotspot.y.anchor != 0 || !anArray.hasAnchor;
			if( itr->hasOwnXAnchor && itr->hasOwnYAnchor &&
				sHotspots[anArray.anchorIdx + itr->firstIdx] == aHotspot )
			{ return; } // skip scan for dependent changes
			sHotspots[anArray.anchorIdx + itr->firstIdx] = aHotspot;
			theUpdatedHotspotArrays.set(aHotspotArrayID);
			theUpdatedHotspots.set(anArray.anchorIdx + itr->firstIdx);
		}

		itr->xOffset = itr->hasOwnXAnchor ? 0 : aHotspot.x.offset;
		itr->yOffset = itr->hasOwnYAnchor ? 0 : aHotspot.y.offset;
		if( itr->hasOwnXAnchor && itr->hasOwnYAnchor )
			aRange = itr + 1;
		else
			aRange = itr;
		
		// Empty description removes range from array and shortens it
		// This changes ->size but does not remove ranges/hotspots so they can
		// be restored later by setting the hotspot back to a valid value
		if( theDesc.empty() )
			itr->removed = true;
		if( itr->removed )
		{
			// If removed in a previous call might be restored now
			if( !theDesc.empty() )
				itr->removed = false;
			// Recalculate array size to one less than first removed range
			anArray.size = 0;
			for(itr = anArray.ranges.begin();
				itr != anArray.ranges.end() && !itr->removed; ++itr)
			{ anArray.size = itr->lastIdx(); }
		}
	}

	// Update actual hotspots to reflect stored offsets in array data
	for(bool rangeAffected = true;
		aRange != anArray.ranges.end() && (rangeAffected || isAnchorHotspot);
		++aRange)
	{
		// Skip if prev range was unchanged and this depends on it
		if( !rangeAffected && aRange->offsetFromPrev )
		{
			rangeAffected = false;
			continue;
		}

		rangeAffected = false;
		if( aRange->hasOwnXAnchor && aRange->hasOwnYAnchor )
			continue;

		for(u16 aHotspotID = aRange->firstIdx + anArray.anchorIdx;
			aHotspotID <= aRange->lastIdx() + anArray.anchorIdx;
			++aHotspotID)
		{
			const u16 aBaseHotspotID =
				aRange->offsetFromPrev ? aHotspotID - 1 :
				anArray.hasAnchor ? anArray.anchorIdx : 0;
			if( aRange->hasOwnXAnchor )
			{
				aHotspot.x = sHotspots[aHotspotID].x;
			}
			else
			{
				aHotspot.x.anchor = sHotspots[aBaseHotspotID].x.anchor;
				aHotspot.x.offset = sHotspots[aBaseHotspotID].x.offset;
				aHotspot.x.offset += aRange->xOffset * anArray.offsetScale;
			}
			if( aRange->hasOwnYAnchor )
			{
				aHotspot.y = sHotspots[aHotspotID].y;
			}
			else
			{
				aHotspot.y.anchor = sHotspots[aBaseHotspotID].y.anchor;
				aHotspot.y.offset = sHotspots[aBaseHotspotID].y.offset;
				aHotspot.y.offset += aRange->yOffset * anArray.offsetScale;
			}
			if( aHotspot != sHotspots[aHotspotID] )
			{
				rangeAffected = true;
				sHotspots[aHotspotID] = aHotspot;
				theUpdatedHotspotArrays.set(aHotspotArrayID);
				theUpdatedHotspots.set(aHotspotID);
			}
		}
	}
}


static void reportCommandAssignment(
	const Command& theCmd,
	const std::string& theCmdStr,
	u16 theSignalID = 0)
{
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		if( !theCmdStr.empty() )
		{
			logError("%s: Not sure how to assign '%s' to '%s'! "
				"Confirm correct spelling of all key words and names!",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str(),
				theCmdStr.c_str());
			break;
		}
		else
		{
			mapDebugPrint("%s: '%s' left blank / skipped / removed!\n",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str());
		}
		break;
	case eCmdType_Unassigned:
		mapDebugPrint("%s: '%s' left <unassigned>\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str());
		break;
	case eCmdType_DoNothing:
		mapDebugPrint("%s: Assigned '%s' to <Do Nothing>\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str());
		break;
	case eCmdType_SignalOnly:
		mapDebugPrint("%s: Assigned '%s' to <Signal #%d Only>\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str(),
			theSignalID);
		break;
	case eCmdType_TriggerKeyBind:
		if( theSignalID ||
			sKeyBinds.vals()[theCmd.keyBindID].type == eCmdType_Empty )
		{
			mapDebugPrint(
				"%s: Assigned '%s' to use '%s' Key Bind + <signal #%d>\n",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str(),
				sKeyBinds.keys()[theCmd.keyBindID].c_str(),
				theSignalID);
		}
		else
		{
			reportCommandAssignment(
				sKeyBinds.vals()[theCmd.keyBindID],
				theCmdStr,
				keyBindSignalID(theCmd.keyBindID));
		}
		break;
	case eCmdType_ChatBoxString:
		{
			#ifndef NDEBUG
			std::string aMacroString = sCmdStrings.keys()[theCmd.stringID];
			aMacroString.resize(aMacroString.size()-1);
			aMacroString = replaceAllStr(aMacroString, "\r", "\\n");
			mapDebugPrint("%s: Assigned '%s' to macro: %s%s\n",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str(),
				aMacroString.c_str(),
				theSignalID
					? (std::string(" <signal #") +
						toString(theSignalID) + ">").c_str()
					: "");
			#endif
		}
		break;
	case eCmdType_TapKey:
	case eCmdType_PressAndHoldKey:
		mapDebugPrint("%s: Assigned '%s' to: %s%s%s%s%s%s\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str(),
			!!(theCmd.vKey & kVKeyShiftFlag) ? "Shift+" : "",
			!!(theCmd.vKey & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(theCmd.vKey & kVKeyAltFlag) ? "Alt+" : "",
			!!(theCmd.vKey & kVKeyWinFlag) ? "Win+" : "",
			virtualKeyToName(theCmd.vKey & kVKeyMask).c_str(),
			theSignalID
				? (std::string(" <signal #") +
					toString(theSignalID) + ">").c_str()
				: "");
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("%s: Assigned '%s' to sequence: %s%s\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str(),
			theCmdStr.c_str(),
			theSignalID
				? (std::string(" <signal #") +
					toString(theSignalID) + ">").c_str()
				: "");
		break;
	case eCmdType_OpenSubMenu:
		mapDebugPrint("%s: Assigned '%s' as a sub-menu\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str());
		break;
	case eCmdType_MenuBack:
		mapDebugPrint("%s: Assigned '%s' to back out of menu\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str());
		break;
	case eCmdType_MenuClose:
		mapDebugPrint("%s: Assigned '%s' to close menu\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str());
		break;
	default:
		mapDebugPrint("%s: Assigned '%s' to command: %s\n",
			sSectionPrintName.c_str(),
			sPropertyPrintName.c_str(),
			theCmdStr.c_str());
		break;
	}
}


static void createEmptyKeyBind(const std::string& theName)
{
	const u16 aPrevKeyBindsSize = sKeyBinds.size();
	const u16 aKeyBindID = sKeyBinds.findOrAddIndex(theName, Command());
	if( sKeyBinds.size() > aPrevKeyBindsSize )
	{
		// Check if should also be part of a Key Bind Array
		std::string aKeyName = theName;
		const int anArrayIdx = breakOffIntegerSuffix(aKeyName);
		if( anArrayIdx >= 0 )
		{
			KeyBindArray& aKeyBindArray = sKeyBindArrays.findOrAdd(aKeyName);

			// Add this key bind as an entry in the array
			if( aKeyBindArray.size() <= anArrayIdx )
				aKeyBindArray.resize(anArrayIdx+1);
			aKeyBindArray[anArrayIdx].keyBindID = aKeyBindID;
			aKeyBindArray[anArrayIdx].hotspotID =
				getHotspotID(aKeyName + toString(anArrayIdx));
			if( aKeyBindArray[anArrayIdx].hotspotID )
			{
				mapDebugPrint(
					"[%s]: Linked array entry '%s' to hotspot '%s'\n",
					kKeyBindsSectionName,
					theName.c_str(),
					hotspotLabel(aKeyBindArray[anArrayIdx].hotspotID).c_str());
			}
		}
	}
}


static u16 applyKeyBindProperty(
	const std::string& theKey,
	const std::string& theCmdStr)
{
	const u16 aKeyBindID = sKeyBinds.findIndex(theKey);
	DBG_ASSERT(aKeyBindID < sKeyBinds.size());
	Command& aCmd = sKeyBinds.vals()[aKeyBindID];

	// Keybinds can only be assigned to direct input - not special commands
	if( theCmdStr.empty() )
		return aKeyBindID;

	// Chat box macro
	aCmd = parseChatBoxMacro(theCmdStr);
	if( aCmd.type != eCmdType_Empty )
		return aKeyBindID;

	// Signal Only
	ECommandKeyWord aKeyWord = commandWordToID(theCmdStr);
	if( aKeyWord == eCmdWord_Nothing )
	{
		aCmd.type = eCmdType_SignalOnly;
		return aKeyBindID;
	}

	// Skip in key bind arrays (same as empty string)
	if( aKeyWord == eCmdWord_Skip )
	{
		aCmd.type = eCmdType_Empty;
		return aKeyBindID;
	}
	
	// Tap key
	std::vector<std::string> aParsedString; aParsedString.reserve(16);
	sanitizeSentence(theCmdStr, aParsedString);
	if( u16 aVKey = namesToVKey(aParsedString) )
	{
		aCmd.type = eCmdType_TapKey;
		aCmd.vKey = aVKey;
		return aKeyBindID;
	}

	// Other Key Bind
	const u16 anotherKeyBindID = sKeyBinds.findIndex(theCmdStr);
	if( anotherKeyBindID < sKeyBinds.size() )
	{
		aCmd.type = eCmdType_TriggerKeyBind;
		aCmd.keyBindID = anotherKeyBindID;
		return aKeyBindID;
	}

	// VKey Sequence
	const std::string& aVKeySeq = namesToVKeySequence(aParsedString);
	if( !aVKeySeq.empty() )
	{
		aCmd.type = eCmdType_VKeySequence;
		aCmd.vKeySeqID = sCmdStrings.findOrAddIndex(aVKeySeq);
		return aKeyBindID;
	}

	// Couldn't figure out what it was!
	aCmd.type = eCmdType_Empty;
	return aKeyBindID;
}


static void validateKeyBind(
	u16 theKeyBindID,
	BitVector<256>& theReferencedKeyBinds)
{
	DBG_ASSERT(theKeyBindID < sKeyBinds.size());
	DBG_ASSERT(theReferencedKeyBinds.size() == sKeyBinds.size());

	theReferencedKeyBinds.set(theKeyBindID);
	Command& aCmd = sKeyBinds.vals()[theKeyBindID];
	if( aCmd.type == eCmdType_TriggerKeyBind )
	{
		// Direct reference to another key bind
		DBG_ASSERT(aCmd.keyBindID < theReferencedKeyBinds.size());
		if( theReferencedKeyBinds.test(aCmd.keyBindID) )
		{
			logError("Key Bind %s ends up referencing itself!",
				sKeyBinds.keys()[theKeyBindID].c_str());
			// Make calling command do nothing but send signal now
			aCmd.type = eCmdType_SignalOnly;
			return;
		}
		validateKeyBind(aCmd.keyBindID, theReferencedKeyBinds);
	}
	else if( aCmd.type == eCmdType_VKeySequence )
	{
		// VKey Sequence may have an embedded reference to a key bind
		DBG_ASSERT(aCmd.stringID < sCmdStrings.size());
		const std::string& aStr = sCmdStrings.keys()[aCmd.stringID];
		for(size_t i = 0; i < aStr.size(); ++i)
		{
			if( aStr[i] == kVKeyTriggerKeyBind )
			{
				DBG_ASSERT(aStr.size() >= i + 2);
				u16 anotherKeyBindID = (u8(aStr[++i]) & 0x7F) << 7;
				anotherKeyBindID |= (u8(aStr[++i]) & 0x7F);
				DBG_ASSERT(anotherKeyBindID < theReferencedKeyBinds.size());
				if( theReferencedKeyBinds.test(anotherKeyBindID) )
				{
					logError("Key Bind %s ends up referencing itself!",
						sKeyBinds.keys()[theKeyBindID].c_str());
					aCmd.type = eCmdType_SignalOnly;
					return;
				}
				validateKeyBind(anotherKeyBindID, theReferencedKeyBinds);	
			}
		}
	}
}


static EButtonAction breakOffButtonAction(std::string& theButtonActionName)
{
	DBG_ASSERT(!theButtonActionName.empty());

	// Assume default "Down" action if none specified by a prefix
	EButtonAction result = eBtnAct_Down;

	// Check for action prefix - not many so just linear search
	for(size_t i = 0; i < eBtnAct_Num; ++i)
	{
		if( const size_t aPrefixPos =
				posAfterPrefix(theButtonActionName, kButtonActionPrefx[i]) )
		{
			result = EButtonAction(i);
			// Chop the prefix off the front of the string
			theButtonActionName = theButtonActionName.substr(aPrefixPos);
			break;
		}
	}

	return result;
}


static void addButtonAction(
	u16 theLayerIdx,
	std::string theBtnKeyName,
	const std::string& theCmdStr,
	bool onlySpecificAction)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theBtnKeyName.empty() || theCmdStr.empty() )
		return;

	// Determine button & action to assign command to
	EButtonAction aBtnAct = breakOffButtonAction(theBtnKeyName);
	int aBtnTime = breakOffIntegerSuffix(theBtnKeyName);
	EButton aBtnID = buttonNameToID(theBtnKeyName);

	bool isA4DirMultiAssign =
		aBtnID == eBtn_LSAny ||
		aBtnID == eBtn_RSAny ||
		aBtnID == eBtn_DPadAny ||
		aBtnID == eBtn_FPadAny;

	if( isA4DirMultiAssign )
	{
		// Attempt to assign to all 4 directional variations of this button
		// to the same command (or directional variations of it).
		const Command& aBaseCmd = stringToCommand(
			theCmdStr, true,
			aBtnAct == eBtnAct_Down);
		bool dirCommandFailed = false;
		for(size_t i = 0; i < 4; ++i)
		{
			// Get true button ID by adding direction key to button name
			aBtnID = buttonNameToID(theBtnKeyName + k4DirCmdSuffix[i]);
			DBG_ASSERT(aBtnID < eBtn_Num);
			// See if can get a different command if append a direction,
			// if didn't already fail previously
			std::string aCmdStr = theCmdStr;
			Command aCmd;
			if( !dirCommandFailed )
			{
				aCmdStr += k4DirCmdSuffix[i];
				aCmd = stringToCommand(
					aCmdStr, true, aBtnAct == eBtnAct_Down);
			}
			// Get destination of command
			ButtonActions* aDestBtn =
				&sLayers.vals()[theLayerIdx].buttonMap.findOrAdd(aBtnID);
			if( !onlySpecificAction ) aDestBtn->initIfEmpty();
			Command* aDestCmd = &(aDestBtn->cmd[aBtnAct]);
			// Direct assignment should take priority over multi-assignment,
			// so if this was already assigned directly then leave it alone.
			if( aDestCmd->type > eCmdType_Unassigned || !isA4DirMultiAssign )
				continue;
			if( aCmd.type < eCmdType_FirstDirectional )
			{// Not a valid directional command! Use base command
				aCmdStr = theCmdStr;
				aCmd = aBaseCmd;
				dirCommandFailed = true;
				// Possibly treat as a single assignment to the special
				// "any direction" button for this 4-dir input
				switch(aBtnID)
				{
				case eBtn_LSLeft: case eBtn_LSRight:
				case eBtn_LSUp: case eBtn_LSDown:
					aBtnID = eBtn_LSAny;
					isA4DirMultiAssign = false;
					break;
				case eBtn_RSLeft: case eBtn_RSRight:
				case eBtn_RSUp: case eBtn_RSDown:
					aBtnID = eBtn_RSAny;
					isA4DirMultiAssign = false;
					break;
				case eBtn_DLeft: case eBtn_DRight:
				case eBtn_DUp: case eBtn_DDown:
					aBtnID = eBtn_DPadAny;
					isA4DirMultiAssign = false;
					break;
				}
				if( !isA4DirMultiAssign )
				{
					aDestBtn = &sLayers.vals()[theLayerIdx].
						buttonMap.findOrAdd(aBtnID);
					if( !onlySpecificAction )
						aDestBtn->initIfEmpty();
					aDestCmd = &aDestBtn->cmd[aBtnAct];
				}
			}
			// Make and report assignment
			*aDestCmd = aCmd;
			if( aBtnAct == eBtnAct_Hold )
			{// Assign time to hold button for this action
				sButtonHoldTimes.setValue(
					std::pair<u16, EButton>(theLayerIdx, aBtnID),
					aBtnTime < 0
						? sDefaultButtonHoldTime
						: aBtnTime);
			}
			#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
			if( aCmd.type == eCmdType_Empty ) 
			#endif
			{
				std::string anExtPropName =
					sPropertyPrintName + k4DirCmdSuffix[i];
				if( isA4DirMultiAssign )
					swap(sPropertyPrintName, anExtPropName);
				reportCommandAssignment(aCmd, aCmdStr);
				if( isA4DirMultiAssign )
					swap(sPropertyPrintName, anExtPropName);
			}
		}
		return;
	}

	if( aBtnTime >= 0 && aBtnID >= eBtn_Num )
	{// Part of the button's name might have been absorbed into aBtnTime
		std::string aTimeAsString = toString(aBtnTime);
		do {
			theBtnKeyName.push_back(aTimeAsString[0]);
			aTimeAsString.erase(0, 1);
			aBtnID = buttonNameToID(theBtnKeyName);
			aBtnTime = aTimeAsString.empty()
				? -1 : intFromString(aTimeAsString);
		} while(aBtnID >= eBtn_Num && !aTimeAsString.empty());
	}

	// If still no valid button ID, must just be a badly-named action + button key
	if( aBtnID >= eBtn_Num )
	{
		logError("Unable to identify Gamepad Button from '%s' requested in %s",
			sPropertyPrintName.c_str(),
			sSectionPrintName.c_str());
		return;
	}

	// Parse command string into a Command struct
	Command aCmd = stringToCommand(theCmdStr, true, aBtnAct == eBtnAct_Down);

	// Make the assignment
	ButtonActions& aDestBtn =
		sLayers.vals()[theLayerIdx].buttonMap.findOrAdd(aBtnID);
	if( !onlySpecificAction ) aDestBtn.initIfEmpty();
	aDestBtn.cmd[aBtnAct] = aCmd;
	if( aBtnAct == eBtnAct_Hold )
	{// Assign time to hold button for this action
		sButtonHoldTimes.setValue(
			std::pair<u16, EButton>(theLayerIdx, aBtnID),
			aBtnTime < 0
				? sDefaultButtonHoldTime
				: aBtnTime);
	}

	// Report the results of the assignment
	reportCommandAssignment(aCmd, theCmdStr);
}


static void addWhenSignalCommand(
	u16 theLayerIdx,
	std::string theSignalKeyName,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theSignalKeyName.empty() || theCmdStr.empty() )
		return;

	// Check for responding to use of any key in a key bind array
	u16 anEntryIdx = sKeyBindArrays.findIndex(theSignalKeyName);
	if( anEntryIdx < sKeyBindArrays.size() )
	{
		Command aCmd = stringToCommand(theCmdStr, true);
		if( aCmd.type != eCmdType_Empty )
		{
			sLayers.vals()[theLayerIdx].signalCommands.setValue(
				keyBindArraySignalID(anEntryIdx), aCmd);
		}

		// Report the results of the assignment
		#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
		if( aCmd.type == eCmdType_Empty ) 
		#endif
		{
			std::string anExtPropName = kSignalCommandPrefix;
			anExtPropName += " " + sPropertyPrintName + " (signal #";
			anExtPropName += toString(keyBindArraySignalID(anEntryIdx)) + ")";
			swap(sPropertyPrintName, anExtPropName);
			reportCommandAssignment(aCmd, theCmdStr);
			swap(sPropertyPrintName, anExtPropName);
		}
		return;
	}

	// Check for responding to use of a key bind
	anEntryIdx = sKeyBinds.findIndex(theSignalKeyName);
	if( anEntryIdx < sKeyBinds.size() )
	{
		Command aCmd = stringToCommand(theCmdStr, true);
		if( aCmd.type != eCmdType_Empty )
		{
			sLayers.vals()[theLayerIdx].signalCommands.setValue(
				keyBindSignalID(anEntryIdx), aCmd);
		}

		// Report the results of the assignment
		#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
		if( aCmd.type == eCmdType_Empty )
		#endif
		{
			std::string anExtPropName = kSignalCommandPrefix;
			anExtPropName += " " + sPropertyPrintName + " (signal #";
			anExtPropName += toString(keyBindSignalID(anEntryIdx)) + ")";
			swap(sPropertyPrintName, anExtPropName);
			reportCommandAssignment(aCmd, theCmdStr);
			swap(sPropertyPrintName, anExtPropName);
		}
		return;
	}

	// Check for responding to use of of "Move", "MoveTurn", and "MoveStrafe"
	switch(commandWordToID(theSignalKeyName))
	{
	case eCmdWord_Move:
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_StrafeL], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_StrafeR], theCmdStr);
		// fall through
	case eCmdWord_Turn:
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_MoveF], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_MoveB], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_TurnL], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_TurnR], theCmdStr);
		return;
	case eCmdWord_Strafe:
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_MoveF], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_MoveB], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_StrafeL], theCmdStr);
		addWhenSignalCommand(theLayerIdx,
			kSpecialKeyBindNames[eSpecialKey_StrafeR], theCmdStr);
		return;
	}

	// For button press signals, need to actually use the word "press"
	// Other button actions (tap, hold, release) are not supported as signals
	if( breakOffButtonAction(theSignalKeyName) == eBtnAct_Press )
	{
		EButton aBtnID = buttonNameToID(theSignalKeyName);
		const bool isA4DirMultiAssign =
			aBtnID == eBtn_LSAny ||
			aBtnID == eBtn_RSAny ||
			aBtnID == eBtn_DPadAny ||
			aBtnID == eBtn_FPadAny;

		if( isA4DirMultiAssign )
		{
			// Attempt to assign to all 4 directional variations of this button
			// to the same command (or directional variations of it).
			const Command& aBaseCmd = stringToCommand(theCmdStr, true);
			bool dirCommandFailed = false;
			for(size_t i = 0; i < 4; ++i)
			{
				aBtnID = buttonNameToID(theSignalKeyName + k4DirCmdSuffix[i]);
				DBG_ASSERT(aBtnID < eBtn_Num);
				std::string aCmdStr = theCmdStr;
				Command aCmd;
				if( !dirCommandFailed )
				{
					aCmdStr += k4DirCmdSuffix[i];
					aCmd = stringToCommand(aCmdStr, true);
				}
				if( aCmd.type == eCmdType_Empty )
				{
					aCmdStr = theCmdStr;
					aCmd = aBaseCmd;
					dirCommandFailed = true;
				}
				Command& aDestCmd = sLayers.vals()[theLayerIdx].
					signalCommands.findOrAdd(aBtnID);
				if( aDestCmd.type != eCmdType_Empty )
					continue;
				aDestCmd = aCmd;
				#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
				if( aCmd.type == eCmdType_Empty )
				#endif
				{
					std::string anExtPropName = kSignalCommandPrefix;
					anExtPropName += " " + sPropertyPrintName + k4DirCmdSuffix[i];
					anExtPropName += " (signal #" + toString(aBtnID) + ")";
					swap(sPropertyPrintName, anExtPropName);
					reportCommandAssignment(aCmd, theCmdStr);
					swap(sPropertyPrintName, anExtPropName);
				}
			}
			return;
		}

		if( aBtnID != eBtn_None && aBtnID < eBtn_Num )
		{
			Command aCmd = stringToCommand(theCmdStr, true);

			// Make the assignment - each button ID matches its signal ID
			if( !aCmd.type == eCmdType_Empty )
			{
				sLayers.vals()[theLayerIdx].
					signalCommands.setValue(aBtnID, aCmd);
			}

			// Report the results of the assignment
			#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
			if( aCmd.type == eCmdType_Empty )
			#endif
			{
				std::string anExtPropName = kSignalCommandPrefix;
				anExtPropName += " " + sPropertyPrintName;
				anExtPropName += " (signal #" + toString(aBtnID) + ")";
				swap(sPropertyPrintName, anExtPropName);
				reportCommandAssignment(aCmd, theCmdStr);
				swap(sPropertyPrintName, anExtPropName);
			}
			return;
		}
	}

	logError("Unrecognized signal name for '%s %s' requested in %s",
		kSignalCommandPrefix.c_str(),
		sPropertyPrintName.c_str(),
		sSectionPrintName.c_str());
}


static void applyControlsLayerProperty(
	u16 theLayerID,
	const std::string& thePropKey,
	const std::string& thePropVal)
{
	ControlsLayer& theLayer = sLayers.vals()[theLayerID];
	const EPropertyType aPropType = propKeyToType(thePropKey);
	switch(aPropType)
	{
	case ePropType_Mouse:
		{
			const EMouseMode aMouseMode = mouseModeNameToID(thePropVal);
			if( aMouseMode >= eMouseMode_Num )
			{
				logError("Unknown mode for '%s = %s' in Layer %s!",
					sPropertyPrintName.c_str(),
					thePropVal.c_str(),
					sSectionPrintName.c_str());
			}
			else
			{
				theLayer.mouseMode = aMouseMode;
				mapDebugPrint("%s: Mouse set to '%s' mode\n",
					sSectionPrintName.c_str(),
					aMouseMode == eMouseMode_Cursor ? "Cursor" :
					aMouseMode == eMouseMode_LookTurn ? "RMB Mouse Look" :
					aMouseMode == eMouseMode_LookOnly ? "LMB Mouse Look" :
					aMouseMode == eMouseMode_AutoLook ? "Auto-Look" :
					aMouseMode == eMouseMode_AutoRunLook ? "Auto-Run-Look" :
					aMouseMode == eMouseMode_Hide ? "Hide" :
					/*otherwise*/ "Default (use other laye)" );
			}
		}
		return;
	
	case ePropType_HUD:
	case ePropType_Hotspots:
		{
			if( aPropType == ePropType_HUD )
			{
				DBG_ASSERT(theLayer.showHUD.size() == sHUDElements.size());
				DBG_ASSERT(theLayer.hideHUD.size() == sHUDElements.size());
				theLayer.showHUD.reset();
				theLayer.hideHUD.reset();
			}
			else
			{
				DBG_ASSERT(theLayer.enableHotspots.size() ==
					sHotspotArrays.size());
				DBG_ASSERT(theLayer.disableHotspots.size() ==
					sHotspotArrays.size());
				theLayer.enableHotspots.reset();
				theLayer.disableHotspots.reset();
			}

			// Break the string into individual words
			std::vector<std::string> aParsedString; aParsedString.reserve(16);
			sanitizeSentence(thePropVal, aParsedString);

			bool enable = true;
			for(size_t i = 0; i < aParsedString.size(); ++i)
			{
				const std::string& aName = aParsedString[i];
				const std::string& aUpperName = upper(aName);
				if( aUpperName == "HIDE" || aUpperName == "DISABLE" )
				{
					enable = false;
					continue;
				}
				if( aUpperName == "SHOW" || aUpperName == "ENABLE" )
				{
					enable = true;
					continue;
				}
				if( commandWordToID(aName) == eCmdWord_Filler )
					continue;
				bool foundItem = false;
				if( aPropType == ePropType_HUD )
				{
					const u16 aHUDElementID = sHUDElements.findIndex(aName);
					if( aHUDElementID < theLayer.showHUD.size() )
					{
						theLayer.showHUD.set(aHUDElementID, enable);
						theLayer.hideHUD.set(aHUDElementID, !enable);
						foundItem = true;
					}
				}
				else
				{
					const u16 aHotspotArrayID =
						sHotspotArrays.findIndex(aUpperName);
					if( aHotspotArrayID < theLayer.enableHotspots.size() )
					{
						theLayer.enableHotspots.set(aHotspotArrayID, enable);
						theLayer.disableHotspots.set(aHotspotArrayID, !enable);
						foundItem = true;
					}
				}
				if( !foundItem )
				{
					logError(
						"Could not find '%s' referenced by %s/%s = %s",
						aName.c_str(),
						sSectionPrintName.c_str(),
						sPropertyPrintName.c_str(),
						thePropVal.c_str());
				}
			}
		}
		return;

	case ePropType_Priority:
		{
			int aPriority = intFromString(thePropVal);
			if( theLayerID == 0 )
			{
				logError(
					"Root layer %s is always lowest priority. "
					"%s = %s property ignored!",
					sSectionPrintName.c_str(),
					sPropertyPrintName.c_str(),
					thePropVal.c_str());
			}
			else if( theLayer.isComboLayer )
			{
				logError(
					"Combo Layer %s ordering is derived automatically "
					"from base layers, so Priority = %s property is ignored!",
					sSectionPrintName.c_str(),
					thePropVal.c_str());
			}
			else if( aPriority < -100 || aPriority > 100 )
			{
				logError(
					"Layer %s %s = %s property "
					"must be -100 to 100 range!",
					sSectionPrintName.c_str(),
					sPropertyPrintName.c_str(),
					thePropVal.c_str());
				theLayer.priority = clamp(aPriority, -100, 100);
			}
			else
			{
				theLayer.priority = s8(aPriority);
			}
		}
		return;
	
	case ePropType_Parent:
		{
			u16 aParentLayerID = 0;
			if( !thePropVal.empty() )
			{
				aParentLayerID = sLayers.findIndex(thePropVal);
				if( aParentLayerID >= sLayers.size() )
				{
					logError("Unrecognized parent layer name '%s' for layer %s",
						thePropVal.c_str(),
						sSectionPrintName.c_str());		
					return;
				}
			}
			if( aParentLayerID == 0 )
			{
				theLayer.parentLayer = 0;
				mapDebugPrint("%s: Parent layer reset to none\n",
					sSectionPrintName.c_str());
			}
			else if( theLayerID == 0 )
			{
				logError(
					"Root layer %s can not have a Parent= layer set!",
					sSectionPrintName.c_str(),
					thePropVal.c_str());
			}
			else if( theLayer.isComboLayer )
			{
				logError(
					"\"Parent=%s\" property ignored for Combo Layer %s!",
					thePropVal.c_str(),
					sSectionPrintName.c_str());
			}
			else
			{
				theLayer.parentLayer = aParentLayerID;
				// Check for infinite parent loop
				BitVector<64> layersProcessed(sLayers.size());
				u16 aCheckLayerIdx = theLayerID;
				layersProcessed.set(aCheckLayerIdx);
				while(sLayers.vals()[aCheckLayerIdx].parentLayer != 0)
				{
					aCheckLayerIdx =
						sLayers.vals()[aCheckLayerIdx].parentLayer;
					if( layersProcessed.test(aCheckLayerIdx) )
					{
						logError("Infinite parent loop with layer %s"
							" trying to set parent layer to %s!",
							sSectionPrintName.c_str(),
							thePropVal.c_str());
						theLayer.parentLayer = 0;
						break;
					}
					layersProcessed.set(aCheckLayerIdx);
				}
				mapDebugPrint("%s: Parent layer set to '%s'\n",
					sSectionPrintName.c_str(),
					sLayers.vals()[aParentLayerID].name.c_str());
			}
		}
		return;
	}

	if( size_t aStrPos = posAfterPrefix(thePropKey, kSignalCommandPrefix) )
	{// WHEN SIGNAL
		sPropertyPrintName = sPropertyPrintName.substr(
			posAfterPrefix(sPropertyPrintName, kSignalCommandPrefix));
		addWhenSignalCommand(
			theLayerID,
			thePropKey.substr(aStrPos),
			thePropVal);
		return;
	}
	
	if( size_t aStrPos = posAfterPrefix(thePropKey, kActionOnlyPrefix) )
	{// "JUST" BUTTON ACTION
		sPropertyPrintName = "(Just) " + sPropertyPrintName.substr(
			posAfterPrefix(sPropertyPrintName, kActionOnlyPrefix));
		addButtonAction(
			theLayerID,
			thePropKey.substr(aStrPos),
			thePropVal, true);
		return;
	}

	// BUTTON COMMAND ASSIGNMENT
	addButtonAction(
		theLayerID,
		thePropKey,
		thePropVal, false);
}


static std::string breakOffMenuItemLabel(std::string& theString)
{
	// Get the label (part of the string before first single colon)
	// Double colons within label become single colons instead of end of label,
	// and are ignored (become whitespace) in the remaining string
	theString = replaceAllStr(theString, "::", "\x01");
	std::string result = breakOffItemBeforeChar(theString, ':');
	result = replaceChar(result, '\x01', ':');
	theString = replaceChar(theString, '\x01', ' ');
	return result;
}


static MenuItem stringToMenuItem(u16 theMenuID, std::string theString)
{
	MenuItem aMenuItem;
	if( theString.empty() )
		return aMenuItem;

	std::string aLabel = breakOffMenuItemLabel(theString);
	if( aLabel.empty() && !theString.empty() && theString[0] != ':' )
	{// Having no : character means this points to a sub-menu
		aMenuItem.cmd.subMenuID = getMenuID(theString, theMenuID);
		if( aMenuItem.cmd.subMenuID >= sMenus.size() )
		{
			logError("'%s / %s = %s' should be a sub-menu "
				"(no ':' character to separate label and command), "
				"but no sub-menu by that name was found! "
				"Changing to '= %s : Do Nothing'!",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str(),
				theString.c_str(), theString.c_str());
			aMenuItem.cmd.type = eCmdType_Unassigned;
			aMenuItem.label = theString;
			return aMenuItem;
		}
		aMenuItem.cmd.type = eCmdType_OpenSubMenu;
		aMenuItem.cmd.menuID =
			sMenus.vals()[aMenuItem.cmd.subMenuID].rootMenuID;
		aMenuItem.label = theString;
		return aMenuItem;
	}

	if( aLabel.empty() && !theString.empty() && theString[0] == ':' )
	{// Possibly valid command with just an empty label
		theString = trim(&theString[1]);
	}
	else
	{// Has a label, but may actually be 2 labels separated by '|'
		aMenuItem.altLabel = breakOffItemBeforeChar(aLabel, '|');
		if( aLabel[0] == '|' )
			aLabel = aLabel.substr(1);
		aMenuItem.label = aLabel;
	}

	aMenuItem.cmd.type = eCmdType_Unassigned;
	if( theString.empty() )
		return aMenuItem;

	if( theString == ".." || commandWordToID(theString) == eCmdWord_Back )
	{// Go back one sub-menu
		aMenuItem.cmd.type = eCmdType_MenuBack;
		aMenuItem.cmd.menuID = sMenus.vals()[theMenuID].rootMenuID;
		return aMenuItem;
	}

	if( commandWordToID(theString) == eCmdWord_Close )
	{// Close menu
		aMenuItem.cmd.type = eCmdType_MenuClose;
		aMenuItem.cmd.menuID = sMenus.vals()[theMenuID].rootMenuID;
		return aMenuItem;
	}

	aMenuItem.cmd = stringToCommand(theString);
	if( aMenuItem.cmd.type == eCmdType_Empty )
	{
		// Possibly just forgot the > at front of a plain string
		aMenuItem.cmd = parseChatBoxMacro(">" + theString);
		logError("%s (%s): '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box macro. "
				 "Add > to start of it if this was the intent!",
				sSectionPrintName.c_str(),
				sPropertyPrintName.c_str(),
				aMenuItem.debugLabel().c_str(),
				theString.c_str());
	}

	return aMenuItem;
}


static void applyMenuProperty(
	u16 theMenuID,
	const std::string& thePropKey,
	const std::string& thePropVal)
{
	Menu& theMenu = sMenus.vals()[theMenuID];

	const EPropertyType aPropType = propKeyToType(thePropKey);
	MenuItem* aMenuItem = null;
	switch(aPropType)
	{
	case ePropType_Label:
		theMenu.label = thePropVal;
		return;

	case ePropType_Type:
		if( theMenu.rootMenuID == theMenuID )
		{
			DBG_ASSERT(theMenu.hudElementID < sHUDElements.size());
			sHUDElements.vals()[theMenu.hudElementID].type =
				menuStyleNameToID(thePropVal);
		}
		else
		{
			logError("'%s = %s' property is ignored on sub-menus like %s!",
				sPropertyPrintName.c_str(),
				thePropVal.c_str(),
				sSectionPrintName.c_str());
		}
		return;

	case ePropType_Hotspots:
		theMenu.hotspotArrayID = sHotspotArrays.findIndex(thePropVal);
		return;

	case ePropType_GridWidth:
		theMenu.gridWidth = u8(max(0, intFromString(thePropVal)) & 0xFF);
		return;

	case ePropType_Auto:
	case ePropType_Back:
		{
			Command aCmd = stringToCommand(thePropVal);
			// Don't allow assigning menu control commands to menu items
			if( aCmd.type >= eCmdType_FirstMenuControl &&
				aCmd.type <= eCmdType_LastMenuControl )
			{ aCmd = Command(); }
			if( aPropType == ePropType_Auto )
				theMenu.autoCommand = aCmd;
			else
				theMenu.backCommand = aCmd;
			reportCommandAssignment(aCmd, thePropVal);
		}
		return;

	case ePropType_MenuItemLeft:
	case ePropType_MenuItemRight:
	case ePropType_MenuItemUp:
	case ePropType_MenuItemDown:
		aMenuItem = &theMenu.dirItems[aPropType];
		*aMenuItem = stringToMenuItem(theMenuID, thePropVal);
		break;

	case ePropType_MenuItemNumber:
		{
			const int aMenuItemID = intFromString(thePropKey);
			if( aMenuItemID < 1 )
			{
				logError("Menu items in %s should start with "
					"\"1 = Label : Command\", not %d!",
					sSectionPrintName.c_str(),
					aMenuItemID);
				return;
			}
			if( aMenuItemID > theMenu.items.size() )
			{
				theMenu.items.resize(aMenuItemID);
				gReshapeHUD.set(hudElementForMenu(theMenuID));
			}
			aMenuItem = &theMenu.items[aMenuItemID-1];
		}
		*aMenuItem = stringToMenuItem(theMenuID, thePropVal);
		break;
	}

	if( aMenuItem )
	{// Final error check and report results for menu item assignments
		std::string anOldSectionName;
		std::string anOldPropertyName;
		swap(anOldSectionName, sSectionPrintName);
		swap(anOldPropertyName, sPropertyPrintName);
		sSectionPrintName = anOldSectionName + " (" + anOldPropertyName + ")";
		sPropertyPrintName = aMenuItem->debugLabel();
		reportCommandAssignment(aMenuItem->cmd, thePropVal);
		swap(anOldSectionName, sSectionPrintName);
		swap(anOldPropertyName, sPropertyPrintName);
	}
}


static void validateMenu(u16 theMenuID)
{
	Menu& theMenu = sMenus.vals()[theMenuID];
	EHUDType aMenuStyle = menuStyle(theMenuID);

	if( aMenuStyle < eMenuStyle_Begin || aMenuStyle >= eMenuStyle_End )
	{// Guarantee menu has a valid menu style
		aMenuStyle = eMenuStyle_List;
		if( theMenu.rootMenuID == theMenuID )
		{
			logError("%s: Style = missing, not recognized, or not allowed! "
				"Setting to Style = List!",
				sSectionPrintName.c_str());
		}
	}

	if( aMenuStyle == eMenuStyle_Hotspots )
	{// Guarantee have hotspot array with at least 1 hotspot and counts match
		const u16 anArrayID = menuHotspotArray(theMenuID);
		if( anArrayID >= sHotspotArrays.size() )
		{
			aMenuStyle = eMenuStyle_List;
			if( theMenu.items.empty() )
				theMenu.items.push_back(MenuItem());
			if( theMenu.rootMenuID == theMenuID )
			{
				logError("%s: Style requires Hotspots = property but it is "
					"missing or did not match a known hotspot array! "
					"Setting to Type = List!",
					sSectionPrintName.c_str());
			}
		}
		else if( sHotspotArrays.vals()[anArrayID].size < 1 )
		{
			aMenuStyle = eMenuStyle_List;
			if( theMenu.rootMenuID == theMenuID )
			{
				logError("%s: Style requires a hotspot array but Hospots = "
					"value was for an individual hospot and not an array! "
					"Setting to Type = List!",
					sSectionPrintName.c_str());
			}
		}
		else
		{
			const HotspotArray& anArray = sHotspotArrays.vals()[anArrayID];
			if( theMenu.items.size() != anArray.size )
			{
				theMenu.items.resize(anArray.size);
				gReshapeHUD.set(hudElementForMenu(theMenuID));
			}
		}
	}
	
	if( aMenuStyle != eMenuStyle_4Dir && aMenuStyle != eMenuStyle_Hotspots )
	{// Guarantee at least 1 menu item and no gaps in menu items
		if( theMenu.items.empty() )
		{
			logError("%s: No menu items found! If empty menu intended, "
				"Set \"1 = :\" to suppress this error",
				sSectionPrintName.c_str());
			theMenu.items.push_back(MenuItem());
		}
		// Silently trim off any empty items on the end of the menu
		while(theMenu.items.size() > 1 &&
			  theMenu.items.back().cmd.type == eCmdType_Empty )
		{
			theMenu.items.resize(theMenu.items.size()-1);
			gReshapeHUD.set(hudElementForMenu(theMenuID));
		}
		// Any empty items between first and last must be a missing gap
		for(u16 i = 1; i < theMenu.items.size()-1; ++i)
		{
			if( theMenu.items[i].cmd.type == eCmdType_Empty )
			{
				logError(" %s is missing menu item #%d! "
					"Set \"%d = : \" to suppress this error",
					sSectionPrintName.c_str(), i+1, i+1);
				break;
			}
		}
	}

	// If any of above had to force a style, apply it now
	if( theMenu.rootMenuID == theMenuID )
	{
		DBG_ASSERT(theMenu.hudElementID < sHUDElements.size());
		sHUDElements.vals()[theMenu.hudElementID].type = aMenuStyle;
	}
}


static void applyHUDElementProperty(
	u16 theHUDElementID,
	const std::string& thePropKey,
	const std::string& thePropVal)
{
	HUDElement& theHUDElement = sHUDElements.vals()[theHUDElementID];
	
	if( theHUDElement.menuID < sMenus.size() )
	{// Menu-based HUD elements shouldn't be processed here!
		applyMenuProperty(theHUDElement.menuID, thePropKey, thePropVal);
		return;
	}

	const EPropertyType aPropType = propKeyToType(thePropKey);
	switch(aPropType)
	{
	case ePropType_Type:
		theHUDElement.type = hudTypeNameToID(thePropVal);
		return;
	case ePropType_Hotspot:
		theHUDElement.hotspotID = getHotspotID(thePropVal);
		return;
	case ePropType_KeyBinds:
	case ePropType_KBArray:
		theHUDElement.keyBindArrayID =
			sKeyBindArrays.findIndex(thePropVal);
		return;
	}
	// Other properties are for visuals and handled in HUD.cpp
}


static void validateHUDElement(HUDElement& theHUDElement)
{
	// Menu-based HUD elements shouldn't be processed here!
	if( theHUDElement.menuID < sMenus.size() )
		return;

	EHUDType aHUDType = theHUDElement.type;

	if( aHUDType < eHUDBaseType_Begin || aHUDType >= eHUDBaseType_End )
	{// Guarantee HUD Element has a valid type
		logError("%s: Type = missing, not recognized, or not allowed! "
			"Setting to Type = Rect!",
			sSectionPrintName.c_str());
		aHUDType = eHUDItemType_Rect;
	}

	if( aHUDType == eHUDType_KBArrayDefault ||
		aHUDType == eHUDType_KBArrayLast )
	{// Confirm has a key bind array specified
		if( theHUDElement.keyBindArrayID >= sKeyBinds.size() )
		{
			logError("%s: Type requires KeyBindArray = property but it is "
				" missing or given name did not match a known array! "
				"Setting to Type = Rect!",
				sSectionPrintName.c_str());
			aHUDType = eHUDItemType_Rect;
		}
	}
	else if( aHUDType == eHUDType_Hotspot )
	{// Confirm has a valid hotspot specified
		if( !theHUDElement.hotspotID ||
			theHUDElement.hotspotID >= sHotspots.size() )
		{
			logError("%s: Type requires Hotspot = property but it is "
				" missing or given name did not match a known hotspot! "
				"Setting to Type = Rect!",
				sSectionPrintName.c_str());
			aHUDType = eHUDItemType_Rect;
		}
	}

	// If any of above had to enforce a type, apply it now
	theHUDElement.type = aHUDType;
}


static void parseLabel(std::string& theLabel)
{
	if( theLabel.size() < 3 )
		return;

	// Search for replacement text tags in format <tag>
	bool replacementNeeded = false;
	std::pair<std::string::size_type, std::string::size_type> aTagCoords =
		findStringTag(theLabel);
	while(aTagCoords.first != std::string::npos )
	{
		const std::string& aTag = theLabel.substr(
			aTagCoords.first + 1, aTagCoords.second - 2);

		// Generate the replacement character sequence
		std::string aNewStr;
		// See if tag matches a layer name to display layer status
		u16 aLayerID = sLayers.findIndex(aTag);
		if( aLayerID > 0 && aLayerID < sLayers.size() )
		{// Set replacement to a 3-char sequence for layer ID
			aNewStr.push_back(kLayerStatusReplaceChar);
			// Encode the layer ID into 14-bit as in checkForVKeySeqPause()
			DBG_ASSERT(aLayerID <= 0x3FFF);
			aNewStr.push_back(u8((((aLayerID) >> 7) & 0x7F) | 0x80));
			aNewStr.push_back(u8(((aLayerID) & 0x7F) | 0x80));
		}
		// TODO: Button names to be replaced with their current assigned comand

		if( !aNewStr.empty() )
		{
			theLabel.replace(aTagCoords.first, aTagCoords.second, aNewStr);
			replacementNeeded = true;
		}
		aTagCoords = findStringTag(theLabel, aTagCoords.first+1);
	}

	if( replacementNeeded )
		theLabel = std::string(1, kLabelContainsDynamicText) + theLabel;
}


static void loadDataFromProfile(
	const Profile::SectionsMap& theProfileMap,
	bool init)
{
	BitVector<32> loadedHotspotArrays(sHotspotArrays.size());
	BitVector<512> loadedHotspots(sHotspots.size());
	BitVector<256> loadedKeyBinds(sKeyBinds.size());
	BitVector<256> referencedKeyBinds(sKeyBinds.size());
	BitVector<256> loadedMenus(sMenus.size());
	BitVector<32> loadedHUDElements(sHUDElements.size());
	if( init )
	{
		loadedHotspotArrays.set();
		loadedKeyBinds.set();
		loadedMenus.set();
		loadedHUDElements.set();
	}

	for(size_t aSectID = 0; aSectID < theProfileMap.size(); ++aSectID)
	{
		// Check if is a subsection, like Layer.Name or Menu.Name, etc
		const size_t aSectionKeySplit =
			theProfileMap.keys()[aSectID].find('.');
		const bool isSubSection = aSectionKeySplit != std::string::npos;
		const std::string& aSectionKey =
			isSubSection
				? theProfileMap.keys()[aSectID].substr(aSectionKeySplit+1)
				: theProfileMap.keys()[aSectID];
		const std::string& aSectionTypeName =
			isSubSection
				? theProfileMap.keys()[aSectID].substr(0, aSectionKeySplit)
				: theProfileMap.keys()[aSectID];
		const EPropertyType aPropType = propKeyToType(aSectionTypeName);
		const Profile::PropertyMap& aPropMap = theProfileMap.vals()[aSectID];
		sSectionPrintName = "[" + theProfileMap.keys()[aSectID] + "]";

		u16 aComponentID = kInvalidID;
		switch(aPropType)
		{
		case ePropType_Hotspots:
			for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
			{
				sPropertyPrintName = aPropMap.keys()[aPropIdx];
				applyHotspotProperty(
					aPropMap.keys()[aPropIdx],
					aPropMap.vals()[aPropIdx],
					loadedHotspotArrays,
					loadedHotspots);
			}
			break;
		case ePropType_KeyBinds:
			for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
			{
				sPropertyPrintName = aPropMap.keys()[aPropIdx];
				const u16 aKeyBindID = applyKeyBindProperty(
					aPropMap.keys()[aPropIdx],
					aPropMap.vals()[aPropIdx]);
				reportCommandAssignment(
					sKeyBinds.vals()[aKeyBindID],
					aPropMap.vals()[aPropIdx],
					keyBindSignalID(aKeyBindID));
			}
			break;
		case ePropType_Layer:
			if( !isSubSection )
				break;
			// fall through
		case ePropType_Scheme:
			aComponentID = sLayers.findIndex(aSectionKey);
			if( aComponentID >= sLayers.size() )
				break;
			for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
			{
				sPropertyPrintName = aPropMap.keys()[aPropIdx];
				applyControlsLayerProperty(
					aComponentID,
					aPropMap.keys()[aPropIdx],
					aPropMap.vals()[aPropIdx]);
			}
			break;
		case ePropType_Menu:
			if( !isSubSection )
				break;
			aComponentID = sMenus.findIndex(aSectionKey);
			if( aComponentID >= sMenus.size() )
				break;
			for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
			{
				sPropertyPrintName = aPropMap.keys()[aPropIdx];
				applyMenuProperty(
					aComponentID,
					aPropMap.keys()[aPropIdx],
					aPropMap.vals()[aPropIdx]);
			}
			loadedMenus.set(aComponentID);
			loadedHUDElements.set(hudElementForMenu(aComponentID));
			break;
		case ePropType_HUD:
			if( !isSubSection )
				break;
			const u16 aComponentID = sHUDElements.findIndex(aSectionKey);
			if( aComponentID >= sHUDElements.size() )
				break;
			for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
			{
				sPropertyPrintName = aPropMap.keys()[aPropIdx];
				applyHUDElementProperty(
					aComponentID,
					aPropMap.keys()[aPropIdx],
					aPropMap.vals()[aPropIdx]);
			}
			loadedHUDElements.set(aComponentID);
			if( sHUDElements.vals()[aComponentID].menuID < sMenus.size() )
				loadedMenus.set(sHUDElements.vals()[aComponentID].menuID);
			break;
		}
	}

	// Report changed hotspots (done after the fact since a single hotspot
	// property can change multiple hotspots at once due to ranges.
	#ifdef INPUT_MAP_DEBUG_PRINT
	for(int aHotspotID = loadedHotspots.firstSetBit();
		aHotspotID < loadedHotspots.size();
		aHotspotID = loadedHotspots.nextSetBit(aHotspotID+1))
	{
		mapDebugPrint("[%s]: Assigned '%s' to %s\n",
			kHotspotsSectionName,
			hotspotLabel(aHotspotID).c_str(),
			HotspotMap::hotspotToString(
				sHotspots[aHotspotID]).c_str());
	}
	#endif

	// Validate interdependent objects are configured properly
	for(int aKeyBindID = loadedKeyBinds.firstSetBit();
		aKeyBindID < loadedKeyBinds.size();
		aKeyBindID = loadedKeyBinds.nextSetBit(aKeyBindID+1))
	{
		referencedKeyBinds.reset();
		validateKeyBind(aKeyBindID, referencedKeyBinds);
	}
	for(u16 i = 0; i < sHUDElements.size(); ++i)
	{
		HUDElement& aHUDElement = sHUDElements.vals()[i];
		if( loadedHUDElements.test(i) )
		{
			sSectionPrintName = "[" + sHUDElements.vals()[i].name + "]";
			validateHUDElement(aHUDElement);
			gFullRedrawHUD.set(i);
			gReshapeHUD.set(i);
		}
		else if( !init )
		{
			switch(aHUDElement.type)
			{
			case eMenuStyle_Hotspots:
				DBG_ASSERT(aHUDElement.menuID < sMenus.size());
				{
					Menu& aMenu = sMenus.vals()[aHUDElement.menuID];
					if( loadedHotspotArrays.test(aMenu.hotspotArrayID) )
					{
						gFullRedrawHUD.set(i);
						gReshapeHUD.set(i);
					}
				}
				break;
			case eHUDType_Hotspot:
				if( loadedHotspots.test(aHUDElement.hotspotID) )
					gReshapeHUD.set(i);
				break;
			case eHUDType_HotspotGuide:
				if( loadedHotspots.any() )
					gFullRedrawHUD.set(i);
				break;
			}
		}
	}
	for(int aMenuID = loadedMenus.firstSetBit();
		aMenuID < loadedMenus.size();
		aMenuID = loadedMenus.nextSetBit(aMenuID+1))
	{
		sSectionPrintName = "[" + sMenus.vals()[aMenuID].name + "]";
		validateMenu(aMenuID);
		Menu& aMenu = sMenus.vals()[aMenuID];
		parseLabel(aMenu.label);
		for(u16 aDir = 0; aDir < ARRAYSIZE(aMenu.dirItems); ++aDir)
		{
			parseLabel(aMenu.dirItems[aDir].label);
			parseLabel(aMenu.dirItems[aDir].altLabel);
		}
		for(u16 anItem = 0; anItem < aMenu.items.size(); ++anItem)
		{
			parseLabel(aMenu.items[anItem].label);
			parseLabel(aMenu.items[anItem].altLabel);
		}
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	// Clear out any data from a previous profile
	sHotspots.clear();
	sHotspotArrays.clear();
	sCmdStrings.clear();
	sKeyBinds.clear();
	sKeyBindArrays.clear();
	sLayers.clear();
	sComboLayers.clear();
	sMenus.clear();
	sHUDElements.clear();
	sButtonHoldTimes.clear();

	// Allocate hotspot arrays
	// Start with the the special named hotspots so they get correct IDs
	for(u16 i = 0; i < eSpecialHotspot_Num - eSpecialHotspot_FirstNamed; ++i)
		createEmptyHotspotArray(kSpecialNamedHotspots[i]);
	 const Profile::PropertyMap* aPropMapPtr =
		 &Profile::getSectionProperties(kHotspotsSectionName);
	for(u16 i = 0; i < aPropMapPtr->size(); ++i)
		createEmptyHotspotArray(aPropMapPtr->keys()[i]);
	sHotspotArrays.trim();

	// Allocate hotspots and link arrays to them
	for(u16 i = 0; i < eSpecialHotspot_FirstNamed; ++i)
		sHotspots.push_back(Hotspot()); // un-named special hotspots
	for(u16 i = 0; i < sHotspotArrays.size(); ++i)
		createEmptyHotspotsForArray(i);
	if( sHotspots.size() < sHotspots.capacity() )
		std::vector<Hotspot>(sHotspots).swap(sHotspots);

	// Allocate key binds and key bind arrays
	// Start with the special keys so they get correct IDs
	for(u16 i = 0; i < eSpecialKey_Num; ++i)
		createEmptyKeyBind(kSpecialKeyBindNames[i]);
	aPropMapPtr =
		 &Profile::getSectionProperties(kKeyBindsSectionName);
	for(u16 i = 0; i < aPropMapPtr->size(); ++i)
		createEmptyKeyBind(aPropMapPtr->keys()[i]);
	sKeyBinds.trim();
	sKeyBindArrays.trim();

	// Allocate controls layers
	sLayers.setValue(kMainLayerSectionName, ControlsLayer());
	sLayers.vals().back().name = kMainLayerSectionName;
	Profile::allSections().findAllWithPrefix(
		kLayerPrefix, createEmptyLayer);
	for(u16 i = 0; i < sLayers.size(); ++i)
		linkComboLayers(i);
	sComboLayers.sort(); sComboLayers.removeDuplicates(); sComboLayers.trim();
	sLayers.trim();

	// Allocate non-menu HUD elements
	sHUDElements.setValue("~", HUDElement());
	sHUDElements.vals().back().type = eHUDType_System;
	sHUDElements.setValue("~~", HUDElement());
	sHUDElements.vals().back().type = eHUDType_HotspotGuide;
	Profile::allSections().findAllWithPrefix(
		kHUDPrefix, createEmptyHUDElement);

	// Allocate menus
	Profile::allSections().findAllWithPrefix(
		kMenuPrefix, createEmptyMenu);
	for(u16 i = 0; i < sMenus.size(); ++i)
		linkMenuToSubMenus(i);
	for(u16 i = 0; i < sMenus.size(); ++i)
		setupRootMenu(i);
	sHUDElements.trim();

	// Set sizes of other data structures that need to match above
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
	{
		sLayers.vals()[aLayerID].hideHUD.resize(sHUDElements.size());
		sLayers.vals()[aLayerID].showHUD.resize(sHUDElements.size());
		sLayers.vals()[aLayerID].disableHotspots.resize(sHotspotArrays.size());
		sLayers.vals()[aLayerID].enableHotspots.resize(sHotspotArrays.size());
	}
	gVisibleHUD.clearAndResize(sHUDElements.size());
	gRefreshHUD.clearAndResize(sHUDElements.size());
	gFullRedrawHUD.clearAndResize(sHUDElements.size());
	gReshapeHUD.clearAndResize(sHUDElements.size());
	gActiveHUD.clearAndResize(sHUDElements.size());
	gDisabledHUD.clearAndResize(sHUDElements.size());
	gConfirmedMenuItem.resize(sHUDElements.size());
	for(size_t i = 0; i < gConfirmedMenuItem.size(); ++i)
		gConfirmedMenuItem[i] = kInvalidItem;
	gKeyBindArrayLastIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayLastIndexChanged.clearAndResize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndexChanged.clearAndResize(sKeyBindArrays.size());
	gFiredSignals.clearAndResize(
		eBtn_Num + sKeyBinds.size() + sKeyBindArrays.size());

	// Fill in the data
	loadDataFromProfile(Profile::allSections(), true);
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();

	// Get default button hold time to execute eBtnAct_Hold command
	sDefaultButtonHoldTime =
		max(0, Profile::getInt("System", "ButtonHoldTime"));

	// Check for any newly-created sub-menus
	for(u16 aSectID = 0; aSectID < theProfileMap.size(); ++aSectID)
	{
		const std::string& aSectName = theProfileMap.keys()[aSectID];
		if( size_t aPrefixEnd = posAfterPrefix(aSectName, kMenuPrefix) )
		{
			const std::string& aMenuName = aSectName.substr(aPrefixEnd);
			DBG_ASSERT(!aMenuName.empty());
			const u16 aMenuID = sMenus.findOrAddIndex(aMenuName);
			Menu& aMenu = sMenus.vals()[aMenuID];
			if( !aMenu.name.empty() )
				continue;
			aMenu.name = aMenuName;
			for(u16 i = 0; i < sMenus.size(); ++i)
				linkMenuToSubMenus(i);
			// Only valid to add sub-menus, not root menus
			DBG_ASSERT(aMenu.parentMenuID != kInvalidID);
			setupRootMenu(aMenuID);	
		}
	}

	loadDataFromProfile(theProfileMap, false);
}


const char* cmdString(const Command& theCommand)
{
	DBG_ASSERT(theCommand.type == eCmdType_ChatBoxString);
	DBG_ASSERT(theCommand.stringID < sCmdStrings.size());
	return sCmdStrings.keys()[theCommand.stringID].c_str();
}


const u8* cmdVKeySeq(const Command& theCommand)
{
	DBG_ASSERT(theCommand.type == eCmdType_VKeySequence);
	DBG_ASSERT(theCommand.vKeySeqID < sCmdStrings.size());
	return (const u8*)sCmdStrings.keys()[theCommand.vKeySeqID].c_str();
}


u16 keyForSpecialAction(ESpecialKey theSpecialKeyID)
{
	DBG_ASSERT(theSpecialKeyID < eSpecialKey_Num);
	DBG_ASSERT(theSpecialKeyID < sKeyBinds.size());
	if( sKeyBinds.vals()[theSpecialKeyID].type == eCmdType_TapKey )
		return sKeyBinds.vals()[theSpecialKeyID].vKey;
	return 0;
}


u16 specialKeySignalID(ESpecialKey theSpecialKeyID)
{
	return keyBindSignalID(theSpecialKeyID);
}


Command keyBindCommand(u16 theKeyBindID)
{
	DBG_ASSERT(theKeyBindID < sKeyBinds.size());
	sKeyBinds.vals()[theKeyBindID].signalID = keyBindSignalID(theKeyBindID);
	return sKeyBinds.vals()[theKeyBindID];
}


u16 keyBindSignalID(u16 theKeyBindID)
{
	return eBtn_Num + theKeyBindID;
}


Command keyBindArrayCommand(u16 theArrayID, u16 theIndex)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays.vals()[theArrayID].size());
	return keyBindCommand(
		sKeyBindArrays.vals()[theArrayID][theIndex].keyBindID);
}


u16 keyBindArraySignalID(u16 theArrayID)
{
	return eBtn_Num + sKeyBinds.size() + theArrayID;
}


u16 offsetKeyBindArrayIndex(
	u16 theArrayID, int theIndex, int theSteps, bool wrap)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	
	const KeyBindArray& aKeyBindArray = sKeyBindArrays.vals()[theArrayID];
	int dir = theSteps < 0 ? -1 : 1;
	theSteps = abs(theSteps) + 1; // +1 is to check initial index for validity
	theIndex = clamp(theIndex, 0, int(aKeyBindArray.size())-1);

	// Try up to 2 full passes (one per direction if !wrap) to find valid entry
	for(int aCycleCount = 0; aCycleCount < 2; ++aCycleCount)
	{
		// Iterate in dir until hit edge of array or find target entry
		for(;theIndex >= 0 && theIndex < aKeyBindArray.size(); theIndex += dir)
		{
			// Entry is valid if .keyBindID != 0 and its command is valid
			if( const u16 aKeyBindID = aKeyBindArray[theIndex].keyBindID )
			{
				DBG_ASSERT(aKeyBindID < sKeyBinds.size());
				if( sKeyBinds.vals()[aKeyBindID].type > eCmdType_Unassigned )
				{
					if( --theSteps == 0 )
						return u16(theIndex);
					// Keep outer loop active as long as finding valid entries
					aCycleCount = 0;
				}
			}
		}

		// Hit array edge with steps still left...
		// If wrap, search will continue from opposite edge in same dir
		if( !wrap )
		{
			// Search in opposite direction from same edge of the array,
			// but stop at the first valid index found (likely this one)
			dir *= -1;
			theSteps = 1;
		}
		theIndex = dir < 0 ? int(aKeyBindArray.size()) - 1 : 0;
	}

	// No valid entries found after 2 full passes through array!
	return 0;
}


const Command* commandsForButton(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);

	ButtonActionsMap::const_iterator itr =
		sLayers.vals()[theLayerID].buttonMap.find(theButton);
	if( itr != sLayers.vals()[theLayerID].buttonMap.end() )
		return &itr->second.cmd[0];

	return null;
}


const VectorMap<u16, Command>& signalCommandsForLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].signalCommands;
}


u32 commandHoldTime(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);

	std::pair<u16, EButton> aKey(theLayerID, theButton);
	VectorMap<std::pair<u16, EButton>, u32>::const_iterator itr =
		sButtonHoldTimes.find(aKey);
	if( itr != sButtonHoldTimes.end() )
		return itr->second;

	return sDefaultButtonHoldTime;
}


u16 parentLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].parentLayer;
}


s8 layerPriority(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].priority;
}


EMouseMode mouseMode(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].mouseMode;
}


const BitVector<32>& hudElementsToShow(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].showHUD;
}


const BitVector<32>& hudElementsToHide(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].hideHUD;
}


const BitVector<32>& hotspotArraysToEnable(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].enableHotspots;
}


const BitVector<32>& hotspotArraysToDisable(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].disableHotspots;
}


u16 comboLayerID(u16 theLayerID1, u16 theLayerID2)
{
	std::pair<u16, u16> aKey(theLayerID1, theLayerID2);
	VectorMap<std::pair<u16, u16>, u16>::iterator itr =
		sComboLayers.find(aKey);
	if( itr != sComboLayers.end() )
		return itr->second;
	return 0;
}


Command commandForMenuItem(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus.vals()[theMenuID].items.size());
	return sMenus.vals()[theMenuID].items[theMenuItemIdx].cmd;
}


Command commandForMenuDir(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus.vals()[theMenuID].dirItems[theDir].cmd;
}


Command menuAutoCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].autoCommand;
}


Command menuBackCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].backCommand;
}


EHUDType menuStyle(u16 theMenuID)
{
	return hudElementType(hudElementForMenu(theMenuID));
}


u16 rootMenuOfMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].rootMenuID;
}


u16 menuHotspotArray(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(menuStyle(theMenuID) == eMenuStyle_Hotspots);
	u16 anArrayID = sMenus.vals()[theMenuID].hotspotArrayID;
	if( anArrayID >= sHotspotArrays.size() &&
		sMenus.vals()[theMenuID].rootMenuID != theMenuID &&
		sMenus.vals()[theMenuID].rootMenuID < sMenus.size() )
	{// Maybe root menu has a proper array to use?
		anArrayID = menuHotspotArray(sMenus.vals()[theMenuID].rootMenuID);
	}
	return anArrayID;
}


u8 menuGridWidth(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	u8 result = 1;
	if( menuStyle(theMenuID) != eMenuStyle_Grid )
		return result;
	result = sMenus.vals()[theMenuID].gridWidth;
	if( result == 0 &&
		sMenus.vals()[theMenuID].rootMenuID != theMenuID &&
		sMenus.vals()[theMenuID].rootMenuID < sMenus.size() )
	{
		result = sMenus.vals()[sMenus.vals()[theMenuID].rootMenuID].gridWidth;
	}
	const u16 aMenuItemCount = menuItemCount(theMenuID);
	// Auto-calculate based on item count
	if( result == 0 )
		result = u8(u32(ceil(sqrt(double(aMenuItemCount)))) & 0xFF);
	result = min(result, aMenuItemCount);

	return result;
}


u8 menuGridHeight(u16 theMenuID)
{
	const u16 anItemCount = menuItemCount(theMenuID);
	const u16 aGridWidth = menuGridWidth(theMenuID);
	return (anItemCount + aGridWidth - 1) / aGridWidth;
}


std::string menuSectionName(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return kMenuPrefix + sMenus.vals()[theMenuID].name;
}


std::string menuItemKeyName(u16 theMenuItemIdx)
{
	return toString(theMenuItemIdx+1);
}


std::string menuItemDirKeyName(ECommandDir theDir)
{
	DBG_ASSERT(theDir < eCmdDir_Num);
	return
		theDir == eCmdDir_L ? "L" :
		theDir == eCmdDir_R ? "R" :
		theDir == eCmdDir_U ? "U" :
		/*eCmdDir_D*/		  "D";
}


void menuItemStringToSubMenuName(std::string& theString)
{
	std::string aLabel = breakOffMenuItemLabel(theString);
	// Can't be a sub-menu if has a separate label, is empty, or
	// starts with ':' indicating a normal command
	if( !aLabel.empty() || theString.empty() || theString[0] == ':' )
		theString.clear();
}


const Hotspot& getHotspot(u16 theHotspotID)
{
	DBG_ASSERT(theHotspotID < sHotspots.size());
	return sHotspots[theHotspotID];
}


u16 firstHotspotInArray(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	HotspotArray& aHotspotArray = sHotspotArrays.vals()[theHotspotArrayID];
	return aHotspotArray.anchorIdx + 1;
}


u16 sizeOfHotspotArray(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	return sHotspotArrays.vals()[theHotspotArrayID].size;
}


const Hotspot* keyBindArrayHotspot(u16 theArrayID, u16 theIndex)
{
	Hotspot* result = null;
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays.vals()[theArrayID].size());
	theIndex = offsetKeyBindArrayIndex(theArrayID, theIndex, 0, false);
	const u16 aHotspotID =
		sKeyBindArrays.vals()[theArrayID][theIndex].hotspotID;
	DBG_ASSERT(aHotspotID < sHotspots.size());
	if( aHotspotID > 0 )
		result = &sHotspots[aHotspotID];
	return result;
}


void modifyHotspot(u16 theHotspotID, const Hotspot& theNewValues)
{
	DBG_ASSERT(theHotspotID && theHotspotID < sHotspots.size());
	sHotspots[theHotspotID] = theNewValues;
}


EHUDType hudElementType(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.vals()[theHUDElementID].type;
}


bool hudElementIsAMenu(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return
		sHUDElements.vals()[theHUDElementID].type >= eMenuStyle_Begin &&
		sHUDElements.vals()[theHUDElementID].type < eMenuStyle_End;
}


u16 menuForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.vals()[theHUDElementID].menuID;
}


u16 hudElementForMenu(u16 theMenuID)
{
	const u16 theRootMenuID = rootMenuOfMenu(theMenuID);
	DBG_ASSERT(theRootMenuID < sMenus.size());
	return sMenus.vals()[theRootMenuID].hudElementID;
}


u16 hotspotForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	DBG_ASSERT(
		sHUDElements.vals()[theHUDElementID].type == eHUDType_Hotspot);
	return sHUDElements.vals()[theHUDElementID].hotspotID;
}


u16 keyBindArrayForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	DBG_ASSERT(
		sHUDElements.vals()[theHUDElementID].type == eHUDType_KBArrayLast ||
		sHUDElements.vals()[theHUDElementID].type == eHUDType_KBArrayDefault);
	return sHUDElements.vals()[theHUDElementID].keyBindArrayID;
}


const std::string& hudElementKeyName(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.keys()[theHUDElementID];
}


u16 keyBindArrayCount()
{
	return sKeyBindArrays.size();
}


u16 keyBindArraySize(u16 theArrayID)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	return u16(sKeyBindArrays.vals()[theArrayID].size());
}


u16 controlsLayerCount()
{
	return sLayers.size();
}


u16 hudElementCount()
{
	return sHUDElements.size();
}


u16 menuCount()
{
	return sMenus.size();
}


u16 menuItemCount(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	Menu& aMenu = sMenus.vals()[theMenuID];
	if( menuStyle(theMenuID) == eMenuStyle_Hotspots )
	{
		const HotspotArray& aHotspotArray =
			sHotspotArrays.vals()[menuHotspotArray(theMenuID)];
		if( aMenu.items.size() != aHotspotArray.size )
		{
			aMenu.items.resize(aHotspotArray.size);
			gReshapeHUD.set(hudElementForMenu(theMenuID));
		}
	}
	return u16(aMenu.items.size());
}


u16 hotspotCount()
{
	return u16(sHotspots.size());
}


u16 hotspotArrayCount()
{
	return sHotspotArrays.size();
}


const std::string& layerLabel(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].name;
}


std::string hotspotLabel(u16 theHotspotID)
{
	std::string result;
	DBG_ASSERT(theHotspotID < sHotspots.size());
	if( theHotspotID < eSpecialHotspot_FirstNamed )
	{// Internal un-named hotspot
		result = toString(theHotspotID); // internal un-named hotspot
	}
	else if( theHotspotID < eSpecialHotspot_Num )
	{// Special-use named hotspot
		result = kSpecialNamedHotspots[
			theHotspotID - eSpecialHotspot_FirstNamed];
	}
	else if( sHotspotArrays.empty() )
	{// Shouldn't be possible...
		result = "ERROR - UNKNOWN";
	}
	else
	{// Should be in one of the arrays
		HotspotArray aSearchArray;
		aSearchArray.anchorIdx = theHotspotID;
		aSearchArray.hasAnchor = true;
		// StringToValueMap values aren't sorted, they are in the order added,
		// but this works because hotspots are created according to the order
		// of hotspot array creation order, thus hotspot arrays are naturally
		// sorted in the same order as the hotspots overall in the end.
		std::vector<HotspotArray>::iterator itr = std::upper_bound(
			sHotspotArrays.vals().begin(),
			sHotspotArrays.vals().end(),
			aSearchArray);
		DBG_ASSERT(itr > sHotspotArrays.vals().begin());
		--itr;
		result = itr->name;
		if( theHotspotID > itr->anchorIdx )
			result += toString(theHotspotID - itr->anchorIdx);
	}
	
	return result;
}


const std::string& hotspotArrayLabel(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	return sHotspotArrays.vals()[theHotspotArrayID].name;
}


const std::string& menuLabel(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	if( !sMenus.vals()[theMenuID].label.empty() )
		return sMenus.vals()[theMenuID].label;
	return sMenus.vals()[theMenuID].name;
}


const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus.vals()[theMenuID].items.size());
	return sMenus.vals()[theMenuID].items[theMenuItemIdx].label;
}


const std::string& menuItemAltLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus.vals()[theMenuID].items.size());
	return sMenus.vals()[theMenuID].items[theMenuItemIdx].altLabel;
}


const std::string& menuDirLabel(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus.vals()[theMenuID].dirItems[theDir].label;
}

#undef mapDebugPrint
#undef INPUT_MAP_DEBUG_PRINT

} // InputMap
