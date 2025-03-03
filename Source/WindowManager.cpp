//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "WindowManager.h"

#include "Dialogs.h"
#include "HUD.h"
#include "InputMap.h"
#include "LayoutEditor.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "TargetApp.h" // targetWindowHandle()

// Forward declares of functions defined in Main.cpp for updating in modal mode
void mainTimerUpdate();
void mainModulesUpdate();

namespace WindowManager
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

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
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct OverlayWindow
	: public ConstructFromZeroInitializedMemory<OverlayWindow>
{
	HWND handle;
	HBITMAP bitmap;
	POINT position;
	SIZE size;
	SIZE bitmapSize;
	std::vector<RECT> components;
	float fadeValue;
	EFadeState fadeState;
	u8 alpha;
	bool windowUpdated;
	bool layoutUpdated;
};


struct OverlayWindowPriority
{
	u16 id;
	s16 priority;

	OverlayWindowPriority() : id(), priority() {}
	bool operator<(const OverlayWindowPriority& rhs) const
	{
		if( priority != rhs.priority )
			return priority < rhs.priority;
		return id < rhs.id; 
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static HWND sMainWindow = NULL;
static HWND sSystemOverlayWindow = NULL;
static HWND sToolbarWindow = NULL;
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
static int sToolbarWindowHUDElementID = -1;
static EIconCopyMethod sIconCopyMethod = EIconCopyMethod(0);
static bool sMainWindowPosInit = false;
static bool sUseChildWindows = false;
static bool sHidden = false;
static bool sInDialogMode = false;
static bool sMainWindowDisabled = false;
static bool sWindowInModalMode = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

DWORD WINAPI modalModeTimerThread(LPVOID lpParam)
{
	HANDLE hTimer = *(HANDLE*)lpParam;
	HANDLE handles[] = { hTimer, sModalModeExit };

	while(true)
	{
		// Wait for the timer to be signaled
		switch(WaitForMultipleObjects(2, handles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			// Timer update
			if( sWindowInModalMode )
			{
				mainTimerUpdate();
				mainModulesUpdate();
			}
			break;
		case WAIT_OBJECT_0 + 1:
			// Exit thread event
			return 0;
		}
	}

	return 0;
}


static void startModalModeUpdates()
{
	if( sWindowInModalMode )
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


static bool normalWindowsProc(
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
		startModalModeUpdates();
		break;

	case WM_SYSCOMMAND:
		if( wParam == SC_MOVE || wParam == SC_SIZE )
			startModalModeUpdates();
		break;
	}

	return false;
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
			gReloadProfile = Profile::queryUserForProfile();
			return 0;
		case ID_EDIT_UILAYOUT:
			LayoutEditor::init();
			return 0;
		case ID_HELP_LICENSE:
			Dialogs::showLicenseAgreement(theWindow);
			return 0;
		}
		break;

	case WM_PAINT:
		HUD::drawMainWindowContents(theWindow,
			sMainWindowDisabled || (GetActiveWindow() != theWindow));
		break;

	case WM_ACTIVATE:
		InvalidateRect(theWindow, NULL, TRUE);
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		// Note position in case are re-created
		{
			RECT aWindowRect;
			GetWindowRect(theWindow, &aWindowRect);
			sMainWindowPos.x = aWindowRect.left;
			sMainWindowPos.y = aWindowRect.top;
		}
		// Clean up modal mode handling
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
		sMainWindow = NULL;
		return 0;

 	case WM_SYSCOLORCHANGE:
	case WM_DISPLAYCHANGE:
		for(size_t i = 0; i < sOverlayWindows.size(); ++i)
		{
			if( sOverlayWindows[i].bitmap )
			{
				DeleteObject(sOverlayWindows[i].bitmap);
				sOverlayWindows[i].bitmap = NULL;
			}
		}
		HUD::init();
		break;

	case WM_DEVICECHANGE:
        // Forward this message to app's main message handler
		PostMessage(NULL, theMessage, wParam, lParam);
		break;
	}

	if( normalWindowsProc(theWindow, theMessage, wParam, lParam) )
		return 0;

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static void setMainWindowEnabled(bool enable = true)
{
	if( !sMainWindow || sMainWindowDisabled != enable )
		return;
	if( enable && sToolbarWindow )
		return;

	sMainWindowDisabled = !enable;
	if( HMENU hMenu = GetMenu(sMainWindow) )
	{
		int itemCount = GetMenuItemCount(hMenu);
		for(int i = 0; i < itemCount; ++i)
		{
			EnableMenuItem(hMenu, i,
				MF_BYPOSITION | (enable ? MF_ENABLED : MF_GRAYED));
		}
		DrawMenuBar(sMainWindow);
		InvalidateRect(sMainWindow, NULL, TRUE);
	}
}


static INT_PTR CALLBACK toolbarWindowProc(
	HWND theDialog, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	if( normalWindowsProc(theDialog, theMessage, wParam, lParam) )
		return (INT_PTR)TRUE;
	if( sToolbarDialogProc )
		return sToolbarDialogProc(theDialog, theMessage, wParam, lParam);
	return (INT_PTR)FALSE;
}


static LRESULT CALLBACK overlayWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	u16 aHUDElementID = u16(GetWindowLongPtr(theWindow, GWLP_USERDATA));
	switch(theMessage)
	{
	case WM_PAINT:
		gRedrawHUD.set(aHUDElementID);
		break;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static LRESULT CALLBACK systemOverlayWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	u16 aHUDElementID = u16(GetWindowLongPtr(theWindow, GWLP_USERDATA));
	switch(theMessage)
	{
	case WM_PAINT:
		gRedrawHUD.set(aHUDElementID);
		break;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	}

	if( sSystemOverlayProc )
		return sSystemOverlayProc(theWindow, theMessage, wParam, lParam);

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static void updateAlphaFades(OverlayWindow& theWindow, u16 id)
{
	EFadeState oldState;
	u8 aNewAlpha = theWindow.alpha;
	const u8 aMaxAlpha = HUD::maxAlpha(id);
	const u8 anInactiveAlpha = HUD::inactiveAlpha(id);
	bool allowReCheck = true;
	do
	{
		oldState = theWindow.fadeState;
		const float oldFadeValue = theWindow.fadeValue;
		switch(theWindow.fadeState)
		{
		case eFadeState_Hidden:
			aNewAlpha = 0;
			if( gActiveHUD.test(id) )
			{
				// Flash on briefly even if fade back out immediately
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				aNewAlpha = aMaxAlpha;
				allowReCheck = false;
				break;
			}
			if( gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeInDelay;
				theWindow.fadeValue = 0;
			}
			break;
		case eFadeState_FadeInDelay:
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				aNewAlpha = aMaxAlpha;
				allowReCheck = false;
				break;
			}
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_Hidden;
				theWindow.fadeValue = 0;
				break;
			}
			theWindow.fadeValue += gAppFrameTime;
			if( theWindow.fadeValue >= HUD::alphaFadeInDelay(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_FadingIn:
			theWindow.fadeValue += HUD::alphaFadeInRate(id) * gAppFrameTime;
			aNewAlpha = u8(min(
				theWindow.fadeValue, (float)aMaxAlpha));
			if( gDisabledHUD.test(id) &&
				anInactiveAlpha < aMaxAlpha &&
				aNewAlpha >= anInactiveAlpha )
			{
				theWindow.fadeState = eFadeState_DisabledFadeOut;
				if( oldFadeValue < anInactiveAlpha )
					aNewAlpha = min(aNewAlpha, anInactiveAlpha);
				break;
			}
			if( gActiveHUD.test(id) || aNewAlpha >= aMaxAlpha )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			break;
		case eFadeState_MaxAlpha:
			aNewAlpha = aMaxAlpha;
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeValue = 0;
				break;
			}
			if( gDisabledHUD.test(id) &&
				anInactiveAlpha < aMaxAlpha )
			{
				theWindow.fadeState = eFadeState_DisabledFadeOut;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( HUD::inactiveFadeOutDelay(id) > 0 &&
				anInactiveAlpha < aMaxAlpha )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= HUD::inactiveFadeOutDelay(id) )
				{
					theWindow.fadeState = eFadeState_InactiveFadeOut;
					theWindow.fadeValue = aNewAlpha;
				}
				break;
			}
			break;
		case eFadeState_DisabledFadeOut:
			if( !gDisabledHUD.test(id) && gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = max(anInactiveAlpha, aNewAlpha);
				break;
			}
			// fall through
		case eFadeState_InactiveFadeOut:
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			theWindow.fadeValue -= HUD::alphaFadeOutRate(id) * gAppFrameTime;
			aNewAlpha = u8(max(theWindow.fadeValue, anInactiveAlpha));
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( aNewAlpha <= anInactiveAlpha )
			{
				theWindow.fadeState = oldState == eFadeState_InactiveFadeOut
					? eFadeState_Inactive : eFadeState_Disabled;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_Disabled:
			if( !gDisabledHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = anInactiveAlpha;
				break;
			}
			if( HUD::inactiveFadeOutDelay(id) > 0 &&
				anInactiveAlpha < aMaxAlpha )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= HUD::inactiveFadeOutDelay(id) )
				{
					theWindow.fadeState = eFadeState_Inactive;
					theWindow.fadeValue = 0;
					break;
				}
			}
			// fall through
		case eFadeState_Inactive:
			aNewAlpha = anInactiveAlpha;
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_FadeOutDelay:
			if( gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			theWindow.fadeValue += gAppFrameTime;
			if( theWindow.fadeValue >= HUD::alphaFadeOutDelay(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			break;
		case eFadeState_FadingOut:
			theWindow.fadeValue -= HUD::alphaFadeOutRate(id) * gAppFrameTime;
			aNewAlpha = u8(max(theWindow.fadeValue, 0.0f));
			if( aNewAlpha == 0 )
			{
				theWindow.fadeState = eFadeState_Hidden;
				theWindow.fadeValue = 0;
				break;
			}
			if( gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				break;
			}
			break;
		default:
			DBG_ASSERT(false && "Invalid WindowManager::EFadeState value");
			theWindow.fadeState = eFadeState_Hidden;
			break;
		}
	} while(oldState != theWindow.fadeState && allowReCheck);

	if( sToolbarWindowHUDElementID == id )
	{
		theWindow.fadeState = eFadeState_MaxAlpha;
		theWindow.fadeValue = 0;
		aNewAlpha = aMaxAlpha;
	}

	if( aNewAlpha != theWindow.alpha )
	{
		theWindow.alpha = aNewAlpha;
		theWindow.windowUpdated = false;
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void createMain(HINSTANCE theAppInstanceHandle)
{
	DBG_ASSERT(sMainWindow == NULL);

	sHidden = false;
	const std::wstring& aMainWindowName = widen(
		Profile::getStr("System/WindowName", "MMO Gamepad Overlay"));
	u16 aMainWindowWidth = Profile::getInt("System/WindowWidth", 160);
	u16 aMainWindowHeight = Profile::getInt("System/WindowHeight", 80);
	const bool isMainWindowHidden =
		(aMainWindowWidth == 0 || aMainWindowHeight == 0);

	if( isMainWindowHidden )
	{
		sUseChildWindows = false;
		aMainWindowWidth = 10;
		aMainWindowHeight = 10;
	}
	else
	{// Determine if supports child layered windows or not (Win8 or higher)
		OSVERSIONINFOW info;
		info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
		GetVersionEx(&info);
		if( info.dwMajorVersion >= 6 && info.dwMinorVersion >= 2 )
			sUseChildWindows = true;
		else
			sUseChildWindows = false;
	}

	// Register window classes
	WNDCLASSEXW aWindowClass = { 0 };
	aWindowClass.cbSize = sizeof(WNDCLASSEXW);
	aWindowClass.style = CS_HREDRAW | CS_VREDRAW;
	aWindowClass.lpfnWndProc = mainWindowProc;
	aWindowClass.hbrBackground =
		isMainWindowHidden
			? (HBRUSH)GetStockObject(NULL_BRUSH)
			: (HBRUSH)GetStockObject(WHITE_BRUSH);
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
		if( !Profile::getStr("System/WindowXPos").empty() )
			sMainWindowPos.x = Profile::getInt("System/WindowXPos");
		if( !Profile::getStr("System/WindowYPos").empty() )
			sMainWindowPos.x = Profile::getInt("System/WindowYPos");
		sMainWindowPosInit = true;
	}

	// Create main app window
	sMainWindow = CreateWindowExW(
		isMainWindowHidden
			? (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_APPWINDOW)
			: 0,
		kMainWindowClassName,
		aMainWindowName.c_str(),
		isMainWindowHidden
			? WS_POPUP
			: WS_SYSMENU | WS_OVERLAPPEDWINDOW
				&~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
		sMainWindowPos.x, sMainWindowPos.y,
		aMainWindowWidth, aMainWindowHeight,
		NULL, NULL, theAppInstanceHandle, NULL);

	if( !sMainWindow )
	{
		logFatalError("Could not create main application window!");
		return;
	}

	ShowWindow(sMainWindow, SW_SHOW);

	// Set overlay client area to full main screen initially
	RECT aScreenRect = { 0, 0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN) };
	resize(aScreenRect, false);
}


void createOverlays(HINSTANCE theAppInstanceHandle)
{
	sIconCopyMethod = EIconCopyMethod(
		clamp(Profile::getInt("System/IconCopyMethod"),
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

	// Create one transparent overlay window per HUD Element
	sOverlayWindows.reserve(InputMap::hudElementCount());
	sOverlayWindows.resize(InputMap::hudElementCount());
	sOverlayWindowOrder.reserve(sOverlayWindows.size());
	sOverlayWindowOrder.resize(sOverlayWindows.size());
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		sOverlayWindowOrder[i].id = u16(i);
		sOverlayWindowOrder[i].priority = HUD::drawPriority(u16(i));
	}
	std::sort(sOverlayWindowOrder.begin(), sOverlayWindowOrder.end());
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];
		const bool isSystemOverlay =
			InputMap::hudElementType(aHUDElementID) == eHUDType_System;
		HUD::updateWindowLayout(aHUDElementID,
			sTargetSize, aWindow.components,
			aWindow.position, aWindow.size,
			sTargetClipRect);
		aWindow.layoutUpdated = true;
		aWindow.windowUpdated = false;
		gReshapeHUD.reset(aHUDElementID);

		aWindow.handle = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_NOACTIVATE |
			WS_EX_TRANSPARENT | WS_EX_LAYERED,
			isSystemOverlay
				? kSystemOverlayWindowClassName : kOverlayWindowClassName,
			widen(InputMap::hudElementKeyName(aHUDElementID)).c_str(),
			WS_POPUP | (sUseChildWindows ? WS_CHILD : 0),
			aWindow.position.x,
			aWindow.position.y,
			aWindow.size.cx,
			aWindow.size.cy,
			sUseChildWindows ? sMainWindow : NULL,
			NULL, theAppInstanceHandle, NULL);

		SetWindowLongPtr(aWindow.handle, GWLP_USERDATA, aHUDElementID);
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
	for(size_t i = 0; i < sOverlayWindows.size(); ++i)
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


void update()
{
	if( sWindowInModalMode || gReloadProfile || gShutdown )
		return;

	sInDialogMode = false;
	setMainWindowEnabled(true);

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
	POINT anOriginPoint = { 0, 0 };
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

		// Check for flag that need to update layout
		if( gReshapeHUD.test(aHUDElementID) )
		{
			aWindow.layoutUpdated = false;
			gReshapeHUD.reset(aHUDElementID);
		}

		// Update alpha fade effects based on gVisibleHUD & gActiveHUD
		updateAlphaFades(aWindow, aHUDElementID);

		// Check visibility status so can mostly ignore hidden windows
		if( sHidden || aWindow.alpha == 0 ||
			(aWindow.layoutUpdated && aWindow.size.cx <= 0) ||
			(aWindow.layoutUpdated && aWindow.size.cy <= 0) )
		{
			if( IsWindowVisible(aWindow.handle) )
				ShowWindow(aWindow.handle, SW_HIDE);
			gActiveHUD.reset(aHUDElementID);
			if( aWindow.bitmap &&
				InputMap::hudElementType(aHUDElementID) == eHUDType_System )
			{// Large bitmap that's rarely needed, so free it from memory
				DeleteObject(aWindow.bitmap);
				aWindow.bitmap = null;
			}
			continue;
		}

		// Check for possible update to window layout
		if( !aWindow.layoutUpdated )
		{
			HUD::updateWindowLayout(aHUDElementID, sTargetSize,
				aWindow.components, aWindow.position,
				aWindow.size, sTargetClipRect);
			aWindow.layoutUpdated = true;
			aWindow.windowUpdated = false;
		}

		// Delete bitmap if bitmap size doesn't match window size
		if( aWindow.bitmap &&
			(aWindow.bitmapSize.cx != aWindow.size.cx ||
			 aWindow.bitmapSize.cy != aWindow.size.cy) )
		{
			DeleteObject(aWindow.bitmap);
			aWindow.bitmap = NULL;
		}

		// Don't create bitmap for a 0-sized window (likely off screen edge)
		if( aWindow.size.cx <= 0 || aWindow.size.cy <= 0 )
		{
			aWindow.size.cx = aWindow.size.cy = 0;
			ShowWindow(aWindow.handle, SW_HIDE);
			gActiveHUD.reset(aHUDElementID);
			continue;
		}

		// Create bitmap if doesn't exist
		if( !aWindow.bitmap )
		{
			gFullRedrawHUD.set(aHUDElementID);
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
		if( gRedrawHUD.test(aHUDElementID) ||
			gFullRedrawHUD.test(aHUDElementID) )
		{
			HUD::drawElement(
				aWindowDC, aCaptureDC, aCaptureOffset, sTargetSize,
				aHUDElementID, aWindow.components,
				gFullRedrawHUD.test(aHUDElementID));
			aWindow.windowUpdated = false;
			gRedrawHUD.reset(aHUDElementID);
			gFullRedrawHUD.reset(aHUDElementID);
		}

		// Update window
		if( !aWindow.windowUpdated )
		{
			BLENDFUNCTION aBlendFunction = {AC_SRC_OVER, 0, aWindow.alpha, 0};
			POINT aWindowScreenPos;
			aWindowScreenPos.x = sScreenTargetRect.left + aWindow.position.x;
			aWindowScreenPos.y = sScreenTargetRect.top + aWindow.position.y;
			UpdateLayeredWindow(aWindow.handle, aScreenDC,
				&aWindowScreenPos, &aWindow.size,
				aWindowDC, &anOriginPoint,
				HUD::transColor(aHUDElementID),
				&aBlendFunction, ULW_ALPHA | ULW_COLORKEY);
			aWindow.windowUpdated = true;
		}
		gActiveHUD.reset(aHUDElementID);

		// Show window if it isn't visible yet
		if( !IsWindowVisible(aWindow.handle) )
			ShowWindow(aWindow.handle, SW_SHOWNOACTIVATE);

		// Cleanup
		RestoreDC(aWindowDC, aWindowDCInitialState);
		DeleteDC(aWindowDC);
	}
	ReleaseDC(NULL, aCaptureDC);
	ReleaseDC(NULL, aScreenDC);

	if( sToolbarWindow && !sHidden && !IsWindowVisible(sToolbarWindow) )
		ShowWindow(sToolbarWindow, SW_SHOW);
}


void prepareForDialog()
{
	stopModalModeUpdates();
	setMainWindowEnabled(false);
	sInDialogMode = true;
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

		if( IsWindowVisible(aWindow.handle) )
			ShowWindow(aWindow.handle, SW_HIDE);
	}
	if( sToolbarWindow && IsWindowVisible(sToolbarWindow) )
		ShowWindow(sToolbarWindow, SW_HIDE);
}


void endDialogMode()
{
	if( !sInDialogMode )
		return;
	
	setMainWindowEnabled(true);
	sInDialogMode = false;
}


void stopModalModeUpdates()
{
	if( !sWindowInModalMode )
		return;
	if( sModalModeTimer )
		CancelWaitableTimer(sModalModeTimer);
	sWindowInModalMode = false;
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

	SIZE aNewTargetSize;
	aNewTargetSize.cx = theNewWindowRect.right - theNewWindowRect.left;
	aNewTargetSize.cy = theNewWindowRect.bottom - theNewWindowRect.top;
	if( aNewTargetSize.cx != sTargetSize.cx ||
		aNewTargetSize.cy != sTargetSize.cy ||
		gReloadProfile )
	{
		sTargetSize = aNewTargetSize;
		updateUIScale();
	}

	sDesktopTargetRect = sScreenTargetRect = theNewWindowRect;
	sDesktopTargetRect.left -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.right -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.top -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	sDesktopTargetRect.bottom -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	sTargetClipRect = sScreenTargetRect;
	if( !isTargetAppWindow )
	{// Restrict to "work" area of active monitor when don't have a target app
		if( HMONITOR hMonitor =
				MonitorFromRect(&sScreenTargetRect, MONITOR_DEFAULTTONEAREST) )
		{
			MONITORINFO aMonitorInfo = { sizeof(MONITORINFO) };
			if( GetMonitorInfo(hMonitor, &aMonitorInfo) )
			{
				if( !IntersectRect(
						&sTargetClipRect,
						&sScreenTargetRect,
						&aMonitorInfo.rcWork) )
				{
					sTargetClipRect = sScreenTargetRect;
				}
			}
		}
	}
	OffsetRect(&sTargetClipRect,
		-sScreenTargetRect.left, -sScreenTargetRect.top);

	for(u16 i = 0; i < sOverlayWindows.size(); ++i)
		sOverlayWindows[i].layoutUpdated = false;
}


void resetOverlays()
{
	sHidden = false;
	RECT aScreenRect = { 0, 0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN) };
	resize(aScreenRect, false);
}


void hideOverlays()
{
	sHidden = true;
}


void showOverlays()
{
	sHidden = false;
}


void setOverlaysToTopZ()
{
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

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


RECT overlayTargetScreenRect()
{
	return sScreenTargetRect;
}


RECT overlayTargetDesktopRect()
{
	return sDesktopTargetRect;
}


void updateUIScale()
{
	gUIScale = Profile::getFloat("System/UIScale", 1.0f);

	const std::string& aUIScaleRegKey =
		Profile::getStr("System/UIScaleRegKey");
	if( !aUIScaleRegKey.empty() )
	{
		HKEY hKey;
		const std::string& aPrefix = upper(getFileName(aUIScaleRegKey));
		std::string aSubKey = upper(getFileDir(aUIScaleRegKey));
		const std::string kRootKey = "HKEY_CURRENT_USER";
		if( aSubKey.substr(0, kRootKey.size()) == kRootKey )
			aSubKey = aSubKey.substr(kRootKey.size()+1);

		if( RegOpenKeyExA(HKEY_CURRENT_USER, aSubKey.c_str(),
			0, KEY_READ, &hKey) != ERROR_SUCCESS )
			return;

		union { u8 buf[sizeof(double)]; double val; } aDoubleValBuffer;
		LONG aResult = 0;
		for(DWORD i = 0; aResult != ERROR_NO_MORE_ITEMS; ++i)
		{
			DWORD dataSize = sizeof(aDoubleValBuffer);
			char aValueName[256];
			DWORD aValueNameSize = ARRAYSIZE(aValueName);
			DWORD type;
			aResult = RegEnumValueA(
				hKey, i, aValueName, &aValueNameSize, NULL, &type,
				&aDoubleValBuffer.buf[0], &dataSize);			
			if( aResult != ERROR_SUCCESS )
				continue;

			if( aValueNameSize > aPrefix.size() &&
				upper(aValueName).substr(0, aPrefix.size()) == aPrefix )
			{
				gUIScale = aDoubleValBuffer.val;
				break;
			}
		}
		RegCloseKey(hKey);
	}

	const int aUIScaleBaseHeight =
		Profile::getInt("System/UIScaleBaseHeight");
	if( aUIScaleBaseHeight > 0 )
		gUIScale *= double(sTargetSize.cy) / aUIScaleBaseHeight;

	for(u16 i = 0; i < sOverlayWindows.size(); ++i)
		sOverlayWindows[i].layoutUpdated = false;
	HUD::updateScaling();
}


void showTargetWindowFound()
{
	HUD::flashSystemWindowBorder();
}


HWND createToolbarWindow(int theResID, DLGPROC theProc, int theHUDElementID)
{
	destroyToolbarWindow();
	sToolbarDialogProc = theProc;
	sToolbarWindow = CreateDialogParam(
		GetModuleHandle(NULL),
		MAKEINTRESOURCE(theResID),
		NULL, toolbarWindowProc, 0);
	sToolbarWindowHUDElementID = theHUDElementID;
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
	sToolbarWindowHUDElementID = -1;
}


void setSystemOverlayCallbacks(WNDPROC theProc, SystemPaintFunc thePaintFunc)
{
	HUD::setSystemOverlayDrawHook(thePaintFunc);
	if( sSystemOverlayWindow && theProc != sSystemOverlayProc )
	{
		sSystemOverlayProc = theProc;
		LONG exStyle = GetWindowLong(sSystemOverlayWindow, GWL_EXSTYLE);
		if( theProc != NULL )
			exStyle &= ~WS_EX_TRANSPARENT;
		else
			exStyle |= WS_EX_TRANSPARENT;
		SetWindowLong(sSystemOverlayWindow, GWL_EXSTYLE, exStyle);
		SetWindowPos(sSystemOverlayWindow, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
}


POINT mouseToOverlayPos(bool clamped)
{
	POINT result;
	//  Get current screen-relative mouse position
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
	result.x = theHotspot.x.anchor;
	result.y = theHotspot.y.anchor;
	// Convert to client-rect-relative pixel position
	result.x = u16ToRangeVal(result.x, sTargetSize.cx);
	result.y = u16ToRangeVal(result.y, sTargetSize.cy);
	// Add pixel offset w/ UI Scale applied
	result.x += theHotspot.x.offset * gUIScale;
	result.y += theHotspot.y.offset * gUIScale;
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
	theMousePos.x = max(0, theMousePos.x + sDesktopTargetRect.left);
	theMousePos.y = max(0, theMousePos.y + sDesktopTargetRect.top);
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
	theSentMousePos.x = u16ToRangeVal(theSentMousePos.x, kDesktopWidth);
	theSentMousePos.y = u16ToRangeVal(theSentMousePos.y, kDesktopHeight);
	// Offset to be client relative position
	theSentMousePos.x -= sDesktopTargetRect.left;
	theSentMousePos.y -= sDesktopTargetRect.top;
	return theSentMousePos;
}


Hotspot hotspotForMenuItem(u16 theMenuID, u16 theMenuItemIdx)
{
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

	switch(InputMap::menuStyle(theMenuID))
	{
	case eMenuStyle_Slots:
		// Only ever allow pointing at the top component (selected item)
		theMenuItemIdx = 0;
		break;
	}

	const size_t aCompIndex =
		min(aWindow.components.size()-1, theMenuItemIdx + 1);
	if( !aWindow.layoutUpdated || gReshapeHUD.test(aHUDElementID) )
	{
		HUD::updateWindowLayout(aHUDElementID, sTargetSize,
			aWindow.components, aWindow.position,
			aWindow.size, sTargetClipRect);
		aWindow.layoutUpdated = true;
		aWindow.windowUpdated = false;
		gReshapeHUD.reset(aHUDElementID);
	}
	POINT aPos;
	aPos.x = aWindow.components[aCompIndex].left;
	aPos.y = aWindow.components[aCompIndex].top;
	aPos.x += aWindow.components[aCompIndex].right;
	aPos.y += aWindow.components[aCompIndex].bottom;
	aPos.x /= 2;
	aPos.y /= 2;
	aPos.x += aWindow.position.x;
	aPos.y += aWindow.position.y;

	return overlayPosToHotspot(aPos);
}


RECT hudElementRect(u16 theHUDElementID)
{
	OverlayWindow& aWindow = sOverlayWindows[theHUDElementID];
	if( !aWindow.layoutUpdated || gReshapeHUD.test(theHUDElementID) )
	{
		HUD::updateWindowLayout(theHUDElementID, sTargetSize,
			aWindow.components, aWindow.position,
			aWindow.size, sTargetClipRect);
		aWindow.layoutUpdated = true;
		aWindow.windowUpdated = false;
		gReshapeHUD.reset(theHUDElementID);
	}

	RECT result;
	result.left = aWindow.position.x - sScreenTargetRect.left;
	result.top = aWindow.position.y - sScreenTargetRect.top;
	result.right = result.left + aWindow.size.cx;
	result.bottom = result.top + aWindow.size.cy;
	return result;
}

} // WindowManager
