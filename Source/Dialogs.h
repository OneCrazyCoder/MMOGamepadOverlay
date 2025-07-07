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

struct ZERO_INIT(ProfileSelectResult)
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

struct ZERO_INIT(TreeViewDialogItem)
{
	std::string name;
	int parentIndex;
	bool isRootCategory;
};
int layoutItemSelect(const std::vector<TreeViewDialogItem*>& theList);

void targetAppPath(std::string& thePath, std::string& theCommandLineParams);

EResult showLicenseAgreement(HWND theParentWindow = NULL);
void showKnownIssues(HWND theParentWindow = NULL);

EResult editMenuCommand(std::string& theString, bool directional = false);

void showError(
	HWND theParentWindow,
	const std::string& theError);
void showNotice(
	HWND theParentWindow,
	const std::string& theNotice,
	const std::string& theTitle = "Notice");
EResult yesNoPrompt(
	HWND theParentWindow,
	const std::string& thePrompt,
	const std::string& theTitle,
	bool skipIfTargetAppRunning = false);

} // Dialogs
