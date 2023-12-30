//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "GlobalConstants.h"

//-----------------------------------------------------------------------------
// Data Tables
//-----------------------------------------------------------------------------

const char* kProfileButtonName[] =
{
	"Auto",		// eBtn_None
	"LSLeft",	// eBtn_LSLeft
	"LSRight",	// eBtn_LSRight
	"LSUp",		// eBtn_LSUp
	"LSDown",	// eBtn_LSDown
	"RSLeft",	// eBtn_RSLeft
	"RSRight",	// eBtn_RSRight
	"RSUp",		// eBtn_RSUp
	"RSDown",	// eBtn_RSDown
	"DLeft",	// eBtn_DLeft
	"DRight",	// eBtn_DRight
	"DUp",		// eBtn_DUp
	"DDown",	// eBtn_DDown
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

DBG_CTASSERT(eCmdType_Num <= 256);
