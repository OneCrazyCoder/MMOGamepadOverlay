//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "OverlayWindow.h"

#include "HUD.h"
#include "Profile.h"

namespace OverlayWindow
{

//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	int autoFadeDelay;
	float minAlpha;
	float maxAlpha;
	float fadeInRate;
	float fadeOutRate;

	void load()
	{
		this->autoFadeDelay = max(1, Profile::getInt("HUD/FadeOutDelay", 500));
		this->minAlpha = float(clamp(Profile::getInt("HUD/MinAlpha", 0), 0, 255));
		this->maxAlpha = float(clamp(Profile::getInt("HUD/MaxAlpha", 255), 0, 255));
		const int aFadeInTime = Profile::getInt("HUD/FadeInTime", 125);
		this->fadeInRate = float(this->maxAlpha - this->minAlpha) / aFadeInTime;
		const int aFadeOutTime = Profile::getInt("HUD/FadeOutTime", 650);
		this->fadeOutRate = float(this->maxAlpha - this->minAlpha) / aFadeOutTime;
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
static float sWindowAlpha = 0;
static int sAutoFadeOutTimer = 0;
static bool sFadingIn = false;
static bool sFadingOut = false;


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

	SetLayeredWindowAttributes(gHandle, RGB(0, 0, 0), BYTE(0), LWA_COLORKEY | LWA_ALPHA);
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
	sWindowAlpha = clamp(sWindowAlpha, kConfig.minAlpha, kConfig.maxAlpha);
	const BYTE anAlphaVal = u8(clamp(sWindowAlpha, 0.0, 255.0));
	SetLayeredWindowAttributes(gHandle, RGB(0, 0, 0), BYTE(anAlphaVal), LWA_COLORKEY | LWA_ALPHA);
	redraw();
}


void update()
{
	if( !gHandle )
		return;

	if( sFadingIn )
	{
		sWindowAlpha = min(sWindowAlpha + kConfig.fadeInRate * gAppFrameTime, kConfig.maxAlpha);
		const BYTE anAlphaVal = u8(clamp(sWindowAlpha, 0.0, 255.0));
		SetLayeredWindowAttributes(gHandle, RGB(0, 0, 0), BYTE(anAlphaVal), LWA_COLORKEY | LWA_ALPHA);
		if( sWindowAlpha >= kConfig.maxAlpha )
			sFadingIn = false;
	}

	if( sFadingOut )
	{
		sWindowAlpha = max(sWindowAlpha - kConfig.fadeOutRate * gAppFrameTime, kConfig.minAlpha);
		const BYTE anAlphaVal = u8(clamp(sWindowAlpha, 0.0, 255.0));
		SetLayeredWindowAttributes(gHandle, RGB(0, 0, 0), BYTE(anAlphaVal), LWA_COLORKEY | LWA_ALPHA);
		if( sWindowAlpha <= kConfig.minAlpha )
			sFadingOut = false;
	}

	if( sAutoFadeOutTimer > 0 )
	{
		sAutoFadeOutTimer -= gAppFrameTime;
		if( sAutoFadeOutTimer <= 0 )
			beginFadeOut();
	}

	HUD::update();
}


void redraw()
{
	if( gHandle )
		InvalidateRect(gHandle, NULL, true);
}

void fadeFullyIn()
{
	sFadingOut = false;
	sFadingIn = true;
}


void beginFadeOut()
{
	abortAutoFadeOut();
	sFadingOut = true;
	sFadingIn = false;
}


void startAutoFadeOutTimer()
{
	sAutoFadeOutTimer = kConfig.autoFadeDelay;
}


void abortAutoFadeOut()
{
	sAutoFadeOutTimer = 0;
}


bool isAutoFadeOutTimeSet()
{
	return gHandle && sAutoFadeOutTimer > 0;
}

} // OverlayWindow
