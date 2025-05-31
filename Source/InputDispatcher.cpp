//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputDispatcher.h"

#include "InputMap.h"
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
kMouseMaxAccelVel = 32768,
kVKeyModsMask = 0x3F00,
kVKeyHoldFlag = 0x4000, // bit unused by VkKeyScan()
kVKeyReleaseFlag = 0x8000, // bit unused by VkKeyScan()
kVKeyForceReleaseFlag = kVKeyHoldFlag | kVKeyReleaseFlag,
MOUSEEVENTF_MOVEABSOLUTE =
	MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK,
};

enum EAutoRunMode
{
	eAutoRunMode_Off,
	eAutoRunMode_Queued,
	eAutoRunMode_Started,
	eAutoRunMode_Active,
	eAutoRunMode_StartLockX,
	eAutoRunMode_StartLockY,
	eAutoRunMode_StartLockXY,
	eAutoRunMode_LockedX,
	eAutoRunMode_LockedY,
	eAutoRunMode_LockedXY,
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct Input : public INPUT
{ Input() { ZeroMemory(this, sizeof(INPUT)); } };

struct ZERO_INIT(DispatchTask)
{ Command cmd; int progress; int queuedTime; bool hasJump; bool slow; };

struct ZERO_INIT(KeyWantDownStatus)
{ s16 depth : 8; s16 queued : 7; u16 pressed : 1; };
typedef VectorMap<u16, KeyWantDownStatus> KeysWantDownMap;


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct ZERO_INIT(Config)
{
	std::vector<u8> safeAsyncKeys;
	double cursorDeadzone;
	double cursorRange;
	double cursorCurve;
	double cursorAccel;
	double mouseLookDeadzone;
	double mouseLookRange;
	double mouseLookCurve;
	double mouseLookAccel;
	double mouseWheelDeadzone;
	double mouseWheelRange;
	double moveDeadzone;
	double moveLookDeadzone;
	double moveLookRange;
	double moveStraightBias;
	int maxTaskQueuedTime; // tasks older than this in queue are skipped
	int chatBoxPostFirstKeyDelay;
	int chatBoxPostEnterDelay;
	int cursorXSpeed;
	int cursorYSpeed;
	int mouseLookXSpeed;
	int mouseLookYSpeed;
	int moveLookSpeed;
	int mouseWheelSpeed;
	int mouseLookAutoRestoreTime;
	int offsetHotspotDist;
	int baseKeyReleaseLockTime;
	int mouseClickLockTime;
	int mouseReClickLockTime;
	int mouseLookMoveLockTime;
	int minModKeyChangeTime;
	int mouseJumpDelayTime;
	int cancelAutoRunDeadzone;
	int mouseDPadAccel;
	bool useScanCodes;

	void load()
	{
		const double aCursorSpeedMult = sqrt(GetSystemMetrics(SM_CYSCREEN) / 720.0);
		maxTaskQueuedTime = Profile::getInt("System", "MaxKeyQueueTime", 1000);
		chatBoxPostFirstKeyDelay = Profile::getInt("System", "ChatBoxStartDelay", 0);
		chatBoxPostEnterDelay = Profile::getInt("System", "ChatBoxEndDelay", 0);
		baseKeyReleaseLockTime = max(0, Profile::getInt("System", "MinKeyHoldTime", 20));
		minModKeyChangeTime = max(0, Profile::getInt("System", "MinModKeyChangeTime", 50));
		mouseClickLockTime = max(0, Profile::getInt("Mouse", "MinButtonClickTime", 25));
		mouseReClickLockTime = max(0, Profile::getInt("Mouse", "MinReClickTime", 0));
		mouseJumpDelayTime = max(0, Profile::getInt("Mouse", "JumpDelayTime", 25));
		mouseLookMoveLockTime = max(0, Profile::getInt("Mouse", "CameraMoveStartDelay", 25));
		useScanCodes = Profile::getBool("System", "UseScanCodes", false);
		cursorXSpeed = cursorYSpeed = Profile::getInt("Mouse", "CursorSpeed", 100);
		cursorXSpeed = int(Profile::getInt("Mouse", "CursorXSpeed", cursorXSpeed) * aCursorSpeedMult);
		cursorYSpeed = int(Profile::getInt("Mouse", "CursorYSpeed", cursorYSpeed) * aCursorSpeedMult);
		cursorDeadzone = clamp(Profile::getInt("Mouse", "CursorDeadzone", 25), 0, 100) / 100.0;
		cursorRange = clamp(Profile::getInt("Mouse", "CursorSaturation", 100), cursorDeadzone, 100) / 100.0;
		cursorRange = max(0.0, cursorRange - cursorDeadzone);
		cursorCurve = max(Profile::getFloat("Mouse", "CursorResponseCurve", 1.0), 0.1);
		cursorAccel = Profile::getInt("Mouse", "CursorAccel", 33) * 4.0 / kMouseMaxAccelVel;
		mouseLookXSpeed = mouseLookYSpeed = Profile::getInt("Mouse", "CameraSpeed", 100);
		mouseLookXSpeed = Profile::getInt("Mouse", "CameraXSpeed", mouseLookXSpeed);
		mouseLookYSpeed = Profile::getInt("Mouse", "CameraYSpeed", mouseLookYSpeed);
		moveLookSpeed = Profile::getInt("Mouse", "MoveLookSpeed", 25);
		mouseLookDeadzone = clamp(Profile::getInt("Mouse", "CameraDeadzone", 25), 0, 100) / 100.0;
		mouseLookRange = clamp(Profile::getInt("Mouse", "CameraSaturation", 100), mouseLookDeadzone, 100) / 100.0;
		mouseLookRange = max(0.0, mouseLookRange - mouseLookDeadzone);
		mouseLookCurve = max(Profile::getFloat("Mouse", "CameraResponseCurve", 1.0), 0.1);
		mouseLookAccel = Profile::getInt("Mouse", "CameraAccel", 0) * 4.0 /  kMouseMaxAccelVel;
		moveLookDeadzone = clamp(Profile::getInt("Mouse", "CameraDeadzone", 25), 0, 100) / 100.0;
		moveLookRange = clamp(Profile::getInt("Mouse", "CameraSaturation", 100), moveLookDeadzone, 100) / 100.0;
		moveLookRange = max(0.0, moveLookRange - moveLookDeadzone);
		mouseDPadAccel = clamp(Profile::getInt("Mouse", "DigitalAccel", 50), 0, 255);
		mouseWheelDeadzone = clamp(Profile::getInt("Mouse", "MouseWheelDeadzone", 25), 0, 100) / 100.0;
		mouseWheelRange = clamp(Profile::getInt("Mouse", "MouseWheelSaturation", 100), mouseWheelDeadzone, 100) / 100.0;
		mouseWheelRange = max(0.0, mouseWheelRange - mouseWheelDeadzone);
		mouseWheelSpeed = Profile::getInt("Mouse", "MouseWheelSpeed", 255);
		moveDeadzone = clamp(Profile::getInt("Gamepad", "MoveCharacterThreshold", 50), 0, 100) / 100.0;
		moveStraightBias = clamp(Profile::getInt("Gamepad", "MoveStraightBias", 50), 0, 100) / 100.0;
		cancelAutoRunDeadzone = clamp(int(Profile::getInt("Gamepad", "CancelAutoRunThreshold", 80) / 100.0 * 255.0), 0, 255);
		mouseLookAutoRestoreTime = Profile::getInt("System", "MouseLookAutoRestoreTime");
		offsetHotspotDist = max(0, Profile::getInt("Mouse", "DefaultHotspotDistance"));

		std::string aString = Profile::getStr("System", "SafeAsyncKeys");
		if( !aString.empty() )
		{
			std::vector<std::string> aParsedString;
			sanitizeSentence(aString, aParsedString);
			for( size_t i = 0; i < aParsedString.size(); ++i)
			{
				u8 aVKey = dropTo<u8>(keyNameToVirtualKey(aParsedString[i]));
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
	// Initial buffer size must be a power of 2!
	DispatchQueue() :
	  mBuffer(16), mHead(), mTail(), mMouseJumpQueueCount()
	{
	}

	void push_back(const Command& theCommand)
	{
		confirmCanFitOneMore();
		mBuffer[mTail].cmd = theCommand;
		setDataForNewTask(mBuffer[mTail]);
		mTail = (mTail + 1) & dropTo<u32>(mBuffer.size() - 1);
	}


	void push_front(const Command& theCommand)
	{
		// Used for embedded commands inside vkey sequences to temporarily
		// jump them to the front of the queue, so properties set differently
		DBG_ASSERT(!empty());
		confirmCanFitOneMore();
		const bool flagAsSlow = mBuffer[mHead].slow;
		mHead = (mHead - 1) & dropTo<u32>(mBuffer.size() - 1);
		mBuffer[mHead].cmd = theCommand;
		mBuffer[mHead].progress = 0;
		mBuffer[mHead].queuedTime = 0xFFFFFFFF;
		mBuffer[mHead].hasJump = false;
		mBuffer[mHead].slow = flagAsSlow;
	}


	void pop_front()
	{
		DBG_ASSERT(!empty());

		if( mBuffer[mHead].hasJump )
			--mMouseJumpQueueCount;
		mHead = (mHead + 1) & dropTo<u32>(mBuffer.size() - 1);
		DBG_ASSERT(!empty() || mMouseJumpQueueCount == 0);
	}


	DispatchTask front()
	{
		DBG_ASSERT(!empty());

		return mBuffer[mHead];
	}


	bool mouseJumpQueued() const
	{
		return mMouseJumpQueueCount > 0;
	}


	void setCurrTaskProgress(u32 theProgress)
	{
		DBG_ASSERT(!empty());
		mBuffer[mHead].progress = theProgress;
	}


	bool hasFastTaskReady()
	{
		if( empty() )
			return false;
		
		if( !mBuffer[mHead].slow )
			return true;

		bool foundFastTask = false;
		const u32 aWrapMask = dropTo<u32>(mBuffer.size()) - 1;
		u32 idx = mHead;
		for(; idx != mTail; idx = (idx + 1) & aWrapMask)
		{
			if( !mBuffer[idx].slow )
			{
				foundFastTask = true;
				break;
			}
		}
		if( !foundFastTask )
			return false;

		// Shift tasks between mHead and idx to move fast task to front
		const DispatchTask aFastTask = mBuffer[idx];
		for(;idx != mHead; idx = (idx - 1) & aWrapMask)
			mBuffer[idx] = mBuffer[(idx - 1) & aWrapMask];
		mBuffer[mHead] = aFastTask;

		return true;
	}


	bool empty() const
	{
		return mHead == mTail;
	}


private:
	void confirmCanFitOneMore()
	{
		if( ((mTail + 1) & dropTo<u32>(mBuffer.size() - 1)) == mHead )
		{// Adding one means head == tail which would report empty()!
			// Resize by doubling to keep power-of-2 size
			std::vector<DispatchTask> newBuffer(mBuffer.size() * 2);

			for(u32 i = 0, end = dropTo<u32>(mBuffer.size()) - 1; i < end; ++i)
				newBuffer[i] = mBuffer[(mHead + i) & u32(mBuffer.size() - 1)];

			mHead = 0;
			mTail = dropTo<u32>(mBuffer.size() - 1);
			swap(mBuffer, newBuffer);
		}
	}

	void setDataForNewTask(DispatchTask& theTask)
	{
		mBuffer[mTail].progress = 0;
		mBuffer[mTail].queuedTime = gAppRunTime;			
		int aCurrJumpHotspotID = 0;
		int aFinalJumpHotspotID = 0;
		Hotspot aJumpDest;
		switch(theTask.cmd.type)
		{
		case eCmdType_VKeySequence:
			scanVKeySeqForFlags(
				InputMap::cmdVKeySeq(theTask.cmd),
				aCurrJumpHotspotID, aFinalJumpHotspotID,
				theTask.hasJump, theTask.slow);
			if( aCurrJumpHotspotID )
				aFinalJumpHotspotID = aCurrJumpHotspotID;
			if( theTask.hasJump )
			{
				++mMouseJumpQueueCount;
				if( aFinalJumpHotspotID )
				{// Assign _LastCursorPos now as source point for next jump
					InputMap::modifyHotspot(
						eSpecialHotspot_LastCursorPos,
						InputMap::getHotspot(aFinalJumpHotspotID));
				}
			}
			break;
		case eCmdType_ChatBoxString:
			theTask.hasJump = false;
			theTask.slow = true;
			break;
		case eCmdType_MoveMouseToHotspot:
		case eCmdType_MouseClickAtHotspot:
		case eCmdType_MoveMouseToMenuItem:
		case eCmdType_MoveMouseToOffset:
			theTask.hasJump = true;
			theTask.slow = true;
			++mMouseJumpQueueCount;
			// Assign _LastCursorPos now as source point for next jump
			aJumpDest.x = theTask.cmd.hotspot.x;
			aJumpDest.y = theTask.cmd.hotspot.y;
			InputMap::modifyHotspot(
				eSpecialHotspot_LastCursorPos, aJumpDest);
			break;
		default:
			theTask.hasJump = false;
			theTask.slow = false;
			break;
		}
	}

	static void scanVKeySeqForFlags(
		const u8* theVKeySeq,
		int& theCurrJumpHotspotID,
		int& theFinalJumpHotspotID,
		bool& hasJump,
		bool& isSlow)
	{
		DBG_ASSERT(theVKeySeq);
		// Hotspots that are clicked on immediately do not count as final
		// destination hotspot since dispatcher will return cursor to previous
		// position in those cases.
		for(const u8* c = theVKeySeq; *c != '\0'; ++c)
		{
			switch(*c)
			{
			case kVKeyMouseJump:
				hasJump = true;
				isSlow = true;
				++c; DBG_ASSERT(*c != '\0');
				theCurrJumpHotspotID = (*c & 0x7F) << 7U;
				++c; DBG_ASSERT(*c != '\0');
				theCurrJumpHotspotID |= (*c & 0x7F);
				break;
			case VK_LBUTTON: case VK_MBUTTON: case VK_RBUTTON:
				// Last hotspot jumped to no longer matters if it is clicked
				theCurrJumpHotspotID = 0;
				break;
			case VK_PAUSE:
				isSlow = true;
				if( theCurrJumpHotspotID )
					theFinalJumpHotspotID = theCurrJumpHotspotID;
				break;
			case kVKeyTriggerKeyBind:
				{
					++c; DBG_ASSERT(*c != '\0');
					int aKeyBindID = (*c & 0x7F) << 7U;
					++c; DBG_ASSERT(*c != '\0');
					aKeyBindID |= (*c & 0x7F);
					const Command& aKeyBindCmd =
						InputMap::keyBindCommand(aKeyBindID);
					switch(aKeyBindCmd.type)
					{
					case eCmdType_TapKey:
						if( aKeyBindCmd.vKey == VK_LBUTTON ||
							aKeyBindCmd.vKey == VK_MBUTTON ||
							aKeyBindCmd.vKey == VK_RBUTTON )
						{ theCurrJumpHotspotID = 0; }
						break;
					case eCmdType_ChatBoxString:
						isSlow = true;
						if( theCurrJumpHotspotID )
							theFinalJumpHotspotID = theCurrJumpHotspotID;
						break;
					case eCmdType_VKeySequence:
						// Use recursion to scan this sequence
						scanVKeySeqForFlags(InputMap::cmdVKeySeq(aKeyBindCmd),
							theCurrJumpHotspotID, theFinalJumpHotspotID,
							hasJump, isSlow);
						break;
					}
				}
				break;
			}
		}
	}

	std::vector<DispatchTask> mBuffer;
	u32 mHead;
	u32 mTail;
	int mMouseJumpQueueCount;
};


//-----------------------------------------------------------------------------
// DispatchTracker
//-----------------------------------------------------------------------------

struct ZERO_INIT(DispatchTracker)
{
	DispatchQueue queue;
	std::vector<Input> inputs;
	int currTaskProgress;
	int queuePauseTime;
	int nonModKeyPressAllowedTime;
	int modKeyChangeAllowedTime;
	int chatBoxEndAllowedTime;
	BitArray<0xFF> keysHeldDown;
	KeysWantDownMap keysWantDown;
	VectorMap<u8, int> keysLockedDown;
	BitArray<eSpecialKey_MoveNum> moveKeysHeld;
	BitArray<eSpecialKey_MoveNum> stickyMoveKeys;
	EAutoRunMode autoRunMode;
	u16 nextQueuedKey;
	u16 backupQueuedKey;
	bool typingChatBoxString;
	bool chatBoxActive;
	bool allowFastTasksDuringQueuePause;

	Hotspot mouseJumpDest;
	EMouseMode mouseMode;
	EMouseMode mouseModeWanted;
	EMouseMode mouseModeRequested;
	EMouseMode mouseJumpToMode;
	int mouseVelX, mouseVelY;
	int mouseDigitalVel;
	int mouseLookAutoRestoreTimer;
	int mouseClickAllowedTime;
	int mouseMoveAllowedTime;
	int mouseJumpAllowedTime;
	int mouseJumpFinishedTime;
	bool mouseJumpToHotspot;
	bool mouseJumpAttempted;
	bool mouseJumpVerified;
	bool mouseJumpInterpolate;
	bool mouseInterpolateRestart;
	bool mouseInterpolateUpdateDest;
	bool mouseAllowMidJumpControl;
	bool mouseLookNeededToStrafe;

	DispatchTracker() :
		mouseMode(eMouseMode_Cursor),
		mouseModeWanted(eMouseMode_Cursor),
		mouseModeRequested(eMouseMode_Cursor),
		mouseJumpToMode(eMouseMode_Cursor)
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

static void fireSignal(int aSignalID)
{
	gFiredSignals.set(aSignalID);
	switch(aSignalID - InputMap::specialKeySignalID(ESpecialKey(0)))
	{
	case eSpecialKey_AutoRun:
		if( InputMap::keyForSpecialAction(eSpecialKey_AutoRun) )
			sTracker.autoRunMode = eAutoRunMode_Started;
		break;
	case eSpecialKey_MoveF:
	case eSpecialKey_MoveB:
		if( sTracker.autoRunMode != eAutoRunMode_Queued &&
			sTracker.autoRunMode != eAutoRunMode_StartLockX &&
			sTracker.autoRunMode != eAutoRunMode_LockedX )
		{
			sTracker.autoRunMode = eAutoRunMode_Off;
		}
		break;
	case eSpecialKey_TurnL:
	case eSpecialKey_TurnR:
	case eSpecialKey_StrafeL:
	case eSpecialKey_StrafeR:
		if( sTracker.autoRunMode == eAutoRunMode_StartLockX ||
			sTracker.autoRunMode == eAutoRunMode_LockedX )
		{
			sTracker.autoRunMode = eAutoRunMode_Off;
		}
		break;
	}
}


static EResult popNextStringChar(const char* theString)
{
	// Strings should start with '/' or '>'
	DBG_ASSERT(theString && (theString[0] == '/' || theString[0] == '>'));
	DBG_ASSERT(size_t(sTracker.currTaskProgress) <= strlen(theString));

	if( theString[sTracker.currTaskProgress] == '\0' )
		return eResult_TaskCompleted;

	const int idx = sTracker.currTaskProgress++;
	const u16 kPasteKey = InputMap::keyForSpecialAction(eSpecialKey_PasteText);

	if( theString[idx] == '/' || theString[idx] == '>' )
	{
		if( sTracker.typingChatBoxString )
		{// Let this flag from prev string clear first before starting!
			--sTracker.currTaskProgress;
			sTracker.queuePauseTime =
				max(sTracker.queuePauseTime, 1);
			sTracker.allowFastTasksDuringQueuePause = true;
			return eResult_Incomplete;
		}

		// Press initial key to switch to chat box mode ('/' or '\r')
		sTracker.nextQueuedKey =
			theString[idx] == '/' ? VkKeyScan('/') : VK_RETURN;
		// Add a pause to make sure game-side async key checking switches to
		// direct text input in chat box before 'typing' at full speed
		sTracker.queuePauseTime =
			max(sTracker.queuePauseTime,
				kConfig.chatBoxPostFirstKeyDelay);
		sTracker.allowFastTasksDuringQueuePause = false;
		// Flag any movement keys now held down as possibly being "sticky",
		// meaning the game will treat them as continuously held down now,
		// even after the keys are released and even after stop using the
		// chat box (such as in EQ Titanium client)
		sTracker.stickyMoveKeys |= sTracker.moveKeysHeld;
		sTracker.chatBoxActive = true;
		// If can paste, copy the rest of the line into the clipboard now
		if( kPasteKey && OpenClipboard(NULL) )
		{
			std::string aStr(&theString[sTracker.currTaskProgress]);
			aStr.resize(aStr.find_first_of("\n\r"));
			const std::wstring& aWStr = widen(aStr);

			EmptyClipboard();
			if( HGLOBAL hGlobUnicode =
					GlobalAlloc(GMEM_MOVEABLE,
					(aWStr.size() + 1) * sizeof(wchar_t)) )
			{
				if( wchar_t* pGlobUnicode =
						(wchar_t*)GlobalLock(hGlobUnicode) )
				{
					memcpy(pGlobUnicode, aWStr.c_str(),
						(aWStr.size() + 1) * sizeof(wchar_t));
					GlobalUnlock(hGlobUnicode);
					SetClipboardData(CF_UNICODETEXT, hGlobUnicode);
				}
			}
			int ansiSize = WideCharToMultiByte(
				CP_ACP, 0, aWStr.c_str(), -1, NULL, 0, NULL, NULL);
			if( HGLOBAL hGlobAnsi =
				GlobalAlloc(GMEM_MOVEABLE, ansiSize) )
			{
				if( char* pGlobAnsi = (char*)GlobalLock(hGlobAnsi) )
				{
					WideCharToMultiByte(CP_ACP, 0, aWStr.c_str(), -1,
						pGlobAnsi, ansiSize, NULL, NULL);
					GlobalUnlock(hGlobAnsi);
					SetClipboardData(CF_TEXT, hGlobAnsi);
				}
			}
			CloseClipboard();

			// Jump progress to the character just before eol char
			sTracker.currTaskProgress += intSize(aStr.size());
			--sTracker.currTaskProgress;
		}
	}
	else if( theString[idx] == '\r' )
	{
		// Send the string by pressing carriage return
		sTracker.nextQueuedKey = VK_RETURN;
		// Add delay after press return before allowing any other key presses.
		// This prevents the chat box interface from "absorbing" gameplay-
		// related key presses, which can happen in some games for a time after
		// the carriage return but before the chat box closes out.
		sTracker.queuePauseTime =
			max(sTracker.queuePauseTime,
				kConfig.chatBoxPostEnterDelay);
		sTracker.allowFastTasksDuringQueuePause = false;
	}
	else if( kPasteKey )
	{
		// Paste the string from the clipboard instead of typing it key-by-key
		sTracker.nextQueuedKey = kPasteKey;
		sTracker.typingChatBoxString = true;
	}
	else
	{
		// Queue typing a string character key (possibly w/ shift key modifier)
		// Only printable ASCII characters are supported with this method!
		if( theString[idx] >= ' ' && theString[idx] <= '~' )
			sTracker.nextQueuedKey = VkKeyScan(theString[idx]);
		// Allow releasing shift quickly to continue typing characters
		// when are using chatbox (shouldn't have the same need for a delay
		// as a key sequence since target game likely uses keyboard events
		// instead of direct keyboard polling for chat box typing).
		// This is also checked for "sticky movement keys" while typing.
		sTracker.typingChatBoxString = true;
	}

	return eResult_Incomplete;
}


static EResult popNextKey(const u8* theVKeySequence)
{
	DBG_ASSERT(sTracker.nextQueuedKey == 0);
	DBG_ASSERT(sTracker.currTaskProgress >= 0);
	DBG_ASSERT(size_t(sTracker.currTaskProgress) <=
		strlen((char*)theVKeySequence));

	while(theVKeySequence[sTracker.currTaskProgress] != '\0')
	{
		const int idx = sTracker.currTaskProgress++;
		u8 aVKey = theVKeySequence[idx];

		if( aVKey == kVKeyTriggerKeyBind )
		{
			// Special 3-byte sequence to execute a key bind
			u8 c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			int aKeyBindID = (c & 0x7F) << 7;
			c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			aKeyBindID |= (c & 0x7F);
			// Jump this command to the front of the queue, and then
			// return to where we left off here once it is done
			sTracker.queue.setCurrTaskProgress(sTracker.currTaskProgress);
			sTracker.queue.push_front(InputMap::keyBindCommand(aKeyBindID));
			sTracker.currTaskProgress = 0;
			return eResult_Incomplete;
		}

		if( aVKey == VK_PAUSE )
		{
			// Special 3-byte sequence to add a forced pause
			u8 c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			int aDelay = (c & 0x7F) << 7;
			c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			aDelay |= (c & 0x7F);
			// Delays at end of sequence are ignored
			if( theVKeySequence[sTracker.currTaskProgress] == '\0' )
				return eResult_TaskCompleted;
			sTracker.queuePauseTime = MAX(sTracker.queuePauseTime, aDelay);
			sTracker.allowFastTasksDuringQueuePause = true;
			return eResult_Incomplete;
		}

		if( aVKey == kVKeyMouseJump )
		{
			// Special 3-byte sequence to cause mouse cursor jump to hotspot
			u8 c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			int aHotspotID = (c & 0x7F) << 7;
			c = theVKeySequence[sTracker.currTaskProgress++];
			DBG_ASSERT(c != '\0');
			aHotspotID |= (c & 0x7F);
			sTracker.mouseJumpDest = InputMap::getHotspot(aHotspotID);
			sTracker.mouseJumpToHotspot = true;
			sTracker.mouseJumpInterpolate = false;
			sTracker.mouseAllowMidJumpControl = false;
			sTracker.mouseJumpToMode = eMouseMode_PostJump;
			continue;
		}

		if( aVKey == kVKeyForceRelease )
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
			case VK_LWIN:
				sTracker.nextQueuedKey |= kVKeyWinFlag;
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
			(sTracker.nextQueuedKey & kVKeyMask) != VK_MENU &&
			(sTracker.nextQueuedKey & kVKeyMask) != VK_LWIN )
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


static bool isMouseButton(int theVKey)
{
	switch(theVKey & kVKeyMask)
	{
	case VK_LBUTTON:
	case VK_MBUTTON:
	case VK_RBUTTON:
		return true;
	}
	return false;
}


static bool isModKey(int theBaseVKey)
{
	DBG_ASSERT(!(theBaseVKey & ~kVKeyMask));
	switch(theBaseVKey)
	{
	case VK_SHIFT:
	case VK_CONTROL:
	case VK_MENU:
	case VK_LWIN:
		return true;
	}
	return false;
}


static int modKeysHeldAsFlags()
{
	int result = 0;
	if( sTracker.keysHeldDown.test(VK_SHIFT) )
		result |= kVKeyShiftFlag;
	if( sTracker.keysHeldDown.test(VK_CONTROL) )
		result |= kVKeyCtrlFlag;
	if( sTracker.keysHeldDown.test(VK_MENU) )
		result |= kVKeyAltFlag;
	if( sTracker.keysHeldDown.test(VK_LWIN) )
		result |= kVKeyWinFlag;

	return result;
}


static bool requiredModKeysAreAlreadyHeld(int theVKey)
{
	return
		(theVKey & kVKeyModsMask) == modKeysHeldAsFlags();
}


static bool isSafeAsyncKey(int theVKey)
{
	// These are keys that can be pressed while typing in a macro into the
	// chat box, without interfering with the typing, and with the given key
	// still having its intended in-game effect even while the chat box is
	// being actively typed into.
	if( kConfig.safeAsyncKeys.empty() )
		return false;

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
		 sTracker.queue.mouseJumpQueued() ||
		 sTracker.mouseJumpToHotspot) )
		return false;

	// Which are safe async keys depend on the game, so use Profile data
	return
		std::binary_search(
			kConfig.safeAsyncKeys.begin(),
			kConfig.safeAsyncKeys.end(),
			theVKey & kVKeyMask);
}


static void offsetMousePos()
{
	if( !sTracker.mouseVelX && !sTracker.mouseVelY )
		return;

	if( sTracker.mouseJumpToHotspot && !sTracker.mouseJumpVerified )
	{
		if( !sTracker.mouseJumpInterpolate || sTracker.mouseJumpAttempted )
			return;

		if( !sTracker.mouseAllowMidJumpControl )
		{
			sTracker.mouseVelX = sTracker.mouseVelY = 0;
			return;
		}

		// Influence hotspot destination rather than applying vel directly
		POINT aDestPos = WindowManager::hotspotToOverlayPos(
			sTracker.mouseJumpDest);
		aDestPos.x += sTracker.mouseVelX;
		aDestPos.y += sTracker.mouseVelY;
		const Hotspot& aDestHotspot =
			WindowManager::overlayPosToHotspot(aDestPos);
		if( sTracker.mouseJumpDest ==
			InputMap::getHotspot(eSpecialHotspot_LastCursorPos) )
		{
			InputMap::modifyHotspot(
				eSpecialHotspot_LastCursorPos,
				aDestHotspot);
		}
		sTracker.mouseJumpDest = aDestHotspot;
		sTracker.mouseVelX = sTracker.mouseVelY = 0;
		sTracker.mouseInterpolateUpdateDest = true;
		return;
	}

	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.dx = sTracker.mouseVelX;
	anInput.mi.dy = sTracker.mouseVelY;
	anInput.mi.dwFlags = MOUSEEVENTF_MOVE;

	// Whether movement is allowed depends on mode
	switch(sTracker.mouseMode)
	{
	case eMouseMode_Cursor:
		// Always allow
		break;
	case eMouseMode_LookTurn:
		// Allow once right mouse button is held down
		if( !sTracker.keysHeldDown.test(VK_RBUTTON) )
			return;
		break;
	case eMouseMode_LookOnly:
		// Allow once left mouse button is held down
		if( !sTracker.keysHeldDown.test(VK_LBUTTON) )
			return;
		break;
	default:
		// Never allow
		sTracker.mouseVelX = sTracker.mouseVelY = 0;
		return;
	}

	if( gAppRunTime < sTracker.mouseMoveAllowedTime )
		return;

	sTracker.inputs.push_back(anInput);
	sTracker.mouseVelX = sTracker.mouseVelY = 0;
	sTracker.mouseLookAutoRestoreTimer = 0;
}


static void jumpMouseToHotspot(const Hotspot& theDestHotspot)
{
	// No jumps allowed while holding down a mouse button!
	DBG_ASSERT(!sTracker.keysHeldDown.test(VK_LBUTTON));
	DBG_ASSERT(!sTracker.keysHeldDown.test(VK_MBUTTON));
	DBG_ASSERT(!sTracker.keysHeldDown.test(VK_RBUTTON));

	if( sTracker.mouseJumpAttempted && sTracker.mouseJumpVerified )
		return;

	sTracker.mouseJumpAttempted = true;
	sTracker.mouseJumpVerified = false;

	// If already at dest pos anyway, don't bother with the jump itself
	POINT aDestPos = WindowManager::hotspotToOverlayPos(theDestHotspot);
	const POINT& aCurrentPos = WindowManager::mouseToOverlayPos();
	if( aCurrentPos.x == aDestPos.x && aCurrentPos.y == aDestPos.y )
	{
		sTracker.mouseJumpVerified = true;
		return;
	}

	sTracker.mouseJumpFinishedTime = gAppRunTime + kConfig.mouseJumpDelayTime;
	aDestPos = WindowManager::overlayPosToNormalizedMousePos(aDestPos);
	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.dx = aDestPos.x;
	anInput.mi.dy = aDestPos.y;
	anInput.mi.dwFlags = MOUSEEVENTF_MOVEABSOLUTE;
	sTracker.inputs.push_back(anInput);
}


static bool verifyCursorJumpedTo(const Hotspot& theDestHotspot)
{
	if( !sTracker.mouseJumpAttempted )
		return false;

	// In simulation mode always act as if jump was successful
	#ifdef INPUT_DISPATCHER_SIMULATION_ONLY
	sTracker.mouseJumpVerified = true;
	#endif

	if( !sTracker.mouseJumpVerified )
	{
		static int sFailedJumpAttemptsInARow = 0;
		const POINT& aDestPos =
			WindowManager::hotspotToOverlayPos(theDestHotspot);
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
	// Now just need to wait minimum post-jump time
	sTracker.mouseJumpVerified = true;
	if( gAppRunTime < sTracker.mouseJumpFinishedTime )
		return false;

	// Jump process completed! Clear flags for next jump attempt
	sTracker.mouseJumpAttempted = false;
	sTracker.mouseJumpVerified = false;
	sTracker.mouseJumpInterpolate = false;
	sTracker.mouseAllowMidJumpControl = false;

	// Reached jump destination and can update mouse mode accordingly
	sTracker.mouseMode = sTracker.mouseJumpToMode;
	sTracker.mouseLookAutoRestoreTimer = 0;
	return true;
}


static void trailMouseToHotspot(const Hotspot& theDestHotspot)
{
	#ifdef INPUT_DISPATCHER_SIMULATION_ONLY
		sTracker.mouseJumpAttempted = true;
		sTracker.mouseJumpVerified = true;
	#endif

	if( sTracker.mouseJumpAttempted && sTracker.mouseJumpVerified )
		return;

	static const int kMinTrailTime = 100;
	static const int kMaxTrailTime = 300;
	static int sStartTime = 0, sTrailTime = 0;
	static int sStartPosX, sStartPosY, sTrailDistX, sTrailDistY;

	if( sTracker.mouseInterpolateRestart )
	{
		sStartTime = gAppRunTime - gAppFrameTime;
		sTracker.mouseInterpolateUpdateDest = true;
	}
	
	if( sTracker.mouseInterpolateUpdateDest )
	{
		const POINT& aDestPos =
			WindowManager::hotspotToOverlayPos(theDestHotspot);
		const POINT& aCurrPos = WindowManager::mouseToOverlayPos(false);
		if( aCurrPos.x == aDestPos.x && aCurrPos.y == aDestPos.y )
		{// Already at destination - treat as verified jump
			sTracker.mouseJumpAttempted = true;
			sTracker.mouseJumpVerified = true;
			return;
		}
		sStartPosX = aCurrPos.x; sStartPosY = aCurrPos.y;
		sTrailDistX = aDestPos.x - aCurrPos.x;
		sTrailDistY = aDestPos.y - aCurrPos.y;
		sTracker.mouseInterpolateUpdateDest = false;
	}

	if( sTracker.mouseInterpolateRestart )
	{
		const int aDistance = int(sqrt(
			double(sTrailDistX) * sTrailDistX +
			double(sTrailDistY) * sTrailDistY));
		sTrailTime = clamp(aDistance, kMinTrailTime, kMaxTrailTime);
		sTracker.mouseInterpolateRestart = false;
	}

	POINT aNewPos = { sStartPosX, sStartPosY };
	const int aTrailTimePassed = gAppRunTime - sStartTime;
	if( aTrailTimePassed >= sTrailTime )
	{
		aNewPos.x += sTrailDistX;
		aNewPos.y += sTrailDistY;
	}
	else
	{
		double anInterpTime = 1.0 - (double(aTrailTimePassed) / sTrailTime);
		anInterpTime = 1.0 - (anInterpTime * anInterpTime);
		aNewPos.x += LONG(sTrailDistX * anInterpTime);
		aNewPos.y += LONG(sTrailDistY * anInterpTime);
	}

	if( aNewPos.x == sStartPosX + sTrailDistX &&
		aNewPos.y == sStartPosY + sTrailDistY )
	{// Should end up at destination - flag as attempted jump
		sTracker.mouseJumpAttempted = true;
		sTracker.mouseJumpVerified = false;
	}

	sTracker.mouseJumpFinishedTime = gAppRunTime + kConfig.mouseJumpDelayTime;
	aNewPos = WindowManager::overlayPosToNormalizedMousePos(aNewPos);
	Input anInput;
	anInput.type = INPUT_MOUSE;
	anInput.mi.dx = aNewPos.x;
	anInput.mi.dy = aNewPos.y;
	anInput.mi.dwFlags = MOUSEEVENTF_MOVEABSOLUTE;
	sTracker.inputs.push_back(anInput);
}


static bool manualCharacterMoveInUse()
{
	switch(sTracker.autoRunMode)
	{
	case eAutoRunMode_Started:
		return true;
	case eAutoRunMode_LockedX:
		return
			sTracker.moveKeysHeld.test(
				eSpecialKey_MoveF - eSpecialKey_FirstMove) ||
			sTracker.moveKeysHeld.test(
				eSpecialKey_MoveB - eSpecialKey_FirstMove);
	case eAutoRunMode_LockedY:
		return
			sTracker.mouseLookNeededToStrafe ||
			sTracker.moveKeysHeld.test(
				eSpecialKey_TurnL - eSpecialKey_FirstMove) ||
			sTracker.moveKeysHeld.test(
				eSpecialKey_TurnR - eSpecialKey_FirstMove) ||
			sTracker.moveKeysHeld.test(
				eSpecialKey_StrafeL - eSpecialKey_FirstMove) ||
			sTracker.moveKeysHeld.test(
				eSpecialKey_StrafeR - eSpecialKey_FirstMove);
	case eAutoRunMode_LockedXY:
		return false;
	default:
		return
			sTracker.moveKeysHeld.any() ||
			sTracker.mouseLookNeededToStrafe;
	}
}


static EMouseMode checkAutoMouseLookMode()
{
	// This is to assist with the "auto" mouse modes that swap between
	// other modes depending on the current situation, or in other cases
	// where mouse mode wanted does not match the actual user request
	switch(sTracker.mouseModeWanted)
	{
	case eMouseMode_AutoLook:
		if( sTracker.mouseVelX != 0 &&
			sTracker.autoRunMode == eAutoRunMode_Off &&
			sTracker.moveKeysHeld.none() )
		{
			return eMouseMode_LookOnly;
		}

		if( sTracker.moveKeysHeld.any() ||
			sTracker.mouseLookNeededToStrafe ||
			sTracker.autoRunMode != eAutoRunMode_Off )
		{
			return eMouseMode_LookTurn;
		}

		if( sTracker.mouseMode != eMouseMode_LookTurn &&
			sTracker.mouseMode != eMouseMode_LookOnly )
		{
			return eMouseMode_LookOnly;
		}
		return sTracker.mouseMode;

	case eMouseMode_AutoRunLook:
		if( manualCharacterMoveInUse() )
			return eMouseMode_LookTurn;

		if( sTracker.mouseVelX != 0 )
			return eMouseMode_LookOnly;

		if( sTracker.mouseMode != eMouseMode_LookTurn &&
			sTracker.mouseMode != eMouseMode_LookOnly )
		{
			return eMouseMode_LookOnly;
		}
		return sTracker.mouseMode;

	case eMouseMode_AutoToTurn:
		if( sTracker.mouseMode == eMouseMode_LookTurn ||
			sTracker.moveKeysHeld.any() ||
			sTracker.mouseLookNeededToStrafe ||
			sTracker.autoRunMode != eAutoRunMode_Off )
		{
			sTracker.mouseModeWanted = eMouseMode_LookTurn;
			return eMouseMode_LookTurn;
		}
		return eMouseMode_LookOnly;

	case eMouseMode_RunToTurn:
		if( sTracker.autoRunMode == eAutoRunMode_Off )
		{
			sTracker.mouseModeWanted = eMouseMode_AutoToTurn;
			return checkAutoMouseLookMode();
		}

		if( sTracker.mouseMode == eMouseMode_LookTurn ||
			manualCharacterMoveInUse() )
		{
			sTracker.mouseModeWanted = eMouseMode_LookTurn;
			return eMouseMode_LookTurn;
		}
		
		if( sTracker.mouseMode == eMouseMode_LookOnly &&
			sTracker.mouseVelX != 0 )
		{
			return eMouseMode_LookOnly;
		}
		if( sTracker.mouseVelX != 0 || sTracker.mouseVelY != 0 )
		{
			sTracker.mouseModeWanted = eMouseMode_LookTurn;
			return eMouseMode_LookTurn;
		}
		return eMouseMode_LookReady;

	case eMouseMode_RunToAuto:
		if( sTracker.autoRunMode == eAutoRunMode_Off ||
			sTracker.mouseMode == eMouseMode_LookTurn ||
			manualCharacterMoveInUse() )
		{
			sTracker.mouseModeWanted = eMouseMode_AutoLook;
			return checkAutoMouseLookMode();
		}
		
		if( sTracker.mouseMode == eMouseMode_LookOnly &&
			sTracker.mouseVelX != 0 )
		{
			return eMouseMode_LookOnly;
		}
		if( sTracker.mouseVelX != 0 || sTracker.mouseVelY != 0 )
		{
			sTracker.mouseModeWanted = eMouseMode_AutoLook;
			return checkAutoMouseLookMode();
		}
		return eMouseMode_LookReady;
	}

	return sTracker.mouseModeWanted;
}


static EMouseMode checkMouseLookRestore(EMouseMode theWantedMode)
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

	// There is also an issue with the Pantheon client where it drops mouse
	// look mode whenever open the chat box while still holding the button.

	// To fix these, if haven't moved the mouse for a while and are requesting
	// a look mode, a mode is used that releases the mouse button and moves
	// the cursor into the mouse look start position. Then, the next time any
	// actual mouse (camera) movement is requested, the mode can start again
	// immmediately by clicking the button again.

	if( kConfig.mouseLookAutoRestoreTime > 0 &&
		!sTracker.mouseLookNeededToStrafe &&
		(theWantedMode == eMouseMode_LookOnly ||
		 theWantedMode == eMouseMode_LookTurn) &&
		sTracker.mouseVelX == 0 && sTracker.mouseVelY == 0 &&
		(sTracker.mouseMode == eMouseMode_LookReady ||
		 sTracker.mouseLookAutoRestoreTimer - gAppFrameTime >=
			 kConfig.mouseLookAutoRestoreTime) )
	{
		#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
		if( sTracker.mouseMode != eMouseMode_LookReady )
			debugPrint("InputDispatcher: Restoring Mouse Look mode\n");
		#endif
		return eMouseMode_LookReady;
	}

	return theWantedMode;
}


static EMouseMode getmouseModeWanted()
{
	EMouseMode aMouseMode = eMouseMode_Cursor;

	#ifndef INPUT_DISPATCHER_SIMULATION_ONLY
	// Prevent modes that hold a mouse button when not in-game yet, etc
	if( WindowManager::requiresNormalCursorControl() )
		return aMouseMode;
	#endif

	aMouseMode = checkAutoMouseLookMode();
	aMouseMode = checkMouseLookRestore(aMouseMode);

	return aMouseMode;
}


static void lockKeyDownFor(int theBaseVKey, int theLockTime)
{
	DBG_ASSERT(!(theBaseVKey & ~kVKeyMask));
	int& aLockEndTime = sTracker.keysLockedDown.findOrAdd(u8(theBaseVKey), 0);
	aLockEndTime = max(aLockEndTime, gAppRunTime + theLockTime);
}


static bool keyIsLockedDown(int theBaseVKey)
{
	VectorMap<u8, int>::iterator itr =
		sTracker.keysLockedDown.find(dropTo<u8>(theBaseVKey));
	if( itr == sTracker.keysLockedDown.end() )
		return false;
	if( gAppRunTime < itr->second )
		return true;
	sTracker.keysLockedDown.erase(itr);
	return false;
}


static EResult setKeyDown(int theKey, bool down)
{
	// No flags should be set on key (break combo keys into individual keys!)
	DBG_ASSERT(!(theKey & ~kVKeyMask));

	if( theKey == 0 )
		return eResult_InvalidParameter;

	const bool wasDown = sTracker.keysHeldDown.test(theKey);
	if( down == wasDown )
		return eResult_Ok;

	// May not be allowed to press non-mod keys yet
	if( down && !sTracker.typingChatBoxString &&
		!isModKey(theKey) &&
		gAppRunTime < sTracker.nonModKeyPressAllowedTime )
	{
		return eResult_NotAllowed;
	}

	// May not be allowed to press or release any mod keys yet
	if( isModKey(theKey) && !sTracker.typingChatBoxString &&
		gAppRunTime < sTracker.modKeyChangeAllowedTime )
	{
		return eResult_NotAllowed;
	}

	// May not be allowed to click a mouse button yet
	if( down && isMouseButton(theKey) &&
		gAppRunTime < sTracker.mouseClickAllowedTime )
 		return eResult_NotAllowed;

	// May not be allowed to release the given key yet
	if( !down && keyIsLockedDown(theKey) )
 		return eResult_NotAllowed;

	Input anInput;
	int aLockDownTime = kConfig.baseKeyReleaseLockTime;
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
		aLockDownTime = kConfig.mouseClickLockTime;
		break;
	case VK_RBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = down
			? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
		aLockDownTime = kConfig.mouseClickLockTime;
		break;
	case VK_MBUTTON:
		anInput.type = INPUT_MOUSE;
		anInput.mi.dwFlags = down
			? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
		aLockDownTime = kConfig.mouseClickLockTime;
		break;
	case VK_SHIFT:
	case VK_CONTROL:
	case VK_MENU:
	case VK_LWIN:
		if( !sTracker.typingChatBoxString )
		{
			aLockDownTime = kConfig.minModKeyChangeTime;
			sTracker.nonModKeyPressAllowedTime =
				gAppRunTime + kConfig.minModKeyChangeTime;
		}
		// fall through
	default:
		anInput.type = INPUT_KEYBOARD;
		anInput.ki.wVk = dropTo<WORD>(theKey);
		anInput.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
		break;
	}

	sTracker.inputs.push_back(anInput);
	sTracker.keysHeldDown.set(theKey, down);
	if( down )
	{
		lockKeyDownFor(theKey, aLockDownTime);
		if( !isModKey(theKey) && !sTracker.typingChatBoxString )
		{
			sTracker.modKeyChangeAllowedTime =
				gAppRunTime + kConfig.minModKeyChangeTime;
		}
	}

	if( anInput.type == INPUT_MOUSE )
	{
		if( down )
		{
			// Don't allow movement immediately after start holding the
			// mouse button for a mouse look mode, or it may actually move
			// the cursor instead in some modes and cause the mouse look
			// to not start properly by accidentally clicking on a UI window
			if( (theKey == VK_LBUTTON &&
				 sTracker.mouseMode == eMouseMode_LookOnly) ||
				(theKey == VK_RBUTTON &&
				 sTracker.mouseMode == eMouseMode_LookTurn) )
			{
				sTracker.mouseMoveAllowedTime =
					gAppRunTime + kConfig.mouseLookMoveLockTime;
			}
		}
		else
		{
			// Lock cursor jump and mod key changes after release a mouse
			// button, as well as re-clicking a mouse button immediately after.
			// Mouse buttons are special because the "click" of a mouse is
			// often on release, unlike a keyboard keys that "click" on initial
			// press, which is why the release time matters for them
			sTracker.mouseJumpAllowedTime =
				gAppRunTime + kConfig.mouseJumpDelayTime;
			sTracker.modKeyChangeAllowedTime =
				gAppRunTime + kConfig.minModKeyChangeTime;
			sTracker.mouseClickAllowedTime =
				gAppRunTime + kConfig.mouseReClickLockTime;
		}
	}

	return eResult_Ok;
}


static bool tryQuickReleaseHeldKey(int theVKey, KeyWantDownStatus& theStatus)
{
	// If multiple gamepad buttons are tied to the same key and more than one
	// is held down, we "release" the key by just decrementing a counter
	if( theStatus.depth > 1 )
	{
		--theStatus.depth;
		return true;
	}

	const int aBaseVKey = theVKey & kVKeyMask;
	DBG_ASSERT((theVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
	DBG_ASSERT(aBaseVKey);

	// Don't release yet if not actually pressed
	if( !theStatus.pressed || !sTracker.keysHeldDown.test(aBaseVKey) )
		return false;

	// In most target apps, releasing a mouse button can itself count as
	// an action being taken (a "click"), whereas a keyboard key generally
	// only means to stop doing an action (like stop moving forward). Thus
	// the target app will check modifier keys on a mouse up event to see
	// if it is a shift-click vs ctrl-click vs normal click, but for keyboard
	// keys usually only cares about the modifiers when the key is pressed.
	// Therefore if the "key" is a mouse button, need to make sure all related
	// modifier keys like Shift are held before releasing the mouse button!
	if( isMouseButton(theVKey) && !requiredModKeysAreAlreadyHeld(theVKey) )
		return false;

	// Make sure no other keysWantDown uses this same base key
	for(KeysWantDownMap::iterator itr = sTracker.keysWantDown.begin();
		itr != sTracker.keysWantDown.end(); ++itr)
	{
		// If find another entry wants this same base key to stay down,
		// don't actually release the key but act as if did so, and let
		// the other entry actually release the key when it is done.
		const int aTestVKey = itr->first & (kVKeyMask | kVKeyModsMask);
		const int aTestBaseVKey = aTestVKey & kVKeyMask;

		if( aTestVKey != theVKey &&
			aTestBaseVKey == aBaseVKey &&
			itr->second.pressed &&
			itr->second.depth > 0 )
		{
			theStatus.depth = 0;
			theStatus.pressed = false;
			return true;
		}
	}

	// Now attempt to actually release the key (may be locked down though)
	if( setKeyDown(aBaseVKey, false) == eResult_Ok )
	{
		theStatus.depth = 0;
		theStatus.pressed = false;
		return true;
	}

	return false;
}


static void debugPrintInputVector()
{
	static int sUpdateCount = 0;
	++sUpdateCount;
#ifndef NDEBUG
#ifdef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
#define siPrint(fmt, ...) debugPrint( \
	(strFormat("InputDispatcher: On update %d (%dms): ", \
	sUpdateCount, gAppRunTime) + fmt).c_str(), __VA_ARGS__)

	for(int i = 0, end = intSize(sTracker.inputs.size()); i < end; ++i)
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
						double(-int(anInput.mi.mouseData)) / WHEEL_DELTA);
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
			for(int i = 0, end = intSize(sTracker.inputs.size()); i < end; ++i)
			{
				if( sTracker.inputs[i].type == INPUT_KEYBOARD )
				{
					sTracker.inputs[i].ki.wScan = dropTo<WORD>(0xFFFF &
						MapVirtualKey(sTracker.inputs[i].ki.wVk, 0));
					switch(sTracker.inputs[i].ki.wVk)
					{
					case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
					case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
					case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
					case VK_LWIN: case VK_RWIN:
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


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.contains("Gamepad") ||
		theProfileMap.contains("Mouse") ||
		theProfileMap.contains("System") )
	{
		kConfig.load();
	}
}


void cleanup()
{
	sTracker.keysLockedDown.clear();
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		setKeyDown(aVKey, false);
	}
	sTracker.keysHeldDown.reset();
	sTracker.keysWantDown.clear();
	sTracker.moveKeysHeld.reset();
	sTracker.stickyMoveKeys.reset();
	sTracker.mouseModeWanted = eMouseMode_Cursor;
	if( sTracker.mouseMode != eMouseMode_Cursor )
	{
		sTracker.mouseJumpToMode = eMouseMode_Cursor;
		jumpMouseToHotspot(
			InputMap::getHotspot(eSpecialHotspot_LastCursorPos));
	}
	sTracker.typingChatBoxString = false;
	sTracker.chatBoxActive = false;
	flushInputVector();
}


void forceReleaseHeldKeys()
{
	sTracker.keysLockedDown.clear();
	for(int aVKey = sTracker.keysHeldDown.firstSetBit();
		aVKey < sTracker.keysHeldDown.size();
		aVKey = sTracker.keysHeldDown.nextSetBit(aVKey+1))
	{
		setKeyDown(aVKey, false);
	}
	if( sTracker.mouseMode != eMouseMode_Cursor )
	{
		sTracker.mouseJumpToMode = eMouseMode_Cursor;
		jumpMouseToHotspot(
			InputMap::getHotspot(eSpecialHotspot_LastCursorPos));
	}
	flushInputVector();
}


void update()
{
	// Update timers
	// -------------
	if( sTracker.queuePauseTime > 0 )
		sTracker.queuePauseTime -= gAppFrameTime;
	sTracker.mouseLookAutoRestoreTimer += gAppFrameTime;


	// Update mouse mode
	// -----------------
	if( sTracker.mouseJumpToHotspot &&
		verifyCursorJumpedTo(sTracker.mouseJumpDest) )
	{// Clear .mouseJumpToHotspot flag once verified destination reached
		sTracker.mouseJumpToHotspot = false;
	}
	if( !sTracker.nextQueuedKey &&
		!sTracker.backupQueuedKey &&
		!sTracker.mouseJumpToHotspot &&
		(sTracker.currTaskProgress == 0 || sTracker.queuePauseTime > 0) )
	{// No tasks in progress that mouse mode change could interfere with

		EMouseMode aMouseModeWanted = getmouseModeWanted();
		bool wantDifferentMode = aMouseModeWanted != sTracker.mouseMode;
		bool mouseMoving = sTracker.mouseVelX != 0 || sTracker.mouseVelY != 0;
		bool holdingLMB = sTracker.keysHeldDown.test(VK_LBUTTON);
		bool holdingRMB = sTracker.keysHeldDown.test(VK_RBUTTON);

		switch(sTracker.mouseMode)
		{
		case eMouseMode_Hide:
		case eMouseMode_LookReady:
			// Mouse is never considered actually moving
			mouseMoving = false;
			break;
		case eMouseMode_PostJump:
			// If mouse was jumped but didn't click after the jump, no need for
			// any further action - just treat as new cursor mode position
			sTracker.mouseMode = eMouseMode_Cursor;
			wantDifferentMode = false;
			break;
		case eMouseMode_LookTurn:
			// Don't exit LookTurn mode while holding a turn key,
			// or it may change from strafing to turning
			if( sTracker.keysHeldDown.test(
					InputMap::keyForSpecialAction(eSpecialKey_TurnL)) ||
				sTracker.keysHeldDown.test(
					InputMap::keyForSpecialAction(eSpecialKey_TurnR)) )
			{
				wantDifferentMode = false;
				break;
			}
			// fall through
		case eMouseMode_LookOnly:
			// If not holding a mouse button in a _Look mode, it must have been
			// force-released, so re-activate the mode
			if( sTracker.mouseMode == aMouseModeWanted &&
				!holdingLMB && !holdingRMB )
			{
				wantDifferentMode = true;
			}
			// Allow transitioning between look modes even while holding mouse
			if( aMouseModeWanted == eMouseMode_LookOnly ||
				aMouseModeWanted == eMouseMode_LookTurn )
			{
				mouseMoving = false;
			}
			// Don't prevent changing modes from holding a button if that
			// mode is always supposed to be holding that button anyway
			if( sTracker.mouseMode == eMouseMode_LookTurn )
				holdingRMB = false;
			if( sTracker.mouseMode == eMouseMode_LookOnly )
				holdingLMB = false;
			break;
		}

		if( wantDifferentMode && !mouseMoving && !holdingLMB && !holdingRMB )
		{// Mouse mode wants changing and mouse isn't otherwise busy
			switch(aMouseModeWanted)
			{
			case eMouseMode_Cursor:
				#ifndef INPUT_DISPATCHER_SIMULATION_ONLY
				if( WindowManager::requiresNormalCursorControl() )
				{// Skip restore jump if cursor mode being forced
					sTracker.mouseMode = eMouseMode_Cursor;
				}
				else
				#endif
				{// Jump cursor to last normal cursor position
					sTracker.mouseJumpDest =
						InputMap::getHotspot(eSpecialHotspot_LastCursorPos);
					sTracker.mouseJumpToMode = aMouseModeWanted;
					sTracker.mouseJumpToHotspot = true;
					sTracker.mouseJumpInterpolate = false;
					sTracker.mouseAllowMidJumpControl = false;
				}
				break;
			case eMouseMode_LookTurn:
				if( sTracker.mouseMode != eMouseMode_JumpClicked )
				{
					// Don't start look turn mode while holding a turn key,
					// or turning may suddenly become strafing
					if( sTracker.keysHeldDown.test(
							InputMap::keyForSpecialAction(eSpecialKey_TurnL)) ||
						sTracker.keysHeldDown.test(
							InputMap::keyForSpecialAction(eSpecialKey_TurnR)) )
					{
						break;
					}
				}
				// fall through
			case eMouseMode_LookOnly:
				if( sTracker.mouseMode == eMouseMode_JumpClicked )
				{// Give one more update to process queue before resuming
					// (in case have multiple jump-clicks queued in a row)
					sTracker.mouseMode = eMouseMode_Default;
				}
				else if( sTracker.mouseMode == eMouseMode_LookReady )
				{// Skip position jump and just hold the button
					sTracker.nextQueuedKey =
						(aMouseModeWanted == eMouseMode_LookTurn)
							? (VK_RBUTTON | kVKeyHoldFlag)
							: (VK_LBUTTON | kVKeyHoldFlag);
					sTracker.mouseMode = aMouseModeWanted;
					sTracker.mouseLookAutoRestoreTimer = 0;
				}
				else if( (sTracker.mouseMode == eMouseMode_LookTurn &&
						  sTracker.keysHeldDown.test(VK_RBUTTON)) ||
						 (sTracker.mouseMode == eMouseMode_LookOnly &&
						  sTracker.keysHeldDown.test(VK_LBUTTON)) )
				{// Just swap which button is held immediately
					sTracker.mouseClickAllowedTime = 0;
					setKeyDown(
						(aMouseModeWanted == eMouseMode_LookTurn)
							? VK_RBUTTON : VK_LBUTTON, true);
					sTracker.mouseMode = aMouseModeWanted;
					sTracker.mouseLookAutoRestoreTimer = 0;
				}
				else if( !sTracker.queue.mouseJumpQueued() )
				{// Jump cursor to safe spot for initial click
					sTracker.mouseJumpDest =
						InputMap::getHotspot(eSpecialHotspot_MouseLookStart);
					sTracker.mouseJumpToMode = aMouseModeWanted;
					sTracker.mouseJumpToHotspot = true;
					sTracker.mouseJumpInterpolate = false;
					sTracker.mouseAllowMidJumpControl = false;
					// Begin holding down the appropriate mouse button
					sTracker.nextQueuedKey =
						(aMouseModeWanted == eMouseMode_LookTurn)
							? (VK_RBUTTON | kVKeyHoldFlag)
							: (VK_LBUTTON | kVKeyHoldFlag);
				}
				break;
			case eMouseMode_Hide:
				// Can't actually hide cursor without messing with target app
				// (or using _Look which can cause undesired side effects)
				// so just move it out of the way (bottom corner usually)
				if( !sTracker.queue.mouseJumpQueued() )
				{
					sTracker.mouseJumpDest = 
						InputMap::getHotspot(eSpecialHotspot_MouseHidden);
					sTracker.mouseJumpToMode = aMouseModeWanted;
					sTracker.mouseJumpToHotspot = true;
					sTracker.mouseJumpInterpolate = false;
					sTracker.mouseAllowMidJumpControl = false;
				}
				break;
			case eMouseMode_LookReady:
				// Just jump to mouse look start position and wait until
				// attempt to move mouse to start actual look mode
				if( !sTracker.queue.mouseJumpQueued() )
				{
					sTracker.mouseJumpDest = 
						InputMap::getHotspot(eSpecialHotspot_MouseLookStart);
					sTracker.mouseJumpToMode = aMouseModeWanted;
					sTracker.mouseJumpToHotspot = true;
					sTracker.mouseJumpInterpolate = false;
					sTracker.mouseAllowMidJumpControl = false;
				}
				break;
			default:
				DBG_ASSERT(false && "Invalid mouse mode wanted value!");
			}
		}
	}


	// Update queue
	// ------------
	if( sTracker.backupQueuedKey )
		sTracker.nextQueuedKey = sTracker.backupQueuedKey;
	sTracker.backupQueuedKey = 0;
	while(sTracker.nextQueuedKey == 0 &&
		  !sTracker.queue.empty() &&
		  (sTracker.queuePauseTime <= 0 ||
		   (sTracker.allowFastTasksDuringQueuePause &&
		    sTracker.queue.hasFastTaskReady())))
	{
		const DispatchTask& aCurrTask = sTracker.queue.front();
		sTracker.currTaskProgress =
			max(sTracker.currTaskProgress, aCurrTask.progress);

		const bool taskIsPastDue =
			sTracker.currTaskProgress == 0 &&
			aCurrTask.queuedTime < (gAppRunTime - kConfig.maxTaskQueuedTime);

		const Command& aCmd = aCurrTask.cmd;
		EResult aTaskResult = eResult_TaskCompleted;

		switch(aCmd.type)
		{
		case eCmdType_PressAndHoldKey:
			DBG_ASSERT(aCmd.vKey != 0);
			DBG_ASSERT((aCmd.vKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
			{// Just set want the key/combo pressed
				KeyWantDownStatus& aKeyStatus =
					sTracker.keysWantDown.findOrAdd(aCmd.vKey);
				--aKeyStatus.queued;
				if( aKeyStatus.depth > 0 )
				{
					++aKeyStatus.depth;
				}
				else
				{
					aKeyStatus.depth = 1;
					aKeyStatus.pressed = false;
					if( !taskIsPastDue )
						fireSignal(aCmd.signalID);
				}
			}
			break;
		case eCmdType_ReleaseKey:
			DBG_ASSERT(aCmd.vKey != 0);
			DBG_ASSERT((aCmd.vKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
			{
				KeyWantDownStatus& aKeyStatus =
					sTracker.keysWantDown.findOrAdd(aCmd.vKey);
				// Do nothing if key has no active hold requests
				// (such as after force-released by kVKeyForceReleaseFlag)
				if( aKeyStatus.depth <= 0 )
					break;
				if( !aKeyStatus.pressed )
				{// Key has yet to be pressed in the first place
					// Abort queue loop and wait for it to be pressed first,
					// unless this is a past-due event, in which case just
					// forget ever wanted the key pressed in the first place
					if( !taskIsPastDue )
					{
						sTracker.queuePauseTime =
							max(sTracker.queuePauseTime, 1);
						sTracker.allowFastTasksDuringQueuePause = false;
						aTaskResult = eResult_Incomplete;
					}
					else
					{
						--aKeyStatus.depth;
					}
					break;
				}
				// Key has been pressed, and should release even if past due
				// Attempt to release now and move on to next queue item
				// If can't, set releasing it as the queued key event
				if( !tryQuickReleaseHeldKey(aCmd.vKey, aKeyStatus) )
				{
					aKeyStatus.depth = 0;
					aKeyStatus.pressed = false;
					sTracker.nextQueuedKey = u16(aCmd.vKey | kVKeyReleaseFlag);
				}
			}
			break;
		case eCmdType_TapKey:
			if( !taskIsPastDue )
			{
				sTracker.nextQueuedKey = aCmd.vKey;
				fireSignal(aCmd.signalID);
			}
			break;
		case eCmdType_VKeySequence:
			{
				const u8* aCmdSeq = InputMap::cmdVKeySeq(aCmd);
				if( !taskIsPastDue )
				{
					if( aCurrTask.progress == 0 )
						fireSignal(aCmd.signalID);
					aTaskResult = popNextKey(aCmdSeq);
				}
			}
			break;
		case eCmdType_ChatBoxString:
			if( !taskIsPastDue )
			{
				if( aCurrTask.progress == 0 )
					fireSignal(aCmd.signalID);
				aTaskResult = popNextStringChar(InputMap::cmdString(aCmd));
			}
			break;
		case eCmdType_MoveMouseToHotspot:
		case eCmdType_MouseClickAtHotspot:
		case eCmdType_MoveMouseToMenuItem:
		case eCmdType_MoveMouseToOffset:
			if( sTracker.mouseJumpToHotspot && !sTracker.mouseJumpInterpolate )
			{// Finish instant jump first
				sTracker.queuePauseTime =
					max(sTracker.queuePauseTime, 1);
				sTracker.allowFastTasksDuringQueuePause = false;
				aTaskResult = eResult_Incomplete;
			}
			else if( !taskIsPastDue )
			{
				Hotspot aCmdHotspot;
				aCmdHotspot.x = aCmd.hotspot.x;
				aCmdHotspot.y = aCmd.hotspot.y;
				sTracker.mouseInterpolateRestart =
					!sTracker.mouseJumpInterpolate ||
					!sTracker.mouseJumpToHotspot ||
					sTracker.mouseJumpDest != aCmdHotspot;
				sTracker.mouseJumpDest = aCmdHotspot;
				sTracker.mouseJumpToMode = eMouseMode_PostJump;
				sTracker.mouseAllowMidJumpControl =
					(aCmd.type == eCmdType_MoveMouseToHotspot ||
					 aCmd.type == eCmdType_MoveMouseToOffset) &&
					(sTracker.mouseMode == eMouseMode_Cursor ||
					 sTracker.mouseMode == eMouseMode_PostJump);
				if( aCmd.type == eCmdType_MouseClickAtHotspot )
					sTracker.nextQueuedKey = VK_LBUTTON;
				sTracker.mouseJumpToHotspot = true;
				sTracker.mouseJumpAttempted = false;
				sTracker.mouseJumpVerified = false;
				sTracker.mouseJumpInterpolate = true;
			}
			break;
		default:
			DBG_ASSERT(false && "Command type should not have been queued!");
			break;
		}
		if( aTaskResult == eResult_TaskCompleted )
		{// Move to next item in queue
			sTracker.currTaskProgress = 0;
			sTracker.queue.pop_front();
		}
		else if( sTracker.queuePauseTime > 0 )
		{
			sTracker.queue.setCurrTaskProgress(sTracker.currTaskProgress);
		}
		// End "chat box active mode" only once finished task ends the loop,
		// or a is pause requested that allows for other tasks to complete.
		if( sTracker.chatBoxActive &&
			(aTaskResult == eResult_TaskCompleted &&
			 (sTracker.nextQueuedKey || sTracker.queue.empty())) ||
			(sTracker.queuePauseTime > 0 &&
			 sTracker.allowFastTasksDuringQueuePause) )
		{
			sTracker.typingChatBoxString = false;
			sTracker.chatBoxActive = false;
			// Trigger restoring mouse look for games like Pantheon
			// that drop it while using the chat box
			sTracker.mouseLookAutoRestoreTimer =
				kConfig.mouseLookAutoRestoreTime;
		}
	}


	// Process keysWantDown
	// --------------------
	BitArray<0xFF> aDesiredKeysDown; aDesiredKeysDown.reset();
	if( sTracker.mouseMode == eMouseMode_LookTurn &&
		sTracker.keysHeldDown.test(VK_RBUTTON) )
	{// Keep holding right mouse button while eMouseMode_LookTurn is active
		aDesiredKeysDown.set(VK_RBUTTON);
	}
	if( sTracker.mouseMode == eMouseMode_LookOnly &&
		sTracker.keysHeldDown.test(VK_LBUTTON) )
	{// Keep holding left mouse button while eMouseMode_LookOnly is active
		aDesiredKeysDown.set(VK_LBUTTON);
	}
	bool hasNonPressedKeyThatWantsHeldDown = false;
	int aPressedKeysDesiredMods = 0;
	for(KeysWantDownMap::iterator itr =
		sTracker.keysWantDown.begin(), next_itr = itr;
		itr != sTracker.keysWantDown.end(); itr = next_itr)
	{
		++next_itr;
		const int aVKey = itr->first;
		const int aBaseVKey = aVKey & kVKeyMask;
		const int aVKeyModFlags = aVKey & kVKeyModsMask;
		const bool pressed = itr->second.pressed;

		if( itr->second.depth <= 0 )
		{// Doesn't want to be pressed any more
			itr->second.pressed = false;
			// If not queued either, can erase from tracking map
			if( itr->second.queued <= 0 )
				next_itr = sTracker.keysWantDown.erase(itr);
			continue;
		}

		if( isMouseButton(aVKey) && !sTracker.mouseAllowMidJumpControl &&
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
			!sTracker.keysHeldDown.test(aBaseVKey) &&
			(sTracker.nextQueuedKey == 0 ||
			 (sTracker.nextQueuedKey & kVKeyModsMask) == aVKeyModFlags) )
		{// Doesn't need a change in mod keys and not held, so can press safely
			aDesiredKeysDown.set(VK_SHIFT, !!(aVKey & kVKeyShiftFlag));
			aDesiredKeysDown.set(VK_CONTROL, !!(aVKey & kVKeyCtrlFlag));
			aDesiredKeysDown.set(VK_MENU, !!(aVKey & kVKeyAltFlag));
			aDesiredKeysDown.set(VK_LWIN, !!(aVKey & kVKeyWinFlag));
			aDesiredKeysDown.set(aBaseVKey);
			hasNonPressedKeyThatWantsHeldDown = true;
			continue;
		}

		// Needs a change in mod keys or to be released from a tap first
		// Take over queued key to do this, but only if another key isn't
		// already doing this and only for initial first press.
		if( sTracker.backupQueuedKey == 0 &&
			(!sTracker.nextQueuedKey || !pressed) )
		{
			DBG_ASSERT(!sTracker.chatBoxActive);
			sTracker.backupQueuedKey = sTracker.nextQueuedKey;
			sTracker.nextQueuedKey = dropTo<u16>(aVKey | kVKeyHoldFlag);
			hasNonPressedKeyThatWantsHeldDown = true;
			continue;
		}

		// If reached here, want to press the key but just can't right now!
		// Will have to wait and try again next frame
	}

	// If nothing queued or needs to be newly pressed, hold down any
	// modifier keys that any combo-keys wanted held down, as well as
	// any modifier keys assigned directly using kVKeyModKeyOnlyBase.
	// Don't do this while in the middle of a key sequence or string though.
	if( !sTracker.nextQueuedKey &&
		!hasNonPressedKeyThatWantsHeldDown &&
		sTracker.currTaskProgress == 0 )
	{
		aDesiredKeysDown.set(VK_SHIFT,
			!!(aPressedKeysDesiredMods & kVKeyShiftFlag));
		aDesiredKeysDown.set(VK_CONTROL,
			!!(aPressedKeysDesiredMods & kVKeyCtrlFlag));
		aDesiredKeysDown.set(VK_MENU,
			!!(aPressedKeysDesiredMods& kVKeyAltFlag));
		// Not for Windows key though since that could bring up Start menu
	}


	// Prepare for queued event (mouse jump and/or nextQueuedKey)
	// ----------------------------------------------------------
	bool readyForQueuedKey = sTracker.nextQueuedKey != 0;
	bool readyForMouseJump = sTracker.mouseJumpToHotspot;
	if( readyForMouseJump )
	{
		if( !sTracker.mouseAllowMidJumpControl )
		{
			aDesiredKeysDown.reset(VK_LBUTTON);
			aDesiredKeysDown.reset(VK_MBUTTON);
			aDesiredKeysDown.reset(VK_RBUTTON);
			if( sTracker.keysHeldDown.test(VK_LBUTTON) ||
				sTracker.keysHeldDown.test(VK_MBUTTON) ||
				sTracker.keysHeldDown.test(VK_RBUTTON) )
			{
				readyForMouseJump = false;
			}
		}
		if( gAppRunTime < sTracker.mouseJumpAllowedTime )
			readyForMouseJump = false;
	}
	if( readyForQueuedKey )
	{
		const int aVKey = sTracker.nextQueuedKey;
		const int aBaseVKey = aVKey & kVKeyMask;
		DBG_ASSERT(aBaseVKey != 0); // otherwise somehow got flags but no key!
		const bool press = !(aVKey & kVKeyReleaseFlag);
		const bool forced = !press && (aVKey & kVKeyHoldFlag);
		// Make sure desired modifier keys match those of the queued key
		aDesiredKeysDown.set(VK_SHIFT, !!(aVKey & kVKeyShiftFlag));
		aDesiredKeysDown.set(VK_CONTROL, !!(aVKey & kVKeyCtrlFlag));
		aDesiredKeysDown.set(VK_MENU, !!(aVKey & kVKeyAltFlag));
		aDesiredKeysDown.set(VK_LWIN, !!(aVKey & kVKeyWinFlag));
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
		// Extra rules may apply for mouse buttons being initially clicked
		if( isMouseButton(aBaseVKey) && press )
		{
			if( sTracker.mouseMode == eMouseMode_Hide &&
				sTracker.mouseModeWanted == eMouseMode_Hide &&
				!sTracker.mouseJumpToHotspot )
			{// In hiding spot - need to restore cursor pos first
				sTracker.mouseJumpDest =
					InputMap::getHotspot(eSpecialHotspot_LastCursorPos);
				sTracker.mouseJumpToMode = eMouseMode_Cursor;
				sTracker.mouseJumpToHotspot = true;
				sTracker.mouseJumpInterpolate = false;
				sTracker.mouseAllowMidJumpControl = false;
				readyForQueuedKey = false;
			}
			if( sTracker.mouseJumpToHotspot &&
				sTracker.mouseJumpToMode == eMouseMode_Hide)
			{// Attempting to hide - abort and let click through first
				sTracker.mouseJumpToHotspot = false;
				readyForMouseJump = false;
			}
			if( sTracker.mouseJumpToHotspot )
			{// Wait until jump finishes before allowing a mouse click
				readyForQueuedKey = false;
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
		but this will obviously make the controls feel less responsive.
	*/


	// Apply normal mouse motion
	// -------------------------
	offsetMousePos();
	if( !sTracker.queue.mouseJumpQueued() &&
		!sTracker.mouseJumpToHotspot &&
		sTracker.mouseMode == eMouseMode_Cursor )
	{// Track cursor position changes in cursor mode when not jumping
		InputMap::modifyHotspot(
			eSpecialHotspot_LastCursorPos,
			WindowManager::overlayPosToHotspot(
				WindowManager::mouseToOverlayPos(true)));
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
			setKeyDown(aVKey, false);
	}
	const int aModKeysHeldAsFlags = modKeysHeldAsFlags();
	for(int aVKey = aDesiredKeysDown.firstSetBit();
		aVKey < aDesiredKeysDown.size();
		aVKey = aDesiredKeysDown.nextSetBit(aVKey+1))
	{
		if( !sTracker.keysHeldDown.test(aVKey) &&
			setKeyDown(aVKey, true) == eResult_Ok )
		{
			const u16 aKeyJustPressed =
				u16((aVKey & 0xFFFF) | aModKeysHeldAsFlags);
			KeysWantDownMap::iterator itr =
				sTracker.keysWantDown.find(aKeyJustPressed);
			if( itr != sTracker.keysWantDown.end() )
				itr->second.pressed = true;
		}
	}


	// Jump mouse cursor if requested
	// ------------------------------
	if( readyForMouseJump )
	{
		if( sTracker.mouseJumpInterpolate )
			trailMouseToHotspot(sTracker.mouseJumpDest);
		else
			jumpMouseToHotspot(sTracker.mouseJumpDest);
		// .mouseJumpToHotspot flag will be cleared once verified next update
	}


	// Send queued key
	// ---------------
	if( readyForQueuedKey )
	{
		int aVKey = sTracker.nextQueuedKey & (kVKeyMask | kVKeyModsMask);
		int aVKeyBase = aVKey & kVKeyMask;
		const bool wantRelease = !!(sTracker.nextQueuedKey & kVKeyReleaseFlag);

		if( setKeyDown(aVKeyBase, !wantRelease) == eResult_Ok )
		{
			const bool wantHold = !!(sTracker.nextQueuedKey & kVKeyHoldFlag);
			sTracker.nextQueuedKey = 0;
			if( wantRelease )
			{
				if( wantHold ) // + wantRelease = kVKeyForceReleaseFlag
				{// Stop holding this key and any combo-keys with same base
					for(KeysWantDownMap::iterator itr =
						sTracker.keysWantDown.begin();
						itr != sTracker.keysWantDown.end(); ++itr)
					{
						if( (itr->first & kVKeyMask) == aVKeyBase )
						{
							itr->second.depth = 0;
							itr->second.pressed = false;
						}
					}
				}
			}
			else if( wantHold )
			{// Flag key as having been pressed
				KeysWantDownMap::iterator itr =
					sTracker.keysWantDown.find(dropTo<u16>(aVKey));
				if( itr != sTracker.keysWantDown.end() )
					itr->second.pressed = true;
			}
			else if( isMouseButton(aVKeyBase) )
			{// Mouse button that wanted to be "clicked" once in a sequence
				// Should release the mouse button for next queued key action
				sTracker.nextQueuedKey = dropTo<u16>(aVKey | kVKeyReleaseFlag);
				if( sTracker.mouseMode == eMouseMode_PostJump )
				{// If jumped first then clicked, next restore previous pos
					sTracker.mouseMode = eMouseMode_JumpClicked;
				}
			}
		}
	}


	// Dispatch input to system
	// ------------------------
	flushInputVector();
}


void sendKeyCommand(const Command& theCommand)
{
	// These values only valid for certain command types
	const int aVKey = theCommand.vKey;
	const int aBaseVKey = aVKey & kVKeyMask;

	switch(theCommand.type)
	{
	case eCmdType_Empty:
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		// Do nothing
		break;
	case eCmdType_SignalOnly:
		// Do nothing but fire off signal
		fireSignal(theCommand.signalID);
		break;
	case eCmdType_PressAndHoldKey:
		DBG_ASSERT(aBaseVKey != 0);
		DBG_ASSERT((aVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
		{// Queue or try to press immediately
			KeyWantDownStatus& aKeyStatus =
				sTracker.keysWantDown.findOrAdd(dropTo<u16>(aVKey));
			if( isSafeAsyncKey(aVKey) && aKeyStatus.queued <= 0 )
			{// Can possibly press right away
				if( aKeyStatus.depth > 0 )
				{// Already have another request to hold this key
					++aKeyStatus.depth;
					break;
				}
				if( setKeyDown(aBaseVKey, true) == eResult_Ok )
				{// Was able to press the key now
					aKeyStatus.depth = 1;
					aKeyStatus.pressed = true;
					aKeyStatus.queued = 0;
					fireSignal(theCommand.signalID);
					break;
				}
			}
			// Add to queue
			++aKeyStatus.queued;
			sTracker.queue.push_back(theCommand);
		}
		break;
	case eCmdType_ReleaseKey:
		DBG_ASSERT(aBaseVKey != 0);
		DBG_ASSERT((aVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
		{// Queue or try to press immediately
			KeyWantDownStatus& aKeyStatus =
				sTracker.keysWantDown.findOrAdd(dropTo<u16>(aVKey));
			if( aKeyStatus.queued <= 0 )
			{// Try releasing the key right away instead of queueing it
				if( tryQuickReleaseHeldKey(aVKey, aKeyStatus) )
					break;
			}
			sTracker.queue.push_back(theCommand);
		}
		break;
	case eCmdType_TapKey:
		DBG_ASSERT(aBaseVKey != 0);
		DBG_ASSERT((aVKey & ~(kVKeyMask | kVKeyModsMask)) == 0);
		{// Try tapping the key right away instead of queueing it
			if( isSafeAsyncKey(aVKey) &&
				!sTracker.keysHeldDown.test(aBaseVKey) &&
				setKeyDown(aBaseVKey, true) == eResult_Ok )
			{// Was able to press the key now, don't need to queue it!
				fireSignal(theCommand.signalID);
				break;
			}
		}
		sTracker.queue.push_back(theCommand);
		break;
	case eCmdType_VKeySequence:
	case eCmdType_ChatBoxString:
		sTracker.queue.push_back(theCommand);
		break;
	default:
		DBG_ASSERT(false && "Invalid command type for sendkeyCommand()!");
	}
}


void setMouseMode(EMouseMode theMouseMode)
{
	DBG_ASSERT(theMouseMode <= eMouseMode_Hide);
	if( theMouseMode != sTracker.mouseModeRequested )
	{
		sTracker.mouseModeRequested = theMouseMode;
		sTracker.mouseLookAutoRestoreTimer = 0;
	}
	if( theMouseMode == eMouseMode_Default )
	{
		sTracker.mouseModeWanted = eMouseMode_Cursor;
	}
	else if( theMouseMode == eMouseMode_LookTurn )
	{
		if( sTracker.mouseModeWanted == eMouseMode_AutoLook )
			sTracker.mouseModeWanted = eMouseMode_AutoToTurn;
		else if( sTracker.mouseModeWanted == eMouseMode_AutoRunLook )
			sTracker.mouseModeWanted = eMouseMode_RunToTurn;
		else
			sTracker.mouseModeWanted = theMouseMode;
	}
	else if( theMouseMode == eMouseMode_AutoLook &&
			 sTracker.mouseModeWanted == eMouseMode_AutoRunLook )
	{
		sTracker.mouseModeWanted = eMouseMode_RunToAuto;
	}
	else
	{
		sTracker.mouseModeWanted = theMouseMode;
	}
	if( sTracker.mouseModeWanted != eMouseMode_Cursor )
		gHotspotsGuideMode = eHotspotGuideMode_Disabled;
}


void moveMouse(int dx, int dy, int lookX, bool digital)
{
	static double sMagnitudeAccelFactor = 0;
	static double sPrevMagnitude = 0;
	static int sMouseXSubPixel = 0;
	static int sMouseYSubPixel = 0;

	const bool kForMouseLook =
		sTracker.mouseMode == eMouseMode_LookOnly ||
		sTracker.mouseMode == eMouseMode_LookTurn ||
		sTracker.mouseMode == eMouseMode_LookReady ||
		sTracker.keysHeldDown.test(VK_RBUTTON);

	// Get magnitude of desired mouse motion in 0 to 1.0 range
	double aMagnitude = std::sqrt(double(dx) * dx + dy * dy) / 255.0;

	// Apply deadzone and saturation to magnitude
	const double kDeadZone = kForMouseLook
		? kConfig.mouseLookDeadzone : kConfig.cursorDeadzone;
	if( aMagnitude <= kDeadZone )
	{
		sMagnitudeAccelFactor = dx = dy = 0;
	}
	else
	{
		aMagnitude -= kDeadZone;
		const double kRange = kForMouseLook
			? kConfig.mouseLookRange : kConfig.cursorRange;
		aMagnitude = min(aMagnitude / kRange, 1.0);

		// Apply adjustments to allow for low-speed fine control
		if( digital && kConfig.mouseDPadAccel )
		{// Apply acceleration to magnitude
			sTracker.mouseDigitalVel = min<int>(
				kMouseMaxAccelVel,
				sTracker.mouseDigitalVel +
					kConfig.mouseDPadAccel * 4 * gAppFrameTime);
			aMagnitude *= double(sTracker.mouseDigitalVel) / kMouseMaxAccelVel;
		}
		else if( aMagnitude < 1.0 )
		{// Apply response curve
			const double kCurve = kForMouseLook
				? kConfig.mouseLookCurve : kConfig.cursorCurve;
			aMagnitude = std::pow(aMagnitude, kCurve);
		}

		// Get angle of desired mouse motion
		const double anAngle = atan2(double(dy), double(dx));

		// Apply acceleration and braking when moving a cursor by pressing
		// fully on analog stick in order to help prevent over-shooting target
		if( !digital &&
			((!kForMouseLook && kConfig.cursorAccel != 0) ||
			 (kForMouseLook && kConfig.mouseLookAccel != 0)) )
		{
			// Apply braking only for cursor movement
			if( !kForMouseLook )
			{
				const double kHighMagBrakeThreshold = 0.0033 * gAppFrameTime;
				const double anOldMagnitude = aMagnitude;
				// Stop immediately on sudden decrease in magnitude
				// Assumption is destination has been reached and should avoid
				// "drifting" past it due to time taken for stick to re-center
				if( aMagnitude < sPrevMagnitude - kHighMagBrakeThreshold )
					aMagnitude = 0;
				sPrevMagnitude = anOldMagnitude;
			}

			// Restart acceleration during and right after mouse jumps
			if( sTracker.mouseJumpToHotspot )
				sMagnitudeAccelFactor = 0;

			const double kHighMagThreshold = 0.5;
			sMagnitudeAccelFactor += gAppFrameTime *
				(kForMouseLook ?
					kConfig.mouseLookAccel : kConfig.cursorAccel);
			sMagnitudeAccelFactor = min(sMagnitudeAccelFactor, aMagnitude);
			// Only clamp to acceleration at high magnitudes
			if( aMagnitude > kHighMagThreshold )
			{
				aMagnitude = min(aMagnitude, sMagnitudeAccelFactor);
				aMagnitude = max(aMagnitude, kHighMagThreshold);
			}
		}

		// Convert back into integer dx & dy w/ 32,768 range
		dx = int(32768.0 * aMagnitude * cos(anAngle));
		dy = int(32768.0 * aMagnitude * sin(anAngle));

		// Apply speed setting
		const int kCursorXSpeed = kForMouseLook
			? kConfig.mouseLookXSpeed : kConfig.cursorXSpeed;
		dx = dx * kCursorXSpeed / kMouseMaxSpeed * gAppFrameTime;
		const int kCursorYSpeed = kForMouseLook
			? kConfig.mouseLookYSpeed : kConfig.cursorYSpeed;
		dy = dy * kCursorYSpeed / kMouseMaxSpeed * gAppFrameTime;
	}

	// Add lookX to dx in mouselook modes while moving
	if( lookX != 0 && sTracker.moveKeysHeld.any() &&
		(sTracker.mouseMode == eMouseMode_LookTurn ||
		 sTracker.mouseMode == eMouseMode_LookReady) )
	{
		aMagnitude = abs(lookX) / 255.0;
		if( aMagnitude > kConfig.moveLookDeadzone )
		{
			aMagnitude -= kConfig.moveLookDeadzone;
			aMagnitude = min(aMagnitude / kConfig.moveLookRange, 1.0);
			lookX = int(32768.0 * (lookX < 0 ? -aMagnitude : aMagnitude));
			lookX = lookX * kConfig.moveLookSpeed;
			lookX = lookX / kMouseMaxSpeed * gAppFrameTime;
			dx += lookX;
		}
	}
	
	// Possibly track and re-apply potentially chopped-off sub-pixel motion
	// Sign of result of operator%() w/ negative dividend may differ by
	// compiler, hence the extra sign checks in this section
	if( kForMouseLook || (dx && abs(dx) < kMouseToPixelDivisor) )
	{
		dx += sMouseXSubPixel;
		if( dx < 0 )
			sMouseXSubPixel = -((-dx) % kMouseToPixelDivisor);
		else
			sMouseXSubPixel = dx % kMouseToPixelDivisor;
	}
	else
	{
		sMouseXSubPixel = 0;
	}

	if( kForMouseLook || (dy && abs(dy) < kMouseToPixelDivisor) )
	{
		dy += sMouseYSubPixel;
		if( dy < 0 )
			sMouseYSubPixel = -((-dy) % kMouseToPixelDivisor);
		else
			sMouseYSubPixel = dy % kMouseToPixelDivisor;
	}
	else
	{
		sMouseYSubPixel = 0;
	}

	// Convert to pixel values (truncate sub-pixel values)
	sTracker.mouseVelX += dx / kMouseToPixelDivisor;
	sTracker.mouseVelY += dy / kMouseToPixelDivisor;
}


void moveMouseTo(const Command& theCommand)
{
	ECommandType aCmdType = theCommand.type;
	if( theCommand.andClick )
		aCmdType = eCmdType_MouseClickAtHotspot;

	Hotspot aDestHotspot;
	switch(theCommand.type)
	{
	case eCmdType_Empty:
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		// Do nothing
		return;
	case eCmdType_MoveMouseToHotspot:
	case eCmdType_MouseClickAtHotspot:
		aDestHotspot = InputMap::getHotspot(theCommand.hotspotID);
		break;
	case eCmdType_MoveMouseToMenuItem:
		gHotspotsGuideMode = eHotspotGuideMode_Disabled;
		aDestHotspot = WindowManager::hotspotForMenuItem(
			theCommand.menuID, theCommand.menuItemID);
		break;
	case eCmdType_MoveMouseToOffset:
		aDestHotspot =
			InputMap::getHotspot(eSpecialHotspot_LastCursorPos);
		{
			// Counter effect of gWindowUIScale on base jump distance
			const int anOffsetDist = int(
				gWindowUIScale < 1.0
					? ceil(kConfig.offsetHotspotDist / gWindowUIScale) :
				gWindowUIScale > 1.0
					? floor(kConfig.offsetHotspotDist / gWindowUIScale) :
				kConfig.offsetHotspotDist);
			int aDestHotspotXOffset = aDestHotspot.x.offset;
			int aDestHotspotYOffset = aDestHotspot.y.offset;
			switch(theCommand.dir)
			{
			case eCmd8Dir_L:
			case eCmd8Dir_UL:
			case eCmd8Dir_DL:
				aDestHotspotXOffset -= anOffsetDist;
				break;
			case eCmd8Dir_R:
			case eCmd8Dir_UR:
			case eCmd8Dir_DR:
				aDestHotspotXOffset += anOffsetDist;
				break;
			}
			switch(theCommand.dir)
			{
			case eCmd8Dir_U:
			case eCmd8Dir_UL:
			case eCmd8Dir_UR:
				aDestHotspotYOffset -= anOffsetDist;
				break;
			case eCmd8Dir_D:
			case eCmd8Dir_DL:
			case eCmd8Dir_DR:
				aDestHotspotYOffset += anOffsetDist;
				break;
			}
			aDestHotspot.x.offset = s16(clamp(
				aDestHotspotXOffset, -0x8000, 0x7FFF));
			aDestHotspot.y.offset = s16(clamp(
				aDestHotspotYOffset, -0x8000, 0x7FFF));
		}
		break;
	default:
		DBG_ASSERT(false && "Invalid command type for moveMouseTo()!");
		return;
	}

	Command aCmd; aCmd.type = aCmdType;
	aCmd.hotspot.x = aDestHotspot.x;
	aCmd.hotspot.y = aDestHotspot.y;
	sTracker.queue.push_back(aCmd);
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
	if( digital && kConfig.mouseDPadAccel )
	{// Apply acceleration to magnitude
		sTracker.mouseDigitalVel = min<int>(
			kMouseMaxAccelVel,
			sTracker.mouseDigitalVel +
				kConfig.mouseDPadAccel * 4 * gAppFrameTime);
		aMagnitude *= double(sTracker.mouseDigitalVel) / kMouseMaxAccelVel;
	}

	// Convert back into integer dy w/ 32,768 range
	dy = dy < 0 ? int(-32768.0 * aMagnitude) : int(32768.0 * aMagnitude);

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


void jumpMouseWheel(ECommandDir theDir, int theCount)
{
	DBG_ASSERT(theCount > 0);
	if( theDir == eCmdDir_Up )
	{
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.mouseData = WHEEL_DELTA * theCount;
		anInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
		sTracker.inputs.push_back(anInput);
	}
	else if( theDir == eCmdDir_Down )
	{
		Input anInput;
		anInput.type = INPUT_MOUSE;
		anInput.mi.mouseData = -WHEEL_DELTA * theCount;
		anInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
		sTracker.inputs.push_back(anInput);
	}
}


void moveCharacter(int move, int turn, int strafe, bool autoRun, bool lock)
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

	// Apply move straight bias setting
	const double kXRange = M_PI * min(0.375,
		0.125 + (0.5 * (1.0 - kConfig.moveStraightBias)));
	const double kYRange = M_PI * min(0.375,
		0.125 + (0.5 * kConfig.moveStraightBias));

	// Calculate which movement actions, if any, should now apply
	enum EMoveKey
	{
		eMoveKey_F = eSpecialKey_MoveF - eSpecialKey_FirstMove,
		eMoveKey_B = eSpecialKey_MoveB - eSpecialKey_FirstMove,
		eMoveKey_TL = eSpecialKey_TurnL - eSpecialKey_FirstMove,
		eMoveKey_TR = eSpecialKey_TurnR - eSpecialKey_FirstMove,
		eMoveKey_SL = eSpecialKey_StrafeL - eSpecialKey_FirstMove,
		eMoveKey_SR = eSpecialKey_StrafeR - eSpecialKey_FirstMove,
		eMoveKey_Num = eSpecialKey_MoveNum,
	};
	BitArray<eMoveKey_Num> moveKeysWantDown;
	moveKeysWantDown.reset();
	moveKeysWantDown.set(eMoveKey_TL,
		applyMoveTurn &&
			(aTurnAngle < -(M_PI - kXRange) ||
			 aTurnAngle > M_PI - kXRange));

	moveKeysWantDown.set(eMoveKey_TR,
		applyMoveTurn && aTurnAngle > -kXRange && aTurnAngle < kXRange);

	moveKeysWantDown.set(eMoveKey_SL,
		applyMoveStrafe &&
			(aStrafeAngle < -(M_PI - kXRange) ||
			 aStrafeAngle > M_PI - kXRange));

	moveKeysWantDown.set(eMoveKey_SR,
		applyMoveStrafe && aStrafeAngle > -kXRange && aStrafeAngle < kXRange);

	// For move forward/back, use the virtual stick that had the greatest X
	// motion in order to make sure a proper circular deadzone is used.
	moveKeysWantDown.set(
		eMoveKey_F,
		(applyMoveTurn && abs(turn) >= abs(strafe) &&
			aTurnAngle > M_PI * 0.5 - kYRange &&
			aTurnAngle < M_PI * 0.5 + kYRange) ||
		(applyMoveStrafe && abs(strafe) >= abs(turn) &&
			aStrafeAngle > M_PI * 0.5 - kYRange &&
			aStrafeAngle < M_PI * 0.5 + kYRange));

	moveKeysWantDown.set(
		eMoveKey_B,
		(applyMoveTurn && abs(turn) >= abs(strafe) &&
			aTurnAngle < -M_PI * 0.5 + kYRange &&
			aTurnAngle > -M_PI * 0.5 - kYRange) ||
		(applyMoveStrafe && abs(strafe) >= abs(turn) &&
			aStrafeAngle < -M_PI * 0.5 + kYRange &&
			aStrafeAngle > -M_PI * 0.5 - kYRange));

	if( lock )
	{// Send desired movement first, then lock it at end of this function
		sTracker.autoRunMode = eAutoRunMode_Off;
		autoRun = moveKeysWantDown.none();
	}
	if( autoRun )
	{// Don't necessarily send the auto run key right away...
		sTracker.autoRunMode = eAutoRunMode_Queued;
		autoRun = false;
	}
	switch(sTracker.autoRunMode)
	{
	case eAutoRunMode_Queued:
		// Wait until release forward to begin auto-run
		if( !moveKeysWantDown.test(eMoveKey_F) )
		{
			autoRun = true;
			// Stop holding back so don't cancel it immediately
			moveKeysWantDown.reset(eMoveKey_B);
		}
		break;
	case eAutoRunMode_Started:
		// Prevent forward or back movement until release and press again
		if( !moveKeysWantDown.test(eMoveKey_F) &&
			!moveKeysWantDown.test(eMoveKey_B) )
		{
			sTracker.autoRunMode = eAutoRunMode_Active;
		}
		else
		{
			moveKeysWantDown.reset(eMoveKey_F);
			moveKeysWantDown.reset(eMoveKey_B);
		}
		break;
	case eAutoRunMode_Active:
		// Apply an extra deadzone to back/forward to prevent early cancel
		if( abs(move) < kConfig.cancelAutoRunDeadzone )
		{
			moveKeysWantDown.reset(eMoveKey_F);
			moveKeysWantDown.reset(eMoveKey_B);
		}
		break;
	case eAutoRunMode_StartLockX:
		// Wait until release X axis so can cancel by re-press
		if( !moveKeysWantDown.test(eMoveKey_TL) &&
			!moveKeysWantDown.test(eMoveKey_TR) &&
			!moveKeysWantDown.test(eMoveKey_SL) &&
			!moveKeysWantDown.test(eMoveKey_SR) )
		{
			sTracker.autoRunMode = eAutoRunMode_LockedX;
		}
		break;
	case eAutoRunMode_StartLockY:
		// Wait until release Y axis so can cancel by re-press
		if( !moveKeysWantDown.test(eMoveKey_F) &&
			!moveKeysWantDown.test(eMoveKey_B) )
		{
			sTracker.autoRunMode = eAutoRunMode_LockedY;
			if( InputMap::keyForSpecialAction(eSpecialKey_AutoRun) &&
				!sTracker.moveKeysHeld.test(eMoveKey_B) )
			{// Switch to auto-run mode for better compatibility w/ chat box
				sTracker.autoRunMode = eAutoRunMode_Active;
				autoRun = true;
			}
		}
		break;
	case eAutoRunMode_StartLockXY:
		// Wait until release all movement so can cancel by re-press
		if( moveKeysWantDown.none() )
			sTracker.autoRunMode = eAutoRunMode_LockedXY;
		break;
	case eAutoRunMode_LockedX:
		// Once push X past increased deadzone, stop locked movement
		if( max(abs(turn), abs(strafe)) >= kConfig.cancelAutoRunDeadzone )
			sTracker.autoRunMode = eAutoRunMode_Off;
		break;
	case eAutoRunMode_LockedY:
		// Once push Y past increased deadzone, stop locked movement
		if( abs(move) >= kConfig.cancelAutoRunDeadzone )
			sTracker.autoRunMode = eAutoRunMode_Off;
		break;
	case eAutoRunMode_LockedXY:
		// Once push X or Y past increased deadzone, stop locked movement
		if( max(abs(turn), abs(strafe)) >= kConfig.cancelAutoRunDeadzone )
			sTracker.autoRunMode = eAutoRunMode_Off;
		else if( abs(move) >= kConfig.cancelAutoRunDeadzone )
			sTracker.autoRunMode = eAutoRunMode_Off;
		break;
	}

	if( sTracker.autoRunMode == eAutoRunMode_StartLockX ||
		sTracker.autoRunMode == eAutoRunMode_StartLockXY ||
		sTracker.autoRunMode == eAutoRunMode_LockedX ||
		sTracker.autoRunMode == eAutoRunMode_LockedXY )
	{// Continue using already-held keys for x axis
		//moveKeysWantDown.set(eMoveKey_TL, sTracker.moveKeysHeld.test(eMoveKey_TL));
		//moveKeysWantDown.set(eMoveKey_TR, sTracker.moveKeysHeld.test(eMoveKey_TR));
		moveKeysWantDown.set(eMoveKey_SL, sTracker.moveKeysHeld.test(eMoveKey_SL));
		moveKeysWantDown.set(eMoveKey_SR, sTracker.moveKeysHeld.test(eMoveKey_SR));
	}
	if( sTracker.autoRunMode == eAutoRunMode_StartLockY ||
		sTracker.autoRunMode == eAutoRunMode_StartLockXY ||
		sTracker.autoRunMode == eAutoRunMode_LockedY ||
		sTracker.autoRunMode == eAutoRunMode_LockedXY )
	{// Continue using already-held keys for y axis
		moveKeysWantDown.set(eMoveKey_F, sTracker.moveKeysHeld.test(eMoveKey_F));
		moveKeysWantDown.set(eMoveKey_B, sTracker.moveKeysHeld.test(eMoveKey_B));
	}
	if( autoRun && !InputMap::keyForSpecialAction(eSpecialKey_AutoRun) )
	{// Force push forward when try auto run without an auto run key
		moveKeysWantDown.set(eMoveKey_F);
		moveKeysWantDown.reset(eMoveKey_B);
	}

	// Deal with strafe vs turn and interaction with mouse look mode
	const bool strafeLeftHasKey =
		InputMap::keyForSpecialAction(eSpecialKey_StrafeL) != 0;
	const bool strafeRightHasKey =
		InputMap::keyForSpecialAction(eSpecialKey_StrafeR) != 0;
	// If strafe keys not set, use turn keys instead of nothing
	if( !strafeLeftHasKey && moveKeysWantDown.test(eMoveKey_SL) )
	{
		moveKeysWantDown.reset(eMoveKey_SL);
		moveKeysWantDown.set(eMoveKey_TL);
	}
	if( !strafeRightHasKey && moveKeysWantDown.test(eMoveKey_SR) )
	{
		moveKeysWantDown.reset(eMoveKey_SR);
		moveKeysWantDown.set(eMoveKey_TR);
	}
	// If trying to turn in mouse look mode, convert to strafing if can,
	// or expect the game will do it if RMB is currently held down
	const bool useMouseLookMovement =
		sTracker.mouseModeRequested == eMouseMode_LookTurn ||
		sTracker.mouseModeRequested == eMouseMode_LookOnly ||
		sTracker.mouseModeRequested == eMouseMode_AutoLook ||
		sTracker.mouseModeRequested == eMouseMode_AutoRunLook ||
		sTracker.keysHeldDown.test(VK_RBUTTON);
	sTracker.mouseLookNeededToStrafe = false;
	if( useMouseLookMovement && moveKeysWantDown.test(eMoveKey_TL) )
	{
		if( strafeLeftHasKey )
		{// Manually convert turn key to strafe key
			moveKeysWantDown.reset(eMoveKey_TL);
			moveKeysWantDown.set(eMoveKey_SL);
		}
		else
		{
			sTracker.mouseLookAutoRestoreTimer = 0;
			if( !sTracker.keysHeldDown.test(VK_RBUTTON) )
			{// Don't turn or strafe until RMB held down to strafe with
				moveKeysWantDown.reset(eMoveKey_TL);
				sTracker.mouseLookNeededToStrafe = true;
			}
		}
	}
	if( useMouseLookMovement && moveKeysWantDown.test(eMoveKey_TR) )
	{
		if( strafeRightHasKey )
		{// Manually convert turn key to strafe key
			moveKeysWantDown.reset(eMoveKey_TR);
			moveKeysWantDown.set(eMoveKey_SR);
		}
		else
		{
			sTracker.mouseLookAutoRestoreTimer = 0;
			if( !sTracker.keysHeldDown.test(VK_RBUTTON) )
			{// Don't turn or strafe until RMB held down to strafe with
				moveKeysWantDown.reset(eMoveKey_TR);
				sTracker.mouseLookNeededToStrafe = true;
			}
		}
	}

	// Process changes to movement keys
	for(int aMoveKey = 0; aMoveKey < eMoveKey_Num; ++aMoveKey)
	{
		Command aCmd;
		aCmd.type = eCmdType_PressAndHoldKey;
		aCmd.vKey = InputMap::keyForSpecialAction(
			ESpecialKey(aMoveKey + eSpecialKey_FirstMove));
		if( !aCmd.vKey )
			aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = dropTo<u16>(
			eBtn_Num + aMoveKey + eSpecialKey_FirstMove);
		if( sTracker.typingChatBoxString && !lock && !autoRun &&
			aCmd.vKey && !isSafeAsyncKey(aCmd.vKey) &&
			!isMouseButton(aCmd.vKey) )
		{
			// Force release this movement key while typing so can re-press it
			// once done - otherwise, if continuously held down, it might not
			// trigger movement once the chat box closes, especially if this
			// same key is pressed as part of the chat box message.
			if( sTracker.moveKeysHeld.test(aMoveKey) )
			{
				aCmd.type = eCmdType_ReleaseKey;
				sendKeyCommand(aCmd);
				sTracker.moveKeysHeld.reset(aMoveKey);
			}
		}
		else if( moveKeysWantDown.test(aMoveKey) )
		{
			if( !sTracker.moveKeysHeld.test(aMoveKey) )
			{// Press this movement key now
				sendKeyCommand(aCmd);
				sTracker.moveKeysHeld.set(aMoveKey);
				// Re-pressing a movement key will un-sticky it
				sTracker.stickyMoveKeys.reset(aMoveKey);
			}
		}
		else
		{
			if( sTracker.moveKeysHeld.test(aMoveKey) )
			{// Release this movement key now that it is no longer wanted
				aCmd.type = eCmdType_ReleaseKey;
				sendKeyCommand(aCmd);
				sTracker.moveKeysHeld.reset(aMoveKey);
			}
			if( sTracker.stickyMoveKeys.test(aMoveKey) &&
				(!sTracker.chatBoxActive ||
				 isSafeAsyncKey(aCmd.vKey) ||
				 isMouseButton(aCmd.vKey)) )
			{// Key may be stuck down by game client - tap it again to release
				aCmd.type = eCmdType_TapKey;
				if( !isMouseButton(aCmd.vKey) )
					sendKeyCommand(aCmd);
				sTracker.stickyMoveKeys.reset(aMoveKey);
			}
		}
	}

	if( lock )
	{
		const bool lockY =
			sTracker.moveKeysHeld.test(eMoveKey_F) ||
			sTracker.moveKeysHeld.test(eMoveKey_B);
		const bool lockX =
			//sTracker.moveKeysHeld.test(eMoveKey_TL) ||
			//sTracker.moveKeysHeld.test(eMoveKey_TR) ||
			sTracker.moveKeysHeld.test(eMoveKey_SL) ||
			sTracker.moveKeysHeld.test(eMoveKey_SR);
		if( lockX && lockY )
			sTracker.autoRunMode = eAutoRunMode_StartLockXY;
		else if( lockX )
			sTracker.autoRunMode = eAutoRunMode_StartLockX;
		else if( lockY )
			sTracker.autoRunMode = eAutoRunMode_StartLockY;
	}
	else if( autoRun )
	{
		Command aCmd;
		aCmd.vKey = InputMap::keyForSpecialAction(eSpecialKey_AutoRun);
		if( aCmd.vKey )
		{
			aCmd.type = eCmdType_TapKey;
			aCmd.signalID = eBtn_Num + eSpecialKey_AutoRun;
			sendKeyCommand(aCmd);
			sTracker.autoRunMode = eAutoRunMode_Started;
		}
		else
		{
			sTracker.autoRunMode = eAutoRunMode_StartLockY;
		}
	}
}

#undef INPUT_DISPATCHER_DEBUG_PRINT_SENT_INPUT
#undef INPUT_DISPATCHER_DEBUG_PRINT_SENT_MOUSE_MOTION
#undef INPUT_DISPATCHER_SIMULATION_ONLY

} // InputDispatcher
