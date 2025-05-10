//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputMap.h"

#include "HotspotMap.h" // stringToHotspot()
#include "Profile.h"

namespace InputMap
{

// Uncomment this to print command assignments to debug window
//#define INPUT_MAP_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kInvalidID = 0xFFFF,
};

const char* kMainLayerSectionName = "Scheme";
const char* kLayerPrefix = "Layer.";
const char kComboLayerDeliminator = '+';
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
const char* kTypeKeys[] = { "Type", "Style" };
const char* kDisplayNameKeys[] = { "Label", "Title", "Name", "String" };
const char* kButtonAliasSectionName = "ButtonNames";
const char* kCommandAliasSectionName = "KeyBinds";
const char* kHotspotsSectionName = "Hotspots";
const char* kButtonRebindsPrefix = "Gamepad";
const char* k4DirMenuItemLabel[] = { "L", "R", "U", "D" }; // match ECommandDir!
const char* k4DirCmdSuffix[] = { " Left", " Right", " Up", " Down" };
DBG_CTASSERT(ARRAYSIZE(k4DirMenuItemLabel) == eCmdDir_Num);
const char* kHotspotsKeys[] =
	{ "Hotspots", "Hotspot", "KeyBindArray", "Array", "KeyBinds" };
const std::string kActionOnlyPrefix = "Just";
const std::string kSignalCommandPrefix = "When";

// These need to be in all upper case
const char* kHUDSettingsKey = "HUD";
const char* kHotspotArraysKey = "HOTSPOTS";
const char* kMouseModeKey = "MOUSE";
const char* kParentLayerKey = "PARENT";
const char* kPriorityKey = "PRIORITY";
const char* kMenuOpenKey = "AUTO";
const char* kMenuCloseKey = "BACK";
const std::string k4DirButtons[] =
{	"LS", "LSTICK", "LEFTSTICK", "LEFT STICK", "DPAD",
	"RS", "RSTICK", "RIGHTSTICK", "RIGHT STICK", "FPAD" };
const char* k4DirKeyNames[] = { "LEFT", "RIGHT", "UP", "DOWN" };

const char* kSpecialHotspotNames[] =
{
	"",						// eSpecialHotspot_None
	"MOUSELOOKSTART",		// eSpecialHotspot_MouseLookStart
	"MOUSEHIDDEN",			// eSpecialHotspot_MouseHidden
	"~",					// eSpecialHotspot_LastCursorPos
};
DBG_CTASSERT(ARRAYSIZE(kSpecialHotspotNames) == eSpecialHotspot_Num);

const char* kButtonActionPrefx[] =
{
	"",						// eBtnAct_Down
	"Press",				// eBtnAct_Press
	"Hold",					// eBtnAct_Hold
	"Tap",					// eBtnAct_Tap
	"Release",				// eBtnAct_Release
};
DBG_CTASSERT(ARRAYSIZE(kButtonActionPrefx) == eBtnAct_Num);

const char* kSpecialKeyNames[] =
{
	"SwapWindowMode",		// eSpecialKey_SwapWindowMode
	"PasteText",			// eSpecialKey_PasteText
	"AutoRun",				// eSpecialKey_AutoRun
	"MoveForward",			// eSpecialKey_MoveF
	"MoveBack",				// eSpecialKey_MoveB
	"TurnLeft",				// eSpecialKey_TurnL
	"TurnRight",			// eSpecialKey_TurnR
	"StrafeLeft",			// eSpecialKey_StrafeL
	"StrafeRight",			// eSpecialKey_StrafeR
};
DBG_CTASSERT(ARRAYSIZE(kSpecialKeyNames) == eSpecialKey_Num);


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct KeyBindArrayEntry
{
	Command cmd;
	u16 hotspotID;

	KeyBindArrayEntry() : hotspotID() {}
};

struct KeyBindArray : public std::vector<KeyBindArrayEntry>
{
	u16 signalID;
};

struct MenuItem
{
	std::string label;
	std::string altLabel;
	Command cmd;
};

struct Menu
{
	std::string label;
	std::string path;
	std::vector<MenuItem> items;
	MenuItem dirItems[eCmdDir_Num];
	Command autoCommand;
	Command backCommand;
	u16 parentMenuID;
	u16 rootMenuID;
	u16 hudElementID;

	Menu() :
		parentMenuID(kInvalidID),
		rootMenuID(kInvalidID),
		hudElementID(kInvalidID)
	{}
};

struct HUDElement
{
	std::string label;
	EHUDType type : 16;
	u16 menuID;
	union { u16 hotspotArrayID; u16 keyBindArrayID; u16 hotspotID; };
	// Visual details will be parsed by HUD module

	HUDElement(EHUDType theType = eHUDType_Num) :
		type(theType),
		menuID(kInvalidID),
		hotspotArrayID(kInvalidID)
	{}
};

struct ButtonActions
{
	//std::string label[eBtnAct_Num]; // TODO
	Command cmd[eBtnAct_Num];
	void initIfEmpty()
	{
		for(size_t i = 0; i < eBtnAct_Num; ++i)
		{
			if( cmd[i].type == eCmdType_Empty )
				cmd[i].type = eCmdType_Unassigned;
		}
	}
};
typedef VectorMap<EButton, ButtonActions> ButtonActionsMap;

struct ControlsLayer
{
	std::string label;
	ButtonActionsMap buttonMap;
	VectorMap<u16, Command> signalCommands;
	BitVector<> showHUD;
	BitVector<> hideHUD;
	BitVector<> enableHotspots;
	BitVector<> disableHotspots;
	EMouseMode mouseMode;
	u16 parentLayer;
	s8 priority;
	bool isComboLayer;

	ControlsLayer() :
		showHUD(),
		hideHUD(),
		enableHotspots(),
		disableHotspots(),
		mouseMode(eMouseMode_Default),
		parentLayer(),
		priority(),
		isComboLayer()
	{}
};

struct HotspotArray
{
	std::string label;
	float offsetScale;
	u16 first, last;
};

// Data used during parsing/building the map but deleted once done
struct InputMapBuilder
{
	std::vector<std::string> parsedString; // TODO - use assert first clear after technique instead of clear first
	VectorMap<ECommandKeyWord, size_t> keyWordMap;
	StringToValueMap<std::string> buttonAliases;
	StringToValueMap<Command> commandAliases;
	StringToValueMap<Command> specialKeyNameToCommandMap;
	StringToValueMap<u16> keyBindArrayNameToIdxMap;
	StringToValueMap<u16> hotspotNameToIdxMap;
	StringToValueMap<u16> hotspotArrayNameToIdxMap;
	BitVector<> elementsProcessed;
	std::string debugItemName;
	std::string debugSubItemName;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<Hotspot> sHotspots;
static std::vector<HotspotArray> sHotspotArrays;
static StringToValueMap<bool> sKeyStrings;
static std::vector<KeyBindArray> sKeyBindArrays;
static StringToValueMap<ControlsLayer> sLayers;
static VectorMap<std::pair<u16, u16>, u16> sComboLayers;
static StringToValueMap<Menu> sMenus;
static StringToValueMap<HUDElement> sHUDElements;
static u16 sSpecialKeys[eSpecialKey_Num];
static VectorMap<std::pair<u16, EButton>, u32> sButtonHoldTimes;
static u32 sDefaultButtonHoldTime = 400;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef INPUT_MAP_DEBUG_PRINT
#define mapDebugPrint(...) debugPrint("InputMap: " __VA_ARGS__)
#else
#define mapDebugPrint(...) ((void)0)
#endif

static u16 checkForComboVKeyName(std::string theKeyName)
{
	u16 result = 0;
	std::string aModKeyName;
	aModKeyName.push_back(theKeyName[0]);
	theKeyName = theKeyName.substr(1);
	while(theKeyName.size() > 1)
	{
		aModKeyName.push_back(theKeyName[0]);
		theKeyName = theKeyName.substr(1);
		const u8 aModKey = keyNameToVirtualKey(aModKeyName);
		if( aModKey != 0 &&
			(aModKey == VK_SHIFT ||
			 aModKey == VK_CONTROL ||
			 aModKey == VK_MENU ||
			 aModKey == VK_LWIN ||
			 aModKey == kVKeyForceRelease) )
		{// Found a valid modifier key
			switch(aModKey)
			{
			case VK_SHIFT:
				result |= kVKeyShiftFlag;
				break;
			case VK_CONTROL:
				result |= kVKeyCtrlFlag;
				break;
			case VK_MENU:
				result |= kVKeyAltFlag;
				break;
			case VK_LWIN:
				result |= kVKeyWinFlag;
				break;
			}
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(theKeyName))
			{// We have a valid key combo!
				result |= aMainKey;
				return result;
			}
			// Perhaps remainder is another mod+key, like ShiftCtrlA?
			if( u16 aPartialVKey = checkForComboVKeyName(theKeyName) )
			{
				result |= aPartialVKey;
				return eResult_Ok;
			}
			// No main key found to go with modifier key
			result = 0;
		}
	}

	// No valid modifier key found
	return 0;
}


static EResult checkForVKeySeqPause(
	const std::string& theKeyName,
	std::string& out,
	bool timeOnly = false)
{
	size_t aStrPos = 0;
	if( !timeOnly )
	{
		if( theKeyName.size() <= 1 )
			return eResult_NotFound;

		// See if starts with P(ause), W(ait), or D(elay)
		switch(theKeyName[0])
		{
		case 'P': case 'D': case 'W': aStrPos = 1; break;
		default: return eResult_NotFound;
		}

		if( theKeyName.compare(0, 5, "PAUSE") == 0 )
			aStrPos = 5;
		else if( theKeyName.compare(0, 5, "DELAY") == 0 )
			aStrPos = 5;
		else if( theKeyName.compare(0, 4, "WAIT") == 0 )
			aStrPos = 4;
		// If whole word is a valid name but nothing else remains,
		// then it is valid but we need a second word for timeOnly
		if( aStrPos == theKeyName.size() )
			return eResult_Incomplete;
	}

	typedef unsigned int u32;
	u32 aTime = 0;
	for(; aStrPos < theKeyName.size(); ++aStrPos)
	{
		const u8 c = theKeyName[aStrPos];
		// Check if is a valid integer digit
		if( c < '0' || c > '9' )
			return eResult_NotFound;
		const u32 aDigit = c - '0';
		aTime *= 10;
		aTime += aDigit;
		if( aTime > 0x3FFF )
		{
			aTime = 0x3FFF;
			logError("Pause time in a key sequence can not exceed 16 seconds!");
			break;
		}
	}

	// Delay of 0 is technically valid but doesn't add to the sequence
	if( aTime == 0 )
		return eResult_Ok;

	// Encode the special-case VK_PAUSE key and the delay amount as a 14-bit
	// number using the 2 bytes after it in the string (making sure each byte
	// has highest bit set so can't end up with either being 0 and acting as
	// a terminator for the string).
	out.push_back(VK_PAUSE);
	out.push_back(u8(((aTime >> 7) & 0x7F) | 0x80));
	out.push_back(u8((aTime & 0x7F) | 0x80));

	// Success!
	return eResult_Ok;
}


static EResult checkForVKeyHotspotPos(
	InputMapBuilder& theBuilder,
	const std::string& theKeyName,
	std::string& out,
	bool afterClickCommand)
{
	u16* aHotspotIdx = theBuilder.hotspotNameToIdxMap.find(theKeyName);
	if( !aHotspotIdx )
		return eResult_NotFound;

	if( *aHotspotIdx == 0 )
		return eResult_Incomplete;

	std::string suffix;
	if( afterClickCommand )
	{// Need to inject the jump command to before the click
		suffix.insert(suffix.begin(), out[out.size()-1]);
		out.erase(out.size()-1);
		while(!out.empty() &&
			(out[out.size()-1] == VK_SHIFT ||
			 out[out.size()-1] == VK_CONTROL ||
			 out[out.size()-1] == VK_MENU ||
			 out[out.size()-1] == VK_LWIN ||
			 out[out.size()-1] == kVKeyForceRelease))
		{
			suffix.insert(suffix.begin(), out[out.size()-1]);
			out.erase(out.size()-1);
		}
		out.push_back(kVKeyMouseJump);
	}

	// Encode the hotspot ID into 14-bit as in checkForVKeySeqPause()
	out.push_back(u8(((*aHotspotIdx >> 7) & 0x7F) | 0x80));
	out.push_back(u8((*aHotspotIdx & 0x7F) | 0x80));

	// Add back in the actual click if had to filter it out
	out += suffix;

	return eResult_Ok;
}


static std::string signalIDToString(u16 theSignalID)
{
	std::string result;
	if( theSignalID > 0 )
	{
		// Add the signal flag character
		result.push_back(kVKeyFireSignal);

		// Encode the signal ID into 14-bit as in checkForVKeySeqPause()
		result.push_back(u8(((theSignalID >> 7) & 0x7F) | 0x80));
		result.push_back(u8((theSignalID & 0x7F) | 0x80));
	}

	return result;
}


static u16 namesToVKey(const std::vector<std::string>& theNames)
{
	u16 result = 0;

	for(int aNameIdx = 0; aNameIdx < theNames.size(); ++aNameIdx)
	{
		if( result & kVKeyMask )
		{// Nothing else should be found after first non-modifier key
			result = 0;
			break;
		}
		const std::string& aName = upper(theNames[aNameIdx]);
		DBG_ASSERT(!aName.empty());
		const u8 aVKey = keyNameToVirtualKey(aName);
		switch(aVKey)
		{
		case 0:
			// Check if it's a modifier+key in one word like Shift2 or Alt1
			result = checkForComboVKeyName(aName);
			if( !result )
				return result;
			break;
		case VK_SHIFT:
			result |= kVKeyShiftFlag;
			break;
		case VK_CONTROL:
			result |= kVKeyCtrlFlag;
			break;
		case VK_MENU:
			result |= kVKeyAltFlag;
			break;
		case VK_LWIN:
			result |= kVKeyWinFlag;
			break;
		case VK_PAUSE:
		case kVKeyFireSignal:
		case kVKeyForceRelease:
		case kVKeyMouseJump:
		case kVKeySeqHasMouseJump:
		case kVKeyStartChatString:
			result = 0;
			return result;
		default:
			result |= aVKey;
			break;
		}
	}

	// If purely mod keys, add dummy base key
	if( result && !(result & kVKeyMask) )
		result |= kVKeyModKeyOnlyBase;

	return result;
}


static std::string namesToVKeySequence(
	InputMapBuilder& theBuilder,
	const std::vector<std::string>& theNames)
{
	std::string result;

	if( theNames.empty() )
		return result;

	bool hasMouseJump = false;
	bool expectingWaitTime = false;
	bool expectingJumpPos = false;
	for(int aNameIdx = 0; aNameIdx < theNames.size(); ++aNameIdx)
	{
		const std::string& aName = upper(theNames[aNameIdx]);
		DBG_ASSERT(!aName.empty());
		if( expectingWaitTime )
		{
			if( checkForVKeySeqPause(aName, result, true) != eResult_Ok )
			{// Didn't get wait time as expected - abort!
				result.clear();
				return result;
			}
			expectingWaitTime = false;
			continue;
		}
		else if( expectingJumpPos )
		{
			const EResult aResult = checkForVKeyHotspotPos(
				theBuilder, aName, result, false);
			if( aResult == eResult_Incomplete )
				continue;
			if( aResult == eResult_Ok )
			{
				hasMouseJump = true;
				expectingJumpPos = false;
				continue;
			}
			// Didn't get jump pos as expected - abort!
			result.clear();
			return result;
		}
		const u8 aVKey = keyNameToVirtualKey(aName);
		if( aVKey == 0 )
		{
			// If previous key was a mouse button, check for follow-up hotspot
			EResult aResult;
			if( !result.empty() &&
				(result[result.size()-1] == VK_LBUTTON ||
				 result[result.size()-1] == VK_MBUTTON ||
				 result[result.size()-1] == VK_RBUTTON)  )
			{
				aResult = checkForVKeyHotspotPos(
					theBuilder, aName, result, true);
				if( aResult == eResult_Ok )
					hasMouseJump = true;
				if( aResult != eResult_NotFound )
					continue;
			}

			// Check if it's a pause/delay/wait command
			aResult = checkForVKeySeqPause(aName, result);
			// Incomplete result means it WAS a wait, now need the time
			if( aResult == eResult_Incomplete )
				expectingWaitTime = true;
			if( aResult != eResult_NotFound )
				continue;

			// Check if it is an alias to another sequence
			if( Command* aCommandAlias = theBuilder.commandAliases.find(aName) )
			{
				if( aCommandAlias->type == eCmdType_SignalOnly )
				{
					result += signalIDToString(aCommandAlias->signalID);
					continue;
				}
				if( aCommandAlias->type == eCmdType_TapKey )
				{
					result += signalIDToString(aCommandAlias->signalID);
					const u16 aVKey = aCommandAlias->vKey;
					if( aVKey & kVKeyShiftFlag ) result += VK_SHIFT;
					if( aVKey & kVKeyCtrlFlag ) result += VK_CONTROL;
					if( aVKey & kVKeyAltFlag ) result += VK_MENU;
					if( aVKey & kVKeyWinFlag ) result += VK_LWIN;
					if( aVKey & kVKeyMask ) result += u8(aVKey & kVKeyMask);
					continue;
				}
				if( aCommandAlias->type == eCmdType_VKeySequence )
				{
					result += signalIDToString(aCommandAlias->signalID);
					DBG_ASSERT(
						aCommandAlias->keyStringID < sKeyStrings.size());
					const std::string& anEmbedSeq =
						sKeyStrings.keys()[aCommandAlias->keyStringID];
					DBG_ASSERT(!anEmbedSeq.empty());
					if( anEmbedSeq[0] == kVKeySeqHasMouseJump )
					{
						result += anEmbedSeq.substr(3);
						hasMouseJump = true;
					}
					else
					{
						result += anEmbedSeq;
					}
					continue;
				}
				if( aCommandAlias->type == eCmdType_ChatBoxString )
				{
					result += signalIDToString(aCommandAlias->signalID);
					DBG_ASSERT(
						aCommandAlias->keyStringID < sKeyStrings.size());
					result += kVKeyStartChatString;
					result += sKeyStrings.keys()[aCommandAlias->keyStringID];
					continue;
				}
			}

			// Check if it's a modifier+key in one word like Shift2 or Alt1
			if( u16 aComboVKey = checkForComboVKeyName(aName) )
			{
				if( aComboVKey & kVKeyShiftFlag )
					result.push_back(VK_SHIFT);
				if( aComboVKey & kVKeyCtrlFlag )
					result.push_back(VK_CONTROL);
				if( aComboVKey & kVKeyAltFlag )
					result.push_back(VK_MENU);
				if( aComboVKey & kVKeyWinFlag )
					result.push_back(VK_LWIN);
				result.push_back(u8(aComboVKey & kVKeyMask));
				continue;
			}

			// Can't figure this word out at all, abort!
			result.clear();
			return result;
		}
		else if( aVKey == kVKeyMouseJump )
		{
			// Get name of hotspot to jump cursor to next
			expectingJumpPos = true;
			result.push_back(aVKey);
		}
		else
		{
			result.push_back(aVKey);
		}
	}

	if( hasMouseJump )
	{
		std::string aStr;
		aStr.push_back(kVKeySeqHasMouseJump);
		aStr.push_back(char(u8(0x80)));
		aStr.push_back(char(u8(0x80)));
		// Determine if running this sequence will leave cursor in a different
		// position, which means jumping to a hotspot and NOT clicking on it.
		bool usesClick = false;
		for(int i = int(result.size()-1); i >= 0; --i)
		{
			switch(result[i])
			{
			case kVKeyMouseJump:
				if( !usesClick )
				{
					aStr[1] = result[i+1];
					aStr[2] = result[i+2];
					i = 0; // to break out of loop
				}
				break;
			case VK_LBUTTON:
			case VK_MBUTTON:
			case VK_RBUTTON:
				usesClick = true;
				break;
			case VK_PAUSE:
			case kVKeyStartChatString:
				usesClick = false;
				break;
			}
		}
		result = aStr + result;
	}

	return result;
}


static Command parseChatBoxMacro(const std::string& theString)
{
	DBG_ASSERT(!theString.empty());
	Command result;

	if( theString[0] != '/' && theString[0] != '>' )
		return result;

	// Treat as Do Nothing command if nothing after the / or >
	if( theString.length() <= 1 )
	{
		result.type = eCmdType_DoNothing;
		return result;
	}

	// If have no \n in string, set as normal ChatBoxString command
	size_t aLineBreakPos = theString.find("\\n");
	if( aLineBreakPos == std::string::npos )
	{
		result.type = eCmdType_ChatBoxString;
		result.keyStringID = sKeyStrings.findOrAddIndex(theString + "\r");
		return result;
	}

	// Treat multi-line macros as a VKey sequence of embedded strings
	std::string aVKeySeq;
	std::string aStrLeft = theString;
	for(;;)
	{
		std::string aStrToAdd = aStrLeft.substr(0, aLineBreakPos);
		if( aStrToAdd.length() > 1 )
		{
			aVKeySeq.push_back(kVKeyStartChatString);
			aVKeySeq += aStrToAdd + "\r";
		}

		if( aLineBreakPos == std::string::npos )
			break;

		aStrLeft = aStrLeft.substr(aLineBreakPos+2);
		if( aStrLeft.empty() )
			break;

		if( aStrLeft[0] != '<' && aStrLeft[0] != '/' )
			aStrLeft = std::string(">") + aStrLeft;
		aLineBreakPos = aStrLeft.find("\\n");
	}

	if( aVKeySeq.empty() )
	{
		result.type = eCmdType_DoNothing;
		return result;
	}

	result.type = eCmdType_VKeySequence;
	result.keyStringID = sKeyStrings.findOrAddIndex(aVKeySeq);
	return result;
}


static void createEmptyLayer(const std::string& theName)
{
	DBG_ASSERT(!theName.empty());
	ControlsLayer& aLayer = sLayers.findOrAdd(condense(theName));
	if( aLayer.label.empty() )
		aLayer.label = theName;
}


static void tryCreateComboLayer(const std::string& theComboName)
{
	DBG_ASSERT(!theComboName.empty());
	
	std::string aSecondLayerName = theComboName;
	std::string aFirstLayerName = breakOffItemBeforeChar(
		aSecondLayerName, kComboLayerDeliminator);
	if( aFirstLayerName.empty() || aSecondLayerName.empty() )
		return;

	const u16 aComboLayerID = sLayers.findIndex(condense(theComboName));
	if( aComboLayerID == 0 || aComboLayerID >= sLayers.size() )
		return;

	const u16 aFirstLayerID = sLayers.findIndex(condense(aFirstLayerName));
	if( aFirstLayerID == 0 || aFirstLayerID >= sLayers.size() )
		return;

	createEmptyLayer(aSecondLayerName);
	tryCreateComboLayer(aSecondLayerName);
	const u16 aSecondLayerID = sLayers.findIndex(condense(aSecondLayerName));
	if( aSecondLayerID == 0 || aSecondLayerID >= sLayers.size() )
		return;

	if( aFirstLayerID == aSecondLayerID )
	{
		logError("Specified same layer twice in combo layer name '%s'!",
			theComboName.c_str());
		return;
	}

	sLayers.vals()[aComboLayerID].isComboLayer = true;
	std::pair<u16, u16> aComboLayerKey(aFirstLayerID, aSecondLayerID);
	sComboLayers.setValue(aComboLayerKey, aComboLayerID);
}


static u16 getLayerID(const std::string& theName)
{
	u16 result = sLayers.findIndex(condense(theName));
	if( result >= sLayers.size() )
		result = 0;
	return result;
}


static u16 getOrCreateHUDElementID(
	InputMapBuilder& theBuilder,
	const std::string& theName,
	bool hasInputAssigned)
{
	DBG_ASSERT(!theName.empty());

	// Check if already exists, and if so return the ID
	u16 aHUDElementID = sHUDElements.findOrAddIndex(upper(theName));
	if( sHUDElements.vals()[aHUDElementID].type != eHUDType_Num )
		return aHUDElementID;

	// Initialize new HUD element
	HUDElement& aHUDElement = sHUDElements.vals()[aHUDElementID];

	const std::string& aMenuPath = kMenuPrefix + theName;
	const std::string& aHUDPath = kHUDPrefix + theName;

	// Try to figure out what type of HUD element / Menu this is
	std::string aHUDTypeName;
	for(int i = 0; aHUDTypeName.empty() && i < ARRAYSIZE(kTypeKeys); ++i)
		aHUDTypeName = Profile::getStr(aMenuPath, kTypeKeys[i]);
	for(int i = 0; aHUDTypeName.empty() && i < ARRAYSIZE(kTypeKeys); ++i)
		aHUDTypeName = Profile::getStr(aHUDPath, kTypeKeys[i]);
	if( aHUDTypeName.empty() )
	{
		logError(
			"Can't find '[%s%s]/%s =' property "
			"for item referenced by [%s]! "
			"Defaulting to type '%s'...",
			hasInputAssigned ? kMenuPrefix : kHUDPrefix,
			theName.c_str(),
			kTypeKeys[hasInputAssigned ? 1 : 0],
			theBuilder.debugItemName.c_str(),
			hasInputAssigned ? "List" : "Rectangle");
		aHUDElement.type =
			hasInputAssigned ? eMenuStyle_List : eHUDItemType_Rect;
	}
	else
	{
		aHUDElement.type = hudTypeNameToID(upper(aHUDTypeName));
		// Special-case - for "hotspot" type/style, have to infer Menu vs HUD
		if( aHUDElement.type == eMenuStyle_Hotspots && !hasInputAssigned )
			aHUDElement.type = eHUDType_Hotspot;
		if( aHUDElement.type >= eHUDType_Num )
		{
			logError(
				"Unrecognized '%s' specified for '[%s%s]/%s =' property "
				"for item referenced by [%s]! "
				"Defaulting to type '%s'...",
				aHUDTypeName.c_str(),
				hasInputAssigned ? kMenuPrefix : kHUDPrefix,
				theName.c_str(),
				kTypeKeys[hasInputAssigned ? 1 : 0],
				theBuilder.debugItemName.c_str(),
				hasInputAssigned ? "List" : "Rectangle");
			aHUDElement.type =
				hasInputAssigned ? eMenuStyle_List : eHUDItemType_Rect;
		}
		else if( hasInputAssigned &&
				 (aHUDElement.type < eMenuStyle_Begin ||
				  aHUDElement.type >= eMenuStyle_End) )
		{
			logError(
				"Attempting to assign a Menu navigation command to "
				"HUD Element '%s' of type '%s' which is not a menu! "
				"Changing it to a Menu of type List instead...",
				theName.c_str(), aHUDTypeName.c_str());
			aHUDElement.type = eMenuStyle_List;
		}
		else if( !hasInputAssigned &&
				 aHUDElement.type >= eMenuStyle_Begin &&
				 aHUDElement.type < eMenuStyle_End )
		{
			logError(
				"Menu '%s' referenced by [%s] "
				"has no buttons assigned to control it! "
				"The menu will appear as only a basic rectangle!",
				theName.c_str(),
				theBuilder.debugItemName.c_str());
			aHUDElement.type = eHUDItemType_Rect;
		}
	}

	{// Get display name if different than key name
		for(int i = 0; aHUDElement.label.empty() &&
			i < ARRAYSIZE(kDisplayNameKeys); ++i)
		{
			aHUDElement.label = Profile::getStr(
				aHUDPath, kDisplayNameKeys[i]);
		}
		for(int i = 0; aHUDElement.label.empty() &&
			i < ARRAYSIZE(kDisplayNameKeys); ++i)
		{
			aHUDElement.label = Profile::getStr(
				aMenuPath, kDisplayNameKeys[i]);
		}
		if( aHUDElement.label.empty() )
			aHUDElement.label = theName;
	}

	if( aHUDElement.type >= eMenuStyle_Begin &&
		aHUDElement.type < eMenuStyle_End )
	{
		// Add new Root Menu to sMenus and link the Menu and HUDElement
		u16 aMenuID = sMenus.findOrAddIndex(
			sHUDElements.keys()[aHUDElementID]);
		if( sMenus.vals()[aMenuID].hudElementID == kInvalidID )
		{
			Menu& aMenu = sMenus.vals()[aMenuID];
			aMenu.label = aHUDElement.label;
			aMenu.path = theName;
			aMenu.parentMenuID = aMenuID;
			aMenu.rootMenuID = aMenuID;
			aMenu.hudElementID = aHUDElementID;
		}
		aHUDElement.menuID = aMenuID;
	}

	if( aHUDElement.type == eMenuStyle_Hotspots ||
		aHUDElement.type == eHUDType_KBArrayLast ||
		aHUDElement.type == eHUDType_KBArrayDefault ||
		aHUDElement.type == eHUDType_Hotspot )
	{
		std::string aLinkedName;
		int i = 0;
		if( aHUDElement.type == eMenuStyle_Hotspots )
		{
			for(; aLinkedName.empty() && i < ARRAYSIZE(kHotspotsKeys); ++i)
				aLinkedName = Profile::getStr(aMenuPath, kHotspotsKeys[i]);
		}
		else
		{
			for(; aLinkedName.empty() && i < ARRAYSIZE(kHotspotsKeys); ++i)
				aLinkedName = Profile::getStr(aHUDPath, kHotspotsKeys[i]);
		}
		if( aLinkedName.empty() )
		{
			logError(
				"Can't find required '[%s%s]/%s =' property "
				"for item referenced by [%s]! ",
				aHUDElement.type == eMenuStyle_Hotspots
					? kMenuPrefix : kHUDPrefix,
				theName.c_str(),
				kHotspotsKeys[0],
				theBuilder.debugItemName.c_str());
			aHUDElement.type = aHUDElement.type == eMenuStyle_Hotspots
				? eMenuStyle_List : eHUDItemType_Rect;
			return aHUDElementID;
		}

		bool foundLinkedItem = false;
		switch(aHUDElement.type)
		{
		case eMenuStyle_Hotspots:
			if( u16* aHotspotArrayID =
					theBuilder.hotspotArrayNameToIdxMap.find(
						condense(aLinkedName)) )
			{
				aHUDElement.hotspotArrayID = *aHotspotArrayID;
				foundLinkedItem = true;
			}
			break;
		case eHUDType_Hotspot:
			if( u16* aHotspotID =
					theBuilder.hotspotNameToIdxMap.find(
						condense(aLinkedName)) )
			{
				aHUDElement.hotspotID = *aHotspotID;
				foundLinkedItem = true;
			}
			break;
		case eHUDType_KBArrayLast:
		case eHUDType_KBArrayDefault:
			if( u16* aKeyBindArrayID =
					theBuilder.keyBindArrayNameToIdxMap.find(
						condense(aLinkedName)) )
			{
				aHUDElement.keyBindArrayID = *aKeyBindArrayID;
				foundLinkedItem = true;
			}
			break;
		}
		if( !foundLinkedItem )
		{
			logError(
				"Unrecognized '%s' specified for '[%s%s]/%s =' property "
				"for item referenced by [%s]! ",
				aLinkedName.c_str(),
				aHUDElement.type == eMenuStyle_Hotspots
					? kMenuPrefix : kHUDPrefix,
				theName.c_str(),
				kHotspotsKeys[i-1],
				theBuilder.debugItemName.c_str());
			aHUDElement.type = aHUDElement.type == eMenuStyle_Hotspots
				? eMenuStyle_List : eHUDItemType_Rect;
		}
	}

	return aHUDElementID;
}


static u16 getOrCreateRootMenuID(
	InputMapBuilder& theBuilder,
	const std::string& theMenuName)
{
	DBG_ASSERT(!theMenuName.empty());

	// Root menus are inherently HUD elements, so start with that
	u16 aHUDElementID = getOrCreateHUDElementID(
		theBuilder, theMenuName, true);
	HUDElement& aHUDElement = sHUDElements.vals()[aHUDElementID];

	DBG_ASSERT(aHUDElement.type >= eMenuStyle_Begin);
	DBG_ASSERT(aHUDElement.type < eMenuStyle_End);
	DBG_ASSERT(aHUDElement.menuID < sMenus.size());
	return aHUDElement.menuID;
}


static u16 getOrCreateMenuID(std::string theMenuName, u16 theParentMenuID)
{
	DBG_ASSERT(!theMenuName.empty());
	DBG_ASSERT(theParentMenuID < sMenus.size());

	std::string aParentPath = sMenus.keys()[theParentMenuID];
	if( theMenuName[0] == '.' )
	{// Starting with '.' signals want to treat "grandparent" as the parent
		theParentMenuID = sMenus.vals()[theParentMenuID].parentMenuID;
		aParentPath = sMenus.keys()[theParentMenuID];
		// Name being ".." means treat this as direct alias to grandparent menu
		if( theMenuName == ".." )
			return theParentMenuID;
		// Remove leading '.'
		theMenuName = theMenuName.substr(1);
	}

	u16 aMenuID = sMenus.findOrAddIndex(
		aParentPath + "." + upper(theMenuName));
	if( sMenus.vals()[aMenuID].parentMenuID != kInvalidID )
		return aMenuID;

	Menu& aMenu = sMenus.vals()[aMenuID];
	aMenu.label = aMenu.path = theMenuName;
	aMenu.parentMenuID = theParentMenuID;
	aMenu.rootMenuID = sMenus.vals()[theParentMenuID].rootMenuID;
	aMenu.hudElementID = sMenus.vals()[theParentMenuID].hudElementID;
	return aMenuID;
}


static Command wordsToSpecialCommand(
	InputMapBuilder& theBuilder,
	const std::vector<std::string>& theWords,
	bool allowButtonActions = false,
	bool allowHoldActions = false)
{
	// Can't allow hold actions if don't also allow button actions
	DBG_ASSERT(!allowHoldActions || allowButtonActions);
	Command result;
	if( theWords.empty() )
		return result;

	// Almost all commands require more than one "word", even if only one of
	// words is actually a command key word (thus can force a keybind to be
	// used instead of a command by specifying the keybind as a single word).
	// The exception are the "nothing" and "unassigned" key words.
	if( theWords.size() <= 1 )
	{
		ECommandKeyWord aKeyWordID = commandWordToID(upper(theWords[0]));
		if( aKeyWordID != eCmdWord_Nothing )
			return result;
	}

	// Find all key words that are actually included and their positions
	theBuilder.keyWordMap.clear();
	BitArray<eCmdWord_Num> keyWordsFound = { 0 };
	BitArray<eCmdWord_Num> allowedKeyWords = { 0 };
	const std::string* anIgnoredWord = null;
	const std::string* anIntegerWord = null;
	const std::string* aSecondLayerName = null;
	result.wrap = false;
	result.count = 1;
	for(size_t i = 0; i < theWords.size(); ++i)
	{
		ECommandKeyWord aKeyWordID = commandWordToID(upper(theWords[i]));
		// Convert LeftWrap/NextNoWrap/etc into just dir & wrap flag
		switch(aKeyWordID)
		{
		case eCmdWord_LeftWrap:
			aKeyWordID = eCmdWord_Left;
			result.wrap = true;
			break;
		case eCmdWord_LeftNoWrap:
			aKeyWordID = eCmdWord_Left;
			result.wrap = false;
			break;
		case eCmdWord_RightWrap:
			aKeyWordID = eCmdWord_Right;
			result.wrap = true;
			break;
		case eCmdWord_RightNoWrap:
			aKeyWordID = eCmdWord_Right;
			result.wrap = false;
			break;
		case eCmdWord_UpWrap:
			aKeyWordID = eCmdWord_Up;
			result.wrap = true;
			break;
		case eCmdWord_UpNoWrap:
			aKeyWordID = eCmdWord_Up;
			result.wrap = false;
			break;
		case eCmdWord_DownWrap:
			aKeyWordID = eCmdWord_Down;
			result.wrap = true;
			break;
		case eCmdWord_DownNoWrap:
			aKeyWordID = eCmdWord_Down;
			result.wrap = false;
			break;
		case eCmdWord_With:
			// Special case for "Replace <name> with <name>"
			if( i < theWords.size() - 1 &&
				keyWordsFound.test(eCmdWord_Replace) )
			{
				aSecondLayerName = &theWords[i+1];
				allowedKeyWords = keyWordsFound;
			}
			aKeyWordID = eCmdWord_Filler;
			break;
		}
		switch(aKeyWordID)
		{
		case eCmdWord_Filler:
			break;
		case eCmdWord_Ignored:
			anIgnoredWord = &theWords[i];
			break;
		case eCmdWord_Wrap:
			result.wrap = true;
			anIgnoredWord = &theWords[i];
			break;
		case eCmdWord_NoWrap:
			result.wrap = false;
			anIgnoredWord = &theWords[i];
			break;
		case eCmdWord_Integer:
			anIntegerWord = &theWords[i];
			result.count = s16(clamp(
				intFromString(theWords[i]),
				result.count, 0x7FFF));
			// fall through
		case eCmdWord_Unknown:
			// Not allowed more than once per command, since
			// these might actually be different values
			if( keyWordsFound.test(aKeyWordID) )
			{
				// Exception: single allowed duplicate
				if( allowedKeyWords.test(aKeyWordID) )
					allowedKeyWords.reset();
				else
					return result;
			}
			// fall through
		default:
			// Don't add duplicate keys to the map
			if( !keyWordsFound.test(aKeyWordID) )
			{
				keyWordsFound.set(aKeyWordID);
				theBuilder.keyWordMap.addPair(aKeyWordID, i);
			}
			break;
		}
	}
	if( theBuilder.keyWordMap.empty() )
		return result;
	theBuilder.keyWordMap.sort();
	// If have no "unknown" word (layer name/etc), use "ignored" word as one
	// This is the only difference between "ignored" and "filler" words
	if( !keyWordsFound.test(eCmdWord_Unknown) &&
		keyWordsFound.test(eCmdWord_Ignored) )
	{
		keyWordsFound.set(eCmdWord_Unknown);
		theBuilder.keyWordMap.setValue(eCmdWord_Unknown,
			theBuilder.keyWordMap.findOrAdd(eCmdWord_Ignored));
	}
	keyWordsFound.reset(eCmdWord_Ignored);

	// Find a command by checking for specific key words + allowed related
	// words (but no extra words beyond that, besides fillers)

	// "= [Do] Nothing"
	if( keyWordsFound.test(eCmdWord_Nothing) && keyWordsFound.count() == 1)
	{
		result.type = eCmdType_DoNothing;
		return result;
	}

	// "= [Change] Profile"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Change);
	allowedKeyWords.set(eCmdWord_Replace);
	allowedKeyWords.set(eCmdWord_Profile);
	if( keyWordsFound.test(eCmdWord_Profile) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_ChangeProfile;
		return result;
	}

	// "= [Edit] Layout"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Change);
	allowedKeyWords.set(eCmdWord_Edit);
	allowedKeyWords.set(eCmdWord_Layout);
	if( keyWordsFound.test(eCmdWord_Layout) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_EditLayout;
		return result;
	}

	// "= Close App"
	if( keyWordsFound.test(eCmdWord_Close) &&
		keyWordsFound.test(eCmdWord_App) &&
		keyWordsFound.count() == 2 )
	{
		result.type = eCmdType_QuitApp;
		return result;
	}

	// "= Lock Movement"
	if( keyWordsFound.test(eCmdWord_Lock) &&
		keyWordsFound.test(eCmdWord_Move) &&
		keyWordsFound.count() == 2 )
	{
		result.type = eCmdType_StartAutoRun;
		result.multiDirAutoRun = true;
		return result;
	}

	// "= Move 'Mouse|Cursor' to <aHotspotName>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Move);
	allowedKeyWords.set(eCmdWord_Mouse);
	if( keyWordsFound.test(eCmdWord_Move) &&
		keyWordsFound.test(eCmdWord_Mouse) &&
		!keyWordsFound.test(eCmdWord_Left) &&
		!keyWordsFound.test(eCmdWord_Right) &&
		!keyWordsFound.test(eCmdWord_Up) &&
		!keyWordsFound.test(eCmdWord_Down) &&
		(keyWordsFound & ~allowedKeyWords).count() == 1 )
	{
		allowedKeyWords = keyWordsFound;
		allowedKeyWords.reset(eCmdWord_Move);
		allowedKeyWords.reset(eCmdWord_Mouse);
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			theBuilder.keyWordMap.find(ECommandKeyWord(
				allowedKeyWords.firstSetBit()));
		if( itr != theBuilder.keyWordMap.end() )
		{
			u16* aHotspotIdx = theBuilder.hotspotNameToIdxMap.find(
				condense(theWords[itr->second]));
			if( aHotspotIdx )
			{
				result.type = eCmdType_MoveMouseToHotspot;
				result.hotspotID = *aHotspotIdx;
				return result;
			}
		}
	}

	// "= Remove [this] Layer"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Layer);
	allowedKeyWords.set(eCmdWord_Remove);
	if( allowButtonActions &&
		keyWordsFound.test(eCmdWord_Remove) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_RemoveControlsLayer;
		// Since can't remove layer 0 (main scheme), 0 acts as a flag
		// meaning to remove calling layer instead
		result.layerID = 0;
		return result;
	}

	// The remainng Layer-related commands need one extra word that is
	// not a key word related to layers, which will be the name of the
	// layer (likely, but not always, the eCmdWord_Unknown entry).
	// Start by defining what key words are related to layer commands
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Layer);
	allowedKeyWords.set(eCmdWord_Add);
	allowedKeyWords.set(eCmdWord_Remove);
	allowedKeyWords.set(eCmdWord_Hold);
	allowedKeyWords.set(eCmdWord_Replace);
	allowedKeyWords.set(eCmdWord_Toggle);
	// Directionals aren't layer-related but also not allowed as layer names
	allowedKeyWords.set(eCmdWord_Left);
	allowedKeyWords.set(eCmdWord_Right);
	allowedKeyWords.set(eCmdWord_Up);
	allowedKeyWords.set(eCmdWord_Down);
	// If have aSecondLayerName, make sure it isn't one of the above
	if( aSecondLayerName )
	{
		allowedKeyWords.set(eCmdWord_Filler);
		ECommandKeyWord aKeyWordID = commandWordToID(upper(*aSecondLayerName));
		while(aSecondLayerName && allowedKeyWords.test(aKeyWordID))
		{
			if( aSecondLayerName == &theWords.back() )
			{
				aSecondLayerName = null;
				break;
			}
			++aSecondLayerName;
			aKeyWordID = commandWordToID(upper(*aSecondLayerName));
		}
		allowedKeyWords.reset(eCmdWord_Filler);
	}
	// Convert allowedKeyWords into all non-layer-related words found
	allowedKeyWords = keyWordsFound & (~allowedKeyWords);
	// If no extra words found, default to anIgnoredWord
	const std::string* aLayerName = anIgnoredWord;
	// If no ignored word either, default to anIntegerWord
	if( !aLayerName ) aLayerName = anIntegerWord;
	if( allowedKeyWords.count() == 1 ||
		(aSecondLayerName && allowedKeyWords.count() == 2) )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			theBuilder.keyWordMap.find(ECommandKeyWord(
				allowedKeyWords.firstSetBit()));
		if( itr != theBuilder.keyWordMap.end() )
			aLayerName = &theWords[itr->second];
	}
	if( aSecondLayerName &&
		aLayerName == aSecondLayerName &&
		allowedKeyWords.count() == 2 )
	{
		aLayerName = anIgnoredWord;
		if( !aLayerName ) aLayerName = anIntegerWord;
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			theBuilder.keyWordMap.find(ECommandKeyWord(
				allowedKeyWords.nextSetBit(allowedKeyWords.firstSetBit())));
		if( itr != theBuilder.keyWordMap.end() )
			aLayerName = &theWords[itr->second];
	}
	u16 aLayerID = 0;
	if( aLayerName )
	{
		aLayerID = getLayerID(*aLayerName);
		if( !aLayerID )
		{
			// TODO - logError
		}
	}
	u16 aSecondLayerID = 0;
	if( aSecondLayerName )
	{
		aSecondLayerID = getLayerID(*aSecondLayerName);
		if( !aSecondLayerID )
		{
			// TODO - logError
		}
		if( aLayerID == aSecondLayerID )
		{
			// TODO - logError
		}
	}
	allowedKeyWords.reset();

	if( aLayerID && aLayerID < sLayers.size() )
	{
		// "= Replace [Layer] <aLayerName> with <aSecondLayerName>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Layer);
		allowedKeyWords.set(eCmdWord_Replace);
		if( keyWordsFound.test(eCmdWord_Replace) &&
			aSecondLayerID && aSecondLayerID < sLayers.size() &&
			(keyWordsFound & ~allowedKeyWords).count() <= 2 )
		{
			result.type = eCmdType_ReplaceControlsLayer;
			result.layerID = aLayerID;
			result.replacementLayer = aSecondLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Replace);

		// "= Add [Layer] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Add);
		if( keyWordsFound.test(eCmdWord_Add) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_AddControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Add);

		// "= Toggle [Layer] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Toggle);
		if( keyWordsFound.test(eCmdWord_Toggle) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_ToggleControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Toggle);

		// "= Replace [this layer with] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Replace);
		if( allowButtonActions &&
			keyWordsFound.test(eCmdWord_Replace) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_ReplaceControlsLayer;
			// Since can't remove layer 0 (main scheme), 0 acts as a flag
			// meaning to remove calling layer instead
			result.layerID = 0;
			result.replacementLayer = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Replace);

		// "= 'Hold'|'Layer'|'Hold Layer' <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Hold);
		if( allowHoldActions &&
			(keyWordsFound.test(eCmdWord_Hold) ||
			 keyWordsFound.test(eCmdWord_Layer)) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_HoldControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Hold);

		// "= Remove [Layer] <aLayerName>"
		// allowedKeyWords = Layer
		allowedKeyWords.set(eCmdWord_Remove);
		if( keyWordsFound.test(eCmdWord_Remove) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_RemoveControlsLayer;
			result.layerID = aLayerID;
			return result;
		}
		//allowedKeyWords.reset(eCmdWord_Remove);
	}

	// Same deal as aLayerName for the Menu-related commands needing a name
	// of the menu in question as the one otherwise-unrelated word.
	const std::string* aMenuName = anIgnoredWord;
	allowedKeyWords = keyWordsFound;
	allowedKeyWords.reset(eCmdWord_Menu);
	allowedKeyWords.reset(eCmdWord_Reset);
	allowedKeyWords.reset(eCmdWord_Select);
	allowedKeyWords.reset(eCmdWord_Confirm);
	allowedKeyWords.reset(eCmdWord_Close);
	allowedKeyWords.reset(eCmdWord_Edit);
	allowedKeyWords.reset(eCmdWord_Left);
	allowedKeyWords.reset(eCmdWord_Right);
	allowedKeyWords.reset(eCmdWord_Up);
	allowedKeyWords.reset(eCmdWord_Down);
	allowedKeyWords.reset(eCmdWord_Hotspot);
	allowedKeyWords.reset(eCmdWord_Default);
	allowedKeyWords.reset(eCmdWord_Integer);
	allowedKeyWords.reset(eCmdWord_Back);
	allowedKeyWords.reset(eCmdWord_Mouse);
	allowedKeyWords.reset(eCmdWord_Click);
	if( allowedKeyWords.count() == 1 )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			theBuilder.keyWordMap.find(
				ECommandKeyWord(allowedKeyWords.firstSetBit()));
		if( itr != theBuilder.keyWordMap.end() )
			aMenuName = &theWords[itr->second];
	}

	if( aMenuName )
	{
		// If add "[with] mouse" to menu commands, causes actual mouse
		// cursor to move and point at currently selected item, and
		// possibly left-click there as well if also use "mouse click".
		// This is used for menus that directly overlay actual in-game
		// menus to save on needing a bunch of hotspots and extra
		// jump/click commands (or alongside eMenuStyle_Hotspots menus).
		result.andClick = keyWordsFound.test(eCmdWord_Click);
		result.withMouse = result.andClick ||
			keyWordsFound.test(eCmdWord_Mouse);

		// "= Reset <aMenuName> [Menu] [to Default] [to #] [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Default);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		allowedKeyWords.set(eCmdWord_Integer);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuReset;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			result.menuItemIdx = result.count;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Reset);
		allowedKeyWords.reset(eCmdWord_Default);
	}

	if( allowButtonActions && aMenuName )
	{
		// "= Confirm <aMenuName> [Menu] [with mouse click]"
		// allowedKeyWords = Menu & Mouse & Click
		allowedKeyWords.set(eCmdWord_Confirm);
		if( keyWordsFound.test(eCmdWord_Confirm) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuConfirm;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}

		// "= Confirm <aMenuName> [Menu] and Close [with mouse click]"
		// allowedKeyWords = Menu & Confirm & Mouse & Click
		allowedKeyWords.set(eCmdWord_Close);
		if( keyWordsFound.test(eCmdWord_Confirm) &&
			keyWordsFound.test(eCmdWord_Close) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuConfirmAndClose;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Confirm);
		allowedKeyWords.reset(eCmdWord_Mouse);
		allowedKeyWords.reset(eCmdWord_Click);

		// "= Edit <aMenuName> [Menu]"
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Edit);
		if( keyWordsFound.test(eCmdWord_Edit) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuEdit;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
	}

	// Now once again same deal for the name of a Key Bind Array
	const std::string* aKeyBindArrayName = anIgnoredWord;
	allowedKeyWords = keyWordsFound;
	allowedKeyWords.reset(eCmdWord_Reset);
	allowedKeyWords.reset(eCmdWord_Last);
	allowedKeyWords.reset(eCmdWord_Default);
	allowedKeyWords.reset(eCmdWord_Load);
	allowedKeyWords.reset(eCmdWord_Set);
	allowedKeyWords.reset(eCmdWord_Prev);
	allowedKeyWords.reset(eCmdWord_Next);
	allowedKeyWords.reset(eCmdWord_Integer);
	if( allowedKeyWords.count() == 1 )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			theBuilder.keyWordMap.find(
				ECommandKeyWord(allowedKeyWords.firstSetBit()));
		if( itr != theBuilder.keyWordMap.end() )
			aKeyBindArrayName = &theWords[itr->second];
	}
	// In this case, need to confirm key bind name is valid and get ID from it
	u16* aKeyBindArrayID = null;
	if( aKeyBindArrayName )
	{
		aKeyBindArrayID =
			theBuilder.keyBindArrayNameToIdxMap.find(
				condense(*aKeyBindArrayName));
	}

	if( aKeyBindArrayID )
	{
		// "= Reset <aKeyBindArrayID> [Last] [to Default]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Last);
		allowedKeyWords.set(eCmdWord_Default);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayResetLast;
			result.keybindArrayID = *aKeyBindArrayID;
			return result;
		}
		// "= <aKeyBindArrayID> [Load] Default"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Load);
		allowedKeyWords.set(eCmdWord_Default);
		if( keyWordsFound.test(eCmdWord_Default) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayDefault;
			result.keybindArrayID = *aKeyBindArrayID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Load);
		// "= <aKeyBindArrayID> 'Set [Default] [to Last]"
		// allowedKeyWords = Default
		allowedKeyWords.set(eCmdWord_Set);
		allowedKeyWords.set(eCmdWord_Last);
		if( keyWordsFound.test(eCmdWord_Set) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArraySetDefault;
			result.keybindArrayID = *aKeyBindArrayID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Set);
		allowedKeyWords.reset(eCmdWord_Default);
		// "= <aKeyBindArrayID> Last"
		// allowedKeyWords = Last
		if( keyWordsFound.test(eCmdWord_Last) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayLast;
			result.keybindArrayID = *aKeyBindArrayID;
			return result;
		}
		// "= <aKeyBindArrayID> Prev [No/Wrap] [#]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Prev);
		allowedKeyWords.set(eCmdWord_Integer);
		if( keyWordsFound.test(eCmdWord_Prev) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayPrev;
			result.keybindArrayID = *aKeyBindArrayID;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Prev);
		// "= <aKeyBindArrayID> Next [No/Wrap] [#]"
		// allowedKeyWords = Integer
		allowedKeyWords.set(eCmdWord_Next);
		if( keyWordsFound.test(eCmdWord_Next) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_KeyBindArrayNext;
			result.keybindArrayID = *aKeyBindArrayID;
			return result;
		}
	}

	// Get ECmdDir from key words for remaining commands
	DBG_ASSERT(result.type == eCmdType_Empty);
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Up);
	allowedKeyWords.set(eCmdWord_Down);
	allowedKeyWords.set(eCmdWord_Left);
	allowedKeyWords.set(eCmdWord_Right);
	allowedKeyWords.set(eCmdWord_Back);
	if( (keyWordsFound & allowedKeyWords).count() != 1 )
		return result;

	ECommandDir aCmdDir = eCmdDir_None;
	if( keyWordsFound.test(eCmdWord_Up) ) aCmdDir = eCmdDir_Up;
	else if( keyWordsFound.test(eCmdWord_Down) ) aCmdDir = eCmdDir_Down;
	else if( keyWordsFound.test(eCmdWord_Left) ) aCmdDir = eCmdDir_Left;
	else if( keyWordsFound.test(eCmdWord_Right) ) aCmdDir = eCmdDir_Right;
	result.dir = aCmdDir;
	// Remove direction-related bits from keyWordsFound
	allowedKeyWords.reset(eCmdWord_Back);
	keyWordsFound &= ~allowedKeyWords;

	if( allowButtonActions && aMenuName && result.dir != eCmdDir_None )
	{
		// "= 'Select'|'Menu'|'Select Menu'
		// <aMenuName> <aCmdDir> [No/Wrap] [#] [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Integer);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		if( (keyWordsFound.test(eCmdWord_Select) ||
			 keyWordsFound.test(eCmdWord_Menu)) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuSelect;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}

		// "= 'Select'|'Menu'|'Select Menu' and Close
		// <aMenuName> <aCmdDir> [No/Wrap] [#] [with mouse click]"
		// allowedKeyWords = Menu & Select & Mouse & Click
		allowedKeyWords.set(eCmdWord_Close);
		if( (keyWordsFound.test(eCmdWord_Select) ||
			 keyWordsFound.test(eCmdWord_Menu)) &&
			keyWordsFound.test(eCmdWord_Close) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuSelectAndClose;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Select);
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Mouse);
		allowedKeyWords.reset(eCmdWord_Click);

		// "= Edit [Menu] <aMenuName> <aCmdDir>"
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Edit);
		if( keyWordsFound.test(eCmdWord_Edit) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuEditDir;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
	}

	if( keyWordsFound.test(eCmdWord_Back) )
	{
		result.dir = eCmdDir_Back;
		keyWordsFound.reset(eCmdWord_Back);
	}

	if( allowButtonActions )
	{
		// "= [Select] [Mouse] Hotspot <aCmdDir> [#]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Hotspot);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Integer);
		if( keyWordsFound.test(eCmdWord_Hotspot) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_HotspotSelect;
			return result;
		}
	}

	if( allowHoldActions )
	{
		// "= 'Move'|'Turn'|'MoveTurn' <aCmdDir>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Move);
		allowedKeyWords.set(eCmdWord_Turn);
		if( (keyWordsFound & allowedKeyWords).any() &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveTurn;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Turn);

		// "= 'Strafe|MoveStrafe' <aCmdDir>"
		// allowedKeyWords = Move
		allowedKeyWords.set(eCmdWord_Strafe);
		if( keyWordsFound.test(eCmdWord_Strafe) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveStrafe;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Strafe);

		// "= 'Look|MoveLook' <aCmdDir>"
		// allowedKeyWords = Move
		allowedKeyWords.set(eCmdWord_Look);
		if( keyWordsFound.test(eCmdWord_Look) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveLook;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Look);

		// "= [Move] Mouse <aCmdDir>"
		// allowedKeyWords = Move
		allowedKeyWords.set(eCmdWord_Mouse);
		if( keyWordsFound.test(eCmdWord_Mouse) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MoveMouse;
			return result;
		}

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Stepped] <aCmdDir>"
		// allowedKeyWords = Move & Mouse
		allowedKeyWords.set(eCmdWord_MouseWheel);
		allowedKeyWords.set(eCmdWord_Stepped);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.mouseWheelMotionType = eMouseWheelMotion_Stepped;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Stepped);

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' Smooth <aCmdDir>"
		// allowedKeyWords = Move & Mouse & Wheel
		allowedKeyWords.set(eCmdWord_Smooth);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			keyWordsFound.test(eCmdWord_Smooth) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.mouseWheelMotionType = eMouseWheelMotion_Smooth;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Smooth);
	}

	// "= [Move] [Mouse] 'Wheel'|'MouseWheel' # <aCmdDir>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Move);
	allowedKeyWords.set(eCmdWord_Mouse);
	allowedKeyWords.set(eCmdWord_MouseWheel);
	allowedKeyWords.set(eCmdWord_Integer);
	if( keyWordsFound.test(eCmdWord_MouseWheel) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MouseWheel;
		result.mouseWheelMotionType = eMouseWheelMotion_Jump;
		return result;
	}

	if( allowButtonActions && aMenuName )
	{
		// This is all the way down here because "back" could be a direction
		// for another command, OR mean backing out of a sub-menu, and want
		// to first make sure <aMenuName> isn't another command's key word
		// "= Menu <aMenuName> Back [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		if( result.dir == eCmdDir_Back &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuBack;
			result.dir = eCmdDir_None;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
	}

	DBG_ASSERT(result.type == eCmdType_Empty);
	return result;
}


static Command stringToCommand(
	InputMapBuilder& theBuilder,
	const std::string& theString,
	bool allowButtonActions = false,
	bool allowHoldActions = false)
{
	Command result;

	if( theString.empty() )
		return result;

	// Check for a chat box macro (message or slash command)
	result = parseChatBoxMacro(theString);
	if( result.type != eCmdType_Empty )
		return result;

	// Check for a simple key assignment
	theBuilder.parsedString.clear();
	sanitizeSentence(theString, theBuilder.parsedString);
	if( u16 aVKey = namesToVKey(theBuilder.parsedString) )
	{
		result.type = eCmdType_TapKey;
		result.vKey = aVKey;
		// _PressAndHoldKey (and indirectly its associated _ReleaseKey)
		// only works properly with a single key (+ mods), just like _TapKey,
		// and only when allowHoldActions is true (_Down button action),
		// so in that case change _TapKey to _PressAndHoldKey instead.
		// This does mean there isn't currently a way to keep this button
		// action assigned to only _TapKey, and that a _Down button action
		// will just act the same as _Press for any other commmands
		// (which can be used intentionally to have 2 commands for button
		// initial press, thus can be useful even if a bit unintuitive).
		result.type = eCmdType_PressAndHoldKey;
		return result;
	}

	// Check for special command
	result = wordsToSpecialCommand(
		theBuilder,
		theBuilder.parsedString,
		allowButtonActions,
		allowHoldActions);

	// Check for alias to a keybind
	if( result.type == eCmdType_Empty )
	{
		std::string aKeyBindName = condense(theString);
		if( Command* aSpecialKeyCommand =
				theBuilder.specialKeyNameToCommandMap.find(aKeyBindName) )
		{
			if( allowHoldActions ||
				aSpecialKeyCommand->type < eCmdType_FirstContinuous )
			{
				result = *aSpecialKeyCommand;
				return result;
			}
		}

		if( Command* aKeyBindCommand =
				theBuilder.commandAliases.find(aKeyBindName) )
		{
			result = *aKeyBindCommand;
			// Check if this keybind is part of an array, and if so, use
			// eCmdType_KeyBindArrayIndex instead so "last" will update
			const int anArrayIdx = breakOffIntegerSuffix(aKeyBindName);
			if( anArrayIdx >= 0 )
			{
				u16* aKeyBindArrayID =
					theBuilder.keyBindArrayNameToIdxMap.find(aKeyBindName);
				if( aKeyBindArrayID )
				{
					// Comment on _PressAndHoldKey above explains this
					if( result.type == eCmdType_TapKey && allowHoldActions )
						result.type = eCmdType_KeyBindArrayHoldIndex;
					else
						result.type = eCmdType_KeyBindArrayIndex;
					result.keybindArrayID = *aKeyBindArrayID;
					result.arrayIdx = anArrayIdx;
				}
			}
		}
	}

	// Check for Virtual-Key Code sequence
	if( result.type == eCmdType_Empty )
	{
		const std::string& aVKeySeq =
			namesToVKeySequence(theBuilder, theBuilder.parsedString);

		if( !aVKeySeq.empty() )
		{
			result.type = eCmdType_VKeySequence;
			result.keyStringID = sKeyStrings.findOrAddIndex(aVKeySeq);
		}
	}

	return result;
}


static void assignHotspots(
	InputMapBuilder& theBuilder,
	const Profile::PropertyMap& thePropMap)
{
	if( theBuilder.hotspotNameToIdxMap.empty() )
	{
		for(u16 i = 0; i < eSpecialHotspot_Num; ++i)
		{
			theBuilder.hotspotNameToIdxMap.
				setValue(kSpecialHotspotNames[i], i);
		}
	}

	// Prepare local data structures
	BitVector<> aFoundArrays;
	aFoundArrays.clearAndResize(sHotspotArrays.size());
	VectorMap<u16, float> aHotspotArrayAnchorOffsetsScaleMap;

	// Parse normal hotspots and pick out any that might be arrays
	for(size_t i = 0; i < thePropMap.size(); ++i)
	{
		std::string aHotspotName = thePropMap.keys()[i];

		// Check if might be part of a hotspot array
		const int anArrayIdx = breakOffIntegerSuffix(aHotspotName);
		if( anArrayIdx > 0 )
		{
			int aStartArrayIdx = anArrayIdx;
			std::string aHotspotArrayName = aHotspotName;
			if( aHotspotArrayName[aHotspotArrayName.size()-1] == '-' )
			{// Part of a range of hotspots, like HotspotName2-8
				aHotspotArrayName.resize(aHotspotArrayName.size()-1);
				aStartArrayIdx = breakOffIntegerSuffix(aHotspotArrayName);
			}
			if( anArrayIdx >= aStartArrayIdx && !aHotspotArrayName.empty() )
			{
				u16& aHotspotArrayIdx = theBuilder.hotspotArrayNameToIdxMap
					.findOrAdd(condense(aHotspotArrayName),
						u16(sHotspotArrays.size()));
				if( aHotspotArrayIdx >= sHotspotArrays.size() )
				{
					HotspotArray anEntry;
					anEntry.label = thePropMap.vals()[i].name;
					anEntry.label.resize(
						posAfterPrefix(anEntry.label, aHotspotArrayName));
					anEntry.first = anEntry.last = 0;
					anEntry.offsetScale = 1.0f;
					sHotspotArrays.push_back(anEntry);
					aFoundArrays.resize(sHotspotArrays.size());
				}
				aFoundArrays.set(aHotspotArrayIdx);
				sHotspotArrays[aHotspotArrayIdx].last = max(
					sHotspotArrays[aHotspotArrayIdx].last,
					sHotspotArrays[aHotspotArrayIdx].first + anArrayIdx - 1);
				continue;
			}
			// Doesn't seem to be part of a valid array, so restore name
			aHotspotName = thePropMap.keys()[i];
		}
		else if( u16* aHotspotArrayIdxPtr =
					theBuilder.hotspotArrayNameToIdxMap.find(aHotspotName) )
		{// Must be the anchor hotspot of an array
			aFoundArrays.set(*aHotspotArrayIdxPtr);
		}

		u16& aHotspotIdx = theBuilder.hotspotNameToIdxMap.findOrAdd(
			aHotspotName, u16(sHotspots.size()));
		if( aHotspotIdx >= sHotspots.size() )
			sHotspots.resize(aHotspotIdx+1);
		std::string aHotspotDescription = thePropMap.vals()[i].val;
		EResult aResult = HotspotMap::stringToHotspot(
			aHotspotDescription, sHotspots[aHotspotIdx]);
		if( aResult == eResult_Overflow )
		{
			logError("Hotspot %s: Invalid coordinate in '%s' "
				"(anchor must be in 0-100% range and limited decimal places)",
				thePropMap.vals()[i].name.c_str(),
				thePropMap.vals()[i].val.c_str());
			sHotspots[aHotspotIdx] = Hotspot();
		}
		else if( aResult == eResult_Malformed )
		{
			logError("Hotspot %s: Could not decipher hotspot position '%s'",
				thePropMap.vals()[i].name.c_str(),
				thePropMap.vals()[i].val.c_str());
			sHotspots[aHotspotIdx] = Hotspot();
		}
		else if( !aHotspotDescription.empty() &&
				 aHotspotDescription[0] == '*' )
		{
			// Hotspot seems to have ended with an offset scaling factor
			const float aScaleFactor =
				floatFromString(aHotspotDescription.substr(1));
			if( aScaleFactor == 0 )
			{
				logError("Hotspot %s: Invalid offset scale factor '%s'",
					thePropMap.vals()[i].name.c_str(),
					aHotspotDescription.c_str());
			}
			else
			{
				aHotspotArrayAnchorOffsetsScaleMap.setValue(
					aHotspotIdx, aScaleFactor);
			}
		}
	}

	// Fill in any discovered hotspot arrays
	for(int aHSA_ID = aFoundArrays.firstSetBit();
		aHSA_ID < aFoundArrays.size();
		aHSA_ID = aFoundArrays.nextSetBit(aHSA_ID+1))
	{
		HotspotArray& aHotspotArray = sHotspotArrays[aHSA_ID];
		Hotspot anAnchor;
		u16* anAnchorIdx = theBuilder.hotspotNameToIdxMap.
			find(condense(aHotspotArray.label));
		if( anAnchorIdx )
		{
			anAnchor = sHotspots[*anAnchorIdx];
			VectorMap<u16, float>::iterator itr =
				aHotspotArrayAnchorOffsetsScaleMap.find(*anAnchorIdx);
			if( itr != aHotspotArrayAnchorOffsetsScaleMap.end() )
				aHotspotArray.offsetScale = itr->second;
		}
		const u16 aHotspotArrayCount =
			aHotspotArray.last - aHotspotArray.first + 1;
		if( aHotspotArray.first == 0 )
		{// Allocate enough hotspots for a new array
			mapDebugPrint("Building Hotspot Array %s\n",
				aHotspotArray.label.c_str());
			aHotspotArray.first = u16(sHotspots.size());
			sHotspots.resize(sHotspots.size() + aHotspotArrayCount);
			aHotspotArray.last += aHotspotArray.first;
		}
		u16 aNextArrayIdx = 1;
		while(aNextArrayIdx <= aHotspotArrayCount)
		{
			u16 aHotspotID =
				aHotspotArray.first + aNextArrayIdx - 1;
			std::string aHotspotName =
				aHotspotArray.label + toString(aNextArrayIdx);
			theBuilder.hotspotNameToIdxMap.setValue(
				condense(aHotspotName), aHotspotID);
			std::string aHotspotValue = Profile::getStr(
				kHotspotsSectionName, aHotspotName);
			if( !aHotspotValue.empty() )
			{
				std::string aHotspotDesc = aHotspotValue;
				Hotspot& aHotspot = sHotspots[aHotspotID];
				EResult aResult = HotspotMap::stringToHotspot(
					aHotspotDesc, aHotspot);
				if( aResult == eResult_Overflow )
				{
					logError("Hotspot %s: Invalid coordinate in '%s' "
						"(anchor must be in 0-100% range "
						"and limited decimal places)",
						aHotspotName.c_str(),
						aHotspotValue.c_str());
				}
				else if( aResult == eResult_Malformed )
				{
					logError(
						"Hotspot %s: Could not decipher hotspot position '%s'",
						aHotspotName.c_str(),
						aHotspotValue.c_str());
				}
				else if( !aHotspotDesc.empty() && aHotspotDesc[0] == '*' )
				{
					logError(
						"Hotspot %s: Only anchor hotspots can have specify "
						"an offset scale factor (using '*')!",
						aHotspotName.c_str());
				}
				// Offset by anchor hotspot if don't have own anchor set
				if( aHotspot.x.anchor == 0 )
				{
					aHotspot.x.offset *= aHotspotArray.offsetScale;
					aHotspot.x.anchor = anAnchor.x.anchor;
					aHotspot.x.offset += anAnchor.x.offset;
				}
				if( aHotspot.y.anchor == 0 )
				{
					aHotspot.y.offset *= aHotspotArray.offsetScale;
					aHotspot.y.anchor = anAnchor.y.anchor;
					aHotspot.y.offset += anAnchor.y.offset;
				}
				++aNextArrayIdx;
			}
			else
			{
				// Attempt to find as part of a range of hotspots
				static Profile::PropertyMap::IndexVector sHotspotRangeIdxVec;
				DBG_ASSERT(sHotspotRangeIdxVec.empty());
				const std::string& aHotspotRangePrefix =
					condense(aHotspotName) + '-';
				thePropMap.findAllWithPrefix(
					aHotspotRangePrefix,
					sHotspotRangeIdxVec);
				if( sHotspotRangeIdxVec.empty() )
				{
					logError(
						"Hotspot Array %s missing hotspot entry #%d",
						aHotspotArray.label.c_str(),
						aNextArrayIdx);
					++aNextArrayIdx;
				}
				else if( sHotspotRangeIdxVec.size() > 1 )
				{
					logError(
						"Hotspot Array %s has overlapping ranges "
						"starting with %d",
						aHotspotArray.label.c_str(),
						aNextArrayIdx);
					++aNextArrayIdx;
				}
				else
				{
					const std::string& aHotspotProfileName =
						thePropMap.vals()[sHotspotRangeIdxVec[0]].name;
					const std::string& aHotspotProfileVal =
						thePropMap.vals()[sHotspotRangeIdxVec[0]].val;
					std::string aHotspotDesc = aHotspotProfileVal;
					Hotspot aDeltaHotspot;
					EResult aResult = HotspotMap::stringToHotspot(
						aHotspotDesc, aDeltaHotspot);
					if( aResult == eResult_Overflow )
					{
						logError("Hotspot %s: Invalid coordinate in '%s' "
							"(anchor must be in 0-100% range "
							"and limited decimal places)",
							aHotspotName.c_str(),
							aHotspotProfileVal.c_str());
						aDeltaHotspot = Hotspot();
					}
					else if( aResult == eResult_Malformed )
					{
						logError(
							"Hotspot %s: Could not decipher offsets '%s'",
							aHotspotName.c_str(),
							aHotspotProfileVal.c_str());
						aDeltaHotspot = Hotspot();
					}
					else if( !aHotspotDesc.empty() && aHotspotDesc[0] == '*' )
					{
						logError(
							"Hotspot %s: Only anchor hotspots can have specify "
							"an offset scale factor (using '*')!",
							aHotspotName.c_str());
					}
					int aLastIdx = intFromString(aHotspotProfileName.substr(
						posAfterPrefix(aHotspotProfileName, aHotspotRangePrefix)));
					Hotspot& aRangeAnchor = sHotspots[aHotspotID-1];
					u16 aPosWithinRange = 1;
					for(; aNextArrayIdx <= aLastIdx; ++aNextArrayIdx )
					{
						aHotspotID =
							aHotspotArray.first + aNextArrayIdx - 1;
						aHotspotName =
							aHotspotArray.label + toString(aNextArrayIdx);
						theBuilder.hotspotNameToIdxMap.setValue(
							condense(aHotspotName), aHotspotID);
						Hotspot& aHotspot = sHotspots[aHotspotID];
						aHotspot.x.anchor = aRangeAnchor.x.anchor;
						aHotspot.x.offset = aDeltaHotspot.x.offset;
						aHotspot.x.offset *= aPosWithinRange;
						aHotspot.x.offset *= aHotspotArray.offsetScale;
						aHotspot.x.offset += aRangeAnchor.x.offset;
						aHotspot.y.anchor = aRangeAnchor.y.anchor;
						aHotspot.y.offset = aDeltaHotspot.y.offset;
						aHotspot.y.offset *= aPosWithinRange;
						aHotspot.y.offset *= aHotspotArray.offsetScale;
						aHotspot.y.offset += aRangeAnchor.y.offset;
						++aPosWithinRange;
					}
				}
				sHotspotRangeIdxVec.clear();
			}
		}
	}
}


static void buildHotspots(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning hotspots...\n");
	sHotspots.resize(eSpecialHotspot_Num);

	assignHotspots(
		theBuilder,
		Profile::getSectionProperties(kHotspotsSectionName));

	// sHotspots[0] is reserved as eSpecialHotspot_None
	// The hotspotNameToIdxMap maps to this for "filler" words between
	// jump/point/click and the actual hotspot name in VKey sequences.
	theBuilder.hotspotNameToIdxMap.setValue("MOUSE", 0);
	theBuilder.hotspotNameToIdxMap.setValue("CURSOR", 0);
	theBuilder.hotspotNameToIdxMap.setValue("TO", 0);
	theBuilder.hotspotNameToIdxMap.setValue("AT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("ON", 0);
	theBuilder.hotspotNameToIdxMap.setValue("HOTSPOT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("HOT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("SPOT", 0);
}


static void reportCommandAssignment(
	const std::string& theSection,
	const std::string& theItemName,
	const Command& theCmd,
	const std::string& theCmdStr,
	bool isKeyBind = false)
{
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		logError("%s: Not sure how to assign '%s' to '%s'!",
			theSection.c_str(),
			theItemName.c_str(),
			theCmdStr.c_str());
		break;
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		mapDebugPrint("%s: Assigned '%s' to <Do Nothing>\n",
			theSection.c_str(),
			theItemName.c_str());
		break;
	case eCmdType_SignalOnly:
		mapDebugPrint("%s: Assigned '%s' to <Signal #%d Only>\n",
			theSection.c_str(),
			theItemName.c_str(),
			theCmd.signalID);
		break;
	case eCmdType_ChatBoxString:
		{
			std::string aMacroString = sKeyStrings.keys()[theCmd.keyStringID];
			aMacroString.resize(aMacroString.size()-1);
			if( aMacroString[0] == kVKeyFireSignal )
				aMacroString = aMacroString.substr(3);
			mapDebugPrint("%s: Assigned '%s' to macro: %s%s\n",
				theSection.c_str(),
				theItemName.c_str(),
				aMacroString.c_str(),
				isKeyBind
					? (std::string(" <signal #") +
						toString(theCmd.signalID) + ">").c_str()
					: "");
		}
		break;
	case eCmdType_TapKey:
	case eCmdType_PressAndHoldKey:
		mapDebugPrint("%s: Assigned '%s' to: %s%s%s%s%s%s\n",
			theSection.c_str(),
			theItemName.c_str(),
			!!(theCmd.vKey & kVKeyShiftFlag) ? "Shift+" : "",
			!!(theCmd.vKey & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(theCmd.vKey & kVKeyAltFlag) ? "Alt+" : "",
			!!(theCmd.vKey & kVKeyWinFlag) ? "Win+" : "",
			virtualKeyToName(theCmd.vKey & kVKeyMask).c_str(),
			isKeyBind
				? (std::string(" <signal #") +
					toString(theCmd.signalID) + ">").c_str()
				: "");
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("%s: Assigned '%s' to sequence: %s%s\n",
			theSection.c_str(),
			theItemName.c_str(),
			theCmdStr.c_str(),
			isKeyBind
				? (std::string(" <signal #") +
					toString(theCmd.signalID) + ">").c_str()
				: "");
		break;
	default:
		mapDebugPrint("%s: Assigned '%s' to command: %s\n",
			theSection.c_str(),
			theItemName.c_str(),
			theCmdStr.c_str());
		break;
	}
}


static Command stringToAliasCommand(
	InputMapBuilder& theBuilder,
	const std::string& theCmdStr,
	u16& theSignalID)
{
	// Keybinds can only be assigned to direct input - not special commands
	Command aCmd;
	if( theCmdStr.empty() )
	{// Do nothing but send a signal
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	// Check for a chat box macro
	aCmd = parseChatBoxMacro(theCmdStr);
	if( aCmd.type == eCmdType_DoNothing )
	{
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}
	if( aCmd.type != eCmdType_Empty )
	{
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	ECommandKeyWord aKeyWord = commandWordToID(condense(theCmdStr));
	if( aKeyWord == eCmdWord_Nothing )
	{// Specifically requested signal only
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	// Tap key
	theBuilder.parsedString.clear();
	sanitizeSentence(theCmdStr, theBuilder.parsedString);
	if( u16 aVKey = namesToVKey(theBuilder.parsedString) )
	{
		aCmd.type = eCmdType_TapKey;
		aCmd.signalID = theSignalID++;
		aCmd.vKey = aVKey;
		return aCmd;
	}

	// VKey Sequence
	std::string aVKeySeq =
		namesToVKeySequence(theBuilder, theBuilder.parsedString);
	if( aVKeySeq.empty() )
		return aCmd;

	aCmd.type = eCmdType_VKeySequence;
	aCmd.signalID = theSignalID++;
	aCmd.keyStringID = sKeyStrings.findOrAddIndex(aVKeySeq);

	return aCmd;
}


static Command createKeyBindEntry(
	InputMapBuilder& theBuilder,
	const std::string& theAlias,
	const std::string& theCmdStr,
	u16& theSignalID)
{
	const Command& aCmd = stringToAliasCommand(
		theBuilder, theCmdStr, theSignalID);

	// Check if could be part of a keybind array
	std::string aKeyBindArrayName = theAlias;
	const int anArrayIdx = breakOffIntegerSuffix(aKeyBindArrayName);
	if( anArrayIdx >= 0 )
	{
		u16 aKeyBindArrayID =
			theBuilder.keyBindArrayNameToIdxMap.findOrAdd(
				condense(aKeyBindArrayName), u16(sKeyBindArrays.size()));
		if( aKeyBindArrayID >= sKeyBindArrays.size() )
			sKeyBindArrays.resize(aKeyBindArrayID+1);
		KeyBindArray& aKeyBindArray = sKeyBindArrays[aKeyBindArrayID];
		if( anArrayIdx >= aKeyBindArray.size() )
			aKeyBindArray.resize(anArrayIdx+1);
		aKeyBindArray[anArrayIdx].cmd = aCmd;
		// Check for a matching named hotspot
		u16* aHotspotID = theBuilder.hotspotNameToIdxMap.find(
			condense(aKeyBindArrayName) + toString(anArrayIdx));
		if( aHotspotID )
			aKeyBindArray[anArrayIdx].hotspotID = *aHotspotID;
		mapDebugPrint("%s: Assigned '%s' Key Bind Array index #%d to %s%s\n",
			theBuilder.debugItemName.c_str(),
			aKeyBindArrayName.c_str(),
			anArrayIdx,
			theAlias.c_str(),
			aHotspotID ? " (+ hotspot)" : "");
	}

	return aCmd;
}


static void buildButtonAliases(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning custom button names...\n");
	const Profile::PropertyMap& aPropMap =
		Profile::getSectionProperties(kButtonAliasSectionName);
	for(size_t i = 0; i < aPropMap.size(); ++i)
	{
		theBuilder.buttonAliases.setValue(
			aPropMap.keys()[i],
			condense(aPropMap.vals()[i].val));
	}
}


static void buildCommandAliases(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning KeyBinds...\n");
	theBuilder.debugItemName = "[";
	theBuilder.debugItemName += kCommandAliasSectionName;
	theBuilder.debugItemName[theBuilder.debugItemName.size()-1] = ']';

	// Manually fetch all the special keys first
	u16 aNextSignalID;
	for(u16 i = 0; i < eSpecialKey_Num; ++i)
	{
		const std::string anAlias = kSpecialKeyNames[i];
		std::string aCmdStr =
			Profile::getStr(kCommandAliasSectionName, anAlias);
		aNextSignalID = eBtn_Num + i;
		Command aCmd = createKeyBindEntry(
			theBuilder, anAlias, aCmdStr, aNextSignalID);
		if( aCmd.type != eCmdType_SignalOnly &&
			aCmd.type != eCmdType_TapKey )
		{
			logError(
				"Special key bind '%s' may only be assigned to a "
				"single key or modifier+key (or nothing)! "
				"Could not assign %s!",
				anAlias.c_str(), aCmdStr.c_str());
			aCmd.type = eCmdType_SignalOnly;
			aCmd.signalID = eBtn_Num + i;
		}
		else
		{
			reportCommandAssignment(
				theBuilder.debugItemName, anAlias, aCmd, aCmdStr, true);
		}
		DBG_ASSERT(aCmd.signalID == eBtn_Num + i);
		sSpecialKeys[i] = aCmd.type == eCmdType_TapKey ? aCmd.vKey : 0;
	}
	// Add the special keys to the command aliases map
	for(u16 i = 0; i < eSpecialKey_Num; ++i)
	{
		Command aCmd; aCmd.vKey = sSpecialKeys[i];
		aCmd.type = aCmd.vKey ? eCmdType_TapKey : eCmdType_SignalOnly;
		aCmd.signalID = eBtn_Num + i;
		theBuilder.commandAliases.setValue(
			condense(kSpecialKeyNames[i]), aCmd);
	}

	// Generate special-key-to-command map
	Command aCmd;
	aCmd.type = eCmdType_StartAutoRun;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_AutoRun]), aCmd);
	aCmd.type = eCmdType_MoveTurn;
	aCmd.dir = eCmdDir_Forward;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_MoveF]), aCmd);
	aCmd.dir = eCmdDir_Back;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_MoveB]), aCmd);
	aCmd.dir = eCmdDir_Left;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_TurnL]), aCmd);
	aCmd.dir = eCmdDir_Right;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_TurnR]), aCmd);
	aCmd.type = eCmdType_MoveStrafe;
	aCmd.dir = eCmdDir_Left;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_StrafeL]), aCmd);
	aCmd.dir = eCmdDir_Right;
	theBuilder.specialKeyNameToCommandMap.setValue(
		condense(kSpecialKeyNames[eSpecialKey_StrafeR]), aCmd);

	// Now process all the rest of the key binds
	const Profile::PropertyMap& aPropMap =
		Profile::getSectionProperties(kCommandAliasSectionName);
	aNextSignalID = eBtn_Num + eSpecialKey_Num;
	theBuilder.elementsProcessed.clearAndResize(aPropMap.size());
	// Mark the already-processed special keys
	for(size_t i = 0; i < aPropMap.size(); ++i)
	{
		if( theBuilder.commandAliases.contains(aPropMap.keys()[i]) )
			theBuilder.elementsProcessed.set(i);
	}

	bool newKeyBindAdded = false;
	bool showErrors = false;
	while(!theBuilder.elementsProcessed.all())
	{
		for(size_t i = 0; i < aPropMap.size(); ++i)
		{
			if( theBuilder.elementsProcessed.test(i) )
				continue;

			const std::string& anAlias = aPropMap.vals()[i].name;
			const std::string& aCmdStr = aPropMap.vals()[i].val;

			const Command& aCmd = createKeyBindEntry(
					theBuilder, anAlias, aCmdStr, aNextSignalID);
			if( aCmd.type != eCmdType_Empty || showErrors )
			{
				theBuilder.commandAliases.setValue(condense(anAlias), aCmd);
				newKeyBindAdded = true;
				theBuilder.elementsProcessed.set(i);
				reportCommandAssignment(
					theBuilder.debugItemName, anAlias, aCmd, aCmdStr, true);
			}
		}
		showErrors = !newKeyBindAdded;
		newKeyBindAdded = false;
	}

	// Assign signal ID's to key bind arrays, which fire whenever ANY key in
	// the array is used, in addition to the specific key's signal
	for(u16 i = 0; i < sKeyBindArrays.size(); ++i)
		sKeyBindArrays[i].signalID = aNextSignalID++;

	// Can now also set size of global variables related to key binds
	gKeyBindArrayLastIndex.reserve(sKeyBindArrays.size());
	gKeyBindArrayLastIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.reserve(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayLastIndexChanged.clearAndResize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndexChanged.clearAndResize(sKeyBindArrays.size());
	gFiredSignals.clearAndResize(aNextSignalID);
}


static EButtonAction breakOffButtonAction(std::string& theButtonActionName)
{
	DBG_ASSERT(!theButtonActionName.empty());

	// Assume default "Down" action if none specified by a prefix
	EButtonAction result = eBtnAct_Down;

	// Check for action prefix - not many so just linear search
	for(size_t i = 0; i < eBtnAct_Num; ++i)
	{
		if( const size_t aPrefixPos =
				posAfterPrefix(theButtonActionName, kButtonActionPrefx[i]) )
		{
			result = EButtonAction(i);
			// Chop the prefix off the front of the string
			theButtonActionName = theButtonActionName.substr(aPrefixPos);
			break;
		}
	}

	return result;
}


static EButton buttonNameToID(
	InputMapBuilder& theBuilder,
	const std::string& theName)
{
	if( std::string* aBtnName = theBuilder.buttonAliases.find(theName) )
		return ::buttonNameToID(*aBtnName);
	return ::buttonNameToID(theName);
}


static void reportButtonAssignment(
	InputMapBuilder& theBuilder,
	const Command& theCmd,
	const std::string& theCmdStr,
	bool onlySpecificAction)
{
	#ifndef INPUT_MAP_DEBUG_PRINT
	if( theCmd.type == eCmdType_Empty )
	#endif
	{
		std::string aSection = "[";
		aSection += theBuilder.debugItemName.c_str();
		aSection += "]";
		std::string anItemName = theBuilder.debugSubItemName;
		if( onlySpecificAction )
			anItemName = std::string("(Just) ") + anItemName;
		reportCommandAssignment(aSection, anItemName, theCmd, theCmdStr);
	}
}


static void addButtonAction(
	InputMapBuilder& theBuilder,
	u16 theLayerIdx,
	std::string theBtnName,
	const std::string& theCmdStr,
	bool onlySpecificAction)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theBtnName.empty() || theCmdStr.empty() )
		return;

	// Determine button & action to assign command to
	EButtonAction aBtnAct = breakOffButtonAction(theBtnName);
	int aBtnTime = breakOffIntegerSuffix(theBtnName);
	std::string aBtnKeyName = condense(theBtnName);
	EButton aBtnID = buttonNameToID(theBuilder, aBtnKeyName);

	bool isA4DirMultiAssign =
		aBtnID == eBtn_LSAny ||
		aBtnID == eBtn_RSAny ||
		aBtnID == eBtn_DPadAny;
	if( aBtnID >= eBtn_Num )
	{
		// Button ID not identified from key string
		// Could be an attempt to assign multiple buttons at once as
		// a single 4-directional input (D-pad, analog sticks, etc).
		for(size_t i = 0; i < ARRAYSIZE(k4DirButtons); ++i)
		{
			if( aBtnKeyName == k4DirButtons[i] )
			{
				isA4DirMultiAssign = true;
				break;
			}
		}
	}

	if( isA4DirMultiAssign )
	{
		// Attempt to assign to all 4 directional variations of this button
		// to the same command (or directional variations of it).
		const Command& aBaseCmd = stringToCommand(
			theBuilder, theCmdStr, true,
			aBtnAct == eBtnAct_Down);
		bool dirCommandFailed = false;
		for(size_t i = 0; i < 4; ++i)
		{
			// Get true button ID by adding direction key to button name
			aBtnID = buttonNameToID(theBuilder,
				aBtnKeyName + k4DirKeyNames[i]);
			DBG_ASSERT(aBtnID < eBtn_Num);
			// See if can get a different command if append a direction,
			// if didn't already fail previously
			std::string aCmdStr = theCmdStr;
			Command aCmd;
			if( !dirCommandFailed )
			{
				aCmdStr += k4DirCmdSuffix[i];
				aCmd = stringToCommand(
					theBuilder, aCmdStr, true, aBtnAct == eBtnAct_Down);
			}
			// Get destination of command. Note that we do this AFTER
			// parsing the command because stringToCommand() can lead to
			// resizing sLayers and thus make this pointer invalid!
			ButtonActions* aDestBtn =
				&sLayers.vals()[theLayerIdx].buttonMap.findOrAdd(aBtnID);
			if( !onlySpecificAction ) aDestBtn->initIfEmpty();
			Command* aDestCmd = &(aDestBtn->cmd[aBtnAct]);
			// Direct assignment should take priority over multi-assignment,
			// so if this was already assigned directly then leave it alone.
			if( aDestCmd->type > eCmdType_Unassigned || !isA4DirMultiAssign )
				continue;
			if( aCmd.type < eCmdType_FirstDirectional )
			{// Not a valid directional command! Use base command
				aCmdStr = theCmdStr;
				aCmd = aBaseCmd;
				dirCommandFailed = true;
				// Possibly treat as a single assignment to the special
				// "any direction" button for this 4-dir input
				switch(aBtnID)
				{
				case eBtn_LSLeft: case eBtn_LSRight:
				case eBtn_LSUp: case eBtn_LSDown:
					aBtnID = eBtn_LSAny;
					isA4DirMultiAssign = false;
					break;
				case eBtn_RSLeft: case eBtn_RSRight:
				case eBtn_RSUp: case eBtn_RSDown:
					aBtnID = eBtn_RSAny;
					isA4DirMultiAssign = false;
					break;
				case eBtn_DLeft: case eBtn_DRight:
				case eBtn_DUp: case eBtn_DDown:
					aBtnID = eBtn_DPadAny;
					isA4DirMultiAssign = false;
					break;
				}
				if( !isA4DirMultiAssign )
				{
					aDestBtn = &sLayers.vals()[theLayerIdx].
						buttonMap.findOrAdd(aBtnID);
					if( !onlySpecificAction )
						aDestBtn->initIfEmpty();
					aDestCmd = &aDestBtn->cmd[aBtnAct];
				}
			}
			// Make and report assignment
			*aDestCmd = aCmd;
			if( aBtnAct == eBtnAct_Hold )
			{// Assign time to hold button for this action
				sButtonHoldTimes.setValue(
					std::pair<u16, EButton>(theLayerIdx, aBtnID),
					aBtnTime < 0
						? sDefaultButtonHoldTime
						: aBtnTime);
			}
			std::string anExtPropName =
				theBuilder.debugSubItemName + k4DirCmdSuffix[i];
			if( isA4DirMultiAssign )
				swap(theBuilder.debugSubItemName, anExtPropName);
			reportButtonAssignment(
				theBuilder, aCmd, aCmdStr, onlySpecificAction);
			if( isA4DirMultiAssign )
				swap(theBuilder.debugSubItemName, anExtPropName);
		}
		return;
	}

	if( aBtnTime >= 0 && aBtnID >= eBtn_Num )
	{// Part of the button's name might have been absorbed into aBtnTime
		std::string aTimeAsString = toString(aBtnTime);
		do {
			theBtnName.push_back(aTimeAsString[0]);
			aTimeAsString.erase(0, 1);
			aBtnKeyName = condense(theBtnName);
			aBtnID = buttonNameToID(theBuilder, aBtnKeyName);
			aBtnTime = aTimeAsString.empty()
				? -1 : intFromString(aTimeAsString);
		} while(aBtnID >= eBtn_Num && !aTimeAsString.empty());
	}

	// If still no valid button ID, must just be a badly-named action + button key
	if( aBtnID >= eBtn_Num )
	{
		logError("Unable to identify Gamepad Button '%s' requested in [%s]",
			theBtnName.c_str(),
			theBuilder.debugItemName.c_str());
		return;
	}

	// Parse command string into a Command struct
	Command aCmd = stringToCommand(
		theBuilder, theCmdStr, true, aBtnAct == eBtnAct_Down);

	// Make the assignment
	ButtonActions& aDestBtn =
		sLayers.vals()[theLayerIdx].buttonMap.findOrAdd(aBtnID);
	if( !onlySpecificAction ) aDestBtn.initIfEmpty();
	aDestBtn.cmd[aBtnAct] = aCmd;
	if( aBtnAct == eBtnAct_Hold )
	{// Assign time to hold button for this action
		sButtonHoldTimes.setValue(
			std::pair<u16, EButton>(theLayerIdx, aBtnID),
			aBtnTime < 0
				? sDefaultButtonHoldTime
				: aBtnTime);
	}

	// Report the results of the assignment
	reportButtonAssignment(
		theBuilder, aCmd, theCmdStr, onlySpecificAction);
}


static void addSignalCommand(
	InputMapBuilder& theBuilder,
	u16 theLayerIdx,
	std::string theSignalName,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theSignalName.empty() || theCmdStr.empty() )
		return;

	// Check for responding to use of any key in a key bind array
	std::string aSignalKey = condense(theSignalName);
	if( u16* aKeyBindArrayID =
			theBuilder.keyBindArrayNameToIdxMap.find(aSignalKey) )
	{
		Command aCmd = stringToCommand(theBuilder, theCmdStr, true);
		if( aCmd.type != eCmdType_Empty )
		{
			sLayers.vals()[theLayerIdx].signalCommands.setValue(
				sKeyBindArrays[*aKeyBindArrayID].signalID, aCmd);
		}

		// Report the results of the assignment
		#ifndef INPUT_MAP_DEBUG_PRINT
		if( aCmd.type == eCmdType_Empty )
		#endif
		{
			std::string aSection = "[";
			aSection += theBuilder.debugItemName;
			aSection += "]";
			std::string anItemName = kSignalCommandPrefix;
			anItemName += " ";
			anItemName += theBuilder.debugSubItemName;
			anItemName += " (signal #";
			anItemName += toString(sKeyBindArrays[*aKeyBindArrayID].signalID);
			anItemName += ")";
			reportCommandAssignment(aSection, anItemName, aCmd, theCmdStr);
		}
		return;
	}

	// Check for responding to use of a key bind
	if( Command* aCommandAlias = theBuilder.commandAliases.find(aSignalKey) )
	{
		Command aCmd = stringToCommand(theBuilder, theCmdStr, true);
		if( aCmd.type != eCmdType_Empty )
		{
			sLayers.vals()[theLayerIdx].signalCommands.setValue(
				aCommandAlias->signalID, aCmd);
		}

		// Report the results of the assignment
		#ifndef INPUT_MAP_DEBUG_PRINT
		if( aCmd.type == eCmdType_Empty )
		#endif
		{
			std::string aSection = "[";
			aSection += theBuilder.debugItemName;
			aSection += "]";
			std::string anItemName = kSignalCommandPrefix;
			anItemName += " ";
			anItemName += theBuilder.debugSubItemName;
			anItemName += " (signal #";
			anItemName += toString(aCommandAlias->signalID);
			anItemName += ")";
			reportCommandAssignment(aSection, anItemName, aCmd, theCmdStr);
		}
		return;
	}

	// Check for responding to use of of "Move", "MoveTurn", and "MoveStrafe"
	switch(commandWordToID(aSignalKey))
	{
	case eCmdWord_Move:
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeL], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeR], theCmdStr);
		// fall through
	case eCmdWord_Turn:
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveF], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveB], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_TurnL], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_TurnR], theCmdStr);
		return;
	case eCmdWord_Strafe:
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveF], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_MoveB], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeL], theCmdStr);
		addSignalCommand(theBuilder, theLayerIdx,
			kSpecialKeyNames[eSpecialKey_StrafeR], theCmdStr);
		return;
	}

	// For button press signals, need to actually use the word "press"
	// Other button actions (tap, hold, release) are not supported as signals
	if( breakOffButtonAction(theSignalName) == eBtnAct_Press )
	{
		aSignalKey = condense(theSignalName);
		EButton aBtnID = buttonNameToID(theBuilder, aSignalKey);
		bool isA4DirMultiAssign = false;
		if( aBtnID >= eBtn_Num )
		{
			for(size_t i = 0; i < ARRAYSIZE(k4DirButtons); ++i)
			{
				if( aSignalKey == k4DirButtons[i] )
				{
					isA4DirMultiAssign = true;
					break;
				}
			}
		}

		if( isA4DirMultiAssign )
		{
			// Attempt to assign to all 4 directional variations of this button
			// to the same command (or directional variations of it).
			const Command& aBaseCmd = stringToCommand(
				theBuilder, theCmdStr, true);
			bool dirCommandFailed = false;
			for(size_t i = 0; i < 4; ++i)
			{
				aBtnID = buttonNameToID(theBuilder,
					aSignalKey + k4DirKeyNames[i]);
				DBG_ASSERT(aBtnID < eBtn_Num);
				std::string aCmdStr = theCmdStr;
				Command aCmd;
				if( !dirCommandFailed )
				{
					aCmdStr += k4DirCmdSuffix[i];
					aCmd = stringToCommand(theBuilder, aCmdStr, true);
				}
				if( aCmd.type == eCmdType_Empty )
				{
					aCmdStr = theCmdStr;
					aCmd = aBaseCmd;
					dirCommandFailed = true;
				}
				Command& aDestCmd = sLayers.vals()[theLayerIdx].
					signalCommands.findOrAdd(aBtnID);
				if( aDestCmd.type != eCmdType_Empty )
					continue;
				aDestCmd = aCmd;
				#ifndef INPUT_MAP_DEBUG_PRINT
				if( aCmd.type == eCmdType_Empty )
				#endif
				{
					std::string aSection = "[";
					aSection += theBuilder.debugItemName.c_str();
					aSection += "]";
					std::string anItemName = kSignalCommandPrefix;
					anItemName += " ";
					anItemName += theBuilder.debugSubItemName;
					anItemName += k4DirCmdSuffix[i];
					anItemName += " (signal #";
					anItemName += toString(aBtnID);
					anItemName += ")";
					reportCommandAssignment(
						aSection, anItemName, aCmd, theCmdStr);
				}
			}
			return;
		}

		if( aBtnID != eBtn_None && aBtnID < eBtn_Num )
		{
			Command aCmd = stringToCommand(theBuilder, theCmdStr, true);

			// Make the assignment - each button ID matches its signal ID
			if( !aCmd.type == eCmdType_Empty )
			{
				sLayers.vals()[theLayerIdx].
					signalCommands.setValue(aBtnID, aCmd);
			}

			// Report the results of the assignment
			#ifndef INPUT_MAP_DEBUG_PRINT
			if( aCmd.type == eCmdType_Empty )
			#endif
			{
				std::string aSection = "[";
				aSection += theBuilder.debugItemName.c_str();
				aSection += "]";
				std::string anItemName = kSignalCommandPrefix;
				anItemName += " ";
				anItemName += theBuilder.debugSubItemName;
				anItemName += " (signal #";
				anItemName += toString(aBtnID);
				anItemName += ")";
				reportCommandAssignment(
					aSection, anItemName, aCmd, theCmdStr);
			}
			return;
		}
	}

	logError("Unrecognized signal name for '%s %s' requested in [%s]",
		kSignalCommandPrefix.c_str(),
		theSignalName.c_str(),
		theBuilder.debugItemName.c_str());
}


static void buildControlsLayer(
	InputMapBuilder& theBuilder,
	u16 theLayerID,
	const Profile::PropertyMap& theProperties)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	ControlsLayer& theLayer = sLayers.vals()[theLayerID];
	theBuilder.debugItemName =
		(theLayerID == 0 ? "" : kLayerPrefix) +
		theLayer.label;

	for(u16 aPropIdx = 0; aPropIdx < theProperties.size(); ++aPropIdx)
	{
		const Profile::Property& aProperty = theProperties.vals()[aPropIdx];
		const std::string& aPropKey = theProperties.keys()[aPropIdx];
		const std::string& aPropName = aProperty.name;
		const std::string& aPropVal = aProperty.val;

		if( aPropKey == kMouseModeKey )
		{// MOUSE MODE
			const EMouseMode aMouseMode =
				mouseModeNameToID(condense(aPropVal));
			if( aMouseMode >= eMouseMode_Num )
			{
				logError("Unknown mode for 'Mouse = %s' in Layer [%s]!",
					aPropVal.c_str(),
					theBuilder.debugItemName.c_str());
			}
			else
			{
				theLayer.mouseMode = aMouseMode;
				mapDebugPrint("[%s]: Mouse set to '%s' mode\n",
					theBuilder.debugItemName.c_str(),
					aMouseMode == eMouseMode_Cursor ? "Cursor" :
					aMouseMode == eMouseMode_LookTurn ? "RMB Mouse Look" :
					aMouseMode == eMouseMode_LookOnly ? "LMB Mouse Look" :
					aMouseMode == eMouseMode_AutoLook ? "Auto-Look" :
					aMouseMode == eMouseMode_AutoRunLook ? "Auto-Run-Look" :
					aMouseMode == eMouseMode_Hide ? "Hide" :
					/*otherwise*/ "Default (use other laye)" );
			}
		}
		else if( aPropKey == kHUDSettingsKey )
		{// HUD VISIBILITY
			// TODO
		}
		else if( aPropKey == kHotspotArraysKey )
		{// HOTSPOTS
			DBG_ASSERT(theLayer.enableHotspots.size() == sHotspotArrays.size());
			DBG_ASSERT(theLayer.disableHotspots.size() == sHotspotArrays.size());
			theLayer.enableHotspots.reset();
			theLayer.disableHotspots.reset();

			// Break the string into individual words
			theBuilder.parsedString.clear();
			sanitizeSentence(aPropVal, theBuilder.parsedString);

			bool enable = true;
			for(size_t i = 0; i < theBuilder.parsedString.size(); ++i)
			{
				const std::string& aName = theBuilder.parsedString[i];
				const std::string& aUpperName = upper(aName);
				if( aUpperName == "HIDE" || aUpperName == "DISABLE" )
				{
					enable = false;
					continue;
				}
				if( aUpperName == "SHOW" || aUpperName == "ENABLE" )
				{
					enable = true;
					continue;
				}
				if( commandWordToID(aUpperName) == eCmdWord_Filler )
					continue;
				if( u16* aHotspotArrayID =
						theBuilder.hotspotArrayNameToIdxMap.find(aUpperName) )
				{
					theLayer.enableHotspots.set(*aHotspotArrayID, enable);
					theLayer.disableHotspots.set(*aHotspotArrayID, !enable);
				}
				else
				{
					logError(
						"Could not find Hotspot Array '%s' "
						"referenced by [%s]/Hotspots = %s",
						aName.c_str(),
						theBuilder.debugItemName.c_str(),
						aPropVal.c_str());
				}
			}
		}
		else if( aPropKey == kPriorityKey )
		{// PRIORITY
			int aPriority = intFromString(aPropVal);
			if( aPriority < -100 || aPriority > 100 )
			{
				logError(
					"Layer [%s] Priority = %d property "
					"must be -100 to 100 range!",
					theBuilder.debugItemName.c_str(),
					aPropVal.c_str());
				aPriority = clamp(aPriority, -100, 100);
			}
			if( theLayerID == 0 )
			{
				logError(
					"Root layer [%s] is always lowest priority. "
					"Priority = %d property ignored!",
					theBuilder.debugItemName.c_str(),
					aPropVal.c_str());
			}
			else if( theLayer.isComboLayer )
			{
				logError(
					"Combo Layer [%s] ordering is derived automatically "
					"from base layers, so Priority = %d property is ignored!",
					theBuilder.debugItemName.c_str(),
					aPropVal.c_str());
			}
			else
			{
				theLayer.priority = s8(aPriority);
			}
		}
		else if( aPropKey == kParentLayerKey )
		{// PARENT LAYER
			u16 aParentLayerID = 0;
			if( !aPropVal.empty() )
			{
				aParentLayerID = getLayerID(aPropVal);
				if( !aParentLayerID )
				{
					// TODO - logError
				}
			}
			if( aParentLayerID )
			{
				if( theLayerID == 0 )
				{
					logError(
						"Root layer [%s] can not have a Parent= layer set!",
						theBuilder.debugItemName.c_str(),
						aPropVal.c_str());
				}
				else if( theLayer.isComboLayer )
				{
					logError(
						"\"Parent=%s\" property ignored for Combo Layer [%s]!",
						aPropVal.c_str(),
						theBuilder.debugItemName.c_str());
				}
				else
				{
					theLayer.parentLayer = aParentLayerID;
					// Check for infinite parent loop
					theBuilder.elementsProcessed.clearAndResize(sLayers.size());
					u16 aCheckLayerIdx = theLayerID;
					theBuilder.elementsProcessed.set(aCheckLayerIdx);
					while(sLayers.vals()[aCheckLayerIdx].parentLayer != 0)
					{
						aCheckLayerIdx =
							sLayers.vals()[aCheckLayerIdx].parentLayer;
						if( theBuilder.elementsProcessed.test(aCheckLayerIdx) )
						{
							logError("Infinite parent loop with layer [%s]"
								" trying to set parent layer to %s!",
								theBuilder.debugItemName.c_str(),
								aPropVal.c_str());
							theLayer.parentLayer = 0;
							break;
						}
						theBuilder.elementsProcessed.set(aCheckLayerIdx);
					}
					mapDebugPrint("[%s]: Parent layer set to '%s'\n",
						theBuilder.debugItemName.c_str(),
						sLayers.vals()[aParentLayerID].label.c_str());
				}
			}
		}
		else if( size_t aStrPos =
					posAfterPrefix(aPropName, kSignalCommandPrefix) )
		{// WHEN SIGNAL
			theBuilder.debugSubItemName = aPropName.substr(aStrPos);
			addSignalCommand(theBuilder, theLayerID,
				aPropName.substr(aStrPos),
				aPropVal);
		}
		else if( size_t aStrPos =
					posAfterPrefix(aPropName, kActionOnlyPrefix) )
		{// "JUST" BUTTON ACTION
			theBuilder.debugSubItemName = aPropName.substr(aStrPos);
			addButtonAction(theBuilder, theLayerID,
				aPropName.substr(aStrPos),
				aPropVal, true);
		}
		else 
		{// BUTTON COMMAND ASSIGNMENT (?)
			theBuilder.debugSubItemName = aPropName;
			addButtonAction(theBuilder, theLayerID,
				aPropName,
				aPropVal, false);
		}
	}
}


static void buildControlScheme(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Building control scheme layers...\n");

	for(u16 aLayerIdx = 0; aLayerIdx < sLayers.size(); ++aLayerIdx)
	{
		ControlsLayer& aLayer = sLayers.vals()[aLayerIdx];
		mapDebugPrint("Building controls layer: %s\n", aLayer.label.c_str());
		aLayer.enableHotspots.clearAndResize(sHotspotArrays.size());
		aLayer.disableHotspots.clearAndResize(sHotspotArrays.size());
		const std::string aLayerSectName =
			(aLayerIdx == 0 ? "" : kLayerPrefix) +
			sLayers.keys()[aLayerIdx];
		const Profile::PropertyMap& aPropMap =
			Profile::getSectionProperties(aLayerSectName);
		buildControlsLayer(theBuilder, aLayerIdx, aPropMap);
	}
}


static std::string breakOffMenuItemLabel(std::string& theString)
{
	// Get the label (part of the string before first single colon)
	// Double colons within label become single colons instead of end of label,
	// and are ignored (become whitespace) in the remaining string
	theString = replaceAllStr(theString, "::", "\x01");
	std::string result = breakOffItemBeforeChar(theString, ':');
	result = replaceChar(result, '\x01', ':');
	theString = replaceChar(theString, '\x01', ' ');
	return result;
}


static MenuItem stringToMenuItem(
	InputMapBuilder& theBuilder,
	u16 theMenuID,
	std::string theString)
{
	MenuItem aMenuItem;
	if( theString.empty() )
	{
		mapDebugPrint("%s: Left <unnamed> and <unassigned>!\n",
			theBuilder.debugItemName.c_str());
		return aMenuItem;
	}

	std::string aLabel = breakOffMenuItemLabel(theString);
	if( aLabel.empty() && !theString.empty() && theString[0] != ':' )
	{// Having no : character means this points to a sub-menu
		const size_t anOldMenuCount = sMenus.size();
		aMenuItem.cmd.type = eCmdType_OpenSubMenu;
		aMenuItem.cmd.subMenuID = getOrCreateMenuID(trim(theString), theMenuID);
		aMenuItem.cmd.menuID =
			sMenus.vals()[aMenuItem.cmd.subMenuID].rootMenuID;
		aMenuItem.label =
			sMenus.vals()[aMenuItem.cmd.subMenuID].label;
		if( sMenus.size() > anOldMenuCount )
		{
			mapDebugPrint("%s: Sub-Menu: '%s'\n",
				theBuilder.debugItemName.c_str(),
				aMenuItem.label.c_str());
		}
		else
		{
			mapDebugPrint("%s: Swap to '%s'\n",
				theBuilder.debugItemName.c_str(),
				menuSectionName(aMenuItem.cmd.subMenuID).c_str());
		}
		return aMenuItem;
	}

	if( aLabel.empty() && !theString.empty() && theString[0] == ':' )
	{// Possibly valid command with just an empty label
		theString = trim(&theString[1]);
		aLabel = "<unnamed>";
	}
	else
	{// Has a label, but may actually be 2 labels separated by '|'
		aMenuItem.altLabel = breakOffItemBeforeChar(aLabel, '|');
		if( aLabel[0] == '|' )
			aLabel = aLabel.substr(1);
		aMenuItem.label = aLabel;
		if( !aMenuItem.altLabel.empty() )
			aLabel += std::string(" (") + aMenuItem.altLabel + ")";
	}

	if( theString.empty() )
	{
		mapDebugPrint("%s: '%s' left <unassigned>!\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	if( theString == ".." ||
		commandWordToID(condense(theString)) == eCmdWord_Back )
	{// Go back one sub-menu
		aMenuItem.cmd.type = eCmdType_MenuBack;
		aMenuItem.cmd.menuID = sMenus.vals()[theMenuID].rootMenuID;
		mapDebugPrint("%s: '%s' assigned to back out of menu\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	if( commandWordToID(condense(theString)) == eCmdWord_Close )
	{// Close menu
		aMenuItem.cmd.type = eCmdType_MenuClose;
		aMenuItem.cmd.menuID = sMenus.vals()[theMenuID].rootMenuID;
		mapDebugPrint("%s: '%s' assigned to close menu\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	aMenuItem.cmd = stringToCommand(theBuilder, theString);
	if( aMenuItem.cmd.type == eCmdType_Empty )
	{
		// Probably just forgot the > at front of a plain string
		aMenuItem.cmd = parseChatBoxMacro(std::string(">") + theString);
		logError("%s: '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box macro. "
				 "Add > to start of it if this was the intent!",
				theBuilder.debugItemName.c_str(),
				aLabel.c_str(), theString.c_str());
	}
	else
	{
		reportCommandAssignment(theBuilder.debugItemName,
			aLabel, aMenuItem.cmd, theString);
	}

	return aMenuItem;
}


static void buildMenus(InputMapBuilder& theBuilder)
{
	if( !sMenus.empty() )
		mapDebugPrint("Building Menus...\n");

	// This loop expects sMenus.size() may grow larger during the loop
	for(u16 aMenuID = 0; aMenuID < sMenus.size(); ++aMenuID)
	{
		const std::string aMenuSectName = kMenuPrefix + sMenus.keys()[aMenuID];
		const u16 aHUDElementID = sMenus.vals()[aMenuID].hudElementID;
		DBG_ASSERT(aHUDElementID < sHUDElements.size());
		const EHUDType aMenuStyle = sHUDElements.vals()[aHUDElementID].type;
		DBG_ASSERT(aMenuStyle >= eMenuStyle_Begin);
		DBG_ASSERT(aMenuStyle < eMenuStyle_End);
		std::string aDebugNamePrefix;
		#ifndef NDEBUG
		aDebugNamePrefix =
			std::string("[") + menuSectionName(aMenuID) + "] (";
		#endif

		// Check for command to execute automatically on menu open
		std::string aSpecialMenuCommandStr =
			Profile::getStr(aMenuSectName, kMenuOpenKey);
		if( !aSpecialMenuCommandStr.empty() )
		{
			theBuilder.debugItemName =
				aDebugNamePrefix + kMenuOpenKey + ")";
			const Command& aCmd =
				stringToCommand(theBuilder, aSpecialMenuCommandStr);
			if( aCmd.type == eCmdType_Empty ||
				(aCmd.type >= eCmdType_FirstMenuControl &&
				 aCmd.type <= eCmdType_LastMenuControl) )
			{
				logError("%s: Invalid command '%s'!",
					theBuilder.debugItemName.c_str(),
					aSpecialMenuCommandStr.c_str());
				sMenus.vals()[aMenuID].autoCommand = Command();
			}
			else
			{
				sMenus.vals()[aMenuID].autoCommand = aCmd;
				mapDebugPrint("%s: Assigned to command: %s\n",
					theBuilder.debugItemName.c_str(),
					aSpecialMenuCommandStr.c_str());
			}
		}

		// Check for command to execute automatically when back out of menu
		aSpecialMenuCommandStr =
			Profile::getStr(aMenuSectName, kMenuCloseKey);
		if( !aSpecialMenuCommandStr.empty() )
		{
			theBuilder.debugItemName =
				aDebugNamePrefix + kMenuCloseKey + ")";
			const Command& aCmd =
				stringToCommand(theBuilder, aSpecialMenuCommandStr);
			if( aCmd.type == eCmdType_Empty ||
				(aCmd.type >= eCmdType_FirstMenuControl &&
				 aCmd.type <= eCmdType_LastMenuControl) )
			{
				logError("%s: Invalid command '%s'!",
					theBuilder.debugItemName.c_str(),
					aSpecialMenuCommandStr.c_str());
				sMenus.vals()[aMenuID].backCommand = Command();
			}
			else
			{
				sMenus.vals()[aMenuID].backCommand = aCmd;
				mapDebugPrint("%s: Assigned to command: %s\n",
					theBuilder.debugItemName.c_str(),
					aSpecialMenuCommandStr.c_str());
			}
		}

		u16 itemIdx = 0;
		bool checkForNextMenuItem = aMenuStyle != eMenuStyle_4Dir;
		while(checkForNextMenuItem)
		{
			checkForNextMenuItem = false;
			const std::string& aMenuItemKeyName = toString(itemIdx+1);
			const std::string& aMenuItemString = Profile::getStr(
				aMenuSectName, aMenuItemKeyName);
			checkForNextMenuItem = !aMenuItemString.empty();
			if( aMenuStyle == eMenuStyle_Hotspots )
			{// Guarantee menu item count matches hotspot count
				const u16 anArrayID =
					sHUDElements.vals()[aHUDElementID].hotspotArrayID;
				DBG_ASSERT(anArrayID < sHotspotArrays.size());
				const HotspotArray& anArray = sHotspotArrays[anArrayID];
				const u16 anArraySize = anArray.last - anArray.first + 1;
				checkForNextMenuItem = itemIdx < anArraySize;
			}
			if( checkForNextMenuItem )
			{
				theBuilder.debugItemName =
					aDebugNamePrefix + aMenuItemKeyName + ")";
				sMenus.vals()[aMenuID].items.push_back(
					stringToMenuItem(
						theBuilder,
						aMenuID,
						aMenuItemString));
			}
			++itemIdx;
		}
		bool hasAtLeastOneMenuItem = !sMenus.vals()[aMenuID].items.empty();
		for(itemIdx = 0; itemIdx < eCmdDir_Num; ++itemIdx)
		{
			const std::string aMenuItemKeyName = k4DirMenuItemLabel[itemIdx];
			const std::string& aMenuItemString = Profile::getStr(
				aMenuSectName, aMenuItemKeyName);
			if( !aMenuItemString.empty() )
			{
				theBuilder.debugItemName =
					aDebugNamePrefix + aMenuItemKeyName + ")";
				hasAtLeastOneMenuItem = true;
				sMenus.vals()[aMenuID].dirItems[itemIdx] =
					stringToMenuItem(
						theBuilder,
						aMenuID,
						aMenuItemString);
				MenuItem& aMenuItem =
					sMenus.vals()[aMenuID].dirItems[itemIdx];
				if( aMenuItem.cmd.type == eCmdType_OpenSubMenu &&
					aMenuStyle != eMenuStyle_4Dir )
				{
					// Requests to open sub-menus with directionals in
					// most menu styles should use swap menu instead,
					// meaning they'll stay on the same "level" as the
					// previous menu instead of being a "child" menu.
					aMenuItem.cmd.type = eCmdType_SwapMenu;
					aMenuItem.cmd.swapDir = u8(itemIdx);
				}
			}
		}

		if( !hasAtLeastOneMenuItem )
		{
			logError("[%s]: No menu items found! If empty menu intended, "
				"Set \"%s = :\" to suppress this error",
				aMenuSectName.c_str(),
				aMenuStyle == eMenuStyle_4Dir ? "U" : "1");
			if( aMenuStyle != eMenuStyle_4Dir )
				sMenus.vals()[aMenuID].items.push_back(MenuItem());
		}
	}
}


static void buildHUDElementsForLayer(
	InputMapBuilder& theBuilder,
	u16 theLayerID)
{
	if( theBuilder.elementsProcessed.test(theLayerID) )
		return;
	theBuilder.elementsProcessed.set(theLayerID);
	theBuilder.debugItemName.clear();
	if( theLayerID != 0 )
		theBuilder.debugItemName = kLayerPrefix;
	theBuilder.debugItemName += sLayers.vals()[theLayerID].label;
	sLayers.vals()[theLayerID].hideHUD.clearAndResize(sHUDElements.size());
	sLayers.vals()[theLayerID].showHUD.clearAndResize(sHUDElements.size());
	const std::string aLayerSectName =
		(theLayerID == 0 ? "" : kLayerPrefix) + sLayers.keys()[theLayerID];
	const std::string& aLayerHUDDescription =
		Profile::getStr(aLayerSectName, kHUDSettingsKey);

	if( aLayerHUDDescription.empty() )
		return;

	// Break the string into individual words
	theBuilder.parsedString.clear();
	sanitizeSentence(aLayerHUDDescription, theBuilder.parsedString);

	bool show = true;
	for(size_t i = 0; i < theBuilder.parsedString.size(); ++i)
	{
		const std::string& anElementName = theBuilder.parsedString[i];
		const std::string& anElementUpperName = upper(anElementName);
		if( anElementUpperName == "HIDE" || anElementUpperName == "DISABLE" )
		{
			show = false;
			continue;
		}
		if( anElementUpperName == "SHOW" || anElementUpperName == "ENABLE" )
		{
			show = true;
			continue;
		}
		if( commandWordToID(anElementUpperName) == eCmdWord_Filler )
			continue;
		u16 anElementIdx = getOrCreateHUDElementID(
			theBuilder, anElementName, false);
		sLayers.vals()[theLayerID].showHUD.resize(sHUDElements.size());
		sLayers.vals()[theLayerID].showHUD.set(anElementIdx, show);
		sLayers.vals()[theLayerID].hideHUD.resize(sHUDElements.size());
		sLayers.vals()[theLayerID].hideHUD.set(anElementIdx, !show);
	}
}


static void buildHUDElements(InputMapBuilder& theBuilder)
{
	// Process the "HUD=" key for each layer
	theBuilder.elementsProcessed.clearAndResize(sLayers.size());
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
		buildHUDElementsForLayer(theBuilder, aLayerID);

	// Special-case internally-managed HUD elements
	sHUDElements.setValue("~HSGuide~", HUDElement(eHUDType_HotspotGuide));
	sHUDElements.setValue("~System~", HUDElement(eHUDType_System));

	// Above may have added new HUD elements. Now that all are added,
	// make sure every layer's hideHUD and showHUD are correct size
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
	{
		sLayers.vals()[aLayerID].hideHUD.resize(sHUDElements.size());
		sLayers.vals()[aLayerID].showHUD.resize(sHUDElements.size());
	}

	// Can now also set size of global sizes related to HUD elements
	gVisibleHUD.clearAndResize(sHUDElements.size());
	gRedrawHUD.clearAndResize(sHUDElements.size());
	gFullRedrawHUD.clearAndResize(sHUDElements.size());
	gReshapeHUD.clearAndResize(sHUDElements.size());
	gActiveHUD.clearAndResize(sHUDElements.size());
	gDisabledHUD.clearAndResize(sHUDElements.size());
	gConfirmedMenuItem.resize(sHUDElements.size());
	for(size_t i = 0; i < gConfirmedMenuItem.size(); ++i)
		gConfirmedMenuItem[i] = kInvalidItem;
}


static void parseLabel(InputMapBuilder& theBuilder, std::string& theLabel)
{
	if( theLabel.size() < 3 )
		return;

	// Search for replacement text tags in format <tag>
	bool replacementNeeded = false;
	std::pair<std::string::size_type, std::string::size_type> aTagCoords =
		findStringTag(theLabel);
	while(aTagCoords.first != std::string::npos )
	{
		const std::string& aTag = condense(theLabel.substr(
			aTagCoords.first + 1, aTagCoords.second - 2));

		// Generate the replacement character sequence
		std::string aNewStr;
		// See if tag matches a layer name to display layer status
		u16 aLayerID = sLayers.findIndex(aTag);
		if( aLayerID > 0 && aLayerID < sLayers.size() )
		{// Set replacement to a 3-char sequence for layer ID
			aNewStr.push_back(kLayerStatusReplaceChar);
			// Encode the layer ID into 14-bit as in checkForVKeySeqPause()
			DBG_ASSERT(aLayerID <= 0x3FFF);
			aNewStr.push_back(u8((((aLayerID) >> 7) & 0x7F) | 0x80));
			aNewStr.push_back(u8(((aLayerID) & 0x7F) | 0x80));
		}
		// TODO: Button names to be replaced with their current assigned comand

		if( !aNewStr.empty() )
		{
			theLabel.replace(aTagCoords.first, aTagCoords.second, aNewStr);
			replacementNeeded = true;
		}
		aTagCoords = findStringTag(theLabel, aTagCoords.first+1);
	}

	if( replacementNeeded )
		theLabel = std::string(1, kLabelContainsDynamicText) + theLabel;
}


static void buildLabels(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Parsing labels for text replacements...\n");
	for(u16 i = 0; i < sHUDElements.size(); ++i)
		parseLabel(theBuilder, sHUDElements.vals()[i].label);
	for(u16 i = 0; i < sMenus.size(); ++i)
	{
		parseLabel(theBuilder, sMenus.vals()[i].label);
		for(u16 aDir = 0; aDir < ARRAYSIZE(sMenus.vals()[i].dirItems); ++aDir)
		{
			parseLabel(theBuilder, sMenus.vals()[i].dirItems[aDir].label);
			parseLabel(theBuilder, sMenus.vals()[i].dirItems[aDir].altLabel);
		}
		for(u16 anItem = 0; anItem < sMenus.vals()[i].items.size(); ++anItem)
		{
			parseLabel(theBuilder, sMenus.vals()[i].items[anItem].label);
			parseLabel(theBuilder, sMenus.vals()[i].items[anItem].altLabel);
		}
	}
}


static void trimMemoryUsage()
{
	if( sHotspots.size() < sHotspots.capacity() )
		std::vector<Hotspot>(sHotspots).swap(sHotspots);
	if( sHotspotArrays.size() < sHotspotArrays.capacity() )
		std::vector<HotspotArray>(sHotspotArrays).swap(sHotspotArrays);
	if( sKeyBindArrays.size() < sKeyBindArrays.capacity() )
		std::vector<KeyBindArray>(sKeyBindArrays).swap(sKeyBindArrays);
	sKeyStrings.trim();
	sLayers.trim();
	sComboLayers.trim();
	sHUDElements.trim();
	sMenus.trim();
	sButtonHoldTimes.trim();
	for(std::vector<Menu>::iterator itr = sMenus.vals().begin();
		itr != sMenus.vals().end(); ++itr)
	{
		if( itr->items.size() < itr->items.capacity() )
			std::vector<MenuItem>(itr->items).swap(itr->items);
	}
	for(std::vector<ControlsLayer>::iterator itr = sLayers.vals().begin();
		itr != sLayers.vals().end(); ++itr)
	{
		itr->signalCommands.trim();
		itr->buttonMap.trim();
	}
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	ZeroMemory(&sSpecialKeys, sizeof(sSpecialKeys));
	sHotspots.clear();
	sHotspotArrays.clear();
	sKeyStrings.clear();
	sKeyBindArrays.clear();
	sLayers.clear();
	sComboLayers.clear();
	sMenus.clear();
	sHUDElements.clear();
	sButtonHoldTimes.clear();

	// Get default button hold time to execute eBtnAct_Hold command
	sDefaultButtonHoldTime =
		max(0, Profile::getInt("System", "ButtonHoldTime"));

	// Create empty objects for each layer, menu, etc
	std::vector<std::string> aSectionNameList;
	// Main layer (0 / [Scheme]) also acts as sentinel since can't reference it
	createEmptyLayer(kMainLayerSectionName);
	Profile::getSectionNamesStartingWith(kLayerPrefix, aSectionNameList);
	for(size_t i = 0; i < aSectionNameList.size(); ++i)
		createEmptyLayer(aSectionNameList[i]);
	for(size_t i = 0; i < aSectionNameList.size(); ++i)
		tryCreateComboLayer(aSectionNameList[i]);

	// Create temp builder object and build everything from the Profile data
	{
		InputMapBuilder anInputMapBuilder;
		buildHotspots(anInputMapBuilder);
		buildButtonAliases(anInputMapBuilder);
		buildCommandAliases(anInputMapBuilder);
		buildControlScheme(anInputMapBuilder);
		buildMenus(anInputMapBuilder);
		buildHUDElements(anInputMapBuilder);
		buildLabels(anInputMapBuilder);
	}

	trimMemoryUsage();
}


void loadProfileChanges()
{
	// TODO
}


const char* cmdStr(const Command& theCommand)
{
	DBG_ASSERT(
		theCommand.type == eCmdType_VKeySequence ||
		theCommand.type == eCmdType_ChatBoxString);
	DBG_ASSERT(theCommand.keyStringID < sKeyStrings.size());
	return sKeyStrings.keys()[theCommand.keyStringID].c_str();
}


u16 keyForSpecialAction(ESpecialKey theAction)
{
	DBG_ASSERT(theAction < eSpecialKey_Num);
	return sSpecialKeys[theAction];
}


const Command& keyBindArrayCommand(u16 theArrayID, u16 theIndex)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays[theArrayID].size());
	return sKeyBindArrays[theArrayID][theIndex].cmd;
}


u16 keyBindArraySignalID(u16 theArrayID)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	return sKeyBindArrays[theArrayID].signalID;
}


u16 offsetKeyBindArrayIndex(
	u16 theArrayID, u16 theIndex, s16 theOffset, bool wrap)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays[theArrayID].size());
	KeyBindArray& aKeyBindArray = sKeyBindArrays[theArrayID];
	// The last command in the array should never be empty
	DBG_ASSERT(aKeyBindArray.back().cmd.type != eCmdType_Empty);
	while(aKeyBindArray[theIndex].cmd.type == eCmdType_Empty)
		++theIndex;
	while(theOffset < 0 && (wrap || theIndex > 0) )
	{
		if( theIndex-- == 0 )
			theIndex = wrap ? u16(aKeyBindArray.size()-1) : 0;
		if( aKeyBindArray[theIndex].cmd.type != eCmdType_Empty )
			++theOffset;
	}
	while(theOffset > 0)
	{
		if( theIndex++ >= aKeyBindArray.size()-1 )
			theIndex = wrap ? 0 : u16(aKeyBindArray.size()-1);
		if( aKeyBindArray[theIndex].cmd.type != eCmdType_Empty )
			--theOffset;
	}
	while(aKeyBindArray[theIndex].cmd.type == eCmdType_Empty)
		++theIndex;
	return theIndex;
}


const Command* commandsForButton(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);

	ButtonActionsMap::const_iterator itr =
		sLayers.vals()[theLayerID].buttonMap.find(theButton);
	if( itr != sLayers.vals()[theLayerID].buttonMap.end() )
		return &itr->second.cmd[0];

	return null;
}


const VectorMap<u16, Command>& signalCommandsForLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].signalCommands;
}


u32 commandHoldTime(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);

	std::pair<u16, EButton> aKey(theLayerID, theButton);
	VectorMap<std::pair<u16, EButton>, u32>::const_iterator itr =
		sButtonHoldTimes.find(aKey);
	if( itr != sButtonHoldTimes.end() )
		return itr->second;

	return sDefaultButtonHoldTime;
}


u16 parentLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].parentLayer;
}


s8 layerPriority(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].priority;
}


EMouseMode mouseMode(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].mouseMode;
}


const BitVector<>& hudElementsToShow(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].showHUD;
}


const BitVector<>& hudElementsToHide(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].hideHUD;
}


const BitVector<>& hotspotArraysToEnable(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].enableHotspots;
}


const BitVector<>& hotspotArraysToDisable(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].disableHotspots;
}


u16 comboLayerID(u16 theLayerID1, u16 theLayerID2)
{
	std::pair<u16, u16> aKey(theLayerID1, theLayerID2);
	VectorMap<std::pair<u16, u16>, u16>::iterator itr =
		sComboLayers.find(aKey);
	if( itr != sComboLayers.end() )
		return itr->second;
	return 0;
}


Command commandForMenuItem(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus.vals()[theMenuID].items.size());
	return sMenus.vals()[theMenuID].items[theMenuItemIdx].cmd;
}


Command commandForMenuDir(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus.vals()[theMenuID].dirItems[theDir].cmd;
}


Command menuAutoCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].autoCommand;
}


Command menuBackCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].backCommand;
}


EHUDType menuStyle(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(sMenus.vals()[theMenuID].hudElementID < sHUDElements.size());
	return sHUDElements.vals()[sMenus.vals()[theMenuID].hudElementID].type;
}


u16 rootMenuOfMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].rootMenuID;
}


u16 menuHotspotArray(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	const u16 aHUDElementID = hudElementForMenu(theMenuID);
	DBG_ASSERT(
		sHUDElements.vals()[aHUDElementID].type == eMenuStyle_Hotspots);
	const u16 anArrayID =
		sHUDElements.vals()[aHUDElementID].hotspotArrayID;
	DBG_ASSERT(anArrayID < sHotspotArrays.size());
	return anArrayID;
}


std::string menuSectionName(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	std::string result = sMenus.vals()[theMenuID].path;
	while(sMenus.vals()[theMenuID].parentMenuID != theMenuID)
	{
		theMenuID = sMenus.vals()[theMenuID].parentMenuID;
		result = sMenus.vals()[theMenuID].path + "." + result;
	}

	return kMenuPrefix + result;
}


std::string menuItemKeyName(u16 theMenuItemIdx)
{
	return toString(theMenuItemIdx+1);
}


std::string menuItemDirKeyName(ECommandDir theDir)
{
	DBG_ASSERT(theDir < eCmdDir_Num);
	return k4DirMenuItemLabel[theDir];
}


void menuItemStringToSubMenuName(std::string& theString)
{
	std::string aLabel = breakOffMenuItemLabel(theString);
	// Can't be a sub-menu if has a separate label, is empty, or
	// starts with ':' indicating a normal command
	if( !aLabel.empty() || theString.empty() || theString[0] == ':' )
		theString.clear();
}


const Hotspot& getHotspot(u16 theHotspotID)
{
	DBG_ASSERT(theHotspotID < sHotspots.size());
	return sHotspots[theHotspotID];
}


u16 firstHotspotInArray(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	return sHotspotArrays[theHotspotArrayID].first;
}


u16 lastHotspotInArray(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	return sHotspotArrays[theHotspotArrayID].last;
}


const Hotspot* keyBindArrayHotspot(u16 theArrayID, u16 theIndex)
{
	Hotspot* result = null;
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	DBG_ASSERT(theIndex < sKeyBindArrays[theArrayID].size());
	theIndex = offsetKeyBindArrayIndex(theArrayID, theIndex, 0, false);
	const u16 aHotspotID = sKeyBindArrays[theArrayID][theIndex].hotspotID;
	DBG_ASSERT(aHotspotID < sHotspots.size());
	if( aHotspotID > 0 )
		result = &sHotspots[aHotspotID];
	return result;
}


void modifyHotspot(u16 theHotspotID, const Hotspot& theNewValues)
{
	DBG_ASSERT(theHotspotID < sHotspots.size());
	sHotspots[theHotspotID] = theNewValues;
}


void reloadHotspotKey(
	const std::string& theHotspotName,
	StringToValueMap<u16>& theHotspotNameMapCache,
	StringToValueMap<u16>& theHotspotArrayNameMapCache)
{
	// TODO - remove with new system
}


void reloadAllHotspots()
{
	// TODO - remove with new system
}


EHUDType hudElementType(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.vals()[theHUDElementID].type;
}


bool hudElementIsAMenu(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return
		sHUDElements.vals()[theHUDElementID].type >= eMenuStyle_Begin &&
		sHUDElements.vals()[theHUDElementID].type < eMenuStyle_End;
}


u16 menuForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.vals()[theHUDElementID].menuID;
}


u16 hudElementForMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus.vals()[theMenuID].hudElementID;
}


u16 hotspotForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	DBG_ASSERT(
		sHUDElements.vals()[theHUDElementID].type == eHUDType_Hotspot);
	return sHUDElements.vals()[theHUDElementID].hotspotID;
}


u16 keyBindArrayForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	DBG_ASSERT(
		sHUDElements.vals()[theHUDElementID].type == eHUDType_KBArrayLast ||
		sHUDElements.vals()[theHUDElementID].type == eHUDType_KBArrayDefault);
	return sHUDElements.vals()[theHUDElementID].keyBindArrayID;
}


const std::string& hudElementKeyName(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.keys()[theHUDElementID];
}


u16 keyBindArrayCount()
{
	return u16(sKeyBindArrays.size());
}


u16 keyBindArraySize(u16 theArrayID)
{
	DBG_ASSERT(theArrayID < sKeyBindArrays.size());
	return u16(sKeyBindArrays[theArrayID].size());
}


u16 controlsLayerCount()
{
	return u16(sLayers.size());
}


u16 hudElementCount()
{
	return u16(sHUDElements.size());
}


u16 menuCount()
{
	return u16(sMenus.size());
}


u16 menuItemCount(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return u16(sMenus.vals()[theMenuID].items.size());
}


u16 hotspotCount()
{
	return u16(sHotspots.size());
}


u16 hotspotArrayCount()
{
	return u16(sHotspotArrays.size());
}


const std::string& layerLabel(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers.vals()[theLayerID].label;
}


const std::string& hotspotArrayLabel(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	return sHotspotArrays[theHotspotArrayID].label;
}


const std::string& menuLabel(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	if( sMenus.vals()[theMenuID].rootMenuID == theMenuID )
		return hudElementLabel(hudElementForMenu(theMenuID));
	return sMenus.vals()[theMenuID].label;
}


const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus.vals()[theMenuID].items.size());
	return sMenus.vals()[theMenuID].items[theMenuItemIdx].label;
}


const std::string& menuItemAltLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus.vals()[theMenuID].items.size());
	return sMenus.vals()[theMenuID].items[theMenuItemIdx].altLabel;
}


const std::string& menuDirLabel(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus.vals()[theMenuID].dirItems[theDir].label;
}


const std::string& hudElementLabel(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements.vals()[theHUDElementID].label;
}

#undef mapDebugPrint
#undef INPUT_MAP_DEBUG_PRINT

} // InputMap
