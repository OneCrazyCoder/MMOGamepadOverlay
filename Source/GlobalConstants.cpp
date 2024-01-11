//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Common.h"
#include "Lookup.h"

//-----------------------------------------------------------------------------
// Data Tables
//-----------------------------------------------------------------------------

DBG_CTASSERT(eCmdType_Num <= 256);

const char* kProfileButtonName[] =
{
	"Auto",		// eBtn_None
	"LSLeft",	// eBtn_LSLeft
	"LSRight",	// eBtn_LSRight
	"LSUp",		// eBtn_LSUp
	"LSDown",	// eBtn_LSDown
	"RSLeft",	// eBtn_RSLeft
	"RSRight",	// eBtn_RSRight
	"RSUp",		// eBtn_RSUp
	"RSDown",	// eBtn_RSDown
	"DLeft",	// eBtn_DLeft
	"DRight",	// eBtn_DRight
	"DUp",		// eBtn_DUp
	"DDown",	// eBtn_DDown
	"FLeft",	// eBtn_FLeft
	"FRight",	// eBtn_FRight
	"FUp",		// eBtn_FUp
	"FDown",	// eBtn_FDown
	"L1",		// eBtn_L1
	"R1",		// eBtn_R1
	"L2",		// eBtn_L2
	"R2",		// eBtn_R2	
	"Select",	// eBtn_Select
	"Start",	// eBtn_Start
	"L3",		// eBtn_L3
	"R3",		// eBtn_R3
	"Home",		// eBtn_Home
	"Extra",	// eBtn_Extra
};
DBG_CTASSERT(ARRAYSIZE(kProfileButtonName) == eBtn_Num);


u8 keyNameToVirtualKey(const std::string& theKeyName, bool allowMouse)
{
	if( theKeyName.size() == 1 )
	{
		SHORT aVKey = VkKeyScan(::tolower(theKeyName[0]));
		DBG_ASSERT(aVKey <= 0xFF);
		return u8(aVKey);
	}

	struct NameToVKeyMapper
	{
		typedef StringToValueMap<u8, u8> NameToVKeyMap;
		NameToVKeyMap map;
		NameToVKeyMapper()
		{
			const size_t kMapSize = 128;
			map.reserve(kMapSize);
			map.setValue("CLICK",			VK_LBUTTON);
			map.setValue("LCLICK",			VK_LBUTTON);
			map.setValue("LEFTCLICK",		VK_LBUTTON);
			map.setValue("LMB",				VK_LBUTTON);
			map.setValue("RCLICK",			VK_RBUTTON);
			map.setValue("RIGHTCLICK",		VK_RBUTTON);
			map.setValue("RMB",				VK_RBUTTON);
			map.setValue("RELEASE",			VK_CANCEL); // Force release a key
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
			// VK_PAUSE not included because it has special use/meaning
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

	u8* result = sKeyMapper.map.find(theKeyName);
	if( !allowMouse && result && *result <= 0x07) result = NULL;
	return result ? *result : 0;
}


EButton buttonNameToID(const std::string& theString)
{	
	struct NameToEnumMapper
	{
		typedef StringToValueMap<EButton, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			const size_t kMapSize = eBtn_Num + 70;
			map.reserve(kMapSize);
			// Add default names for each button to map
			for(int i = 0; i < eBtn_Num; ++i)
				map.setValue(upper(kProfileButtonName[i]), EButton(i));
			// Add some extra aliases
			map.setValue("LL",				eBtn_LSLeft);
			map.setValue("LSL",				eBtn_LSLeft);
			map.setValue("LSTICKLEFT",		eBtn_LSLeft);
			map.setValue("LEFTSTICKLEFT",	eBtn_LSLeft);
			map.setValue("LR",				eBtn_LSRight);
			map.setValue("LSR",				eBtn_LSRight);
			map.setValue("LSTICKRIGHT",		eBtn_LSRight);
			map.setValue("LEFTSTICKRIGHT",	eBtn_LSRight);
			map.setValue("LU",				eBtn_LSUp);
			map.setValue("LSU",				eBtn_LSUp);
			map.setValue("LSTICKUP",		eBtn_LSUp);
			map.setValue("LEFTSTICKUP",		eBtn_LSUp);
			map.setValue("LD",				eBtn_LSDown);
			map.setValue("LSD",				eBtn_LSDown);
			map.setValue("LSTICKDOWN",		eBtn_LSDown);
			map.setValue("LEFTSTICKDOWN",	eBtn_LSDown);
			map.setValue("RL",				eBtn_RSLeft);
			map.setValue("RSL",				eBtn_RSLeft);
			map.setValue("RSTICKLEFT",		eBtn_RSLeft);
			map.setValue("RIGHTSTICKLEFT",	eBtn_RSLeft);
			map.setValue("RR",				eBtn_RSRight);
			map.setValue("RSR",				eBtn_RSRight);
			map.setValue("RSTICKRIGHT",		eBtn_RSRight);
			map.setValue("RIGHTSTICKRIGHT",	eBtn_RSRight);
			map.setValue("RU",				eBtn_RSUp);
			map.setValue("RSU",				eBtn_RSUp);
			map.setValue("RSTICKUP",		eBtn_RSUp);
			map.setValue("RIGHTSTICKUP",	eBtn_RSUp);
			map.setValue("RD",				eBtn_RSDown);
			map.setValue("RSD",				eBtn_RSDown);
			map.setValue("RSTICKDOWN",		eBtn_RSDown);
			map.setValue("RIGHTSTICKDOWN",	eBtn_RSDown);
			map.setValue("DL",				eBtn_DLeft);
			map.setValue("DPL",				eBtn_DLeft);
			map.setValue("DPLEFT",			eBtn_DLeft);
			map.setValue("DPADLEFT",		eBtn_DLeft);
			map.setValue("DR",				eBtn_DRight);
			map.setValue("DPR",				eBtn_DRight);
			map.setValue("DPRIGHT",			eBtn_DRight);
			map.setValue("DPADRIGHT",		eBtn_DRight);
			map.setValue("DU",				eBtn_DUp);
			map.setValue("DPU",				eBtn_DUp);
			map.setValue("DPUP",			eBtn_DUp);
			map.setValue("DPADUP",			eBtn_DUp);
			map.setValue("DD",				eBtn_DDown);
			map.setValue("DPD",				eBtn_DDown);
			map.setValue("DPDOWN",			eBtn_DDown);
			map.setValue("DPADDOWN",		eBtn_DDown);
			map.setValue("FL",				eBtn_FLeft);
			map.setValue("FPL",				eBtn_FLeft);
			map.setValue("FPADLEFT",		eBtn_FLeft);
			map.setValue("SQUARE",			eBtn_FLeft);
			map.setValue("XBX",				eBtn_FLeft);
			map.setValue("FR",				eBtn_FRight);
			map.setValue("FPR",				eBtn_FRight);
			map.setValue("FPADRIGHT",		eBtn_FRight);
			map.setValue("CIRCLE",			eBtn_FRight);
			map.setValue("CIRC",			eBtn_FRight);
			map.setValue("XBB",				eBtn_FRight);
			map.setValue("FU",				eBtn_FUp);
			map.setValue("FPU",				eBtn_FUp);
			map.setValue("FPADUP",			eBtn_FUp);
			map.setValue("TRIANGLE",		eBtn_FUp);
			map.setValue("TRI",				eBtn_FUp);
			map.setValue("XBY",				eBtn_FUp);
			map.setValue("FD",				eBtn_FDown);
			map.setValue("FPD",				eBtn_FDown);
			map.setValue("FPADDOWN",		eBtn_FDown);
			map.setValue("XBA",				eBtn_FDown);
			map.setValue("PSX",				eBtn_FDown);
			DBG_ASSERT(map.size() == kMapSize);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EButton* result = sNameToEnumMapper.map.find(theString);
	return result ? *result : eBtn_Num;
}


EHUDElement hudElementNameToID(const std::string& theString)
{	
	struct NameToElemMapper
	{
		typedef StringToValueMap<EHUDElement, u8> NameToElemMap;
		NameToElemMap map;
		NameToElemMapper()
		{
			const size_t kMapSize = 6;
			map.reserve(kMapSize);
			map.setValue("MACROS",		eHUDElement_Macros);
			map.setValue("ABILITIES",	eHUDElement_Abilities);
			map.setValue("ABILITY",		eHUDElement_Abilities);
			map.setValue("SPELLS",		eHUDElement_Abilities);
			map.setValue("GRID",		eHUDElement_Abilities);
			map.setValue("HOTBAR",		eHUDElement_Abilities);
			DBG_ASSERT(map.size() == kMapSize);
		}
	};
	static NameToElemMapper sNameToElemMapper;

	EHUDElement* result = sNameToElemMapper.map.find(theString);
	return result ? *result : eHUDElement_Num;
}
