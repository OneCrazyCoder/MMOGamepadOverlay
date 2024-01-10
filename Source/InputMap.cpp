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
const std::string k4DirButtons[] =
{	"LS", "LSTICK", "LEFTSTICK", "LEFT STICK", "DPAD",
	"RS", "RSTICK", "RIGHTSTICK", "RIGHT STICK", "FPAD" };
struct CommandKeyWord { char* str; ECommandType cmd; };
const CommandKeyWord kCmdKeyWords[] =
{// In order of priority, earlier words supersede later ones
	{ "ADD", eCmdType_AddControlsLayer },
	{ "REMOVE", eCmdType_RemoveControlsLayer },
	{ "HOLD", eCmdType_HoldControlsLayer },
	{ "LAYER", eCmdType_HoldControlsLayer },
	{ "SMOOTH", eCmdType_SmoothMouseWheel },
	{ "STEP", eCmdType_StepMouseWheel },
	{ "STEPPED", eCmdType_StepMouseWheel },
	{ "WHEEL", eCmdType_SmoothMouseWheel },
	{ "MOUSEWHEEL", eCmdType_SmoothMouseWheel },
	{ "MOUSE", eCmdType_MoveMouse },
	{ "MOVETURN", eCmdType_MoveTurn },
	{ "STRAFE", eCmdType_MoveStrafe },
	{ "MOVESTRAFE", eCmdType_MoveStrafe },
	{ "MOVE", eCmdType_MoveTurn },
	{ "CAST", eCmdType_UseAbility },
	{ "USE", eCmdType_UseAbility },
	{ "ABILITY", eCmdType_SelectAbility },
	{ "SPELL", eCmdType_SelectAbility },
	{ "HOTBAR", eCmdType_SelectAbility },
	{ "RESET",	eCmdType_ChangeMacroSet },
	{ "REWRITE", eCmdType_RewriteMacro },
	{ "MACRO",	eCmdType_SelectMacro },
	{ "HOTSPOT", eCmdType_SelectHotspot },
	{ "CONFIRM", eCmdType_ConfirmMenu },
	{ "CANCEL", eCmdType_CancelMenu },
	{ "MENU", eCmdType_SelectMenu },
};

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

enum ESpecialKeys
{
	eSpecialKey_MoveF,
	eSpecialKey_MoveB,
	eSpecialKey_TurnL,
	eSpecialKey_TurnR,
	eSpecialKey_StrafeL,
	eSpecialKey_StrafeR,
	eSpecialKey_MLMoveF,
	eSpecialKey_MLMoveB,
	eSpecialKey_MLTurnL,
	eSpecialKey_MLTurnR,
	eSpecialKey_MLStrafeL,
	eSpecialKey_MLStrafeR,

	eSpecialKey_Num
};

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
	size_t currentSet;
	std::string debugSlotName;
	std::vector<std::string> parsedString;
	StringToValueMap<u16> nameToIdxMap;
	StringToValueMap<u16> keyAliases;
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static std::vector<MacroSet> sMacroSets;
static std::vector<std::string> sKeyStrings;
static std::vector<ControlsLayer> sLayers;
static u16 sSpecialKeys[eSpecialKey_Num];


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

static EResult checkForComboKeyName(
	std::string theKeyName,
	std::string& out,
	bool allowMouse)
{
	std::string aModKeyName;
	aModKeyName.push_back(theKeyName[0]);
	theKeyName = theKeyName.substr(1);
	while(theKeyName.size() > 1)
	{
		aModKeyName.push_back(theKeyName[0]);
		theKeyName = theKeyName.substr(1);
		const u8 aModKey = keyNameToVirtualKey(aModKeyName, allowMouse);
		if( aModKey != 0 &&
			(aModKey == VK_SHIFT ||
			 aModKey == VK_CONTROL ||
			 aModKey == VK_MENU) )
		{// Found a valid modifier key
			// Is rest of the name a valid key now?
			if( u8 aMainKey = keyNameToVirtualKey(theKeyName, allowMouse))
			{// We have a valid key combo!
				out.push_back(aModKey);
				out.push_back(aMainKey);
				return eResult_Ok;
			}
			// Perhaps remainder is another mod+key, like ShiftCtrlA?
			std::string suffix;
			if( checkForComboKeyName(
					theKeyName, suffix, allowMouse) == eResult_Ok )
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
		const char c = theKeyName[aStrPos];
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
	out.push_back(((aTime >> 7) & 0x7F) | 0x80);
	out.push_back((aTime & 0x7F) | 0x80);

	// Success!
	return eResult_Ok;
}


static std::string namesToVKeySequence(
	const std::vector<std::string>& theNames,
	bool allowMouse)
{
	std::string aVKeySeq;

	if( theNames.empty() )
		return aVKeySeq;

	bool expectingWaitTime = false;
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
		const u8 aVKey = keyNameToVirtualKey(aName, allowMouse);
		if( aVKey == 0 )
		{
			// Check if it's a pause/delay/wait command
			EResult aResult = checkForVKeySeqPause(aName, aVKeySeq);
			// Incomplete result means it WAS a wait, now need the time
			if( aResult == eResult_Incomplete )
				expectingWaitTime = true;
			if( aResult != eResult_NotFound )
				continue;

			// Check if it's a modifier+key in one word like Shift2 or Alt1
			aResult = checkForComboKeyName(aName, aVKeySeq, allowMouse);
			if( aResult != eResult_Ok )
			{
				// Can't figure this word out at all, abort!
				aVKeySeq.clear();
				return aVKeySeq;
			}
		}
		else
		{
			aVKeySeq.push_back(aVKey);
		}
	}

	return aVKeySeq;
}


static MacroSlot stringToMacroSlot(
	InputMapBuilder& theBuilder,
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
			sMacroSets[theBuilder.currentSet].label +
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

	// Check for a slash command or say string, which outputs the whole string
	if( theString[0] == '/' )
	{
		sKeyStrings.push_back(theString);
		aMacroSlot.cmd.type = eCmdType_SlashCommand;
		aMacroSlot.cmd.data = u16(sKeyStrings.size()-1);
		mapDebugPrint("%s: Macro '%s' assigned to string: %s\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
		return aMacroSlot;
	}
	if( theString[0] == '>' )
	{
		sKeyStrings.push_back(theString);
		aMacroSlot.cmd.type = eCmdType_SayString;
		aMacroSlot.cmd.data = u16(sKeyStrings.size()-1);
		mapDebugPrint("%s: Macro '%s' assigned to string: %s\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), &theString[1]);
		return aMacroSlot;
	}

	// Check for alias to a keybind
	if( u16* aKeyStringIdxPtr =
			theBuilder.keyAliases.find(condense(theString)) )
	{
		aMacroSlot.cmd.type = eCmdType_VKeySequence;
		aMacroSlot.cmd.data = *aKeyStringIdxPtr;
		mapDebugPrint("%s: Macro '%s' assigned to KeyBind: '%s'\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
		return aMacroSlot;
	}

	// Break the remaining string (after label) into individual words
	theBuilder.parsedString.clear();
	sanitizeSentence(theString, theBuilder.parsedString);

	// Process the words as a sequence of Virtual-Key Code names
	const std::string& aVKeySeq = namesToVKeySequence(
		theBuilder.parsedString, false);
	if( !aVKeySeq.empty() )
	{
		sKeyStrings.push_back(aVKeySeq);
		aMacroSlot.cmd.type = eCmdType_VKeySequence;
		aMacroSlot.cmd.data = u16(sKeyStrings.size()-1);
		mapDebugPrint("%s: Macro '%s' assigned to key sequence: %s\n",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
		return aMacroSlot;
	}

	// Probably just forgot the > at front of a plain string
	sKeyStrings.push_back(std::string(">") + theString);
	aMacroSlot.cmd.type = eCmdType_SayString;
	aMacroSlot.cmd.data = u16(sKeyStrings.size()-1);
	logError("%s: Macro '%s' unsure of meaning of '%s'. "
			 "Assigning as a chat box say string. "
			 "Add > to start of it if this was the intent.",
			theBuilder.debugSlotName.c_str(),
			aMacroSlot.label.c_str(), theString.c_str());
	return aMacroSlot;
}


static void buildMacroSets(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Building Macro Sets...\n");

	sMacroSets.push_back(MacroSet());
	sMacroSets.back().label = kMainMacroSetLabel;
	for(int aSet = 0; aSet < sMacroSets.size(); ++aSet )
	{
		theBuilder.currentSet = aSet;
		const std::string& aPrefix = condense(sMacroSets[aSet].label);
		for(int aSlot = 0; aSlot < kMacroSlotsPerSet; ++aSlot)
		{
			theBuilder.debugSlotName = std::string("[") +
				sMacroSets[aSet].label + "] (" +
				kMacroSlotLabel[aSlot] + ")";

			sMacroSets[aSet].slot[aSlot] =
				stringToMacroSlot(
					theBuilder,
					Profile::getStr(
						aPrefix + "/" + kMacroSlotLabel[aSlot]));
		}
	}
}


static void buildKeyAliases(InputMapBuilder& theBuilder)
{
	mapDebugPrint("Assigning KeyBinds...\n");

	Profile::KeyValuePairs aKeyBindRequests;
	Profile::getAllKeys(kKeybindsPrefix, aKeyBindRequests);
	for(size_t i = 0; i < aKeyBindRequests.size(); ++i)
	{
		const char* anActionName = aKeyBindRequests[i].first;
		const char* aKeysDescription = aKeyBindRequests[i].second;

		if( !aKeysDescription || aKeysDescription[0] == '\0' )
			continue;

		// Break keys description string into individual words
		theBuilder.parsedString.clear();
		sanitizeSentence(aKeysDescription, theBuilder.parsedString);
		const std::string& aVKeySeq = namesToVKeySequence(
			theBuilder.parsedString, true);
		if( !aVKeySeq.empty() )
		{
			sKeyStrings.push_back(aVKeySeq);
			theBuilder.keyAliases.setValue(
				anActionName, u16(sKeyStrings.size()-1));

			mapDebugPrint("Assigned to alias '%s': '%s'\n",
				anActionName, aKeysDescription);
		}
		else
		{
			logError(
				"%s%s: Unable to decipher and assign '%s'",
				kKeybindsPrefix, anActionName, aKeysDescription);
		}
	}
}


static bool layerIncludes(size_t theLayerIdx, size_t theTestIdx)
{
	DBG_ASSERT(theLayerIdx < sLayers.size());
	DBG_ASSERT(theTestIdx < sLayers.size());

	do {
		if( theLayerIdx == theTestIdx )
			return true;
		theLayerIdx = sLayers[theLayerIdx].includeLayer;
	} while(theLayerIdx != 0);

	return false;
}


static u16 getOrCreateLayerID(
	InputMapBuilder& theBuilder,
	const std::string& theLayerName,
	std::vector<std::string>& theChildList = std::vector<std::string>())
{
	DBG_ASSERT(!theLayerName.empty());

	// Check if already exists, and if so return the index
	const std::string& aLayerKeyName = upper(theLayerName);
	if( u16* idx = theBuilder.nameToIdxMap.find(aLayerKeyName) )
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
	theBuilder.nameToIdxMap.setValue(aLayerKeyName, u16(sLayers.size()));
	sLayers.push_back(ControlsLayer());
	sLayers.back().label = theLayerName;
	sLayers.back().includeLayer = anIncludeIdx;

	return u16(sLayers.size() - 1);
}


static Command buildSpecialCommand(
	InputMapBuilder& theBuilder,
	u16 theLayerIdx)
{
	std::vector<std::string>& aCmdStrings = theBuilder.parsedString;

	Command result;
	if( aCmdStrings.empty() || aCmdStrings.front().empty() )
		return result;

	// Search for key words in order of priority
	size_t aCommandWordIdx = 0;
	for(size_t i = 0; i < ARRAYSIZE(kCmdKeyWords); ++i)
	{
		const char* aCheckWord = kCmdKeyWords[i].str;
		for(size_t aWordIdx = 0; aWordIdx < aCmdStrings.size(); ++aWordIdx)
		{
			const std::string& aTestWord = upper(aCmdStrings[aWordIdx]);
			if( aTestWord == aCheckWord )
			{
				result.type = kCmdKeyWords[i].cmd;
				aCommandWordIdx = aWordIdx;
				break;
			}
		}
		if( result.type != eCmdType_Empty )
			break;
	}

	if( result.type == eCmdType_Empty )
		return result;

	switch(result.type)
	{
	case eCmdType_AddControlsLayer:
		// Need to know the parent layer of new added layer
		result.data2 = theLayerIdx;
		// fall through
	case eCmdType_HoldControlsLayer:
		// Assume last word in the command is the layer name
		if( aCommandWordIdx < aCmdStrings.size() - 1 )
			result.data = getOrCreateLayerID(theBuilder, aCmdStrings.back());
		else
			result.type = eCmdType_Empty;
		break;
	case eCmdType_RemoveControlsLayer:
		// Assume mean own layer if none specified
		result.data = theLayerIdx;
		if( aCommandWordIdx < aCmdStrings.size() - 1 )
		{// If last word matches existing layer, remove it instead
			if( u16* aLayerIdx =
				theBuilder.nameToIdxMap.find(aCmdStrings.back()) )
			{
				result.data = *aLayerIdx;
			}
		}
		break;
	case eCmdType_ChangeMacroSet:
	case eCmdType_UseAbility:
	case eCmdType_ConfirmMenu:
	case eCmdType_CancelMenu:
		// No data field needed (or 0 IS what it should be set to)
		break;
	case eCmdType_SelectAbility:
	case eCmdType_SelectMenu:
	case eCmdType_SelectHotspot:
	case eCmdType_SelectMacro:
	case eCmdType_RewriteMacro:
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
	case eCmdType_SmoothMouseWheel:
	case eCmdType_StepMouseWheel:
		if( aCommandWordIdx >= aCmdStrings.size() - 1 )
		{
			result.type = eCmdType_Empty;
			break;
		}
		// Set data to ECommandDir based on direction
		// Get direction by first character of last word
		switch(aCmdStrings.back()[0])
		{
		case 'U': // Up
		case 'F': // Forward
		case 'P': // Previous
			result.data = eCmdDir_Up;
			break;
		case 'D': // Down
		case 'B': // Back
		case 'N': // Next
			result.data = eCmdDir_Down;
			break;
		case 'L': // Left
			result.data = eCmdDir_Left;
			break;
		case 'R': // Right
			result.data = eCmdDir_Right;
			break;
		default:
			// Can't figure out direction
			result.type = eCmdType_Empty;
			break;
		}
		break;
	default:
		DBG_ASSERT(false && "unhandled special command type");
	}

	return result;
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


static u16 vKeySeqToSingleKey(const std::string& theVKeySeq)
{
	u16 result = 0;
	if( theVKeySeq.size() == 1 )
	{
		result = theVKeySeq[0];
		return result;
	}

	bool hasNonModKey = false;
	for(size_t i = 0; i < theVKeySeq.size(); ++i)
	{
		// If encounter anything else after the first non-mod key,
		// it is must be a sequence of keys rather than of a "single key",
		// and thus can not be used with _PressAndHoldKey or _ReleaseKey.
		if( result & kVKeyMask )
		{
			result = 0;
			return result;
		}

		switch(theVKeySeq[i])
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
		default:
			result |= theVKeySeq[i];
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

	// Check for alias to a keybind
	// Done first in case keybind name happens to contain a command word
	Command aCmd;
	if( u16* aKeyStringIdxPtr =
			theBuilder.keyAliases.find(condense(theCmdStr)) )
	{
		aCmd.type = eCmdType_VKeySequence;
		aCmd.data = *aKeyStringIdxPtr;
	}

	// Check for special command
	if( aCmd.type == eCmdType_Empty )
	{
		theBuilder.parsedString.clear();
		sanitizeSentence(theCmdStr, theBuilder.parsedString);
		aCmd = buildSpecialCommand(theBuilder, theLayerIdx);
	}

	// Check for direct VKey sequence
	if( aCmd.type == eCmdType_Empty )
	{
		// .parsedString was already generated for commands check above
		const std::string& aVKeySeq =
			namesToVKeySequence(theBuilder.parsedString, true);

		if( !aVKeySeq.empty() )
		{
			aCmd.type = eCmdType_VKeySequence;
			aCmd.data = u16(sKeyStrings.size());
			sKeyStrings.push_back(aVKeySeq);
		}
	}

	// Convert eCmdType_VKeySequence to eCmdType_PressAndHoldKey? 
	if( aCmd.type == eCmdType_VKeySequence && aBtnAct == eBtnAct_Down )
	{
		// True "while held down" only works with a single key (w/ mods).
		// If more than one normal key was specified, this will be
		// skipped and _Down will just act like another _Press action
		// (meaning could have 2 commands execute on button press).
		if( u16 aVKey = vKeySeqToSingleKey(sKeyStrings[aCmd.data]) )
		{
			aCmd.type = eCmdType_PressAndHoldKey;
			aCmd.data = aVKey;
		}
	}

	// Check for bad combinations of button+action+command
	if( aCmd.type == eCmdType_RemoveControlsLayer && theLayerIdx == 0 )
	{
		logError("Can not remove root control layer ([%s] %s%s%s = %s)",
			theBuilder.debugSlotName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kButtonActionPrefx[aBtnAct][0] ? " " : "",
			kProfileButtonName[aBtnID],
			theCmdStr.c_str());
		return;
	}
	if( aBtnAct != eBtnAct_Down &&
		(aCmd.type >= eCmdType_FirstContinuous ||
		 aCmd.type == eCmdType_HoldControlsLayer ||
		 aCmd.type == eCmdType_PressAndHoldKey) )
	{
		logError(
			"[%s]: Invalid assignment of '%s %s' action to '%s'! "
			"Must remove the '%s' prefix for this command!",
			theBuilder.debugSlotName.c_str(),
			kButtonActionPrefx[aBtnAct],
			kProfileButtonName[aBtnID],
			theCmdStr.c_str(),
			kButtonActionPrefx[aBtnAct]);
		return;
	}

	// Generic error for inability to parse the request
	// Note that an empty command string still counts as a valid assignment,
	// possibly to block lower layers' assignments from doing anything
	if( aCmd.type == eCmdType_Empty && !theCmdStr.empty() )
	{
		logError("[%s]: Not sure how to assign '%s%s%s' to '%s'",
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


static void assignSpecialKeys(InputMapBuilder& theBuilder)
{
	for(size_t i = 0; i < eSpecialKey_Num; ++i)
	{
		u16* aKeyStringIdxPtr =
			theBuilder.keyAliases.find(kSpecialKeyNames[i]);
		if( !aKeyStringIdxPtr )
			continue;
		if( sKeyStrings[*aKeyStringIdxPtr].size() != 1 )
		{
			logError("Can only assign a single key to %s!",
				kSpecialKeyNames[i]);
			continue;
		}
		sSpecialKeys[i] = sKeyStrings[*aKeyStringIdxPtr][0];
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
	sKeyStrings.clear();
	sLayers.clear();
	sMacroSets.clear();

	// Build control scheme and macros
	{
		InputMapBuilder anInputMapBuilder;
		buildKeyAliases(anInputMapBuilder);
		buildControlScheme(anInputMapBuilder);
		buildMacroSets(anInputMapBuilder);
		assignSpecialKeys(anInputMapBuilder);
	}

	// Trim unused memory
	if( sMacroSets.size() < sMacroSets.capacity() )
		std::vector<MacroSet>(sMacroSets).swap(sMacroSets);
	if( sKeyStrings.size() < sKeyStrings.capacity() )
		std::vector<std::string>(sKeyStrings).swap(sKeyStrings);
	if( sLayers.size() < sLayers.capacity() )
		std::vector<ControlsLayer>(sLayers).swap(sLayers);

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


u8 keyForMoveTurn(ECommandDir theDir)
{
	switch(theDir)
	{
	case eCmdDir_L:	return sSpecialKeys[eSpecialKey_TurnL];
	case eCmdDir_R:	return sSpecialKeys[eSpecialKey_TurnR];
	case eCmdDir_U:	return sSpecialKeys[eSpecialKey_MoveF];
	case eCmdDir_D:	return sSpecialKeys[eSpecialKey_MoveB];
	}

	return 0;
}


u8 keyForMoveStrafe(ECommandDir theDir)
{
	u8 result = 0;

	switch(theDir)
	{
	case eCmdDir_L:	result = sSpecialKeys[eSpecialKey_StrafeL]; break;
	case eCmdDir_R:	result = sSpecialKeys[eSpecialKey_StrafeR]; break;
	}

	if( !result ) result = keyForMoveTurn(theDir);
	return result;
}


u8 keyForMouseLookMoveTurn(ECommandDir theDir)
{
	u8 result = 0;

	switch(theDir)
	{
	case eCmdDir_L:	result = sSpecialKeys[eSpecialKey_MLTurnL]; break;
	case eCmdDir_R:	result = sSpecialKeys[eSpecialKey_MLTurnR]; break;
	case eCmdDir_U:	result = sSpecialKeys[eSpecialKey_MLMoveF]; break;
	case eCmdDir_D:	result = sSpecialKeys[eSpecialKey_MLMoveB]; break;
	}

	if( !result ) result = keyForMoveTurn(theDir);
	return result;
}


u8 keyForMouseLookMoveStrafe(ECommandDir theDir)
{
	u8 result = 0;

	switch(theDir)
	{
	case eCmdDir_L:	result = sSpecialKeys[eSpecialKey_MLStrafeL]; break;
	case eCmdDir_R:	result = sSpecialKeys[eSpecialKey_MLStrafeR]; break;
	}

	if( !result ) result = keyForMoveStrafe(theDir);
	return result;
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
