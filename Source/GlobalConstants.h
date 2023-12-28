//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, typedefs (structs), and
	data lookup tables that are needed by multiple modules.
*/

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
DBG_CTASSERT(eCmdType_Num <= 256);

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
	eButtonAction_Press,	// First pushed
	eButtonAction_Tap,		// Released quickly after pressed
	eButtonAction_OnceHeld,	// Held past "tap" time (once per press only)
	eButtonAction_Release,	// Released (after any hold time)
	eButtonAction_Analog,	// Continuous analog input (mouse)

	eButtonAction_Num
};

enum EResult
{
	eResult_Ok,
	eResult_TaskCompleted = eResult_Ok,

	eResult_Fail,
	eResult_NotFound,
	eResult_Incomplete,
	eResult_NotAllowed,
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
