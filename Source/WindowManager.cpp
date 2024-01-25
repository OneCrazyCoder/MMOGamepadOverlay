//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "WindowManager.h"

#include "HUD.h"
#include "InputMap.h"
#include "Profile.h"

namespace WindowManager
{

#ifdef _DEBUG
// Make the overall overlay region obvious by drawing a frame
//#define DEBUG_DRAW_OVERLAY_FRAME
#endif

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kNoticeStringDisplayTimePerChar = 60,
kNoticeStringMinTime = 3000,
};

const wchar_t* kMainWindowClassName = L"MMOGO Main";
const wchar_t* kOverlayWindowClassName = L"MMOGO Overlay";
const wchar_t* kErrorWindowClassName = L"MMOGO Errors";


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct OverlayWindow
	: public ConstructFromZeroInitializedMemory<OverlayWindow>
{
	HWND handle;
	POINT menuItemSize;
	RECT region;
	bool visible;

	POINT clientSize()
	{
		POINT result;
		result.x = region.right - region.left;
		result.y = region.bottom - region.top;
		return result;
	}
};



//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static HWND sMainWindow = NULL;
static std::vector<OverlayWindow> sOverlayWindows; 
static RECT sDesktopTargetRect; // relative to virtual desktop
static RECT sScreenTargetRect; // relative to main screen
static POINT sTargetSize = { 0 };
static std::wstring sErrorMessage;
static std::wstring sNoticeMessage;
static int sErrorMessageTimer = 0;
static int sNoticeMessageTimer = 0;
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
	case WM_DESTROY:
		PostQuitMessage(0);
		sMainWindow = NULL;
		return 0;

	case WM_DEVICECHANGE:
	case WM_SYSCOLORCHANGE:
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
		HUD::drawElement(
			theWindow,
			aHUDElementID,
			sOverlayWindows[aHUDElementID].menuItemSize,
			sOverlayWindows[aHUDElementID].clientSize());
		return 0;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


static LRESULT CALLBACK errorWindowProc(
	HWND theWindow, UINT theMessage, WPARAM wParam, LPARAM lParam)
{
	switch(theMessage)
	{
	case WM_PAINT:
		{// Draw error strings (and debug frame)
			PAINTSTRUCT aPaintStruct;
			HDC hdc = BeginPaint(theWindow, &aPaintStruct);

			#ifdef DEBUG_DRAW_OVERLAY_FRAME
			{
				// Normally I'd prefer not to create and destroy these on-the-fly,
				// but since this is purely for debugging purposes...
				HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
				HPEN hRedPen = CreatePen(PS_INSIDEFRAME, 3, RGB(255, 0, 0));
				HPEN hOldPen = (HPEN)SelectObject(hdc, hRedPen);
				HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBlackBrush);
				RECT aClientRect;
				GetClientRect(theWindow, &aClientRect);
				Rectangle(hdc, 0, 0, aClientRect.right, aClientRect.bottom);
				SelectObject(hdc, hOldPen);
				SelectObject(hdc, hOldBrush);
				DeleteObject(hRedPen);
			}
			#endif

			if( !sErrorMessage.empty() )
			{
				RECT aTextRect;
				GetClientRect(theWindow, &aTextRect);
				InflateRect(&aTextRect, -8, -8);
				SetBkColor(hdc, RGB(255, 0, 0));
				SetTextColor(hdc, RGB(255, 255, 0));
				DrawText(hdc, sErrorMessage.c_str(), -1, &aTextRect,
					DT_WORDBREAK);
			}

			// Draw notice string
			if( !sNoticeMessage.empty() )
			{
				RECT aTextRect;
				GetClientRect(theWindow, &aTextRect);
				InflateRect(&aTextRect, -8, -8);
				SetBkColor(hdc, RGB(0, 0, 255));
				SetTextColor(hdc, RGB(255, 255, 255));
				DrawText(hdc, sNoticeMessage.c_str(), -1, &aTextRect,
					DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
			}

			EndPaint(theWindow, &aPaintStruct);
		}
		return 0;
	}

	return DefWindowProc(theWindow, theMessage, wParam, lParam);
}


bool overlayWindowShouldBeShown(size_t theOverlayWindowID)
{
	if( sHidden )
		return false;

	if( theOverlayWindowID >= sOverlayWindows.size() )
		return false;

	if( theOverlayWindowID < gVisibleHUD.size() )
		return gVisibleHUD.test(theOverlayWindowID);

	// Must be the error message overlay window
	#ifdef DEBUG_DRAW_OVERLAY_FRAME
	return true;
	#endif
	return !sNoticeMessage.empty() || !sErrorMessage.empty();
}


void updateVisibility()
{
	for(u16 i = 0; i < sOverlayWindows.size(); ++i)
	{
		DBG_ASSERT(sOverlayWindows[i].handle);
		const bool wantVisible = overlayWindowShouldBeShown(i);
		if( sOverlayWindows[i].visible != wantVisible )
		{
			ShowWindow(sOverlayWindows[i].handle,
				wantVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
			sOverlayWindows[i].visible = wantVisible;
		}
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
	u16 aMainWindowWidth = Profile::getInt("System/WindowWidth", 150);
	u16 aMainWindowHeight = Profile::getInt("System/WindowHeight", 50);
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
	aWindowClass.hInstance = theAppInstanceHandle;
	aWindowClass.lpszClassName = kMainWindowClassName;
	RegisterClassExW(&aWindowClass);

	aWindowClass.lpfnWndProc = overlayWindowProc;
	aWindowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	aWindowClass.hCursor = NULL;
	aWindowClass.lpszClassName = kOverlayWindowClassName;
	RegisterClassExW(&aWindowClass);

	aWindowClass.lpfnWndProc = errorWindowProc;
	aWindowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	aWindowClass.hCursor = NULL;
	aWindowClass.lpszClassName = kErrorWindowClassName;
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

	// Create one window per HUD Element, plus an extra for error messages
	sOverlayWindows.reserve(InputMap::hudElementCount() + 1);
	sOverlayWindows.resize(InputMap::hudElementCount() + 1);
	for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
	{
		sOverlayWindows[i].menuItemSize = HUD::menuItemSize(i, sTargetSize);
		sOverlayWindows[i].region = HUD::elementRectNeeded(
			i, sOverlayWindows[i].menuItemSize, sScreenTargetRect);
		sOverlayWindows[i].handle = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_NOACTIVATE |
			WS_EX_TRANSPARENT | WS_EX_LAYERED,
			kOverlayWindowClassName,
			NULL,
			WS_POPUP | (sUseChildWindows ? WS_CHILD : 0),
			sOverlayWindows[i].region.left,
			sOverlayWindows[i].region.top,
			sOverlayWindows[i].region.right - sOverlayWindows[i].region.left,
			sOverlayWindows[i].region.bottom - sOverlayWindows[i].region.top,
			sUseChildWindows ? sMainWindow : NULL,
			NULL, theAppInstanceHandle, NULL);

		SetWindowLongPtr(sOverlayWindows[i].handle, GWLP_USERDATA, i);
		SetLayeredWindowAttributes(
			sOverlayWindows[i].handle, RGB(0, 0, 0), BYTE(0), LWA_COLORKEY);
	}

	sOverlayWindows[InputMap::hudElementCount()].handle =
		CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_NOACTIVATE |
			WS_EX_TRANSPARENT | WS_EX_LAYERED,
			kErrorWindowClassName,
			NULL,
			WS_POPUP | (sUseChildWindows ? WS_CHILD : 0),
			0, 0, sTargetSize.x, sTargetSize.y,
			sUseChildWindows ? sMainWindow : NULL,
			NULL, theAppInstanceHandle, NULL);

	SetLayeredWindowAttributes(
		sOverlayWindows[InputMap::hudElementCount()].handle,
		RGB(0, 0, 0), BYTE(0), LWA_COLORKEY);
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
		if( sOverlayWindows[i].handle )
			DestroyWindow(sOverlayWindows[i].handle);
	}
	sOverlayWindows.clear();
	UnregisterClassW(kMainWindowClassName, theAppInstanceHandle);
	UnregisterClassW(kOverlayWindowClassName, theAppInstanceHandle);
}


void update()
{
	// Hande display of error messages and notices on a top-most overlay
	bool needErrorWindowRefresh = false;
	if( sErrorMessageTimer > 0 )
	{
		sErrorMessageTimer -= gAppFrameTime;
		if( sErrorMessageTimer <= 0 )
		{
			sErrorMessageTimer = 0;
			sErrorMessage.clear();
			needErrorWindowRefresh = true;
		}
	}
	else if( !gErrorString.empty() && !hadFatalError() )
	{
		sErrorMessage =
			widen("MMOGO ERROR: ") +
			gErrorString +
			widen("\nCheck errorlog.txt for other possible errors!");
		sErrorMessageTimer = max(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sErrorMessage.size()));
		gErrorString.clear();
		needErrorWindowRefresh = true;
	}
	
	if( sNoticeMessageTimer > 0 )
	{
		sNoticeMessageTimer -= gAppFrameTime;
		if( sNoticeMessageTimer <= 0 )
		{
			sNoticeMessageTimer = 0;
			sNoticeMessage.clear();
			needErrorWindowRefresh = true;
		}
	}
	if( !gNoticeString.empty() )
	{
		sNoticeMessage = gNoticeString;
		sNoticeMessageTimer = max(
			kNoticeStringMinTime,
			int(kNoticeStringDisplayTimePerChar *
				sNoticeMessage.size()));
		gNoticeString.clear();
		needErrorWindowRefresh = true;
	}

	if( needErrorWindowRefresh )
	{
		InvalidateRect(
			sOverlayWindows[sOverlayWindows.size()-1].handle, NULL, true);
	}

	// Show or hide windows according to gVisibleHUD
	updateVisibility();
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


bool overlaysVisible()
{
	return false;
}


void resize(RECT theNewWindowRect)
{
	if( !sMainWindow )
		return;

	sTargetSize.x = theNewWindowRect.right - theNewWindowRect.left;
	sTargetSize.y = theNewWindowRect.bottom - theNewWindowRect.top;
	sDesktopTargetRect = sScreenTargetRect = theNewWindowRect;
	sDesktopTargetRect.left -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.right -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.top -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	sDesktopTargetRect.bottom -= GetSystemMetrics(SM_YVIRTUALSCREEN);

	for(u16 i = 0; i < sOverlayWindows.size(); ++i)
	{
		DBG_ASSERT(sOverlayWindows[i].handle);
		if( i < InputMap::hudElementCount() )
		{
			sOverlayWindows[i].menuItemSize = HUD::menuItemSize(i, sTargetSize);
			sOverlayWindows[i].region = HUD::elementRectNeeded(
				i, sOverlayWindows[i].menuItemSize, sScreenTargetRect);
		}
		else
		{
			sOverlayWindows[i].region = sScreenTargetRect;
		}
		SetWindowPos(
			sOverlayWindows[i].handle, NULL,
			sOverlayWindows[i].region.left, sOverlayWindows[i].region.top,
			sOverlayWindows[i].region.right - sOverlayWindows[i].region.left,
			sOverlayWindows[i].region.bottom - sOverlayWindows[i].region.top,
			SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
	}
}


void hideOverlays()
{
	if( !sMainWindow || sHidden )
		return;

	sHidden = true;
	updateVisibility();
}


void showOverlays()
{
	if( !sMainWindow || !sHidden )
		return;

	sHidden = false;
	updateVisibility();
}


void refreshZOrder()
{
	// TODO
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
	aPos = aPos * sTargetSize.x / 0x10000;
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
	aPos = aPos * sTargetSize.y / 0x10000;
	aPos += theHotspot.y.offset;
	aPos = max(0, aPos + sDesktopTargetRect.top);
	aPos = min(0xFFFF, s64(aPos) * 0x10000 / kDesktopHeight);
	return u16(aPos);
}


int hotspotClientX(const Hotspot& theHotspot)
{
	if( !sMainWindow ) return 0; // Left edge of window
	int aPos = theHotspot.x.origin;
	aPos = aPos * sTargetSize.x / 0x10000;
	aPos += theHotspot.x.offset;
	return aPos;
}


int hotspotClientY(const Hotspot& theHotspot)
{
	if( !sMainWindow ) return 0; // Top edge of window
	int aPos = theHotspot.y.origin;
	aPos = aPos * sTargetSize.y / 0x10000;
	aPos += theHotspot.y.offset;
	return aPos;
}

} // WindowManager
