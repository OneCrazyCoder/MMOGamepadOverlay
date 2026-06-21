//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

/*
	Contains application entry point (WinMain). Initializes all other
	modules and runs the main application loop. The loop handles Windows
	messages and calls update functions for other modules. Finally, checks
	for quit message to exit the loop, finalize all modules, and shut down.
*/

#include "Common.h"

#include "Gamepad.h"
#include "HotspotMap.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "InputTranslator.h"
#include "LayoutEditor.h"
#include "Menus.h"
#include "Profile.h"
#include "TargetApp.h"
#include "TargetConfigSync.h"
#include "WindowManager.h"
#include "WindowPainter.h"

#ifdef _DEBUG
// Scan for memory leaks
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static u64 sAppStartTime = 0;
static u64 sUpdateStartTime = 0;
static LARGE_INTEGER sSystemTimeFreq;
static bool sUpdateLoopStarted = false;


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static u64 getSystemTime()
{
	LARGE_INTEGER aCurrentTime;
	QueryPerformanceCounter(&aCurrentTime);
	return aCurrentTime.QuadPart / sSystemTimeFreq.QuadPart;
}


//------------------------------------------------------------------------------
// Global Main Loop Functions (also used in Dialogs.cpp and WindowManager.cpp)
//------------------------------------------------------------------------------

void mainTimerUpdate()
{
	u64 aCurrentTime = getSystemTime();
	gAppFrameTime = dropTo<int>(aCurrentTime - sUpdateStartTime);
	gAppRunTime = u32(aCurrentTime - sAppStartTime);
	sUpdateStartTime = aCurrentTime;
}


void mainWindowMessagePump(HWND theDialog)
{
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
		WindowManager::stopModalModeUpdates();
	}
}


void mainLoopUpdate(HWND theDialog)
{
	// Don't process this again until mainLoopSleep() is called
	if( sUpdateLoopStarted )
		return;
	sUpdateLoopStarted = true;
	mainTimerUpdate();
	mainWindowMessagePump(theDialog);
}


void mainModulesUpdate()
{
	if( !gProfileToLoad.empty() )
		return;

	Gamepad::update();
	HotspotMap::update();
	InputMap::resetChangedHotspots();
	InputTranslator::update();
	InputDispatcher::update();
	TargetApp::update();
	WindowPainter::update();
	WindowManager::update();
	TargetConfigSync::update();
}


void mainLoopSleep()
{
	// Don't process this again until mainLoopUpdate() is called
	if( !sUpdateLoopStarted )
		return;
	sUpdateLoopStarted = false;

	// Sleep for half the remaining frame time (full amount can over-sleep)
	const int aTimeTakenByUpdate =
		dropTo<int>(getSystemTime() - sUpdateStartTime);
	const int aTimeToSleep = gAppTargetFrameTime - aTimeTakenByUpdate;
	Sleep(DWORD(max(1, aTimeToSleep / 2)));

	// Wait out the rest of the frame time
	u64 aFrameTimePassed = getSystemTime() - sUpdateStartTime;
	while(aFrameTimePassed < gAppTargetFrameTime)
	{
		Sleep(0);
		aFrameTimePassed = getSystemTime() - sUpdateStartTime;
	}
}


void mainLoopTimeSkip()
{
	sUpdateStartTime = getSystemTime();
}


//------------------------------------------------------------------------------
// Application entry point - WinMain
//------------------------------------------------------------------------------

INT APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, INT /*cmd_show*/)
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

	// Get high-resolution system timer and note app start time
	QueryPerformanceFrequency(&sSystemTimeFreq);
	sSystemTimeFreq.QuadPart /= 1000; // milliseconds instead of seconds
	sAppStartTime = sUpdateStartTime = getSystemTime();

	// Initialize gamepad module so can use it in initial dialogs
	Gamepad::init();

	// Initialize Profile module to set up auto-load, prompt for manual load,
	// or possibly initialize first-time run with license agreement
	Profile::init();

	while(!gProfileToLoad.empty() && !gShutdown && !hadFatalError())
	{
		// Load requested profile
		Profile::load();

		// Overwrite some profile properties with target app config
		if( !gShutdown && !hadFatalError() )
			TargetConfigSync::load();

		// Create main application window
		if( !gShutdown && !hadFatalError() )
			WindowManager::createMain(hInstance);

		// Load configuration settings for each module from profile
		if( !gShutdown && !hadFatalError() )
		{
			InputMap::loadProfile();
			HotspotMap::init();
			Menus::init();
			InputTranslator::loadProfile();
			InputDispatcher::loadProfile();
			TargetApp::loadProfile();
			WindowPainter::init();
			WindowManager::createOverlays(hInstance);
		}

		// Finalize profile load
		if( !gShutdown && !hadFatalError() )
		{
			Profile::clearChangedSections();
			Profile::saveChangesToFile();
		}

		// Launch target app if requested and haven't already
		if( !gShutdown && !hadFatalError() )
			TargetApp::autoLaunch();

		// Initiate frame timing
		gAppTargetFrameTime = Profile::getInt("System", "FrameTime", 15);
		timeBeginPeriod(gAppTargetFrameTime / 2);

		// Main loop
		while(!gShutdown && gProfileToLoad.empty() && !hadFatalError())
		{
			// Check if need to react to changed Profile data (via variables)
			if( !Profile::changedSections().empty() )
			{
				TargetConfigSync::loadProfileChanges();
				InputMap::loadProfileChanges();
				HotspotMap::loadProfileChanges();
				Menus::loadProfileChanges();
				InputTranslator::loadProfileChanges();
				InputDispatcher::loadProfileChanges();
				TargetApp::loadProfileChanges();
				WindowPainter::loadProfileChanges();
				WindowManager::loadProfileChanges();
				Profile::clearChangedSections();
				InputMap::resetChangedHotspots();
			}

			if( gLayoutEditorRequested )
				LayoutEditor::init();

			// Update frame timers and process windows messages
			mainLoopUpdate(NULL);

			// Update modules
			mainModulesUpdate();

			// Yield via Sleep() so sent input can be processed by target
			mainLoopSleep();
		}

		timeEndPeriod(gAppTargetFrameTime / 2);

		Profile::saveChangesToFile();

		// Cleanup
		TargetConfigSync::cleanup();
		WindowPainter::cleanup();
		Menus::cleanup();
		InputDispatcher::cleanup();
		InputTranslator::cleanup();
		HotspotMap::cleanup();
		LayoutEditor::cleanup();
		if( !hadFatalError() )
			WindowManager::destroyAll(hInstance);
	}

	// Note that MessageBox self-closes immediately if WM_QUIT has been
	// posted, meaning this will fail to report any fatal errors that
	// happen after user manually closes the main window. Will instead
	// need to check MMOGO.log for those (if it matters by then).
	if( hadFatalError() )
	{
		MessageBox(
			WindowManager::mainHandle(),
			gErrorString.c_str(),
			L"MMO Gamepad Overlay: Critical Error",
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
