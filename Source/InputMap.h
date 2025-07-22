//-----------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//-----------------------------------------------------------------------------

#pragma once

/*
	Loads and manages the configuration settings for mapping gamepad
	input to keyboard/mouse input, including menu configurations and actions.
	Also tracks some HUD-related information relevant to control schemes
	such as button labels and which HUD elements need to be displayed.
*/

#include "Common.h"

namespace InputMap
{

struct ButtonActions
{
	Command cmd[eBtnAct_Num];
	int holdTimeForAction; // -1 == not specified (use default)
	ButtonActions() : holdTimeForAction(-1) {}
};
struct ButtonRemap : public std::vector<EButton>
{
	ButtonRemap() : std::vector<EButton>(eBtn_Num)
	{ for(int i = 0; i < eBtn_Num; ++i) (*this)[i] = EButton(i); }
	ButtonRemap(const ButtonRemap& rhs) : std::vector<EButton>(rhs) {}
	private: void resize(size_t);
};
typedef VectorMap<EButton, ButtonActions> ButtonActionsMap;
typedef VectorMap<u16, Command> SignalActionsMap;

// Load the input mappings and macro sets from current profile
void loadProfile();
void loadProfileChanges();

// COMMANDS
const char* cmdString(const Command& theCommand);
const u8* cmdVKeySeq(const Command& theCommand);

// KEYBINDS
Command keyBindCommand(int theKeyBindID);
u16 keyForSpecialAction(ESpecialKey);
u16 specialKeyToKeyBindID(ESpecialKey);
u32 keyBindSignalID(int theKeyBindID);
u16 keyBindArrayIndexToKeyBindID(int theArrayID, int theIndex);
u32 keyBindArraySignalID(int theArrayID);
// Adjust index by theSteps but skipping over any invalid (unassigned) keys
// Steps of 0 may still adjust by +1 or more if array[theIndex] is invalid
int offsetKeyBindArrayIndex(
	int theArrayID, int theIndex, int theSteps, bool wrap);

// CONTROLS LAYERS
const ButtonActionsMap& buttonCommandsForLayer(int theLayerID);
const SignalActionsMap& signalCommandsForLayer(int theLayerID);
const ButtonRemap& buttonRemap(int theLayerID);
// Returns how long given button needs to be held to trigger eBtnAct_Hold
int commandHoldTime(int theLayerID, EButton theButton);
int parentLayer(int theLayerID);
int comboParentLayer(int theLayerID); // 0 if not a combo layer

// Gets sort priority for given layer ID
s8 layerPriority(int theLayerID);

// Checks what EMouseMode given controls layer requests be used
EMouseMode mouseMode(int theLayerID);

// Gets what HUD elements given layer specifically wants to show
const BitVector<32>& hudElementsToShow(int theLayerID);

// Gets what HUD elements given layer specifically wants to hide
// (overrides any lower layers wishing to show these HUD elements)
const BitVector<32>& hudElementsToHide(int theLayerID);

// Gets what hotspot arrays given layer specifically wants to enable
const BitVector<32>& hotspotArraysToEnable(int theLayerID);

// Gets what hotspot arrays given layer specifically wants to disable
// (overrides any lower layers wishing to enable these hotspots)
const BitVector<32>& hotspotArraysToDisable(int theLayerID);

// Gets layers to auto-add or remove alongside given layer
const BitVector<32>& autoAddLayers(int theLayerID);
const BitVector<32>& autoRemoveLayers(int theLayerID);

// Returns a combo layer ID if one exists, otherwise 0
int comboLayerID(int theLayerID1, int theLayerID2);

// MENUS
Command commandForMenuItem(int theMenuID, int theMenuItemIdx);
Command commandForMenuDir(int theMenuID, ECommandDir theDir);
Command menuAutoCommand(int theMenuID);
Command menuBackCommand(int theMenuID);
EHUDType menuStyle(int theMenuID);
int rootMenuOfMenu(int theMenuID);
int menuHotspotArray(int theMenuID); // for eMenuStyle_Hotspots only
int menuGridWidth(int theMenuID); // for eMenuStyle_Grid
int menuGridHeight(int theMenuID); // for eMenuStyle_Grid
// Strings used for Profile getStr()/setStr() functions
std::string menuSectionName(int theMenuID);
std::string menuItemKeyName(int theMenuItemIdx);
std::string menuItemDirKeyName(ECommandDir theDir);
int menuSectionNameToID(const std::string& theProfileSectionName);
void menuItemStringToSubMenuName(std::string& theFullMenuItemString);

// HOTSPOTS
const Hotspot& getHotspot(int theHotspotID);
int firstHotspotInArray(int theHotspotArrayID);
int sizeOfHotspotArray(int theHotspotArrayID);
const Hotspot* keyBindArrayHotspot(int theArrayID, int theIndex);
void modifyHotspot(int theHotspotID, const Hotspot& theNewValues);

// HUD ELEMENTS
EHUDType hudElementType(int theHUDElementID);
bool hudElementIsAMenu(int theHUDElementID);
// Not all HUD elements are menus, so may return invalid menu ID
int menuForHUDElement(int theHUDElementID);
int hudElementForMenu(int theMenuID);
// Only valid for the Hotspot HUD element type
int hotspotForHUDElement(int theHUDElementID);
// Only valid for HUD element types that are tied to Key Bind Arrays
int keyBindArrayForHUDElement(int theHUDElementID);
// Does not include HUD. or Menu. prefix
std::string hudElementKeyName(int theHUDElementID);

// SIZES
int keyBindCount();
int keyBindArrayCount();
int keyBindArraySize(int theArrayID);
int controlsLayerCount();
int hudElementCount();
int menuCount();
int menuItemCount(int theMenuID);
int hotspotCount();
int hotspotArrayCount();

// LABELS
const std::string& layerLabel(int theLayerID);
std::string hotspotLabel(int theHotspotID);
const std::string& hotspotArrayLabel(int theHotspotArrayID);
const std::string& menuLabel(int theMenuID);
const std::string& menuItemLabel(int theMenuID, int theMenuItemIdx);
const std::string& menuItemAltLabel(int theMenuID, int theMenuItemIdx);
const std::string& menuDirLabel(int theMenuID, ECommandDir theDir);
const std::string& keyBindLabel(int theKeyBindID);

} // InputMap
