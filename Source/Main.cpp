//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

/*
	Contains application entry point (WinMain). Initializes all other
	modules and runs the main application loop. The loop handles Windows
	messages and calls update functions for other modules. Finally, checks
	for quit message to exit the loop, finalize all modules, and shut down.
*/

#include "Common.h"

#include "Gamepad.h"
#include "HotspotMap.h"
#include "HUD.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "InputTranslator.h"
#include "LayoutEditor.h"
#include "Menus.h"
#include "Profile.h"
#include "TargetApp.h"
#include "TargetConfigSync.h"
#include "WindowManager.h"

#ifdef _DEBUG
// Scan for memory leaks
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static u64 sAppStartTime = 0;
static u64 sUpdateStartTime = 0;
static LARGE_INTEGER sSystemTimeFreq;
static u32 sUpdateLoopCount = 0;
static bool sUpdateLoopStarted = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static u64 getSystemTime()
{
	LARGE_INTEGER aCurrentTime;
	QueryPerformanceCounter(&aCurrentTime);
	return aCurrentTime.QuadPart / sSystemTimeFreq.QuadPart;
}


//-----------------------------------------------------------------------------
// Global Main Loop Functions (also used in Dialogs.cpp and WindowManager.cpp)
//-----------------------------------------------------------------------------

void mainTimerUpdate()
{
	u64 aCurrentTime = getSystemTime();
	gAppFrameTime = aCurrentTime - sUpdateStartTime;
	gAppRunTime = u32(aCurrentTime - sAppStartTime);
	sUpdateStartTime = aCurrentTime;
}


void mainLoopUpdate(HWND theDialog)
{
	// Don't process this again until mainLoopSleep() is called
	if( sUpdateLoopStarted )
		return;
	sUpdateLoopStarted = true;
	mainTimerUpdate();

	MSG aWindowsMessage = MSG();
	while(PeekMessage(&aWindowsMessage, NULL, 0, 0, PM_REMOVE))
	{
		switch(aWindowsMessage.message)
		{
		case WM_DEVICECHANGE:
			Gamepad::checkDeviceChange();
			break;
		case WM_HOTKEY:
			switch(aWindowsMessage.wParam)
			{
			case kSwapWindowModeHotkeyID:
				TargetApp::swapWindowMode();
				break;
			case kCancelToolbarHotkeyID:
				if( WindowManager::toolbarHandle() )
				{
					SendDlgItemMessage(WindowManager::toolbarHandle(),
						IDCANCEL, BM_CLICK, 0 ,0);
				}
				break;
			}
			break;
		case WM_QUIT:
			gShutdown = true;
			break;
		default:
			if( !theDialog && WindowManager::toolbarHandle() )
				theDialog = WindowManager::toolbarHandle();
			if( !theDialog ||
				!IsDialogMessage(theDialog, &aWindowsMessage) )
			{
				TranslateMessage(&aWindowsMessage);
				DispatchMessage(&aWindowsMessage);
			}
			break;
		}
	}

	WindowManager::stopModalModeUpdates();
}


void mainModulesUpdate()
{
	if( gLoadNewProfile )
		return;

	Gamepad::update();
	HotspotMap::update();
	InputTranslator::update();
	InputDispatcher::update();
	TargetApp::update();
	HUD::update();
	WindowManager::update();
	TargetConfigSync::update();
}


void mainLoopSleep()
{
	// Don't process this again until mainLoopUpdate() is called
	if( !sUpdateLoopStarted )
		return;
	sUpdateLoopStarted = false;

	// Sleep for half the remaining frame time (full amount can overr-sleep)
	const int aTimeTakenByUpdate = int(getSystemTime() - sUpdateStartTime);
	const int aTimeToSleep = gAppTargetFrameTime - aTimeTakenByUpdate;
	Sleep(DWORD(max(1, aTimeToSleep / 2)));

	// Wait out the rest of the frame time
	u64 aFrameTimePassed = getSystemTime() - sUpdateStartTime;
	while(aFrameTimePassed < gAppTargetFrameTime)
	{
		Sleep(0);
		aFrameTimePassed = getSystemTime() - sUpdateStartTime;
	}

	++sUpdateLoopCount;
}


void mainLoopTimeSkip()
{
	sUpdateStartTime = getSystemTime();
}


//-----------------------------------------------------------------------------
// Application entry point - WinMain
//-----------------------------------------------------------------------------

INT APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, INT cmd_show)
{
	#ifdef _DEBUG
	// Change to the number in {} of memory leaks detected to break on alloc
	_crtBreakAlloc = -1; // -1 when not hunting down memory leaks
	_CrtSetDbgFlag (_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);
	#endif

	// Get high-resolution system timer
	QueryPerformanceFrequency(&sSystemTimeFreq);
	sSystemTimeFreq.QuadPart /= 1000; // milliseconds instead of seconds

	HANDLE hMutex = CreateMutex(NULL, TRUE, L"MMOGamepadOverlay");
	if( GetLastError() == ERROR_ALREADY_EXISTS )
	{
		MessageBox(NULL,
			L"Another instance of this application is already running.",
			L"Application Already Running",
			MB_OK | MB_ICONEXCLAMATION);
		return 0;
	}

	// Initialize gamepad module so can use it in initial dialogs
	Gamepad::init();

	// Load core profile to get system settings
	Profile::loadCore();
	gAppTargetFrameTime = max(1,
		Profile::getInt("System", "FrameTime",
		gAppTargetFrameTime));

	// Initiate frame timing
	timeBeginPeriod(gAppTargetFrameTime / 2);
	sAppStartTime = sUpdateStartTime = getSystemTime();

	while(gLoadNewProfile && !gShutdown && !hadFatalError())
	{
		// Load current profile
		Profile::load();

		// Overwrite profile properties with game config file ones
		if( !gShutdown && !hadFatalError() )
			TargetConfigSync::load();

		// Create main application window
		if( !gShutdown && !hadFatalError() )
			WindowManager::createMain(hInstance);

		// Load configuration settings for each module from profile
		if( !gShutdown && !hadFatalError() )
		{
			gAppTargetFrameTime = max(1,
				Profile::getInt("System", "FrameTime",
				gAppTargetFrameTime));
			InputMap::loadProfile();
			HotspotMap::init();
			Menus::init();
			InputTranslator::loadProfile();
			InputDispatcher::loadProfile();
			TargetApp::loadProfile();
			HUD::init();
			WindowManager::createOverlays(hInstance);
		}

		if( !gShutdown && !hadFatalError() )
		{
			// Check status of XInput "double input" fix
			if( !TargetApp::targetWindowIsActive() )
				Profile::confirmXInputFix(WindowManager::mainHandle());

			// Launch target app if requested and haven't already
			TargetApp::autoLaunch();

			// Finalize profile load
			Profile::clearChangedSections();
			Profile::saveChangesToFile();
		}

		// Main loop
		gLoadNewProfile = false;
		while(!gShutdown && !gLoadNewProfile && !hadFatalError())
		{
			// Check if need to react to changed Profile data
			if( !Profile::changedSections().empty() )
			{
				TargetConfigSync::loadProfileChanges();
				InputMap::loadProfileChanges();
				HotspotMap::loadProfileChanges();
				Menus::loadProfileChanges();
				InputTranslator::loadProfileChanges();
				InputDispatcher::loadProfileChanges();
				TargetApp::loadProfile();
				HUD::loadProfileChanges();
				WindowManager::loadProfileChanges();
				Profile::clearChangedSections();
			}

			// Update frame timers and process windows messages
			mainLoopUpdate(NULL);

			// Update modules
			mainModulesUpdate();

			// Yield via Sleep() so sent input can be processed by target
			mainLoopSleep();
		}

		Profile::saveChangesToFile();

		// Cleanup
		TargetConfigSync::cleanup();
		HUD::cleanup();
		Menus::cleanup();
		InputDispatcher::cleanup();
		InputTranslator::cleanup();
		HotspotMap::cleanup();
		LayoutEditor::cleanup();
		if( !hadFatalError() )
			WindowManager::destroyAll(hInstance);
	}
	timeEndPeriod(gAppTargetFrameTime / 2);

	// Report performance
	if( !hadFatalError() && gAppRunTime > 0 )
	{
		const double averageFPS = sUpdateLoopCount / (gAppRunTime / 1000.0);
		debugPrint("Average FPS: %.2f\n", averageFPS);
	}

	// Note that MessageBox self-closes immediately if WM_QUIT has been
	// posted, meaning this will fail to report any fatal errors that
	// happen after user manually closes the main window. Will instead
	// need to check errorlog.txt for those (if it matters by then).
	if( hadFatalError() )
	{
		MessageBox(
			WindowManager::mainHandle(),
			gErrorString.c_str(),
			L"MMO Gamepad Overlay Error",
			MB_OK | MB_ICONERROR);
		WindowManager::destroyAll(hInstance);
	}

	// Final cleanup
	Gamepad::cleanup();
	TargetApp::cleanup();
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);

	return hadFatalError() ? EXIT_FAILURE : EXIT_SUCCESS;
}
