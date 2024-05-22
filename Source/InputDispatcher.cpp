//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputDispatcher.h"

#include "InputMap.h"
#include "Lookup.h"
#include "Profile.h"
#include "WindowManager.h"

namespace InputDispatcher
{

// Uncomment this to print out SendInput events to debug window
//#define INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
// Uncomment this along with the above to also print mouse movement
//#define INPUT_DISPATCHER_DEBUG_PRINT_SENT_MOUSE_MOTION
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
	u32 baseKeyReleaseLockTime;
	u32 mouseButtonReleaseLockTime;
	u32 modKeyReleaseLockTime;
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
	int mouseLookZoneFixTime;
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
		mouseLookZoneFixTime = Profile::getInt("System/MouseLookZoneFix");
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
	: public ConstructFromZeroInitializedMemory<DispatchTracker>
{
	DispatchQueue queue;
	std::vector<Input> inputs;
	size_t currTaskProgress;
	int queuePauseTime;
	BitArray<0xFF> keysHeldDown;
	KeysWantDownMap keysWantDown;
	VectorMap<u8, u32> keysLockedDown;
	BitArray<eSpecialKey_MoveNum> moveKeysHeld;
	u16 nextQueuedKey;
	u16 backupQueuedKey;
	bool typingChatBoxString;

	EMouseMode mouseMode;
	EMouseMode mouseModeWanted;
	int mouseVelX, mouseVelY;
	int mouseDigitalVel;
	int mouseLookZoneFixTimer;
	u16 mouseJumpToHotspot;
	bool mouseJumpAttempted;
	bool mouseJumpVerified;
	bool mouseJumpQueued;
	bool mouseJumpInterpolate;
	bool mouseInterpolateRestart;

	DispatchTracker() :
		mouseMode(eMouseMode_Cursor),
		mouseModeWanted(eMouseMode_Cursor)
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
			sTracker.mouseJumpToHotspot = (c & 0x7F) << 7;
			c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			sTracker.mouseJumpToHotspot |= (c & 0x7F);
			sTracker.mouseJumpInterpolate = false;
			continue;
		}

		if( aVKey == VK_CANCEL )
		{
			// Flag next key should be released instead of pressed
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


static bool requiredModKeysAreAlreadyHeld(u16 theVKey)
{
	return
		(theVKey & kVKeyModsMask) == modKeysHeldAsFlags();
}


static bool isSafeAsyncKey(u16 theVKey)
{
	// These are keys that can be pressed while typing
	// in a macro into the chat box without interfering
	// with the macro or being interfered with by it.

	// Only keys that don't need modifier keys are safe
	if( (theVKey & ~kVKeyMask) != 0 )
		return false;

	// No keys are safe while already holding a modifier key
	if( modKeysHeldAsFlags() != 0 )
		return false;

	// Mouse buttons are not safe while in certain mouse modes
	if( isMouseButton(theVKey) &&
		(sTracker.mouseMode == eMouseMode_Hide ||
		 sTracker.mouseMode == eMouseMode_PostJump ||
		 sTracker.mouseMode == eMouseMode_JumpClicked ||
		 sTracker.mouseJumpQueued ||
		 sTracker.mouseJumpToHotspot) )
		return false;

	const u8 aBaseVkey = u8(theVKey & kVKeyMask);

	// Remaining safe async keys depend on target, so use Profile data
	return
		std::binary_search(
			kConfig.safeAsyncKeys.begin(),
			kConfig.safeAsyncKeys.end(),
			aBaseVkey);
}


static void offsetMousePos(int x, int y)
{
	switch(sTracker.mouseMode)
	{
	case eMouseMode_Cursor:
	case eMouseMode_Look:
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.dx = sTracker.mouseVelX;
		anInput.mi.dy = sTracker.mouseVelY;
		anInput.mi.dwFlags = MOUSEEVENTF_MOVE;
		sTracker.inputs.push_back(anInput);
		sTracker.mouseLookZoneFixTimer = 0;
		break;
	}
}


static void jumpMouseToHotspot(u16 theHotspotID)
{
	// No jumps allowed while holding down a mouse button!
	DBG_ASSERT(!sTracker.keysHeldDown.test(VK_LBUTTON));
	DBG_ASSERT(!sTracker.keysHeldDown.test(VK_MBUTTON));
	DBG_ASSERT(!sTracker.keysHeldDown.test(VK_RBUTTON));

	const POINT& aCurrentPos = WindowManager::mouseToOverlayPos();
	if( (!sTracker.mouseJumpAttempted || sTracker.mouseJumpVerified) &&
		(sTracker.mouseMode == eMouseMode_Cursor ||
		 sTracker.mouseMode == eMouseMode_PostJump) )
	{// Save pre-jump mouse position to possibly restore later
		InputMap::modifyHotspot(
			eSpecialHotspot_LastCursorPos,
			WindowManager::overlayPosToHotspot(aCurrentPos));
	}
	sTracker.mouseJumpAttempted = true;
	sTracker.mouseJumpVerified = false;

	// If already at dest pos anyway, don't bother with the jump itself
	POINT aDestPos = WindowManager::hotspotToOverlayPos(
		InputMap::getHotspot(theHotspotID));
	if( aCurrentPos.x == aDestPos.x && aCurrentPos.y == aDestPos.y )
	{
		sTracker.mouseJumpVerified = true;
		return;
	}

	aDestPos = WindowManager::overlayPosToNormalizedMousePos(aDestPos);
	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.dx = aDestPos.x;
	anInput.mi.dy = aDestPos.y;
	anInput.mi.dwFlags = MOUSEEVENTF_MOVEABSOLUTE;
	sTracker.inputs.push_back(anInput);
	sTracker.mouseLookZoneFixTimer = 0;
}


static bool verifyCursorJumpedTo(u16 theHotspotID)
{
	if( !sTracker.mouseJumpAttempted )
		return false;

	// In simulation mode always act as if jump was successful
	#ifdef INPUT_DISPATCHER_SIMULATION_ONLY
	sTracker.mouseJumpVerified = true;
	#endif

	if( !sTracker.mouseJumpVerified )
	{
		static u8 sFailedJumpAttemptsInARow = 0;
		const POINT& aDestPos = WindowManager::hotspotToOverlayPos(
			InputMap::getHotspot(theHotspotID));
		const POINT& aCurrentPos = WindowManager::mouseToOverlayPos();
		if( abs(aCurrentPos.x - aDestPos.x) >= 2 ||
			abs(aCurrentPos.y - aDestPos.y) >= 2 )
		{
			const bool retryJump = ++sFailedJumpAttemptsInARow < 5;
			#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
			debugPrint(
				"InputDispatcher: Cursor jump to %d x %d failed! %s\n",
				aDestPos.x, aDestPos.y,
				retryJump ? "Will attempt jump again" : "Giving up!");
			#endif
			if( retryJump )
				return false;
		}
		sFailedJumpAttemptsInARow = 0;
	}

	// If reached this point, jump was attempted and verified successful
	// Clear flags for next jump attempt
	sTracker.mouseJumpAttempted = false;
	sTracker.mouseJumpVerified = false;

	// Reached jump destination and can update mouse mode accordingly
	if( (theHotspotID == eSpecialHotspot_MouseLookStart &&
		 sTracker.mouseModeWanted == eMouseMode_Look) ||
		(theHotspotID == eSpecialHotspot_MouseHidden &&
		 sTracker.mouseModeWanted == eMouseMode_Hide) ||
		(theHotspotID == eSpecialHotspot_LastCursorPos &&
		 sTracker.mouseModeWanted == eMouseMode_Cursor) )
	{// In position to start desired mouse mode
		sTracker.mouseMode = sTracker.mouseModeWanted;
		return true;
	}
	
	// Prevent motion and wait to see if just a basic jump or a jump-click
	sTracker.mouseMode = eMouseMode_PostJump;
	return true;
}


static void trailMouseToHotspot(u16 theHotspotID)
{
	#ifdef INPUT_DISPATCHER_SIMULATION_ONLY
		jumpMouseToHotspot(theHotspotID);
		return;
	#endif

	// Stop accumulating standard mouse motion during this
	sTracker.mouseVelX = sTracker.mouseVelY = 0;

	static const int kMinTrailTime = 100;
	static const int kMaxTrailTime = 300;
	static int sStartTime = 0, sTrailTime = 0;
	static int sStartPosX, sStartPosY, sTrailDistX, sTrailDistY;

	if( sTracker.mouseInterpolateRestart )
	{
		sTracker.mouseInterpolateRestart = false;
		sStartTime = gAppRunTime - gAppFrameTime;
		const Hotspot& aDestHotspot = InputMap::getHotspot(theHotspotID);
		// Count destination as new valid cursor position
		InputMap::modifyHotspot(eSpecialHotspot_LastCursorPos, aDestHotspot);
		const POINT& aDestPos =
			WindowManager::hotspotToOverlayPos(aDestHotspot);
		const POINT& aCurrPos = WindowManager::mouseToOverlayPos(false);
		if( aCurrPos.x == aDestPos.x &&
			aCurrPos.y == aDestPos.y )
		{// Already at destination - treat as verified jump
			sTracker.mouseJumpInterpolate = false;
			sTracker.mouseJumpAttempted = true;
			sTracker.mouseJumpVerified = true;
			return;
		}
		sStartPosX = aCurrPos.x; sStartPosY = aCurrPos.y;
		sTrailDistX = aDestPos.x - aCurrPos.x;
		sTrailDistY = aDestPos.y - aCurrPos.y;
		const int aDistance =
			sqrt(float(sTrailDistX) * sTrailDistX + sTrailDistY * sTrailDistY);
		sTrailTime = clamp(aDistance, kMinTrailTime, kMaxTrailTime);
	}

	POINT aNewPos = { sStartPosX, sStartPosY };
	const int aTrailTimePassed = min(gAppRunTime - sStartTime, sTrailTime);
	if( aTrailTimePassed >= sTrailTime )
	{
		aNewPos.x += sTrailDistX;
		aNewPos.y += sTrailDistY;
	}
	else
	{
		float anInterpTime = 1.0 - (float(aTrailTimePassed) / sTrailTime);
		anInterpTime = 1.0 - (anInterpTime * anInterpTime);
		aNewPos.x += sTrailDistX * anInterpTime;
		aNewPos.y += sTrailDistY * anInterpTime;
	}

	if( aNewPos.x == sStartPosX + sTrailDistX &&
		aNewPos.y == sStartPosY + sTrailDistY )
	{// Should end up at destination - treat as attempted jump
		sTracker.mouseJumpInterpolate = false;
		sTracker.mouseJumpAttempted = true;
		sTracker.mouseJumpVerified = false;
	}

	aNewPos = WindowManager::overlayPosToNormalizedMousePos(aNewPos);
	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.dx = aNewPos.x;
	anInput.mi.dy = aNewPos.y;
	anInput.mi.dwFlags = MOUSEEVENTF_MOVEABSOLUTE;
	sTracker.inputs.push_back(anInput);
	sTracker.mouseLookZoneFixTimer = 0;
}


static void tryMouseLookZoningFix()
{
	// In the EverQuest Titanium client, if you zone while holding RMB for
	// Mouse Look mode and keep holding it throughout the zoning, there's
	// a minor bug where the RMB is force-released and re-pressed once
	// done zoning, causing a right-click at whatever location the cursor
	// actually is (the cursor continues to move invisibly during Mouse Look
	// and the game just restores it's old position once release RMB).
	// This right-click is thus at an unpredictable location, potentially
	// being on a UI element which will cause a drop out of Mouse Look mode
	// as well as forcing the user to press Esc or left-click somewhere to
	// close the contextual menu that pops up.

	// To fix this, if haven't moved the mouse or character for a while
	// and the mouse has moved away from _MouseLookStart, assume zoning
	// might be happening and, either way, it should be safe to release
	// RMB and re-initialize MouseLook mode at the safe _MouseLookStart pos
	// (at worse will make mouse cursor flicker visible for a frame).
	if( kConfig.mouseLookZoneFixTime == 0 ||
		sTracker.mouseMode != eMouseMode_Look ||
		!sTracker.keysHeldDown.test(VK_RBUTTON) ||
		WindowManager::overlaysAreHidden() )
		return;

	if( sTracker.mouseLookZoneFixTimer - gAppFrameTime <
			kConfig.mouseLookZoneFixTime )
		return;

	sTracker.mouseLookZoneFixTimer = 0;

#ifndef INPUT_DISPATCHER_SIMULATION_ONLY
	DBG_ASSERT(sTracker.nextQueuedKey == 0);
	const Hotspot& aHotspot =
		InputMap::getHotspot(eSpecialHotspot_MouseLookStart);
	const POINT& anExpectedPos =
		WindowManager::hotspotToOverlayPos(aHotspot);
	const POINT& anActualPos =
		WindowManager::mouseToOverlayPos();
	if( anExpectedPos.x != anActualPos.x ||
		anExpectedPos.y != anActualPos.y )
	{
		#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
		debugPrint("Input Dispatcher: Refreshing Mouse Look mode\n");
		#endif
		// Force release RMB
		sTracker.nextQueuedKey = VK_RBUTTON | kVKeyForceReleaseFlag;
		// eMouseMode_Look will cause automatic jump to safe spot
		// and re-press of RMB on a later update
	}
#endif
}


static void lockKeyDownFor(u8 theBaseVKey, u32 theLockTime)
{
	u32& aLockEndTime = sTracker.keysLockedDown.findOrAdd(theBaseVKey, 0);
	aLockEndTime = max(aLockEndTime, gAppRunTime + theLockTime);
}


static bool keyIsLockedDown(u8 theBaseVKey)
{
	VectorMap<u8, u32>::iterator itr =
		sTracker.keysLockedDown.find(theBaseVKey);
	if( itr == sTracker.keysLockedDown.end() )
		return false;
	if( gAppRunTime < itr->second )
		return true;
	sTracker.keysLockedDown.erase(itr);
	return false;
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

	// May not be allowed to release the given key yet
	if( !down && keyIsLockedDown(u8(theKey)) )
		return eResult_NotAllowed;

	Input anInput;
	u32 aLockDownTime = kConfig.baseKeyReleaseLockTime;
	switch(theKey)
	{
	case kVKeyModKeyOnlyBase:
		// Just a dummy base key used when want to press a mod key by itself
		// Don't actually press it, but act like did
		sTracker.keysHeldDown.set(theKey, down);
		return eResult_Ok;
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
	if( down ) lockKeyDownFor(u8(theKey), aLockDownTime);

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
			lockKeyDownFor(VK_SHIFT, aLockDownTime);
		if( sTracker.keysHeldDown.test(VK_CONTROL) )
			lockKeyDownFor(VK_CONTROL, aLockDownTime);
		if( sTracker.keysHeldDown.test(VK_MENU) )
			lockKeyDownFor(VK_MENU, aLockDownTime);
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
	if( isMouseButton(aVKey) && !requiredModKeysAreAlreadyHeld(aVKey) )
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
			case MOUSEEVENTF_MOVE:
				#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_MOUSE_MOTION
				{
					siPrint("Mouse vel: %dx x %dy\n",
						anInput.mi.dx, anInput.mi.dy);
				}
				#endif
				break;
			case MOUSEEVENTF_WHEEL:
				#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_MOUSE_MOTION
				{
					siPrint("Mouse wheel: %.02f steps\n",
						float(-int(anInput.mi.mouseData)) / WHEEL_DELTA);
				}
				#endif
				break;
			case MOUSEEVENTF_MOVEABSOLUTE:
				if( !sTracker.mouseJumpToHotspot ||
					!sTracker.mouseJumpInterpolate )
				{
					POINT aPos = { anInput.mi.dx, anInput.mi.dy };
					aPos = WindowManager::normalizedMouseToOverlayPos(aPos);
					siPrint("Jumped cursor to %dx x %dy\n",
						aPos.x, aPos.y);
				}
				#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_MOUSE_MOTION
				else
				{
					POINT anOldPos = WindowManager::mouseToOverlayPos();
					POINT aPos = { anInput.mi.dx, anInput.mi.dy };
					aPos = WindowManager::normalizedMouseToOverlayPos(aPos);
					siPrint("Mouse shifted by: %dx x %dy to reach %d x %d\n",
						aPos.x - anOldPos.x, aPos.y - anOldPos.y,
						aPos.x, aPos.y);
				}
				#endif
				break;
			}
		}
		else if( anInput.type == INPUT_KEYBOARD )
		{
			siPrint("%s %s\n",
				virtualKeyToName(anInput.ki.wVk).c_str(),
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
	sTracker.mouseModeWanted = eMouseMode_Cursor;
	if( sTracker.mouseMode != eMouseMode_Cursor )
		jumpMouseToHotspot(eSpecialHotspot_LastCursorPos);
	flushInputVector();
}


void forceReleaseHeldKeys()
{
	sTracker.keysLockedDown.clear();
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		setKeyDown(u16(aVKey), false);
	}
	if( sTracker.mouseMode != eMouseMode_Cursor )
		jumpMouseToHotspot(eSpecialHotspot_LastCursorPos);
	flushInputVector();
}


void update()
{
	// Update timers
	// -------------
	if( sTracker.queuePauseTime > 0 )
		sTracker.queuePauseTime -= gAppFrameTime;
	sTracker.mouseLookZoneFixTimer += gAppFrameTime;


	// Update mouse mode
	// -----------------
	if( sTracker.mouseJumpToHotspot &&
		verifyCursorJumpedTo(sTracker.mouseJumpToHotspot) )
	{// Can clear .mouseJumpToHotspot now that verified it worked
		sTracker.mouseJumpToHotspot = 0;
		sTracker.mouseJumpInterpolate = false;
	}
	if( !sTracker.nextQueuedKey &&
		!sTracker.backupQueuedKey &&
		!sTracker.mouseJumpToHotspot &&
		!sTracker.mouseJumpQueued &&
		(sTracker.currTaskProgress == 0 || sTracker.queuePauseTime > 0) )
	{// No tasks in progress that mouse mode change could interfere with

		// If mouse was jumped but didn't click during action just completed,
		// no need for any further action - just treat as new cursor mode pos
		if( sTracker.mouseMode == eMouseMode_PostJump )
			sTracker.mouseMode = eMouseMode_Cursor;
		// If not holding RMB in _Look mode, it must have been force-released
		if( sTracker.mouseMode == eMouseMode_Look &&
			sTracker.mouseModeWanted == eMouseMode_Look &&
			!sTracker.keysHeldDown.test(VK_RBUTTON) )
		{
			// Should force following code to re-jump and r-click again
			sTracker.mouseMode = eMouseMode_Default;
		}
		if( sTracker.mouseMode != sTracker.mouseModeWanted )
		{
			// Not in the middle of another task, so transition mouse mode
			switch(sTracker.mouseModeWanted)
			{
			case eMouseMode_Cursor:
				// Restore last known normal cursor position
				sTracker.mouseJumpToHotspot = eSpecialHotspot_LastCursorPos;
				sTracker.mouseJumpInterpolate = false;
				break;
			case eMouseMode_Look:
				if( sTracker.mouseMode == eMouseMode_JumpClicked )
				{// Give one more update to process queue before resuming
					// (in case have multiple jump-clicks queued in a row)
					sTracker.mouseMode = eMouseMode_Default;
				}
				else if( !WindowManager::overlaysAreHidden() )
				{// Jump curser to safe spot for initial right-click
					sTracker.mouseJumpToHotspot =
						eSpecialHotspot_MouseLookStart;
					sTracker.mouseJumpInterpolate = false;
					// Begin holding down right mouse button
					sTracker.nextQueuedKey = VK_RBUTTON | kVKeyHoldFlag;
					sTracker.mouseLookZoneFixTimer = 0;
				}
				break;
			case eMouseMode_Hide:
				// Can't actually hide cursor without messing with target app
				// (or using _Look which can affect undesired side effects)
				// so just move it out of the way (bottom corner usually)
				if( !WindowManager::overlaysAreHidden() )
				{
					sTracker.mouseJumpToHotspot = eSpecialHotspot_MouseHidden;
					sTracker.mouseJumpInterpolate = false;
				}
				break;
			default:
				DBG_ASSERT(false && "Invalid sTracker.mouseModeWanted value!");
			}
		}
		tryMouseLookZoningFix();
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
			DBG_ASSERT(aCmd.vKey != 0);
			DBG_ASSERT((aCmd.vKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
			sTracker.keysWantDown.findOrAdd(aCmd.vKey).depth += 1;
			break;
		case eCmdType_ReleaseKey:
			// Do nothing if key was never requested down anyway
			// (or was already force-released by kVKeyForceReleaseFlag)
			DBG_ASSERT(aCmd.vKey != 0);
			DBG_ASSERT((aCmd.vKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
			aKeyWantDownItr = sTracker.keysWantDown.find(aCmd.vKey);
			if( aKeyWantDownItr == sTracker.keysWantDown.end() )
				break;
			if( !aKeyWantDownItr->second.pressed )
			{// Key has yet to be pressed in the first place
				// Abort queue loop and wait for it to be pressed first,
				// unless this is a past-due event, in which case just
				// forget ever wanted the key pressed in the first place
				if( !taskIsPastDue )
				{
					sTracker.queuePauseTime = 1; 
					aTaskResult = eResult_Incomplete;
				}
				else if( --aKeyWantDownItr->second.depth <= 0 )
				{
					sTracker.keysWantDown.erase(aKeyWantDownItr);
				}
			}
			else
			{// Key has been pressed, and should release even if past due

				// Attempt to release now and move on to next queue item
				// If can't, set releasing it as the queued key event
				if( !tryQuickReleaseHeldKey(aKeyWantDownItr) )
				{
					--aKeyWantDownItr->second.depth;
					sTracker.nextQueuedKey = aCmd.vKey | kVKeyReleaseFlag;
				}
			}
			break;
		case eCmdType_TapKey:
			if( !taskIsPastDue )
				sTracker.nextQueuedKey = aCmd.vKey;
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
		case eCmdType_MoveMouseToHotspot:
			if( sTracker.mouseJumpToHotspot &&
				!sTracker.mouseJumpInterpolate )
			{// Finish instant jump first
				sTracker.queuePauseTime = 1; 
				aTaskResult = eResult_Incomplete;
			}
			else if( !taskIsPastDue )
			{
				sTracker.mouseInterpolateRestart =
					!sTracker.mouseJumpInterpolate ||
					aCmd.hotspotID != sTracker.mouseJumpToHotspot;
				sTracker.mouseJumpInterpolate = true;
				sTracker.mouseJumpToHotspot = aCmd.hotspotID;
			}
			break;
		case eCmdType_MoveMouseToMenuItem:
			if( sTracker.mouseJumpToHotspot &&
				!sTracker.mouseJumpInterpolate )
			{// Finish instant jump first
				sTracker.queuePauseTime = 1; 
				aTaskResult = eResult_Incomplete;
			}
			else if( !taskIsPastDue )
			{
				sTracker.mouseInterpolateRestart =
					!sTracker.mouseJumpInterpolate ||
					aCmd.hotspotID != sTracker.mouseJumpToHotspot;
				const Hotspot oldMenuHotspot =
					InputMap::getHotspot(eSpecialHotspot_MenuItemPos);
				InputMap::modifyHotspot(eSpecialHotspot_MenuItemPos,
					WindowManager::hotspotForMenuItem(
						aCmd.menuID, aCmd.menuItemIdx));
				const Hotspot& newMenuHotspot =
					InputMap::getHotspot(eSpecialHotspot_MenuItemPos);
				if( !sTracker.mouseInterpolateRestart &&
					!(oldMenuHotspot == newMenuHotspot) )
				{// Same hotspot but it changed location, so restart interp
					sTracker.mouseInterpolateRestart = true;
				}
				sTracker.mouseJumpInterpolate = true;
				sTracker.mouseJumpToHotspot = eSpecialHotspot_MenuItemPos;
				if( aCmd.andClick )
					sTracker.nextQueuedKey = VK_LBUTTON;
			}
			break;
		}
		if( aTaskResult == eResult_TaskCompleted )
		{
			sTracker.currTaskProgress = 0;
			sTracker.queue.pop_front();
			if( sTracker.queue.empty() )
				sTracker.mouseJumpQueued = false;
		}
	}


	// Process keysWantDown
	// --------------------
	BitArray<0xFF> aDesiredKeysDown; aDesiredKeysDown.reset();
	if( sTracker.mouseMode == eMouseMode_Look &&
		sTracker.keysHeldDown.test(VK_RBUTTON) )
	{// Keep holding right mouse button while eMouseMode_Look is active
		aDesiredKeysDown.set(VK_RBUTTON);
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

		if( isMouseButton(aVKey) &&
			(sTracker.mouseMode == eMouseMode_Hide ||
			 sTracker.mouseMode == eMouseMode_PostJump ||
			 sTracker.mouseMode == eMouseMode_JumpClicked ||
			 sTracker.mouseJumpToHotspot) )
		{// Mouse not in a state where can hold a mouse button
			hasNonPressedKeyThatWantsHeldDown = true;
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

		if( requiredModKeysAreAlreadyHeld(aVKey) &&
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
	// modifier keys that any combo-keys wanted held down, as well as
	// any modifier keys assigned directly using kVKeyModKeyOnlyBase
	if( !sTracker.nextQueuedKey && !hasNonPressedKeyThatWantsHeldDown )
	{
		aDesiredKeysDown.set(VK_SHIFT,
			!!(aPressedKeysDesiredMods & kVKeyShiftFlag));
		aDesiredKeysDown.set(VK_CONTROL,
			!!(aPressedKeysDesiredMods & kVKeyCtrlFlag));
		aDesiredKeysDown.set(VK_MENU,
			!!(aPressedKeysDesiredMods& kVKeyAltFlag));
	}


	// Prepare for queued event (mouse jump and/or nextQueuedKey)
	// ----------------------------------------------------------
	bool readyForQueuedKey = sTracker.nextQueuedKey != 0;
	bool readyForMouseJump = sTracker.mouseJumpToHotspot != 0;
	if( readyForMouseJump )
	{
		// No mouse button should be held down during a jump
		aDesiredKeysDown.reset(VK_LBUTTON);
		aDesiredKeysDown.reset(VK_MBUTTON);
		aDesiredKeysDown.reset(VK_RBUTTON);
		if( sTracker.keysHeldDown.test(VK_LBUTTON) ||
			sTracker.keysHeldDown.test(VK_MBUTTON) ||
			sTracker.keysHeldDown.test(VK_RBUTTON) )
		{
			readyForMouseJump = false;
		}
		// Don't allow mouse clicks until done with mouse movement
		if( isMouseButton(sTracker.nextQueuedKey) )
			readyForQueuedKey = false;
	}
	if( readyForQueuedKey )
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
		readyForQueuedKey =
			requiredModKeysAreAlreadyHeld(sTracker.nextQueuedKey);
		// Make sure base key is in opposite of desired pressed state
		if( !forced )
		{
			aDesiredKeysDown.set(aBaseVKey, !press);
			if( sTracker.keysHeldDown.test(aBaseVKey) == press )
				readyForQueuedKey = false;
		}
		// Skip mouse clicks entirely while mouse is hidden
		if( sTracker.mouseMode == eMouseMode_Hide &&
			isMouseButton(aBaseVKey) && press )
		{
			sTracker.nextQueuedKey = 0;
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
		but this will obviously make the controls feel less responsive.
	*/


	// Apply normal mouse motion
	// -------------------------
	if( !sTracker.mouseJumpToHotspot )
	{
		if( sTracker.mouseMode == eMouseMode_Cursor )
		{// Track cursor position changes in cursor mode
			POINT aCurrPos = WindowManager::mouseToOverlayPos(true);
			aCurrPos.x += sTracker.mouseVelX;
			aCurrPos.y += sTracker.mouseVelY;
			InputMap::modifyHotspot(
				eSpecialHotspot_LastCursorPos,
				WindowManager::overlayPosToHotspot(aCurrPos));
		}
		if( sTracker.mouseVelX || sTracker.mouseVelY )
		{
			offsetMousePos(sTracker.mouseVelX, sTracker.mouseVelY);
			sTracker.mouseVelX = sTracker.mouseVelY = 0;
		}
	}
	// Return speed from digital mouse acceleration back to 0 over time
	sTracker.mouseDigitalVel = max(0,
		sTracker.mouseDigitalVel -
		kConfig.mouseDPadAccel * 3 * gAppFrameTime);


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


	// Send queued event (cursor jump OR queued key)
	// ---------------------------------------------
	if( readyForMouseJump )
	{
		if( sTracker.mouseJumpInterpolate )
			trailMouseToHotspot(sTracker.mouseJumpToHotspot);
		else
			jumpMouseToHotspot(sTracker.mouseJumpToHotspot);
		// mouseJumpToHotspot will be reset once jump is verified next update
	}
	if( readyForQueuedKey )
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
				if( sTracker.mouseMode == eMouseMode_PostJump )
				{// If jumped first then clicked, next restore previous pos
					sTracker.mouseMode = eMouseMode_JumpClicked;
				}
			}
		}
	}
	sTracker.typingChatBoxString = false;


	// Dispatch input to system
	// ------------------------
	flushInputVector();
}


void sendKeyCommand(const Command& theCommand)
{
	// These values only valid for certain command types
	const u16 aVKey = theCommand.vKey;
	const u8 aBaseVKey = u8(aVKey & kVKeyMask);

	switch(theCommand.type)
	{
	case eCmdType_Empty:
	case eCmdType_DoNothing:
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
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_VKeySequence:
		for(const char* c = theCommand.string; *c != '\0'; ++c)
		{
			if( *c == VK_SELECT )
			{
				sTracker.mouseJumpQueued = true;
				break;
			}
		}
		// fall through
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		sTracker.queue.push_back(theCommand);
		break;
	default:
		DBG_ASSERT(false && "Invalid command type for sendkeyCommand()!");
	}
}


void setMouseMode(EMouseMode theMouseMode)
{
	DBG_ASSERT(theMouseMode <= eMouseMode_Hide);
	if( theMouseMode == eMouseMode_Default )
		theMouseMode = eMouseMode_Cursor;
	sTracker.mouseModeWanted = theMouseMode;
}


void moveMouse(int dx, int dy, bool digital)
{
	const bool kMouseLookSpeed =
		sTracker.mouseModeWanted == eMouseMode_Look ||
		sTracker.keysHeldDown.test(VK_RBUTTON);

	// Get magnitude of desired mouse motion in 0 to 1.0 range
	double aMagnitude = std::sqrt(double(dx) * dx + dy * dy) / 255.0;

	// Apply deadzone and saturation to magnitude
	const double kDeadZone = kMouseLookSpeed
		? kConfig.mouseLookDeadzone : kConfig.cursorDeadzone;
	if( aMagnitude <= kDeadZone )
		return;
	aMagnitude -= kDeadZone;
	const double kRange = kMouseLookSpeed
		? kConfig.mouseLookRange : kConfig.cursorRange;
	aMagnitude = min(aMagnitude / kRange, 1.0);

	// Apply adjustments to allow for low-speed fine control
	if( digital )
	{// Apply acceleration to magnitude
		sTracker.mouseDigitalVel = min(
			kMouseMaxDigitalVel,
			sTracker.mouseDigitalVel +
				kConfig.mouseDPadAccel * 4 * gAppFrameTime);
		aMagnitude *= double(sTracker.mouseDigitalVel) / kMouseMaxDigitalVel;
	}
	else if( aMagnitude < 1.0 )
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
	sTracker.mouseVelX += dx / kMouseToPixelDivisor;

	if( dy < 0 )
		sMouseYSubPixel = -((-dy) % kMouseToPixelDivisor);
	else
		sMouseYSubPixel = dy % kMouseToPixelDivisor;
	sTracker.mouseVelY +=dy / kMouseToPixelDivisor;
}


void moveMouseTo(const Command& theCommand)
{
	switch(theCommand.type)
	{
	case eCmdType_MoveMouseToHotspot:
	case eCmdType_MoveMouseToMenuItem:
		sTracker.queue.push_back(theCommand);
		sTracker.mouseJumpQueued = true;
		break;
	default:
		DBG_ASSERT(false && "Invalid command type for moveMouseTo()!");
	}
}


void scrollMouseWheel(int dy, bool digital, bool stepped)
{
	// Get magnitude of desired mouse motion in 0 to 1.0 range
	double aMagnitude = abs(dy) / 255.0;

	// Apply deadzone and saturation to dy
	if( aMagnitude <= kConfig.mouseWheelDeadzone )
		return;
	aMagnitude -= kConfig.mouseWheelDeadzone;
	aMagnitude = min(aMagnitude / kConfig.mouseWheelRange, 1.0);

	// Apply adjustments to allow for low-speed fine control
	if( digital )
	{// Apply acceleration to magnitude
		sTracker.mouseDigitalVel = min(
			kMouseMaxDigitalVel,
			sTracker.mouseDigitalVel +
				kConfig.mouseDPadAccel * 4 * gAppFrameTime);
		aMagnitude *= double(sTracker.mouseDigitalVel) / kMouseMaxDigitalVel;
	}
	else if( aMagnitude < 1.0 )
	{// Apply exponential easing curve to magnitude
		aMagnitude = std::pow(2, 10 * (aMagnitude - 1));
	}

	// Convert back into integer dy w/ 32,768 range
	dy = dy < 0 ? (-32768.0 * aMagnitude) : (32768.0 * aMagnitude);

	// Apply speed setting
	dy = dy * kConfig.mouseWheelSpeed / kMouseMaxSpeed * gAppFrameTime;

	// Use same logic as shiftMouseCursor() for fractional speeds
	static int sMouseWheelSubPixel = 0;
	dy += sMouseWheelSubPixel;
	if( dy < 0 )
		sMouseWheelSubPixel = -((-dy) % kMouseToPixelDivisor);
	else
		sMouseWheelSubPixel = dy % kMouseToPixelDivisor;
	dy = dy / kMouseToPixelDivisor;
	if( !dy )
		return;

	if( stepped )
	{
		static int sMouseWheelDeltaAcc = 0;
		sMouseWheelDeltaAcc += dy;
		dy = sMouseWheelDeltaAcc / WHEEL_DELTA;
		if( !dy )
			return;
		dy *= WHEEL_DELTA;
		sMouseWheelDeltaAcc -= dy;
	}

	// Send the mouse wheel movement to the OS now
	// (no need to queue as shouldn't interfere with anything else)
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

	// Calculate which movement actions, if any, should now apply
	BitArray<eSpecialKey_MoveNum> moveKeysWantDown;
	moveKeysWantDown.reset();
	moveKeysWantDown.set(eSpecialKey_TurnL - eSpecialKey_FirstMove,
		applyMoveTurn &&
			(aTurnAngle < M_PI * -0.625 || aTurnAngle > M_PI * 0.625));

	moveKeysWantDown.set(eSpecialKey_TurnR - eSpecialKey_FirstMove,
		applyMoveTurn &&
			aTurnAngle > M_PI * -0.375 && aTurnAngle < M_PI * 0.375);

	moveKeysWantDown.set(eSpecialKey_StrafeL - eSpecialKey_FirstMove,
		applyMoveStrafe &&
		(aStrafeAngle < M_PI * -0.625 || aStrafeAngle > M_PI * 0.625));

	moveKeysWantDown.set(eSpecialKey_StrafeR - eSpecialKey_FirstMove,
		applyMoveStrafe &&
			aStrafeAngle > M_PI * -0.375 && aStrafeAngle < M_PI * 0.375);

	// Effects of left/right move keys can change while in Mouse Look mode,
	// so reset Mouse Look zone fix timer whenever any of them are pressed
	if( moveKeysWantDown.any() )
		sTracker.mouseLookZoneFixTimer = 0;

	// For move forward/back, use the virtual stick that had the greatest X
	// motion in order to make sure a proper circular deadzone is used.
	moveKeysWantDown.set(
		eSpecialKey_MoveF - eSpecialKey_FirstMove,
		(applyMoveTurn && abs(turn) >= abs(strafe) &&
			aTurnAngle > M_PI * 0.125 && aTurnAngle < M_PI * 0.875) ||
		(applyMoveStrafe && abs(strafe) >= abs(turn) &&
			aStrafeAngle > M_PI * 0.125 && aStrafeAngle < M_PI * 0.875));

	moveKeysWantDown.set(
		eSpecialKey_MoveB - eSpecialKey_FirstMove,
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
			aCmd.vKey = InputMap::keyForSpecialAction(
				ESpecialKey(aWantedKey + eSpecialKey_FirstMove));
			if( aCmd.vKey != 0 )
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
			aCmd.vKey = InputMap::keyForSpecialAction(
				ESpecialKey(aHeldKey + eSpecialKey_FirstMove));
			if( aCmd.vKey != 0 )
				sendKeyCommand(aCmd);
			sTracker.moveKeysHeld.reset(aHeldKey);
		}
	}
}

} // InputDispatcher
