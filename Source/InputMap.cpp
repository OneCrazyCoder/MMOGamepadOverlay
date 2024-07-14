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
kComboParentLayer = 0xFFFF,
};

const char* kMainLayerLabel = "Scheme";
const char* kLayerPrefix = "Layer.";
const char kComboLayerDeliminator = '+';
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
const char* kTypeKeys[] = { "Type", "Style" };
const char* kDisplayNameKeys[] = { "Label", "Title", "Name", "String" };
const char* kKeybindsPrefix = "KeyBinds/";
const char* kHotspotsPrefix = "Hotspots/";
const char* kButtonRebindsPrefix = "Gamepad";
const char* k4DirMenuItemLabel[] = { "L", "R", "U", "D" }; // match ECommandDir!
DBG_CTASSERT(ARRAYSIZE(k4DirMenuItemLabel) == eCmdDir_Num);
const char* kHotspotsKeys[] =
	{ "Hotspots", "Hotspot", "KeyBindArray", "Array", "KeyBinds" };

// These need to be in all upper case
const char* kHUDSettingsKey = "HUD";
const char* kHotspotArraysKey = "HOTSPOTS";
const char* kMouseModeKey = "MOUSE";
const char* kParentLayerKey = "PARENT";
const char* kPriorityKey = "PRIORITY";
const char* kMenuOpenKey = "AUTO";
const char* kMenuCloseKey = "BACK";
const std::string kActionOnlyPrefix = "Just";
const std::string kSignalCommandPrefix = "When";
const std::string k4DirButtons[] =
{	"LS", "LSTICK", "LEFTSTICK", "LEFT STICK", "DPAD",
	"RS", "RSTICK", "RIGHTSTICK", "RIGHT STICK", "FPAD" };
const char* k4DirKeyNames[] = { "LEFT", "RIGHT", "UP", "DOWN" };
const char* k4DirCmdSuffix[] = { " Left", " Right", " Up", " Down" };

const char* kSpecialHotspotNames[] =
{
	"",						// eSpecialHotspot_None
	"MOUSELOOKSTART",		// eSpecialHotspot_MouseLookStart
	"MOUSEHIDDEN",			// eSpecialHotspot_MouseHidden
	"~",					// eSpecialHotspot_LastCursorPos
	"~~",					// eSpecialHotspot_MenuItemPos
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
	std::string keyName;
	std::string displayName;
	EHUDType type : 16;
	u16 menuID; 
	union { u16 hotspotArrayID; u16 keyBindArrayID; u16 hotspotID; };
	// Visual details will be parsed by HUD module

	HUDElement() :
		menuID(kInvalidID),
		hotspotArrayID(kInvalidID)
	{ type = eHUDItemType_Rect; }
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

	ControlsLayer() :
		showHUD(),
		hideHUD(),
		enableHotspots(),
		disableHotspots(),
		priority(),
		mouseMode(eMouseMode_Default),
		parentLayer()
	{}
};

struct HotspotArray
{
	std::string label;
	u16 first, last;
};

// Data used during parsing/building the map but deleted once done
struct InputMapBuilder
{
	std::vector<std::string> parsedString;
	Profile::KeyValuePairs keyValueList;
	VectorMap<ECommandKeyWord, size_t> keyWordMap;
	StringToValueMap<Command> commandAliases;
	StringToValueMap<Command> specialKeyNameToCommandMap;
	StringToValueMap<u16> keyBindArrayNameToIdxMap;
	StringToValueMap<u16> hotspotNameToIdxMap;
	StringToValueMap<u16> hotspotArrayNameToIdxMap;
	StringToValueMap<u16> layerNameToIdxMap;
	StringToValueMap<u16> comboLayerNameToIdxMap;
	StringToValueMap<u16> hudNameToIdxMap;
	StringToValueMap<u16> menuPathToIdxMap;
	BitVector<> elementsProcessed;
	std::string debugItemName;
	std::string debugSubItemName;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<Hotspot> sHotspots;
static std::vector<HotspotArray> sHotspotArrays;
static std::vector<std::string> sKeyStrings;
static std::vector<KeyBindArray> sKeyBindArrays;
static std::vector<ControlsLayer> sLayers;
static VectorMap<std::pair<u16, u16>, u16> sComboLayers;
static std::vector<Menu> sMenus;
static std::vector<HUDElement> sHUDElements;
static u16 sSpecialKeys[eSpecialKey_Num];
static EButton sButtonRemap[eBtn_Num];
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

static EResult checkForComboKeyName(
	std::string theKeyName,
	std::string& out)
{
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
			 aModKey == VK_CANCEL) )
		{// Found a valid modifier key
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(theKeyName))
			{// We have a valid key combo!
				out.push_back(aModKey);
				out.push_back(aMainKey);
				return eResult_Ok;
			}
			// Perhaps remainder is another mod+key, like ShiftCtrlA?
			std::string suffix;
			if( checkForComboKeyName(theKeyName, suffix) == eResult_Ok )
			{
				out.push_back(aModKey);
				out.append(suffix);
				return eResult_Ok;
			}
		}
	}

	// No valid modifier key found
	return eResult_NotFound;
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
			 out[out.size()-1] == VK_CANCEL))
		{
			suffix.insert(suffix.begin(), out[out.size()-1]);
			out.erase(out.size()-1);
		}
		out.push_back(VK_SELECT);
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


static std::string namesToVKeySequence(
	InputMapBuilder& theBuilder,
	const std::vector<std::string>& theNames)
{
	std::string aVKeySeq;

	if( theNames.empty() )
		return aVKeySeq;

	bool expectingWaitTime = false;
	bool expectingJumpPos = false;
	for(int aNameIdx = 0; aNameIdx < theNames.size(); ++aNameIdx)
	{
		const std::string& aName = upper(theNames[aNameIdx]);
		DBG_ASSERT(!aName.empty());
		if( expectingWaitTime )
		{
			if( checkForVKeySeqPause(aName, aVKeySeq, true) != eResult_Ok )
			{// Didn't get wait time as expected - abort!
				aVKeySeq.clear();
				return aVKeySeq;
			}
			expectingWaitTime = false;
			continue;
		}
		else if( expectingJumpPos )
		{
			const EResult aResult = checkForVKeyHotspotPos(
				theBuilder, aName, aVKeySeq, false);
			if( aResult == eResult_Incomplete )
				continue;
			if( aResult == eResult_Ok )
			{
				expectingJumpPos = false;
				continue;
			}
			// Didn't get jump pos as expected - abort!
			aVKeySeq.clear();
			return aVKeySeq;
		}
		if( Command* aCommandAlias = theBuilder.commandAliases.find(aName) )
		{
			if( aCommandAlias->type == eCmdType_SignalOnly )
			{
				aVKeySeq += signalIDToString(aCommandAlias->signalID);
				continue;
			}
			if( aCommandAlias->type == eCmdType_TapKey )
			{
				if( aCommandAlias->signalID )
					aVKeySeq += signalIDToString(aCommandAlias->signalID);
				const u16 aVKey = aCommandAlias->vKey;
				if( aVKey & kVKeyShiftFlag ) aVKeySeq += VK_SHIFT;
				if( aVKey & kVKeyCtrlFlag ) aVKeySeq += VK_CONTROL;
				if( aVKey & kVKeyAltFlag ) aVKeySeq += VK_MENU;
				if( aVKey & kVKeyWinFlag ) aVKeySeq += VK_LWIN;
				if( aVKey & kVKeyMask ) aVKeySeq += u8(aVKey & kVKeyMask);
				continue;
			}
			if( aCommandAlias->type == eCmdType_VKeySequence )
			{
				DBG_ASSERT(aCommandAlias->keyStringIdx < sKeyStrings.size());
				aVKeySeq += sKeyStrings[aCommandAlias->keyStringIdx];
				continue;
			}
			if( aCommandAlias->type == eCmdType_ChatBoxString )
			{
				aVKeySeq += VK_EXECUTE;
				aVKeySeq += sKeyStrings[aCommandAlias->keyStringIdx];
				continue;
			}
		}
		const u8 aVKey = keyNameToVirtualKey(aName);
		if( aVKey == 0 )
		{
			// If previous key was a mouse button, check for follow-up hotspot
			EResult aResult;
			if( !aVKeySeq.empty() &&
				(aVKeySeq[aVKeySeq.size()-1] == VK_LBUTTON ||
				 aVKeySeq[aVKeySeq.size()-1] == VK_MBUTTON ||
				 aVKeySeq[aVKeySeq.size()-1] == VK_RBUTTON)  )
			{
				aResult = checkForVKeyHotspotPos(
					theBuilder, aName, aVKeySeq, true);
				if( aResult != eResult_NotFound )
					continue;
			}

			// Check if it's a pause/delay/wait command
			aResult = checkForVKeySeqPause(aName, aVKeySeq);
			// Incomplete result means it WAS a wait, now need the time
			if( aResult == eResult_Incomplete )
				expectingWaitTime = true;
			if( aResult != eResult_NotFound )
				continue;

			// Check if it's a modifier+key in one word like Shift2 or Alt1
			aResult = checkForComboKeyName(aName, aVKeySeq);
			if( aResult != eResult_Ok )
			{
				// Can't figure this word out at all, abort!
				aVKeySeq.clear();
				return aVKeySeq;
			}
		}
		else if( aVKey == VK_SELECT )
		{
			// Get name of hotspot to jump cursor to next
			expectingJumpPos = true;
			aVKeySeq.push_back(aVKey);
		}
		else
		{
			aVKeySeq.push_back(aVKey);
		}
	}

	return aVKeySeq;
}


static u16 vKeySeqToSingleKey(const u8* theVKeySeq)
{
	u16 result = 0;
	if( theVKeySeq == null || theVKeySeq[0] == '\0' )
		return result;

	for(const u8* aVKeyPtr = theVKeySeq; *aVKeyPtr != '\0'; ++aVKeyPtr)
	{
		// If encounter anything else after the first non-mod key,
		// it must be a sequence of keys rather than of a "single key",
		// and thus can not be used with _TapKey or _PressAndHoldKey.
		if( result & kVKeyMask )
		{
			result = 0;
			return result;
		}

		switch(*aVKeyPtr)
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
		case VK_SELECT:
		case VK_CANCEL:
			// Can't use these special-case "keys" with single-key commands
			result = 0;
			return result;
		case kVKeyFireSignal:
			// Skip over the signal chars
			aVKeyPtr += 2;
			break;
		default:
			result |= *aVKeyPtr;
			break;
		}
	}

	// If purely mod keys, add dummy base key
	if( result && !(result & kVKeyMask) )
		result |= kVKeyModKeyOnlyBase;

	return result;
}


static u16 getOrCreateLayerID(
	InputMapBuilder& theBuilder,
	const std::string& theLayerName,
	std::vector<std::string>& theLoopCheckList = std::vector<std::string>())
{
	DBG_ASSERT(!theLayerName.empty());

	// Check if already exists, and if so return the index
	const std::string& aLayerKeyName = upper(theLayerName);
	if( u16* idx = theBuilder.layerNameToIdxMap.find(aLayerKeyName) )
		return *idx;

	// Add new layer to sLayers and the name-to-index map
	theBuilder.layerNameToIdxMap.setValue(aLayerKeyName, u16(sLayers.size()));
	sLayers.push_back(ControlsLayer());
	sLayers.back().label = theLayerName;

	return u16(sLayers.size() - 1);
}


static u16 getOrCreateComboLayerID(
	InputMapBuilder& theBuilder,
	const std::string& theComboName)
{
	if( theComboName.empty() )
		return 0;

	std::string aRemainingName = theComboName;
	std::string aLayerName = breakOffItemBeforeChar(
		aRemainingName, kComboLayerDeliminator);
	if( aLayerName.empty() )
		swap(aLayerName, aRemainingName);
	u16* aLayerID = theBuilder.layerNameToIdxMap.find(upper(aLayerName));
	if( !aLayerID || *aLayerID == 0 )
		return 0;
	if( aRemainingName.empty() )
		return *aLayerID;
	std::pair<u16, u16> aComboLayerKey;
	aComboLayerKey.first = *aLayerID;
	aComboLayerKey.second = getOrCreateComboLayerID(
		theBuilder, aRemainingName);
	if( aComboLayerKey.second == 0 )
		return 0;
	if( aComboLayerKey.first == aComboLayerKey.second )
	{
		logError("Specified same layer twice in combo layer name '%s+%s'!",
			sLayers[aComboLayerKey.first].label.c_str(),
			sLayers[aComboLayerKey.second].label.c_str());
		return 0;
	}
	VectorMap<std::pair<u16, u16>, u16>::iterator itr =
		sComboLayers.find(aComboLayerKey);
	if( itr != sComboLayers.end() )
	{
		theBuilder.comboLayerNameToIdxMap.setValue(
			theComboName, itr->second);
		return itr->second;
	}

	aLayerName =
		sLayers[aComboLayerKey.first].label +
		kComboLayerDeliminator +
		sLayers[aComboLayerKey.second].label;
	u16 aComboLayerID = getOrCreateLayerID(theBuilder, aLayerName);
	sLayers[aComboLayerID].parentLayer = kComboParentLayer;
	sComboLayers.setValue(aComboLayerKey, aComboLayerID);
	return aComboLayerID;
}


static u16 getOrCreateHUDElementID(
	InputMapBuilder& theBuilder,
	const std::string& theName,
	bool hasInputAssigned)
{
	DBG_ASSERT(!theName.empty());

	// Check if already exists, and if so return the ID
	u16 aHUDElementID = theBuilder.hudNameToIdxMap.findOrAdd(
		upper(theName), u16(sHUDElements.size()));
	if( aHUDElementID < sHUDElements.size() )
		return aHUDElementID;

	// Add new HUD element
	sHUDElements.push_back(HUDElement());
	HUDElement& aHUDElement = sHUDElements.back();
	aHUDElement.keyName = theName;

	const std::string& aMenuPath = std::string(kMenuPrefix) + theName;
	const std::string& aHUDPath = std::string(kHUDPrefix) + theName;

	// Try to figure out what type of HUD element / Menu this is
	std::string aHUDTypeName;
	for(int i = 0; aHUDTypeName.empty() && i < ARRAYSIZE(kTypeKeys); ++i)
		aHUDTypeName = Profile::getStr(aMenuPath + "/" + kTypeKeys[i]);
	for(int i = 0; aHUDTypeName.empty() && i < ARRAYSIZE(kTypeKeys); ++i)
		aHUDTypeName = Profile::getStr(aHUDPath + "/" + kTypeKeys[i]);
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
		for(int i = 0; aHUDElement.displayName.empty() &&
			i < ARRAYSIZE(kDisplayNameKeys); ++i)
		{
			aHUDElement.displayName = Profile::getStr(
				aHUDPath + "/" + kDisplayNameKeys[i]);
		}
		for(int i = 0; aHUDElement.displayName.empty() &&
			i < ARRAYSIZE(kDisplayNameKeys); ++i)
		{
			aHUDElement.displayName = Profile::getStr(
				aMenuPath + "/" + kDisplayNameKeys[i]);
		}
	}

	if( aHUDElement.type >= eMenuStyle_Begin &&
		aHUDElement.type < eMenuStyle_End )
	{
		// Add new Root Menu to sMenus and link the Menu and HUDElement
		u16 aMenuID = theBuilder.menuPathToIdxMap.findOrAdd(
			upper(aMenuPath), u16(sMenus.size()));
		if( aMenuID == sMenus.size() )
		{
			sMenus.push_back(Menu());
			Menu& aMenu = sMenus.back();
			aMenu.label = theName;
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
				aLinkedName = Profile::getStr(aMenuPath + "/" + kHotspotsKeys[i]);
		}
		else
		{
			for(; aLinkedName.empty() && i < ARRAYSIZE(kHotspotsKeys); ++i)
				aLinkedName = Profile::getStr(aHUDPath + "/" + kHotspotsKeys[i]);
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
	HUDElement& aHUDElement = sHUDElements[aHUDElementID];

	DBG_ASSERT(aHUDElement.type >= eMenuStyle_Begin);
	DBG_ASSERT(aHUDElement.type < eMenuStyle_End);
	DBG_ASSERT(aHUDElement.menuID < sMenus.size());
	return aHUDElement.menuID;
}


static std::string menuPathOf(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	std::string result = sMenus[theMenuID].label;
	while(sMenus[theMenuID].parentMenuID != theMenuID)
	{
		theMenuID = sMenus[theMenuID].parentMenuID;
		result = sMenus[theMenuID].label + "." + result;
	}
	result = kMenuPrefix + result;
	return result;
}


static u16 getOrCreateMenuID(
	InputMapBuilder& theBuilder,
	std::string theMenuName,
	u16 theParentMenuID)
{
	DBG_ASSERT(!theMenuName.empty());
	DBG_ASSERT(theParentMenuID < sMenus.size());

	std::string aParentPath = menuPathOf(theParentMenuID);
	std::string aFullPath;
	if( theMenuName[0] == '.' )
	{// Starting with '.' signals want to treat "grandparent" as the parent
		theParentMenuID = sMenus[theParentMenuID].parentMenuID;
		aParentPath = menuPathOf(theParentMenuID);
		// Name being ".." means treat this as direct alias to grandparent menu
		if( theMenuName == ".." )
			return theParentMenuID;
		// Remove leading '.'
		theMenuName = theMenuName.substr(1);
	}
	aFullPath = aParentPath + "." + theMenuName;

	u16 aMenuID = theBuilder.menuPathToIdxMap.findOrAdd(
		upper(aFullPath), u16(sMenus.size()));
	if( aMenuID < sMenus.size() )
		return aMenuID;

	sMenus.push_back(Menu());
	Menu& aMenu = sMenus.back();
	aMenu.label = theMenuName;
	aMenu.parentMenuID = theParentMenuID;
	aMenu.rootMenuID = sMenus[theParentMenuID].rootMenuID;
	aMenu.hudElementID = sMenus[theParentMenuID].hudElementID;
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
			result.count = max(result.count, intFromString(theWords[i]));
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

	// "= [Update] UIScale"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Change);
	allowedKeyWords.set(eCmdWord_Edit);
	allowedKeyWords.set(eCmdWord_UIScale);
	if( keyWordsFound.test(eCmdWord_UIScale) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_UpdateUIScale;
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
	allowedKeyWords.reset();

	if( aLayerName )
	{
		// "= Replace [Layer] <aLayerName> with <aSecondLayerName>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Layer);
		allowedKeyWords.set(eCmdWord_Replace);
		if( keyWordsFound.test(eCmdWord_Replace) &&
			aSecondLayerName && aSecondLayerName != aLayerName &&
			(keyWordsFound & ~allowedKeyWords).count() <= 2 )
		{
			result.type = eCmdType_ReplaceControlsLayer;
			result.layerID =
				getOrCreateLayerID(theBuilder, *aLayerName);
			result.replacementLayer =
				getOrCreateLayerID(theBuilder, *aSecondLayerName);
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
			result.layerID = getOrCreateLayerID(theBuilder, *aLayerName);
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
			result.layerID = getOrCreateLayerID(theBuilder, *aLayerName);
			DBG_ASSERT(result.layerID != 0);
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
			result.replacementLayer =
				getOrCreateLayerID(theBuilder, *aLayerName);
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
			result.layerID = getOrCreateLayerID(theBuilder, *aLayerName);
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
			result.layerID = getOrCreateLayerID(theBuilder, *aLayerName);
			DBG_ASSERT(result.layerID != 0);
			return result;
		}
		//allowedKeyWords.reset(eCmdWord_Remove);
	}

	// Same deal as aLayerName for the Menu-related commands needing a name
	// of the menu in question as the one otherwise-unrelated word.
	const std::string* aMenuName = anIgnoredWord;
	if( allowButtonActions )
	{
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
	}

	if( allowButtonActions && aMenuName )
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

		// "= Reset <aMenuName> [Menu] [to Default] [with mouse click]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Menu);
		allowedKeyWords.set(eCmdWord_Default);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_Click);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuReset;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Reset);
		allowedKeyWords.reset(eCmdWord_Default);

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
		// "= [Select] [Mouse] Hotspot <aCmdDir> [No/Wrap] [#]"
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
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Hotspot);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			if( keyWordsFound.test(eCmdWord_Hotspot) )
			{
				result.type = eCmdType_HotspotSelect;
				result.mouseWheelMotionType = eMouseWheelMotion_Stepped;
				result.withMouse = true;
				return result;
			}
			else if( !keyWordsFound.test(eCmdWord_Select) )
			{
				result.type = eCmdType_MouseWheel;
				result.mouseWheelMotionType = eMouseWheelMotion_Stepped;
				return result;
			}
		}
		allowedKeyWords.reset(eCmdWord_Stepped);

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' Smooth <aCmdDir>"
		// allowedKeyWords = Move & Mouse & Wheel & Select & Hotspot
		allowedKeyWords.set(eCmdWord_Smooth);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			keyWordsFound.test(eCmdWord_Smooth) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			if( keyWordsFound.test(eCmdWord_Hotspot) )
			{
				result.type = eCmdType_HotspotSelect;
				result.mouseWheelMotionType = eMouseWheelMotion_Smooth;
				result.withMouse = true;
				return result;
			}
			else if( !keyWordsFound.test(eCmdWord_Select) )
			{
				result.type = eCmdType_MouseWheel;
				result.mouseWheelMotionType = eMouseWheelMotion_Smooth;
				return result;
			}
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

	// Check for a slash command or say string, which stores the string
	// as ASCII text and outputs it by typing it into the chat box
	if( theString[0] == '/' || theString[0] == '>' )
	{
		sKeyStrings.push_back(theString + "\r");
		result.type = eCmdType_ChatBoxString;
		result.keyStringIdx = u16(sKeyStrings.size()-1);
		return result;
	}

	// Check for special command
	theBuilder.parsedString.clear();
	sanitizeSentence(theString, theBuilder.parsedString);
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
					// Comment on _PressAndHoldKey further down explains this
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

	// Check for Virtual-Key Code sequence or single key tap
	if( result.type == eCmdType_Empty )
	{
		// .parsedString was already generated for commands check above
		const std::string& aVKeySeq =
			namesToVKeySequence(theBuilder, theBuilder.parsedString);

		if( !aVKeySeq.empty() )
		{
			if( u16 aVKey = vKeySeqToSingleKey((const u8*)aVKeySeq.c_str()) )
			{
				result.type = eCmdType_TapKey;
				result.vKey = aVKey;
			}
			else
			{
				result.type = eCmdType_VKeySequence;
				result.keyStringIdx = u16(sKeyStrings.size());
				sKeyStrings.push_back(aVKeySeq);
			}
		}
	}

	// _PressAndHoldKey (and indirectly its associated _ReleaseKey)
	// only works properly with a single key (+ mods), just like _TapKey,
	// and only when allowHoldActions is true (_Down button action),
	// so in that case change _TapKey to _PressAndHoldKey instead.
	// This does mean there isn't currently a way to keep this button
	// action assigned to only _TapKey, and that a _Down button action
	// will just act the same as _Press for any other commmands
	// (which can be used intentionally to have 2 commands for button
	// initial press, thus can be useful even if a bit unintuitive).
	if( result.type == eCmdType_TapKey && allowHoldActions )
		result.type = eCmdType_PressAndHoldKey;

	return result;
}


static void buildHotspots(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning hotspots...\n");
	sHotspots.resize(eSpecialHotspot_Num);
	// sHotspots[0] is reserved as eSpecialHotspot_None
	// The hotspotNameToIdxMap maps to this for "filler" words between
	// jump/point/click and the actual hotspot name.
	theBuilder.hotspotNameToIdxMap.setValue("MOUSE", 0);
	theBuilder.hotspotNameToIdxMap.setValue("CURSOR", 0);
	theBuilder.hotspotNameToIdxMap.setValue("TO", 0);
	theBuilder.hotspotNameToIdxMap.setValue("AT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("ON", 0);
	theBuilder.hotspotNameToIdxMap.setValue("HOTSPOT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("HOT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("SPOT", 0);

	for(u16 i = 0; i < eSpecialHotspot_Num; ++i)
		theBuilder.hotspotNameToIdxMap.setValue(kSpecialHotspotNames[i], i);

	// Parse normal hotspots and pick out any that might be arrays
	DBG_ASSERT(theBuilder.keyValueList.empty());
	Profile::getAllKeys(kHotspotsPrefix, theBuilder.keyValueList);
	for(size_t i = 0; i < theBuilder.keyValueList.size(); ++i)
	{
		std::string aHotspotName = condense(theBuilder.keyValueList[i].first);
		std::string aHotspotDescription = theBuilder.keyValueList[i].second;

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
					anEntry.label = theBuilder.keyValueList[i].first;
					anEntry.label.resize(
						posAfterPrefix(anEntry.label, aHotspotArrayName));
					anEntry.first = anEntry.last = 0;
					sHotspotArrays.push_back(anEntry);
				}
				sHotspotArrays[aHotspotArrayIdx].last = max(
					sHotspotArrays[aHotspotArrayIdx].last,
					anArrayIdx);
				continue;
			}
			// Doesn't seem to be part of a valid array, so restore name
			aHotspotName = condense(theBuilder.keyValueList[i].first);
		}

		u16& aHotspotIdx = theBuilder.hotspotNameToIdxMap.findOrAdd(
			aHotspotName, u16(sHotspots.size()));
		if( aHotspotIdx >= sHotspots.size() )
			sHotspots.resize(aHotspotIdx+1);
		EResult aResult = HotspotMap::stringToHotspot(
			aHotspotDescription, sHotspots[aHotspotIdx]);
		if( aResult == eResult_Malformed )
		{
			logError("Hotspot %s: Could not decipher hotspot position '%s'",
				theBuilder.keyValueList[i].first,
				theBuilder.keyValueList[i].second);
			sHotspots[aHotspotIdx] = Hotspot();
		}
	}
	theBuilder.keyValueList.clear();

	// Fill in the hotspot arrays
	for(size_t aHSA_ID = 0; aHSA_ID < sHotspotArrays.size(); ++aHSA_ID)
	{
		HotspotArray& aHotspotArray = sHotspotArrays[aHSA_ID];
		mapDebugPrint("Building Hotspot Array %s\n", aHotspotArray.label.c_str());
		Hotspot anAnchor;
		u16* anAnchorIdx = theBuilder.hotspotNameToIdxMap.
			find(condense(aHotspotArray.label));
		if( anAnchorIdx )
			anAnchor = sHotspots[*anAnchorIdx];
		// Allocate enough hotspots for the array
		const u16 aHotspotArrayCount = aHotspotArray.last;
		aHotspotArray.first = u16(sHotspots.size());
		sHotspots.resize(aHotspotArray.first + aHotspotArrayCount);
		aHotspotArray.last = u16(sHotspots.size()-1);
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
				std::string(kHotspotsPrefix) + aHotspotName);
			if( !aHotspotValue.empty() )
			{
				std::string aHotspotDesc = aHotspotValue;
				EResult aResult = HotspotMap::stringToHotspot(
					aHotspotDesc, sHotspots[aHotspotID]);
				if( aResult == eResult_Malformed )
				{
					logError(
						"Hotspot %s: Could not decipher hotspot position '%s'",
						aHotspotName.c_str(),
						aHotspotValue.c_str());
				}
				if( sHotspots[aHotspotID].x.anchor == 0 &&
					sHotspots[aHotspotID].y.anchor == 0 )
				{// Offset by anchor hotspot
					sHotspots[aHotspotID].x.anchor += anAnchor.x.anchor;
					sHotspots[aHotspotID].y.anchor += anAnchor.y.anchor;
					sHotspots[aHotspotID].x.offset += anAnchor.x.offset;
					sHotspots[aHotspotID].y.offset += anAnchor.y.offset;
					sHotspots[aHotspotID].x.scaled += anAnchor.x.scaled;
					sHotspots[aHotspotID].y.scaled += anAnchor.y.scaled;
				}
				++aNextArrayIdx;
			}
			else
			{
				// Attempt to find as part of a range of hotspots
				Profile::getAllKeys(
					std::string(kHotspotsPrefix)+aHotspotName+'-',
					theBuilder.keyValueList);
				if( theBuilder.keyValueList.empty() )
				{
					logError(
						"Hotspot Array %s missing hotspot entry #%d",
						aHotspotArray.label.c_str(),
						aNextArrayIdx);
					++aNextArrayIdx;
				}
				else if( theBuilder.keyValueList.size() > 1 )
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
					std::string aHotspotDesc = theBuilder.keyValueList[0].second;
					Hotspot aDeltaHotspot;
					EResult aResult = HotspotMap::stringToHotspot(
						aHotspotDesc, aDeltaHotspot);
					if( aResult == eResult_Malformed )
					{
						logError(
							"Hotspot %s: Could not decipher offsets '%s'",
							aHotspotName.c_str(),
							theBuilder.keyValueList[0].second);
						aDeltaHotspot = Hotspot();
					}
					int aLastIdx =
						intFromString(theBuilder.keyValueList[0].first);
					for(; aNextArrayIdx <= aLastIdx; ++aNextArrayIdx )
					{
						aHotspotID =
							aHotspotArray.first + aNextArrayIdx - 1;
						aHotspotName =
							aHotspotArray.label + toString(aNextArrayIdx);
						theBuilder.hotspotNameToIdxMap.setValue(
							condense(aHotspotName), aHotspotID);
						Hotspot& aPrevEntry = sHotspots[aHotspotID-1];
						sHotspots[aHotspotID].x.anchor = aPrevEntry.x.anchor;
						sHotspots[aHotspotID].y.anchor = aPrevEntry.y.anchor;
						sHotspots[aHotspotID].x.offset =
							aPrevEntry.x.offset + aDeltaHotspot.x.offset;
						sHotspots[aHotspotID].y.offset =
							aPrevEntry.y.offset + aDeltaHotspot.y.offset;
						sHotspots[aHotspotID].x.scaled =
							aPrevEntry.x.scaled + aDeltaHotspot.x.scaled;
						sHotspots[aHotspotID].y.scaled =
							aPrevEntry.y.scaled + aDeltaHotspot.y.scaled;
					}
				}
				theBuilder.keyValueList.clear();
			}
		}
	}
}


static void buildHotspotArraysForLayer(
	InputMapBuilder& theBuilder,
	u16 theLayerID,
	const std::string& theLayerHotspotsDesc)
{
	DBG_ASSERT(!theLayerHotspotsDesc.empty());

	sLayers[theLayerID].enableHotspots.clearAndResize(sHotspotArrays.size());
	sLayers[theLayerID].disableHotspots.clearAndResize(sHotspotArrays.size());

	// Break the string into individual words
	theBuilder.parsedString.clear();
	sanitizeSentence(theLayerHotspotsDesc, theBuilder.parsedString);

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
			sLayers[theLayerID].enableHotspots.set(*aHotspotArrayID, enable);
			sLayers[theLayerID].disableHotspots.set(*aHotspotArrayID, !enable);
		}
		else
		{
			logError(
				"Could not find Hotspot Array '%s' "
				"referenced by [%s]/Hotspots = %s",
				aName.c_str(),
				theBuilder.debugItemName.c_str(),
				theLayerHotspotsDesc.c_str());
		}
	}
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
			std::string aMacroString = sKeyStrings[theCmd.keyStringIdx];
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

	if( theCmdStr[0] == '/' || theCmdStr[0] == '>' )
	{// Slash command or say string (types into chat box)
		// '>' becomes Enter to start chat box for say strings
		aCmd.type = eCmdType_ChatBoxString;
		aCmd.signalID = theSignalID++;
		aCmd.keyStringIdx = u16(sKeyStrings.size());
		sKeyStrings.push_back(
			signalIDToString(aCmd.signalID) + theCmdStr + "\r");
		return aCmd;
	}

	ECommandKeyWord aKeyWord = commandWordToID(condense(theCmdStr));
	if( aKeyWord == eCmdWord_Nothing )
	{// Specifically requested signal only
		aCmd.type = eCmdType_SignalOnly;
		aCmd.signalID = theSignalID++;
		return aCmd;
	}

	// VKey Sequence or tap key
	theBuilder.parsedString.clear();
	sanitizeSentence(theCmdStr, theBuilder.parsedString);
	const std::string& aVKeySeq =
		namesToVKeySequence(theBuilder, theBuilder.parsedString);
	if( aVKeySeq.empty() )
		return aCmd;

	if( u16 aVKey = vKeySeqToSingleKey((const u8*)aVKeySeq.c_str()) )
	{
		aCmd.type = eCmdType_TapKey;
		aCmd.signalID = theSignalID++;
		aCmd.vKey = aVKey;
		return aCmd;
	}

	aCmd.type = eCmdType_VKeySequence;
	aCmd.signalID = theSignalID++;
	aCmd.keyStringIdx = u16(sKeyStrings.size());
	sKeyStrings.push_back(
		signalIDToString(aCmd.signalID) + aVKeySeq);
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


static void buildCommandAliases(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning KeyBinds...\n");
	theBuilder.debugItemName = "[";
	theBuilder.debugItemName += kKeybindsPrefix;
	theBuilder.debugItemName[theBuilder.debugItemName.size()-1] = ']';

	// Manually fetch all the special keys first
	u16 aNextSignalID;
	for(u16 i = 0; i < eSpecialKey_Num; ++i)
	{
		const std::string anAlias = kSpecialKeyNames[i];
		std::string aCmdStr = Profile::getStr(kKeybindsPrefix + anAlias);
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
	DBG_ASSERT(theBuilder.keyValueList.empty());
	Profile::getAllKeys(kKeybindsPrefix, theBuilder.keyValueList);
	aNextSignalID = eBtn_Num + eSpecialKey_Num;
	theBuilder.elementsProcessed.clearAndResize(theBuilder.keyValueList.size());
	// Mark the already-processed special keys
	for(size_t i = 0; i < theBuilder.keyValueList.size(); ++i)
	{
		if( theBuilder.commandAliases.contains(
				condense(theBuilder.keyValueList[i].first)) )
		{
			theBuilder.elementsProcessed.set(i);
		}
	}

	bool newKeyBindAdded = false;
	bool showErrors = false;
	while(!theBuilder.elementsProcessed.all())
	{
		for(size_t i = 0; i < theBuilder.keyValueList.size(); ++i)
		{
			if( theBuilder.elementsProcessed.test(i) )
				continue;

			const std::string& anAlias = theBuilder.keyValueList[i].first;
			const std::string& aCmdStr = theBuilder.keyValueList[i].second;
			
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
	theBuilder.keyValueList.clear();

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
		if( kButtonActionPrefx[i][0] == '\0' )
			continue;
		const char* aPrefixChar = &kButtonActionPrefx[i][0];
		const char* aStrChar = theButtonActionName.c_str();
		bool matchFound = true;
		for(; *aPrefixChar; ++aPrefixChar, ++aStrChar)
		{
			// Ignore whitespace in the string
			while(*aStrChar <= ' ') ++aStrChar;
			// Mismatch if reach end of string or chars don't match
			if( !*aStrChar || ::toupper(*aPrefixChar) != ::toupper(*aStrChar) )
			{
				matchFound = false;
				break;
			}
		}
		if( matchFound )
		{
			result = EButtonAction(i);
			// Chop the prefix (and any whitespace after) off the front of the string
			theButtonActionName = trim(aStrChar);
			break;
		}
	}

	return result;
}


static void reportButtonAssignment(
	InputMapBuilder& theBuilder,
	EButtonAction theBtnAct,
	EButton theBtnID,
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
	EButton aBtnID = buttonNameToID(aBtnKeyName);

	bool isA4DirMultiAssign = false;
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
			aBtnID = buttonNameToID(aBtnKeyName + k4DirKeyNames[i]);
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
			// If not, use the base command
			if( aCmd.type == eCmdType_Empty )
			{
				aCmdStr = theCmdStr;
				aCmd = aBaseCmd;
				dirCommandFailed = true;
			}
			// Get destination of command. Note that we do this AFTER
			// parsing the command because stringToCommand() can lead to
			// resizing sLayers and thus make this reference invalid!
			ButtonActions& aDestBtn =
				sLayers[theLayerIdx].buttonMap.findOrAdd(aBtnID);
			if( !onlySpecificAction ) aDestBtn.initIfEmpty();
			Command& aDestCmd = aDestBtn.cmd[aBtnAct];
			// Direct assignment should take priority over multi-assignment,
			// so if this was already assigned directly then leave it alone.
			if( aDestCmd.type > eCmdType_Unassigned )
				continue;
			// Make and report assignment
			aDestCmd = aCmd;
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
			swap(theBuilder.debugSubItemName, anExtPropName);
			reportButtonAssignment(
				theBuilder, aBtnAct, aBtnID, aDestCmd, aCmdStr,
				onlySpecificAction);
			swap(theBuilder.debugSubItemName, anExtPropName);
		}
		return;
	}

	if( aBtnTime >= 0 && aBtnID >= eBtn_Num )
	{// Part of the button's name might have been absorbed into aBtnTime
		std::string aTimeAsString = toString(aBtnTime);
		std::string aBtnTmpName = theBtnName;
		aBtnTmpName.push_back(aTimeAsString[0]);
		aTimeAsString.erase(0, 1);
		aBtnKeyName = condense(aBtnTmpName);
		aBtnID = buttonNameToID(aBtnKeyName);
		if( aBtnID < eBtn_Num )
		{
			aBtnTime = -1;
			if( !aTimeAsString.empty() )
				aBtnTime = intFromString(aTimeAsString);
			theBtnName = aBtnTmpName;
		}
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
		sLayers[theLayerIdx].buttonMap.findOrAdd(aBtnID);
	if( !onlySpecificAction ) aDestBtn.initIfEmpty();
	Command& aDestCmd = aDestBtn.cmd[aBtnAct]; 
	aDestCmd = aCmd;
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
		theBuilder, aBtnAct, aBtnID, aDestCmd, theCmdStr,
		onlySpecificAction);
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
			sLayers[theLayerIdx].signalCommands.setValue(
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
			sLayers[theLayerIdx].signalCommands.setValue(
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
		EButton aBtnID = buttonNameToID(aSignalKey);
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
				aBtnID = buttonNameToID(aSignalKey + k4DirKeyNames[i]);
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
				Command& aDestCmd =
					sLayers[theLayerIdx].signalCommands.findOrAdd(aBtnID);
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
				sLayers[theLayerIdx].signalCommands.setValue(aBtnID, aCmd);

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


static void buildControlsLayer(InputMapBuilder& theBuilder, u16 theLayerIdx)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	const bool isComboLayer =
		sLayers[theLayerIdx].parentLayer == kComboParentLayer;
	sLayers[theLayerIdx].parentLayer = 0;

	// Make local copy of name string since sLayers can reallocate memory here
	const std::string aLayerName = sLayers[theLayerIdx].label;
	theBuilder.debugItemName.clear();
	if( theLayerIdx != 0 )
	{
		theBuilder.debugItemName = kLayerPrefix;
		mapDebugPrint("Building controls layer: %s\n", aLayerName.c_str());
	}
	theBuilder.debugItemName += aLayerName;

	std::string aLayerPrefix;
	if( theLayerIdx == 0 )
		aLayerPrefix = aLayerName+"/";
	else
		aLayerPrefix = std::string(kLayerPrefix)+aLayerName+"/";

	{// Get mouse mode layer setting directly
		const std::string& aMouseModeStr =
			Profile::getStr(aLayerPrefix + kMouseModeKey);
		if( !aMouseModeStr.empty() )
		{
			const EMouseMode aMouseMode =
				mouseModeNameToID(condense(aMouseModeStr));
			if( aMouseMode >= eMouseMode_Num )
			{
				logError("Unknown mode for 'Mouse = %s' in Layer [%s]!",
					aMouseModeStr.c_str(),
					theBuilder.debugItemName.c_str());
			}
			else
			{
				sLayers[theLayerIdx].mouseMode = aMouseMode;
			}
		}
	}
	DBG_ASSERT(sLayers[theLayerIdx].mouseMode < eMouseMode_Num);
	if( sLayers[theLayerIdx].mouseMode != eMouseMode_Default )
	{
		mapDebugPrint("[%s]: Mouse set to '%s' mode\n",
			theBuilder.debugItemName.c_str(),
			sLayers[theLayerIdx].mouseMode == eMouseMode_Cursor ? "Cursor" :
			sLayers[theLayerIdx].mouseMode == eMouseMode_LookTurn ? "RMB Mouse Look" :
			sLayers[theLayerIdx].mouseMode == eMouseMode_LookOnly ? "LMB Mouse Look" :
			sLayers[theLayerIdx].mouseMode == eMouseMode_Hide ? "Hidden" :
			/*otherwise*/ "Hidden OR Mouse Look" );
	}

	{// Get hotspot arrays setting directly
		const std::string& aHotspotsStr =
			Profile::getStr(aLayerPrefix + kHotspotArraysKey);
		if( !aHotspotsStr.empty() )
			buildHotspotArraysForLayer(theBuilder, theLayerIdx, aHotspotsStr);
	}

	{// Get priority setting directly
		sLayers[theLayerIdx].priority = Profile::getInt(
			aLayerPrefix + kPriorityKey);
		if( sLayers[theLayerIdx].priority )
		{
			if( theLayerIdx == 0 )
			{
				logError(
					"Root layer [%s] is always lowest priority. "
					"Priority=%d property ignored!",
					theBuilder.debugItemName.c_str(),
					sLayers[theLayerIdx].priority);
			}
			else if( isComboLayer )
			{
				logError(
					"Combo Layer [%s] ordering is derived automatically "
					"from base layers, so Priority=%d property is ignored!",
					theBuilder.debugItemName.c_str(),
					sLayers[theLayerIdx].priority);
			}
		}
	}

	{// Get parent layer setting directly
		const std::string& aParentLayerName =
			Profile::getStr(aLayerPrefix + kParentLayerKey);
		if( !aParentLayerName.empty() )
		{
			if( isComboLayer )
			{
				logError(
					"\"Parent=%s\" property ignored for Combo Layer [%s]!",
					aParentLayerName.c_str(),
					theBuilder.debugItemName.c_str());
			}
			else if( theLayerIdx == 0 )
			{
				logError(
					"Root layer [%s] can not have a Parent= layer set!",
					theBuilder.debugItemName.c_str(),
					aParentLayerName.c_str());
			}
			else
			{
				sLayers[theLayerIdx].parentLayer = 
					getOrCreateLayerID(theBuilder, aParentLayerName);
				// Check for infinite parent loop
				theBuilder.elementsProcessed.clearAndResize(sLayers.size());
				u16 aCheckLayerIdx = theLayerIdx;
				theBuilder.elementsProcessed.set(aCheckLayerIdx);
				while(sLayers[aCheckLayerIdx].parentLayer != 0)
				{
					aCheckLayerIdx = sLayers[aCheckLayerIdx].parentLayer;
					if( theBuilder.elementsProcessed.test(aCheckLayerIdx) )
					{
						logError("Infinite parent loop with layer [%s]"
							" trying to set parent layer to %s!",
							theBuilder.debugItemName.c_str(),
							aParentLayerName.c_str());
						sLayers[theLayerIdx].parentLayer = 0;
						break;
					}
					theBuilder.elementsProcessed.set(aCheckLayerIdx);
				}
			}
		}
	}
	DBG_ASSERT(sLayers[theLayerIdx].parentLayer < sLayers.size());
	if( sLayers[theLayerIdx].parentLayer != eMouseMode_Default )
	{
		mapDebugPrint("[%s]: Parent layer set to '%s'\n",
			theBuilder.debugItemName.c_str(),
			sLayers[sLayers[theLayerIdx].parentLayer].label.c_str());
	}

	// Check each key-value pair for command assignment requests
	DBG_ASSERT(theBuilder.keyValueList.empty());
	Profile::getAllKeys(aLayerPrefix, theBuilder.keyValueList);
	if( theBuilder.keyValueList.empty() && !isComboLayer )
	{
		logError("No properties found for Layer [%s]!",
			theBuilder.debugItemName.c_str());
	}
	for(Profile::KeyValuePairs::const_iterator itr =
		theBuilder.keyValueList.begin();
		itr != theBuilder.keyValueList.end(); ++itr)
	{
		const std::string& aKey = condense(itr->first);
		if( aKey == kParentLayerKey ||
			aKey == kMouseModeKey ||
			aKey == kHUDSettingsKey ||
			aKey == kHotspotArraysKey ||
			aKey == kPriorityKey )
			continue;

		theBuilder.debugSubItemName = itr->first;
		// Check for a signal command
		if( aKey.compare(0,
				kSignalCommandPrefix.length(),
				condense(kSignalCommandPrefix)) == 0 )
		{
			theBuilder.debugSubItemName = theBuilder.debugSubItemName.substr(
				posAfterPrefix(itr->first, kSignalCommandPrefix));
			addSignalCommand(theBuilder, theLayerIdx,
				&itr->first[posAfterPrefix(itr->first, kSignalCommandPrefix)],
				itr->second);
			continue;
		}

		// Parse and add assignment to this layer's commands map
		if( aKey.compare(0,
				kActionOnlyPrefix.length(),
				condense(kActionOnlyPrefix)) == 0 )
		{
			theBuilder.debugSubItemName = theBuilder.debugSubItemName.substr(
				posAfterPrefix(itr->first, kActionOnlyPrefix));
			addButtonAction(theBuilder, theLayerIdx,
				&itr->first[posAfterPrefix(itr->first, kActionOnlyPrefix)],
				itr->second, true);
			continue;
		}

		addButtonAction(theBuilder, theLayerIdx,
			itr->first, itr->second, false);
	}
	theBuilder.keyValueList.clear();
	sLayers[theLayerIdx].buttonMap.trim();

	// Check for possible combo layers based on this layer
	if( theLayerIdx > 0 && !isComboLayer )
	{
		// Collect all keys that start with "Layer.aLayerName+", and
		// strip them down to the strings "LayerName1+LayerName2"
		std::string aComboLayerPrefix(kLayerPrefix);
		aComboLayerPrefix += aLayerName;
		aComboLayerPrefix += kComboLayerDeliminator;
		DBG_ASSERT(theBuilder.keyValueList.empty());
		Profile::getAllKeys(aComboLayerPrefix, theBuilder.keyValueList);
		for(Profile::KeyValuePairs::const_iterator itr =
			theBuilder.keyValueList.begin();
			itr != theBuilder.keyValueList.end(); ++itr)
		{
			std::string aComboLayerName = condense(itr->first);
			aComboLayerName = breakOffItemBeforeChar(aComboLayerName, '/');
			aComboLayerName = condense(aLayerName) +
				kComboLayerDeliminator + aComboLayerName;
			theBuilder.comboLayerNameToIdxMap.findOrAdd(aComboLayerName);
		}
		theBuilder.keyValueList.clear();
	}
}


static void buildRemainingControlsLayers(
	InputMapBuilder& theBuilder,  u16 aFirstLayer)
{
	// Build layers - sLayers size can expand as each layer adds other layers
	for(u16 aLayerIdx = aFirstLayer; aLayerIdx <= sLayers.size(); ++aLayerIdx)
	{
		if( !theBuilder.comboLayerNameToIdxMap.empty() &&
			aLayerIdx >= sLayers.size() )
		{// Try creating any combo layers that have all bases available
			for(u16 i = 0; i < theBuilder.comboLayerNameToIdxMap.size(); ++i)
			{
				const std::string aComboLayerName =
					theBuilder.comboLayerNameToIdxMap.keys()[i];
				u16 aComboLayerID =
					theBuilder.comboLayerNameToIdxMap.values()[i];
				if( aComboLayerID == 0 )
				{// Combo layer not yet generated, see if can do so now
					getOrCreateComboLayerID(theBuilder, aComboLayerName);
				}
			}
		}
		if( aLayerIdx >= sLayers.size() )
			break;
		buildControlsLayer(theBuilder, aLayerIdx);
	}
}


static void buildControlScheme(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Building control scheme layers...\n");

	// Create layer ID 0 for root layer
	DBG_ASSERT(sLayers.empty());
	getOrCreateLayerID(theBuilder, kMainLayerLabel);
	buildRemainingControlsLayers(theBuilder, 0);
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

	// Get the label (part of the string before first colon)
	std::string aLabel = breakOffItemBeforeChar(theString, ':');

	if( aLabel.empty() && !theString.empty() && theString[0] != ':' )
	{// Having no : character means this points to a sub-menu
		const size_t anOldMenuCount = sMenus.size();
		aMenuItem.cmd.type = eCmdType_OpenSubMenu;
		aMenuItem.cmd.subMenuID = getOrCreateMenuID(
			theBuilder, trim(theString), theMenuID);
		aMenuItem.cmd.menuID = sMenus[aMenuItem.cmd.subMenuID].rootMenuID;
		aMenuItem.label = sMenus[aMenuItem.cmd.subMenuID].label;
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
				menuPathOf(aMenuItem.cmd.subMenuID).c_str());
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
		aMenuItem.cmd.menuID = sMenus[theMenuID].rootMenuID;
		mapDebugPrint("%s: '%s' assigned to back out of menu\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	if( commandWordToID(condense(theString)) == eCmdWord_Close )
	{// Close menu
		aMenuItem.cmd.type = eCmdType_MenuClose;
		aMenuItem.cmd.menuID = sMenus[theMenuID].rootMenuID;
		mapDebugPrint("%s: '%s' assigned to close menu\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str());
		return aMenuItem;
	}

	aMenuItem.cmd = stringToCommand(theBuilder, theString);
	if( aMenuItem.cmd.type == eCmdType_Empty )
	{
		// Probably just forgot the > at front of a plain string
		sKeyStrings.push_back(std::string(">") + theString + "\r");
		aMenuItem.cmd.type = eCmdType_ChatBoxString;
		aMenuItem.cmd.keyStringIdx = u16(sKeyStrings.size()-1);
		logError("%s: '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box string. "
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
		// A menu command may add a new Layer with "Add Layer" command,
		// so need to check to see if sLayers.size() increases to detect
		// this and make sure to build out the newly-added Layer
		// (which may itself add even more Menus making sMenus larger).
		const u16 anOLdLayerCount = u16(sLayers.size());

		const std::string aPrefix = menuPathOf(aMenuID);
		const u16 aHUDElementID = sMenus[aMenuID].hudElementID;
		DBG_ASSERT(aHUDElementID < sHUDElements.size());
		const EHUDType aMenuStyle = sHUDElements[aHUDElementID].type;
		DBG_ASSERT(aMenuStyle >= eMenuStyle_Begin);
		DBG_ASSERT(aMenuStyle < eMenuStyle_End);
		const std::string aDebugNamePrefix =
			std::string("[") + aPrefix + "] (";

		// Check for command to execute automatically on menu open
		std::string aSpecialMenuCommandStr =
			Profile::getStr(aPrefix + "/" + kMenuOpenKey);
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
				sMenus[aMenuID].autoCommand = Command();
			}
			else
			{
				sMenus[aMenuID].autoCommand = aCmd;
				mapDebugPrint("%s: Assigned to command: %s\n",
					theBuilder.debugItemName.c_str(),
					aSpecialMenuCommandStr.c_str());
			}
		}

		// Check for command to execute automatically when back out of menu
		aSpecialMenuCommandStr =
			Profile::getStr(aPrefix + "/" + kMenuCloseKey);
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
				sMenus[aMenuID].backCommand = Command();
			}
			else
			{
				sMenus[aMenuID].backCommand = aCmd;
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
				aPrefix + "/" + aMenuItemKeyName);
			checkForNextMenuItem = !aMenuItemString.empty();
			if( aMenuStyle == eMenuStyle_Hotspots )
			{// Guarantee menu item count matches hotspot count
				const u16 anArrayID =
					sHUDElements[aHUDElementID].hotspotArrayID;
				DBG_ASSERT(anArrayID < sHotspotArrays.size());
				const HotspotArray& anArray = sHotspotArrays[anArrayID];
				const u16 anArraySize = anArray.last - anArray.first + 1;
				checkForNextMenuItem = itemIdx < anArraySize;
			}
			if( checkForNextMenuItem || itemIdx == 0 )
			{
				theBuilder.debugItemName =
					aDebugNamePrefix + aMenuItemKeyName + ")";
				sMenus[aMenuID].items.push_back(
					stringToMenuItem(
						theBuilder,
						aMenuID,
						aMenuItemString));
			}
			++itemIdx;
		}
		for(itemIdx = 0; itemIdx < eCmdDir_Num; ++itemIdx)
		{
			const std::string aMenuItemKeyName = k4DirMenuItemLabel[itemIdx];
			const std::string& aMenuItemString = Profile::getStr(
				aPrefix + "/" + aMenuItemKeyName);
			if( !aMenuItemString.empty() || aMenuStyle == eMenuStyle_4Dir )
			{
				theBuilder.debugItemName =
					aDebugNamePrefix + aMenuItemKeyName + ")";
				sMenus[aMenuID].dirItems[itemIdx] =
					stringToMenuItem(
						theBuilder,
						aMenuID,
						aMenuItemString);
				MenuItem& aMenuItem = sMenus[aMenuID].dirItems[itemIdx];
				if( aMenuItem.cmd.type == eCmdType_OpenSubMenu &&
					aMenuStyle != eMenuStyle_4Dir )
				{
					// Requests to open sub-menus with directionals in
					// most menu styles should use replace menu instead,
					// so they behave like "side" instead of "sub" menus
					aMenuItem.cmd.type = eCmdType_ReplaceMenu;
				}
			}
		}

		// Buld any new controls layers added by this menu
		buildRemainingControlsLayers(theBuilder, anOLdLayerCount);
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
	theBuilder.debugItemName += sLayers[theLayerID].label;
	sLayers[theLayerID].hideHUD.clearAndResize(sHUDElements.size());
	sLayers[theLayerID].showHUD.clearAndResize(sHUDElements.size());
	std::string aLayerHUDKey = sLayers[theLayerID].label;
	if( theLayerID == 0 )
		aLayerHUDKey += "/";
	else
		aLayerHUDKey = std::string(kLayerPrefix)+aLayerHUDKey+"/";
	aLayerHUDKey += kHUDSettingsKey;
	std::string aLayerHUDDescription = Profile::getStr(aLayerHUDKey);

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
		sLayers[theLayerID].showHUD.resize(sHUDElements.size());
		sLayers[theLayerID].showHUD.set(anElementIdx, show);
		sLayers[theLayerID].hideHUD.resize(sHUDElements.size());
		sLayers[theLayerID].hideHUD.set(anElementIdx, !show);
	}
}


static void buildHUDElements(InputMapBuilder& theBuilder)
{
	// Process the "HUD=" key for each layer
	theBuilder.elementsProcessed.clearAndResize(sLayers.size());
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
		buildHUDElementsForLayer(theBuilder, aLayerID);

	// Special-case manually-managed HUD element (top-most overlay)
	sHUDElements.push_back(HUDElement());
	sHUDElements.back().type = eHUDType_System;

	// Above may have added new HUD elements. Now that all are added,
	// make sure every layer's hideHUD and showHUD are correct size
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
	{
		sLayers[aLayerID].hideHUD.resize(sHUDElements.size());
		sLayers[aLayerID].showHUD.resize(sHUDElements.size());
	}

	// Can now also set size of global sizes related to HUD elements
	gVisibleHUD.clearAndResize(sHUDElements.size());
	gRedrawHUD.clearAndResize(sHUDElements.size());
	gReshapeHUD.clearAndResize(sHUDElements.size());
	gActiveHUD.clearAndResize(sHUDElements.size());
	gDisabledHUD.clearAndResize(sHUDElements.size());
	gConfirmedMenuItem.resize(sHUDElements.size());
	for(size_t i = 0; i < gConfirmedMenuItem.size(); ++i)
		gConfirmedMenuItem[i] = kInvalidItem;
}


static void buildGamepadButtonRemaps(InputMapBuilder& theBuilder)
{
	DBG_ASSERT(theBuilder.keyValueList.empty());
	StringToValueMap<bool> kOtherGamepadProperties;
	kOtherGamepadProperties.setValue("MOUSECURSORDEADZONE", true);
	kOtherGamepadProperties.setValue("MOUSECURSORSATURATION", true);
	kOtherGamepadProperties.setValue("MOUSELOOKDEADZONE", true);
	kOtherGamepadProperties.setValue("MOUSELOOKSATURATION", true);
	kOtherGamepadProperties.setValue("MOUSEWHEELDEADZONE", true);
	kOtherGamepadProperties.setValue("MOUSEWHEELSATURATION", true);
	kOtherGamepadProperties.setValue("MOUSEDPADACCEL", true);
	kOtherGamepadProperties.setValue("MOVEDEADZONE", true);
	kOtherGamepadProperties.setValue("CANCELAUTORUNDEADZONE", true);
	kOtherGamepadProperties.setValue("MOVESTRAIGHTBIAS", true);
	kOtherGamepadProperties.setValue("LSTICKBUTTONTHRESHOLD", true);
	kOtherGamepadProperties.setValue("RSTICKBUTTONTHRESHOLD", true);
	kOtherGamepadProperties.setValue("TRIGGERBUTTONTHRESHOLD", true);

	Profile::getAllKeys(std::string(kButtonRebindsPrefix) + "/",
		theBuilder.keyValueList);
	for(size_t i = 0; i < theBuilder.keyValueList.size(); ++i)
	{
		const std::string& aPropName = theBuilder.keyValueList[i].first;
		const std::string& aPropVal = theBuilder.keyValueList[i].second;
		const std::string& aPressBtnName = condense(aPropName);
		const std::string& aResultBtnName = condense(aPropVal);
		if( kOtherGamepadProperties.contains(aPressBtnName) )
			continue;
		EButton aPressedBtnID = buttonNameToID(aPressBtnName);
		if( aPressedBtnID != eBtn_None && aPressedBtnID < eBtn_Num )
		{
			EButton aResultBtnID = buttonNameToID(aResultBtnName);
			if( aResultBtnID != eBtn_None && aResultBtnID < eBtn_Num )
			{
				sButtonRemap[aPressedBtnID] = aResultBtnID;
				mapDebugPrint("Remapping '%s' button to activate commands "
					"originally assigned to '%s' instead!\n",
					aPropName.c_str(),
					aPropVal.c_str());
			}
			else
				logError("Unrecognized gamepad button name '%s' to map %s to",
					aPropVal.c_str(), aPropName.c_str());
		}
		else
		{
			logError("Unrecognized [%s] property name (button name?) '%s'",
				kButtonRebindsPrefix, aPropName.c_str());
		}
	}
	theBuilder.keyValueList.clear();
}


static void setCStringPointerFor(Command* theCommand)
{
	// Important that the raw string pointer set here is no longer held
	// past anything happening to sKeyStrings vector (being resized/etc)
	// or we'd get a dangling pointer - but passing strings to external
	// modules this way saves on string copies! It should be fine as
	// long as external modules release references to this data before
	// loadProfile() is called again.

	switch(theCommand->type)
	{
	case eCmdType_VKeySequence:
	case eCmdType_ChatBoxString:
		DBG_ASSERT(theCommand->keyStringIdx < sKeyStrings.size());
		theCommand->string = sKeyStrings[theCommand->keyStringIdx].c_str();
		break;
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
	for(size_t i= 0; i < eBtn_Num; ++i)
		sButtonRemap[i] = EButton(i);

	// Get default button hold time to execute eBtnAct_Hold command
	sDefaultButtonHoldTime = max(0, Profile::getInt("System/ButtonHoldTime"));

	// Create temp builder object and build everything from the Profile data
	{
		InputMapBuilder anInputMapBuilder;
		buildHotspots(anInputMapBuilder);
		buildCommandAliases(anInputMapBuilder);
		buildControlScheme(anInputMapBuilder);
		buildMenus(anInputMapBuilder);
		buildHUDElements(anInputMapBuilder);
		buildGamepadButtonRemaps(anInputMapBuilder);
	}

	// Trim unused memory
	if( sHotspots.size() < sHotspots.capacity() )
		std::vector<Hotspot>(sHotspots).swap(sHotspots);
	if( sHotspotArrays.size() < sHotspotArrays.capacity() )
		std::vector<HotspotArray>(sHotspotArrays).swap(sHotspotArrays);
	if( sKeyBindArrays.size() < sKeyBindArrays.capacity() )
		std::vector<KeyBindArray>(sKeyBindArrays).swap(sKeyBindArrays);
	if( sKeyStrings.size() < sKeyStrings.capacity() )
		std::vector<std::string>(sKeyStrings).swap(sKeyStrings);
	if( sLayers.size() < sLayers.capacity() )
		std::vector<ControlsLayer>(sLayers).swap(sLayers);
	sComboLayers.trim();
	if( sHUDElements.size() < sHUDElements.capacity() )
		std::vector<HUDElement>(sHUDElements).swap(sHUDElements);
	if( sMenus.size() < sMenus.capacity() )
		std::vector<Menu>(sMenus).swap(sMenus);
	sButtonHoldTimes.trim();

	// Now that are done messing with resizing vectors which can invalidate
	// pointers, can convert Commands with temp keyStringIdx field being a
	// sKeyStrings index into having direct pointers to the C-strings
	// ('string' field of the Command) for use in other modules.
	for(std::vector<KeyBindArray>::iterator itr = sKeyBindArrays.begin();
		itr != sKeyBindArrays.end(); ++itr)
	{
		for(KeyBindArray::iterator itr2 = itr->begin();
			itr2 != itr->end(); ++itr2)
		{
			setCStringPointerFor(&itr2->cmd);
		}
	}
	for(std::vector<Menu>::iterator itr = sMenus.begin();
		itr != sMenus.end(); ++itr)
	{
		setCStringPointerFor(&itr->autoCommand);
		setCStringPointerFor(&itr->backCommand);
		// Trim unused memory while here anyway
		if( itr->items.size() < itr->items.capacity() )
			std::vector<MenuItem>(itr->items).swap(itr->items);
		for(std::vector<MenuItem>::iterator itr2 = itr->items.begin();
			itr2 != itr->items.end(); ++itr2)
		{
			setCStringPointerFor(&itr2->cmd);
		}
		for(size_t i = 0; i < ARRAYSIZE(itr->dirItems); ++i)
			setCStringPointerFor(&itr->dirItems[i].cmd);
	}

	for(std::vector<ControlsLayer>::iterator itr = sLayers.begin();
		itr != sLayers.end(); ++itr)
	{
		// Trim unused memory while here anyway
		itr->signalCommands.trim();
		itr->buttonMap.trim();
		for(ButtonActionsMap::iterator itr2 = itr->buttonMap.begin();
			itr2 != itr->buttonMap.end(); ++itr2)
		{
			for(size_t i = 0; i < eBtnAct_Num; ++i)
				setCStringPointerFor(&itr2->second.cmd[i]);
		}
		for(VectorMap<u16, Command>::iterator itr2 =
			itr->signalCommands.begin();
			itr2 != itr->signalCommands.end(); ++itr2)
		{
			setCStringPointerFor(&itr2->second);
		}
	}
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
	theButton = sButtonRemap[theButton];

	ButtonActionsMap::const_iterator itr =
		sLayers[theLayerID].buttonMap.find(theButton);
	if( itr != sLayers[theLayerID].buttonMap.end() )
		return &itr->second.cmd[0];

	return null;
}


const VectorMap<u16, Command>& signalCommandsForLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].signalCommands;
}


u32 commandHoldTime(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);
	theButton = sButtonRemap[theButton];

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
	return sLayers[theLayerID].parentLayer;
}


s8 layerPriority(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].priority;	
}


EMouseMode mouseMode(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].mouseMode;
}


const BitVector<>& hudElementsToShow(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].showHUD;
}


const BitVector<>& hudElementsToHide(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].hideHUD;
}


const BitVector<>& hotspotArraysToEnable(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].enableHotspots;
}


const BitVector<>& hotspotArraysToDisable(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].disableHotspots;
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


const Command& commandForMenuItem(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus[theMenuID].items.size());
	return sMenus[theMenuID].items[theMenuItemIdx].cmd;
}


const Command& commandForMenuDir(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus[theMenuID].dirItems[theDir].cmd;
}


const Command& menuAutoCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus[theMenuID].autoCommand;
}


const Command& menuBackCommand(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus[theMenuID].backCommand;
}


EHUDType menuStyle(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(sMenus[theMenuID].hudElementID < sHUDElements.size());
	return sHUDElements[sMenus[theMenuID].hudElementID].type;
}


u16 rootMenuOfMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus[theMenuID].rootMenuID;
}


u16 menuHotspotArray(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	const u16 aHUDElementID = hudElementForMenu(theMenuID);
	DBG_ASSERT(sHUDElements[aHUDElementID].type == eMenuStyle_Hotspots);
	const u16 anArrayID = sHUDElements[aHUDElementID].hotspotArrayID;
	DBG_ASSERT(anArrayID < sHotspotArrays.size());
	return anArrayID;
}


std::string menuSectionName(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return menuPathOf(theMenuID);
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


EHUDType hudElementType(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements[theHUDElementID].type;
}


bool hudElementIsAMenu(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return
		sHUDElements[theHUDElementID].type >= eMenuStyle_Begin &&
		sHUDElements[theHUDElementID].type < eMenuStyle_End;
}


u16 menuForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements[theHUDElementID].menuID;
}


u16 hudElementForMenu(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus[theMenuID].hudElementID;
}


u16 hotspotForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	DBG_ASSERT(sHUDElements[theHUDElementID].type == eHUDType_Hotspot);
	return sHUDElements[theHUDElementID].hotspotID;	
}


u16 keyBindArrayForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	DBG_ASSERT(
		sHUDElements[theHUDElementID].type == eHUDType_KBArrayLast ||
		sHUDElements[theHUDElementID].type == eHUDType_KBArrayDefault);
	return sHUDElements[theHUDElementID].keyBindArrayID;
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
	return u16(sMenus[theMenuID].items.size());
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
	return sLayers[theLayerID].label;
}


const std::string& hotspotArrayLabel(u16 theHotspotArrayID)
{
	DBG_ASSERT(theHotspotArrayID < sHotspotArrays.size());
	return sHotspotArrays[theHotspotArrayID].label;
}


const std::string& menuLabel(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	if( sMenus[theMenuID].rootMenuID == theMenuID )
		return hudElementDisplayName(hudElementForMenu(theMenuID));
	return sMenus[theMenuID].label;
}


const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus[theMenuID].items.size());
	return sMenus[theMenuID].items[theMenuItemIdx].label;
}


const std::string& menuItemAltLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus[theMenuID].items.size());
	return sMenus[theMenuID].items[theMenuItemIdx].altLabel;
}


const std::string& menuDirLabel(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus[theMenuID].dirItems[theDir].label;
}


const std::string& hudElementKeyName(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements[theHUDElementID].keyName;
}

const std::string& hudElementDisplayName(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	if( sHUDElements[theHUDElementID].displayName.empty() )
		return sHUDElements[theHUDElementID].keyName;
	return sHUDElements[theHUDElementID].displayName;
}

#undef mapDebugPrint
#undef INPUT_MAP_DEBUG_PRINT

} // InputMap
