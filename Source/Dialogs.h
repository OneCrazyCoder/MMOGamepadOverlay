//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Displays dialog box windows to prompt information from the user, with
	support for controlling said dialog boxes via Gamepad input in some cases
	(aside from typing into text boxes which will still require a keyboard).
*/

#include "Common.h"

namespace Dialogs
{

struct ProfileSelectResult
{
	std::string newName;
	int selectedIndex;
	bool autoLoadRequested;
	bool cancelled;

	ProfileSelectResult() :
		selectedIndex(), autoLoadRequested(), cancelled()
	{}
};

ProfileSelectResult profileSelect(
	const std::vector<std::string>& theLoadableProfiles,
	const std::vector<std::string>& theTemplateProfiles);

std::string targetAppPath(std::string& theCommandLineParams);

} // Dialogs
