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
const char* kButtonAliasSectionName = "ButtonNames";
const char* kKeyBindsSectionName = "KeyBinds";
const char* kHotspotsSectionName = "Hotspots";
const char* kButtonRebindsPrefix = "Gamepad";
const char* k4DirCmdSuffix[] = { " Left", " Right", " Up", " Down" };
const char* k4DirKeySuffix[] = { "LEFT", "RIGHT", "UP", "DOWN" };
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

const char* kSpecialKeyNames[] =
{
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
DBG_CTASSERT(ARRAYSIZE(kSpecialKeyNames) == eSpecialKey_Num);

enum EPropertyType
{
	// These 4 must be first and match ECommandDir order
	ePropType_MenuItemLeft,		// eCmdDir_L
	ePropType_MenuItemRight,	// eCmdDir_R
	ePropType_MenuItemUp,		// eCmdDir_U
	ePropType_MenuItemDown,		// eCmdDir_D

	ePropType_Mouse,
	ePropType_HUD,
	ePropType_Hotspot,
	ePropType_HotspotArray,
	ePropType_Parent,
	ePropType_Priority,
	ePropType_Auto,
	ePropType_Back,
	ePropType_Type,
	ePropType_KBArray,
	ePropType_Label,
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
				{ "HUD",			ePropType_HUD			}, 
				{ "HOTSPOT",		ePropType_Hotspot		},
				{ "HOT",			ePropType_Hotspot		},
				{ "SPOT",			ePropType_Hotspot		},
				{ "POINT",			ePropType_Hotspot		},
				{ "HOTSPOTS",		ePropType_HotspotArray	},
				{ "MOUSE",			ePropType_Mouse			},
				{ "CURSOR",			ePropType_Mouse			},
				{ "PARENT",			ePropType_Parent		},
				{ "PRIORITY",		ePropType_Priority		},
				{ "AUTO",			ePropType_Auto			},
				{ "BACK",			ePropType_Back			},
				{ "TYPE",			ePropType_Type			},
				{ "STYLE",			ePropType_Type			},
				{ "KEYBINDARRAY",	ePropType_KBArray		},
				{ "KBARRAY",		ePropType_KBArray		},
				{ "KEYBINDARRAY",	ePropType_KBArray		},
				{ "ARRAY",			ePropType_KBArray		},
				{ "KEYBINDS",		ePropType_KBArray		},
				{ "LABEL",			ePropType_Label			},
				{ "TITLE",			ePropType_Label			},
				{ "NAME",			ePropType_Label			},
				{ "STRING",			ePropType_Label			},
				{ "TO",				ePropType_Filler		},
				{ "AT",				ePropType_Filler		},
				{ "ON",				ePropType_Filler		},
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
	Command cmd;
	u16 hotspotID;
};

struct KeyBindArray : public std::vector<KeyBindArrayEntry>
{
	u16 signalID;

	KeyBindArray() : signalID() {}
};

struct MenuItem
{
	std::string label;
	std::string altLabel;
	Command cmd;
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

	Menu() :
		parentMenuID(kInvalidID),
		rootMenuID(kInvalidID),
		hudElementID(kInvalidID),
		hotspotArrayID(kInvalidID)
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
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<Hotspot> sHotspots;
static StringToValueMap<HotspotArray> sHotspotArrays;
static StringToValueMap<bool> sCmdStrings;
static StringToValueMap<Command> sKeyBinds;
static StringToValueMap<KeyBindArray> sKeyBindArrays;
static StringToValueMap<ControlsLayer> sLayers;
static VectorMap<std::pair<u16, u16>, u16> sComboLayers;
static StringToValueMap<Menu> sMenus;
static StringToValueMap<HUDElement> sHUDElements;
static StringToValueMap<std::string> sButtonAliases; // TODO: remove?
static u16 sSpecialKeys[eSpecialKey_Num];
static VectorMap<std::pair<u16, EButton>, u32> sButtonHoldTimes;
static u32 sDefaultButtonHoldTime = 400;
static std::string sDebugItemName; // TODO: remove
static std::string sDebugSubItemName; // TODO: remove


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
			logError("Pause time in a key sequence can not exceed 16 seconds!");
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


static std::string signalIDToString(u16 theSignalID)
{
	std::string result;
	if( theSignalID > 0 )
	{
		// Add the signal flag character
		result.push_back(kVKeyFireSignal);

		// Encode the signal ID into 14-bit as in checkForVKeySeqPause()
		result.push_back(u8(((theSignalID >> 7) & 0x7F) | 0x80));
		result.push_back(u8((theSignalID & 0x7F) | 0x80));
	}

	return result;
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
		case VK_PAUSE:
		case kVKeyFireSignal:
		case kVKeyForceRelease:
		case kVKeyMouseJump:
		case kVKeySeqHasMouseJump:
		case kVKeyStartChatString:
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

	bool hasMouseJump = false;
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
				hasMouseJump = true;
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
				if( aResult == eResult_Ok )
					hasMouseJump = true;
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

			// Check if it is an alias to another sequence (key bind)
			if( Command* aKeyBind = sKeyBinds.find(aName) )
			{
				if( aKeyBind->type == eCmdType_SignalOnly )
				{
					result += signalIDToString(aKeyBind->signalID);
					continue;
				}
				if( aKeyBind->type == eCmdType_TapKey )
				{
					result += signalIDToString(aKeyBind->signalID);
					const u16 aVKey = aKeyBind->vKey;
					if( aVKey & kVKeyShiftFlag ) result += VK_SHIFT;
					if( aVKey & kVKeyCtrlFlag ) result += VK_CONTROL;
					if( aVKey & kVKeyAltFlag ) result += VK_MENU;
					if( aVKey & kVKeyWinFlag ) result += VK_LWIN;
					if( aVKey & kVKeyMask ) result += u8(aVKey & kVKeyMask);
					continue;
				}
				if( aKeyBind->type == eCmdType_VKeySequence )
				{
					result += signalIDToString(aKeyBind->signalID);
					DBG_ASSERT(
						aKeyBind->vKeySeqID < sCmdStrings.size());
					const std::string& anEmbedSeq =
						sCmdStrings.keys()[aKeyBind->vKeySeqID];
					DBG_ASSERT(!anEmbedSeq.empty());
					if( anEmbedSeq[0] == kVKeySeqHasMouseJump )
					{
						result += anEmbedSeq.substr(3);
						hasMouseJump = true;
					}
					else
					{
						result += anEmbedSeq;
					}
					continue;
				}
				if( aKeyBind->type == eCmdType_ChatBoxString )
				{
					result += signalIDToString(aKeyBind->signalID);
					DBG_ASSERT(
						aKeyBind->stringID < sCmdStrings.size());
					result += kVKeyStartChatString;
					result += sCmdStrings.keys()[aKeyBind->stringID];
					continue;
				}
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

	if( hasMouseJump )
	{
		std::string aStr;
		aStr.push_back(kVKeySeqHasMouseJump);
		aStr.push_back(char(u8(0x80)));
		aStr.push_back(char(u8(0x80)));
		// Determine if running this sequence will leave cursor in a different
		// position, which means jumping to a hotspot and NOT clicking on it.
		bool usesClick = false;
		for(int i = int(result.size()-1); i >= 0; --i)
		{
			switch(result[i])
			{
			case kVKeyMouseJump:
				if( !usesClick )
				{
					aStr[1] = result[i+1];
					aStr[2] = result[i+2];
					i = 0; // to break out of loop
				}
				break;
			case VK_LBUTTON:
			case VK_MBUTTON:
			case VK_RBUTTON:
				usesClick = true;
				break;
			case VK_PAUSE:
			case kVKeyStartChatString:
				usesClick = false;
				break;
			}
		}
		result = aStr + result;
	}

	return result;
}


static Command parseChatBoxMacro(const std::string& theString)
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

	// If have no \n in string, set as normal ChatBoxString command
	size_t aLineBreakPos = theString.find("\\n");
	if( aLineBreakPos == std::string::npos )
	{
		result.type = eCmdType_ChatBoxString;
		result.stringID = sCmdStrings.findOrAddIndex(theString + "\r");
		return result;
	}

	// Treat multi-line macros as a VKey sequence of embedded strings
	std::string aVKeySeq;
	std::string aStrLeft = theString;
	for(;;)
	{
		std::string aStrToAdd = aStrLeft.substr(0, aLineBreakPos);
		if( aStrToAdd.length() > 1 )
		{
			aVKeySeq.push_back(kVKeyStartChatString);
			aVKeySeq += aStrToAdd + "\r";
		}

		if( aLineBreakPos == std::string::npos )
			break;

		aStrLeft = aStrLeft.substr(aLineBreakPos+2);
		if( aStrLeft.empty() )
			break;

		if( aStrLeft[0] != '<' && aStrLeft[0] != '/' )
			aStrLeft = std::string(">") + aStrLeft;
		aLineBreakPos = aStrLeft.find("\\n");
	}

	if( aVKeySeq.empty() )
	{
		result.type = eCmdType_DoNothing;
		return result;
	}

	result.type = eCmdType_VKeySequence;
	result.vKeySeqID = sCmdStrings.findOrAddIndex(aVKeySeq);
	return result;
}


static void createEmptyLayer(const std::string& theName)
{
	DBG_ASSERT(!theName.empty());
	// Layers with + in the name are combo layers and should be skipped here
	if( theName.find('+') != std::string::npos )
		return;
	ControlsLayer& aLayer = sLayers.findOrAdd(condense(theName));
	if( aLayer.name.empty() )
		aLayer.name = theName;
}


static u16 getLayerID(const std::string& theName)
{
	return sLayers.findIndex(condense(theName));
}


static void createComboLayer(const std::string& theComboName)
{
	DBG_ASSERT(!theComboName.empty());
	
	std::string aSecondLayerName = theComboName;
	std::string aFirstLayerName = breakOffItemBeforeChar(
		aSecondLayerName, kComboLayerDeliminator);
	if( aFirstLayerName.empty() || aSecondLayerName.empty() )
		return;

	const u16 aFirstLayerID = getLayerID(aFirstLayerName);
	if( aFirstLayerID >= sLayers.size() )
	{
		logError("Base layer [%s] not found for combo layer [%s]",
			(kLayerPrefix + aFirstLayerName).c_str(),
			(kLayerPrefix + theComboName).c_str());
		return;
	}

	// Second layer may itself be a combo layer (or dummy combo layer)
	createComboLayer(aSecondLayerName);
	const u16 aSecondLayerID = getLayerID(aSecondLayerName);
	if( aSecondLayerID >= sLayers.size() )
	{
		logError("Base layer [%s] not found for combo layer [%s]",
			(kLayerPrefix + aSecondLayerName).c_str(),
			(kLayerPrefix + theComboName).c_str());
		return;
	}

	if( aFirstLayerID == aSecondLayerID )
	{
		logError("Specified same layer twice in combo layer name '%s'!",
			theComboName.c_str());
		return;
	}

	const u16 aComboLayerID =
		sLayers.findOrAddIndex(condense(theComboName));
	ControlsLayer& aComboLayer = sLayers.vals()[aComboLayerID];
	if( aComboLayer.name.empty() )
		aComboLayer.name = theComboName;
	aComboLayer.isComboLayer = true;
	std::pair<u16, u16> aComboLayerKey(aFirstLayerID, aSecondLayerID);
	sComboLayers.setValue(aComboLayerKey, aComboLayerID);
}


static void createEmptyHUDElement(
	const std::string& theName,
	EHUDType theType = eHUDType_Num)
{
	DBG_ASSERT(!theName.empty());
	HUDElement& aHUDElement = sHUDElements.findOrAdd(condense(theName));
	if( aHUDElement.name.empty() )
	{
		aHUDElement.name = theName;
		aHUDElement.type = theType;
	}
}


static u16 getHUDElementID(const std::string& theName)
{
	return sHUDElements.findIndex(condense(theName));
}


static void createEmptyMenu(const std::string& theName)
{
	DBG_ASSERT(!theName.empty());
	Menu& aMenu = sMenus.findOrAdd(condense(theName));
	if( aMenu.name.empty() )
		aMenu.name = theName;
}


static void linkMenuToSubMenus(u16 theMenuID)
{
	// Find all menus whose key starts with this menu's key
	const std::string& aPrefix =
		sMenus.keys()[theMenuID] + kSubMenuDeliminator;
	StringToValueMap<Menu>::IndexVector aVec;
	sMenus.findAllWithPrefix(aPrefix, aVec);
	for(size_t i = 0; i < aVec.size(); ++i)
	{
		Menu& aSubMenu = sMenus.vals()[aVec[i]];
		const std::string& aPotentialLabel =
			aSubMenu.name.substr(posAfterPrefix(aSubMenu.name, aPrefix));
		if( aSubMenu.label.empty() || 
			aSubMenu.label.size() > aPotentialLabel.size() )
		{
			aSubMenu.parentMenuID = theMenuID;
			aSubMenu.label = aPotentialLabel;
		}
	}
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
	u16 result = sMenus.findIndex(condense(theName));
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

	return sMenus.findIndex(aParentPath + "." + condense(theMenuName));
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
		ECommandKeyWord aKeyWordID = commandWordToID(upper(theWords[0]));
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
		ECommandKeyWord aKeyWordID = commandWordToID(upper(theWords[i]));
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
			u16 aHotspotID = getHotspotID(condense(theWords[itr->second]));
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
		ECommandKeyWord aKeyWordID = commandWordToID(upper(*aSecondLayerName));
		while(aSecondLayerName && allowedKeyWords.test(aKeyWordID))
		{
			if( aSecondLayerName == &theWords.back() )
			{
				aSecondLayerName = null;
				break;
			}
			++aSecondLayerName;
			aKeyWordID = commandWordToID(upper(*aSecondLayerName));
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
	{
		aLayerID = getLayerID(*aLayerName);
		if( aLayerID >= sLayers.size() )
		{
			// TODO - logError?
		}
	}
	u16 aSecondLayerID = kInvalidID;
	if( aSecondLayerName )
	{
		aSecondLayerID = getLayerID(*aSecondLayerName);
		if( aSecondLayerID >= sLayers.size() )
		{
			// TODO - logError?
		}
		if( aLayerID == aSecondLayerID )
		{
			aSecondLayerID = kInvalidID;
			// TODO - logError? - Actually isn't this a fine way to "reload" a layer?
		}
	}
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
	{
		aMenuID = getRootMenuID(*aMenuName);
		if( aMenuID >= sMenus.size() )
		{
			// TODO - logError?
		}
	}

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
			result.menuItemIdx = result.count;
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
	{
		aKeyBindArrayID = sKeyBindArrays.findIndex(
			condense(*aKeyBindArrayName));
	}

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
	// instead of becoming _TapKey w/ whatever TurnLeft is bound to.
	// This makes sure special code for some of these commands is utilized
	// when use the key bind name instead of "MoveTurn" and such.

	static StringToValueMap<Command> sSpecialKeyBindMap;
	if( sSpecialKeyBindMap.empty() )
	{
		sSpecialKeyBindMap.reserve(7); // <-- update this if add more!
		Command aCmd;
		aCmd.type = eCmdType_StartAutoRun;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_AutoRun]), aCmd);
		aCmd.type = eCmdType_MoveTurn;
		aCmd.dir = eCmdDir_Forward;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_MoveF]), aCmd);
		aCmd.dir = eCmdDir_Back;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_MoveB]), aCmd);
		aCmd.dir = eCmdDir_Left;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_TurnL]), aCmd);
		aCmd.dir = eCmdDir_Right;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_TurnR]), aCmd);
		aCmd.type = eCmdType_MoveStrafe;
		aCmd.dir = eCmdDir_Left;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_StrafeL]), aCmd);
		aCmd.dir = eCmdDir_Right;
		sSpecialKeyBindMap.setValue(
			condense(kSpecialKeyNames[eSpecialKey_StrafeR]), aCmd);
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

	// Check for alias to a keybind
	if( result.type == eCmdType_Empty )
	{
		std::string aKeyBindName = condense(theString);
		if( Command* aSpecialKeyBindCommand =
				specialKeyBindNameToCommand(aKeyBindName) )
		{
			if( allowHoldActions ||
				aSpecialKeyBindCommand->type < eCmdType_FirstContinuous )
			{
				result = *aSpecialKeyBindCommand;
				return result;
			}
		}

		if( Command* aKeyBindCommand = sKeyBinds.find(aKeyBindName) )
		{
			result = *aKeyBindCommand;
			// Check if this keybind is part of an array, and if so, use
			// eCmdType_KeyBindArrayIndex instead so "last" will update
			const int anArrayIdx = breakOffIntegerSuffix(aKeyBindName);
			if( anArrayIdx >= 0 )
			{
				u16 aKeyBindArrayID = sKeyBindArrays.findIndex(aKeyBindName);
				if( aKeyBindArrayID < sKeyBindArrays.size() )
				{
					// Comment on _PressAndHoldKey above explains this
					if( result.type == eCmdType_TapKey && allowHoldActions )
						result.type = eCmdType_KeyBindArrayHoldIndex;
					else
						result.type = eCmdType_KeyBindArrayIndex;
					result.keybindArrayID = aKeyBindArrayID;
					result.arrayIdx = anArrayIdx;
				}
			}
		}
	}

	// Check for Virtual-Key Code sequence
	if( result.type == eCmdType_Empty )
	{
		const std::string& aVKeySeq = namesToVKeySequence(aParsedString);
		if( !aVKeySeq.empty() )
		{
			result.type = eCmdType_VKeySequence;
			result.vKeySeqID = sCmdStrings.findOrAddIndex(aVKeySeq);
		}
	}

	return result;
}


static void createEmptyHotspotArray(const std::string& theName)
{
	const std::string& theKey = condense(theName);

	// Check if name ends in a number and thus could be part of an array
	std::string anArrayKey = theKey;
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
				// TODO - logError - bad range specification
				isAnchorHotspot = true;
			}
		}
	}

	// Restore full key string for an anchor name that may have been modified
	if( isAnchorHotspot && aRangeEndIdx >= 0 )
		anArrayKey = theKey;

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
			// TODO - logError: overlap with previous range
			return; // skip adding
		}
	}

	// Check for overlap with next range (if any)
	if( itr != anArray.ranges.end() )
	{
		const HotspotRange& aNextRange = *itr;
		if( aNewRange.lastIdx() >= aNextRange.firstIdx )
		{
			// TODO - logError: overlap with next range
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
			// TODO - logError - missing hotspot
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
	const std::string& theName,
	const std::string& theDesc)
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
	HotspotArray* anArray = sHotspotArrays.find(anArrayKey);
	DBG_ASSERT(anArray);
	DBG_ASSERT(!isAnchorHotspot || anArray->hasAnchor);

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
				theName.c_str(), theDesc.c_str());
			aHotspot = Hotspot();
		}
		else if( aResult == eResult_Malformed )
		{
			logError("Hotspot %s: Could not decipher hotspot position '%s'",
				theName.c_str(), theDesc.c_str());
			aHotspot = Hotspot();
		}
		else if( !aHotspotDesc.empty() && aHotspotDesc[0] == '*' )
		{
			if( !isAnchorHotspot || anArray->ranges.empty() )
			{
				logError(
					"Hotspot %s: Only array anchor hotspots can specify "
					"an offset scale factor (using '*')!",
					theName.c_str());
			}
			else
			{
				anOffsetScale = floatFromString(aHotspotDesc.substr(1));
				if( anOffsetScale == 0 )
				{
					logError("Hotspot %s: Invalid offset scale factor '%s'",
						theName.c_str(), aHotspotDesc.c_str());
				}
			}
		}
	}

	std::vector<HotspotRange>::iterator aRange;
	if( isAnchorHotspot )
	{
		// Skip any further work if no different than previous setting
		if( sHotspots[anArray->anchorIdx] == aHotspot &&
			(anOffsetScale == 0 || anOffsetScale == anArray->offsetScale) )
		{ return; }

		sHotspots[anArray->anchorIdx] = aHotspot;
		if( anOffsetScale )
			anArray->offsetScale = anOffsetScale;

		// Start scanning at first range for dependent hotspots
		aRange = anArray->ranges.begin();
	}
	else
	{
		// Find matching range entry to apply the property to
		HotspotRange aCmpRange;
		aCmpRange.firstIdx = aRangeStartIdx;
		aCmpRange.count = aRangeEndIdx - aRangeStartIdx + 1;
		std::vector<HotspotRange>::iterator itr = std::lower_bound(
			anArray->ranges.begin(), anArray->ranges.end(), aCmpRange);

		// Exit if none match exactly (should already have warned)
		if( itr == anArray->ranges.end() ||
			itr->firstIdx != aCmpRange.firstIdx ||
			itr->lastIdx() != aCmpRange.lastIdx() )
		{ return; }

		if( itr->count == 1 && !itr->offsetFromPrev )
		{// Might have own anchors - set hotspot directly for now
			itr->hasOwnXAnchor = aHotspot.x.anchor != 0 || !anArray->hasAnchor;
			itr->hasOwnYAnchor = aHotspot.y.anchor != 0 || !anArray->hasAnchor;
			if( itr->hasOwnXAnchor && itr->hasOwnYAnchor &&
				sHotspots[anArray->anchorIdx + itr->firstIdx] == aHotspot )
			{ return; } // skip scan for dependent changes
			sHotspots[anArray->anchorIdx + itr->firstIdx] = aHotspot;
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
			anArray->size = 0;
			for(itr = anArray->ranges.begin();
				itr != anArray->ranges.end() && !itr->removed; ++itr)
			{ anArray->size = itr->lastIdx(); }
		}
	}

	// Update actual hotspots to reflect stored offsets in array data
	for(bool rangeAffected = true;
		aRange != anArray->ranges.end() && (rangeAffected || isAnchorHotspot);
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

		for(u16 aHotspotID = aRange->firstIdx + anArray->anchorIdx;
			aHotspotID <= aRange->lastIdx() + anArray->anchorIdx;
			++aHotspotID)
		{
			const u16 aBaseHotspotID =
				aRange->offsetFromPrev ? aHotspotID - 1 :
				anArray->hasAnchor ? anArray->anchorIdx : 0;
			if( aRange->hasOwnXAnchor )
			{
				aHotspot.x = sHotspots[aHotspotID].x;
			}
			else
			{
				aHotspot.x.anchor = sHotspots[aBaseHotspotID].x.anchor;
				aHotspot.x.offset = sHotspots[aBaseHotspotID].x.offset;
				aHotspot.x.offset += aRange->xOffset * anArray->offsetScale;
			}
			if( aRange->hasOwnYAnchor )
			{
				aHotspot.y = sHotspots[aHotspotID].y;
			}
			else
			{
				aHotspot.y.anchor = sHotspots[aBaseHotspotID].y.anchor;
				aHotspot.y.offset = sHotspots[aBaseHotspotID].y.offset;
				aHotspot.y.offset += aRange->yOffset * anArray->offsetScale;
			}
			if( aHotspot != sHotspots[aHotspotID] )
			{
				rangeAffected = true;
				sHotspots[aHotspotID] = aHotspot;
			}
		}
	}
}


static void buildHotspots()
{
	mapDebugPrint("Assigning hotspots...\n");

	Profile::PropertyMap aPropMap =
		Profile::getSectionProperties(kHotspotsSectionName);
	for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
	{
		applyHotspotProperty(
			aPropMap.keys()[aPropIdx],
			aPropMap.vals()[aPropIdx].name,
			aPropMap.vals()[aPropIdx].val);
	}
}


static void reportCommandAssignment(
	const std::string& theSection,
	const std::string& theItemName,
	const Command& theCmd,
	const std::string& theCmdStr,
	bool isKeyBind = false)
{
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		logError("%s: Not sure how to assign '%s' to '%s'!",
			theSection.c_str(),
			theItemName.c_str(),
			theCmdStr.c_str());
		break;
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		mapDebugPrint("%s: Assigned '%s' to <Do Nothing>\n",
			theSection.c_str(),
			theItemName.c_str());
		break;
	case eCmdType_SignalOnly:
		mapDebugPrint("%s: Assigned '%s' to <Signal #%d Only>\n",
			theSection.c_str(),
			theItemName.c_str(),
			theCmd.signalID);
		break;
	case eCmdType_ChatBoxString:
		{
			std::string aMacroString = sCmdStrings.keys()[theCmd.stringID];
			aMacroString.resize(aMacroString.size()-1);
			if( aMacroString[0] == kVKeyFireSignal )
				aMacroString = aMacroString.substr(3);
			mapDebugPrint("%s: Assigned '%s' to macro: %s%s\n",
				theSection.c_str(),
				theItemName.c_str(),
				aMacroString.c_str(),
				isKeyBind
					? (std::string(" <signal #") +
						toString(theCmd.signalID) + ">").c_str()
					: "");
		}
		break;
	case eCmdType_TapKey:
	case eCmdType_PressAndHoldKey:
		mapDebugPrint("%s: Assigned '%s' to: %s%s%s%s%s%s\n",
			theSection.c_str(),
			theItemName.c_str(),
			!!(theCmd.vKey & kVKeyShiftFlag) ? "Shift+" : "",
			!!(theCmd.vKey & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(theCmd.vKey & kVKeyAltFlag) ? "Alt+" : "",
			!!(theCmd.vKey & kVKeyWinFlag) ? "Win+" : "",
			virtualKeyToName(theCmd.vKey & kVKeyMask).c_str(),
			isKeyBind
				? (std::string(" <signal #") +
					toString(theCmd.signalID) + ">").c_str()
				: "");
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("%s: Assigned '%s' to sequence: %s%s\n",
			theSection.c_str(),
			theItemName.c_str(),
			theCmdStr.c_str(),
			isKeyBind
				? (std::string(" <signal #") +
					toString(theCmd.signalID) + ">").c_str()
				: "");
		break;
	default:
		mapDebugPrint("%s: Assigned '%s' to command: %s\n",
			theSection.c_str(),
			theItemName.c_str(),
			theCmdStr.c_str());
		break;
	}
}


static Command stringToKeyBindCommand(
	const std::string& theCmdStr,
	u16& theSignalID)
{
	// Keybinds can only be assigned to direct input - not special commands
	Command aCmd;
	if( theCmdStr.empty() )
	{// Do nothing but send a signal
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	// Check for a chat box macro
	aCmd = parseChatBoxMacro(theCmdStr);
	if( aCmd.type == eCmdType_DoNothing )
	{
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}
	if( aCmd.type != eCmdType_Empty )
	{
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	ECommandKeyWord aKeyWord = commandWordToID(condense(theCmdStr));
	if( aKeyWord == eCmdWord_Nothing )
	{// Specifically requested signal only
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	// Tap key
	std::vector<std::string> aParsedString; aParsedString.reserve(16);
	sanitizeSentence(theCmdStr, aParsedString);
	if( u16 aVKey = namesToVKey(aParsedString) )
	{
		aCmd.type = eCmdType_TapKey;
		aCmd.signalID = theSignalID++;
		aCmd.vKey = aVKey;
		return aCmd;
	}

	// VKey Sequence
	const std::string& aVKeySeq = namesToVKeySequence(aParsedString);
	if( aVKeySeq.empty() )
		return aCmd;

	aCmd.type = eCmdType_VKeySequence;
	aCmd.signalID = theSignalID++;
	aCmd.vKeySeqID = sCmdStrings.findOrAddIndex(aVKeySeq);

	return aCmd;
}


static Command createKeyBindEntry(
	const std::string& theAlias,
	const std::string& theCmdStr,
	u16& theSignalID)
{
	const Command& aCmd = stringToKeyBindCommand(theCmdStr, theSignalID);

	// Check if could be part of a keybind array
	std::string aKeyBindArrayName = theAlias;
	const int anArrayIdx = breakOffIntegerSuffix(aKeyBindArrayName);
	if( anArrayIdx >= 0 )
	{
		KeyBindArray& aKeyBindArray =
			sKeyBindArrays.findOrAdd(condense(aKeyBindArrayName));
		if( anArrayIdx >= aKeyBindArray.size() )
			aKeyBindArray.resize(anArrayIdx+1);
		aKeyBindArray[anArrayIdx].cmd = aCmd;
		// Check for a matching named hotspot
		u16 aHotspotID = getHotspotID(
			condense(aKeyBindArrayName) + toString(anArrayIdx));
		if( aHotspotID )
			aKeyBindArray[anArrayIdx].hotspotID = aHotspotID;
		mapDebugPrint("%s: Assigned '%s' Key Bind Array index #%d to %s%s\n",
			sDebugItemName.c_str(),
			aKeyBindArrayName.c_str(),
			anArrayIdx,
			theAlias.c_str(),
			aHotspotID ? " (+ hotspot)" : "");
	}

	return aCmd;
}


static void buildButtonAliases()
{
	mapDebugPrint("Assigning custom button names...\n");
	const Profile::PropertyMap& aPropMap =
		Profile::getSectionProperties(kButtonAliasSectionName);
	for(size_t i = 0; i < aPropMap.size(); ++i)
	{
		sButtonAliases.setValue(
			aPropMap.keys()[i],
			condense(aPropMap.vals()[i].val));
	}
}


static void buildKeyBinds()
{
	mapDebugPrint("Assigning Key Binds...\n");
	sDebugItemName = "[";
	sDebugItemName += kKeyBindsSectionName;
	sDebugItemName[sDebugItemName.size()-1] = ']';

	// Directly fetch all the special keys first
	u16 aNextSignalID;
	for(u16 i = 0; i < eSpecialKey_Num; ++i)
	{
		const std::string anAlias = kSpecialKeyNames[i];
		std::string aCmdStr =
			Profile::getStr(kKeyBindsSectionName, anAlias);
		aNextSignalID = eBtn_Num + i;
		Command aCmd = createKeyBindEntry(anAlias, aCmdStr, aNextSignalID);
		if( aCmd.type != eCmdType_SignalOnly &&
			aCmd.type != eCmdType_TapKey )
		{
			logError(
				"Special key bind '%s' may only be assigned to a "
				"single key or modifier+key (or nothing)! "
				"Could not assign %s!",
				anAlias.c_str(), aCmdStr.c_str());
			aCmd.type = eCmdType_SignalOnly;
			aCmd.signalID = eBtn_Num + i;
		}
		else
		{
			reportCommandAssignment(
				sDebugItemName, anAlias, aCmd, aCmdStr, true);
		}
		DBG_ASSERT(aCmd.signalID == eBtn_Num + i);
		sSpecialKeys[i] = aCmd.type == eCmdType_TapKey ? aCmd.vKey : 0;
	}
	// Add the special keys to the key binds map
	for(u16 i = 0; i < eSpecialKey_Num; ++i)
	{
		Command aCmd; aCmd.vKey = sSpecialKeys[i];
		aCmd.type = aCmd.vKey ? eCmdType_TapKey : eCmdType_SignalOnly;
		aCmd.signalID = eBtn_Num + i;
		sKeyBinds.setValue(
			condense(kSpecialKeyNames[i]), aCmd);
	}

	// Now process all the rest of the key binds
	const Profile::PropertyMap& aPropMap =
		Profile::getSectionProperties(kKeyBindsSectionName);
	aNextSignalID = eBtn_Num + eSpecialKey_Num;
	BitVector<128> keyBindsProcessed;
	keyBindsProcessed.clearAndResize(aPropMap.size());
	// Mark the already-processed special keys
	for(size_t i = 0; i < aPropMap.size(); ++i)
	{
		if( sKeyBinds.contains(aPropMap.keys()[i]) )
			keyBindsProcessed.set(i);
	}

	// Keep trying to add key binds until a pass doesn't add any at all,
	// ignoring ones that can't be processed yet due to interdependencies.
	// Then do one final pass on the remaining ones that allows errors
	// to be shown (via reportCommandAssignment()), which will also cause
	// keyBindsProcessed.all() to become true to end this loop.
	bool newKeyBindAdded = false;
	bool showErrors = false;
	while(!keyBindsProcessed.all())
	{
		for(size_t i = 0; i < aPropMap.size(); ++i)
		{
			if( keyBindsProcessed.test(i) )
				continue;

			const std::string& anAlias = aPropMap.vals()[i].name;
			const std::string& aCmdStr = aPropMap.vals()[i].val;

			const Command& aCmd = createKeyBindEntry(
					anAlias, aCmdStr, aNextSignalID);
			if( aCmd.type != eCmdType_Empty || showErrors )
			{
				sKeyBinds.setValue(condense(anAlias), aCmd);
				newKeyBindAdded = true;
				keyBindsProcessed.set(i);
				reportCommandAssignment(
					sDebugItemName, anAlias, aCmd, aCmdStr, true);
			}
		}
		showErrors = !newKeyBindAdded;
		newKeyBindAdded = false;
	}

	// Assign signal ID's to key bind arrays, which fire whenever ANY key in
	// the array is used, in addition to the specific key's signal
	for(u16 i = 0; i < sKeyBindArrays.size(); ++i)
		sKeyBindArrays.vals()[i].signalID = aNextSignalID++;

	// Can now also set size of global variables related to key binds
	gKeyBindArrayLastIndex.reserve(sKeyBindArrays.size());
	gKeyBindArrayLastIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.reserve(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayLastIndexChanged.clearAndResize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndexChanged.clearAndResize(sKeyBindArrays.size());
	gFiredSignals.clearAndResize(aNextSignalID);
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


static EButton buttonNameToID(const std::string& theName)
{
	if( std::string* aBtnName = sButtonAliases.find(theName) )
		return ::buttonNameToID(*aBtnName);
	return ::buttonNameToID(theName);
}


static void reportButtonAssignment(
	const Command& theCmd,
	const std::string& theCmdStr,
	bool onlySpecificAction)
{
	#ifndef INPUT_MAP_DEBUG_PRINT
	if( theCmd.type == eCmdType_Empty )
	#endif
	{
		std::string aSection = "[";
		aSection += sDebugItemName.c_str();
		aSection += "]";
		std::string anItemName = sDebugSubItemName;
		if( onlySpecificAction )
			anItemName = std::string("(Just) ") + anItemName;
		reportCommandAssignment(aSection, anItemName, theCmd, theCmdStr);
	}
}


static void addButtonAction(
	u16 theLayerIdx,
	std::string theBtnName,
	const std::string& theCmdStr,
	bool onlySpecificAction)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theBtnName.empty() || theCmdStr.empty() )
		return;

	// Determine button & action to assign command to
	EButtonAction aBtnAct = breakOffButtonAction(theBtnName);
	int aBtnTime = breakOffIntegerSuffix(theBtnName);
	std::string aBtnKeyName = condense(theBtnName);
	EButton aBtnID = buttonNameToID(aBtnKeyName);

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
			aBtnID = buttonNameToID(aBtnKeyName + k4DirKeySuffix[i]);
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
			// Get destination of command. Note that we do this AFTER
			// parsing the command because stringToCommand() can lead to
			// resizing sLayers and thus make this pointer invalid!
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
			std::string anExtPropName =
				sDebugSubItemName + k4DirCmdSuffix[i];
			if( isA4DirMultiAssign )
				swap(sDebugSubItemName, anExtPropName);
			reportButtonAssignment(aCmd, aCmdStr, onlySpecificAction);
			if( isA4DirMultiAssign )
				swap(sDebugSubItemName, anExtPropName);
		}
		return;
	}

	if( aBtnTime >= 0 && aBtnID >= eBtn_Num )
	{// Part of the button's name might have been absorbed into aBtnTime
		std::string aTimeAsString = toString(aBtnTime);
		do {
			theBtnName.push_back(aTimeAsString[0]);
			aTimeAsString.erase(0, 1);
			aBtnKeyName = condense(theBtnName);
			aBtnID = buttonNameToID(aBtnKeyName);
			aBtnTime = aTimeAsString.empty()
				? -1 : intFromString(aTimeAsString);
		} while(aBtnID >= eBtn_Num && !aTimeAsString.empty());
	}

	// If still no valid button ID, must just be a badly-named action + button key
	if( aBtnID >= eBtn_Num )
	{
		logError("Unable to identify Gamepad Button '%s' requested in [%s]",
			theBtnName.c_str(),
			sDebugItemName.c_str());
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
	reportButtonAssignment(aCmd, theCmdStr, onlySpecificAction);
}


static void addSignalCommand(
	u16 theLayerIdx,
	std::string theSignalName,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theSignalName.empty() || theCmdStr.empty() )
		return;

	// Check for responding to use of any key in a key bind array
	std::string aSignalKey = condense(theSignalName);
	if( KeyBindArray* aKeyBindArray = sKeyBindArrays.find(aSignalKey) )
	{
		Command aCmd = stringToCommand(theCmdStr, true);
		if( aCmd.type != eCmdType_Empty )
		{
			sLayers.vals()[theLayerIdx].signalCommands.setValue(
				aKeyBindArray->signalID, aCmd);
		}

		// Report the results of the assignment
		#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
		if( aCmd.type == eCmdType_Empty ) 
		#endif
		{
			std::string aSection = "[";
			aSection += sDebugItemName;
			aSection += "]";
			std::string anItemName = kSignalCommandPrefix;
			anItemName += " ";
			anItemName += sDebugSubItemName;
			anItemName += " (signal #";
			anItemName += toString(aKeyBindArray->signalID);
			anItemName += ")";
			reportCommandAssignment(aSection, anItemName, aCmd, theCmdStr);
		}
		return;
	}

	// Check for responding to use of a key bind
	if( Command* aKeyBind = sKeyBinds.find(aSignalKey) )
	{
		Command aCmd = stringToCommand(theCmdStr, true);
		if( aCmd.type != eCmdType_Empty )
		{
			sLayers.vals()[theLayerIdx].signalCommands.setValue(
				aKeyBind->signalID, aCmd);
		}

		// Report the results of the assignment
		#ifndef INPUT_MAP_DEBUG_PRINT // only report error (empty)
		if( aCmd.type == eCmdType_Empty )
		#endif
		{
			std::string aSection = "[";
			aSection += sDebugItemName;
			aSection += "]";
			std::string anItemName = kSignalCommandPrefix;
			anItemName += " ";
			anItemName += sDebugSubItemName;
			anItemName += " (signal #";
			anItemName += toString(aKeyBind->signalID);
			anItemName += ")";
			reportCommandAssignment(aSection, anItemName, aCmd, theCmdStr);
		}
		return;
	}

	// Check for responding to use of of "Move", "MoveTurn", and "MoveStrafe"
	switch(commandWordToID(aSignalKey))
	{
	case eCmdWord_Move:
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeL], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeR], theCmdStr);
		// fall through
	case eCmdWord_Turn:
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveF], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveB], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_TurnL], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_TurnR], theCmdStr);
		return;
	case eCmdWord_Strafe:
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveF], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveB], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeL], theCmdStr);
		addSignalCommand(theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeR], theCmdStr);
		return;
	}

	// For button press signals, need to actually use the word "press"
	// Other button actions (tap, hold, release) are not supported as signals
	if( breakOffButtonAction(theSignalName) == eBtnAct_Press )
	{
		aSignalKey = condense(theSignalName);
		EButton aBtnID = buttonNameToID(aSignalKey);
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
				aBtnID = buttonNameToID(aSignalKey + k4DirKeySuffix[i]);
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
					std::string aSection = "[";
					aSection += sDebugItemName.c_str();
					aSection += "]";
					std::string anItemName = kSignalCommandPrefix;
					anItemName += " ";
					anItemName += sDebugSubItemName;
					anItemName += k4DirCmdSuffix[i];
					anItemName += " (signal #";
					anItemName += toString(aBtnID);
					anItemName += ")";
					reportCommandAssignment(
						aSection, anItemName, aCmd, theCmdStr);
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
				std::string aSection = "[";
				aSection += sDebugItemName.c_str();
				aSection += "]";
				std::string anItemName = kSignalCommandPrefix;
				anItemName += " ";
				anItemName += sDebugSubItemName;
				anItemName += " (signal #";
				anItemName += toString(aBtnID);
				anItemName += ")";
				reportCommandAssignment(
					aSection, anItemName, aCmd, theCmdStr);
			}
			return;
		}
	}

	logError("Unrecognized signal name for '%s %s' requested in [%s]",
		kSignalCommandPrefix.c_str(),
		theSignalName.c_str(),
		sDebugItemName.c_str());
}


static void applyControlsLayerProperty(
	ControlsLayer& theLayer, u16 theLayerID,
	const std::string& thePropKey,
	const std::string& thePropName,
	const std::string& thePropVal)
{
	const EPropertyType aPropType = propKeyToType(thePropKey);
	switch(aPropType)
	{
	case ePropType_Mouse:
		{
			const EMouseMode aMouseMode =
				mouseModeNameToID(condense(thePropVal));
			if( aMouseMode >= eMouseMode_Num )
			{
				logError("Unknown mode for '%s = %s' in Layer [%s]!",
					thePropName.c_str(),
					thePropVal.c_str(),
					sDebugItemName.c_str());
			}
			else
			{
				theLayer.mouseMode = aMouseMode;
				mapDebugPrint("[%s]: Mouse set to '%s' mode\n",
					sDebugItemName.c_str(),
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
	case ePropType_HotspotArray:
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
				if( commandWordToID(aUpperName) == eCmdWord_Filler )
					continue;
				bool foundItem = false;
				if( aPropType == ePropType_HUD )
				{
					const u16 aHUDElementID = getHUDElementID(aUpperName);
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
						"Could not find '%s' referenced by [%s]/%s = %s",
						aName.c_str(),
						sDebugItemName.c_str(),
						thePropName.c_str(),
						thePropVal.c_str());
				}
			}
		}
		return;

	case ePropType_Priority:
		{
			int aPriority = intFromString(thePropVal);
			if( aPriority < -100 || aPriority > 100 )
			{
				logError(
					"Layer [%s] %s = %d property "
					"must be -100 to 100 range!",
					sDebugItemName.c_str(),
					thePropName.c_str(),
					thePropVal.c_str());
				aPriority = clamp(aPriority, -100, 100);
			}
			if( theLayerID == 0 )
			{
				logError(
					"Root layer [%s] is always lowest priority. "
					"%s = %d property ignored!",
					sDebugItemName.c_str(),
					thePropName.c_str(),
					thePropVal.c_str());
			}
			else if( theLayer.isComboLayer )
			{
				logError(
					"Combo Layer [%s] ordering is derived automatically "
					"from base layers, so Priority = %d property is ignored!",
					sDebugItemName.c_str(),
					thePropVal.c_str());
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
				aParentLayerID = getLayerID(thePropVal);
				if( aParentLayerID >= sLayers.size() )
				{
					// TODO - logError
					return;
				}
			}
			if( aParentLayerID == 0 )
			{
				theLayer.parentLayer = 0;
				mapDebugPrint("[%s]: Parent layer reset to none\n",
					sDebugItemName.c_str());
			}
			else if( theLayerID == 0 )
			{
				logError(
					"Root layer [%s] can not have a Parent= layer set!",
					sDebugItemName.c_str(),
					thePropVal.c_str());
			}
			else if( theLayer.isComboLayer )
			{
				logError(
					"\"Parent=%s\" property ignored for Combo Layer [%s]!",
					thePropVal.c_str(),
					sDebugItemName.c_str());
			}
			else
			{
				theLayer.parentLayer = aParentLayerID;
				// Check for infinite parent loop
				BitVector<64> layersProcessed;
				layersProcessed.clearAndResize(sLayers.size());
				u16 aCheckLayerIdx = theLayerID;
				layersProcessed.set(aCheckLayerIdx);
				while(sLayers.vals()[aCheckLayerIdx].parentLayer != 0)
				{
					aCheckLayerIdx =
						sLayers.vals()[aCheckLayerIdx].parentLayer;
					if( layersProcessed.test(aCheckLayerIdx) )
					{
						logError("Infinite parent loop with layer [%s]"
							" trying to set parent layer to %s!",
							sDebugItemName.c_str(),
							thePropVal.c_str());
						theLayer.parentLayer = 0;
						break;
					}
					layersProcessed.set(aCheckLayerIdx);
				}
				mapDebugPrint("[%s]: Parent layer set to '%s'\n",
					sDebugItemName.c_str(),
					sLayers.vals()[aParentLayerID].name.c_str());
			}
		}
		return;
	}

	if( size_t aStrPos = posAfterPrefix(thePropName, kSignalCommandPrefix) )
	{// WHEN SIGNAL
		sDebugSubItemName = thePropName.substr(aStrPos);
		addSignalCommand(
			theLayerID,
			thePropName.substr(aStrPos),
			thePropVal);
		return;
	}
	
	if( size_t aStrPos = posAfterPrefix(thePropName, kActionOnlyPrefix) )
	{// "JUST" BUTTON ACTION
		sDebugSubItemName = thePropName.substr(aStrPos);
		addButtonAction(
			theLayerID,
			thePropName.substr(aStrPos),
			thePropVal, true);
		return;
	}

	// BUTTON COMMAND ASSIGNMENT
	sDebugSubItemName = thePropName;
	addButtonAction(
		theLayerID,
		thePropName,
		thePropVal, false);
}


static void buildControlScheme()
{
	mapDebugPrint("Building control scheme layers...\n");

	for(u16 i = 0; i < sLayers.size(); ++i)
	{
		ControlsLayer& aLayer = sLayers.vals()[i];
		aLayer.enableHotspots.clearAndResize(sHotspotArrays.size());
		aLayer.disableHotspots.clearAndResize(sHotspotArrays.size());
		const std::string& aLayerSectName =
			(i == 0 ? "" : kLayerPrefix) + sLayers.keys()[i];
		sDebugItemName =
			(i == 0 ? "" : kLayerPrefix) + aLayer.name;
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(aLayerSectName);
		for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
		{
			applyControlsLayerProperty(
				aLayer, i,
				aPropMap.keys()[aPropIdx],
				aPropMap.vals()[aPropIdx].name,
				aPropMap.vals()[aPropIdx].val);
		}
	}
}


static void applyHUDElementProperty(
	HUDElement& theElement,
	const std::string& thePropKey,
	const std::string& thePropName,
	const std::string& thePropVal)
{
	// This is only for non-menu HUD elements
	const EPropertyType aPropType = propKeyToType(thePropKey);
	switch(aPropType)
	{
	case ePropType_Type:
		theElement.type =
			hudTypeNameToID(condense(thePropVal));
		// TODO - logError if unrecognized type
		return;
	case ePropType_Hotspot:
		theElement.hotspotID = getHotspotID(condense(thePropVal));
		// TODO - logError if unrecognized hotspot name (0)
		return;
	case ePropType_KBArray:
		theElement.keyBindArrayID =
			sKeyBindArrays.findIndex(condense(thePropVal));
		// TODO - logError if unrecognized key bind array name
		return;
	}
	// Other properties are for visuals and handled in HUD.cpp
}


static void validateHUDElement(HUDElement& theHUDElement)
{
	EHUDType aHUDType = theHUDElement.type;

	if( aHUDType < eHUDBaseType_Begin || aHUDType >= eHUDBaseType_End )
	{// Guarantee HUD Element has a valid type
		// TODO - logError - hud type not set correctly
		aHUDType = eHUDItemType_Rect;
	}

	if( aHUDType == eHUDType_KBArrayDefault ||
		aHUDType == eHUDType_KBArrayLast )
	{// Confirm has a key bind array specified
		if( theHUDElement.keyBindArrayID >= sKeyBinds.size() )
		{
			// TODO - logError - no key bind array set
			aHUDType = eHUDItemType_Rect;
		}
	}
	else if( aHUDType == eHUDType_Hotspot )
	{// Confirm has a valid hotspot specified
		if( !theHUDElement.hotspotID ||
			theHUDElement.hotspotID >= sHotspots.size() )
		{
			// TODO - logError - no proper hotspot set
			aHUDType = eHUDItemType_Rect;
		}
	}

	// If any of above had to enforce a type, apply it now
	theHUDElement.type = eHUDItemType_Rect;
}


static void buildHUDElements()
{
	mapDebugPrint("Building Non-Menu HUD Elements...\n");

	// Skip first 2 HUD elements (internal use only)
	for(u16 i = 2; i < sHUDElements.size(); ++i)
	{
		HUDElement& aHUDElement = sHUDElements.vals()[i];
		const std::string& aHUDElementSectName =
			kHUDPrefix + sHUDElements.keys()[i];
		sDebugItemName = kHUDPrefix + aHUDElement.name;
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(aHUDElementSectName);
		for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
		{
			applyHUDElementProperty(
				aHUDElement,
				aPropMap.keys()[aPropIdx],
				aPropMap.vals()[aPropIdx].name,
				aPropMap.vals()[aPropIdx].val);
		}
		validateHUDElement(aHUDElement);
	}
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
	aMenuItem.cmd.type = eCmdType_Unassigned;
	if( theString.empty() )
	{
		mapDebugPrint("%s: Left <unnamed> and <unassigned>!\n",
			sDebugSubItemName.c_str());
		return aMenuItem;
	}

	std::string aLabel = breakOffMenuItemLabel(theString);
	if( aLabel.empty() && !theString.empty() && theString[0] != ':' )
	{// Having no : character means this points to a sub-menu
		aMenuItem.cmd.type = eCmdType_OpenSubMenu;
		aMenuItem.cmd.subMenuID = getMenuID(theString, theMenuID);
		if( aMenuItem.cmd.subMenuID >= sMenus.size() )
		{
			// TODO - log error & change to do nothing command
		}
		aMenuItem.cmd.menuID =
			sMenus.vals()[aMenuItem.cmd.subMenuID].rootMenuID;
		aMenuItem.label =
			sMenus.vals()[aMenuItem.cmd.subMenuID].label;
		mapDebugPrint("%s: Sub-Menu: '%s'\n",
			sDebugSubItemName.c_str(),
			aMenuItem.label.c_str());
		return aMenuItem;
	}

	if( aLabel.empty() && !theString.empty() && theString[0] == ':' )
	{// Possibly valid command with just an empty label
		theString = trim(&theString[1]);
		aLabel = "<unnamed>";
	}
	else
	{// Has a label, but may actually be 2 labels separated by '|'
		aMenuItem.altLabel = breakOffItemBeforeChar(aLabel, '|');
		if( aLabel[0] == '|' )
			aLabel = aLabel.substr(1);
		aMenuItem.label = aLabel;
		if( !aMenuItem.altLabel.empty() )
			aLabel += std::string(" (") + aMenuItem.altLabel + ")";
	}

	if( theString.empty() )
	{
		mapDebugPrint("%s: '%s' left <unassigned>!\n",
			sDebugSubItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	if( theString == ".." ||
		commandWordToID(condense(theString)) == eCmdWord_Back )
	{// Go back one sub-menu
		aMenuItem.cmd.type = eCmdType_MenuBack;
		aMenuItem.cmd.menuID = sMenus.vals()[theMenuID].rootMenuID;
		mapDebugPrint("%s: '%s' assigned to back out of menu\n",
			sDebugSubItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	if( commandWordToID(condense(theString)) == eCmdWord_Close )
	{// Close menu
		aMenuItem.cmd.type = eCmdType_MenuClose;
		aMenuItem.cmd.menuID = sMenus.vals()[theMenuID].rootMenuID;
		mapDebugPrint("%s: '%s' assigned to close menu\n",
			sDebugSubItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	aMenuItem.cmd = stringToCommand(theString);
	if( aMenuItem.cmd.type == eCmdType_Empty )
	{
		// Probably just forgot the > at front of a plain string
		aMenuItem.cmd = parseChatBoxMacro(std::string(">") + theString);
		logError("%s: '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box macro. "
				 "Add > to start of it if this was the intent!",
				sDebugSubItemName.c_str(),
				aLabel.c_str(), theString.c_str());
	}
	else
	{
		reportCommandAssignment(sDebugSubItemName,
			aLabel, aMenuItem.cmd, theString);
	}

	return aMenuItem;
}


static void applyMenuProperty(
	Menu& theMenu, u16 theMenuID,
	const std::string& thePropKey,
	const std::string& thePropName,
	const std::string& thePropVal)
{
	const EPropertyType aPropType = propKeyToType(thePropKey);
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
				menuStyleNameToID(condense(thePropVal));
			// TODO - logError if unrecognized type
		}
		else
		{
			// TODO - logError (can't set on sub-type)
		}
		return;

	case ePropType_Auto:
	case ePropType_Back:
		{
			Command aCmd = stringToCommand(thePropVal);
			if( aCmd.type == eCmdType_Empty ||
				(aCmd.type >= eCmdType_FirstMenuControl &&
				 aCmd.type <= eCmdType_LastMenuControl) )
			{
				// TODO - logError - invalid command
				return;
			}
			if( aPropType == ePropType_Auto )
				theMenu.autoCommand = aCmd;
			else
				theMenu.backCommand = aCmd;
			mapDebugPrint("[%s] (%s): Assigned to command: %s\n",
				sDebugItemName.c_str(),
				thePropName.c_str(),
				thePropVal.c_str());
		}
		return;

	case ePropType_HotspotArray:
		theMenu.hotspotArrayID =
			sHotspotArrays.findIndex(condense(thePropVal));
		// TODO - logError if unrecognized hotspot array name
		return;

	case ePropType_MenuItemLeft:
	case ePropType_MenuItemRight:
	case ePropType_MenuItemUp:
	case ePropType_MenuItemDown:
		sDebugSubItemName =
			"[" + sDebugItemName + "] (" + thePropName + ")";
		theMenu.dirItems[aPropType] =
			stringToMenuItem(theMenuID, thePropVal);
		return;

	case ePropType_MenuItemNumber:
		{
			const int aMenuItemIdx = intFromString(thePropKey);
			if( aMenuItemIdx < 1 )
			{
				// TODO - logError
				return;
			}
			sDebugSubItemName =
				"[" + sDebugItemName + "] (" + thePropName + ")";
			if( aMenuItemIdx > theMenu.items.size() )
				theMenu.items.resize(aMenuItemIdx);
			theMenu.items[aMenuItemIdx-1] =
				stringToMenuItem(theMenuID, thePropVal);
		}
		return;
	}
}


static void validateMenu(Menu& theMenu, u16 theMenuID)
{
	EHUDType aMenuStyle = menuStyle(theMenuID);

	if( aMenuStyle < eMenuStyle_Begin || aMenuStyle >= eMenuStyle_End )
	{// Guarantee menu has a valid menu style
		aMenuStyle = eMenuStyle_List;
		if( theMenu.rootMenuID == theMenuID )
		{
			// TODO - logError - menu style not set correctly
		}
	}

	if( aMenuStyle == eMenuStyle_Hotspots )
	{// Guarantee have hotspot array with at least 1 hotspot and counts match
		const u16 anArrayID = menuHotspotArray(theMenuID);
		if( anArrayID >= sHotspotArrays.size() )
		{
			aMenuStyle = eMenuStyle_List;
			if( theMenu.rootMenuID == theMenuID )
			{
				// TODO errorLog - need valid array specified
			}
		}
		else if( sHotspotArrays.vals()[anArrayID].size < 1 )
		{
			aMenuStyle = eMenuStyle_List;
			if( theMenu.rootMenuID == theMenuID )
			{
				// TODO errorLog - need array with at least 1 hotspot
			}
		}
		else
		{
			const HotspotArray& anArray = sHotspotArrays.vals()[anArrayID];
			if( theMenu.items.size() != anArray.size )
				theMenu.items.resize(anArray.size);
		}
	}

	if( theMenu.items.empty() &&
		theMenu.dirItems[eCmdDir_L].cmd.type == eCmdType_Empty &&
		theMenu.dirItems[eCmdDir_R].cmd.type == eCmdType_Empty &&
		theMenu.dirItems[eCmdDir_U].cmd.type == eCmdType_Empty &&
		theMenu.dirItems[eCmdDir_D].cmd.type == eCmdType_Empty )
	{// Guarantee at least 1 menu item
		logError("[%s]: No menu items found! If empty menu intended, "
			"Set \"%s = :\" to suppress this error",
			(kMenuPrefix + theMenu.name).c_str(),
			aMenuStyle == eMenuStyle_4Dir ? "U" : "1");
		if( aMenuStyle != eMenuStyle_4Dir )
			theMenu.items.push_back(MenuItem());
	}

	// If any of above had to force a style, apply it now
	if( theMenu.rootMenuID == theMenuID )
	{
		DBG_ASSERT(theMenu.hudElementID < sHUDElements.size());
		sHUDElements.vals()[theMenu.hudElementID].type = aMenuStyle;
	}
}


static void buildMenus()
{
	mapDebugPrint("Building Menus...\n");

	for(u16 i = 0; i < sMenus.size(); ++i)
	{
		Menu& aMenu = sMenus.vals()[i];
		const std::string& aMenuSectName =
			kMenuPrefix + sMenus.keys()[i];
		sDebugItemName = kMenuPrefix + aMenu.name;
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(aMenuSectName);
		for(u16 aPropIdx = 0; aPropIdx < aPropMap.size(); ++aPropIdx)
		{
			applyMenuProperty(
				aMenu, i,
				aPropMap.keys()[aPropIdx],
				aPropMap.vals()[aPropIdx].name,
				aPropMap.vals()[aPropIdx].val);
		}
		validateMenu(aMenu, i);
	}
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
		const std::string& aTag = condense(theLabel.substr(
			aTagCoords.first + 1, aTagCoords.second - 2));

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


static void buildLabels()
{
	mapDebugPrint("Parsing labels for text replacements...\n");
	for(u16 i = 0; i < sMenus.size(); ++i)
	{
		parseLabel(sMenus.vals()[i].label);
		for(u16 aDir = 0; aDir < ARRAYSIZE(sMenus.vals()[i].dirItems); ++aDir)
		{
			parseLabel(sMenus.vals()[i].dirItems[aDir].label);
			parseLabel(sMenus.vals()[i].dirItems[aDir].altLabel);
		}
		for(u16 anItem = 0; anItem < sMenus.vals()[i].items.size(); ++anItem)
		{
			parseLabel(sMenus.vals()[i].items[anItem].label);
			parseLabel(sMenus.vals()[i].items[anItem].altLabel);
		}
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	// Clear out any data from a previous profile
	ZeroMemory(&sSpecialKeys, sizeof(sSpecialKeys));
	sHotspots.clear();
	sHotspotArrays.clear();
	sCmdStrings.clear();
	sKeyBinds.clear();
	sKeyBindArrays.clear();
	sLayers.clear();
	sComboLayers.clear();
	sMenus.clear();
	sHUDElements.clear();
	sButtonAliases.clear();
	sButtonHoldTimes.clear();

	// Get default button hold time to execute eBtnAct_Hold command
	sDefaultButtonHoldTime =
		max(0, Profile::getInt("System", "ButtonHoldTime"));

	// Allocate controls layers
	std::vector<std::string> aSectionNameList;
	Profile::getSectionNamesStartingWith(kLayerPrefix, aSectionNameList);
	createEmptyLayer(kMainLayerSectionName); // Main layer - [Scheme]
	for(size_t i = 0; i < aSectionNameList.size(); ++i)
		createEmptyLayer(aSectionNameList[i]);
	for(size_t i = 0; i < aSectionNameList.size(); ++i)
		createComboLayer(aSectionNameList[i]);
	sLayers.trim();

	// Allocate non-menu HUD elements
	createEmptyHUDElement("~", eHUDType_System);
	createEmptyHUDElement("~~", eHUDType_HotspotGuide);
	aSectionNameList.clear();
	Profile::getSectionNamesStartingWith(kHUDPrefix, aSectionNameList);
	for(size_t i = 0; i < aSectionNameList.size(); ++i)
		createEmptyHUDElement(aSectionNameList[i]);

	// Allocate menus
	aSectionNameList.clear();
	Profile::getSectionNamesStartingWith(kMenuPrefix, aSectionNameList);
	for(size_t i = 0; i < aSectionNameList.size(); ++i)
		createEmptyMenu(aSectionNameList[i]);
	for(u16 i = 0; i < sMenus.size(); ++i)
		linkMenuToSubMenus(i);
	for(u16 i = 0; i < sMenus.size(); ++i)
		setupRootMenu(i);
	sHUDElements.trim();

	// Allocate hotspot arrays
	// Start with the the special named hotspots so they get correct IDs
	for(u16 i = 0; i < eSpecialHotspot_Num - eSpecialHotspot_FirstNamed; ++i)
		createEmptyHotspotArray(kSpecialNamedHotspots[i]);
	const Profile::PropertySection* const aHotspotsSection =
		Profile::getSection(kHotspotsSectionName);
	for(u16 i = 0; i < aHotspotsSection->properties.size(); ++i)
		createEmptyHotspotArray(aHotspotsSection->properties.vals()[i].name);

	// Allocate hotspots and link arrays to them
	for(u16 i = 0; i < eSpecialHotspot_FirstNamed; ++i)
		sHotspots.push_back(Hotspot()); // un-named special hotspots
	for(u16 i = 0; i < sHotspotArrays.size(); ++i)
		createEmptyHotspotsForArray(i);
	if( sHotspots.size() < sHotspots.capacity() )
		std::vector<Hotspot>(sHotspots).swap(sHotspots);

	// Set sizes of other data structures that need to match above
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
	{
		sLayers.vals()[aLayerID].hideHUD.resize(sHUDElements.size());
		sLayers.vals()[aLayerID].showHUD.resize(sHUDElements.size());
	}
	gVisibleHUD.clearAndResize(sHUDElements.size());
	gRedrawHUD.clearAndResize(sHUDElements.size());
	gFullRedrawHUD.clearAndResize(sHUDElements.size());
	gReshapeHUD.clearAndResize(sHUDElements.size());
	gActiveHUD.clearAndResize(sHUDElements.size());
	gDisabledHUD.clearAndResize(sHUDElements.size());
	gConfirmedMenuItem.resize(sHUDElements.size());
	for(size_t i = 0; i < gConfirmedMenuItem.size(); ++i)
		gConfirmedMenuItem[i] = kInvalidItem;

	// Fill in the data
	buildHotspots();
	buildButtonAliases();
	buildKeyBinds();
	buildControlScheme();
	buildHUDElements();
	buildMenus();
	buildLabels();
}


void loadProfileChanges()
{
	// TODO
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


u16 keyForSpecialAction(ESpecialKey theAction)
{
	DBG_ASSERT(theAction < eSpecialKey_Num);
	return sSpecialKeys[theAction];
}


const Command& keyBindArrayCommand(u16 theArrayID, u16 theIndex)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays.vals()[theArrayID].size());
	return sKeyBindArrays.vals()[theArrayID][theIndex].cmd;
}


u16 keyBindArraySignalID(u16 theArrayID)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	return sKeyBindArrays.vals()[theArrayID].signalID;
}


u16 offsetKeyBindArrayIndex(
	u16 theArrayID, u16 theIndex, s16 theOffset, bool wrap)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays.vals()[theArrayID].size());
	KeyBindArray& aKeyBindArray = sKeyBindArrays.vals()[theArrayID];
	// The last command in the array should never be empty
	DBG_ASSERT(aKeyBindArray.back().cmd.type != eCmdType_Empty);
	while(aKeyBindArray[theIndex].cmd.type == eCmdType_Empty)
		++theIndex;
	while(theOffset < 0 && (wrap || theIndex > 0) )
	{
		if( theIndex-- == 0 )
			theIndex = wrap ? u16(aKeyBindArray.size()-1) : 0;
		if( aKeyBindArray[theIndex].cmd.type != eCmdType_Empty )
			++theOffset;
	}
	while(theOffset > 0)
	{
		if( theIndex++ >= aKeyBindArray.size()-1 )
			theIndex = wrap ? 0 : u16(aKeyBindArray.size()-1);
		if( aKeyBindArray[theIndex].cmd.type != eCmdType_Empty )
			--theOffset;
	}
	while(aKeyBindArray[theIndex].cmd.type == eCmdType_Empty)
		++theIndex;
	return theIndex;
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
	DBG_ASSERT(anArrayID < sHotspotArrays.size());
	return anArrayID;
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


void reloadHotspotKey(
	const std::string& theHotspotName,
	StringToValueMap<u16>& theHotspotNameMapCache,
	StringToValueMap<u16>& theHotspotArrayNameMapCache)
{
	// TODO - remove with new system
}


void reloadAllHotspots()
{
	// TODO - remove with new system
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
	return u16(sMenus.vals()[theMenuID].items.size());
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
