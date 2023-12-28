//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputDispatcher.h"

#include "Profile.h"

namespace InputDispatcher
{

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kVKeyShiftMask = 0x0100, // from MS docs for VkKeyScan()
kVKeyCtrlMask = 0x0200,
kVKeyAltMask = 0x0400,
vKeyModMask = 0xFF00,
vMkeyMask = 0x00FF,
kMouseMaxSpeed = 256,
kMouseToPixelDivisor = 8192,
kMouseMaxDigitalVel = 32768,
};


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	int maxTaskQueuedTime; // tasks older than this in queue are skipped
	int slashCommandPostFirstKeyDelay;
	int modKeyReleaseLockTime;
	double cursorDeadzone;
	double cursorRange;
	int cursorXSpeed;
	int cursorYSpeed;
	double mouseLookDeadzone;
	double mouseLookRange;
	int mouseLookXSpeed;
	int mouseLookYSpeed;
	u8 mouseDPadAccel;
	double mouseWheelDeadzone;
	double mouseWheelRange;
	int mouseWheelSpeed;

	bool useScanCodes;

	void load()
	{
		maxTaskQueuedTime = Profile::getInt("System/MaxKeyQueueTime", 1000);
		slashCommandPostFirstKeyDelay = Profile::getInt("System/PostSlashKeyDelay", 0);
		modKeyReleaseLockTime = Profile::getInt("System/MinModKeyHoldTime", 0);
		useScanCodes = Profile::getBool("System/UseScanCodes", false);
		cursorXSpeed = cursorYSpeed = Profile::getInt("Mouse/CursorSpeed", 100);
		cursorXSpeed = Profile::getInt("Mouse/CursorXSpeed", cursorXSpeed);
		cursorYSpeed = Profile::getInt("Mouse/CursorYSpeed", cursorYSpeed);
		cursorDeadzone = clamp(Profile::getInt("Mouse/CursorDeadzone", 25), 0, 100) / 100.0;
		cursorRange = clamp(Profile::getInt("Mouse/CursorSaturation", 100), cursorDeadzone, 100) / 100.0;
		cursorRange = max(0, cursorRange - cursorDeadzone);
		mouseLookXSpeed = mouseLookYSpeed = Profile::getInt("Mouse/MouseLookSpeed", 100);
		mouseLookXSpeed = Profile::getInt("Mouse/MouseLookXSpeed", mouseLookXSpeed);
		mouseLookYSpeed = Profile::getInt("Mouse/MouseLookYSpeed", mouseLookYSpeed);
		mouseLookDeadzone = clamp(Profile::getInt("Mouse/MouseLookDeadzone", 25), 0, 100) / 100.0;
		mouseLookRange = clamp(Profile::getInt("Mouse/MouseLookSaturation", 100), mouseLookDeadzone, 100) / 100.0;
		mouseLookRange = max(0, mouseLookRange - mouseLookDeadzone);
		mouseDPadAccel = max(8, Profile::getInt("Mouse/DPadAccel", 50));
		mouseWheelDeadzone = clamp(Profile::getInt("Mouse/WheelDeadzone", 25), 0, 100) / 100.0;
		mouseWheelRange = clamp(Profile::getInt("Mouse/WheelSaturation", 100), cursorDeadzone, 100) / 100.0;
		mouseWheelRange = max(0, mouseWheelRange - mouseWheelDeadzone);
		mouseWheelSpeed = Profile::getInt("Mouse/WheelSpeed", 255);
	}
};


//-----------------------------------------------------------------------------
// Input (derived version of WinAPI INPUT that just zero-initializes it)
//-----------------------------------------------------------------------------

struct Input : public INPUT
{ Input() { ZeroMemory(this, sizeof(INPUT)); } };


//-----------------------------------------------------------------------------
// DispatchTask
//-----------------------------------------------------------------------------

struct DispatchTask
{
	Command cmd;
	u32 queuedTime;
};


//-----------------------------------------------------------------------------
// DispatchQueue - auto-capacity-exanding circular buffer
//-----------------------------------------------------------------------------

class DispatchQueue
{
public:
	DispatchQueue()
	{
		// Initial capacity must be a power of 2!
		mBuffer.resize(4);
		mHead = 0;
		mTail = 0;
	}


	void push_back(const Command& theCommand)
	{
		if( ((mTail + 1) & (mBuffer.size() - 1)) == mHead )
		{// Buffer is full, resize by doubling to keep power-of-2 size
			std::vector<DispatchTask> newBuffer(mBuffer.size() * 2);

			for(std::size_t i = 0; i < mBuffer.size(); ++i)
				newBuffer[i] = mBuffer[(mHead + i) & (mBuffer.size() - 1)];

			mHead = 0;
			mTail = mBuffer.size();
			swap(mBuffer, newBuffer);
		}

		mBuffer[mTail].cmd = theCommand;
		mBuffer[mTail].queuedTime = gAppRunTime;
		mTail = (mTail + 1) & (mBuffer.size() - 1);
	}

	
	void pop_front()
	{
		DBG_ASSERT(!empty());
	
		mHead = (mHead + 1) & (mBuffer.size() - 1);
	}


	DispatchTask front()
	{
		DBG_ASSERT(!empty());
		
		return mBuffer[mHead];
	}


	bool empty() const
	{
		return mHead == mTail;
	}


private:
	std::vector<DispatchTask> mBuffer;
	std::size_t mHead;
	std::size_t mTail;
};


//-----------------------------------------------------------------------------
// DispatchTracker
//-----------------------------------------------------------------------------

struct DispatchTracker
{
	DispatchQueue queue;
	std::vector<Input> inputs;
	int queuePauseTime;
	int modKeyReleaseLockTime;
	int digitalMouseVel;
	size_t currTaskProgress;
	BitArray<0xFF> keysHeldDown;
	BitArray<0xFF> keysWantDown;
	u16 nextQueuedKeyTap;
	bool mouseLookActive;

	DispatchTracker() :
		queuePauseTime(),
		modKeyReleaseLockTime(),
		currTaskProgress(),
		digitalMouseVel(),
		keysHeldDown(),
		nextQueuedKeyTap(),
		mouseLookActive(false)
	{}
};


//-----------------------------------------------------------------------------
// Static variables
//-----------------------------------------------------------------------------

static Config kConfig;
static DispatchTracker sTracker;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static EResult popNextKey(const char* theVKeySequence)
{
	while( theVKeySequence[sTracker.currTaskProgress] != '\0' )
	{
		const size_t idx = sTracker.currTaskProgress++;
		u8 aVKey = theVKeySequence[idx];

		if( aVKey == VK_SHIFT )
		{
			sTracker.nextQueuedKeyTap |= kVKeyShiftMask;
			sTracker.modKeyReleaseLockTime = kConfig.modKeyReleaseLockTime;
		}
		else if( aVKey == VK_CONTROL )
		{
			sTracker.nextQueuedKeyTap |= kVKeyCtrlMask;
			sTracker.modKeyReleaseLockTime = kConfig.modKeyReleaseLockTime;
		}
		else if( aVKey == VK_MENU )
		{
			sTracker.nextQueuedKeyTap |= kVKeyAltMask;
			sTracker.modKeyReleaseLockTime = kConfig.modKeyReleaseLockTime;
		}
		else
		{
			sTracker.nextQueuedKeyTap |= aVKey;
			break;
		}
	}

	if( theVKeySequence[sTracker.currTaskProgress] == '\0' )
		return eResult_TaskCompleted;

	return eResult_Incomplete;
}


static EResult popNextStringChar(const char* theString)
{
	// Strings should start with '/' or '>'
	DBG_ASSERT(theString && (theString[0] == '/' || theString[0] == '>'));
	DBG_ASSERT(sTracker.currTaskProgress <= strlen(theString));

	const size_t idx = sTracker.currTaskProgress++;
	// End with carriage return, and start with it for say strings
	const char c =
		(idx == 0 && theString[0] == '>') || theString[idx] == '\0'
			? '\r'
			: theString[idx];

	// Skip non-printable or non-ASCII characters
	if( idx > 0 && theString[idx] != '\0' && (c < ' ' || c > '~') )
		return popNextStringChar(theString);

	// Queue the key + modifiers (shift key)
	sTracker.nextQueuedKeyTap = VkKeyScan(c);

	if( idx == 0 ) // the initial key to switch to chat bar (/ or Enter)
	{
		// Add a pause to make sure async key checking switches to
		// direct text input in chat box before 'typing' at full speed
		sTracker.queuePauseTime =
			max(sTracker.queuePauseTime,
				kConfig.slashCommandPostFirstKeyDelay);
	}

	if( theString[idx] == '\0' )
		return eResult_TaskCompleted;

	return eResult_Incomplete;
}


static bool isSafeAsyncKey(u8 theVKey)
{
	// These are keys that can be pressed while typing
	// in a macro into the chat box without interfering
	// with the macro or being interfered with by it.
	// This may need to be moved into use configuration if
	// it differs by target application.
	if( sTracker.keysHeldDown.test(VK_SHIFT) ||
		sTracker.keysHeldDown.test(VK_CONTROL) ||
		sTracker.keysHeldDown.test(VK_MENU) )
		return false;

	if( theVKey == VK_LBUTTON && sTracker.keysHeldDown.test(VK_RBUTTON) )
		return true;

	if( theVKey == VK_MBUTTON )
		return true;

	if( theVKey >= VK_PRIOR && theVKey <= VK_INSERT )
		return true;

	if( theVKey >= VK_NUMPAD0 && theVKey <= VK_NUMPAD9 )
		return true;

	if( theVKey >= VK_F1 && theVKey <= VK_F12 )
		return true;

	return false;
}


static EResult setKeyDown(u8 theKey, bool down)
{
	if( !down && sTracker.modKeyReleaseLockTime > 0 &&
		(theKey == VK_SHIFT || theKey == VK_CONTROL || theKey == VK_MENU) )
	{// Not allowed to release modifier keys yet
		return eResult_NotAllowed;
	}

	if( down != sTracker.keysHeldDown.test(theKey) )
	{
		Input anInput;
		switch(theKey)
		{
		case VK_LBUTTON:
			anInput.type = INPUT_MOUSE;
			anInput.mi.dwFlags = down
				? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
			break;
		case VK_RBUTTON:
			anInput.type = INPUT_MOUSE;
			anInput.mi.dwFlags = down
				? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
			break;
		case VK_MBUTTON:
			anInput.type = INPUT_MOUSE;
			anInput.mi.dwFlags = down
				? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
			break;
		default:
			anInput.type = INPUT_KEYBOARD;
			anInput.ki.wVk = theKey;
			anInput.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
			break;
		}
		sTracker.inputs.push_back(anInput);
		sTracker.keysHeldDown.set(theKey, down);
	}

	return eResult_Ok;
}


static void sendQueuedKeyTap()
{
	const bool shiftDown = sTracker.keysHeldDown.test(VK_SHIFT);
	const bool ctrlDown = sTracker.keysHeldDown.test(VK_CONTROL);
	const bool altDown = sTracker.keysHeldDown.test(VK_MENU);
	const bool wantShift = (sTracker.nextQueuedKeyTap & kVKeyShiftMask) != 0;
	const bool wantCtrl = (sTracker.nextQueuedKeyTap & kVKeyCtrlMask) != 0;
	const bool wantAlt = (sTracker.nextQueuedKeyTap & kVKeyAltMask) != 0;
	if( shiftDown != wantShift || ctrlDown != wantCtrl || altDown != wantAlt )
		return;

	if( const u8 aVkey = sTracker.nextQueuedKeyTap & vMkeyMask )
	{
		Input anInput;
		anInput.type = INPUT_KEYBOARD;
		anInput.ki.wVk = aVkey;
		sTracker.inputs.push_back(anInput);

		anInput.ki.dwFlags = KEYEVENTF_KEYUP;
		sTracker.inputs.push_back(anInput);
	}

	sTracker.nextQueuedKeyTap = 0;
}


static void releaseAllHeldKeys()
{
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		Input anInput;
		anInput.type = INPUT_KEYBOARD;
		anInput.ki.wVk = aVKey;
		anInput.ki.dwFlags = KEYEVENTF_KEYUP;
		sTracker.inputs.push_back(anInput);
	}
	sTracker.keysHeldDown.reset();
}


static void flushInputVector()
{
	if( !sTracker.inputs.empty() )
	{
		if( kConfig.useScanCodes )
		{// Convert Virtual-Key Codes into scan codes
			for(size_t i = 0; i < sTracker.inputs.size(); ++i)
			{
				if( sTracker.inputs[i].type == INPUT_KEYBOARD )
				{
					sTracker.inputs[i].ki.wScan = 
						MapVirtualKey(sTracker.inputs[i].ki.wVk, 0);
					// Using this method for compatibility with older Windows
					switch(sTracker.inputs[i].ki.wVk)
					{
					case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
					case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
					case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
					case VK_NUMLOCK:
						sTracker.inputs[i].ki.wScan |= 0xE000;
						sTracker.inputs[i].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
					}
					sTracker.inputs[i].ki.dwFlags |= KEYEVENTF_SCANCODE;
					sTracker.inputs[i].ki.wVk = 0;
				}
			}
		}
		SendInput(
			UINT(sTracker.inputs.size()),
			static_cast<INPUT*>(&sTracker.inputs[0]),
			sizeof(INPUT));
		sTracker.inputs.clear();
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
}


void cleanup()
{
	releaseAllHeldKeys();
	flushInputVector();
}


void update()
{
	// Update timers
	// -------------
	if( sTracker.queuePauseTime > 0 )
		sTracker.queuePauseTime -= gAppFrameTime;
	if( sTracker.modKeyReleaseLockTime > 0 )
		sTracker.modKeyReleaseLockTime -= gAppFrameTime;


	// Update queue
	// ------------
	while(sTracker.queuePauseTime <= 0 &&
		  sTracker.nextQueuedKeyTap == 0 &&
		  !sTracker.queue.empty() )
	{
		DispatchTask& aCurrTask = sTracker.queue.front();
		const bool taskIsPastDue =
			sTracker.currTaskProgress == 0 &&
			(sTracker.queue.front().queuedTime
				+ kConfig.maxTaskQueuedTime) < gAppRunTime;

		EResult aTaskResult;
		switch(aCurrTask.cmd.type)
		{
		case eCmdType_SlashCommand:
		case eCmdType_SayString:
			if( taskIsPastDue )
				aTaskResult = eResult_TaskCompleted;
			else
				aTaskResult = popNextStringChar(aCurrTask.cmd.string);
			break;
		case eCmdType_PressAndHoldKey:
			sTracker.keysWantDown.set(aCurrTask.cmd.data);
			aTaskResult = eResult_TaskCompleted;
			break;
		case eCmdType_ReleaseKey:
			if( !taskIsPastDue )
			{// Make sure not to skip a tap before next press
				if( sTracker.keysWantDown.test(aCurrTask.cmd.data) )
					setKeyDown(aCurrTask.cmd.data, true);
				if( setKeyDown(aCurrTask.cmd.data, false) != eResult_Ok )
					sTracker.queuePauseTime = 1;
			}
			sTracker.keysWantDown.reset(aCurrTask.cmd.data);
			aTaskResult = eResult_TaskCompleted;
			break;
		case eCmdType_VKeySequence:
			if( taskIsPastDue )
				aTaskResult = eResult_TaskCompleted;
			else
				aTaskResult = popNextKey(aCurrTask.cmd.string);
			break;
		}
		if( aTaskResult == eResult_TaskCompleted )
		{
			sTracker.currTaskProgress = 0;
			sTracker.queue.pop_front();
		}
	}


	// Update special-case keys
	// ------------------------
	const bool wantShift = sTracker.keysWantDown.test(VK_SHIFT);
	const bool wantCtrl = sTracker.keysWantDown.test(VK_CONTROL);
	const bool wantAlt = sTracker.keysWantDown.test(VK_MENU);
	const bool wantRMB = sTracker.keysWantDown.test(VK_RBUTTON);
	if( sTracker.nextQueuedKeyTap )
	{// Modifier keys must match those desired by queued key tap
		sTracker.keysWantDown.set(VK_SHIFT,
			(sTracker.nextQueuedKeyTap & kVKeyShiftMask) != 0);
		sTracker.keysWantDown.set(VK_CONTROL,
			(sTracker.nextQueuedKeyTap & kVKeyCtrlMask) != 0);
		sTracker.keysWantDown.set(VK_MENU,
			(sTracker.nextQueuedKeyTap & kVKeyAltMask) != 0);
	}
	// Right mouse button must stay active while mouse-look is
	if( sTracker.mouseLookActive )
		sTracker.keysWantDown.set(VK_RBUTTON);


	// Sync keys held to wanted keys held
	// ----------------------------------
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		if( !sTracker.keysWantDown.test(aVKey) )
			setKeyDown(u8(aVKey), false);
	}
	for(int aVKey = sTracker.keysWantDown.firstSetBit();
		aVKey < sTracker.keysWantDown.size();
		aVKey = sTracker.keysWantDown.nextSetBit(aVKey+1))
	{
		if( !sTracker.keysHeldDown.test(aVKey) )
			setKeyDown(u8(aVKey), true);
	}


	// Send queued key tap for key sequences / strings
	// -----------------------------------------------
	if( sTracker.nextQueuedKeyTap )
		sendQueuedKeyTap();


	// Restore status of special-case held keys
	// ----------------------------------------
	sTracker.keysWantDown.set(VK_SHIFT, wantShift);
	sTracker.keysWantDown.set(VK_CONTROL, wantCtrl);
	sTracker.keysWantDown.set(VK_MENU, wantAlt);
	sTracker.keysWantDown.set(VK_RBUTTON, wantRMB);


	// Update mouse input
	// ------------------
	sTracker.digitalMouseVel = max(0,
		sTracker.digitalMouseVel -
		kConfig.mouseDPadAccel * 3 * gAppFrameTime);


	// Dispatch input to system
	// ------------------------
	flushInputVector();
}


void sendKeyCommand(const Command& theCommand)
{
	switch(theCommand.type)
	{
	case eCmdType_Empty:
		// Do nothing, but don't assert either
		break;
	case eCmdType_PressAndHoldKey:
		if( isSafeAsyncKey(theCommand.data) &&
			setKeyDown(theCommand.data, true) == eResult_Ok )
		{// Don't need to queue this key
			sTracker.keysWantDown.set(theCommand.data);
			break;
		}
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_ReleaseKey:
		if( sTracker.keysHeldDown.test(theCommand.data) &&
			sTracker.keysWantDown.test(theCommand.data) &&
			setKeyDown(theCommand.data, false) == eResult_Ok )
		{// Safely released key right away, no need to queue it
			sTracker.keysWantDown.reset(theCommand.data);
			break;
		}
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_VKeySequence:
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		sTracker.queue.push_back(theCommand);
		break;
	default:
		DBG_ASSERT(false && "Invalid command type for sendkeyCommand()!");
	}
}


void moveMouse(int dx, int dy, bool digital)
{
	const bool kMouseLookSpeed =
		sTracker.mouseLookActive ||
		sTracker.keysHeldDown.test(VK_RBUTTON);

	// Get magnitude of desired mouse motion in 0 to 1.0f range
    double aMagnitude = std::sqrt(double(dx) * dx + dy * dy) / 255.0;

	// Apply deadzone and saturation to magnitude
	const double kDeadZone = kMouseLookSpeed
		? kConfig.mouseLookDeadzone : kConfig.cursorDeadzone;
	if( aMagnitude < kDeadZone )
		return;
	aMagnitude -= kDeadZone;
	const double kRange = kMouseLookSpeed
		? kConfig.mouseLookRange : kConfig.cursorRange;
	aMagnitude = min(aMagnitude / kRange, 1.0);

	// Apply adjustments to allow for low-speed fine control
	if( digital )
	{// Apply acceleration to magnitude
		sTracker.digitalMouseVel = min(
			kMouseMaxDigitalVel,
			sTracker.digitalMouseVel +
				kConfig.mouseDPadAccel * 4 * gAppFrameTime);
		aMagnitude *= double(sTracker.digitalMouseVel) / kMouseMaxDigitalVel;
	}
	else
	{// Apply exponential easing curve to magnitude
		aMagnitude = std::pow(2, 10 * (aMagnitude - 1));
	}

	// Get angle of desired mouse motion
	const double anAngle = atan2(double(dy), double(dx));

	// Convert back into integer dx & dy w/ 32,768 range
	dx = 32768.0 * aMagnitude * cos(anAngle);
	dy = 32768.0 * aMagnitude * sin(anAngle);

	// Apply speed setting
	const int kCursorXSpeed = kMouseLookSpeed
		? kConfig.mouseLookXSpeed : kConfig.cursorXSpeed;
	dx = dx * kCursorXSpeed / kMouseMaxSpeed * gAppFrameTime;
	const int kCursorYSpeed = kMouseLookSpeed
		? kConfig.mouseLookYSpeed : kConfig.cursorYSpeed;
	dy = dy * kCursorYSpeed / kMouseMaxSpeed * gAppFrameTime;

	// Add in previously-stored sub-pixel movement amounts
	static int sMouseXSubPixel = 0;
	static int sMouseYSubPixel = 0;
	dx += sMouseXSubPixel;
	dy += sMouseYSubPixel;

	// Convert to pixels and retain sub-pixel amounts to add in later
	// Sign of result of operator%() w/ negative dividend may
	// differ by compiler, hence the extra sign check here
	if( dx < 0 )
		sMouseXSubPixel = -((-dx) % kMouseToPixelDivisor);
	else
		sMouseXSubPixel = dx % kMouseToPixelDivisor;
	dx = dx / kMouseToPixelDivisor;

	if( dy < 0 )
		sMouseYSubPixel = -((-dy) % kMouseToPixelDivisor);
	else
		sMouseYSubPixel = dy % kMouseToPixelDivisor;
	dy = dy / kMouseToPixelDivisor;

	// Send the mouse movement to the OS
	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.dx = dx;
	anInput.mi.dy = dy;
	anInput.mi.dwFlags = MOUSEEVENTF_MOVE;
	sTracker.inputs.push_back(anInput);
}


void setMouseLookMode(bool active)
{
	sTracker.mouseLookActive = active;
	if( active && !sTracker.keysHeldDown.test(VK_RBUTTON) )
	{
		// Jump to center screen first, then begin holding right-click
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.dx = 32768;
		anInput.mi.dy = 32768;
		anInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		sTracker.inputs.push_back(anInput);
		setKeyDown(VK_RBUTTON, true);
	}
}


bool isInMouseLookMode()
{
	return sTracker.mouseLookActive;
}


void scrollMouseWheel(int dy, bool digital, bool stepped)
{
	// Get magnitude of desired mouse motion in 0 to 1.0f range
	double aMagnitude = abs(dy) / 255.0;

	// Apply deadzone and saturation to dy
	if( aMagnitude < kConfig.mouseWheelDeadzone )
		return;
	aMagnitude -= kConfig.mouseWheelDeadzone;
	aMagnitude = min(aMagnitude / kConfig.mouseWheelRange, 1.0);

	// Apply adjustments to allow for low-speed fine control
	if( digital )
	{// Apply acceleration to magnitude
		sTracker.digitalMouseVel = min(
			kMouseMaxDigitalVel,
			sTracker.digitalMouseVel +
				kConfig.mouseDPadAccel * 4 * gAppFrameTime);
		aMagnitude *= double(sTracker.digitalMouseVel) / kMouseMaxDigitalVel;
	}
	else
	{// Apply exponential easing curve to magnitude
		aMagnitude = std::pow(2, 10 * (aMagnitude - 1));
	}

	// Restrict to increments of WHEEL_DELTA when stepped == true
	if( stepped )
		aMagnitude /= WHEEL_DELTA;

	// Convert back into integer dy w/ 32,768 range
	dy = dy < 0 ? (-32768.0 * aMagnitude) : (32768.0 * aMagnitude);

	// Apply speed setting
	dy = dy * kConfig.mouseWheelSpeed / kMouseMaxSpeed * gAppFrameTime;

	// Use same logic as shiftMouseCursor() for fractional speeds
	// Especially important when restricting to WHEEL_DATA increments
	static int sMouseWheelSubPixel = 0;
	dy += sMouseWheelSubPixel;
	if( dy < 0 )
		sMouseWheelSubPixel = -((-dy) % kMouseToPixelDivisor);
	else
		sMouseWheelSubPixel = dy % kMouseToPixelDivisor;
	dy = dy / kMouseToPixelDivisor;

	// Counter above division by WHEEL_DELTA for true speed,
	// but now resulting value will be a multiple of WHEEL_DATA
	if( stepped )
		dy *= WHEEL_DELTA;

	// Send the mouse wheel movement to the OS
	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.mouseData = DWORD(-dy);
	anInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
	sTracker.inputs.push_back(anInput);
}

} // InputDispatcher
