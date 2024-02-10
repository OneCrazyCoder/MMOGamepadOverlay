//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, typedefs (structs), and
	data lookup tables that are needed by multiple modules.
*/

enum {
kFullScreenHotkeyID = 0x01,
kVKeyShiftFlag = 0x0100, // from MS docs for VkKeyScan()
kVKeyCtrlFlag = 0x0200,
kVKeyAltFlag = 0x0400,
kVKeyMask = 0x00FF,
};

enum ECommandType
{
	eCmdType_Empty,

	// First group are valid for InputDispatcher::sendKeyCommand()
	eCmdType_PressAndHoldKey,
	eCmdType_ReleaseKey,
	eCmdType_TapKey,
	eCmdType_VKeySequence,
	eCmdType_SlashCommand,
	eCmdType_SayString,

	// These trigger changes in Controls Layers (button configuration)
	eCmdType_AddControlsLayer,
	eCmdType_HoldControlsLayer,
	eCmdType_RemoveControlsLayer,

	// These control Menu selections/state
	eCmdType_OpenSubMenu,
	eCmdType_MenuReset,
	eCmdType_MenuConfirm,
	eCmdType_MenuConfirmAndClose,
	eCmdType_MenuBack, // close last sub-menu
	eCmdType_MenuReassign,

	// This should have 'data' set as an ETargetGroupType
	eCmdType_TargetGroup,

	// These should have 'data' set to an ECommandDir
	eCmdType_MenuSelect,
	eCmdType_MenuSelectAndClose,
	eCmdType_MenuReassignDir,
	eCmdType_HotspotSelect,

	// These should be processed continuously, not just on digital press
	// (may respond to analog axis data), and also have 'data' as ECommandDir
	eCmdType_MoveTurn,
	eCmdType_MoveStrafe,
	eCmdType_MoveMouse,
	eCmdType_MouseWheel,

	eCmdType_Num,

	eCmdType_LastDirectInput = eCmdType_SayString,
	eCmdType_FirstMenuControl = eCmdType_OpenSubMenu,
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
	eMouseWheelMotion_Once,
};

enum ETargetGroupType
{
	// These first 2 do NOT actually send input to game to target anyone
	eTargetGroupType_Reset,			// gGroupTargetOrigin = gDefaultGroupTarget
	eTargetGroupType_SetDefault,	// gDefaultGroupTarget = gLastGroupTarget

	// All of the below set gLastGroupTarget & gGroupTargetOrigin to target #
	eTargetGroupType_Default,		// Target gDefaultGroupTarget
	eTargetGroupType_Last,			// Target gLastGroupTarget (or their pet)
	eTargetGroupType_Prev,			// Target max(0, gGroupTargetOrigin - 1)
	eTargetGroupType_Next,			// Target min(max, gGroupTargetOrigin + 1)
	eTargetGroupType_PrevWrap,		// Target wrap(gGroupTargetOrigin - 1)
	eTargetGroupType_NextWrap,		// Target wrap(gGroupTargetOrigin + 1)

	eTargetGroupType_Num
};

enum EButtonAction
{
	eBtnAct_Down,		// Action (key) held as long as button is held (if can)
	eBtnAct_Press,		// First pushed (assigned action is just 'tapped')
	eBtnAct_ShortHold,	// Held a short time (action tapped once)
	eBtnAct_LongHold,	// Held a long time (action tapped once)
	eBtnAct_Tap,		// Released before _S/LHold triggers (action tapped once)
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
	eBtn_FirstDigital = eBtn_DLeft,
	eBtn_FirstDInputBtn = eBtn_FLeft,
	eBtn_DInputBtnNum = eBtn_Num - eBtn_FirstDInputBtn,
};

enum EHUDType
{
	eMenuStyle_List,
	eMenuStyle_4Dir,
	eMenuStyle_Grid,
	eMenuStyle_Pillar,
	eMenuStyle_Bar,
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

	eHUDType_GroupTarget,
	eHUDType_DefaultTarget,
	eHUDType_System, // Internal use only

	eHUDType_Num,

	eMenuStyle_Begin = 0,
	eMenuStyle_End = eHUDItemType_Rect,
	eHUDItemType_Begin = eHUDItemType_Rect,
	eHUDItemType_End = eHUDType_GroupTarget,
	eHUDType_Begin = eHUDType_GroupTarget,
	eHUDType_End = eHUDType_Num,
};

enum ESpecialKey
{
	eSpecialKey_MoveF,
	eSpecialKey_MoveB,
	eSpecialKey_TurnL,
	eSpecialKey_TurnR,
	eSpecialKey_StrafeL,
	eSpecialKey_StrafeR,
	eSpecialKey_MLMoveF,
	eSpecialKey_MLMoveB,
	eSpecialKey_MLTurnL,
	eSpecialKey_MLTurnR,
	eSpecialKey_MLStrafeL,
	eSpecialKey_MLStrafeR,

	eSpecialKey_TargetSelf,
	eSpecialKey_TargetGroup1,
	eSpecialKey_TargetGroup2,
	eSpecialKey_TargetGroup3,
	eSpecialKey_TargetGroup4,
	eSpecialKey_TargetGroup5,
	eSpecialKey_TargetGroup6,
	eSpecialKey_TargetGroup7,
	eSpecialKey_TargetGroup8,
	eSpecialKey_TargetGroup9,

	eSpecialKey_Num,

	eSpecialKey_FirstMove = eSpecialKey_MoveF,
	eSpecialKey_LastMove = eSpecialKey_MLStrafeR,
	eSpecialKey_MoveNum =
		eSpecialKey_LastMove - eSpecialKey_FirstMove + 1,

	eSpecialKey_FirstGroupTarget = eSpecialKey_TargetSelf,
	eSpecialKey_LastGroupTarget = eSpecialKey_TargetGroup9,
	eSpecialKey_GroupTargetNum =
		eSpecialKey_LastGroupTarget - eSpecialKey_FirstGroupTarget + 1,
};

enum ESpecialHotspot
{
	eSpecialHotspot_None,
	eSpecialHotspot_MouseLookStart,
	eSpecialHotspot_TargetSelf,
	eSpecialHotspot_TargetGroup1,
	eSpecialHotspot_TargetGroup2,
	eSpecialHotspot_TargetGroup3,
	eSpecialHotspot_TargetGroup4,
	eSpecialHotspot_TargetGroup5,
	eSpecialHotspot_TargetGroup6,
	eSpecialHotspot_TargetGroup7,
	eSpecialHotspot_TargetGroup8,
	eSpecialHotspot_TargetGroup9,

	eSpecialHotspot_Num
};

enum ECommandKeyWord
{
	eCmdWord_Unknown,

	eCmdWord_Add,
	eCmdWord_Remove,
	eCmdWord_Hold,
	eCmdWord_Layer,
	eCmdWord_Mouse,
	eCmdWord_MouseWheel,
	eCmdWord_Smooth,
	eCmdWord_Stepped,
	eCmdWord_Once,
	eCmdWord_Move,
	eCmdWord_Turn,
	eCmdWord_Strafe,
	eCmdWord_Select,
	eCmdWord_Hotspot,
	eCmdWord_Reset,
	eCmdWord_Reassign,
	eCmdWord_Menu,
	eCmdWord_Confirm,
	eCmdWord_Back,
	eCmdWord_Close,
	eCmdWord_Target,
	eCmdWord_Group,
	eCmdWord_Left,
	eCmdWord_Right,
	eCmdWord_Up,
	eCmdWord_Down,
	eCmdWord_PrevWrap,
	eCmdWord_NextWrap,
	eCmdWord_Wrap,
	eCmdWord_PrevNoWrap,
	eCmdWord_NextNoWrap,
	eCmdWord_NoWrap,
	eCmdWord_Default,
	eCmdWord_Load,
	eCmdWord_Set,
	eCmdWord_Last,
	eCmdWord_Pet,

	eCmdWord_Filler,
	eCmdWord_Num,

	eCmdWord_Forward = eCmdWord_Up,
	eCmdWord_Prev = eCmdWord_Up,
	eCmdWord_Next = eCmdWord_Down,
	eCmdWord_Top = eCmdWord_Up,
	eCmdWord_Bottom = eCmdWord_Down,
};

enum EResult
{
	eResult_Ok,
	eResult_TaskCompleted = eResult_Ok,
	eResult_Accepted = eResult_Ok,

	eResult_Fail,
	eResult_NotFound,
	eResult_InvalidParameter,
	eResult_Incomplete,
	eResult_NotAllowed,
	eResult_Malformed,
	eResult_Overflow,
	eResult_Empty,
	eResult_Declined,
};

struct Command : public ConstructFromZeroInitializedMemory<Command>
{
	ECommandType type;
	union
	{
		struct{ u16 data; u16 data2; };
		const char* string;
	};
};

struct Hotspot : public ConstructFromZeroInitializedMemory<Hotspot>
{
	struct Coord
	{
		u16 origin; // normalized 0-65535 percentage of window
		s16 offset; // direct pixel offset from .origin
	} x, y;
};

struct MenuState : public ConstructFromZeroInitializedMemory<MenuState>
{
	u16 subMenuID;
	u16 selectedID;
};

// Generic button names used in Profile .ini files
extern const char* kProfileButtonName[];

// Conversions between constant values (enums) and strings
// Strings must already be in all upper-case!
extern u8 keyNameToVirtualKey(const std::string& theKeyName);
extern std::string virtualKeyToName(u8 theVKey);
extern EButton buttonNameToID(const std::string& theName);
extern EHUDType hudTypeNameToID(const std::string& theName);
extern ECommandKeyWord commandWordToID(const std::string& theWord);
