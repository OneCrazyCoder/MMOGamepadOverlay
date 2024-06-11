//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "HotspotMap.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "Menus.h"
#include "Profile.h"
#include "WindowManager.h" // readUIScale()

namespace InputTranslator
{

// Whether or not debug messages print depends on which line is commented out
//#define transDebugPrint(...) debugPrint("InputTranslator: " __VA_ARGS__)
#define transDebugPrint(...) ((void)0)

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
	u16 tapHoldTime;
	u16 autoRepeatDelay;
	u16 autoRepeatRate;
	u8 defaultThreshold[eBtn_Num];

	void load()
	{
		tapHoldTime = Profile::getInt("System/ButtonTapTime", 500);
		autoRepeatDelay = Profile::getInt("System/AutoRepeatDelay", 400);
		autoRepeatRate = Profile::getInt("System/AutoRepeatRate", 100);
		for(size_t i = 0; i < eBtn_Num; ++i)
			defaultThreshold[i] = 0;
		u8 aThreshold =
			clamp(Profile::getInt("Gamepad/LStickButtonThreshold", 40),
				0, 100) * 255 / 100;
		defaultThreshold[eBtn_LSLeft] = aThreshold;
		defaultThreshold[eBtn_LSRight] = aThreshold;
		defaultThreshold[eBtn_LSUp] = aThreshold;
		defaultThreshold[eBtn_LSDown] = aThreshold;
		aThreshold =
			clamp(Profile::getInt("Gamepad/RStickButtonThreshold", 40),
				0, 100) * 255 / 100;
		defaultThreshold[eBtn_RSLeft] = aThreshold;
		defaultThreshold[eBtn_RSRight] = aThreshold;
		defaultThreshold[eBtn_RSUp] = aThreshold;
		defaultThreshold[eBtn_RSDown] = aThreshold;
		aThreshold =
			clamp(Profile::getInt("Gamepad/TriggerButtonThreshold", 12),
				0, 100) * 255 / 100;
		defaultThreshold[eBtn_L2] = aThreshold;
		defaultThreshold[eBtn_R2] = aThreshold;
	}
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct WithActionCommand
{
	const Command* cmd;
	u16 layerID;
};

struct ButtonState
{
	const Command* commands;
	const Command* commandsWhenPressed;
	std::vector<WithActionCommand> withCommands;
	u16 commandsLayer;
	u16 layerWhenPressed;
	u16 layerHeld;
	u16 heldTime;
	u16 holdTimeForAction;
	s16 repeatDelay;
	u16 vKeyHeld;
	bool isAutoButton;
	bool heldDown;
	bool holdActionDone;
	bool usedInButtonCombo;
	bool allowHotspotToMouseWheel;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		commands = null;
		commandsWhenPressed = null;
		withCommands.clear();
		commandsLayer = 0;
		layerWhenPressed = 0;
		layerHeld = 0;
		heldTime = 0;
		holdTimeForAction = 0;
		repeatDelay = 0;
		isAutoButton = false;
		heldDown = false;
		holdActionDone = false;
		usedInButtonCombo = false;
		allowHotspotToMouseWheel = false;
	}

	void resetWhenReleased()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		commandsWhenPressed = null;
		layerWhenPressed = 0;
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
	bool ownedButtonHit;
	bool heldActiveByButton;

	void clear()
	{
		autoButton.clear();
		autoButton.isAutoButton = true;
		parentLayerID = 0;
		altParentLayerID = 0;
		active = false;
		autoButtonHit = false;
		ownedButtonHit = false;
		heldActiveByButton = false;
	}
};

struct TranslatorState
{
	ButtonState gamepadButtons[eBtn_Num];
	std::vector<LayerState> layers;
	std::vector<u16> layerOrder;

	void clear()
	{
		for(size_t i = 0; i < ARRAYSIZE(gamepadButtons); ++i)
			gamepadButtons[i].clear();
		for(size_t i = 0; i < layers.size(); ++i)
			layers[i].autoButton.clear();
		layers.clear();
		layerOrder.clear();
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

static void loadLayerData()
{
	DBG_ASSERT(sState.layers.empty());

	sState.layers.reserve(InputMap::controlsLayerCount());
	sState.layers.resize(InputMap::controlsLayerCount());
	for(u16 i = 0; i < sState.layers.size(); ++i)
	{
		sState.layers[i].clear();
		ButtonState& anAutoButton = sState.layers[i].autoButton;
		anAutoButton.commandsLayer = i;
		anAutoButton.commands = InputMap::commandsForButton(i, eBtn_None);
		if( !anAutoButton.commands )
			continue;
		anAutoButton.holdTimeForAction =
			InputMap::commandHoldTime(i, eBtn_None);
		if( anAutoButton.commands[eBtnAct_With].type >= eCmdType_FirstValid )
		{
			WithActionCommand aWithActCmd;
			aWithActCmd.cmd = &anAutoButton.commands[eBtnAct_With];
			aWithActCmd.layerID = i;
			anAutoButton.withCommands.reserve(1);
			anAutoButton.withCommands.push_back(aWithActCmd);
		}
	}
}


static void	loadButtonCommandsForCurrentLayers()
{
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx) // skip eBtn_None
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		u8 analogToDigitalThreshold = kConfig.defaultThreshold[aBtnIdx];
		const bool isAnalog =
			Gamepad::axisForButton(EButton(aBtnIdx)) != Gamepad::eAxis_None;
		aBtnState.commandsLayer = 0;
		aBtnState.commands = null;
		aBtnState.withCommands.clear();
		// Assign commands to the button for each layer,
		// with later layers overriding earlier ones
		for(std::vector<u16>::const_iterator itr =
			sState.layerOrder.begin();
			itr != sState.layerOrder.end(); ++itr)
		{
			const Command* aCommandsArray = 
				InputMap::commandsForButton(*itr, EButton(aBtnIdx));
			if( !aCommandsArray )
				continue;
			const bool hasWithCommand =
				aCommandsArray[eBtnAct_With].type >= eCmdType_FirstValid;
			if( hasWithCommand )
			{// Add to the list of tack-on commands for this button
				WithActionCommand aWithActCmd;
				aWithActCmd.cmd = &aCommandsArray[eBtnAct_With];
				aWithActCmd.layerID = *itr;
				aBtnState.withCommands.push_back(aWithActCmd);
			}
			// Must have a non-empty, non-unassigned command on an action
			// other than the "With" action to control this button
			bool shouldControlButton = false;
			for(size_t aBtnAct = 0; aBtnAct < eBtnAct_With; ++aBtnAct)
			{
				if( aCommandsArray[aBtnAct].type != eCmdType_Empty &&
					aCommandsArray[aBtnAct].type != eCmdType_Unassigned )
				{
					shouldControlButton = true;
					break;
				}
			}
			if( isAnalog && (shouldControlButton || hasWithCommand) )
			{
				const u8* aThreshold =
					InputMap::commandThreshold(*itr, EButton(aBtnIdx));
				if( aThreshold )
					analogToDigitalThreshold = *aThreshold;
			}
			if( !shouldControlButton )
				continue;
			aBtnState.commands = aCommandsArray;
			aBtnState.commandsLayer = *itr;
		}
		aBtnState.holdTimeForAction = InputMap::commandHoldTime(
			aBtnState.commandsLayer, EButton(aBtnIdx));
		if( isAnalog )
		{
			Gamepad::setPressThreshold(
				EButton(aBtnIdx), analogToDigitalThreshold);
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
			aLayer.ownedButtonHit = false;
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


static std::vector<u16>::iterator layerOrderInsertPos(u16 theParentLayerID)
{
	// The order for this function only applies to normal or child layers.
	// Held layers are always placed directly on top of all (back()),
	// and combo layers have their own special sort function. So this is
	// really only concerned with making sure normal layers are kept
	// below held layers, unless they are a child layer, in which case
	// they are placed directly above their parent & siblings regardless
	// of what kind of layer the parent is.
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

	if( theParentLayerID == 0 )
	{
		// Continue to move forward until hit end() or a held layer
		while( result != sState.layerOrder.end() &&
			   !sState.layers[*result].heldActiveByButton )
		{
			++result;
		}
	}
	else
	{
		// Continue to move forward until hit end() or one unrelated to parent
		while( result != sState.layerOrder.end() &&
			   layerIsDescendant(sState.layers[*result], theParentLayerID) )
		{
			++result;
		}
	}

	return result;
}


static void addComboLayers(u16 theNewLayerID); // forward declare

static void addControlsLayer(u16 theLayerID)
{
	DBG_ASSERT(theLayerID < sState.layers.size());

	const u16 aParentLayerID = InputMap::parentLayer(theLayerID);
	DBG_ASSERT(aParentLayerID < sState.layers.size());
	if( aParentLayerID && !sState.layers[aParentLayerID].active )
		addControlsLayer(aParentLayerID);
	if( theLayerID > 0 )
		removeControlsLayer(theLayerID);
	DBG_ASSERT(!sState.layers[theLayerID].active);

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

	sState.layerOrder.insert(
		layerOrderInsertPos(aParentLayerID), theLayerID);
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

	if( aLayerWasAdded && !sState.layers[theNewLayerID].altParentLayerID )
	{
		// Need to re-sort combo layers into proper positions in layer order
		// General idea is put combo layers directly above their top-most
		// parent layer, but it gets more complex if that location can
		// apply to more than one combo layer, at which point they try to
		// match the relative order of their respective parent layers.
		// This sorting relies on the fact that all combo layers should have
		// only a non-combo layer for their parent, with only altParent
		// having the possibility of being another combo layer.
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
}


static void flagOwnedButtonHit(u16 theLayerIdx)
{
	if( theLayerIdx == 0 )
		return;

	sState.layers[theLayerIdx].ownedButtonHit = true;
	flagOwnedButtonHit(sState.layers[theLayerIdx].parentLayerID);
	flagOwnedButtonHit(sState.layers[theLayerIdx].altParentLayerID);
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

static u16 menuOwningLayer(u16 theMenuID)
{
	u16 result = 0;
	// Find lowest layer that is keeping given menu visible
	// via HUD= property, which is assumed to be the layer
	// that "owns" the menu
	const u16 theHUDElementID = InputMap::hudElementForMenu(theMenuID);
	for(u16 i = 0; i < sState.layerOrder.size(); ++i)
	{
		if( InputMap::hudElementsToShow(sState.layerOrder[i])
				.test(theHUDElementID) )
		{
			result = sState.layerOrder[i];
		}
		if( InputMap::hudElementsToHide(sState.layerOrder[i])
				.test(theHUDElementID) )
		{
			result = 0;
		}
	}

	return result;
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
	Command aForwardCmd;
	switch(theCmd.type)
	{
	case eCmdType_Empty:
	case eCmdType_DoNothing:
	case eCmdType_Unassigned:
		// Do nothing
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
	case eCmdType_SlashCommand:
	case eCmdType_SayString:
		// Queue to send last, since can block other input the longest
		sResults.strings.push_back(theCmd);
		break;
	case eCmdType_MoveMouseToHotspot:
	case eCmdType_MoveMouseToMenuItem:
		// Send right away to happen before a queued mouse click
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
		break;
	case eCmdType_ChangeProfile:
		Profile::queryUserForProfile();
		gReloadProfile = true;
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
			// Close menu first if this won't just switch to a sub-menu
			u16 aLayerToRemoveID = menuOwningLayer(theCmd.menuID);
			while(aLayerToRemoveID > 0 &&
				  aForwardCmd.type >= eCmdType_FirstValid &&
				  aForwardCmd.type < eCmdType_FirstMenuControl)
			{
				// If closing self, set theLayerIdx to parent layer first,
				// since this layer will be invalid for aForwardCmd
				if( aLayerToRemoveID == theLayerIdx )
					theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				removeControlsLayer(aLayerToRemoveID);
				aLayerToRemoveID = menuOwningLayer(theCmd.menuID);
			}
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		break;
	case eCmdType_MenuBack:
		if( const Command* aCmdPtr = Menus::closeLastSubMenu(theCmd.menuID) )
		{
			processCommand(theBtnState, *aCmdPtr, theLayerIdx);
			moveMouseToSelectedMenuItem(theCmd);
		}
		break;
	case eCmdType_MenuBackOrClose:
		if( const Command* aCmdPtr = Menus::closeLastSubMenu(theCmd.menuID) )
		{
			processCommand(theBtnState, *aCmdPtr, theLayerIdx);
			sResults.menuAutoCommandRun.set(theCmd.menuID);
			moveMouseToSelectedMenuItem(theCmd);
			break;
		}
		// Returning null means at are root, so fall through to close
	case eCmdType_MenuClose:
		{
			u16 aLayerToRemoveID = menuOwningLayer(theCmd.menuID);
			while(aLayerToRemoveID != 0)
			{
				removeControlsLayer(aLayerToRemoveID);
				aLayerToRemoveID = menuOwningLayer(theCmd.menuID);
			}
		}
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
			if( aForwardCmd.type >= eCmdType_FirstValid )
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
			if( aForwardCmd.type >= eCmdType_FirstValid )
			{
				// Close menu first if this won't just switch to a sub-menu
				bool aMenuWasClosed = false;
				u16 aLayerToRemoveID = menuOwningLayer(theCmd.menuID);
				while(aLayerToRemoveID > 0 &&
					  aForwardCmd.type >= eCmdType_FirstValid &&
					  (aForwardCmd.type < eCmdType_FirstMenuControl ||
					   aForwardCmd.type > eCmdType_LastMenuControl) )
				{
					// If closing self, set theLayerIdx to parent layer first,
					// since this layer will be invalid for aForwardCmd
					if( aLayerToRemoveID == theLayerIdx )
						theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
					removeControlsLayer(aLayerToRemoveID);
					aLayerToRemoveID = menuOwningLayer(theCmd.menuID);
					aMenuWasClosed = true;
				}
				processCommand(theBtnState, aForwardCmd, theLayerIdx);
				// Break out of this function entirely if closed a menu
				if( aMenuWasClosed )
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
		if( theCmd.mouseWheelMotionType == eMouseWheelMotion_Once )
		{
			InputDispatcher::scrollMouseWheelOnce(ECommandDir(theCmd.dir));
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
	// Process the rule-breaking "With" actions before anything else
	for(size_t i = 0; i < theBtnState.withCommands.size(); ++i)
	{
		DBG_ASSERT(theBtnState.withCommands[i].cmd);
		processCommand(&theBtnState,
			*theBtnState.withCommands[i].cmd,
			theBtnState.withCommands[i].layerID);
	}

	// When first pressed, back up copy of current commands to be referenced
	// by other button actions later. This makes sure if layers change
	// before the button is released, it will still behave as it would
	// previously, which could be important - especially for cases like
	// _Release matching up with a particular _Press.
	theBtnState.commandsWhenPressed = theBtnState.commands;
	theBtnState.layerWhenPressed = theBtnState.commandsLayer;

	// Log that at least one button in the assigned layer has been pressed
	// (unless it is just the Auto button for the layer, which doesn't count)
	if( !theBtnState.isAutoButton &&
		!sState.layers[theBtnState.commandsLayer].ownedButtonHit )
	{
		flagOwnedButtonHit(theBtnState.commandsLayer);
	}

	if( !theBtnState.commands )
		return;

	// _Press is processed before _Down since it is specifically called out
	// and the name implies it should be first action on button press.
	processCommand(&theBtnState,
		theBtnState.commands[eBtnAct_Press],
		theBtnState.commandsLayer);

	// Get _Down command for this button (default when no action specified)
	const Command& aCmd = theBtnState.commands[eBtnAct_Down];
	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Handled in processContinuousInput instead
		return;
	case eCmdType_MouseWheel:
		// Handled in processContinuousInput instead unless set to _Once
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Once )
			return;
		break;
	}
	processCommand(&theBtnState, aCmd, theBtnState.commandsLayer);
}


static void processContinuousInput(
	ButtonState& theBtnState,
	u8 theAnalogVal,
	bool isDigitalDown)
{
	// Use commandsWhenPressed if has a continuous action, otherwise current
	// These inputs should always be assigned to the _Down button action
	Command aCmd;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Down];
	if( theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		(aCmd.type < eCmdType_FirstValid || isTapOnlyCommand(aCmd)) )
	{
		aCmd = theBtnState.commands[eBtnAct_Down];
	}

	switch(aCmd.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveMouse:
		// Continue to analog checks below
		break;
	case eCmdType_MouseWheel:
		// Continue to analog checks below unless set to _Once
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Once )
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
	if( kConfig.autoRepeatRate == 0 || theBtnState.isAutoButton )
		return;

	// Auto-repeat only commandsWhenPressed assigned to _Down
	Command aCmd;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Down];

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
	if( theBtnState.commandsWhenPressed[eBtnAct_Tap].type ||
		theBtnState.commandsWhenPressed[eBtnAct_Hold].type )
		return;

	// Needs to be held for initial held time first before start repeating
	if( theBtnState.heldTime < kConfig.autoRepeatDelay )
		return;

	// Now can start using repeatDelay to re-send command at autoRepeatRate
	if( theBtnState.repeatDelay <= 0 )
	{
		processCommand(&theBtnState, aCmd, true);
		theBtnState.repeatDelay += kConfig.autoRepeatRate;
	}
	theBtnState.repeatDelay -= gAppFrameTime;
}


static void processButtonHold(ButtonState& theBtnState)
{
	theBtnState.holdActionDone = true;

	// Skip if this button was used as a modifier to execute a button
	// combination command (i.e. is L2 for the combo L2+X).
	if( theBtnState.usedInButtonCombo )
		return;

	// Only ever use commandsWhenPressed for short hold action
	if( !theBtnState.commandsWhenPressed )
		return;

	processCommand(&theBtnState,
		theBtnState.commandsWhenPressed[eBtnAct_Hold],
		theBtnState.layerWhenPressed);
}


static void processButtonTap(ButtonState& theBtnState)
{
	// Should not even call this if button was used in a button combo
	DBG_ASSERT(!theBtnState.usedInButtonCombo);

	// Only ever use commandsWhenPressed for tap action
	if( !theBtnState.commandsWhenPressed )
		return;

	// If has a hold command, then a "tap" is just releasing before said
	// command had a chance to execute. Otherwise it is based on holding
	// less than tapHoldTime before releasing.
	const bool hasHold =
		theBtnState.commandsWhenPressed[eBtnAct_Hold]
			.type >= eCmdType_FirstValid;

	if( (hasHold && !theBtnState.holdActionDone) ||
		(!hasHold && theBtnState.heldTime < kConfig.tapHoldTime) )
	{
		processCommand(&theBtnState,
			theBtnState.commandsWhenPressed[eBtnAct_Tap],
			theBtnState.layerWhenPressed);
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
	// standard _HoldControlsLayer method that does it automatically.
	Command aCmd;
	u16 aCmdLayer = theBtnState.layerWhenPressed;
	if( theBtnState.commandsWhenPressed )
		aCmd = theBtnState.commandsWhenPressed[eBtnAct_Release];
	if( aCmd.type < eCmdType_FirstValid &&
		theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		theBtnState.commands[eBtnAct_Press].type < eCmdType_FirstValid )
	{
		aCmd = theBtnState.commands[eBtnAct_Release];
		aCmdLayer = theBtnState.commandsLayer;
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
			sState.layers[theBtnState.layerHeld].ownedButtonHit )
		{
			theBtnState.usedInButtonCombo = true;
		}

		if( wasDown && theBtnState.heldTime < (0xFFFF - gAppFrameTime) )
			theBtnState.heldTime += gAppFrameTime;
		if( theBtnState.heldTime >= theBtnState.holdTimeForAction &&
			!theBtnState.holdActionDone )
		{
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
		if( sState.layers[theBtnState.layerHeld].ownedButtonHit )
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
	if( !theBtnState.commands )
		return false;
	const Command& aDownCmd = theBtnState.commands[eBtnAct_Down];
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

		// Held layers are always placed on "top" (back() of the vector)
		// of all other layers at the time they are added.
		sState.layerOrder.push_back(aLayerID);
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
			loadButtonCommandsForCurrentLayers();
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
		if( aBtnState.commands )
		{
			for(size_t aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				u16 aHUDIdx = 0xFFFF;
				switch(aBtnState.commands[aBtnAct].type)
				{
				case eCmdType_MenuReset:
				case eCmdType_MenuConfirm:
				case eCmdType_MenuConfirmAndClose:
				case eCmdType_MenuBack:
				case eCmdType_MenuBackOrClose:
				case eCmdType_MenuEdit:
				case eCmdType_MenuSelect:
				case eCmdType_MenuSelectAndClose:
				case eCmdType_MenuEditDir:
					aHUDIdx = InputMap::hudElementForMenu(
						aBtnState.commands[aBtnAct].menuID);
					break;
				default:
					continue;
				}
				DBG_ASSERT(aHUDIdx < gDisabledHUD.size());
				gDisabledHUD.reset(aHUDIdx);
			}
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
		if( !wasDisabled || (!isDisabled && wasDisabled) )
		{
			processCommand(null,
				Menus::autoCommand(aMenuID),
				menuOwningLayer(aMenuID));
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
	loadButtonCommandsForCurrentLayers();
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

	// Update button commands, mouse mode, HUD, etc for new layer order
	if( sResults.layerChangeMade )
	{
		loadButtonCommandsForCurrentLayers();
		updateHUDStateForCurrentLayers();
		updateHotspotArraysForCurrentLayers();
		updateMouseModeForCurrentLayers();
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

	// Send input that was queued up by any of the above
	InputDispatcher::moveCharacter(
		sResults.charMove,
		sResults.charTurn,
		sResults.charStrafe);
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
	sResults.clear();
}

#undef transDebugPrint

} // InputTranslator
