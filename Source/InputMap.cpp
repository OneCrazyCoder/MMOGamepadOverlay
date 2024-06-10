//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputMap.h"

#include "HotspotMap.h" // stringToHotspot()
#include "Lookup.h"
#include "Profile.h"

namespace InputMap
{

// Whether or not debug messages print depends on which line is commented out
//#define mapDebugPrint(...) debugPrint("InputMap: " __VA_ARGS__)
#define mapDebugPrint(...) ((void)0)

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
const char* kKBArrayKeys[] = { "KeyBindArray", "Array", "KeyBinds" };
const char* kKeybindsPrefix = "KeyBinds/";
const char* kHotspotsPrefix = "Hotspots/";
const char* k4DirMenuItemLabel[] = { "L", "R", "U", "D" }; // match ECommandDir!
DBG_CTASSERT(ARRAYSIZE(k4DirMenuItemLabel) == eCmdDir_Num);

// These need to be in all upper case
const char* kIncludeKey = "INCLUDE";
const char* kHUDSettingsKey = "HUD";
const char* kHotspotArraysKey = "HOTSPOTS";
const char* kMouseModeKey = "MOUSE";
const char* kParentLayerKey = "PARENT";
const char* kMenuOpenKey = "AUTO";
const char* kThresholdSuffix = "THRESHOLD";
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
	"With",					// eBtnAct_With
};
DBG_CTASSERT(ARRAYSIZE(kButtonActionPrefx) == eBtnAct_Num);

const char* kSpecialKeyNames[] =
{
	"SWAPWINDOWMODE",		// eSpecialKey_SwapWindowMode
	"MOVEFORWARD",			// eSpecialKey_MoveF
	"MOVEBACK",				// eSpecialKey_MoveB
	"TURNLEFT",				// eSpecialKey_TurnL
	"TURNRIGHT",			// eSpecialKey_TurnR
	"STRAFELEFT",			// eSpecialKey_StrafeL
	"STRAFERIGHT",			// eSpecialKey_StrafeR
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
typedef std::vector<KeyBindArrayEntry> KeyBindArray;

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
	union { u16 hotspotArrayID; u16 keyBindArrayID; };
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
};
typedef VectorMap<EButton, ButtonActions> ButtonActionsMap;

struct ControlsLayer
{
	std::string label;
	ButtonActionsMap map;
	BitVector<> showHUD;
	BitVector<> hideHUD;
	BitVector<> enableHotspots;
	BitVector<> disableHotspots;
	EMouseMode mouseMode;
	u16 includeLayer;
	u16 parentLayer;

	ControlsLayer() :
		showHUD(),
		hideHUD(),
		enableHotspots(),
		disableHotspots(),
		mouseMode(eMouseMode_Default),
		includeLayer(),
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
static VectorMap<std::pair<u16, EButton>, u16> sButtonHoldTimes;
static VectorMap<std::pair<u16, EButton>, u8> sButtonThresholds;
static u16 sDefaultButtonHoldTime = 400;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

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

	// Check if has an "include=" layer specified that needs adding first
	std::string aLayerPrefix;
	if( sLayers.empty() )
		aLayerPrefix = aLayerKeyName+"/";
	else
		aLayerPrefix = upper(kLayerPrefix)+aLayerKeyName+"/";
	const std::string& anIncludeName =
		Profile::getStr(aLayerPrefix + kIncludeKey);
	u16 anIncludeIdx = 0;
	if( sLayers.empty() && !anIncludeName.empty() )
	{
		logError("Root layer [%s] can not Include= another layer. "
			"Consider using 'Auto = Layer %s' as only entry instead",
			theLayerName.c_str(),
			anIncludeName.c_str());
	}
	else if( !anIncludeName.empty() )
	{
		// Check for infinite loop of Include= properties
		std::vector<std::string>::iterator itr = std::find(
			theLoopCheckList.begin(),
			theLoopCheckList.end(),
			upper(anIncludeName));
		if( itr != theLoopCheckList.end() )
		{
			logError("Infinite include loop with layer [%s%s]"
				" trying to include layer %s",
				kLayerPrefix, theLayerName.c_str(), itr->c_str());
		}
		else
		{
			theLoopCheckList.push_back(aLayerKeyName);
			anIncludeIdx = getOrCreateLayerID(
				theBuilder, anIncludeName, theLoopCheckList);
		}
	}

	// Add new layer to sLayers and the name-to-index map
	theBuilder.layerNameToIdxMap.setValue(aLayerKeyName, u16(sLayers.size()));
	sLayers.push_back(ControlsLayer());
	sLayers.back().label = theLayerName;
	sLayers.back().includeLayer = anIncludeIdx;

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
	bool hasInputAssigned = false)
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

		if( aHUDElement.type == eMenuStyle_Hotspots )
		{
			std::string aHotspotArrayName =
				Profile::getStr(aMenuPath + "/" + kHotspotArraysKey);
			if( aHotspotArrayName.empty() )
			{
				logError(
					"Can't find required '[%s%s]/%s =' property "
					"for item referenced by [%s]! ",
					kMenuPrefix,
					theName.c_str(),
					kHotspotArraysKey,
					theBuilder.debugItemName.c_str());
				aHUDElement.type = eMenuStyle_List;
			}
			else if( u16* aHotspotArrayID =
						theBuilder.hotspotArrayNameToIdxMap.find(
							condense(aHotspotArrayName)) )
			{
				aHUDElement.hotspotArrayID = *aHotspotArrayID;
			}
			else
			{
				logError(
					"Hotspot Array '%s' not found for menu [%s%s]! "
					"Changing it to a Menu of type List instead...",
					aHotspotArrayName.c_str(),
					kMenuPrefix,
					theName.c_str());
				aHUDElement.type = eMenuStyle_List;
			}
		}
	}

	if( aHUDElement.type == eHUDType_KBArrayLast ||
		aHUDElement.type == eHUDType_KBArrayDefault )
	{
		std::string aKBArrayName;
		int i = 0;
		for(; aKBArrayName.empty() && i < ARRAYSIZE(kKBArrayKeys); ++i)
			aKBArrayName = Profile::getStr(aHUDPath + "/" + kKBArrayKeys[i]);
		if( aKBArrayName.empty() )
		{
			logError(
				"Can't find required '[%s%s]/%s =' property "
				"for item referenced by [%s]! ",
				kHUDPrefix,
				theName.c_str(),
				kKBArrayKeys[0],
				theBuilder.debugItemName.c_str());
			aHUDElement.type = eHUDItemType_Rect;
		}
		else if( u16* aKeyBindArrayID =
					theBuilder.keyBindArrayNameToIdxMap.find(
						condense(aKBArrayName)) )
		{
			aHUDElement.keyBindArrayID = *aKeyBindArrayID;
		}
		else
		{
			logError(
				"Unrecognized '%s' specified for '[%s%s]/%s =' property "
				"for item referenced by [%s]! ",
				aKBArrayName.c_str(),
				kHUDPrefix,
				theName.c_str(),
				kKBArrayKeys[i-1],
				theBuilder.debugItemName.c_str());
			aHUDElement.type = eHUDItemType_Rect;
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
		if( aKeyWordID != eCmdWord_Nothing &&
			aKeyWordID != eCmdWord_Unassigned )
		{
			return result;
		}
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

	// "= [Leave] Unassigned"
	if( keyWordsFound.test(eCmdType_Unassigned) && keyWordsFound.count() == 1)
	{
		result.type = eCmdType_Unassigned;
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
	allowedKeyWords.set(eCmdWord_UIScale);
	if( keyWordsFound.test(eCmdWord_UIScale) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_UpdateUIScale;
		return result;
	}

	// "= Close App"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Close);
	allowedKeyWords.set(eCmdWord_App);
	if( keyWordsFound.test(eCmdWord_Close) &&
		keyWordsFound.test(eCmdWord_App) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_QuitApp;
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
	if( keyWordsFound.test(eCmdWord_Remove) &&
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
		if( keyWordsFound.test(eCmdWord_Replace) &&
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

		// "= [Menu] <aMenuName> Back or Close [with mouse click]"
		// allowedKeyWords = Menu & Back & Mouse & Click
		allowedKeyWords.set(eCmdWord_Back);
		allowedKeyWords.set(eCmdWord_Close);
		if( keyWordsFound.test(eCmdWord_Back) &&
			keyWordsFound.test(eCmdWord_Close) &&
			(keyWordsFound & ~allowedKeyWords).count() <= 1 )
		{
			result.type = eCmdType_MenuBackOrClose;
			result.menuID = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Back);
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Mouse);
		allowedKeyWords.reset(eCmdWord_Click);

		// "= Edit <aMenuName> [Menu] [with mouse click]"
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
	else if( keyWordsFound.test(eCmdWord_Back) ) aCmdDir = eCmdDir_Back;
	result.dir = aCmdDir;
	// Remove direction-related bits from keyWordsFound
	keyWordsFound &= ~allowedKeyWords;

	if( allowButtonActions && aMenuName )
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

	// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Once] <aCmdDir>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Move);
	allowedKeyWords.set(eCmdWord_Mouse);
	allowedKeyWords.set(eCmdWord_MouseWheel);
	allowedKeyWords.set(eCmdWord_Once);
	if( keyWordsFound.test(eCmdWord_MouseWheel) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MouseWheel;
		result.mouseWheelMotionType = eMouseWheelMotion_Once;
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
	if( theString[0] == '/' )
	{
		sKeyStrings.push_back(theString);
		result.type = eCmdType_SlashCommand;
		result.keyStringIdx = u16(sKeyStrings.size()-1);
		return result;
	}
	if( theString[0] == '>' )
	{
		sKeyStrings.push_back(theString);
		result.type = eCmdType_SayString;
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
		if( allowHoldActions )
		{
			if( Command* aSpecialKeyCommand =
					theBuilder.specialKeyNameToCommandMap.find(aKeyBindName) )
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

	// Special hotspots default to center if not specified
	Hotspot aHotspot;
	aHotspot.x.anchor = 32768;
	aHotspot.y.anchor = 32768;
	for(u16 i = 0; i < eSpecialHotspot_Num; ++i)
	{
		sHotspots[i] = aHotspot;
		theBuilder.hotspotNameToIdxMap.setValue(kSpecialHotspotNames[i], i);
	}

	// Parse normal hotspots and pick out any that might be arrays
	DBG_ASSERT(theBuilder.keyValueList.empty());
	Profile::getAllKeys(kHotspotsPrefix, theBuilder.keyValueList);
	for(size_t i = 0; i < theBuilder.keyValueList.size(); ++i)
	{
		std::string aHotspotName = theBuilder.keyValueList[i].first;
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
					.findOrAdd(aHotspotArrayName, u16(sHotspotArrays.size()));
				if( aHotspotArrayIdx >= sHotspotArrays.size() )
				{
					HotspotArray anEntry;
					anEntry.label  = aHotspotArrayName;
					anEntry.first = anEntry.last = 0;
					sHotspotArrays.push_back(anEntry);
				}
				sHotspotArrays[aHotspotArrayIdx].last = max(
					sHotspotArrays[aHotspotArrayIdx].last,
					anArrayIdx);
				continue;
			}
			// Doesn't seem to be part of a valid array, so restore name
			aHotspotName = theBuilder.keyValueList[i].first;
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
				aHotspotName.c_str(), theBuilder.keyValueList[i].second);
			sHotspots[aHotspotIdx] = aHotspot;
		}
	}
	theBuilder.keyValueList.clear();

	// Fill in the hotspot arrays
	for(size_t aHSA_ID = 0; aHSA_ID < sHotspotArrays.size(); ++aHSA_ID)
	{
		HotspotArray& aHotspotArray = sHotspotArrays[aHSA_ID];
		mapDebugPrint("Building Hotspot Array %s\n", aHotspotArray.label);
		Hotspot anAnchor;
		u16* anAnchorIdx = theBuilder.hotspotNameToIdxMap.
			find(aHotspotArray.label);
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
				aHotspotName, aHotspotID);
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
						"starting with %d\n",
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
							aHotspotName, aHotspotID);
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
			// Set better name for the hotspot array than default all-uppercase
			sHotspotArrays[*aHotspotArrayID].label = aName;
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


static void buildCommandAliases(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning KeyBinds...\n");

	DBG_ASSERT(theBuilder.keyValueList.empty());
	Profile::getAllKeys(kKeybindsPrefix, theBuilder.keyValueList);
	for(size_t i = 0; i < theBuilder.keyValueList.size(); ++i)
	{
		std::string anActionName = theBuilder.keyValueList[i].first;
		std::string aCommandDescription = theBuilder.keyValueList[i].second;

		// Keybinds can only be assigned to direct input
		Command aCmd;
		if( aCommandDescription.empty() )
		{// Do nothing
			aCmd.type = eCmdType_DoNothing;
			aCommandDescription = "<Do Nothing>";
		}
		else if( aCommandDescription[0] == '/' )
		{// Slash command (types into chat box)
			aCmd.type = eCmdType_SlashCommand;
			sKeyStrings.push_back(aCommandDescription);
			aCmd.keyStringIdx = u16(sKeyStrings.size()-1);
		}
		else if( aCommandDescription[0] == '>' )
		{// Say string (types into chat box - '>' becomes Enter to start)
			aCmd.type = eCmdType_SayString;
			sKeyStrings.push_back(aCommandDescription);
			aCmd.keyStringIdx = u16(sKeyStrings.size()-1);
		}
		else
		{// VKey Sequence
			theBuilder.parsedString.clear();
			sanitizeSentence(aCommandDescription, theBuilder.parsedString);
			const std::string& aVKeySeq = namesToVKeySequence(
				theBuilder, theBuilder.parsedString);
			if( !aVKeySeq.empty() )
			{
				if( u16 aVKey = vKeySeqToSingleKey((const u8*)aVKeySeq.c_str()) )
				{
					aCmd.type = eCmdType_TapKey;
					aCmd.vKey = aVKey;
				}
				else
				{
					aCmd.type = eCmdType_VKeySequence;
					aCmd.keyStringIdx = u16(sKeyStrings.size());
					sKeyStrings.push_back(aVKeySeq);
				}
			}
		}

		if( aCmd.type != eCmdType_Empty )
		{
			theBuilder.commandAliases.setValue(anActionName, aCmd);

			mapDebugPrint("Assigned to alias '%s': '%s'\n",
				anActionName.c_str(),
				aCommandDescription.c_str());

			// Check if could be part of a keybind array
			const int anArrayIdx = breakOffIntegerSuffix(anActionName);
			if( anArrayIdx >= 0 )
			{
				u16 aKeyBindArrayID =
					theBuilder.keyBindArrayNameToIdxMap.findOrAdd(
						anActionName, u16(sKeyBindArrays.size()));
				if( aKeyBindArrayID >= sKeyBindArrays.size() )
					sKeyBindArrays.resize(aKeyBindArrayID+1);
				KeyBindArray& aKeyBindArray = sKeyBindArrays[aKeyBindArrayID];
				if( anArrayIdx >= aKeyBindArray.size() )
					aKeyBindArray.resize(anArrayIdx+1);
				aKeyBindArray[anArrayIdx].cmd = aCmd;
				// Check for a matching named hotspot
				u16* aHotspotID = theBuilder.hotspotNameToIdxMap.find(
					anActionName + toString(anArrayIdx));
				if( aHotspotID )
					aKeyBindArray[anArrayIdx].hotspotID = *aHotspotID;
				mapDebugPrint("Assigned KeyBindArray '%s' index #%d to '%s'%s\n",
					anActionName.c_str(),
					anArrayIdx,
					aCommandDescription.c_str(),
					aHotspotID ? " (+ hotspot)" : "");
			}
		}
		else
		{
			logError(
				"%s%s: Unable to decipher and assign '%s'",
				kKeybindsPrefix,
				anActionName.c_str(),
				aCommandDescription.c_str());
		}
	}
	theBuilder.keyValueList.clear();

	// Can now also set size of global vectors related to Key Bind Arrays
	gKeyBindArrayLastIndex.reserve(sKeyBindArrays.size());
	gKeyBindArrayLastIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.reserve(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndex.resize(sKeyBindArrays.size());
	gKeyBindArrayLastIndexChanged.clearAndResize(sKeyBindArrays.size());
	gKeyBindArrayDefaultIndexChanged.clearAndResize(sKeyBindArrays.size());

	// Generate special key to command map while here
	Command aCmd;
	aCmd.type = eCmdType_MoveTurn;
	aCmd.dir = eCmdDir_Forward;
	theBuilder.specialKeyNameToCommandMap.setValue(
		kSpecialKeyNames[eSpecialKey_MoveF], aCmd);
	aCmd.dir = eCmdDir_Back;
	theBuilder.specialKeyNameToCommandMap.setValue(
		kSpecialKeyNames[eSpecialKey_MoveB], aCmd);
	aCmd.dir = eCmdDir_Left;
	theBuilder.specialKeyNameToCommandMap.setValue(
		kSpecialKeyNames[eSpecialKey_TurnL], aCmd);
	aCmd.dir = eCmdDir_Right;
	theBuilder.specialKeyNameToCommandMap.setValue(
		kSpecialKeyNames[eSpecialKey_TurnR], aCmd);
	aCmd.type = eCmdType_MoveStrafe;
	aCmd.dir = eCmdDir_Left;
	theBuilder.specialKeyNameToCommandMap.setValue(
		kSpecialKeyNames[eSpecialKey_StrafeL], aCmd);
	aCmd.dir = eCmdDir_Right;
	theBuilder.specialKeyNameToCommandMap.setValue(
		kSpecialKeyNames[eSpecialKey_StrafeR], aCmd);
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
			if( !*aStrChar || ::toupper(*aPrefixChar) != *aStrChar )
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
	Command& theCmd,
	const std::string& theCmdStr)
{
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		logError("[%s]: Not sure how to assign '%s%s%s' to '%s'!",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID],
			theCmdStr.c_str());
		break;
	case eCmdType_DoNothing:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to <do nothing>\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID]);
		break;
	case eCmdType_Unassigned:
		mapDebugPrint("[%s]: '%s%s%s' left as <unassigned> "
			"(overrides Include= layer)\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID]);
		break;
	case eCmdType_SlashCommand:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID],
			sKeyStrings[theCmd.keyStringIdx].c_str());
		break;
	case eCmdType_SayString:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID],
			sKeyStrings[theCmd.keyStringIdx].c_str() + 1);
	case eCmdType_TapKey:
	case eCmdType_PressAndHoldKey:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to: %s (%s%s%s%s%s)\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID],
			theCmdStr.c_str(),
			!!(theCmd.vKey & kVKeyShiftFlag) ? "Shift+" : "",
			!!(theCmd.vKey & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(theCmd.vKey & kVKeyAltFlag) ? "Alt+" : "",
			!!(theCmd.vKey & kVKeyWinFlag) ? "Win+" : "",
			virtualKeyToName(theCmd.vKey & kVKeyMask).c_str());
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to sequence: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID],
			theCmdStr.c_str());
		break;
	default:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to command: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[theBtnAct],
			kButtonActionPrefx[theBtnAct][0] ? " " : "",
			kProfileButtonName[theBtnID],
			theCmdStr.c_str());
		break;
	}
}


static void addButtonAction(
	InputMapBuilder& theBuilder,
	u16 theLayerIdx,
	std::string theBtnName,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theBtnName.empty() || theCmdStr.empty() )
		return;

	// Determine button & action to assign command to
	EButtonAction aBtnAct = breakOffButtonAction(theBtnName);
	int aBtnTime = breakOffIntegerSuffix(theBtnName);
	EButton aBtnID = buttonNameToID(theBtnName);

	if( aBtnID >= eBtn_Num )
	{
		// Button ID not identified from key string
		// Could be an attempt to assign multiple buttons at once as
		// a single 4-directional input (D-pad, analog sticks, etc).
		bool isA4DirMultiAssign = false;
		for(size_t i = 0; i < ARRAYSIZE(k4DirButtons); ++i)
		{
			if( theBtnName == k4DirButtons[i] )
			{
				isA4DirMultiAssign = true;
				break;
			}
		}
		// If not, must just be a badly-named action + button key
		if( !isA4DirMultiAssign )
		{
			logError("Unable to identify Gamepad Button '%s' requested in [%s]",
				theBtnName.c_str(),
				theBuilder.debugItemName.c_str());
			return;
		}

		// Attempt to assign to all 4 directional variations of this button
		// to the same command (or directional variations of it).
		const Command& aBaseCmd = stringToCommand(
			theBuilder, theCmdStr, true,
			aBtnAct == eBtnAct_Down);
		bool dirCommandFailed = false;
		for(size_t i = 0; i < 4; ++i)
		{
			// Get true button ID by adding direction key to button name
			aBtnID = buttonNameToID(theBtnName + k4DirKeyNames[i]);
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
			Command& aDestCmd =
				sLayers[theLayerIdx].map.findOrAdd(aBtnID).cmd[aBtnAct];
			// Direct assignment should take priority over multi-assignment,
			// so if this was already assigned directly then leave it alone.
			if( aDestCmd.type != eCmdType_Empty )
				continue;
			// Make and report assignment
			aDestCmd = aCmd;
			if( aBtnAct == eBtnAct_Hold )
			{// Assign time to hold button for this action
				sButtonHoldTimes.setValue(
					std::pair<u16, EButton>(theLayerIdx, aBtnID),
					aBtnTime < 0
						? sDefaultButtonHoldTime
						: u16(aBtnTime));
			}
			reportButtonAssignment(
				theBuilder, aBtnAct, aBtnID, aDestCmd, aCmdStr);
		}
		return;
	}

	// Parse command string into a Command struct
	Command aCmd = stringToCommand(
		theBuilder, theCmdStr, true, aBtnAct == eBtnAct_Down);

	// Make the assignment
	Command& aDestCmd =
		sLayers[theLayerIdx].map.findOrAdd(aBtnID).cmd[aBtnAct];
	aDestCmd = aCmd;
	if( aBtnAct == eBtnAct_Hold )
	{// Assign time to hold button for this action
		sButtonHoldTimes.setValue(
			std::pair<u16, EButton>(theLayerIdx, aBtnID),
			aBtnTime < 0
				? sDefaultButtonHoldTime
				: u16(aBtnTime));
	}

	// Report the results of the assignment
	reportButtonAssignment(theBuilder, aBtnAct, aBtnID, aDestCmd, theCmdStr);
}


static void addButtonThreshold(
	InputMapBuilder& theBuilder,
	u16 theLayerIdx,
	std::string theBtnName,
	const std::string& theThresholdValueStr)
{
	EButton aBtnID = buttonNameToID(theBtnName);
	if( aBtnID >= eBtn_Num )
	{
		bool isA4DirMultiAssign = false;
		for(size_t i = 0; i < ARRAYSIZE(k4DirButtons); ++i)
		{
			if( theBtnName == k4DirButtons[i] )
			{
				isA4DirMultiAssign = true;
				break;
			}
		}
		// If not, must just be a badly-named button
		if( !isA4DirMultiAssign )
		{
			logError("Unable to identify Gamepad Button '%s' requested in [%s]",
				theBtnName.c_str(),
				theBuilder.debugItemName.c_str());
			return;
		}

		// Attempt to assign to all 4 directional variations of this button
		// to the same threshold value
		for(size_t i = 0; i < 4; ++i)
		{
			// Get true button ID by adding direction key to button name
			const std::string& aBtnName = theBtnName + k4DirKeyNames[i];
			aBtnID = buttonNameToID(aBtnName);
			DBG_ASSERT(aBtnID < eBtn_Num);
			// Direct assignment should take priority over multi-assignment,
			// so if this was already assigned directly then leave it alone.
			const std::pair<u16, EButton> aKey(theLayerIdx, aBtnID);
			if( sButtonThresholds.contains(aKey) )
				continue;
			// Make individual assignment
			addButtonThreshold(theBuilder, theLayerIdx, aBtnName,
				theThresholdValueStr);
		}
		return;
	}

	const std::pair<u16, EButton> aKey(theLayerIdx, aBtnID);
	const u8 aThreshold = clamp(intFromString(theThresholdValueStr),
		0, 100) * 255 / 100;

	sButtonThresholds.setValue(aKey, aThreshold);
	mapDebugPrint("[%s]: '%s' analog-to-digital press threshold set to: %s%%\n",
		theBuilder.debugItemName.c_str(),
		kProfileButtonName[aBtnID],
		theThresholdValueStr.c_str());
}


static void buildControlsLayer(InputMapBuilder& theBuilder, u16 theLayerIdx)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	const bool isComboLayer =
		sLayers[theLayerIdx].parentLayer == kComboParentLayer;
	sLayers[theLayerIdx].parentLayer = 0;

	// If has an includeLayer, get default settings from it first
	const u16 anIncludeLayer = sLayers[theLayerIdx].includeLayer;
	if( anIncludeLayer != 0 )
	{
		sLayers[theLayerIdx].mouseMode =
			sLayers[anIncludeLayer].mouseMode;
		sLayers[theLayerIdx].enableHotspots =
			sLayers[anIncludeLayer].enableHotspots;
		sLayers[theLayerIdx].disableHotspots =
			sLayers[anIncludeLayer].disableHotspots;
		sLayers[theLayerIdx].parentLayer =
			sLayers[anIncludeLayer].parentLayer;
	}

	// Make local copy of name string since sLayers can reallocate memory here
	const std::string aLayerName = sLayers[theLayerIdx].label;
	theBuilder.debugItemName.clear();
	if( theLayerIdx != 0 )
	{
		theBuilder.debugItemName = kLayerPrefix;
		mapDebugPrint("Building controls layer: %s\n", aLayerName.c_str());
		if( anIncludeLayer != 0 )
		{
			mapDebugPrint("[%s%s]: Including all data from layer '%s'\n",
				kLayerPrefix,
				aLayerName.c_str(),
				sLayers[anIncludeLayer].label.c_str());
		}
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
			sLayers[theLayerIdx].mouseMode == eMouseMode_Look ? "Mouse Look" :
			sLayers[theLayerIdx].mouseMode == eMouseMode_Hide ? "Hidden" :
			/*otherwise*/ "Hidden OR Mouse Look" );
	}

	{// Get hotspot arrays setting directly
		const std::string& aHotspotsStr =
			Profile::getStr(aLayerPrefix + kHotspotArraysKey);
		if( !aHotspotsStr.empty() )
			buildHotspotArraysForLayer(theBuilder, theLayerIdx, aHotspotsStr);
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

	// Check each key-value pair for button assignment requests
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
		const std::string aKey = itr->first;
		if( aKey == kIncludeKey ||
			aKey == kParentLayerKey ||
			aKey == kMouseModeKey ||
			aKey == kHUDSettingsKey ||
			aKey == kHotspotArraysKey )
			continue;

		static const size_t kThresholdKeyLen = strlen(kThresholdSuffix);
		if( aKey.size() > kThresholdKeyLen &&
			aKey.compare(aKey.size() - kThresholdKeyLen,
				kThresholdKeyLen, kThresholdSuffix) == 0 )
		{// Set custom analog-to-digital threshold
			addButtonThreshold(
				theBuilder, theLayerIdx,
				aKey.substr(0, aKey.size()-kThresholdKeyLen),
				itr->second);
			continue;
		}

		// Parse and add assignment to this layer's commands map
		addButtonAction(theBuilder, theLayerIdx, aKey, itr->second);
	}
	theBuilder.keyValueList.clear();

	// Do final cleanup and processing of overall commands map
	// (sLayers shouldn't grow any past this point so safe to use const ref's)
	ButtonActionsMap& aMap = sLayers[theLayerIdx].map;
	for(size_t i = 0; i < aMap.size(); ++i)
	{
		ButtonActions& aBtnActions = aMap[i].second;

		// If base "Down" action is set to "unassigned", treat all actions
		// set to _Empty as also "unassigned", meaning they will not copy over
		// commands from their include layer.
		if( aBtnActions.cmd[eBtnAct_Down].type == eCmdType_Unassigned )
		{
			for(int aBtnAct = 1; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				if( aBtnActions.cmd[aBtnAct].type == eCmdType_Empty )
					aBtnActions.cmd[aBtnAct].type = eCmdType_Unassigned;
			}
		}

		// If included another layer, copy commands from include layer into any
		// _Empty actions for any buttons that are in the map (meaning they had
		// at least one action assigned to something). Buttons that had no new
		// assignments at all (aren't in the map) don't need to do this because
		// the include layer's copy will be directly returned instead.
		if( anIncludeLayer != 0 )
		{
			if( const Command* incCommands =
				commandsForButton(anIncludeLayer, aMap[i].first) )
			{
				for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
				{
					if( aBtnActions.cmd[aBtnAct].type != eCmdType_Empty )
						continue;
					if( incCommands[aBtnAct].type == eCmdType_Empty )
						continue;
					aBtnActions.cmd[aBtnAct] = incCommands[aBtnAct];
				}
			}
		}
	}
	aMap.trim();

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
			std::string aComboLayerName = itr->first;
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


static void linkComboControlsLayers()
{
	// Link any derived layers (ones using .includeLayer) to also activate
	// any combo layers that the included layer activates
	for(u16 aLayerIdx = 2; aLayerIdx < sLayers.size(); ++aLayerIdx)
	{
		const u16 anIncludeLayerIdx = sLayers[aLayerIdx].includeLayer;
		if( anIncludeLayerIdx == 0 )
			continue;
		for(u16 aCLI = 0; aCLI < sComboLayers.size(); ++aCLI)
		{
			const u16 aComboBase1 = sComboLayers[aCLI].first.first;
			const u16 aComboBase2 = sComboLayers[aCLI].first.second;
			const u16 aComboLayer = sComboLayers[aCLI].second;
			if( aComboBase1 == anIncludeLayerIdx )
			{
				std::pair<u16, u16> aComboLayerKey;
				aComboLayerKey.first = aLayerIdx;
				aComboLayerKey.second = aComboBase2;
				sComboLayers.findOrAdd(aComboLayerKey, aComboLayer);
			}
			else if( aComboBase2 == anIncludeLayerIdx )
			{
				std::pair<u16, u16> aComboLayerKey;
				aComboLayerKey.first = aComboBase1;
				aComboLayerKey.second = aLayerIdx;
				sComboLayers.findOrAdd(aComboLayerKey, aComboLayer);
			}
		}
	}
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
	{// Go back one sub-menu or close menu
		aMenuItem.cmd.type = eCmdType_MenuBackOrClose;
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

	switch(aMenuItem.cmd.type)
	{
	case eCmdType_SlashCommand:
		mapDebugPrint("%s: '%s' assigned to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str(), theString.c_str());
		break;
	case eCmdType_SayString:
		mapDebugPrint("%s: '%s' assigned to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str(), theString.c_str() + 1);
		break;
	case eCmdType_TapKey:
		mapDebugPrint("%s: '%s' assigned to: %s (%s%s%s%s%s)\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str(),
			theString.c_str(),
			!!(aMenuItem.cmd.vKey & kVKeyShiftFlag) ? "Shift+" : "",
			!!(aMenuItem.cmd.vKey & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(aMenuItem.cmd.vKey & kVKeyAltFlag) ? "Alt+" : "",
			!!(aMenuItem.cmd.vKey & kVKeyWinFlag) ? "Win+" : "",
			virtualKeyToName(aMenuItem.cmd.vKey & kVKeyMask).c_str());
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("%s: '%s' assigned to sequence: %s\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str(), theString.c_str());
		break;
	case eCmdType_DoNothing:
	case eCmdType_Unassigned:
		mapDebugPrint("%s: '%s' left <unassigned>!\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str());
		break;
	case eCmdType_Empty:
		// Probably just forgot the > at front of a plain string
		sKeyStrings.push_back(std::string(">") + theString);
		aMenuItem.cmd.type = eCmdType_SayString;
		aMenuItem.cmd.keyStringIdx = u16(sKeyStrings.size()-1);
		logError("%s: '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box string. "
				 "Add > to start of it if this was the intent!",
				theBuilder.debugItemName.c_str(),
		aLabel.c_str(), theString.c_str());
		break;
	default:
		mapDebugPrint("%s: '%s' assigned to command: %s\n",
			theBuilder.debugItemName.c_str(),
			aLabel.c_str(), theString.c_str());
		break;
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
		const std::string anOnOpenCmd =
			Profile::getStr(aPrefix + "/" + kMenuOpenKey);
		if( !anOnOpenCmd.empty() )
		{
			theBuilder.debugItemName =
				aDebugNamePrefix + kMenuOpenKey + ")";
			const Command& aCmd =
				stringToCommand(theBuilder, anOnOpenCmd);
			if( aCmd.type == eCmdType_Empty ||
				(aCmd.type >= eCmdType_FirstMenuControl &&
				 aCmd.type <= eCmdType_LastMenuControl) )
			{
				logError("%s: Invalid command '%s'!\n",
					theBuilder.debugItemName.c_str(),
					anOnOpenCmd.c_str());
				sMenus[aMenuID].autoCommand = Command();
			}
			else
			{
				sMenus[aMenuID].autoCommand = aCmd;
				mapDebugPrint("%s: Assigned to command: %s\n",
					theBuilder.debugItemName.c_str(),
					anOnOpenCmd.c_str());
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
	{// Use include layer's settings, if have one
		if( sLayers[theLayerID].includeLayer > 0 )
		{
			buildHUDElementsForLayer(
				theBuilder, sLayers[theLayerID].includeLayer);
			sLayers[theLayerID].showHUD =
				sLayers[sLayers[theLayerID].includeLayer].showHUD;
			sLayers[theLayerID].hideHUD =
				sLayers[sLayers[theLayerID].includeLayer].hideHUD;
		}
		return;
	}

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


static void assignSpecialKeys(InputMapBuilder& theBuilder)
{
	for(size_t i = 0; i < eSpecialKey_Num; ++i)
	{
		DBG_ASSERT(sSpecialKeys[i] == 0);
		Command* aKeyBindCommand =
			theBuilder.commandAliases.find(kSpecialKeyNames[i]);
		if( !aKeyBindCommand || aKeyBindCommand->type == eCmdType_DoNothing )
			continue;
		if( aKeyBindCommand->type != eCmdType_TapKey )
		{
			logError("Can not assign a full key sequence to %s! "
				"Please assign only a single key!",
				kSpecialKeyNames[i]);
			continue;
		}
		sSpecialKeys[i] = aKeyBindCommand->vKey;
	}

	// Have some special keys borrow the value of others if left unassigned
	if( sSpecialKeys[eSpecialKey_StrafeL] == 0 )
		sSpecialKeys[eSpecialKey_StrafeL] = sSpecialKeys[eSpecialKey_TurnL];
	if( sSpecialKeys[eSpecialKey_StrafeR] == 0 )
		sSpecialKeys[eSpecialKey_StrafeR] = sSpecialKeys[eSpecialKey_TurnR];
	if( sSpecialKeys[eSpecialKey_TurnL] == 0 )
		sSpecialKeys[eSpecialKey_TurnL] = sSpecialKeys[eSpecialKey_StrafeL];
	if( sSpecialKeys[eSpecialKey_TurnR] == 0 )
		sSpecialKeys[eSpecialKey_TurnR] = sSpecialKeys[eSpecialKey_StrafeR];
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
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
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
	sButtonThresholds.clear();
	sButtonHoldTimes.clear();

	// Get default button hold time to execute eBtnAct_Hold command
	sDefaultButtonHoldTime =
		clamp(Profile::getInt("System/ButtonHoldTime",
			sDefaultButtonHoldTime), 0, 0xFFFF);

	// Create temp builder object and build everything from the Profile data
	{
		InputMapBuilder anInputMapBuilder;
		buildHotspots(anInputMapBuilder);
		buildCommandAliases(anInputMapBuilder);
		buildControlScheme(anInputMapBuilder);
		buildMenus(anInputMapBuilder);
		buildHUDElements(anInputMapBuilder);
		linkComboControlsLayers();
		assignSpecialKeys(anInputMapBuilder);
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
	sButtonThresholds.trim();
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
		for(ButtonActionsMap::iterator itr2 = itr->map.begin();
			itr2 != itr->map.end(); ++itr2)
		{
			for(size_t i = 0; i < eBtnAct_Num; ++i)
				setCStringPointerFor(&itr2->second.cmd[i]);
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

	ButtonActionsMap::const_iterator itr;
	do {
		itr = sLayers[theLayerID].map.find(theButton);
		if( itr != sLayers[theLayerID].map.end() )
		{// Button has something assigned
			return &itr->second.cmd[0];
		}
		else
		{// Check if included layer has this button assigned
			theLayerID = sLayers[theLayerID].includeLayer;
		}
	} while(theLayerID != 0);

	return null;
}


u16 commandHoldTime(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);
	std::pair<u16, EButton> aKey;
	aKey.second = theButton;
	VectorMap<std::pair<u16, EButton>, u16>::const_iterator itr;
	do {
		aKey.first = theLayerID;
		itr = sButtonHoldTimes.find(aKey);
		if( itr != sButtonHoldTimes.end() )
		{// Button has a custom hold time assigned
			return itr->second;
		}
		else
		{// Check if included layer has a custom hold time assigned
			theLayerID = sLayers[theLayerID].includeLayer;
		}
	} while(theLayerID != 0);

	return sDefaultButtonHoldTime;
}


const u8* commandThreshold(u16 theLayerID, EButton theButton)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	DBG_ASSERT(theButton < eBtn_Num);
	std::pair<u16, EButton> aKey;
	aKey.second = theButton;
	VectorMap<std::pair<u16, EButton>, u8>::const_iterator itr;
	do {
		aKey.first = theLayerID;
		itr = sButtonThresholds.find(aKey);
		if( itr != sButtonThresholds.end() )
		{// Button has a custom threshold assigned
			return &itr->second;
		}
		else
		{// Check if included layer has a custom threshold assigned
			theLayerID = sLayers[theLayerID].includeLayer;
		}
	} while(theLayerID != 0);

	return null;
}


u16 parentLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].parentLayer;
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


u16 keyBindArrayForHUDElement(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
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

} // InputMap
