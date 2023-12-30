//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, typedefs (structs), and
	data lookup tables that are needed by multiple modules.
*/

#include "Common.h"

enum EHUDElement
{
	eHUDElement_None,
	eHUDElement_Abilities,
	eHUDElement_Macros,

	eHUDElement_Num,
};

enum ECommandType
{
	eCmdType_Empty,

	// These are valid for InputDispatcher::sendKeyCommand()
	eCmdType_PressAndHoldKey,
	eCmdType_ReleaseKey,
	eCmdType_VKeySequence,
	eCmdType_SlashCommand,
	eCmdType_SayString,

	// These are translated into other forms by InputTranslator
	eCmdType_ChangeMode,
	eCmdType_ChangeMacroSet,
	eCmdType_MoveCharacter,
	eCmdType_SelectAbility,
	eCmdType_SelectMacro,
	eCmdType_SelectMenu,
	eCmdType_RewriteMacro,
	eCmdType_TargetGroup,
	eCmdType_NextMouseHotspot,
	eCmdType_MoveMouse,

	eCmdType_Num
};

enum ECommandSubType
{
	// These first 4 must remain in this position & order!
	// This is to align with the layout of macro sets
	eCmdSubType_Up,
	eCmdSubType_Left,
	eCmdSubType_Right,
	eCmdSubType_Down,

	eCmdSubType_Forward = eCmdSubType_Up,
	eCmdSubType_Back = eCmdSubType_Down,
	eCmdSubType_Prev,
	eCmdSubType_Next,
	eCmdSubType_Confirm,
	eCmdSubType_Cancel,
	eCmdSubType_Load,
	eCmdSubType_Save,
	eCmdSubType_Repeat,
	eCmdSubType_StrafeLeft,
	eCmdSubType_StrafeRight,
	eCmdSubType_WheelUp,
	eCmdSubType_WheelDown,
	eCmdSubType_WheelUpStepped,
	eCmdSubType_WheelDownStepped,
};

enum EButtonAction
{
	eBtnAct_PressAndHold,	// Key held as long as button is held
	eBtnAct_Press,			// First pushed (assigned key is tapped)
	eBtnAct_ShortHold,		// Held a short time (key tapped once)
	eBtnAct_LongHold,		// Held a long time (key tapped once)
	eBtnAct_Tap,			// Released before short hold time
	eBtnAct_Release,		// Released (any hold time, key tapped once)
	eBtnAct_HoldRelease,	// Releases key held by _PressAndHold
	eBtnAct_Analog,			// Continuous analog input (mouse)

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
		int data;
		const char* string;
	};
};

// Generic button names used in Profile .ini files
extern const char* kProfileButtonName[];
