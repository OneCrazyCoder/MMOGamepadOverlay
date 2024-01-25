//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "TargetApp.h"

#include "InputDispatcher.h" // send Alt+Enter to exit full-screen mode
#include "Profile.h"
#include "WindowManager.h"

namespace TargetApp
{

// Whether or not debug messages print depends on which line is commented out
//#define targetDebugPrint(...) debugPrint("TargetApp: " __VA_ARGS__)
#define targetDebugPrint(...) ((void)0)

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
const LONG kFullScreenWindowStyleEx = WS_EX_TOPMOST;
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
	std::string targetAppLaunchString;
	std::wstring targetWindowName;
	bool autoCloseWithTargetApp;
	bool autoCloseWithTargetWindow;
	bool forceFullScreenWindow;
	bool startInFullScreenWindow;

	void load()
	{
		targetAppLaunchString = Profile::getStr("System/AutoLaunchApp");
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
static bool sFullScreenHotkeyRegistered = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

void checkWindowExists()
{
	if( kConfig.targetWindowName.empty() )
		return;

	HWND aForegroundWindow = GetForegroundWindow();
	if( aForegroundWindow == NULL ||
		aForegroundWindow == sTargetWindowHandle ||
		WindowManager::isOwnedByThisApp(aForegroundWindow) )
	{
		// Don't need to worry about this foreground window
		return;
	}

	// Check if foreground window matches name we are looking for
	wchar_t wczTitle[256];
	GetWindowText(aForegroundWindow, wczTitle, sizeof(wczTitle));
	if( kConfig.targetWindowName != wczTitle )
		return;

	// Target window found!
	targetDebugPrint("Target window '%s' found!\n",
		narrow(kConfig.targetWindowName).c_str());
	sTargetWindowHandle = aForegroundWindow;
	sNextCheck = eCheck_WindowPosition;
	sRepeatCheckTime = 0;
}


void checkWindowActive()
{
	if( !sTargetWindowHandle )
		return;

	if( sTargetWindowHandle == GetForegroundWindow() &&
		!IsIconic(sTargetWindowHandle) )
		return;

	if( WindowManager::areOverlaysHidden() )
		return;

	// Target window is not the active foreground window
	// Hide overlay windows until target window is active again
	targetDebugPrint("Target window inactive! Hiding overlays!\n");
	WindowManager::hideOverlays();
	// Trigger checkWindowPosition() to show overlays later
	SetRect(&sTargetWindowRect, 0, 0, 0, 0);
	// Don't interfere with Alt+Enter in other applications
	if( sFullScreenHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kFullScreenHotkeyID);
		sFullScreenHotkeyRegistered = false;
	}
}


void checkWindowZOrder()
{
	// TODO - Fix this with new WindowManager module
	if( !sTargetWindowHandle ||
		sTargetWindowHandle != GetForegroundWindow() ||
		IsIconic(sTargetWindowHandle) )
		return;

	// If Target isn't a TOPMOST, it must be beneath overlays
	if( !(GetWindowLongPtr(sTargetWindowHandle, GWL_EXSTYLE) & WS_EX_TOPMOST) )
		return;

	size_t anOverlayCount = WindowManager::visibleOverlayCount();
	if( anOverlayCount == 0 )
		return;

	// Loop through windows above target and count our overlays
	size_t aFoundCount = 0;
	HWND aWindow = GetWindow(sTargetWindowHandle, GW_HWNDPREV);
	// GetWindow can sometimes cause an infinite loop, so just assume
	// no more than 10+anOverlayCount windows need to be searched
	for(int i = 0; i < 10+anOverlayCount; ++i)
	{
		if( aWindow == NULL )
			break;
		if( aWindow != WindowManager::mainHandle() &&
			WindowManager::isOwnedByThisApp(aWindow) )
		{
			if( ++aFoundCount >= anOverlayCount )
				return;
		}
		aWindow = GetWindow(aWindow, GW_HWNDPREV);
	}

	// Did not find all overlay windows above target!
	targetDebugPrint("Moving overlays back over top of target window!\n");
	WindowManager::refreshZOrder();
	WindowManager::showOverlays();
}


void checkWindowClosed()
{
	if( !sTargetWindowHandle )
		return;

	if( IsWindow(sTargetWindowHandle) )
		return;

	// Target window found but then closed
	sTargetWindowHandle = NULL;
	if( kConfig.autoCloseWithTargetWindow )
	{
		gShutdown = true;
		targetDebugPrint("Target window closed! Shutting down!\n");
	}
	else
	{
		WindowManager::hideOverlays();
		targetDebugPrint("Target window closed! Hiding overlays!\n");
	}
	SetRect(&sTargetWindowRect, 0, 0, 0, 0);
	SetRect(&sTargetWindowRestoreRect, 0, 0, 0, 0);
	sTargetWindowRestoreStyle = 0;
	sTargetWindowRestoreExStyle = 0;
	sTargetWindowRestoreMenu = NULL;
	sLastKnownTargetMode = eWindowMode_Unknown;
	if( sFullScreenHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kFullScreenHotkeyID);
		sFullScreenHotkeyRegistered = false;
	}
}


void checkWindowPosition()
{
	if( !sTargetWindowHandle ||
		sTargetWindowHandle != GetForegroundWindow() ||
		IsIconic(sTargetWindowHandle) )
		return;

	RECT aWRect;
	GetClientRect(sTargetWindowHandle, &aWRect);
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.left);
	ClientToScreen(sTargetWindowHandle, (LPPOINT)&aWRect.right);
	if( !EqualRect(&aWRect, &sTargetWindowRect) )
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


void checkWindowMode()
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
		aCurrMode = eWindowMode_Normal;
		// Save info about normal window mode for restoring it later
		GetWindowRect(sTargetWindowHandle, &sTargetWindowRestoreRect);
		sTargetWindowRestoreStyle =
			aWStyle & ~(WS_POPUP | WS_MINIMIZE | WS_MAXIMIZE);
		sTargetWindowRestoreExStyle =
			aWStyleEx & ~WS_EX_TOPMOST;
		sTargetWindowRestoreMenu = aMenu;
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
		// Send Alt+Enter and hope the app responds by time loop back here
		targetDebugPrint(
			"Attempting to break target out of true Full Screen mode!\n");
		// Make sure don't intercept the Alt+Enter ourselves!
		if( sFullScreenHotkeyRegistered )
		{
			UnregisterHotKey(NULL, kFullScreenHotkeyID);
			sFullScreenHotkeyRegistered = false;
		}
		// Use InputDispatcher to send Alt+Enter to the target app
		Command aCmd;
		aCmd.type = eCmdType_TapKey;
		aCmd.data = VK_RETURN | kVKeyAltFlag;
		InputDispatcher::sendKeyCommand(aCmd);
		// Give some time for the target app to respond to the request
		sNextCheckDelay = 1000;
		sLastKnownTargetMode = eWindowMode_TrueFullScreen;
		return;
	}

	// Intercept Alt+Enter to swap between FSW and normal window mode,
	// avoiding it causing a switch to "true" Full Screen mode. This
	// should work even if Alt+Enter is assigned to the InputMap for
	// the user to switch screen modes via the controller.
	if( !sFullScreenHotkeyRegistered )
	{
		RegisterHotKey(NULL, kFullScreenHotkeyID, MOD_ALT, VK_RETURN);
		sFullScreenHotkeyRegistered = true;
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
		WindowManager::refreshZOrder();
		WindowManager::showOverlays();
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


void checkAppClosed()
{
	if( !kConfig.autoCloseWithTargetApp || !sTargetAppProcess )
		return;

	if( WaitForSingleObject(sTargetAppProcess, 0) )
		return;

	// Application was auto-launched but has since closed
	targetDebugPrint("Auto-launch app closed - shutting down overlay!\n");
	CloseHandle(sTargetAppProcess);
	sTargetAppProcess = NULL;
	gShutdown = true;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
	if( kConfig.startInFullScreenWindow )
		sDesiredTargetMode = eWindowMode_FullScreenWindow;
	else
		sDesiredTargetMode = eWindowMode_Unknown;
}


void autoLaunch()
{
	// Only ever want to try this once
	if( sHaveTriedAutoLaunch )
		return;
	sHaveTriedAutoLaunch = true;

	if( kConfig.targetAppLaunchString.empty() )
		return;

	// Convert given path into a non-const wide string for CreateProcess
	WCHAR aFinalPath[MAX_PATH];
	std::string aPath = kConfig.targetAppLaunchString;
	if( !isAbsolutePath(kConfig.targetAppLaunchString) )
	{
		GetModuleFileName(NULL, aFinalPath, MAX_PATH);
		const std::string aParams = getPathParams(aPath);
		aPath =
			std::string("\"") +
			getFileDir(narrow(aFinalPath), true) +
			removePathParams(aPath) +
			"\"";
		if( !aParams.empty() )
		{
			aPath += " " ;
			aPath += aParams;
		}
	}
	wcsncpy(aFinalPath, widen(aPath).c_str(), MAX_PATH); 

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(STARTUPINFO));
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
	si.cb = sizeof(STARTUPINFO);
	if( CreateProcess(NULL, aFinalPath,
			NULL, NULL, FALSE, 0, NULL,
			NULL, &si, &pi))
	{
		CloseHandle(pi.hThread);
		if( kConfig.autoCloseWithTargetApp )
			sTargetAppProcess = pi.hProcess;
		else
			CloseHandle(pi.hProcess);
	}
	else
	{
		logFatalError("Failed to auto-launch target application: %s",
			narrow(aFinalPath).c_str());
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
	if( sFullScreenHotkeyRegistered )
	{
		UnregisterHotKey(NULL, kFullScreenHotkeyID);
		sFullScreenHotkeyRegistered = false;
	}
}


void update()
{
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


void toggleFullScreenMode()
{
	DBG_ASSERT(kConfig.forceFullScreenWindow);

	if( sDesiredTargetMode == eWindowMode_FullScreenWindow )
	{
		targetDebugPrint("Alt+Enter detected: Requesting normal window\n");
		sDesiredTargetMode = eWindowMode_Normal;
	}
	else
	{
		targetDebugPrint("Alt+Enter detected: Requesting full screen mode\n");
		sDesiredTargetMode = eWindowMode_FullScreenWindow;
	}
	sNextCheck = eCheck_WindowMode;
	sRepeatCheckTime = 0;
}

} // TargetApp
