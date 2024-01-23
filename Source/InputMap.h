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

// Get certain special-case keybinds directly
u16 keyForSpecialAction(ESpecialKey theAction);


// CONTROLS LAYERS

// Get commands to execute for given button in given layer, in the
// form of an array of 'Command' of size 'eBtnAct_Num', or NULL
// if no commands have been assigned to given layer & button at all
const Command* commandsForButton(u16 theLayerID, EButton theButton);

// Checks if given controls layer should have mouse look turned on
bool mouseLookShouldBeOn(u16 theLayerID);

// Gets what HUD elements given layer specifically wants to show
const BitVector<>& hudElementsToShow(u16 theLayerID);

// Gets what HUD elements given layer specifically wants to hide
// (overrides any lower layers wishing to show these HUD elements)
const BitVector<>& hudElementsToHide(u16 theLayerID);

// MENUS
const Command& commandForMenuItem(u16 theMenuID, u16 theMenuItemIdx);
EMenuStyle menuStyle(u16 theMenuID);
u16 rootMenuOfMenu(u16 theMenuID);

// HOTSPOTS
const Hotspot& getHotspot(u16 theHotspotID);

// SIZES
u16 controlsLayerCount();
u16 hudElementCount();
u16 menuCount();
u16 menuItemCount(u16 theMenuID);
u16 hotspotCount(); 
u8 targetGroupSize();

// LABELS
const std::string& layerLabel(u16 theLayerID); 
const std::string& menuLabel(u16 theMenuID);
const std::string& menuItemLabel(u16 theMenuID, u16 theMenuItemIdx);

} // InputMap
