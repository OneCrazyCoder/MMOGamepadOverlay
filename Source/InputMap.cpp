//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputMap.h"

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

const u16 kInvalidID = 0xFFFF;
const char* kMainLayerLabel = "Scheme";
const char* kLayerPrefix = "Layer.";
const char* kMenuPrefix = "Menu.";
const char* kHUDPrefix = "HUD.";
const char* kTypeKeys[] = { "Type", "Style" };
const char* kKeybindsPrefix = "KeyBinds/";
const char* kGlobalHotspotsPrefix = "Hotspots/";
const char* k4DirMenuItemLabel[] = { "L", "R", "U", "D" }; // match ECommandDir!
DBG_CTASSERT(ARRAYSIZE(k4DirMenuItemLabel) == eCmdDir_Num);

// These need to be in all upper case
const char* kIncludeKey = "INCLUDE";
const char* kHUDSettingsKey = "HUD";
const char* kMouseLookKey = "MOUSELOOK";
const std::string k4DirButtons[] =
{	"LS", "LSTICK", "LEFTSTICK", "LEFT STICK", "DPAD",
	"RS", "RSTICK", "RIGHTSTICK", "RIGHT STICK", "FPAD" };

const char* kSpecialHotspotNames[] =
{
	"",						// eSpecialHotspot_None
	"MOUSELOOKSTART",		// eSpecialHotspot_MouseLookStart
	"TARGETSELF",			// eSpecialHotspot_TargetSelf
	"TARGETGROUP1",			// eSpecialHotspot_TargetGroup1
	"TARGETGROUP2",			// eSpecialHotspot_TargetGroup2
	"TARGETGROUP3",			// eSpecialHotspot_TargetGroup3
	"TARGETGROUP4",			// eSpecialHotspot_TargetGroup4
	"TARGETGROUP5",			// eSpecialHotspot_TargetGroup5
	"TARGETGROUP6",			// eSpecialHotspot_TargetGroup6
	"TARGETGROUP7",			// eSpecialHotspot_TargetGroup7
	"TARGETGROUP8",			// eSpecialHotspot_TargetGroup8
	"TARGETGROUP9",			// eSpecialHotspot_TargetGroup9
};
DBG_CTASSERT(ARRAYSIZE(kSpecialHotspotNames) == eSpecialHotspot_Num);

const char* kButtonActionPrefx[] =
{
	"",						// eBtnAct_Down
	"Press",				// eBtnAct_Press
	"Hold",					// eBtnAct_ShortHold
	"LongHold",				// eBtnAct_LongHold
	"Tap",					// eBtnAct_Tap
	"Release",				// eBtnAct_Release
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
	"MOUSELOOKMOVEFORWARD",	// eSpecialKey_MLMoveF
	"MOUSELOOKMOVEBACK",	// eSpecialKey_MLMoveB
	"MOUSELOOKTURNLEFT",	// eSpecialKey_MLTurnL
	"MOUSELOOKTURNRIGHT",	// eSpecialKey_MLTurnR
	"MOUSELOOKSTRAFELEFT",	// eSpecialKey_MLStrafeL
	"MOUSELOOKSTRAFERIGHT",	// eSpecialKey_MLStrafeR
	"TARGETSELF",			// eSpecialKey_TargetSelf
	"TARGETGROUP1",			// eSpecialKey_TargetGroup1
	"TARGETGROUP2",			// eSpecialKey_TargetGroup2
	"TARGETGROUP3",			// eSpecialKey_TargetGroup3
	"TARGETGROUP4",			// eSpecialKey_TargetGroup4
	"TARGETGROUP5",			// eSpecialKey_TargetGroup5
	"TARGETGROUP6",			// eSpecialKey_TargetGroup6
	"TARGETGROUP7",			// eSpecialKey_TargetGroup7
	"TARGETGROUP8",			// eSpecialKey_TargetGroup8
	"TARGETGROUP9",			// eSpecialKey_TargetGroup9
};
DBG_CTASSERT(ARRAYSIZE(kSpecialKeyNames) == eSpecialKey_Num);


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct MenuItem
{
	std::string label;
	Command cmd;
};

struct Menu
{
	std::string label;
	std::vector<MenuItem> items;
	MenuItem dirItems[eCmdDir_Num];
	u16 parentMenuID;
	u16 rootMenuID;
	u16 hudElementID;

	Menu() : rootMenuID(kInvalidID), hudElementID(kInvalidID) {}
};

struct HUDElement
{
	std::string label;
	EHUDType type : 16;
	u16 menuID;
	// Visual details will be parsed by HUD module

	HUDElement() : menuID(kInvalidID) { type = eHUDItemType_Rect; }
};

struct ButtonActions
{
	std::string label[eBtnAct_Num];
	Command cmd[eBtnAct_Num];
};
typedef VectorMap<EButton, ButtonActions> ButtonActionsMap;

struct ControlsLayer
{
	std::string label;
	ButtonActionsMap map;
	u16 includeLayer;
	BitVector<> showHUD;
	BitVector<> hideHUD;
	bool mouseLookOn;

	ControlsLayer() :
		includeLayer(),
		showHUD(),
		hideHUD(),
		mouseLookOn()
	{}
};

// Data used during parsing/building the map but deleted once done
struct InputMapBuilder
{
	std::vector<std::string> parsedString;
	VectorMap<ECommandKeyWord, size_t> keyWordMap;
	StringToValueMap<Command> commandAliases;
	StringToValueMap<u16> hotspotNameToIdxMap;
	StringToValueMap<u16> layerNameToIdxMap;
	StringToValueMap<u16> hudNameToIdxMap;
	StringToValueMap<u16> menuPathToIdxMap;
	std::string debugItemName;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<Hotspot> sHotspots;
static std::vector<std::string> sKeyStrings;
static std::vector<ControlsLayer> sLayers;
static std::vector<Menu> sMenus;
static u16 sRootMenuCount = 0;
static std::vector<HUDElement> sHUDElements;
static u16 sSpecialKeys[eSpecialKey_Num];
static u8 sTargetGroupSize = 1;


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
	if( theVKeySeq == null )
		return result;
	if( theVKeySeq[0] == '\0' )
		return result;
	if( theVKeySeq[1] == '\0' )
	{
		result = theVKeySeq[0];
		return result;
	}

	bool hasNonModKey = false;
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

	return result;
}


static u16 getOrCreateLayerID(
	InputMapBuilder& theBuilder,
	const std::string& theLayerName,
	std::vector<std::string>& theChildList = std::vector<std::string>())
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
		// Check for infinite recursion of include= statements
		std::vector<std::string>::iterator itr = std::find(
			theChildList.begin(),
			theChildList.end(),
			upper(anIncludeName));
		if( itr != theChildList.end() )
		{
			logError("Infinite include loop with layer [%s%s]"
				" trying to include layer %s",
				kLayerPrefix, theLayerName.c_str(), itr->c_str());
		}
		else
		{
			theChildList.push_back(aLayerKeyName);
			anIncludeIdx =
				getOrCreateLayerID(theBuilder, anIncludeName, theChildList);
		}
	}

	// Add new layer to sLayers and the name-to-index map
	theBuilder.layerNameToIdxMap.setValue(aLayerKeyName, u16(sLayers.size()));
	sLayers.push_back(ControlsLayer());
	sLayers.back().label = theLayerName;
	sLayers.back().includeLayer = anIncludeIdx;

	return u16(sLayers.size() - 1);
}


static u16 getOrCreateHUDElementID(
	InputMapBuilder& theBuilder,
	const std::string& theName,
	u16 theControlsLayerIndex = 0,
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
	aHUDElement.label = theName;

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
			"Can't find '[%s%s]/%s =' entry "
			"for item referenced by Layer '%s'! "
			"Defaulting to type '%s'...",
			hasInputAssigned ? kMenuPrefix : kHUDPrefix,
			theName.c_str(),
			kTypeKeys[hasInputAssigned ? 1 : 0],
			sLayers[theControlsLayerIndex].label.c_str(),
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
				"Unrecognized '%s' specified for '[%s%s]/%s ='entry "
				"for item referenced by Layer '%s'! "
				"Defaulting to type '%s'...",
				aHUDTypeName.c_str(),
				hasInputAssigned ? kMenuPrefix : kHUDPrefix,
				theName.c_str(),
				kTypeKeys[hasInputAssigned ? 1 : 0],
				sLayers[theControlsLayerIndex].label.c_str(),
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
				"HUD Element %s assigned to a Menu Style but has no "
				"buttons assigned to navigate the menu!",
				theName.c_str());
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

	return aHUDElementID;
}


static u16 getOrCreateRootMenuID(
	InputMapBuilder& theBuilder,
	const std::string& theMenuName,
	u16 theControlsLayerIndex = 0)
{
	DBG_ASSERT(!theMenuName.empty());

	// Root menus are inherently HUD elements, so start with that
	u16 aHUDElementID = getOrCreateHUDElementID(
		theBuilder, theMenuName, theControlsLayerIndex, true);
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
	u16 theControlsLayerIndex = 0,
	bool allowButtonActions = false,
	bool allowHoldActions = false)
{
	// Can't allow hold actions if don't also allow button actions
	DBG_ASSERT(!allowHoldActions || allowButtonActions);
	Command result;

	// All commands require more than one "word", even if only one of the
	// words is actually a command key word (thus can force a keybind to be
	// used instead of a command by specifying the keybind as a single word)
	if( theWords.size() <= 1 )
		return result;

	// Find all key words that are actually included and their positions
	theBuilder.keyWordMap.clear();
	BitArray<eCmdWord_Num> keyWordsFound = { 0 };
	for(size_t i = 0; i < theWords.size(); ++i)
	{
		ECommandKeyWord aKeyWordID = commandWordToID(upper(theWords[i]));
		if( aKeyWordID == eCmdWord_Filler )
			continue;
		// The same key word (including "unknown") can't be used twice
		if( keyWordsFound.test(aKeyWordID) )
			return result;
		keyWordsFound.set(aKeyWordID);
		theBuilder.keyWordMap.addPair(aKeyWordID, i);
	}
	if( theBuilder.keyWordMap.empty() )
		return result;
	theBuilder.keyWordMap.sort();

	// Find a command by checking for specific key words + allowed related
	// words and none more than that
	BitArray<eCmdWord_Num> allowedKeyWords;

	// "= [Change] Profile"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Change);
	allowedKeyWords.set(eCmdWord_Profile);
	if( keyWordsFound.test(eCmdWord_Profile) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_ChangeProfile;
		return result;
	}

	// "= Quit [App]"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Quit);
	allowedKeyWords.set(eCmdWord_App);
	if( keyWordsFound.test(eCmdWord_Quit) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_QuitApp;
		return result;
	}

	// For the following Layer-related commands, most need one extra word
	// that is not a key word related to layers, which will be the name
	// of the layer (likely, but not always, the eCmdWord_Unknown entry).
	allowedKeyWords = keyWordsFound;
	allowedKeyWords.reset(eCmdWord_Layer);
	allowedKeyWords.reset(eCmdWord_Add);
	allowedKeyWords.reset(eCmdWord_Remove);
	allowedKeyWords.reset(eCmdWord_Hold);
	const std::string* aLayerName = null;
	if( allowedKeyWords.count() == 1 )
	{
		VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
			theBuilder.keyWordMap.find(
				ECommandKeyWord(allowedKeyWords.firstSetBit()));
		if( itr != theBuilder.keyWordMap.end() )
			aLayerName = &theWords[itr->second];
	}
	allowedKeyWords.reset();

	// "= Add [Layer] <aLayerName>"
	allowedKeyWords.set(eCmdWord_Layer);
	allowedKeyWords.set(eCmdWord_Add);
	if( keyWordsFound.test(eCmdWord_Add) &&
		aLayerName &&
		(keyWordsFound & ~allowedKeyWords).count() == 1 )
	{
		result.type = eCmdType_AddControlsLayer;
		result.data = getOrCreateLayerID(theBuilder, *aLayerName);
		// Need to know the parent layer of new added layer
		result.data2 = theControlsLayerIndex;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Add);

	// "= Remove [Layer] <aLayerName>"
	// allowedKeyWords = Layer
	allowedKeyWords.set(eCmdWord_Remove);
	if( keyWordsFound.test(eCmdWord_Remove) &&
		aLayerName &&
		(keyWordsFound & ~allowedKeyWords).count() == 1 )
	{
		if( u16* aLayerIdx =
				theBuilder.layerNameToIdxMap.find(upper(*aLayerName)) )
		{
			if( *aLayerIdx != 0 )
			{
				result.type = eCmdType_RemoveControlsLayer;
				result.data = *aLayerIdx;
				return result;
			}
		}
	}

	// "= Remove [Layer]"
	// allowedKeyWords = Layer
	if( keyWordsFound.test(eCmdWord_Remove) &&
		theControlsLayerIndex > 0 &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_RemoveControlsLayer;
		result.data = theControlsLayerIndex;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Remove);

	// "= 'Hold'|'Layer'|'Hold Layer' <aLayerName>"
	// allowedKeyWords = Layer
	allowedKeyWords.set(eCmdWord_Hold);
	if( allowHoldActions &&
		(keyWordsFound.test(eCmdWord_Hold) ||
		 keyWordsFound.test(eCmdWord_Layer)) &&
		aLayerName &&
		(keyWordsFound & ~allowedKeyWords).count() == 1 )
	{
		result.type = eCmdType_HoldControlsLayer;
		result.data = getOrCreateLayerID(theBuilder, *aLayerName);
		return result;
	}

	// Same deal here for the Menu-related commands needing a name of the
	// menu in question as the one otherwise-unrelated word.
	const std::string* aMenuName = null;
	if( allowButtonActions )
	{
		allowedKeyWords = keyWordsFound;
		allowedKeyWords.reset(eCmdWord_Menu);
		allowedKeyWords.reset(eCmdWord_Reset);
		allowedKeyWords.reset(eCmdWord_Select);
		allowedKeyWords.reset(eCmdWord_Confirm);
		allowedKeyWords.reset(eCmdWord_Back);
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Edit);
		allowedKeyWords.reset(eCmdWord_Left);
		allowedKeyWords.reset(eCmdWord_Right);
		allowedKeyWords.reset(eCmdWord_Up);
		allowedKeyWords.reset(eCmdWord_Down);
		if( allowedKeyWords.count() == 1 )
		{
			VectorMap<ECommandKeyWord, size_t>::const_iterator itr =
				theBuilder.keyWordMap.find(
					ECommandKeyWord(allowedKeyWords.firstSetBit()));
			if( itr != theBuilder.keyWordMap.end() )
				aMenuName = &theWords[itr->second];
		}

		// "= Reset <aMenuName> [Menu]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Menu);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuReset;
			result.data = getOrCreateRootMenuID(
				theBuilder, *aMenuName, theControlsLayerIndex);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Reset);

		// "= Confirm <aMenuName> [Menu]
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Confirm);
		if( keyWordsFound.test(eCmdWord_Confirm) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuConfirm;
			result.data = getOrCreateRootMenuID(
				theBuilder, *aMenuName, theControlsLayerIndex);
			return result;
		}

		// "= Confirm <aMenuName> [Menu] and Close
		// allowedKeyWords = Menu & Confirm
		allowedKeyWords.set(eCmdWord_Close);
		if( keyWordsFound.test(eCmdWord_Confirm) &&
			keyWordsFound.test(eCmdWord_Close) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuConfirmAndClose;
			result.data = getOrCreateRootMenuID(
				theBuilder, *aMenuName, theControlsLayerIndex);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Close);
		allowedKeyWords.reset(eCmdWord_Confirm);

		// "= [Menu] <aMenuName> Back
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Back);
		if( keyWordsFound.test(eCmdWord_Back) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuBack;
			result.data = getOrCreateRootMenuID(
				theBuilder, *aMenuName, theControlsLayerIndex);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Back);

		// "= Edit <aMenuName> [Menu]
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Edit);
		if( keyWordsFound.test(eCmdWord_Edit) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuEdit;
			result.data = getOrCreateRootMenuID(
				theBuilder, *aMenuName, theControlsLayerIndex);
			return result;
		}
	}

	// "= Target Group <eTargetGroupType>"
	if( keyWordsFound.test(eCmdWord_Target) &&
		keyWordsFound.test(eCmdWord_Group) )
	{
		result.type = eCmdType_TargetGroup;

		// "= Target Group Reset [Last]"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Target);
		allowedKeyWords.set(eCmdWord_Group);
		allowedKeyWords.set(eCmdWord_Reset);
		allowedKeyWords.set(eCmdWord_Last);
		if( keyWordsFound.test(eCmdWord_Reset) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_Reset;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Reset);
		allowedKeyWords.reset(eCmdWord_Last);
		// "= Target Group [Load] [Default]"
		// allowedKeyWords = Target & Group
		allowedKeyWords.set(eCmdWord_Load);
		allowedKeyWords.set(eCmdWord_Default);
		if( (keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_Default;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Load);
		// "= Target Group 'Set [Default]'|'Left'"
		// allowedKeyWords = Target & Group & Default
		allowedKeyWords.set(eCmdWord_Set);
		allowedKeyWords.set(eCmdWord_Left);
		allowedKeyWords.set(eCmdWord_Wrap);
		allowedKeyWords.set(eCmdWord_NoWrap);
		if( (keyWordsFound.test(eCmdWord_Set) ||
			 keyWordsFound.test(eCmdWord_Left)) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_SetDefault;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Set);
		allowedKeyWords.reset(eCmdWord_Left);
		allowedKeyWords.reset(eCmdWord_Default);
		// "= Target Group 'Last'|'Pet'|'Right'"
		// allowedKeyWords = Target & Group & Wrap & NoWrap
		allowedKeyWords.set(eCmdWord_Last);
		allowedKeyWords.set(eCmdWord_Pet);
		allowedKeyWords.set(eCmdWord_Right);
		if( (keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_Last;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Last);
		allowedKeyWords.reset(eCmdWord_Pet);
		allowedKeyWords.reset(eCmdWord_Right);
		// "= Target Group 'Prev'|'Up'|'PrevNoWrap' [NoWrap]"
		// allowedKeyWords = Target & Group & Wrap & NoWrap
		allowedKeyWords.reset(eCmdWord_Wrap);
		allowedKeyWords.set(eCmdWord_Prev);
		allowedKeyWords.set(eCmdWord_PrevNoWrap);
		if( (keyWordsFound.test(eCmdWord_Prev) ||
			 keyWordsFound.test(eCmdWord_PrevNoWrap)) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_Prev;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Prev);
		allowedKeyWords.reset(eCmdWord_PrevNoWrap);
		// "= Target Group 'Next'|'Down'|'NextNoWrap' [NoWrap]"
		// allowedKeyWords = Target & Group & NoWrap
		allowedKeyWords.set(eCmdWord_Next);
		allowedKeyWords.set(eCmdWord_NextNoWrap);
		if( (keyWordsFound.test(eCmdWord_Next) ||
			 keyWordsFound.test(eCmdWord_NextNoWrap)) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_Next;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Next);
		allowedKeyWords.reset(eCmdWord_NextNoWrap);
		allowedKeyWords.reset(eCmdWord_NoWrap);
		// "= Target Group 'Prev Wrap'|'Up Wrap'|'PrevWrap'|'UpWrap'"
		// allowedKeyWords = Target & Group
		allowedKeyWords.set(eCmdWord_Wrap);
		allowedKeyWords.set(eCmdWord_Prev);
		allowedKeyWords.set(eCmdWord_PrevWrap);
		if( (keyWordsFound.test(eCmdWord_PrevWrap) ||
			 (keyWordsFound.test(eCmdWord_Prev) &&
			  keyWordsFound.test(eCmdWord_Wrap))) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_PrevWrap;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Prev);
		allowedKeyWords.reset(eCmdWord_PrevWrap);
		// "= Target Group 'Next Wrap'|'Down Wrap'|'NextWrap'|'DownWrap'"
		// allowedKeyWords = Target & Group & Wrap
		allowedKeyWords.set(eCmdWord_Next);
		allowedKeyWords.set(eCmdWord_NextWrap);
		if( (keyWordsFound.test(eCmdWord_NextWrap) ||
			 (keyWordsFound.test(eCmdWord_Next) &&
			  keyWordsFound.test(eCmdWord_Wrap))) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_NextWrap;
			return result;
		}
		result.type = eCmdType_Empty;
	}

	// Get ECmdDir from key words for remaining commands
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
	result.data = aCmdDir;
	// Remove direction-related bits from keyWordsFound
	keyWordsFound &= ~allowedKeyWords;

	if( allowButtonActions )
	{
		// "= 'Select'|'Menu'|'Select Menu' <aMenuName> <aCmdDir>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Menu);
		if( (keyWordsFound.test(eCmdWord_Select) ||
			 keyWordsFound.test(eCmdWord_Menu)) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuSelect;
			result.data = aCmdDir;
			result.data2 = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}

		// "= 'Select'|'Menu'|'Select Menu' <aMenuName> <aCmdDir> and Close"
		// allowedKeyWords = Menu & Select
		allowedKeyWords.set(eCmdWord_Close);
		if( (keyWordsFound.test(eCmdWord_Select) ||
			 keyWordsFound.test(eCmdWord_Menu)) &&
			keyWordsFound.test(eCmdWord_Close) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuSelectAndClose;
			result.data = aCmdDir;
			result.data2 = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Select);
		allowedKeyWords.reset(eCmdWord_Close);

		// "= Edit [Menu] <aMenuName> <aCmdDir>"
		// allowedKeyWords = Menu
		allowedKeyWords.set(eCmdWord_Edit);
		if( keyWordsFound.test(eCmdWord_Edit) &&
			aMenuName &&
			(keyWordsFound & ~allowedKeyWords).count() == 1 )
		{
			result.type = eCmdType_MenuEditDir;
			result.data = aCmdDir;
			result.data2 = getOrCreateRootMenuID(theBuilder, *aMenuName);
			return result;
		}

		// "= [Select] Hotspot <aCmdDir>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Select);
		allowedKeyWords.set(eCmdWord_Hotspot);
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

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Smooth] <aCmdDir>"
		// allowedKeyWords = Move & Mouse
		allowedKeyWords.set(eCmdWord_MouseWheel);
		allowedKeyWords.set(eCmdWord_Smooth);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.data2 = eMouseWheelMotion_Smooth;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Smooth);

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' Step[ped] <aCmdDir>"
		// allowedKeyWords = Move & Mouse & Wheel
		allowedKeyWords.set(eCmdWord_Stepped);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			keyWordsFound.test(eCmdWord_Stepped) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.data2 = eMouseWheelMotion_Stepped;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Stepped);

		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' <aCmdDir> Once"
		// allowedKeyWords = Move & Mouse & Wheel
		allowedKeyWords.set(eCmdWord_Once);
		if( keyWordsFound.test(eCmdWord_MouseWheel) &&
			keyWordsFound.test(eCmdWord_Stepped) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.data2 = eMouseWheelMotion_Once;
			return result;
		}
	}
	else if( allowButtonActions )
	{
		// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Once] <aCmdDir>"
		allowedKeyWords.reset();
		allowedKeyWords.set(eCmdWord_Move);
		allowedKeyWords.set(eCmdWord_Mouse);
		allowedKeyWords.set(eCmdWord_MouseWheel);
		allowedKeyWords.set(eCmdWord_Once);
		if( allowButtonActions &&
			keyWordsFound.test(eCmdWord_MouseWheel) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.type = eCmdType_MouseWheel;
			result.data2 = eMouseWheelMotion_Once;
			return result;
		}
	}

	DBG_ASSERT(result.type == eCmdType_Empty);
	result.data = 0;
	return result;
}


static Command stringToCommand(
	InputMapBuilder& theBuilder,
	const std::string& theString,
	u16 theControlsLayerIndex = 0,
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
		result.data = u16(sKeyStrings.size()-1);
		return result;
	}
	if( theString[0] == '>' )
	{
		sKeyStrings.push_back(theString);
		result.type = eCmdType_SayString;
		result.data = u16(sKeyStrings.size()-1);
		return result;
	}

	// Check for special command
	theBuilder.parsedString.clear();
	sanitizeSentence(theString, theBuilder.parsedString);
	result = wordsToSpecialCommand(
		theBuilder,
		theBuilder.parsedString,
		theControlsLayerIndex,
		allowButtonActions,
		allowHoldActions);

	// Check for alias to a keybind
	if( result.type == eCmdType_Empty )
	{
		if( Command* aKeyBindCommand =
				theBuilder.commandAliases.find(condense(theString)) )
		{
			result = *aKeyBindCommand;
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
				result.data = aVKey;
			}
			else
			{
				result.type = eCmdType_VKeySequence;
				result.data = u16(sKeyStrings.size());
				sKeyStrings.push_back(aVKeySeq);
			}
		}
	}

	return result;
}


static EResult stringToHotspotCoord(
	std::string& theString,
	Hotspot::Coord& out,
	bool allowCommasInIntegers)
{
	// This function also removes the coordinate from start of string
	out = Hotspot::Coord();
	if( theString.empty() )
		return eResult_Empty;

	enum EMode
	{
		eMode_Prefix,		// Checking for C/R/B in CX+10, R-8, B - 5, etc
		eMode_Numerator,	// Checking for 50%, 10. in 10.5%, 0. in 0.75, etc
		eMode_Denominator,	// Checking for 5 in 0.5, 5% in 10.5%, etc
		eMode_OffsetSign,	// Checking for -/+ in 50%+10, R-8, B - 5, etc
		eMode_OffsetSpace,	// Optional space between -/+ and offset number
		eMode_OffsetNumber, // Checking for 10 in 50% + 10, CX+10, R-10, etc
	} aMode = eMode_Prefix;

	u32 aNumerator = 0;
	u32 aDenominator = 0;
	u32 anOffset = 0;
	bool done = false;
	bool isOffsetNegative  = false;
	size_t aCharPos = 0;
	char c = theString[aCharPos];

	while(!done)
	{
		switch(c)
		{
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			switch(aMode)
			{
			case eMode_Prefix:
				aMode = eMode_Numerator;
				// fall through
			case eMode_Numerator:
			case eMode_Denominator:
				aDenominator *= 10;
				aNumerator *= 10;
				aNumerator += u32(c - '0');
				if( aNumerator > 0x7FFF )
					return eResult_Overflow;
				if( aDenominator > 0x7FFF )
					return eResult_Overflow;
				break;
			case eMode_OffsetSign:
				// Assume part of next coordinate
				done = true;
				break;
			case eMode_OffsetSpace:
				aMode = eMode_OffsetNumber;
				// fall through
			case eMode_OffsetNumber:
				anOffset *= 10;
				anOffset += u32(c - '0');
				if( anOffset > 0x7FFF )
					return eResult_Overflow;
				break;
			}
			break;
		case '-':
		case '+':
			switch(aMode)
			{
			case eMode_Prefix:
				// Skipping directly to offset
				aDenominator = 1;
				// fall through
			case eMode_Denominator:
			case eMode_OffsetSign:
				isOffsetNegative = (c == '-');
				aMode = eMode_OffsetSpace;
				break;
			case eMode_Numerator:
			case eMode_OffsetSpace:
			case eMode_OffsetNumber:
				// Invalid if found in this mode
				return eResult_Malformed;
			}
			break;
		case '.':
			switch(aMode)
			{
			case eMode_Prefix:
			case eMode_Numerator:
				aMode = eMode_Denominator;
				aDenominator = 1;
				break;
			case eMode_OffsetSign:
				// Assume part of next coordinate
				done = true;
				break;
			case eMode_Denominator:
			case eMode_OffsetSpace:
			case eMode_OffsetNumber:
				// Invalid if found in this mode
				return eResult_Malformed;
			}
			break;
		case '%':
		case 'p':
			switch(aMode)
			{
			case eMode_Prefix:
				// Ignored
				break;
			case eMode_Numerator:
			case eMode_Denominator:
				if( !aDenominator ) aDenominator = 1;
				aDenominator *= 100; // Convert 50% to 0.5
				aMode = eMode_OffsetSign;
				break;
			case eMode_OffsetSign:
			case eMode_OffsetSpace:
			case eMode_OffsetNumber:
				// Invalid if found in these modes
				return eResult_Malformed;
			}
			break;
		case 'l': case 'L': // aka "Left"
		case 't': case 'T': // aka "Top"
			switch(aMode)
			{
			case eMode_Prefix:
				aNumerator = 0;
				aDenominator = 1;
				aMode = eMode_OffsetSign;
				break;
			case eMode_Numerator:
			case eMode_Denominator:
			case eMode_OffsetSign:
			case eMode_OffsetSpace:
			case eMode_OffsetNumber:
				// Assume part of next coordinate
				done = true;
				break;
			}
			break;
		case 'r': case 'R': // aka "Right"
		case 'b': case 'B': // aka "Bottom"
			switch(aMode)
			{
			case eMode_Prefix:
				aNumerator = 1;
				aDenominator = 1;
				aMode = eMode_OffsetSign;
				break;
			case eMode_Numerator:
			case eMode_Denominator:
			case eMode_OffsetSign:
			case eMode_OffsetSpace:
			case eMode_OffsetNumber:
				// Assume part of next coordinate
				done = true;
				break;
			}
			break;
		case 'c': case 'C':
			switch(aMode)
			{
			case eMode_Prefix:
				aNumerator = 1;
				aDenominator = 2;
				aMode = eMode_OffsetSign;
				break;
			case eMode_Numerator:
			case eMode_Denominator:
			case eMode_OffsetSign:
			case eMode_OffsetSpace:
			case eMode_OffsetNumber:
				// Assume part of next coordinate
				done = true;
				break;
			}
			break;
		case 'x': case 'X':
		case 'y': case 'Y':
			switch(aMode)
			{
			case eMode_Prefix:
			case eMode_OffsetSign:
			case eMode_OffsetSpace:
				// Ignore (may be part of 'CX' or '# x #')
				break;
			case eMode_Numerator:
			case eMode_Denominator:
			case eMode_OffsetNumber:
				// Assume marks end of this coordinate
				done = true;
				break;
			}
			break;
		case ' ':
		case ',':
		default:
			switch(aMode)
			{
			case eMode_Prefix:
			case eMode_OffsetSpace:
			case eMode_OffsetSign:
				// Leading whitspace, ignore
				break;
			case eMode_Numerator:
			case eMode_OffsetNumber:
				// Comma may be allowed (and ignored) during whole numbers
				if( c == ',' && allowCommasInIntegers )
					break;
				// fall through
			case eMode_Denominator:
				// Assume marks end of this coordinate
				done = true;
				break;
			}
			break;
		}

		if( !done )
		{
			++aCharPos;
			if( aCharPos >= theString.size() )
				done = true;
			else
				c = theString[aCharPos];
		}
	}

	// Parsing was a success so far - now assemble the final value
	if( aDenominator == 0 )
	{
		// Origin unspecified - assume 0% and numerator is the offset
		anOffset = aNumerator;
		aNumerator = 0;
		aDenominator = 1;
	}

	if( aNumerator >= aDenominator )
		out.origin = 0xFFFF;
	else
		out.origin = u16((aNumerator * 0x10000) / aDenominator);
	out.offset = s16(anOffset);
	if( isOffsetNegative )
		out.offset = -out.offset;

	// Remove processed section from start of string
	theString = theString.substr(aCharPos);

	return eResult_Ok;
}


static EResult stringToHotspot(std::string& theString, Hotspot& out)
{
	// This function also removes the hotspot from start of string
	// in case multiple hotspots are specified by the same string
	EResult aResult = eResult_Empty;
	if( theString.empty() )
		return aResult;

	std::string backupString = theString;
	bool allowCommasInIntegers = true;
	aResult = stringToHotspotCoord(theString, out.x, true);
	if( aResult == eResult_Overflow )
	{
		// May have confused numbers separated by ',' and no space, like
		// 100,100 as a single large number. Try again treating comma
		// in a number as a breaking character.
		allowCommasInIntegers = false;
		aResult = stringToHotspotCoord(
			theString, out.x, allowCommasInIntegers);
	}
	if( aResult != eResult_Ok )
		return aResult;
	aResult = stringToHotspotCoord(theString, out.y, allowCommasInIntegers);
	if( aResult != eResult_Ok && allowCommasInIntegers )
	{
		// May need to redo both x and y with comma-breaking to work
		theString = backupString;
		out = Hotspot();
		stringToHotspotCoord(theString, out.x, false);
		aResult = stringToHotspotCoord(theString, out.y, false);
	}

	return aResult;
}


static void buildGlobalHotspots(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning global hotspots...\n");
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
	aHotspot.x.origin = 32768;
	aHotspot.y.origin = 32768;
	for(u16 i = 0; i < eSpecialHotspot_Num; ++i)
	{
		sHotspots[i] = aHotspot;
		theBuilder.hotspotNameToIdxMap.setValue(kSpecialHotspotNames[i], i);
	}

	Profile::KeyValuePairs aHotspotRequests;
	Profile::getAllKeys(kGlobalHotspotsPrefix, aHotspotRequests);
	for(size_t i = 0; i < aHotspotRequests.size(); ++i)
	{
		std::string aHotspotName = condense(aHotspotRequests[i].first);
		std::string aHotspotDescription(aHotspotRequests[i].second);

		u16& aHotspotIdx = theBuilder.hotspotNameToIdxMap.findOrAdd(
			condense(aHotspotName), u16(sHotspots.size()));
		while(aHotspotIdx >= sHotspots.size())
			sHotspots.push_back(Hotspot());
		EResult aResult =
			stringToHotspot(aHotspotDescription, sHotspots[aHotspotIdx]);
		if( aResult == eResult_Malformed )
		{
			logError("Hotspot %s: Could not decipher hotspot position '%s'",
				aHotspotRequests[i].first, aHotspotRequests[i].second);
			sHotspots[aHotspotIdx] = aHotspot;
		}
	}
}


static void buildCommandAliases(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning KeyBinds...\n");

	Profile::KeyValuePairs aKeyBindRequests;
	Profile::getAllKeys(kKeybindsPrefix, aKeyBindRequests);
	for(size_t i = 0; i < aKeyBindRequests.size(); ++i)
	{
		const char* anActionName = aKeyBindRequests[i].first;
		const char* aCommandDescription = aKeyBindRequests[i].second;

		if( !aCommandDescription || aCommandDescription[0] == '\0' )
			continue;

		// Break keys description string into individual words
		theBuilder.parsedString.clear();
		sanitizeSentence(aCommandDescription, theBuilder.parsedString);
		const std::string& aVKeySeq = namesToVKeySequence(
			theBuilder, theBuilder.parsedString);
		if( !aVKeySeq.empty() )
		{
			// Keybinds can only be assigned to Virtual-Key Code sequences/taps
			Command aCmd;
			if( u16 aVKey = vKeySeqToSingleKey((const u8*)aVKeySeq.c_str()) )
			{
				aCmd.type = eCmdType_TapKey;
				aCmd.data = aVKey;
			}
			else
			{
				aCmd.type = eCmdType_VKeySequence;
				aCmd.data = u16(sKeyStrings.size());
				sKeyStrings.push_back(aVKeySeq);
			}
			theBuilder.commandAliases.setValue(anActionName, aCmd);

			mapDebugPrint("Assigned to alias '%s': '%s'\n",
				anActionName, aCommandDescription);
		}
		else
		{
			logError(
				"%s%s: Unable to decipher and assign '%s'",
				kKeybindsPrefix, anActionName, aCommandDescription);
		}
	}
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


static void addButtonAction(
	InputMapBuilder& theBuilder,
	u16 theLayerIdx,
	std::string theBtnName,
	const std::string& theCmdStr)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	if( theBtnName.empty() )
		return;

	// Handle shortcuts for assigning multiple at once
	for(size_t i = 0; i < ARRAYSIZE(k4DirButtons); ++i)
	{
		// Check if the *end* (after action tag) of button name matches
		if( theBtnName.size() >= k4DirButtons[i].size() &&
			theBtnName.compare(
				theBtnName.size() - k4DirButtons[i].size(),
				k4DirButtons[i].size(),
				k4DirButtons[i]) == 0 )
		{
			addButtonAction(theBuilder, theLayerIdx,
				theBtnName + "UP", theCmdStr + " Up");
			addButtonAction(theBuilder, theLayerIdx,
				theBtnName + "DOWN", theCmdStr + " Down");
			addButtonAction(theBuilder, theLayerIdx,
				theBtnName + "LEFT", theCmdStr + " Left");
			addButtonAction(theBuilder, theLayerIdx,
				theBtnName + "RIGHT", theCmdStr + " Right");
			return;
		}
	}

	// Determine button & action to assign command to
	EButtonAction aBtnAct = breakOffButtonAction(theBtnName);
	EButton aBtnID = buttonNameToID(theBtnName);
	if( aBtnID >= eBtn_Num )
	{
		logError("Unable to identify Gamepad Button '%s%s' requested in [%s]",
			kButtonActionPrefx[aBtnAct],
			theBtnName.c_str(),
			theBuilder.debugItemName.c_str());
		return;
	}

	Command aCmd = stringToCommand(
		theBuilder, theCmdStr, theLayerIdx, true, aBtnAct == eBtnAct_Down);

	// Convert eCmdType_TapKey to eCmdType_PressAndHoldKey?
	// True "while held down" only works with a single key (w/ mods),
	// just like _TapKey, and only when assigned to eBtnAct_Down.
	// For anything besides a single key, this will be skipped and
	// _Down will just act the same as _Press (which can be used
	// intentionally to have 2 actions on button initial press).
	if( aCmd.type == eCmdType_TapKey && aBtnAct == eBtnAct_Down )
		aCmd.type = eCmdType_PressAndHoldKey;

	// Give error for inability to parse the command
	// Note that an empty command string still counts as a valid assignment,
	// possibly to block lower layers' assignments from doing anything
	if( aCmd.type == eCmdType_Empty && !theCmdStr.empty() )
	{
		logError("[%s]: Not sure how to assign '%s%s%s' to '%s'!",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			theCmdStr.c_str());
		return;
	}

	// Make the assignment!
	sLayers[theLayerIdx].map.findOrAdd(aBtnID).cmd[aBtnAct] = aCmd;

	switch(aCmd.type)
	{
	case eCmdType_Empty:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to: <Do Nothing>\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID]);
		break;
	case eCmdType_SlashCommand:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			sKeyStrings[aCmd.data].c_str());
		break;
	case eCmdType_SayString:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			sKeyStrings[aCmd.data].c_str() + 1);
	case eCmdType_TapKey:
	case eCmdType_PressAndHoldKey:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to: %s (%s%s%s%s)\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			theCmdStr.c_str(),
			!!(aCmd.data & kVKeyShiftFlag) ? "Shift+" : "",
			!!(aCmd.data & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(aCmd.data & kVKeyAltFlag) ? "Alt+" : "",
			virtualKeyToName(aCmd.data & kVKeyMask).c_str());
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to sequence: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			theCmdStr.c_str());
		break;
	default:
		mapDebugPrint("[%s]: Assigned '%s%s%s' to command: %s\n",
			theBuilder.debugItemName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			theCmdStr.c_str());
		break;
	}
}


static void buildControlsLayer(InputMapBuilder& theBuilder, u16 theLayerIdx)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());

	// If has an includeLayer, get default settings from it first
	if( sLayers[theLayerIdx].includeLayer != 0 )
	{
		sLayers[theLayerIdx].mouseLookOn =
			sLayers[sLayers[theLayerIdx].includeLayer].mouseLookOn;
		sLayers[theLayerIdx].showHUD =
			sLayers[sLayers[theLayerIdx].includeLayer].showHUD;
		sLayers[theLayerIdx].hideHUD =
			sLayers[sLayers[theLayerIdx].includeLayer].hideHUD;
	}

	const std::string& aLayerName = sLayers[theLayerIdx].label;
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

	// Get mouse look layer setting directly
	sLayers[theLayerIdx].mouseLookOn = Profile::getBool(
		aLayerPrefix + kMouseLookKey,
		sLayers[theLayerIdx].mouseLookOn);

	// Check each key-value pair for button assignment requests
	Profile::KeyValuePairs aSettings;
	Profile::getAllKeys(aLayerPrefix, aSettings);
	for(Profile::KeyValuePairs::const_iterator itr = aSettings.begin();
		itr != aSettings.end(); ++itr)
	{
		const std::string aKey = itr->first;
		if( aKey == kIncludeKey ||
			aKey == kMouseLookKey ||
			aKey == kHUDSettingsKey )
			continue;

		// Parse and add assignment to this layer's commands map
		addButtonAction(theBuilder, theLayerIdx, aKey, itr->second);
	}
	sLayers[theLayerIdx].map.trim();
}


static void buildControlScheme(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Building control scheme layers...\n");

	getOrCreateLayerID(theBuilder, kMainLayerLabel);
	for(u16 idx = 0; idx < sLayers.size(); ++idx)
		buildControlsLayer(theBuilder, idx);

	// Process the "HUD=" key for each layer
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
	{
		sLayers[aLayerID].hideHUD.clearAndResize(sHUDElements.size());
		sLayers[aLayerID].showHUD.clearAndResize(sHUDElements.size());
		std::string aLayerHUDKey = sLayers[aLayerID].label;
		if( aLayerID == 0 )
			aLayerHUDKey += "/";
		else
			aLayerHUDKey = std::string(kLayerPrefix)+aLayerHUDKey+"/";
		aLayerHUDKey += kHUDSettingsKey;
		const std::string& aLayerHUDDescription =
			Profile::getStr(aLayerHUDKey);

		if( aLayerHUDDescription.empty() )
			continue;

		// Break the string into individual words
		theBuilder.parsedString.clear();
		sanitizeSentence(aLayerHUDDescription, theBuilder.parsedString);

		bool show = true;
		for(size_t i = 0; i < theBuilder.parsedString.size(); ++i)
		{
			const std::string& anElementName = theBuilder.parsedString[i];
			if( upper(anElementName) == "HIDE" )
			{
				show = false;
				continue;
			}
			if( upper(anElementName) == "SHOW" )
			{
				show = true;
				continue;
			}
			u16 anElementIdx = getOrCreateHUDElementID(
				theBuilder, anElementName, aLayerID, false);
			sLayers[aLayerID].showHUD.resize(sHUDElements.size());
			sLayers[aLayerID].showHUD.set(anElementIdx, show);
			sLayers[aLayerID].hideHUD.resize(sHUDElements.size());
			sLayers[aLayerID].hideHUD.set(anElementIdx, !show);
		}
	}

	// Special-case manually-managed HUD element (top-most overlay)
	sHUDElements.push_back(HUDElement());
	sHUDElements.back().type = eHUDType_System;

	// Above may have added new HUD elements, now that all are added
	// make sure every layer's hideHUD and showHUD are correct size
	for(u16 aLayerID = 0; aLayerID < sLayers.size(); ++aLayerID)
	{
		sLayers[aLayerID].hideHUD.resize(sHUDElements.size());
		sLayers[aLayerID].showHUD.resize(sHUDElements.size());
	}
	gVisibleHUD.clearAndResize(sHUDElements.size());
	gRedrawHUD.clearAndResize(sHUDElements.size());
	gActiveHUD.clearAndResize(sHUDElements.size());
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
	aMenuItem.label = breakOffItemBeforeChar(theString, ':');

	if( aMenuItem.label.empty() && !theString.empty() )
	{// Having no : character means this points to a sub-menu
		const size_t anOldMenuCount = sMenus.size();
		aMenuItem.cmd.type = eCmdType_OpenSubMenu;
		aMenuItem.cmd.data = getOrCreateMenuID(
			theBuilder, trim(theString), theMenuID);
		aMenuItem.label = sMenus[aMenuItem.cmd.data].label;
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
				menuPathOf(aMenuItem.cmd.data).c_str());
		}
		return aMenuItem;
	}

	if( theString.empty() )
	{
		mapDebugPrint("%s: '%s' left <unassigned>!\n",
			theBuilder.debugItemName.c_str(),
			aMenuItem.label.c_str());
		return aMenuItem;
	}

	aMenuItem.cmd = stringToCommand(theBuilder, theString);

	switch(aMenuItem.cmd.type)
	{
	case eCmdType_SlashCommand:
		mapDebugPrint("%s: '%s' assigned to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			aMenuItem.label.c_str(), theString.c_str());
		break;
	case eCmdType_SayString:
		mapDebugPrint("%s: '%s' assigned to macro: %s\n",
			theBuilder.debugItemName.c_str(),
			aMenuItem.label.c_str(), theString.c_str() + 1);
		break;
	case eCmdType_TapKey:
		mapDebugPrint("%s: '%s' assigned to: %s (%s%s%s%s)\n",
			theBuilder.debugItemName.c_str(),
			aMenuItem.label.c_str(),
			theString.c_str(),
			!!(aMenuItem.cmd.data & kVKeyShiftFlag) ? "Shift+" : "",
			!!(aMenuItem.cmd.data & kVKeyCtrlFlag) ? "Ctrl+" : "",
			!!(aMenuItem.cmd.data & kVKeyAltFlag) ? "Alt+" : "",
			virtualKeyToName(aMenuItem.cmd.data & kVKeyMask).c_str());
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("%s: '%s' assigned to sequence: %s\n",
			theBuilder.debugItemName.c_str(),
			aMenuItem.label.c_str(), theString.c_str());
		break;
	case eCmdType_Empty:
		// Probably just forgot the > at front of a plain string
		sKeyStrings.push_back(std::string(">") + theString);
		aMenuItem.cmd.type = eCmdType_SayString;
		aMenuItem.cmd.data = u16(sKeyStrings.size()-1);
		logError("%s: '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box string. "
				 "Add > to start of it if this was the intent!",
				theBuilder.debugItemName.c_str(),
		aMenuItem.label.c_str(), theString.c_str());
		break;
	default:
		mapDebugPrint("%s: '%s' assigned to command: %s\n",
			theBuilder.debugItemName.c_str(),
			aMenuItem.label.c_str(), theString.c_str());
		break;
	}

	return aMenuItem;
}


static void buildMenus(InputMapBuilder& theBuilder)
{
	sRootMenuCount = u16(sMenus.size());
	if( sRootMenuCount )
		mapDebugPrint("Building Menus...\n");

	for(u16 aMenuID = 0; aMenuID < sMenus.size(); ++aMenuID)
	{
		const std::string aPrefix = menuPathOf(aMenuID);
		const u16 aHUDElementID = sMenus[aMenuID].hudElementID;
		DBG_ASSERT(aHUDElementID < sHUDElements.size());
		const EHUDType aMenuStyle = sHUDElements[aHUDElementID].type;
		DBG_ASSERT(aMenuStyle >= eMenuStyle_Begin);
		DBG_ASSERT(aMenuStyle < eMenuStyle_End);
		const std::string aDebugNamePrefix =
			std::string("[") + aPrefix + "] (";

		u16 itemIdx = 0;
		bool checkForNextMenuItem = aMenuStyle != eMenuStyle_4Dir;
		while(checkForNextMenuItem)
		{
			checkForNextMenuItem = false;
			const std::string& aMenuItemKeyName = toString(itemIdx+1);
			const std::string& aMenuItemString = Profile::getStr(
				aPrefix + "/" + aMenuItemKeyName);
			checkForNextMenuItem = !aMenuItemString.empty();
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
					// most menu styles sholud use replace menu instead,
					// so they behave like "side" instead of "sub" menus
					aMenuItem.cmd.type = eCmdType_ReplaceMenu;
				}
			}
		}
	}
}


static void assignSpecialKeys(InputMapBuilder& theBuilder)
{
	sTargetGroupSize = 1;
	for(size_t i = 0; i < eSpecialKey_Num; ++i)
	{
		DBG_ASSERT(sSpecialKeys[i] == 0);
		Command* aKeyBindCommand =
			theBuilder.commandAliases.find(kSpecialKeyNames[i]);
		if( !aKeyBindCommand )
			continue;
		if( aKeyBindCommand->type != eCmdType_TapKey )
		{
			logError("Can not assign a full key sequence to %s! "
				"Please assign only a single key!",
				kSpecialKeyNames[i]);
			continue;
		}
		sSpecialKeys[i] = aKeyBindCommand->data;
		if( i >= eSpecialKey_FirstGroupTarget &&
			i <= eSpecialKey_LastGroupTarget )
		{
			sTargetGroupSize = max(sTargetGroupSize,
				u8(i + 1 - eSpecialKey_FirstGroupTarget));
		}
	}

	// Have some special keys borrow the value of others if left unassigned
	if( sSpecialKeys[eSpecialKey_MLMoveF] == 0 )
		sSpecialKeys[eSpecialKey_MLMoveF] = sSpecialKeys[eSpecialKey_MoveF];
	if( sSpecialKeys[eSpecialKey_MLMoveB] == 0 )
		sSpecialKeys[eSpecialKey_MLMoveB] = sSpecialKeys[eSpecialKey_MoveB];
	if( sSpecialKeys[eSpecialKey_MLTurnL] == 0 )
		sSpecialKeys[eSpecialKey_MLTurnL] = sSpecialKeys[eSpecialKey_TurnL];
	if( sSpecialKeys[eSpecialKey_MLTurnR] == 0 )
		sSpecialKeys[eSpecialKey_MLTurnR] = sSpecialKeys[eSpecialKey_TurnR];
	if( sSpecialKeys[eSpecialKey_MLStrafeL] == 0 )
	{
		sSpecialKeys[eSpecialKey_MLStrafeL] =
			sSpecialKeys[eSpecialKey_StrafeL]
				? sSpecialKeys[eSpecialKey_StrafeL]
				: sSpecialKeys[eSpecialKey_MLTurnL];
	}
	if( sSpecialKeys[eSpecialKey_MLStrafeR] == 0 )
	{
		sSpecialKeys[eSpecialKey_MLStrafeR] =
			sSpecialKeys[eSpecialKey_StrafeR]
				? sSpecialKeys[eSpecialKey_StrafeR]
				: sSpecialKeys[eSpecialKey_MLTurnR];
	}
}


void setCStringPointerFor(Command* theCommand)
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
		DBG_ASSERT(theCommand->data < sKeyStrings.size());
		theCommand->string = sKeyStrings[theCommand->data].c_str();
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
	sKeyStrings.clear();
	sLayers.clear();
	sMenus.clear();
	sHUDElements.clear();

	// Create temp builder object and build everything from the Profile data
	{
		InputMapBuilder anInputMapBuilder;
		buildGlobalHotspots(anInputMapBuilder);
		buildCommandAliases(anInputMapBuilder);
		buildControlScheme(anInputMapBuilder);
		buildMenus(anInputMapBuilder);
		assignSpecialKeys(anInputMapBuilder);
	}

	// Trim unused memory
	if( sHotspots.size() < sHotspots.capacity() )
		std::vector<Hotspot>(sHotspots).swap(sHotspots);
	if( sKeyStrings.size() < sKeyStrings.capacity() )
		std::vector<std::string>(sKeyStrings).swap(sKeyStrings);
	if( sLayers.size() < sLayers.capacity() )
		std::vector<ControlsLayer>(sLayers).swap(sLayers);
	if( sHUDElements.size() < sHUDElements.capacity() )
		std::vector<HUDElement>(sHUDElements).swap(sHUDElements);
	if( sMenus.size() < sMenus.capacity() )
		std::vector<Menu>(sMenus).swap(sMenus);

	// Now that are done messing with resizing vectors which can
	// invalidate pointers, can convert Commands with 'data' field
	// being an sKeyStrings index into having direct pointers to
	// the C-strings for use in other modules.
	for(std::vector<Menu>::iterator itr = sMenus.begin();
		itr != sMenus.end(); ++itr)
	{
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

	return NULL;
}


bool mouseLookShouldBeOn(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].mouseLookOn;
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


std::string menuItemKey(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return menuPathOf(theMenuID) + "/" + toString(theMenuItemIdx+1);
}


std::string menuItemDirKey(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return menuPathOf(theMenuID) + "/" + k4DirMenuItemLabel[theDir];
}


const Hotspot& getHotspot(u16 theHotspotID)
{
	DBG_ASSERT(theHotspotID < sHotspots.size());
	return sHotspots[theHotspotID];
}


EResult profileStringToHotspot(std::string& theString, Hotspot& out)
{
	return stringToHotspot(theString, out);
}


EHUDType hudElementType(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements[theHUDElementID].type;
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


u16 controlsLayerCount()
{
	return u16(sLayers.size());
}


u16 hudElementCount()
{
	return u16(sHUDElements.size());
}


u16 rootMenuCount()
{
	return sRootMenuCount;
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


u8 targetGroupSize()
{
	return sTargetGroupSize;
}


u16 hotspotCount()
{
	return u16(sHotspots.size());
}


const std::string& layerLabel(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].label;
}


const std::string& menuLabel(u16 theMenuID)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	return sMenus[theMenuID].label;
}


const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theMenuItemIdx < sMenus[theMenuID].items.size());
	return sMenus[theMenuID].items[theMenuItemIdx].label;
}


const std::string& menuDirLabel(u16 theMenuID, ECommandDir theDir)
{
	DBG_ASSERT(theMenuID < sMenus.size());
	DBG_ASSERT(theDir < eCmdDir_Num);
	return sMenus[theMenuID].dirItems[theDir].label;
}


const std::string& hudElementLabel(u16 theHUDElementID)
{
	DBG_ASSERT(theHUDElementID < sHUDElements.size());
	return sHUDElements[theHUDElementID].label;
}

} // InputMap
