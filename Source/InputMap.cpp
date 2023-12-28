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

enum {
kMacroSlotsPerSet = 4,
};

const char* kMainMacroSetLabel = "Macros";
const char* kMacroSlotLabel[] = { "U", "L", "R", "D" };
DBG_CTASSERT(ARRAYSIZE(kMacroSlotLabel) >= kMacroSlotsPerSet);


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

struct MacroSetBuilder
{
	size_t currMacroSet;
	std::vector<std::string> parsedCommand;
	std::string debugMacroSlotName;
};

struct ButtonActions
{
	Command cmd[eButtonAction_Num];
};

typedef VectorMap<Gamepad::EButton, ButtonActions> ButtonActionsMap;

struct ControlsMode
{
	ButtonActionsMap actions;
	u8 parentMode;
	BitArray8<eHUDElement_Num> hud;
	bool mouseLookOn;

	ControlsMode() :
		parentMode(), hud(), mouseLookOn()
	{}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<MacroSet> sMacroSets;
static std::vector<std::string> sKeyStrings;
static std::vector<ControlsMode> sModes;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static u8 keyNameToVirtualKey(const std::string& theKeyName)
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
			map.reserve(116);
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
			DBG_ASSERT(map.size() == 116 && "update reserve above to match if assert");
		}
	};
	static NameToVKeyMapper sKeyMapper;

	u8* result = sKeyMapper.map.find(upper(theKeyName));
	return result ? *result : 0;
}


EResult checkForComboKeyName(std::string aKeyName, std::string* out)
{
	std::string aModKeyName;
	aModKeyName.push_back(aKeyName[0]);
	aKeyName = aKeyName.substr(1);
	while(aKeyName.size() > 1)
	{
		aModKeyName.push_back(aKeyName[0]);
		aKeyName = aKeyName.substr(1);
		const u8 aModKey = keyNameToVirtualKey(aModKeyName);
		if( aModKey != 0 &&
			(aModKey == VK_SHIFT ||
			 aModKey == VK_CONTROL ||
			 aModKey == VK_MENU) )
		{// Found a valid modifier key
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(aKeyName))
			{// We have a valid key combo!
				out->push_back(aModKey);
				out->push_back(aMainKey);
				return eResult_Ok;
			}
			// Perhaps remainder is another mod+key, like ShiftCtrlA?
			std::string suffix;
			if( checkForComboKeyName(aKeyName, &suffix) )
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


static MacroSlot stringToMacroSlot(
	MacroSetBuilder* theSetBuilder,
	std::string theString)
{
	DBG_ASSERT(theSetBuilder);

	MacroSlot aMacroSlot;
	if( theString.empty() )
		return aMacroSlot;

	// Get the label (part of the string before first colon)
	aMacroSlot.label = breakOffItemBeforeChar(theString, ':');

	if( aMacroSlot.label.empty() && !theString.empty() )
	{// Having no : character means this points to a sub-set
		aMacroSlot.label = trim(theString);
		aMacroSlot.cmd.type = eCmdType_ChangeMacroSet;
		aMacroSlot.cmd.data = int(sMacroSets.size());
		sMacroSets.push_back(MacroSet());
		sMacroSets.back().label =
			sMacroSets[theSetBuilder->currMacroSet].label +
			"." + aMacroSlot.label;
		return aMacroSlot;
	}

	if( theString.empty() )
		return aMacroSlot;

	// Check for a slash command or say string, which outputs the whole string
	if( theString[0] == '/' )
	{
		sKeyStrings.push_back(theString);
		aMacroSlot.cmd.type = eCmdType_SlashCommand;
		aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
		return aMacroSlot;
	}
	if( theString[0] == '>' )
	{
		sKeyStrings.push_back(theString);
		aMacroSlot.cmd.type = eCmdType_SayString;
		aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
		return aMacroSlot;
	}

	// Break the remaining string (after label) into individual words
	theSetBuilder->parsedCommand.clear();
	sanitizeSentence(theString, &theSetBuilder->parsedCommand);
	if( theSetBuilder->parsedCommand.empty() )
		return aMacroSlot;

	// Should be a key press sequence
	std::string aKeySeq;
	for(int aWord = 0; aWord < theSetBuilder->parsedCommand.size(); ++aWord)
	{
		DBG_ASSERT(!theSetBuilder->parsedCommand[aWord].empty());
		const std::string& aKeyName = theSetBuilder->parsedCommand[aWord];
		const u8 aVKey = keyNameToVirtualKey(aKeyName);
		if( aVKey == 0 )
		{
			// Check if it's a modifier+key in one word like Shift2 or Alt1
			if( checkForComboKeyName(aKeyName, &aKeySeq) != eResult_Ok )
			{
				// Probably just forgot the > at front of a plain string
				aKeySeq.clear();
				aKeySeq.push_back('>');
				aKeySeq += theString;
				sKeyStrings.push_back(aKeySeq);
				aMacroSlot.cmd.type = eCmdType_SayString;
				aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
				return aMacroSlot;
			}
		}
		else
		{
			aKeySeq.push_back(aVKey);
		}
	}

	if( !aKeySeq.empty() )
	{
		sKeyStrings.push_back(aKeySeq);
		aMacroSlot.cmd.type = eCmdType_VKeySequence;
		aMacroSlot.cmd.data = int(sKeyStrings.size()-1);
	}

	return aMacroSlot;
}


static void buildMacroSets()
{
	MacroSetBuilder aMacroSetBuilder;

	sMacroSets.push_back(MacroSet());
	sMacroSets.back().label = kMainMacroSetLabel;
	for(int aSet = 0; aSet < sMacroSets.size(); ++aSet )
	{
		aMacroSetBuilder.currMacroSet = aSet;
		const std::string aPrefix = sMacroSets[aSet].label;
		for(int aSlot = 0; aSlot < kMacroSlotsPerSet; ++aSlot)
		{
			aMacroSetBuilder.debugMacroSlotName = kMacroSlotLabel[aSlot];
			sMacroSets[aSet].slot[aSlot] =
				stringToMacroSlot(
					&aMacroSetBuilder,
					Profile::getStr(
						aPrefix + "/" + kMacroSlotLabel[aSlot]));
		}
	}
}


static void buildControlSchemes()
{
	using namespace Gamepad;

	// TEMP - hard coded for now
	// First mode is empty so parentMode == 0 means no parent
	// It will itself use the default mode as its parent so at
	// startup, mode of '0' will refer to this one but end up
	// returning the data from the default mode.
	sModes.push_back(ControlsMode());
	sModes.back().parentMode = 1;

	sModes.push_back(ControlsMode());
	ControlsMode& aCursorMode = sModes.back();
	aCursorMode.parentMode = 0;

	ButtonActions aCmdArray;
	Command aCmd;

	aCmd.type = eCmdType_MoveMouse;
	aCmd.data = eCmdSubType_Left;
	aCmdArray.cmd[eButtonAction_Analog] = aCmd;
	aCursorMode.actions.addPair(eBtn_RSLeft, aCmdArray);
	aCmdArray.cmd[eButtonAction_Analog].data = eCmdSubType_Right;
	aCursorMode.actions.addPair(eBtn_RSRight, aCmdArray);
	aCmdArray.cmd[eButtonAction_Analog].data = eCmdSubType_Up;
	aCursorMode.actions.addPair(eBtn_RSUp, aCmdArray);
	aCmdArray.cmd[eButtonAction_Analog].data = eCmdSubType_Down;
	aCursorMode.actions.addPair(eBtn_RSDown, aCmdArray);

	aCmdArray = ButtonActions();
	aCmd.type = eCmdType_ChangeMode;
	aCmd.data = 2;
	aCmdArray.cmd[eButtonAction_Press] = aCmd;
	aCursorMode.actions.addPair(eBtn_L2, aCmdArray);
	aCursorMode.actions.sort();
	aCursorMode.actions.trim();

	sModes.push_back(ControlsMode());
	ControlsMode& aMacroMode = sModes.back();
	aMacroMode.parentMode = 1;
	aMacroMode.hud.set(eHUDElement_Macros);

	aCmdArray = ButtonActions();
	aCmd.type = eCmdType_ChangeMode;
	aCmd.data = 1;
	aCmdArray.cmd[eButtonAction_Release] = aCmd;
	aMacroMode.actions.addPair(eBtn_L2, aCmdArray);

	aCmdArray = ButtonActions();
	aCmdArray.cmd[eButtonAction_Press].type = eCmdType_ChangeMacroSet;
	aCmdArray.cmd[eButtonAction_Press].data = 0;
	aMacroMode.actions.addPair(eBtn_None, aCmdArray);

	aCmdArray = ButtonActions();
	aCmdArray.cmd[eButtonAction_Tap].type = eCmdType_SelectMacro;
	aCmdArray.cmd[eButtonAction_Tap].data = eCmdSubType_Left;
	aMacroMode.actions.addPair(eBtn_DLeft, aCmdArray);
	aMacroMode.actions.addPair(eBtn_FLeft, aCmdArray);

	aCmdArray.cmd[eButtonAction_Tap].data = eCmdSubType_Right;
	aMacroMode.actions.addPair(eBtn_DRight, aCmdArray);
	aMacroMode.actions.addPair(eBtn_FRight, aCmdArray);

	aCmdArray.cmd[eButtonAction_Tap].data = eCmdSubType_Up;
	aMacroMode.actions.addPair(eBtn_DUp, aCmdArray);
	aMacroMode.actions.addPair(eBtn_FUp, aCmdArray);

	aCmdArray.cmd[eButtonAction_Tap].data = eCmdSubType_Down;
	aMacroMode.actions.addPair(eBtn_DDown, aCmdArray);
	aMacroMode.actions.addPair(eBtn_FDown, aCmdArray);

	aMacroMode.actions.sort();
	aMacroMode.actions.trim();
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	sKeyStrings.clear();
	sModes.clear();
	buildControlSchemes();
	sMacroSets.clear();
	buildMacroSets();
}


bool mouseLookShouldBeOn(int theModeID)
{
	DBG_ASSERT((unsigned)theModeID < sModes.size());
	return sModes[theModeID].mouseLookOn;
}


Command commandForButton(
	int theModeID,
	Gamepad::EButton theButton,
	EButtonAction theAction)
{
	DBG_ASSERT((unsigned)theModeID < sModes.size());
	DBG_ASSERT(theAction < eButtonAction_Num);

	Command result;
	ButtonActionsMap::const_iterator itr;
	do
	{
		itr = sModes[theModeID].actions.find(Gamepad::EButton(theButton));
		if( itr != sModes[theModeID].actions.end() )
		{
			result = itr->second.cmd[theAction];
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


const std::string& macroLabel(int theMacroSetID, int theMacroSlotID)
{
	DBG_ASSERT((unsigned)theMacroSetID < sMacroSets.size());
	DBG_ASSERT((unsigned)theMacroSlotID < kMacroSlotsPerSet);

	return sMacroSets[theMacroSetID].slot[theMacroSlotID].label;
}

} // InputMap
