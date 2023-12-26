//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Central location for global constants, enums, and data lookup tables
	that are needed by multiple modules.
*/


// Used as first byte of key sequence strings to indicate purpose
// If first char is none of these, a Virtual-Key Code sequence is assumed
enum ECommandChar
{
	eCommandChar_ChangeMacroSet = 0x01,
	eCommandChar_PressAndHoldKey = 0x02,
	eCommandChar_ReleaseKey = 0x03,
	eCommandChar_SlashCommand = '/',
	eCommandChar_SayString = '>',
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
