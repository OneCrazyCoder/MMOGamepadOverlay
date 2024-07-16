//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Dialogs.h"

#include "Gamepad.h"
#include "InputDispatcher.h" // forceReleaseHeldKeys()
#include "Resources/resource.h"
#include "TargetApp.h"
#include "WindowManager.h"

// Enable support for Edit_SetCueBannerText
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <CommCtrl.h>

// Support for GetOpenFileName()
#include <CommDlg.h>

// Support for ShellExecute (to open files in default text editor)
#include <ShellAPI.h>

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
	bool initialProfile;
	bool allowEditing;

	ProfileSelectDialogData(
		const std::vector<std::string>& loadable,
		const std::vector<std::string>& templates,
		ProfileSelectResult& res,
		bool initialProfile,
		bool allowEditing)
		:
		loadableProfiles(loadable),
		templateProfiles(templates),
		result(res),
		initialProfile(initialProfile),
		allowEditing(allowEditing)
		{}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::string* sDialogEditText;
static size_t sDialogSelected = 0;
static bool sDialogDone = false;
static bool sDialogFocusShown = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static void setDialogFocus(HWND hdlg, HWND hwndControl)
{
	SendMessage(hdlg, WM_NEXTDLGCTL, (WPARAM)hwndControl, TRUE);
}


static void setDialogFocus(HWND hdlg, int theID)
{
	setDialogFocus(hdlg, GetDlgItem(hdlg, theID));
}


static void updateProfileList(
	HWND theDialog,
	const ProfileSelectDialogData* theData,
	int theDefaultSelectedIdx = -1)
{
	DBG_ASSERT(theDialog);
	DBG_ASSERT(theData);

	// Get handle to the list box
	HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS);
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
		if( TargetApp::targetWindowIsFullScreen() )
		{// Disable edit box
			SetDlgItemText(theDialog, IDC_EDIT_PROFILE_NAME,
				L"Can't edit now - game is full-screen");
			EnableWindow(GetDlgItem(theDialog, IDC_EDIT_PROFILE_NAME), false);
		}
		else
		{// Add prompt to edit box
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_PROFILE_NAME);
			Edit_SetCueBannerText(hEditBox,
				L"OR enter new Profile name here...");
		}
		EnableWindow(GetDlgItem(theDialog, IDC_BUTTON_EDIT),
			theData->allowEditing);
		if( theData->initialProfile )
		{
			SetDlgItemText(theDialog, IDC_STATIC_PROMPT,
				L"Select Profile to auto-generate:");
			SetDlgItemText(theDialog, IDOK, L"Create");
		}
		return (INT_PTR)TRUE;

	case WM_ACTIVATE:
		if( LOWORD(wParam) == WA_INACTIVE &&
			TargetApp::targetWindowIsFullScreen() )
		{// Deactivating the window should be same as clicking Cancel
			theData = (ProfileSelectDialogData*)(UINT_PTR)
				GetWindowLongPtr(theDialog, GWLP_USERDATA);
			if( theData )
				theData->result.cancelled = true;
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;

	case WM_COMMAND:
		// Process control commands
		theData = (ProfileSelectDialogData*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( !theData )
			break;
		switch(LOWORD(wParam))
		{
		case IDOK:
		case IDC_BUTTON_EDIT:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Okay button clicked
				theData->result.cancelled = false;
				// Add selected item from the list box to result
				HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS);
				DBG_ASSERT(hListBox);
				theData->result.selectedIndex = max(0,
					SendMessage(hListBox, LB_GETCURSEL, 0, 0));
				// Add auto-load checkbox status to result
				theData->result.autoLoadRequested =
					(IsDlgButtonChecked(theDialog, IDC_CHECK_AUTOLOAD)
						== BST_CHECKED);
				// Flag if actually want to edit profile instead of load it
				theData->result.editProfileRequested =
					LOWORD(wParam) == IDC_BUTTON_EDIT;
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
			if( HIWORD(wParam) == EN_CHANGE &&
				!TargetApp::targetWindowIsFullScreen() )
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
					SetDlgItemText(theDialog, IDC_STATIC_PROMPT,
						hasNewName ? L"Select format of new Profile:" :
						theData->initialProfile
							? L"Select Profile to auto-generate:"
							: L"Select Profile to load:");
					SetDlgItemText(theDialog, IDOK,
						hasNewName || theData->initialProfile
							? L"Create" : L"Load");
					EnableWindow(GetDlgItem(theDialog, IDC_BUTTON_EDIT),
						theData->allowEditing && !hasNewName);
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


static INT_PTR CALLBACK editProfileSelectProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	std::vector<std::string>* theFiles = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		theFiles = (std::vector<std::string>*)(UINT_PTR)lParam;
		// Allow other messages to access theFiles later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theFiles);
		DBG_ASSERT(theFiles);
		{// Add available files to the list box
			HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS);
			DBG_ASSERT(hListBox);
			for(size_t i = 0; i < theFiles->size(); ++i)
			{
				SendMessage(hListBox, LB_ADDSTRING, 0,
					(LPARAM)(widen(getFileName((*theFiles)[i])).c_str()));
			}
			SendMessage(hListBox, LB_SETCURSEL, theFiles->size()-1, 0);
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Okay (open file) button clicked
				theFiles = (std::vector<std::string>*)(UINT_PTR)
					GetWindowLongPtr(theDialog, GWLP_USERDATA);
				if( !theFiles )
					break;
				// Get selected item and open it w/ user's text editor
				HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS);
				DBG_ASSERT(hListBox);
				int aFile = max(0, SendMessage(hListBox, LB_GETCURSEL, 0, 0));
				ShellExecute(NULL, L"open",
					widen((*theFiles)[aFile]).c_str(),
					NULL, NULL, SW_SHOWNORMAL);
				return (INT_PTR)TRUE;
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Cancel button clicked
				// Signal to main loop that are ready to close
				sDialogDone = true;
				return (INT_PTR)TRUE;
			}
			break;
		}
		break;

	case WM_CLOSE:
		// Treat the same as Cancel being clicked
		sDialogDone = true;
		return (INT_PTR)TRUE;

	case WM_DEVICECHANGE:
		Gamepad::checkDeviceChange();
		break;
	}

	return (INT_PTR)FALSE;
}


static INT_PTR CALLBACK layoutItemSelectProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	std::vector<TreeViewDialogItem*>* theItems = NULL;
	LPNMHDR nmhdr = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		theItems = (std::vector<TreeViewDialogItem*>*)
			(UINT_PTR)lParam;
		DBG_ASSERT(theItems);
		EnableWindow(GetDlgItem(theDialog, IDOK), FALSE);
		{// Add available items to the tree
			HWND hTreeView = GetDlgItem(theDialog, IDC_TREE_ITEMS);
			DBG_ASSERT(hTreeView);
			TVINSERTSTRUCT tvInsert;
			tvInsert.hInsertAfter = TVI_SORT;
			std::vector<HTREEITEM> aHandlesList;
			aHandlesList.reserve(theItems->size());
			aHandlesList.push_back(TVI_ROOT);
			BitVector<> anItemHasChildren;
			anItemHasChildren.clearAndResize(theItems->size());
			for(size_t i = 1; i < theItems->size(); ++i)
				anItemHasChildren.set((*theItems)[i]->parentIndex);
			for(size_t i = 1; i < theItems->size(); ++i)
			{
				DBG_ASSERT((*theItems)[i] != null);
				const std::wstring& anItemName = widen((*theItems)[i]->name);
				const size_t aParentIdx = (*theItems)[i]->parentIndex;
				const u16 isOkAllowed = (*theItems)[i]->allowedAsResult;
				tvInsert.item.pszText = const_cast<WCHAR*>(anItemName.c_str());
				tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM;
				tvInsert.item.cChildren = anItemHasChildren.test(i) ? 1 : 0;
				if( tvInsert.item.cChildren )
					tvInsert.item.mask |= TVIF_CHILDREN;
				tvInsert.item.lParam = MAKELPARAM(i, isOkAllowed);
				tvInsert.hParent = aHandlesList[aParentIdx];
				aHandlesList.push_back((HTREEITEM)SendMessage(
					hTreeView,
					TVM_INSERTITEM, 0,
					(LPARAM)&tvInsert));
			}
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Okay button clicked
				sDialogSelected = 0;
				HWND hTreeView = GetDlgItem(theDialog, IDC_TREE_ITEMS);
				 HTREEITEM hSelectedItem = (HTREEITEM)
					 SendMessage(hTreeView, TVM_GETNEXTITEM, TVGN_CARET, 0);
				sDialogDone = true;
				if( hSelectedItem )
				{
					TVITEM tvItem;
					tvItem.mask = TVIF_PARAM;
					tvItem.hItem = hSelectedItem;
					SendMessage(hTreeView, TVM_GETITEM, 0, (LPARAM)&tvItem);
					sDialogSelected = (size_t)LOWORD(tvItem.lParam);
				}
				return (INT_PTR)TRUE;
			}
			break;

		case IDCANCEL:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Cancel button clicked
				sDialogSelected = 0;
				// Signal to main loop that are ready to close
				sDialogDone = true;
				return (INT_PTR)TRUE;
			}
			break;
		}
		break;

	case WM_NOTIFY:
		nmhdr = (LPNMHDR)lParam;
		if( nmhdr->idFrom == IDC_TREE_ITEMS && nmhdr->code == TVN_SELCHANGED )
		{
			LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)nmhdr;
			HTREEITEM hSelectedItem = pnmtv->itemNew.hItem;
			if( hSelectedItem )
			{
				TVITEM tvItem;
				tvItem.mask = TVIF_PARAM;
				tvItem.hItem = hSelectedItem;
				SendMessage(nmhdr->hwndFrom, TVM_GETITEM, 0, (LPARAM)&tvItem);
				EnableWindow(GetDlgItem(theDialog, IDOK), HIWORD(tvItem.lParam) != 0);
			}
			else
			{
				EnableWindow(GetDlgItem(theDialog, IDOK), FALSE);
			}
		}
		return (INT_PTR)TRUE;

	case WM_CLOSE:
		// Treat the same as Cancel being clicked
		sDialogSelected = 0;
		sDialogDone = true;
		return (INT_PTR)TRUE;

	case WM_DEVICECHANGE:
		Gamepad::checkDeviceChange();
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
					DWORD aSize = SizeofResource(NULL, hTextRes);
					std::string aString; aString.reserve(aSize);
					for(size_t i = 0; i < aSize; ++i)
					{
						if( ((char*)pTextResource)[i] == '\0' )
							break;
						aString.push_back(((char*)pTextResource)[i]);
					}
					HWND hEditControl =
						GetDlgItem(theDialog, IDC_EDIT_LICENSE_TEXT);
					SendMessage(hEditControl, WM_SETTEXT, 0,
						(LPARAM)widen(aString).c_str());					
					SetTimer(theDialog, 1, 50, NULL);
					setDialogFocus(theDialog, hEditControl);
				}
			}
		}
		return TRUE;

	case WM_TIMER:
		if( wParam == 1 )
		{
			KillTimer(theDialog, 1);
			HWND hEditControl =
				GetDlgItem(theDialog, IDC_EDIT_LICENSE_TEXT);
			InvalidateRect(hEditControl, NULL, TRUE);
			UpdateWindow(hEditControl);
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

	case WM_ACTIVATE:
		if( LOWORD(wParam) == WA_INACTIVE &&
			TargetApp::targetWindowIsFullScreen() )
		{// Deactivating the window cancels change being made
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;

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


static void dialogCheckGamepad(HWND theDialog)
{
	Gamepad::update();

	if( GetForegroundWindow() != theDialog )
		return;

	HWND aFocusControl = GetFocus();
	UINT aFocusID =
		(aFocusControl  && IsChild(theDialog, aFocusControl))
			? GetDlgCtrlID(aFocusControl) : 0;
	INPUT input[2] = { 0 };
	input[0].type = INPUT_KEYBOARD;
	input[1].type = INPUT_KEYBOARD;
	input[1].ki.dwFlags = KEYEVENTF_KEYUP;

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
		case IDC_EDIT_PROFILE_NAME:
			setDialogFocus(theDialog, IDC_LIST_ITEMS);
			break;
		default:
			input[0].ki.wVk = input[1].ki.wVk = VK_RETURN;
			SendInput(2, input, sizeof(INPUT));
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
		// Show keyboard focus indicators from now on
		SendMessage(theDialog,
			WM_CHANGEUISTATE,
			MAKELONG(UIS_CLEAR, UISF_HIDEACCEL | UISF_HIDEFOCUS),
			0);
	}

	switch(aNewDir)
	{
	case eDialogDir_Left:
		switch(aFocusID)
		{
		case IDC_LIST_ITEMS:
		case IDC_EDIT_PROFILE_NAME:
			break;
		default:
			if( GetDlgItem(theDialog, IDC_LIST_ITEMS) )
			{
				setDialogFocus(theDialog, IDC_LIST_ITEMS);
			}
			else
			{
				input[0].ki.wVk = input[1].ki.wVk = VK_LEFT;
				SendInput(2, input, sizeof(INPUT));
			}
			break;
		}
		break;
	case eDialogDir_Right:
		switch(aFocusID)
		{
		case IDC_LIST_ITEMS:
		case IDC_EDIT_PROFILE_NAME:
			input[0].ki.wVk = input[1].ki.wVk = VK_TAB;
			SendInput(2, input, sizeof(INPUT));
			break;
		default:
			input[0].ki.wVk = input[1].ki.wVk = VK_RIGHT;
			SendInput(2, input, sizeof(INPUT));
			break;
		}
		break;
	case eDialogDir_Up:
		switch(aFocusID)
		{
		case IDOK:
			break;
		case IDC_EDIT_PROFILE_NAME:
			setDialogFocus(theDialog, IDC_LIST_ITEMS);
			break;
		default:
			input[0].ki.wVk = input[1].ki.wVk = VK_UP;
			SendInput(2, input, sizeof(INPUT));
			break;
		}
		break;
	case eDialogDir_Down:
		switch(aFocusID)
		{
		case IDC_CHECK_AUTOLOAD:
			setDialogFocus(theDialog, IDC_EDIT_PROFILE_NAME);
			break;
		default:
			input[0].ki.wVk = input[1].ki.wVk = VK_DOWN;
			SendInput(2, input, sizeof(INPUT));
			break;
		}
		break;
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

ProfileSelectResult profileSelect(
	const std::vector<std::string>& theLoadableProfiles,
	const std::vector<std::string>& theTemplateProfiles,
	int theDefaultSelection, bool wantsAutoLoad, bool firstRun)
{
	DBG_ASSERT(!theLoadableProfiles.empty());
	DBG_ASSERT(!theTemplateProfiles.empty());

	// Initialize data structures
	ProfileSelectResult result;
	result.selectedIndex = theDefaultSelection;
	result.autoLoadRequested = wantsAutoLoad;
	result.cancelled = true;

	const bool needsToBeTopMost =
		TargetApp::targetWindowIsTopMost() ||
		TargetApp::targetWindowIsFullScreen();
	const bool allowEditing = !firstRun && !needsToBeTopMost;
	ProfileSelectDialogData aDataStruct(
		theLoadableProfiles,
		theTemplateProfiles,
		result, firstRun, allowEditing);

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
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
	if( needsToBeTopMost )
		SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	SetForegroundWindow(hWnd);

	// Loop until dialog signals it is done
	sDialogDone = false;
	sDialogFocusShown = false;
	while(!gShutdown && !hadFatalError())
	{
		mainLoopUpdate(hWnd);
		dialogCheckGamepad(hWnd);

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


void profileEdit(const std::vector<std::string>& theFileList)
{
	DBG_ASSERT(!theFileList.empty());

	// Initialize data structures
	ProfileSelectResult result;

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
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
		MAKEINTRESOURCE(IDD_DIALOG_PROFILE_EDIT),
		NULL,
		editProfileSelectProc,
		reinterpret_cast<LPARAM>(&theFileList));
	ShowWindow(hWnd, SW_SHOW);
	SetForegroundWindow(hWnd);

	// Loop until dialog signals it is done
	sDialogDone = false;
	sDialogFocusShown = false;
	while(!gShutdown && !hadFatalError())
	{
		mainLoopUpdate(hWnd);
		dialogCheckGamepad(hWnd);

		if( sDialogDone )
			break;

		mainLoopSleep();
	}

	// Cleanup
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);
	if( WindowManager::mainHandle() )
		ShowWindow(WindowManager::mainHandle(), SW_SHOW);
	WindowManager::showOverlays();
}


size_t layoutItemSelect(const std::vector<TreeViewDialogItem*>& theList)
{
	DBG_ASSERT(!theList.empty());

	// Initialize data structures
	sDialogSelected = 0;

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
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
		MAKEINTRESOURCE(IDD_DIALOG_LAYOUT_SELECT),
		NULL,
		layoutItemSelectProc,
		reinterpret_cast<LPARAM>(&theList));
	ShowWindow(hWnd, SW_SHOW);
	SetForegroundWindow(hWnd);

	// Loop until dialog signals it is done
	sDialogDone = false;
	sDialogFocusShown = false;
	while(!gShutdown && !hadFatalError())
	{
		mainLoopUpdate(hWnd);
		dialogCheckGamepad(hWnd);

		if( sDialogDone )
			break;

		mainLoopSleep();
	}

	// Cleanup
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);
	if( WindowManager::mainHandle() )
		ShowWindow(WindowManager::mainHandle(), SW_SHOW);
	WindowManager::showOverlays();

	return sDialogSelected;
}


void targetAppPath(std::string& thePath, std::string& theCommandLineParams)
{
	// Don't ask about auto-launching an app when already have one active
	if( TargetApp::targetAppActive() )
	{
		thePath.clear();
		return;
	}

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
		thePath.clear();
		return;
	}

	const std::wstring& anInitDir = widen(getFileDir(thePath));
	const std::wstring& aFileName = widen(getFileName(thePath));
	OPENFILENAME ofn;
	WCHAR aWPath[MAX_PATH] = { 0 };
	if( !aFileName.empty() )
		wcsncpy(aWPath, aFileName.c_str(), MAX_PATH-1);
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hTempParentWindow;
	ofn.lpstrTitle = L"Select Auto-Launch App";
	ofn.lpstrFile = aWPath;
	ofn.nMaxFile = sizeof(aWPath) / sizeof(aWPath[0]);
	ofn.lpstrFilter =
		L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	if( !anInitDir.empty() )
		ofn.lpstrInitialDir = anInitDir.c_str();
	ofn.hInstance = GetModuleHandle(NULL);
	ofn.Flags =
		OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY |
		OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE;
	ofn.lpTemplateName = MAKEINTRESOURCE(IDD_DIALOG_TARGET_APP);
	ofn.lpfnHook = targetAppPathProc;
	if( GetOpenFileName(&ofn) )
	{
		thePath = narrow(aWPath);
	}
	else
	{
		MessageBox(hTempParentWindow,
			L"No target app path selected.\n"
			L"You can manually edit the .ini file [System]AutoLaunchApp= "
			L"entry to add one later.",
			L"Auto-Launch Target App",
			MB_OK | MB_ICONWARNING);
		thePath.clear();
	}

	// Cleanup
	sDialogEditText = NULL;
	if( hTempParentWindow )
		DestroyWindow(hTempParentWindow);

	mainLoopTimeSkip();
}


EResult showLicenseAgreement(HWND theParentWindow)
{
	TargetApp::prepareForDialog();
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
	TargetApp::prepareForDialog();
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


void showError(const std::string& theError)
{
	InputDispatcher::forceReleaseHeldKeys();

	HWND hTempParentWindow = NULL;
	if( TargetApp::targetWindowIsTopMost() )
	{// Create a temporary invisible top-most window as dialog parent
		hTempParentWindow = CreateWindowEx(
			WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
			NULL, NULL, GetModuleHandle(NULL), NULL);
	}

	EResult result = eResult_Yes;
	MessageBox(
		hTempParentWindow,
		widen(theError).c_str(),
		L"Error",
		MB_OK | MB_ICONWARNING);

	// Cleanup
	if( hTempParentWindow )
		DestroyWindow(hTempParentWindow);

	mainLoopTimeSkip();
}


EResult yesNoPrompt(
	const std::string& thePrompt,
	const std::string& theTitle,
	bool skipIfTargetAppRunning)
{
	if( skipIfTargetAppRunning && TargetApp::targetAppActive() )
		return eResult_Cancel;

	InputDispatcher::forceReleaseHeldKeys();

	HWND hTempParentWindow = NULL;
	if( TargetApp::targetWindowIsTopMost() )
	{// Create a temporary invisible top-most window as dialog parent
		hTempParentWindow = CreateWindowEx(
			WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0,
			NULL, NULL, GetModuleHandle(NULL), NULL);
	}

	EResult result = eResult_Yes;
	if( MessageBox(
			hTempParentWindow,
			widen(thePrompt).c_str(),
			widen(theTitle).c_str(),
			MB_YESNO) != IDYES )
	{
		result = eResult_No;
	}

	// Cleanup
	if( hTempParentWindow )
		DestroyWindow(hTempParentWindow);

	mainLoopTimeSkip();
	return result;
}

} // Dialogs
