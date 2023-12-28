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
kFirstExButton = -4,
kExButtonLStick = -4,
kExButtonRStick = -3,
kExButtonDPad = -2,
kExButtonFPad = -1,
};

const char* kMainMacroSetLabel = "Macros";
const char* kModePrefix = "Mode.";
const char* kMacroSlotLabel[] = { "U", "L", "R", "D" };
DBG_CTASSERT(ARRAYSIZE(kMacroSlotLabel) == kMacroSlotsPerSet);

const char* kParentModeKey = "Inherits";
const char* kHUDSettingsKey = "HUD";
const char* kMouseLookKey = "MouseLook";

const char* kButtonActionPrefx[] =
{
	"Press",	// eButtonAction_Press
	"Tap",		// eButtonAction_Tap
	"Hold",		// eButtonAction_OnceHeld
	"Release",	// eButtonAction_Release
	"",			// eButtonAction_Analog
};
DBG_CTASSERT(ARRAYSIZE(kButtonActionPrefx) == eButtonAction_Num);


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

struct InputMapBuilder
{
	size_t currentItem;
	std::vector<std::string> parsedCommand;
	StringToValueMap<u8> nameToIdxMap;
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
			map.reserve(126);
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
			DBG_ASSERT(map.size() == 126 && "update reserve above to match if assert");
		}
	};
	static NameToVKeyMapper sKeyMapper;

	u8* result = sKeyMapper.map.find(upper(theKeyName));
	if( !allowMouse && result && *result <= 0x07) result = NULL;
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
		const u8 aModKey = keyNameToVirtualKey(aModKeyName, false);
		if( aModKey != 0 &&
			(aModKey == VK_SHIFT ||
			 aModKey == VK_CONTROL ||
			 aModKey == VK_MENU) )
		{// Found a valid modifier key
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(aKeyName, false))
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
	InputMapBuilder* theBuilder,
	std::string theString)
{
	DBG_ASSERT(theBuilder);

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
			sMacroSets[theBuilder->currentItem].label +
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
	theBuilder->parsedCommand.clear();
	sanitizeSentence(theString, &theBuilder->parsedCommand);
	if( theBuilder->parsedCommand.empty() )
		return aMacroSlot;

	// Should be a key press sequence
	std::string aKeySeq;
	for(int aWord = 0; aWord < theBuilder->parsedCommand.size(); ++aWord)
	{
		DBG_ASSERT(!theBuilder->parsedCommand[aWord].empty());
		const std::string& aKeyName = theBuilder->parsedCommand[aWord];
		const u8 aVKey = keyNameToVirtualKey(aKeyName, false);
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


static void buildMacroSets(InputMapBuilder* theBuilder)
{
	DBG_ASSERT(theBuilder);

	sMacroSets.push_back(MacroSet());
	sMacroSets.back().label = kMainMacroSetLabel;
	for(int aSet = 0; aSet < sMacroSets.size(); ++aSet )
	{
		theBuilder->currentItem = aSet;
		const std::string aPrefix = sMacroSets[aSet].label;
		for(int aSlot = 0; aSlot < kMacroSlotsPerSet; ++aSlot)
		{
			sMacroSets[aSet].slot[aSlot] =
				stringToMacroSlot(
					theBuilder,
					Profile::getStr(
						aPrefix + "/" + kMacroSlotLabel[aSlot]));
		}
	}
}


static const char* exButtonName(int theButton)
{
	DBG_ASSERT(theButton >= kFirstExButton);
	switch(theButton)
	{
	case kExButtonLStick:	return "LStick";
	case kExButtonRStick:	return "RStick";
	case kExButtonDPad:		return "DPad";
	case kExButtonFPad:		return "FPad";
	}

	return Gamepad::profileButtonName(Gamepad::EButton(theButton));
}


static void addButtonAction(
	InputMapBuilder* theBuilder,
	u8 theModeIdx,
	int theButton,
	u8 theAction,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theBuilder);

	debugPrint("Assign %s Mode: %s %s to: %s\n",
		theBuilder->nameToIdxMap.keys()[theModeIdx-1].c_str(),
		kButtonActionPrefx[theAction],
		exButtonName(theButton),
		theCmdStr.c_str());
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
		const std::string aModePrefix(kModePrefix + theModeName + "/");
		// Get (or make) parent mode, if has one
		const std::string& aParentModeName =
			Profile::getStr(aModePrefix + kParentModeKey);
		if( !aParentModeName.empty() )
		{
			sModes[idx].parentMode =
				getOrCreateMode(theBuilder, aParentModeName);
		}
		// TODO: Figure out hud bits, starting w/ parent and looking up show/hide
		// Use parent mouselook setting unless have our own specified
		sModes[idx].mouseLookOn = Profile::getBool(
			aModePrefix + kMouseLookKey,
			sModes[sModes[idx].parentMode].mouseLookOn);
		// TODO: Get special-case things like assigning "Move" to "LStick" to set
		// defaults for individual LStick axis all in one step
		// Check every action for every button for things to assign
		for(int aBtn = kFirstExButton; aBtn < Gamepad::eBtn_Num; ++aBtn)
		{
			const std::string aBtnName(exButtonName(aBtn));
			for(u8 act = 0; act < eButtonAction_Num; ++act)
			{
				const std::string aCmdStr = Profile::getStr(
					aModePrefix + kButtonActionPrefx[act] + aBtnName);
				if( !aCmdStr.empty() )
					addButtonAction(theBuilder, idx, aBtn, act, aCmdStr);
			}
		}
	}

	return idx;
}


static void buildControlSchemes(InputMapBuilder* theBuilder)
{
	using namespace Gamepad;

	// First mode is empty so parentMode == 0 means no parent.
	// It will itself use the default mode as its parent so at
	// startup, mode of '0' will refer to this one but end up
	// returning the button assignments from the default mode.
	sModes.push_back(ControlsMode());

	// Get name of default mode
	std::string aDefaultModeName = Profile::getStr("Setup/Mode");
	if( aDefaultModeName.empty() )
		return;

	sModes[0].parentMode = getOrCreateMode(theBuilder, aDefaultModeName);
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
	sModes.clear();
	sMacroSets.clear();

	InputMapBuilder anInputMapBuilder;
	buildControlSchemes(&anInputMapBuilder);
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
