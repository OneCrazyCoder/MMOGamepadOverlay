//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputMap.h"

#include "Gamepad.h"
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

const char* kMacroSlotLabel[] = { "U", "L", "R", "D" };
DBG_CTASSERT(ARRAYSIZE(kMacroSlotLabel) >= kMacroSlotsPerSet);


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct MacroData
{
	std::string label;
	std::string keys;
};

struct MacroSet
{
	std::string label;
	MacroData slot[kMacroSlotsPerSet];
};

struct MacroSetBuilder
{
	size_t currMacroSet;
	std::vector<std::string> parsedCommand;
	std::string debugMacroSlotName;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<MacroSet> sMacroSets;
static std::vector<Scheme> sSchemes;


//-----------------------------------------------------------------------------
// Scheme
//-----------------------------------------------------------------------------

Scheme::Scheme() :
	mouseLookOn(false)
{
}


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


static MacroData stringToMacroSlot(
	MacroSetBuilder* theSetBuilder,
	std::string theString)
{
	DBG_ASSERT(theSetBuilder);

	MacroData aMacroData;
	if( theString.empty() )
		return aMacroData;

	// Get the label (part of the string before first colon)
	aMacroData.label = breakOffItemBeforeChar(theString, ':');

	if( aMacroData.label.empty() && !theString.empty() )
	{// Having no : character means this is a sub-set
		aMacroData.label = trim(theString);
		aMacroData.keys.push_back(eCmdChar_ChangeMacroSet);
		aMacroData.keys.push_back(u8(sMacroSets.size()));
		if( sMacroSets.size() > 0xFF )
			aMacroData.keys.push_back(u8(sMacroSets.size() >> 8));
		sMacroSets.push_back(MacroSet());
		sMacroSets.back().label =
			sMacroSets[theSetBuilder->currMacroSet].label +
			"." + aMacroData.label;
		return aMacroData;
	}

	if( theString.empty() )
		return aMacroData;

	// Check for a slash command or say string, which outputs the whole string
	if( theString[0] == eCmdChar_SlashCommand ||
		theString[0] == eCmdChar_SayString )
	{
		aMacroData.keys = theString;
		return aMacroData;
	}

	// Break the remaining string (after label) into individual words
	theSetBuilder->parsedCommand.clear();
	sanitizeSentence(theString, &theSetBuilder->parsedCommand);
	if( theSetBuilder->parsedCommand.empty() )
		return aMacroData;

	// Should be a key press sequence
	for(int aWord = 0; aWord < theSetBuilder->parsedCommand.size(); ++aWord)
	{
		DBG_ASSERT(!theSetBuilder->parsedCommand[aWord].empty());
		const std::string& aKeyName = theSetBuilder->parsedCommand[aWord];
		const u8 aVKey = keyNameToVirtualKey(aKeyName);
		if( aVKey == 0 )
		{
			// Check if it's a modifier+key in one word like Shift2 or Alt1
			if( checkForComboKeyName(aKeyName, &aMacroData.keys) != eResult_Ok )
			{
				// Probably just forgot the > at front of a plain string
				aMacroData.keys.clear();
				aMacroData.keys.push_back('>');
				aMacroData.keys += theString;
				return aMacroData;
			}
		}
		else
		{
			aMacroData.keys.push_back(aVKey);
		}
	}

	return aMacroData;
}


static void buildMacroSets()
{
	MacroSetBuilder aMacroSetBuilder;

	// Root macro set is just named "Macros"
	sMacroSets.push_back(MacroSet());
	sMacroSets.back().label = "Macros";
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
	sSchemes.push_back(Scheme());
	Scheme& aScheme = sSchemes.back();
	aScheme.mouseLookOn = false;

	std::string aCmd;
	aCmd.push_back(eCmdChar_Mouse);
	aCmd.push_back(eSubCmdChar_Left);
	aScheme.cmd[eBtn_RSLeft].press = aCmd;
	aCmd[1] = eSubCmdChar_Right;
	aScheme.cmd[eBtn_RSRight].press = aCmd;
	aCmd[1] = eSubCmdChar_Up;
	aScheme.cmd[eBtn_RSUp].press = aCmd;
	aCmd[1] = eSubCmdChar_Down;
	aScheme.cmd[eBtn_RSDown].press = aCmd;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	sSchemes.clear();
	buildControlSchemes();
	sMacroSets.clear();
	buildMacroSets();
}


const Scheme& controlScheme(int theModeID)
{
	DBG_ASSERT(theModeID == 0);
	return sSchemes[0];
}


u32 visibleHUDElements(int theMode)
{
	return eHUDElement_None;
}


std::string macroOutput(int theMacroSetID, int theMacroSlotID)
{
	DBG_ASSERT((unsigned)theMacroSetID < sMacroSets.size());
	DBG_ASSERT((unsigned)theMacroSlotID < kMacroSlotsPerSet);

	return sMacroSets[theMacroSetID].slot[theMacroSlotID].keys;
}


std::string macroLabel(int theMacroSetID, int theMacroSlotID)
{
	DBG_ASSERT((unsigned)theMacroSetID < sMacroSets.size());
	DBG_ASSERT((unsigned)theMacroSlotID < kMacroSlotsPerSet);

	return sMacroSets[theMacroSetID].slot[theMacroSlotID].label;
}

} // InputMap
