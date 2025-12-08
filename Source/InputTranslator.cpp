//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#include "InputTranslator.h"

#include "Gamepad.h"
#include "HotspotMap.h"
#include "InputDispatcher.h"
#include "InputMap.h"
#include "LayoutEditor.h"
#include "Menus.h"
#include "Profile.h"
#include "TargetConfigSync.h"
#include "WindowManager.h" // readUIScale()

namespace InputTranslator
{

// Uncomment this to print layer order changes and such to debug window
//#define INPUT_TRANSLATOR_DEBUG_PRINT

//------------------------------------------------------------------------------
// Const Data
//------------------------------------------------------------------------------

enum {
kMaxLayerChangesPerUpdate = 32,
};


//------------------------------------------------------------------------------
// Config
//------------------------------------------------------------------------------

struct ZERO_INIT(Config)
{
	int tapHoldTime;
	int defaultHoldTimeForAction;
	int autoRepeatDelay;
	int autoRepeatRate;
	int hotspotAutoRepeatDelay;
	int hotspotAutoRepeatRate;

	void load()
	{
		tapHoldTime =
			max(0, Profile::getInt("System", "ButtonTapTime", 500));
		defaultHoldTimeForAction =
			max(0, Profile::getInt("System", "ButtonHoldTime", 400));
		autoRepeatDelay =
			max(0, Profile::getInt("System", "AutoRepeatDelay", 400));
		autoRepeatRate =
			max(0, Profile::getInt("System", "AutoRepeatRate", 100));
		hotspotAutoRepeatDelay =
			max(0, Profile::getInt("System", "SelectHotspotRepeatDelay", 150));
		hotspotAutoRepeatRate =
			max(0, Profile::getInt("System", "SelectHotspotRepeatRate", 75));
		u8 aThreshold = u8(clamp(
			Profile::getInt("Gamepad", "LStickThreshold", 40),
				0, 100) * 255 / 100);
		Gamepad::setPressThreshold(eBtn_LSLeft, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSRight, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSUp, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSDown, aThreshold);
		Gamepad::setPressThreshold(eBtn_LSAny, aThreshold);
		aThreshold = u8(clamp(
			Profile::getInt("Gamepad", "RStickThreshold", 40),
				0, 100) * 255 / 100);
		Gamepad::setPressThreshold(eBtn_RSLeft, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSRight, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSUp, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSDown, aThreshold);
		Gamepad::setPressThreshold(eBtn_RSAny, aThreshold);
		aThreshold = u8(clamp(
			Profile::getInt("Gamepad", "TriggerThreshold", 12),
				0, 100) * 255 / 100);
		Gamepad::setPressThreshold(eBtn_L2, aThreshold);
		Gamepad::setPressThreshold(eBtn_R2, aThreshold);
	}
};


//------------------------------------------------------------------------------
// Local Structures
//------------------------------------------------------------------------------

struct CommandArray
{
	Command data[eBtnAct_Num];
	Command& operator[](int index) { return data[index]; }
	const Command& operator[](int index) const { return data[index]; }
	bool operator==(const CommandArray& other) const
	{
		for (int i = 0; i < eBtnAct_Num; ++i)
		{
			if (!(data[i] == other.data[i]))
				return false;
		}
		return true;
	}
};

struct ButtonCommandSet
{
	CommandArray cmd;
	u16 layer[eBtnAct_Num];
	int holdTimeForAction;

	ButtonCommandSet() { clear(); }
	void clear()
	{
		for(int i = 0; i < eBtnAct_Num; ++i)
		{
			cmd[i] = Command();
			layer[i] = 0;
		}
		holdTimeForAction = 500;
	}
};

struct ZERO_INIT(ButtonState)
{
	ButtonCommandSet commands;
	ButtonCommandSet commandsWhenPressed;
	int heldTime;
	u16 layerHeld;
	u16 vKeyHeld;
	u16 buttonID;
	u16 heldDown : 1;
	u16 holdActionDone : 1;
	u16 usedInButtonCombo : 1;
	u16 __padding : 13;

	void clear()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		commands.clear();
		commandsWhenPressed.clear();
		layerHeld = 0;
		heldTime = 0;
		buttonID = eBtn_None;
		heldDown = false;
		holdActionDone = false;
		usedInButtonCombo = false;
	}

	void resetWhenReleased()
	{
		DBG_ASSERT(vKeyHeld == 0);
		DBG_ASSERT(layerHeld == 0);
		commandsWhenPressed.clear();
		heldTime = 0;
		heldDown = false;
		holdActionDone = false;
		usedInButtonCombo = false;
	}

	ButtonState() : vKeyHeld(), layerHeld() { clear(); }
};

struct ZERO_INIT(LayerState)
{
	ButtonState autoButton;
	int lastAddedTime;
	s8 refCount;
	bool autoButtonHit;
	bool addedNormally;
	bool buttonCommandUsed;

	bool active() { return refCount > 0; }
	bool autoButtonDown() { return active(); }
	bool heldActiveByButton()
	{ return refCount > 1 || (refCount == 1 && !addedNormally); }

	void clear()
	{
		autoButton.clear();
		autoButton.buttonID = eBtn_None;
		refCount = 0;
		autoButtonHit = false;
		addedNormally = false;
		buttonCommandUsed = false;
	}
};

struct ZERO_INIT(ActiveSignal)
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
	ButtonState* exclusiveAutoRepeatButton;
	int mouseControllingRootMenu;
	int layersAddedCount;
	int exclusiveAutoRepeatDelay;
	int syncAutoRepeatDelay;
	bool syncAutoRepeatActive;
	bool layersNeedSorting;

	void clear()
	{
		for(int i = 0; i < ARRAYSIZE(gamepadButtons); ++i)
		{
			gamepadButtons[i].clear();
			gamepadButtons[i].buttonID = dropTo<u16>(i);
		}
		for(int i = 0, end = intSize(layers.size()); i < end; ++i)
			layers[i].autoButton.clear();
		layers.clear();
		layerOrder.clear();
		signalCommands.clear();
		mouseControllingRootMenu = -1;
		layersAddedCount = 0;
		exclusiveAutoRepeatButton = null;
		exclusiveAutoRepeatDelay = 0;
		syncAutoRepeatDelay = 0;
		syncAutoRepeatActive = false;
		layersNeedSorting = false;
	}
};

struct InputResults
{
	std::vector<Command> queuedKeys;
	std::vector<int> changedLayers;
	BitVector<32> menuStackAutoCommandRun;
	BitArray<eBtn_Num> buttonPressProcessed;
	ECommandDir selectHotspotDir;
	int charMove;
	int charTurn;
	int charStrafe;
	int charLookX;
	int mouseMoveX;
	int mouseMoveY;
	int mouseWheelY;
	bool mouseMoveDigital;
	bool mouseWheelDigital;
	bool mouseWheelStepped;
	bool charMoveStartAutoRun;
	bool charMoveLockMovement;
	bool exclusiveAutoRepeatNeeded;
	bool syncAutoRepeatNeeded;

	void clear()
	{
		queuedKeys.clear();
		changedLayers.clear();
		menuStackAutoCommandRun.clearAndResize(InputMap::menuOverlayCount());
		buttonPressProcessed.reset();
		selectHotspotDir = eCmd8Dir_None;
		charMove = 0;
		charTurn = 0;
		charStrafe = 0;
		charLookX = 0;
		mouseMoveX = 0;
		mouseMoveY = 0;
		mouseWheelY = 0;
		mouseMoveDigital = false;
		mouseWheelDigital = false;
		mouseWheelStepped = false;
		charMoveStartAutoRun = false;
		charMoveLockMovement = false;
		exclusiveAutoRepeatNeeded = false;
		syncAutoRepeatNeeded = false;
	}
};


//------------------------------------------------------------------------------
// Static Variables
//------------------------------------------------------------------------------

static Config kConfig;
static TranslatorState sState;
static InputResults sResults;


//------------------------------------------------------------------------------
// Debugging
//------------------------------------------------------------------------------

#ifdef INPUT_TRANSLATOR_DEBUG_PRINT
#define transDebugPrint(...) debugPrint("InputTranslator: " __VA_ARGS__)
#else
#define transDebugPrint(...) ((void)0)
#endif


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static void loadLayerData()
{
	DBG_ASSERT(sState.layers.empty());

	sState.layers.reserve(InputMap::controlsLayerCount());
	sState.layers.resize(InputMap::controlsLayerCount());
	for(int aLayerID = 0, end = intSize(sState.layers.size());
		aLayerID < end; ++aLayerID)
	{
		LayerState& aLayer = sState.layers[aLayerID];
		aLayer.clear();
		const InputMap::ButtonActionsMap& aBtnCmdsMap =
			InputMap::buttonCommandsForLayer(aLayerID);
		if( !aBtnCmdsMap.empty() && aBtnCmdsMap[0].first == eBtn_None )
		{// eBtn_None == 0 (so first in map if it exists) == "auto" button
			const InputMap::ButtonActions& aBtnActions = aBtnCmdsMap[0].second;
			for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				aLayer.autoButton.commands.cmd[aBtnAct] =
					aBtnActions.cmd[aBtnAct];
				aLayer.autoButton.commands.layer[aBtnAct] =
					dropTo<u16>(aLayerID);
			}
			aLayer.autoButton.commands.holdTimeForAction =
				aBtnActions.holdTimeForAction >= 0
					? aBtnActions.holdTimeForAction
					: kConfig.defaultHoldTimeForAction;
		}
	}
}


static EButton multiDirFirstChildButton(EButton theButtonID)
{
	switch(theButtonID)
	{
	case eBtn_LSAny:	return eBtn_LSLeft;
	case eBtn_RSAny:	return eBtn_RSLeft;
	case eBtn_DPadAny:	return eBtn_DLeft;
	case eBtn_FPadAny:	return eBtn_FLeft;
	}

	return eBtn_None;
}


static EButton multiDirParentButton(EButton theButtonID)
{
	switch(theButtonID)
	{
	case eBtn_LSLeft: case eBtn_LSRight: case eBtn_LSUp: case eBtn_LSDown:
		return eBtn_LSAny;
	case eBtn_RSLeft: case eBtn_RSRight: case eBtn_RSUp: case eBtn_RSDown:
		return eBtn_RSAny;
	case eBtn_DLeft: case eBtn_DRight: case eBtn_DUp: case eBtn_DDown:
		return eBtn_DPadAny;
	case eBtn_FLeft: case eBtn_FRight: case eBtn_FUp: case eBtn_FDown:
		return eBtn_FPadAny;
	}

	return eBtn_None;
}


static void	loadCommandsForCurrentLayers()
{
	DBG_ASSERT(!sState.layersNeedSorting);
	sState.signalCommands.clear();
	for(int aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx) // skip eBtn_None
	{
		sState.gamepadButtons[aBtnIdx].commands.clear();
		sState.gamepadButtons[aBtnIdx].commands.holdTimeForAction =
			kConfig.defaultHoldTimeForAction;
	}

	// Load commands such that higher layers override lower ones
	for(std::vector<u16>::const_iterator itr =
		sState.layerOrder.begin();
		itr != sState.layerOrder.end(); ++itr)
	{
		const int aLayerID = *itr;
		const InputMap::ButtonActionsMap& aBtnCmdsMap =
			InputMap::buttonCommandsForLayer(aLayerID);
		const InputMap::ButtonRemap& aBtnRemap =
			InputMap::buttonRemap(aLayerID);
		for(int i = 0, end = intSize(aBtnCmdsMap.size()); i < end; ++i)
		{
			const EButton aBtnID = aBtnRemap[aBtnCmdsMap[i].first];
			if( aBtnID == eBtn_None )
				continue;
			const InputMap::ButtonActions& aBtnActions = aBtnCmdsMap[i].second;
			// See if contains any "defer" commands, which change any
			// _Unassigned commands (but not _DoNothing) to not override
			bool hasDeferCmd = false;
			for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				if( aBtnActions.cmd[aBtnAct].type == eCmdType_Defer )
				{
					hasDeferCmd = true;
					break;
				}
			}
			ButtonState& aBtnState = sState.gamepadButtons[aBtnID];
			for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				const Command& aCmd = aBtnActions.cmd[aBtnAct];
				DBG_ASSERT(aCmd.type >= eCmdType_Defer);
				if( hasDeferCmd && aCmd.type <= eCmdType_Unassigned )
					continue;

				aBtnState.commands.cmd[aBtnAct] = aCmd;
				aBtnState.commands.layer[aBtnAct] = dropTo<u16>(aLayerID);
				// For multi-directionals, the _LSAny/etc command should block
				// single direction buttons from lower layers and vice versa.
				if( EButton aChildBtnID = multiDirFirstChildButton(aBtnID) )
				{
					for(int aDir = 0; aDir < eCmdDir_Num; ++aDir,
						aChildBtnID = EButton(aChildBtnID+1))
					{
						ButtonCommandSet& aDirBtnSet =
							sState.gamepadButtons[aChildBtnID].commands;
						if( aDirBtnSet.layer[aBtnAct] != aLayerID )
						{
							aDirBtnSet.cmd[aBtnAct].type = eCmdType_Unassigned;
							aDirBtnSet.layer[aBtnAct] = u16(aLayerID);
						}
					}
				}
				else if( EButton aParentBtnID = multiDirParentButton(aBtnID) )
				{
					ButtonCommandSet& aMultDirCmdSet =
						sState.gamepadButtons[aParentBtnID].commands;
					if( aMultDirCmdSet.layer[aBtnAct] != aLayerID )
					{
						aMultDirCmdSet.cmd[aBtnAct].type = eCmdType_Unassigned;
						aMultDirCmdSet.layer[aBtnAct] = u16(aLayerID);
					}
				}
			}
			// Don't override hold time unless also overriding related actions
			if( aBtnActions.cmd[eBtnAct_Hold].type > eCmdType_Unassigned &&
				(aBtnActions.cmd[eBtnAct_Hold].type != eCmdType_DoNothing ||
				 aBtnActions.cmd[eBtnAct_Tap].type >= eCmdType_FirstValid) )
			{
				aBtnState.commands.holdTimeForAction =
					aBtnActions.holdTimeForAction >= 0
						? aBtnActions.holdTimeForAction
						: kConfig.defaultHoldTimeForAction;
			}
		}
		const InputMap::SignalActionsMap& aSignalsList =
			InputMap::signalCommandsForLayer(aLayerID);
		for(int i = 0, end = intSize(aSignalsList.size()); i < end; ++i)
		{
			ActiveSignal aSignalCmd;
			aSignalCmd.signalID = aSignalsList[i].first;
			aSignalCmd.layerID = dropTo<u16>(aLayerID);
			aSignalCmd.cmd = aSignalsList[i].second;
			sState.signalCommands.push_back(aSignalCmd);
		}
	}

	// For multi-directionals, the _LSAny/etc command may need to be shifted
	// over to the single directional buttons (_LSLeft/Right/Up/Down).
	const EButton kMultiDirButtons[] =
		{ eBtn_LSAny, eBtn_RSAny, eBtn_DPadAny, eBtn_FPadAny };
	for(int aMultiDirBtnIdx = 0; aMultiDirBtnIdx < ARRAYSIZE(kMultiDirButtons);
		++aMultiDirBtnIdx)
	{
		const EButton aBtnID = kMultiDirButtons[aMultiDirBtnIdx];
		ButtonCommandSet& aMultDirCmdSet =
			sState.gamepadButtons[aBtnID].commands;
		const EButton aFirstChildBtnID = multiDirFirstChildButton(aBtnID);
		DBG_ASSERT(aFirstChildBtnID != eBtn_None);
		ButtonCommandSet* aDirBtnSet[eCmdDir_Num];
		for(int aDir = 0; aDir < eCmdDir_Num; ++aDir)
		{
			aDirBtnSet[aDir] =
				&sState.gamepadButtons[aFirstChildBtnID + aDir].commands;
		}
		for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
		{
			if( aMultDirCmdSet.cmd[aBtnAct].type < eCmdType_FirstValid )
				continue;
			// Directional commands MUST be moved to individual dir inputs
			const bool isDirectionalCommand =
				aMultDirCmdSet.cmd[aBtnAct].type >= eCmdType_FirstDirectional;
			bool needsMerging = isDirectionalCommand;
			// If some individual inputs have valid commands set for this
			// action, should move the _LSAny/etc command to those that don't.
			// The _LSAny/etc button should only be used directly (activated
			// by pressing ANY direction and acts as "holding" until release
			// ALL directions related to it) when no separate commands are
			// included for individual directions. Usually setting a command
			// for one is just a shortcut to assigning multiple dirs at once.
			for(int aDir = 0; aDir < eCmdDir_Num && !needsMerging; ++aDir)
			{
				if( aDirBtnSet[aDir]->cmd[aBtnAct].type >= eCmdType_DoNothing )
					needsMerging = true;
			}
			if( !needsMerging )
				continue;
			for(int aDir = 0; aDir < eCmdDir_Num; ++aDir)
			{
				if( aDirBtnSet[aDir]->cmd[aBtnAct].type >= eCmdType_DoNothing )
					continue;
				aDirBtnSet[aDir]->cmd[aBtnAct] = aMultDirCmdSet.cmd[aBtnAct];
				aDirBtnSet[aDir]->layer[aBtnAct] =
					aMultDirCmdSet.layer[aBtnAct];
				if( isDirectionalCommand )
					aDirBtnSet[aDir]->cmd[aBtnAct].dir = dropTo<u16>(aDir);
				if( aBtnAct == eBtnAct_Hold )
				{
					aDirBtnSet[aDir]->holdTimeForAction =
						aMultDirCmdSet.holdTimeForAction;
				}
			}
			aMultDirCmdSet.cmd[aBtnAct] = Command();
		}
	}
}


static bool removeControlsLayer(int theLayerID, bool force = false)
{
	DBG_ASSERT(theLayerID != 0);

	if( force && sState.layers[theLayerID].heldActiveByButton() )
	{// Force remove layer held active by a button or buttons
		for(int i = 0; i < ARRAYSIZE(sState.gamepadButtons); ++i)
		{
			ButtonState& aBtnState = sState.gamepadButtons[i];
			if( aBtnState.layerHeld == theLayerID )
			{
				if( sState.layers[theLayerID].buttonCommandUsed )
					aBtnState.usedInButtonCombo = true;
				aBtnState.layerHeld = 0;
			}
		}
		for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
		{
			ButtonState& aBtnState =
				sState.layers[sState.layerOrder[i]].autoButton;
			if( aBtnState.layerHeld == theLayerID )
			{
				if( sState.layers[theLayerID].buttonCommandUsed )
					aBtnState.usedInButtonCombo = true;
				aBtnState.layerHeld = 0;
			}
		}
		sState.layers[theLayerID].refCount = 0;
		sState.layers[theLayerID].addedNormally = false;
	}
	if( sState.layers[theLayerID].addedNormally )
	{// Un-do a previous add layer command
		--sState.layers[theLayerID].refCount;
		sState.layers[theLayerID].addedNormally = false;
	}
	// Only actually remove if not being kept active after above
	if( sState.layers[theLayerID].active() )
		return false;

	// Remove given layer ID and any of its "child" layers.
	// Use reverse iteration so children are removed first.
	for(std::vector<u16>::reverse_iterator itr =
		sState.layerOrder.rbegin(), next_itr = itr;
		itr != sState.layerOrder.rend(); itr = next_itr)
	{
		++next_itr;
		LayerState& aLayer = sState.layers[*itr];
		if( *itr == theLayerID )
		{
			transDebugPrint("Removing Controls Layer: %s\n",
				InputMap::layerLabel(*itr));

			// Log layer change made
			aLayer.buttonCommandUsed = false;
			sResults.changedLayers.push_back(theLayerID);

			// Remove from the layer order and recover iterator to continue
			next_itr = std::vector<u16>::reverse_iterator(
				sState.layerOrder.erase((itr+1).base()));
		}
		else if( InputMap::parentLayer(*itr) == theLayerID ||
				 InputMap::comboParentLayer(*itr) == theLayerID )
		{
			// Need to use recursion so also remove grandchildren, etc
			if( removeControlsLayer(*itr) )
			{
				// Recursion will mess up iterator beyond reasonable recovery,
				// so just start at the beginning (end) again after removal
				next_itr = sState.layerOrder.rbegin();
			}
		}
	}

	return true;
}


static bool layerIsDescendant(int theLayerID, int theParentLayerID)
{
	const int aParentOfThisLayer = InputMap::parentLayer(theLayerID);
	if( aParentOfThisLayer == theParentLayerID )
		return true;

	if( aParentOfThisLayer == 0 )
		return false;

	return
		layerIsDescendant(
			aParentOfThisLayer,
			theParentLayerID) ||
		layerIsDescendant(
			InputMap::comboParentLayer(theLayerID),
			theParentLayerID);
}


static void addControlsLayer(int theLayerID, bool = false); // forward declare
static void addRelatedLayers(int theNewLayerID); // forward declare

static void moveControlsLayerToTop(
	int theLayerID, BitVector<32>& theMovedLayers)
{
	DBG_ASSERT(size_t(theLayerID) < sState.layers.size());
	DBG_ASSERT(sState.layers[theLayerID].active());
	DBG_ASSERT(InputMap::comboParentLayer(theLayerID) == 0);
	if( theMovedLayers.test(theLayerID) ) // prevent infinite recursion
		return;
	theMovedLayers.set(theLayerID);
	//transDebugPrint(
	//	"Re-sorting Controls Layer '%s' as if it had been newly added\n",
	//	InputMap::layerLabel(theLayerID));

	std::vector<u16>::iterator beginItr = std::find(
		sState.layerOrder.begin(),
		sState.layerOrder.end(),
		theLayerID);
	DBG_ASSERT(beginItr != sState.layerOrder.end());
	// Make sure to move any descendants along with it
	std::vector<u16>::iterator itrEnd = beginItr;
	for(++itrEnd; itrEnd != sState.layerOrder.end() &&
		layerIsDescendant(*itrEnd, theLayerID);
		++itrEnd) {}
	for(std::vector<u16>::iterator itr = beginItr; itr != itrEnd; ++itr)
	{
		sState.layers[*itr].lastAddedTime = ++sState.layersAddedCount;
		// Reset "Hold Auto ### =" held times for any moving layers that have
		// not yet executed their hold action (these are usually used for
		// some kind of delayed action after the layer has been left alone
		// for a while, like hiding a menu overlay by removing themselves).
		if( !sState.layers[*itr].autoButton.holdActionDone )
			sState.layers[*itr].autoButton.heldTime = 0;
	}

	// Re-process associated auto layers
	const BitVector<32>& autoRemoveLayers =
		InputMap::autoRemoveLayers(theLayerID);
	for(int i = autoRemoveLayers.firstSetBit();
		i < autoRemoveLayers.size();
		i = autoRemoveLayers.nextSetBit(i+1))
	{
		removeControlsLayer(i);
	}

	const BitVector<>& autoAddLayers =
		InputMap::autoAddLayers(theLayerID);
	for(int i = autoAddLayers.firstSetBit();
		i < autoAddLayers.size();
		i = autoAddLayers.nextSetBit(i+1))
	{
		if( !sState.layers[i].active() || !sState.layers[i].addedNormally )
			addControlsLayer(i);
		else
			moveControlsLayerToTop(i, theMovedLayers);
		theMovedLayers.set(i);
	}

	// Actual moving of layers will happen in sort function
	sState.layersNeedSorting = true;
}


static void addControlsLayer(int theLayerID, bool asHeldLayer)
{
	DBG_ASSERT(size_t(theLayerID) < sState.layers.size());

	// Parent layer must be added first
	const int aParentLayerID = InputMap::parentLayer(theLayerID);
	DBG_ASSERT(size_t(aParentLayerID) < sState.layers.size());
	if( aParentLayerID && !sState.layers[aParentLayerID].active() )
	{
		// Can't add combo layers via adding their children,
		// meaning can't add the children until combo layer added first
		if( InputMap::comboParentLayer(aParentLayerID) )
			return;
		addControlsLayer(aParentLayerID);
	}

	// Use reference counting and bumping to top if adding already-active layer
	if( sState.layers[theLayerID].active() )
	{
		if( asHeldLayer )
		{
			++sState.layers[theLayerID].refCount;
			sState.layers[theLayerID].buttonCommandUsed = false;
		}
		else if( !sState.layers[theLayerID].addedNormally )
		{
			++sState.layers[theLayerID].refCount;
			sState.layers[theLayerID].addedNormally = true;
		}
		BitVector<32> movedLayers(sState.layers.size());
		moveControlsLayerToTop(theLayerID, movedLayers);
		return;
	}

	#ifdef INPUT_TRANSLATOR_DEBUG_PRINT
	if( theLayerID > 0 )
	{
		if( int aComboParentLayerID = InputMap::comboParentLayer(theLayerID) )
		{
			transDebugPrint(
				"Adding Controls Layer %d '%s' (since '%s' and '%s' exist)\n",
				theLayerID,
				InputMap::layerLabel(theLayerID),
				InputMap::layerLabel(aParentLayerID),
				InputMap::layerLabel(aComboParentLayerID));
		}
		else if( asHeldLayer )
		{
			transDebugPrint(
				"Holding Controls Layer %d '%s'\n",
				theLayerID, InputMap::layerLabel(theLayerID));
		}
		else if( aParentLayerID > 0 )
		{
			transDebugPrint(
				"Adding Controls Layer %d '%s' as child of Layer '%s'\n",
				theLayerID,
				InputMap::layerLabel(theLayerID),
				InputMap::layerLabel(aParentLayerID));
		}
		else
		{
			transDebugPrint(
				"Adding Controls Layer %d '%s'\n",
				theLayerID, InputMap::layerLabel(theLayerID));
		}
	}
	#endif

	sState.layerOrder.push_back(dropTo<u16>(theLayerID));
	sState.layersNeedSorting = true;
	LayerState& aLayer = sState.layers[theLayerID];
	aLayer.lastAddedTime = ++sState.layersAddedCount;
	aLayer.refCount = 1;
	aLayer.addedNormally = !asHeldLayer;
	aLayer.autoButtonHit = true;
	sResults.changedLayers.push_back(theLayerID);
	addRelatedLayers(theLayerID);
}


static void addRelatedLayers(int theNewLayerID)
{
	// Add/remove any auto layers associated with the new layer ID
	const BitVector<32>& autoRemoveLayers =
		InputMap::autoRemoveLayers(theNewLayerID);
	for(int i = autoRemoveLayers.firstSetBit();
		i < autoRemoveLayers.size();
		i = autoRemoveLayers.nextSetBit(i+1))
	{
		removeControlsLayer(i);
	}
	const BitVector<32>& autoAddLayers =
		InputMap::autoAddLayers(theNewLayerID);
	for(int i = autoAddLayers.firstSetBit();
		i < autoAddLayers.size();
		i = autoAddLayers.nextSetBit(i+1))
	{
		addControlsLayer(i);
	}

	DBG_ASSERT(size_t(theNewLayerID) < sState.layers.size());

	// Find and add any combo layers with theNewLayerID as a base and
	// the other base layer also being active
	for(int i = 1; i < intSize(sState.layerOrder.size()); ++i)
	{
		const int aLayerID = sState.layerOrder[i];
		if( aLayerID == theNewLayerID )
			continue;
		int aComboLayerID = InputMap::comboLayerID(theNewLayerID, aLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active() )
			addControlsLayer(aComboLayerID);
		aComboLayerID = InputMap::comboLayerID(aLayerID, theNewLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active() )
			addControlsLayer(aComboLayerID);
	}
}


static void sortLayers()
{
	if( !sState.layersNeedSorting || sState.layerOrder.size() < 2 )
		return;
	sState.layersNeedSorting = false;

	struct ZERO_INIT(LayerSortData)
	{
		enum
		{// This enum's order determines sort order!
			eType_Combo,
			eType_Normal,
			eType_Held,
		} type;
		int parentID;
		int preSortPos;
		int lastAddedTime;
		int priority;
	};

	struct SortSiblings
	{
		const std::vector<LayerSortData>& data;
		SortSiblings(const std::vector<LayerSortData>& d) : data(d) {}
		bool operator()(int lhs, int rhs) const
		{
			const LayerSortData& a = data[lhs];
			const LayerSortData& b = data[rhs];

			// 1. By priority value
			if( a.priority != b.priority )
				return a.priority < b.priority;

			// 2. By layer type
			if( a.type != b.type )
				return a.type < b.type;

			// 3. By base layers' positions (combo layers only)
			if( a.type == LayerSortData::eType_Combo )
			{
				if( a.preSortPos != b.preSortPos )
					return a.preSortPos < b.preSortPos;
			}

			// 4. By time added
			return a.lastAddedTime < b.lastAddedTime;
		}
	};

	struct SortChildren
	{
		const std::vector<LayerSortData>& data;
		SortChildren(const std::vector<LayerSortData>& d) : data(d) {}
		bool operator()(int lhs, int rhs) const
		{
			if( lhs == rhs )
				return false;

			// Walk up each path to root to check if one is the ancestor
			// of the other - and get their depths otherwise
			int lhsDepth = 0, rhsDepth = 0;
			for(int p = lhs; p != 0; p = data[p].parentID)
			{
				if( p == rhs )
					return false; // r is ancestor of l, so l > r
				++lhsDepth;
			}
			for(int p = rhs; p != 0; p = data[p].parentID)
			{
				if( p == lhs )
					return true; // l is ancestor of r, so l < r
				++rhsDepth;
			}

			// Align depths if different
			int lhsWalk = lhs, rhsWalk = rhs;
			while(lhsDepth > rhsDepth)
			{
				lhsWalk = data[lhsWalk].parentID;
				--lhsDepth;
			}
			while(rhsDepth > lhsDepth)
			{
				rhsWalk = data[rhsWalk].parentID;
				--rhsDepth;
			}

			// Walk up in sync to find shared ancestor
			while(lhsWalk != rhsWalk)
			{
				lhsWalk = data[lhsWalk].parentID;
				rhsWalk = data[rhsWalk].parentID;
			}
			const int aSharedAncestor = lhsWalk; // now same as rhsWalk

			// Find the immediate children of that ancestor by walking
			// one more time and stopping just before hit the ancestor
			lhsWalk = lhs; rhsWalk = rhs;
			while(data[lhsWalk].parentID != aSharedAncestor)
				lhsWalk = data[lhsWalk].parentID;
			while(data[rhsWalk].parentID != aSharedAncestor)
				rhsWalk = data[rhsWalk].parentID;

			// Compare pre-sort position at found divergence point
			return data[lhsWalk].preSortPos < data[rhsWalk].preSortPos;
		}
	};

	static std::vector<LayerSortData> sLayerSortData;
	static std::vector<u16> sLastLayerOrder;

	// Layer 0 is always the first (bottom) layer so can skip it in loops/sorts
	DBG_ASSERT(sState.layerOrder[0] == 0);

	// Set up static portions of layer sort data
	const int kLayerCount = InputMap::controlsLayerCount();
	sLayerSortData.resize(kLayerCount);
	for(int i = 1; i < kLayerCount; ++i)
	{
		if( !sState.layers[i].active() )
			continue;
		sLayerSortData[i].parentID = InputMap::parentLayer(i);
		sLayerSortData[i].priority = InputMap::layerPriority(i);
		sLayerSortData[i].lastAddedTime = sState.layers[i].lastAddedTime;
		sLayerSortData[i].type =
			sState.layers[i].heldActiveByButton() ?
				LayerSortData::eType_Held :
			InputMap::comboParentLayer(i) ?
				LayerSortData::eType_Combo :
			LayerSortData::eType_Normal;

		// Special case: Held layers at default (0) priority are automatically
		// bumped to max priority to place them on top of their normal siblings
		// even when said siblings have a > 0 priority set
		if( sLayerSortData[i].type == LayerSortData::eType_Held &&
			sLayerSortData[i].priority == 0 )
		{ sLayerSortData[i].priority = 255; }
	}

	int aLoopCount = 0;
	do
	{
		// Make backup of current order so know if any change occurred
		sLastLayerOrder = sState.layerOrder;

		// Update combo layer data that is dependent on current order
		for(int i = 1; i < kLayerCount; ++i)
		{
			if( !sState.layers[i].active() ||
				sLayerSortData[i].type != LayerSortData::eType_Combo )
			{ continue; }

			// Combo layers use the higher-positioned (later in vector) of
			// their base layers as their parent for sorting, and use the
			// other layer as a special tie-breaker in the sibling sort.
			int aParentLayer = InputMap::parentLayer(i);
			int anAltBaseLayer = InputMap::comboParentLayer(i);
			std::vector<u16>::const_iterator aParentLayerPos = std::find(
				sState.layerOrder.begin()+1,
				sState.layerOrder.end(),
				dropTo<u16>(aParentLayer));
			DBG_ASSERT(aParentLayerPos != sState.layerOrder.end());

			std::vector<u16>::const_iterator anAltBaseLayerPos = std::find(
				sState.layerOrder.begin()+1,
				sState.layerOrder.end(),
				dropTo<u16>(anAltBaseLayer));
			DBG_ASSERT(anAltBaseLayerPos != sState.layerOrder.end());

			if( anAltBaseLayerPos > aParentLayerPos )
			{
				swap(aParentLayer, anAltBaseLayer);
				swap(aParentLayerPos, anAltBaseLayerPos);
			}
			sLayerSortData[i].parentID = aParentLayer;
			sLayerSortData[i].preSortPos =
				dropTo<int>(anAltBaseLayerPos - sState.layerOrder.begin());
		}

		// Sort siblings with each other (skip sorting root layer)
		std::sort(
			sState.layerOrder.begin()+1,
			sState.layerOrder.end(),
			SortSiblings(sLayerSortData));

		// Note order in order to maintain sibling relative order when
		// sort them to just above their immediate parents
		for(int i = 1, end = intSize(sState.layerOrder.size()); i < end; ++i)
			sLayerSortData[sState.layerOrder[i]].preSortPos = i;

		// Sort children just after parents
		std::sort(
			sState.layerOrder.begin()+1,
			sState.layerOrder.end(),
			SortChildren(sLayerSortData));

		// Since some sort factors depend on previous order, may not actually
		// be fully sorted yet, so need to repeat sorting until the sort
		// stabilizes (no change is made from either sort).
		if( ++aLoopCount > 64 )
		{
			DBG_ASSERT(false && "Infinite loop in sortLayers()!");
			break;
		}
	} while(sLastLayerOrder != sState.layerOrder);
}


static void flagLayerButtonCommandUsed(int theLayerID)
{
	if( theLayerID == 0 )
		return;

	sState.layers[theLayerID].buttonCommandUsed = true;
	flagLayerButtonCommandUsed(InputMap::parentLayer(theLayerID));
	flagLayerButtonCommandUsed(InputMap::comboParentLayer(theLayerID));
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


static bool tryPressAndHoldKey(
	ButtonState* theBtnState, const Command& theCmd)
{
	if( !theBtnState || !theCmd.asHoldAction )
		return false;

	Command aHoldCmd = theCmd;
	while(aHoldCmd.type == eCmdType_TriggerKeyBind)
		aHoldCmd = InputMap::keyBindCommand(aHoldCmd.keyBindID);
	if( aHoldCmd.type != eCmdType_TapKey )
		return false;

	aHoldCmd.type = eCmdType_PressAndHoldKey;
	if( theCmd.type == eCmdType_TriggerKeyBind )
	{
		aHoldCmd.hasKeybindSignal = true;
		aHoldCmd.keyBindID = theCmd.keyBindID;
	}
	// Release any previously-held key first!
	releaseKeyHeldByButton(*theBtnState);
	// Send this right away since can often be instantly processed
	// instead of needing to wait for input queue
	InputDispatcher::sendKeyCommand(aHoldCmd);
	// Make note that this button is now holding this key
	theBtnState->vKeyHeld = aHoldCmd.vKey;
	return true;
}


static void releaseLayerHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.layerHeld )
	{
		// Flag if used in a button combo, like L2 in L2+X
		if( sState.layers[theBtnState.layerHeld].buttonCommandUsed )
			theBtnState.usedInButtonCombo = true;
		if( --sState.layers[theBtnState.layerHeld].refCount <= 0 )
			removeControlsLayer(theBtnState.layerHeld);
		theBtnState.layerHeld = 0;
	}
}


static void updateMouseForMenu(int theRootMenuID, bool andClick = false)
{
	if( andClick || theRootMenuID == sState.mouseControllingRootMenu )
	{
		Command aMoveCmd;
		aMoveCmd.type = eCmdType_MoveMouseToMenuItem;
		aMoveCmd.rootMenuID = dropTo<u16>(theRootMenuID);
		aMoveCmd.menuItemID = dropTo<u16>(Menus::selectedItem(theRootMenuID));
		aMoveCmd.andClick = andClick;
		InputDispatcher::moveMouseTo(aMoveCmd);
		HotspotMap::update();
	}
}


static void processCommand(
	ButtonState* theBtnState,
	const Command& theCmd,
	int theLayerIdx,
	bool repeated = false)
{
	// Flag when used a command assigned to a layer button (besides Auto)
	if( theBtnState && theLayerIdx && theBtnState->buttonID != eBtn_None )
		flagLayerButtonCommandUsed(theLayerIdx);

	Command aForwardCmd;
	switch(theCmd.type)
	{
	case eCmdType_Invalid:
	case eCmdType_Empty:
	case eCmdType_Defer:
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		// Do nothing
		break;
	case eCmdType_PressAndHoldKey:
	case eCmdType_ReleaseKey:
		// Handled by tryPressAndHoldKey() and releaseKeyHeldByButton() instead
		DBG_ASSERT(false && "Invalid command sent to processCommand()");
		break;
	case eCmdType_TapKey:
	case eCmdType_TriggerKeyBind:
		// Send immediately if it can be a press-and-hold, otherwise queue
		if( !tryPressAndHoldKey(theBtnState, theCmd) )
			sResults.queuedKeys.push_back(theCmd);
		break;
	case eCmdType_VKeySequence:
	case eCmdType_ChatBoxString:
		// Queue to send after any press-and-hold commands
		sResults.queuedKeys.push_back(theCmd);
		break;
	case eCmdType_MoveMouseToHotspot:
	case eCmdType_MoveMouseToMenuItem:
	case eCmdType_MoveMouseToOffset:
		// Send right away, to happen before a queued mouse click
		InputDispatcher::moveMouseTo(theCmd);
		// Update hotspot map in case another command wants to move
		// directly from this spot to a different relative hotspot
		HotspotMap::update();
		break;
	case eCmdType_KeyBindCycleReset:
		DBG_ASSERT(theCmd.keyBindCycleID < gKeyBindCycleLastIndex.size());
		gKeyBindCycleLastIndex[theCmd.keyBindCycleID] = -1;
		gKeyBindCycleLastIndexChanged.set(theCmd.keyBindCycleID);
		break;
	case eCmdType_KeyBindCycleSetDefault:
		DBG_ASSERT(theCmd.keyBindCycleID < gKeyBindCycleDefaultIndex.size());
		gKeyBindCycleDefaultIndex[theCmd.keyBindCycleID] =
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID];
		gKeyBindCycleDefaultIndexChanged.set(theCmd.keyBindCycleID);
		break;
	case eCmdType_KeyBindCyclePrev:
		DBG_ASSERT(theCmd.keyBindCycleID < gKeyBindCycleLastIndex.size());
		if( gKeyBindCycleLastIndex[theCmd.keyBindCycleID] < 0 )
		{
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID] =
				gKeyBindCycleDefaultIndex[theCmd.keyBindCycleID];
		}
		else
		{
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID] -= theCmd.count;
			while(gKeyBindCycleLastIndex[theCmd.keyBindCycleID] < 0)
			{
				if( theCmd.wrap )
				{
					gKeyBindCycleLastIndex[theCmd.keyBindCycleID] +=
						InputMap::keyBindCycleSize(theCmd.keyBindCycleID);
				}
				else
				{
					gKeyBindCycleLastIndex[theCmd.keyBindCycleID] = 0;
				}
			}
		}
		aForwardCmd.type = eCmdType_TriggerKeyBind;
		aForwardCmd.keyBindID = InputMap::keyBindCycleIndexToKeyBindID(
			theCmd.keyBindCycleID,
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID]);
		aForwardCmd.fromKeyBindCycle = true;
		aForwardCmd.keyBindCycleID = theCmd.keyBindCycleID;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindCycleLastIndexChanged.set(theCmd.keyBindCycleID);
		// Allow holding this button to auto-repeat after a delay
		sState.exclusiveAutoRepeatButton = theBtnState;
		break;
	case eCmdType_KeyBindCycleNext:
		DBG_ASSERT(theCmd.keyBindCycleID < gKeyBindCycleLastIndex.size());
		if( gKeyBindCycleLastIndex[theCmd.keyBindCycleID] < 0 )
		{
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID] =
				gKeyBindCycleDefaultIndex[theCmd.keyBindCycleID];
		}
		else
		{
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID] += theCmd.count;
			while(gKeyBindCycleLastIndex[theCmd.keyBindCycleID] >=
					InputMap::keyBindCycleSize(theCmd.keyBindCycleID) )
			{
				if( theCmd.wrap )
				{
					gKeyBindCycleLastIndex[theCmd.keyBindCycleID] -=
						InputMap::keyBindCycleSize(theCmd.keyBindCycleID);
				}
				else
				{
					gKeyBindCycleLastIndex[theCmd.keyBindCycleID] =
						InputMap::keyBindCycleSize(theCmd.keyBindCycleID)-1;
				}
			}
		}
		aForwardCmd.type = eCmdType_TriggerKeyBind;
		aForwardCmd.keyBindID = InputMap::keyBindCycleIndexToKeyBindID(
			theCmd.keyBindCycleID,
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID]);
		aForwardCmd.fromKeyBindCycle = true;
		aForwardCmd.keyBindCycleID = theCmd.keyBindCycleID;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindCycleLastIndexChanged.set(theCmd.keyBindCycleID);
		// Allow holding this button to auto-repeat after a delay
		sState.exclusiveAutoRepeatButton = theBtnState;
		break;
	case eCmdType_KeyBindCycleLast:
		DBG_ASSERT(theCmd.keyBindCycleID < gKeyBindCycleLastIndex.size());
		if( gKeyBindCycleLastIndex[theCmd.keyBindCycleID] < 0 )
		{
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID] =
				gKeyBindCycleDefaultIndex[theCmd.keyBindCycleID];
			gKeyBindCycleLastIndexChanged.set(theCmd.keyBindCycleID);
		}
		aForwardCmd.type = eCmdType_TriggerKeyBind;
		aForwardCmd.keyBindID = InputMap::keyBindCycleIndexToKeyBindID(
			theCmd.keyBindCycleID,
			gKeyBindCycleLastIndex[theCmd.keyBindCycleID]);
		aForwardCmd.asHoldAction = theCmd.asHoldAction;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_SetVariable:
		Profile::setVariable(
			theCmd.variableID,
			InputMap::cmdString(theCmd),
			theCmd.temporary);
		break;
	case eCmdType_StartAutoRun:
		sResults.charMoveStartAutoRun = true;
		if( theCmd.multiDirAutoRun )
			sResults.charMoveLockMovement = true;
		break;
	case eCmdType_ChangeProfile:
		gLoadNewProfile = Profile::queryUserForProfile();
		break;
	case eCmdType_EditLayout:
		LayoutEditor::init();
		break;
	case eCmdType_ChangeTargetConfigSyncFile:
		TargetConfigSync::promptUserForSyncFileToUse();
		break;
	case eCmdType_QuitApp:
		gShutdown = true;
		break;
	case eCmdType_AddControlsLayer:
		addControlsLayer(theCmd.layerID);
		break;
	case eCmdType_RemoveControlsLayer:
		removeControlsLayer(
			theCmd.layerID == 0
				? dropTo<u16>(theLayerIdx)
				: theCmd.layerID,
			theCmd.forced);
		break;
	case eCmdType_HoldControlsLayer:
		DBG_ASSERT(theCmd.layerID > 0);
		if( theBtnState && theBtnState->layerHeld != theCmd.layerID )
		{
			releaseLayerHeldByButton(*theBtnState);
			addControlsLayer(theCmd.layerID, true);
			theBtnState->layerHeld = theCmd.layerID;
		}
		break;
	case eCmdType_ReplaceControlsLayer:
		DBG_ASSERT(theCmd.replacementLayer > 0);
		aForwardCmd = theCmd;
		aForwardCmd.type = eCmdType_RemoveControlsLayer;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		aForwardCmd.type = eCmdType_AddControlsLayer;
		aForwardCmd.layerID = theCmd.replacementLayer;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_ToggleControlsLayer:
		aForwardCmd = theCmd;
		DBG_ASSERT(theCmd.layerID > 0);
		if( sState.layers[theCmd.layerID].active() )
			aForwardCmd.type = eCmdType_RemoveControlsLayer;
		else
			aForwardCmd.type = eCmdType_AddControlsLayer;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_OpenSubMenu:
		aForwardCmd = Menus::openSubMenu(theCmd.rootMenuID, theCmd.subMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		sResults.menuStackAutoCommandRun.set(
			InputMap::menuOverlayID(theCmd.rootMenuID));
		updateMouseForMenu(theCmd.rootMenuID);
		break;
	case eCmdType_SwapMenu:
		aForwardCmd = Menus::swapMenu(
			theCmd.rootMenuID, theCmd.subMenuID, ECommandDir(theCmd.swapDir));
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		sResults.menuStackAutoCommandRun.set(
			InputMap::menuOverlayID(theCmd.rootMenuID));
		updateMouseForMenu(theCmd.rootMenuID);
		break;
	case eCmdType_MenuReset:
		aForwardCmd = Menus::reset(theCmd.rootMenuID, theCmd.menuItemID);
		if( aForwardCmd.type != eCmdType_Invalid )
		{
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			sResults.menuStackAutoCommandRun.set(
				InputMap::menuOverlayID(theCmd.rootMenuID));
		}
		updateMouseForMenu(theCmd.rootMenuID);
		break;
	case eCmdType_MenuConfirm:
		updateMouseForMenu(theCmd.rootMenuID, theCmd.andClick);
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.rootMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_MenuConfirmAndClose:
		updateMouseForMenu(theCmd.rootMenuID, theCmd.andClick);
		aForwardCmd = Menus::selectedMenuItemCommand(theCmd.rootMenuID);
		if( aForwardCmd.type >= eCmdType_FirstValid )
		{
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			// Close menu if didn't just switch to a sub-menu
			if( aForwardCmd.type < eCmdType_FirstMenuControl ||
				aForwardCmd.type > eCmdType_LastMenuControl )
			{
				processCommand(theBtnState,
					Menus::closeCommand(theCmd.rootMenuID), theLayerIdx);
			}
		}
		break;
	case eCmdType_MenuBack:
		processCommand(theBtnState,
			Menus::backCommand(theCmd.rootMenuID), theLayerIdx);
		aForwardCmd = Menus::closeLastSubMenu(theCmd.rootMenuID);
		if( aForwardCmd.type != eCmdType_Invalid )
		{
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			updateMouseForMenu(theCmd.rootMenuID);
		}
		break;
	case eCmdType_MenuClose:
		processCommand(theBtnState,
			Menus::closeCommand(theCmd.rootMenuID), theLayerIdx);
		break;
	case eCmdType_MenuEdit:
		Menus::editMenuItem(theCmd.rootMenuID);
		break;
	case eCmdType_MenuSelect:
		for(int i = 0; i < theCmd.count; ++i)
		{
			DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
			aForwardCmd = Menus::selectMenuItem(
				theCmd.rootMenuID, ECommandDir(theCmd.dir),
				theCmd.wrap, repeated || i < theCmd.count-1);
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		updateMouseForMenu(theCmd.rootMenuID);
		// Allow holding this button to auto-repeat after a delay
		sState.exclusiveAutoRepeatButton = theBtnState;
		break;
	case eCmdType_MenuSelectAndClose:
		for(int i = 0; i < theCmd.count; ++i)
		{
			DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
			aForwardCmd = Menus::selectMenuItem(
				theCmd.rootMenuID, ECommandDir(theCmd.dir),
				theCmd.wrap, repeated || i < theCmd.count-1);
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			// Close menu if didn't just switch to a sub-menu
			if( aForwardCmd.type < eCmdType_FirstMenuControl ||
				aForwardCmd.type > eCmdType_LastMenuControl )
			{
				processCommand(theBtnState,
					Menus::closeCommand(theCmd.rootMenuID), theLayerIdx);
				return;
			}
		}
		updateMouseForMenu(theCmd.rootMenuID);
		break;
	case eCmdType_MenuEditDir:
		DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
		Menus::editMenuItemDir(theCmd.rootMenuID, ECommandDir(theCmd.dir));
		break;
	case eCmdType_HotspotSelect:
		if( gHotspotsGuideMode == eHotspotGuideMode_Showing )
			gHotspotsGuideMode = eHotspotGuideMode_Redisplay;
		else
			gHotspotsGuideMode = eHotspotGuideMode_Redraw;
		DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
		sResults.selectHotspotDir = combined8Dir(
			sResults.selectHotspotDir, ECommandDir(theCmd.dir));
		break;
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveLook:
	case eCmdType_MoveMouse:
		// Invalid command for this function!
		DBG_ASSERT(false && "Invalid command sent to processCommand()");
		break;
	case eCmdType_MouseWheel:
		if( theCmd.mouseWheelMotionType == eMouseWheelMotion_Jump )
		{
			DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
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


static bool isAnalogCommand(const Command& theCommand)
{
	switch(theCommand.type)
	{
	case eCmdType_MoveTurn:
	case eCmdType_MoveStrafe:
	case eCmdType_MoveLook:
	case eCmdType_MoveMouse:
		return true;
	case eCmdType_MouseWheel:
		if( theCommand.mouseWheelMotionType != eMouseWheelMotion_Jump )
			return true;
		return false;
	default:
		return false;
	}
}


static bool isAutoRepeatCommand(const Command& theCommand)
{
	switch(theCommand.type)
	{
	case eCmdType_MenuSelect:
	case eCmdType_KeyBindCyclePrev:
	case eCmdType_KeyBindCycleNext:
	case eCmdType_HotspotSelect:
		return true;
	}

	return false;
}


static void processButtonPress(ButtonState& theBtnState)
{
	// Don't allow the same button to do this twice from the same press,
	// which could happen from both update() and processLayerHoldButtons().
	if( sResults.buttonPressProcessed.test(theBtnState.buttonID) )
		return;
	if( theBtnState.buttonID != eBtn_None )
		sResults.buttonPressProcessed.set(theBtnState.buttonID);

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
	case eCmdType_MoveLook:
	case eCmdType_MoveMouse:
		// Handled in processAnalogInput instead
		return;
	case eCmdType_MouseWheel:
		// Handled in processAnalogInput instead unless set to _Jump
		if( aCmd.mouseWheelMotionType != eMouseWheelMotion_Jump )
			return;
		break;
	case eCmdType_HotspotSelect:
		// If sync auto-repeat is happening already, assume it is from a
		// perpindicular direction being held and sync up with it instead
		if( sState.syncAutoRepeatActive )
			return;
		break;
	}
	processCommand(&theBtnState, aCmd,
		theBtnState.commands.layer[eBtnAct_Down]);
}


static void processAnalogInput(
	ButtonState& theBtnState,
	u8 theAnalogVal,
	bool isDigitalDown)
{
	if( theAnalogVal )
		isDigitalDown = false;
	else if( isDigitalDown )
		theAnalogVal = 255;
	else // if( theAnalogVal == 0 && !isDigitalDown )
		return;

	// All analog inputs should always be assigned to the _Down button action
	// When layer configuration changes, commands related to analog stick
	// movement may want to take effect immediately rather than waiting until
	// the stick is fully centered and pressed again. Below logic is to handle
	// which cases this is allowed.
	const CommandArray& aPressedCmds = theBtnState.commandsWhenPressed.cmd;
	const CommandArray& aCurrentCmds = theBtnState.commands.cmd;
	const bool pressedHasAnalogCmd =
		isAnalogCommand(aPressedCmds[eBtnAct_Down]);
	const bool currentHasAnalogCmd =
		isAnalogCommand(aCurrentCmds[eBtnAct_Down]);
	if( !pressedHasAnalogCmd && !currentHasAnalogCmd )
		return;

	Command aCmd;
	if( !currentHasAnalogCmd || aPressedCmds == aCurrentCmds ||
		theBtnState.vKeyHeld != 0 ||
		aPressedCmds[eBtnAct_Tap].type >= eCmdType_FirstValid ||
		aPressedCmds[eBtnAct_Hold].type >= eCmdType_FirstValid ||
		aCurrentCmds[eBtnAct_Press].type >= eCmdType_FirstValid ||
		isAutoRepeatCommand(aPressedCmds[eBtnAct_Down]) ||
		(theBtnState.layerHeld != 0 &&
		 theBtnState.commands.layer[eBtnAct_Down] != theBtnState.layerHeld) )
	{// Pressed command still active (or transition may have side effects)
		if( !pressedHasAnalogCmd )
			return;
		aCmd = aPressedCmds[eBtnAct_Down];
	}
	else if( pressedHasAnalogCmd && currentHasAnalogCmd )
	{// Both are analog commands - but only some allow a hot-swap
		aCmd = aPressedCmds[eBtnAct_Down];
		switch(aPressedCmds[eBtnAct_Down].type)
		{
		case eCmdType_MoveStrafe:
		case eCmdType_MoveLook:
			if( aCurrentCmds[eBtnAct_Down].type == eCmdType_MoveStrafe ||
				aCurrentCmds[eBtnAct_Down].type == eCmdType_MoveLook )
				aCmd = aCurrentCmds[eBtnAct_Down];
			break;
		}
	}
	else
	{// Should be no harmful side effect for switching to current immediately
		DBG_ASSERT(currentHasAnalogCmd);
		aCmd = aCurrentCmds[eBtnAct_Down];
	}

	DBG_ASSERT(isAnalogCommand(aCmd));

	DBG_ASSERT(aCmd.dir >= 0 && aCmd.dir < eCmdDir_Num);
	switch(u32(aCmd.type << 16) | aCmd.dir)
	{
	case (eCmdType_MoveTurn << 16) | eCmdDir_Forward:
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Forward:
	case (eCmdType_MoveLook << 16) | eCmdDir_Forward:
		sResults.charMove += theAnalogVal;
		break;
	case (eCmdType_MoveTurn << 16) | eCmdDir_Back:
	case (eCmdType_MoveStrafe << 16) | eCmdDir_Back:
	case (eCmdType_MoveLook << 16) | eCmdDir_Back:
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
	case (eCmdType_MoveLook << 16) | eCmdDir_Left:
		sResults.charStrafe -= theAnalogVal;
		sResults.charLookX -= theAnalogVal;
		break;
	case (eCmdType_MoveLook << 16) | eCmdDir_Right:
		sResults.charStrafe += theAnalogVal;
		sResults.charLookX += theAnalogVal;
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
	if( theBtnState.buttonID == eBtn_None )
		return;

	// Auto-repeat only commandsWhenPressed assigned to _Down
	const CommandArray& aCmdArray = theBtnState.commandsWhenPressed.cmd;
	Command aCmd = aCmdArray[eBtnAct_Down];

	// Don't auto-repeat when button has other conflicting actions assigned
	if( aCmdArray[eBtnAct_Tap].type >= eCmdType_FirstValid ||
		aCmdArray[eBtnAct_Hold].type >= eCmdType_FirstValid )
		return;

	if( aCmd.type == eCmdType_HotspotSelect )
	{
		// Use alternate timing and synchronize when holding multiple
		// directions at once to allow for slant-wise hotspot jumps
		if( kConfig.hotspotAutoRepeatRate == 0 )
			return;

		// Needs to be held for initial held time first before start repeating
		if( theBtnState.heldTime >= kConfig.hotspotAutoRepeatDelay ||
			sState.syncAutoRepeatActive )
		{
			// Flag need syncing auto-repeat in main update()
			sResults.syncAutoRepeatNeeded = true;

			// Wait until response flag from update() is set to continue
			if( !sState.syncAutoRepeatActive )
				return;

			// Repeat command whenever hit 0 delay (subtracted in update())
			if( sState.syncAutoRepeatDelay <= 0 )
			{
				processCommand(&theBtnState, aCmd,
					theBtnState.commandsWhenPressed.layer[eBtnAct_Down],
					true);
			}
		}
		return;
	}

	if( kConfig.autoRepeatRate == 0 )
		return;

	// Only the most recently-used button w/ auto-repeat command is allowed
	if( &theBtnState != sState.exclusiveAutoRepeatButton )
		return;

	// Wait for initial delay before beginning repeat
	if( theBtnState.heldTime < kConfig.autoRepeatDelay )
		return;

	// Flag need to update auto-repeat delay in main update()
	sResults.exclusiveAutoRepeatNeeded = true;

	// Repeat command whenever hit 0 delay (subtracted in update())
	if( sState.exclusiveAutoRepeatDelay <= 0 )
	{
		processCommand(&theBtnState, aCmd,
			theBtnState.commandsWhenPressed.layer[eBtnAct_Down],
			true);
	}
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
	// Always release anything being held by this button when it is released
	releaseKeyHeldByButton(theBtnState);
	releaseLayerHeldByButton(theBtnState);
	sResults.buttonPressProcessed.reset(theBtnState.buttonID);

	// Stop using this button for auto-repeat
	if( sState.exclusiveAutoRepeatButton == &theBtnState )
		sState.exclusiveAutoRepeatButton = null;

	// If this button was used as a modifier to execute a button combination
	// command (i.e. is L2 for the combo L2+X), then do not perform any other
	// actions for this button besides releasing above and resetting state.
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
	int aCmdLayer = theBtnState.commandsWhenPressed.layer[eBtnAct_Release];
	if( aCmd.type < eCmdType_FirstValid &&
		theBtnState.commands.cmd[eBtnAct_Press].type < eCmdType_FirstValid )
	{
		aCmd = theBtnState.commands.cmd[eBtnAct_Release];
		aCmdLayer = theBtnState.commands.layer[eBtnAct_Release];
	}
	processCommand(&theBtnState, aCmd, aCmdLayer);

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

	// Process analog input (analog axis values) continuously,
	// since they do not necessarily return hit/release when lightly pressed
	processAnalogInput(theBtnState, theAnalogVal, isDown);

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

	// If already holding this layer, no change needed
	if( aDownCmd.layerID == theBtnState.layerHeld )
		return false;

	// Possibly add held layer only if button newly hit
	if( !wasHit )
		return false;

	const int aLayerID = aDownCmd.layerID;
	DBG_ASSERT(size_t(aLayerID) < sState.layers.size());
	LayerState& aLayer = sState.layers[aLayerID];
	const bool layerAlreadyHeld = aLayer.active();

	// Process the button press event to begin holding the layer
	processButtonPress(theBtnState);

	return aLayer.active() && !layerAlreadyHeld;
}


static void processFiredSignals()
{
	gFiredSignals.reset(eBtn_None); // dummy signal, ignored
	gFiredSignals.reset(eBtn_Num); // aka eSpecialKey_None, ignored

	// Swap to temp bitset in case these cause any other signals to fire
	// (they'll be processed next time this function is called)
	const BitVector<256> lastFiredSignals(gFiredSignals);
	gFiredSignals.reset();

	if( lastFiredSignals.any() )
	{
		for(int i = 0, end = intSize(sState.signalCommands.size());
			i < end; ++i)
		{
			ActiveSignal& aSignalCmd = sState.signalCommands[i];
			DBG_ASSERT(aSignalCmd.signalID < lastFiredSignals.size());
			if( lastFiredSignals.test(aSignalCmd.signalID) )
				processCommand(null, aSignalCmd.cmd, aSignalCmd.layerID);
		}
	}
}


static void processLayerHoldButtons()
{
	int aLoopCount = 0;
	bool aLayerWasAdded = true;
	while(aLayerWasAdded)
	{
		aLayerWasAdded = false;
		for(int i = 1; i < eBtn_Num; ++i) // skip eBtn_None
		{
			aLayerWasAdded = aLayerWasAdded ||
				tryAddLayerFromButton(
					sState.gamepadButtons[i],
					Gamepad::buttonDown(EButton(i)),
					Gamepad::buttonHit(EButton(i)));
		}
		if( !aLayerWasAdded )
		{
			for(int i = 0; i < intSize(sState.layers.size()); ++i)
			{
				LayerState& aLayer = sState.layers[i];
				if( tryAddLayerFromButton(
						aLayer.autoButton,
						aLayer.autoButtonDown(),
						aLayer.autoButtonHit) )
				{
					aLayerWasAdded = true;
					aLayer.autoButtonHit = false;
				}
			}
		}

		// Update button commands for newly added layers
		// (removed layers & menus will be handled later in main update)
		if( aLayerWasAdded )
		{
			sortLayers();
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


static void updateMenusForCurrentLayers()
{
	BitVector<32> aPrevVisibleOverlays = gVisibleOverlays;
	BitVector<32> aPrevDisabledOverlays = gDisabledOverlays;
	gVisibleOverlays.reset();
	DBG_ASSERT(!sState.layersNeedSorting);
	for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
	{
		gVisibleOverlays |=
			InputMap::overlaysToShow(sState.layerOrder[i]);
		gVisibleOverlays &=
			~InputMap::overlaysToHide(sState.layerOrder[i]);
	}

	// Also check to see if any Menus should be shown as disabled because
	// they don't have any commands assigned to them any more.
	// Start by assuming all visible menus (that are of a style that CAN
	// have commmands assigned) are disabled...
	for(int i = 0, end = InputMap::menuOverlayCount(); i < end; ++i)
	{
		if( !Menus::overlayMenuAcceptsCommands(i) )
			gDisabledOverlays.set(i, false);
		else if( gVisibleOverlays.test(i) )
			gDisabledOverlays.set(i, true);
	}

	// Now re-enable any menus that have a command associated with them
	for(int aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx)
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
		{
			int anOverlayIdx = 0xFFFF;
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
				anOverlayIdx = InputMap::menuOverlayID(
					aBtnState.commands.cmd[aBtnAct].rootMenuID);
				break;
			default:
				continue;
			}
			DBG_ASSERT(anOverlayIdx < gDisabledOverlays.size());
			gDisabledOverlays.reset(anOverlayIdx);
		}
	}

	// Run Auto command for any newly-visible or newly-enabled menus,
	// update cursor for them if they are set to control mouse cursor,
	// and re-draw menus that changed disabled status to change if
	// selected item is drawn differently or not
	for(int i = gVisibleOverlays.firstSetBit();
		i < gVisibleOverlays.size();
		i = gVisibleOverlays.nextSetBit(i+1))
	{
		const int aMenuID = InputMap::overlayRootMenuID(i);
		const bool wasDisabled = aPrevDisabledOverlays.test(i);
		const bool isDisabled = gDisabledOverlays.test(i);
		if( wasDisabled != isDisabled )
			gRefreshOverlays.set(i);
		if( sResults.menuStackAutoCommandRun.test(i) )
			continue;
		if( !aPrevVisibleOverlays.test(i) || (wasDisabled && !isDisabled) )
		{
			processCommand(null, Menus::autoCommand(aMenuID), 0);
			sResults.menuStackAutoCommandRun.set(i);
			updateMouseForMenu(aMenuID);
		}
	}
}


static void updateHotspotArraysForCurrentLayers()
{
	DBG_ASSERT(!sState.layersNeedSorting);
	BitVector<32> aHotspotArraysEnabled(InputMap::hotspotArrayCount());
	for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
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
	DBG_ASSERT(!sState.layersNeedSorting);
	EMouseMode aFinalMouseMode = eMouseMode_Cursor;
	sState.mouseControllingRootMenu = -1;
	for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
	{
		const EMouseMode aLayerMouseMode =
			InputMap::mouseMode(sState.layerOrder[i]);
		// _Default means just use lower layers' mode
		if( aLayerMouseMode == eMouseMode_Default )
			continue;
		sState.mouseControllingRootMenu = -1;
		if( aLayerMouseMode == eMouseMode_HideOrLook )
		{
			// Act like _Default unless currently set to show cursor,
			// in which case act like _Hide
			if( aFinalMouseMode == eMouseMode_Cursor )
				aFinalMouseMode = eMouseMode_Hide;
			continue;
		}
		if( aLayerMouseMode == eMouseMode_Menu )
		{
			// Act as cursor mode but will trail cursor to point at
			// given menu ID's active selection whenever it changes
			aFinalMouseMode = eMouseMode_Cursor;
			sState.mouseControllingRootMenu =
				InputMap::mouseModeMenu(sState.layerOrder[i]);
			continue;
		}
		aFinalMouseMode = aLayerMouseMode;
	}

	InputDispatcher::setMouseMode(aFinalMouseMode);
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void loadProfile()
{
	kConfig.load();
	sState.clear();
	sResults.clear();
	loadLayerData();
	addControlsLayer(0);
	sState.layersNeedSorting = false;
	loadCommandsForCurrentLayers();
	updateMenusForCurrentLayers();
	updateMouseModeForCurrentLayers();
}


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.contains("Gamepad") || theProfileMap.contains("System") )
		kConfig.load();

	if( theProfileMap.contains("Scheme") ||
		theProfileMap.containsPrefix("Layer.") )
	{
		loadCommandsForCurrentLayers();
		updateMenusForCurrentLayers();
		updateHotspotArraysForCurrentLayers();
	}
}


void cleanup()
{
	for(int i = 0; i < ARRAYSIZE(sState.gamepadButtons); ++i)
	{
		releaseKeyHeldByButton(sState.gamepadButtons[i]);
		releaseLayerHeldByButton(sState.gamepadButtons[i]);
	}
	for(int i = 0, end = intSize(sState.layers.size()); i < end; ++i)
	{
		releaseKeyHeldByButton(sState.layers[i].autoButton);
		releaseLayerHeldByButton(sState.layers[i].autoButton);
	}
	sState.clear();
	sResults.clear();
}


void update()
{
	// Respond to any signals sent by key binds etc last dispatch update
	processFiredSignals();

	// Buttons that 'hold' layers need to be checked first, in order to make
	// sure that combinations enabled through layers like 'L2+X' work when
	// both buttons are pressed at exactly the same time.
	processLayerHoldButtons();

	// Treat each layer as also being a virtual button ("Auto" button)
	for(int i = 0, end = intSize(sState.layers.size()); i < end; ++i)
	{
		LayerState& aLayer = sState.layers[i];
		processButtonState(
			aLayer.autoButton,
			aLayer.autoButtonDown(),
			aLayer.autoButtonHit);
		aLayer.autoButtonHit = false;
	}

	// Process state changes of actual physical Gamepad buttons
	for(int i = 1; i < eBtn_Num; ++i) // skip eBtn_None
	{
		processButtonState(
			sState.gamepadButtons[i],
			Gamepad::buttonDown(EButton(i)),
			Gamepad::buttonHit(EButton(i)),
			Gamepad::buttonAnalogVal(EButton(i)));
	}

	// Responds to any signals caused by button presses
	processFiredSignals();

	// Process auto-button events for any newly-added/removed layers
	// .changedLayers might increase in size during this loop
	for(int i = 0, aLoopCount = 0;
		i < intSize(sResults.changedLayers.size());
		++i, ++aLoopCount)
	{
		LayerState& aLayer = sState.layers[sResults.changedLayers[i]];
		if( aLayer.autoButtonHit ||
			(!aLayer.autoButtonDown() && aLayer.autoButton.heldDown) )
		{
			processButtonState(
				aLayer.autoButton,
				aLayer.autoButtonDown(),
				aLayer.autoButtonHit);
			aLayer.autoButtonHit = false;
		}
		if( aLoopCount > kMaxLayerChangesPerUpdate )
		{
			logFatalError(
				"Infinite loop of Controls Layer changes detected!");
			break;
		}
	}

	const bool aLayerOrderChanged = !sResults.changedLayers.empty();
	if( aLayerOrderChanged )
	{
		// Sort layers so settings reflect layer priorities
		sortLayers();
		// Update mouse mode to reflect new layer layout before
		// sending input that might be affected by mouse mode
		updateMouseModeForCurrentLayers();
	}

	// Send input that was queued up by any of the above
	InputDispatcher::moveCharacter(
		sResults.charMove,
		sResults.charTurn,
		sResults.charStrafe,
		sResults.charMoveStartAutoRun,
		sResults.charMoveLockMovement);
	if( sResults.selectHotspotDir != eCmd8Dir_None )
	{
		Command aCmd;
		const int aNextHotspot = HotspotMap::getNextHotspotInDir(
			sResults.selectHotspotDir);
		if( aNextHotspot )
		{
			aCmd.type = eCmdType_MoveMouseToHotspot;
			aCmd.hotspotID = dropTo<u16>(aNextHotspot);
		}
		else
		{
			aCmd.type = eCmdType_MoveMouseToOffset;
			aCmd.dir = dropTo<u16>(sResults.selectHotspotDir);
		}
		InputDispatcher::moveMouseTo(aCmd);
	}
	InputDispatcher::moveMouse(
		sResults.mouseMoveX,
		sResults.mouseMoveY,
		sResults.charLookX,
		sResults.mouseMoveDigital);
	InputDispatcher::scrollMouseWheel(
		sResults.mouseWheelY,
		sResults.mouseWheelDigital,
		sResults.mouseWheelStepped);
	for(int i = 0, end = intSize(sResults.queuedKeys.size()); i < end; ++i)
		InputDispatcher::sendKeyCommand(sResults.queuedKeys[i]);

	// Update timing for held auto-repeat buttons
	if( sResults.syncAutoRepeatNeeded )
	{
		if( sState.syncAutoRepeatActive )
		{
			if( sState.syncAutoRepeatDelay <= 0 )
				sState.syncAutoRepeatDelay += kConfig.hotspotAutoRepeatRate;
			sState.syncAutoRepeatDelay -= gAppFrameTime;
		}
		sState.syncAutoRepeatActive = true;
	}
	else
	{
		sState.syncAutoRepeatDelay = 0;
		sState.syncAutoRepeatActive = false;
	}
	if( sResults.exclusiveAutoRepeatNeeded )
	{
		if( sState.exclusiveAutoRepeatDelay <= 0 )
			sState.exclusiveAutoRepeatDelay += kConfig.autoRepeatRate;
		sState.exclusiveAutoRepeatDelay -= gAppFrameTime;
	}
	else
	{
		sState.exclusiveAutoRepeatDelay = 0;
	}

	// Clear results for next update
	sResults.clear();

	// Update settings for new layer order to use during next update
	if( aLayerOrderChanged )
	{
		sortLayers();
		loadCommandsForCurrentLayers();
		updateMenusForCurrentLayers();
		updateHotspotArraysForCurrentLayers();
		#ifdef INPUT_TRANSLATOR_DEBUG_PRINT
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
