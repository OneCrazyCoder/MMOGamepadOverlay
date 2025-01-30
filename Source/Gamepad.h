//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Manages the gamepad connection, input polling, and state tracking.
	Detects and handles gamepad connection and disconnection events.
	Tracks gamepad buttons' press and held states, as well as analog
	values for axis-based inputs like analog sticks and triggers buttons.
*/

#include "Common.h"

namespace Gamepad
{

enum EAxis
{
	eAxis_None = 0,

	// Left analog stick
	eAxis_LSLeft,	// DInput X-
	eAxis_LSRight,	// DInput X+
	eAxis_LSUp,		// DInput Y-
	eAxis_LSDown,	// DInput Y+
	eAxis_LSAny,	// Magnitude of combo of all of above (0 when centered)

	// Right analog stick
	eAxis_RSLeft,
	eAxis_RSRight,
	eAxis_RSUp,
	eAxis_RSDown,
	eAxis_RSAny,

	// Analog triggers
	eAxis_LTrig,
	eAxis_RTrig,

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
void init(bool restartAfterDisconnect = false);

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

// Sets the amount (0 to 255) that associated button must be pushed in order
// to register as a digital EButton press. Means nothing for non-axis buttons,
// and does NOT affect axisVal() or buttonAnalogVal()!
void setPressThreshold(EButton theButton, u8 theThreshold);

// Forces button to be considered released until next time it is pushed,
// even if it is currently actually down. Doesn't affect axisVal() though.
void ignoreUntilPressedAgain(EButton theButton);

// Returns true if given button was newly pressed on any active gamepad
// Only checks selected gamepad if one is selected (auto or otherwise)
bool buttonHit(EButton theButton);

// Returns true if given button is held down on any active gamepad
// Only checks selected gamepad if one is selected (auto or otherwise)
bool buttonDown(EButton theButton);

// Returns a value between 0 and 255 for how much given axis is moved.
// Return value is NOT affected by setPressThreshold()!
u8 axisVal(EAxis theAxis);

// Returns axisVal() of axis associated with given button, if has one.
// Returns 0 for buttons with no analog input possible, *even when pressed*!
u8 buttonAnalogVal(EButton theButton);

// Returns the associated EAxis value for given EButton, or eAxis_None
EAxis axisForButton(EButton theButton);

// Returns the associated EButton value for given EAxis, or eBtn_None
EButton buttonForAxis(EAxis theAxis);

// Returns value previously set by setPressThreshold (or default value)
u8 getPressThreshhold(EButton theButton);

// Used for setting custom controls, this returns the last new button
// value change by any gamepads currently active, or eBtn_None
// if none pressed since the last time update() was called.
EButton lastButtonPressed();

// How many active gamepads does the system have?
int gamepadCount();

// Returns whether or not given gamepad ID will respond to vibration requests
// -1 is the same as sending selectedGamepadID()
bool forceFeedbackSupported(int theGamepadID = -1);

// Returns the name of a specific controller by ID number, or "None"
// -1 is the same as sending selectedGamepadID()
std::string gamepadName(int theGamepadID = -1);

// Returns the ID number of the gamepad that is being used for game input.
// Returns -1 if no gamepad is currently selected (or none were found).
int selectedGamepadID();

// Returns the name of the specified EButton to display to user
// (for use in printing in control configurations dialogs and such).
// Button names may change depending on the current selectedGamepadID.
const char* buttonName(EButton theButton);

// Returns vendor ID of controller (for button prompts, etc)
// -1 is the same as sending selectedGamepadID()
EVendorID vendorID(int theGamepadID = -1);

} // Gamepad
