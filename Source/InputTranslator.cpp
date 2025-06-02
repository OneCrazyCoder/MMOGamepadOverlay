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


//-----------------------------------------------------------------------------
// Local Structures
//-----------------------------------------------------------------------------

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

struct ButtonState
{
	ButtonCommandSet commands;
	ButtonCommandSet commandsWhenPressed;
	int heldTime;
	u16 layerHeld;
	u16 vKeyHeld;
	u16 buttonID;
	bool heldDown;
	bool holdActionDone;
	bool usedInButtonCombo;

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

struct LayerState
{
	ButtonState autoButton;
	u16 parentLayerID;
	u16 altParentLayerID; // 0 unless is a combo layer
	union{ bool active; bool autoButtonDown; }; // mean the same thing
	s8 heldActiveByButton;
	bool autoButtonHit;
	bool buttonCommandUsed;

	void clear()
	{
		autoButton.clear();
		autoButton.buttonID = eBtn_None;
		parentLayerID = 0;
		altParentLayerID = 0;
		active = false;
		autoButtonHit = false;
		buttonCommandUsed = false;
		heldActiveByButton = 0;
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
	int exclusiveAutoRepeatDelay;
	int syncAutoRepeatDelay;
	bool syncAutoRepeatActive;

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
		exclusiveAutoRepeatButton = null;
		exclusiveAutoRepeatDelay = 0;
		syncAutoRepeatDelay = 0;
		syncAutoRepeatActive = false;
	}
};

struct InputResults
{
	std::vector<Command> queuedKeys;
	BitVector<32> menuHEAutoCommandRun;
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
	bool layerChangeMade;
	bool exclusiveAutoRepeatNeeded;
	bool syncAutoRepeatNeeded;

	void clear()
	{
		queuedKeys.clear();
		menuHEAutoCommandRun.clearAndResize(InputMap::hudElementCount());
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
		layerChangeMade = false;
		exclusiveAutoRepeatNeeded = false;
		syncAutoRepeatNeeded = false;
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
		for(int i = 0, end = intSize(aBtnCmdsMap.size()); i < end; ++i)
		{
			const EButton aBtnID = aBtnCmdsMap[i].first;
			if( aBtnID == eBtn_None )
				continue;
			const InputMap::ButtonActions& aBtnActions = aBtnCmdsMap[i].second;
			ButtonState& aBtnState = sState.gamepadButtons[aBtnID];
			for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
			{
				if( aBtnActions.cmd[aBtnAct].type == eCmdType_Empty )
					continue;

				aBtnState.commands.cmd[aBtnAct] = aBtnActions.cmd[aBtnAct];
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
			if( aBtnActions.cmd[eBtnAct_Hold].type != eCmdType_Empty &&
				aBtnActions.cmd[eBtnAct_Hold].type != eCmdType_Unassigned &&
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


static void removeControlsLayer(int theLayerID)
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
			aLayer.heldActiveByButton = 0;
			sResults.layerChangeMade = true;

			// Remove from the layer order and recover iterator to continue
			next_itr = std::vector<u16>::reverse_iterator(
				sState.layerOrder.erase((itr+1).base()));
		}
		else if( (aLayer.parentLayerID == theLayerID ||
				  aLayer.altParentLayerID == theLayerID) &&
				  aLayer.heldActiveByButton <= 0 )
		{
			// Need to use recursion so also remove grandchildren, etc
			removeControlsLayer(*itr);
			// Recursion will mess up our iterator beyond reasonable recovery,
			// so just start at the beginning (end) again if removed anything
			next_itr = sState.layerOrder.rbegin();
		}
	}
}


static bool layerIsDescendant(const LayerState& theLayer, int theParentLayerID)
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
	int theParentLayerID, int thePriority, bool isHeldLayer = false)
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
		if( sState.layers[*result].heldActiveByButton > 0 && !isHeldLayer )
			break;
		if( sState.layers[*result].heldActiveByButton <= 0 && isHeldLayer )
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

static void addControlsLayer(int theLayerID); // forward declare
static void addComboLayers(int theNewLayerID); // forward declare
static void sortComboLayers(); // forward declare

static void moveControlsLayerToTop(int theLayerID, bool isHeldLayer = false)
{
	DBG_ASSERT(size_t(theLayerID) < sState.layers.size());
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
	std::vector<u16> sTempOrder; 
	sTempOrder.assign(anOldPos, anOldPosEnd);
	sState.layerOrder.erase(anOldPos, anOldPosEnd);

	// Reset "Hold Auto ### =" held times for any moved layer that have
	// not yet executed their hold action (these are usually used for
	// some kind of delayed action after the layer has been left alone
	// for a while, like hiding a HUD element by removing themselves).
	for(int i = 0, end = intSize(sTempOrder.size()); i < end; ++i)
	{
		if( !sState.layers[sTempOrder[i]].autoButton.holdActionDone )
			sState.layers[sTempOrder[i]].autoButton.heldTime = 0;
	}

	// Find new position to add the layers back to
	std::vector<u16>::iterator aNewPos = sState.layerOrder.end();
	if( sState.layers[theLayerID].heldActiveByButton <= 0 )
	{
		aNewPos = layerOrderInsertPos(
			sState.layers[theLayerID].parentLayerID,
			InputMap::layerPriority(theLayerID), isHeldLayer);
	}
	sState.layerOrder.insert(aNewPos, sTempOrder.begin(), sTempOrder.end());
	sResults.layerChangeMade = true;

	// Re-sort any possibly affected combo layers
	sortComboLayers();
}


static void addControlsLayer(int theLayerID)
{
	DBG_ASSERT(size_t(theLayerID) < sState.layers.size());

	if( sState.layers[theLayerID].active )
	{
		moveControlsLayerToTop(theLayerID);
		return;
	}
	DBG_ASSERT(!sState.layers[theLayerID].active);

	const int aParentLayerID = InputMap::parentLayer(theLayerID);
	DBG_ASSERT(size_t(aParentLayerID) < sState.layers.size());
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
		dropTo<u16>(theLayerID));
	LayerState& aLayer = sState.layers[theLayerID];
	aLayer.parentLayerID = dropTo<u16>(aParentLayerID);
	aLayer.altParentLayerID = 0;
	aLayer.active = true;
	aLayer.autoButtonHit = true;
	sResults.layerChangeMade = true;
	addComboLayers(theLayerID);
}


static void addComboLayers(int theNewLayerID)
{
	DBG_ASSERT(size_t(theNewLayerID) < sState.layers.size());

	// Find and add any combo layers with theNewLayerID as a base and
	// the other base layer also being active
	bool aLayerWasAdded = false;
	for(int i = 1; i < intSize(sState.layerOrder.size()); ++i)
	{
		const int aLayerID = sState.layerOrder[i];
		if( aLayerID == theNewLayerID )
			continue;
		int aComboLayerID = InputMap::comboLayerID(theNewLayerID, aLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active )
		{
			addControlsLayer(aComboLayerID);
			sState.layers[aComboLayerID].parentLayerID =
				dropTo<u16>(theNewLayerID);
			sState.layers[aComboLayerID].altParentLayerID =
				dropTo<u16>(aLayerID);
			aLayerWasAdded = true;
			i = 0; // since sState.layerOrder might have changed
		}
		aComboLayerID = InputMap::comboLayerID(aLayerID, theNewLayerID);
		if( aComboLayerID != 0 && !sState.layers[aComboLayerID].active )
		{
			addControlsLayer(aComboLayerID);
			sState.layers[aComboLayerID].parentLayerID =
				dropTo<u16>(aLayerID);
			sState.layers[aComboLayerID].altParentLayerID =
				dropTo<u16>(theNewLayerID);
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
		int oldPos;
		LayerPriority *parent, *altParent;
		int get(int depth) const
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
		int id;
		LayerPriority* priority;
		bool operator<(const LayerSorter& rhs) const
		{
			int priorityDepth = 0;
			int priorityA = priority->get(priorityDepth);
			int priorityB = rhs.priority->get(priorityDepth);
			while(priorityA == priorityB && (priorityA || priorityB))
			{
				++priorityDepth;
				priorityA = priority->get(priorityDepth);
				priorityB = rhs.priority->get(priorityDepth);
			}
			return priorityA < priorityB;
		}
	};
	static std::vector<LayerPriority> sLayerPriorities;
	static std::vector<LayerSorter> sLayerSorters;
	sLayerPriorities.resize(sState.layerOrder.size());
	sLayerSorters.resize(sState.layerOrder.size());
	for(int i = 0; i < intSize(sState.layerOrder.size()); ++i)
	{
		const int aLayerID = sState.layerOrder[i];
		LayerState& aLayer = sState.layers[aLayerID];
		sLayerPriorities[i].oldPos = i;
		sLayerPriorities[i].parent = null;
		sLayerPriorities[i].altParent = null;
		if( aLayer.altParentLayerID )
		{
			for(int j = 0; j < intSize(sState.layerOrder.size()); ++j)
			{
				if( sState.layerOrder[j] == aLayer.parentLayerID )
					sLayerPriorities[i].parent = &sLayerPriorities[j];
				if( sState.layerOrder[j] == aLayer.altParentLayerID )
					sLayerPriorities[i].altParent = &sLayerPriorities[j];
			}
		}
		sLayerSorters[i].id = sState.layerOrder[i];
		sLayerSorters[i].priority = &sLayerPriorities[i];
	}
	std::sort(sLayerSorters.begin()+1, sLayerSorters.end());
	for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
		sState.layerOrder[i] = dropTo<u16>(sLayerSorters[i].id);
}


static void flagLayerButtonCommandUsed(int theLayerID)
{
	if( theLayerID == 0 )
		return;

	sState.layers[theLayerID].buttonCommandUsed = true;
	flagLayerButtonCommandUsed(sState.layers[theLayerID].parentLayerID);
	flagLayerButtonCommandUsed(sState.layers[theLayerID].altParentLayerID);
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
		aMoveCmd.menuItemID = dropTo<u16>(Menus::selectedItem(theCmd.menuID));
		aMoveCmd.andClick = theCmd.andClick;
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
	case eCmdType_Empty:
	case eCmdType_Unassigned:
	case eCmdType_DoNothing:
		// Do nothing
		break;
	case eCmdType_SignalOnly:
		// Do nothing but fire off signal
		sResults.queuedKeys.push_back(theCmd);
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
	case eCmdType_ChatBoxString:
		// Queue to send after press/release commands
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
	case eCmdType_TriggerKeyBind:
		// Just forward on the key bind's command
		aForwardCmd = InputMap::keyBindCommand(theCmd.keyBindID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
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
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
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
		// Allow holding this button to auto-repeat after a delay
		sState.exclusiveAutoRepeatButton = theBtnState;
		break;
	case eCmdType_KeyBindArrayNext:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
		gKeyBindArrayLastIndex[theCmd.keybindArrayID] =
			InputMap::offsetKeyBindArrayIndex(
				theCmd.keybindArrayID,
				gKeyBindArrayLastIndex[theCmd.keybindArrayID],
				theCmd.count, theCmd.wrap);
		aForwardCmd = InputMap::keyBindArrayCommand(
			theCmd.keybindArrayID,
			gKeyBindArrayLastIndex[theCmd.keybindArrayID]);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		gKeyBindArrayLastIndexChanged.set(theCmd.keybindArrayID);
		// Allow holding this button to auto-repeat after a delay
		sState.exclusiveAutoRepeatButton = theBtnState;
		break;
	case eCmdType_KeyBindArrayDefault:
		DBG_ASSERT(theCmd.keybindArrayID < gKeyBindArrayLastIndex.size());
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
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
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
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
		gFiredSignals.set(
			InputMap::keyBindArraySignalID(theCmd.keybindArrayID));
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
	case eCmdType_QuitApp:
		gShutdown = true;
		break;
	case eCmdType_AddControlsLayer:
		addControlsLayer(theCmd.layerID);
		break;
	case eCmdType_RemoveControlsLayer:
		aForwardCmd.layerID = theCmd.layerID;
		if( aForwardCmd.layerID == 0 )
			aForwardCmd.layerID = dropTo<u16>(theLayerIdx);
		removeControlsLayer(aForwardCmd.layerID);
		break;
	case eCmdType_HoldControlsLayer:
		// Special-case, handled elsewhere
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
		if( sState.layers[theCmd.layerID].active )
			aForwardCmd.type = eCmdType_RemoveControlsLayer;
		else
			aForwardCmd.type = eCmdType_AddControlsLayer;
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		break;
	case eCmdType_OpenSubMenu:
		aForwardCmd = Menus::openSubMenu(theCmd.menuID, theCmd.subMenuID);
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		sResults.menuHEAutoCommandRun.set(
			InputMap::hudElementForMenu(theCmd.menuID));
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_SwapMenu:
		aForwardCmd = Menus::swapMenu(
			theCmd.menuID, theCmd.subMenuID, ECommandDir(theCmd.swapDir));
		processCommand(theBtnState, aForwardCmd, theLayerIdx);
		sResults.menuHEAutoCommandRun.set(
			InputMap::hudElementForMenu(theCmd.menuID));
		moveMouseToSelectedMenuItem(theCmd);
		break;
	case eCmdType_MenuReset:
		aForwardCmd = Menus::reset(theCmd.menuID, theCmd.menuItemID);
		if( aForwardCmd.type != eCmdType_Empty )
		{
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
			sResults.menuHEAutoCommandRun.set(
				InputMap::hudElementForMenu(theCmd.menuID));
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
			// Close menu if didn't just switch to a sub-menu
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
		aForwardCmd = Menus::closeLastSubMenu(theCmd.menuID);
		if( aForwardCmd.type != eCmdType_Empty )
		{
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
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
			DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
			aForwardCmd = Menus::selectMenuItem(
				theCmd.menuID, ECommandDir(theCmd.dir),
				theCmd.wrap, repeated || i < theCmd.count-1);
			processCommand(theBtnState, aForwardCmd, theLayerIdx);
		}
		moveMouseToSelectedMenuItem(theCmd);
		// Allow holding this button to auto-repeat after a delay
		sState.exclusiveAutoRepeatButton = theBtnState;
		break;
	case eCmdType_MenuSelectAndClose:
		for(int i = 0; i < theCmd.count; ++i)
		{
			DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
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
		DBG_ASSERT(theCmd.dir >= 0 && theCmd.dir < eCmdDir_Num);
		Menus::editMenuItemDir(theCmd.menuID, ECommandDir(theCmd.dir));
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


static bool isContinuousCommand(const Command& theCommand)
{
	switch(theCommand.type)
	{
	case eCmdType_PressAndHoldKey:
	case eCmdType_KeyBindArrayHoldIndex:
	case eCmdType_HoldControlsLayer:
		// Held state
		return true;
	case eCmdType_MenuSelect:
	case eCmdType_KeyBindArrayPrev:
	case eCmdType_KeyBindArrayNext:
	case eCmdType_HotspotSelect:
		// Auto-repeat
		return true;
	}

	// Analog input
	return isAnalogCommand(theCommand);
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
	if( aPressedCmds == aCurrentCmds || !currentHasAnalogCmd ||
		aPressedCmds[eBtnAct_Release].type >= eCmdType_FirstValid ||
		aPressedCmds[eBtnAct_Tap].type >= eCmdType_FirstValid ||
		aPressedCmds[eBtnAct_Hold].type >= eCmdType_FirstValid ||
		aCurrentCmds[eBtnAct_Press].type >= eCmdType_FirstValid ||
		isContinuousCommand(aPressedCmds[eBtnAct_Down]) )
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
		case eCmdType_MouseWheel:
		case eCmdType_HotspotSelect:
			if( aCurrentCmds[eBtnAct_Down].type == eCmdType_MouseWheel ||
				aCurrentCmds[eBtnAct_Down].type == eCmdType_HotspotSelect )
				aCmd = aCurrentCmds[eBtnAct_Down];
			break;
		}
	}
	else
	{// Should be no harmful side effect for switching to current immediately
		if( !currentHasAnalogCmd )
			return;
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
	// Always release any key being held by this button when button is released
	releaseKeyHeldByButton(theBtnState);

	// Stop using this button for auto-repeat
	if( sState.exclusiveAutoRepeatButton == &theBtnState )
		sState.exclusiveAutoRepeatButton = null;

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
	int aCmdLayer = theBtnState.commandsWhenPressed.layer[eBtnAct_Release];
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


static void releaseLayerHeldByButton(ButtonState& theBtnState)
{
	if( theBtnState.layerHeld )
	{
		// Flag if used in a button combo, like L2 in L2+X
		if( sState.layers[theBtnState.layerHeld].buttonCommandUsed )
			theBtnState.usedInButtonCombo = true;
		if( sState.layers[theBtnState.layerHeld].heldActiveByButton <= 1 )
			removeControlsLayer(theBtnState.layerHeld);
		else
			--sState.layers[theBtnState.layerHeld].heldActiveByButton;
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

	// If already holding this layer, no change needed
	if( aDownCmd.layerID == theBtnState.layerHeld )
		return false;

	// Possibly add held layer only if button newly hit
	if( !wasHit )
		return false;

	// Release any layer previously held by this button
	const int aLayerID = aDownCmd.layerID;
	DBG_ASSERT(size_t(aLayerID) < sState.layers.size());
	DBG_ASSERT(aLayerID > 0);
	releaseLayerHeldByButton(theBtnState);
	LayerState& aLayer = sState.layers[aLayerID];

	// Check if layer is already active by other means
	if( aLayer.active )
	{
		if( aLayer.heldActiveByButton > 0 )
		{// Second button also holding the same layer - increment ref counter
			++aLayer.heldActiveByButton;
			theBtnState.layerHeld = dropTo<u16>(aLayerID);
			moveControlsLayerToTop(aLayerID, true);
			aLayer.buttonCommandUsed = false;
			return false;
		}
		else
		{// Remove non-held layer so can re-add as a held layer below
			removeControlsLayer(aLayerID);
		}
	}

	transDebugPrint(
		"Holding Controls Layer '%s'\n",
		InputMap::layerLabel(aLayerID).c_str());

	sState.layerOrder.insert(layerOrderInsertPos(
		0, InputMap::layerPriority(aLayerID), true),
		dropTo<u16>(aLayerID));
	aLayer.parentLayerID = 0;
	aLayer.altParentLayerID = 0;
	aLayer.active = true;
	aLayer.autoButtonHit = true;
	aLayer.heldActiveByButton = 1;
	sResults.layerChangeMade = true;
	theBtnState.layerHeld = dropTo<u16>(aLayerID);
	addComboLayers(aLayerID);
	return true;
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
	BitVector<32> aPrevVisibleHUD = gVisibleHUD;
	BitVector<32> aPrevDisabledHUD = gDisabledHUD;
	gVisibleHUD.reset();
	for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
	{
		gVisibleHUD |=
			InputMap::hudElementsToShow(sState.layerOrder[i]);
		gVisibleHUD &=
			~InputMap::hudElementsToHide(sState.layerOrder[i]);
	}

	// Also check to see if any Menus should be shown as disabled because
	// they don't have any commands assigned to them any more.
	// Start by assuming all visible menus are disabled...
	for(int i = 0, end = InputMap::hudElementCount(); i < end; ++i)
	{
		if( InputMap::hudElementIsAMenu(i) && gVisibleHUD.test(i) )
			gDisabledHUD.set(i);
	}

	// Now re-enable any menus that have a command associated with them
	for(int aBtnIdx = 1; aBtnIdx < eBtn_Num; ++aBtnIdx)
	{
		ButtonState& aBtnState = sState.gamepadButtons[aBtnIdx];
		for(int aBtnAct = 0; aBtnAct < eBtnAct_Num; ++aBtnAct)
		{
			int aHUDIdx = 0xFFFF;
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
		const int aMenuID = InputMap::menuForHUDElement(i);
		const bool wasDisabled = aPrevDisabledHUD.test(i);
		const bool isDisabled = gDisabledHUD.test(i);
		if( wasDisabled != isDisabled )
			gRefreshHUD.set(i);
		if( sResults.menuHEAutoCommandRun.test(i) )
			continue;
		if( !aPrevVisibleHUD.test(i) || (wasDisabled && !isDisabled) )
		{
			processCommand(null, Menus::autoCommand(aMenuID), 0);
			sResults.menuHEAutoCommandRun.set(i);
		}
	}
}


static void updateHotspotArraysForCurrentLayers()
{
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
	EMouseMode aMouseMode = eMouseMode_Cursor;
	for(int i = 0, end = intSize(sState.layerOrder.size()); i < end; ++i)
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


void loadProfileChanges()
{
	const Profile::SectionsMap& theProfileMap = Profile::changedSections();
	if( theProfileMap.contains("Gamepad") || theProfileMap.contains("System") )
		kConfig.load();

	if( theProfileMap.contains("Scheme") ||
		theProfileMap.containsPrefix("Layer.") )
	{
		loadCommandsForCurrentLayers();
		updateHUDStateForCurrentLayers();
		updateHotspotArraysForCurrentLayers();
	}
}


void cleanup()
{
	for(int i = 0; i < ARRAYSIZE(sState.gamepadButtons); ++i)
		releaseKeyHeldByButton(sState.gamepadButtons[i]);
	for(int i = 0, end = intSize(sState.layers.size()); i < end; ++i)
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
	for(int i = 0, end = intSize(sState.layers.size()); i < end; ++i)
	{
		LayerState& aLayer = sState.layers[i];
		processButtonState(
			aLayer.autoButton,
			aLayer.autoButtonDown,
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

	// Execute commands by active layers in response to fired signals
	BitVector<256> aFiredSignals(gFiredSignals);
	gFiredSignals.reset();
	aFiredSignals.reset(eBtn_None); // dummy signal, ignored
	if( aFiredSignals.any() )
	{
		for(int i = 0, end = intSize(sState.signalCommands.size());
			i < end; ++i)
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
			for(int i = 0, end = intSize(sState.layers.size()); i < end; ++i)
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


bool isLayerActive(int theLayerID)
{
	DBG_ASSERT(size_t(theLayerID) < sState.layers.size());
	return sState.layers[theLayerID].active;
}

#undef transDebugPrint
#undef INPUT_TRANSLATOR_DEBUG_PRINT

} // InputTranslator
