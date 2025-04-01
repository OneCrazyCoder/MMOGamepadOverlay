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
#include <CommDlg.h> // GetOpenFileName()
#include <richedit.h> // Rich text edit field
#include <ShellAPI.h> // ShellExecute() (open files in notepad)
#include <shlobj.h> // SHBrowseForFolder()


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


struct XInputFixDialogData
{
	std::wstring destFolder;
	std::wstring gameExeName;
	bool bitWidthKnown;
	bool bitWidthIs64Bit;
	bool readyForPath;
	bool pathEntered;
	bool readyForExport;
	XInputFixDialogData() :
		bitWidthKnown(),
		bitWidthIs64Bit(),
		pathEntered(),
		readyForPath(),
		readyForExport()
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


static DWORD CALLBACK rtfStreamCallback(
	DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb)
{
	const char** ppData = (const char**)dwCookie;
	memcpy(pbBuff, *ppData, cb);
	*ppData += cb;
	*pcb = cb;
	return 0;
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
		if( TargetApp::targetWindowIsTopMost() )
		{// Disable edit box
			SetDlgItemText(theDialog, IDC_EDIT_PROFILE_NAME,
				L"Can't edit - game is top-most window");
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
				!TargetApp::targetWindowIsTopMost() )
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

	case WM_DEVICECHANGE:
		Gamepad::checkDeviceChange();
		break;
	}

	return (INT_PTR)FALSE;
}


static int CALLBACK layoutItemSortProc(
	LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	std::vector<TreeViewDialogItem*>* theItems =
		(std::vector<TreeViewDialogItem*>*)(UINT_PTR)lParamSort;
	TreeViewDialogItem* anItem1 =
		(*theItems)[LOWORD(lParam1)];
	TreeViewDialogItem* anItem2 =
		(*theItems)[LOWORD(lParam2)];
	std::string aName1 = condense(anItem1->name);
	std::string aName2 = condense(anItem2->name);
	int aName1Val = breakOffIntegerSuffix(aName1);
	int aName2Val = breakOffIntegerSuffix(aName2);
	if( aName1Val >= 0 && aName1[aName1.size()-1] == '-' )
	{
		aName1.resize(aName1.size()-1);
		aName1Val = breakOffIntegerSuffix(aName1);
	}
	if( aName2Val >= 0 && aName2[aName2.size()-1] == '-' )
	{
		aName2.resize(aName2.size()-1);
		aName2Val = breakOffIntegerSuffix(aName2);
	}
	if( aName1 == aName2 )
		return aName1Val - aName2Val;
	return anItem1->name.compare(anItem2->name);
}


static void layoutItemSortTree(
	HWND hTreeView,
	HTREEITEM hItem,
	std::vector<TreeViewDialogItem*>* theItems)
{
	TVSORTCB tvs;
	tvs.hParent = hItem;
	tvs.lpfnCompare = layoutItemSortProc;
	tvs.lParam = (LPARAM)theItems;
	if( hItem != TVI_ROOT )
		TreeView_SortChildrenCB(hTreeView, &tvs, 0);
	
	HTREEITEM hChild = TreeView_GetChild(hTreeView, hItem);
	while(hChild)
	{
		layoutItemSortTree(hTreeView, hChild, theItems);
		hChild = TreeView_GetNextSibling(hTreeView, hChild);
	}
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
		DBG_ASSERT(theItems && !theItems->empty());
		EnableWindow(GetDlgItem(theDialog, IDOK), FALSE);
		{// Add available items to the tree
			HWND hTreeView = GetDlgItem(theDialog, IDC_TREE_ITEMS);
			DBG_ASSERT(hTreeView);
			TVINSERTSTRUCT tvInsert;
			tvInsert.hInsertAfter = TVI_LAST;
			std::vector<HTREEITEM> aHandlesList(theItems->size());
			BitVector<> anItemHasChildren, anItemWasAdded;
			anItemHasChildren.clearAndResize(theItems->size());
			anItemWasAdded.clearAndResize(theItems->size());
			for(size_t i = 1; i < theItems->size(); ++i)
				anItemHasChildren.set((*theItems)[i]->parentIndex);
			aHandlesList[0] = TVI_ROOT;
			anItemWasAdded.set(0);
			while(!anItemWasAdded.all())
			{
				for(size_t i = 1; i < theItems->size(); ++i)
				{
					DBG_ASSERT((*theItems)[i] != null);
					if( anItemWasAdded.test(i) )
						continue;
					const std::wstring& anItemName =
						widen((*theItems)[i]->name);
					const u32 aParentIdx =
						u32((*theItems)[i]->parentIndex);
					if( !anItemWasAdded.test(aParentIdx) )
						continue;
					const u32 isRootCategory =
						u32((*theItems)[i]->isRootCategory);
					tvInsert.item.pszText =
						const_cast<WCHAR*>(anItemName.c_str());
					tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM;
					tvInsert.item.cChildren =
						anItemHasChildren.test(i) ? 1 : 0;
					if( tvInsert.item.cChildren )
						tvInsert.item.mask |= TVIF_CHILDREN;
					tvInsert.item.lParam = MAKELPARAM(i, isRootCategory);
					tvInsert.hParent = aHandlesList[aParentIdx];
					aHandlesList[i] = (HTREEITEM)SendMessage(
						hTreeView,
						TVM_INSERTITEM, 0,
						(LPARAM)&tvInsert);
					anItemWasAdded.set(i);
				}
			}
			layoutItemSortTree(hTreeView, TVI_ROOT, theItems);
			if( size_t anInitialSel = (*theItems)[0]->parentIndex )
			{
				HTREEITEM hInitialItem = aHandlesList[anInitialSel];
				TreeView_SelectItem(hTreeView, hInitialItem);
				TreeView_EnsureVisible(hTreeView, hInitialItem);
			}
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		switch(LOWORD(wParam))
		{
		case IDOK:
			if( HIWORD(wParam) == BN_CLICKED )
			{// Okay button clicked - signal which item was selected
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
		{// Update enabled status of OK button when treeview item selected
			LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)nmhdr;
			HTREEITEM hSelectedItem = pnmtv->itemNew.hItem;
			if( hSelectedItem )
			{
				TVITEM tvItem;
				tvItem.mask = TVIF_PARAM;
				tvItem.hItem = hSelectedItem;
				SendMessage(nmhdr->hwndFrom, TVM_GETITEM, 0, (LPARAM)&tvItem);
				const u32 isRootCategory = HIWORD(tvItem.lParam) != 0;
				EnableWindow(GetDlgItem(theDialog, IDOK), !isRootCategory);
			}
			else
			{
				EnableWindow(GetDlgItem(theDialog, IDOK), FALSE);
			}
		}
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
						GetDlgItem(theDialog, IDC_EDIT_READ_ONLY_TEXT);
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
				GetDlgItem(theDialog, IDC_EDIT_READ_ONLY_TEXT);
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
	}

	return FALSE;
}


static BOOL CALLBACK browseFolderShowSelectionCallback(
	HWND hWndChild, LPARAM lParam)
{
	// Used as a workaround for a Win32 bug where the folder browser
	// won't scroll the window to the initial selection folder
	WCHAR aBuffer[MAX_PATH];
	GetClassName(hWndChild, aBuffer, MAX_PATH);
	if( std::wstring(aBuffer) == L"SysTreeView32" )
	{
		HTREEITEM hNode = TreeView_GetSelection(hWndChild);
		TreeView_EnsureVisible(hWndChild, hNode);
		return FALSE; // stop enumerating
	}

	return TRUE; // continue enumerating
}


static int CALLBACK browseFolderDialogProc(
	HWND theDialog, UINT theMessage, LPARAM lParam, LPARAM lpData)
{
	switch(theMessage) 
	{ 
	case BFFM_INITIALIZED:
		if( lpData && ((WCHAR*)lpData)[0] != L'\0' )
		{// Initial selection has been requested
			SendMessage(theDialog, BFFM_SETSELECTION, TRUE, lpData);
			// Signal on next BFFM_SELCHANGED to scroll to selection
			((WCHAR*)lpData)[0] = 1; ((WCHAR*)lpData)[1] = L'\0';
		}
		break;
	case BFFM_SELCHANGED:
		if( lpData && ((WCHAR*)lpData)[0] == 1 )
		{// Force scroll to selection once only
			EnumChildWindows(theDialog, browseFolderShowSelectionCallback, 0);
			((WCHAR*)lpData)[0] = L'\0';
		}
		break;
	}

	return 0;
}


static bool getTargetGameExePath(
	HWND hWnd,
	std::wstring& theFolderPath,
	std::wstring& theFileName)
{
	static bool sWasWarnedAboutPatcher = false;
	if( !sWasWarnedAboutPatcher )
	{
		if( MessageBox(hWnd,
			L"You will need to select the game's main executable.\n\n"
			L"WARNING: This is likely NOT the same as the launcher/patcher "
			L"application and may be in a completely different location!",
			L"NOTICE",
			MB_OKCANCEL | MB_ICONINFORMATION) != IDOK )
		{
			return false;
		}
		sWasWarnedAboutPatcher = true;
	}

	OPENFILENAME ofn;
	WCHAR aWPath[MAX_PATH] = { 0 };
	if( !theFileName.empty() )
		wcsncpy(aWPath, theFileName.c_str(), MAX_PATH-1);
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrTitle = L"Select Game Executable";
	ofn.lpstrFile = aWPath;
	ofn.nMaxFile = sizeof(aWPath) / sizeof(aWPath[0]);
	ofn.lpstrFilter =
		L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	if( !theFolderPath.empty() )
		ofn.lpstrInitialDir = toAbsolutePath(theFolderPath).c_str();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	if( !GetOpenFileName(&ofn) )
		return false;

	theFolderPath = aWPath;
	std::wstring::size_type aLastSlash = theFolderPath.find_last_of(L"\\/");
	if( aLastSlash == std::wstring::npos || aLastSlash == 0 )
	{// Just the file (somehow?)
		theFileName = theFolderPath;
		theFolderPath.clear();
	}
	else if( theFolderPath[aLastSlash-1] == ':' )
	{// File in root directory (weird...)
		theFileName = theFolderPath.substr(aLastSlash+1);
		theFolderPath.resize(aLastSlash+1);
	}
	else
	{// Normal path + file
		theFileName = theFolderPath.substr(aLastSlash+1);
		theFolderPath.resize(aLastSlash);
	}

	return true;
}


static bool trySaveXInputFix(HWND hWnd, const XInputFixDialogData* theData)
{
	const std::wstring& aFolderPath =
		toAbsolutePath(theData->destFolder, true);
	if( !isValidFolderPath(aFolderPath) )
	{
		MessageBox(hWnd,
			L"Destination folder not found. "
			L"Enter a valid destination path and try again!",
			L"Invalid Path",
			MB_OK | MB_ICONWARNING);
		return false;
	}

	if( isSamePath(aFolderPath, getAppFolderW()) )
	{
		if( MessageBox(hWnd,
			L"Creating the files in this folder will block "
			L"this application itself from detecting gamepads!\n\n"
			L"Are you sure you wish to save to here?",
			L"Confirm potential conflict location",
			MB_YESNO) != IDYES )
		{
			return false;
		}
	}

	std::wstring aFilePath[2];
	aFilePath[0] = aFolderPath + L"xinput1_3.dll";
	aFilePath[1] = aFolderPath + L"xinput1_4.dll";

	bool fileAlreadyExists = false;
	for(int i = 0; i < ARRAYSIZE(aFilePath); ++i)
	{
		if( isValidFilePath(aFilePath[i]) )
		{
			fileAlreadyExists = true;
			break;
		}
	}

	if( fileAlreadyExists )
	{
		if( MessageBox(hWnd,
			L"File already exists - fix may have already been applied.\n\n"
			L"Would you like to overwrite the existing file?",
			L"Overwrite file?",
			MB_YESNO | MB_ICONWARNING) != IDYES )
		{
			return false;
		}
	}

	bool exportFailed = false;
	for(int i = 0; i < ARRAYSIZE(aFilePath); ++i)
	{
		if( !writeResourceToFile(
				theData->bitWidthIs64Bit
					? IDR_BINARY_XINPUT_DLL_64
					: IDR_BINARY_XINPUT_DLL_32,
				L"BINARY", aFilePath[i].c_str()) )
		{
			exportFailed = true;
			break;
		}
	}

	if( exportFailed )
	{
		MessageBox(hWnd,
			L"File saving failed! "
			L"Destination may be write-protected!\n\n"
			L"Suggestions:\n\n"
			L"1) Choose a different destination folder, then manually copy "
			L"the file to the folder containing the game's main .exe file.\n\n"
			L"2) Temporarily run this app as an administrator and try again "
			L"using the [Help] -> [Double Input Fix] menu option.",
			L"Sove error",
			MB_OK | MB_ICONERROR);
		return false;
	}

	MessageBox(hWnd,
		L"Fix successfully applied (xinput .dll stub files created)!\n\n"
		L"To confirm, exit the game and this app and run the game by itself "
		L"(i.e. WITHOUT this app running). Make sure the game does NOT "
		L"respond to any gamepad input, to avoid any \"double input\" issues "
		L"while using this app with the game.\n\n"
		L"The [Help] -> [Double Input Fix] menu option has more information.",
		L"Fix applied",
		MB_OK);

	return true;
}


static INT_PTR CALLBACK xInputFixDialogProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	XInputFixDialogData* theData = NULL;
	bool msgWasProcessed = false;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Initialize contents
		theData = (XInputFixDialogData*)(UINT_PTR)lParam;
		// Allow other messages to access theData later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theData);
		DBG_ASSERT(theData);
		{// Add prompts to path field
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_FILE_PATH);
			Edit_SetCueBannerText(hEditBox,
				L"Enter destination folder...");
			SendMessage(hEditBox, WM_SETTEXT, 0, (LPARAM)
				L"Use \"Auto-detect\" below and select game .exe file");
		}
		{// Copy rich text into the description box
			HWND hRichEdit = GetDlgItem(theDialog, IDC_EDIT_READ_ONLY_TEXT);
			DBG_ASSERT(hRichEdit);
			HRSRC hRes = FindResource(null, 
				MAKEINTRESOURCE(IDR_TEXT_XINPUT_FIX), L"TEXT");
			DBG_ASSERT(hRes);	
			HGLOBAL hData = LoadResource(null, hRes);
			DBG_ASSERT(hData);		
			const char* pRtfData = (const char*)LockResource(hData);
			DWORD dwSize = SizeofResource(null, hRes);
			EDITSTREAM es = { 0 };
			es.dwCookie = (DWORD_PTR)&pRtfData;
			es.pfnCallback = &rtfStreamCallback;
			SendMessage(hRichEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
		}	
		msgWasProcessed = true;
		break;

	case WM_COMMAND:
		theData = (XInputFixDialogData*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( !theData )
			break;
		switch(LOWORD(wParam))
		{
		case IDC_RADIO_32BIT:
			theData->bitWidthKnown = true;
			theData->bitWidthIs64Bit = false;
			msgWasProcessed = true;
			break;
		case IDC_RADIO_64BIT:
			theData->bitWidthKnown = true;
			theData->bitWidthIs64Bit = true;
			msgWasProcessed = true;
			break;

		case IDC_BUTTON_AUTO_DETECT:
			if( getTargetGameExePath(theDialog,
					theData->destFolder,
					theData->gameExeName) )
			{
				if( getExeArchitecture(
						toAbsolutePath(theData->destFolder, true) +
							theData->gameExeName,
						theData->bitWidthIs64Bit) )
				{
					theData->bitWidthKnown = true;
					CheckRadioButton(theDialog,
						IDC_RADIO_32BIT, IDC_RADIO_64BIT,
						theData->bitWidthIs64Bit
							? IDC_RADIO_64BIT : IDC_RADIO_32BIT);
				}
				else
				{
					MessageBox(theDialog,
						L"Could not detect bit width - please select manually",
						L"Bit width detecting failed",
						MB_OK | MB_ICONERROR);
				}
				if( theData->readyForPath )
				{
					SetDlgItemText(theDialog, IDC_EDIT_FILE_PATH,
						theData->destFolder.c_str());
				}
			}
			break;

		case IDC_EDIT_FILE_PATH:
			if( HIWORD(wParam) == EN_CHANGE && theData->readyForPath )
			{
				WCHAR aWPath[MAX_PATH] = { 0 };
				GetDlgItemText(theDialog, IDC_EDIT_FILE_PATH,
					aWPath, MAX_PATH);
				theData->destFolder = aWPath;
				if( !theData->destFolder.empty() )
					theData->pathEntered = true;
			}
			break;

		case IDC_BUTTON_BROWSE:
			{// Get path to save .dll file to
				WCHAR aWPath[MAX_PATH] = { 0 };
				DBG_ASSERT(theData->readyForPath);
				// Use existing path as default selection in picker
				wcsncpy(aWPath,
					toAbsolutePath(theData->destFolder).c_str(),
					MAX_PATH-1);
				BROWSEINFO bi = {0};
				bi.hwndOwner = theDialog;
				bi.lpszTitle = L"Select game folder";
				bi.lpfn = browseFolderDialogProc;
				bi.lParam = (LPARAM)aWPath;
				bi.ulFlags =
					BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
				if( LPITEMIDLIST pidl = SHBrowseForFolder(&bi) )
				{
					SHGetPathFromIDList(pidl, aWPath);
					SetDlgItemText(theDialog, IDC_EDIT_FILE_PATH, aWPath);
					CoTaskMemFree(pidl);
				}
			}
			break;

		case IDOK:
			DBG_ASSERT(theData->bitWidthKnown);
			DBG_ASSERT(theData->readyForExport);
			if( trySaveXInputFix(theDialog, theData) )
				EndDialog(theDialog, IDOK);
			msgWasProcessed = true;
			break;

		case IDCANCEL:
			EndDialog(theDialog, IDCANCEL);
			msgWasProcessed = true;
			break;
		}
		// Once have file type data set allow export
		if( !theData->readyForPath && theData->bitWidthKnown )
		{
			theData->readyForPath = true;
			SetDlgItemText(theDialog, IDC_EDIT_FILE_PATH,
				theData->destFolder.c_str());
			EnableWindow(GetDlgItem(theDialog, IDC_BUTTON_BROWSE), true);
			EnableWindow(GetDlgItem(theDialog, IDC_EDIT_FILE_PATH), true);
		}
		if( !theData->readyForExport && theData->pathEntered )
		{
			EnableWindow(GetDlgItem(theDialog, IDOK), true);
			theData->readyForExport = true;
		}
		break;
	}
	
	return msgWasProcessed ? TRUE : FALSE;
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


static void dialogCheckGamepad(HWND theDialog)
{
	Gamepad::update();

	const int aCancelID = GetDlgItem(theDialog, IDCANCEL) ? IDCANCEL : IDOK;
	if( GetForegroundWindow() != theDialog )
	{
		if( TargetApp::targetWindowIsFullScreen() &&
			TargetApp::targetWindowIsActive() )
		{
			// If switched from dialog to full-screen game it can be difficult
			// to regain normal controls, so just cancel the dialog out
			SendDlgItemMessage(theDialog, aCancelID, BM_CLICK, 0, 0);		
		}
		return;
	}

	HWND aFocusControl = GetFocus();
	UINT aFocusID =
		(aFocusControl  && IsChild(theDialog, aFocusControl))
			? GetDlgCtrlID(aFocusControl) : 0;
	INPUT input[2] = { 0 };
	input[0].type = INPUT_KEYBOARD;
	input[1].type = INPUT_KEYBOARD;
	input[1].ki.dwFlags = KEYEVENTF_KEYUP;

	// Check for cancel/back (B or Y on XB)
	if( Gamepad::buttonHit(eBtn_FRight) || Gamepad::buttonHit(eBtn_FUp) )
	{
		SendDlgItemMessage(theDialog, aCancelID, BM_CLICK, 0, 0);
		Gamepad::ignoreUntilPressedAgain(eBtn_FRight);
		return;
	}

	// Check for confirm (A or X on XB)
	if( Gamepad::buttonHit(eBtn_FDown) || Gamepad::buttonHit(eBtn_FLeft) )
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

	const bool needsToBeTopMost = TargetApp::targetWindowIsTopMost();
	const bool allowEditing = !firstRun && !needsToBeTopMost;
	ProfileSelectDialogData aDataStruct(
		theLoadableProfiles,
		theTemplateProfiles,
		result, firstRun, allowEditing);

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
	WindowManager::prepareForDialog();

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
	WindowManager::endDialogMode();
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);

	return result;
}


void profileEdit(const std::vector<std::string>& theFileList, bool firstRun)
{
	DBG_ASSERT(!theFileList.empty());

	// Initialize data structures
	ProfileSelectResult result;

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
	WindowManager::prepareForDialog();

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

	// Open main customize file automatically on first run
	if( firstRun )
	{
		ShellExecute(NULL, L"open",
			widen(theFileList.back()).c_str(),
			NULL, NULL, SW_SHOWNORMAL);
	}

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
	WindowManager::endDialogMode();
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);
}


size_t layoutItemSelect(const std::vector<TreeViewDialogItem*>& theList)
{
	DBG_ASSERT(!theList.empty());

	// Initialize data structures
	sDialogSelected = 0;
	const bool needsToBeTopMost = TargetApp::targetWindowIsTopMost();

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
	WindowManager::prepareForDialog();

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
	WindowManager::endDialogMode();
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);

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
			L"Would you like to automatically launch target game's "
			L"launcher/patcher when loading this profile at startup?",
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
		thePath = std::string("\"") + narrow(aWPath) + std::string("\"");
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


void showXInputFixDetails(HWND theParentWindow)
{
	TargetApp::prepareForDialog();
	InputDispatcher::forceReleaseHeldKeys();

	HMODULE hRichEdit = LoadLibrary(L"Riched20.dll");

	XInputFixDialogData aDataStruct = XInputFixDialogData();
	DialogBoxParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDD_DIALOG_XINPUT_DETAILED_FIX),
		theParentWindow,
		xInputFixDialogProc,
		reinterpret_cast<LPARAM>(&aDataStruct));

	mainLoopTimeSkip();

	FreeLibrary(hRichEdit);
}


EResult editMenuCommand(std::string& theString, bool directional)
{
	const std::string anOriginalString = theString;

	// Hide main window and overlays until dialog is done
	TargetApp::prepareForDialog();
	WindowManager::prepareForDialog();

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
		dialogCheckGamepad(hWnd);

		if( sDialogDone )
			break;

		mainLoopSleep();
	}

	// Cleanup
	WindowManager::endDialogMode();
	SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
	DestroyWindow(hWnd);

	return (theString != anOriginalString) ? eResult_Ok : eResult_Cancel;
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
