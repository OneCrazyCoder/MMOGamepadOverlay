//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "Gamepad.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <xinput.h>

namespace Gamepad
{

// Whether or not debug messages print depends on which line is commented out
//#define gamepadDebugPrint(...) debugPrint("Gamepad: " __VA_ARGS__)
#define gamepadDebugPrint(...) ((void)0)

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kMaxGamepadsEnumerated = 8,
kDIGamepadDataBufferSize = 64,
// If a gamepad receives new input and its been this long since any other
// gamepad received new input, then that gamepad becomes auto-selected.
kGamepadAutoSelectTimeout = 2000,	// 2 seconds (in milliseconds)
};

static const u16 kVendorID[] =
{
	0x0000,	// eVendorID_Unknown
	0x054c,	// eVendorID_Sony
	0x057e,	// eVendorID_Nintendo
	0x045e,	// eVendorID_Microsoft
};
DBG_CTASSERT(ARRAYSIZE(kVendorID) == eVendorID_Num);

static const u16 kXInputMap[] =
{
	0x0000,							// eBtn_None
	0x0000,	0x0000,	0x0000,	0x0000,	// eBtn_LS
	0x0000,	0x0000,	0x0000,	0x0000,	// eBtn_RS
	XINPUT_GAMEPAD_DPAD_LEFT,		// eBtn_DLeft
	XINPUT_GAMEPAD_DPAD_RIGHT,		// eBtn_DRight
	XINPUT_GAMEPAD_DPAD_UP,			// eBtn_DUp
	XINPUT_GAMEPAD_DPAD_DOWN,		// eBtn_DDown
	XINPUT_GAMEPAD_X,				// eBtn_FLeft
	XINPUT_GAMEPAD_B,				// eBtn_FRight
	XINPUT_GAMEPAD_Y,				// eBtn_FUp
	XINPUT_GAMEPAD_A,				// eBtn_FDown
	XINPUT_GAMEPAD_LEFT_SHOULDER,	// eBtn_L1
	XINPUT_GAMEPAD_RIGHT_SHOULDER,	// eBtn_R1
	0x0000,	0x0000,					// eBtn_L2/R2
	XINPUT_GAMEPAD_BACK,			// eBtn_Select
	XINPUT_GAMEPAD_START,			// eBtn_Start
	XINPUT_GAMEPAD_LEFT_THUMB,		// eBtn_L3
	XINPUT_GAMEPAD_RIGHT_THUMB,		// eBtn_R3
	0x0400,							// eBtn_Home
	0x0800,							// eBtn_Extra
};
DBG_CTASSERT(ARRAYSIZE(kXInputMap) == eBtn_Num);

static const EButton kDIToEFButton[eVendorID_Num][4] =
{
	{ eBtn_FLeft, eBtn_FRight, eBtn_FUp, eBtn_FDown }, // eVendorID_Unknown
	{ eBtn_FLeft, eBtn_FDown, eBtn_FRight, eBtn_FUp }, // eVendorID_Sony
	{ eBtn_FDown, eBtn_FRight, eBtn_FLeft, eBtn_FUp }, // eVendorID_Nintendo
	{ eBtn_FDown, eBtn_FRight, eBtn_FLeft, eBtn_FUp }, // eVendorID_Microsoft
};

static const EAxis kDIToEAxis[eVendorID_Num][6] =
{// From DIJOFS_X,_Y,_Z,_RX,_RY,_RZ
	// eVender_Unknown
	{ eAxis_LSLeft, eAxis_LSUp, eAxis_None,
	  eAxis_RSLeft,	eAxis_RSUp,	eAxis_None },
	// eVendor_Sony
	{ eAxis_LSLeft, eAxis_LSUp, eAxis_RSLeft,
	  eAxis_None,	eAxis_None,	eAxis_RSUp },
	// eVendor_Nintendo
	{ eAxis_LSLeft, eAxis_LSUp, eAxis_None,
	  eAxis_RSLeft,	eAxis_RSUp,	eAxis_None },
	// eVendor_Microsoft
	{ eAxis_LSLeft, eAxis_LSUp, eAxis_RTrigger,
	  eAxis_RSLeft,	eAxis_RSUp,	eAxis_LTrigger },		
};


//-----------------------------------------------------------------------------
// GamepadData
//-----------------------------------------------------------------------------

struct GamepadData : private ConstructFromZeroInitializedMemory<GamepadData>
{
	LPDIRECTINPUT8 hDirectInput8;
	int deviceCountForDInput;
	int deviceCountForXInput;
	int selectedGamepadID;
	DIDEVICEINSTANCE* gamepadGUID[kMaxGamepadsEnumerated];
	struct Gamepad
	{
		LPDIRECTINPUTDEVICE8 device;
		std::string name;
		DWORD lastUpdateTime;
		DWORD xInputPacketNum;
		BitArray<eBtn_Num> buttonsHit;
		BitArray<eBtn_Num> buttonsDown;
		BitArray<eBtn_Num> initialState;
		EVendorID vendorID : 8;
		u8 axisVal[eAxis_Num];
		u8 xInputID;
		bool wasConnected;
		bool initialStateSet;
		bool hasReceivedInput;
	} gamepad[kMaxGamepadsEnumerated];

	u8 digitalDeadzone[eBtn_FirstDigital];
	bool disconnectDetected;
	bool doNotAutoSelectDInputDevices;
	bool initialized;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static GamepadData sGamepadData;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

// Forward declares
static EResult activateGamepad(int);

static void filterInitialInputs(int theGamepadID)
{
	// This function exists to make sure that if a gamepad is sending constant
	// data that a button is held down or a stick is being pressed, it doesn't
	// register it in the game. This is to prevent the game from "acting up"
	// because the user had a gamepad plugged in that's sitting forgotten
	// upside-down on the desk and is sending input constantly.
	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[theGamepadID];
	if( !aGamepad.initialStateSet )
	{
		// Log first input received as initial state
		aGamepad.initialState = aGamepad.buttonsDown;
		// Don't react to anything initially held down
		aGamepad.buttonsDown.reset();
		aGamepad.buttonsHit.reset();
		for(int i = 0; i < eAxis_Num; ++i)
			aGamepad.axisVal[i] = 0;
		aGamepad.initialStateSet = true;
	}
	else if( aGamepad.initialState.any() )
	{
		// Once initially-held input is released, stop tracking it
		aGamepad.initialState &= aGamepad.buttonsDown;
		// Don't count anything in initialState as being pressed
		aGamepad.buttonsDown &= ~aGamepad.initialState;
		aGamepad.buttonsHit &= ~aGamepad.initialState;
	}

	// All analog values are locked to 0 until hasReceivedInput is set
	// to prevent any slow unexplained "drifting" from an unused controller
	// (also note that hasReceivedInput being false prevents any requests
	// for vibration, so unused controller doesn't rattle away on the desk)
	if( !aGamepad.hasReceivedInput )
	{
		for(int i = 0; i < eAxis_Num; ++i)
			aGamepad.axisVal[i] = 0;
	}
}


//-----------------------------------------------------------------------------
// This function was taken directly from MSDN wholesale!
// Enum each PNP device using WMI and check each deviceID to see if it contains
// "IG_" (ex. "VID_045E&PID_028E&IG_00"). If so, then it's an XInput device!
// Unfortunately this information can not be found by just using DirectInput.
//-----------------------------------------------------------------------------
#include <wbemidl.h>
#include <oleauto.h>
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }
static BOOL isXInputDevice( const GUID* pGuidProductFromDirectInput )
{
	IWbemLocator*			pIWbemLocator	= NULL;
	IEnumWbemClassObject*   pEnumDevices	= NULL;
	IWbemClassObject*		pDevices[20]	= {0};
	IWbemServices*			pIWbemServices	= NULL;
	BSTR					bstrNamespace	= NULL;
	BSTR					bstrDeviceID	= NULL;
	BSTR					bstrClassName	= NULL;
	DWORD					uReturned		= 0;
	bool					bIsXinputDevice	= false;
	UINT					iDevice			= 0;
	VARIANT					var;
	HRESULT					hr;

	// CoInit if needed
	hr = CoInitialize(NULL);
	bool bCleanupCOM = SUCCEEDED(hr);

	// Create WMI
	hr = CoCreateInstance( __uuidof(WbemLocator),
						   NULL,
						   CLSCTX_INPROC_SERVER,
						   __uuidof(IWbemLocator),
						   (LPVOID*) &pIWbemLocator);

	if( FAILED(hr) || pIWbemLocator == NULL ) goto LCleanup;
	bstrNamespace = SysAllocString( L"\\\\.\\root\\cimv2" );
	if( bstrNamespace == NULL ) goto LCleanup;		
	bstrClassName = SysAllocString( L"Win32_PNPEntity" );
	if( bstrClassName == NULL ) goto LCleanup;		
	bstrDeviceID  = SysAllocString( L"DeviceID" );
	if( bstrDeviceID == NULL )  goto LCleanup;		
	
	// Connect to WMI 
	hr = pIWbemLocator->ConnectServer( bstrNamespace, NULL, NULL, 0L, 
									   0L, NULL, NULL, &pIWbemServices );
	if( FAILED(hr) || pIWbemServices == NULL )
		goto LCleanup;

	// Switch security level to IMPERSONATE. 
	CoSetProxyBlanket(
		pIWbemServices,
		RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, 
		RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
		NULL, EOAC_NONE );

	hr = pIWbemServices->CreateInstanceEnum(
		bstrClassName, 0, NULL, &pEnumDevices);
	if( FAILED(hr) || pEnumDevices == NULL )
		goto LCleanup;

	// Loop over all devices
	for( ;; )
	{
		// Get 20 at a time
		hr = pEnumDevices->Next( 10000, 20, pDevices, &uReturned );
		if( FAILED(hr) )
			goto LCleanup;
		if( uReturned == 0 )
			break;

		for( iDevice=0; iDevice<uReturned; iDevice++ )
		{
			// For each device, get its device ID
			hr = pDevices[iDevice]->Get( bstrDeviceID, 0L, &var, NULL, NULL );
			if( SUCCEEDED( hr ) && var.vt == VT_BSTR && var.bstrVal != NULL )
			{
				// Check if the device ID contains "IG_".
				if( wcsstr( var.bstrVal, L"IG_" ) )
				{
					// If it does, then get the VID/PID from var.bstrVal
					DWORD dwPid = 0, dwVid = 0;
					WCHAR* strVid = wcsstr( var.bstrVal, L"VID_" );
					if( strVid && swscanf( strVid, L"VID_%4X", &dwVid ) != 1 )
						dwVid = 0;
					WCHAR* strPid = wcsstr( var.bstrVal, L"PID_" );
					if( strPid && swscanf( strPid, L"PID_%4X", &dwPid ) != 1 )
						dwPid = 0;

					// Compare the VID/PID to the DInput device
					DWORD dwVidPid = MAKELONG( dwVid, dwPid );
					if( dwVidPid == pGuidProductFromDirectInput->Data1 )
					{
						bIsXinputDevice = true;
						goto LCleanup;
					}
				}
			}
			SAFE_RELEASE( pDevices[iDevice] );
		}
	}

LCleanup:
	if(bstrNamespace)
		SysFreeString(bstrNamespace);
	if(bstrDeviceID)
		SysFreeString(bstrDeviceID);
	if(bstrClassName)
		SysFreeString(bstrClassName);
	for( iDevice=0; iDevice<20; iDevice++ )
		SAFE_RELEASE( pDevices[iDevice] );
	SAFE_RELEASE( pEnumDevices );
	SAFE_RELEASE( pIWbemLocator );
	SAFE_RELEASE( pIWbemServices );

	if( bCleanupCOM )
		CoUninitialize();

	return bIsXinputDevice;
}
#undef SAFE_RELEASE


static void pollXInputGamepad(int theGamepadID)
{
	DBG_ASSERT(theGamepadID >= 0 && theGamepadID < kMaxGamepadsEnumerated);
	DBG_ASSERT(sGamepadData.gamepad[theGamepadID].xInputID > 0);
	DBG_ASSERT(sGamepadData.gamepad[theGamepadID].xInputID <= XUSER_MAX_COUNT);
	
	const DWORD xInputID = sGamepadData.gamepad[theGamepadID].xInputID - 1;

	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[theGamepadID];
	aGamepad.buttonsHit.reset();

	DWORD dwResult;	
	XINPUT_STATE state;
	ZeroMemory( &state, sizeof(XINPUT_STATE) );
	dwResult = XInputGetState( xInputID, &state );
	
	if( dwResult == ERROR_SUCCESS )
	{ 
		// Controller is connected
		aGamepad.wasConnected = true;
		if( state.dwPacketNumber != aGamepad.xInputPacketNum )
		{// New data received!
			aGamepad.xInputPacketNum = state.dwPacketNumber;
			for(int i = 1; i < eBtn_Num; ++i)
			{
				u16 isDown = 0;	// not a bool since using bit flag checks
				switch(i)
				{
				case eBtn_L2:
					aGamepad.axisVal[eAxis_LTrigger] = state.Gamepad.bLeftTrigger;
					isDown = state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
					break;
				case eBtn_R2:
					aGamepad.axisVal[eAxis_RTrigger] = state.Gamepad.bRightTrigger;
					isDown = state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
					break;
				case eBtn_LSRight:
					aGamepad.axisVal[eAxis_LSRight] = state.Gamepad.sThumbLX > 0
						? u8(state.Gamepad.sThumbLX >> 7) : 0;
					break;
				case eBtn_LSLeft:
					aGamepad.axisVal[eAxis_LSLeft] = state.Gamepad.sThumbLX < 0
						? u8(-(state.Gamepad.sThumbLX +1) >> 7) : 0;
					break;
				case eBtn_LSDown:
					aGamepad.axisVal[eAxis_LSDown] = state.Gamepad.sThumbLY < 0
						? u8(-(state.Gamepad.sThumbLY +1) >> 7) : 0;
					break;
				case eBtn_LSUp:
					aGamepad.axisVal[eAxis_LSUp] = state.Gamepad.sThumbLY > 0
						? u8(state.Gamepad.sThumbLY >> 7) : 0;
					break;
				case eBtn_RSRight:
					aGamepad.axisVal[eAxis_RSRight] = state.Gamepad.sThumbRX > 0
						? u8(state.Gamepad.sThumbRX >> 7) : 0;
					break;
				case eBtn_RSLeft:
					aGamepad.axisVal[eAxis_RSLeft] = state.Gamepad.sThumbRX < 0
						? u8(-(state.Gamepad.sThumbRX +1) >> 7) : 0;
					break;
				case eBtn_RSDown:
					aGamepad.axisVal[eAxis_RSDown] = state.Gamepad.sThumbRY < 0
						? u8(-(state.Gamepad.sThumbRY +1) >> 7) : 0;
					break;
				case eBtn_RSUp:
					aGamepad.axisVal[eAxis_RSUp] = state.Gamepad.sThumbRY > 0
						? u8(state.Gamepad.sThumbRY >> 7) : 0;
					break;
				default:
					isDown = state.Gamepad.wButtons & kXInputMap[i];
					break;
				}
				if( i < eBtn_FirstDigital )
				{
					const EAxis anAxis = axisForButton(EButton(i));
					isDown = aGamepad.axisVal[anAxis] > sGamepadData.digitalDeadzone[anAxis];
				}
				if( isDown )
				{
					if( !aGamepad.buttonsDown.test(i) )
						aGamepad.buttonsHit.set(i);
					aGamepad.buttonsDown.set(i);
				}
				else
				{
					aGamepad.buttonsDown.reset(i);
				}
			}
			filterInitialInputs(theGamepadID);
		}
	}
	else if( aGamepad.wasConnected )
	{
		sGamepadData.disconnectDetected = true;
		gamepadDebugPrint("XInput Gamepad %s (%d) has been disconnected!\n",
			aGamepad.name.c_str(), aGamepad.xInputID);
	}
}


static char nextFreeXInputID()
{
	if( sGamepadData.deviceCountForXInput >= XUSER_MAX_COUNT )
		return 0;

	const int prevXInputDeviceCount = sGamepadData.deviceCountForXInput;

	DWORD dwResult;
	XINPUT_STATE state;

	do {
		// Query the controller state from XInput to see if it is plugged in
		dwResult = XInputGetState(sGamepadData.deviceCountForXInput, &state);

		// We actually return 1+ the ID number - so controller 0 is ID 1
		++sGamepadData.deviceCountForXInput;

		// If device was connected, we can return the new ID as valid
		if( dwResult == ERROR_SUCCESS )
			return sGamepadData.deviceCountForXInput;

		// Otherwise try the next ID and see if that controller is connected
	} while(dwResult != ERROR_SUCCESS &&
			sGamepadData.deviceCountForXInput < XUSER_MAX_COUNT);

	// No controller actually found - reset device count to previous value
	sGamepadData.deviceCountForXInput = prevXInputDeviceCount;

	return 0;
}


static void addXInputGamepad(int theGamepadID)
{
	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[theGamepadID];

	aGamepad.name = std::string("XInput Controller #") +
		toString(sGamepadData.gamepad[theGamepadID].xInputID);

	gamepadDebugPrint("%s will use XInput PlayerID %d!\n",
		aGamepad.name.c_str(), aGamepad.xInputID);

	aGamepad.wasConnected = true;

	// Get initial XInput controller state so can begin tracking state changes
	pollXInputGamepad(theGamepadID);
	aGamepad.lastUpdateTime = gAppRunTime;

	// Stop any previously active vibration
	const DWORD xInputID = aGamepad.xInputID - 1;
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = 0;
	vibration.wRightMotorSpeed = 0;
	XInputSetState(xInputID, &vibration);
}


static void addGamepad(LPCDIDEVICEINSTANCE lpddi)
{
	const int aNewGamepadID = sGamepadData.deviceCountForDInput;
	delete sGamepadData.gamepadGUID[aNewGamepadID];
	sGamepadData.gamepadGUID[aNewGamepadID] = new DIDEVICEINSTANCE;
	*sGamepadData.gamepadGUID[aNewGamepadID] = *lpddi;

	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[aNewGamepadID];
	aGamepad.name = narrow(lpddi->tszInstanceName);

	gamepadDebugPrint("Gamepad found: %s\n", aGamepad.name.c_str());

	aGamepad.vendorID = eVendorID_Unknown;
	for(int id = 1; id < eVendorID_Num; ++id)
	{
		if( (lpddi->guidProduct.Data1 & 0xFFFF) == kVendorID[id] )
		{
			aGamepad.vendorID = EVendorID(id);
			break;
		}
	}

	if( isXInputDevice(&lpddi->guidProduct) )
	{
		aGamepad.xInputID = nextFreeXInputID();
		if( aGamepad.xInputID > 0 )
		{
			// Successfully identified this XInput controller
			addXInputGamepad(aNewGamepadID);
		}

		// Assume XInput controllers are MS controllers if no info otherwise
		if( aGamepad.vendorID == eVendorID_Unknown )
			aGamepad.vendorID = eVendorID_Microsoft;
	}
	
	++sGamepadData.deviceCountForDInput;
}


static void releaseGamepad(int theGamepadID)
{
	if( theGamepadID < 0 ||
		theGamepadID >= kMaxGamepadsEnumerated ||
		sGamepadData.gamepadGUID[theGamepadID] == NULL )
		return;

	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[theGamepadID];
	aGamepad.buttonsDown.reset();
	aGamepad.buttonsHit.reset();
	aGamepad.lastUpdateTime = 0;
	aGamepad.xInputID = 0;
	aGamepad.initialState.reset();
	aGamepad.wasConnected = false;
	aGamepad.initialStateSet = false;
	aGamepad.hasReceivedInput = false;

	gamepadDebugPrint("Released gamepad %s!\n", aGamepad.name.c_str());

	// No need to release or unacquire XInput devices
	if( aGamepad.xInputID > 0 || aGamepad.device == NULL )
		return;

	aGamepad.device->Unacquire();
	aGamepad.device->Release();
	aGamepad.device = NULL;
}


static void pollGamepad(int theGamepadID)
{
	DBG_ASSERT(theGamepadID >= 0 && theGamepadID < kMaxGamepadsEnumerated);

	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[theGamepadID];
	if( aGamepad.xInputID > 0 )
	{
		// Use XInput version of polling
		pollXInputGamepad(theGamepadID);
		return;
	}

	if( aGamepad.device == NULL )
	{
		// Need to activate this gamepad first
		if( activateGamepad(theGamepadID) != eResult_Ok )
			return;
	}
	
	aGamepad.buttonsHit.reset();
	aGamepad.device->Poll();

	DIDEVICEOBJECTDATA rgdod[kDIGamepadDataBufferSize]; 
	DWORD dwItems = kDIGamepadDataBufferSize; 
	HRESULT hr =
		aGamepad.device->GetDeviceData(
			sizeof(DIDEVICEOBJECTDATA), 
			rgdod, 
			&dwItems, 
			0);

	// dwItems = Number of elements read (could be zero).
	bool readEvents = dwItems > 0;
	while(SUCCEEDED(hr) && readEvents)
	{
		readEvents = (hr == DI_BUFFEROVERFLOW);
		// Consider increasing kDIGamepadDataBufferSize if see this often
		if( readEvents )
		{
			gamepadDebugPrint(
				"GetDeviceData buffer overflow (not critical)\n");
		}

		for(DWORD i = 0; i < dwItems; ++i)
		{
			DWORD anInputOfs = rgdod[i].dwOfs;
			if( anInputOfs >= DIJOFS_BUTTON0 &&
				anInputOfs < DIJOFS_BUTTON0 + eBtn_DInputBtnNum )
			{// Button toggle
				const DWORD aDIButtonIdx = anInputOfs - DIJOFS_BUTTON0;
				EButton aButton = eBtn_FirstDInputBtn;
				// Remap face buttons according to vendorID
				if( aDIButtonIdx < 4 )
					aButton = kDIToEFButton[aGamepad.vendorID][aDIButtonIdx];
				else
					aButton = EButton(aButton + aDIButtonIdx);
				if( aGamepad.vendorID == eVendorID_Microsoft &&
					aButton >= eBtn_L2 )
				{// Compensate for Microsoft missing digital L2/R2 buttons
					aButton = EButton(aButton + 2);
				}
				if( rgdod[i].dwData )
				{// Button is now down
					aGamepad.buttonsHit.set(aButton);
					aGamepad.buttonsDown.set(aButton);
				}
				else
				{// Button is now up
					aGamepad.buttonsDown.reset(aButton);
				}
			}
			else if( anInputOfs >= DIJOFS_X && anInputOfs <= DIJOFS_RZ )
			{// Axis movement
				const DWORD aDIAxisIdx = (anInputOfs - DIJOFS_X) / sizeof(LONG);
				EAxis anAxis[2];
				anAxis[0] = kDIToEAxis[aGamepad.vendorID][aDIAxisIdx];
				anAxis[1] = eAxis_None;
				if( anAxis[0] != eAxis_None )
				{// Analog stick axis
					anAxis[1] = EAxis(anAxis[0] + 1);
					const int anAxisVal = int(rgdod[i].dwData) - 0x8000;
					if( anAxisVal >= 0 )
					{
						aGamepad.axisVal[anAxis[0]] = 0;
						aGamepad.axisVal[anAxis[1]] = u8(anAxisVal >> 7);
					}
					else
					{
						aGamepad.axisVal[anAxis[0]] = u8(-(anAxisVal +1) >> 7);
						aGamepad.axisVal[anAxis[1]] = 0;
					}
				}
				else if( aGamepad.vendorID == eVendorID_Sony )
				{
					// Sony maps L2 to RX and R2 to RY,
					// but with the full axis range of 0-65535 for each
					if( anInputOfs == DIJOFS_RX )
						anAxis[0] = eAxis_LTrigger;
					else if( anInputOfs == DIJOFS_RY )
						anAxis[0] = eAxis_RTrigger;
					aGamepad.axisVal[anAxis[0]] = rgdod[i].dwData >> 8;
				}
				for(int j = 0; j < 2; ++j)
				{
					const EButton aButton = buttonForAxis(anAxis[j]);
					if( aButton &&
						aGamepad.axisVal[anAxis[j]] > sGamepadData.digitalDeadzone[aButton] )
					{
						if( !aGamepad.buttonsDown.test(aButton) )
							aGamepad.buttonsHit.set(aButton);
						aGamepad.buttonsDown.set(aButton);
					}
					else
					{
						aGamepad.buttonsDown.reset(aButton);
					}
				}
			}
			else if( anInputOfs == DIJOFS_POV(0) )
			{// Hat/POV/D-Pad movement
				BitArray<eBtn_Num> prevInputsDown = aGamepad.buttonsDown;
				aGamepad.buttonsDown.reset(eBtn_DLeft);
				aGamepad.buttonsDown.reset(eBtn_DRight);
				aGamepad.buttonsDown.reset(eBtn_DUp);
				aGamepad.buttonsDown.reset(eBtn_DDown);
				if( (signed)rgdod[i].dwData != -1 && (LOWORD(rgdod[i].dwData) != 0xFFFF) )
				{// Moved
					if( rgdod[i].dwData >= 22500 && rgdod[i].dwData <= 31500 )
					{// Left
						if( !prevInputsDown.test(eBtn_DLeft) )
							aGamepad.buttonsHit.set(eBtn_DLeft);
						aGamepad.buttonsDown.set(eBtn_DLeft);
					}
					if( rgdod[i].dwData >= 4500 && rgdod[i].dwData <= 13500 )
					{// Right
						if( !prevInputsDown.test(eBtn_DRight) )
							aGamepad.buttonsHit.set(eBtn_DRight);
						aGamepad.buttonsDown.set(eBtn_DRight);
					}
					if( rgdod[i].dwData <= 4500 || rgdod[i].dwData >= 31500 )
					{// Up
						if( !prevInputsDown.test(eBtn_DUp) )
							aGamepad.buttonsHit.set(eBtn_DUp);
						aGamepad.buttonsDown.set(eBtn_DUp);
					}
					if( rgdod[i].dwData >= 13500 && rgdod[i].dwData <= 22500 )
					{// Down
						if( !prevInputsDown.test(eBtn_DDown) )
							aGamepad.buttonsHit.set(eBtn_DDown);
						aGamepad.buttonsDown.set(eBtn_DDown);
					}
				}
			}
		}
		if( readEvents )
		{// Get rest of events if got buffer overflow
			dwItems = kDIGamepadDataBufferSize; 
			hr = aGamepad.device->GetDeviceData(
					sizeof(DIDEVICEOBJECTDATA), 
					rgdod, 
					&dwItems, 
					0);
		}
	}
		
	if( SUCCEEDED(hr) && dwItems > 0 )
	{
		filterInitialInputs(theGamepadID);
	}
	else if( hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED )
	{// Attempt to re-acquire gamepad and try again next time
		gamepadDebugPrint(
			"Gamepad %s lost...\n",
			aGamepad.name.c_str());
		if( SUCCEEDED(aGamepad.device->Acquire()) )
		{
			aGamepad.wasConnected = true;
			gamepadDebugPrint(
				"Successfully re-acquired gamepad %s\n",
				aGamepad.name.c_str());
		}
		else if( aGamepad.wasConnected )
		{
			// Controller was connected but no longer is - need to re-check controllers!
			sGamepadData.disconnectDetected = true;
			gamepadDebugPrint(
				"Failed re-acquiring gamepad %s! Assuming disconnected...\n",
				aGamepad.name.c_str());
		}
	}
}


static EResult activateGamepad(int theGamepadID)
{
	if( theGamepadID < 0 ||
		theGamepadID >= kMaxGamepadsEnumerated ||
		sGamepadData.gamepadGUID[theGamepadID] == NULL ||
		!sGamepadData.initialized )
		return eResult_Fail;

	GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[theGamepadID];
	if( aGamepad.xInputID > 0 )
	{// Using XInput for this device, no need to initialize it!
		return eResult_Ok;
	}

	// If already activated, exit
	if( aGamepad.device != NULL )
		return eResult_Ok;

   // Obtain an interface to the enumerated gamepad.
	HRESULT hr =
		sGamepadData.hDirectInput8->CreateDevice(
			sGamepadData.gamepadGUID[theGamepadID]->guidInstance,
			&aGamepad.device,
			NULL );

	// If it failed, then we can't use this gamepad
	// Maybe the user unplugged during enumeration?
	if( FAILED(hr) )
	{
		releaseGamepad(theGamepadID);
		gamepadDebugPrint(
			"Failed to create gamepad device %s\n",
			aGamepad.name.c_str());
		return eResult_Fail;
	}

	// Setup data format
	if( FAILED(aGamepad.device->SetDataFormat(&c_dfDIJoystick)) )
	{
		releaseGamepad(theGamepadID);
		gamepadDebugPrint(
			"Failed to set data format for device %s\n",
			aGamepad.name.c_str());
		return eResult_Fail;
	}
	
	// Setup cooperative level
	if( FAILED(aGamepad.device->SetCooperativeLevel(
				NULL, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)) )
	{
		releaseGamepad(theGamepadID);
		gamepadDebugPrint(
			"Failed to set cooperative level for device %s\n",
			aGamepad.name.c_str());
		return eResult_Fail;
	}

	// Set buffer size
	DIPROPDWORD dipdw;
	dipdw.diph.dwSize		= sizeof( dipdw ); 
	dipdw.diph.dwHeaderSize	= sizeof( dipdw.diph ); 
	dipdw.diph.dwObj		= 0;
	dipdw.diph.dwHow		= DIPH_DEVICE;
   	dipdw.dwData			= kDIGamepadDataBufferSize;
	if( FAILED(aGamepad.device->SetProperty(
			DIPROP_BUFFERSIZE, &dipdw.diph)) )
	{
		releaseGamepad(theGamepadID);
		gamepadDebugPrint(
			"Failed to set up data buffer size for device %s\n",
			aGamepad.name.c_str());
		return eResult_Fail;
	}
 
	// Aqcuire gamepad (don't worry about failing here - will try again later)
	if( SUCCEEDED(aGamepad.device->Acquire()) )
	{
		aGamepad.wasConnected = true;
		gamepadDebugPrint(
			"Successfully acquired gamepad %s\n",
			aGamepad.name.c_str());
	}
	
	// Get initial gamepad values
	pollGamepad(theGamepadID);
	aGamepad.lastUpdateTime = gAppRunTime;

	return eResult_Ok;
}


static BOOL CALLBACK enumDevicesCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID)
{
	if( sGamepadData.hDirectInput8 == NULL )
		return DIENUM_STOP;
	
	addGamepad(lpddi);

	return
		(sGamepadData.deviceCountForDInput >= kMaxGamepadsEnumerated) ?
			DIENUM_STOP :
			DIENUM_CONTINUE;
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void init()
{
	DBG_ASSERT(!sGamepadData.initialized);

	sGamepadData.initialized = true;
	sGamepadData.selectedGamepadID = -1;

	// Initialise DirectInput and enumerate gamepads
	HRESULT hr = DirectInput8Create(
		GetModuleHandle(NULL), DIRECTINPUT_VERSION, 
			IID_IDirectInput8,
			(void**)&sGamepadData.hDirectInput8,
			NULL); 

	if( FAILED(hr) || sGamepadData.hDirectInput8 == NULL )
	{ 
		// DirectInput not available
		sGamepadData.hDirectInput8 = NULL;
		gamepadDebugPrint("Failed to initialize DirectInput 8!\n");
	}
	else
	{
		gamepadDebugPrint("DirectInput 8 initialized!\n");

		// Enumerate gamepads	
		hr = sGamepadData.hDirectInput8->EnumDevices(
			DI8DEVCLASS_GAMECTRL,
			enumDevicesCallback,
			NULL, DIEDFL_ATTACHEDONLY);
		if( FAILED(hr) )
			gamepadDebugPrint("Error enumerating gamepads!\n");
	}

	if( sGamepadData.deviceCountForDInput == 0 )
		gamepadDebugPrint("No DirectInput controllers found!\n");

	if( sGamepadData.deviceCountForXInput < XUSER_MAX_COUNT )
	{
		// Check for XInput controllers that don't have a DInput device
		// associated with them, which can happen if user disables the
		// Game Controller entry in Device Manager, and may possibly happen
		// in future versions of Windows that don't support DirectInput
		for(int i = 0; i < XUSER_MAX_COUNT; ++i)
		{
			const int aNewGamepadID = sGamepadData.deviceCountForDInput;
			sGamepadData.gamepad[aNewGamepadID].xInputID = nextFreeXInputID();
			if( sGamepadData.gamepad[aNewGamepadID].xInputID > 0 )
			{
				// Successfully identified this XInput controller
				sGamepadData.gamepadGUID[aNewGamepadID] = new DIDEVICEINSTANCE;
				addXInputGamepad(aNewGamepadID);
				++sGamepadData.deviceCountForDInput;
			}
		}
	}

	// If only found 1 valid gamepad, select it automatically
	if( sGamepadData.deviceCountForDInput == 1 )
		sGamepadData.selectedGamepadID = 0;

	// Set default deadzones for digital button presses
	for(int i = 0; i < eBtn_FirstDigital; ++i)
		setDigitalDeadzone(EButton(i));
}


void checkDeviceChange()
{
	// May have inserted a new controller
	// Quick and dirty method of dealing with this is to just re-int
	gamepadDebugPrint("Reinitializing to detect connected controller\n");
	cleanup();
	init();
}


void cleanup()
{
	setVibration(0, 0);

	if( sGamepadData.hDirectInput8 != NULL )
	{
		for( int i = 0; i < kMaxGamepadsEnumerated; ++i )
		{
			releaseGamepad(i);
			delete sGamepadData.gamepadGUID[i];
			sGamepadData.gamepadGUID[i] = NULL;
		}
	}

	sGamepadData.deviceCountForDInput = 0;
	sGamepadData.hDirectInput8->Release();
	sGamepadData.hDirectInput8 = NULL;

	gamepadDebugPrint("DirectInput 8 released!\n");

	// Reset data to default values
	GamepadData aFreshData = GamepadData();
	swap(sGamepadData, aFreshData);
}


void update()
{
	DBG_ASSERT(sGamepadData.initialized);

	if( sGamepadData.disconnectDetected )
	{// Controller we were polling got disconnected - restart everything!
		gamepadDebugPrint("Restarting DInput after controller disconnect!\n");
		cleanup();
		init();
		return;
	}

	if( sGamepadData.selectedGamepadID < 0 ||
		sGamepadData.selectedGamepadID >= sGamepadData.deviceCountForDInput ||
		(sGamepadData.gamepad[sGamepadData.selectedGamepadID].device == NULL &&
		 sGamepadData.gamepad[sGamepadData.selectedGamepadID].xInputID == 0) )
	{
		// Haven't selected a gamepad yet
		// Poll all of them to see which the user is actually using, if any
		sGamepadData.selectedGamepadID = -1;
		for(int i = 0; i < sGamepadData.deviceCountForDInput; ++i )
		{
			pollGamepad(i);
			GamepadData::Gamepad& aGamepad = sGamepadData.gamepad[i];
			if( aGamepad.xInputID == 0 &&
				sGamepadData.doNotAutoSelectDInputDevices )
			{
				// Once using an XInput controller, only auto-select XInput!
				// This prevents somehow getting a single controller that is
				// reporting as separate XInput and DInput controllers
				// (despite efforts to avoid this) and getting double input.
				aGamepad.lastUpdateTime = 0;
				aGamepad.hasReceivedInput = false;
			}
			else
			{
				// Log if received new input for auto-select feature and
				// for hasReceivedInput (needed for analog & vibration support)
				if( aGamepad.buttonsHit.any() )
				{
					aGamepad.lastUpdateTime = gAppRunTime;
					aGamepad.hasReceivedInput = true;
					if( aGamepad.xInputID > 0 )
						sGamepadData.doNotAutoSelectDInputDevices = true;
				}
			}
		}

		if( sGamepadData.deviceCountForDInput == 1 )
		{
			sGamepadData.selectedGamepadID = 0;
		}
		else
		{
			// Attempt to auto-select a gamepad
			// Do this by detecting if player pressed something on it
			for(int i = 0; i < sGamepadData.deviceCountForDInput; ++i )
			{
				if( sGamepadData.gamepad[i].buttonsHit.any() )
				{// Gamepad received new input - should it be auto-selected?
					bool autoSelect = true;
					for(int j = 0; j < sGamepadData.deviceCountForDInput; ++j )
					{
						if( j != i &&
							sGamepadData.gamepad[j].lastUpdateTime >
								gAppRunTime - kGamepadAutoSelectTimeout )
						{
							autoSelect = false;
							break;
						}
					}
					if( autoSelect )
					{
						gamepadDebugPrint(
							"Auto-selecting %s as player controller!\n",
							sGamepadData.gamepad[i].name.c_str());
						selectGamepad(i);
						break;
					}
				}
			}
		}
	}
	else
	{// Just poll the selected gamepad
		pollGamepad(sGamepadData.selectedGamepadID);
		GamepadData::Gamepad& aGamepad =
			sGamepadData.gamepad[sGamepadData.selectedGamepadID];
		if( !aGamepad.hasReceivedInput && aGamepad.buttonsHit.any() )
			aGamepad.hasReceivedInput = true;
	}
}


EResult selectGamepad(int theGamepadID)
{
	DBG_ASSERT(sGamepadData.initialized);

	if( theGamepadID < 0 || theGamepadID >= sGamepadData.deviceCountForDInput )
	{
		sGamepadData.selectedGamepadID = -1;
		return eResult_Fail;
	}

	if( activateGamepad(theGamepadID) != eResult_Ok )
		return eResult_Fail;

	// Now that have selected a gamepad, release all other gamepads
	for(int i = 0; i < sGamepadData.deviceCountForDInput; ++i )
	{
		if( i != theGamepadID )
			releaseGamepad(i);
	}

	sGamepadData.selectedGamepadID = theGamepadID;

	return eResult_Ok;
}


void setVibration(u16 theLowMotor, u16 theHighMotor)
{
	if( !sGamepadData.initialized )
		return;

	if( sGamepadData.selectedGamepadID >= 0 &&
		sGamepadData.selectedGamepadID < sGamepadData.deviceCountForDInput &&
		sGamepadData.gamepad[sGamepadData.selectedGamepadID].xInputID > 0 &&
		sGamepadData.gamepad[sGamepadData.selectedGamepadID].hasReceivedInput )
	{
		GamepadData::Gamepad& aGamepad =
			sGamepadData.gamepad[sGamepadData.selectedGamepadID];
		const DWORD xInputID = aGamepad.xInputID - 1;
		XINPUT_VIBRATION vibration;
		ZeroMemory( &vibration, sizeof(XINPUT_VIBRATION) );
		vibration.wLeftMotorSpeed = theLowMotor;
		vibration.wRightMotorSpeed = theHighMotor;
		XInputSetState( xInputID, &vibration );
	}
}


void setDigitalDeadzone(EButton theButton, u8 theDeadzone)
{
	DBG_ASSERT(sGamepadData.initialized);

	if( (unsigned)theButton < eBtn_FirstDigital )
		sGamepadData.digitalDeadzone[theButton] = theDeadzone;
}


bool buttonHit(EButton theButton)
{
	DBG_ASSERT(sGamepadData.initialized);
	
	if( (unsigned)theButton >= eBtn_Num )
		return false;

	if( sGamepadData.selectedGamepadID < 0 ||
		sGamepadData.selectedGamepadID >= sGamepadData.deviceCountForDInput )
	{// See if button was hit on ANY gamepad
		for( int i = 0; i < sGamepadData.deviceCountForDInput; ++i )
		{
			if( sGamepadData.gamepad[i].buttonsHit.test(theButton) )
				return true;
		}
	}
	else
	{// See if button was hit on selected gamepad
		return sGamepadData.gamepad[sGamepadData.selectedGamepadID]
			.buttonsHit.test(theButton);
	}

	return false;
}


bool buttonDown(EButton theButton)
{
	DBG_ASSERT(sGamepadData.initialized);

	if( (unsigned)theButton >= eBtn_Num )
		return false;

	if( sGamepadData.selectedGamepadID < 0 ||
		sGamepadData.selectedGamepadID >= sGamepadData.deviceCountForDInput )
	{// See if button is down on ANY gamepad
		for( int i = 0; i < sGamepadData.deviceCountForDInput; ++i )
		{
			if( sGamepadData.gamepad[i].buttonsDown.test(theButton) )
				return true;
		}
	}
	else
	{// See if button is down on selected gamepad
		return sGamepadData.gamepad[sGamepadData.selectedGamepadID]
				.buttonsDown.test(theButton);
	}

	return false;
}


u8 axisVal(EAxis theAxis)
{
	DBG_ASSERT(sGamepadData.initialized);

	u8 result = 0;

	if( (unsigned)theAxis > eAxis_Num )
		return result;

	if( sGamepadData.selectedGamepadID < 0 ||
		sGamepadData.selectedGamepadID >= sGamepadData.deviceCountForDInput )
	{// Get highest axis value of ALL gamepads with this axis
		for( int i = 0; i < sGamepadData.deviceCountForDInput; ++i )
			result = max(result, sGamepadData.gamepad[i].axisVal[theAxis]);
	}
	else
	{// Get exact axis value on selected gamepad
		result = sGamepadData.gamepad[sGamepadData.selectedGamepadID]
					.axisVal[theAxis];
	}

	return result;
}


u8 buttonAnalogVal(EButton theButton)
{
	return axisVal(axisForButton(theButton));
}


EAxis axisForButton(EButton theButton)
{
	switch(theButton)
	{
	case eBtn_LSLeft:	return eAxis_LSLeft;
	case eBtn_LSDown:	return eAxis_LSDown;
	case eBtn_LSRight:	return eAxis_LSRight;
	case eBtn_LSUp:		return eAxis_LSUp;
	case eBtn_RSLeft:	return eAxis_RSLeft;
	case eBtn_RSDown:	return eAxis_RSDown;
	case eBtn_RSRight:	return eAxis_RSRight;
	case eBtn_RSUp:		return eAxis_RSUp;
	case eBtn_L2:		return eAxis_LTrigger;
	case eBtn_R2:		return eAxis_RTrigger;
	}

	return eAxis_None;
}


EButton buttonForAxis(EAxis theAxis)
{
	switch(theAxis)
	{
	case eAxis_LSLeft:		return eBtn_LSLeft;
	case eAxis_LSDown:		return eBtn_LSDown;
	case eAxis_LSRight:		return eBtn_LSRight;
	case eAxis_LSUp:		return eBtn_LSUp;
	case eAxis_RSLeft:		return eBtn_RSLeft;
	case eAxis_RSDown:		return eBtn_RSDown;
	case eAxis_RSRight:		return eBtn_RSRight;
	case eAxis_RSUp:		return eBtn_RSUp;
	case eAxis_RTrigger:	return eBtn_R2;
	case eAxis_LTrigger:	return eBtn_L2;
	}

	return eBtn_None;
}


u8 getDigitalDeadzone(EButton theButton)
{
	if( (unsigned)theButton < eBtn_FirstDigital )
		return sGamepadData.digitalDeadzone[theButton];

	return 0;
}


EButton lastButtonPressed()
{
	DBG_ASSERT(sGamepadData.initialized);

	// See if any active controller has received an input hit
	for(size_t i = 0; i < sGamepadData.deviceCountForDInput; ++i)
	{
		const int firstHit = sGamepadData.gamepad[i].buttonsHit.firstSetBit();
		if( firstHit && firstHit < sGamepadData.gamepad[i].buttonsHit.size() )
			return EButton(firstHit);
	}

	return eBtn_None;
}


int gamepadCount()
{
	DBG_ASSERT(sGamepadData.initialized);

	return sGamepadData.deviceCountForDInput;
}


bool forceFeedbackSupported(int theGamepadID)
{
	DBG_ASSERT(sGamepadData.initialized);

	if( theGamepadID < 0 || theGamepadID >= sGamepadData.deviceCountForDInput )
		return false;

	// Currently only XInput controllers support force feedback, and it's
	// assumed by the XInput drivers that ALL XInput devices support it
	if( sGamepadData.gamepad[theGamepadID].xInputID > 0 )
		return true;

	return false;
}


std::string gamepadName(int theGamepadID)
{
	DBG_ASSERT(sGamepadData.initialized);

	if( theGamepadID < 0 || theGamepadID >= sGamepadData.deviceCountForDInput )
		return "None";

	return sGamepadData.gamepad[theGamepadID].name;
}


int selectedGamepadID()
{
	return sGamepadData.initialized ? sGamepadData.selectedGamepadID : -1;
}


const char* buttonName(EButton theButton)
{
	switch(vendorID())
	{
	case eVendorID_Unknown:
		switch(theButton)
		{
		case eBtn_None:		return "<Unassigned>";
		case eBtn_LSLeft:	return "Axis X-";
		case eBtn_LSRight:	return "Axis X+";
		case eBtn_LSUp:		return "Axis Y-";
		case eBtn_LSDown:	return "Axis Y+";
		case eBtn_RSLeft:	return "Axis RX-";
		case eBtn_RSRight:	return "AXis RX+";
		case eBtn_RSUp:		return "Axis RY-";
		case eBtn_RSDown:	return "AXis RY+";
		case eBtn_DLeft:	return "D-Pad Left";
		case eBtn_DRight:	return "D-Pad Right";
		case eBtn_DUp:		return "D-Pad Up";
		case eBtn_DDown:	return "D-Pad Down";
		case eBtn_FLeft:	return "Button 1";
		case eBtn_FRight:	return "Button 2";
		case eBtn_FUp:		return "Button 3";
		case eBtn_FDown:	return "Button 4";
		case eBtn_L1:		return "Button 5";
		case eBtn_R1:		return "Button 6";
		case eBtn_L2:		return "Button 7";
		case eBtn_R2:		return "Button 8";
		case eBtn_Select:	return "Button 9";
		case eBtn_Start:	return "Button 10";
		case eBtn_L3:		return "Button 11";
		case eBtn_R3:		return "Button 12";
		case eBtn_Home:		return "Button 13";
		case eBtn_Extra:	return "Button 14";
		}
		break;
	case eVendorID_Sony:
		switch(theButton)
		{
		case eBtn_None:		return "<Unassigned>";
		case eBtn_LSLeft:	return "L-Stick Left";
		case eBtn_LSRight:	return "L-Stick Right";
		case eBtn_LSUp:		return "L-Stick Up";
		case eBtn_LSDown:	return "L-Stick Down";
		case eBtn_RSLeft:	return "R-Stick Left";
		case eBtn_RSRight:	return "R-Stick Right";
		case eBtn_RSUp:		return "R-Stick Up";
		case eBtn_RSDown:	return "R-Stick Down";
		case eBtn_DLeft:	return "D-Pad Left";
		case eBtn_DRight:	return "D-Pad Right";
		case eBtn_DUp:		return "D-Pad Up";
		case eBtn_DDown:	return "D-Pad Down";
		case eBtn_FLeft:	return "Square";
		case eBtn_FRight:	return "Circle";
		case eBtn_FUp:		return "Triangle";
		case eBtn_FDown:	return "X Button";
		case eBtn_L1:		return "L1";
		case eBtn_R1:		return "R1";
		case eBtn_L2:		return "L2";
		case eBtn_R2:		return "R2";
		case eBtn_Select:	return "Select";
		case eBtn_Start:	return "Start";
		case eBtn_L3:		return "L3";
		case eBtn_R3:		return "R3";
		case eBtn_Home:		return "Home";
		case eBtn_Extra:	return "Touchpad";
		}
		break;
	case eVendorID_Nintendo:
		switch(theButton)
		{
		case eBtn_None:		return "<Unassigned>";
		case eBtn_LSLeft:	return "L-Stick Left";
		case eBtn_LSRight:	return "L-Stick Right";
		case eBtn_LSUp:		return "L-Stick Up";
		case eBtn_LSDown:	return "L-Stick Down";
		case eBtn_RSLeft:	return "R-Stick Left";
		case eBtn_RSRight:	return "R-Stick Right";
		case eBtn_RSUp:		return "R-Stick Up";
		case eBtn_RSDown:	return "R-Stick Down";
		case eBtn_DLeft:	return "D-Pad Left";
		case eBtn_DRight:	return "D-Pad Right";
		case eBtn_DUp:		return "D-Pad Up";
		case eBtn_DDown:	return "D-Pad Down";
		case eBtn_FLeft:	return "Y Button";
		case eBtn_FRight:	return "A Button";
		case eBtn_FUp:		return "X Button";
		case eBtn_FDown:	return "B Button";
		case eBtn_L1:		return "L";
		case eBtn_R1:		return "R";
		case eBtn_L2:		return "ZL";
		case eBtn_R2:		return "ZR";
		case eBtn_Select:	return "- Button";
		case eBtn_Start:	return "+ Button";
		case eBtn_L3:		return "L-Stick Button";
		case eBtn_R3:		return "R-Stick Button";
		case eBtn_Home:		return "Home";
		case eBtn_Extra:	return "Capture";
		}
		break;
	case eVendorID_Microsoft:
		switch(theButton)
		{
		case eBtn_None:		return "<Unassigned>";
		case eBtn_LSLeft:	return "L-Stick Left";
		case eBtn_LSRight:	return "L-Stick Right";
		case eBtn_LSUp:		return "L-Stick Up";
		case eBtn_LSDown:	return "L-Stick Down";
		case eBtn_RSLeft:	return "R-Stick Left";
		case eBtn_RSRight:	return "R-Stick Right";
		case eBtn_RSUp:		return "R-Stick Up";
		case eBtn_RSDown:	return "R-Stick Down";
		case eBtn_DLeft:	return "D-Pad Left";
		case eBtn_DRight:	return "D-Pad Right";
		case eBtn_DUp:		return "D-Pad Up";
		case eBtn_DDown:	return "D-Pad Down";
		case eBtn_FLeft:	return "X Button";
		case eBtn_FRight:	return "B Button";
		case eBtn_FUp:		return "Y Button";
		case eBtn_FDown:	return "A Button";
		case eBtn_L1:		return "LB";
		case eBtn_R1:		return "RB";
		case eBtn_L2:		return "LT";
		case eBtn_R2:		return "RT";
		case eBtn_Select:	return "Back";
		case eBtn_Start:	return "Menu";
		case eBtn_L3:		return "L-Stick Button";
		case eBtn_R3:		return "R-Stick Button";
		case eBtn_Home:		return "Guide";
		}
		break;
	}
	
	return "Unknown";
}


EVendorID vendorID(int theGamepadID)
{
	if( theGamepadID < 0 )
		theGamepadID = selectedGamepadID();

	if( sGamepadData.initialized &&
		theGamepadID >= 0 &&
		theGamepadID < sGamepadData.deviceCountForDInput )
	{
		return EVendorID(sGamepadData.gamepad[theGamepadID].vendorID);
	}

	return eVendorID_Unknown;
}

#undef gamepadDebugPrint

} // Gamepad
