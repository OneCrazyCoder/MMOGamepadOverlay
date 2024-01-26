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
// Application entry point - WinMain
//-----------------------------------------------------------------------------

INT APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, INT cmd_show)
{
	#ifdef _DEBUG
	// Change to the number in {} of memory leaks detected to break on alloc
	_crtBreakAlloc = -1; // -1 when not hunting down memory leaks 
	_CrtSetDbgFlag (_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);
	#endif

	// Initialize gamepad module so can use it in profile selection
	Gamepad::init();

	DWORD aMillisecsPerUpdate = 14; // allows >= 60 fps without taxing CPU
	while(gReloadProfile && !gShutdown && !hadFatalError())
	{
		// Load profile
		Profile::load();
		gReloadProfile = false;
	
		// Create main application window
		if( !hadFatalError() )
			WindowManager::createMain(hInstance);

		// Load configuration settings for each module from profile
		if( !hadFatalError() )
		{
			aMillisecsPerUpdate = (DWORD)
				Profile::getInt("System/FrameTime", aMillisecsPerUpdate);
			InputMap::loadProfile();
			InputTranslator::loadProfile();
			InputDispatcher::loadProfile();
			TargetApp::loadProfile();
			Menus::init();
			HUD::init();
			WindowManager::createOverlays(hInstance);
		}

		// Launch target app if requested and haven't already
		if( !hadFatalError() )
			TargetApp::autoLaunch();

		// Main loop
		DWORD aLastUpdateTime = timeGetTime();
		while(!gShutdown && !gReloadProfile && !hadFatalError())
		{
			// Update frame timers
			const DWORD anUpdateStartTime = timeGetTime();
			gAppFrameTime = anUpdateStartTime - aLastUpdateTime;
			gAppRunTime += gAppFrameTime;
			aLastUpdateTime = anUpdateStartTime;

			// Handle Windows messages
			MSG aWindowsMessage = MSG();
			while(PeekMessage(&aWindowsMessage, NULL, 0, 0, PM_REMOVE))
			{
				switch(aWindowsMessage.message)
				{
				case WM_DEVICECHANGE:
					Gamepad::checkDeviceChange();
					break;
				case WM_SYSCOLORCHANGE:
					gReloadProfile = true;
					break;
				case WM_HOTKEY:
					if( aWindowsMessage.wParam == kFullScreenHotkeyID )
						TargetApp::toggleFullScreenMode();
					break;
				case WM_QUIT:
					gShutdown = true;
					break;
				default:
					TranslateMessage(&aWindowsMessage);
					DispatchMessage(&aWindowsMessage);
					break;
				}
			}

			// Update modules
			if( !gReloadProfile )
			{
				Gamepad::update();
				InputTranslator::update();
				InputDispatcher::update();
				TargetApp::update();
				HUD::update();
				WindowManager::update();
			}

			// Yield via Sleep() so sent input can be processed by target
			const DWORD aTimeTakenByUpdate = timeGetTime() - anUpdateStartTime;
			if( aTimeTakenByUpdate < aMillisecsPerUpdate )
				Sleep(aMillisecsPerUpdate - aTimeTakenByUpdate);
			else
				Sleep(1);

			++gAppUpdateCount;
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
		WindowManager::destroyAll(hInstance);
	}

	// Report performance
	if( !hadFatalError() )
	{
		const double averageFPS = gAppUpdateCount / (gAppRunTime / 1000.0);
		debugPrint("Average FPS: %.2f\n", averageFPS);
	}

	// Final cleanup
	Gamepad::cleanup();
	TargetApp::cleanup();

	return hadFatalError() ? EXIT_FAILURE : EXIT_SUCCESS;
}
