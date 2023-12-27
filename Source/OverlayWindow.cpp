//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "OverlayWindow.h"

#include "HUD.h"
#include "InputMap.h"
#include "Profile.h"

namespace OverlayWindow
{

//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	void load()
	{
	}
};


//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

HWND gHandle = NULL;


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static WNDCLASSEXW sWindowClass;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static LRESULT CALLBACK windowProcCallback(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_CREATE:
		break;

	case WM_PAINT:
		HUD::render(theWindow);
		break;

	case WM_DESTROY:
		HUD::cleanup();
		PostQuitMessage(0);
		return 0;

	case WM_DEVICECHANGE:
         // Forward this message to app's main message handler
		PostMessage(NULL, WM_DEVICECHANGE, wParam, lParam);
		break;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void create(HINSTANCE theAppInstanceHandle)
{
	DBG_ASSERT(gHandle == NULL);

	// Create/register window class
	WNDCLASSEXW sWindowClass = WNDCLASSEXW();
	sWindowClass.cbSize = sizeof(WNDCLASSEXW);
	sWindowClass.style = CS_HREDRAW | CS_VREDRAW;
	sWindowClass.lpfnWndProc = windowProcCallback;
	sWindowClass.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
	sWindowClass.hInstance = theAppInstanceHandle;
	sWindowClass.lpszClassName = L"MMO Gamepad Overlay Class";
	RegisterClassExW(&sWindowClass);

	// Create transparent overlay window
	gHandle = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		sWindowClass.lpszClassName,
		L"MMO Gamepad Overlay",
		WS_POPUP,
		0, 0, 1280, 720,
		NULL, NULL, theAppInstanceHandle, NULL);

	if( !gHandle )
	{
		logError("Could not create overlay window!");
		gHadFatalError = true;
		return;
	}

	SetLayeredWindowAttributes(gHandle, RGB(0, 0, 0), BYTE(0), LWA_COLORKEY);
	ShowWindow(gHandle, SW_SHOW);
	UpdateWindow(gHandle);
}


void destroy()
{
	if( gHandle )
	{
		DestroyWindow(gHandle);
		UnregisterClassW(sWindowClass.lpszClassName, sWindowClass.hInstance);
		gHandle = NULL;
	}
}


void loadProfile()
{
	kConfig.load();
	HUD::init();
}


void update()
{
	if( !gHandle )
		return;

	HUD::update();
}


void redraw()
{
	if( gHandle )
		InvalidateRect(gHandle, NULL, true);
}

} // OverlayWindow
