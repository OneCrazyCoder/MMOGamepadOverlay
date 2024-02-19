//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Dialogs.h"

#include "Resources/resource.h"
#include "WindowManager.h"

// Enable support for Edit_SetCueBannerText
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <CommCtrl.h>

// Support for GetOpenFileName()
#include <CommDlg.h>


namespace Dialogs
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

struct ProfileSelectDialogData
{
	const std::vector<std::string>& loadableProfiles;
	const std::vector<std::string>& templateProfiles;
	ProfileSelectResult& result;

	ProfileSelectDialogData(
		const std::vector<std::string>& loadable,
		const std::vector<std::string>& templates,
		ProfileSelectResult& res)
		:
		loadableProfiles(loadable),
		templateProfiles(templates),
		result(res)
		{}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::string* sDialogEditText;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void updateProfileList(
	HWND theDialog,
	const ProfileSelectDialogData* theData,
	int theDefaultSelectedIdx = -1)
{
	DBG_ASSERT(theDialog);
	DBG_ASSERT(theData);

	// Get handle to the list box
	HWND hListBox = GetDlgItem(theDialog, IDC_LIST_PROFILES);
	DBG_ASSERT(hListBox);

	// Get current selection to restore later
	if( theDefaultSelectedIdx < 0 )
		theDefaultSelectedIdx = SendMessage(hListBox, LB_GETCURSEL, 0, 0);

	// Clear the existing content of the list box
	SendMessage(hListBox, LB_RESETCONTENT, 0, 0);

	// Determine which string vector to use for new list
	const std::vector<std::string>* aStrVec =
		theData->result.newName.empty()
			? &theData->loadableProfiles
			: &theData->templateProfiles;

	// Add new items to the list box
	for(size_t i = 0; i < aStrVec->size(); ++i)
	{
		const std::wstring aWStr = widen((*aStrVec)[i]);
		SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)(aWStr.c_str()));
	}

	// Restore initially selected item (or closest possible)
	if( theDefaultSelectedIdx >= int(aStrVec->size()) )
		theDefaultSelectedIdx = int(aStrVec->size()) - 1;
	if( theDefaultSelectedIdx < 0 && !aStrVec->empty() )
		theDefaultSelectedIdx = 0;
	SendMessage(hListBox, LB_SETCURSEL, theDefaultSelectedIdx, 0);
}


static INT_PTR CALLBACK profileSelectProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	ProfileSelectDialogData* theData = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Initialize contents
		theData = (ProfileSelectDialogData*)(UINT_PTR)lParam;
		// Allow other messages to access theData later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theData);
		DBG_ASSERT(theData);
		// Add available profiles to the list box
		updateProfileList(theDialog, theData, theData->result.selectedIndex);
		// Set initial value of auto-load checkbox
		CheckDlgButton(theDialog, IDC_CHECK_AUTOLOAD,
			theData->result.autoLoadRequested);
		{// Add prompt to edit box
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_PROFILE_NAME);
			Edit_SetCueBannerText(hEditBox,
				L"OR enter new Profile name here...");
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		theData = (ProfileSelectDialogData*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( !theData )
			break;
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Okay button clicked
				theData->result.cancelled = false;
				// Add selected item from the list box to result
				HWND hListBox = GetDlgItem(theDialog, IDC_LIST_PROFILES);
				DBG_ASSERT(hListBox);
				theData->result.selectedIndex = max(0,
					SendMessage(hListBox, LB_GETCURSEL, 0, 0));
				// Add auto-load checkbox status to result
				theData->result.autoLoadRequested =
					(IsDlgButtonChecked(theDialog, IDC_CHECK_AUTOLOAD)
						== BST_CHECKED);
				// Signal to application (NULL) that are ready to close
				PostMessage(NULL, WM_DESTROY, 0, 0);
				return (INT_PTR)TRUE;
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Cancel button clicked
				theData->result.cancelled = true;
				// Signal to application (NULL) that are ready to close
				PostMessage(NULL, WM_DESTROY, 0, 0);
				return (INT_PTR)TRUE;
			}
			break;

		case IDC_EDIT_PROFILE_NAME:
			if( HIWORD(wParam) == EN_CHANGE )
			{// Edit box contents changed - may affect other controls!
				// Update result.newName to sanitized edit box string
				HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_PROFILE_NAME);
				std::vector<WCHAR> aWStrBuffer(GetWindowTextLength(hEditBox)+1);
				GetDlgItemText(theDialog, IDC_EDIT_PROFILE_NAME,
					&aWStrBuffer[0], int(aWStrBuffer.size()));
				const bool hadNewName = !theData->result.newName.empty();
				theData->result.newName = safeFileName(narrow(&aWStrBuffer[0]));
				const bool hasNewName = !theData->result.newName.empty();
				if( hasNewName != hadNewName )
				{// Changed "modes" (Load Profile vs Create New Profile)
					updateProfileList(theDialog, theData);
					SetDlgItemText(theDialog, IDC_STATIC_PROMPT, hasNewName
							? L"Select format of new Profile:"
							: L"Select Profile to load:");
				}
				return (INT_PTR)TRUE;
			}
			break;
		}
		break;

	case WM_CLOSE:
		theData = (ProfileSelectDialogData*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( theData )
		{// Treat the same as Cancel being clicked
			theData->result.cancelled = true;
			PostMessage(NULL, WM_DESTROY, 0, 0);
			return (INT_PTR)TRUE;
		}
		break;
	}

	return (INT_PTR)FALSE;
}


static UINT_PTR CALLBACK targetAppPathProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_INITDIALOG:
		if( sDialogEditText && !sDialogEditText->empty() )
		{
			SetDlgItemText(theDialog, IDC_EDIT_TARGET_PARAMS,
				widen(*sDialogEditText).c_str());
			return (UINT_PTR)TRUE;
		}
		break;

	case WM_NOTIFY:
		if( sDialogEditText )
		{
			OFNOTIFY* aNotify = (OFNOTIFY*)lParam;
			if( aNotify->hdr.code == CDN_FILEOK )
			{
				HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_TARGET_PARAMS);
				std::vector<WCHAR> aWStrBuffer(GetWindowTextLength(hEditBox)+1);
				GetDlgItemText(theDialog, IDC_EDIT_TARGET_PARAMS,
					&aWStrBuffer[0], int(aWStrBuffer.size()));
				*sDialogEditText = narrow(&aWStrBuffer[0]);
				return (UINT_PTR)TRUE;
			}
		}
		break;
	}

	return (UINT_PTR)FALSE;
}


INT_PTR CALLBACK licenseDialogProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_INITDIALOG:
		if( GetParent(theDialog) )
		{// Change prompt to accept or decline to just "OK" if have parent
			ShowWindow(GetDlgItem(theDialog, IDCANCEL), SW_HIDE);
			ShowWindow(GetDlgItem(theDialog, IDC_STATIC_PROMPT), SW_HIDE);
			SetDlgItemText(theDialog, IDOK, L"OK");
		}
		// Load text from custom resource and set it to the Edit control
		if( HRSRC hTextRes = FindResource(NULL,
				MAKEINTRESOURCE(IDR_TEXT_LICENSE), L"TEXT") )
		{
			if( HGLOBAL hGlobal = LoadResource(NULL, hTextRes) )
			{
				if( LPVOID pTextResource = LockResource(hGlobal) )
				{
					SetDlgItemText(
						theDialog,
						IDC_EDIT_LICENSE_TEXT,
						widen((char*)pTextResource).c_str());
				}
			}
		}
		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK: // Accept button
			EndDialog(theDialog, IDOK);
			return TRUE;
		case IDCANCEL: // Decline button
			EndDialog(theDialog, IDCANCEL);
			return TRUE;
		}
		break;

	case WM_CLOSE:
		// Treat the same as clicking Decline (cancel)
		EndDialog(theDialog, IDCANCEL);
		return TRUE;
	}

	return FALSE;
}


static INT_PTR CALLBACK editMenuCommandProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	std::string* theString = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Initialize contents
		theString = (std::string*)(UINT_PTR)lParam;
		// Allow other messages to access theString later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theString);
		DBG_ASSERT(theString);
		{// Set initial string in Edit box, and select it
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_COMMAND);
			SetDlgItemText(theDialog, IDC_EDIT_COMMAND,
				widen(*theString).c_str());
			SendMessage(hEditBox, EM_SETSEL, 0, -1);
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		theString = (std::string*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( !theString )
			break;
		if( LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED )
		{// Okay button clicked - update string to edit box contents
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_COMMAND);
			std::vector<WCHAR> aWStrBuffer(GetWindowTextLength(hEditBox)+1);
			GetDlgItemText(theDialog, IDC_EDIT_COMMAND,
				&aWStrBuffer[0], int(aWStrBuffer.size()));
			*theString = trim(narrow(&aWStrBuffer[0]));
			// Signal to application (NULL) that are ready to close
			PostMessage(NULL, WM_DESTROY, 0, 0);
			return (INT_PTR)TRUE;
		}
		break;
	}

	return (INT_PTR)FALSE;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

ProfileSelectResult profileSelect(
	const std::vector<std::string>& theLoadableProfiles,
	const std::vector<std::string>& theTemplateProfiles,
	int theDefaultSelection, bool wantsAutoLoad)
{
	DBG_ASSERT(!theLoadableProfiles.empty());
	DBG_ASSERT(!theTemplateProfiles.empty());

	// Initialize data structures
	ProfileSelectResult result;
	result.selectedIndex = theDefaultSelection;
	result.autoLoadRequested = wantsAutoLoad;
	result.cancelled = false;
	ProfileSelectDialogData aDataStruct(
		theLoadableProfiles,
		theTemplateProfiles,
		result);

	// Hide main window and overlays until dialog is done
	if( WindowManager::mainHandle() )
	{
		ShowWindow(WindowManager::mainHandle(), SW_HIDE);
		WindowManager::hideOverlays();
		WindowManager::update();
	}

	// Create and show dialog window
	HWND hWnd = CreateDialogParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDD_DIALOG_PROFILE_SELECT),
		NULL,
		profileSelectProc,
		reinterpret_cast<LPARAM>(&aDataStruct));
	ShowWindow(hWnd, SW_SHOW);

	// Message handling loop
	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0))
	{
		// Standard dialog message handling
		if( !IsDialogMessage(hWnd, &msg) )
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// Dialog has signaled it is ready to be destroyed
		if( msg.message == WM_DESTROY )
			break;
	}

	// Cleanup
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyWindow(hWnd);
	if( WindowManager::mainHandle() )
		ShowWindow(WindowManager::mainHandle(), SW_SHOW);
	if( result.cancelled )
		WindowManager::showOverlays();

	return result;
}


std::string targetAppPath(std::string& theCommandLineParams)
{
	std::string result;
	sDialogEditText = &theCommandLineParams;

	OPENFILENAME ofn;
	WCHAR aPath[MAX_PATH];
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrTitle = L"Select Auto-Launch App";
	ofn.lpstrFile = aPath;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(aPath);
	ofn.lpstrFilter =
		L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.hInstance = GetModuleHandle(NULL);
	ofn.Flags =
		OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY |
		OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE;
	ofn.lpTemplateName = MAKEINTRESOURCE(IDD_DIALOG_TARGET_APP);
	ofn.lpfnHook = targetAppPathProc;
	if( GetOpenFileName(&ofn) )
		result = narrow(aPath);

	// Cleanup
	sDialogEditText = NULL;

	return result;
}


EResult showLicenseAgreement(HWND theParentWindow)
{
	if( DialogBoxParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDD_DIALOG_LICENSE),
		theParentWindow,
		licenseDialogProc,
		0) == IDOK )
	{
		return eResult_Accepted;
	}

	return eResult_Declined;
}


EResult editMenuCommand(std::string& theString, bool directional)
{
	const std::string anOriginalString = theString;

	// Hide main window and overlays until dialog is done
	if( WindowManager::mainHandle() )
	{
		ShowWindow(WindowManager::mainHandle(), SW_HIDE);
		WindowManager::hideOverlays();
		WindowManager::update();
	}

	// Create and show dialog window
	HWND hWnd = CreateDialogParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(
			directional
				? IDD_DIALOG_EDIT_DIR_CMD
				: IDD_DIALOG_EDIT_COMMAND),
		NULL,
		editMenuCommandProc,
		reinterpret_cast<LPARAM>(&theString));
	
	ShowWindow(hWnd, SW_SHOW);
	SetFocus(GetDlgItem(hWnd, IDC_EDIT_COMMAND));

	// Message handling loop
	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0))
	{
		// Standard dialog message handling
		if( !IsDialogMessage(hWnd, &msg) )
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// Dialog has signaled it is ready to be destroyed
		if( msg.message == WM_DESTROY )
			break;
	}

	// Cleanup
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyWindow(hWnd);
	if( WindowManager::mainHandle() )
		ShowWindow(WindowManager::mainHandle(), SW_SHOW);

	if( theString != anOriginalString )
		return eResult_Ok;

	WindowManager::showOverlays();
	return eResult_Cancel;
}

} // Dialogs
