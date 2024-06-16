//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, typedefs (structs), and
	data lookup tables that are needed by multiple modules.
*/

enum {
kSwapWindowModeHotkeyID = 0x01,
kVKeyModKeyOnlyBase = 0x0F, // unassigned by MS
kVKeyShiftFlag = 0x0100, // from MS docs for VkKeyScan()
kVKeyCtrlFlag = 0x0200,
kVKeyAltFlag = 0x0400,
kVKeyWinFlag = 0x1000,
kVKeyMask = 0x00FF,
kAllLayers = 0xFFFF,
kInvalidItem = 0xFFFF,
};

enum ECommandType
{
	eCmdType_Empty, // Does nothing but also means 'not set to anything'
	eCmdType_DoNothing, // _Empty but overrides lower Layers' assignments
	eCmdType_Unassigned, // _Empty but overrides Include= Layer's assignment

	// First group are valid for InputDispatcher::sendKeyCommand()
	eCmdType_PressAndHoldKey,
	eCmdType_ReleaseKey,
	eCmdType_TapKey,
	eCmdType_VKeySequence,
	eCmdType_SlashCommand,
	eCmdType_SayString,

	// Valid for InputDispatcher::moveMouseTo()
	eCmdType_MoveMouseToHotspot, 
	eCmdType_MoveMouseToMenuItem,

	// These active "keybind arrays" which allow a sequence of different keys
	// to be pressed by a single buton that changes the key pressed each time.
	// These first 2 do not actually send any input...
	eCmdType_KeyBindArrayResetLast, // Sets "last" index to "default" index
	eCmdType_KeyBindArraySetDefault, // Sets "default" index to "last" index
	// These all update "last" to the index in the array that was pressed
	eCmdType_KeyBindArrayPrev, // Press previous key in array
	eCmdType_KeyBindArrayNext, // Press next key in array
	eCmdType_KeyBindArrayDefault, // Press key at saved "default" index
	eCmdType_KeyBindArrayLast, // Re-press last-pressed key in the array
	eCmdType_KeyBindArrayIndex, // Skip to pressing key at specified index
	eCmdType_KeyBindArrayHoldIndex, // PressAndHold version of above

	// These are just a mix of special-case one-off commands
	eCmdType_StartAutoRun,
	eCmdType_ChangeProfile,
	eCmdType_UpdateUIScale,
	eCmdType_QuitApp,

	// These trigger changes in Controls Layers (button configuration)
	eCmdType_AddControlsLayer,
	eCmdType_HoldControlsLayer,
	eCmdType_RemoveControlsLayer,
	eCmdType_ReplaceControlsLayer,
	eCmdType_ToggleControlsLayer,

	// These control Menu selections/state
	eCmdType_OpenSubMenu,
	eCmdType_ReplaceMenu,
	eCmdType_MenuReset,
	eCmdType_MenuConfirm,
	eCmdType_MenuConfirmAndClose,
	eCmdType_MenuBack, // close last sub-menu
	eCmdType_MenuBackOrClose, // close menu if no sub-menus open 
	eCmdType_MenuClose,
	eCmdType_MenuEdit,

	// These should have 'dir' set to an ECommandDir
	eCmdType_MenuSelect,
	eCmdType_MenuSelectAndClose,
	eCmdType_MenuEditDir,
	eCmdType_HotspotSelect,

	// These should be processed continuously, not just on digital press
	// (may respond to analog axis data), and also have 'dir' as an ECommandDir
	eCmdType_MoveTurn,
	eCmdType_MoveStrafe,
	eCmdType_MoveMouse,
	eCmdType_MouseWheel,

	eCmdType_Num,

	eCmdType_FirstValid = eCmdType_PressAndHoldKey,
	eCmdType_FirstMenuControl = eCmdType_OpenSubMenu,
	eCmdType_LastMenuControl = eCmdType_MenuEditDir,
	eCmdType_FirstContinuous = eCmdType_MoveTurn,
};

enum ECommandDir
{
	eCmdDir_L,
	eCmdDir_R,
	eCmdDir_U,
	eCmdDir_D,

	eCmdDir_Num,
	eCmdDir_None = eCmdDir_Num,

	eCmdDir_Left = eCmdDir_L,
	eCmdDir_Right = eCmdDir_R,
	eCmdDir_Up = eCmdDir_U,
	eCmdDir_Down = eCmdDir_D,

	eCmdDir_Forward = eCmdDir_Up,
	eCmdDir_Back = eCmdDir_Down,
	eCmdDir_Prev = eCmdDir_Up,
	eCmdDir_Next = eCmdDir_Down,
	eCmdDir_Top = eCmdDir_Up,
	eCmdDir_Bottom = eCmdDir_Down,
};

enum EMouseWheelMotion
{
	eMouseWheelMotion_Smooth,
	eMouseWheelMotion_Stepped,
	eMouseWheelMotion_Jump,
};

enum EButtonAction
{
	eBtnAct_Down,		// Action (key) held as long as button is held (if can)
	eBtnAct_Press,		// First pushed (assigned action is just 'tapped')
	eBtnAct_Hold,		// Held a short time (action tapped once)
	eBtnAct_Tap,		// Released before _S/LHold triggers (action tapped once)
	eBtnAct_Release,	// Released (any hold time, action tapped once)

	eBtnAct_With,		// MUST BE LAST! Same as _Press but ignores layer order

	eBtnAct_Num
};

enum EButton
{
	eBtn_None = 0,

	// Left analog stick (pushed past set digital deadzone)
	eBtn_LSLeft,
	eBtn_LSRight,
	eBtn_LSUp,
	eBtn_LSDown,

	// Right analog stick (pushed past set digital deadzone)
	eBtn_RSLeft,
	eBtn_RSRight,
	eBtn_RSUp,
	eBtn_RSDown,

	// D-pad
	eBtn_DLeft,
	eBtn_DRight,
	eBtn_DUp,
	eBtn_DDown,

	// Face buttons
	eBtn_FLeft,		// Left face button - PS=Sqr, XB=X, N=Y
	eBtn_FRight,	// Right face button - PS=Cir, XB=B, N=A
	eBtn_FUp,		// Top face button - PS=Tri, XB=Y, N=X
	eBtn_FDown,		// Bottom face button - PS=X, XB=A, N=B

	// Shoulder buttons
	eBtn_L1,
	eBtn_R1,
	eBtn_L2,
	eBtn_R2,

	// Other buttons
	eBtn_Select,	// Or Back or Share or whatever
	eBtn_Start,		// Or Menu or Options or whatever
	eBtn_L3,		// Pressing in on the left analog stick
	eBtn_R3,		// Pressing in on the right analog stick
	eBtn_Home,		// PS button, XB Guide button, etc
	eBtn_Extra,		// Touchpad on PS, Capture on NSwitch

	eBtn_Num,
	eBtn_FirstDInputBtn = eBtn_FLeft,
	eBtn_DInputBtnNum = eBtn_Num - eBtn_FirstDInputBtn,
};

enum EMouseMode
{
	eMouseMode_Default,		// Use whatever previous layers specified
	eMouseMode_Cursor,		// Normal mouse cursor (default if none specified)
	eMouseMode_Look,		// Mouse Look (holding right mouse button)
	eMouseMode_Hide,		// Hide cursor without using MouseLook
	eMouseMode_HideOrLook,	// Hide unless below layer is _Look, then _Look
	eMouseMode_PostJump,	// Cursor mode just after a jump
	eMouseMode_JumpClicked,	// Cursor mode just after jump-then-click event

	eMouseMode_Num
};

enum EHUDType
{
	eMenuStyle_List,
	eMenuStyle_Bar,
	eMenuStyle_Grid,
	eMenuStyle_Hotspots,
	eMenuStyle_Slots,
	eMenuStyle_4Dir,
	eMenuStlye_Ring,
	eMenuStyle_Radial,

	eHUDItemType_Rect,
	eHUDItemType_RndRect,
	eHUDItemType_Bitmap,
	eHUDItemType_Circle,
	eHUDItemType_ArrowL,
	eHUDItemType_ArrowR,
	eHUDItemType_ArrowU,
	eHUDItemType_ArrowD,

	eHUDType_Hotspot,
	eHUDType_KBArrayLast,
	eHUDType_KBArrayDefault,
	eHUDType_System, // Internal use only

	eHUDType_Num,

	eMenuStyle_Begin = 0,
	eMenuStyle_End = eHUDItemType_Rect,
	eHUDItemType_Begin = eHUDItemType_Rect,
	eHUDItemType_End = eHUDType_Hotspot,
};

enum ESpecialKey
{
	eSpecialKey_SwapWindowMode,
	eSpecialKey_AutoRun,
	eSpecialKey_MoveF,
	eSpecialKey_MoveB,
	eSpecialKey_TurnL,
	eSpecialKey_TurnR,
	eSpecialKey_StrafeL,
	eSpecialKey_StrafeR,

	eSpecialKey_Num,

	eSpecialKey_FirstMove = eSpecialKey_MoveF,
	eSpecialKey_LastMove = eSpecialKey_StrafeR,
	eSpecialKey_MoveNum =
		eSpecialKey_LastMove - eSpecialKey_FirstMove + 1,
};

enum ESpecialHotspot
{
	eSpecialHotspot_None = 0,

	eSpecialHotspot_MouseLookStart,
	eSpecialHotspot_MouseHidden,
	eSpecialHotspot_LastCursorPos,
	eSpecialHotspot_MenuItemPos,

	eSpecialHotspot_Num
};

enum ECommandKeyWord
{
	eCmdWord_Unknown,

	eCmdWord_Nothing,
	eCmdWord_Unassigned,
	eCmdWord_Add,
	eCmdWord_Remove,
	eCmdWord_Hold,
	eCmdWord_Replace,
	eCmdWord_With,
	eCmdWord_Toggle,
	eCmdWord_Layer,
	eCmdWord_Mouse,
	eCmdWord_Click,
	eCmdWord_MouseWheel,
	eCmdWord_Smooth,
	eCmdWord_Stepped,
	eCmdWord_Move,
	eCmdWord_Turn,
	eCmdWord_Strafe,
	eCmdWord_Select,
	eCmdWord_Hotspot,
	eCmdWord_Reset,
	eCmdWord_Edit,
	eCmdWord_Menu,
	eCmdWord_Confirm,
	eCmdWord_Back,
	eCmdWord_Close,
	eCmdWord_Wrap,
	eCmdWord_NoWrap,
	eCmdWord_Left,
	eCmdWord_LeftWrap,
	eCmdWord_LeftNoWrap,
	eCmdWord_Right,
	eCmdWord_RightWrap,
	eCmdWord_RightNoWrap,
	eCmdWord_Up,
	eCmdWord_UpWrap,
	eCmdWord_UpNoWrap,
	eCmdWord_Down,
	eCmdWord_DownWrap,
	eCmdWord_DownNoWrap,
	eCmdWord_Default,
	eCmdWord_Load,
	eCmdWord_Set,
	eCmdWord_Last,
	eCmdWord_Change,
	eCmdWord_Profile,
	eCmdWord_UIScale,
	eCmdWord_App,

	eCmdWord_Integer,
	eCmdWord_Ignored,
	eCmdWord_Filler,
	eCmdWord_Num,

	eCmdWord_Forward = eCmdWord_Up,
	eCmdWord_Prev = eCmdWord_Up,
	eCmdWord_PrevWrap = eCmdWord_UpWrap,
	eCmdWord_PrevNoWrap = eCmdWord_UpNoWrap,
	eCmdWord_Next = eCmdWord_Down,
	eCmdWord_NextWrap = eCmdWord_DownWrap,
	eCmdWord_NextNoWrap = eCmdWord_DownNoWrap,
	eCmdWord_Top = eCmdWord_Up,
	eCmdWord_Bottom = eCmdWord_Down,
};

enum EResult
{
	eResult_None,

	eResult_Ok,
	eResult_TaskCompleted = eResult_Ok,
	eResult_Accepted = eResult_Ok,

	eResult_Fail,
	eResult_Cancel,
	eResult_NotFound,
	eResult_InvalidParameter,
	eResult_Incomplete,
	eResult_NotAllowed,
	eResult_Malformed,
	eResult_Overflow,
	eResult_Empty,
	eResult_Declined,
	eResult_NotNeeded,
};

struct Command : public ConstructFromZeroInitializedMemory<Command>
{
	ECommandType type;
	union
	{
		struct
		{
			union
			{
				u16 dir;
				u16 vKey;
				u16 layerID;
				u16 subMenuID;
				u16 hotspotID;
				u16 keyStringIdx;
				u16 arrayIdx;
				u16 menuItemIdx;
			};
			union
			{
				u16 replacementLayer;
				u16 menuID;
				u16 keybindArrayID;
				u16 mouseWheelMotionType;
			};
			u8 count;
			bool wrap;
			bool withMouse;
			bool andClick;
		};
		const char* string;
		u64 compare;
	};

	bool operator==(const Command& rhs) const
	{ return type == rhs.type && compare == rhs.compare; }
};

struct Hotspot : public ConstructFromZeroInitializedMemory<Hotspot>
{
	struct Coord
	{
		u16 anchor; // normalized x/65536 percentage of area
		s16 offset; // fixed pixel offset from .anchor
		s16 scaled; // scaled pixel offset from .anchor + .offset
		bool operator==(const Coord& rhs) const
		{ return anchor == rhs.anchor &&
			offset == rhs.offset &&
			scaled == rhs.scaled; }
	} x, y;

	bool operator==(const Hotspot& rhs) const
	{ return x == rhs.x && y == rhs.y; }
};

// Generic button names used in Profile .ini files
extern const char* kProfileButtonName[];

// Conversions between constant values (enums) and strings
// Strings must already be in all upper-case!
extern u8 keyNameToVirtualKey(const std::string& theKeyName);
extern std::string virtualKeyToName(u8 theVKey);
extern EButton buttonNameToID(const std::string& theName);
extern EMouseMode mouseModeNameToID(const std::string& theName);
extern EHUDType hudTypeNameToID(const std::string& theName);
extern ECommandKeyWord commandWordToID(const std::string& theWord);
