//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "TargetApp.h"

#include "InputDispatcher.h" // forceReleaseHeldKeys(), send _SwapWindowMode
#include "InputMap.h" // get eSpecialKey_SwapWindowMode
#include "Profile.h"
#include "WindowManager.h"

namespace TargetApp
{

// Uncomment this to print status of target app/window tracking to debug window
//#define TARGET_APP_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum ECheck
{
	eCheck_WindowExists,
	eCheck_WindowActive,
	eCheck_WindowZOrder,
	eCheck_WindowClosed,
	eCheck_WindowPosition,
	eCheck_WindowMode,
	eCheck_AppClosed,

	eCheck_Num
};

enum EWindowMode
{
	eWindowMode_Unknown,
	eWindowMode_Normal,
	eWindowMode_FullScreenWindow,
	eWindowMode_MaybeFullScreen,
	eWindowMode_TrueFullScreen,
};

const LONG kFullScreenWindowStyle = WS_VISIBLE;
const LONG kFullScreenWindowStyleEx = 0;
const LONG kNormalWindowStyle = WS_VISIBLE | WS_CAPTION | WS_MINIMIZEBOX;
const LONG kNormalOnlyStyleFlags = WS_CAPTION | WS_BORDER | WS_THICKFRAME;
const LONG kIgnoredStyleFlags = WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
const LONG kIgnoredStyleExFlags =
	WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR | WS_EX_RTLREADING;


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	std::string targetAppPath;
	std::string targetAppParams;
	std::wstring targetWindowName;
	bool autoCloseWithTargetApp;
	bool autoCloseWithTargetWindow;
	bool forceFullScreenWindow;
	bool startInFullScreenWindow;

	void load()
	{
		targetAppPath = Profile::getStr("System/AutoLaunchApp");
		targetAppParams = Profile::getStr("System/AutoLaunchAppParams");
		targetWindowName = widen(Profile::getStr("System/TargetWindow"));
		if( targetWindowName.empty() )
			targetWindowName = widen(Profile::getStr("System/TargetWindowName"));
		autoCloseWithTargetApp = Profile::getBool("System/QuitWhenAutoLaunchAppDoes");
		autoCloseWithTargetWindow = Profile::getBool("System/QuitWhenTargetWindowCloses");
		forceFullScreenWindow = Profile::getBool("System/ForceFullScreenWindow");
		startInFullScreenWindow = Profile::getBool("System/StartInFullScreenWindow");
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static HANDLE sTargetAppProcess = NULL;
static HWND sTargetWindowHandle = NULL;
static RECT sTargetWindowRect = { 0 };
static RECT sTargetWindowRestoreRect = { 0 };
static LONG sTargetWindowRestoreStyle = 0;
static LONG sTargetWindowRestoreExStyle = 0;
static HMENU sTargetWindowRestoreMenu = NULL;
static ECheck sNextCheck = ECheck(0);
static int sRepeatCheckTime = 1000; // for initial target window search
static int sNextCheckDelay = 0;
static bool sHaveTriedAutoLaunch = false;
static EWindowMode sDesiredTargetMode = eWindowMode_Unknown;
static EWindowMode sLastKnownTargetMode = eWindowMode_Unknown;
static bool sSwapWindowModeHotkeyRegistered = false;
static bool sRestoreTargetWindow = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef TARGET_APP_DEBUG_PRINT
#define targetDebugPrint(...) debugPrint("TargetApp: " __VA_ARGS__)
#else
#define targetDebugPrint(...) ((void)0)
#endif

static void dropTargetWindow()
{
	if( !sTargetWindowHandle )
		return;

	targetDebugPrint("Dropping tracking of former target window\n");
	sTargetWindowHandle = NULL;
	SetRect(&sTargetWindowRect, 0, 0, 0, 0);
	SetRect(&sTargetWindowRestoreRect, 0, 0, 0, 0);
	sTargetWindowRestoreStyle = 0;
	sTargetWindowRestoreExStyle = 0;
	sTargetWindowRestoreMenu = NULL;
	sLastKnownTargetMode = eWindowMode_Unknown;
	WindowManager::resetOverlays();
	if( sSwapWindowModeHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kSwapWindowModeHotkeyID);
		sSwapWindowModeHotkeyRegistered = false;
	}
}


static void restoreTargetWindow()
{
	if( gShutdown || gReloadProfile || WindowManager::toolbarHandle() )
		return;
	sRestoreTargetWindow = false;
	if( !sTargetWindowHandle )
		return;
	// Make sure target window still matches name in current profile
	wchar_t aWStr[256];
	GetWindowText(sTargetWindowHandle, aWStr, ARRAYSIZE(aWStr));
	if( kConfig.targetWindowName != aWStr )
	{
		dropTargetWindow();
		return;
	}

	if( sTargetWindowHandle != GetForegroundWindow() )
	{
		targetDebugPrint("Restoring target window to active window\n");
		SetForegroundWindow(sTargetWindowHandle);
		sNextCheckDelay = 0;
		sRepeatCheckTime = 500;
		sNextCheck = eCheck_WindowZOrder;
	}
}


static void checkWindowExists()
{
	if( kConfig.targetWindowName.empty() )
		return;

	HWND aForegroundWindow = GetForegroundWindow();
	if( aForegroundWindow == NULL ||
		aForegroundWindow == sTargetWindowHandle ||
		WindowManager::isOwnedByThisApp(aForegroundWindow) )
	{// Don't need to worry about this foreground window
		return;
	}

	// Check if foreground window is a known system window
	wchar_t aWStr[256];
	GetClassName(aForegroundWindow, aWStr, ARRAYSIZE(aWStr));
	if( widen("CabinetWClass") == aWStr ||
		widen("ExplorerWClass") == aWStr )
	{// Don't need to worry about this foreground window
		return;
	}

	// Check if foreground window matches name we are looking for
	GetWindowText(aForegroundWindow, aWStr, ARRAYSIZE(aWStr));
	if( kConfig.targetWindowName != aWStr )
		return;

	// Make sure the window is a normal window size (not minimized etc)
	RECT aWRect;
	GetClientRect(aForegroundWindow, &aWRect);
	if( aWRect.right - aWRect.left < 640 ||
		aWRect.bottom - aWRect.top < 480 )
		return;

	// Target window found!
	targetDebugPrint("Target window '%s' found!\n",
		narrow(kConfig.targetWindowName).c_str());
	sTargetWindowHandle = aForegroundWindow;
	sNextCheck = eCheck_WindowPosition;
	sRepeatCheckTime = 0;
	WindowManager::showTargetWindowFound();
}


static void checkWindowActive()
{
	if( !sTargetWindowHandle )
		return;

	HWND aForegroundWindow = GetForegroundWindow();

	if( sTargetWindowHandle == aForegroundWindow &&
		!IsIconic(sTargetWindowHandle) )
		return;

	if( aForegroundWindow == WindowManager::mainHandle() &&
		targetWindowIsTopMost() )
	{// Switch focus from own main window to target if target is topmost
		targetDebugPrint("Switching focus to target window\n");
		SetForegroundWindow(sTargetWindowHandle);
	}

	// Allow toolbar to be active over target, unless target is minimized
	if( WindowManager::toolbarHandle() &&
		aForegroundWindow == WindowManager::toolbarHandle() &&
		!IsIconic(sTargetWindowHandle) )
		return;

	if( WindowManager::overlaysAreHidden() )
		return;

	// Target window is not the active foreground window
	// Hide overlay windows until target window is active again
	targetDebugPrint("Target window inactive! Hiding overlays!\n");
	WindowManager::hideOverlays();
	InputDispatcher::forceReleaseHeldKeys();
	// Trigger checkWindowPosition() to show overlays later
	SetRect(&sTargetWindowRect, 0, 0, 0, 0);
	// Don't interfere with other applications by keeping hotkey registered
	if( sSwapWindowModeHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kSwapWindowModeHotkeyID);
		sSwapWindowModeHotkeyRegistered = false;
	}
}


static void checkWindowZOrder()
{
	if( !sTargetWindowHandle ||
		sTargetWindowHandle != GetForegroundWindow() ||
		IsIconic(sTargetWindowHandle) ||
		WindowManager::overlaysAreHidden() )
		return;

	// If Target isn't a TOPMOST, it must be beneath overlays
	if( !(GetWindowLongPtr(sTargetWindowHandle, GWL_EXSTYLE) & WS_EX_TOPMOST) )
		return;

	// Loop through windows above target and check for any overlay
	// The overlays are all lumped together so if any are above, all should be
	HWND aWindow = GetWindow(sTargetWindowHandle, GW_HWNDPREV);
	// GetWindow can sometimes cause an infinite loop, so just assume
	// no more than 10 windows need to be searched
	for(int i = 0; i < 10; ++i)
	{
		if( aWindow == NULL )
			break;
		if( aWindow != WindowManager::mainHandle() &&
			WindowManager::isOwnedByThisApp(aWindow) )
		{// Overlay window found!
			return;
		}
		aWindow = GetWindow(aWindow, GW_HWNDPREV);
	}

	// Did not find any overlay windows above target!
	targetDebugPrint("Moving overlays back over top of target window\n");
	WindowManager::setOverlaysToTopZ();
}


static void checkWindowClosed()
{
	if( !sTargetWindowHandle )
		return;

	if( IsWindow(sTargetWindowHandle) )
		return;

	// Target window found but then closed
	if( kConfig.autoCloseWithTargetWindow )
	{
		gShutdown = true;
		targetDebugPrint("Target window closed! Shutting down!\n");
	}
	else
	{
		InputDispatcher::forceReleaseHeldKeys();
		WindowManager::resetOverlays();
		targetDebugPrint("Target window closed! Resetting overlays!\n");
	}
	dropTargetWindow();
}


static void checkWindowPosition()
{
	if( !sTargetWindowHandle ||
		sTargetWindowHandle != GetForegroundWindow() ||
		IsIconic(sTargetWindowHandle) )
		return;

	RECT aWRect;
	GetClientRect(sTargetWindowHandle, &aWRect);
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.left);
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.right);
	if( !EqualRect(&aWRect, &sTargetWindowRect) &&
		aWRect.bottom > aWRect.top &&
		aWRect.right > aWRect.left )
	{// Target window has been moved/resized/activated
		targetDebugPrint(
			"Repositioning Overlay to Target (%d x %d -> %d x %d)\n",
			aWRect.left, aWRect.top, aWRect.right, aWRect.bottom);
		sTargetWindowRect = aWRect;
		WindowManager::resize(aWRect);
		WindowManager::showOverlays();
		// Check every update for a bit in case dragging window around
		sRepeatCheckTime = 500;
		sNextCheck = eCheck_WindowPosition;
	}
}


static void checkWindowMode()
{
	if( !kConfig.forceFullScreenWindow ||
		!sTargetWindowHandle ||
		sTargetWindowHandle != GetForegroundWindow() ||
		IsIconic(sTargetWindowHandle) )
		return;

	// Gather info about the window's current mode
	RECT aMonRect = { 0 };
	{
		HMONITOR hMonitor = MonitorFromWindow(
			sTargetWindowHandle, MONITOR_DEFAULTTONEAREST);
		MONITORINFO aMonitor = { sizeof(MONITORINFO) };
		GetMonitorInfo(hMonitor, &aMonitor);
		aMonRect = aMonitor.rcMonitor;
	};
	const LONG aMonRectWidth = aMonRect.right - aMonRect.left;
	const LONG aMonRectHeight = aMonRect.bottom - aMonRect.top;
	const LONG anEpsilon = aMonRectWidth >> 7;

	RECT aWRect;
	GetClientRect(sTargetWindowHandle, &aWRect);
	const LONG aWRectWidth = aWRect.right - aWRect.left;
	const LONG aWRectHeight = aWRect.bottom - aWRect.top;
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.left);
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.right);

	const LONG aWStyle = GetWindowLong(sTargetWindowHandle, GWL_STYLE);
	const LONG aWStyleEx = GetWindowLongPtr(sTargetWindowHandle, GWL_EXSTYLE);
	const HMENU aMenu = GetMenu(sTargetWindowHandle);

	// It is very difficult to detect "true" full screen windows, since
	// they are set up through another system like DirectX rather than
	// normal Windows API. If forceFullScreenWindow is requested though,
	// it is assumed that any full screen mode NOT set by this override
	// code must be "true" full screen mode. Checking for a borderless
	// full-screen-sized window is easy enough, but the trick is knowing
	// the difference between the target app setting it that way vs this
	// code. The method employed here is assuming the target app version
	// won't have the exact same style flags. In particular, WS_POPUP or
	// WS_MAXIMIZE will often be set, but aren't used here (WS_POPUP may
	// be needed to initially create a borderless window, but has no real
	// purpose in modern Windows ater Window creation, and WS_MAXIMIZE
	// isn't needed if manually set the window size).
	EWindowMode aCurrMode = eWindowMode_Unknown;
	if( (aWStyle & kNormalOnlyStyleFlags) || aMenu != NULL )
	{
		if( aWRectWidth >= 640 && aWRectHeight >= 480 )
		{// Save info about normal window mode for restoring it later
			aCurrMode = eWindowMode_Normal;
			GetWindowRect(sTargetWindowHandle, &sTargetWindowRestoreRect);
			sTargetWindowRestoreStyle =
				aWStyle & ~(WS_POPUP | WS_MINIMIZE | WS_MAXIMIZE);
			sTargetWindowRestoreExStyle =
				aWStyleEx & ~WS_EX_TOPMOST;
			sTargetWindowRestoreMenu = aMenu;
		}
	}
	else if( abs(aWRect.left - aMonRect.left) > anEpsilon ||
			 abs(aWRect.top - aMonRect.top) > anEpsilon ||
			 abs(aWRectWidth - aMonRectWidth) > anEpsilon ||
			 abs(aWRectHeight - aMonRectHeight) > anEpsilon )
	{
		// Borderless but not full-screen - splash screen? moved FSW?
		// If were in FSW mode, assume it just moved somehow and
		// set to eWindowMode_Normal to force the FSW back into place
		aCurrMode =
			sLastKnownTargetMode == eWindowMode_FullScreenWindow
				? eWindowMode_Normal : eWindowMode_Unknown;
	}
	else if( sLastKnownTargetMode == eWindowMode_FullScreenWindow &&
			 (aWStyle & ~kIgnoredStyleFlags) == kFullScreenWindowStyle &&
			 (aWStyleEx & ~kIgnoredStyleExFlags) == kFullScreenWindowStyleEx )
	{
		aCurrMode = eWindowMode_FullScreenWindow;
	}
	else if( sLastKnownTargetMode == eWindowMode_MaybeFullScreen )
	{
		aCurrMode = eWindowMode_TrueFullScreen;
		// Once detect "true" full-screen, assume really want FSW
		if( sDesiredTargetMode == eWindowMode_Unknown )
			sDesiredTargetMode = eWindowMode_FullScreenWindow;
	}
	else
	{
		// Probably full screen mode, but might be a temporary transition
		// state. Check again in a second.
		sLastKnownTargetMode = eWindowMode_MaybeFullScreen;
		sNextCheckDelay = 1000;
		return;
	}

	if( aCurrMode == eWindowMode_TrueFullScreen &&
		IsWindow(sTargetWindowHandle) )
	{
		// Can't easily force a window out of "true" full-screen mode...
		// Send a hotkey to request swapping to window mode,
		// and hope the app responds by time loop back here
		targetDebugPrint(
			"Attempting to break target out of true Full Screen mode!\n");
		// Make sure don't intercept the hotkey ourselves!
		if( sSwapWindowModeHotkeyRegistered )
		{
			UnregisterHotKey(NULL, kSwapWindowModeHotkeyID);
			sSwapWindowModeHotkeyRegistered = false;
		}
		// Use InputDispatcher to send hotkey to the target app
		Command aCmd;
		aCmd.type = eCmdType_TapKey;
		aCmd.vKey = InputMap::keyForSpecialAction(eSpecialKey_SwapWindowMode);
		aCmd.signalID = eBtn_Num + eSpecialKey_SwapWindowMode;
		InputDispatcher::sendKeyCommand(aCmd);
		// Give some time for the target app to respond to the request
		sNextCheckDelay = 1000;
		sLastKnownTargetMode = eWindowMode_TrueFullScreen;
		return;
	}

	// Intercept the hotkey to swap between FSW and normal window mode,
	// avoiding it causing a switch to "true" Full Screen mode. This
	// should work even if this keybind is assigned for the user to
	// switch screen modes via the controller.
	if( !sSwapWindowModeHotkeyRegistered )
	{
		const u16 aFullVKey =
			InputMap::keyForSpecialAction(eSpecialKey_SwapWindowMode);
		UINT aVKey = aFullVKey & kVKeyMask;
		UINT aModKey = 0;
		if( aFullVKey & kVKeyShiftFlag ) aModKey |= MOD_SHIFT;
		if( aFullVKey & kVKeyCtrlFlag ) aModKey |= MOD_CONTROL;
		if( aFullVKey & kVKeyAltFlag ) aModKey |= MOD_ALT;
		if( aFullVKey & kVKeyWinFlag ) aModKey |= MOD_WIN;
		RegisterHotKey(NULL, kSwapWindowModeHotkeyID, aModKey, aVKey);
		sSwapWindowModeHotkeyRegistered = true;
	}

	// Mode matches desired mode already (or one of them is unknown)
	if( aCurrMode == sDesiredTargetMode ||
		aCurrMode == eWindowMode_Unknown ||
		sDesiredTargetMode == eWindowMode_Unknown )
		return;

	if( sDesiredTargetMode == eWindowMode_FullScreenWindow )
	{
		targetDebugPrint("Switching target window to Full Screen Window!\n");
		SetWindowLong(sTargetWindowHandle, GWL_STYLE,
			(aWStyle & kIgnoredStyleFlags) | kFullScreenWindowStyle);
		SetWindowLongPtr(sTargetWindowHandle, GWL_EXSTYLE,
			(aWStyleEx & kIgnoredStyleExFlags) | kFullScreenWindowStyleEx);
		SetMenu(sTargetWindowHandle, NULL);
		SetWindowPos(sTargetWindowHandle, HWND_TOPMOST,
			aMonRect.left, aMonRect.top,
			aMonRectWidth, aMonRectHeight,
			SWP_FRAMECHANGED);
		SetWindowPos(sTargetWindowHandle, HWND_NOTOPMOST,
			aMonRect.left, aMonRect.top,
			aMonRectWidth, aMonRectHeight,
			SWP_NOMOVE | SWP_NOSIZE);
		sLastKnownTargetMode = eWindowMode_FullScreenWindow;
		return;
	}

	// If reach this point, should be because requesting normal window
	DBG_ASSERT(sDesiredTargetMode == eWindowMode_Normal);

	if( sTargetWindowRestoreRect.right > sTargetWindowRestoreRect.left &&
		sTargetWindowRestoreRect.bottom > sTargetWindowRestoreRect.top )
	{
		targetDebugPrint("Restoring target window to normal window mode!\n");
		SetWindowLong(sTargetWindowHandle, GWL_STYLE,
			sTargetWindowRestoreStyle);
		SetWindowLongPtr(sTargetWindowHandle, GWL_EXSTYLE,
			sTargetWindowRestoreExStyle);
		SetMenu(sTargetWindowHandle, sTargetWindowRestoreMenu);
		SetWindowPos(sTargetWindowHandle, HWND_NOTOPMOST,
			sTargetWindowRestoreRect.left, sTargetWindowRestoreRect.top,
			sTargetWindowRestoreRect.right - sTargetWindowRestoreRect.left,
			sTargetWindowRestoreRect.bottom - sTargetWindowRestoreRect.top,
			SWP_FRAMECHANGED);
		sLastKnownTargetMode = eWindowMode_Normal;
		return;
	}

	// Want a normal window but somehow don't have valid restore information
	// to get back to it. Have to wing it with some typical default values.
	targetDebugPrint("Forcing target window to a default window style!\n");
	SetWindowLong(sTargetWindowHandle, GWL_STYLE,
		(aWStyle & kIgnoredStyleFlags) | kNormalWindowStyle);
	SetWindowLongPtr(sTargetWindowHandle, GWL_EXSTYLE,
		(aWStyleEx & kIgnoredStyleExFlags));
	RECT aNewRect = { 0, 0, aWRectWidth, aWRectHeight };
	AdjustWindowRect(&aNewRect, kNormalWindowStyle, FALSE);
	LONG aNewWidth = aNewRect.right - aNewRect.left;
	LONG aNewHeight = aNewRect.right - aNewRect.left;
	SetWindowPos(sTargetWindowHandle, HWND_NOTOPMOST,
		aMonRect.left + max(0, (aMonRectWidth - aNewWidth) / 2),
		aMonRect.top + max(0, (aMonRectHeight - aNewHeight) / 2),
		aNewWidth, aNewHeight, SWP_FRAMECHANGED);
	sLastKnownTargetMode = eWindowMode_Normal;
}


static void checkAppClosed()
{
	if( !sTargetAppProcess )
		return;

	if( WaitForSingleObject(sTargetAppProcess, 0) )
		return;

	// Application was auto-launched but has since closed
	if( kConfig.autoCloseWithTargetApp )
	{
		targetDebugPrint(
			"Auto-launch app closed - shutting down overlay!\n");
		gShutdown = true;
	}
	CloseHandle(sTargetAppProcess);
	sTargetAppProcess = NULL;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	targetDebugPrint("Loading new profile data\n");
	kConfig.load();
	if( sTargetWindowHandle && !sRestoreTargetWindow )
		dropTargetWindow();
	if( !sTargetWindowHandle )
	{
		if( kConfig.startInFullScreenWindow )
			sDesiredTargetMode = eWindowMode_FullScreenWindow;
		else
			sDesiredTargetMode = eWindowMode_Unknown;
	}
}


void autoLaunch()
{
	// Only ever want to try this once, even if load alternate profile
	if( sHaveTriedAutoLaunch )
		return;
	sHaveTriedAutoLaunch = true;

	if( kConfig.targetAppPath.empty() )
		return;

	// Convert given path into a non-const wide string for CreateProcess
	WCHAR aFinalPath[MAX_PATH] = { 0 };
	std::string aPath = kConfig.targetAppPath;
	std::string aParams = getPathParams(aPath);
	if( !kConfig.targetAppParams.empty() )
	{
		if( !aParams.empty() )
			aParams.push_back(' ');
		aParams += kConfig.targetAppParams;
	}
	aPath = removePathParams(aPath);
	if( !isAbsolutePath(kConfig.targetAppPath) )
	{
		GetModuleFileName(NULL, aFinalPath, MAX_PATH);
		aPath = getFileDir(narrow(aFinalPath), true) + aPath;
	}
	const std::string aDirPath = getFileDir(aPath);
	if( !aParams.empty() )
		aPath = "\"" + aPath + "\" " + aParams;
	wcsncpy(aFinalPath, widen(aPath).c_str(), MAX_PATH-1);

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(STARTUPINFO));
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
	si.cb = sizeof(STARTUPINFO);
	if( CreateProcess(NULL, aFinalPath,
			NULL, NULL, FALSE, 0, NULL,
			widen(aDirPath).c_str(),
			&si, &pi))
	{
		CloseHandle(pi.hThread);
		if( kConfig.autoCloseWithTargetApp )
			sTargetAppProcess = pi.hProcess;
		else
			CloseHandle(pi.hProcess);
	}
	else
	{
		LPVOID anErrorMessage;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&anErrorMessage, 0, NULL);
		logFatalError("Failed to auto-launch target application: %s\n"
			"Error given: %s",
			narrow(aFinalPath).c_str(),
			narrow((LPWSTR)anErrorMessage).c_str());
		LocalFree(anErrorMessage);
	}
}


void cleanup()
{
	if( sTargetAppProcess )
		CloseHandle(sTargetAppProcess);
	sTargetAppProcess = NULL;
	sTargetWindowHandle = NULL;
	SetRect(&sTargetWindowRect, 0, 0, 0, 0);
	SetRect(&sTargetWindowRestoreRect, 0, 0, 0, 0);
	sTargetWindowRestoreStyle = 0;
	sTargetWindowRestoreExStyle = 0;
	sTargetWindowRestoreMenu = NULL;
	sLastKnownTargetMode = eWindowMode_Unknown;
	if( sSwapWindowModeHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kSwapWindowModeHotkeyID);
		sSwapWindowModeHotkeyRegistered = false;
	}
}


void update()
{
	if( sRestoreTargetWindow )
		restoreTargetWindow();

	if( sNextCheckDelay > 0 )
	{
		sNextCheckDelay -= gAppFrameTime;
		return;
	}

	// Only make one check per update to save CPU, as none of these
	// need an instantaneous response anyway.
	ECheck aCheckType = sNextCheck;
	if( sRepeatCheckTime > 0 )
		sRepeatCheckTime -= gAppFrameTime;
	else
		sNextCheck = ECheck(sNextCheck + 1);

	switch(aCheckType)
	{
	case eCheck_WindowExists:	checkWindowExists();	break;
	case eCheck_WindowActive:	checkWindowActive();	break;
	case eCheck_WindowZOrder:	checkWindowZOrder();	break;
	case eCheck_WindowClosed:	checkWindowClosed();	break;
	case eCheck_WindowPosition:	checkWindowPosition();	break;
	case eCheck_WindowMode:		checkWindowMode();		break;
	case eCheck_AppClosed:		checkAppClosed();		break;
	default: sNextCheck = ECheck(0); // wrap check type
	}
}


void swapWindowMode()
{
	DBG_ASSERT(kConfig.forceFullScreenWindow);

	if( sDesiredTargetMode == eWindowMode_FullScreenWindow )
	{
		targetDebugPrint(
			"Screen mode swap hotkey detected: Requesting normal window\n");
		sDesiredTargetMode = eWindowMode_Normal;
	}
	else
	{
		targetDebugPrint(
			"Screen mode swap hotkey detected: Requesting full screen mode\n");
		sDesiredTargetMode = eWindowMode_FullScreenWindow;
	}
	sNextCheck = eCheck_WindowMode;
	sRepeatCheckTime = 0;
}


void prepareForDialog()
{
	sRestoreTargetWindow = sRestoreTargetWindow ||
		(GetForegroundWindow() == sTargetWindowHandle) ||
		(WindowManager::toolbarHandle() &&
		 GetForegroundWindow() == sTargetWindowHandle);
	// Overlays may get resized while in dialog, so prepare to restore them
	SetRect(&sTargetWindowRect, 0, 0, 0, 0);
	targetDebugPrint("Preparing for dialog box - will%srestore target window\n",
		sRestoreTargetWindow ? " " : " NOT ");
	if( sSwapWindowModeHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kSwapWindowModeHotkeyID);
		sSwapWindowModeHotkeyRegistered = false;
	}
}


HWND targetWindowHandle()
{
	return sTargetWindowHandle;
}


bool targetAppActive()
{
	return sTargetWindowHandle || sTargetAppProcess;
}


bool targetWindowIsTopMost()
{
	if( !sTargetWindowHandle )
		return false;

	if( GetWindowLong(sTargetWindowHandle, GWL_EXSTYLE) & WS_EX_TOPMOST )
		return true;
	
	return false;
}


bool targetWindowIsFullScreen()
{
	if( !sTargetWindowHandle )
		return false;

	// Check if the window style indicates it is a borderless window
	const LONG aWStyle = GetWindowLong(sTargetWindowHandle, GWL_STYLE);
	if( (aWStyle & WS_POPUP) != WS_POPUP && 
		(aWStyle & (WS_CAPTION | WS_THICKFRAME)) != 0 )
	{
		return false;
	}

	// Gather info about the window's current mode
	RECT aMonRect = { 0 };
	{
		HMONITOR hMonitor = MonitorFromWindow(
			sTargetWindowHandle, MONITOR_DEFAULTTONEAREST);
		MONITORINFO aMonitor = { sizeof(MONITORINFO) };
		GetMonitorInfo(hMonitor, &aMonitor);
		aMonRect = aMonitor.rcMonitor;
	};
	const LONG aMonRectWidth = aMonRect.right - aMonRect.left;
	const LONG aMonRectHeight = aMonRect.bottom - aMonRect.top;
	const LONG anEpsilon = aMonRectWidth >> 7;

	RECT aWRect;
	GetClientRect(sTargetWindowHandle, &aWRect);
	const LONG aWRectWidth = aWRect.right - aWRect.left;
	const LONG aWRectHeight = aWRect.bottom - aWRect.top;
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.left);
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.right);

	return
		abs(aWRect.left - aMonRect.left) <= anEpsilon &&
		abs(aWRect.top - aMonRect.top) <= anEpsilon &&
		abs(aWRectWidth - aMonRectWidth) <= anEpsilon &&
		abs(aWRectHeight - aMonRectHeight) <= anEpsilon;
}


bool targetWindowIsActive()
{
	return
		sTargetWindowHandle &&
		GetForegroundWindow() == sTargetWindowHandle;
}

#undef TARGET_APP_DEBUG_PRINT
#undef targetDebugPrint

} // TargetApp
