//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "HotspotMap.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "LayoutEditor.h"
#include "Menus.h"
#include "Profile.h"
#include "WindowManager.h" // readUIScale()

namespace InputTranslator
{

// Uncomment this to print layer order changes and such to debug window
//#define INPUT_TRANSLATOR_DEBUG_PRINT

//-----------------------------------------------------------------------------
// Const Data
//-----------------------------------------------------------------------------

enum {
kMaxLayerChangesPerUpdate = 32,
};


//-----------------------------------------------------------------------------
// Config
//-----------------------------------------------------------------------------

struct Config
{
	u32 tapHoldTime;
	u32 autoRepeatDelay;
	u32 autoRepeatRate;

	void load()
	{
		tapHoldTime = Profile::getInt("System/ButtonTapTime", 500);
		autoRepeatDelay = Profile::getInt("System/AutoRepeatDelay", 400);
		autoRepeatRate = Profile::getInt("System/AutoRepeatRate", 100);
		u8 aThreshold =
			clamp(Profile::getInt("Gamepad/LStickButtonThreshold", 40),
				0, 100) * 255 / 100;
		Gamepad::setPressThreshold(eBtn_LSLeft, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSRight, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSUp, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSDown, aThreshold);
		aThreshold =
			clamp(Profile::getInt("Gamepad/RStickButtonThreshold", 40),
				0, 100) * 255 / 100;
		Gamepad::setPressThreshold(eBtn_RSLeft, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSRight, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSUp, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSDown, aThreshold);
		aThreshold =
			clamp(Profile::getInt("Gamepad/TriggerButtonThreshold", 12),
				0, 100) * 255 / 100;
		Gamepad::setPressThreshold(eBtn_L2, aThreshold);
		Gamepad::setPressThreshold(eBtn_R2, aThreshold);
	}
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ButtonCommandSet
{
	Command cmd[eBtnAct_Num];
	u16 layer[eBtnAct_Num];
	u32 holdTimeForAction;

	void clear()
	{
		for(size_t i = 0; i < eBtnAct_Num; ++i)
		{
			cmd[i] = Command();
			layer[i] = 0;
		}
		holdTimeForAction = 500;
	}
};

struct ButtonState
{
	ButtonCommandSet commands;
	ButtonCommandSet commandsWhenPressed;
	u32 heldTime;
	u16 layerHeld;
	s16 repeatDelay;
	u16 vKeyHeld;
	u16 buttonID;
	bool heldDown;
	bool holdActionDone;
	bool usedInButtonCombo;
	bool allowHotspotToMouseWheel;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		commands.clear();
		commandsWhenPressed.clear();
		layerHeld = 0;
		heldTime = 0;
		repeatDelay = 0;
		buttonID = eBtn_None;
		heldDown = false;
		holdActionDone = false;
		usedInButtonCombo = false;
		allowHotspotToMouseWheel = false;
	}

	void resetWhenReleased()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		commandsWhenPressed.clear();
		heldTime = 0;
		repeatDelay = 0;
		heldDown = false;
		holdActionDone = false;
		usedInButtonCombo = false;
		allowHotspotToMouseWheel = false;
	}

	ButtonState() : vKeyHeld(), layerHeld() { clear(); }
};

struct LayerState
{
	ButtonState autoButton;
	u16 parentLayerID;
	u16 altParentLayerID; // 0 unless is a combo layer
	union{ bool active; bool autoButtonDown; };
	bool autoButtonHit;
	bool buttonCommandUsed;
	bool heldActiveByButton;

	void clear()
	{
		autoButton.clear();
		autoButton.buttonID = eBtn_None;
		parentLayerID = 0;
		altParentLayerID = 0;
		active = false;
		autoButtonHit = false;
		buttonCommandUsed = false;
		heldActiveByButton = false;
	}
};

struct ActiveSignal
{
	u16 signalID;
	u16 layerID;
	Command cmd;
};

struct TranslatorState
{
	ButtonState gamepadButtons[eBtn_Num];
	std::vector<LayerState> layers;
	std::vector<u16> layerOrder;
	std::vector<ActiveSignal> signalCommands;

	void clear()
	{
		for(size_t i = 0; i < ARRAYSIZE(gamepadButtons); ++i)
		{
			gamepadButtons[i].clear();
			gamepadButtons[i].buttonID = u16(i);
		}
		for(size_t i = 0; i < layers.size(); ++i)
			layers[i].autoButton.clear();
		layers.clear();
		layerOrder.clear();
		signalCommands.clear();
	}
};

struct InputResults
{
	std::vector<Command> keys;
	std::vector<Command> strings;
	BitVector<> menuAutoCommandRun;
	s16 charMove;
	s16 charTurn;
	s16 charStrafe;
	s16 mouseMoveX;
	s16 mouseMoveY;
	s16 mouseWheelY;
	bool mouseMoveDigital;
	bool mouseWheelDigital;
	bool mouseWheelStepped;
	bool charMoveStartAutoRun;
	bool charMoveLockMovement;
	bool layerChangeMade;

	void clear()
	{
		keys.clear();
		strings.clear();
		menuAutoCommandRun.clearAndResize(InputMap::menuCount());
		charMove = 0;
		charTurn = 0;
		charStrafe = 0;
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		mouseWheelStepped = false;
		charMoveStartAutoRun = false;
		charMoveLockMovement = false;
		layerChangeMade = false;
	}
};


//-----------------------------------------------------------------------------
// Static Variables
//-----------------------------------------------------------------------------

static Config kConfig;
static TranslatorState sState;
static InputResults sResults;


//-----------------------------------------------------------------------------
// Local Functions
//-----------------------------------------------------------------------------

#ifdef INPUT_TRANSLATOR_DEBUG_PRINT
#define transDebugPrint(...) debugPrint("InputTranslator: " __VA_ARGS__)
#else
#define transDebugPrint(...) ((void)0)
#endif

static void loadLayerData()
{
	DBG_ASSERT(sState.layers.empty());

	sState.layers.reserve(InputMap::controlsLayerCount());
	sState.layers.resize(InputMap::controlsLayerCount());
	for(u16 i = 0; i < sState.layers.size(); ++i)
	{
		sState.layers[i].clear();
		const Command* autoButtonCommands =
			InputMap::commandsForButton(i, eBtn_None);
		if( autoButtonCommands )
		{
			for(size_t aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				sState.layers[i].autoButton.commands.cmd[aBtnAct] =
					autoButtonCommands[aBtnAct];
				sState.layers[i].autoButton.commands.layer[aBtnAct] = i;
			}
		}
		sState.layers[i].autoButton.commands.holdTimeForAction =
			InputMap::commandHoldTime(i, eBtn_None);
	}
}


static void	loadCommandsForCurrentLayers()
{
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx) // skip eBtn_None
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		aBtnState.commands.clear();
		for(std::vector<u16>::const_iterator itr =
			sState.layerOrder.begin();
			itr != sState.layerOrder.end(); ++itr)
		{
			const u16 aLayerID = *itr;
			const Command* aCommandsArray = 
				InputMap::commandsForButton(aLayerID, EButton(aBtnIdx));
			if( !aCommandsArray )
				continue;
			for(size_t aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				if( aCommandsArray[aBtnAct].type == eCmdType_Empty )
					continue;
				aBtnState.commands.cmd[aBtnAct] = aCommandsArray[aBtnAct];
				aBtnState.commands.layer[aBtnAct] = aLayerID;
			}
			if( aCommandsArray[eBtnAct_Hold].type != eCmdType_Empty &&
				aCommandsArray[eBtnAct_Hold].type != eCmdType_Unassigned &&
				(aCommandsArray[eBtnAct_Hold].type != eCmdType_DoNothing ||
				 aCommandsArray[eBtnAct_Tap].type >= eCmdType_FirstValid) )
			{
				aBtnState.commands.holdTimeForAction =
					InputMap::commandHoldTime(aLayerID, EButton(aBtnIdx));
			}
		}
	}

	sState.signalCommands.clear();
	for(size_t i = 0; i < sState.layerOrder.size(); ++i)
	{
		const u16 aLayerID = sState.layerOrder[i];
		const VectorMap<u16, Command>& aSignalsList =
			InputMap::signalCommandsForLayer(aLayerID);
		for(VectorMap<u16, Command>::const_iterator
			itr = aSignalsList.begin();
			itr != aSignalsList.end(); ++itr)
		{
			ActiveSignal aSignalCmd;
			aSignalCmd.signalID = itr->first;
			aSignalCmd.layerID = aLayerID;
			aSignalCmd.cmd = itr->second;
			sState.signalCommands.push_back(aSignalCmd);
		}
	}
}


static void removeControlsLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID != 0);
	if( !sState.layers[theLayerID].active )
		return;

	// Remove given layer ID and any of its "child" layers.
	// Use reverse iteration so children are removed first.
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin(), next_itr = itr;
		itr != sState.layerOrder.rend(); itr = next_itr)
	{
		++next_itr;
		LayerState& aLayer = sState.layers[*itr];
		if( *itr == theLayerID && aLayer.active )
		{
			transDebugPrint("Removing Controls Layer: %s\n",
				InputMap::layerLabel(*itr).c_str());

			// Reset some layer properties
			aLayer.active = false;
			aLayer.buttonCommandUsed = false;
			aLayer.heldActiveByButton = false;
			sResults.layerChangeMade = true;

			// Remove from the layer order and recover iterator to continue
			next_itr = std::vector<u16>::reverse_iterator(
				sState.layerOrder.erase((itr+1).base()));
		}
		else if( (aLayer.parentLayerID == theLayerID ||
				  aLayer.altParentLayerID == theLayerID) &&
				 !aLayer.heldActiveByButton )
		{
			// Need to use recursion so also remove grandchildren, etc
			removeControlsLayer(*itr);
			// Recursion will mess up our iterator beyond reasonable recovery,
			// so just start at the beginning (end) again if removed anything
			next_itr = sState.layerOrder.rbegin();
		}
	}
}


static bool layerIsDescendant(const LayerState& theLayer, u16 theParentLayerID)
{
	if( theLayer.parentLayerID == theParentLayerID )
		return true;

	if( theLayer.parentLayerID == 0 )
		return false;

	return
		layerIsDescendant(
			sState.layers[theLayer.parentLayerID],
			theParentLayerID) ||
		layerIsDescendant(
			sState.layers[theLayer.altParentLayerID],
			theParentLayerID);
}


static std::vector<u16>::iterator layerOrderInsertPos(
	u16 theParentLayerID, s8 thePriority, bool isHeldLayer = false)
{
	// The order for this function only applies to normal, child, or
	// held layers -combo layers have their own special sort function.
	std::vector<u16>::iterator result = sState.layerOrder.begin();

	// First, work backwards until find theParentLayerID's position in the
	// order to make sure insert after it. If it is not found, should end
	// up at the root (0) layer position (or this IS the root layer).
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin();
		itr != sState.layerOrder.rend(); ++itr)
	{
		result = itr.base(); // gets position AFTER itr (in normal direction)
		if( *itr == theParentLayerID )
			break;
	}

	// Move forward until find a layer that should be higher priority
	// (whether due to type, parent hierarchy, or thePriority) and
	// insert just before it.
	while(result != sState.layerOrder.end())
	{
		// Held layers have higher priority over non-held layers
		if( sState.layers[*result].heldActiveByButton && !isHeldLayer )
			break;
		if( !sState.layers[*result].heldActiveByButton && isHeldLayer )
		{
			++result;
			continue;
		}
		// Anything not descended from same parent layer is higher priority
		if( !layerIsDescendant(sState.layers[*result], theParentLayerID) )
			break;
		// Layers descended from same parent but not direct siblings are
		// lower priority than direct children of theParentLayerID
		if( sState.layers[*result].parentLayerID != theParentLayerID )
		{
			++result;
			continue;
		}
		// Layer must be a direct sibling - check its actual priority value
		if( InputMap::layerPriority(*result) > thePriority )
			break;
		// Must be a direct sibling but same or lower priority - move on
		++result;
	}

	return result;
}

static void addComboLayers(u16 theNewLayerID); // forward declare
static void sortComboLayers(); // forward declare

static void moveControlsLayerToTop(u16 theLayerID)
{
	DBG_ASSERT(theLayerID > 0);
	DBG_ASSERT(theLayerID < sState.layers.size());
	DBG_ASSERT(sState.layers[theLayerID].active);
	DBG_ASSERT(sState.layers[theLayerID].altParentLayerID == 0);
	transDebugPrint(
		"Re-sorting Controls Layer '%s' as if it had been newly added\n",
		InputMap::layerLabel(theLayerID).c_str());

	std::vector<u16>::iterator anOldPos = std::find(
		sState.layerOrder.begin(),
		sState.layerOrder.end(),
		theLayerID);
	DBG_ASSERT(anOldPos != sState.layerOrder.end());
	// Make sure to move any descendants along with it
	std::vector<u16>::iterator anOldPosEnd = anOldPos;
	for(++anOldPosEnd; anOldPosEnd != sState.layerOrder.end() &&
		layerIsDescendant(sState.layers[*anOldPosEnd], theLayerID);
		++anOldPosEnd) {}
	const std::vector<u16> aTempOrder(anOldPos, anOldPosEnd);
	sState.layerOrder.erase(anOldPos, anOldPosEnd);

	// Reset "Hold Auto ### =" held times for any moved layer that have
	// not yet executed their hold action (these are usually used for
	// some kind of delayed action after the layer has been left alone
	// for a while, like hiding a HUD element by removing themselves).
	for(size_t i = 0; i < aTempOrder.size(); ++i)
	{
		if( !sState.layers[aTempOrder[i]].autoButton.holdActionDone )
			sState.layers[aTempOrder[i]].autoButton.heldTime = 0;
	}

	// Find new position to add the layers back to
	std::vector<u16>::iterator aNewPos = sState.layerOrder.end();
	if( !sState.layers[theLayerID].heldActiveByButton )
	{
		aNewPos = layerOrderInsertPos(
			sState.layers[theLayerID].parentLayerID,
			InputMap::layerPriority(theLayerID));
	}
	sState.layerOrder.insert(aNewPos, aTempOrder.begin(), aTempOrder.end());
	sResults.layerChangeMade = true;

	// Re-sort any possibly affected combo layers
	sortComboLayers();
}


static void addControlsLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sState.layers.size());

	if( sState.layers[theLayerID].active )
	{
		moveControlsLayerToTop(theLayerID);
		return;
	}
	DBG_ASSERT(!sState.layers[theLayerID].active);

	const u16 aParentLayerID = InputMap::parentLayer(theLayerID);
	DBG_ASSERT(aParentLayerID < sState.layers.size());
	if( aParentLayerID && !sState.layers[aParentLayerID].active )
		addControlsLayer(aParentLayerID);

	if( theLayerID > 0 )
	{
		if( aParentLayerID > 0 )
		{
			transDebugPrint(
				"Adding Controls Layer '%s' as child of Layer '%s'\n",
				InputMap::layerLabel(theLayerID).c_str(),
				InputMap::layerLabel(aParentLayerID).c_str());
		}
		else
		{
			transDebugPrint(
				"Adding Controls Layer '%s'\n",
				InputMap::layerLabel(theLayerID).c_str());
		}
	}

	sState.layerOrder.insert(layerOrderInsertPos(
		aParentLayerID, InputMap::layerPriority(theLayerID)),
		theLayerID);
	LayerState& aLayer = sState.layers[theLayerID];
	aLayer.parentLayerID = aParentLayerID;
	aLayer.altParentLayerID = 0;
	aLayer.active = true;
	aLayer.autoButtonHit = true;
	sResults.layerChangeMade = true;
	addComboLayers(theLayerID);
}


static void addComboLayers(u16 theNewLayerID)
{
	// Find and add any combo layers with theNewLayerID as a base and
	// the other base layer also being active
	bool aLayerWasAdded = false;
	for(u16 i = 1; i < sState.layerOrder.size(); ++i)
	{
		const u16 aLayerID = sState.layerOrder[i];
		if( aLayerID == theNewLayerID )
			continue;
		u16 aComboLayerID = InputMap::comboLayerID(theNewLayerID, aLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active )
		{
			addControlsLayer(aComboLayerID);
			sState.layers[aComboLayerID].parentLayerID = theNewLayerID;
			sState.layers[aComboLayerID].altParentLayerID = aLayerID;
			aLayerWasAdded = true;
			i = 0; // since sState.layerOrder might have changed
		}
		aComboLayerID = InputMap::comboLayerID(aLayerID, theNewLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active )
		{
			addControlsLayer(aComboLayerID);
			sState.layers[aComboLayerID].parentLayerID = aLayerID;
			sState.layers[aComboLayerID].altParentLayerID = theNewLayerID;
			aLayerWasAdded = true;
			i = 0; // since sState.layerOrder might have changed
		}
	}

	// May need to re-sort combo layers into proper positions in layer order
	// Don't waste time doing this unless both a layer was added, and the
	// layer being checked (theNewLayerID) is a normal (non-combo) layer,
	// since could be recursively adding combo layers of combo layers.
	// This will only trigger once get back to done with the original, normal
	// layer and therefore will only do a single full sort at the end.
	if( aLayerWasAdded && !sState.layers[theNewLayerID].altParentLayerID )
		sortComboLayers();
}


static void sortComboLayers()
{
	// General idea is put combo layers directly above their top-most
	// parent layer, but it gets more complex if that location can
	// apply to more than one combo layer, at which point they try to
	// match the relative order of their respective parent layers.
	// This sorting relies on the fact that all combo layers should have
	// only a non-combo layer for their parent, with only altParent
	// having the possibility of being itself another combo layer.
	struct LayerPriority
	{
		u16 oldPos;
		LayerPriority *parent, *altParent;
		u16 get(u16 depth) const
		{
			if( parent != null )
			{
				switch(depth)
				{
				case 0: return max(get(1), get(2));
				case 1: return parent->get(0);
				case 2: return altParent->get(0);
				default: return altParent->get(depth-2);
				}
			}
			else if( depth == 0 )
			{
				return oldPos;
			}
			return 0;
		}
	};
	struct LayerSorter
	{
		u16 id;
		LayerPriority* priority;
		bool operator<(const LayerSorter& rhs) const
		{
			u16 priorityDepth = 0;
			u16 priorityA = priority->get(priorityDepth);
			u16 priorityB = rhs.priority->get(priorityDepth);
			while(priorityA == priorityB && (priorityA || priorityB))
			{
				++priorityDepth;
				priorityA = priority->get(priorityDepth);
				priorityB = rhs.priority->get(priorityDepth);
			}
			return priorityA < priorityB;
		}
	};
	std::vector<LayerPriority> aLayerPriorities(sState.layerOrder.size());
	std::vector<LayerSorter> aLayerSorters(sState.layerOrder.size());
	for(u16 i = 0; i < sState.layerOrder.size(); ++i)
	{
		const u16 aLayerID = sState.layerOrder[i];
		LayerState& aLayer = sState.layers[aLayerID];
		aLayerPriorities[i].oldPos = i;
		aLayerPriorities[i].parent = null;
		aLayerPriorities[i].altParent = null;
		if( aLayer.altParentLayerID )
		{
			for(size_t j = 0; j < sState.layerOrder.size(); ++j)
			{
				if( sState.layerOrder[j] == aLayer.parentLayerID )
					aLayerPriorities[i].parent = &aLayerPriorities[j];
				if( sState.layerOrder[j] == aLayer.altParentLayerID )
					aLayerPriorities[i].altParent = &aLayerPriorities[j];
			}
		}
		aLayerSorters[i].id = sState.layerOrder[i];
		aLayerSorters[i].priority = &aLayerPriorities[i];
	}
	std::sort(aLayerSorters.begin()+1, aLayerSorters.end());
	for(u16 i = 0; i < sState.layerOrder.size(); ++i)
		sState.layerOrder[i] = aLayerSorters[i].id;
}


static void flagLayerButtonCommandUsed(u16 theLayerIdx)
{
	if( theLayerIdx == 0 || sState.layers[theLayerIdx].buttonCommandUsed )
		return;

	sState.layers[theLayerIdx].buttonCommandUsed = true;
	flagLayerButtonCommandUsed(sState.layers[theLayerIdx].parentLayerID);
	flagLayerButtonCommandUsed(sState.layers[theLayerIdx].altParentLayerID);
}


static void releaseKeyHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.vKeyHeld )
	{
		Command aCmd;
		aCmd.type = eCmdType_ReleaseKey;
		aCmd.vKey = theBtnState.vKeyHeld;
		InputDispatcher::sendKeyCommand(aCmd);
		theBtnState.vKeyHeld = 0;
	}
}


static void moveMouseToSelectedMenuItem(const Command& theCmd)
{
	if( theCmd.withMouse )
	{
		Command aMoveCmd;
		aMoveCmd.type = eCmdType_MoveMouseToMenuItem;
		aMoveCmd.menuID = theCmd.menuID;
		aMoveCmd.menuItemIdx = Menus::selectedItem(theCmd.menuID);
		aMoveCmd.andClick = theCmd.andClick;
		InputDispatcher::moveMouseTo(aMoveCmd);
	}
}


static void processCommand(
	ButtonState* theBtnState,
	const Command& theCmd,
	u16 theLayerIdx,
	bool repeated = false)
{
	// Flag when used a command assigned to a layer button (besides Auto)
	if( theBtnState && theLayerIdx && theBtnState->buttonID != eBtn_None )
		flagLayerButtonCommandUsed(theLayerIdx);
		
	Command aForwardCmd;
	switch(theCmd.type)
	{
	case eCmdType_Empty:
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		// Do nothing
		break;
	case eCmdType_SignalOnly:
		// Do nothing but fire off signal
		gFiredSignals.set(theCmd.signalID);
		break;
	case eCmdType_PressAndHoldKey:
		DBG_ASSERT(theBtnState);
		// Release any previously-held key first!
		releaseKeyHeldByButton(*theBtnState);
		// Send this right away since can often be instantly processed
		// instead of needing to wait for input queue
		InputDispatcher::sendKeyCommand(theCmd);
		// Make note that this button is holding this key
		theBtnState->vKeyHeld = theCmd.vKey;
		break;
	case eCmdType_ReleaseKey:
		// Handled by releaseKeyHeldByButton() instead
		DBG_ASSERT(false && "_ReleaseKey should not be directly assigned!");
		break;
	case eCmdType_TapKey:
	case eCmdType_VKeySequence:
		// Queue to send after press/release commands but before strings
		sResults.keys.push_back(theCmd);
		break;
	case eCmdType_ChatBoxString:
		// Queue to send last, since can block other input for a while
		sResults.strings.push_back(theCmd);
		break;
	case eCmdType_MoveMouseToHotspot:
	case eCmdType_MoveMouseToMenuItem:
		// Send right away, to happen before a queued mouse click
		InputDispatcher::moveMouseTo(theCmd);
		break;
	case eCmdType_KeyBindArrayResetLast:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayDefaultIndex[theCmd.keybindArrayID],
				0, false);
		break;
	case eCmdType_KeyBindArraySetDefault:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayDefaultIndex.size());
		gKeyBindArrayDefaultIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayLastIndex[theCmd.keybindArrayID],
				0, false);
		gKeyBindArrayDefaultIndexChanged.set(theCmd.keybindArrayID);
		break;
	case eCmdType_KeyBindArrayPrev:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayLastIndex[theCmd.keybindArrayID],
			-s16(theCmd.count), theCmd.wrap);
		aForwardCmd = InputMap::keyBindArrayCommand(
			theCmd.keybindArrayID,
			gKeyBindArrayLastIndex[theCmd.keybindArrayID]);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindArrayLastIndexChanged.set(theCmd.keybindArrayID);
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
		break;
	case eCmdType_KeyBindArrayNext:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayLastIndex[theCmd.keybindArrayID],
				s16(theCmd.count), theCmd.wrap);
		aForwardCmd = InputMap::keyBindArrayCommand(
			theCmd.keybindArrayID,
			gKeyBindArrayLastIndex[theCmd.keybindArrayID]);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindArrayLastIndexChanged.set(theCmd.keybindArrayID);
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
		break;
	case eCmdType_KeyBindArrayDefault:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayDefaultIndex[theCmd.keybindArrayID],
				0, false);
		aForwardCmd = InputMap::keyBindArrayCommand(
			theCmd.keybindArrayID,
			gKeyBindArrayLastIndex[theCmd.keybindArrayID]);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindArrayLastIndexChanged.set(theCmd.keybindArrayID);
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
		break;
	case eCmdType_KeyBindArrayLast:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayLastIndex[theCmd.keybindArrayID],
				0, false);
		aForwardCmd = InputMap::keyBindArrayCommand(
			theCmd.keybindArrayID,
			gKeyBindArrayLastIndex[theCmd.keybindArrayID]);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindArrayLastIndexChanged.set(theCmd.keybindArrayID);
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
		break;
	case eCmdType_KeyBindArrayIndex:
	case eCmdType_KeyBindArrayHoldIndex:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID, theCmd.arrayIdx,
				0, false);
		aForwardCmd = InputMap::keyBindArrayCommand(
			theCmd.keybindArrayID,
			gKeyBindArrayLastIndex[theCmd.keybindArrayID]);
		if( theCmd.type == eCmdType_KeyBindArrayHoldIndex )
		{
			DBG_ASSERT(aForwardCmd.type == eCmdType_TapKey);
			aForwardCmd.type = eCmdType_PressAndHoldKey;
		}
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindArrayLastIndexChanged.set(theCmd.keybindArrayID);
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
		break;
	case eCmdType_StartAutoRun:
		sResults.charMoveStartAutoRun = true;
		if( theCmd.multiDirAutoRun )
			sResults.charMoveLockMovement = true;
		break;
	case eCmdType_ChangeProfile:
		Profile::queryUserForProfile();
		gReloadProfile = true;
		break;
	case eCmdType_EditLayout:
		LayoutEditor::init();
		break;
	case eCmdType_UpdateUIScale:
		// TODO - more with this
		WindowManager::readUIScale();
		break;
	case eCmdType_QuitApp:
		gShutdown = true;
		break;
	case eCmdType_AddControlsLayer:
		addControlsLayer(theCmd.layerID);
		break;
	case eCmdType_RemoveControlsLayer:
		if( theCmd.layerID == 0 )
			removeControlsLayer(theLayerIdx);
		else
			removeControlsLayer(theCmd.layerID);
		break;
	case eCmdType_HoldControlsLayer:
		// Special-case, handled elsewhere
		break;
	case eCmdType_ReplaceControlsLayer:
		DBG_ASSERT(theCmd.replacementLayer > 0);
		if( theCmd.layerID == 0 )
			removeControlsLayer(theLayerIdx);
		else
			removeControlsLayer(theCmd.layerID);
		addControlsLayer(theCmd.replacementLayer);
		break;
	case eCmdType_ToggleControlsLayer:
		aForwardCmd = theCmd;
		DBG_ASSERT(theCmd.layerID > 0);
		if( sState.layers[theCmd.layerID].active )
			removeControlsLayer(theCmd.layerID);
		else
			addControlsLayer(theCmd.layerID);
		break;
	case eCmdType_OpenSubMenu:
		aForwardCmd = Menus::openSubMenu(theCmd.menuID, theCmd.subMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		sResults.menuAutoCommandRun.set(theCmd.menuID);
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_ReplaceMenu:
		aForwardCmd = Menus::replaceMenu(theCmd.menuID, theCmd.subMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		sResults.menuAutoCommandRun.set(theCmd.menuID);
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_MenuReset:
		if( const Command* aCmdPtr = Menus::reset(theCmd.menuID) )
		{
			processCommand(theBtnState, *aCmdPtr, theLayerIdx);
			sResults.menuAutoCommandRun.set(theCmd.menuID);
		}
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_MenuConfirm:
		moveMouseToSelectedMenuItem(theCmd);
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.menuID);
		aForwardCmd.withMouse = theCmd.withMouse;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuConfirmAndClose:
		moveMouseToSelectedMenuItem(theCmd);
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.menuID);
		aForwardCmd.withMouse = theCmd.withMouse;
		if( aForwardCmd.type >= eCmdType_FirstValid )
		{
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			// Close menu first if didn't just switch to a sub-menu
			if( aForwardCmd.type < eCmdType_FirstMenuControl ||
				aForwardCmd.type > eCmdType_LastMenuControl )
			{
				processCommand(theBtnState,
					Menus::closeCommand(theCmd.menuID), theLayerIdx);
			}
		}
		break;
	case eCmdType_MenuBack:
		processCommand(theBtnState,
			Menus::backCommand(theCmd.menuID), theLayerIdx);
		if( const Command* aCmdPtr = Menus::closeLastSubMenu(theCmd.menuID) )
		{
			processCommand(theBtnState, *aCmdPtr, theLayerIdx);
			moveMouseToSelectedMenuItem(theCmd);
		}
		break;
	case eCmdType_MenuClose:
		processCommand(theBtnState,
			Menus::closeCommand(theCmd.menuID), theLayerIdx);
		break;
	case eCmdType_MenuEdit:
		Menus::editMenuItem(theCmd.menuID);
		break;
	case eCmdType_MenuSelect:
		for(int i = 0; i < theCmd.count; ++i)
		{
			aForwardCmd = Menus::selectMenuItem(
				theCmd.menuID, ECommandDir(theCmd.dir),
				theCmd.wrap, repeated || i < theCmd.count-1);
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_MenuSelectAndClose:
		for(int i = 0; i < theCmd.count; ++i)
		{
			aForwardCmd = Menus::selectMenuItem(
				theCmd.menuID, ECommandDir(theCmd.dir),
				theCmd.wrap, repeated || i < theCmd.count-1);
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			// Close menu if didn't just switch to a sub-menu
			if( aForwardCmd.type < eCmdType_FirstMenuControl ||
				aForwardCmd.type > eCmdType_LastMenuControl )
			{
				processCommand(theBtnState,
					Menus::closeCommand(theCmd.menuID), theLayerIdx);
				return;
			}
		}
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_MenuEditDir:
		Menus::editMenuItemDir(theCmd.menuID, ECommandDir(theCmd.dir));
		break;
	case eCmdType_HotspotSelect:
		if( u16 aNextHotspot =
				HotspotMap::getNextHotspotInDir(ECommandDir(theCmd.dir)) )
		{
			aForwardCmd.type = eCmdType_MoveMouseToHotspot;
			aForwardCmd.hotspotID = aNextHotspot;
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			if( theBtnState )
				theBtnState->allowHotspotToMouseWheel = false;
		}
		else if( theCmd.withMouse )
		{
			if( theBtnState )
				theBtnState->allowHotspotToMouseWheel = true;
		}
		break;
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Invalid command for this function!
		DBG_ASSERT(false && "Invalid command sent to processCommand()");
		break;
	case eCmdType_MouseWheel:
		if( theCmd.mouseWheelMotionType == eMouseWheelMotion_Jump )
		{
			InputDispatcher::jumpMouseWheel(
				ECommandDir(theCmd.dir),
				theCmd.count);
			break;
		}
		// fall through
	default:
		// Invalid command for this function!
		DBG_ASSERT(false && "Invalid command sent to processCommand()");
		break;
	}
}


static bool isTapOnlyCommand(const Command& theCommand)
{
	switch(theCommand.type)
	{
	case eCmdType_PressAndHoldKey:
	case eCmdType_KeyBindArrayHoldIndex:
	case eCmdType_HoldControlsLayer:
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
	case eCmdType_MouseWheel:
		return false;
	}

	return true;
}


static void processButtonPress(ButtonState& theBtnState)
{
	// Pressing a button fires one of the first set of signal bits
	gFiredSignals.set(theBtnState.buttonID);

	// When first pressed, back up copy of current commands to be referenced
	// by other button actions later. This makes sure if layers change
	// before the button is released, it will still behave as it would
	// previously, which could be important - especially for cases like
	// _Release matching up with a particular _Press.
	theBtnState.commandsWhenPressed = theBtnState.commands;

	// _Press is processed before _Down since it is specifically called out
	// and the name implies it should be first action on button press.
	processCommand(&theBtnState,
		theBtnState.commands.cmd[eBtnAct_Press],
		theBtnState.commands.layer[eBtnAct_Press]);

	// Get _Down command for this button (default when no action specified)
	const Command& aCmd = theBtnState.commands.cmd[eBtnAct_Down];
	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Handled in processContinuousInput instead
		return;
	case eCmdType_MouseWheel:
		// Handled in processContinuousInput instead unless set to _Jump
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Jump )
			return;
		break;
	}
	processCommand(&theBtnState, aCmd,
		theBtnState.commands.layer[eBtnAct_Down]);
}


static void processContinuousInput(
	ButtonState& theBtnState,
	u8 theAnalogVal,
	bool isDigitalDown)
{
	// Use commandsWhenPressed if has a continuous action, otherwise current
	// These inputs should always be assigned to the _Down button action
	Command aCmd = theBtnState.commandsWhenPressed.cmd[eBtnAct_Down];
	if( aCmd.type < eCmdType_FirstValid || isTapOnlyCommand(aCmd) )
		aCmd = theBtnState.commands.cmd[eBtnAct_Down];

	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Continue to analog checks below
		break;
	case eCmdType_MouseWheel:
		// Continue to analog checks below unless set to _Jump
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Jump )
			break;
		return;
	case eCmdType_HotspotSelect:
		// Continue if set to "Select Hotspot or MouseWheel" and no hotspot
		if( aCmd.withMouse && theBtnState.allowHotspotToMouseWheel )
			break;
	default:
		// Handled elsewhere
		return;
	}

	if( theAnalogVal )
		isDigitalDown = false;
	else if( isDigitalDown )
		theAnalogVal = 255;
	else // if( theAnalogVal == 0 && !isDigitalDown )
		return;

	switch(u32(aCmd.type << 16) | aCmd.dir)
	{
	case (eCmdType_MoveTurn << 16) | eCmdDir_Forward:
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Forward:
		sResults.charMove += theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Back:
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Back:
		sResults.charMove -= theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Left:
		sResults.charTurn -= theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Right:
		sResults.charTurn += theAnalogVal;
		break;
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Left:
		sResults.charStrafe -= theAnalogVal;
		break;
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Right:
		sResults.charStrafe += theAnalogVal;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Left:
		sResults.mouseMoveX -= theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Right:
		sResults.mouseMoveX += theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Up:
		sResults.mouseMoveY -= theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MoveMouse << 16) | eCmdDir_Down:
		sResults.mouseMoveY += theAnalogVal;
		sResults.mouseMoveDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Up:
	case (eCmdType_HotspotSelect << 16) | eCmdDir_Up:
		if( aCmd.mouseWheelMotionType == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY -= theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Down:
	case (eCmdType_HotspotSelect << 16) | eCmdDir_Down:
		if( aCmd.mouseWheelMotionType == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY += theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Left:
	case (eCmdType_MouseWheel << 16) | eCmdDir_Right:
	case (eCmdType_HotspotSelect << 16) | eCmdDir_Left:
	case (eCmdType_HotspotSelect << 16) | eCmdDir_Right:
		// Ignore (multi-directional assigned)
		break;
	default:
		DBG_ASSERT(false && "Command and direction combo invalid");
		break;
	}
}


static void processAutoRepeat(ButtonState& theBtnState)
{
	if( kConfig.autoRepeatRate == 0 || theBtnState.buttonID == eBtn_None )
		return;

	// Auto-repeat only commandsWhenPressed assigned to _Down
	const ButtonCommandSet& aCmdSet = theBtnState.commandsWhenPressed;
	Command aCmd = aCmdSet.cmd[eBtnAct_Down];

	// Filter out which commands can use auto-repeat safely
	switch(aCmd.type)
	{
	case eCmdType_MenuSelect:
	case eCmdType_MenuSelectAndClose:
	case eCmdType_KeyBindArrayPrev:
	case eCmdType_KeyBindArrayNext:
		// Continue to further checks below
		break;
	case eCmdType_HotspotSelect:
		// Continue to further checks unless are using as a mouse wheel cmd
		if( !aCmd.withMouse || !theBtnState.allowHotspotToMouseWheel )
			break;
		return;
	default:
		// Incompatible with this feature
		return;
	}
	
	// Don't auto-repeat when button has other conflicting actions assigned
	if( aCmdSet.cmd[eBtnAct_Tap].type >= eCmdType_FirstValid ||
		aCmdSet.cmd[eBtnAct_Hold].type >= eCmdType_FirstValid )
		return;

	// Needs to be held for initial held time first before start repeating
	if( theBtnState.heldTime < kConfig.autoRepeatDelay )
		return;

	// Now can start using repeatDelay to re-send command at autoRepeatRate
	if( theBtnState.repeatDelay <= 0 )
	{
		processCommand(&theBtnState, aCmd,
			theBtnState.commandsWhenPressed.layer[eBtnAct_Down],
			true);
		theBtnState.repeatDelay += kConfig.autoRepeatRate;
	}
	theBtnState.repeatDelay -= gAppFrameTime;
}


static void processButtonHold(ButtonState& theBtnState)
{
	if( theBtnState.holdActionDone )
		return;

	// Only ever use commandsWhenPressed for hold action
	if( theBtnState.heldTime <
			theBtnState.commandsWhenPressed.holdTimeForAction )
		return;

	theBtnState.holdActionDone = true;

	// Skip if this button was used as a modifier to execute a button
	// combination command (i.e. is L2 for the combo L2+X).
	if( theBtnState.usedInButtonCombo )
		return;

	processCommand(&theBtnState,
		theBtnState.commandsWhenPressed.cmd[eBtnAct_Hold],
		theBtnState.commandsWhenPressed.layer[eBtnAct_Hold]);
}


static void processButtonTap(ButtonState& theBtnState)
{
	// Should not even call this if button was used in a button combo
	DBG_ASSERT(!theBtnState.usedInButtonCombo);

	// If has a hold command, then a "tap" is just releasing before said
	// command had a chance to execute. Otherwise it is based on holding
	// less than tapHoldTime before releasing.
	const bool hasHold =
		theBtnState.commandsWhenPressed.cmd[eBtnAct_Hold]
			.type >= eCmdType_FirstValid;

	// Only ever use commandsWhenPressed for tap action
	if( (hasHold && !theBtnState.holdActionDone) ||
		(!hasHold && theBtnState.heldTime < kConfig.tapHoldTime) )
	{
		processCommand(&theBtnState,
			theBtnState.commandsWhenPressed.cmd[eBtnAct_Tap],
			theBtnState.commandsWhenPressed.layer[eBtnAct_Tap]);
	}
}


static void processButtonReleased(ButtonState& theBtnState)
{
	// Always release any key being held by this button when button is released
	releaseKeyHeldByButton(theBtnState);

	// If this button was used as a modifier to execute a button combination
	// command (i.e. is L2 for the combo L2+X), then do not perform any other
	// actions for this button besides releasing held key and resetting state.
	if( theBtnState.usedInButtonCombo )
	{
		theBtnState.resetWhenReleased();
		return;
	}

	// If released quickly enough, process 'tap' event
	processButtonTap(theBtnState);

	// Use commandsWhenPressed if it has a _Release action.
	// If not, can use the _Release action for current setup instead,
	// but ONLY if current commands do NOT have an associated _Press
	// action along with the _Release. This exception is to handle a
	// case where a layer was added that intends to be removed by
	// releasing a held button, but for some reason didn't use the
	// standard _HoldControlsLayer method that does it automatically
	// or just assign the _Release on the same layer that added it.
	Command aCmd = theBtnState.commandsWhenPressed.cmd[eBtnAct_Release];
	u16 aCmdLayer = theBtnState.commandsWhenPressed.layer[eBtnAct_Release];
	if( aCmd.type < eCmdType_FirstValid &&
		theBtnState.commands.cmd[eBtnAct_Press].type < eCmdType_FirstValid )
	{
		aCmd = theBtnState.commands.cmd[eBtnAct_Release];
		aCmdLayer = theBtnState.commands.layer[eBtnAct_Release];
	}
	processCommand(&theBtnState, aCmd, aCmdLayer);

	// At this point no keys should be held by this button,
	// but maybe something weird happened with release/tap commands,
	// so check again to make sure
	releaseKeyHeldByButton(theBtnState);

	// Reset most of the button state when button is released
	theBtnState.resetWhenReleased();
}


static void processButtonState(
	ButtonState& theBtnState,
	bool isDown, bool wasHit, u8 theAnalogVal = 0)
{
	const bool wasDown = theBtnState.heldDown;

	// If button was down previously and has either been hit again or
	// isn't down any more, then it must have been released, and we should
	// process that before any new hits so use commandsWhenPressed.
	if( wasDown && (!isDown || wasHit) )
		processButtonReleased(theBtnState);

	// If was hit (or somehow is down but wasn't before), process a press
	if( wasHit || (isDown && !wasDown) )
	{
		processButtonPress(theBtnState);
		// Now that have processed a press, if it's not down any more,
		// it must have already been released, so process that as well.
		if( !isDown )
			processButtonReleased(theBtnState);
	}

	// Process continuous input, such as for analog axis values which
	// which do not necessarily return hit/release when lightly pressed
	processContinuousInput(theBtnState, theAnalogVal, isDown);

	// Update heldTime value and see if need to process a 'hold' event
	if( isDown )
	{
		theBtnState.heldDown = true;

		// If this button is holding a layer active and another button that
		// is assigned a command from that layer was pressed, then flag
		// this button as being used as the modifier key in a button combo.
		if( theBtnState.layerHeld &&
			sState.layers[theBtnState.layerHeld].buttonCommandUsed )
		{
			theBtnState.usedInButtonCombo = true;
		}

		if( wasDown )
		{
			theBtnState.heldTime += gAppFrameTime;
			processButtonHold(theBtnState);
		}

		// Process auto-repeat feature for certain commands
		processAutoRepeat(theBtnState);
	}
}


static void releaseLayerHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.layerHeld )
	{
		// Flag if used in a button combo, like L2 in L2+X
		if( sState.layers[theBtnState.layerHeld].buttonCommandUsed )
			theBtnState.usedInButtonCombo = true;
		removeControlsLayer(theBtnState.layerHeld);
		theBtnState.layerHeld = 0;
	}
}


static bool tryAddLayerFromButton(
	ButtonState& theBtnState, bool isDown, bool wasHit)
{
	// A hit and quick release again doesn't matter for holding layers active,
	// so if the button isn't down right now then just release held layer.
	if( !isDown )
	{
		releaseLayerHeldByButton(theBtnState);
		return false;
	}

	// Only concerned with buttons that have _HoldControlsLayer for _Down
	// (in current 'commands', not necessarily 'commandsWhenPressed'!)
	const Command& aDownCmd = theBtnState.commands.cmd[eBtnAct_Down];
	if( aDownCmd.type != eCmdType_HoldControlsLayer )
		return false;

	// Only concerned with layers that aren't already active
	const u16 aLayerID = aDownCmd.layerID;
	if( sState.layers[aLayerID].active )
		return false;

	// If newly hit, replace any past layer with new layer
	if( wasHit )
	{
		DBG_ASSERT(aLayerID < sState.layers.size());
		DBG_ASSERT(aLayerID > 0);
		releaseLayerHeldByButton(theBtnState);

		transDebugPrint(
			"Holding Controls Layer '%s'\n",
			InputMap::layerLabel(aLayerID).c_str());

		sState.layerOrder.insert(layerOrderInsertPos(
			0, InputMap::layerPriority(aLayerID), true),
			aLayerID);
		LayerState& aLayer = sState.layers[aLayerID];
		aLayer.parentLayerID = 0;
		aLayer.altParentLayerID = 0;
		aLayer.active = true;
		aLayer.autoButtonHit = true;
		aLayer.heldActiveByButton = true;
		sResults.layerChangeMade = true;
		theBtnState.layerHeld = aLayerID;
		addComboLayers(aLayerID);
		return true;
	}

	return false;
}


static void processLayerHoldButtons()
{
	int aLoopCount = 0;
	bool aLayerWasAdded = true;
	while(aLayerWasAdded)
	{
		aLayerWasAdded = false;
		for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
		{
			aLayerWasAdded = aLayerWasAdded ||
				tryAddLayerFromButton(
					sState.gamepadButtons[i],
					Gamepad::buttonDown(EButton(i)),
					Gamepad::buttonHit(EButton(i)));
		}
		if( !aLayerWasAdded )
		{
			for(size_t i = 0; i < sState.layers.size(); ++i)
			{
				LayerState& aLayer = sState.layers[i];
				aLayerWasAdded = aLayerWasAdded ||
					tryAddLayerFromButton(
						aLayer.autoButton,
						aLayer.autoButtonDown,
						aLayer.autoButtonHit);
			}
		}

		// Update button commands for newly added layers
		// (removed layers & HUD will be handled later in main update)
		if( aLayerWasAdded )
		{
			loadCommandsForCurrentLayers();
			if( ++aLoopCount > kMaxLayerChangesPerUpdate )
			{
				logFatalError(
					"Infinite loop of Controls Layer changes detected!");
				break;
			}
		}
	}
}


static void updateHUDStateForCurrentLayers()
{
	BitVector<> aPrevVisibleHUD = gVisibleHUD;
	BitVector<> aPrevDisabledHUD = gDisabledHUD;
	gVisibleHUD.reset();
	for(u16 i = 0; i < sState.layerOrder.size(); ++i)
	{
		gVisibleHUD |=
			InputMap::hudElementsToShow(sState.layerOrder[i]);
		gVisibleHUD &=
			~InputMap::hudElementsToHide(sState.layerOrder[i]);
	}

	// Also check to see if any Menus should be shown as disabled because
	// they don't have any commands assigned to them any more.
	// Start by assuming all visible menus are disabled...
	for(u16 i = 0; i < InputMap::hudElementCount(); ++i)
	{
		if( InputMap::hudElementIsAMenu(i) && gVisibleHUD.test(i) )
			gDisabledHUD.set(i);
	}

	// Now re-enable any menus that have a command associated with them
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx)
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		for(size_t aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
		{
			u16 aHUDIdx = 0xFFFF;
			switch(aBtnState.commands.cmd[aBtnAct].type)
			{
			case eCmdType_MenuReset:
			case eCmdType_MenuConfirm:
			case eCmdType_MenuConfirmAndClose:
			case eCmdType_MenuBack:
			case eCmdType_MenuEdit:
			case eCmdType_MenuSelect:
			case eCmdType_MenuSelectAndClose:
			case eCmdType_MenuEditDir:
				aHUDIdx = InputMap::hudElementForMenu(
					aBtnState.commands.cmd[aBtnAct].menuID);
				break;
			default:
				continue;
			}
			DBG_ASSERT(aHUDIdx < gDisabledHUD.size());
			gDisabledHUD.reset(aHUDIdx);
		}
	}

	// Run Auto command for any newly-visible or newly-enabled menus
	// Also re-draw menus that changed disabled status to change if
	// selected item is drawn differently or not
	for(int i = gVisibleHUD.firstSetBit();
		i < gVisibleHUD.size();
		i = gVisibleHUD.nextSetBit(i+1))
	{
		if( !InputMap::hudElementIsAMenu(i) )
			continue;
		const u16 aMenuID = InputMap::menuForHUDElement(i);
		const bool wasDisabled = aPrevDisabledHUD.test(i);
		const bool isDisabled = gDisabledHUD.test(i);
		if( wasDisabled != isDisabled )
			gRedrawHUD.set(i);
		if( sResults.menuAutoCommandRun.test(aMenuID) )
			continue;
		if( !aPrevVisibleHUD.test(i) || (wasDisabled && !isDisabled) )
		{
			processCommand(null,Menus::autoCommand(aMenuID), 0);
			sResults.menuAutoCommandRun.set(i);
		}
	}
}


static void updateHotspotArraysForCurrentLayers()
{
	BitVector<> aHotspotArraysEnabled;
	aHotspotArraysEnabled.clearAndResize(InputMap::hotspotArrayCount());
	for(u16 i = 0; i < sState.layerOrder.size(); ++i)
	{
		aHotspotArraysEnabled |=
			InputMap::hotspotArraysToEnable(sState.layerOrder[i]);
		aHotspotArraysEnabled &=
			~InputMap::hotspotArraysToDisable(sState.layerOrder[i]);
	}
	HotspotMap::setEnabledHotspotArrays(aHotspotArraysEnabled);
}


static void updateMouseModeForCurrentLayers()
{
	EMouseMode aMouseMode = eMouseMode_Cursor;
	for(u16 i = 0; i < sState.layerOrder.size(); ++i)
	{
		EMouseMode aLayerMouseMode =
			InputMap::mouseMode(sState.layerOrder[i]);
		// _Default means just use lower layers' mode
		if( aLayerMouseMode == eMouseMode_Default )
			continue;
		if( aLayerMouseMode == eMouseMode_HideOrLook )
		{
			// Act like _Default unless currently set to show cursor,
			// in which case act like _Hide
			if( aMouseMode == eMouseMode_Cursor )
				aMouseMode = eMouseMode_Hide;
			continue;
		}
		aMouseMode = aLayerMouseMode;
	}

	InputDispatcher::setMouseMode(aMouseMode);
}


//-----------------------------------------------------------------------------
// Global Functions
//-----------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
	sState.clear();
	sResults.clear();
	loadLayerData();
	addControlsLayer(0);
	loadCommandsForCurrentLayers();
	updateHUDStateForCurrentLayers();
	updateMouseModeForCurrentLayers();
}


void cleanup()
{
	for(size_t i = 0; i < ARRAYSIZE(sState.gamepadButtons); ++i)
		releaseKeyHeldByButton(sState.gamepadButtons[i]);
	for(size_t i = 0; i < sState.layers.size(); ++i)
		releaseKeyHeldByButton(sState.layers[i].autoButton);
	sState.clear();
	sResults.clear();
}


void update()
{
	// Buttons that 'hold' layers need to be checked first, in order to make
	// sure that combinations enabled through layers like 'L2+X' work when
	// both buttons are pressed at exactly the same time.
	processLayerHoldButtons();

	// Treat each layer as also being a virtual button ("Auto" button)
	for(size_t i = 0; i < sState.layers.size(); ++i)
	{
		LayerState& aLayer = sState.layers[i];
		processButtonState(
			aLayer.autoButton,
			aLayer.autoButtonDown,
			aLayer.autoButtonHit);
		aLayer.autoButtonHit = false;
	}

	// Process state changes of actual physical Gamepad buttons
	for(size_t i = 1; i < eBtn_Num; ++i) // skip eBtn_None
	{
		processButtonState(
			sState.gamepadButtons[i],
			Gamepad::buttonDown(EButton(i)),
			Gamepad::buttonHit(EButton(i)),
			Gamepad::buttonAnalogVal(EButton(i)));
	}

	// Execute commands by active layers in response to fired signals
	BitVector<> aFiredSignals(gFiredSignals);
	gFiredSignals.reset();
	aFiredSignals.reset(eBtn_None);
	if( aFiredSignals.any() )
	{
		for(size_t i = 0; i < sState.signalCommands.size(); ++i)
		{
			ActiveSignal& aSignalCmd = sState.signalCommands[i];
			DBG_ASSERT(aSignalCmd.signalID < aFiredSignals.size());
			if( aFiredSignals.test(aSignalCmd.signalID) )
				processCommand(null, aSignalCmd.cmd, aSignalCmd.layerID);
		}
	}

	if( sResults.layerChangeMade )
	{
		// Process any newly-added layers' autoButtonHit events
		int aLoopCount = 0;
		while(sResults.layerChangeMade)
		{
			sResults.layerChangeMade = false;
			for(size_t i = 0; i < sState.layers.size(); ++i)
			{
				LayerState& aLayer = sState.layers[i];
				if( aLayer.autoButtonHit )
				{
					processButtonState(
						aLayer.autoButton,
						aLayer.autoButtonDown,
						aLayer.autoButtonHit);
					aLayer.autoButtonHit = false;
				}
			}
			if( ++aLoopCount > kMaxLayerChangesPerUpdate )
			{
				logFatalError(
					"Infinite loop of Controls Layer changes detected!");
				break;
			}
		}
		sResults.layerChangeMade = true;
	}

	// Update mouse mode to reflect new layer layout
	if( sResults.layerChangeMade )
		updateMouseModeForCurrentLayers();

	// Send input that was queued up by any of the above
	InputDispatcher::moveCharacter(
		sResults.charMove,
		sResults.charTurn,
		sResults.charStrafe,
		sResults.charMoveStartAutoRun,
		sResults.charMoveLockMovement);
	InputDispatcher::moveMouse(
		sResults.mouseMoveX,
		sResults.mouseMoveY,
		sResults.mouseMoveDigital);
	InputDispatcher::scrollMouseWheel(
		sResults.mouseWheelY,
		sResults.mouseWheelDigital,
		sResults.mouseWheelStepped);
	for(size_t i = 0; i < sResults.keys.size(); ++i)
		InputDispatcher::sendKeyCommand(sResults.keys[i]);
	for(size_t i = 0; i < sResults.strings.size(); ++i)
		InputDispatcher::sendKeyCommand(sResults.strings[i]);

	// Clear results for next update
	const bool aLayerOrderChanged = sResults.layerChangeMade;
	sResults.clear();

	// Update settings for new layer order to use during next update
	if( aLayerOrderChanged )
	{
		loadCommandsForCurrentLayers();
		updateHUDStateForCurrentLayers();
		updateHotspotArraysForCurrentLayers();
		#ifndef NDEBUG
		std::string aNewLayerOrder("Layers: ");
		for(std::vector<u16>::iterator itr =
			sState.layerOrder.begin();
			itr != sState.layerOrder.end(); ++itr)
		{
			aNewLayerOrder += InputMap::layerLabel(*itr);
			if( itr + 1 != sState.layerOrder.end() )
				aNewLayerOrder += " < ";
		}
		aNewLayerOrder += "\n";
		transDebugPrint("%s", aNewLayerOrder.c_str());
		#endif
	}
}

#undef transDebugPrint
#undef INPUT_TRANSLATOR_DEBUG_PRINT

} // InputTranslator
