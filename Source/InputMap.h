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

// Load the input mappings and macro sets from current profile
void loadProfile();
void loadProfileChanges();

// COMMANDS
const char* cmdString(const Command& theCommand);
const u8* cmdVKeySeq(const Command& theCommand);

// KEYBINDS
u16 keyForSpecialAction(ESpecialKey);
u32 specialKeySignalID(ESpecialKey);
Command keyBindCommand(int theKeyBindID);
u32 keyBindSignalID(int theKeyBindID);
Command keyBindArrayCommand(int theArrayID, int theIndex);
u32 keyBindArraySignalID(int theArrayID);
// Adjust index by theSteps but skipping over any invalid (unassigned) keys
// Steps of 0 may still adjust by +1 or more if array[theIndex] is invalid
int offsetKeyBindArrayIndex(
	int theArrayID, int theIndex, int theSteps, bool wrap);

// CONTROLS LAYERS

// Get commands to execute for given button in given layer, in the
// form of an array of 'Command' of size 'eBtnAct_Num', or NULL
// if no commands have been assigned to given layer & button at all
const Command* commandsForButton(int theLayerID, EButton theButton);

// Get commands to execute in response to bits set in gFiredSignals
const VectorMap<u16, Command>& signalCommandsForLayer(int theLayerID);

// Returns how long given button needs to be held to trigger eBtnAct_Hold
int commandHoldTime(int theLayerID, EButton theButton);

// Gets parent layer for given layer ID
int parentLayer(int theLayerID);

// Gets sort priority for given layer ID
int layerPriority(int theLayerID);

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
const std::string& hudElementKeyName(int theHUDElementID);

// SIZES
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

} // InputMap
