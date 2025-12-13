//------------------------------------------------------------------------------
//	Originally written by Taron Millet, except where otherwise noted
//------------------------------------------------------------------------------

#pragma once

/*
	Loads and manages the configuration settings for mapping gamepad
	input to keyboard/mouse input, including menu configurations and actions.
	Also tracks some basic menu visual information relevant to control schemes
	such as button labels and which menu overlays need to be displayed when.
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
ESpecialKey keyBindIDToSpecialKey(int theKeyBindID); // or _None
u32 keyBindSignalID(int theKeyBindID);
u16 keyBindCycleIndexToKeyBindID(int theCycleID, int theIndex);

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
// If above returns eMouseMode_Menu, which menu ID?
int mouseModeMenu(int theLayerID);

// Gets what overlays (root menus) given layer specifically wants to show
const BitVector<32>& overlaysToShow(int theLayerID);

// Gets what menu overlays given layer specifically wants to hide
// (overrides any lower layers wishing to show these menus)
const BitVector<32>& overlaysToHide(int theLayerID);

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
EMenuStyle menuStyle(int theMenuID);
int rootMenuOfMenu(int theMenuID);
int menuOverlayID(int theMenuID);
int overlayRootMenuID(int theOverlayID);
int menuDefaultItemIdx(int theMenuID);
int menuItemHotspotID(int theMenuID, int theMenuItemIdx); // or 0
int menuKeyBindCycleID(int theMenuID); // for _KBCycle styles
int menuGridWidth(int theMenuID); // for _Grid style
int menuGridHeight(int theMenuID); // for _Grid style
int menuSectionID(int theMenuID);
// Strings used for Profile getStr()/setStr() functions
std::string menuSectionName(int theMenuID); // includes "Menu."
std::string menuKeyName(int theMenuID); // sans "Menu."
std::string menuItemKeyName(int theMenuItemIdx);
std::string menuItemDirKeyName(ECommandDir theDir);
int menuSectionNameToID(const std::string& theProfileSectionName);
void menuItemStringToSubMenuName(std::string& theFullMenuItemString);

// HOTSPOTS
const Hotspot& getHotspot(int theHotspotID);
int hotspotIDFromName(const std::string& theHotspotName); // or 0
int hotspotArrayIDFromName(const std::string& theHotspotArrayName); // or count
int firstHotspotInArray(int theHotspotArrayID);
int lastHotspotInArray(int theHotspotArrayID);
int sizeOfHotspotArray(int theHotspotArrayID);
bool hotspotArrayHasAnchor(int theHotspotArrayID);
float hotspotScale(int theHotspotID);
const Hotspot* KeyBindCycleHotspot(int theArrayID, int theIndex);
void modifyHotspot(int theHotspotID, const Hotspot& theNewValues);

// SIZES
int keyBindCount();
int keyBindCycleCount();
int keyBindCycleSize(int theCycleID);
int controlsLayerCount();
int menuCount();
int menuOverlayCount();
int menuItemCount(int theMenuID);
int hotspotCount();
int hotspotArrayCount();

// LABELS
const char* layerLabel(int theLayerID);
std::string hotspotLabel(int theHotspotID);
const char* hotspotArrayLabel(int theHotspotArrayID);
const char* menuLabel(int theMenuID);
const char* menuItemLabel(int theMenuID, int theMenuItemIdx);
const char* menuItemAltLabel(int theMenuID, int theMenuItemIdx);
const char* menuDirLabel(int theMenuID, ECommandDir theDir);
const char* keyBindLabel(int theKeyBindID);

} // InputMap
