//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, typedefs (structs), and
	data lookup tables that are needed by multiple modules.
*/

enum {
kVKeyShiftMask = 0x0100, // from MS docs for VkKeyScan()
kVKeyCtrlMask = 0x0200,
kVKeyAltMask = 0x0400,
vKeyModMask = 0xFF00,
vMkeyMask = 0x00FF,
};

enum EHUDElement
{
	eHUDElement_Macros,
	eHUDElement_Abilities,

	eHUDElement_Num,
};

enum ECommandType
{
	eCmdType_Empty,

	// First group are valid for InputDispatcher::sendKeyCommand()
	eCmdType_PressAndHoldKey,
	eCmdType_ReleaseKey,
	eCmdType_VKeySequence,
	eCmdType_SlashCommand,
	eCmdType_SayString,

	// These trigger an action or state change in InputTranslator
	eCmdType_HoldControlsLayer,
	eCmdType_AddControlsLayer,
	eCmdType_RemoveControlsLayer,
	eCmdType_ChangeMacroSet,
	eCmdType_UseAbility, // includes spells, skills, & hotbuttons
	eCmdType_ConfirmMenu,
	eCmdType_CancelMenu,

	// These should have 'data' set to an ECommandDir
	eCmdType_SelectAbility,
	eCmdType_SelectMenu,
	eCmdType_SelectHotspot,
	eCmdType_SelectMacro,
	eCmdType_RewriteMacro,

	// These should be processed continuously, not just on digital press
	// (may respond to analog axis data), and also have 'data' as ECommandDir
	eCmdType_MoveTurn,
	eCmdType_MoveStrafe,
	eCmdType_MoveMouse,
	eCmdType_SmoothMouseWheel,
	eCmdType_StepMouseWheel,

	eCmdType_Num,

	eCmdType_LastDirectInput = eCmdType_SayString,
	eCmdType_FirstDirectional = eCmdType_SelectAbility,
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

enum EButtonAction
{
	eBtnAct_Down,		// Key held as long as button is held
	eBtnAct_Press,		// First pushed (assigned key is tapped)
	eBtnAct_ShortHold,	// Held a short time (key tapped once)
	eBtnAct_LongHold,	// Held a long time (key tapped once)
	eBtnAct_Tap,		// Released before short hold time
	eBtnAct_Release,	// Released (any hold time, key tapped once)

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

enum EResult
{
	eResult_Ok,
	eResult_TaskCompleted = eResult_Ok,

	eResult_Fail,
	eResult_NotFound,
	eResult_Incomplete,
	eResult_NotAllowed,
	eResult_Malformed,
	eResult_Empty,
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

// Generic button names used in Profile .ini files
extern const char* kProfileButtonName[];

extern u8 keyNameToVirtualKey(const std::string& theKeyName, bool allowMouse);
extern EButton buttonNameToID(const std::string& theString);
extern EHUDElement hudElementNameToID(const std::string& theString);
