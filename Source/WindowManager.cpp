//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "WindowManager.h"

#include "Dialogs.h"
#include "HUD.h"
#include "InputMap.h"
#include "Profile.h"
#include "Resources/resource.h"

namespace WindowManager
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

const wchar_t* kMainWindowClassName = L"MMOGO Main";
const wchar_t* kOverlayWindowClassName = L"MMOGO Overlay";

enum EFadeState
{
	eFadeState_Hidden,
	eFadeState_FadeInDelay,
	eFadeState_FadingIn,
	eFadeState_MaxAlpha,
	eFadeState_InactiveFadeOut,
	eFadeState_Inactive,
	eFadeState_FadeOutDelay,
	eFadeState_FadingOut,
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct OverlayWindow
	: public ConstructFromZeroInitializedMemory<OverlayWindow>
{
	HWND handle;
	HBITMAP bitmap;
	POINT position;
	SIZE size;
	SIZE bitmapSize;
	SIZE componentSize;
	float fadeValue;
	EFadeState fadeState;
	u8 alpha;
	bool hideUntilActivated;
	bool updated;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static HWND sMainWindow = NULL;
static std::vector<OverlayWindow> sOverlayWindows; 
static RECT sDesktopTargetRect; // relative to virtual desktop
static RECT sScreenTargetRect; // relative to main screen
static SIZE sTargetSize = { 0 };
static bool sUseChildWindows = false;
static bool sHidden = false;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static LRESULT CALLBACK mainWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case ID_FILE_EXIT:
			PostQuitMessage(0);
			return 0;
		case ID_FILE_PROFILE:
			// TODO
			return 0;
		case ID_HELP_LICENSE:
			Dialogs::showLicenseAgreement(theWindow);
			return 0;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		sMainWindow = NULL;
		return 0;

	case WM_DEVICECHANGE:
         // Forward this message to app's main message handler
		PostMessage(NULL, theMessage, wParam, lParam);
		break;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static LRESULT CALLBACK overlayWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	u16 aHUDElementID = u16(GetWindowLongPtr(theWindow, GWLP_USERDATA));
	switch(theMessage)
	{
	case WM_PAINT:
		gRedrawHUD.set(aHUDElementID);
		break;
	case WM_SYSCOLORCHANGE:
		if( sOverlayWindows.size() > aHUDElementID &&
			sOverlayWindows[aHUDElementID].bitmap )
		{
			DeleteObject(sOverlayWindows[aHUDElementID].bitmap);
			sOverlayWindows[aHUDElementID].bitmap = NULL;
		}
		break;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static void updateAlphaFades(OverlayWindow& theWindow, u16 id)
{
	EFadeState oldState;
	u8 aNewAlpha = theWindow.alpha;
	do
	{
		oldState = theWindow.fadeState;
		switch(theWindow.fadeState)
		{
		case eFadeState_Hidden:
			aNewAlpha = 0;
			if( gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeInDelay;
				theWindow.fadeValue = 0;
			}
			break;
		case eFadeState_FadeInDelay:
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_Hidden;
				theWindow.fadeValue = 0;
				break;
			}
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			theWindow.fadeValue += gAppFrameTime;
			if( theWindow.fadeValue >= HUD::alphaFadeInDelay(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_FadingIn:
			theWindow.fadeValue += HUD::alphaFadeInRate(id) * gAppFrameTime;
			aNewAlpha = u8(min(
				theWindow.fadeValue, (float)HUD::maxAlpha(id)));
			if( gActiveHUD.test(id) || aNewAlpha >= HUD::maxAlpha(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			break;
		case eFadeState_MaxAlpha:
			aNewAlpha = HUD::maxAlpha(id);
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeValue = 0;
			}
			else if( HUD::inactiveFadeOutDelay(id) > 0 &&
					 HUD::inactiveAlpha(id) < HUD::maxAlpha(id) )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= HUD::inactiveFadeOutDelay(id) )
				{
					theWindow.fadeState = eFadeState_InactiveFadeOut;
					theWindow.fadeValue = aNewAlpha;
				}
			}
			break;
		case eFadeState_InactiveFadeOut:
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			theWindow.fadeValue -= HUD::alphaFadeOutRate(id) * gAppFrameTime;
			aNewAlpha = u8(max(theWindow.fadeValue, HUD::inactiveAlpha(id)));
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( aNewAlpha <= HUD::inactiveAlpha(id) )
			{
				theWindow.fadeState = eFadeState_Inactive;
				break;
			}
			break;
		case eFadeState_Inactive:
			aNewAlpha = HUD::inactiveAlpha(id);
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_FadeOutDelay:
			if( gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			theWindow.fadeValue += gAppFrameTime;
			if( theWindow.fadeValue >= HUD::alphaFadeOutDelay(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			break;
		case eFadeState_FadingOut:
			theWindow.fadeValue -= HUD::alphaFadeOutRate(id) * gAppFrameTime;
			aNewAlpha = u8(max(theWindow.fadeValue, 0.0f));
			if( aNewAlpha == 0 )
			{
				theWindow.fadeState = eFadeState_Hidden;
				theWindow.fadeValue = 0;
				break;
			}
			if( gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				break;
			}
			break;
		default:
			DBG_ASSERT(false && "Invalid WindowManager::EFadeState value");
			theWindow.fadeState = eFadeState_Hidden;
			break;
		}
	} while(oldState != theWindow.fadeState);

	if( aNewAlpha != theWindow.alpha )
	{
		theWindow.alpha = aNewAlpha;
		theWindow.updated = false;
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void createMain(HINSTANCE theAppInstanceHandle)
{
	DBG_ASSERT(sMainWindow == NULL);

	const std::wstring& aMainWindowName = widen(
		Profile::getStr("System/WindowName", "MMO Gamepad Overlay"));
	u16 aMainWindowWidth = Profile::getInt("System/WindowWidth", 160);
	u16 aMainWindowHeight = Profile::getInt("System/WindowHeight", 80);
	const bool isMainWindowHidden = 
		(aMainWindowWidth == 0 || aMainWindowHeight == 0);

	RECT aScreenRect;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &aScreenRect, 0);

	if( isMainWindowHidden )
	{
		sUseChildWindows = false;
		aMainWindowWidth = 10;
		aMainWindowHeight = 10;
	}
	else
	{// Determine if supports child layered windows or not (Win8 or higher)
		OSVERSIONINFOW info;
		info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
		GetVersionEx(&info);
		if( info.dwMajorVersion >= 6 && info.dwMinorVersion >= 2 )
			sUseChildWindows = true;
		else
			sUseChildWindows = false;
	}

	// Register window classes
	WNDCLASSEXW aWindowClass = { 0 };
	aWindowClass.cbSize = sizeof(WNDCLASSEXW);
	aWindowClass.style = CS_HREDRAW | CS_VREDRAW;
	aWindowClass.lpfnWndProc = mainWindowProc;
	aWindowClass.hbrBackground =
		isMainWindowHidden
			? (HBRUSH)GetStockObject(NULL_BRUSH)
			: (HBRUSH)GetStockObject(WHITE_BRUSH);
	aWindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	aWindowClass.hIcon = LoadIcon(theAppInstanceHandle,
		MAKEINTRESOURCE(IDI_ICON_MAIN));
	aWindowClass.hInstance = theAppInstanceHandle;
	aWindowClass.lpszClassName = kMainWindowClassName;
	aWindowClass.lpszMenuName = MAKEINTRESOURCE(IDR_MENU_MAIN);
	RegisterClassExW(&aWindowClass);

	aWindowClass.lpfnWndProc = overlayWindowProc;
	aWindowClass.hbrBackground = NULL;
	aWindowClass.hCursor = NULL;
	aWindowClass.lpszClassName = kOverlayWindowClassName;
	RegisterClassExW(&aWindowClass);

	// Create main app window
	sMainWindow = CreateWindowExW(
		isMainWindowHidden ? (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_APPWINDOW) : 0,
		kMainWindowClassName,
		aMainWindowName.c_str(),
		isMainWindowHidden ? WS_POPUP : WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX) | WS_SYSMENU,
		aScreenRect.right - aMainWindowWidth, aScreenRect.bottom - aMainWindowHeight, aMainWindowWidth, aMainWindowHeight,
		NULL, NULL, theAppInstanceHandle, NULL);

	if( !sMainWindow )
	{
		logFatalError("Could not create main application window!");
		return;
	}

	ShowWindow(sMainWindow, SW_SHOW);

	// Set overlay client area to full main screen initially
	aScreenRect.left = 0;
	aScreenRect.top = 0;
	aScreenRect.right = GetSystemMetrics(SM_CXSCREEN);
	aScreenRect.bottom = GetSystemMetrics(SM_CYSCREEN);
	resize(aScreenRect);
}


void createOverlays(HINSTANCE theAppInstanceHandle)
{
	DBG_ASSERT(sOverlayWindows.empty());

	// Create one transparent overlay window per HUD Element
	sOverlayWindows.reserve(InputMap::hudElementCount());
	sOverlayWindows.resize(InputMap::hudElementCount());
	for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
	{
		OverlayWindow& aWindow = sOverlayWindows[i];
		HUD::updateWindowLayout(i, sTargetSize,
			aWindow.componentSize, aWindow.position, aWindow.size);
		aWindow.handle = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_NOACTIVATE |
			WS_EX_TRANSPARENT | WS_EX_LAYERED,
			kOverlayWindowClassName,
			widen(InputMap::hudElementLabel(i)).c_str(),
			WS_POPUP | (sUseChildWindows ? WS_CHILD : 0),
			aWindow.position.x,
			aWindow.position.y,
			aWindow.size.cx,
			aWindow.size.cy,
			sUseChildWindows ? sMainWindow : NULL,
			NULL, theAppInstanceHandle, NULL);
		SetWindowLongPtr(aWindow.handle, GWLP_USERDATA, i);
		aWindow.hideUntilActivated = HUD::shouldStartHidden(u16(i));
	}
}


void destroyAll(HINSTANCE theAppInstanceHandle)
{
	if( sMainWindow )
	{
		DestroyWindow(sMainWindow);
		sMainWindow = NULL;
	}
	for(size_t i = 0; i < sOverlayWindows.size(); ++i)
	{
		if( sOverlayWindows[i].bitmap )
			DeleteObject(sOverlayWindows[i].bitmap);
		if( sOverlayWindows[i].handle )
			DestroyWindow(sOverlayWindows[i].handle);
	}
	sOverlayWindows.clear();
	UnregisterClassW(kMainWindowClassName, theAppInstanceHandle);
	UnregisterClassW(kOverlayWindowClassName, theAppInstanceHandle);
}


void update()
{
	// Update each overlay window as needed
	HDC aScreenDC = GetDC(NULL);
	POINT anOriginPoint = { 0, 0 };
	for(size_t i = 0; i < sOverlayWindows.size(); ++i)
	{
		OverlayWindow& aWindow = sOverlayWindows[i];

		// Update alpha fade effects based on gVisibleHUD & gActiveHUD
		updateAlphaFades(aWindow, u16(i));
		
		// Check visibility status so can mostly ignore hidden windows
		if( sHidden || aWindow.alpha == 0 ||
			(aWindow.hideUntilActivated && !gActiveHUD.test(i)) )
		{
			if( IsWindowVisible(aWindow.handle) )
				ShowWindow(aWindow.handle, SW_HIDE);
			aWindow.hideUntilActivated = HUD::shouldStartHidden(u16(i));
			gActiveHUD.reset(i);
			continue;
		}
		aWindow.hideUntilActivated = false;

		// Check for possible update to window layout
		if( !aWindow.updated || gRedrawHUD.test(i) || gActiveHUD.test(i) )
		{
			HUD::updateWindowLayout(u16(i), sTargetSize,
				aWindow.componentSize, aWindow.position, aWindow.size);
			aWindow.updated = false;
			if( aWindow.size.cx <= 0 || aWindow.size.cy <= 0 )
			{
				gActiveHUD.reset(i);
				continue;
			}
		}
	
		// Delete bitmap if bitmap size doesn't match window size
		if( aWindow.bitmap &&
			(aWindow.bitmapSize.cx != aWindow.size.cx ||
			 aWindow.bitmapSize.cy != aWindow.size.cy) )
		{
			DeleteObject(aWindow.bitmap);
			aWindow.bitmap = NULL;
		}

		// Create bitmap if doesn't exist
		const bool needToEraseBitmap = !aWindow.bitmap;
		if( !aWindow.bitmap )
		{
			aWindow.bitmapSize = aWindow.size;
			aWindow.bitmap = CreateCompatibleBitmap(
				aScreenDC, aWindow.size.cx, aWindow.size.cy);
			if( !aWindow.bitmap )
			{
				logFatalError("Could not create bitmap for overlay!");
				continue;
			}
			gRedrawHUD.set(i);
		}

		// Redraw bitmap contents if needed
		HDC aWindowDC = CreateCompatibleDC(aScreenDC);
		const int aWindowDCInitialState = SaveDC(aWindowDC);
		if( !aWindowDC )
		{
			logFatalError("Could not create device context for overlay!");
			continue;
		}
		SelectObject(aWindowDC, aWindow.bitmap);
		if( gRedrawHUD.test(i) )
		{
			HUD::drawElement(
				aWindowDC, u16(i),
				aWindow.componentSize, aWindow.size,
				needToEraseBitmap);
			aWindow.updated = false;
			gRedrawHUD.reset(i);
		}

		// Update window
		if( !aWindow.updated )
		{
			BLENDFUNCTION aBlendFunction = {AC_SRC_OVER, 0, aWindow.alpha, 0};
			UpdateLayeredWindow(aWindow.handle, aScreenDC,
				&aWindow.position, &aWindow.size,
				aWindowDC, &anOriginPoint,
				HUD::transColor(u16(i)), &aBlendFunction,
				ULW_ALPHA | ULW_COLORKEY);
			aWindow.updated = true;
		}
		gActiveHUD.reset(i);

		// Show window if it isn't visible yet
		if( !IsWindowVisible(aWindow.handle) )
			ShowWindow(aWindow.handle, SW_SHOWNOACTIVATE);

		// Cleanup
		RestoreDC(aWindowDC, aWindowDCInitialState);
		DeleteDC(aWindowDC);
	}
	ReleaseDC(NULL, aScreenDC);
}


bool isOwnedByThisApp(HWND theWindow)
{
	DWORD aWindowProcessId = 0;
	GetWindowThreadProcessId(theWindow, &aWindowProcessId);

	return (aWindowProcessId == GetCurrentProcessId());
}


HWND mainHandle()
{
	return sMainWindow;
}


void resize(RECT theNewWindowRect)
{
	if( !sMainWindow )
		return;

	sTargetSize.cx = theNewWindowRect.right - theNewWindowRect.left;
	sTargetSize.cy = theNewWindowRect.bottom - theNewWindowRect.top;
	sDesktopTargetRect = sScreenTargetRect = theNewWindowRect;
	sDesktopTargetRect.left -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.right -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.top -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	sDesktopTargetRect.bottom -= GetSystemMetrics(SM_YVIRTUALSCREEN);

	for(u16 i = 0; i < sOverlayWindows.size(); ++i)
		sOverlayWindows[i].updated = false;
}


void hideOverlays()
{
	sHidden = true;
}


void showOverlays()
{
	sHidden = false;
}


void setOverlaysToTopZ()
{
	for(size_t i = 0; i < sOverlayWindows.size(); ++i)
	{
		OverlayWindow& aWindow = sOverlayWindows[i];
		DBG_ASSERT(aWindow.handle);
		SetWindowPos(
			aWindow.handle, HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
	}
}


bool areOverlaysHidden()
{
	return sHidden;
}


u16 hotspotMousePosX(const Hotspot& theHotspot)
{
	if( !sMainWindow ) return 32768; // center of desktop
	// Client Rect left is always 0
	const int kDesktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);

	// Start with percentage of client rect as u16 (i.e. 65536 == 100%)
	int aPos = theHotspot.x.origin;
	// Convert client-rect-relative  pixel position
	aPos = aPos * sTargetSize.cx / 0x10000;
	// Add pixel offset
	aPos += theHotspot.x.offset;
	// Convert to virtual desktop pixel coordinate
	aPos = max(0, aPos + sDesktopTargetRect.left);
	// Convert to % of virtual desktop size (again 65536 == 100%)
	// Use 64-bit variable temporarily to avoid multiply overflow
	aPos = min(0xFFFF, s64(aPos) * 0x10000 / kDesktopWidth);
	return u16(aPos);
}


u16 hotspotMousePosY(const Hotspot& theHotspot)
{
	if( !sMainWindow ) return 32768; // center of desktop
	const int kDesktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	int aPos = theHotspot.y.origin;
	aPos = aPos * sTargetSize.cy / 0x10000;
	aPos += theHotspot.y.offset;
	aPos = max(0, aPos + sDesktopTargetRect.top);
	aPos = min(0xFFFF, s64(aPos) * 0x10000 / kDesktopHeight);
	return u16(aPos);
}


int hotspotClientX(const Hotspot& theHotspot)
{
	if( !sMainWindow ) return 0; // Left edge of window
	int aPos = theHotspot.x.origin;
	aPos = aPos * sTargetSize.cx / 0x10000;
	aPos += theHotspot.x.offset;
	return aPos;
}


int hotspotClientY(const Hotspot& theHotspot)
{
	if( !sMainWindow ) return 0; // Top edge of window
	int aPos = theHotspot.y.origin;
	aPos = aPos * sTargetSize.cy / 0x10000;
	aPos += theHotspot.y.offset;
	return aPos;
}

} // WindowManager
