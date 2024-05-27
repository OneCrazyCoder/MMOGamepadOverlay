//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Dialogs.h"

#include "Gamepad.h"
#include "InputDispatcher.h" // prepareForDialog()
#include "Resources/resource.h"
#include "TargetApp.h" // targetAppActive(), targetWindowIsTopMost()
#include "WindowManager.h"

// Enable support for Edit_SetCueBannerText
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <CommCtrl.h>

// Support for GetOpenFileName()
#include <CommDlg.h>

// Forward declares of functions defined in Main.cpp for main loop update
void mainLoopUpdate(HWND theDialog);
void mainLoopSleep();
void mainLoopTimeSkip();

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
static bool sDialogDone = false;
static bool sDialogFocusShown = false;


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
				// Signal to main loop that are ready to close
				sDialogDone = true;
				return (INT_PTR)TRUE;
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Cancel button clicked
				theData->result.cancelled = true;
				// Signal to main loop that are ready to close
				sDialogDone = true;
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
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;

	case WM_DEVICECHANGE:
		Gamepad::checkDeviceChange();
		break;
	}

	return (INT_PTR)FALSE;
}


static void profileSelectCheckGamepad(HWND theDialog)
{
	Gamepad::update();

	if( GetForegroundWindow() != theDialog )
		return;

	HWND aFocusControl = GetFocus();
	UINT aFocusID =
		(aFocusControl  && IsChild(theDialog, aFocusControl))
			? GetDlgCtrlID(aFocusControl) : 0;

	// Check for confirm (A on XB) or back (B on XB)
	if( Gamepad::buttonHit(eBtn_FRight) )
	{
		SendDlgItemMessage(theDialog, IDCANCEL, BM_CLICK, 0, 0);
		Gamepad::ignoreUntilPressedAgain(eBtn_FRight);
		return;
	}

	if( Gamepad::buttonHit(eBtn_FDown) )
	{
		switch(aFocusID)
		{
		case IDOK:
		case IDCANCEL:
		case IDC_CHECK_AUTOLOAD:
			SendMessage(GetFocus(), BM_CLICK, 0, 0);
			break;
		case IDC_EDIT_PROFILE_NAME:
			SetFocus(GetDlgItem(theDialog, IDC_LIST_PROFILES));
			break;
		default:
			SendDlgItemMessage(theDialog, IDOK, BM_CLICK, 0, 0);
			break;
		}
		Gamepad::ignoreUntilPressedAgain(eBtn_FDown);
		return;
	}
	
	// Check for focus change or scrolling through list with directional input
	static int sAutoRepeatTimer = 0;
	enum EDialogDir
	{
		eDialogDir_None,
		eDialogDir_Left,
		eDialogDir_Right,
		eDialogDir_Up,
		eDialogDir_Down,
	};
	EDialogDir aNewDir = eDialogDir_None;
	EDialogDir aHeldDir = eDialogDir_None;

	if( Gamepad::buttonHit(eBtn_DLeft) ||
		Gamepad::buttonHit(eBtn_LSLeft) )
	{
		aNewDir = eDialogDir_Left;
		sAutoRepeatTimer = 0;
	}
	else if( Gamepad::buttonHit(eBtn_DRight) ||
			 Gamepad::buttonHit(eBtn_LSRight) )
	{
		aNewDir = eDialogDir_Right;
		sAutoRepeatTimer = 0;
	}
	else if( Gamepad::buttonHit(eBtn_DUp) ||
			 Gamepad::buttonHit(eBtn_LSUp) )
	{
		aNewDir = eDialogDir_Up;
		sAutoRepeatTimer = 0;
	}
	else if( Gamepad::buttonHit(eBtn_DDown) ||
			 Gamepad::buttonHit(eBtn_LSDown) )
	{
		aNewDir = eDialogDir_Down;
		sAutoRepeatTimer = 0;
	}
	else if( Gamepad::buttonDown(eBtn_DUp) ||
			 Gamepad::buttonDown(eBtn_LSUp) )
	{
		aHeldDir = eDialogDir_Up;
	}
	else if( Gamepad::buttonDown(eBtn_DDown) ||
			 Gamepad::buttonDown(eBtn_LSDown) )
	{
		aHeldDir = eDialogDir_Down;
	}
	else
	{
		sAutoRepeatTimer = 0;
	}

	if( aHeldDir != eDialogDir_None )
	{
		const int oldTimer = sAutoRepeatTimer;
		sAutoRepeatTimer += gAppFrameTime;
		if( oldTimer < 400 && sAutoRepeatTimer >= 400 )
		{
			aNewDir = aHeldDir;
			sAutoRepeatTimer -= 100;
		}
	}

	if( aNewDir == eDialogDir_None )
		return;

	const bool hasKBFocus =
		!(SendMessage(theDialog, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
	if( !hasKBFocus )
	{
		if( aNewDir == eDialogDir_Down || aNewDir == eDialogDir_Up)
		{// First time press up or down, focus on profile list
			aFocusControl = GetDlgItem(theDialog, IDC_LIST_PROFILES);
			SetFocus(aFocusControl);
			aFocusID = IDC_LIST_PROFILES;
		}
		// Show keyboard focus indicators from now on
		SendMessage(theDialog,
			WM_CHANGEUISTATE,
			MAKELONG(UIS_CLEAR, UISF_HIDEACCEL | UISF_HIDEFOCUS),
			0);
	}

	INPUT input[2] = { 0 };
	input[0].type = INPUT_KEYBOARD;
	input[1].type = INPUT_KEYBOARD;
	input[1].ki.dwFlags = KEYEVENTF_KEYUP;

	switch(aNewDir)
	{
	case eDialogDir_Left:
		switch(aFocusID)
		{
		case IDC_LIST_PROFILES:
		case IDC_EDIT_PROFILE_NAME:
			break;
		default:
			SetFocus(GetDlgItem(theDialog, IDC_LIST_PROFILES));
		}
		break;
	case eDialogDir_Right:
		switch(aFocusID)
		{
		case IDC_LIST_PROFILES:
		case IDC_EDIT_PROFILE_NAME:
			SetFocus(GetDlgItem(theDialog, IDC_CHECK_AUTOLOAD));
			break;
		}
		break;
	case eDialogDir_Up:
		switch(aFocusID)
		{
		case IDOK:
			break;
		case IDC_EDIT_PROFILE_NAME:
			SetFocus(GetDlgItem(theDialog, IDC_LIST_PROFILES));
			break;
		default:
			input[0].ki.wVk = VK_UP;
			input[1].ki.wVk = VK_UP;
			SendInput(2, input, sizeof(INPUT));
			break;
		}
		break;
	case eDialogDir_Down:
		switch(aFocusID)
		{
		case IDC_CHECK_AUTOLOAD:
			SetFocus(GetDlgItem(theDialog, IDC_EDIT_PROFILE_NAME));
			break;
		default:
			input[0].ki.wVk = VK_DOWN;
			input[1].ki.wVk = VK_DOWN;
			SendInput(2, input, sizeof(INPUT));
			break;
		}
		break;
	}
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


static INT_PTR CALLBACK licenseDialogProc(
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
			// Signal to application that are ready to close
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;

	case WM_DEVICECHANGE:
		Gamepad::checkDeviceChange();
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
	result.cancelled = true;
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

	// Release any keys held by InputDispatcher first
	InputDispatcher::forceReleaseHeldKeys();

	// Create and show dialog window
	HWND hWnd = CreateDialogParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDD_DIALOG_PROFILE_SELECT),
		NULL,
		profileSelectProc,
		reinterpret_cast<LPARAM>(&aDataStruct));
	ShowWindow(hWnd, SW_SHOW);
	if( TargetApp::targetWindowIsTopMost() )
		SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	SetForegroundWindow(hWnd);

	// Loop until dialog signals it is done
	sDialogDone = false;
	sDialogFocusShown = false;
	while(!gShutdown && !hadFatalError())
	{
		mainLoopUpdate(hWnd);
		profileSelectCheckGamepad(hWnd);

		if( sDialogDone )
			break;

		mainLoopSleep();
	}

	// Cleanup
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
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

	// Don't ask about auto-launching an app when already have one active
	if( TargetApp::targetAppActive() )
		return result;

	sDialogEditText = &theCommandLineParams;

	InputDispatcher::forceReleaseHeldKeys();

	HWND hTempParentWindow = NULL;
	if( TargetApp::targetWindowIsTopMost() )
	{// Create a temporary invisible top-most window as dialog parent
		hTempParentWindow = CreateWindowEx(
			WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
			NULL, NULL, GetModuleHandle(NULL), NULL);

	}

	if( MessageBox(
			hTempParentWindow,
			L"Would you like to automatically launch target game "
			L"when loading this profile at startup?",
			L"Auto-Launch Target App",
			MB_YESNO) != IDYES )
	{
		mainLoopTimeSkip();
		return result;
	}

	OPENFILENAME ofn;
	WCHAR aPath[MAX_PATH];
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hTempParentWindow;
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
	{
		result = narrow(aPath);
	}
	else
	{
		MessageBox(hTempParentWindow,
			L"No target app path selected.\n"
			L"You can manually edit the .ini file [System]AutoLaunchApp= "
			L"entry to add one later.",
			L"Auto-Launch Target App",
			MB_OK | MB_ICONWARNING);
	}

	// Cleanup
	sDialogEditText = NULL;
	if( hTempParentWindow )
		DestroyWindow(hTempParentWindow);

	mainLoopTimeSkip();
	return result;
}


EResult showLicenseAgreement(HWND theParentWindow)
{
	InputDispatcher::forceReleaseHeldKeys();

	if( DialogBoxParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDD_DIALOG_LICENSE),
		theParentWindow,
		licenseDialogProc,
		0) == IDOK )
	{
		mainLoopTimeSkip();
		return eResult_Accepted;
	}

	mainLoopTimeSkip();
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

	// Release any keys held by InputDispatcher first
	InputDispatcher::forceReleaseHeldKeys();

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
	SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	SetForegroundWindow(hWnd);
	SetFocus(GetDlgItem(hWnd, IDC_EDIT_COMMAND));

	// Loop until dialog signals it is done
	sDialogDone = false;
	sDialogFocusShown = false;
	while(!gShutdown && !hadFatalError())
	{
		mainLoopUpdate(hWnd);

		Gamepad::update();
		if( GetForegroundWindow() == hWnd &&
			(Gamepad::buttonHit(eBtn_FRight) ||
			 Gamepad::buttonHit(eBtn_FDown)) )
		{// Treat the same as clicking Okay on the dialog
			SendDlgItemMessage(hWnd, IDOK, BM_CLICK, 0, 0);
			Gamepad::ignoreUntilPressedAgain(eBtn_FRight);
			Gamepad::ignoreUntilPressedAgain(eBtn_FDown);
		}

		if( sDialogDone )
			break;

		mainLoopSleep();
	}

	// Cleanup
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);
	if( WindowManager::mainHandle() )
		ShowWindow(WindowManager::mainHandle(), SW_SHOW);

	if( theString != anOriginalString )
		return eResult_Ok;

	WindowManager::showOverlays();
	return eResult_Cancel;
}

} // Dialogs
