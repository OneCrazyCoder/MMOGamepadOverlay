//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "WindowManager.h"

#include "Dialogs.h"
#include "HUD.h"
#include "InputMap.h"
#include "Profile.h"
#include "Resources/resource.h"
#include "TargetApp.h" // targetWindowHandle()

namespace WindowManager
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

const wchar_t* kMainWindowClassName = L"MMOGO Main";
const wchar_t* kOverlayWindowClassName = L"MMOGO Overlay";

enum EIconCopyMethod
{
	eIconCopyMethod_Desktop,
	eIconCopyMethod_TargetWindow,
	eIconCopyMethod_ExcludeFromCapture,
	//eIconCopyMethod_PaintSingleWindow,

	eIconCopyMethod_Num
};

enum EFadeState
{
	eFadeState_Hidden,
	eFadeState_FadeInDelay,
	eFadeState_FadingIn,
	eFadeState_MaxAlpha,
	eFadeState_DisabledFadeOut,
	eFadeState_Disabled,
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
	std::vector<RECT> components;
	float fadeValue;
	EFadeState fadeState;
	u8 alpha;
	bool hideUntilActivated;
	bool bitmapUpdated;
	bool layoutUpdated;
};


struct OverlayWindowPriority
{
	u16 id;
	s16 priority;

	OverlayWindowPriority() : id(), priority() {}
	bool operator<(const OverlayWindowPriority& rhs) const
	{
		if( priority != rhs.priority )
			return priority < rhs.priority;
		return id < rhs.id; 
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static HWND sMainWindow = NULL;
static std::vector<OverlayWindow> sOverlayWindows;
static std::vector<OverlayWindowPriority> sOverlayWindowOrder;
static RECT sDesktopTargetRect; // relative to virtual desktop
static RECT sScreenTargetRect; // relative to main screen
static SIZE sTargetSize = { 0 };
static EIconCopyMethod sIconCopyMethod = EIconCopyMethod(0);
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
			if( Profile::queryUserForProfile() )
				gReloadProfile = true;
			return 0;
		case ID_HELP_LICENSE:
			Dialogs::showLicenseAgreement(theWindow);
			return 0;
		}
		break;

	case WM_PAINT:
		HUD::drawMainWindowContents(theWindow);
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		sMainWindow = NULL;
		return 0;

	case WM_DEVICECHANGE:
 	case WM_SYSCOLORCHANGE:
	case WM_DISPLAYCHANGE:
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
	case WM_DISPLAYCHANGE:
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
	const u8 aMaxAlpha = HUD::maxAlpha(id);
	const u8 anInactiveAlpha = HUD::inactiveAlpha(id);
	const bool startHidden =
		HUD::shouldStartHidden(id) && !gActiveHUD.test(id);
	do
	{
		oldState = theWindow.fadeState;
		const float oldFadeValue = theWindow.fadeValue;
		switch(theWindow.fadeState)
		{
		case eFadeState_Hidden:
			aNewAlpha = 0;
			if( gActiveHUD.test(id) && gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( gVisibleHUD.test(id) && !startHidden )
			{
				theWindow.fadeState = eFadeState_FadeInDelay;
				theWindow.fadeValue = 0;
			}
			break;
		case eFadeState_FadeInDelay:
			if( gActiveHUD.test(id) && gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_Hidden;
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
				theWindow.fadeValue, (float)aMaxAlpha));
			if( gDisabledHUD.test(id) &&
				anInactiveAlpha < aMaxAlpha &&
				aNewAlpha >= anInactiveAlpha )
			{
				theWindow.fadeState = eFadeState_DisabledFadeOut;
				if( oldFadeValue < anInactiveAlpha )
					aNewAlpha = min(aNewAlpha, anInactiveAlpha);
				break;
			}
			if( gActiveHUD.test(id) || aNewAlpha >= aMaxAlpha )
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
			aNewAlpha = aMaxAlpha;
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadeOutDelay;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeValue = 0;
				break;
			}
			if( gDisabledHUD.test(id) &&
				anInactiveAlpha < aMaxAlpha )
			{
				theWindow.fadeState = eFadeState_DisabledFadeOut;
				theWindow.fadeValue = aNewAlpha;
				break;
			}
			if( HUD::inactiveFadeOutDelay(id) > 0 &&
				anInactiveAlpha < aMaxAlpha )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= HUD::inactiveFadeOutDelay(id) )
				{
					theWindow.fadeState = eFadeState_InactiveFadeOut;
					theWindow.fadeValue = aNewAlpha;
				}
				break;
			}
			break;
		case eFadeState_DisabledFadeOut:
			if( !gDisabledHUD.test(id) && gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = max(anInactiveAlpha, aNewAlpha);
				break;
			}
			// fall through
		case eFadeState_InactiveFadeOut:
			if( !gVisibleHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingOut;
				break;
			}
			theWindow.fadeValue -= HUD::alphaFadeOutRate(id) * gAppFrameTime;
			aNewAlpha = u8(max(theWindow.fadeValue, anInactiveAlpha));
			if( gActiveHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_MaxAlpha;
				theWindow.fadeValue = 0;
				break;
			}
			if( aNewAlpha <= anInactiveAlpha )
			{
				theWindow.fadeState = oldState == eFadeState_InactiveFadeOut
					? eFadeState_Inactive : eFadeState_Disabled;
				theWindow.fadeValue = 0;
				break;
			}
			break;
		case eFadeState_Disabled:
			if( !gDisabledHUD.test(id) )
			{
				theWindow.fadeState = eFadeState_FadingIn;
				theWindow.fadeValue = anInactiveAlpha;
				break;
			}
			if( HUD::inactiveFadeOutDelay(id) > 0 &&
				anInactiveAlpha < aMaxAlpha )
			{
				theWindow.fadeValue += gAppFrameTime;
				if( theWindow.fadeValue >= HUD::inactiveFadeOutDelay(id) )
				{
					theWindow.fadeState = eFadeState_Inactive;
					theWindow.fadeValue = 0;
					break;
				}
			}
			// fall through
		case eFadeState_Inactive:
			aNewAlpha = anInactiveAlpha;
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
				if( startHidden )
				{
					theWindow.fadeState = eFadeState_Hidden;
					theWindow.fadeValue = 0;
				}
				else
				{
					theWindow.fadeState = eFadeState_FadingIn;
					theWindow.fadeValue = aNewAlpha;
				}
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
				if( startHidden )
				{
					theWindow.fadeState = eFadeState_Hidden;
					theWindow.fadeValue = 0;
				}
				else
				{
					theWindow.fadeState = eFadeState_FadingIn;
				}
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
		theWindow.bitmapUpdated = false;
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void createMain(HINSTANCE theAppInstanceHandle)
{
	DBG_ASSERT(sMainWindow == NULL);

	sHidden = false;
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
	aWindowClass.lpszMenuName = NULL;
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
	readUIScale();
	aScreenRect.left = 0;
	aScreenRect.top = 0;
	aScreenRect.right = GetSystemMetrics(SM_CXSCREEN);
	aScreenRect.bottom = GetSystemMetrics(SM_CYSCREEN);
	resize(aScreenRect);
}


void createOverlays(HINSTANCE theAppInstanceHandle)
{
	sIconCopyMethod = EIconCopyMethod(
		clamp(Profile::getInt("System/IconCopyMethod"),
			1, eIconCopyMethod_Num)-1);

	#ifndef WDA_EXCLUDEFROMCAPTURE
	#define WDA_EXCLUDEFROMCAPTURE 0x00000011
	#endif
	typedef BOOL(WINAPI* PSetWindowDisplayAffinity)(HWND, DWORD);
	PSetWindowDisplayAffinity pSetWindowDisplayAffinity = NULL;
	if( sIconCopyMethod == eIconCopyMethod_ExcludeFromCapture )
	{
		HMODULE hUser32 = LoadLibrary(TEXT("User32.dll"));
		if( hUser32 )
		{
			pSetWindowDisplayAffinity = 
				(PSetWindowDisplayAffinity)GetProcAddress(
					hUser32, "SetWindowDisplayAffinity");
			FreeLibrary(hUser32);
		}
	}

	DBG_ASSERT(sOverlayWindows.empty());
	DBG_ASSERT(sOverlayWindowOrder.empty());

	// Create one transparent overlay window per HUD Element
	sOverlayWindows.reserve(InputMap::hudElementCount());
	sOverlayWindows.resize(InputMap::hudElementCount());
	sOverlayWindowOrder.reserve(sOverlayWindows.size());
	sOverlayWindowOrder.resize(sOverlayWindows.size());
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		sOverlayWindowOrder[i].id = u16(i);
		sOverlayWindowOrder[i].priority = HUD::drawPriority(u16(i));
	}
	std::sort(sOverlayWindowOrder.begin(), sOverlayWindowOrder.end());
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];
		HUD::updateWindowLayout(aHUDElementID, sTargetSize,
			aWindow.components, aWindow.position, aWindow.size);
		aWindow.layoutUpdated = true;

		aWindow.handle = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_NOACTIVATE |
			WS_EX_TRANSPARENT | WS_EX_LAYERED,
			kOverlayWindowClassName,
			widen(InputMap::hudElementKeyName(aHUDElementID)).c_str(),
			WS_POPUP | (sUseChildWindows ? WS_CHILD : 0),
			aWindow.position.x,
			aWindow.position.y,
			aWindow.size.cx,
			aWindow.size.cy,
			sUseChildWindows ? sMainWindow : NULL,
			NULL, theAppInstanceHandle, NULL);

		SetWindowLongPtr(aWindow.handle, GWLP_USERDATA, aHUDElementID);
		aWindow.hideUntilActivated = HUD::shouldStartHidden(aHUDElementID);
		if( sIconCopyMethod == eIconCopyMethod_ExcludeFromCapture &&
			pSetWindowDisplayAffinity )
		{
			pSetWindowDisplayAffinity(aWindow.handle, WDA_EXCLUDEFROMCAPTURE);
		}
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
	sOverlayWindowOrder.clear();
	UnregisterClassW(kMainWindowClassName, theAppInstanceHandle);
	UnregisterClassW(kOverlayWindowClassName, theAppInstanceHandle);
}


void update()
{
	// Update each overlay window as needed
	HDC aScreenDC = GetDC(NULL);
	HDC aCaptureDC = NULL;
	POINT aCaptureOffset = { 0 };
	if( sIconCopyMethod == eIconCopyMethod_TargetWindow &&
		TargetApp::targetWindowHandle() )
	{
		aCaptureDC = GetDC(TargetApp::targetWindowHandle());
	}
	else
	{
		aCaptureDC = GetDC(NULL);
		aCaptureOffset.x = sScreenTargetRect.left;
		aCaptureOffset.y = sScreenTargetRect.top;
	}
	POINT anOriginPoint = { 0, 0 };
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

		// Update alpha fade effects based on gVisibleHUD & gActiveHUD
		updateAlphaFades(aWindow, aHUDElementID);

		// Check visibility status so can mostly ignore hidden windows
		if( sHidden || aWindow.alpha == 0 ||
			(aWindow.hideUntilActivated && !gActiveHUD.test(aHUDElementID)) )
		{
			if( IsWindowVisible(aWindow.handle) )
				ShowWindow(aWindow.handle, SW_HIDE);
			aWindow.hideUntilActivated = HUD::shouldStartHidden(aHUDElementID);
			gActiveHUD.reset(aHUDElementID);
			continue;
		}
		aWindow.hideUntilActivated = false;

		// Check for possible update to window layout
		if( !aWindow.layoutUpdated || gReshapeHUD.test(aHUDElementID) )
		{
			HUD::updateWindowLayout(aHUDElementID, sTargetSize,
				aWindow.components, aWindow.position, aWindow.size);
			if( aWindow.size.cx <= 0 || aWindow.size.cy <= 0 )
			{
				gActiveHUD.reset(aHUDElementID);
				continue;
			}
			aWindow.layoutUpdated = true;
			aWindow.bitmapUpdated = false;
			gReshapeHUD.reset(aHUDElementID);
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
			gRedrawHUD.set(aHUDElementID);
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
		if( gRedrawHUD.test(aHUDElementID) )
		{
			HUD::drawElement(
				aWindowDC, aCaptureDC, aCaptureOffset, sTargetSize,
				aHUDElementID, aWindow.components, needToEraseBitmap);
			aWindow.bitmapUpdated = false;
			gRedrawHUD.reset(aHUDElementID);
		}

		// Update window
		if( !aWindow.bitmapUpdated )
		{
			BLENDFUNCTION aBlendFunction = {AC_SRC_OVER, 0, aWindow.alpha, 0};
			POINT aWindowScreenPos;
			aWindowScreenPos.x = sScreenTargetRect.left + aWindow.position.x;
			aWindowScreenPos.y = sScreenTargetRect.top + aWindow.position.y;
			UpdateLayeredWindow(aWindow.handle, aScreenDC,
				&aWindowScreenPos, &aWindow.size,
				aWindowDC, &anOriginPoint,
				HUD::transColor(aHUDElementID),
				&aBlendFunction, ULW_ALPHA | ULW_COLORKEY);
			aWindow.bitmapUpdated = true;
		}
		gActiveHUD.reset(aHUDElementID);

		// Show window if it isn't visible yet
		if( !IsWindowVisible(aWindow.handle) )
			ShowWindow(aWindow.handle, SW_SHOWNOACTIVATE);

		// Cleanup
		RestoreDC(aWindowDC, aWindowDCInitialState);
		DeleteDC(aWindowDC);
	}
	ReleaseDC(NULL, aCaptureDC);
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

	SIZE aNewTargetSize;
	aNewTargetSize.cx = theNewWindowRect.right - theNewWindowRect.left;
	aNewTargetSize.cy = theNewWindowRect.bottom - theNewWindowRect.top;
	if( aNewTargetSize.cx != sTargetSize.cx ||
		aNewTargetSize.cy != sTargetSize.cy )
	{
		sTargetSize = aNewTargetSize;
		HUD::updateScaling();
	}

	sDesktopTargetRect = sScreenTargetRect = theNewWindowRect;
	sDesktopTargetRect.left -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.right -= GetSystemMetrics(SM_XVIRTUALSCREEN);
	sDesktopTargetRect.top -= GetSystemMetrics(SM_YVIRTUALSCREEN);
	sDesktopTargetRect.bottom -= GetSystemMetrics(SM_YVIRTUALSCREEN);

	for(u16 i = 0; i < sOverlayWindows.size(); ++i)
		sOverlayWindows[i].layoutUpdated = false;
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
	for(size_t i = 0; i < sOverlayWindowOrder.size(); ++i)
	{
		const u16 aHUDElementID = sOverlayWindowOrder[i].id;
		OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

		DBG_ASSERT(aWindow.handle);
		SetWindowPos(
			aWindow.handle, HWND_TOPMOST,
			0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
	}
}


bool overlaysAreHidden()
{
	return sHidden;
}


SIZE overlayTargetSize()
{
	return sTargetSize;
}


void readUIScale()
{
	gUIScale = Profile::getFloat("System/UIScale", 1.0f);

	const std::string& aUIScaleRegKey =
		Profile::getStr("System/UIScaleRegKey");
	if( !aUIScaleRegKey.empty() )
	{
		HKEY hKey;
		const std::string& aPrefix = upper(getFileName(aUIScaleRegKey));
		std::string aSubKey = upper(getFileDir(aUIScaleRegKey));
		const std::string kRootKey = "HKEY_CURRENT_USER";
		if( aSubKey.substr(0, kRootKey.size()) == kRootKey )
			aSubKey = aSubKey.substr(kRootKey.size()+1);

		if( RegOpenKeyExA(HKEY_CURRENT_USER, aSubKey.c_str(),
			0, KEY_READ, &hKey) != ERROR_SUCCESS )
			return;

		union { u8 buf[sizeof(double)]; double val; } aDoubleValBuffer;
		LONG aResult = 0;
		for(DWORD i = 0; aResult != ERROR_NO_MORE_ITEMS; ++i)
		{
			DWORD dataSize = sizeof(aDoubleValBuffer);
			char aValueName[256];
			DWORD aValueNameSize = ARRAYSIZE(aValueName);
			DWORD type;
			aResult = RegEnumValueA(
				hKey, i, aValueName, &aValueNameSize, NULL, &type,
				&aDoubleValBuffer.buf[0], &dataSize);			
			if( aResult != ERROR_SUCCESS )
				continue;

			if( aValueNameSize > aPrefix.size() &&
				upper(aValueName).substr(0, aPrefix.size()) == aPrefix )
			{
				gUIScale = aDoubleValBuffer.val;
				break;
			}
		}
		RegCloseKey(hKey);
	}
	HUD::updateScaling();
}



POINT mouseToOverlayPos(bool clamped)
{
	POINT result;
	//  Get current screen-relative mouse position
	GetCursorPos(&result);
	// Offset to client relative position
	result.x -= sScreenTargetRect.left;
	result.y -= sScreenTargetRect.top;
	if( clamped )
	{// Clamp to within client rect range
		result.x = clamp(result.x, 0, sTargetSize.cx - 1);
		result.y = clamp(result.y, 0, sTargetSize.cy - 1);
	}
	return result;
}


POINT hotspotToOverlayPos(const Hotspot& theHotspot)
{
	POINT result = {0, 0};
	if( !sMainWindow )
		return result;

	// Start with percentage of client rect as u16 (i.e. 65536 == 100%)
	result.x = theHotspot.x.anchor;
	result.y = theHotspot.y.anchor;
	// Convert to client-rect-relative pixel position
	result.x = result.x * sTargetSize.cx / 0x10000;
	result.y = result.y * sTargetSize.cy / 0x10000;
	// Add fixed pixel offset
	result.x += theHotspot.x.offset;
	result.y += theHotspot.y.offset;
	// Add pixel offset w/ position scaling applied
	result.x += theHotspot.x.scaled * gUIScale;
	result.y += theHotspot.y.scaled * gUIScale;
	// Clamp to within client rect range
	result.x = clamp(result.x, 0, sTargetSize.cx - 1);
	result.y = clamp(result.y, 0, sTargetSize.cy - 1);
	return result;
}


Hotspot overlayPosToHotspot(POINT theMousePos)
{
	Hotspot result;
	result.x.anchor = theMousePos.x * 0x10000 / sTargetSize.cx;
	result.y.anchor = theMousePos.y * 0x10000 / sTargetSize.cy;
	return result;
}


POINT overlayPosToNormalizedMousePos(POINT theMousePos)
{
	if( !sMainWindow )
	{
		theMousePos.x = 32768;
		theMousePos.y = 32768;
		return theMousePos;
	}

	const int kDesktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int kDesktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	// Convert to virtual desktop pixel coordinate
	theMousePos.x = max(0, theMousePos.x + sDesktopTargetRect.left);
	theMousePos.y = max(0, theMousePos.y + sDesktopTargetRect.top);
	// Convert to % of virtual desktop size as normalized 0-65535
	theMousePos.x = (theMousePos.x + 1) * 0xFFFF / kDesktopWidth;
	theMousePos.y = (theMousePos.y + 1) * 0xFFFF / kDesktopHeight;
	return theMousePos;
}


POINT normalizedMouseToOverlayPos(POINT theSentMousePos)
{
	const int kDesktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	const int kDesktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	
	// Restore from normalized to pixel position of virtual desktop
	theSentMousePos.x = theSentMousePos.x * kDesktopWidth / 0x10000;
	theSentMousePos.y = theSentMousePos.y * kDesktopHeight / 0x10000;
	// Offset to be client relative position
	theSentMousePos.x -= sDesktopTargetRect.left;
	theSentMousePos.y -= sDesktopTargetRect.top;
	return theSentMousePos;
}


Hotspot hotspotForMenuItem(u16 theMenuID, u16 theMenuItemIdx)
{
	const u16 aHUDElementID = InputMap::hudElementForMenu(theMenuID);
	OverlayWindow& aWindow = sOverlayWindows[aHUDElementID];

	const size_t aCompIndex =
		min(aWindow.components.size()-1, theMenuItemIdx + 1);
	POINT aPos;
	aPos.x = aWindow.components[aCompIndex].left;
	aPos.y = aWindow.components[aCompIndex].top;
	aPos.x += aWindow.components[aCompIndex].right;
	aPos.y += aWindow.components[aCompIndex].bottom;
	aPos.x /= 2;
	aPos.y /= 2;
	aPos.x += aWindow.position.x;
	aPos.y += aWindow.position.y;

	return overlayPosToHotspot(aPos);
}

} // WindowManager
