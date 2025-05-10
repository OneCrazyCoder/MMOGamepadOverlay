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
const char* cmdStr(const Command& theCommand);

// KEYBINDS
u16 keyForSpecialAction(ESpecialKey theAction);
const Command& keyBindArrayCommand(u16 theArrayID, u16 theIndex);
u16 keyBindArraySignalID(u16 theArrayID);
// Adjust index by theOffset but skipping over any _Empty commands
// Offset of 0 may still adjust by +1 or more if array[theIndex] is _Empty
u16 offsetKeyBindArrayIndex(
	u16 theArrayID, u16 theIndex, s16 theOffset, bool wrap);

// CONTROLS LAYERS

// Get commands to execute for given button in given layer, in the
// form of an array of 'Command' of size 'eBtnAct_Num', or NULL
// if no commands have been assigned to given layer & button at all
const Command* commandsForButton(u16 theLayerID, EButton theButton);

// Get commands to execute in response to bits set in gFiredSignals
const VectorMap<u16, Command>& signalCommandsForLayer(u16 theLayerID);

// Returns how long given button needs to be held to trigger eBtnAct_Hold
u32 commandHoldTime(u16 theLayerID, EButton theButton);

// Gets parent layer for given layer ID
u16 parentLayer(u16 theLayerID);

// Gets sort priority for given layer ID
s8 layerPriority(u16 theLayerID);

// Checks what EMouseMode given controls layer requests be used
EMouseMode mouseMode(u16 theLayerID);

// Gets what HUD elements given layer specifically wants to show
const BitVector<32>& hudElementsToShow(u16 theLayerID);

// Gets what HUD elements given layer specifically wants to hide
// (overrides any lower layers wishing to show these HUD elements)
const BitVector<32>& hudElementsToHide(u16 theLayerID);

// Gets what hotspot arrays given layer specifically wants to enable
const BitVector<32>& hotspotArraysToEnable(u16 theLayerID);

// Gets what hotspot arrays given layer specifically wants to disable
// (overrides any lower layers wishing to enable these hotspots)
const BitVector<32>& hotspotArraysToDisable(u16 theLayerID);

// Returns a combo layer ID if one exists, otherwise 0
u16 comboLayerID(u16 theLayerID1, u16 theLayerID2);

// MENUS
Command commandForMenuItem(u16 theMenuID, u16 theMenuItemIdx);
Command commandForMenuDir(u16 theMenuID, ECommandDir theDir);
Command menuAutoCommand(u16 theMenuID);
Command menuBackCommand(u16 theMenuID);
EHUDType menuStyle(u16 theMenuID);
u16 rootMenuOfMenu(u16 theMenuID);
u16 menuHotspotArray(u16 theMenuID); // for eMenuStyle_Hotspots only
// Strings used for Profile getStr()/setStr() functions
std::string menuSectionName(u16 theMenuID);
std::string menuItemKeyName(u16 theMenuItemIdx);
std::string menuItemDirKeyName(ECommandDir theDir);
void menuItemStringToSubMenuName(std::string& theFullMenuItemString);

// HOTSPOTS
const Hotspot& getHotspot(u16 theHotspotID);
u16 firstHotspotInArray(u16 theHotspotArrayID);
u16 lastHotspotInArray(u16 theHotspotArrayID);
const Hotspot* keyBindArrayHotspot(u16 theArrayID, u16 theIndex);
void modifyHotspot(u16 theHotspotID, const Hotspot& theNewValues);
void reloadHotspotKey(const std::string& theHotspotName,
	StringToValueMap<u16>& theHotspotNameMapCache,
	StringToValueMap<u16>& theHotspotArrayNameMapCache);
void reloadAllHotspots();

// HUD ELEMENTS
EHUDType hudElementType(u16 theHUDElementID);
bool hudElementIsAMenu(u16 theHUDElementID);
// Not all HUD elements are menus, so may return invalid menu ID
u16 menuForHUDElement(u16 theHUDElementID);
u16 hudElementForMenu(u16 theMenuID);
// Only valid for the Hotspot HUD element type
u16 hotspotForHUDElement(u16 theHUDElementID);
// Only valid for HUD element types that are tied to Key Bind Arrays
u16 keyBindArrayForHUDElement(u16 theHUDElementID);
const std::string& hudElementKeyName(u16 theHUDElementID);

// SIZES
u16 keyBindArrayCount();
u16 keyBindArraySize(u16 theArrayID);
u16 controlsLayerCount();
u16 hudElementCount();
u16 menuCount();
u16 menuItemCount(u16 theMenuID);
u16 hotspotCount();
u16 hotspotArrayCount();

// LABELS
const std::string& layerLabel(u16 theLayerID);
const std::string& hotspotArrayLabel(u16 theHotspotArrayID);
const std::string& menuLabel(u16 theMenuID);
const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx);
const std::string& menuItemAltLabel(u16 theMenuID, u16 theMenuItemIdx);
const std::string& menuDirLabel(u16 theMenuID, ECommandDir theDir);
const std::string& hudElementLabel(u16 theHUDElementID);

} // InputMap
