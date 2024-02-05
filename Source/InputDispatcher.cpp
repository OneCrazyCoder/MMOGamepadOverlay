//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputDispatcher.h"

#include "InputMap.h"
#include "Lookup.h"
#include "Profile.h"
#include "WindowManager.h" // hotspotMousePosX/Y()

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
kMinMouseLookTimeForAltMove = 100,
kVKeyModsMask = 0x3F00,
kVKeyHoldFlag = 0x4000, // bit unused by VkKeyScan()
kVKeyReleaseFlag = 0x8000, // bit unused by VkKeyScan()
kVKeyForceReleaseFlag = kVKeyHoldFlag | kVKeyReleaseFlag,
MOUSEEVENTF_MOVEABSOLUTE =
	MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK,
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct Input : public INPUT
{ Input() { ZeroMemory(this, sizeof(INPUT)); } };

struct DispatchTask
{ Command cmd; u32 queuedTime; };

struct KeyWantDownStatus
{ s8 depth; bool pressed; KeyWantDownStatus() : depth(), pressed() {} };
typedef VectorMap<u16, KeyWantDownStatus> KeysWantDownMap;


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
	double moveDeadzone;
	std::vector<u8> safeAsyncKeys;
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
		cursorDeadzone = clamp(Profile::getInt("Gamepad/MouseCursorDeadzone", 25), 0, 100) / 100.0;
		cursorRange = clamp(Profile::getInt("Gamepad/MouseCursorSaturation", 100), cursorDeadzone, 100) / 100.0;
		cursorRange = max(0, cursorRange - cursorDeadzone);
		mouseLookXSpeed = mouseLookYSpeed = Profile::getInt("Mouse/LookSpeed", 100);
		mouseLookXSpeed = Profile::getInt("Mouse/LookXSpeed", mouseLookXSpeed);
		mouseLookYSpeed = Profile::getInt("Mouse/LookYSpeed", mouseLookYSpeed);
		mouseLookDeadzone = clamp(Profile::getInt("Gamepad/MouseLookDeadzone", 25), 0, 100) / 100.0;
		mouseLookRange = clamp(Profile::getInt("Gamepad/MouseLookSaturation", 100), mouseLookDeadzone, 100) / 100.0;
		mouseLookRange = max(0, mouseLookRange - mouseLookDeadzone);
		mouseDPadAccel = max(8, Profile::getInt("Gamepad/MouseDPadAccel", 50));
		mouseWheelDeadzone = clamp(Profile::getInt("Gamepad/MouseWheelDeadzone", 25), 0, 100) / 100.0;
		mouseWheelRange = clamp(Profile::getInt("Gamepad/MouseWheelSaturation", 100), mouseWheelDeadzone, 100) / 100.0;
		mouseWheelRange = max(0, mouseWheelRange - mouseWheelDeadzone);
		mouseWheelSpeed = Profile::getInt("Mouse/WheelSpeed", 255);
		moveDeadzone = clamp(Profile::getInt("Gamepad/MoveDeadzone", 50), 0, 100) / 100.0;
		std::string aString = Profile::getStr("System/safeAsyncKeys");
		if( !aString.empty() )
		{
			std::vector<std::string> aParsedString;
			sanitizeSentence(aString, aParsedString);
			for( size_t i = 0; i < aParsedString.size(); ++i)
			{
				u8 aVKey = keyNameToVirtualKey(upper(aParsedString[i]));
				if( aVKey == 0 )
				{
					logError("Unrecognized key name for safeAsyncKeys: %s",
						aParsedString[i].c_str());
				}
				else if( aVKey == VK_F1 )
				{// Assume all function keys included
					for( aVKey = VK_F1; aVKey <= VK_F24; ++aVKey )
						safeAsyncKeys.push_back(aVKey);
				}
				else if( aVKey == VK_NUMPAD0 || aVKey == VK_NUMPAD1 )
				{// Assume all numpad numbers are included
					for( aVKey = VK_NUMPAD0; aVKey <= VK_NUMPAD9; ++aVKey )
						safeAsyncKeys.push_back(aVKey);
				}
				else if( aVKey == '0' || aVKey == '1' )
				{// Assume all number keys are included
					for( aVKey = '0'; aVKey <= '9'; ++aVKey )
						safeAsyncKeys.push_back(aVKey);
				}
				else
				{
					safeAsyncKeys.push_back(aVKey);
				}
			}
			std::sort(safeAsyncKeys.begin(), safeAsyncKeys.end());
			safeAsyncKeys.erase(
				std::unique(safeAsyncKeys.begin(), safeAsyncKeys.end()),
				safeAsyncKeys.end());
			if( safeAsyncKeys.size() < safeAsyncKeys.capacity() )
			{// Shrink to fit
				std::vector<u8> temp(safeAsyncKeys);
				temp.swap(safeAsyncKeys);
			}
		}
	}
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
	int mouseLookActiveTime;
	size_t currTaskProgress;
	BitArray<0xFF> keysHeldDown;
	KeysWantDownMap keysWantDown;
	VectorMap<u8, int> keysLockedDown;
	BitArray<eSpecialKey_MoveNum> moveKeysHeld;
	u16 nextQueuedKey;
	u16 backupQueuedKey;
	u16 jumpToHotspot;
	bool mouseLookWanted;
	bool typingChatBoxString;

	DispatchTracker() :
		queuePauseTime(),
		currTaskProgress(),
		digitalMouseVel(),
		mouseLookActiveTime(),
		keysHeldDown(),
		nextQueuedKey(),
		backupQueuedKey(),
		jumpToHotspot(),
		mouseLookWanted(),
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

static EResult popNextKey(const u8* theVKeySequence)
{
	DBG_ASSERT(sTracker.nextQueuedKey == 0);
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

		if( aVKey == VK_SELECT )
		{
			// Special 3-byte sequence to cause mouse cursor jump to hotspot
			u8 c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			sTracker.jumpToHotspot = (c & 0x7F) << 7;
			c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			sTracker.jumpToHotspot |= (c & 0x7F);
			continue;
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


static bool isSafeAsyncKey(u16 theVKey)
{
	// These are keys that can be pressed while typing
	// in a macro into the chat box without interfering
	// with the macro or being interfered with by it.

	// Only keys that don't need modifier keys are safe
	if( (theVKey & ~kVKeyMask) != 0 )
		return false;

	const u8 aBaseVkey = u8(theVKey & kVKeyMask);

	// If MouseLookMoveForward is Left-Click, should be safe during mouselook
	// because it won't cause a click in the UI
	if( aBaseVkey == VK_LBUTTON &&
		InputMap::keyForSpecialAction(eSpecialKey_MLMoveF) == VK_LBUTTON &&
		sTracker.mouseLookActiveTime > kMinMouseLookTimeForAltMove )
		return true;

	// Remaining safe async keys depend on target, so use Profile data
	return
		std::binary_search(
			kConfig.safeAsyncKeys.begin(),
			kConfig.safeAsyncKeys.end(),
			aBaseVkey);
}


static bool isMouseButton(u16 theVKey)
{
	const u8 aBaseVKey = u8(theVKey & kVKeyMask);
	switch(aBaseVKey)
	{
	case VK_LBUTTON:
	case VK_MBUTTON:
	case VK_RBUTTON:
		return true;
	}
	return false;
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


static EResult setKeyDown(u16 theKey, bool down)
{
	// No flags should be set on key (break combo keys into individual keys!)
	DBG_ASSERT(!(theKey & ~kVKeyMask));

	if( theKey == 0 )
		return eResult_InvalidParameter;

	const bool wasDown = sTracker.keysHeldDown.test(theKey);
	if( down == wasDown )
		return eResult_Ok;

	if( !down )
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
		anInput.mi.dwFlags = down
			? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
		aLockDownTime = kConfig.mouseButtonReleaseLockTime;
		break;
	case VK_RBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = down
			? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
		aLockDownTime = kConfig.mouseButtonReleaseLockTime;
		break;
	case VK_MBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = down
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
		anInput.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
		break;
	}

	sTracker.inputs.push_back(anInput);
	sTracker.keysHeldDown.set(theKey, down);
	if( down )
		sTracker.keysLockedDown.setValue(theKey, aLockDownTime);

	if( anInput.type == INPUT_MOUSE )
	{
		// For mouse clicks, make sure any mod keys active for the
		// click stay down until some time after the mouse button
		// will be released, which is the true "click" time
		// (unlike keyboard keys which are "clicked" at the moment
		// pressed rather than when released in most cases).
		if( down )
			aLockDownTime += kConfig.modKeyReleaseLockTime;
		else
			aLockDownTime = kConfig.modKeyReleaseLockTime;
		if( sTracker.keysHeldDown.test(VK_SHIFT) )
			sTracker.keysLockedDown.setValue(VK_SHIFT, aLockDownTime);
		if( sTracker.keysHeldDown.test(VK_CONTROL) )
			sTracker.keysLockedDown.setValue(VK_CONTROL, aLockDownTime);
		if( sTracker.keysHeldDown.test(VK_MENU) )
			sTracker.keysLockedDown.setValue(VK_MENU, aLockDownTime);

		// Releasing RClick resets mouseLookActiveTime
		if( sTracker.mouseLookActiveTime && theKey == VK_RBUTTON && !down )
			sTracker.mouseLookActiveTime = 0;
	}

	return eResult_Ok;
}


static bool tryQuickReleaseHeldKey(KeysWantDownMap::iterator theKeyItr)
{
	DBG_ASSERT(theKeyItr != sTracker.keysWantDown.end());

	// If multiple gamepad buttons are tied to the same key and more than one
	// is held down, we "release" the key by just decrementing a counter
	if( theKeyItr->second.depth > 1 )
	{
		--theKeyItr->second.depth;
		return true;
	}

	const u16 aVKey = theKeyItr->first & (kVKeyMask | kVKeyModsMask);
	const u8 aBaseVKey = u8(aVKey & kVKeyMask);
	DBG_ASSERT(aBaseVKey);

	// Don't release yet if not actually pressed
	if( !theKeyItr->second.pressed || !sTracker.keysHeldDown.test(aBaseVKey) )
		return false;

	// In most target apps, releasing a mouse button can itself count as
	// an action being taken (a "click"), whereas a keyboard key generally
	// only means to stop doing an action (like stop moving forward). Thus
	// the target app will check modifier keys on a mouse up event to see
	// if it is a shift-click vs ctrl-click vs normal click, but for keyboard
	// keys usually only cares about the modifiers when the key is pressed.
	// Therefore if the "key" is a mouse button, need to make sure all related
	// modifier keys like Shift are held before releasing the mouse button!
	if( isMouseButton(aVKey) && !areModKeysHeld(aVKey) )
		return false;

	// Make sure no other keysWantDown use this same base key
	for(KeysWantDownMap::iterator itr = sTracker.keysWantDown.begin();
		itr != sTracker.keysWantDown.end(); ++itr)
	{
		// If find another entry wants this same base key to stay down,
		// don't actually release the key, but return true and leave this
		// version of the key at 0 depth as nothing more needs done now.
		// The actual key will be released when this other entry is done.
		if( itr != theKeyItr &&
			(itr->first & kVKeyMask) == aBaseVKey &&
			itr->second.pressed &&
			itr->second.depth > 0 )
		{
			theKeyItr->second.depth = 0;
			return true;
		}
	}

	// Now attempt to actually release the key (may be locked down though)
	if( setKeyDown(aBaseVKey, false) == eResult_Ok )
	{
		theKeyItr->second.depth = 0;
		return true;
	}

	return false;
}


static void releaseAllHeldKeys()
{
	sTracker.keysLockedDown.clear();
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		setKeyDown(u16(aVKey), false);
	}
	sTracker.keysHeldDown.reset();
	sTracker.keysWantDown.clear();
	sTracker.moveKeysHeld.reset();
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
			case MOUSEEVENTF_MOVEABSOLUTE:
				siPrint("Jumped cursor to %d%%x x %d%%y of desktop\n",
					anInput.mi.dx * 100 / 0xFFFF,
					anInput.mi.dy * 100 / 0xFFFF);
				break;
			}
		}
		else if( anInput.type == INPUT_KEYBOARD )
		{
			siPrint("%s %s\n",
				virtualKeyToName(anInput.ki.wVk),
				(anInput.ki.dwFlags & KEYEVENTF_KEYUP)
					? "released" : "pressed");
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
	if( sTracker.mouseLookWanted &&
		!sTracker.keysHeldDown.test(VK_RBUTTON) &&
		!sTracker.nextQueuedKey &&
		!sTracker.backupQueuedKey &&
		!sTracker.jumpToHotspot &&
		(sTracker.queuePauseTime > 0 || sTracker.currTaskProgress == 0) )
	{// Between other tasks, so should start up mouse look mode
		// Jump curser to safe spot for initial right-click first
		sTracker.jumpToHotspot = eSpecialHotspot_MouseLookStart;
		sTracker.nextQueuedKey = VK_RBUTTON | kVKeyHoldFlag;
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
		KeysWantDownMap::iterator aKeyWantDownItr;
		EResult aTaskResult = eResult_TaskCompleted;

		switch(aCmd.type)
		{
		case eCmdType_PressAndHoldKey:
			// Just set want the key/combo pressed
			DBG_ASSERT(aCmd.data != 0);
			DBG_ASSERT((aCmd.data & ~(kVKeyMask | kVKeyModsMask)) == 0);
			sTracker.keysWantDown.findOrAdd(aCmd.data).depth += 1;
			break;
		case eCmdType_ReleaseKey:
			// Do nothing if key was never requested down anyway
			// (or was already force-released by kVKeyForceReleaseFlag)
			DBG_ASSERT(aCmd.data != 0);
			DBG_ASSERT((aCmd.data & ~(kVKeyMask | kVKeyModsMask)) == 0);
			aKeyWantDownItr = sTracker.keysWantDown.find(aCmd.data);
			if( aKeyWantDownItr == sTracker.keysWantDown.end() )
				break;
			if( !aKeyWantDownItr->second.pressed )
			{// Key has yet to be pressed in the first place
				// Abort queue loop and wait for it to be pressed first,
				// unless this is a past-due event, in which case just
				// forget ever wanted the key pressed in the first place
				if( !taskIsPastDue )
					sTracker.queuePauseTime = 1; // to abort queue loop
				else if( --aKeyWantDownItr->second.depth <= 0 )
					sTracker.keysWantDown.erase(aKeyWantDownItr);
			}
			else
			{// Key has been pressed, and should release even if past due

				// Attempt to release now and move on to next queue item
				// If can't, set releasing it as the queued key event
				if( !tryQuickReleaseHeldKey(aKeyWantDownItr) )
				{
					--aKeyWantDownItr->second.depth;
					sTracker.nextQueuedKey = aCmd.data | kVKeyReleaseFlag;
				}
			}
			break;
		case eCmdType_TapKey:
			if( !taskIsPastDue )
				sTracker.nextQueuedKey = aCmd.data;
			break;
		case eCmdType_VKeySequence:
			if( !taskIsPastDue )
				aTaskResult = popNextKey((const u8*)aCmd.string);
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
	if( sTracker.mouseLookWanted && sTracker.keysHeldDown.test(VK_RBUTTON) )
	{// Keep holding right mouse button once mouselook mode started
		aDesiredKeysDown.set(VK_RBUTTON);
		sTracker.mouseLookActiveTime += gAppFrameTime;
	}
	bool hasNonPressedKeyThatWantsHeldDown = false;
	u16 aPressedKeysDesiredMods = 0;
	for(KeysWantDownMap::iterator itr =
		sTracker.keysWantDown.begin(), next_itr = itr;
		itr != sTracker.keysWantDown.end(); itr = next_itr)
	{
		++next_itr;
		const u16 aVKey = itr->first;
		const u8 aBaseVKey = u8(aVKey & kVKeyMask);
		const u16 aVKeyModFlags = aVKey & kVKeyModsMask;
		const bool pressed = itr->second.pressed;
		const bool wantPressed = itr->second.depth > 0;

		if( pressed && !wantPressed )
		{// Been pressed and no longer needs to be
			next_itr = sTracker.keysWantDown.erase(itr);
			continue;
		}
		
		if( pressed && sTracker.keysHeldDown.test(aBaseVKey) )
		{// Already been pressed and base key still held
			aDesiredKeysDown.set(aBaseVKey);
			aPressedKeysDesiredMods |= aVKeyModFlags;
			if( aVKeyModFlags &&
				(sTracker.nextQueuedKey & kVKeyReleaseFlag) &&
				(sTracker.nextQueuedKey & kVKeyMask) == aBaseVKey )
			{// Going to release base key, can keep mod keys down until then
				sTracker.nextQueuedKey |= aVKeyModFlags;
			}
			continue;
		}
		
		if( areModKeysHeld(aVKey) &&
			(sTracker.nextQueuedKey == 0 ||
			 (sTracker.nextQueuedKey & kVKeyModsMask) == aVKeyModFlags) )
		{// Doesn't need a change in mod keys, so can press safely
			aDesiredKeysDown.set(VK_SHIFT, !!(aVKey & kVKeyShiftFlag));
			aDesiredKeysDown.set(VK_CONTROL, !!(aVKey & kVKeyCtrlFlag));
			aDesiredKeysDown.set(VK_MENU, !!(aVKey & kVKeyAltFlag));
			aDesiredKeysDown.set(aBaseVKey);
			hasNonPressedKeyThatWantsHeldDown = true;
			continue;
		}
		
		if( sTracker.backupQueuedKey == 0 &&
			(!sTracker.nextQueuedKey || !pressed) )
		{// Needs a change in mod keys - take over queued key to do this
			sTracker.backupQueuedKey = sTracker.nextQueuedKey;
			sTracker.nextQueuedKey = aVKey | kVKeyHoldFlag;
			hasNonPressedKeyThatWantsHeldDown = true;
			continue;
		}
		
		// If reached here, want to press the key but just can't right now!
		// Will have to wait and try again next frame
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
	bool readyForQueuedEvent = true;
	if( sTracker.nextQueuedKey )
	{
		const u16 aVKey = sTracker.nextQueuedKey;
		const u8 aBaseVKey = u8(aVKey & kVKeyMask);
		DBG_ASSERT(aBaseVKey != 0); // otherwise somehow got flags but no key!
		const bool press = !(aVKey & kVKeyReleaseFlag);
		const bool forced = !press && (aVKey & kVKeyHoldFlag);
		// Make sure desired modifier keys match those of the queued key
		aDesiredKeysDown.set(VK_SHIFT, !!(aVKey & kVKeyShiftFlag));
		aDesiredKeysDown.set(VK_CONTROL, !!(aVKey & kVKeyCtrlFlag));
		aDesiredKeysDown.set(VK_MENU, !!(aVKey & kVKeyAltFlag));
		// Only send the key if related keys are already in correct state
		// Otherwise, need to wait until other keys are ready next frame
		readyForQueuedEvent = areModKeysHeld(sTracker.nextQueuedKey);
		// Make sure base key is in opposite of desired pressed state
		if( !forced )
		{
			aDesiredKeysDown.set(aBaseVKey, !press);
			if( sTracker.keysHeldDown.test(aBaseVKey) == press )
				readyForQueuedEvent = false;
		}
		// For mouse buttons in key sequences, before *any* are pressed,
		// *all* other mouse buttons should be released.
		if( !forced && press && isMouseButton(aVKey) &&
			!(aVKey & kVKeyHoldFlag) )
		{
			aDesiredKeysDown.reset(VK_LBUTTON);
			aDesiredKeysDown.reset(VK_MBUTTON);
			aDesiredKeysDown.reset(VK_RBUTTON);
			if( sTracker.keysHeldDown.test(VK_LBUTTON) ||
				sTracker.keysHeldDown.test(VK_MBUTTON) ||
				sTracker.keysHeldDown.test(VK_RBUTTON) )
			{
				readyForQueuedEvent = false;
			}
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
			setKeyDown(u8(aVKey), false);
	}
	const u16 aModKeysHeldAsFlags = modKeysHeldAsFlags();
	for(int aVKey = aDesiredKeysDown.firstSetBit();
		aVKey < aDesiredKeysDown.size();
		aVKey = aDesiredKeysDown.nextSetBit(aVKey+1))
	{
		if( !sTracker.keysHeldDown.test(aVKey) &&
			setKeyDown(u8(aVKey), true) == eResult_Ok )
		{
			const u16 keyJustPressed = u16(aVKey) | aModKeysHeldAsFlags;
			KeysWantDownMap::iterator itr =
				sTracker.keysWantDown.find(keyJustPressed);
			if( itr != sTracker.keysWantDown.end() )
				itr->second.pressed = true;
		}
	}


	// Send queued key event OR jump to hotspot event
	// ----------------------------------------------
	if( readyForQueuedEvent && sTracker.jumpToHotspot )
	{
		Input anInput;
		anInput.type = INPUT_MOUSE;
		const Hotspot& aHotspot = InputMap::getHotspot(sTracker.jumpToHotspot);
		anInput.mi.dx = WindowManager::hotspotMousePosX(aHotspot);
		anInput.mi.dy = WindowManager::hotspotMousePosY(aHotspot);
		anInput.mi.dwFlags = MOUSEEVENTF_MOVEABSOLUTE;
		sTracker.inputs.push_back(anInput);		
		sTracker.jumpToHotspot = 0;
	}
	else if( readyForQueuedEvent && sTracker.nextQueuedKey )
	{
		u16 aVKey = sTracker.nextQueuedKey & (kVKeyMask | kVKeyModsMask);
		u8 aVKeyBase = u8(aVKey & kVKeyMask);
		const bool wantRelease = !!(sTracker.nextQueuedKey & kVKeyReleaseFlag);

		if( setKeyDown(aVKeyBase, !wantRelease) == eResult_Ok )
		{
			const bool wantHold = !!(sTracker.nextQueuedKey & kVKeyHoldFlag);
			sTracker.nextQueuedKey = 0;
			if( wantRelease )
			{
				if( wantHold ) // wantHold + wantRelease = forced released
				{// Stop tracking this key and any combo-keys with same base
					for(KeysWantDownMap::iterator itr =
						sTracker.keysWantDown.begin(), next_itr = itr;
						itr != sTracker.keysWantDown.end(); itr = next_itr)
					{
						++next_itr;
						if( (itr->first & kVKeyMask) == aVKeyBase )
							next_itr = sTracker.keysWantDown.erase(itr);
					}
				}
			}
			else if( wantHold )
			{// Flag key as having been pressed
				KeysWantDownMap::iterator itr =
					sTracker.keysWantDown.find(aVKey);
				if( itr != sTracker.keysWantDown.end() )
					itr->second.pressed = true;
			}
			else if( isMouseButton(aVKeyBase) )
			{// Mouse button that wanted to be "clicked" once in a sequence
				// Should release the mouse button for next queued key action
				sTracker.nextQueuedKey = aVKey | kVKeyReleaseFlag;
			}
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
	const u8 aBaseVKey = u8(aVKey & kVKeyMask);

	switch(theCommand.type)
	{
	case eCmdType_Empty:
		// Do nothing, but don't assert either
		break;
	case eCmdType_PressAndHoldKey:
		DBG_ASSERT(aBaseVKey != 0);
		DBG_ASSERT((aVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
		{// Try pressing the key right away instead of queueing it
			const size_t aPos = sTracker.keysWantDown.findInsertPos(aVKey);
			if( aPos < sTracker.keysWantDown.size() &&
				sTracker.keysWantDown[aPos].first == aVKey )
			{
				// Already requested hold this key...
				// Just increment counter of simultaneous hold requests
				++sTracker.keysWantDown[aPos].second.depth;
				break;
			}
			if( isSafeAsyncKey(aVKey) &&
				setKeyDown(aBaseVKey, true) == eResult_Ok )
			{// Was able to press the key now, don't need to queue it!
				KeyWantDownStatus aStatus;
				aStatus.pressed = true;
				aStatus.depth = 1;
				sTracker.keysWantDown.insert(
					sTracker.keysWantDown.begin() + aPos,
					std::make_pair(aVKey, aStatus));
				break;
			}
		}
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_ReleaseKey:
		DBG_ASSERT(aBaseVKey != 0);
		DBG_ASSERT((aVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
		{// Try releasing the key right away instead of queueing it
			KeysWantDownMap::iterator itr =
				sTracker.keysWantDown.find(aVKey);
			if( itr != sTracker.keysWantDown.end() &&
				tryQuickReleaseHeldKey(itr) )
			{
				break;
			}
		}
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_TapKey:
		DBG_ASSERT(aBaseVKey != 0);
		DBG_ASSERT((aVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
		{// Try tapping the key right away instead of queueing it
			if( isSafeAsyncKey(aVKey) &&
				!sTracker.keysHeldDown.test(aBaseVKey) &&
				setKeyDown(aBaseVKey, true) == eResult_Ok )
			{// Was able to press the key now, don't need to queue it!
				break;
			}
		}
		// fall through
	case eCmdType_VKeySequence:
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		sTracker.queue.push_back(theCommand);
		break;
	default:
		DBG_ASSERT(false && "Invalid command type for sendkeyCommand()!");
	}
}


void setMouseLookMode(bool active)
{
	sTracker.mouseLookWanted = active;
}


void moveMouse(int dx, int dy, bool digital)
{
	// Ignore mouse movement while trying to jump cursor to a hotspot
	if( sTracker.jumpToHotspot )
		return;

	const bool kMouseLookSpeed =
		sTracker.mouseLookWanted ||
		sTracker.keysHeldDown.test(VK_RBUTTON);

	// Get magnitude of desired mouse motion in 0 to 1.0 range
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
	else if( aMagnitude < 1.0 )
	{// Apply exponential easing curve to magnitude
		aMagnitude = std::pow(2, 10 * (aMagnitude - 1));
	}

	// Assume user manually using mouse look if drag mouse while holding RMB
	if( !sTracker.mouseLookWanted && sTracker.keysHeldDown.test(VK_RBUTTON) )
		sTracker.mouseLookActiveTime += gAppFrameTime * aMagnitude;

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


void scrollMouseWheel(int dy, bool digital, bool stepped)
{
	// Get magnitude of desired mouse motion in 0 to 1.0 range
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
	else if( aMagnitude < 1.0 )
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


void scrollMouseWheelOnce(ECommandDir theDir)
{
	if( theDir == eCmdDir_Up )
	{
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.mouseData = WHEEL_DELTA;
		anInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
		sTracker.inputs.push_back(anInput);
	}
	else if( theDir == eCmdDir_Down )
	{
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.mouseData = -WHEEL_DELTA;
		anInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
		sTracker.inputs.push_back(anInput);
	}
}


void moveCharacter(int move, int turn, int strafe)
{
	BitArray<eSpecialKey_MoveNum> moveKeysWantDown;
	moveKeysWantDown.reset();

	// Treat as 2 virtual analog sticks initially,
	// one for MoveTurn and one for MoveStrafe

	// Get magnitude of MoveTurn motion in 0 to 1.0 range
	double aMagnitude = std::sqrt(double(turn) * turn + move * move) / 255.0;
	// Get angle of desired MoveTurn motion (right=0, left=pi, up=+, down=-)
	const double aTurnAngle = atan2(double(move), double(turn));
	// Check if MoveTurn should apply
	const bool applyMoveTurn = aMagnitude > kConfig.moveDeadzone;

	// Get magnitude of MoveStrafe motion in 0 to 1.0 range
	aMagnitude = std::sqrt(double(strafe) * strafe + move * move) / 255.0;
	// Get angle of desired MoveStrafe motion (right=0, left=pi, up=+, down=-)
	const double aStrafeAngle = atan2(double(move), double(strafe));
	// Check if MoveStrafe should apply
	const bool applyMoveStrafe = aMagnitude > kConfig.moveDeadzone;

	/*
		Decide if should use mouselook movement keys...
		What is the point of having a separate set of move keys for mouselook?

		The primary reason is to make sure movement is processed as much as
		possible, and movement keys like WASD will be locked out when
		sending a macro or holding a modifier like Shift because that could
		change what they do (type in a chat box or be a totally different
		command like opening a menu). However, in most clients holding
		right-click and then pressing left-click also acts as move forward,
		and that won't interfere with any of the above, so might as well
		take advantage of it for more responsiveness during mouselook.
		But left-click won't move forward unless in mouselook, which is why
		need to figure out if it is active or not to know what keys to use.
	*/
	const bool useMouseLookMoveKeys =
		sTracker.mouseLookActiveTime > kMinMouseLookTimeForAltMove;

	// Calculate which movement actions, if any, should now apply
	moveKeysWantDown.set(
		useMouseLookMoveKeys
			? eSpecialKey_MLTurnL - eSpecialKey_FirstMove
			: eSpecialKey_TurnL - eSpecialKey_FirstMove,
		applyMoveTurn &&
		(aTurnAngle < M_PI * -0.625 || aTurnAngle > M_PI * 0.625));

	moveKeysWantDown.set(
		useMouseLookMoveKeys
			? eSpecialKey_MLTurnR - eSpecialKey_FirstMove
			: eSpecialKey_TurnR - eSpecialKey_FirstMove,
		applyMoveTurn &&
		aTurnAngle > M_PI * -0.375 && aTurnAngle < M_PI * 0.375);

	moveKeysWantDown.set(
		useMouseLookMoveKeys
			? eSpecialKey_MLStrafeL - eSpecialKey_FirstMove
			: eSpecialKey_StrafeL - eSpecialKey_FirstMove,
		applyMoveStrafe &&
		(aStrafeAngle < M_PI * -0.625 || aStrafeAngle > M_PI * 0.625));

	moveKeysWantDown.set(
		useMouseLookMoveKeys
			? eSpecialKey_MLStrafeR - eSpecialKey_FirstMove
			: eSpecialKey_StrafeR - eSpecialKey_FirstMove,
		applyMoveStrafe &&
		aStrafeAngle > M_PI * -0.375 && aStrafeAngle < M_PI * 0.375);

	// For move forward/back, use the virtual stick that had the greatest X
	// motion in order to make sure a proper circular deadzone is used.
	moveKeysWantDown.set(
		useMouseLookMoveKeys
			? eSpecialKey_MLMoveF - eSpecialKey_FirstMove
			: eSpecialKey_MoveF - eSpecialKey_FirstMove,
		(applyMoveTurn && abs(turn) >= abs(strafe) &&
			aTurnAngle > M_PI * 0.125 && aTurnAngle < M_PI * 0.875) ||
		(applyMoveStrafe && abs(strafe) >= abs(turn) &&
			aStrafeAngle > M_PI * 0.125 && aStrafeAngle < M_PI * 0.875));

	moveKeysWantDown.set(
		useMouseLookMoveKeys
			? eSpecialKey_MLMoveB - eSpecialKey_FirstMove
			: eSpecialKey_MoveB - eSpecialKey_FirstMove,
		(applyMoveTurn && abs(turn) >= abs(strafe) &&
			aTurnAngle < M_PI * -0.125 && aTurnAngle > M_PI * -0.875) ||
		(applyMoveStrafe && abs(strafe) >= abs(turn) &&
			aStrafeAngle < M_PI * -0.125 && aStrafeAngle > M_PI * -0.875));

	// Press new movement keys
	Command aCmd; aCmd.type = eCmdType_PressAndHoldKey;
	for(int aWantedKey = moveKeysWantDown.firstSetBit();
		aWantedKey < moveKeysWantDown.size();
		aWantedKey = moveKeysWantDown.nextSetBit(aWantedKey+1))
	{
		if( !sTracker.moveKeysHeld.test(aWantedKey) )
		{
			aCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(aWantedKey + eSpecialKey_FirstMove));
			if( aCmd.data != 0 )
				sendKeyCommand(aCmd);
			sTracker.moveKeysHeld.set(aWantedKey);
		}
	}

	// Release movement keys that aren't needed any more
	aCmd.type = eCmdType_ReleaseKey;
	for(int aHeldKey = sTracker.moveKeysHeld.firstSetBit();
		aHeldKey < sTracker.moveKeysHeld.size();
		aHeldKey = sTracker.moveKeysHeld.nextSetBit(aHeldKey+1))
	{
		if( !moveKeysWantDown.test(aHeldKey) )
		{
			aCmd.data = InputMap::keyForSpecialAction(
				ESpecialKey(aHeldKey + eSpecialKey_FirstMove));
			if( aCmd.data != 0 )
				sendKeyCommand(aCmd);
			sTracker.moveKeysHeld.reset(aHeldKey);
		}
	}
}

} // InputDispatcher
