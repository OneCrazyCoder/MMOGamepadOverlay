//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "WindowManager.h"

#include "Dialogs.h"
#include "InputMap.h"
#include "LayoutEditor.h"
#include "Menus.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "TargetConfigSync.h"
#include "TargetApp.h" // targetWindowHandle()
#include "WindowPainter.h"

// Forward declares of functions defined in Main.cpp for updating in modal mode
void mainTimerUpdate();
void mainModulesUpdate();
void mainWindowMessagePump(HWND);

namespace WindowManager
{

// Normally overlay treats desktop size as the "working" size, meaning it cuts
// out the task bar, but when debugging using reference screenshots set as a
// desktop background image, this can be used to have the full monitor size
// be used for the overlay, even though part of it will be clipped by task bar.
//#define FORCE_FULL_SIZE_FOR_DESKTOP_OVERLAY

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

const wchar_t* kMainWindowClassName = L"MMOGO Main";
const wchar_t* kOverlayWindowClassName = L"MMOGO Overlay";
const wchar_t* kSystemOverlayWindowClassName = L"MMOGO System Overlay";

enum EIconCopyMethod
{
	eIconCopyMethod_OverlayBlocksCopy,	// 1
	eIconCopyMethod_TargetWindow,		// 2
	eIconCopyMethod_ExcludeFromCapture,	// 3

	eIconCopyMethod_Num
};

enum EFadeState
{
	eFadeState_Hidden,
	eFadeState_FadeInDelay,
	eFadeState_FadingIn,
	eFadeState_MaxAlpha,
	eFadeState_DisabledFadeOut,
	eFadeState_Disabled,
	eFadeState_InactiveFadeOut,
	eFadeState_Inactive,
	eFadeState_FadeOutDelay,
	eFadeState_FadingOut,
	eFadeState_PulseForever,
	eFadeState_Minimized,
};


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct ZERO_INIT(OverlayWindow)
{
	HWND handle;
	HBITMAP bitmap;
	POINT position;
	SIZE size;
	SIZE lastDrawnSize;
	SIZE bitmapSize;
	double fadeValue;
	EFadeState fadeState;
	u8 alpha;
	bool windowReady;
	bool layoutReady;
};

struct ZERO_INIT(OverlayWindowPriority)
{
	u16 id;
	s16 priority;

	bool operator<(const OverlayWindowPriority& rhs) const
	{
		if( priority != rhs.priority )
			return priority < rhs.priority;
		return id < rhs.id;
	}
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static HWND sMainWindow = NULL;
static HWND sSystemOverlayWindow = NULL;
static HWND sToolbarWindow = NULL;
static HWND sDialogWindow = NULL;
static HMENU sDetachedMenu = NULL;
static DLGPROC sToolbarDialogProc = NULL;
static HANDLE sModalModeTimer = NULL;
static HANDLE sModalModeThread = NULL;
static HANDLE sModalModeExit = NULL;
static POINT sMainWindowPos;
static std::vector<OverlayWindow> sOverlayWindows;
static std::vector<OverlayWindowPriority> sOverlayWindowOrder;
static RECT sDesktopTargetRect; // relative to virtual desktop
static RECT sScreenTargetRect; // relative to main screen
static RECT sTargetClipRect; // relative to sScreenTargetRect
static SIZE sTargetSize = { 0 };
static WNDPROC sSystemOverlayProc = NULL;
static int sToolbarWindowOverlayID = -1;
static EIconCopyMethod sIconCopyMethod = EIconCopyMethod(0);
static bool sMainWindowPosInit = false;
static bool sUseChildWindows = false;
static bool sHidden = false;
static bool sMainWindowDisabled = false;
static volatile bool sWindowInModalMode = false;
static volatile bool sModalUpdateRunning = false;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

DWORD WINAPI modalModeTimerThread(LPVOID lpParam)
{
	HANDLE hTimer = *(HANDLE*)lpParam;
	HANDLE handles[] = { hTimer, sModalModeExit };

	bool isReadyToExit = false;
	while(!isReadyToExit)
	{
		// Wait for the timer (or exit) to be signaled
		switch(WaitForMultipleObjects(2, handles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			{// Timer update
				sModalUpdateRunning = true;
				if( sWindowInModalMode )
				{
					mainTimerUpdate();
					mainModulesUpdate();
				}
				sModalUpdateRunning = false;
			}
			break;
		case WAIT_OBJECT_0 + 1:
			// Exit thread event
			isReadyToExit = true;
			break;
		}
	}

	return 0;
}


static void startModalModeUpdates()
{
	if( sWindowInModalMode || sDialogWindow )
		return;

	// This is used to make sure gamepad input still has an effect when
	// click in a window's title bar, close button, menu, etc. just in
	// case it was our own gamepad-to-mouse translations that started it.
	// Without this, would be stuck in a modal update loop and never get
	// any further updates from the gamepad to release the mouse button,
	// requiring using the actual mouse to break out of the modal loop!
	if( !sModalModeTimer )
	{
		sModalModeTimer = CreateWaitableTimer(NULL, FALSE, NULL);
		sModalModeExit = CreateEvent(NULL, TRUE, FALSE, NULL);
		if( sModalModeTimer && sModalModeExit )
		{
			sModalModeThread = CreateThread(
				NULL, 0, modalModeTimerThread, &sModalModeTimer, 0, NULL);
		}
	}
	if( sModalModeTimer )
	{
		LARGE_INTEGER liDueTime;
		liDueTime.QuadPart = -LONGLONG(gAppTargetFrameTime) * 10000LL;
		SetWaitableTimer(
			sModalModeTimer, &liDueTime,
			gAppTargetFrameTime, NULL, NULL, FALSE);
	}

	sWindowInModalMode = true;
}


static void minimizeOverlays()
{
	for(int i = 0, end = intSize(sOverlayWindows.size()); i < end; ++i)
		sOverlayWindows[i].fadeState = eFadeState_Minimized;
}


static void restoreMinimizedOverlays()
{
	for(int i = 0, end = intSize(sOverlayWindows.size());
		i < end; ++i)
	{
		if( sOverlayWindows[i].fadeState == eFadeState_Minimized )
			sOverlayWindows[i].fadeState = eFadeState_Hidden;
	}
}


static int normalWindowsProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
 	case WM_SYSCOLORCHANGE:
	case WM_DISPLAYCHANGE:
		InvalidateRect(theWindow, NULL, false);
		break;

	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONDBLCLK:
	case WM_ENTERSIZEMOVE:
	case WM_ENTERMENULOOP:
		startModalModeUpdates();
		return -2;

	case WM_SYSCOMMAND:
		if( wParam == SC_MOVE || wParam == SC_SIZE || wParam == SC_KEYMENU )
		{
			startModalModeUpdates();
			return -2;
		}
		break;

	case WM_DEVICECHANGE:
		// Forward this message to app's main message handler
		PostMessage(NULL, theMessage, wParam, lParam);
		break;
	}

	return -1;
}


static LRESULT CALLBACK mainWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case ID_FILE_EXIT:
			PostQuitMessage(0);
			return 0;
		case ID_FILE_PROFILE:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				gProfileToLoad = Profile::queryUserForProfile();
			return 0;
		case ID_FILE_CHARACTERCONFIGFILE:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				TargetConfigSync::promptUserForSyncFileToUse();
			return 0;
		case ID_EDIT_UILAYOUT:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				LayoutEditor::init();
			return 0;
		case ID_EDIT_TEXTFILES:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				gProfileToLoad = Profile::userEditCurrentProfile();
			return 0;
		case ID_HELP_LICENSE:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				Dialogs::showLicenseAgreement();
			return 0;
		case ID_HELP_DOCS:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				Dialogs::showHelpDocuments();
			return 0;
		case ID_HELP_KNOWN_ISSUES:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				Dialogs::showKnownIssues();
			return 0;
		case ID_MINIMIZE:
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				ShowWindow(theWindow, SW_MINIMIZE);
			return 0;
		}
		break;

	case WM_PAINT:
		WindowPainter::paintMainWindowContents(theWindow,
			sMainWindowDisabled || (GetActiveWindow() != theWindow));
		break;

	case WM_SIZE:
		switch(wParam)
		{
		case SIZE_MINIMIZED:
			// Hide overlays as well when main app is minimized and are in
			// "desktop mode" (no target app window found) with a special mode
			// that makes each only stay hidden until the are interact with or
			// made newly visible via controller input.
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				minimizeOverlays();
			break;
		case SIZE_RESTORED:
			// Restore any "minimized" overlays to being just "hidden", which
			// will make gVisibleOverlays control their visibility instead
			if( sWindowInModalMode || sModalUpdateRunning )
				PostMessage(theWindow, theMessage, wParam, lParam);
			else
				restoreMinimizedOverlays();
			break;
		}
		break;

	case WM_ACTIVATE:
		if( LOWORD(wParam) != WA_INACTIVE && (sDialogWindow || sToolbarWindow) )
		{
			HWND aPopupWindow = sDialogWindow ? sDialogWindow : sToolbarWindow;
			SetForegroundWindow(aPopupWindow);
			FLASHWINFO aFlashInfo =
				{ sizeof(FLASHWINFO), aPopupWindow, FLASHW_CAPTION, 5, 60 };
			FlashWindowEx(&aFlashInfo);
			return 0;
		}
		InvalidateRect(theWindow, NULL, TRUE);
		break;

	case WM_MOUSEACTIVATE:
		if( sDialogWindow || sToolbarWindow )
		{
			HWND aPopupWindow = sDialogWindow ? sDialogWindow : sToolbarWindow;
			SetForegroundWindow(aPopupWindow);
			FLASHWINFO aFlashInfo =
				{ sizeof(FLASHWINFO), aPopupWindow, FLASHW_CAPTION, 5, 60 };
			FlashWindowEx(&aFlashInfo);
			return MA_NOACTIVATEANDEAT;
		}
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		{// Note position in case are re-created
			RECT aWindowRect;
			GetWindowRect(theWindow, &aWindowRect);
			sMainWindowPos.x = aWindowRect.left;
			sMainWindowPos.y = aWindowRect.top;
		}
		// Clean up modal mode handling
		DBG_ASSERT(!sWindowInModalMode && !sModalUpdateRunning);
		if( sModalModeExit )
		{
			SetEvent(sModalModeExit);
			WaitForSingleObject(sModalModeThread, INFINITE);
			CloseHandle(sModalModeExit);
			sModalModeExit = NULL;
		}
		if( sModalModeTimer )
		{
			CloseHandle(sModalModeTimer);
			sModalModeTimer = NULL;
		}
		if( sModalModeThread )
		{
			CloseHandle(sModalModeThread);
			sModalModeThread = NULL;
		}
		if( sDetachedMenu )
		{
			DestroyMenu(sDetachedMenu);
			sDetachedMenu = NULL;
		}
		sMainWindow = NULL;
		return 0;

	case WM_SYSCOLORCHANGE:
	case WM_DISPLAYCHANGE:
		stopModalModeUpdates();
		for(int i = 0, end = intSize(sOverlayWindows.size()); i < end; ++i)
		{
			if( sOverlayWindows[i].bitmap )
			{
				DeleteObject(sOverlayWindows[i].bitmap);
				sOverlayWindows[i].bitmap = NULL;
			}
		}
		WindowPainter::cleanup();
		WindowPainter::init();
		break;
	}

	int result = normalWindowsProc(theWindow, theMessage, wParam, lParam);
	if( result >= 0 )
		return (LRESULT)result;

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static void setMainWindowEnabled(bool enable = true)
{
	if( !sMainWindow || sMainWindowDisabled != enable )
		return;

	// I do this instead of fully disabling the window so that the app
	// can still be exited via closing the main window from the task bar
	// The menu is removed because clicking directly on the disabled
	// menu activates a modal mode that won't exit until click elsewhere!
	sMainWindowDisabled = !enable;
	if( enable && sDetachedMenu )
	{
		SetMenu(sMainWindow, sDetachedMenu);
		sDetachedMenu = NULL;
		DrawMenuBar(sMainWindow);
	}
	else if( !enable )
	{
		sDetachedMenu = GetMenu(sMainWindow);
		SetMenu(sMainWindow, NULL);
		DrawMenuBar(sMainWindow);
	}
	InvalidateRect(sMainWindow, NULL, TRUE);
}


static INT_PTR CALLBACK toolbarWindowProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	int result = normalWindowsProc(theDialog, theMessage, wParam, lParam);
	if( result != -1 )
		return (INT_PTR)max(result, 0);
	if( sToolbarDialogProc )
	{
		stopModalModeUpdates();
		return sToolbarDialogProc(theDialog, theMessage, wParam, lParam);
	}
	return (INT_PTR)FALSE;
}


static LRESULT CALLBACK overlayWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	const int anOverlayID = dropTo<int>(
		GetWindowLongPtr(theWindow, GWLP_USERDATA));
	switch(theMessage)
	{
	case WM_PAINT:
		gRefreshOverlays.set(anOverlayID);
		break;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static LRESULT CALLBACK systemOverlayWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	const int anOverlayID = dropTo<int>(
		GetWindowLongPtr(theWindow, GWLP_USERDATA));
	switch(theMessage)
	{
	case WM_PAINT:
		gRefreshOverlays.set(anOverlayID);
		break;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	}

	if( sSystemOverlayProc )
		return sSystemOverlayProc(theWindow, theMessage, wParam, lParam);

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static void updateAlphaFades(OverlayWindow& theWindow, int id)
{
	EFadeState oldState;
	u8 aNewAlpha = theWindow.alpha;
	const WindowPainter::WindowAlphaInfo& wai =
		WindowPainter::alphaInfo(id);
	bool allowReCheck = true;
	do
	{
		oldState = theWindow.fadeState;
		const double oldFadeValue = theWindow.fadeValue;
		switch(theWindow.fadeState)
		{
		case eFadeState_Hidden:
			aNewAlpha = 0;
			if( gActiveOverlays.test(id) )
			{
				// Flash on briefly even if fade back out immediately
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				aNewAlpha = wai.maxAlpha;
				allowReCheck = false;
				break;
			}
			if( gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeInDelay;
				theWindow.fadeValue = 0;
			}
			break;
		case eFadeState_FadeInDelay:
			if( gActiveOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				aNewAlpha = wai.maxAlpha;
				allowReCheck = false;
				break;
			}
			if( !gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_Hidden;
				theWindow.fadeValue = 0;
				break;
			}
			theWindow.fadeValue += gAppFrameTime;
			if( theWindow.fadeValue >= wai.fadeInDelay )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_FadingIn:
			theWindow.fadeValue += wai.fadeInRate * gAppFrameTime;
			aNewAlpha = u8(min(theWindow.fadeValue, double(wai.maxAlpha)));
			if( gDisabledOverlays.test(id) &&
				wai.inactiveAlpha < wai.maxAlpha &&
				aNewAlpha >= wai.inactiveAlpha )
			{
				theWindow.fadeState = eFadeState_DisabledFadeOut;
				if( oldFadeValue < wai.inactiveAlpha )
					aNewAlpha = min(aNewAlpha, wai.inactiveAlpha);
				break;
			}
			if( gActiveOverlays.test(id) || aNewAlpha >= wai.maxAlpha )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( !gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			break;
		case eFadeState_MaxAlpha:
			aNewAlpha = wai.maxAlpha;
			if( !gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveOverlays.test(id) )
			{
				theWindow.fadeValue = 0;
				break;
			}
			if( gDisabledOverlays.test(id) &&
				wai.inactiveAlpha < wai.maxAlpha )
			{
				theWindow.fadeState = eFadeState_DisabledFadeOut;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( wai.inactiveFadeOutDelay > 0 &&
				wai.inactiveAlpha < wai.maxAlpha )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= wai.inactiveFadeOutDelay )
				{
					theWindow.fadeState = eFadeState_InactiveFadeOut;
					theWindow.fadeValue = aNewAlpha;
				}
				break;
			}
			break;
		case eFadeState_DisabledFadeOut:
			if( !gDisabledOverlays.test(id) && gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = max(wai.inactiveAlpha, aNewAlpha);
				break;
			}
			// fall through
		case eFadeState_InactiveFadeOut:
			if( !gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			theWindow.fadeValue -= wai.fadeOutRate * gAppFrameTime;
			aNewAlpha = u8(max<double>(theWindow.fadeValue, wai.inactiveAlpha));
			if( gActiveOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( aNewAlpha <= wai.inactiveAlpha )
			{
				theWindow.fadeState = oldState == eFadeState_InactiveFadeOut
					? eFadeState_Inactive : eFadeState_Disabled;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_Disabled:
			if( !gDisabledOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = wai.inactiveAlpha;
				break;
			}
			if( wai.fadeOutDelay > 0 && wai.inactiveAlpha < wai.maxAlpha )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= wai.inactiveFadeOutDelay )
				{
					theWindow.fadeState = eFadeState_Inactive;
					theWindow.fadeValue = 0;
					break;
				}
			}
			// fall through
		case eFadeState_Inactive:
			aNewAlpha = wai.inactiveAlpha;
			if( !gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_FadeOutDelay:
			if( gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			theWindow.fadeValue += gAppFrameTime;
			if( theWindow.fadeValue >= wai.fadeOutDelay )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			break;
		case eFadeState_FadingOut:
			theWindow.fadeValue -= wai.fadeOutRate * gAppFrameTime;
			aNewAlpha = u8(max(theWindow.fadeValue, 0.0));
			if( aNewAlpha == 0 )
			{
				theWindow.fadeState = eFadeState_Hidden;
				theWindow.fadeValue = 0;
				break;
			}
			if( gVisibleOverlays.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				break;
			}
			break;
		case eFadeState_PulseForever:
			{// Special locked-visible state for use by LayoutEditor
				const double kHoldFullTime = 3000.0;
				const double kFadeTime = 750.0;
				const double kHoldMinTime = 1500.0;
				const double kTotalTime =
					kHoldFullTime + kFadeTime + kHoldMinTime + kFadeTime;
				const u8 kMinAlpha = 80;
				theWindow.fadeValue += gAppFrameTime;
				while(theWindow.fadeValue >= kTotalTime)
					theWindow.fadeValue -= kTotalTime;
				double t = theWindow.fadeValue;
				if( t < kHoldFullTime )
				{
					aNewAlpha = 255;
					break;
				}
				t -= kHoldFullTime;
				if( t < kFadeTime )
				{
					aNewAlpha = u8(clamp(
						kMinAlpha +
						(1.0 - (t / kFadeTime)) * (255 - kMinAlpha),
						0.0, 255.0));
					break;
				}
				t -= kFadeTime;
				if( t < kHoldMinTime )
				{
					aNewAlpha = u8(kMinAlpha);
					break;
				}
				t -= kHoldMinTime;
				aNewAlpha = u8(clamp(
					kMinAlpha + (t / kFadeTime) * (255 - kMinAlpha),
					0.0, 255.0));
			}
			break;
		case eFadeState_Minimized:
			aNewAlpha = 0;
			if( gActiveOverlays.test(id) ||
				!gVisibleOverlays.test(id) )
			{
				// Restore to just normal hidden state once external
				// code hides the overlay or it is directly activated,
				// so it can show back up again.
				theWindow.fadeState = eFadeState_Hidden;
			}
			break;
		default:
			DBG_ASSERT(false && "Invalid WindowManager::EFadeState value");
			theWindow.fadeState = eFadeState_Hidden;
			break;
		}
	} while(oldState != theWindow.fadeState && allowReCheck);

	if( aNewAlpha != theWindow.alpha )
	{
		theWindow.alpha = aNewAlpha;
		theWindow.windowReady = false;
	}
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void createMain(HINSTANCE theAppInstanceHandle)
{
	DBG_ASSERT(sMainWindow == NULL);

	sHidden = false;
	const std::wstring& aMainWindowName = widen(
		Profile::getStr("System", "WindowName", "MMO Gamepad Overlay"));
	const int aMainWindowWidth =
		max(140, Profile::getInt("System", "WindowWidth", 160));
	const int aMainWindowHeight =
		max(64, Profile::getInt("System", "WindowHeight", 80));
	const bool shouldStartMinimized =
		Profile::getBool("System", "WindowStartMinimized", false);

	{// Determine if supports child layered windows or not (Win8 or higher)
		OSVERSIONINFOW info;
		info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
		GetVersionEx(&info);
		sUseChildWindows =
			info.dwMajorVersion >= 6 && info.dwMinorVersion >= 2;
	}

	// Register window classes
	WNDCLASSEXW aWindowClass = { 0 };
	aWindowClass.cbSize = sizeof(WNDCLASSEXW);
	aWindowClass.style = CS_HREDRAW | CS_VREDRAW;
	aWindowClass.lpfnWndProc = mainWindowProc;
	aWindowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	aWindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	aWindowClass.hIcon = LoadIcon(theAppInstanceHandle,
		MAKEINTRESOURCE(IDI_ICON_MAIN));
	aWindowClass.hInstance = theAppInstanceHandle;
	aWindowClass.lpszClassName = kMainWindowClassName;
	aWindowClass.lpszMenuName = MAKEINTRESOURCE(IDR_MENU_MAIN);
	RegisterClassExW(&aWindowClass);

	aWindowClass.lpfnWndProc = overlayWindowProc;
	aWindowClass.hbrBackground = NULL;
	aWindowClass.hCursor = NULL;
	aWindowClass.lpszClassName = kOverlayWindowClassName;
	aWindowClass.lpszMenuName = NULL;
	RegisterClassExW(&aWindowClass);

	aWindowClass.lpfnWndProc = systemOverlayWindowProc;
	aWindowClass.lpszClassName = kSystemOverlayWindowClassName;
	RegisterClassExW(&aWindowClass);

	// Determine main app window position if haven't yet
	if( !sMainWindowPosInit )
	{
		RECT aWorkRect;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &aWorkRect, 0);
		sMainWindowPos.x = aWorkRect.right - aMainWindowWidth;
		sMainWindowPos.y = aWorkRect.bottom - aMainWindowHeight;
		if( !Profile::getStr("System", "WindowXPos").empty() )
			sMainWindowPos.x = Profile::getInt("System", "WindowXPos");
		if( !Profile::getStr("System", "WindowYPos").empty() )
			sMainWindowPos.x = Profile::getInt("System", "WindowYPos");
		sMainWindowPosInit = true;
	}

	// Create main app window
	sMainWindow = CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_APPWINDOW,
		kMainWindowClassName,
		aMainWindowName.c_str(),
		WS_MINIMIZEBOX | WS_SYSMENU,
		sMainWindowPos.x, sMainWindowPos.y,
		aMainWindowWidth, aMainWindowHeight,
		NULL, NULL, theAppInstanceHandle, NULL);

	if( !sMainWindow )
	{
		logFatalError("Could not create main application window!");
		return;
	}

	if( shouldStartMinimized )
	{
		// While this will cause the window to flicker on-screen for a moment,
		// it is necessary to ensure that both A) the window will actually show
		// up in the task bar despite having WS_EX_TOOLWINDOW (which, even with
		// WS_EX_APPWINDOW set will make the window never show in the task bar
		// if initially show it with SW_SHOWNOACTIVATE) and B) that the task bar
		// mouse hover "peek" feature actually looks like the real window.
		SetWindowLongPtr(sMainWindow, GWL_EXSTYLE, WS_EX_APPWINDOW);
		ShowWindow(sMainWindow, SW_SHOWNOACTIVATE);
		SetWindowLongPtr(sMainWindow, GWL_EXSTYLE,
			WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);
		UpdateWindow(sMainWindow);
		PostMessage(sMainWindow, WM_COMMAND, ID_MINIMIZE, 0);
	}
	else
	{
		ShowWindow(sMainWindow, SW_SHOW);
	}

	// Set overlay client area to full main screen initially
	RECT aScreenRect = { 0, 0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN) };
	resize(aScreenRect, false);

	// Load initial UIScale
	loadProfileChanges();
}


void createOverlays(HINSTANCE theAppInstanceHandle)
{
	sIconCopyMethod = EIconCopyMethod(
		clamp(Profile::getInt("System", "IconCopyMethod"),
			1, eIconCopyMethod_Num)-1);

	#ifndef WDA_EXCLUDEFROMCAPTURE
	#define WDA_EXCLUDEFROMCAPTURE 0x00000011
	#endif
	typedef BOOL(WINAPI* PSetWindowDisplayAffinity)(HWND, DWORD);
	PSetWindowDisplayAffinity pSetWindowDisplayAffinity = NULL;
	if( sIconCopyMethod == eIconCopyMethod_ExcludeFromCapture )
	{
		HMODULE hUser32 = LoadLibrary(TEXT("User32.dll"));
		if( hUser32 )
		{
			pSetWindowDisplayAffinity =
				(PSetWindowDisplayAffinity)GetProcAddress(
					hUser32, "SetWindowDisplayAffinity");
			FreeLibrary(hUser32);
		}
	}

	DBG_ASSERT(sOverlayWindows.empty());
	DBG_ASSERT(sOverlayWindowOrder.empty());

	// Create one transparent overlay window per root menu
	sOverlayWindows.reserve(InputMap::menuOverlayCount());
	sOverlayWindows.resize(InputMap::menuOverlayCount());
	sOverlayWindowOrder.reserve(sOverlayWindows.size());
	sOverlayWindowOrder.resize(sOverlayWindows.size());
	for(int i = 0, end = intSize(sOverlayWindowOrder.size()); i < end; ++i)
	{
		sOverlayWindowOrder[i].id = dropTo<u16>(i);
		sOverlayWindowOrder[i].priority = dropTo<s16>(
			WindowPainter::drawPriority(i));
	}
	std::sort(sOverlayWindowOrder.begin(), sOverlayWindowOrder.end());
	for(int i = 0, end = intSize(sOverlayWindowOrder.size()); i < end; ++i)
	{
		const int anOverlayID = sOverlayWindowOrder[i].id;
		const int aMenuID = InputMap::overlayRootMenuID(anOverlayID);
		OverlayWindow& aWindow = sOverlayWindows[anOverlayID];
		const bool isSystemOverlay =
			InputMap::menuStyle(aMenuID) == eMenuStyle_System;
		WindowPainter::updateWindowLayout(
			anOverlayID, sTargetSize, sTargetClipRect,
			aWindow.position, aWindow.size);
		aWindow.layoutReady = true;
		aWindow.windowReady = false;
		gReshapeOverlays.reset(anOverlayID);

		aWindow.handle = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_NOACTIVATE |
			WS_EX_TRANSPARENT | WS_EX_LAYERED,
			isSystemOverlay
				? kSystemOverlayWindowClassName : kOverlayWindowClassName,
			widen(InputMap::menuLabel(aMenuID)).c_str(),
			WS_POPUP | (sUseChildWindows ? WS_CHILD : 0),
			aWindow.position.x,
			aWindow.position.y,
			aWindow.size.cx,
			aWindow.size.cy,
			sUseChildWindows ? sMainWindow : NULL,
			NULL, theAppInstanceHandle, NULL);

		SetWindowLongPtr(aWindow.handle, GWLP_USERDATA, anOverlayID);
		if( sIconCopyMethod == eIconCopyMethod_ExcludeFromCapture &&
			pSetWindowDisplayAffinity )
		{
			pSetWindowDisplayAffinity(aWindow.handle, WDA_EXCLUDEFROMCAPTURE);
		}
		if( isSystemOverlay )
			sSystemOverlayWindow = aWindow.handle;
	}
}


void destroyAll(HINSTANCE theAppInstanceHandle)
{
	if( sMainWindow )
	{
		DestroyWindow(sMainWindow);
		sMainWindow = NULL;
	}
	destroyToolbarWindow();
	for(int i = 0, end = intSize(sOverlayWindows.size()); i < end; ++i)
	{
		if( sOverlayWindows[i].bitmap )
			DeleteObject(sOverlayWindows[i].bitmap);
		if( sOverlayWindows[i].handle )
			DestroyWindow(sOverlayWindows[i].handle);
	}
	sSystemOverlayProc = NULL;
	sSystemOverlayWindow = NULL;
	sOverlayWindows.clear();
	sOverlayWindowOrder.clear();
	UnregisterClassW(kMainWindowClassName, theAppInstanceHandle);
	UnregisterClassW(kOverlayWindowClassName, theAppInstanceHandle);
	UnregisterClassW(kSystemOverlayWindowClassName, theAppInstanceHandle);
}


void loadProfileChanges()
{
	// Variables are not added to changedSections(), so just re-read UIScale
	const std::string& aUIScaleStr = Profile::getVariable("UIScale");
	const double oldUIScale = gUIScale;
	gUIScale = stringToDouble(aUIScaleStr);
	if( gUIScale <= 0 )
		gUIScale = 1.0;
	if( gUIScale != oldUIScale )
	{
		WindowPainter::updateScaling();
		for(int i = 0, end = intSize(sOverlayWindows.size()); i < end; ++i)
			sOverlayWindows[i].layoutReady = false;
	}
}


void update()
{
	if( !gProfileToLoad.empty() || gShutdown )
		return;

	endDialog();
	setMainWindowEnabled(!sToolbarWindow);

	// Update each overlay window as needed
	HDC aScreenDC = GetDC(NULL);
	HDC aCaptureDC = NULL;
	POINT aCaptureOffset = { 0 };
	if( sIconCopyMethod == eIconCopyMethod_TargetWindow &&
		TargetApp::targetWindowHandle() )
	{
		aCaptureDC = GetDC(TargetApp::targetWindowHandle());
	}
	else if( sIconCopyMethod != eIconCopyMethod_TargetWindow )
	{
		aCaptureDC = GetDC(NULL);
		aCaptureOffset.x = sScreenTargetRect.left;
		aCaptureOffset.y = sScreenTargetRect.top;
	}
	bool windowReorderNeeded = false;
	POINT anOriginPoint = { 0, 0 };
	for(int i = 0, end = intSize(sOverlayWindowOrder.size()); i < end; ++i)
	{
		const int anOverlayID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[anOverlayID];

		// Check if window priority changed
		const int aDrawPriority = WindowPainter::drawPriority(anOverlayID);
		if( aDrawPriority != sOverlayWindowOrder[i].priority )
		{
			sOverlayWindowOrder[i].priority = dropTo<s16>(aDrawPriority);
			windowReorderNeeded = true;
		}

		// Check for flag that need to update layout
		if( gReshapeOverlays.test(anOverlayID) )
		{
			aWindow.layoutReady = false;
			gReshapeOverlays.reset(anOverlayID);
		}

		// Update alpha fade effects based on gVisibleOverlay & gActiveOverlay
		updateAlphaFades(aWindow, anOverlayID);

		// Check visibility status so can mostly ignore hidden windows
		if( sHidden || aWindow.alpha == 0 ||
			(aWindow.layoutReady && aWindow.size.cx <= 0) ||
			(aWindow.layoutReady && aWindow.size.cy <= 0) )
		{
			if( IsWindowVisible(aWindow.handle) )
				ShowWindow(aWindow.handle, SW_HIDE);
			gActiveOverlays.reset(anOverlayID);
			if( aWindow.bitmap &&
				InputMap::menuStyle(anOverlayID) == eMenuStyle_System )
			{// Large bitmap that's rarely needed, so free it from memory
				DeleteObject(aWindow.bitmap);
				aWindow.bitmap = null;
			}
			continue;
		}

		// Don't update window contents while fading out
		const bool allowRedraw =
			aWindow.fadeState != eFadeState_FadeOutDelay &&
			aWindow.fadeState != eFadeState_FadingOut;

		// Check for possible update to window layout
		if( !aWindow.layoutReady && allowRedraw )
		{
			WindowPainter::updateWindowLayout(
				anOverlayID, sTargetSize, sTargetClipRect,
				aWindow.position, aWindow.size);
			aWindow.layoutReady = true;
			aWindow.windowReady = false;
		}

		// Check for window size changes requiring redraw or bitmap delete
		if( aWindow.bitmap &&
			(aWindow.size.cx != aWindow.lastDrawnSize.cx ||
			 aWindow.size.cy != aWindow.lastDrawnSize.cy) )
		{
			// Always do full redraw for size changes even if reuse bitmap
			gFullRedrawOverlays.set(anOverlayID);
			// If bitmap is too small, or this is a large window that shrank
			// significantly, than delete and re-create the bitmap
			if( aWindow.bitmapSize.cx < aWindow.size.cx ||
				aWindow.bitmapSize.cy < aWindow.size.cy ||
				(aWindow.bitmapSize.cx >= aWindow.size.cx * 3 / 2 &&
				 aWindow.bitmapSize.cx >= sTargetSize.cx / 3) ||
				(aWindow.bitmapSize.cy >= aWindow.size.cy * 3 / 2 &&
				 aWindow.bitmapSize.cy >= sTargetSize.cy / 3) )
			{
				DeleteObject(aWindow.bitmap);
				aWindow.bitmap = NULL;
			}
		}

		// Don't create bitmap for a 0-sized window (likely off screen edge)
		if( aWindow.size.cx <= 0 || aWindow.size.cy <= 0 )
		{
			aWindow.size.cx = aWindow.size.cy = 0;
			ShowWindow(aWindow.handle, SW_HIDE);
			gActiveOverlays.reset(anOverlayID);
			continue;
		}

		// Create bitmap if doesn't exist by this point
		if( !aWindow.bitmap )
		{
			gFullRedrawOverlays.set(anOverlayID);
			aWindow.bitmapSize = aWindow.size;
			aWindow.bitmap = CreateCompatibleBitmap(
				aScreenDC, aWindow.size.cx, aWindow.size.cy);
			if( !aWindow.bitmap )
			{
				logFatalError("Could not create bitmap for overlay!");
				continue;
			}
		}

		// Redraw bitmap contents if needed
		HDC aWindowDC = CreateCompatibleDC(aScreenDC);
		const int aWindowDCInitialState = SaveDC(aWindowDC);
		if( !aWindowDC )
		{
			logFatalError("Could not create device context for overlay!");
			continue;
		}
		SelectObject(aWindowDC, aWindow.bitmap);
		if( allowRedraw &&
			(gRefreshOverlays.test(anOverlayID) ||
			 gFullRedrawOverlays.test(anOverlayID)) )
		{
			WindowPainter::paintWindowContents(
				aWindowDC, aCaptureDC, aCaptureOffset, sTargetSize,
				anOverlayID, gFullRedrawOverlays.test(anOverlayID));
			aWindow.windowReady = false;
			aWindow.lastDrawnSize = aWindow.size;
			gRefreshOverlays.reset(anOverlayID);
			gFullRedrawOverlays.reset(anOverlayID);
		}

		// Update window
		if( !aWindow.windowReady )
		{
			BLENDFUNCTION aBlendFunction = {AC_SRC_OVER, 0, aWindow.alpha, 0};
			POINT aWindowScreenPos;
			aWindowScreenPos.x = sScreenTargetRect.left + aWindow.position.x;
			aWindowScreenPos.y = sScreenTargetRect.top + aWindow.position.y;
			UpdateLayeredWindow(aWindow.handle, aScreenDC,
				&aWindowScreenPos, &aWindow.size,
				aWindowDC, &anOriginPoint,
				WindowPainter::transColor(anOverlayID),
				&aBlendFunction, ULW_ALPHA | ULW_COLORKEY);
			aWindow.windowReady = true;
		}
		gActiveOverlays.reset(anOverlayID);

		// Show window if it isn't visible yet
		if( !IsWindowVisible(aWindow.handle) )
			ShowWindow(aWindow.handle, SW_SHOWNOACTIVATE);

		// Cleanup
		RestoreDC(aWindowDC, aWindowDCInitialState);
		DeleteDC(aWindowDC);
	}
	ReleaseDC(NULL, aCaptureDC);
	ReleaseDC(NULL, aScreenDC);
	
	// TODO
	//if( windowReorderNeeded )

	if( sToolbarWindow && !sHidden && !IsWindowVisible(sToolbarWindow) )
		ShowWindow(sToolbarWindow, SW_SHOW);
}


void beginDialog(HWND theDialog)
{
	stopModalModeUpdates();
	setMainWindowEnabled(false);
	sDialogWindow = theDialog;
	for(int i = 0, end = intSize(sOverlayWindowOrder.size()); i < end; ++i)
	{
		const int anOverlayID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[anOverlayID];

		if( IsWindowVisible(aWindow.handle) )
			ShowWindow(aWindow.handle, SW_HIDE);
	}
	if( sToolbarWindow && IsWindowVisible(sToolbarWindow) )
		ShowWindow(sToolbarWindow, SW_HIDE);
}


void endDialog()
{
	if( !sDialogWindow )
		return;

	sDialogWindow = NULL;
	setMainWindowEnabled(!sToolbarWindow);
}


void stopModalModeUpdates()
{
	if( !sWindowInModalMode )
		return;
	sWindowInModalMode = false;
	if( sModalModeTimer )
		CancelWaitableTimer(sModalModeTimer);
	while(sModalUpdateRunning)
	{
		// Handle any windows messages needed by timer thread while waiting
		mainWindowMessagePump(NULL);
		Sleep(0);
	}
}


bool isOwnedByThisApp(HWND theWindow)
{
	DWORD aWindowProcessId = 0;
	GetWindowThreadProcessId(theWindow, &aWindowProcessId);

	return (aWindowProcessId == GetCurrentProcessId());
}


HWND mainHandle()
{
	return sMainWindow;
}


HWND toolbarHandle()
{
	return sToolbarWindow;
}


HWND parentWindowHandle()
{
	if( sMainWindow &&
		IsWindowVisible(sMainWindow) &&
		!TargetApp::targetWindowIsActive() &&
		!TargetApp::targetWindowIsTopMost() &&
		!TargetApp::targetWindowIsFullScreen() )
	{
		return sMainWindow;
	}
	
	return NULL;
}


bool requiresNormalCursorControl()
{
	return
		sHidden || sToolbarWindow ||
		(!TargetApp::targetWindowHandle() &&
		 TargetApp::targetWindowRequested());
}


void resize(RECT theNewWindowRect, bool isTargetAppWindow)
{
	if( !sMainWindow ||
		theNewWindowRect.right <= theNewWindowRect.left ||
		theNewWindowRect.bottom <= theNewWindowRect.top )
		return;

	// Restrict to "work" area of active monitor when don't have a target app
	sTargetClipRect = theNewWindowRect;
	if( !isTargetAppWindow )
	{
		if( HMONITOR hMonitor =
				MonitorFromRect(&theNewWindowRect, MONITOR_DEFAULTTONEAREST) )
		{
			MONITORINFO aMonitorInfo = { sizeof(MONITORINFO) };
			if( GetMonitorInfo(hMonitor, &aMonitorInfo) )
			{
				if( IntersectRect(
						&sTargetClipRect,
						&theNewWindowRect,
						&aMonitorInfo.rcWork) )
				{
					#ifndef FORCE_FULL_SIZE_FOR_DESKTOP_OVERLAY
					theNewWindowRect = sTargetClipRect;
					#endif
				}
				else
				{
					sTargetClipRect = theNewWindowRect;
				}
			}
		}
	}

	// Update sDesktopTargetRect, sScreenTargetRect, and sTargetClipRect
	sDesktopTargetRect = sScreenTargetRect = theNewWindowRect;
	sDesktopTargetRect.left -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.right -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.top -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	sDesktopTargetRect.bottom -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	OffsetRect(&sTargetClipRect,
		-sScreenTargetRect.left, -sScreenTargetRect.top);

	// Update sTargetSize
	SIZE aNewTargetSize;
	aNewTargetSize.cx = theNewWindowRect.right - theNewWindowRect.left;
	aNewTargetSize.cy = theNewWindowRect.bottom - theNewWindowRect.top;
	if( aNewTargetSize.cx != sTargetSize.cx ||
		aNewTargetSize.cy != sTargetSize.cy ||
		!isTargetAppWindow )
	{
		sTargetSize = aNewTargetSize;

		Profile::setVariable("W", toString(sTargetSize.cx), true);
		Profile::setVariable("H", toString(sTargetSize.cy), true);
	}

	// Flag all overlay windows to update position & size accordingly
	for(int i = 0, end = intSize(sOverlayWindows.size()); i < end; ++i)
		sOverlayWindows[i].layoutReady = false;
	WindowPainter::updateTargetRect();
}


void resetOverlays()
{
	sHidden = false;
	RECT aScreenRect = { 0, 0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN) };
	resize(aScreenRect, false);
	if( sMainWindow && IsWindowVisible(sMainWindow) && !IsIconic(sMainWindow) )
		restoreMinimizedOverlays();
	else
		minimizeOverlays();
}


void hideOverlays()
{
	sHidden = true;
}


void showOverlays()
{
	sHidden = false;
	for(int i = 0, end = intSize(sOverlayWindows.size()); i < end; ++i)
	{
		if( sOverlayWindows[i].fadeState == eFadeState_Minimized )
			sOverlayWindows[i].fadeState = eFadeState_Hidden;
	}
}


void setOverlaysToTopZ()
{
	for(int i = 0, end = intSize(sOverlayWindowOrder.size()); i < end; ++i)
	{
		const int anOverlayID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[anOverlayID];

		DBG_ASSERT(aWindow.handle);
		SetWindowPos(
			aWindow.handle, HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
	}
	if( sToolbarWindow )
	{
		SetWindowPos(
			sToolbarWindow, HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
	}
}


bool overlaysAreHidden()
{
	return sHidden;
}


SIZE overlayTargetSize()
{
	return sTargetSize;
}


RECT overlayClipRect()
{
	return sTargetClipRect;
}


void showTargetWindowFound()
{
	WindowPainter::flashSystemOverlayBorder();
}


HWND createToolbarWindow(int theResID, DLGPROC theProc, int theMenuID)
{
	destroyToolbarWindow();
	sToolbarDialogProc = theProc;
	sToolbarWindow = CreateDialogParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(theResID),
		parentWindowHandle(),
		toolbarWindowProc, 0);
	if( theMenuID >= 0 )
	{
		sToolbarWindowOverlayID = InputMap::menuOverlayID(theMenuID);
		OverlayWindow& aWindow = sOverlayWindows[sToolbarWindowOverlayID];
		// Below not only causes the window to pulse but "locks" the window to
		// being visible (since turning window visibility off really just
		// triggers an alpha-fade-out state, and this eFadeState never reaches
		// 0 alpha, ignores visibility setting, and doesn't change to any other
		// eFadeState - until it is aborted by destroyToolbarWindow() later).
		aWindow.fadeState = eFadeState_PulseForever;
		aWindow.alpha = 255;
		aWindow.fadeValue = 0;
		Menus::tempForceShowSubMenu(theMenuID);
	}
	setMainWindowEnabled(false);
	return sToolbarWindow;
}


void destroyToolbarWindow()
{
	if( !sToolbarWindow && !sToolbarDialogProc )
		return;
	DestroyWindow(sToolbarWindow);
	sToolbarWindow = NULL;
	sToolbarDialogProc = NULL;
	if( sToolbarWindowOverlayID >= 0 )
	{
		OverlayWindow& aWindow = sOverlayWindows[sToolbarWindowOverlayID];
		aWindow.fadeState = eFadeState_FadingIn;
		aWindow.fadeValue = 0;
		aWindow.alpha = 0;
		sToolbarWindowOverlayID = -1;
	}
	Menus::tempForceShowSubMenu(-1);
}


void setSystemOverlayCallbacks(WNDPROC theProc, SystemPaintFunc thePaintFunc)
{
	WindowPainter::setSystemOverlayDrawHook(thePaintFunc);
	if( sSystemOverlayWindow && theProc != sSystemOverlayProc )
	{
		sSystemOverlayProc = theProc;
		LONG_PTR exStyle = GetWindowLongPtr(sSystemOverlayWindow, GWL_EXSTYLE);
		if( theProc != NULL )
			exStyle &= ~WS_EX_TRANSPARENT;
		else
			exStyle |= WS_EX_TRANSPARENT;
		SetWindowLongPtr(sSystemOverlayWindow, GWL_EXSTYLE, exStyle);
		SetWindowPos(sSystemOverlayWindow, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
}


POINT mouseToOverlayPos(bool clamped)
{
	POINT result;
	// Get current screen-relative mouse position
	GetCursorPos(&result);
	// Offset to client relative position
	result.x -= sScreenTargetRect.left;
	result.y -= sScreenTargetRect.top;
	if( clamped )
	{// Clamp to within client rect range
		result.x = clamp(result.x, 0, sTargetSize.cx - 1);
		result.y = clamp(result.y, 0, sTargetSize.cy - 1);
	}
	return result;
}


POINT hotspotToOverlayPos(const Hotspot& theHotspot)
{
	POINT result = {0, 0};
	if( !sMainWindow )
		return result;

	// Start with percentage of client rect as u16 (i.e. 65536 == 100%)
	// and convert it to client-rect-relative pixel position
	result.x = u16ToRangeVal(theHotspot.x.anchor, sTargetSize.cx);
	result.y = u16ToRangeVal(theHotspot.y.anchor, sTargetSize.cy);
	// Add pixel offset w/ UI Scale applied
	result.x += LONG(theHotspot.x.offset * gUIScale);
	result.y += LONG(theHotspot.y.offset * gUIScale);
	// Clamp to within client rect range
	result.x = clamp(result.x, 0, sTargetSize.cx - 1);
	result.y = clamp(result.y, 0, sTargetSize.cy - 1);
	return result;
}


Hotspot overlayPosToHotspot(POINT thePos)
{
	Hotspot result;
	thePos.x = clamp(thePos.x, 0, sTargetSize.cx-1);
	thePos.y = clamp(thePos.y, 0, sTargetSize.cy-1);
	result.x.anchor = ratioToU16(thePos.x, sTargetSize.cx);
	result.y.anchor = ratioToU16(thePos.y, sTargetSize.cy);
	return result;
}


POINT overlayPosToNormalizedMousePos(POINT theMousePos)
{
	if( !sMainWindow )
	{
		theMousePos.x = 32768;
		theMousePos.y = 32768;
		return theMousePos;
	}

	const int kDesktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int kDesktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	// Convert to virtual desktop pixel coordinate
	theMousePos.x = max(0L, theMousePos.x + sDesktopTargetRect.left);
	theMousePos.y = max(0L, theMousePos.y + sDesktopTargetRect.top);
	// Convert to % of virtual desktop size as normalized 0-65535
	theMousePos.x = ratioToU16(theMousePos.x, kDesktopWidth);
	theMousePos.y = ratioToU16(theMousePos.y, kDesktopHeight);
	return theMousePos;
}


POINT normalizedMouseToOverlayPos(POINT theSentMousePos)
{
	const int kDesktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int kDesktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	// Restore from normalized to pixel position of virtual desktop
	const u16 aMousePosX16 = dropTo<u16>(theSentMousePos.x);
	const u16 aMousePosY16 = dropTo<u16>(theSentMousePos.y);
	theSentMousePos.x = u16ToRangeVal(aMousePosX16, kDesktopWidth);
	theSentMousePos.y = u16ToRangeVal(aMousePosY16, kDesktopHeight);
	// Offset to be client relative position
	theSentMousePos.x -= sDesktopTargetRect.left;
	theSentMousePos.y -= sDesktopTargetRect.top;
	return theSentMousePos;
}


Hotspot hotspotForMenuItem(int theRootMenuID, int theMenuItemIdx)
{
	const int theOverlayID = InputMap::menuOverlayID(theRootMenuID);
	const int theMenuID = Menus::activeMenuForOverlayID(theOverlayID);

	switch(InputMap::menuStyle(theMenuID))
	{
	case eMenuStyle_Slots:
		// Only ever allow pointing at the top component (selected item)
		theMenuItemIdx = 0;
		break;
	case eMenuStyle_Hotspots:
	case eMenuStyle_Highlight:
		// Directly use the hotspot associated with the menu item already
		return InputMap::getHotspot(
			InputMap::menuItemHotspotID(theMenuID, theMenuItemIdx));
	}

	OverlayWindow& aWindow = sOverlayWindows[theOverlayID];
	if( !aWindow.layoutReady || gReshapeOverlays.test(theOverlayID) )
	{
		WindowPainter::updateWindowLayout(
			theOverlayID, sTargetSize, sTargetClipRect,
			aWindow.position, aWindow.size);
		aWindow.layoutReady = true;
		aWindow.windowReady = false;
		gReshapeOverlays.reset(theOverlayID);
	}

	const RECT& aMenuRect =
		WindowPainter::windowLayoutRect(theOverlayID, theMenuItemIdx + 1);
	POINT aPos;
	aPos.x = aMenuRect.left;
	aPos.y = aMenuRect.top;
	aPos.x += aMenuRect.right;
	aPos.y += aMenuRect.bottom;
	aPos.x /= 2;
	aPos.y /= 2;
	aPos.x += aWindow.position.x;
	aPos.y += aWindow.position.y;

	return overlayPosToHotspot(aPos);
}


RECT overlayRect(int theOverlayID)
{
	OverlayWindow& aWindow = sOverlayWindows[theOverlayID];
	if( !aWindow.layoutReady || gReshapeOverlays.test(theOverlayID) )
	{
		WindowPainter::updateWindowLayout(
			theOverlayID, sTargetSize, sTargetClipRect,
			aWindow.position, aWindow.size);
		aWindow.layoutReady = true;
		aWindow.windowReady = false;
		gReshapeOverlays.reset(theOverlayID);
	}

	RECT result;
	result.left = aWindow.position.x - sScreenTargetRect.left;
	result.top = aWindow.position.y - sScreenTargetRect.top;
	result.right = result.left + aWindow.size.cx;
	result.bottom = result.top + aWindow.size.cy;
	return result;
}

} // WindowManager
