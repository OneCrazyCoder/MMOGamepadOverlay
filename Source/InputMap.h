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

// KEYBINDS
u16 keyForSpecialAction(ESpecialKey theAction);
const Command& keyBindArrayCommand(u16 theArrayID, u16 theIndex);
// Adjust index by theOffset but skipping over any _Empty commands
// Offset of 0 may still adjust by +1 or more if array[theIndex] is _Empty
u16 offsetKeyBindArrayIndex(
	u16 theArrayID, u16 theIndex, s16 theOffset, bool wrap);

// CONTROLS LAYERS

// Get commands to execute for given button in given layer, in the
// form of an array of 'Command' of size 'eBtnAct_Num', or NULL
// if no commands have been assigned to given layer & button at all
const Command* commandsForButton(u16 theLayerID, EButton theButton);

// Gets parent layer for given layer ID
u16 parentLayer(u16 theLayerID);

// Checks what EMouseMode given controls layer requests be used
EMouseMode mouseMode(u16 theLayerID);

// Gets what HUD elements given layer specifically wants to show
const BitVector<>& hudElementsToShow(u16 theLayerID);

// Gets what HUD elements given layer specifically wants to hide
// (overrides any lower layers wishing to show these HUD elements)
const BitVector<>& hudElementsToHide(u16 theLayerID);

// Returns a combo layer ID if one exists, otherwise 0
u16 comboLayerID(u16 theLayerID1, u16 theLayerID2);

// MENUS
const Command& commandForMenuItem(u16 theMenuID, u16 theMenuItemIdx);
const Command& commandForMenuDir(u16 theMenuID, ECommandDir theDir);
const Command& menuAutoCommand(u16 theMenuID);
EHUDType menuStyle(u16 theMenuID);
u16 rootMenuOfMenu(u16 theMenuID);
// Full Profile (.ini file) key for specified menu item's command
std::string menuItemKey(u16 theMenuID, u16 theMenuItemIdx);
std::string menuItemDirKey(u16 theMenuID, ECommandDir theDir);

// HOTSPOTS
const Hotspot& getHotspot(u16 theHotspotID);
const Hotspot* keyBindArrayHotspot(u16 theArrayID, u16 theIndex);
void modifyHotspot(u16 theHotspotID, const Hotspot& theNewValues);
// This function also removes the hotspot from start of string
// in case multiple hotspots are specified by the same string
EResult profileStringToHotspot(std::string& theString, Hotspot& out);

// HUD ELEMENTS
EHUDType hudElementType(u16 theHUDElementID);
bool hudElementIsAMenu(u16 theHUDElementID);
// Not all HUD elements are menus, so may return invalid menu ID
u16 menuForHUDElement(u16 theHUDElementID);
u16 hudElementForMenu(u16 theMenuID);
// Only valid for HUD element types that are tied to Key Bind Arrays
u16 keyBindArrayForHUDElement(u16 theHUDElementID);

// SIZES
u16 keyBindArrayCount();
u16 keyBindArraySize(u16 theArrayID);
u16 controlsLayerCount();
u16 hudElementCount();
u16 menuCount();
u16 menuItemCount(u16 theMenuID);
u16 hotspotCount();

// LABELS
const std::string& layerLabel(u16 theLayerID);
const std::string& menuLabel(u16 theMenuID);
const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx);
const std::string& menuItemAltLabel(u16 theMenuID, u16 theMenuItemIdx);
const std::string& menuDirLabel(u16 theMenuID, ECommandDir theDir);
const std::string& hudElementKeyName(u16 theHUDElementID);
const std::string& hudElementDisplayName(u16 theHUDElementID);

} // InputMap
