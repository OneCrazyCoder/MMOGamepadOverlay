//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Manages the gamepad connection, input polling, and state tracking.
	Detects and handles gamepad connection and disconnection events.
	Tracks gamepad buttons' pressed, held, and released states, as well
	as analog values for axis-based inputs like analog sticks.
*/

#include "Common.h"

namespace Gamepad
{

enum EButton
{
	eBtn_None = 0,

	// Left analog stick (pushed past set digital deadzone)
	eBtn_LSLeft,
	eBtn_LSRight,
	eBtn_LSUp,
	eBtn_LSDown,

	// Right analog stick (pushed past set digital deadzone)
	eBtn_RSLeft,
	eBtn_RSRight,
	eBtn_RSUp,
	eBtn_RSDown,

	// D-pad
	eBtn_DLeft,
	eBtn_DRight,
	eBtn_DUp,
	eBtn_DDown,

	// Face buttons
	eBtn_FLeft,		// Left face button - PS=Sqr, XB=X, N=Y
	eBtn_FRight,	// Right face button - PS=Cir, XB=B, N=A
	eBtn_FUp,		// Top face button - PS=Tri, XB=Y, N=X
	eBtn_FDown,		// Bottom face button - PS=X, XB=A, N=B

	// Shoulder buttons
	eBtn_L1,
	eBtn_R1,
	eBtn_L2,
	eBtn_R2,

	// Other buttons
	eBtn_Select,	// Or Back or Share or whatever
	eBtn_Start,		// Or Menu or Options or whatever
	eBtn_L3,		// Pressing in on the left analog stick
	eBtn_R3,		// Pressing in on the right analog stick
	eBtn_Home,		// PS button, XB Guide button, etc
	eBtn_Extra,		// Touchpad on PS, Capture on NSwitch

	eBtn_Num,
	eBtn_FirstDigital = eBtn_DLeft,
	eBtn_FirstDInputBtn = eBtn_FLeft,
	eBtn_DInputBtnNum = eBtn_Num - eBtn_FirstDInputBtn,
};

enum EAxis
{
	eAxis_None = 0,

	// Left analog stick
	eAxis_LSLeft,	// DInput X-
	eAxis_LSRight,	// DInput X+
	eAxis_LSUp,		// DInput Y-
	eAxis_LSDown,	// DInput Y+

	// Right analog stick
	eAxis_RSLeft,
	eAxis_RSRight,
	eAxis_RSUp,
	eAxis_RSDown,

	// Analog triggers (R first to match XB360 using DInput)
	eAxis_RTrigger,
	eAxis_LTrigger,

	eAxis_Num
};

enum EVendorID
{
	eVendorID_Unknown,
	eVendorID_Sony,
	eVendorID_Nintendo,
	eVendorID_Microsoft,

	eVendorID_Num
};


// Initializes gamepad system
void init();

// Should be called when get a WM_DEVICECHANGE message to check if user
// plugged in/turned on a gamepad or disconnected one.
void checkDeviceChange();

// Safely releases memory and devices and such and prepares for app shutdown
void cleanup();

// Checks game controller buffers for new input data, among other things.
// Must be called once per update for accurate data.
void update();

// Attempts to make the given gamepad ID become the active/selected gamepad.
// If ID is < 0 or >= gamepadCount() then resets to none selected (activates
// auto-select feature). Returns eResult_Failure if selected id did not
// become selected controller.
// NOTE: Not usually needed since gamepads will be automatically selected
// according to which one the user starts pressing buttons on.
EResult selectGamepad(int theGamepadID);

// Sets force feedback on compatible controller, if the controller has been
// used for input at least once before this point. 
void setVibration(u16 theLowMotor, u16 theHighMotor);
void setImpulseTriggersEnabled(bool);

// Sets the amount (0 to 255) that associated EAxis should
// be pushed in order to register as a digital EButton press.
// Only used for analog sticks (not the trigger buttons).
// Does NOT affect axisVal() or buttonAnalogVal()!
void setDigitalDeadzone(EButton theButton, u8 theDeadzone = 100);

// Returns true if given Code input trigger was hit on selected gamepad
// between most recent update() call and the update() call prior to that.
bool buttonHit(EButton theBtn);

// Returns true if given Code input on selected gamepad is active
bool buttonDown(EButton theButton);

// Returns a value between 0 and 255 for given axis.
// Return value is NOT affected by setDigitalDeadzone()!
u8 axisVal(EAxis theAxis);

// Returns axisVal() of axis associated with given button, if has one.
// Returns 0 for buttons with no analog input possible, *even when pressed*!
u8 buttonAnalogVal(EButton theButton);

// Returns the associated EAxis value for given EButton, or eBtn_None
EAxis axisForButton(EButton theButton);

// Returns the associated EButton value for given EAxis, or eAxis_None
EButton buttonForAxis(EAxis theAxis);

// Returns value previously set by setDigitalDeadzone (or default value)
u8 getDigitalDeadzone(EButton theButton);

// Used for setting custom controls, this returns the last new button
// value change by any gamepads currently active, or eBtn_None
// if none pressed since the last time update() was called.
EButton lastButtonPressed();

// How many active gamepads does the system have?
int gamepadCount();

// Returns whether or not given gamepad ID will respond to vibration requests
// -1 is the same as sending selectedGamepadID()
bool forceFeedbackSupported(int theGamepadID = -1);
bool impulseTriggersSupported(int theGamepadID = -1);

// Returns the name of a specific controller by ID number, or "None"
std::string gamepadName(int theGamepadID = -1);

// Returns the ID number of the gamepad that is being used for game input.
// Returns -1 if no gamepad is currently selected (or none were found).
int selectedGamepadID();

// Returns the name of the specified EButton
// (for use in printing in control configurations dialogs and such).
// Button names may change depending on the current selectedGamepadID.
const char* buttonName(EButton theButton);

// Returns vendor ID of controller (for button prompts, etc)
// -1 is the same as sending selectedGamepadID()
EVendorID vendorID(int theGamepadID = -1);

} // Gamepad
