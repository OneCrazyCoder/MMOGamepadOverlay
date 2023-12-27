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
	eHUDElement_None		= 0x00,
	eHUDElement_Abilities	= 0x01,
	eHUDElement_Macros		= 0x02,
};

// Used as first byte of key sequence strings to indicate purpose
// If first char is none of these, a Virtual-Key Code sequence is assumed
enum ECommandChar
{
	eCmdChar_ChangeMacroSet = 0x01,
	eCmdChar_ChangeMode = 0x02,
	eCmdChar_PressAndHoldKey = 0x03,
	eCmdChar_ReleaseKey = 0x04,
	eCmdChar_Mouse = 0x05,
	eCmdChar_MoveCharacter = 0x06,
	eCmdChar_SelectAbility = 0x07,
	eCmdChar_SelectMacro = 0x0E,
	eCmdChar_SelectMenu = 0x0F,
	eCmdChar_ChangeMacro = 0x10,
	eCmdChar_TargetGroup = 0x11,
	eCmdChar_NextMouseHotspot = 0x12,
	eCmdChar_SlashCommand = '/',
	eCmdChar_SayString = '>',
};

// Used as the second byte for some of the above commands for more info
enum ESubCommandChar
{
	eSubCmdChar_None,
	eSubCmdChar_Up,
	eSubCmdChar_Down,
	eSubCmdChar_Left,
	eSubCmdChar_Right,
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
