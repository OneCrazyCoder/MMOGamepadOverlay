//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "TargetApp.h"

#include "OverlayWindow.h"
#include "Profile.h"

namespace TargetApp
{

//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	std::string targetAppLaunchString;
	std::wstring targetWindowName;
	u32 targetCheckFrequency;
	bool autoCloseWithTargetApp;
	bool autoCloseWithTargetWindow;

	void load()
	{
		targetAppLaunchString = Profile::getStr("System/AutoLaunchApp");
		targetWindowName = widen(Profile::getStr("System/TargetWindowName"));
		autoCloseWithTargetApp = Profile::getBool("System/QuitWhenAutoLaunchAppDoes");
		autoCloseWithTargetWindow = Profile::getBool("System/QuitWhenTargetWindowCloses");
		targetCheckFrequency = Profile::getInt("System/TargetWindowCheckRate", 250);
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static HANDLE sTargetAppProcess = NULL;
static HWND sTargetWindowHandle = NULL;
static RECT sTargetWindowRect = { 0 };
static int sTimeUntilNextCheck = -500; // check every update initially
static bool sHaveTriedAutoLaunch = false;


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
}


void autoLaunch()
{
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
		sTargetAppProcess = pi.hProcess;
	}
	else
	{
		logError("Failed to auto-launch target application: %s",
			narrow(aFinalPath).c_str());
		gHadFatalError = true;
	}
}


void cleanup()
{
	if( sTargetAppProcess )
		CloseHandle(sTargetAppProcess);
	sTargetAppProcess = NULL;
	sTargetWindowHandle = NULL;
}


void update()
{
	// Don't need to check every update most of the time
	if( sTimeUntilNextCheck >= 0 )
	{// Check once per 'kConfig.targetCheckFrequency' milliseconds
		sTimeUntilNextCheck -= gAppFrameTime;
		if( sTimeUntilNextCheck > 0 )
			return;
		sTimeUntilNextCheck = kConfig.targetCheckFrequency;
	}
	else
	{// Check every update until timer goes positive
		sTimeUntilNextCheck += gAppFrameTime;
	}

	// Check status of auto-launched target app
	if( sTargetAppProcess &&
		!WaitForSingleObject(sTargetAppProcess, 0) )
	{// Application was launched but has since closed
		CloseHandle(sTargetAppProcess);
		sTargetAppProcess = NULL;
		if( kConfig.autoCloseWithTargetApp )
			PostQuitMessage(0);
	}

	// Stop if target window tracking was not requested anyway
	if( kConfig.targetWindowName.empty() )
		return;

	// Check status of (or initially find) target window
	HWND aForegroundWindow = GetForegroundWindow();
	if( sTargetWindowHandle != aForegroundWindow )
	{
		wchar_t wczTitle[256];
		GetWindowText(aForegroundWindow, wczTitle, sizeof(wczTitle));
		if( kConfig.targetWindowName == wczTitle )
		{// Target window found!
			sTargetWindowHandle = aForegroundWindow;
			// Trigger resize check below
			ZeroMemory(&sTargetWindowRect, sizeof(RECT));
		}
	}

	// Stop if have no target window handle (yet)
	if( !sTargetWindowHandle )
		return;

	if( !IsWindow(sTargetWindowHandle) )
	{// Target window must have closed
		sTargetWindowHandle = NULL;
		if( kConfig.autoCloseWithTargetWindow )
			PostQuitMessage(0);
		else
			OverlayWindow::minimize();
		ZeroMemory(&sTargetWindowRect, sizeof(RECT));
		return;
	}

	if( sTargetWindowHandle != aForegroundWindow )
	{// Target window found but no longer in foreground
		OverlayWindow::minimize();
		// Once found again, below size check will restore from minimize
		ZeroMemory(&sTargetWindowRect, sizeof(RECT));
		return;
	}

	RECT aWRect;
	GetWindowRect(sTargetWindowHandle, &aWRect);
	if( aWRect.left != sTargetWindowRect.left ||
		aWRect.top != sTargetWindowRect.top ||
		aWRect.right != sTargetWindowRect.right ||
		aWRect.bottom != sTargetWindowRect.bottom )
	{// Target window has been moved/resized/activated
		sTargetWindowRect = aWRect;
		OverlayWindow::resize(aWRect);
		// Check every update for a bit in case dragging window around
		sTimeUntilNextCheck = -500;
	}
}

} // TargetApp
