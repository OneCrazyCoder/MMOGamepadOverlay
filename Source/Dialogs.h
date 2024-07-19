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
	bool editProfileRequested;
	bool cancelled;
};
ProfileSelectResult profileSelect(
	const std::vector<std::string>& theLoadableProfiles,
	const std::vector<std::string>& theTemplateProfiles,
	int theDefaultSelection, bool wantsAutoLoad, bool firstRun);
void profileEdit(const std::vector<std::string>& theFileList, bool firstRun);

struct TreeViewDialogItem
{
	std::string name;
	size_t parentIndex;
	bool isRootCategory;
};
size_t layoutItemSelect(const std::vector<TreeViewDialogItem*>& theList);
void targetAppPath(std::string& thePath, std::string& theCommandLineParams);
EResult showLicenseAgreement(HWND theParentWindow = NULL);
EResult editMenuCommand(std::string& theString, bool directional = false);
void showError(const std::string& theError);
EResult yesNoPrompt(const std::string& thePrompt,
					const std::string& theTitle,
					bool skipIfTargetAppRunning = false);

} // Dialogs
