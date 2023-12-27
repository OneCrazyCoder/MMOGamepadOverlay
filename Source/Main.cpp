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
#include "InputDispatcher.h"
#include "InputMap.h"
#include "InputTranslator.h"
#include "OverlayWindow.h"
#include "Profile.h"

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
	bool wantShutdown = false;
	while(gReloadProfile && !wantShutdown && !gHadFatalError)
	{
		// Load profile
		Profile::load();
		gReloadProfile = false;
	
		// Create transparent overlay window
		if( !gHadFatalError )
			OverlayWindow::create(hInstance);

		// Load configuration settings for each module from profile
		if( !gHadFatalError )
		{
			aMillisecsPerUpdate = (DWORD)
				Profile::getInt("System/FrameTime", aMillisecsPerUpdate);
			InputMap::loadProfile();
			InputTranslator::loadProfile();
			InputDispatcher::loadProfile();
			OverlayWindow::loadProfile();
		}

		// Main loop
		DWORD aLastUpdateTime = timeGetTime();
		while(!wantShutdown && !gReloadProfile && !gHadFatalError)
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
				case WM_QUIT:
					wantShutdown = true;
					break;
				default:
					TranslateMessage(&aWindowsMessage);
					DispatchMessage(&aWindowsMessage);
					break;
				}
			}

			// Update modules
			Gamepad::update();
			InputTranslator::update();
			InputDispatcher::update();
			OverlayWindow::update();

			// Yield via Sleep() so sent input can be processed by target
			const DWORD aTimeTakenByUpdate = timeGetTime() - anUpdateStartTime;
			if( aTimeTakenByUpdate < aMillisecsPerUpdate )
				Sleep(aMillisecsPerUpdate - aTimeTakenByUpdate);
			else
				Sleep(0);

			++gAppUpdateCount;
		}

		// Cleanup
		InputTranslator::cleanup();
		InputDispatcher::cleanup();
		OverlayWindow::destroy();
	}

	// Report performance
	if( !gHadFatalError )
	{
		const double averageFPS = gAppUpdateCount / (gAppRunTime / 1000.0);
		debugPrint("Average FPS: %.2f\n", averageFPS);
	}

	// Final cleanup
	Gamepad::cleanup();

	return gHadFatalError ? EXIT_FAILURE : EXIT_SUCCESS;
}
