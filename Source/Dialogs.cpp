//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "Dialogs.h"

#include "Gamepad.h"
#include "InputDispatcher.h" // forceReleaseHeldKeys()
#include "Profile.h" // getKnownIssuesRTF()
#include "Resources/resource.h"
#include "TargetApp.h"
#include "WindowManager.h"

// Enable support for Edit_SetCueBannerText and LoadIconMetric
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

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum EDialogLayout
{
	eDialogLayout_Basic,
	eDialogLayout_ProfileSelect,
	eDialogLayout_CharacterSelect,
};

enum EMsgBoxType
{
	eMsgBoxType_Notice,
	eMsgBoxType_Warning,
	eMsgBoxType_YesNo,
};

struct MsgBoxData
{
	EMsgBoxType type;
	std::wstring text;
	std::wstring title;
};

struct ProfileSelectDialogData
{
	const std::vector<std::string>& loadableProfiles;
	const std::vector<std::string>& templateProfiles;
	ProfileSelectResult& result;
	bool initialProfile;

	ProfileSelectDialogData(
		const std::vector<std::string>& loadable,
		const std::vector<std::string>& templates,
		ProfileSelectResult& res,
		bool initialProfile)
		:
		loadableProfiles(loadable),
		templateProfiles(templates),
		result(res),
		initialProfile(initialProfile)
		{}
};

struct CharacterSelectDialogData
{
	const std::vector<std::wstring>& characterNames;
	CharacterSelectResult& result;

	CharacterSelectDialogData(
		const std::vector<std::wstring>& characterNames,
		CharacterSelectResult& result)
		:
		characterNames(characterNames),
		result(result)
		{}
};

struct ZERO_INIT(RTF_StreamData)
{
	const char* buffer;
	LONG remaining;
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static std::string* sDialogEditText;
static int sDialogSelected = 0;
static bool sDialogDone = false;
static bool sDialogFocusShown = false;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static void setDialogFocus(HWND hdlg, HWND hwndControl)
{
	SendMessage(hdlg, WM_NEXTDLGCTL, (WPARAM)hwndControl, TRUE);
}


static void setDialogFocus(HWND hdlg, int theID)
{
	setDialogFocus(hdlg, GetDlgItem(hdlg, theID));
}


static INT_PTR CALLBACK defaultDialogProc(
	HWND theDialog, UINT theMessage, WPARAM, LPARAM)
{
	switch(theMessage)
	{
	case WM_DEVICECHANGE:
		Gamepad::checkDeviceChange();
		break;
	}

	return (INT_PTR)FALSE;
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
	{
		theDefaultSelectedIdx = dropTo<int>(
			SendMessage(hListBox, LB_GETCURSEL, 0, 0));
	}

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
		const std::wstring& aWStr = widen((*aStrVec)[i]);
		SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)(aWStr.c_str()));
	}

	// Restore initially selected item (or closest possible)
	if( theDefaultSelectedIdx >= intSize(aStrVec->size()) )
		theDefaultSelectedIdx = intSize(aStrVec->size()) - 1;
	if( theDefaultSelectedIdx < 0 && !aStrVec->empty() )
		theDefaultSelectedIdx = 0;
	SendMessage(hListBox, LB_SETCURSEL, theDefaultSelectedIdx, 0);
}


static INT_PTR CALLBACK profileSelectProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	ProfileSelectDialogData* theData = NULL;
	bool targetWindowIsTopMost;

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
		targetWindowIsTopMost = TargetApp::targetWindowIsTopMost();
		if( targetWindowIsTopMost )
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
			!theData->initialProfile && !targetWindowIsTopMost);
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
				theData->result.selectedIndex = max(0, dropTo<int>(
					SendMessage(hListBox, LB_GETCURSEL, 0, 0)));
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
					&aWStrBuffer[0], intSize(aWStrBuffer.size()));
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
						!theData->initialProfile && !hasNewName);
				}
				return (INT_PTR)TRUE;
			}
			break;
		}
		break;
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
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
			for(int i = 0, end = intSize(theFiles->size()); i < end; ++i)
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
				int aFile = max(0, dropTo<int>(
					SendMessage(hListBox, LB_GETCURSEL, 0, 0)));
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
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
}


static INT_PTR CALLBACK characterSelectProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	CharacterSelectDialogData* theData = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Initialize contents
		theData = (CharacterSelectDialogData*)(UINT_PTR)lParam;
		// Allow other messages to access theData later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theData);
		DBG_ASSERT(theData);
		// Populate the list box with alphabetically sorted character names,
		// and store each item's original index as item data.
		{
			HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS);
			DBG_ASSERT(hListBox);

			for(int i = 0, end = intSize(theData->characterNames.size());
				i < end; ++i)
			{
				int aSortedIdx = (int)SendMessage(
					hListBox, LB_ADDSTRING, 0,
					(LPARAM)theData->characterNames[i].c_str());
				DBG_ASSERT(aSortedIdx != LB_ERR);
				// Store origial index
				SendMessage(hListBox, LB_SETITEMDATA, aSortedIdx, (LPARAM)i);
			}

			// Select the item corresponding to the stored original index
			if( size_t(theData->result.selectedIndex) >=
					theData->characterNames.size() )
			{// Invalid initial index - just select first item
				SendMessage(hListBox, LB_SETCURSEL, 0, 0);
			}
			else
			{
				for(int i = 0, end = intSize(theData->characterNames.size());
					i < end; ++i)
				{
					int aStoredIndex = dropTo<int>(
						SendMessage(hListBox, LB_GETITEMDATA, i, 0));
					if( aStoredIndex == theData->result.selectedIndex )
					{
						SendMessage(hListBox, LB_SETCURSEL, i, 0);
						break;
					}
				}
			}
		}
		// Set initial value of auto-select checkbox
		CheckDlgButton(theDialog, IDC_CHECK_AUTOLOAD,
			theData->result.autoSelectRequested);
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		theData = (CharacterSelectDialogData*)(UINT_PTR)
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
				HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS);
				DBG_ASSERT(hListBox);
				const int aListSelIdx = max(0, dropTo<int>(
					SendMessage(hListBox, LB_GETCURSEL, 0, 0)));
				theData->result.selectedIndex = clamp(dropTo<int>(
					SendMessage(hListBox, LB_GETITEMDATA, aListSelIdx, 0)),
					0, intSize(theData->characterNames.size())-1);
				// Add auto-select checkbox status to result
				theData->result.autoSelectRequested =
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
		}
		break;
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
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
	int aName1Val, aName2Val, aDummyVal;
	std::string aName1, aName2;
	fetchRangeSuffix(condense(anItem1->name), aName1, aName1Val, aDummyVal);
	fetchRangeSuffix(condense(anItem2->name), aName2, aName2Val, aDummyVal);
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
			TVINSERTSTRUCT tvInsert = {};
			tvInsert.hInsertAfter = TVI_LAST;
			std::vector<HTREEITEM> aHandlesList(theItems->size());
			BitVector<256> anItemHasChildren(theItems->size());
			BitVector<256> anItemWasAdded(theItems->size());
			for(int i = 1, end = intSize(theItems->size()); i < end; ++i)
				anItemHasChildren.set((*theItems)[i]->parentIndex);
			aHandlesList[0] = TVI_ROOT;
			anItemWasAdded.set(0);
			while(!anItemWasAdded.all())
			{
				for(int i = 1, end = intSize(theItems->size()); i < end; ++i)
				{
					DBG_ASSERT((*theItems)[i] != null);
					if( anItemWasAdded.test(i) )
						continue;
					const std::wstring& anItemName =
						widen((*theItems)[i]->name);
					const int aParentIdx = (*theItems)[i]->parentIndex;
					if( !anItemWasAdded.test(aParentIdx) )
						continue;
					tvInsert.item.pszText =
						const_cast<WCHAR*>(anItemName.c_str());
					tvInsert.item.mask = TVIF_TEXT | TVIF_PARAM;
					tvInsert.item.cChildren =
						anItemHasChildren.test(i) ? 1 : 0;
					if( tvInsert.item.cChildren )
						tvInsert.item.mask |= TVIF_CHILDREN;
					tvInsert.item.lParam = MAKELPARAM(
						dropTo<WORD>(i),
						WORD((*theItems)[i]->isRootCategory));
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
			{// Start with requested initial selection
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
					sDialogSelected = LOWORD(tvItem.lParam);
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
				const bool isRootCategory = HIWORD(tvItem.lParam) != 0;
				EnableWindow(GetDlgItem(theDialog, IDOK), !isRootCategory);
			}
			else
			{
				EnableWindow(GetDlgItem(theDialog, IDOK), FALSE);
			}
		}
		return (INT_PTR)TRUE;
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
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

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
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
		return (INT_PTR)TRUE;

	case WM_TIMER:
		if( wParam == 1 )
		{
			KillTimer(theDialog, 1);
			HWND hEditControl =
				GetDlgItem(theDialog, IDC_EDIT_READ_ONLY_TEXT);
			InvalidateRect(hEditControl, NULL, TRUE);
			UpdateWindow(hEditControl);
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK: // Accept button
			sDialogSelected = 1;
			sDialogDone = true;
			return (INT_PTR)TRUE;
		case IDCANCEL: // Decline button
			sDialogSelected = 0;
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
}


static DWORD CALLBACK rtfStreamCallback(
	DWORD_PTR theCookie,
	LPBYTE theBuffer,
	LONG theBytesToWrite,
	LONG* theBytesWritten)
{
	RTF_StreamData* theData = (RTF_StreamData*)theCookie;
	DBG_ASSERT(theData);

	LONG aCopyByteCount = min(theBytesToWrite, theData->remaining);
	if( aCopyByteCount > 0 )
	{
		memcpy(theBuffer, theData->buffer, aCopyByteCount);
		theData->buffer += aCopyByteCount;
		theData->remaining -= aCopyByteCount;
	}
	*theBytesWritten = aCopyByteCount;
	return 0;
}


static INT_PTR CALLBACK knownIssuesDialogProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_INITDIALOG:
		{// Initialize contents
			HWND hRichEdit = GetDlgItem(theDialog, IDC_EDIT_READ_ONLY_TEXT);
			SendMessage(hRichEdit, EM_AUTOURLDETECT, TRUE, 0);
			RTF_StreamData aRTF_StreamData;
			const std::string& aStr = Profile::getKnownIssuesRTF();
			aRTF_StreamData.buffer = aStr.c_str();
			aRTF_StreamData.remaining = dropTo<LONG>(aStr.size()+1);
			EDITSTREAM es = { 0 };
			es.dwCookie = (DWORD_PTR)&aRTF_StreamData;
			es.pfnCallback = &rtfStreamCallback;
			SendMessage(hRichEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
			SendMessage(hRichEdit, EM_SETEVENTMASK, 0, ENM_LINK);
		}
		return (INT_PTR)TRUE;

	case WM_NOTIFY:
		{// Make URL's open browser and follow link when clicked
			NMHDR* pNMHDR = (NMHDR*)lParam;
			if( pNMHDR->code == EN_LINK )
			{
				ENLINK* pENLink = (ENLINK*)lParam;
				if( pENLink->msg == WM_LBUTTONDOWN )
				{
					// Get the clicked text range
					CHAR szUrl[512] = {0};
					TEXTRANGEA tr;
					tr.chrg = pENLink->chrg;
					tr.lpstrText = szUrl;

					HWND hRichEdit =
						GetDlgItem(theDialog, IDC_EDIT_READ_ONLY_TEXT);
					SendMessage(hRichEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

					// Launch browser
					ShellExecute(NULL, TEXT("open"),
						widen(szUrl).c_str(), NULL, NULL, SW_SHOWNORMAL);
				}
			}
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
		case IDCANCEL:
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
}


static INT_PTR CALLBACK editMenuCommandProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	std::pair<const std::string*, std::string*>* theData = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Initialize contents
		theData = (std::pair<const std::string*, std::string*>*)(UINT_PTR)
			lParam;
		// Allow other messages to access theData later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theData);
		DBG_ASSERT(theData);
		{// Set caption of dialog box
			SetWindowText(theDialog, widen(*theData->first).c_str());
		}
		{// Set initial string in Edit box, and select it
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_COMMAND);
			SetDlgItemText(theDialog, IDC_EDIT_COMMAND,
				widen(*theData->second).c_str());
			SendMessage(hEditBox, EM_SETSEL, 0, -1);
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		// Process control commands
		theData = (std::pair<const std::string*, std::string*>*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( !theData )
			break;
		if( LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED )
		{// Okay button clicked - update string to edit box contents
			HWND hEditBox = GetDlgItem(theDialog, IDC_EDIT_COMMAND);
			std::vector<WCHAR> aWStrBuffer(GetWindowTextLength(hEditBox)+1);
			GetDlgItemText(theDialog, IDC_EDIT_COMMAND,
				&aWStrBuffer[0], int(aWStrBuffer.size()));
			*(theData->second) = trim(narrow(&aWStrBuffer[0]));
			// Signal to application that are ready to close
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		if( LOWORD(wParam) == IDCANCEL && HIWORD(wParam) == BN_CLICKED )
		{// Cancel button clicked - just close without updating string
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
}


static INT_PTR CALLBACK msgBoxProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	MsgBoxData* theData = NULL;

	switch(theMessage)
	{
	case WM_INITDIALOG:
		// Initialize contents
		theData = (MsgBoxData*)(UINT_PTR)lParam;
		// Allow other messages to access theData later
		SetWindowLongPtr(theDialog, GWLP_USERDATA, (LONG_PTR)theData);
		DBG_ASSERT(theData);
		SetWindowText(theDialog, theData->title.c_str());
		SetDlgItemText(theDialog, IDC_STATIC_PROMPT, theData->text.c_str());
		// Set up icon
		if( HWND hIconCtrl = GetDlgItem(theDialog, IDC_PROMPT_ICON) )
		{
			if( theData->type == eMsgBoxType_Warning )
			{
				HICON hIcon = NULL;
				LoadIconMetric(NULL, IDI_WARNING, LIM_LARGE, &hIcon);
				SendMessage(hIconCtrl, STM_SETICON, (WPARAM)hIcon, 0);
				ShowWindow(hIconCtrl, SW_SHOW);
			}
			else
			{
				ShowWindow(hIconCtrl, SW_HIDE);
			}
		}
		// Reshape according to text string render size & icon visibility
		if( HWND hTextCtrl = GetDlgItem(theDialog, IDC_STATIC_PROMPT) )
		{
			RECT aCtrlRect, aTextRect;
			GetWindowRect(hTextCtrl, &aCtrlRect);
			ScreenToClient(theDialog, (LPPOINT)&aCtrlRect.left);
			ScreenToClient(theDialog, (LPPOINT)&aCtrlRect.right);
			if( theData->type != eMsgBoxType_Warning )
			{// Apply right margin to the left margin to overlap icon area
				RECT aDialogRect;
				GetClientRect(theDialog, &aDialogRect);
				aCtrlRect.left = aDialogRect.right - aCtrlRect.right;
			}
			HFONT hFont = (HFONT)SendMessage(theDialog, WM_GETFONT, 0, 0);
			HDC hdc = GetDC(theDialog);
			SelectObject(hdc, hFont);
			aTextRect = aCtrlRect;
			DrawText(hdc, theData->text.c_str(), -1, &aTextRect,
				DT_CALCRECT | DT_WORDBREAK);
			if( aTextRect.bottom < aCtrlRect.bottom )
			{// Center text vertically by moving top edge down
				aCtrlRect.top += (aCtrlRect.bottom - aTextRect.bottom) / 2;
				aCtrlRect.bottom = aCtrlRect.top + aTextRect.bottom - aTextRect.top;
			}
			else if( aTextRect.bottom > aCtrlRect.bottom )
			{// Extend everything down - including overall dialog height!
				const LONG aDelta = aTextRect.bottom - aCtrlRect.bottom;
				aCtrlRect.bottom = aTextRect.bottom;
				RECT aRect;
				GetWindowRect(theDialog, &aRect);
				SetWindowPos(theDialog, NULL, 0, 0,
					aRect.right - aRect.left,
					aRect.bottom - aRect.top + aDelta,
					SWP_NOMOVE | SWP_NOZORDER);
				if( HWND hBtn = GetDlgItem(theDialog, IDCANCEL) )
				{
					GetWindowRect(hBtn, &aRect);
					ScreenToClient(theDialog, (LPPOINT)&aRect.left);
					SetWindowPos(hBtn, NULL, aRect.left, aRect.top + aDelta,
						0, 0, SWP_NOSIZE | SWP_NOZORDER); 
				}
				if( HWND hBtn = GetDlgItem(theDialog, IDOK) )
				{
					GetWindowRect(hBtn, &aRect);
					ScreenToClient(theDialog, (LPPOINT)&aRect.left);
					SetWindowPos(hBtn, NULL, aRect.left, aRect.top + aDelta,
						0, 0, SWP_NOSIZE | SWP_NOZORDER); 
				}
			}
			// Can't shrink the width of the text TOO much or have no buttons!
			const LONG aMinTextWidth = (aCtrlRect.right - aCtrlRect.left) / 2;
			if( (aTextRect.right - aTextRect.left) < aMinTextWidth )
				aTextRect.right = aTextRect.left + aMinTextWidth;
			if( aTextRect.right != aCtrlRect.right )
			{// Adjust width of the dialog to match needed width for text
				const LONG aDelta = aTextRect.right - aCtrlRect.right;
				aCtrlRect.right = aTextRect.right;
				RECT aRect;
				GetWindowRect(theDialog, &aRect);
				SetWindowPos(theDialog, NULL, 0, 0,
					aRect.right - aRect.left + aDelta,
					aRect.bottom - aRect.top,
					SWP_NOMOVE | SWP_NOZORDER);
				if( HWND hBtn = GetDlgItem(theDialog, IDCANCEL) )
				{
					GetWindowRect(hBtn, &aRect);
					ScreenToClient(theDialog, (LPPOINT)&aRect.left);
					SetWindowPos(hBtn, NULL, aRect.left + aDelta, aRect.top,
						0, 0, SWP_NOSIZE | SWP_NOZORDER); 
				}
				if( HWND hBtn = GetDlgItem(theDialog, IDOK) )
				{
					GetWindowRect(hBtn, &aRect);
					ScreenToClient(theDialog, (LPPOINT)&aRect.left);
					SetWindowPos(hBtn, NULL, aRect.left + aDelta, aRect.top,
						0, 0, SWP_NOSIZE | SWP_NOZORDER); 
				}				
			}
			SetWindowPos(hTextCtrl, NULL,
				aCtrlRect.left, aCtrlRect.top,
				aCtrlRect.right - aCtrlRect.left,
				aCtrlRect.bottom - aCtrlRect.top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// Adjust buttons depending on type
		switch(theData->type)
		{
		case eMsgBoxType_Notice:
		case eMsgBoxType_Warning:
			if( HWND hCancelBtn = GetDlgItem(theDialog, IDCANCEL) )
			{// Hide cancel button
				ShowWindow(GetDlgItem(theDialog, IDCANCEL), SW_HIDE);
				if( HWND hOkBtn = GetDlgItem(theDialog, IDOK) )
				{// Move OK button to where Cancel button was
					RECT aRect;
					GetWindowRect(hCancelBtn, &aRect);
					ScreenToClient(theDialog, (LPPOINT)&aRect.left);
					SetWindowPos(hOkBtn, NULL, aRect.left, aRect.top,
						0, 0, SWP_NOSIZE | SWP_NOZORDER);
				}
			}
			break;
		case eMsgBoxType_YesNo:
			SetDlgItemText(theDialog, IDCANCEL, L"No");
			SetDlgItemText(theDialog, IDOK, L"Yes");
			break;
		}
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			sDialogSelected = 1;
			sDialogDone = true;
			return (INT_PTR)TRUE;
		case IDCANCEL:
			sDialogSelected = 0;
			sDialogDone = true;
			return (INT_PTR)TRUE;
		}
		break;

	case WM_CTLCOLORSTATIC:
		if( (HWND)lParam == GetDlgItem(theDialog, IDC_PROMPT_ICON) ||
			(HWND)lParam == GetDlgItem(theDialog, IDC_STATIC_PROMPT) )
		{
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
		}
		break;

	case WM_ERASEBKGND:
		theData = (MsgBoxData*)(UINT_PTR)
			GetWindowLongPtr(theDialog, GWLP_USERDATA);
		if( theData )
		{
			RECT aDlgRect, anIconRect, aTextRect, aDstRect;
			GetClientRect(theDialog, &aDlgRect);
			GetWindowRect(GetDlgItem(theDialog, IDC_PROMPT_ICON), &anIconRect);
			ScreenToClient(theDialog, (LPPOINT)&anIconRect.left);
			ScreenToClient(theDialog, (LPPOINT)&anIconRect.right);
			GetWindowRect(GetDlgItem(theDialog, IDC_STATIC_PROMPT), &aTextRect);
			ScreenToClient(theDialog, (LPPOINT)&aTextRect.right);
			aDstRect = aDlgRect;
			aDstRect.bottom = max(anIconRect.bottom, aTextRect.bottom);
			aDstRect.bottom += anIconRect.top; // match top margin for bottom
			FillRect((HDC)wParam, &aDlgRect, GetSysColorBrush(COLOR_BTNFACE));
			FillRect((HDC)wParam, &aDstRect, GetSysColorBrush(COLOR_WINDOW)); 
		}
		return TRUE; 
	}

	return defaultDialogProc(theDialog, theMessage, wParam, lParam);
}


static void dialogCheckEnvironment(HWND theDialog)
{
	if( WindowManager::mainHandle() && !TargetApp::targetWindowHandle() )
	{
		TargetApp::update();
		if( TargetApp::targetWindowIsActive() )
		{
			// If target app window popped up during a dialog (after initial
			// profile load with main window active), cancel the dialog to
			// so overlays can activate over the target app and controls work
			SendMessage(theDialog, WM_COMMAND, IDCANCEL, 0);
		}
	}
	else if( GetForegroundWindow() == TargetApp::targetWindowHandle() )
	{
		if( TargetApp::targetWindowIsFullScreen() )
		{
			// If switched from dialog to full-screen game it can be difficult
			// to regain normal controls, so just cancel the dialog out
			SendMessage(theDialog, WM_COMMAND, IDCANCEL, 0);
		}
		else
		{
			// Otherwise pop our dialog in front of the target window for now,
			// as if the dialog is coming from the game itself basically
			SetForegroundWindow(theDialog);
			FLASHWINFO aFlashInfo =
				{ sizeof(FLASHWINFO), theDialog, FLASHW_CAPTION, 5, 60 };
			FlashWindowEx(&aFlashInfo);
		}
	}
}


static void dialogCheckGamepad(HWND theDialog, EDialogLayout theLayout)
{
	Gamepad::update();

	if( GetForegroundWindow() != theDialog )
		return;

	HWND aFocusControl = GetFocus();
	UINT aFocusID =
		(aFocusControl && IsChild(theDialog, aFocusControl))
			? GetDlgCtrlID(aFocusControl) : 0;
	INPUT input[4] = { 0 };
	input[0].type = INPUT_KEYBOARD;
	input[1].type = INPUT_KEYBOARD;
	input[2].type = INPUT_KEYBOARD;
	input[2].ki.dwFlags = KEYEVENTF_KEYUP;
	input[3].type = INPUT_KEYBOARD;
	input[3].ki.dwFlags = KEYEVENTF_KEYUP;

	// Check for cancel/back (B or Y on XB)
	if( Gamepad::buttonHit(eBtn_FRight) || Gamepad::buttonHit(eBtn_FUp) )
	{
		SendMessage(theDialog, WM_COMMAND, IDCANCEL, 0);
		Gamepad::ignoreUntilPressedAgain(eBtn_FRight);
		return;
	}

	// Check for confirm (A or X on XB)
	if( Gamepad::buttonHit(eBtn_FDown) || Gamepad::buttonHit(eBtn_FLeft) )
	{
		switch(aFocusID)
		{
		case IDC_EDIT_PROFILE_NAME:
			if( theLayout == eDialogLayout_ProfileSelect )
			{
				setDialogFocus(theDialog, IDC_LIST_ITEMS);
				break;
			}
			// fall through
		default:
			{// Checkboxes etc should use SPACE instead of RETURN
				WCHAR aClassName[32];
				GetClassName(aFocusControl, aClassName, 32);

				if( wcscmp(aClassName, L"Button") == 0 )
				{
					input[1].ki.wVk = input[2].ki.wVk = VK_SPACE;
					SendInput(2, &input[1], sizeof(INPUT));
					break;
				}
			}
			input[1].ki.wVk = input[2].ki.wVk = VK_RETURN;
			SendInput(2, &input[1], sizeof(INPUT));
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

	// Layout-specific overrides to just sending arrow keys
	if( theLayout == eDialogLayout_ProfileSelect )
	{
		switch(aNewDir)
		{
		case eDialogDir_Left:
			switch(aFocusID)
			{
			case IDC_LIST_ITEMS:
			case IDC_EDIT_PROFILE_NAME:
				return;
			default:
				setDialogFocus(theDialog, IDC_LIST_ITEMS);
				return;
			}
			break;
		case eDialogDir_Right:
			switch(aFocusID)
			{
			case IDC_LIST_ITEMS:
				setDialogFocus(theDialog, IDOK);
				return;
			case IDC_EDIT_PROFILE_NAME:
				setDialogFocus(theDialog, IDC_CHECK_AUTOLOAD);
				return;
			}
			return;
		case eDialogDir_Up:
			switch(aFocusID)
			{
			case IDOK:
				return;
			case IDC_EDIT_PROFILE_NAME:
				setDialogFocus(theDialog, IDC_LIST_ITEMS);
				return;
			}
		}
	}
	else if( theLayout == eDialogLayout_CharacterSelect )
	{
		switch(aNewDir)
		{
		case eDialogDir_Left:
		case eDialogDir_Down:
			if( aFocusID == IDC_CHECK_AUTOLOAD )
				return;
			break;
		case eDialogDir_Up:
			if( aFocusID != IDC_LIST_ITEMS )
			{
				setDialogFocus(theDialog, IDC_LIST_ITEMS);
				return;
			}
			break;
		}
	}
	if( aNewDir == eDialogDir_Down &&
		aFocusID == IDC_LIST_ITEMS &&
		theLayout != eDialogLayout_Basic )
	{// Check for pushing down while at end of list
		if( HWND hListBox = GetDlgItem(theDialog, IDC_LIST_ITEMS) )
		{
			const int aListSize = dropTo<int>(
				SendMessage(hListBox, LB_GETCOUNT, 0, 0));
			const int aListSelIdx = dropTo<int>(
				SendMessage(hListBox, LB_GETCURSEL, 0, 0));
			if( aListSize == 0 || aListSelIdx >= aListSize - 1 )
			{
				switch(theLayout)
				{
				case eDialogLayout_ProfileSelect:
					input[0].ki.wVk = input[3].ki.wVk = VK_SHIFT;
					input[1].ki.wVk = input[2].ki.wVk = VK_TAB;
					SendInput(4, &input[0], sizeof(INPUT));
					return;
				case eDialogLayout_CharacterSelect:
					setDialogFocus(theDialog, IDC_CHECK_AUTOLOAD);
					return;
				}
			}
		}
	}

	// Basic direction = arrow key input
	switch(aNewDir)
	{
	case eDialogDir_Left:
		if( aFocusID != IDC_LIST_ITEMS )
		{
			input[1].ki.wVk = input[2].ki.wVk = VK_LEFT;
			SendInput(2, &input[1], sizeof(INPUT));
		}
		break;
	case eDialogDir_Right:
		if( aFocusID != IDC_LIST_ITEMS )
		{
			input[1].ki.wVk = input[2].ki.wVk = VK_RIGHT;
			SendInput(2, &input[1], sizeof(INPUT));
		}
		break;
	case eDialogDir_Up:
		input[1].ki.wVk = input[2].ki.wVk = VK_UP;
		SendInput(2, &input[1], sizeof(INPUT));
		break;
	case eDialogDir_Down:
		input[1].ki.wVk = input[2].ki.wVk = VK_DOWN;
		SendInput(2, &input[1], sizeof(INPUT));
		break;
	}
}


static HWND openDialog(
	int theResID,
	DLGPROC theProc,
	LPARAM theLParam)
{
	// If have target window active, prepare to switch back to it when done
	TargetApp::prepareForDialog();

	// Release any keys held by InputDispatcher
	InputDispatcher::forceReleaseHeldKeys();

	// Create dialog window
	HWND result = CreateDialogParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(theResID),
		WindowManager::parentWindowHandle(),
		theProc,
		theLParam);

	// Disable main window and hide overlays
	WindowManager::beginDialog(result);

	// Display dialog window
	ShowWindow(result, SW_SHOW);

	// Move dialog to top-most if need to be above target
	if( TargetApp::targetWindowIsTopMost() )
		SetWindowPos(result, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	SetForegroundWindow(result);

	return result;
}


void runDialog(HWND theDialog, EDialogLayout theLayout)
{
	// Loop until dialog signals it is done
	sDialogDone = false;
	sDialogFocusShown = false;
	while(!gShutdown && !hadFatalError())
	{
		mainLoopUpdate(theDialog);
		dialogCheckEnvironment(theDialog);
		if( !sDialogDone )
			dialogCheckGamepad(theDialog, theLayout);

		if( sDialogDone )
			break;

		mainLoopSleep();
	}

	// Cleanup
	WindowManager::endDialog();
	SetWindowLongPtr(theDialog, GWLP_USERDATA, NULL);
	DestroyWindow(theDialog);	
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

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

	ProfileSelectDialogData aDataStruct(
		theLoadableProfiles,
		theTemplateProfiles,
		result, firstRun);

	runDialog(openDialog(
		IDD_DIALOG_PROFILE_SELECT,
		profileSelectProc,
		reinterpret_cast<LPARAM>(&aDataStruct)),
		eDialogLayout_ProfileSelect);

	return result;
}


void profileEdit(const std::vector<std::string>& theFileList, bool firstRun)
{
	DBG_ASSERT(!theFileList.empty());

	sDialogSelected = 0;
	HWND aDialog = openDialog(
		IDD_DIALOG_PROFILE_EDIT,
		editProfileSelectProc,
		reinterpret_cast<LPARAM>(&theFileList));

	// Open main customize file automatically on first run
	if( firstRun )
	{
		ShellExecute(NULL, L"open",
					widen(theFileList.back()).c_str(),
					NULL, NULL, SW_SHOWNORMAL);
	}

	runDialog(aDialog, eDialogLayout_Basic);
}



CharacterSelectResult characterSelect(
	const std::vector<std::wstring>& theFoundCharacters,
	int theDefaultSelection, bool wantsAutoSelect)
{
	DBG_ASSERT(!theFoundCharacters.empty());

	// Initialize data structures
	CharacterSelectResult result;
	result.selectedIndex = theDefaultSelection;
	result.autoSelectRequested = wantsAutoSelect;
	result.cancelled = true;

	CharacterSelectDialogData aDataStruct(theFoundCharacters, result);

	runDialog(openDialog(
		IDD_DIALOG_CHARACTER_SELECT,
		characterSelectProc,
		reinterpret_cast<LPARAM>(&aDataStruct)),
		eDialogLayout_CharacterSelect);

	return result;
}


int layoutItemSelect(const std::vector<TreeViewDialogItem*>& theList)
{
	DBG_ASSERT(!theList.empty());

	sDialogSelected = 0;

	runDialog(openDialog(IDD_DIALOG_LAYOUT_SELECT,
		layoutItemSelectProc,
		reinterpret_cast<LPARAM>(&theList)),
		eDialogLayout_Basic);

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

	if( yesNoPrompt(
			"Would you like to automatically launch target game's "
			"launcher/patcher when loading this profile at startup?",
			"Auto-Launch Target App") != eResult_Yes )
	{
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
	ofn.hwndOwner = WindowManager::parentWindowHandle();
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
		mainLoopTimeSkip();
		thePath = std::string("\"") + narrow(aWPath) + std::string("\"");
	}
	else
	{
		mainLoopTimeSkip();
		showNotice(
			"No target app path selected.\n"
			"You can manually edit the .ini file [System]AutoLaunchApp= "
			"entry to add one later.",
			"Auto-Launch Target App");
		thePath.clear();
	}

	// Cleanup
	sDialogEditText = NULL;
}


EResult showLicenseAgreement()
{
	sDialogSelected = 0;
	runDialog(
		openDialog(IDD_DIALOG_LICENSE, licenseDialogProc, 0),
		eDialogLayout_Basic);

	return sDialogSelected ? eResult_Accepted : eResult_Declined;
}


void showHelpDocuments()
{
	if( yesNoPrompt(
		"Documentation and source code for this app is hosted on GitHub. "
		"Would you like to request your default browser open "
		"https://onecrazycoder.github.io/MMOGamepadOverlay/ now?",
		"Documentation") == eResult_Yes )
	{
		ShellExecute(NULL, TEXT("open"),
			L"https://onecrazycoder.github.io/MMOGamepadOverlay/",
			NULL, NULL, SW_SHOWNORMAL);
	}
}


void showKnownIssues()
{
	HMODULE hRichEdit = LoadLibrary(L"Riched20.dll");

	runDialog(
		openDialog(IDD_DIALOG_KNOWN_ISSUES, knownIssuesDialogProc, 0),
		eDialogLayout_Basic);

	FreeLibrary(hRichEdit);
}


EResult editMenuCommand(
	const std::string& theLabel,
	std::string& theString,
	bool allowInsert)
{
	const std::string anOriginalString = theString;
	std::pair<const std::string*, std::string*> data(&theLabel, &theString);

	runDialog(openDialog(
		allowInsert
			? IDD_DIALOG_EDIT_MOVE_COMMAND
			: IDD_DIALOG_EDIT_COMMAND,
		editMenuCommandProc,
		reinterpret_cast<LPARAM>(&data)),
		eDialogLayout_Basic);

	return (theString != anOriginalString) ? eResult_Ok : eResult_Cancel;
}


void showError(const std::string& theError)
{
	logToFile(theError.c_str());

	MsgBoxData data;
	data.type = eMsgBoxType_Warning;
	data.title = L"Error";
	data.text = widen(theError);

	runDialog(openDialog(
		IDD_DIALOG_MSGBOX,
		msgBoxProc,
		reinterpret_cast<LPARAM>(&data)),
		eDialogLayout_Basic);
}


void showNotice(
	const std::string& theNotice,
	const std::string& theTitle)
{
	MsgBoxData data;
	data.type = eMsgBoxType_Notice;
	data.title = widen(theTitle);
	data.text = widen(theNotice);

	runDialog(openDialog(
		IDD_DIALOG_MSGBOX,
		msgBoxProc,
		reinterpret_cast<LPARAM>(&data)),
		eDialogLayout_Basic);
}


EResult yesNoPrompt(
	const std::string& thePrompt,
	const std::string& theTitle,
	bool skipIfTargetAppRunning)
{
	if( skipIfTargetAppRunning && TargetApp::targetAppActive() )
		return eResult_Cancel;

	MsgBoxData data;
	data.type = eMsgBoxType_YesNo;
	data.title = widen(theTitle);
	data.text = widen(thePrompt);
	sDialogSelected = 0;

	runDialog(openDialog(
		IDD_DIALOG_MSGBOX,
		msgBoxProc,
		reinterpret_cast<LPARAM>(&data)),
		eDialogLayout_Basic);

	return sDialogSelected ? eResult_Yes : eResult_No;
}

} // Dialogs
