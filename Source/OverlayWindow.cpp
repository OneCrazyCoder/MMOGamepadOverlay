//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "OverlayWindow.h"

#include "HUD.h"
#include "Profile.h"

namespace OverlayWindow
{

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

HWND gHandle = NULL;
bool gHidden = false;


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static WNDCLASSEXW sWindowClass;
static RECT sDesktopWindowRect; // relative to virtual desktop
static RECT sWindowClientRect; // relative to window


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
		if( !gHidden )
		{
			HUD::render(theWindow, sWindowClientRect);
			return 0;
		}
		break;

	case WM_MOVE:
	case WM_SIZE:
		GetClientRect(theWindow, &sWindowClientRect);
		sDesktopWindowRect = sWindowClientRect;
		ClientToScreen(theWindow, (LPPOINT)&sDesktopWindowRect.left);
		ClientToScreen(theWindow, (LPPOINT)&sDesktopWindowRect.right);
		sDesktopWindowRect.left -= GetSystemMetrics(SM_XVIRTUALSCREEN);
		sDesktopWindowRect.right -= GetSystemMetrics(SM_XVIRTUALSCREEN);
		sDesktopWindowRect.top -= GetSystemMetrics(SM_YVIRTUALSCREEN);
		sDesktopWindowRect.bottom -= GetSystemMetrics(SM_YVIRTUALSCREEN);
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

	// Create transparent overlay window (at screen size initially)
	gHandle = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		sWindowClass.lpszClassName,
		L"MMO Gamepad Overlay",
		WS_POPUP,
		0, 0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN),
		NULL, NULL, theAppInstanceHandle, NULL);

	if( !gHandle )
	{
		logError("Could not create overlay window!");
		gHadFatalError = true;
		return;
	}

	HUD::init();
	SetLayeredWindowAttributes(gHandle, RGB(0, 0, 0), BYTE(0), LWA_COLORKEY);
	ShowWindow(gHandle, SW_SHOW);
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


void update()
{
	if( !gHandle )
		return;

	HUD::update();
}


void resize(RECT theNewWindowRect)
{
	if( !gHandle )
		return;

	if( gHidden )
	{
		gHidden = false;
		InvalidateRect(gHandle, NULL, true);
	}

	SetWindowPos(
		gHandle, NULL,
		theNewWindowRect.left, theNewWindowRect.top,
		theNewWindowRect.right - theNewWindowRect.left,
		theNewWindowRect.bottom - theNewWindowRect.top,
		SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}


void minimize()
{
	if( !gHandle || gHidden )
		return;

	// Actual minimize instead causes some Z-Order nonsense and
	// potential endless cycling between minimized and not depending
	// on what triggers it. Since are fully transparent anyway, just
	// not drawing anything has the same effect as being minimized,
	// and resize() above un-hide's which is what it would also do
	// if the window were actually literally minimized.
	gHidden = true;

	// Make sure anything previously drawn is erased
	InvalidateRect(gHandle, NULL, true);
}


void redraw()
{
	if( gHandle )
		InvalidateRect(gHandle, NULL, true);
}


u16 hotspotMousePosX(const Hotspot& theHotspot)
{
	u16 result = 32768; // center of desktop
	if( !gHandle )
		return result;
	const int kClientWidth =
		sWindowClientRect.right - sWindowClientRect.left;
	const int kDesktopWidth =
		GetSystemMetrics(SM_CXVIRTUALSCREEN);

	// Start with percentage of client rect as u16 (i.e. 65535 == 100%)
	int aPos = theHotspot.x.origin;
	// Convert client-rect-relative  pixel position
	aPos = aPos * kClientWidth / 0xFFFF;
	// Add pixel offset
	aPos += theHotspot.x.offset;
	// Convert to virtual desktop pixel coordinate
	aPos = max(0, aPos + sDesktopWindowRect.left);
	// Convert to % of virtual desktop size (again 65535 == 100%)
	// Use 64-bit variable temporarily to avoid multiply overflow
	aPos = min(0xFFFF, s64(aPos) * 0xFFFF / kDesktopWidth);
	return u16(aPos);
}


u16 hotspotMousePosY(const Hotspot& theHotspot)
{
	u16 result = 32768; // center of desktop
	if( !gHandle )
		return result;
	const int kClientHeight =
		sWindowClientRect.bottom - sWindowClientRect.top;
	const int kDesktopHeight =
		GetSystemMetrics(SM_CYVIRTUALSCREEN);

	int aPos = theHotspot.y.origin;
	aPos = aPos * kClientHeight / 0xFFFF;
	aPos += theHotspot.y.offset;
	aPos = max(0, aPos + sDesktopWindowRect.top);
	aPos = min(0xFFFF, s64(aPos) * 0xFFFF / kDesktopHeight);
	return u16(aPos);
}

} // OverlayWindow
