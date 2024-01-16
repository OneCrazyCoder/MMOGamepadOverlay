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

enum {
kMacroSlotsPerSet = 4,
kMouseLookStartHotspotID = 1,
};

const char* kMainMacroSetLabel = "Macros";
const char* kMainLayerLabel = "Scheme";
const char* kLayerPrefix = "Layer.";
const char* kKeybindsPrefix = "KeyBinds/";
const char* kMacroSlotLabel[] = { "L", "R", "U", "D" }; // match ECommandDir!
DBG_CTASSERT(ARRAYSIZE(kMacroSlotLabel) == kMacroSlotsPerSet);

// These need to be in all upper case
const char* kIncludeKey = "INCLUDE";
const char* kHUDSettingsKey = "HUD";
const char* kMouseLookKey = "MOUSELOOK";
const char* kMouseLookStartHotspotKey = "MOUSELOOKSTART";
const std::string k4DirButtons[] =
{	"LS", "LSTICK", "LEFTSTICK", "LEFT STICK", "DPAD",
	"RS", "RSTICK", "RIGHTSTICK", "RIGHT STICK", "FPAD" };

const char* kButtonActionPrefx[] =
{
	"",			// eBtnAct_Down
	"Press",	// eBtnAct_Press
	"Hold",		// eBtnAct_ShortHold
	"LongHold",	// eBtnAct_LongHold
	"Tap",		// eBtnAct_Tap
	"Release",	// eBtnAct_Release
};
DBG_CTASSERT(ARRAYSIZE(kButtonActionPrefx) == eBtnAct_Num);

const char* kSpecialKeyNames[] =
{
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

struct MacroSlot
{
	std::string label;
	Command cmd;
};

struct MacroSet
{
	std::string label;
	MacroSlot slot[kMacroSlotsPerSet];
};

struct ButtonActions
{
	std::string label[eBtnAct_Num];
	Command cmd[eBtnAct_Num];
};
typedef VectorMap<EButton, ButtonActions> ButtonActionsMap;

struct Hotspot
{
	struct Coord
	{
		int value : 24;
		enum EType
		{
			eType_MinPlus,
			eType_MaxMinus,
			eType_CenterPlus,
			eType_CenerMinus,
			eType_Percent,
		} type : 8;
		Coord() : value(), type(eType_MinPlus) {}
	} x, y;
};
typedef std::vector<Hotspot> HotspotSet;

struct ControlsLayer
{
	std::string label;
	ButtonActionsMap map;
	u16 includeLayer;
	BitArray<eHUDElement_Num> showHUD;
	BitArray<eHUDElement_Num> hideHUD;
	bool mouseLookOn;

	ControlsLayer() :
		includeLayer(),
		showHUD(),
		hideHUD(),
		mouseLookOn()
	{}
};

struct InputMapBuilder
{
	std::vector<std::string> parsedString;
	StringToValueMap<u16> layerNameToIdxMap;
	StringToValueMap<u16> hotspotNameToIdxMap;
	StringToValueMap<Command> commandAliases;
	std::string debugSlotName;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<HotspotSet> sHotspotSets;
static std::vector<std::string> sKeyStrings;
static std::vector<MacroSet> sMacroSets;
static std::vector<ControlsLayer> sLayers;
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


static Command wordsToSpecialCommand(
	InputMapBuilder& theBuilder,
	const std::vector<std::string>& theWords,
	u16 theControlsLayerIndex = 0,
	bool allowHoldActions = false)
{
	Command result;

	// All commands require more than one "word", even if only one of the
	// words is actually a command key word (thus can force a keybind to be
	// used instead of a command by specifying the keybind as a single word)
	if( theWords.size() <= 1 )
		return result;

	// Find all key words that are actually included
	ECommandKeyWord aLastKeyWordID = eCmdWord_Filler;
	BitArray<eCmdWord_Num> keyWordsFound = { 0 };
	for(size_t i = 0; i < theWords.size(); ++i)
	{
		// Only the actual last word can be an unknown word
		if( aLastKeyWordID == eCmdWord_Unknown )
			return result;
		aLastKeyWordID = commandWordToID(upper(theWords[i]));
		keyWordsFound.set(aLastKeyWordID);
	}

	keyWordsFound.reset(eCmdWord_Filler);
	if( keyWordsFound.none() )
		return result;

	// Last word is the layer name for layer commands, but only if it
	// is not itself a key word related to layers (other key words are
	// allowed as layer names though!).
	const std::string* aLayerName = null;
	if( (keyWordsFound.test(eCmdWord_Layer) ||
		 keyWordsFound.test(eCmdWord_Add) ||
		 keyWordsFound.test(eCmdWord_Remove) ||
		 keyWordsFound.test(eCmdWord_Hold)) &&
		aLastKeyWordID != eCmdWord_Layer &&
		aLastKeyWordID != eCmdWord_Add &&
		aLastKeyWordID != eCmdWord_Remove &&
		aLastKeyWordID != eCmdWord_Hold )
	{
		aLayerName = &theWords[theWords.size()-1];
		keyWordsFound.reset(aLastKeyWordID);
	}

	// "= Add [Layer] <aLayerName>"
	BitArray<eCmdWord_Num> allowedKeyWords = { 0 };
	allowedKeyWords.set(eCmdWord_Layer);
	allowedKeyWords.set(eCmdWord_Add);
	if( keyWordsFound.test(eCmdWord_Add) &&
		aLayerName &&
		(keyWordsFound & ~allowedKeyWords).none() )
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
		(keyWordsFound & ~allowedKeyWords).none() )
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
	// allowedKeyWords = Layer & Remove
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
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_HoldControlsLayer;
		result.data = getOrCreateLayerID(theBuilder, *aLayerName);
		return result;
	}

	// "= Reset Macros"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Reset);
	allowedKeyWords.set(eCmdWord_Macro);
	if( keyWordsFound.test(eCmdWord_Reset) &&
		keyWordsFound.test(eCmdWord_Macro) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_ChangeMacroSet;
		result.data = 0;
	}

	// "= Target Group <eTargetGroupType>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Target);
	allowedKeyWords.set(eCmdWord_Group);
	allowedKeyWords.set(eCmdWord_Wrap);
	allowedKeyWords.set(eCmdWord_NoWrap);
	if( keyWordsFound.test(eCmdWord_Target) &&
		keyWordsFound.test(eCmdWord_Group) )
	{
		result.type = eCmdType_TargetGroup;
		// "= Target Group [Load] [Favorite|Default]"
		// allowedKeyWords = Target & Group & Wrap & NoWrap
		allowedKeyWords.set(eCmdWord_Favorite);
		allowedKeyWords.set(eCmdWord_Load); // or "Recall"
		allowedKeyWords.set(eCmdWord_Default);
		if( (keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_LoadFavorite;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Load);
		// "= Target Group 'Save'|'Left' [Favorite|Default]"
		// allowedKeyWords = Target & Group & Wrap & NoWrap & Default & Favorite
		allowedKeyWords.set(eCmdWord_Save);
		allowedKeyWords.set(eCmdWord_Left);
		if( (keyWordsFound.test(eCmdWord_Save) ||
			 keyWordsFound.test(eCmdWord_Left)) &&
			(keyWordsFound & ~allowedKeyWords).none() )
		{
			result.data = eTargetGroupType_SaveFavorite;
			return result;
		}
		allowedKeyWords.reset(eCmdWord_Save);
		allowedKeyWords.reset(eCmdWord_Left);
		allowedKeyWords.reset(eCmdWord_Default);
		allowedKeyWords.reset(eCmdWord_Favorite);
		// "= Target Group 'Last'|'Right'|'Pet'"
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

	// "= [Select] Menu <aCmdDir>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Select);
	allowedKeyWords.set(eCmdWord_Menu);
	if( keyWordsFound.test(eCmdWord_Menu) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_SelectMenu;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Menu);

	// "= [Select] Hotspot <aCmdDir>"
	// allowedKeyWords = Select
	allowedKeyWords.set(eCmdWord_Hotspot);
	if( keyWordsFound.test(eCmdWord_Hotspot) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_SelectHotspot;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Hotspot);

	// "= [Select] Macro <aCmdDir>"
	// allowedKeyWords = Select
	allowedKeyWords.set(eCmdWord_Macro);
	if( keyWordsFound.test(eCmdWord_Macro) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_SelectMacro;
		return result;
	}

	// "= Rewrite [Macro] <aCmdDir>"
	// allowedKeyWords = Select & Macro
	allowedKeyWords.reset(eCmdWord_Select);
	allowedKeyWords.set(eCmdWord_Rewrite);
	if( keyWordsFound.test(eCmdWord_Rewrite) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_SelectMacro;
		return result;
	}

	// "= 'Move'|'Turn'|'MoveTurn' <aCmdDir>"
	allowedKeyWords.reset();
	allowedKeyWords.set(eCmdWord_Move);
	allowedKeyWords.set(eCmdWord_Turn);
	if( allowHoldActions &&
		(keyWordsFound & allowedKeyWords).any() &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MoveTurn;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Turn);

	// "= 'Strafe|MoveStrafe' <aCmdDir>"
	// allowedKeyWords = Move
	allowedKeyWords.set(eCmdWord_Strafe);
	if( allowHoldActions &&
		keyWordsFound.test(eCmdWord_Strafe) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MoveStrafe;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Strafe);

	// "= [Move] Mouse <aCmdDir>"
	// allowedKeyWords = Move
	allowedKeyWords.set(eCmdWord_Mouse);
	if( allowHoldActions &&
		keyWordsFound.test(eCmdWord_Mouse) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MoveMouse;
		return result;
	}

	// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Smooth] <aCmdDir>"
	// allowedKeyWords = Move & Mouse
	allowedKeyWords.set(eCmdWord_MouseWheel);
	allowedKeyWords.set(eCmdWord_Smooth);
	if( allowHoldActions &&
		keyWordsFound.test(eCmdWord_MouseWheel) &&
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
	if( allowHoldActions &&
		keyWordsFound.test(eCmdWord_MouseWheel) &&
		keyWordsFound.test(eCmdWord_Stepped) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MouseWheel;
		result.data2 = eMouseWheelMotion_Stepped;
		return result;
	}
	allowedKeyWords.reset(eCmdWord_Stepped);
	
	// "= [Move] [Mouse] 'Wheel'|'MouseWheel' [Once] <aCmdDir>"
	// 'Once' is only optional when NOT assigning to a hold action
	// allowedKeyWords = Move & Mouse & Wheel
	allowedKeyWords.set(eCmdWord_Once);
	if( keyWordsFound.test(eCmdWord_MouseWheel) &&
		(!allowHoldActions ||
		 keyWordsFound.test(eCmdWord_Once)) &&
		(keyWordsFound & ~allowedKeyWords).none() )
	{
		result.type = eCmdType_MouseWheel;
		result.data2 = eMouseWheelMotion_Once;
		return result;
	}

	DBG_ASSERT(result.type == eCmdType_Empty);
	result.data = 0;
	return result;
}


static Command stringToCommand(
	InputMapBuilder& theBuilder,
	std::string theString,
	u16 theControlsLayerIndex = 0,
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


static void buildGlobalHotspots(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning global hotspots...\n");
	sHotspotSets.push_back(HotspotSet(kMouseLookStartHotspotID + 1));
	// Hotspot set 0 is used for "named" hotspots in [Hotspots] in Profile
	// that can be referenced within macros and key binds.
	// sHotspotSets[0][0] is reserved to essentially mean "none"
	// The hotspotNameToIdxMap maps to 0 for "filler" words between
	// jump/point/click and the actual hotspot name.
	theBuilder.hotspotNameToIdxMap.setValue("MOUSE", 0);
	theBuilder.hotspotNameToIdxMap.setValue("CURSOR", 0);
	theBuilder.hotspotNameToIdxMap.setValue("TO", 0);
	theBuilder.hotspotNameToIdxMap.setValue("AT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("ON", 0);
	theBuilder.hotspotNameToIdxMap.setValue("HOTSPOT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("HOT", 0);
	theBuilder.hotspotNameToIdxMap.setValue("SPOT", 0);

	// Create default hotspot for MouseLookStart in case none specified
	Hotspot aHotspot;
	aHotspot.x.type = Hotspot::Coord::eType_Percent;
	aHotspot.x.value = 32768;
	aHotspot.y.type = Hotspot::Coord::eType_Percent;
	aHotspot.y.value = 32768;
	sHotspotSets[0][kMouseLookStartHotspotID] = aHotspot;
	theBuilder.hotspotNameToIdxMap.setValue(
		kMouseLookStartHotspotKey, kMouseLookStartHotspotID);

	// TODO: Actually parse [Hotspots]
	sHotspotSets[0].push_back(aHotspot);
	theBuilder.hotspotNameToIdxMap.setValue(
		"CENTERSCREEN", u16(sHotspotSets[0].size()-1));
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
			theBuilder.debugSlotName.c_str());
		return;
	}

	Command aCmd = stringToCommand(
		theBuilder, theCmdStr, theLayerIdx, aBtnAct == eBtnAct_Down);

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
			theBuilder.debugSlotName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			theCmdStr.c_str());
		return;
	}

	// Make the assignment!
	sLayers[theLayerIdx].map.findOrAdd(aBtnID).cmd[aBtnAct] = aCmd;

	mapDebugPrint("[%s]: Assigned '%s%s%s' to '%s'\n",
		theBuilder.debugSlotName.c_str(),
		kButtonActionPrefx[aBtnAct],
		kButtonActionPrefx[aBtnAct][0] ? " " : "",
		kProfileButtonName[aBtnID],
		theCmdStr.empty() ? "<Do Nothing>" : theCmdStr.c_str());
}


static void updateLayerHUDSettings(
	InputMapBuilder& theBuilder,
	size_t theLayerIdx,
	const std::string& theString)
{
	// Break the string into individual words
	theBuilder.parsedString.clear();
	sanitizeSentence(upper(theString), theBuilder.parsedString);
	
	bool show = true;
	for(size_t i = 0; i < theBuilder.parsedString.size(); ++i)
	{
		const std::string& anElementName = upper(theBuilder.parsedString[i]);
		const EHUDElement aHUD_ID = hudElementNameToID(anElementName);
		if( aHUD_ID != eHUDElement_Num )
		{
			sLayers[theLayerIdx].showHUD.set(aHUD_ID, show);
			sLayers[theLayerIdx].hideHUD.set(aHUD_ID, !show);
		}
		else if( anElementName == "HIDE" )
		{
			show = false;
		}
		else if( anElementName == "SHOW" )
		{
			show = true;
		}
		else
		{
			logError("Uknown HUD element(s) specified for [%s]: %s",
				theBuilder.debugSlotName.c_str(),
				theString.c_str());
			return;
		}
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
	theBuilder.debugSlotName.clear();
	if( theLayerIdx != 0 )
	{
		theBuilder.debugSlotName = kLayerPrefix;
		mapDebugPrint("Building controls layer: %s\n", aLayerName.c_str());
	}
	theBuilder.debugSlotName += aLayerName;

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
		if( aKey == kIncludeKey || aKey == kMouseLookKey )
			continue;

		if( aKey == kHUDSettingsKey )
		{
			updateLayerHUDSettings(theBuilder, theLayerIdx, itr->second);
			continue;
		}

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
}


static MacroSlot stringToMacroSlot(
	InputMapBuilder& theBuilder,
	size_t theMacroSet,
	std::string theString)
{
	MacroSlot aMacroSlot;
	if( theString.empty() )
	{
		mapDebugPrint("%s: Left <unnamed> and <unassigned>!\n",
			theBuilder.debugSlotName.c_str());
		return aMacroSlot;
	}

	// Get the label (part of the string before first colon)
	aMacroSlot.label = breakOffItemBeforeChar(theString, ':');

	if( aMacroSlot.label.empty() && !theString.empty() )
	{// Having no : character means this points to a sub-set
		aMacroSlot.label = trim(theString);
		aMacroSlot.cmd.type = eCmdType_ChangeMacroSet;
		aMacroSlot.cmd.data = u16(sMacroSets.size());
		sMacroSets.push_back(MacroSet());
		sMacroSets.back().label =
			sMacroSets[theMacroSet].label +
			"." + aMacroSlot.label;
		mapDebugPrint("%s: Macro Set: '%s'\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str());
		return aMacroSlot;
	}

	if( theString.empty() )
	{
		mapDebugPrint("%s: Macro '%s' left <unassigned>!\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str());
		return aMacroSlot;
	}

	aMacroSlot.cmd = stringToCommand(theBuilder, theString);

	switch(aMacroSlot.cmd.type)
	{
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		mapDebugPrint("%s: Macro '%s' assigned to string: %s\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
		break;
	case eCmdType_TapKey:
		mapDebugPrint("%s: Macro '%s' assigned to key/button: %s\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
		break;
	case eCmdType_VKeySequence:
		mapDebugPrint("%s: Macro '%s' assigned to key sequence: %s\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
		break;
	case eCmdType_Empty:
		// Probably just forgot the > at front of a plain string
		sKeyStrings.push_back(std::string(">") + theString);
		aMacroSlot.cmd.type = eCmdType_SayString;
		aMacroSlot.cmd.data = u16(sKeyStrings.size()-1);
		logError("%s: Macro '%s' unsure of meaning of '%s'. "
				 "Assigning as a chat box say string. "
				 "Add > to start of it if this was the intent!",
				theBuilder.debugSlotName.c_str(),
		aMacroSlot.label.c_str(), theString.c_str());
		break;
	}

	return aMacroSlot;
}


static void buildMacroSets(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Building Macro Sets...\n");

	sMacroSets.push_back(MacroSet());
	sMacroSets.back().label = kMainMacroSetLabel;
	for(size_t aSet = 0; aSet < sMacroSets.size(); ++aSet )
	{
		const std::string& aPrefix = condense(sMacroSets[aSet].label);
		for(size_t aSlot = 0; aSlot < kMacroSlotsPerSet; ++aSlot)
		{
			theBuilder.debugSlotName = std::string("[") +
				sMacroSets[aSet].label + "] (" +
				kMacroSlotLabel[aSlot] + ")";

			sMacroSets[aSet].slot[aSlot] =
				stringToMacroSlot(
					theBuilder,
					aSet,
					Profile::getStr(
						aPrefix + "/" + kMacroSlotLabel[aSlot]));
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


void convertKeyStringIndexesToPointers()
{
	for(std::vector<MacroSet>::iterator itr = sMacroSets.begin();
		itr != sMacroSets.end(); ++itr)
	{
		for(size_t i = 0; i < kMacroSlotsPerSet; ++i)
			setCStringPointerFor(&itr->slot[i].cmd);
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


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	ZeroMemory(&sSpecialKeys, sizeof(sSpecialKeys));
	sHotspotSets.clear();
	sKeyStrings.clear();
	sLayers.clear();
	sMacroSets.clear();

	// Create temp builder object and build everything from the Profile data
	{
		InputMapBuilder anInputMapBuilder;
		buildGlobalHotspots(anInputMapBuilder);
		buildCommandAliases(anInputMapBuilder);
		buildControlScheme(anInputMapBuilder);
		buildMacroSets(anInputMapBuilder);
		assignSpecialKeys(anInputMapBuilder);
	}

	// Trim unused memory
	if( sHotspotSets.size() < sHotspotSets.capacity() )
		std::vector<HotspotSet>(sHotspotSets).swap(sHotspotSets);
	if( sKeyStrings.size() < sKeyStrings.capacity() )
		std::vector<std::string>(sKeyStrings).swap(sKeyStrings);
	if( sLayers.size() < sLayers.capacity() )
		std::vector<ControlsLayer>(sLayers).swap(sLayers);
	if( sMacroSets.size() < sMacroSets.capacity() )
		std::vector<MacroSet>(sMacroSets).swap(sMacroSets);

	// Now that are done messing with resizing vectors which can
	// invalidate pointers, can convert Commands with 'data' field
	// being an sKeyStrings index into having direct pointers to
	// the C-strings for use in other modules.
	convertKeyStringIndexesToPointers();
}


size_t availableLayerCount()
{
	return sLayers.size();
}


bool mouseLookShouldBeOn(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].mouseLookOn;
}


const BitArray<eHUDElement_Num>& hudElementsToShow(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].showHUD;
}


const BitArray<eHUDElement_Num>& hudElementsToHide(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].hideHUD;
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


Command commandForMacro(u16 theMacroSetID, u8 theMacroSlotID)
{
	DBG_ASSERT(theMacroSetID < sMacroSets.size());
	DBG_ASSERT(theMacroSlotID < kMacroSlotsPerSet);
	
	return sMacroSets[theMacroSetID].slot[theMacroSlotID].cmd;
}


u16 keyForSpecialAction(ESpecialKey theAction)
{
	DBG_ASSERT(theAction < eSpecialKey_Num);
	return sSpecialKeys[theAction];
}


int hotspotMousePosX(u16 theHotspotSet, u16 theHotspotID)
{
	DBG_ASSERT(theHotspotSet < sHotspotSets.size());
	DBG_ASSERT(theHotspotID < sHotspotSets[theHotspotSet].size());
	// TODO: Convert to proper coordinates system
	return sHotspotSets[theHotspotSet][theHotspotID].x.value;
}


int hotspotMousePosY(u16 theHotspotSet, u16 theHotspotID)
{
	DBG_ASSERT(theHotspotSet < sHotspotSets.size());
	DBG_ASSERT(theHotspotID < sHotspotSets[theHotspotSet].size());
	// TODO: Convert to proper coordinates system
	return sHotspotSets[theHotspotSet][theHotspotID].y.value;
}


u16 mouseLookStartHotspotID()
{
	return kMouseLookStartHotspotID;
}


u8 targetGroupSize()
{
	return sTargetGroupSize;
}


const std::string& layerLabel(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sLayers.size());
	return sLayers[theLayerID].label;
}


const std::string& macroLabel(u16 theMacroSetID, u8 theMacroSlotID)
{
	DBG_ASSERT(theMacroSetID < sMacroSets.size());
	DBG_ASSERT(theMacroSlotID < kMacroSlotsPerSet);

	return sMacroSets[theMacroSetID].slot[theMacroSlotID].label;
}

} // InputMap
