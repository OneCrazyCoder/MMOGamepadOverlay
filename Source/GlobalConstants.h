//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, and data lookup tables
	that are needed by multiple modules.
*/

enum EHUDElement
{
	eHUDElement_None,
	eHUDElement_Abilities,
	eHUDElement_Macros,

	eHUDElement_Num,
};

// Used as first byte of output command strings to indicate purpose/format
enum ECommandChar
{
	eCmdChar_Empty,
	eCmdChar_ChangeMode,
	eCmdChar_ChangeMacroSet,
	eCmdChar_PressAndHoldKey,
	eCmdChar_ReleaseKey,
	eCmdChar_Mouse,
	eCmdChar_MoveCharacter,
	eCmdChar_SelectAbility,
	eCmdChar_SelectMacro,
	eCmdChar_SelectMenu,
	eCmdChar_ChangeMacro,
	eCmdChar_TargetGroup,
	eCmdChar_NextMouseHotspot,
	eCmdChar_VKeySequence = ' ',
	eCmdChar_SlashCommand = '/',
	eCmdChar_SayString = '>',
};

// Used as the second byte for some of the above commands for more info
enum ESubCommandChar
{
	// These first 4 must remain in this position & order!
	// This is to align with the layout of macro sets
	eSubCmdChar_Up,
	eSubCmdChar_Left,
	eSubCmdChar_Right,
	eSubCmdChar_Down,

	eSubCmdChar_Prev,
	eSubCmdChar_Next,
	eSubCmdChar_Confirm,
	eSubCmdChar_Cancel,
	eSubCmdChar_Load,
	eSubCmdChar_Save,
	eSubCmdChar_Repeat,
	eSubCmdChar_StrafeLeft,
	eSubCmdChar_StrafeRight,
	eSubCmdChar_WheelUp,
	eSubCmdChar_WheelDown,
	eSubCmdChar_WheelUpStepped,
	eSubCmdChar_WheelDownStepped,
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
