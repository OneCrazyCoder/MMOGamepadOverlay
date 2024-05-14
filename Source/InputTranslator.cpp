//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "Menus.h"
#include "Profile.h"

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
	u16 shortHoldTime;
	u16 longHoldTime;
	u16 tapHoldTime;
	u16 autoRepeatDelay;
	u16 autoRepeatRate;

	void load()
	{
		shortHoldTime = Profile::getInt("System/ButtonShortHoldTime", 400);
		longHoldTime = Profile::getInt("System/ButtonLongHoldTime", 800);
		tapHoldTime = Profile::getInt("System/ButtonTapTime", 500);
		autoRepeatDelay = Profile::getInt("System/AutoRepeatDelay", 400);
		autoRepeatRate = Profile::getInt("System/AutoRepeatRate", 100);
	}
};


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

struct ButtonState
{
	const Command* commands;
	const Command* commandsWhenPressed;
	u16 commandsLayer;
	u16 layerWhenPressed;
	u16 layerHeld;
	u16 heldTime;
	s16 repeatDelay;
	u16 vKeyHeld;
	bool isAutoButton;
	bool heldDown;
	bool shortHoldDone;
	bool longHoldDone;
	bool usedInButtonCombo;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		ZeroMemory(this, sizeof(ButtonState));
	}

	void resetWhenReleased()
	{
		const Command* oldCommands = this->commands;
		const u16 oldParentLayer = this->commandsLayer;
		const u16 oldChildLayer = this->layerHeld;
		const bool oldIsAutoButton = this->isAutoButton;
		clear();
		this->commands = oldCommands;
		this->commandsLayer = oldParentLayer;
		this->layerHeld = oldChildLayer;
		this->isAutoButton = oldIsAutoButton;
	}

	ButtonState() : vKeyHeld() { clear(); }
};

struct LayerState
{
	ButtonState autoButton;
	u16 parentLayerID;
	u16 altParentLayerID; // 0 unless is a combo layer
	bool active;
	bool newlyActive;
	bool ownedButtonHit;
	bool heldActiveByButton;

	void clear()
	{
		autoButton.clear();
		autoButton.isAutoButton = true;
		parentLayerID = 0;
		altParentLayerID = 0;
		active = false;
		newlyActive = false;
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
		sState.layers[i].autoButton.commandsLayer = i;
		sState.layers[i].autoButton.commands =
			InputMap::commandsForButton(i, eBtn_None);
	}
}


static void	loadButtonCommandsForCurrentLayers()
{
	for(size_t aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx) // skip eBtn_None
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		// Start with the front-most layer, and stop once get a non-null
		for(std::vector<u16>::const_reverse_iterator itr =
			sState.layerOrder.rbegin();
			itr != sState.layerOrder.rend(); ++itr)
		{
			aBtnState.commands =
				InputMap::commandsForButton(*itr, EButton(aBtnIdx));
			if( aBtnState.commands )
			{
				aBtnState.commandsLayer = *itr;
				break;
			}
		}
	}
}


static bool removeControlsLayer(u16 theLayerID)
{
	// Layer ID 0 can't be removed, but trying to removes everything else
	if( theLayerID == 0 )
		transDebugPrint(
			"Removing ALL Layers (besides [Scheme] & held by button)!\n");

	// Remove given layer ID and any "child" layers it spawned via 'Add'.
	// Child layers that are being held active by a held button are NOT
	// automatically removed along with their parent layer though!
	// Use reverse iteration so children are likely to be removed first.
	bool layerWasRemoved = false;
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin(), next_itr = itr;
		itr != sState.layerOrder.rend(); itr = next_itr)
	{
		++next_itr;
		// Never remove root layer directly
		if( *itr == 0 )
			continue;
		LayerState& aLayer = sState.layers[*itr];
		if( *itr == theLayerID && aLayer.active )
		{
			transDebugPrint("Removing Controls Layer: %s\n",
				InputMap::layerLabel(*itr).c_str());

			// Reset some layer properties
			aLayer.active = false;
			aLayer.ownedButtonHit = false;
			aLayer.newlyActive = false;
			aLayer.heldActiveByButton = false;
			sResults.layerChangeMade = true;
			// .parentLayerID is intentionally left at last setting
			// for refernece by commands on buttons that may still
			// be active due to commandsWhenPressed system

			// Remove from the layer order and recover iterator to continue
			next_itr = std::vector<u16>::reverse_iterator(
				sState.layerOrder.erase((itr+1).base()));
			layerWasRemoved = true;
		}
		else if( (aLayer.parentLayerID == theLayerID ||
				  aLayer.altParentLayerID == theLayerID) &&
				 !aLayer.heldActiveByButton )
		{
			// Need to use recursion so also remove grandchildren, etc
			// Recursion will mess up our iterator beyond reasonable recovery,
			// so just start at the beginning (end) again if removed anything
			if( removeControlsLayer(*itr) )
			{
				next_itr = sState.layerOrder.rbegin();
				layerWasRemoved = true;
			}
		}
	}

	return layerWasRemoved;
}


static bool layerIsParentOfLayer(u16 theParentLayerID, u16 theCheckLayerID)
{
	if( theCheckLayerID == theParentLayerID )
		return true;

	LayerState& aCheckLayer = sState.layers[theCheckLayerID];
	if( aCheckLayer.parentLayerID == theParentLayerID )
		return true;

	if( aCheckLayer.altParentLayerID == theParentLayerID )
		return true;

	if( aCheckLayer.parentLayerID != 0 &&
		aCheckLayer.parentLayerID != theCheckLayerID &&
		layerIsParentOfLayer(theParentLayerID, aCheckLayer.parentLayerID) )
		return true;

	if( aCheckLayer.altParentLayerID != 0 &&
		aCheckLayer.altParentLayerID != theCheckLayerID &&
		layerIsParentOfLayer(theParentLayerID, aCheckLayer.altParentLayerID) )
		return true;

	return false;
}


static std::vector<u16>::iterator layerOrderInsertPos(u16 theParentLayerID)
{
	// New layers typically should be at the "top" (back() of the vector).
	// However, layers being held active by buttons should stay on top of
	// layers added by Add/Replace/Toggle (except for their own "children").
	std::vector<u16>::iterator result = sState.layerOrder.begin();

	// First, work backwards until find theParentLayerID's position in the
	// order to make sure insert after it.
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin();
		itr != sState.layerOrder.rend(); ++itr)
	{
		result = itr.base();
		if( *itr == theParentLayerID )
			break;
	}
	// Now move forward until run into top (back()) or hit a held layer,
	// in which case will insert below it.
	while( result != sState.layerOrder.end() &&
		   !sState.layers[*result].heldActiveByButton )
	{
		++result;
	}

	return result;
}


static void addComboLayers(u16 theNewLayerID); // forward declare

static void addControlsLayer(
	u16 theLayerID, u16 theSpawningLayerID, u16 theComboLayerParentID = 0)
{
	DBG_ASSERT(theLayerID < sState.layers.size());
	DBG_ASSERT(theSpawningLayerID < sState.layers.size());

	if( theSpawningLayerID != 0 && !sState.layers[theSpawningLayerID].active )
	{
		transDebugPrint(
			"Add layer '%s' request ignored - parent '%s' is missing!\n",
			InputMap::layerLabel(theLayerID).c_str(),
			InputMap::layerLabel(theSpawningLayerID).c_str());
		return;
	}
	
	// Check to see if layer is already active
	if( sState.layers[theLayerID].active )
	{
		// If request came from a child layer of the layer being added,
		// meaning could end up with a recursive child/parent relationship,
		// simply bump this layer to new position in layer order instead,
		// without adding/removing anything or changing parent relationships.
		if( layerIsParentOfLayer(theLayerID, theSpawningLayerID) )
		{
			// Remove from layer order
			sState.layerOrder.erase(std::find(
				sState.layerOrder.begin(),
				sState.layerOrder.end(),
				theLayerID));
			// Re-insert into layer order
			sState.layerOrder.insert(
				layerOrderInsertPos(theSpawningLayerID), theLayerID);
			transDebugPrint("Moving Controls Layer '%s' to top\n",
				InputMap::layerLabel(theLayerID).c_str());
			sResults.layerChangeMade = true;
			return;
		}

		// Otherwise, remove layer first then re-add it
		removeControlsLayer(theLayerID);
		DBG_ASSERT(!sState.layers[theLayerID].active);
	}

	if( theLayerID > 0 )
	{
		if( theSpawningLayerID > 0 )
		{
			transDebugPrint(
				"Adding Controls Layer '%s' as child of Layer '%s'\n",
				InputMap::layerLabel(theLayerID).c_str(),
				InputMap::layerLabel(theSpawningLayerID).c_str());
		}
		else
		{
			transDebugPrint(
				"Adding Controls Layer '%s'\n",
				InputMap::layerLabel(theLayerID).c_str());
		}
	}

	sState.layerOrder.insert(
		layerOrderInsertPos(theSpawningLayerID), theLayerID);
	LayerState& aLayer = sState.layers[theLayerID];
	aLayer.parentLayerID = theSpawningLayerID;
	aLayer.altParentLayerID = theComboLayerParentID;
	aLayer.active = true;
	aLayer.newlyActive = true;
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
			addControlsLayer(aComboLayerID, theNewLayerID, aLayerID);
			aLayerWasAdded = true;
			i = 0; // since sState.layerOrder might have changed
		}
		aComboLayerID = InputMap::comboLayerID(aLayerID, theNewLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active )
		{
			addControlsLayer(aComboLayerID, aLayerID, theNewLayerID);
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

static u16 menuCloseLayer(u16 theMenuID, u16 theAskingLayerID)
{
	u16 result = theAskingLayerID;
	// Find lowest layer that is keeping given menu visible
	// via HUD= property, which is assumed to be the layer
	// "holding" the menu open and thus the menu can be
	// "closed" by removing that layer
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
			result = theAskingLayerID;
		}
	}

	return result;
}


static void processCommand(
	ButtonState& theBtnState,
	const Command& theCmd,
	u16 theLayerIdx,
	bool repeated = false)
{
	Command aForwardCmd;
	switch(theCmd.type)
	{
	case eCmdType_Empty:
		// Do nothing
		break;
	case eCmdType_PressAndHoldKey:
		// Release any previously-held key first!
		releaseKeyHeldByButton(theBtnState);
		// Send this right away since can often be instantly processed
		// instead of needing to wait for input queue
		InputDispatcher::sendKeyCommand(theCmd);
		// Make note that this button is holding this key
		theBtnState.vKeyHeld = theCmd.vKey;
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
				-theCmd.count, theCmd.wrap);
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
	case eCmdType_QuitApp:
		gShutdown = true;
		break;
	case eCmdType_AddControlsLayer:
		// relativeLayer is how many parent layers up from current to attach to
		for(int i = 0; i < theCmd.relativeLayer && theLayerIdx != 0; ++i)
			theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
		addControlsLayer(theCmd.layerID, theLayerIdx);
		break;
	case eCmdType_RemoveControlsLayer:
		if( theCmd.layerID == 0 )
		{// 0 means to remove relative rather than direct layer ID
			if( theCmd.relativeLayer == kAllLayers )
			{
				removeControlsLayer(0); // removes all BUT 0 actually
			}
			else
			{
				// relativeLayer means how many parent layers up from calling
				for(int i = 0; i < theCmd.relativeLayer &&
					sState.layers[theLayerIdx].parentLayerID != 0; ++i)
				{
					theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				}
				removeControlsLayer(theLayerIdx);
			}
		}
		else
		{// Otherwise remove a specific layer ID specified
			removeControlsLayer(theCmd.layerID);
		}
		break;
	case eCmdType_HoldControlsLayer:
		// Special-case, handled elsewhere
		break;
	case eCmdType_ReplaceControlsLayer:
		DBG_ASSERT(theCmd.replacementLayer > 0);
		if( theCmd.layerID == 0 )
		{// 0 means to remove relative rather than direct layer ID
			if( theCmd.relativeLayer == kAllLayers )
			{
				removeControlsLayer(0); // removes all BUT 0 actually
				theLayerIdx = 0;
			}
			else
			{
				// relativeLayer means how many parent layers up from calling
				for(int i = 0; i < theCmd.relativeLayer &&
					sState.layers[theLayerIdx].parentLayerID != 0; ++i)
				{
					theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				}
				u16 aLayerToRemoveID = theLayerIdx;
				// Want to add new layer to removed layer's parent
				theLayerIdx = sState.layers[aLayerToRemoveID].parentLayerID;
				removeControlsLayer(aLayerToRemoveID);
			}
		}
		else
		{// Otherwise remove a specific layer ID specified
			theLayerIdx = sState.layers[theCmd.layerID].parentLayerID;
			removeControlsLayer(theCmd.layerID);
		}
		// Now add the replacement layer to the removed layer's parent,
		// if it isn't already active anyway
		if( !sState.layers[theCmd.replacementLayer].active )
			addControlsLayer(theCmd.replacementLayer, theLayerIdx);
		break;
	case eCmdType_ToggleControlsLayer:
		aForwardCmd = theCmd;
		DBG_ASSERT(theCmd.layerID != 0);
		DBG_ASSERT(theCmd.layerID < sState.layers.size());
		aForwardCmd.type = sState.layers[theCmd.layerID].active
			? eCmdType_RemoveControlsLayer
			: eCmdType_AddControlsLayer;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_OpenSubMenu:
		aForwardCmd = Menus::openSubMenu(theCmd.menuID, theCmd.subMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_ReplaceMenu:
		aForwardCmd = Menus::replaceMenu(theCmd.menuID, theCmd.subMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuReset:
		aForwardCmd = Menus::reset(theCmd.menuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuConfirm:
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.menuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuConfirmAndClose:
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.menuID);
		if( aForwardCmd.type != eCmdType_Empty )
		{
			// Close menu first if this won't just switch to a sub-menu
			const u16 aLayerToRemoveID =
				menuCloseLayer(theCmd.menuID, theLayerIdx);
			if( aForwardCmd.type != eCmdType_Empty &&
				aForwardCmd.type < eCmdType_FirstMenuControl &&
				aLayerToRemoveID > 0 )
			{
				// If closing self, set theLayerIdx to parent layer first,
				// since this layer will be invalid for aForwardCmd
				if( aLayerToRemoveID == theLayerIdx )
					theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
				removeControlsLayer(aLayerToRemoveID);
			}
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		break;
	case eCmdType_MenuBack:
		if( const Command* aCmdPtr = Menus::closeLastSubMenu(theCmd.menuID) )
			processCommand(theBtnState, *aCmdPtr, theLayerIdx);
		break;
	case eCmdType_MenuBackOrClose:
		if( const Command* aCmdPtr = Menus::closeLastSubMenu(theCmd.menuID) )
		{
			processCommand(theBtnState, *aCmdPtr, theLayerIdx);
			break;
		}
		// Returning null means at are root, so fall through to close
	case eCmdType_MenuClose:
		{
			u16 aLayerToRemoveID = menuCloseLayer(theCmd.menuID, theLayerIdx);
			if( aLayerToRemoveID != 0 )
				removeControlsLayer(aLayerToRemoveID);
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
			if( aForwardCmd.type != eCmdType_Empty )
				processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		break;
	case eCmdType_MenuSelectAndClose:
		for(int i = 0; i < theCmd.count; ++i)
		{
			aForwardCmd = Menus::selectMenuItem(
				theCmd.menuID, ECommandDir(theCmd.dir),
				theCmd.wrap, repeated || i < theCmd.count-1);
			if( aForwardCmd.type != eCmdType_Empty )
			{
				// Close menu first if this won't just switch to a sub-menu
				const u16 aLayerToRemoveID =
					menuCloseLayer(theCmd.menuID, theLayerIdx);
				if( aForwardCmd.type != eCmdType_Empty &&
					aLayerToRemoveID > 0 &&
					(aForwardCmd.type < eCmdType_FirstMenuControl ||
					 aForwardCmd.type > eCmdType_LastMenuControl) )
				{
					// If closing self, set theLayerIdx to parent layer first,
					// since this layer will be invalid for aForwardCmd
					if( aLayerToRemoveID == theLayerIdx )
						theLayerIdx = sState.layers[theLayerIdx].parentLayerID;
					removeControlsLayer(aLayerToRemoveID);
					// Process command and break out of loop
					processCommand(theBtnState, aForwardCmd, theLayerIdx);
					break;
				}
				processCommand(theBtnState, aForwardCmd, theLayerIdx);
			}
		}
		break;
	case eCmdType_MenuEditDir:
		Menus::editMenuItemDir(theCmd.menuID, ECommandDir(theCmd.dir));
		break;
	case eCmdType_HotspotSelect:
		// TODO
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


static void processButtonPress(ButtonState& theBtnState)
{
	// When first pressed, back up copy of current commands to be referenced
	// by other button actions later. This makes sure if layers change
	// before the button is released, it will still behave as it would
	// previously, which could be important - especially for cases like
	// _Release matching up with a particular _Press.
	theBtnState.commandsWhenPressed = theBtnState.commands;
	theBtnState.layerWhenPressed = theBtnState.commandsLayer;

	// Log that at least one button in the assigned layer has been pressed
	// (unless it is just the Auto button for the layer, which doesn't count)
	if( !theBtnState.isAutoButton )
		sState.layers[theBtnState.commandsLayer].ownedButtonHit = true;

	if( !theBtnState.commands )
		return;

	// _Press is processed before _Down since it is specifically called out
	// and the name implies it should be first action on button press.
	processCommand(theBtnState,
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
	processCommand(theBtnState, aCmd, theBtnState.commandsLayer);
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
	if( aCmd.type == eCmdType_Empty && theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed )
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
		if( aCmd.mouseWheelMotionType == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY -= theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Down:
		if( aCmd.mouseWheelMotionType == eMouseWheelMotion_Stepped )
			sResults.mouseWheelStepped = true;
		sResults.mouseWheelY += theAnalogVal;
		sResults.mouseWheelDigital = isDigitalDown;
		break;
	case (eCmdType_MouseWheel << 16) | eCmdDir_Left:
	case (eCmdType_MouseWheel << 16) | eCmdDir_Right:
		// Ignore (multi-directional assigned)
		break;
	default:
		DBG_ASSERT(false && "Command and direction combo invalid");
		break;
	}
}


static void processAutoRepeat(ButtonState& theBtnState)
{
	if( kConfig.autoRepeatRate == 0 )
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
	case eCmdType_HotspotSelect:
	case eCmdType_KeyBindArrayPrev:
	case eCmdType_KeyBindArrayNext:
		// Continue to further checks below
		break;
	default:
		// Incompatible with this feature
		return;
	}
	
	// Don't auto-repeat when button has other conflicting actions assigned
	if( theBtnState.commandsWhenPressed[eBtnAct_Tap].type ||
		theBtnState.commandsWhenPressed[eBtnAct_ShortHold].type ||
		theBtnState.commandsWhenPressed[eBtnAct_LongHold].type )
		return;

	// Needs to be held for initial held time first before start repeating
	if( theBtnState.heldTime < kConfig.autoRepeatDelay )
		return;

	// Now can start using repeatDelay to re-send command at autoRepeatRate
	if( theBtnState.repeatDelay <= 0 )
	{
		processCommand(theBtnState, aCmd, true);
		theBtnState.repeatDelay += kConfig.autoRepeatRate;
	}
	theBtnState.repeatDelay -= gAppFrameTime;
}


static void processButtonShortHold(ButtonState& theBtnState)
{
	theBtnState.shortHoldDone = true;

	// Skip if this button was used as a modifier to execute a button
	// combination command (i.e. is L2 for the combo L2+X).
	if( theBtnState.usedInButtonCombo )
		return;

	// Only ever use commandsWhenPressed for short hold action
	if( !theBtnState.commandsWhenPressed )
		return;

	processCommand(theBtnState,
		theBtnState.commandsWhenPressed[eBtnAct_ShortHold],
		theBtnState.layerWhenPressed);
}


static void processButtonLongHold(ButtonState& theBtnState)
{
	theBtnState.longHoldDone = true;

	// Skip if this button was used as a modifier to execute a button
	// combination command (i.e. is L2 for the combo L2+X).
	if( theBtnState.usedInButtonCombo )
		return;

	// Only ever use commandsWhenPressed for long hold action
	if( !theBtnState.commandsWhenPressed )
		return;

	processCommand(theBtnState,
		theBtnState.commandsWhenPressed[eBtnAct_LongHold],
		theBtnState.layerWhenPressed);
}


static void processButtonTap(ButtonState& theBtnState)
{
	// Should not even call this if button was used in a button combo
	DBG_ASSERT(!theBtnState.usedInButtonCombo);

	// Only ever use commandsWhenPressed for tap action
	if( !theBtnState.commandsWhenPressed )
		return;

	// If has a short or long hold command, then a "tap" is just releasing
	// before said command had a chance to execute. Otherwise it is based
	// on holding less than tapHoldTime before releasing.
	const bool hasShortHold =
		theBtnState.commandsWhenPressed[eBtnAct_ShortHold]
			.type != eCmdType_Empty;
	const bool hasLongHold =
		theBtnState.commandsWhenPressed[eBtnAct_LongHold]
			.type != eCmdType_Empty;

	if( (hasShortHold && !theBtnState.shortHoldDone) ||
		(!hasShortHold && hasLongHold && !theBtnState.longHoldDone) ||
		(!hasShortHold && !hasLongHold &&
			theBtnState.heldTime < kConfig.tapHoldTime) )
	{
		processCommand(theBtnState,
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
	if( aCmd.type == eCmdType_Empty &&
		theBtnState.commands &&
		theBtnState.commands != theBtnState.commandsWhenPressed &&
		theBtnState.commands[eBtnAct_Press].type == eCmdType_Empty )
	{
		aCmd = theBtnState.commands[eBtnAct_Release];
		aCmdLayer = theBtnState.commandsLayer;
	}
	processCommand(theBtnState, aCmd, aCmdLayer);

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
		if( theBtnState.heldTime >= kConfig.shortHoldTime &&
			!theBtnState.shortHoldDone )
		{
			processButtonShortHold(theBtnState);
		}
		if( theBtnState.heldTime >= kConfig.longHoldTime &&
			!theBtnState.longHoldDone )
		{
			processButtonLongHold(theBtnState);
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
	const u16 aParentLayer = theBtnState.commandsLayer;
	if( wasHit )
	{
		DBG_ASSERT(aLayerID < sState.layers.size());
		DBG_ASSERT(aLayerID > 0);
		DBG_ASSERT(aParentLayer < sState.layers.size());
		releaseLayerHeldByButton(theBtnState);

		transDebugPrint(
			"Holding Controls Layer '%s' as child of Layer '%s'\n",
			InputMap::layerLabel(aLayerID).c_str(),
			InputMap::layerLabel(aParentLayer).c_str());

		// Held layers are always placed on "top" (back() of the vector)
		// of all other layers when they are added.
		sState.layerOrder.push_back(aLayerID);
		LayerState& aLayer = sState.layers[aLayerID];
		aLayer.parentLayerID = aParentLayer;
		aLayer.altParentLayerID = 0;
		aLayer.active = true;
		aLayer.newlyActive = true;
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
						aLayer.active,
						aLayer.newlyActive);
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
	loadLayerData();
	addControlsLayer(0, 0);
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
	Menus::cleanup();
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
			aLayer.active,
			aLayer.newlyActive);
		aLayer.newlyActive = false;
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

	// Process any virtual "auto" buttons for layers just added by above
	for(size_t i = 0; i < sState.layers.size(); ++i)
	{
		LayerState& aLayer = sState.layers[i];
		if( aLayer.newlyActive )
		{
			processButtonState(
				aLayer.autoButton,
				aLayer.active,
				aLayer.newlyActive);
			aLayer.newlyActive = false;
		}
	}

	// Update button commands, mouse mode, HUD, etc for new layer order
	if( sResults.layerChangeMade )
	{
		loadButtonCommandsForCurrentLayers();
		updateHUDStateForCurrentLayers();
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

} // InputTranslator
