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


u8 keyNameToVirtualKey(const std::string& theKeyName)
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
			const size_t kMapSize = 131;
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
			map.setValue("JUMP",			VK_SELECT); // jump mouse to pos
			map.setValue("POINT",			VK_SELECT); // jump mouse to pos
			map.setValue("CURSOR",			VK_SELECT); // jump mouse to pos
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
			const size_t actualMapSize = map.size();
			DBG_ASSERT(actualMapSize == kMapSize);
		}
	};
	static NameToVKeyMapper sKeyMapper;

	u8* result = sKeyMapper.map.find(theKeyName);
	return result ? *result : 0;
}


std::string virtualKeyToName(u8 theVKey)
{
	switch(theVKey)
	{
	case VK_LBUTTON: return "Left Mouse Button";
	case VK_MBUTTON: return "Middle Mouse Button";
	case VK_RBUTTON: return "Right Mouse Button";
	}
	LONG aScanCode = MapVirtualKey(theVKey, 0) << 16;
	switch(theVKey)
	{
	case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
	case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
	case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
	case VK_NUMLOCK:
		aScanCode |= KF_EXTENDED;
	}
	char aKeyName[256];
	if( GetKeyNameTextA(aScanCode, aKeyName, 256) )
		return aKeyName;
	return "UNKNOWN";
}


EButton buttonNameToID(const std::string& theName)
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
			const size_t actualMapSize = map.size();
			DBG_ASSERT(actualMapSize == kMapSize);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EButton* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eBtn_Num;
}


EHUDType hudTypeNameToID(const std::string& theName)
{
	struct NameToEnumMapper
	{
		typedef StringToValueMap<u32, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			const size_t kMapSize = 73;
			map.reserve(kMapSize);
			map.setValue("LIST",				eMenuStyle_List);
			map.setValue("BASIC",				eMenuStyle_List);
			map.setValue("DEFAULT",				eMenuStyle_List);
			map.setValue("NORMAL",				eMenuStyle_List);
			map.setValue("LISTWRAP",			eMenuStyle_ListWrap);
			map.setValue("BASICWRAP",			eMenuStyle_ListWrap);
			map.setValue("DEFAULTWRAP",			eMenuStyle_ListWrap);
			map.setValue("NORMALWRAP",			eMenuStyle_ListWrap);
			map.setValue("SLOT",				eMenuStyle_Slots);
			map.setValue("SLOTS",				eMenuStyle_Slots);
			map.setValue("PILLAR",				eMenuStyle_Slots);
			map.setValue("PILLARS",				eMenuStyle_Slots);
			map.setValue("COLUMN",				eMenuStyle_Slots);
			map.setValue("COLUMNS",				eMenuStyle_Slots);
			map.setValue("BAR",					eMenuStyle_Bar);
			map.setValue("BARS",				eMenuStyle_Bar);
			map.setValue("ROW",					eMenuStyle_Bar);
			map.setValue("ROWS",				eMenuStyle_Bar);
			map.setValue("HOTBAR",				eMenuStyle_Bar);
			map.setValue("BARWRAP",				eMenuStyle_BarWrap);
			map.setValue("BARSWRAP",			eMenuStyle_BarWrap);
			map.setValue("ROWWRAP",				eMenuStyle_BarWrap);
			map.setValue("ROWSWRAP",			eMenuStyle_BarWrap);
			map.setValue("HOTBARWRAP",			eMenuStyle_BarWrap);
			map.setValue("4DIR",				eMenuStyle_4Dir);
			map.setValue("COMPASS",				eMenuStyle_4Dir);
			map.setValue("CROSS",				eMenuStyle_4Dir);
			map.setValue("DPAD",				eMenuStyle_4Dir);
			map.setValue("DIR",					eMenuStyle_4Dir);
			map.setValue("DIRECTIONS",			eMenuStyle_4Dir);
			map.setValue("DIRECTIONAL",			eMenuStyle_4Dir);
			map.setValue("GRID",				eMenuStyle_Grid);
			map.setValue("GRIDWRAP",			eMenuStyle_GridWrap);
			map.setValue("RING",				eMenuStlye_Ring);
			map.setValue("RADIAL",				eMenuStyle_Radial);
			map.setValue("RECTANGLE",			eHUDItemType_Rect);
			map.setValue("RECT",				eHUDItemType_Rect);
			map.setValue("BLOCK",				eHUDItemType_Rect);
			map.setValue("SQUARE",				eHUDItemType_Rect);
			map.setValue("RNDRECT",				eHUDItemType_RndRect);
			map.setValue("ROUNDRECT",			eHUDItemType_RndRect);
			map.setValue("ROUNDEDRECT",			eHUDItemType_RndRect);
			map.setValue("ROUNDRECTANGLE",		eHUDItemType_RndRect);
			map.setValue("ROUNDEDRECTANGLE",	eHUDItemType_RndRect);
			map.setValue("BITMAP",				eHUDItemType_Bitmap);
			map.setValue("IMAGE",				eHUDItemType_Bitmap);
			map.setValue("CIRCLE",				eHUDItemType_Circle);
			map.setValue("DOT",					eHUDItemType_Circle);
			map.setValue("ELLIPSE",				eHUDItemType_Circle);
			map.setValue("ARROWL",				eHUDItemType_ArrowL);
			map.setValue("LARROW",				eHUDItemType_ArrowL);
			map.setValue("ARROWLEFT",			eHUDItemType_ArrowL);
			map.setValue("LEFTARROW",			eHUDItemType_ArrowL);
			map.setValue("ARROWR",				eHUDItemType_ArrowR);
			map.setValue("RARROW",				eHUDItemType_ArrowR);
			map.setValue("ARROWRIGHT",			eHUDItemType_ArrowR);
			map.setValue("RIGHTARROW",			eHUDItemType_ArrowR);
			map.setValue("ARROWU",				eHUDItemType_ArrowU);
			map.setValue("UARROW",				eHUDItemType_ArrowU);
			map.setValue("ARROWUP",				eHUDItemType_ArrowU);
			map.setValue("UPARROW",				eHUDItemType_ArrowU);
			map.setValue("ARROWD",				eHUDItemType_ArrowD);
			map.setValue("DARROW",				eHUDItemType_ArrowD);
			map.setValue("ARROWDOWN",			eHUDItemType_ArrowD);
			map.setValue("DOWNARROW",			eHUDItemType_ArrowD);
			map.setValue("GROUPTARGET",			eHUDType_GroupTarget);
			map.setValue("TARGETGROUP",			eHUDType_GroupTarget);
			map.setValue("SAVEDTARGET",			eHUDType_DefaultTarget);
			map.setValue("DEFAULTTARGET",		eHUDType_DefaultTarget);
			map.setValue("FAVORITETARGET",		eHUDType_DefaultTarget);
			map.setValue("TARGETSAVED",			eHUDType_DefaultTarget);
			map.setValue("TARGETDEFAULT",		eHUDType_DefaultTarget);
			map.setValue("TARGETFAVORITE",		eHUDType_DefaultTarget);

			const size_t actualMapSize = map.size();
			DBG_ASSERT(actualMapSize == kMapSize);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	u32* result = sNameToEnumMapper.map.find(theName);
	return result ? EHUDType(*result) : eHUDType_Num;
}


ECommandKeyWord commandWordToID(const std::string& theWord)
{
	struct WordToEnumMapper
	{
		typedef StringToValueMap<ECommandKeyWord, u8> WordToEnumMap;
		WordToEnumMap map;
		WordToEnumMapper()
		{
			const size_t kMapSize = 108;
			map.reserve(kMapSize);
			map.setValue("ADD",				eCmdWord_Add);
			map.setValue("REMOVE",			eCmdWord_Remove);
			map.setValue("HOLD",			eCmdWord_Hold);
			map.setValue("REPLACE",			eCmdWord_Replace);
			map.setValue("SWAP",			eCmdWord_Replace);
			map.setValue("TOGGLE",			eCmdWord_Toggle);
			map.setValue("LAYER",			eCmdWord_Layer);
			map.setValue("LAYERS",			eCmdWord_Layer);
			map.setValue("NONCHILD",		eCmdWord_NonChild);
			map.setValue("INDEPENDENT",		eCmdWord_NonChild);
			map.setValue("INDEPENDENTLY",	eCmdWord_NonChild);
			map.setValue("PARENT",			eCmdWord_Parent);
			map.setValue("OWNER",			eCmdWord_Parent);
			map.setValue("GRANDPARENT",		eCmdWord_Grandparent);
			map.setValue("ALL",				eCmdWord_All);
			map.setValue("MOUSE",			eCmdWord_Mouse);
			map.setValue("WHEEL",			eCmdWord_MouseWheel);
			map.setValue("MOUSEWHEEL",		eCmdWord_MouseWheel);
			map.setValue("SMOOTH",			eCmdWord_Smooth);
			map.setValue("STEP",			eCmdWord_Stepped);
			map.setValue("STEPPED",			eCmdWord_Stepped);
			map.setValue("ONCE",			eCmdWord_Once);
			map.setValue("SINGLE",			eCmdWord_Once);
			map.setValue("MOVE",			eCmdWord_Move);
			map.setValue("TURN",			eCmdWord_Turn);
			map.setValue("MOVETURN",		eCmdWord_Turn);
			map.setValue("STRAFE",			eCmdWord_Strafe);
			map.setValue("MOVESTRAFE",		eCmdWord_Strafe);
			map.setValue("SELECT",			eCmdWord_Select);
			map.setValue("HOTSPOT",			eCmdWord_Hotspot);
			map.setValue("HOTSPOTS",		eCmdWord_Hotspot);
			map.setValue("RESET",			eCmdWord_Reset);
			map.setValue("EDIT",			eCmdWord_Edit);
			map.setValue("REASSIGN",		eCmdWord_Edit);
			map.setValue("REWRITE",			eCmdWord_Edit);
			map.setValue("MENU",			eCmdWord_Menu);
			map.setValue("CONFIRM",			eCmdWord_Confirm);
			map.setValue("B",				eCmdWord_Back);
			map.setValue("BACK",			eCmdWord_Back);
			map.setValue("CANCEL",			eCmdWord_Back);
			map.setValue("CLOSE",			eCmdWord_Close);
			map.setValue("QUIT",			eCmdWord_Close);
			map.setValue("EXIT",			eCmdWord_Close);
			map.setValue("TARGET",			eCmdWord_Target);
			map.setValue("GROUP",			eCmdWord_Group);
			map.setValue("L",				eCmdWord_Left);
			map.setValue("LEFT",			eCmdWord_Left);
			map.setValue("R",				eCmdWord_Right);
			map.setValue("RIGHT",			eCmdWord_Right);
			map.setValue("U",				eCmdWord_Up);
			map.setValue("UP",				eCmdWord_Up);
			map.setValue("F",				eCmdWord_Up);
			map.setValue("FORWARD",			eCmdWord_Up);
			map.setValue("PREV",			eCmdWord_Up);
			map.setValue("TOP",				eCmdWord_Up);
			map.setValue("D",				eCmdWord_Down);
			map.setValue("DOWN",			eCmdWord_Down);
			map.setValue("NEXT",			eCmdWord_Down);
			map.setValue("BOTTOM",			eCmdWord_Down);
			map.setValue("PREVWRAP",		eCmdWord_PrevWrap);
			map.setValue("UWRAP",			eCmdWord_PrevWrap);
			map.setValue("UPWRAP",			eCmdWord_PrevWrap);
			map.setValue("NEXTWRAP",		eCmdWord_NextWrap);
			map.setValue("DWRAP",			eCmdWord_NextWrap);
			map.setValue("DOWNWRAP",		eCmdWord_NextWrap);
			map.setValue("WRAP",			eCmdWord_Wrap);
			map.setValue("PREVNOWRAP",		eCmdWord_PrevNoWrap);
			map.setValue("UNOWRAP",			eCmdWord_PrevNoWrap);
			map.setValue("UPNOWRAP",		eCmdWord_PrevNoWrap);
			map.setValue("NEXTNOWRAP",		eCmdWord_NextNoWrap);
			map.setValue("DNOWRAP",			eCmdWord_NextNoWrap);
			map.setValue("DOWNNOWRAP",		eCmdWord_NextNoWrap);
			map.setValue("NOWRAP",			eCmdWord_NoWrap);
			map.setValue("FAVORITE",		eCmdWord_Default);
			map.setValue("SAVED",			eCmdWord_Default);
			map.setValue("DEFAULT",			eCmdWord_Default);
			map.setValue("LOAD",			eCmdWord_Load);
			map.setValue("RECALL",			eCmdWord_Load);
			map.setValue("SAVE",			eCmdWord_Set);
			map.setValue("STORE",			eCmdWord_Set);
			map.setValue("SET",				eCmdWord_Set);
			map.setValue("LAST",			eCmdWord_Last);
			map.setValue("PET",				eCmdWord_Pet);
			map.setValue("CHANGE",			eCmdWord_Change);
			map.setValue("SWITCH",			eCmdWord_Change);
			map.setValue("PROFILE",			eCmdWord_Profile);
			map.setValue("APP",				eCmdWord_App);
			map.setValue("APPLICATION",		eCmdWord_App);
			map.setValue("PROGRAM",			eCmdWord_App);
			map.setValue("OVERLAY",			eCmdWord_App);
			map.setValue("MEMBER",			eCmdWord_Ignored);
			map.setValue("CHAR",			eCmdWord_Ignored);
			map.setValue("CHARACTER",		eCmdWord_Ignored);
			map.setValue("SELECTION",		eCmdWord_Ignored);
			map.setValue("CURRENT",			eCmdWord_Ignored);
			map.setValue("ACTIVE",			eCmdWord_Ignored);
			map.setValue("ITEM",			eCmdWord_Ignored);
			map.setValue("OPTION",			eCmdWord_Ignored);
			map.setValue("A",				eCmdWord_Filler);
			map.setValue("AND",				eCmdWord_Filler);
			map.setValue("OR",				eCmdWord_Filler);
			map.setValue("THE",				eCmdWord_Filler);
			map.setValue("IN",				eCmdWord_Filler);
			map.setValue("TO",				eCmdWord_Filler);
			map.setValue("OUT",				eCmdWord_Filler);
			map.setValue("OF",				eCmdWord_Filler);
			map.setValue("AT",				eCmdWord_Filler);
			map.setValue("WITH",			eCmdWord_Filler);
			const size_t actualMapSize = map.size();
			DBG_ASSERT(actualMapSize == kMapSize);
		}
	};
	static WordToEnumMapper sWordToEnumMapper;

	ECommandKeyWord* result = sWordToEnumMapper.map.find(theWord);
	if( !result )
	{
		if( isAnInteger(theWord) )
			return eCmdWord_Integer;
		return eCmdWord_Unknown;
	}

	return *result;
}
