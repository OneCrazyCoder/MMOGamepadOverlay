//-----------------------------------------------------------------------------
//	Stub version of xinput1_3.dll that reports a single, wired 360 controller
//	that just isn't doing anything, ever. Placing the .dll in the same folder
//	as a game executable should cause the game to connect to this fake gamepad
//	and not try to fall back to other input methods (like DirectInput), yet
//	will not actually get any real gamepad input. This essentially blocks any
//	game that uses XInput 1.3 (or 1.4 - just rename the file) from responding
//	to any gamepad input.
//
//	A .def is required to make sure ordinals match the real xinput .dll's.
//-----------------------------------------------------------------------------

#include <windows.h>

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

#define XINPUT_ERROR_BAD_ARGUMENTS 0x160

// XInput structures (matches official XInput.h)
typedef struct _XINPUT_GAMEPAD {
	WORD  wButtons;
	BYTE  bLeftTrigger;
	BYTE  bRightTrigger;
	SHORT sThumbLX;
	SHORT sThumbLY;
	SHORT sThumbRX;
	SHORT sThumbRY;
} XINPUT_GAMEPAD, *PXINPUT_GAMEPAD;

typedef struct _XINPUT_STATE {
	DWORD	   dwPacketNumber;
	XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE, *PXINPUT_STATE;

typedef struct _XINPUT_VIBRATION {
	WORD wLeftMotorSpeed;
	WORD wRightMotorSpeed;
} XINPUT_VIBRATION, *PXINPUT_VIBRATION;

typedef struct _XINPUT_CAPABILITIES {
	BYTE  Type;
	BYTE  SubType;
	WORD  Flags;
	XINPUT_GAMEPAD Gamepad;
	XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES, *PXINPUT_CAPABILITIES;

typedef struct _XINPUT_BATTERY_INFORMATION {
	BYTE BatteryType;
	BYTE BatteryLevel;
} XINPUT_BATTERY_INFORMATION, *PXINPUT_BATTERY_INFORMATION;

typedef struct _XINPUT_KEYSTROKE {
	WORD  VirtualKey;
	WCHAR Unicode;
	WORD  Flags;
	BYTE  UserIndex;
	BYTE  HidCode;
} XINPUT_KEYSTROKE, *PXINPUT_KEYSTROKE;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
	return TRUE;
}


//-----------------------------------------------------------------------------
// DLL Functions
//-----------------------------------------------------------------------------

extern "C" DWORD WINAPI XInputGetState(
	DWORD dwUserIndex, PXINPUT_STATE pState)
{
	if( !pState )
		return XINPUT_ERROR_BAD_ARGUMENTS;

	// Treat all but 0th controller as disconnected
	if( dwUserIndex != 0 )
		return ERROR_DEVICE_NOT_CONNECTED;

	// Return empty state (nothing pressed) every time
	ZeroMemory(pState, sizeof(XINPUT_STATE));
	pState->dwPacketNumber = 1; // no change means no increase in packet number
	return ERROR_SUCCESS;
}


extern "C" DWORD WINAPI XInputSetState(
	DWORD dwUserIndex, PXINPUT_VIBRATION pVibration)
{
	if( !pVibration )
		return XINPUT_ERROR_BAD_ARGUMENTS;

	// Pretend set vibration as requested
	return (dwUserIndex == 0) ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}


extern "C" DWORD WINAPI XInputGetCapabilities(
	DWORD dwUserIndex, DWORD flags, PXINPUT_CAPABILITIES pCapabilities)
{
	if( !pCapabilities )
		return XINPUT_ERROR_BAD_ARGUMENTS;
	
	// Treat all but 0th controller as disconnected
	if( dwUserIndex != 0 )
		return ERROR_DEVICE_NOT_CONNECTED;

	// Report wired 360 controller
	ZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));
	pCapabilities->Type = 0x01;
	pCapabilities->SubType = 0x01;
	return ERROR_SUCCESS;
}


extern "C" void WINAPI XInputEnable(BOOL enable)
{
	// Do nothing (just like actual XInput for this function)
}


extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(
	DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid)
{
	if( !pDSoundRenderGuid || !pDSoundCaptureGuid )
		return XINPUT_ERROR_BAD_ARGUMENTS;
	
	// Treat all but 0th controller as disconnected
	if( dwUserIndex != 0 )
		return ERROR_DEVICE_NOT_CONNECTED;

	// Report no sound support
	ZeroMemory(pDSoundRenderGuid, sizeof(GUID));
	ZeroMemory(pDSoundCaptureGuid, sizeof(GUID));
	return ERROR_SUCCESS;
}


extern "C" DWORD WINAPI XInputGetBatteryInformation(
	DWORD dwUserIndex, BYTE devType, PXINPUT_BATTERY_INFORMATION pBatteryInfo)
{
	if( !pBatteryInfo ) return
		XINPUT_ERROR_BAD_ARGUMENTS;

	// Treat all but 0th controller as disconnected
	if( dwUserIndex != 0 )
		return ERROR_DEVICE_NOT_CONNECTED;

	// Report a wired controller - no battery info
	pBatteryInfo->BatteryType = 0x01;
	pBatteryInfo->BatteryLevel = 0x00;
	return ERROR_SUCCESS;
}


extern "C" DWORD WINAPI XInputGetKeystroke(
	DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke)
{
	if( !pKeystroke )
		return XINPUT_ERROR_BAD_ARGUMENTS;

	return (dwUserIndex == 0) ? ERROR_EMPTY : ERROR_DEVICE_NOT_CONNECTED;
}
