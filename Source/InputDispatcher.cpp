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
	u8 mouseDPadAccel;

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
		mouseDPadAccel = max(8, Profile::getInt("Mouse/DPadAccel", 50));
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
	std::string keys;
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


	void push_back(const std::string& theKeys)
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

		mBuffer[mTail].keys = theKeys;
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
	u16 modKeysDown;
	u16 nextQueuedKey;

	DispatchTracker() :
		queuePauseTime(),
		modKeyReleaseLockTime(),
		currTaskProgress(),
		digitalMouseVel(),
		modKeysDown(),
		nextQueuedKey()
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

static EResult popNextKey(const std::string& theVKeySequence)
{
	while( sTracker.currTaskProgress < theVKeySequence.size() )
	{
		const size_t idx = sTracker.currTaskProgress++;
		u8 aVKey = theVKeySequence[idx];

		if( aVKey == VK_SHIFT )
		{
			sTracker.nextQueuedKey |= kVKeyShiftMask;
			sTracker.modKeyReleaseLockTime = kConfig.modKeyReleaseLockTime;
		}
		else if( aVKey == VK_CONTROL )
		{
			sTracker.nextQueuedKey |= kVKeyCtrlMask;
			sTracker.modKeyReleaseLockTime = kConfig.modKeyReleaseLockTime;
		}
		else if( aVKey == VK_MENU )
		{
			sTracker.nextQueuedKey |= kVKeyAltMask;
			sTracker.modKeyReleaseLockTime = kConfig.modKeyReleaseLockTime;
		}
		else
		{
			sTracker.nextQueuedKey |= aVKey;
			break;
		}
	}

	if( sTracker.currTaskProgress >= theVKeySequence.size() )
		return eResult_TaskCompleted;

	return eResult_Incomplete;
}


static EResult popNextStringChar(const std::string& theString)
{
	DBG_ASSERT(theString[0] == '/' || theString[0] == '>');

	const size_t idx = sTracker.currTaskProgress++;
	const char c =
		(idx == 0 && theString[0] == '>') || idx >= theString.size()
		? '\r'
		: theString[idx];

	// Skip non-printable or non-ASCII characters
	if( idx > 0 && idx < theString.size() && (c < ' ' || c > '~') )
		return popNextStringChar(theString);

	// Queue the key + modifiers (shift key)
	sTracker.nextQueuedKey = VkKeyScan(c);

	if( idx == 0 ) // the initial '/' or '\r' (from '>') character
	{
		// Add a pause to make sure async key checking switches to
		// direct text input in chat box before 'typing' at full speed
		sTracker.queuePauseTime =
			max(sTracker.queuePauseTime,
				kConfig.slashCommandPostFirstKeyDelay);
	}

	if( idx >= theString.size() )
		return eResult_TaskCompleted;

	return eResult_Incomplete;
}


static void setShift(bool down)
{
	const bool shiftHeld = (sTracker.modKeysDown & kVKeyShiftMask) != 0;
	if( down != shiftHeld )
	{
		sTracker.inputs.push_back(Input());
		sTracker.inputs.back().type = INPUT_KEYBOARD;
		sTracker.inputs.back().ki.wVk = VK_SHIFT;
		sTracker.inputs.back().ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
		if( down )
			sTracker.modKeysDown |= kVKeyShiftMask;
		else
			sTracker.modKeysDown &= ~kVKeyShiftMask;
	}
}


static void setCtrl(bool down)
{
	const bool ctrlHeld = (sTracker.modKeysDown & kVKeyCtrlMask) != 0;
	if( down != ctrlHeld )
	{
		sTracker.inputs.push_back(Input());
		sTracker.inputs.back().type = INPUT_KEYBOARD;
		sTracker.inputs.back().ki.wVk = VK_CONTROL;
		sTracker.inputs.back().ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
		if( down )
			sTracker.modKeysDown |= kVKeyCtrlMask;
		else
			sTracker.modKeysDown &= ~kVKeyCtrlMask;
	}
}


static void setAlt(bool down)
{
	const bool altHeld = (sTracker.modKeysDown & kVKeyAltMask) != 0;
	if( down != altHeld )
	{
		sTracker.inputs.push_back(Input());
		sTracker.inputs.back().type = INPUT_KEYBOARD;
		sTracker.inputs.back().ki.wVk = VK_MENU;
		sTracker.inputs.back().ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
		if( down )
			sTracker.modKeysDown |= kVKeyAltMask;
		else
			sTracker.modKeysDown &= ~kVKeyAltMask;
	}
}


static void sendQueuedKey()
{
	setShift((sTracker.nextQueuedKey & kVKeyShiftMask) != 0);
	setCtrl((sTracker.nextQueuedKey & kVKeyCtrlMask) != 0);
	setAlt((sTracker.nextQueuedKey & kVKeyAltMask) != 0);

	if( const u8 aVkey = sTracker.nextQueuedKey & vMkeyMask )
	{
		sTracker.inputs.push_back(Input());
		sTracker.inputs.back().type = INPUT_KEYBOARD;
		sTracker.inputs.back().ki.wVk = aVkey;

		sTracker.inputs.push_back(Input());
		sTracker.inputs.back().type = INPUT_KEYBOARD;
		sTracker.inputs.back().ki.wVk = aVkey;
		sTracker.inputs.back().ki.dwFlags = KEYEVENTF_KEYUP;
	}

	sTracker.nextQueuedKey = 0;
}


static void flushInputVector()
{
	if( !sTracker.inputs.empty() )
	{
		if( kConfig.useScanCodes )
		{// Convert virtual key codes into scan codes
			for(size_t i = 0; i < sTracker.inputs.size(); ++i)
			{
				if( sTracker.inputs[i].type == INPUT_KEYBOARD )
				{
					sTracker.inputs[i].ki.wScan =
						MapVirtualKey(sTracker.inputs[i].ki.wVk, 0);
					sTracker.inputs[i].ki.dwFlags |= KEYEVENTF_SCANCODE;
				}
			}
		}
		SendInput(
			UINT(sTracker.inputs.size()),
			static_cast<INPUT*>(&sTracker.inputs[0]),
			sizeof(INPUT));
		sTracker.inputs.clear();
	}
	#ifdef _DEBUG
	else
	{
		// Try to flush out input stuck in the message queue by sending
		// key presses of the non-existant virtual key code ID 0.
		// Most apps don't seem to need this, but Notepad in Windows 11
		// does for some reason, and I use it to test output
		Input aDummyInput[2];
		aDummyInput[0].type = INPUT_KEYBOARD;
		aDummyInput[1].type = INPUT_KEYBOARD;
		aDummyInput[1].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(2, static_cast<INPUT*>(aDummyInput), sizeof(INPUT));
	}
	#endif
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
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
	if( sTracker.queuePauseTime <= 0 &&
		sTracker.nextQueuedKey == 0 &&
		!sTracker.queue.empty() )
	{
		DispatchTask& aCurrTask = sTracker.queue.front();
		EResult aTaskResult;
		if( aCurrTask.keys[0] == '/' || aCurrTask.keys[0] == '>' )
			aTaskResult = popNextStringChar(aCurrTask.keys);
		else
			aTaskResult = popNextKey(aCurrTask.keys);
		if( aTaskResult == eResult_TaskCompleted )
		{
			sTracker.currTaskProgress = 0;
			sTracker.queue.pop_front();
			while(!sTracker.queue.empty() &&
				  sTracker.queue.front().queuedTime + kConfig.maxTaskQueuedTime < gAppRunTime)
			{// Get rid of super-old queued tasks
				sTracker.queue.pop_front();
			}
		}
	}

	// Update keyboard input
	// ---------------------
	if( sTracker.modKeyReleaseLockTime > 0 )
	{
		// Don't send the key yet if need to release modifiers first
		if( ((~sTracker.nextQueuedKey) & sTracker.modKeysDown) == 0 )
			sendQueuedKey();
	}
	else
	{
		sendQueuedKey();
	}

	// Update mouse input
	// ------------------
	sTracker.digitalMouseVel = max(0,
		sTracker.digitalMouseVel -
		kConfig.mouseDPadAccel * 3 * gAppFrameTime);

	// Dispatch input to system
	// ------------------------
	flushInputVector();
}


void cleanup()
{
	setShift(false);
	setCtrl(false);
	setAlt(false);
	flushInputVector();
}


void sendMacro(const std::string& theMacro)
{
	if( theMacro.empty() )
		return;

	sTracker.queue.push_back(theMacro);
}


void shiftMouseCursor(int dx, int dy, bool digital)
{
	// Get magnitude of desired mouse motion in 0 to 1.0f range
    double aMagnitude = std::sqrt(double(dx) * dx + dy * dy) / 255.0;

	// Apply deadzone and saturation to magnitude
	if( aMagnitude < kConfig.cursorDeadzone )
		return;
	aMagnitude -= kConfig.cursorDeadzone;
	aMagnitude = min(aMagnitude / kConfig.cursorRange, 1.0);

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
	dx = dx * kConfig.cursorXSpeed / kMouseMaxSpeed * gAppFrameTime;
	dy = dy * kConfig.cursorYSpeed / kMouseMaxSpeed * gAppFrameTime;

	// Add in previously-stored sub-pixel movement amounts
	static int sMouseXSubPixel = 0;
	static int sMouseYSubPixel = 0;
	dx += sMouseXSubPixel;
	dy += sMouseYSubPixel;

	// Convert to pixels and retain sub-pixel amounts to add in later
	// Sign of result of operator%() w/ negative dividend is
	// implementation-defined, hence the extra sign check here
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
	SendInput(1, static_cast<INPUT*>(&anInput), sizeof(INPUT));
}

} // InputDispatcher
