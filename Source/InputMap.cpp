//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputMap.h"

#include "Lookup.h"
#include "Profile.h"

namespace InputMap
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

// Whether or not debug messages print depends on which line is commented out
#define mapDebugPrint(...) debugPrint("InputMap: " __VA_ARGS__)
//#define mapDebugPrint(...) ((void)0)

enum {
kMacroSlotsPerSet = 4,
};

const char* kMainMacroSetLabel = "Macros";
const std::string kModePrefix("Mode");
const char* kKeybindsPrefix("KeyBinds/");
const char* kMacroSlotLabel[] = { "U", "L", "R", "D" };
DBG_CTASSERT(ARRAYSIZE(kMacroSlotLabel) == kMacroSlotsPerSet);

// These need to be in all upper case
const char* kParentModeKeys[] = { "DEFAULT", "INHERITS", "PARENT" };
const char* kHUDSettingsKey = "HUD";
const char* kMouseLookKey = "MOUSELOOK";
const char* k4DirButtons[] =
{	"LS", "LSTICK", "LEFTSTICK", "LEFT STICK", "DPAD",
	"RS", "RSTICK", "RIGHTSTICK", "RIGHT STICK", "FPAD" };

const char* kButtonActionPrefx[] =
{
	"",			// eBtnAct_PressAndHold
	"Press",	// eBtnAct_Press
	"Hold",		// eBtnAct_ShortHold
	"LongHold",	// eBtnAct_LongHold
	"Tap",		// eBtnAct_Tap
	"Release",	// eBtnAct_Release
	"",			// eBtnAct_HoldRelease
	"",			// eBtnAct_Analog
};
DBG_CTASSERT(ARRAYSIZE(kButtonActionPrefx) == eBtnAct_Num);


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct MacroSlot
{
	std::string label;
	Command cmd;
};

struct MacroSet
{
	std::string label;
	MacroSlot slot[kMacroSlotsPerSet];
};

struct ButtonActionID
{
	EButton btn : 8;
	EButtonAction act : 8;

	ButtonActionID() : btn(eBtn_Num), act(eBtnAct_Num) {}
	ButtonActionID(EButton theBtn, EButtonAction theAct)
		: btn(theBtn), act(theAct) {}
	operator bool() const
		{ return btn != eBtn_Num && act != eBtnAct_Num; }
	bool operator<(const ButtonActionID& rhs) const
		{ return btn < rhs.btn || (btn == rhs.btn && act < rhs.act); }
	bool operator==(const ButtonActionID& rhs) const
		{ return btn == rhs.btn && act == rhs.act; }
	bool operator!=(const ButtonActionID& rhs) const
		{ return !(*this == rhs); }
};
typedef VectorMap<ButtonActionID, Command> ButtonActionsCommands;

struct ControlsMode
{
	ButtonActionsCommands commands;
	u8 parentMode;
	BitArray8<eHUDElement_Num> hud;
	bool mouseLookOn;

	ControlsMode() :
		parentMode(), hud(), mouseLookOn()
	{}
};

struct InputMapBuilder
{
	size_t currentSet;
	std::string debugSlotName;
	std::vector<std::string> parsedString;
	StringToValueMap<u8> nameToIdxMap;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<MacroSet> sMacroSets;
static std::vector<std::string> sKeyStrings;
static StringToValueMap<int> sKeyBinds;
static std::vector<ControlsMode> sModes;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static u8 keyNameToVirtualKey(const std::string& theKeyName, bool allowMouse)
{
	if( theKeyName.size() == 1 )
	{
		SHORT aVKey = VkKeyScan(tolower(theKeyName[0]));
		DBG_ASSERT(aVKey <= 0xFF);
		return u8(aVKey);
	}

	struct NameToVKeyMapper
	{
		typedef StringToValueMap<u8, u8> NameToVKeyMap;
		NameToVKeyMap map;
		NameToVKeyMapper()
		{
			const size_t kMapSize = 126;
			map.reserve(kMapSize);
			map.setValue("LCLICK",			VK_LBUTTON);
			map.setValue("LEFTCLICK",		VK_LBUTTON);
			map.setValue("LMB",				VK_LBUTTON);
			map.setValue("RCLICK",			VK_RBUTTON);
			map.setValue("RIGHTCLICK",		VK_RBUTTON);
			map.setValue("RMB",				VK_RBUTTON);
			map.setValue("MCLICK",			VK_MBUTTON);
			map.setValue("MIDDLECLICK",		VK_MBUTTON);
			map.setValue("MMB",				VK_MBUTTON);
			map.setValue("BACK",			VK_BACK);
			map.setValue("BACKSPACE",		VK_BACK);
			map.setValue("BS",				VK_BACK);
			map.setValue("TAB",				VK_TAB);
			map.setValue("RETURN",			VK_RETURN);
			map.setValue("ENTER",			VK_RETURN);
			map.setValue("SH",				VK_SHIFT);
			map.setValue("SHFT",			VK_SHIFT);
			map.setValue("SHIFT",			VK_SHIFT);
			map.setValue("CTRL",			VK_CONTROL);
			map.setValue("CONTROL",			VK_CONTROL);
			map.setValue("ALT",				VK_MENU);
			map.setValue("MENU",			VK_MENU);
			map.setValue("ESC",				VK_ESCAPE);
			map.setValue("ESCAPE",			VK_ESCAPE);
			map.setValue("SPACE",			VK_SPACE);
			map.setValue("SPACEBAR",		VK_SPACE);
			map.setValue("PGUP",			VK_PRIOR);
			map.setValue("PAGEUP",			VK_PRIOR);
			map.setValue("PAGEDOWN",		VK_NEXT);
			map.setValue("PGDOWN",			VK_NEXT);
			map.setValue("PGDWN",			VK_NEXT);
			map.setValue("END",				VK_END);
			map.setValue("HOME",			VK_HOME);
			map.setValue("LEFT",			VK_LEFT);
			map.setValue("LEFTARROW",		VK_LEFT);
			map.setValue("UP",				VK_UP);
			map.setValue("UPARROW",			VK_UP);
			map.setValue("RIGHT",			VK_RIGHT);
			map.setValue("RIGHTARROW",		VK_RIGHT);
			map.setValue("DOWN",			VK_DOWN);
			map.setValue("DOWNARROW",		VK_DOWN);
			map.setValue("INS",				VK_INSERT);
			map.setValue("INSERT",			VK_INSERT);
			map.setValue("DEL",				VK_DELETE);
			map.setValue("DELETE",			VK_DELETE);
			map.setValue("NUMMULT",			VK_MULTIPLY);
			map.setValue("NUMMULTIPLY",		VK_MULTIPLY);
			map.setValue("NUMADD",			VK_ADD);
			map.setValue("NUMPLUS",			VK_ADD);
			map.setValue("NUMSUB",			VK_SUBTRACT);
			map.setValue("NUMSUBTRACT",		VK_SUBTRACT);
			map.setValue("NUMMINUS",		VK_SUBTRACT);
			map.setValue("NUMPERIOD",		VK_DECIMAL);
			map.setValue("NUMDECIMAL",		VK_DECIMAL);
			map.setValue("NUMDIV",			VK_DIVIDE);
			map.setValue("NUMDIVIDE",		VK_DIVIDE);
			map.setValue("NUMSLASH",		VK_DIVIDE);
			map.setValue("NUMPADMULT",		VK_MULTIPLY);
			map.setValue("NUMPADMULTIPLY",	VK_MULTIPLY);
			map.setValue("NUMPADADD",		VK_ADD);
			map.setValue("NUMPADPLUS",		VK_ADD);
			map.setValue("NUMPADSUB",		VK_SUBTRACT);
			map.setValue("NUMPADSUBTRACT",	VK_SUBTRACT);
			map.setValue("NUMPADMINUS",		VK_SUBTRACT);
			map.setValue("NUMPADPERIOD",	VK_DECIMAL);
			map.setValue("NUMPADDECIMAL",	VK_DECIMAL);
			map.setValue("NUMPADDIV",		VK_DIVIDE);
			map.setValue("NUMPADDIVIDE",	VK_DIVIDE);
			map.setValue("NUMPADSLASH",		VK_DIVIDE);
			map.setValue("NUMLOCK",			VK_NUMLOCK);
			map.setValue("COLON",			VK_OEM_1);
			map.setValue("SEMICOLON",		VK_OEM_1);
			map.setValue("PLUS",			VK_OEM_PLUS);
			map.setValue("EQUAL",			VK_OEM_PLUS);
			map.setValue("EQUALS",			VK_OEM_PLUS);
			map.setValue("COMMA",			VK_OEM_COMMA);
			map.setValue("MINUS",			VK_OEM_MINUS);
			map.setValue("DASH",			VK_OEM_MINUS);
			map.setValue("HYPHEN",			VK_OEM_MINUS);
			map.setValue("UNDERSCORE",		VK_OEM_MINUS);
			map.setValue("PERIOD",			VK_OEM_PERIOD);
			map.setValue("SLASH",			VK_OEM_2);
			map.setValue("FORWARDSLASH",	VK_OEM_2);
			map.setValue("DIVIDE",			VK_OEM_2);
			map.setValue("DIV",				VK_OEM_2);
			map.setValue("QUESTION",		VK_OEM_2);
			map.setValue("TILDE",			VK_OEM_3);
			map.setValue("BACKTICK",		VK_OEM_3);
			map.setValue("OPENB",			VK_OEM_4);
			map.setValue("OPENBRACKET",		VK_OEM_4);
			map.setValue("BACKSLASH",		VK_OEM_5);
			map.setValue("CLOSEB",			VK_OEM_6);
			map.setValue("CLOSEBRACKET",	VK_OEM_6);
			map.setValue("QUOTE",			VK_OEM_7);
			for(int i = 0; i <= 9; ++i)
			{
				map.setValue((std::string("NUM") + toString(i)).c_str(), VK_NUMPAD0 + i);
				map.setValue(std::string("NUMPAD") + toString(i), VK_NUMPAD0 + i);
			}
			for(int i = 1; i <= 12; ++i)
				map.setValue((std::string("F") + toString(i)).c_str(), VK_F1 - 1 + i);
			DBG_ASSERT(map.size() == kMapSize);
		}
	};
	static NameToVKeyMapper sKeyMapper;

	u8* result = sKeyMapper.map.find(upper(theKeyName));
	if( !allowMouse && result && *result <= 0x07) result = NULL;
	return result ? *result : 0;
}


static ButtonActionID buttonActionNameToID(std::string theString)
{
	DBG_ASSERT(!theString.empty());

	ButtonActionID result;
	
	// Assume press-and-hold action if none specified by a prefix
	result.act = eBtnAct_PressAndHold;

	// Check for action prefix - not many so just linear search
	for(size_t i = 0; i < eBtnAct_Num; ++i)
	{
		if( kButtonActionPrefx[i][0] == '\0' )
			continue;
		const char* aPrefixChar = &kButtonActionPrefx[i][0];
		const char* aStrChar = theString.c_str();
		bool matchFound = true;
		for(; *aPrefixChar; ++aPrefixChar, ++aStrChar)
		{
			// Ignore whitespace in the string
			while(*aStrChar <= ' ') ++aStrChar;
			// Mismatch if reach end of string or chars don't match
			if( !*aStrChar || ::toupper(*aPrefixChar) != ::toupper(*aStrChar) )
			{
				matchFound = false;
				break;
			}
		}
		if( matchFound )
		{
			result.act = EButtonAction(i);
			// Chop the prefix (and any whitespace after) off the front of the string
			theString = trim(aStrChar);
			break;
		}
	}

	struct NameToEnumMapper
	{
		typedef StringToValueMap<EButton, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			const size_t kMapSize = eBtn_Num + 38;
			map.reserve(kMapSize);
			for(int i = 0; i < eBtn_Num; ++i)
				map.setValue(upper(kProfileButtonName[i]), EButton(i));
			// Add some extra aliases
			map.setValue("LSTICKLEFT",		eBtn_LSLeft);
			map.setValue("LSTICKRIGHT",		eBtn_LSRight);
			map.setValue("LSTICKUP",		eBtn_LSUp);
			map.setValue("LSTICKDOWN",		eBtn_LSDown);
			map.setValue("LEFTSTICKLEFT",	eBtn_LSLeft);
			map.setValue("LEFTSTICKRIGHT",	eBtn_LSRight);
			map.setValue("LEFTSTICKUP",		eBtn_LSUp);
			map.setValue("LEFTSTICKDOWN",	eBtn_LSDown);
			map.setValue("RSTICKLEFT",		eBtn_RSLeft);
			map.setValue("RSTICKRIGHT",		eBtn_RSRight);
			map.setValue("RSTICKUP",		eBtn_RSUp);
			map.setValue("RSTICKDOWN",		eBtn_RSDown);
			map.setValue("RIGHTSTICKLEFT",	eBtn_RSLeft);
			map.setValue("RIGHTSTICKRIGHT",	eBtn_RSRight);
			map.setValue("RIGHTSTICKUP",	eBtn_RSUp);
			map.setValue("RIGHTSTICKDOWN",	eBtn_RSDown);
			map.setValue("DPLEFT",			eBtn_DLeft);
			map.setValue("DPADLEFT",		eBtn_DLeft);
			map.setValue("DPRIGHT",			eBtn_DRight);
			map.setValue("DPADRIGHT",		eBtn_DRight);
			map.setValue("DPUP",			eBtn_DUp);
			map.setValue("DPADUP",			eBtn_DUp);
			map.setValue("DPDOWN",			eBtn_DDown);
			map.setValue("DPADDOWN",		eBtn_DDown);
			map.setValue("FPADLEFT",		eBtn_FLeft);
			map.setValue("SQUARE",			eBtn_FLeft);
			map.setValue("XBX",				eBtn_FLeft);
			map.setValue("FPADRIGHT",		eBtn_FRight);
			map.setValue("CIRCLE",			eBtn_FRight);
			map.setValue("CIRC",			eBtn_FRight);
			map.setValue("XBB",				eBtn_FRight);
			map.setValue("FPADUP",			eBtn_FUp);
			map.setValue("TRIANGLE",		eBtn_FUp);
			map.setValue("TRI",				eBtn_FUp);
			map.setValue("XBY",				eBtn_FUp);
			map.setValue("FPADDOWN",		eBtn_FDown);
			map.setValue("XBA",				eBtn_FDown);
			map.setValue("PSX",				eBtn_FDown);
			DBG_ASSERT(map.size() == kMapSize);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	if( EButton* aBtnID = sNameToEnumMapper.map.find(upper(theString)) )
		result.btn = *aBtnID;
	
	return result;
}


EResult checkForComboKeyName(
	std::string aKeyName,
	std::string* out,
	bool allowMouse)
{
	std::string aModKeyName;
	aModKeyName.push_back(aKeyName[0]);
	aKeyName = aKeyName.substr(1);
	while(aKeyName.size() > 1)
	{
		aModKeyName.push_back(aKeyName[0]);
		aKeyName = aKeyName.substr(1);
		const u8 aModKey = keyNameToVirtualKey(aModKeyName, allowMouse);
		if( aModKey != 0 &&
			(aModKey == VK_SHIFT ||
			 aModKey == VK_CONTROL ||
			 aModKey == VK_MENU) )
		{// Found a valid modifier key
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(aKeyName, allowMouse))
			{// We have a valid key combo!
				out->push_back(aModKey);
				out->push_back(aMainKey);
				return eResult_Ok;
			}
			// Perhaps remainder is another mod+key, like ShiftCtrlA?
			std::string suffix;
			if( checkForComboKeyName(aKeyName, &suffix, allowMouse) )
			{
				out->push_back(aModKey);
				out->append(suffix);
				return eResult_Ok;
			}
		}
	}

	// No valid modifier key found
	return eResult_NotFound;
}


static std::string namesToVKeySequence(
	const std::vector<std::string>& theNames,
	bool allowMouse)
{
	std::string result;

	if( theNames.empty() )
		return result;

	for(int aNameIdx = 0; aNameIdx < theNames.size(); ++aNameIdx)
	{
		DBG_ASSERT(!theNames[aNameIdx].empty());
		const u8 aVKey = keyNameToVirtualKey(theNames[aNameIdx], allowMouse);
		if( aVKey == 0 )
		{
			// Check if it's a modifier+key in one word like Shift2 or Alt1
			if( checkForComboKeyName(
					theNames[aNameIdx], &result, allowMouse) != eResult_Ok )
			{
				result.clear();
				return result;
			}
		}
		else
		{
			result.push_back(aVKey);
		}
	}

	return result;
}


static MacroSlot stringToMacroSlot(
	InputMapBuilder* theBuilder,
	std::string theString)
{
	DBG_ASSERT(theBuilder);

	MacroSlot aMacroSlot;
	if( theString.empty() )
	{
		mapDebugPrint("Macro left <unnamed> and <unassigned>!\n");
		return aMacroSlot;
	}

	// Get the label (part of the string before first colon)
	aMacroSlot.label = breakOffItemBeforeChar(theString, ':');

	if( aMacroSlot.label.empty() && !theString.empty() )
	{// Having no : character means this points to a sub-set
		aMacroSlot.label = trim(theString);
		aMacroSlot.cmd.type = eCmdType_ChangeMacroSet;
		aMacroSlot.cmd.data = int(sMacroSets.size());
		sMacroSets.push_back(MacroSet());
		sMacroSets.back().label =
			sMacroSets[theBuilder->currentSet].label +
			"." + aMacroSlot.label;
		mapDebugPrint("New Macro Set: '%s'\n",
			aMacroSlot.label.c_str());
		return aMacroSlot;
	}

	if( theString.empty() )
	{
		mapDebugPrint("Macro '%s' left <unassigned>!\n",
			aMacroSlot.label.c_str());
		return aMacroSlot;
	}

	// Check for a slash command or say string, which outputs the whole string
	if( theString[0] == '/' )
	{
		sKeyStrings.push_back(theString);
		aMacroSlot.cmd.type = eCmdType_SlashCommand;
		aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
		mapDebugPrint("Macro '%s' assigned to command: %s\n",
			aMacroSlot.label.c_str(), theString.c_str());
		return aMacroSlot;
	}
	if( theString[0] == '>' )
	{
		sKeyStrings.push_back(theString);
		aMacroSlot.cmd.type = eCmdType_SayString;
		aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
		mapDebugPrint("Macro '%s' assigned to string: %s\n",
			aMacroSlot.label.c_str(), &theString[1]);
		return aMacroSlot;
	}

	// Break the remaining string (after label) into individual words
	theBuilder->parsedString.clear();
	sanitizeSentence(theString, &theBuilder->parsedString);

	// Process the words as a sequence of Virtual-Key Code names
	const std::string& aVKeySeq = namesToVKeySequence(
		theBuilder->parsedString, false);
	if( !aVKeySeq.empty() )
	{
		sKeyStrings.push_back(aVKeySeq);
		aMacroSlot.cmd.type = eCmdType_VKeySequence;
		aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
		mapDebugPrint("Macro '%s' assigned to key sequence: %s\n",
			aMacroSlot.label.c_str(), theString.c_str());
		return aMacroSlot;
	}

	// Probably just forgot the > at front of a plain string
	sKeyStrings.push_back(std::string(">") + theString);
	aMacroSlot.cmd.type = eCmdType_SayString;
	aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
	mapDebugPrint("Macro '%s' assigned to string (forgot '>'?): %s\n",
		aMacroSlot.label.c_str(), theString.c_str());
	return aMacroSlot;
}


static void buildMacroSets(InputMapBuilder* theBuilder)
{
	DBG_ASSERT(theBuilder);

	mapDebugPrint("Building Macro Sets...\n");

	sMacroSets.push_back(MacroSet());
	sMacroSets.back().label = kMainMacroSetLabel;
	for(int aSet = 0; aSet < sMacroSets.size(); ++aSet )
	{
		theBuilder->currentSet = aSet;
		const std::string aPrefix = sMacroSets[aSet].label;
		for(int aSlot = 0; aSlot < kMacroSlotsPerSet; ++aSlot)
		{
			theBuilder->debugSlotName =
				aPrefix + " (" + kMacroSlotLabel[aSlot] + ")";
			mapDebugPrint("Parsing macro for slot '%s'\n",
				theBuilder->debugSlotName.c_str());

			sMacroSets[aSet].slot[aSlot] =
				stringToMacroSlot(
					theBuilder,
					Profile::getStr(
						aPrefix + "/" + kMacroSlotLabel[aSlot]));
		}
	}
}


static void buildKeyBinds(InputMapBuilder* theBuilder)
{
	mapDebugPrint("Assigning KeyBinds...\n");

	Profile::KeyValuePairs aKeyBindRequests;
	Profile::getAllKeys(kKeybindsPrefix, &aKeyBindRequests);
	for(size_t i = 0; i < aKeyBindRequests.size(); ++i)
	{
		const char* anActionName = aKeyBindRequests[i].first;
		const char* aKeysDescription = aKeyBindRequests[i].second;

		// Break keys description string into individual words
		theBuilder->parsedString.clear();
		sanitizeSentence(aKeysDescription, &theBuilder->parsedString);
		const std::string& aVKeySeq = namesToVKeySequence(
			theBuilder->parsedString, true);
		if( !aVKeySeq.empty() )
		{
			sKeyStrings.push_back(aVKeySeq);
			sKeyBinds.setValue(upper(anActionName), (int)sKeyStrings.size()-1);

			mapDebugPrint("Assigned to alias '%s': '%s'\n",
				anActionName, aKeysDescription);
		}
		else
		{
			logError(
				"Keybind '%s': Unable to decipher and assign '%s'",
				anActionName, aKeysDescription);
		}
	}
}


static void addButtonAction(
	InputMapBuilder* theBuilder,
	ButtonActionsCommands* theCommands,
	const std::string& theBtnName,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theBuilder);
	DBG_ASSERT(theCommands);
	if( theBtnName.empty() || theCmdStr.empty() )
		return;

	// Handle shortcuts for assigning multiple at once
	for(size_t i = 0; i < ARRAYSIZE(k4DirButtons); ++i)
	{
		if( theBtnName == k4DirButtons[i] )
		{
			addButtonAction(theBuilder, theCommands,
				theBtnName + "UP", theCmdStr + " Up");
			addButtonAction(theBuilder, theCommands,
				theBtnName + "DOWN", theCmdStr + " Down");
			addButtonAction(theBuilder, theCommands,
				theBtnName + "LEFT", theCmdStr + " Left");
			addButtonAction(theBuilder, theCommands,
				theBtnName + "RIGHT", theCmdStr + " Right");
			return;
		}
	}

	// Determine button & action to assign command to
	ButtonActionID aButtonActionID = buttonActionNameToID(theBtnName);
	if( !aButtonActionID )
	{
		logError("Unable to identify Gamepad Button '%s' requested in [%s.%s]",
			theBtnName.c_str(),
			kModePrefix.c_str(),
			theBuilder->debugSlotName.c_str());
		return;
	}

	// Break command string into individual words
	theBuilder->parsedString.clear();
	sanitizeSentence(theCmdStr, &theBuilder->parsedString);

	// Check for special commands
	// TODO

	// Check for keybind alias
	std::string* aVKeySeqPtr = NULL;
	if( int* aKeyStringIdxPtr = sKeyBinds.find(upper(theCmdStr)) )
		aVKeySeqPtr = &sKeyStrings[*aKeyStringIdxPtr];

	// Check for direct key sequence
	std::string aVKeySeq;
	if( !aVKeySeqPtr )
	{
		aVKeySeq = namesToVKeySequence(
				theBuilder->parsedString, true);
		aVKeySeqPtr = &aVKeySeq;
	}

	Command aCmd;
	if( aVKeySeqPtr && aVKeySeqPtr->size() == 1 &&
		aButtonActionID.act == eBtnAct_PressAndHold )
	{
		// True press-and-hold only works with a single key.
		// If more than one key was specified, this will be
		// skipped and _PressAndHold will just act like the
		// normal _Press action (possibly having 2), and no
		// _HoldRelease command will be added with it.
		aCmd.data = (*aVKeySeqPtr)[0];

		// Add the extra release command now
		aCmd.type = eCmdType_ReleaseKey;
		aButtonActionID.act = eBtnAct_HoldRelease;
		theCommands->addPair(aButtonActionID, aCmd);

		aButtonActionID.act = eBtnAct_PressAndHold;
		aCmd.type = eCmdType_PressAndHoldKey;
	}

	if( aCmd.type == eCmdType_Empty &&
		aVKeySeqPtr && !aVKeySeqPtr->empty() )
	{
		if( aVKeySeqPtr == &aVKeySeq )
		{
			sKeyStrings.push_back(aVKeySeq);
			aVKeySeqPtr = &sKeyStrings[sKeyStrings.size()-1];
		}
		DBG_ASSERT(aVKeySeqPtr >= &sKeyStrings[0]);
		DBG_ASSERT(aVKeySeqPtr < &sKeyStrings[0] + sKeyStrings.size());
		aCmd.data = int(aVKeySeqPtr - &sKeyStrings[0]);
		aCmd.type = eCmdType_VKeySequence;
	}

	if( aCmd.type != eCmdType_Empty )
	{
		theCommands->addPair(aButtonActionID, aCmd);
		mapDebugPrint("[Mode.%s]: Assigned '%s%s%s' to '%s'\n",
			theBuilder->debugSlotName.c_str(),
			kButtonActionPrefx[aButtonActionID.act],
			kButtonActionPrefx[aButtonActionID.act][0] ? " " : "",
			kProfileButtonName[aButtonActionID.btn],
			theCmdStr.c_str());
	}
	else
	{
		logError("Not sure how to assign [%s.%s] %s = %s",
			kModePrefix.c_str(),
			theBuilder->debugSlotName.c_str(),
			theBtnName.c_str(),
			theCmdStr.c_str());
	}
}


static u8 getOrCreateMode(
	InputMapBuilder* theBuilder,
	const std::string& theModeName)
{
	DBG_ASSERT(theBuilder);

	u8 idx = theBuilder->nameToIdxMap.findOrAdd(theModeName, (u8)sModes.size());
	if( idx == sModes.size() )
	{// Need to create the new mode
		sModes.push_back(ControlsMode());
		const std::string aModePrefix(kModePrefix + "." + theModeName + "/");
		{// Get (or make) parent mode first, if has one assigned
			std::string aParentName;
			for(int i = 0; i < ARRAYSIZE(kParentModeKeys); ++i)
			{
				aParentName = Profile::getStr(
					aModePrefix + kParentModeKeys[i],
					aParentName);
			}
			if( !aParentName.empty() )
			{
				const u8 aParentIdx = getOrCreateMode(theBuilder, aParentName);
				// Inherit default settings from parent
				sModes[idx].mouseLookOn = sModes[aParentIdx].mouseLookOn;
				sModes[idx].hud = sModes[aParentIdx].hud;
				sModes[idx].parentMode = aParentIdx;
			}
		}
		theBuilder->debugSlotName = theModeName;
		mapDebugPrint("Building controls mode: %s\n", theModeName.c_str());

		// Get mouse look mode setting directly
		sModes[idx].mouseLookOn = Profile::getBool(
			aModePrefix + kMouseLookKey,
			sModes[idx].mouseLookOn);

		// Check each key-value pair for button assignment reqests
		Profile::KeyValuePairs aSettings;
		Profile::getAllKeys(aModePrefix, &aSettings);
		for(Profile::KeyValuePairs::const_iterator itr = aSettings.begin();
			itr != aSettings.end(); ++itr)
		{
			const std::string aKey = itr->first;
			bool skipSetting = false;
			for(size_t j = 0; j < ARRAYSIZE(kParentModeKeys); ++j)
			{
				if( aKey == kParentModeKeys[j] )
				{
					skipSetting = true;
					break;
				}
			}
			if( aKey == kMouseLookKey )
				skipSetting = true;
			if( skipSetting )
				continue;

			if( aKey == kHUDSettingsKey )
			{
				// TODO
				continue;
			}

			// Parse and add assignment to this mode's commands map
			addButtonAction(
				theBuilder,
				&sModes[idx].commands,
				aKey,
				itr->second);
		}

		sModes[idx].commands.sort();
		sModes[idx].commands.removeDuplicates();
		sModes[idx].commands.trim();
	}

	return idx;
}


static void buildControlScheme(InputMapBuilder* theBuilder)
{
	// Set up keybinds used by control schemes
	buildKeyBinds(theBuilder);

	mapDebugPrint("Building control scheme (modes)...\n");

	// First mode is empty so parentMode == 0 means no parent.
	// It will itself use the default mode as its parent so at
	// startup, mode of '0' will refer to this one but end up
	// returning the button assignments from the default mode.
	sModes.push_back(ControlsMode());

	// Get name of default mode (parent to mode 0)
	std::string aParentName;
	for(int i = 0; i < ARRAYSIZE(kParentModeKeys); ++i)
	{
		aParentName = Profile::getStr(
			kModePrefix + "/" + kParentModeKeys[i],
			aParentName);
	}
	if( aParentName.empty() )
		return;

	sModes[0].parentMode = getOrCreateMode(theBuilder, aParentName);
	DBG_ASSERT(sModes[0].parentMode != 0);
	DBG_ASSERT(sModes[0].parentMode < sModes.size());
	sModes[0].hud = sModes[sModes[0].parentMode].hud;
	sModes[0].mouseLookOn = sModes[sModes[0].parentMode].mouseLookOn;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	sKeyStrings.clear();
	sKeyBinds.clear();
	sModes.clear();
	sMacroSets.clear();

	InputMapBuilder anInputMapBuilder;
	buildControlScheme(&anInputMapBuilder);
	buildMacroSets(&anInputMapBuilder);

	// Trim unused memory
	if( sMacroSets.size() < sMacroSets.capacity() )
		std::vector<MacroSet>(sMacroSets).swap(sMacroSets);
	if( sKeyStrings.size() < sKeyStrings.capacity() )
		std::vector<std::string>(sKeyStrings).swap(sKeyStrings);
	if( sModes.size() < sModes.capacity() )
		std::vector<ControlsMode>(sModes).swap(sModes);
}


bool mouseLookShouldBeOn(int theModeID)
{
	DBG_ASSERT((unsigned)theModeID < sModes.size());
	return sModes[theModeID].mouseLookOn;
}


Command commandForButtonAction(
	int theModeID,
	EButton theButton,
	EButtonAction theAction)
{
	DBG_ASSERT((unsigned)theModeID < sModes.size());
	DBG_ASSERT(theAction < eBtnAct_Num);

	Command result;
	ButtonActionsCommands::const_iterator itr;
	do
	{
		const ButtonActionID aBtnAct = ButtonActionID(
			EButton(theButton), EButtonAction(theAction));
		itr = sModes[theModeID].commands.find(aBtnAct);
		if( itr != sModes[theModeID].commands.end() )
		{
			result = itr->second;
			switch(result.type)
			{
			case eCmdType_VKeySequence:
			case eCmdType_SlashCommand:
			case eCmdType_SayString:
				DBG_ASSERT((unsigned)result.data < sKeyStrings.size());
				// Important that this raw string pointer gets used before
				// anything happens to our sMacroSets data or we'd get a
				// dangling pointer - but this prevents string copies!
				result.string = sKeyStrings[result.data].c_str();
				break;
			}
			return result;
		}
		else
		{
			theModeID = sModes[theModeID].parentMode;
		}
	} while(theModeID != 0);

	return result;
}


Command commandForMacro(int theMacroSetID, u8 theMacroSlotID)
{
	DBG_ASSERT((unsigned)theMacroSetID < sMacroSets.size());
	DBG_ASSERT((unsigned)theMacroSlotID < kMacroSlotsPerSet);
	
	Command result = sMacroSets[theMacroSetID].slot[theMacroSlotID].cmd;
	switch(result.type)
	{
	case eCmdType_VKeySequence:
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		DBG_ASSERT((unsigned)result.data < sKeyStrings.size());
		// Important that this raw string pointer gets used before
		// anything happens to our sMacroSets data or we'd get a
		// dangling pointer - but this prevents string copies!
		result.string = sKeyStrings[result.data].c_str();
		break;
	}

	return result;
}


BitArray8<eHUDElement_Num> visibleHUDElements(int theModeID)
{
	DBG_ASSERT((unsigned)theModeID < sModes.size());
	return sModes[theModeID].hud;
}


const std::string& macroLabel(int theMacroSetID, u8 theMacroSlotID)
{
	DBG_ASSERT((unsigned)theMacroSetID < sMacroSets.size());
	DBG_ASSERT((unsigned)theMacroSlotID < kMacroSlotsPerSet);

	return sMacroSets[theMacroSetID].slot[theMacroSlotID].label;
}

} // InputMap
