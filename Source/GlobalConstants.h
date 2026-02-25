//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, and data lookup tables that
	are needed by multiple modules.
*/

enum {
kSwapWindowModeHotkeyID = 0x01,
kCancelToolbarHotkeyID = 0x02,
kVKeyForceRelease = VK_CANCEL,
kVKeyMouseJump = VK_SELECT,
kVKeyTriggerKeyBind = VK_EXECUTE,
kVKeyModKeyOnlyBase = 0x0F, // unassigned by MS
kVKeyShiftFlag = 0x0100, // from MS docs for VkKeyScan()
kVKeyCtrlFlag = 0x0200,
kVKeyAltFlag = 0x0400,
kVKeyWinFlag = 0x1000,
kVKeyMask = 0x00FF,
kAllLayers = 0xFFFF,
kInvalidID = 0xFFFF,
kSystemMenuID = 0,
kSystemOverlayID = 0,
kHotspotGuideMenuID = 1,
kHotspotGuideOverlayID = 1,
};

enum ECommandType
{
	eCmdType_Invalid, // Uninitialized / invalid / sentinel value
	eCmdType_Empty, // Empty string (defer to lower Layer's assignments)
	eCmdType_Defer, // _Empty but by specific request
	eCmdType_Unassigned, // _Empty but overrides lower Layers' assignments
	eCmdType_DoNothing, // _Unassigned but by specific request

	// First group are valid for InputDispatcher::sendCommand()
	eCmdType_PressAndHoldKey,
	eCmdType_ReleaseKey,
	eCmdType_TapKey,
	eCmdType_TriggerKeyBind,
	eCmdType_VKeySequence,
	eCmdType_ChatBoxString,
	eCmdType_MoveMouseToHotspot,
	eCmdType_MoveMouseToMenuItem,
	eCmdType_MoveMouseToOffset,
	eCmdType_MouseClickAtHotspot,

	// These active "keybind cycles" which allow a sequence of different keys
	// to be pressed by a single buton that changes the key pressed each time.
	// These first 2 do not actually send any input...
	eCmdType_KeyBindCycleReset, // Resets such that prev/next will be "default"
	eCmdType_KeyBindCycleSetDefault, // Sets "default" index to "last" index
	// These all update "last" to the index in the array that was pressed
	eCmdType_KeyBindCyclePrev, // Press previous key in array
	eCmdType_KeyBindCycleNext, // Press next key in array
	eCmdType_KeyBindCycleLast, // Re-press last-pressed key in the array

	// These are just a mix of special-case one-off commands
	eCmdType_SetVariable,
	eCmdType_StartAutoRun,
	eCmdType_ChangeProfile,
	eCmdType_EditLayout,
	eCmdType_ChangeTargetConfigSyncFile,
	eCmdType_QuitApp,

	// These trigger changes in Controls Layers (button configuration)
	eCmdType_AddControlsLayer,
	eCmdType_HoldControlsLayer,
	eCmdType_RemoveControlsLayer,
	eCmdType_ReplaceControlsLayer,
	eCmdType_ToggleControlsLayer,

	// These control Menu selections/state
	eCmdType_OpenSubMenu,
	eCmdType_OpenSideMenu,
	eCmdType_MenuReset,
	eCmdType_MenuConfirm,
	eCmdType_MenuConfirmAndClose,
	eCmdType_MenuBack, // close last sub-menu
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
	eCmdType_MoveLook,
	eCmdType_MoveMouse,
	eCmdType_MouseWheel,

	eCmdType_Num,

	eCmdType_FirstValid = eCmdType_PressAndHoldKey,
	eCmdType_FirstMenuControl = eCmdType_OpenSubMenu,
	eCmdType_LastMenuControl = eCmdType_MenuEditDir,
	eCmdType_FirstDirectional = eCmdType_MenuSelect,
	eCmdType_FirstContinuous = eCmdType_MoveTurn,
};

enum ECommandDir
{
	eCmdDir_L,
	eCmdDir_R,
	eCmdDir_U,
	eCmdDir_D,

	eCmdDir_Num,

	eCmd8Dir_L = eCmdDir_L,
	eCmd8Dir_R = eCmdDir_R,
	eCmd8Dir_U = eCmdDir_U,
	eCmd8Dir_D = eCmdDir_D,
	eCmd8Dir_UL,
	eCmd8Dir_UR,
	eCmd8Dir_DL,
	eCmd8Dir_DR,
	eCmd8Dir_Num,

	eCmd8Dir_None = eCmd8Dir_Num,
	eCmdDir_None = eCmd8Dir_None,

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
	eBtnAct_Tap,		// Released before _Hold triggers (action tapped once)
	eBtnAct_Release,	// Released (any hold time, action tapped once)

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
	eBtn_LSAny,

	// Right analog stick (pushed past set digital deadzone)
	eBtn_RSLeft,
	eBtn_RSRight,
	eBtn_RSUp,
	eBtn_RSDown,
	eBtn_RSAny,

	// D-pad
	eBtn_DLeft,
	eBtn_DRight,
	eBtn_DUp,
	eBtn_DDown,
	eBtn_DPadAny,

	// Face buttons
	eBtn_FPadAny,
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
	eMouseMode_LookTurn,	// Mouse Look (holding right mouse button)
	eMouseMode_LookOnly,	// Alternate Mouse Look (holding left mouse button)
	eMouseMode_LookAuto,	// LookOnly or LookTurn based on if moving or not
	eMouseMode_Hide,		// Hide cursor without using MouseLook

	// Below are not valid to send to InputDispatcher::setMouseMode()
	eMouseMode_HideOrLook,	// _Default, unless it is _Cursor, then _Hide
	eMouseMode_PostJump,	// Cursor mode just after a jump
	eMouseMode_JumpClicked,	// Cursor mode just after jump-then-click event
	eMouseMode_SwapToLook,	// Transitioning to LookOnly from aonther Look mode
	eMouseMode_SwapToTurn,	// Transitioning to LookTurn from another Look mode
	eMouseMode_LookReady,	// Cursor in place but haven't clicked to start yet

	eMouseMode_Num
};

enum EMenuMouseMode
{
	eMenuMouseMode_None,
	eMenuMouseMode_Move,
	eMenuMouseMode_Click,

	eMenuMouseMode_Num
};

enum EMenuStyle
{
	eMenuStyle_List,
	eMenuStyle_Bar,
	eMenuStyle_Grid,
	eMenuStyle_Columns,
	eMenuStyle_Slots,
	eMenuStyle_4Dir,
	eMenuStlye_Ring,
	eMenuStyle_Radial,

	eMenuStyle_Hotspots,
	eMenuStyle_Highlight,

	eMenuStyle_HUD,
	eMenuStyle_KBCycleLast,
	eMenuStyle_KBCycleDefault,

	eMenuStyle_HotspotGuide, // Internal use only
	eMenuStyle_System, // Internal use only

	eMenuStyle_Num,
};

enum EMenuItemType
{
	eMenuItemType_Rect,
	eMenuItemType_RndRect,
	eMenuItemType_Bitmap,
	eMenuItemType_Circle,
	eMenuItemType_ArrowL,
	eMenuItemType_ArrowR,
	eMenuItemType_ArrowU,
	eMenuItemType_ArrowD,
	eMenuItemType_Label,

	eMenuItemType_Num
};

enum ESpecialKey
{
	eSpecialKey_None,
	eSpecialKey_SwapWindowMode,
	eSpecialKey_PasteText,
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
	eSpecialHotspot_None,
	eSpecialHotspot_LastCursorPos,
	eSpecialHotspot_MouseLookStart,
	eSpecialHotspot_MouseHidden,

	eSpecialHotspot_Num,
};

enum EHotspotGuideMode
{
	eHotspotGuideMode_Disabled,
	eHotspotGuideMode_Redraw,
	eHotspotGuideMode_Redisplay,
	eHotspotGuideMode_Showing,
};

enum ECommandKeyWord
{
	eCmdWord_Unknown,

	eCmdWord_Nothing,
	eCmdWord_Skip,
	eCmdWord_Add,
	eCmdWord_Remove,
	eCmdWord_Hold,
	eCmdWord_Replace,
	eCmdWord_With,
	eCmdWord_To,
	eCmdWord_Force,
	eCmdWord_Toggle,
	eCmdWord_Layer,
	eCmdWord_Defer,
	eCmdWord_Lower,
	eCmdWord_Mouse,
	eCmdWord_Click,
	eCmdWord_MouseWheel,
	eCmdWord_Smooth,
	eCmdWord_Stepped,
	eCmdWord_Lock,
	eCmdWord_Move,
	eCmdWord_Turn,
	eCmdWord_Strafe,
	eCmdWord_Look,
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
	eCmdWord_Set,
	eCmdWord_Variable,
	eCmdWord_Last,
	eCmdWord_Repeat,
	eCmdWord_Change,
	eCmdWord_Profile,
	eCmdWord_Layout,
	eCmdWord_Config,
	eCmdWord_File,
	eCmdWord_App,
	eCmdWord_Temp,

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
	eResult_Yes = eResult_Ok,

	eResult_Fail,
	eResult_Cancel,
	eResult_No,
	eResult_NotFound,
	eResult_InvalidParameter,
	eResult_Incomplete,
	eResult_NotAllowed,
	eResult_Malformed,
	eResult_Overflow,
	eResult_Empty,
	eResult_Declined,
	eResult_NotNeeded,
	eResult_Retry,
};

// Generic button names used in Profile .ini files
extern const char* kProfileButtonName[];

// Conversions between constant values (enums) and strings
// Strings must already be in all upper-case!
int keyNameToVirtualKey(const std::string& theKeyName);
std::string virtualKeyToName(int theVKey);
EButton buttonNameToID(const std::string& theName);
EMouseMode mouseModeNameToID(const std::string& theName);
EMenuStyle menuStyleNameToID(const std::string& theName);
EMenuItemType menuItemTypeNameToID(const std::string& theName);
ECommandKeyWord commandWordToID(const std::string& theWord);
bool isEffectivelyEmptyString(const std::string& theString);

// Conversions between one constant value to another related one
ECommandDir oppositeDir(ECommandDir);
ECommandDir combined8Dir(ECommandDir, ECommandDir);
