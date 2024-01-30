//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Displays dialog box windows to prompt information from the user, with
	support for controlling said dialog boxes via Gamepad input (aside from
	typing into text boxes which will still require a keyboard).
*/

#include "Common.h"

namespace Dialogs
{

struct ProfileSelectResult
{
	int selectedIndex;
	std::string newName;
	bool autoLoadRequested;
	bool cancelled;
};

ProfileSelectResult profileSelect(
	const std::vector<std::string>& theLoadableProfiles,
	const std::vector<std::string>& theTemplateProfiles);

} // Dialogs
