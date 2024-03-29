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
			struct { const char* str; u8 val; } kEntries[] = {
				{ "CLICK",			VK_LBUTTON		},
				{ "LCLICK",			VK_LBUTTON		},
				{ "LEFTCLICK",		VK_LBUTTON		},
				{ "LMB",			VK_LBUTTON		},
				{ "RCLICK",			VK_RBUTTON		},
				{ "RIGHTCLICK",		VK_RBUTTON		},
				{ "RMB",			VK_RBUTTON		},
				{ "RELEASE",		VK_CANCEL		}, // Force release a key
				{ "MCLICK",			VK_MBUTTON		},
				{ "MIDDLECLICK",	VK_MBUTTON		},
				{ "MMB",			VK_MBUTTON		},
				{ "BACK",			VK_BACK			},
				{ "BACKSPACE",		VK_BACK			},
				{ "BS",				VK_BACK			},
				{ "TAB",			VK_TAB			},
				{ "RETURN",			VK_RETURN		},
				{ "ENTER",			VK_RETURN		},
				{ "SH",				VK_SHIFT		},
				{ "SHFT",			VK_SHIFT		},
				{ "SHIFT",			VK_SHIFT		},
				{ "CTRL",			VK_CONTROL		},
				{ "CONTROL",		VK_CONTROL		},
				{ "ALT",			VK_MENU			},
				{ "MENU",			VK_MENU			},
				{ "ESC",			VK_ESCAPE		},
				{ "ESCAPE",			VK_ESCAPE		},
				{ "SPACE",			VK_SPACE		},
				{ "SPACEBAR",		VK_SPACE		},
				{ "PGUP",			VK_PRIOR		},
				{ "PAGEUP",			VK_PRIOR		},
				{ "PAGEDOWN",		VK_NEXT			},
				{ "PGDOWN",			VK_NEXT			},
				{ "PGDWN",			VK_NEXT			},
				{ "END",			VK_END			},
				{ "HOME",			VK_HOME			},
				{ "LEFT",			VK_LEFT			},
				{ "LEFTARROW",		VK_LEFT			},
				{ "UP",				VK_UP			},
				{ "UPARROW",		VK_UP			},
				{ "RIGHT",			VK_RIGHT		},
				{ "RIGHTARROW",		VK_RIGHT		},
				{ "DOWN",			VK_DOWN			},
				{ "DOWNARROW",		VK_DOWN			},
				{ "JUMP",			VK_SELECT		}, // jump mouse to pos
				{ "POINT",			VK_SELECT		}, // jump mouse to pos
				{ "CURSOR",			VK_SELECT		}, // jump mouse to pos
				{ "INS",			VK_INSERT		},
				{ "INSERT",			VK_INSERT		},
				{ "DEL",			VK_DELETE		},
				{ "DELETE",			VK_DELETE		},
				{ "NUMMULT",		VK_MULTIPLY		},
				{ "NUMMULTIPLY",	VK_MULTIPLY		},
				{ "NUMADD",			VK_ADD			},
				{ "NUMPLUS",		VK_ADD			},
				{ "NUMSUB",			VK_SUBTRACT		},
				{ "NUMSUBTRACT",	VK_SUBTRACT		},
				{ "NUMMINUS",		VK_SUBTRACT		},
				{ "NUMPERIOD",		VK_DECIMAL		},
				{ "NUMDECIMAL",		VK_DECIMAL		},
				{ "NUMDIV",			VK_DIVIDE		},
				{ "NUMDIVIDE",		VK_DIVIDE		},
				{ "NUMSLASH",		VK_DIVIDE		},
				{ "NUMPADMULT",		VK_MULTIPLY		},
				{ "NUMPADMULTIPLY",	VK_MULTIPLY		},
				{ "NUMPADADD",		VK_ADD			},
				{ "NUMPADPLUS",		VK_ADD			},
				{ "NUMPADSUB",		VK_SUBTRACT		},
				{ "NUMPADSUBTRACT",	VK_SUBTRACT		},
				{ "NUMPADMINUS",	VK_SUBTRACT		},
				{ "NUMPADPERIOD",	VK_DECIMAL		},
				{ "NUMPADDECIMAL",	VK_DECIMAL		},
				{ "NUMPADDIV",		VK_DIVIDE		},
				{ "NUMPADDIVIDE",	VK_DIVIDE		},
				{ "NUMPADSLASH",	VK_DIVIDE		},
				{ "NUMLOCK",		VK_NUMLOCK		},
				{ "COLON",			VK_OEM_1		},
				{ "SEMICOLON",		VK_OEM_1		},
				{ "PLUS",			VK_OEM_PLUS		},
				{ "EQUAL",			VK_OEM_PLUS		},
				{ "EQUALS",			VK_OEM_PLUS		},
				{ "COMMA",			VK_OEM_COMMA	},
				{ "MINUS",			VK_OEM_MINUS	},
				{ "DASH",			VK_OEM_MINUS	},
				{ "HYPHEN",			VK_OEM_MINUS	},
				{ "UNDERSCORE",		VK_OEM_MINUS	},
				{ "PERIOD",			VK_OEM_PERIOD	},
				{ "SLASH",			VK_OEM_2		},
				{ "FORWARDSLASH",	VK_OEM_2		},
				{ "DIVIDE",			VK_OEM_2		},
				{ "DIV",			VK_OEM_2		},
				{ "QUESTION",		VK_OEM_2		},
				{ "TILDE",			VK_OEM_3		},
				{ "BACKTICK",		VK_OEM_3		},
				{ "OPENB",			VK_OEM_4		},
				{ "OPENBRACKET",	VK_OEM_4		},
				{ "BACKSLASH",		VK_OEM_5		},
				{ "CLOSEB",			VK_OEM_6		},
				{ "CLOSEBRACKET",	VK_OEM_6		},
				{ "QUOTE",			VK_OEM_7		},
			};
			// VK_PAUSE not included because it has special use/meaning
			map.reserve(ARRAYSIZE(kEntries) + 10 /*num*/ + 10 /*numpad*/ + 12 /*F1-12*/);
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
			for(int i = 0; i <= 9; ++i)
			{
				map.setValue((std::string("NUM") + toString(i)).c_str(), VK_NUMPAD0 + i);
				map.setValue(std::string("NUMPAD") + toString(i), VK_NUMPAD0 + i);
			}
			for(int i = 1; i <= 12; ++i)
				map.setValue((std::string("F") + toString(i)).c_str(), VK_F1 - 1 + i);
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
			// These are in addition to kProfileButtonName above!
			struct { const char* str; EButton val; } kEntries[] = {
				{ "LL",				eBtn_LSLeft		},
				{ "LSL",			eBtn_LSLeft		},
				{ "LSTICKLEFT",		eBtn_LSLeft		},
				{ "LEFTSTICKLEFT",	eBtn_LSLeft		},
				{ "LR",				eBtn_LSRight	},
				{ "LSR",			eBtn_LSRight	},
				{ "LSTICKRIGHT",	eBtn_LSRight	},
				{ "LEFTSTICKRIGHT",	eBtn_LSRight	},
				{ "LU",				eBtn_LSUp		},
				{ "LSU",			eBtn_LSUp		},
				{ "LSTICKUP",		eBtn_LSUp		},
				{ "LEFTSTICKUP",	eBtn_LSUp		},
				{ "LD",				eBtn_LSDown		},
				{ "LSD",			eBtn_LSDown		},
				{ "LSTICKDOWN",		eBtn_LSDown		},
				{ "LEFTSTICKDOWN",	eBtn_LSDown		},
				{ "RL",				eBtn_RSLeft		},
				{ "RSL",			eBtn_RSLeft		},
				{ "RSTICKLEFT",		eBtn_RSLeft		},
				{ "RIGHTSTICKLEFT",	eBtn_RSLeft		},
				{ "RR",				eBtn_RSRight	},
				{ "RSR",			eBtn_RSRight	},
				{ "RSTICKRIGHT",	eBtn_RSRight	},
				{ "RIGHTSTICKRIGHT",eBtn_RSRight	},
				{ "RU",				eBtn_RSUp		},
				{ "RSU",			eBtn_RSUp		},
				{ "RSTICKUP",		eBtn_RSUp		},
				{ "RIGHTSTICKUP",	eBtn_RSUp		},
				{ "RD",				eBtn_RSDown		},
				{ "RSD",			eBtn_RSDown		},
				{ "RSTICKDOWN",		eBtn_RSDown		},
				{ "RIGHTSTICKDOWN",	eBtn_RSDown		},
				{ "DL",				eBtn_DLeft		},
				{ "DPL",			eBtn_DLeft		},
				{ "DPLEFT",			eBtn_DLeft		},
				{ "DPADLEFT",		eBtn_DLeft		},
				{ "DR",				eBtn_DRight		},
				{ "DPR",			eBtn_DRight		},
				{ "DPRIGHT",		eBtn_DRight		},
				{ "DPADRIGHT",		eBtn_DRight		},
				{ "DU",				eBtn_DUp		},
				{ "DPU",			eBtn_DUp		},
				{ "DPUP",			eBtn_DUp		},
				{ "DPADUP",			eBtn_DUp		},
				{ "DD",				eBtn_DDown		},
				{ "DPD",			eBtn_DDown		},
				{ "DPDOWN",			eBtn_DDown		},
				{ "DPADDOWN",		eBtn_DDown		},
				{ "FL",				eBtn_FLeft		},
				{ "FPL",			eBtn_FLeft		},
				{ "FPADLEFT",		eBtn_FLeft		},
				{ "SQUARE",			eBtn_FLeft		},
				{ "XBX",			eBtn_FLeft		},
				{ "FR",				eBtn_FRight		},
				{ "FPR",			eBtn_FRight		},
				{ "FPADRIGHT",		eBtn_FRight		},
				{ "CIRCLE",			eBtn_FRight		},
				{ "CIRC",			eBtn_FRight		},
				{ "XBB",			eBtn_FRight		},
				{ "FU",				eBtn_FUp		},
				{ "FPU",			eBtn_FUp		},
				{ "FPADUP",			eBtn_FUp		},
				{ "TRIANGLE",		eBtn_FUp		},
				{ "TRI",			eBtn_FUp		},
				{ "XBY",			eBtn_FUp		},
				{ "FD",				eBtn_FDown		},
				{ "FPD",			eBtn_FDown		},
				{ "FPADDOWN",		eBtn_FDown		},
				{ "XBA",			eBtn_FDown		},
				{ "PSX",			eBtn_FDown		},
			};
			size_t aSize = eBtn_Num + ARRAYSIZE(kEntries);
			map.reserve(eBtn_Num + ARRAYSIZE(kEntries));
			for(int i = 0; i < eBtn_Num; ++i)
				map.setValue(upper(kProfileButtonName[i]), EButton(i));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
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
		typedef StringToValueMap<EHUDType, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			struct { const char* str; EHUDType val; } kEntries[] = {
				{ "LIST",				eMenuStyle_List			},
				{ "BASIC",				eMenuStyle_List			},
				{ "DEFAULT",			eMenuStyle_List			},
				{ "NORMAL",				eMenuStyle_List			},
				{ "LISTWRAP",			eMenuStyle_ListWrap		},
				{ "BASICWRAP",			eMenuStyle_ListWrap		},
				{ "DEFAULTWRAP",		eMenuStyle_ListWrap		},
				{ "NORMALWRAP",			eMenuStyle_ListWrap		},
				{ "SLOT",				eMenuStyle_Slots		},
				{ "SLOTS",				eMenuStyle_Slots		},
				{ "PILLAR",				eMenuStyle_Slots		},
				{ "PILLARS",			eMenuStyle_Slots		},
				{ "COLUMN",				eMenuStyle_Slots		},
				{ "COLUMNS",			eMenuStyle_Slots		},
				{ "BAR",				eMenuStyle_Bar			},
				{ "BARS",				eMenuStyle_Bar			},
				{ "ROW",				eMenuStyle_Bar			},
				{ "ROWS",				eMenuStyle_Bar			},
				{ "HOTBAR",				eMenuStyle_Bar			},
				{ "BARWRAP",			eMenuStyle_BarWrap		},
				{ "BARSWRAP",			eMenuStyle_BarWrap		},
				{ "ROWWRAP",			eMenuStyle_BarWrap		},
				{ "ROWSWRAP",			eMenuStyle_BarWrap		},
				{ "HOTBARWRAP",			eMenuStyle_BarWrap		},
				{ "4DIR",				eMenuStyle_4Dir			},
				{ "COMPASS",			eMenuStyle_4Dir			},
				{ "CROSS",				eMenuStyle_4Dir			},
				{ "DPAD",				eMenuStyle_4Dir			},
				{ "DIR",				eMenuStyle_4Dir			},
				{ "DIRECTIONS",			eMenuStyle_4Dir			},
				{ "DIRECTIONAL",		eMenuStyle_4Dir			},
				{ "GRID",				eMenuStyle_Grid			},
				{ "GRIDWRAP",			eMenuStyle_GridWrap		},
				{ "RING",				eMenuStlye_Ring			},
				{ "RADIAL",				eMenuStyle_Radial		},
				{ "RECTANGLE",			eHUDItemType_Rect		},
				{ "RECT",				eHUDItemType_Rect		},
				{ "BLOCK",				eHUDItemType_Rect		},
				{ "SQUARE",				eHUDItemType_Rect		},
				{ "RNDRECT",			eHUDItemType_RndRect	},
				{ "ROUNDRECT",			eHUDItemType_RndRect	},
				{ "ROUNDEDRECT",		eHUDItemType_RndRect	},
				{ "ROUNDRECTANGLE",		eHUDItemType_RndRect	},
				{ "ROUNDEDRECTANGLE",	eHUDItemType_RndRect	},
				{ "BITMAP",				eHUDItemType_Bitmap		},
				{ "IMAGE",				eHUDItemType_Bitmap		},
				{ "CIRCLE",				eHUDItemType_Circle		},
				{ "DOT",				eHUDItemType_Circle		},
				{ "ELLIPSE",			eHUDItemType_Circle		},
				{ "ARROWL",				eHUDItemType_ArrowL		},
				{ "LARROW",				eHUDItemType_ArrowL		},
				{ "ARROWLEFT",			eHUDItemType_ArrowL		},
				{ "LEFTARROW",			eHUDItemType_ArrowL		},
				{ "ARROWR",				eHUDItemType_ArrowR		},
				{ "RARROW",				eHUDItemType_ArrowR		},
				{ "ARROWRIGHT",			eHUDItemType_ArrowR		},
				{ "RIGHTARROW",			eHUDItemType_ArrowR		},
				{ "ARROWU",				eHUDItemType_ArrowU		},
				{ "UARROW",				eHUDItemType_ArrowU		},
				{ "ARROWUP",			eHUDItemType_ArrowU		},
				{ "UPARROW",			eHUDItemType_ArrowU		},
				{ "ARROWD",				eHUDItemType_ArrowD		},
				{ "DARROW",				eHUDItemType_ArrowD		},
				{ "ARROWDOWN",			eHUDItemType_ArrowD		},
				{ "DOWNARROW",			eHUDItemType_ArrowD		},
				{ "GROUPTARGET",		eHUDType_GroupTarget	},
				{ "TARGETGROUP",		eHUDType_GroupTarget	},
				{ "SAVEDTARGET",		eHUDType_DefaultTarget	},
				{ "DEFAULTTARGET",		eHUDType_DefaultTarget	},
				{ "FAVORITETARGET",		eHUDType_DefaultTarget	},
				{ "TARGETSAVED",		eHUDType_DefaultTarget	},
				{ "TARGETDEFAULT",		eHUDType_DefaultTarget	},
				{ "TARGETFAVORITE",		eHUDType_DefaultTarget	},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EHUDType* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eHUDType_Num;
}


ECommandKeyWord commandWordToID(const std::string& theWord)
{
	struct WordToEnumMapper
	{
		typedef StringToValueMap<ECommandKeyWord, u8> WordToEnumMap;
		WordToEnumMap map;
		WordToEnumMapper()
		{
			struct { const char* str; ECommandKeyWord val; } kEntries[] = {
				{ "NOTHING",		eCmdWord_Nothing	},
				{ "BLANK",			eCmdWord_Nothing	},
				{ "EMPTY",			eCmdWord_Nothing	},
				{ "ADD",			eCmdWord_Add		},
				{ "REMOVE",			eCmdWord_Remove		},
				{ "HOLD",			eCmdWord_Hold		},
				{ "REPLACE",		eCmdWord_Replace	},
				{ "SWAP",			eCmdWord_Replace	},
				{ "TOGGLE",			eCmdWord_Toggle		},
				{ "LAYER",			eCmdWord_Layer		},
				{ "LAYERS",			eCmdWord_Layer		},
				{ "NONCHILD",		eCmdWord_NonChild	},
				{ "INDEPENDENT",	eCmdWord_NonChild	},
				{ "INDEPENDENTLY",	eCmdWord_NonChild	},
				{ "PARENT",			eCmdWord_Parent		},
				{ "OWNER",			eCmdWord_Parent		},
				{ "GRANDPARENT",	eCmdWord_Grandparent},
				{ "ALL",			eCmdWord_All		},
				{ "MOUSE",			eCmdWord_Mouse		},
				{ "WHEEL",			eCmdWord_MouseWheel	},
				{ "MOUSEWHEEL",		eCmdWord_MouseWheel	},
				{ "SMOOTH",			eCmdWord_Smooth		},
				{ "STEP",			eCmdWord_Stepped	},
				{ "STEPPED",		eCmdWord_Stepped	},
				{ "ONCE",			eCmdWord_Once		},
				{ "SINGLE",			eCmdWord_Once		},
				{ "MOVE",			eCmdWord_Move		},
				{ "TURN",			eCmdWord_Turn		},
				{ "MOVETURN",		eCmdWord_Turn		},
				{ "STRAFE",			eCmdWord_Strafe		},
				{ "MOVESTRAFE",		eCmdWord_Strafe		},
				{ "SELECT",			eCmdWord_Select		},
				{ "CHOOSE",			eCmdWord_Select		},
				{ "HOTSPOT",		eCmdWord_Hotspot	},
				{ "HOTSPOTS",		eCmdWord_Hotspot	},
				{ "RESET",			eCmdWord_Reset		},
				{ "EDIT",			eCmdWord_Edit		},
				{ "REASSIGN",		eCmdWord_Edit		},
				{ "REWRITE",		eCmdWord_Edit		},
				{ "MENU",			eCmdWord_Menu		},
				{ "CONFIRM",		eCmdWord_Confirm	},
				{ "ACTIVATE",		eCmdWord_Confirm	},
				{ "B",				eCmdWord_Back		},
				{ "BACK",			eCmdWord_Back		},
				{ "CANCEL",			eCmdWord_Back		},
				{ "CLOSE",			eCmdWord_Close		},
				{ "QUIT",			eCmdWord_Close		},
				{ "EXIT",			eCmdWord_Close		},
				{ "TARGET",			eCmdWord_Target		},
				{ "TARGETS",		eCmdWord_Target		},
				{ "TARGETED",		eCmdWord_Target		},
				{ "RETARGET",		eCmdWord_Target		},
				{ "GROUP",			eCmdWord_Group		},
				{ "L",				eCmdWord_Left		},
				{ "LEFT",			eCmdWord_Left		},
				{ "R",				eCmdWord_Right		},
				{ "RIGHT",			eCmdWord_Right		},
				{ "U",				eCmdWord_Up			},
				{ "UP",				eCmdWord_Up			},
				{ "F",				eCmdWord_Up			},
				{ "FORWARD",		eCmdWord_Up			},
				{ "PREV",			eCmdWord_Up			},
				{ "PREVIOUS",		eCmdWord_Up			},
				{ "TOP",			eCmdWord_Up			},
				{ "D",				eCmdWord_Down		},
				{ "DOWN",			eCmdWord_Down		},
				{ "NEXT",			eCmdWord_Down		},
				{ "BOTTOM",			eCmdWord_Down		},
				{ "PREVWRAP",		eCmdWord_PrevWrap	},
				{ "UWRAP",			eCmdWord_PrevWrap	},
				{ "UPWRAP",			eCmdWord_PrevWrap	},
				{ "NEXTWRAP",		eCmdWord_NextWrap	},
				{ "DWRAP",			eCmdWord_NextWrap	},
				{ "DOWNWRAP",		eCmdWord_NextWrap	},
				{ "WRAP",			eCmdWord_Wrap		},
				{ "WRAPPING",		eCmdWord_Wrap		},
				{ "PREVNOWRAP",		eCmdWord_PrevNoWrap	},
				{ "UNOWRAP",		eCmdWord_PrevNoWrap	},
				{ "UPNOWRAP",		eCmdWord_PrevNoWrap	},
				{ "NEXTNOWRAP",		eCmdWord_NextNoWrap	},
				{ "DNOWRAP",		eCmdWord_NextNoWrap	},
				{ "DOWNNOWRAP",		eCmdWord_NextNoWrap	},
				{ "NOWRAP",			eCmdWord_NoWrap		},
				{ "FAVORITE",		eCmdWord_Default	},
				{ "SAVED",			eCmdWord_Default	},
				{ "DEFAULT",		eCmdWord_Default	},
				{ "LOAD",			eCmdWord_Load		},
				{ "RECALL",			eCmdWord_Load		},
				{ "SAVE",			eCmdWord_Set		},
				{ "STORE",			eCmdWord_Set		},
				{ "SET",			eCmdWord_Set		},
				{ "LAST",			eCmdWord_Last		},
				{ "PET",			eCmdWord_Pet		},
				{ "CHANGE",			eCmdWord_Change		},
				{ "SWITCH",			eCmdWord_Change		},
				{ "PROFILE",		eCmdWord_Profile	},
				{ "APP",			eCmdWord_App		},
				{ "APPLICATION",	eCmdWord_App		},
				{ "PROGRAM",		eCmdWord_App		},
				{ "OVERLAY",		eCmdWord_App		},
				{ "MEMBER",			eCmdWord_Ignored	},
				{ "MEMBERS",		eCmdWord_Ignored	},
				{ "CHAR",			eCmdWord_Ignored	},
				{ "CHARACTER",		eCmdWord_Ignored	},
				{ "SELECTION",		eCmdWord_Ignored	},
				{ "CURRENT",		eCmdWord_Ignored	},
				{ "ACTIVE",			eCmdWord_Ignored	},
				{ "ITEM",			eCmdWord_Ignored	},
				{ "OPTION",			eCmdWord_Ignored	},
				{ "A",				eCmdWord_Filler		},
				{ "AND",			eCmdWord_Filler		},
				{ "OR",				eCmdWord_Filler		},
				{ "THE",			eCmdWord_Filler		},
				{ "IN",				eCmdWord_Filler		},
				{ "TO",				eCmdWord_Filler		},
				{ "ON",				eCmdWord_Filler		},
				{ "OUT",			eCmdWord_Filler		},
				{ "OF",				eCmdWord_Filler		},
				{ "AT",				eCmdWord_Filler		},
				{ "WITH",			eCmdWord_Filler		},
				{ "W",				eCmdWord_Filler		},
				{ "DO",				eCmdWord_Filler		},
				{ "THIS",			eCmdWord_Filler		},
				{ "THEN",			eCmdWord_Filler		},
				{ "IT",				eCmdWord_Filler		},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
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
