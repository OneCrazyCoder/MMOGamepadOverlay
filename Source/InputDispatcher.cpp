//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputDispatcher.h"

#include "Lookup.h"
#include "Profile.h"

namespace InputDispatcher
{

// Uncomment this to print out SendInput events to debug window
//#define INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT

// Uncomment this to stop sending actual input (can still print via above)
//#define INPUT_DISPATCHER_SIMULATION_ONLY


//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kMouseMaxSpeed = 256,
kMouseToPixelDivisor = 8192,
kMouseMaxDigitalVel = 32768,
kVKeyModsMask = 0x3F00,
kVKeyHoldFlag = 0x4000, // bit unused by VkKeyScan()
kVKeyReleaseFlag = 0x8000, // bit unused by VkKeyScan()
kVKeyForceReleaseFlag = kVKeyHoldFlag | kVKeyReleaseFlag,
};

enum EKeyState
{
	eKeyState_Up,
	eKeyState_Down,
};


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	int maxTaskQueuedTime; // tasks older than this in queue are skipped
	int chatBoxPostFirstKeyDelay;
	int baseKeyReleaseLockTime;
	int mouseButtonReleaseLockTime;
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
		chatBoxPostFirstKeyDelay = Profile::getInt("System/ChatBoxStartDelay", 0);
		baseKeyReleaseLockTime = Profile::getInt("System/MinKeyHoldTime", 20);
		modKeyReleaseLockTime = Profile::getInt("System/MinModKeyHoldTime", 20);
		mouseButtonReleaseLockTime = Profile::getInt("System/MinMouseButtonHoldTime", 25);
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
	int digitalMouseVel;
	size_t currTaskProgress;
	BitArray<0xFF> keysHeldDown;
	VectorMap<u16, EKeyState> keysWantDown;
	VectorMap<u8, int> keysLockedDown;
	u16 nextQueuedKey;
	u16 backupQueuedKey;
	bool mouseLookActive;
	bool typingChatBoxString;

	DispatchTracker() :
		queuePauseTime(),
		currTaskProgress(),
		digitalMouseVel(),
		keysHeldDown(),
		nextQueuedKey(),
		backupQueuedKey(),
		mouseLookActive(),
		typingChatBoxString()
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
	DBG_ASSERT(sTracker.nextQueuedKey = 0);
	while( theVKeySequence[sTracker.currTaskProgress] != '\0' )
	{
		const size_t idx = sTracker.currTaskProgress++;
		u8 aVKey = theVKeySequence[idx];

		if( aVKey == VK_PAUSE )
		{
			// Special 3-byte sequence to add a forced pause
			u8 c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			u16 aDelay = (c & 0x7F) << 7;
			c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			aDelay |= (c & 0x7F);
			// Delays at end of sequence are ignored
			if( theVKeySequence[sTracker.currTaskProgress] == '\0' )
				return eResult_TaskCompleted;
			sTracker.queuePauseTime = max(sTracker.queuePauseTime, aDelay);
			return eResult_Incomplete;
		}

		if( aVKey == VK_CANCEL )
		{
			// Just set the flags since this can never be an actual key
			sTracker.nextQueuedKey |= kVKeyForceReleaseFlag;
		}
		else if( sTracker.nextQueuedKey & kVKeyMask )
		{
			// Has a key assigned - must be a modifier key, so
			// first convert it to a flag value instead
			switch(sTracker.nextQueuedKey & kVKeyMask)
			{
			case VK_SHIFT:
				sTracker.nextQueuedKey |= kVKeyShiftFlag;
				break;
			case VK_CONTROL:
				sTracker.nextQueuedKey |= kVKeyCtrlFlag;
				break;
			case VK_MENU:
				sTracker.nextQueuedKey |= kVKeyAltFlag;
				break;
			default:
				// Should have exited aleady if it wasn't a modifier key!
				DBG_ASSERT(false);
			}
			sTracker.nextQueuedKey &= ~kVKeyMask;
			// Now add the actual key
			sTracker.nextQueuedKey |= aVKey;
		}
		else
		{
			// Add the key while maintaining flags.
			// If this is a modifier key and is not the last one,
			// the next value will convert this key to a flag instead.
			sTracker.nextQueuedKey |= aVKey;
		}
		// Check if reached a non-modifier key to press
		if( (sTracker.nextQueuedKey & kVKeyMask) != 0 &&
			(sTracker.nextQueuedKey & kVKeyMask) != VK_SHIFT &&
			(sTracker.nextQueuedKey & kVKeyMask) != VK_CONTROL &&
			(sTracker.nextQueuedKey & kVKeyMask) != VK_MENU )
		{
			// Done for now
			break;
		}
	}

	if( theVKeySequence[sTracker.currTaskProgress] == '\0' )
	{
		// Set a flags-only key to just be 0
		if( sTracker.nextQueuedKey && !(sTracker.nextQueuedKey & kVKeyMask) )
			sTracker.nextQueuedKey = 0;
		return eResult_TaskCompleted;
	}

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
	sTracker.nextQueuedKey = VkKeyScan(c);

	if( idx == 0 ) // the initial key to switch to chat bar (/ or Enter)
	{
		// Add a pause to make sure async key checking switches to
		// direct text input in chat box before 'typing' at full speed
		sTracker.queuePauseTime =
			max(sTracker.queuePauseTime,
				kConfig.chatBoxPostFirstKeyDelay);
	}
	else
	{
		// Allow releasing shift quickly to continue typing characters
		// when are using chatbox (shouldn't have the same need for a delay
		// as a key sequence since target likely uses keyboard events instead).
		sTracker.typingChatBoxString = true;
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
	// This may need to be moved into user configuration if
	// it differs by target application.
	if( sTracker.keysHeldDown.test(VK_SHIFT) ||
		sTracker.keysHeldDown.test(VK_CONTROL) ||
		sTracker.keysHeldDown.test(VK_MENU) )
		return false;

	// Move forward during mouse look (won't cause a click on UI to abort chat)
	if( theVKey == VK_LBUTTON && sTracker.keysHeldDown.test(VK_RBUTTON) )
		return true;

	// Middle click
	if( theVKey == VK_MBUTTON )
		return true;

	// Includes arrow keys, but since can't get here while holding shift,
	// does not include shift+arrow keys used for moving cursor in chat box
	if( theVKey >= VK_PRIOR && theVKey <= VK_INSERT )
		return true;

	// Numpad keys don't actually type anything into chat box
	if( theVKey >= VK_NUMPAD0 && theVKey <= VK_NUMPAD9 )
		return true;

	// Function keys have no effect on chat box
	if( theVKey >= VK_F1 && theVKey <= VK_F12 )
		return true;

	return false;
}


static bool isMouseButton(u16 theVKey)
{
	return (theVKey & kVKeyMask) > 0 && ((theVKey & kVKeyMask) < 0x07);
}


static u16 modKeysHeldAsFlags()
{
	u16 result = 0;
	if( sTracker.keysHeldDown.test(VK_SHIFT) )
		result |= kVKeyShiftFlag;
	if( sTracker.keysHeldDown.test(VK_CONTROL) )
		result |= kVKeyCtrlFlag;
	if( sTracker.keysHeldDown.test(VK_MENU) )
		result |= kVKeyAltFlag;

	return result;
}


static bool areModKeysHeld(u16 theVKey)
{
	return
		(theVKey & kVKeyModsMask) == modKeysHeldAsFlags();
}


static EResult setKeyState(u16 theKey, EKeyState theNewState)
{
	// No flags should be set on key (break combo keys into individual keys!)
	DBG_ASSERT(!(theKey & ~kVKeyMask));

	if( theKey == 0 )
		return eResult_InvalidParameter;

	const EKeyState aCurrState = sTracker.keysHeldDown.test(theKey)
		? eKeyState_Down : eKeyState_Up;

	if( theNewState == aCurrState )
		return eResult_Ok;

	if( theNewState == eKeyState_Up )
	{// May not be allowed to release the given key yet
		VectorMap<u8, int>::iterator itr =
			sTracker.keysLockedDown.find(theKey);
		if( itr != sTracker.keysLockedDown.end() && itr->second > 0 )
			return eResult_NotAllowed;
	}

	Input anInput;
	int aLockDownTime = kConfig.baseKeyReleaseLockTime;
	switch(theKey)
	{
	case VK_LBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = theNewState == eKeyState_Down
			? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
		aLockDownTime = kConfig.mouseButtonReleaseLockTime;
		break;
	case VK_RBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = theNewState == eKeyState_Down
			? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
		aLockDownTime = kConfig.mouseButtonReleaseLockTime;
		break;
	case VK_MBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = theNewState == eKeyState_Down
			? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
		aLockDownTime = kConfig.mouseButtonReleaseLockTime;
		break;
	case VK_SHIFT:
	case VK_CONTROL:
	case VK_MENU:
		if( !sTracker.typingChatBoxString )
			aLockDownTime = kConfig.modKeyReleaseLockTime;
		// fall through
	default:
		anInput.type = INPUT_KEYBOARD;
		anInput.ki.wVk = theKey;
		anInput.ki.dwFlags = theNewState == eKeyState_Down
			? 0 : KEYEVENTF_KEYUP;
		break;
	}

	sTracker.inputs.push_back(anInput);
	sTracker.keysHeldDown.set(theKey, theNewState == eKeyState_Down);
	if( theNewState == eKeyState_Down )
		sTracker.keysLockedDown.setValue(theKey, aLockDownTime);

	if( anInput.type == INPUT_MOUSE )
	{
		// For mouse clicks, make sure any mod keys active for the
		// click stay down until some time after the mouse button
		// will be released, which is the true "click" time
		// (unlike keyboard keys which are "clicked" at the moment
		// pressed rather than when released in most cases).
		if( theNewState == eKeyState_Down )
			aLockDownTime += kConfig.modKeyReleaseLockTime;
		else
			aLockDownTime = kConfig.modKeyReleaseLockTime;
		if( sTracker.keysHeldDown.test(VK_SHIFT) )
			sTracker.keysLockedDown.setValue(VK_SHIFT, aLockDownTime);
		if( sTracker.keysHeldDown.test(VK_CONTROL) )
			sTracker.keysLockedDown.setValue(VK_CONTROL, aLockDownTime);
		if( sTracker.keysHeldDown.test(VK_MENU) )
			sTracker.keysLockedDown.setValue(VK_MENU, aLockDownTime);
	}

	return eResult_Ok;
}


static void releaseAllHeldKeys()
{
	sTracker.keysLockedDown.clear();
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		setKeyState(u8(aVKey), eKeyState_Up);
	}
	sTracker.keysHeldDown.reset();
	sTracker.keysWantDown.clear();
}


static void debugPrintInputVector()
{
	static u32 sUpdateCount = 0;
	++sUpdateCount;
#ifndef NDEBUG
#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
#define siPrint(fmt, ...) debugPrint( \
	(strFormat("InputDispatcher: On update %d (%dms): ", \
	sUpdateCount, gAppRunTime) + fmt).c_str(), __VA_ARGS__)

	for(size_t i = 0; i < sTracker.inputs.size(); ++i)
	{
		Input anInput = sTracker.inputs[i];
		if( anInput.type == INPUT_MOUSE )
		{
			switch(anInput.mi.dwFlags)
			{
			case MOUSEEVENTF_LEFTDOWN: siPrint("LMB pressed\n"); break;
			case MOUSEEVENTF_LEFTUP: siPrint("LMB released\n"); break;
			case MOUSEEVENTF_RIGHTDOWN: siPrint("RMB pressed\n"); break;
			case MOUSEEVENTF_RIGHTUP: siPrint("RMB released\n"); break;
			case MOUSEEVENTF_MIDDLEDOWN: siPrint("MMB pressed\n"); break;
			case MOUSEEVENTF_MIDDLEUP: siPrint("MMB released\n"); break;
			}
		}
		else if( anInput.type == INPUT_KEYBOARD )
		{
			LONG aScanCode = MapVirtualKey(anInput.ki.wVk, 0) << 16;
			switch(anInput.ki.wVk)
			{
			case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
			case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
			case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
			case VK_NUMLOCK:
				aScanCode |= (1 << 24);
			}
			aScanCode |= (1 << 25);
			char aKeyName[256];
			if( GetKeyNameTextA(aScanCode, aKeyName, 256) )
			{
				siPrint("%s %s\n",
					aKeyName,
					(anInput.ki.dwFlags & KEYEVENTF_KEYUP)
						? "released" : "pressed");
			}
		}
	}
#undef siPrint
#endif
#endif
}


static void flushInputVector()
{
	debugPrintInputVector();
	if( !sTracker.inputs.empty() )
	{
		#ifndef INPUT_DISPATCHER_SIMULATION_ONLY
		if( kConfig.useScanCodes )
		{// Convert Virtual-Key Codes into scan codes
			for(size_t i = 0; i < sTracker.inputs.size(); ++i)
			{
				if( sTracker.inputs[i].type == INPUT_KEYBOARD )
				{
					sTracker.inputs[i].ki.wScan = 
						MapVirtualKey(sTracker.inputs[i].ki.wVk, 0);
					switch(sTracker.inputs[i].ki.wVk)
					{
					case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
					case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
					case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
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
		#endif
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
	setMouseLookMode(false);
	releaseAllHeldKeys();
	flushInputVector();
}


void update()
{
	// Update timers
	// -------------
	if( sTracker.queuePauseTime > 0 )
		sTracker.queuePauseTime -= gAppFrameTime;
	for(VectorMap<u8, int>::iterator itr = sTracker.keysLockedDown.begin(),
		next_itr = itr; itr != sTracker.keysLockedDown.end(); itr = next_itr)
	{
		++next_itr;
		itr->second -= gAppFrameTime;
		if( itr->second <= 0 )
			next_itr = sTracker.keysLockedDown.erase(itr);
	}


	// Initiate MouseLook mode
	// -----------------------
	if( sTracker.mouseLookActive && !sTracker.keysHeldDown.test(VK_RBUTTON) &&
		!sTracker.nextQueuedKey && !sTracker.backupQueuedKey &&
		(sTracker.queuePauseTime > 0 || sTracker.currTaskProgress == 0) )
	{// Between other tasks, so should start up mouse look mode
		sTracker.nextQueuedKey = VK_RBUTTON;
		// Jump curser to safe spot for initial right-click first
		#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
		debugPrint("InputDispatcher: Jumping cursor to begin MouseLook mode\n");
		#endif
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.dx = 32768;
		anInput.mi.dy = 32768;
		anInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		sTracker.inputs.push_back(anInput);
	}


	// Update queue
	// ------------
	if( !sTracker.nextQueuedKey )
		sTracker.nextQueuedKey = sTracker.backupQueuedKey;
	sTracker.backupQueuedKey = 0;
	while(sTracker.queuePauseTime <= 0 &&
		  sTracker.nextQueuedKey == 0 &&
		  !sTracker.queue.empty() )
	{
		const DispatchTask& aCurrTask = sTracker.queue.front();

		const bool taskIsPastDue =
			sTracker.currTaskProgress == 0 &&
			(aCurrTask.queuedTime + kConfig.maxTaskQueuedTime) < gAppRunTime;

		const Command& aCmd = aCurrTask.cmd;
		VectorMap<u16, EKeyState>::iterator aKeyWantDownItr;
		EResult aTaskResult = eResult_TaskCompleted;

		switch(aCmd.type)
		{
		case eCmdType_PressAndHoldKey:
			// Just set want the key/combo pressed
			sTracker.keysWantDown.findOrAdd(aCmd.data, eKeyState_Up);
			break;
		case eCmdType_ReleaseKey:
			// Do nothing if key was never requested down anyway
			// (or was already force-released by kVKeyForceReleaseFlag)
			aKeyWantDownItr = sTracker.keysWantDown.find(aCmd.data);
			if( aKeyWantDownItr == sTracker.keysWantDown.end() )
				break;
			if( !aKeyWantDownItr->second )
			{// Key hasn't been pressed yet
				// If its a past due event, just forget it, otherwise
				// abort the queue loop and wait for it to be pressed
				if( taskIsPastDue )
					sTracker.keysWantDown.erase(aKeyWantDownItr);
				else
					sTracker.queuePauseTime = 1; // to abort queue loop
			}
			else
			{// Key has been pressed - always release even if past due!
				// If it's a keyboard key, or current modifier keys match
				// those requested for a mouse button, then attempt to
				// release right now so can continue to next queue item.
				// Otherwise set the release request as the queued key.
				const u8 aBaseVKey = aCmd.data & kVKeyMask;
				if( (!isMouseButton(aCmd.data) ||
					 areModKeysHeld(aCmd.data)) &&
					setKeyState(aBaseVKey, eKeyState_Up) == eResult_Ok )
				{
					// Was able to immediately release
					sTracker.keysWantDown.erase(aKeyWantDownItr);
				}
				else
				{
					// Can't immediately release, use queue key instead
					sTracker.nextQueuedKey = aCmd.data | kVKeyReleaseFlag;
				}
			}
			break;
		case eCmdType_VKeySequence:
			if( !taskIsPastDue )
				aTaskResult = popNextKey(aCmd.string);
			break;
		case eCmdType_SlashCommand:
		case eCmdType_SayString:
			if( !taskIsPastDue )
				aTaskResult = popNextStringChar(aCmd.string);
			break;
		}
		if( aTaskResult == eResult_TaskCompleted )
		{
			sTracker.currTaskProgress = 0;
			sTracker.queue.pop_front();
		}
	}


	// Process keysWantDown
	// --------------------
	BitArray<0xFF> aDesiredKeysDown; aDesiredKeysDown.reset();
	// Keep holding right mouse button once mouselook mode started
	if( sTracker.mouseLookActive && sTracker.keysHeldDown.test(VK_RBUTTON) )
		aDesiredKeysDown.set(VK_RBUTTON);
	bool hasNonPressedKeyThatWantsHeldDown = false;
	u16 aPressedKeysDesiredMods = 0;
	for(size_t i = 0; i < sTracker.keysWantDown.size(); ++i)
	{
		const u16 aVKey = sTracker.keysWantDown[i].first;
		const u8 aBaseVKey = u8(aVKey & kVKeyMask);
		const u16 aVKeyModFlags = aVKey & kVKeyModsMask;
		const EKeyState aState = sTracker.keysWantDown[i].second;

		if( aState == eKeyState_Down )
		{// Already been pressed initially
			aDesiredKeysDown.set(aBaseVKey);
			aPressedKeysDesiredMods |= aVKeyModFlags;
			if( aVKeyModFlags &&
				(sTracker.nextQueuedKey & kVKeyReleaseFlag) &&
				(sTracker.nextQueuedKey & kVKeyMask) == aBaseVKey )
			{// Going to release base key, can keep mod keys down until then
				sTracker.nextQueuedKey |= aVKeyModFlags;
			}
		}
		else if( areModKeysHeld(aVKey) &&
				 (sTracker.nextQueuedKey == 0 ||
				  (sTracker.nextQueuedKey & kVKeyModsMask) == aVKeyModFlags) )
		{// Doesn't need a change in mod keys, so can press safely
			aDesiredKeysDown.set(aBaseVKey);
			hasNonPressedKeyThatWantsHeldDown = true;
		}
		else
		{// Needs a change in mod keys - take over queued key to do this
			sTracker.backupQueuedKey = sTracker.nextQueuedKey;
			sTracker.nextQueuedKey = aVKey | kVKeyHoldFlag;
			hasNonPressedKeyThatWantsHeldDown = true;
		}
	}
	// If nothing queued or needs to be newly pressed, hold down any
	// modifier keys that any combo-keys wanted held down.
	// This likely isn't necessary but target apps may behave better.
	if( !sTracker.nextQueuedKey && !hasNonPressedKeyThatWantsHeldDown )
	{
		aDesiredKeysDown.set(VK_SHIFT,
			!!(aPressedKeysDesiredMods & kVKeyShiftFlag));
		aDesiredKeysDown.set(VK_CONTROL,
			!!(aPressedKeysDesiredMods & kVKeyCtrlFlag));
		aDesiredKeysDown.set(VK_MENU,
			!!(aPressedKeysDesiredMods& kVKeyAltFlag));
	}


	// Prepare for nextQueuedKey
	// -------------------------
	bool canSendQueuedKey = false;
	if( sTracker.nextQueuedKey )
	{
		const u16 aVKey = sTracker.nextQueuedKey;
		const u8 aBaseVKey = aVKey & kVKeyMask;
		const bool press = !(aVKey & kVKeyReleaseFlag);
		const bool forced = !press && (aVKey & kVKeyHoldFlag);
		// Make sure desired modifier keys match those of the queued key
		aDesiredKeysDown.set(VK_SHIFT, !!(aVKey & kVKeyShiftFlag));
		aDesiredKeysDown.set(VK_CONTROL, !!(aVKey & kVKeyCtrlFlag));
		aDesiredKeysDown.set(VK_MENU, !!(aVKey & kVKeyAltFlag));
		// Make sure base key is in opposite of desired pressed state
		if( !forced )
			aDesiredKeysDown.set(aBaseVKey, !press);
		// Only send the key if related keys are already in correct state
		// Otherwise, need to wait until other keys are ready from above
		if( areModKeysHeld(sTracker.nextQueuedKey) &&
			(forced || sTracker.keysHeldDown.test(aBaseVKey) == !press) )
		{
			canSendQueuedKey = true;
		}
	}
	/*
		Note that the above logic allows shift/ctrl/alt to be force-released
		temporarily to allow other keys to be pressed that do not want those
		modifiers, even when they are supposed to be held down by a button.
		The assumption is that target applications only check the modifier
		keys at the point a keyboard key is initially pressed, or at press and
		release time in the case of mouse buttons. If this turns out not to
		be the case then the logic can change to just not allow any keys to
		be pressed that don't align with the current modifiers being held down,
		but this will obviously lower responsiveness.
	*/


	// Sync actual keys held to desired state
	// --------------------------------------
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		if( !aDesiredKeysDown.test(aVKey) )
			setKeyState(u8(aVKey), eKeyState_Up);
	}
	const u16 aModKeysHeldAsFlags = modKeysHeldAsFlags();
	for(int aVKey = aDesiredKeysDown.firstSetBit();
		aVKey < aDesiredKeysDown.size();
		aVKey = aDesiredKeysDown.nextSetBit(aVKey+1))
	{
		if( !sTracker.keysHeldDown.test(aVKey) &&
			setKeyState(u8(aVKey), eKeyState_Down) == eResult_Ok )
		{
			const u16 keyJustPressed = u16(aVKey) | aModKeysHeldAsFlags;
			VectorMap<u16, EKeyState>::iterator itr =
				sTracker.keysWantDown.find(keyJustPressed);
			if( itr != sTracker.keysWantDown.end() )
				itr->second = eKeyState_Down;
		}
	}


	// Send queued key event for this update
	// -------------------------------------
	if( canSendQueuedKey )
	{
		u16 aVKey = sTracker.nextQueuedKey & (kVKeyMask | kVKeyModsMask);
		u8 aVKeyBase = u8(aVKey & kVKeyMask);
		const bool wantRelease = !!(sTracker.nextQueuedKey & kVKeyReleaseFlag);
		const EKeyState aNewState =
			wantRelease ? eKeyState_Up : eKeyState_Down;

		if( setKeyState(aVKeyBase, aNewState) == eResult_Ok )
		{
			if( wantRelease )
			{// Stop tracking this key and any combo-keys with same base key
				for(VectorMap<u16, EKeyState>::iterator itr =
					sTracker.keysWantDown.begin(), next_itr = itr;
					itr != sTracker.keysWantDown.end(); itr = next_itr)
				{
					++next_itr;
					if( (itr->first & kVKeyMask) == aVKeyBase )
						next_itr = sTracker.keysWantDown.erase(itr);
				}
			}
			else if( !!(sTracker.nextQueuedKey & kVKeyHoldFlag) )
			{// Begin tracking this key and keeping it held down
				sTracker.keysWantDown.setValue(aVKey, eKeyState_Down);
			}
			sTracker.nextQueuedKey = 0;
		}
	}
	sTracker.typingChatBoxString = false;

	// Update mouse acceleration from digital buttons
	// ----------------------------------------------
	sTracker.digitalMouseVel = max(0,
		sTracker.digitalMouseVel -
		kConfig.mouseDPadAccel * 3 * gAppFrameTime);


	// Dispatch input to system
	// ------------------------
	flushInputVector();
}


void sendKeyCommand(const Command& theCommand)
{
	// These values only valid for certain command types
	const u16 aVKey = theCommand.data;
	const u8 aBaseVKey = aVKey & kVKeyMask;

	switch(theCommand.type)
	{
	case eCmdType_Empty:
		// Do nothing, but don't assert either
		break;
	case eCmdType_PressAndHoldKey:
		if( isSafeAsyncKey(aVKey) &&
			areModKeysHeld(aVKey) &&
			setKeyState(aBaseVKey, eKeyState_Down) == eResult_Ok )
		{// Don't need to queue this key
			sTracker.keysWantDown.setValue(aVKey, eKeyState_Down);
			break;
		}
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_ReleaseKey:
		if( (!isMouseButton(aVKey) || areModKeysHeld(aVKey)) &&
			sTracker.keysHeldDown.test(aBaseVKey) &&
			setKeyState(aBaseVKey, eKeyState_Up) == eResult_Ok )
		{// Safely released key right away, no need to queue release
			sTracker.keysWantDown.erase(aVKey);
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
