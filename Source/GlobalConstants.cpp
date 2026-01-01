//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "Common.h"

//------------------------------------------------------------------------------
// Data Tables
//------------------------------------------------------------------------------

DBG_CTASSERT(eCmdType_Num <= 256);

const char* kProfileButtonName[] =
{
	"Auto",		// eBtn_None
	"LSLeft",	// eBtn_LSLeft
	"LSRight",	// eBtn_LSRight
	"LSUp",		// eBtn_LSUp
	"LSDown",	// eBtn_LSDown
	"LStick",	// eBtn_LSAny
	"RSLeft",	// eBtn_RSLeft
	"RSRight",	// eBtn_RSRight
	"RSUp",		// eBtn_RSUp
	"RSDown",	// eBtn_RSDown
	"RStick",	// eBtn_RSAny
	"DLeft",	// eBtn_DLeft
	"DRight",	// eBtn_DRight
	"DUp",		// eBtn_DUp
	"DDown",	// eBtn_DDown
	"DPad",		// eBtn_DPadAny
	"FPad",		// eBtn_FPadAny
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


int keyNameToVirtualKey(const std::string& theKeyName)
{
	if( theKeyName.size() == 1 && !(theKeyName[0] & 0x80) )
	{
		int aVKey = VkKeyScan(WCHAR(::tolower(theKeyName[0])));
		DBG_ASSERT(aVKey <= 0xFF);
		return aVKey;
	}

	struct NameToVKeyMapper
	{
		typedef StringToValueMap<u8, u8> NameToVKeyMap;
		NameToVKeyMap map;
		NameToVKeyMapper()
		{
			struct { const char* str; u8 val; } kEntries[] = {
				{ "Click",			VK_LBUTTON			},
				{ "LClick",			VK_LBUTTON			},
				{ "LeftClick",		VK_LBUTTON			},
				{ "LMB",			VK_LBUTTON			},
				{ "RClick",			VK_RBUTTON			},
				{ "RightClick",		VK_RBUTTON			},
				{ "RMB",			VK_RBUTTON			},
				{ "Release",		kVKeyForceRelease	},
				{ "Rel",			kVKeyForceRelease	},
				{ "Cancel",			kVKeyForceRelease	},
				{ "Stop",			kVKeyForceRelease	},
				{ "Lift",			kVKeyForceRelease	},
				{ "MClick",			VK_MBUTTON			},
				{ "MiddleClick",	VK_MBUTTON			},
				{ "MMB",			VK_MBUTTON			},
				{ "Back",			VK_BACK				},
				{ "Backspace",		VK_BACK				},
				{ "BS",				VK_BACK				},
				{ "Tab",			VK_TAB				},
				{ "Return",			VK_RETURN			},
				{ "Enter",			VK_RETURN			},
				{ "Sh",				VK_SHIFT			},
				{ "Shft",			VK_SHIFT			},
				{ "Shift",			VK_SHIFT			},
				{ "Ctrl",			VK_CONTROL			},
				{ "Control",		VK_CONTROL			},
				{ "Alt",			VK_MENU				},
				{ "Menu",			VK_MENU				},
				{ "Esc",			VK_ESCAPE			},
				{ "Escape",			VK_ESCAPE			},
				{ "Space",			VK_SPACE			},
				{ "Spacebar",		VK_SPACE			},
				{ "PgUp",			VK_PRIOR			},
				{ "PageUp",			VK_PRIOR			},
				{ "PageDown",		VK_NEXT				},
				{ "PgDown",			VK_NEXT				},
				{ "PgDwn",			VK_NEXT				},
				{ "End",			VK_END				},
				{ "Home",			VK_HOME				},
				{ "Left",			VK_LEFT				},
				{ "LeftArrow",		VK_LEFT				},
				{ "Up",				VK_UP				},
				{ "UpArrow",		VK_UP				},
				{ "Right",			VK_RIGHT			},
				{ "RightArrow",		VK_RIGHT			},
				{ "Down",			VK_DOWN				},
				{ "DownArrow",		VK_DOWN				},
				{ "Jump",			kVKeyMouseJump		},
				{ "Point",			kVKeyMouseJump		},
				{ "Cursor",			kVKeyMouseJump		},
				{ "Ins",			VK_INSERT			},
				{ "Insert",			VK_INSERT			},
				{ "Del",			VK_DELETE			},
				{ "Delete",			VK_DELETE			},
				{ "Win",			VK_LWIN				},
				{ "Windows",		VK_LWIN				},
				{ "LWin",			VK_LWIN				},
				{ "NumMult",		VK_MULTIPLY			},
				{ "NumMultiply",	VK_MULTIPLY			},
				{ "NumAdd",			VK_ADD				},
				{ "NumPlus",		VK_ADD				},
				{ "NumSub",			VK_SUBTRACT			},
				{ "NumSubtract",	VK_SUBTRACT			},
				{ "NumMinus",		VK_SUBTRACT			},
				{ "NumPeriod",		VK_DECIMAL			},
				{ "NumDecimal",		VK_DECIMAL			},
				{ "NumDiv",			VK_DIVIDE			},
				{ "NumDivide",		VK_DIVIDE			},
				{ "NumSlash",		VK_DIVIDE			},
				{ "NumpadMult",		VK_MULTIPLY			},
				{ "NumpadMultiply",	VK_MULTIPLY			},
				{ "NumpadAdd",		VK_ADD				},
				{ "NumpadPlus",		VK_ADD				},
				{ "NumpadSub",		VK_SUBTRACT			},
				{ "NumpadSubtract",	VK_SUBTRACT			},
				{ "NumpadMinus",	VK_SUBTRACT			},
				{ "NumpadPeriod",	VK_DECIMAL			},
				{ "NumpadDecimal",	VK_DECIMAL			},
				{ "NumpadDiv",		VK_DIVIDE			},
				{ "NumpadDivide",	VK_DIVIDE			},
				{ "NumpadSlash",	VK_DIVIDE			},
				{ "NumLock",		VK_NUMLOCK			},
				{ "Colon",			VK_OEM_1			},
				{ "Semicolon",		VK_OEM_1			},
				{ "Plus",			VK_OEM_PLUS			},
				{ "Equal",			VK_OEM_PLUS			},
				{ "Equals",			VK_OEM_PLUS			},
				{ "Comma",			VK_OEM_COMMA		},
				{ "Minus",			VK_OEM_MINUS		},
				{ "Dash",			VK_OEM_MINUS		},
				{ "Hyphen",			VK_OEM_MINUS		},
				{ "Underscore",		VK_OEM_MINUS		},
				{ "Period",			VK_OEM_PERIOD		},
				{ "Slash",			VK_OEM_2			},
				{ "ForwardSlash",	VK_OEM_2			},
				{ "Divide",			VK_OEM_2			},
				{ "Div",			VK_OEM_2			},
				{ "Question",		VK_OEM_2			},
				{ "Tilde",			VK_OEM_3			},
				{ "Backtick",		VK_OEM_3			},
				{ "Openb",			VK_OEM_4			},
				{ "OpenBracket",	VK_OEM_4			},
				{ "Backslash",		VK_OEM_5			},
				{ "CloseB",			VK_OEM_6			},
				{ "CloseBracket",	VK_OEM_6			},
				{ "Quote",			VK_OEM_7			},
			};
			// VK_PAUSE not included because it has special use/meaning
			map.reserve(ARRAYSIZE(kEntries)
				+ 10 /*num*/ + 10 /*numpad*/ + 12 /*F1-12*/);
			for(int i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
			for(int i = 0; i <= 9; ++i)
			{
				map.setValue((std::string("NUM") + toString(i)).c_str(),
					dropTo<u8>(VK_NUMPAD0 + i));
				map.setValue(std::string("NUMPAD") + toString(i),
					dropTo<u8>(VK_NUMPAD0 + i));
			}
			for(int i = 1; i <= 12; ++i)
			{
				map.setValue((std::string("F") + toString(i)).c_str(),
					dropTo<u8>(VK_F1 - 1 + i));
			}
		}
	};
	static NameToVKeyMapper sKeyMapper;

	u8* result = sKeyMapper.map.find(theKeyName);
	return result ? *result : 0;
}


std::string virtualKeyToName(int theVKey)
{
	DBG_ASSERT(u32(theVKey) <= 0xFF);
	switch(theVKey)
	{
	case VK_LBUTTON:	return "Left Mouse Button";
	case VK_MBUTTON:	return "Middle Mouse Button";
	case VK_RBUTTON:	return "Right Mouse Button";
	case VK_LEFT:		return "Left";
	case VK_RIGHT:		return "Right";
	case VK_UP:			return "Up";
	case VK_DOWN:		return "Down";
	case VK_PRIOR:		return "PageUp";
	case VK_NEXT:		return "PageDown";
	case VK_END:		return "End";
	case VK_HOME:		return "Home";
	case VK_INSERT:		return "Insert";
	case VK_DELETE:		return "Del";
	case VK_LWIN:		return "Win";
	case VK_DIVIDE:		return "Num Div";
	case VK_NUMLOCK:	return "NumLock";
	case VK_OEM_COMMA:	return "Comma";
	case VK_OEM_PERIOD:	return "Period";
	case kVKeyModKeyOnlyBase : return "Nothing";
	}
	LONG aScanCode = MapVirtualKey(theVKey, 0) << 16;
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
			struct { const char* str; EButton val; } kEntries[] = {
			// !!! These are in addition to kProfileButtonName above!!!
				{ "LSL",			eBtn_LSLeft		},
				{ "LStickLeft",		eBtn_LSLeft		},
				{ "LeftStickLeft",	eBtn_LSLeft		},
				{ "LR",				eBtn_LSRight	},
				{ "LSR",			eBtn_LSRight	},
				{ "LStickRight",	eBtn_LSRight	},
				{ "LeftStickRight",	eBtn_LSRight	},
				{ "LSU",			eBtn_LSUp		},
				{ "LStickUp",		eBtn_LSUp		},
				{ "LeftStickUp",	eBtn_LSUp		},
				{ "LSD",			eBtn_LSDown		},
				{ "LStickDown",		eBtn_LSDown		},
				{ "LeftStickDown",	eBtn_LSDown		},
				{ "LS",				eBtn_LSAny		},
				{ "LeftStick",		eBtn_LSAny		},
				{ "RSL",			eBtn_RSLeft		},
				{ "RStickLeft",		eBtn_RSLeft		},
				{ "RightStickLeft",	eBtn_RSLeft		},
				{ "RSR",			eBtn_RSRight	},
				{ "RStickRight",	eBtn_RSRight	},
				{ "RightStickRight",eBtn_RSRight	},
				{ "RSU",			eBtn_RSUp		},
				{ "RStickUp",		eBtn_RSUp		},
				{ "RightStickUp",	eBtn_RSUp		},
				{ "RSD",			eBtn_RSDown		},
				{ "RStickDown",		eBtn_RSDown		},
				{ "RightStickDown",	eBtn_RSDown		},
				{ "RS",				eBtn_RSAny		},
				{ "RightStick",		eBtn_RSAny		},
				{ "DPL",			eBtn_DLeft		},
				{ "DPLeft",			eBtn_DLeft		},
				{ "DPadLeft",		eBtn_DLeft		},
				{ "DPR",			eBtn_DRight		},
				{ "DPRight",		eBtn_DRight		},
				{ "DPadRight",		eBtn_DRight		},
				{ "DPU",			eBtn_DUp		},
				{ "DPUp",			eBtn_DUp		},
				{ "DPadUp",			eBtn_DUp		},
				{ "DPD",			eBtn_DDown		},
				{ "DPDown",			eBtn_DDown		},
				{ "DPadDown",		eBtn_DDown		},
				{ "FPL",			eBtn_FLeft		},
				{ "FPadLeft",		eBtn_FLeft		},
				{ "Square",			eBtn_FLeft		},
				{ "XB-X",			eBtn_FLeft		},
				{ "PS-S",			eBtn_FLeft		},
				{ "FPR",			eBtn_FRight		},
				{ "FPadRight",		eBtn_FRight		},
				{ "Circle",			eBtn_FRight		},
				{ "Circ",			eBtn_FRight		},
				{ "XB-B",			eBtn_FRight		},
				{ "PS-C",			eBtn_FRight		},
				{ "FPU",			eBtn_FUp		},
				{ "FPadUp",			eBtn_FUp		},
				{ "Triangle",		eBtn_FUp		},
				{ "Tri",			eBtn_FUp		},
				{ "XB-Y",			eBtn_FUp		},
				{ "PS-T",			eBtn_FUp		},
				{ "FPD",			eBtn_FDown		},
				{ "FPadDown",		eBtn_FDown		},
				{ "XB-A",			eBtn_FDown		},
				{ "PS-X",			eBtn_FDown		},
				{ "Back",			eBtn_Select		},
				{ "View",			eBtn_Select		},
				{ "Share",			eBtn_Select		},
				{ "Create",			eBtn_Select		},
				{ "Options",		eBtn_Start		},
				{ "Menu",			eBtn_Start		},
			};
			map.reserve(eBtn_Num + ARRAYSIZE(kEntries));
			for(int i = 0; i < eBtn_Num; ++i)
				map.setValue(kProfileButtonName[i], EButton(i));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EButton* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eBtn_Num;
}


EMouseMode mouseModeNameToID(const std::string& theName)
{
	struct NameToEnumMapper
	{
		typedef StringToValueMap<EMouseMode, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			struct { const char* str; EMouseMode val; } kEntries[] = {
				{ "",						eMouseMode_Default		},
				{ "Default",				eMouseMode_Default		},
				{ "Cursor",					eMouseMode_Cursor		},
				{ "Show",					eMouseMode_Cursor		},
				{ "Look",					eMouseMode_LookTurn		},
				{ "Camera",					eMouseMode_LookTurn		},
				{ "MouseLook",				eMouseMode_LookTurn		},
				{ "MouseLookR",				eMouseMode_LookTurn		},
				{ "RightLook",				eMouseMode_LookTurn		},
				{ "HoldRMB",				eMouseMode_LookTurn		},
				{ "LookTurn",				eMouseMode_LookTurn		},
				{ "LookAndTurn",			eMouseMode_LookTurn		},
				{ "LookR",					eMouseMode_LookTurn		},
				{ "FreeLook",				eMouseMode_LookOnly		},
				{ "LookOnly",				eMouseMode_LookOnly		},
				{ "MouseLookL",				eMouseMode_LookOnly		},
				{ "MouseLookOnly",			eMouseMode_LookOnly		},
				{ "LookL",					eMouseMode_LookOnly		},
				{ "HoldLMB",				eMouseMode_LookOnly		},
				{ "CameraOnly",				eMouseMode_LookOnly		},
				{ "AutoLook",				eMouseMode_LookAuto		},
				{ "AutoCamera",				eMouseMode_LookAuto		},
				{ "SmartLook",				eMouseMode_LookAuto		},
				{ "SmartCamera",			eMouseMode_LookAuto		},
				{ "LookAuto",				eMouseMode_LookAuto		},
				{ "CameraAuto",				eMouseMode_LookAuto		},
				{ "Hide",					eMouseMode_Hide			},
				{ "Hidden",					eMouseMode_Hide			},
				{ "HideOrLook",				eMouseMode_HideOrLook	},
				{ "HideOrCamera",			eMouseMode_HideOrLook	},
				{ "HideOrMouseLook",		eMouseMode_HideOrLook	},
				{ "HiddenOrLook",			eMouseMode_HideOrLook	},
				{ "HiddenOrCamera",			eMouseMode_HideOrLook	},
				{ "HiddenOrMouseLook",		eMouseMode_HideOrLook	},
				{ "LookOrHide",				eMouseMode_HideOrLook	},
				{ "CameraOrHide",			eMouseMode_HideOrLook	},
				{ "MouseLookOrHide",		eMouseMode_HideOrLook	},
				{ "LookOrHidden",			eMouseMode_HideOrLook	},
				{ "CameraOrHidden",			eMouseMode_HideOrLook	},
				{ "MouseLookOrHidden",		eMouseMode_HideOrLook	},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EMouseMode* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eMouseMode_Num;
}


EMenuStyle menuStyleNameToID(const std::string& theName)
{
	struct NameToEnumMapper
	{
		typedef StringToValueMap<EMenuStyle, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			struct { const char* str; EMenuStyle val; } kEntries[] = {
				{ "List",					eMenuStyle_List				},
				{ "Basic",					eMenuStyle_List				},
				{ "Default",				eMenuStyle_List				},
				{ "Normal",					eMenuStyle_List				},
				{ "Bar",					eMenuStyle_Bar				},
				{ "Bars",					eMenuStyle_Bar				},
				{ "Row",					eMenuStyle_Bar				},
				{ "Rows",					eMenuStyle_Bar				},
				{ "Hotbar",					eMenuStyle_Bar				},
				{ "Grid",					eMenuStyle_Grid				},
				{ "Rows",					eMenuStyle_Grid				},
				{ "GridRows",				eMenuStyle_Grid				},
				{ "HorizGrid",				eMenuStyle_Grid				},
				{ "HorizontalGrid",			eMenuStyle_Grid				},
				{ "GridHoriz",				eMenuStyle_Grid				},
				{ "GridHorizantal",			eMenuStyle_Grid				},
				{ "Column",					eMenuStyle_Columns			},
				{ "Columns",				eMenuStyle_Columns			},
				{ "GridColumns",			eMenuStyle_Columns			},
				{ "VertGrid",				eMenuStyle_Columns			},
				{ "VerticalGrid",			eMenuStyle_Columns			},
				{ "GridVert",				eMenuStyle_Columns			},
				{ "GridVertical",			eMenuStyle_Columns			},
				{ "Hotspot",				eMenuStyle_Hotspots			},
				{ "Hotspots",				eMenuStyle_Hotspots			},
				{ "Highlight",				eMenuStyle_Highlight		},
				{ "Outline",				eMenuStyle_Highlight		},
				{ "BorderOnly",				eMenuStyle_Highlight		},
				{ "SelectHotspot",			eMenuStyle_Highlight		},
				{ "SingleHotspot",			eMenuStyle_Highlight		},
				{ "Slot",					eMenuStyle_Slots			},
				{ "Slots",					eMenuStyle_Slots			},
				{ "4-Dir",					eMenuStyle_4Dir				},
				{ "Compass",				eMenuStyle_4Dir				},
				{ "Cross",					eMenuStyle_4Dir				},
				{ "DPad",					eMenuStyle_4Dir				},
				{ "Dir",					eMenuStyle_4Dir				},
				{ "Directions",				eMenuStyle_4Dir				},
				{ "Directional",			eMenuStyle_4Dir				},
				{ "Ring",					eMenuStlye_Ring				},
				{ "Radial",					eMenuStyle_Radial			},
				{ "HUD",					eMenuStyle_HUD				},
				{ "Visual",					eMenuStyle_HUD				},
				{ "Graphic",				eMenuStyle_HUD				},
				{ "Label",					eMenuStyle_HUD				},
				{ "Text",					eMenuStyle_HUD				},
				{ "String",					eMenuStyle_HUD				},
				{ "KeyBindCycleLast",		eMenuStyle_KBCycleLast		},
				{ "KeyBindCycle",			eMenuStyle_KBCycleLast		},
				{ "KBCycleLast",			eMenuStyle_KBCycleLast		},
				{ "KBCycle",				eMenuStyle_KBCycleLast		},
				{ "KeyBindCycleDefault",	eMenuStyle_KBCycleDefault	},
				{ "KBCycleDefault",			eMenuStyle_KBCycleDefault	},
				{ "KeyBindCycleFavorite",	eMenuStyle_KBCycleDefault	},
				{ "KBCycleFavorite",		eMenuStyle_KBCycleDefault	},
				{ "KeyBindCycleSaved",		eMenuStyle_KBCycleDefault	},
				{ "KBCycleSaved",			eMenuStyle_KBCycleDefault	},
				{ "",						eMenuStyle_Num				},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EMenuStyle* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eMenuStyle_Num;
}


EMenuItemType menuItemTypeNameToID(const std::string& theName)
{
	struct NameToEnumMapper
	{
		typedef StringToValueMap<EMenuItemType, u8> NameToEnumMap;
		NameToEnumMap map;
		NameToEnumMapper()
		{
			struct { const char* str; EMenuItemType val; } kEntries[] = {
				{ "Rectangle",				eMenuItemType_Rect		},
				{ "Rect",					eMenuItemType_Rect		},
				{ "Block",					eMenuItemType_Rect		},
				{ "Square",					eMenuItemType_Rect		},
				{ "RndRect",				eMenuItemType_RndRect	},
				{ "RoundRect",				eMenuItemType_RndRect	},
				{ "RoundedRect",			eMenuItemType_RndRect	},
				{ "RoundRectangle",			eMenuItemType_RndRect	},
				{ "RoundedRectangle",		eMenuItemType_RndRect	},
				{ "Bitmap",					eMenuItemType_Bitmap	},
				{ "Image",					eMenuItemType_Bitmap	},
				{ "Circle",					eMenuItemType_Circle	},
				{ "Dot",					eMenuItemType_Circle	},
				{ "Ellipse",				eMenuItemType_Circle	},
				{ "ArrowL",					eMenuItemType_ArrowL	},
				{ "LArrow",					eMenuItemType_ArrowL	},
				{ "ArrowLeft",				eMenuItemType_ArrowL	},
				{ "LeftArrow",				eMenuItemType_ArrowL	},
				{ "ArrowR",					eMenuItemType_ArrowR	},
				{ "RArrow",					eMenuItemType_ArrowR	},
				{ "ArrowRight",				eMenuItemType_ArrowR	},
				{ "RightArrow",				eMenuItemType_ArrowR	},
				{ "ArrowU",					eMenuItemType_ArrowU	},
				{ "UArrow",					eMenuItemType_ArrowU	},
				{ "ArrowUp",				eMenuItemType_ArrowU	},
				{ "UpArrow",				eMenuItemType_ArrowU	},
				{ "ArrowD",					eMenuItemType_ArrowD	},
				{ "DArrow",					eMenuItemType_ArrowD	},
				{ "ArrowDown",				eMenuItemType_ArrowD	},
				{ "DownArrow",				eMenuItemType_ArrowD	},
				{ "Text",					eMenuItemType_Label		},
				{ "String",					eMenuItemType_Label		},
				{ "Label",					eMenuItemType_Label		},
				{ "Icon",					eMenuItemType_Label		},
			};
			map.reserve(ARRAYSIZE(kEntries));
			for(size_t i = 0; i < ARRAYSIZE(kEntries); ++i)
				map.setValue(kEntries[i].str, kEntries[i].val);
		}
	};
	static NameToEnumMapper sNameToEnumMapper;

	EMenuItemType* result = sNameToEnumMapper.map.find(theName);
	return result ? *result : eMenuItemType_Num;
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
				{ "Nothing",		eCmdWord_Nothing	},
				{ "DoNothing",		eCmdWord_Nothing	},
				{ "Reserved",		eCmdWord_Nothing	},
				{ "Signal",			eCmdWord_Nothing	},
				{ "SignalOnly",		eCmdWord_Nothing	},
				{ "OnlySignal",		eCmdWord_Nothing	},
				{ "Unused",			eCmdWord_Nothing	},
				{ "Unassigned",		eCmdWord_Nothing	},
				{ "None",			eCmdWord_Nothing	},
				{ "TBD",			eCmdWord_Nothing	},
				{ "",				eCmdWord_Skip		},
				{ "\"\"",			eCmdWord_Skip		},
				{ "Skip",			eCmdWord_Skip		},
				{ "Empty",			eCmdWord_Skip		},
				{ "Null",			eCmdWord_Skip		},
				{ "Blank",			eCmdWord_Skip		},
				{ "Ignore",			eCmdWord_Skip		},
				{ "Add",			eCmdWord_Add		},
				{ "Remove",			eCmdWord_Remove		},
				{ "Hold",			eCmdWord_Hold		},
				{ "Replace",		eCmdWord_Replace	},
				{ "Swap",			eCmdWord_Replace	},
				{ "with",			eCmdWord_With		},
				{ "to",				eCmdWord_To			},
				{ "Force",			eCmdWord_Force		},
				{ "Always",			eCmdWord_Force		},
				{ "Override",		eCmdWord_Force		},
				{ "Toggle",			eCmdWord_Toggle		},
				{ "Layer",			eCmdWord_Layer		},
				{ "Layers",			eCmdWord_Layer		},
				{ "Defer",			eCmdWord_Defer		},
				{ "Yield",			eCmdWord_Defer		},
				{ "Redirect",		eCmdWord_Defer		},
				{ "Passthrough",	eCmdWord_Defer		},
				{ "Pass",			eCmdWord_Defer		},
				{ "Fallthrough",	eCmdWord_Defer		},
				{ "Delegate",		eCmdWord_Defer		},
				{ "Lower",			eCmdWord_Lower		},
				{ "Base",			eCmdWord_Lower		},
				{ "Earlier",		eCmdWord_Lower		},
				{ "Other",			eCmdWord_Lower		},
				{ "Others",			eCmdWord_Lower		},
				{ "Mouse",			eCmdWord_Mouse		},
				{ "MoveMouse",		eCmdWord_Mouse		},
				{ "Cursor",			eCmdWord_Mouse		},
				{ "Click",			eCmdWord_Click		},
				{ "LClick",			eCmdWord_Click		},
				{ "LeftClick",		eCmdWord_Click		},
				{ "LMB",			eCmdWord_Click		},
				{ "Wheel",			eCmdWord_MouseWheel	},
				{ "MouseWheel",		eCmdWord_MouseWheel	},
				{ "Smooth",			eCmdWord_Smooth		},
				{ "Step",			eCmdWord_Stepped	},
				{ "Stepped",		eCmdWord_Stepped	},
				{ "Lock",			eCmdWord_Lock		},
				{ "Move",			eCmdWord_Move		},
				{ "Movement",		eCmdWord_Move		},
				{ "Motion",			eCmdWord_Move		},
				{ "Slide",			eCmdWord_Move		},
				{ "Point",			eCmdWord_Move		}, // for menu Mouse=
				{ "Turn",			eCmdWord_Turn		},
				{ "MoveTurn",		eCmdWord_Turn		},
				{ "Strafe",			eCmdWord_Strafe		},
				{ "MoveStrafe",		eCmdWord_Strafe		},
				{ "Look",			eCmdWord_Look		},
				{ "MoveLook",		eCmdWord_Look		},
				{ "MoveAndLook",	eCmdWord_Look		},
				{ "LookMove",		eCmdWord_Look		},
				{ "Select",			eCmdWord_Select		},
				{ "Choose",			eCmdWord_Select		},
				{ "Hotspot",		eCmdWord_Hotspot	},
				{ "Hotspots",		eCmdWord_Hotspot	},
				{ "Reset",			eCmdWord_Reset		},
				{ "Edit",			eCmdWord_Edit		},
				{ "Reassign",		eCmdWord_Edit		},
				{ "Rewrite",		eCmdWord_Edit		},
				{ "Menu",			eCmdWord_Menu		},
				{ "Confirm",		eCmdWord_Confirm	},
				{ "Activate",		eCmdWord_Confirm	},
				{ "B",				eCmdWord_Back		},
				{ "Back",			eCmdWord_Back		},
				{ "Cancel",			eCmdWord_Back		},
				{ "Close",			eCmdWord_Close		},
				{ "Quit",			eCmdWord_Close		},
				{ "Exit",			eCmdWord_Close		},
				{ "CloseMenu",		eCmdWord_Close		},
				{ "Wrap",			eCmdWord_Wrap		},
				{ "Wrapping",		eCmdWord_Wrap		},
				{ "NoWrap",			eCmdWord_NoWrap		},
				{ "NoWrapping",		eCmdWord_NoWrap		},
				{ "L",				eCmdWord_Left		},
				{ "Left",			eCmdWord_Left		},
				{ "LWrap",			eCmdWord_LeftWrap	},
				{ "LeftWrap",		eCmdWord_LeftWrap	},
				{ "LNoWrap",		eCmdWord_LeftNoWrap	},
				{ "LeftNoWrap",		eCmdWord_LeftNoWrap	},
				{ "R",				eCmdWord_Right		},
				{ "Right",			eCmdWord_Right		},
				{ "RWrap",			eCmdWord_RightWrap	},
				{ "RightWrap",		eCmdWord_RightWrap	},
				{ "RightNoWrap",	eCmdWord_RightNoWrap},
				{ "RightNoWrap",	eCmdWord_RightNoWrap},
				{ "U",				eCmdWord_Up			},
				{ "Up",				eCmdWord_Up			},
				{ "F",				eCmdWord_Up			},
				{ "Forward",		eCmdWord_Up			},
				{ "Prev",			eCmdWord_Up			},
				{ "Previous",		eCmdWord_Up			},
				{ "Top",			eCmdWord_Up			},
				{ "UWrap",			eCmdWord_UpWrap		},
				{ "UpWrap",			eCmdWord_UpWrap		},
				{ "PrevWrap",		eCmdWord_UpWrap		},
				{ "UNoWrap",		eCmdWord_UpNoWrap	},
				{ "UpNoWrap",		eCmdWord_UpNoWrap	},
				{ "PrevNoWrap",		eCmdWord_UpNoWrap	},
				{ "D",				eCmdWord_Down		},
				{ "Down",			eCmdWord_Down		},
				{ "Next",			eCmdWord_Down		},
				{ "Bottom",			eCmdWord_Down		},
				{ "DWrap",			eCmdWord_DownWrap	},
				{ "DownWrap",		eCmdWord_DownWrap	},
				{ "NextWrap",		eCmdWord_DownWrap	},
				{ "DNoWrap",		eCmdWord_DownNoWrap	},
				{ "DownNoWrap",		eCmdWord_DownNoWrap	},
				{ "NextNoWrap",		eCmdWord_DownNoWrap	},
				{ "Favorite",		eCmdWord_Default	},
				{ "Saved",			eCmdWord_Default	},
				{ "Default",		eCmdWord_Default	},
				{ "Save",			eCmdWord_Set		},
				{ "Store",			eCmdWord_Set		},
				{ "Set",			eCmdWord_Set		},
				{ "Var",			eCmdWord_Variable	},
				{ "Variable",		eCmdWord_Variable	},
				{ "Last",			eCmdWord_Last		},
				{ "Repeat",			eCmdWord_Repeat		},
				{ "Repress",		eCmdWord_Repeat		},
				{ "Change",			eCmdWord_Change		},
				{ "Switch",			eCmdWord_Change		},
				{ "Update",			eCmdWord_Change		},
				{ "Reload",			eCmdWord_Change		},
				{ "Layout",			eCmdWord_Layout		},
				{ "Profile",		eCmdWord_Profile	},
				{ "UI",				eCmdWord_Layout		},
				{ "UILayout",		eCmdWord_Layout		},
				{ "Config",			eCmdWord_Config		},
				{ "Configuration",	eCmdWord_Config		},
				{ "JSON",			eCmdWord_Config		},
				{ "INI",			eCmdWord_Config		},
				{ "File",			eCmdWord_File		},
				{ "App",			eCmdWord_App		},
				{ "Application",	eCmdWord_App		},
				{ "Program",		eCmdWord_App		},
				{ "Overlay",		eCmdWord_App		},
				{ "Temp",			eCmdWord_Temp		},
				{ "Temporary",		eCmdWord_Temp		},
				{ "Temporarily",	eCmdWord_Temp		},
				{ "Runtime",		eCmdWord_Temp		},
				{ "Now",			eCmdWord_Temp		},
				{ "Only",			eCmdWord_Temp		},
				{ "Member",			eCmdWord_Ignored	}, // "group member"
				{ "Members",		eCmdWord_Ignored	},
				{ "Char",			eCmdWord_Ignored	},
				{ "Character",		eCmdWord_Ignored	},
				{ "Sync",			eCmdWord_Ignored	},
				{ "Target",			eCmdWord_Ignored	},
				{ "Direction",		eCmdWord_Ignored	},
				{ "Selection",		eCmdWord_Ignored	},
				{ "Selected",		eCmdWord_Ignored	},
				{ "Current",		eCmdWord_Ignored	},
				{ "Active",			eCmdWord_Ignored	},
				{ "Item",			eCmdWord_Ignored	},
				{ "Option",			eCmdWord_Ignored	},
				{ "Press",			eCmdWord_Ignored	},
				{ "Re-press",		eCmdWord_Ignored	},
				{ "Pressed",		eCmdWord_Ignored	},
				{ "Array",			eCmdWord_Ignored	},
				{ "Key",			eCmdWord_Ignored	},
				{ "a",				eCmdWord_Filler		},
				{ "and",			eCmdWord_Filler		},
				{ "by",				eCmdWord_Filler		},
				{ "or",				eCmdWord_Filler		},
				{ "the",			eCmdWord_Filler		},
				{ "in",				eCmdWord_Filler		},
				{ "out",			eCmdWord_Filler		},
				{ "of",				eCmdWord_Filler		},
				{ "w",/*i.e. "w/"*/	eCmdWord_Filler		},
				{ "use",			eCmdWord_Filler		},
				{ "using",			eCmdWord_Filler		},
				{ "do",				eCmdWord_Filler		},
				{ "this",			eCmdWord_Filler		},
				{ "then",			eCmdWord_Filler		},
				{ "it",				eCmdWord_Filler		},
				{ "on",				eCmdWord_Filler		},
				{ "at",				eCmdWord_Filler		},
				{ "when",			eCmdWord_Filler		},
				{ "go",				eCmdWord_Filler		},
				{ "during",			eCmdWord_Filler		},
				{ "through",		eCmdWord_Filler		},
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


bool isEffectivelyEmptyString(const std::string& theString)
{
	return commandWordToID(theString) == eCmdWord_Skip;
}


ECommandDir opposite8Dir(ECommandDir theDir)
{
	switch(theDir)
	{
	case eCmd8Dir_L:	return eCmd8Dir_R;
	case eCmd8Dir_R:	return eCmd8Dir_L;
	case eCmd8Dir_U:	return eCmd8Dir_D;
	case eCmd8Dir_D:	return eCmd8Dir_U;
	case eCmd8Dir_UL:	return eCmd8Dir_DR;
	case eCmd8Dir_UR:	return eCmd8Dir_DL;
	case eCmd8Dir_DL:	return eCmd8Dir_UR;
	case eCmd8Dir_DR:	return eCmd8Dir_UL;
	default:			return theDir;
	}
}


static u8 bitsFor8Dir(ECommandDir theDir)
{
	switch(theDir)
	{
	case eCmd8Dir_L: case eCmd8Dir_R: case eCmd8Dir_U: case eCmd8Dir_D:
		return u8(1U << theDir);
	case eCmd8Dir_UL: return bitsFor8Dir(eCmdDir_U) | bitsFor8Dir(eCmdDir_L);
	case eCmd8Dir_UR: return bitsFor8Dir(eCmdDir_U) | bitsFor8Dir(eCmdDir_R);
	case eCmd8Dir_DL: return bitsFor8Dir(eCmdDir_D) | bitsFor8Dir(eCmdDir_L);
	case eCmd8Dir_DR: return bitsFor8Dir(eCmdDir_D) | bitsFor8Dir(eCmdDir_R);
	}
	return 0;
}


ECommandDir combined8Dir(ECommandDir theDir1, ECommandDir theDir2)
{
	if( theDir1 == eCmd8Dir_None ) return theDir2;
	if( theDir2 == eCmd8Dir_None ) return theDir1;
	if( theDir1 == theDir2 ) return theDir1;
	const u32 kLBit = 1 << eCmd8Dir_L;
	const u32 kRBit = 1 << eCmd8Dir_R;
	const u32 kUBit = 1 << eCmd8Dir_U;
	const u32 kDBit = 1 << eCmd8Dir_D;
	const u32 aComboBits = bitsFor8Dir(theDir1) | bitsFor8Dir(theDir2);
	if( (aComboBits & (kLBit|kRBit)) == (kLBit|kRBit) ) return eCmd8Dir_None;
	if( (aComboBits & (kUBit|kDBit)) == (kUBit|kDBit) ) return eCmd8Dir_None;
	switch(aComboBits)
	{
	case kLBit: return eCmd8Dir_L;
	case kRBit: return eCmd8Dir_R;
	case kUBit: return eCmd8Dir_U;
	case kDBit: return eCmd8Dir_D;
	case (kUBit|kLBit): return eCmd8Dir_UL;
	case (kUBit|kRBit): return eCmd8Dir_UR;
	case (kDBit|kLBit): return eCmd8Dir_DL;
	case (kDBit|kRBit): return eCmd8Dir_DR;
	default: return eCmd8Dir_None;
	}
}
