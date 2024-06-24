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
#include "Menus.h"
#include "Profile.h"
#include "TargetApp.h"
#include "WindowManager.h"

#ifdef _DEBUG
// Scan for memory leaks
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static DWORD sMillisecsPerUpdate = 14; // allows >= 60 fps without taxing CPU
static DWORD sLastUpdateTime = 0;
static DWORD sUpdateStartTime = 0;
static u32 sUpdateLoopCount = 0;
static bool sUpdateLoopStarted = false;


//-----------------------------------------------------------------------------
// Global Main Loop Functions (also used in Dialogs.cpp)
//-----------------------------------------------------------------------------

void mainLoopUpdate(HWND theDialog)
{
	// Don't process this again until mainLoopSleep() is called
	if( sUpdateLoopStarted )
		return;
	sUpdateLoopStarted = true;

	sUpdateStartTime = timeGetTime();
	gAppFrameTime = sUpdateStartTime - sLastUpdateTime;
	gAppRunTime += gAppFrameTime;
	sLastUpdateTime = sUpdateStartTime;

	MSG aWindowsMessage = MSG();
	while(PeekMessage(&aWindowsMessage, NULL, 0, 0, PM_REMOVE))
	{
		switch(aWindowsMessage.message)
		{
		case WM_DEVICECHANGE:
			Gamepad::checkDeviceChange();
			break;
		case WM_HOTKEY:
			if( aWindowsMessage.wParam == kSwapWindowModeHotkeyID )
				TargetApp::swapWindowMode();
			break;
		case WM_QUIT:
			gShutdown = true;
			break;
		default:
			if( !theDialog ||
				!IsDialogMessage(theDialog, &aWindowsMessage) )
			{
				TranslateMessage(&aWindowsMessage);
				DispatchMessage(&aWindowsMessage);
			}
			break;
		}
	}
}


void mainLoopSleep()
{
	// Don't process this again until mainLoopUpdate() is called
	if( !sUpdateLoopStarted )
		return;
	sUpdateLoopStarted = false;

	const DWORD aTimeTakenByUpdate = timeGetTime() - sUpdateStartTime;
	if( aTimeTakenByUpdate < sMillisecsPerUpdate )
		Sleep(sMillisecsPerUpdate - aTimeTakenByUpdate);
	else
		Sleep(1);

	++sUpdateLoopCount;
}


void mainLoopTimeSkip()
{
	sLastUpdateTime = sUpdateStartTime = timeGetTime();
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
	sMillisecsPerUpdate = (DWORD)
		Profile::getInt("System/FrameTime", sMillisecsPerUpdate);

	sLastUpdateTime = timeGetTime();
	while(gReloadProfile && !gShutdown && !hadFatalError())
	{
		// Load current profile
		Profile::load();
		gReloadProfile = false;

		// Create main application window
		if( !gShutdown && !hadFatalError() )
			WindowManager::createMain(hInstance);

		// Load configuration settings for each module from profile
		if( !gShutdown && !hadFatalError() )
		{
			sMillisecsPerUpdate = (DWORD)
				Profile::getInt("System/FrameTime", sMillisecsPerUpdate);
			InputMap::loadProfile();
			HotspotMap::init();
			InputTranslator::loadProfile();
			InputDispatcher::loadProfile();
			TargetApp::loadProfile();
			Menus::init();
			HUD::init();
			WindowManager::createOverlays(hInstance);
		}

		// Launch target app if requested and haven't already
		if( !gShutdown && !hadFatalError() )
			TargetApp::autoLaunch();

		// Main loop
		while(!gShutdown && !gReloadProfile && !hadFatalError())
		{
			// Update frame timers and process windows messages
			mainLoopUpdate(NULL);

			// Update modules
			if( !gReloadProfile )
			{
				Gamepad::update();
				HotspotMap::update();
				InputTranslator::update();
				InputDispatcher::update();
				TargetApp::update();
				HUD::update();
				WindowManager::update();
			}

			// Yield via Sleep() so sent input can be processed by target
			mainLoopSleep();
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
		}

		// Cleanup
		HUD::cleanup();
		Menus::cleanup();
		InputDispatcher::cleanup();
		InputTranslator::cleanup();
		HotspotMap::cleanup();
		WindowManager::destroyAll(hInstance);
	}

	// Report performance
	if( !hadFatalError() && gAppRunTime > 0 )
	{
		const double averageFPS = sUpdateLoopCount / (gAppRunTime / 1000.0);
		debugPrint("Average FPS: %.2f\n", averageFPS);
	}

	// Final cleanup
	Gamepad::cleanup();
	TargetApp::cleanup();
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);

	return hadFatalError() ? EXIT_FAILURE : EXIT_SUCCESS;
}
