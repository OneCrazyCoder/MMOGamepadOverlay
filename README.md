# MMO Gamepad Overlay

Download links for latest built version:

* [Windows 32-bit](https://bitbucket.org/TaronM/mmogamepadoverlay/downloads/MMOGamepadOverlay-x86.zip)
* [Windows 64-bit](https://bitbucket.org/TaronM/mmogamepadoverlay/downloads/MMOGamepadOverlay-a64.zip)

This application translates game controller input (via DirectInput and/or XInput) into keyboard and mouse input (via the SendInput() Win32 API function), possibly with HUD elements (drawn as a transparent overlay windows over top of a game's window) to help visualize what various buttons may do (particularly with the use of customizable on-screen menus).

Although there are plenty of other applications that can fit this basic description, including Steam, this particular application was specifically designed for playing the *EverQuest* emulation servers *Project 1999* and *Project Quarm*, and eventually the upcoming *Monsters & Memories*, with a control scheme inspired by the only MMORPG ever exclusively designed for playing with a controller - *EQOA* for the PlayStation 2. It thus has specific features related to these games that are difficult to reproduce with other, more generic options for translating gamepad input for games without native gamepad support.

Nothing says it can't also be used for other games though, as it is customizable.

## Basic operation

Place the executable wherever is convenient, keeping in mind that it will generate and read text files with the *.ini* extension in the same folder in which it is placed. When run for the first time, you will be prompted to create a **Profile**, which is associated with one or more *.ini* files that customize how the application looks and behaves. Multiple example Profiles are provided to pick from as a base template. You can just choose to have a single Profile and auto-load it every time, or have different ones for different games or even different characters for the same game. You will then be prompted if you want your Profile to also automatically launch an associated game (if it is the first Profile loaded when launching the app), for convenience.

After that, load up the game and you should be able to use the controller to move your character, move the mouse, and perform actions. How that all functions depends on the game and the settings in your loaded Profile.

## Gamepad conflict for Windows 10+

Some games (notably *Monsters & Memories* pre-alpha tests as of this writing) may actually respond to Gamepad input already in Windows 10+, but not in any useful way. In fact, Windows itself can respond to Gamepad input such as in the start menu by using this new UWP "feature". This can be problematic because this application can not stop other applications or Windows from detecting Gamepad input, causing buttons you press to result in extra actions you did not intend (in M&M's case, pressing "A" on an XBox controller can cause it to click on the last button you clicked with the mouse, for example, in addition to whatever you actually assign "A" to do here).

One way to stop this is using a utility called HidHide (which, if you are using a PlayStation controller and something like DS4Windows, you may already have installed anyway). Search your computer for "HidHide Configuration" and run that app if you have this installed. There, you can set it to "hide" your gamepads from the game in question, so they will ONLY respond to the mouse and keyboard input sent by this application (and your actual mouse and keyboard). HidHide can't stop Windows itself from responding to Gamepad input though.

Another option is to disable this feature altogether, if your Windows is updated enough to allow that, through a Registry edit. [Here](https://github.com/microsoft/microsoft-ui-xaml/issues/1495#issuecomment-745586048) is a description of how to do it. In case that link dies at some point, the brief version is to make a Registry key ``
HKLM\Software\Microsoft\Input\Settings\ControllerProcessor\ControllerToVKMapping`` and add a DWORD to it called ``Enabled`` and set its value to 0. This will only disable Windows and some newer "UWP" apps from using the Gamepad for basic functionality - it will not prevent games that natively support Gamepads for full gameplay, or utilities like this application or Steam from remapping a Gamepad to keyboard & mouse input.

## Profile Setup

**NOTE: The rest of this file explains how to customize your control scheme by editing .ini files using a text editor. This may seem daunting, but keep in mind that you don't actually have to learn all this stuff if the default Profiles provided (or maybe shared by other users) work for your needs! You will likely at least need to adjust the [Icons] and [Hotspots] sections to match your preferred UI layout, however, so skip to the sections "Position and size properties" and "Icons copied from game window" to just learn about those.**

The application generates a *MMOGO_Core.ini* file which contains some default settings and is used to track what other profiles you have created and their names. You can edit this file and any other *.ini* files it generates with any text editor, or create your own, once you know how they work.

The list of profiles is at the top of *MMOGO_Core.ini*, along with an entry for specifying which one to load automatically, if any. Each Profile *.ini* file can specify a "parent" Profile with a line like:

    ParentProfile = MyBaseProfile
This system is intended to allow for having a "base" profile for a particular game, and then multiple profiles for different characters that use that same base as their parent. You can set up as long of a chain of parent profiles as you desire. It is not necessary to specify the "Core" profile as a ParentProfile at any point, as it will always be loaded first anyway. Profiles are loaded in order from parent to child, and any duplicate properties are overwritten as they are encountered. That means the specific Profile you are loading will take priority over its parent base, which itself will take priority over any parent it has, and all other files will take priority over "Core".

All of this is set up automatically with the default example profiles generated on first launch.

You can edit MMOGO_Core.ini yourself to add more profiles, or use the menu option File->Profile from within the application to do so with a GUI.

## Profile customization

Each Profile is a *.ini* file (and possibly one or more parent *.ini* file(s) as explained above). These are plain-text files you can edit in Notepad/etc that contain a list of *properties*. Each property is identified by a *property category* and a *property name* with an associated *property value*.

If the same *property category*+*property name* is encountered more than once, the most recent one will override any previous ones (which allows for having "default values" specified in Core or a parent Profile that are then overwritten by a specific child Profile). However, the same *property name* can be used in more than one *property category* and will be considered different properties. There is also a special unnamed "root" category at the top of each file before the first category label is encountered, which is where the ParentProfile property mentioned earlier is placed.

The .ini files are formatted as follows:

    Root Property Name = Property Value

    [SectionName1]
    Property Name 1 = Property Value 1
    Property Name 2 = Property Value 2
    # Comment type 1

    [SectionName1.SubSectionA]
    Property Name 1 = Property Value 1
    Property Name 2 = Property Value 2

    [SectionName2]
    ; Comment type 2
    ;Property Name 1 = Property Value 1 - commented out
    Property Name 2 = Property Value 2

*NOTE: Comments are only supported by placing # and ; at the beginning of a line, you can NOT add comments at the end of a line, it will instead be considered part of the Property Value.*

## [Scheme] Category

This is the main category for determining how Gamepad input is translated into keyboard and mouse input. With a couple of special exceptions, each *property name* in this category represents a gamepad button (and optionally an action associated with that button like *press*, *tap*, *release*, etc), and each *property value* represents a **Command** for the application to execute when that button is used.

Commands are usually input to send to the game, such as keyboard keys, mouse buttons, and mouse movement. For example, to assign R2 to act as the right mouse button, you could include:

    [Scheme]
    R2 = RMB
There are various ways supported of specifying gamepad buttons and keys, so you could instead use:

    [Scheme]
    RT = Right-click

If you want the full list, check *Source\GlobalConstants.cpp* in the source code.

### Multi-button assignment

Note also that you can sometimes assign 4 buttons at once in the case of the D-pad, analog sticks, and face buttons. For example:

    [Scheme]
    DPad = MoveTurn
    LStick = MoveStrafe
    RStick = Mouse
    # Below treats face buttons like a D-pad
    FPad = Move

Each of these (including the analog sticks) are otherwise treated as 4 separate buttons like "LStickUp" or "DPadDown", etc. when want to assign each direction to a separate function.

### Button actions

When the *property name* is simply the button name by itself like in the above examples, it is treated as the action "press and hold". So in the earlier ``R2 = RMB`` example, the right mouse button will be pressed when R2 is, held for an long as R2 is held, and released when R2 is released.

Other "button actions" can be specified instead, and each button can have multiple Commands assigned to it at once such as for a "tap" vs a "hold." For example:

    [Scheme]
    R2 = A
    Press R2 = B
    Tap R2 = C
    Release R2 = D
    Hold R2 = E
    Long Hold R2 = F

This example demonstrates the maximum number of Commands that could be assigned to a single button. When R2 is first pressed, 'A' and 'B' keyboard keys would be sent to the game ('A' would be held down but 'B' would just be tapped and immediately released). If R2 was quickly released, a single tap of the 'C' key would be sent. If R2 was held for a short time, an 'E' tap would be sent once. If R2 was still held for a while after that, a single 'F' tap would be sent as well. No matter how long it is held, even if just briefly tapped, once let go of R2 a single tap of 'D' would be sent to the game, as well as finally releasing 'A'.

Notice how only the base ``R2=`` property can actually hold a key down for more than split second. All other button actions can only "tap" a key (press and then immediately release it). Certain Commands can't be "held" anyway, so assigning one of these to just ``R2=`` will make it act the same as assigning it to ``Press R2=`` (meaning can essentially have two ``Press R2=`` commands on the same button in these cases).

## Commands

As mentioned above, Commands assigned to buttons can be as simple as the name of a keyboard key or mouse button, as well as mouse movement (such as ``=Mouse Up`` or ``=Mouse Left``).

Not mentioned yet is mouse wheel movement, which can be set with Commands such as ``=MouseWheel Up Smooth`` or ``=MouseWheel Down Stepped`` or ``MouseWheel Down Once``. "Smooth" vs "Stepped" affects whether or not movement of less than one 'notch' at a time is sent to the application, and "Once" can be used if only want the wheel to only move one 'notch' even when the button assigned to the command is held continuously.

### Combination keys

A command can also be a keyboard key or mouse button combined with a *modifier* key - Shift, Ctrl, or Alt - such as:

    R2 = Shift+A
    L2 = Ctrl X
    L1 = Ctrl-Alt-R
These can still be "held" as if they are single keys.

*WARNING: Modifier keys should be used sparingly, as they can interfere with or delay other keys. For example, if you are holding Shift+A and then want to press just 'X', since the Shift key is still being held down, the game would normally interpret it as you pressing 'Shift+X', which may be totally different command. This application specifically avoids this by briefly releasing Shift before pressing X and then re-pressing Shift again as needed, but this can make the controls seem less responsive due to the delays needed to make sure each release and re-press are processed in the correct order. Consider re-mapping controls for the game to use Shift/Ctrl/Alt as little as possible for best results!*

### Key Sequence

You can also specify a sequence of keys to be pressed. For example, you could have a single button press the sequence Shift+2 (to switch to hotbar #2), then 1 (to use hotbutton #1), then Shift+1 (to switch back to hotbar #1), like so:

    R2 = Shift+2, 1, Shift+1

Key sequences can NOT be "held", so holding R2 vs just tapping it will give the same result in the above example.

You can also add delays (specified in milliseconds) into the sequence if needed, such as this sequence to automatically "consider" a target when changing targets:

    # 'Delay' or 'Wait' also work
    R1 = F8, pause 100, C
    
*WARNING: Do not use this to fully automate complex tasks, or you're likely to get banned from whichever game you are using this with.*

#### Mouse jump in key sequence

On a more advanced note, you can also request in the sequence to jump the mouse cursor to a named **Hotspot** location (defined in [Hotspots]) to click on it, such as:

    [Hotspots]
    CenterScreen = 50%, 50%

    [Scheme]
    R1 = Point to CenterScreen, LClick
    R2 = Release RMB->LClick at CenterScreen->RClick
    
### Chat box macros

While a Key Sequence could technically be used to type a message directly into the game's chat box, it is easier to directly use a *Slash Command* or *Say String* Command to do this.

Slash Commands start with ``/`` and Say Strings (chat messages) start with ``>`` (the '>' is replaced with the Return key to switch to the chat box when the Command is actually executed). These Commands will actually "type" the sequence into the chat box as a series of keyboard key presses, followed by pressing Return to send the macro. For example:

    [Scheme]
    Hold R1 = /who
    Hold R2 = /g Roll for loot please!
    Hold L1 = >Would you like to group?

This will lock out most other inputs while typing though, so in general it is better to instead create macros using the in-game interface and activate them via key sequences that activate in-game "hotbuttons", like ``= Shift+2, 4`` instead.

## Key Binds (aliases)

Key Binds are basically just aliases or shortcuts for sent input. Using Key Binds, instead of saying:

    [Scheme]
    XB_A = Space
    R2 = Release RMB->LClick at CenterScreen->RClick

You would instead say:

    [KeyBinds]
    Jump = Space
    UseCenterScreen = Release RMB->LClick at CenterScreen->RClick

    [Scheme]
    XB_A = Jump
    R2 = UseCenterScreen

In this example it may not seem worth the effort, but it can be convenient when using the same input in multiple places, or for just making your [Scheme] easier to read.

There are also some KeyBinds that are specifically checked for and used by the application for certain commands - namely for character movement.

### Special Key Binds

A few Key Bind names are specifically checked for by the program and used directly as more than just aliases. These include:

    SwapWindowMode =
    MoveForward =
    MoveBack =
    TurnLeft =
    TurnRight =
    StrafeLeft =
    StrafeRight =

``SwapWindowMode=`` is used in the code for attempting to force the target game into full-screen-windowed mode (as opposed to *true* full screen mode which would prevent the overlay from being visible), which can be set in the [System] category with the flag ``ForceFullScreenWindow = Yes`` (and optionally ``StartInFullScreenWindow = Yes``). It is typically set to ``=Alt+Enter``.

The Move/Turn/Strafe commands are used when assign buttons to ``=Move`` (same as ``=MoveTurn``) or ``=MoveStrafe`` (or their directional versions if not using multi-assign, like ``=Move Left``, etc). While you could just manually assign the movement keys or use different Key Binds, using these utilizes special extra code that helps improve movement responsiveness, particularly when using analog sticks. If the Strafe versions aren't set, the Turn versions will be used instead automatically (which for many games will automatically convert to being a Strafe motion while in Mouse Look mode).

### Key Bind Arrays

If multiple Key Binds have the same name except for a number on the end of the name, they will be logged as a **Key Bind Array**. For example:

    [KeyBinds]
    TargetGroup1 = F1
    TargetGroup2 = F2
    TargetGroup3 = F3
    TargetGroup4 = F4
    TargetGroup5 = F5
    TargetGroup6 = F6

Will make a Key Bind Array called "TargetGroup" with 6 elements. Each can be used as a normal alias by directly referencing "TargetGroup2", for example, but the last one used and a default (the first one initially) will be remembered. Certain commands can then request re-using the last one used, using the default in the array, and using the next/previous ones in the array. This could allow, for example, to assign a button that will cycle through them each time it is pressed by assigning:

    [Scheme]
    L1 = TargetGroup Next Wrap

Other commands that can use Key Bind Arrays include *Previous, Last, Default, Set Default* (sets Default to Last), and *Reset* (sets Last to Default). *Previous* and *Next* can be set to *Wrap* or *NoWrap*.

When using this for the above example of relative group targeting, a visual indicator may be helpful to know what will happen the next time the button is pressed. There are special **HUD Elements** covered later to help with this, ``Type=KeyBindArrayLast`` and ``Type=KeyBindArrayDefault``.

## Position and size properties

**Hotspots** are positions on the screen of significance, such as where a mouse click should occur in a Key Sequence as mentioned before. Hotspots and HUD Element positions (and sizes) are specified as X and Y coordinates with Y=0 being the top of the window/screen/bitmap and X=0 being the left side.

Each axis can possibly have a relative and/or an absolute value. The relative value is related to the size of the target game's window/screen size, and the absolute value is in pixels. They can be specified in the format ``relativeValue% +/- absoluteValue`` if specify both. Some relative values can be also be specified by shortcuts like L/T/R/B/C/CX/CY/W/H instead of numbers.

Some accepted examples of valid positions for reference:

    # Center of the screen/window
    = 50% x 50%
    = 0.5, 0.5
    = CX CY
    # 10 pixels to the left of right edge, and
    # 5 pixels down from 30.5% of the game window's height
    = R - 10, 30.5% + 5
    # Pixel position 200 x 100 regardless of target window size
    = 200 x 100

In some cases multiple coordinates can be used in a single property, such as when specifying both the position and size of the rectangle to copy from a bitmap image or multiple Hotspots in a single line. These just follow the same pattern but continue to alternate between X and Y coordinates. For example:

    Bitmap = MyImage: 50%, 0, 25%, H

means 50% of the image size for the left edge of the source rectangle to copy, the top of the image for the top edge, 25% of the image width for the width, and the entire height of the image for the height.

## Controls Layers

To really unlock the full range of actions in an MMO using a Gamepad, you will almost certainly need to employ button combinations like L2+X to execute different Commands. This and more can be accomplished through the use of **Controls Layers**. These Layers change what Commands are assigned to what buttons while the Layer is active.

You can have multiple Layers added at once. Layers can be thought of as stacked on top of each other, and for any given button, the top-most Layer's assignments will take priority. If the top-most Layer has nothing assigned to a button, the next Layer below it will be checked for an assignment for that button, and so on. [Scheme] is always the bottom-most layer, and newly-added layers are added to the top of the stack (with some exceptions).

Layers can be added with the ``=Add Layer <LayerName>`` command and removed with the ``=Remove Layer`` command (or both added and removed with the same command with a single ``=Toggle Layer <LayerName>`` command). Alternatively, they can be "held" by just using the ``=Layer <LayerName>`` command assigned to a button, which means that the Layer will be added when the button is first pressed, and then automatically removed when the button is released. For example:

    [Scheme]
    # Add "Alt" layer as long as L2 is held
    L2 = Layer Alt
    # Add "MouseLook" layer until another Command elsewhere removes it
    R3 = Add Layer MouseLook

Layers are defined the same as [Scheme], with just the category name [Layer.LayerName] instead. So for the above example, you could add:

    [Layer.Alt]
    # L2+Triangle = Jump
    Triangle = Jump

    [Layer.MouseLook]
    Mouse = Look
    # If don't specify Layer name, removes self
    R3 = Remove Layer
    
With the above setup, L2+Triangle will cause the character to jump (via a ``Jump=`` Key Bind), and R3 will act as a toggle button turning MouseLook mode on and off (alternatively could have just assigned ``R3 = Toggle Layer MouseLook`` in [Scheme] instead, but in complex control schemes Toggle may not always be ideal).

*There can only be one of each named Layer active at once, so trying to add a Layer with the same name again will simply move the old one to the top of the stack instead.*

*Assigning something to **any** action for a button blocks **all** commands assigned to that button from lower layers. For example, a command assigned to ``Press L2=`` on a lower Layer will never execute if a higher Layer has ``Tap L2=`` assigned to something even though those are different actions*.

*If you want a button to literally do nothing, including blocking lower layers' assignments for that button, set that specific button to do nothing (``L2 = Do Nothing``). Leaving the button assignment blank (just ``L2=``) has that button defer to lower layers' assignments instead.*

### The "Auto" Button

Each Layer also has a special 'virtual button' unique to it, that can be assigned commands like any real Gamepad button. This button is called "Auto". It is "pressed" whenever the Layer is added, and then "released" whenever the Layer is removed. This can be particularly useful to assign a button to simultaneously 'hold' a Layer while also holding a key, by having the Layer hold the key instead using its Auto button.

For example, let's say you wanted to make pressing and holding Circle on a PS controller act the same as holding the left mouse button, but you also want to make it so while holding Circle, you could use your left thumb on the D-pad to move the cursor around to "drag" the mouse, even though normally the D-pad is used for character movement. You could accomplish this as follows:

    [Scheme]
    D-Pad = Move
    Circle = Layer MouseDrag

    [Layer.MouseDrag]
    Auto = LMB
    D-Pad = Mouse

With this setup, pressing Circle will add the MouseDrag Layer, which will click and hold the left mouse button for as long as the Layer is active via Auto, while also changing the D-Pad to control the mouse. Releasing Circle will remove the Layer, restoring the D-Pad to character movement instead and releasing the left mouse button (since Auto is "released" when the Layer is removed).

You can even assign commands to ``Release Auto=`` and ``Tap Auto =`` and so on, like any real button.

### Layer Includes

To save on copying and pasting a lot when editing the .ini file, each Layer can also "include" the contents of another Layer, if they are mostly the same anyway. For example, this:

    [Layer.MyLayer]
    L1 = Target NPC
    R1 = Target PC
    PS_X = Attack
    R2 = RMB
    ...

    [Layer.MyLayerButR1Jumps]
    Include = MyLayer
    R1 = Jump

Would be the same as typing out this:

    [Layer.MyLayer]
    L1 = Target NPC
    R1 = Target PC
    PS_X = Attack
    R2 = RMB
    ...

    [Layer.MyLayerButR1Jumps]
    L1 = Target NPC
    R1 = Jump
    PS_X = Attack
    R2 = RMB
    ...

*If you want to include a layer but change one of the buttons from the included layer to be unassigned (thus allowing whatever is assigned to lower layers to be used for that button instead), set that specific button to be unassigned (``R1 = Unassigned``). Leaving the button assignment blank (just ``R1=``) defers to whatever the included layer had set for it.*

### Other Layer Properties

In addition to changing button assignments temporarily while they are active, each Layer (and the [Scheme] category) has a few other properties.

This includes changing how the mouse is treated by using ``Mouse=Cursor`` (normal), ``Mouse=Look`` (holding the right-mouse button down to keep MouseLook mode active), or ``Mouse=Hide`` ("hide" the cursor by jumping it to the corner of the screen). The top-most Layer with a ``Mouse=`` property specified dictates the mouse mode used, with the special exception of ``Mouse=HideOrLook`` which changes what it does based on the layers beneath it.

Each Layer also specifies which **HUD Elements** (including **Menus**) should be visible while that Layer in active. Layers can also specifically *hide* HUD Elements that were requested to be shown by lower Layers, stopping them from being shown (unless yet another, higher layer overrides the *hide*). This is done via the ``HUD=`` property including a list of HUD Element names to show (and optionally the 'Show' and 'Hide' key words), such as:

    [Layer.MainMenu]
    HUD = MainMenu
    
    [Layer.MouseLook]
    Mouse = Look
    HUD = Show Reticle
    
    [Layer.TopMost]
    HUD = Hide MainMenu, Show GroupTargetLast

### Advanced Layer control (parenting)

When a Command adds a new Layer, that new Layer treats the Layer that contained the Command as its "Parent Layer". If this Parent Layer is later removed, the new "child" Layer will be automatically removed along with it (with some exceptions). This allows removing an entire hierarchy of Layers all at once by just removing the base Layer that spawned them.

In some cases this is not desired, so you can override the new Layer's parent by including extra specifiers to commands like ``=Add Layer``, such as ``Add Layer <LayerName> to Parent`` (causes the new layer to treat the current's parent as its own), ``Add Layer <LayerName> to Grandparent`` or ``Add Layer <LayerName> to Parent +1`` (both do the same thing), or even ``Add Independent Layer <LayerName>`` to prevent it being removed automatically at all (uses [Scheme] as its parent). You can also use these with ``=Remove Layer``, such as a layer removing both itself and its parent with ``=Remove parent layer``.

Note that "held" Layers (layer active only so long as are holding a button such as [Layer.Alt] from the ``L2 = Layer Alt`` example above) are NOT automatically removed when their parent is removed. They are only removed when the button "holding" it active (L2 in this case) is released!

There is also the more advanced Layer command Replace, such as ``Replace Layer with <LayerName>`` or ``Replace Parent/GrandParent/etc Layer with <LayerName>`` or even ``Replace All Layers with <LayerName>`` which can be used to simultaneously Remove and Add in a single Command. *Note that Replace All will **not** remove any "held" Layers!*

In terms of actual Layer order in the stack (which affects the final button assignments, visible HUD, etc), parenting *mostly* doesn't matter, only the order the Layers were added in and what type of Layer it is.

There are 4 types of Layers when it comes to ordering: the root [Scheme], a "normal" Layer (added with Add/Replace/Toggle), a "combo" Layer (explained later), and a "held" Layer (held active by a button). The basic order is [Scheme] at the bottom, followed by normal layers above it in the order added, and then held layers at the top, again in order added. Combo layers are always placed immediately above the top-most of their base layers.

There is also a special exception to the above ordering for normal layers added as children to a held layer. These will be sorted directly on top of their parent held layer, yet beneath any other held layers above that.

### Combo layers

These special layers can not be manually added, but are instead automatically added and removed whenever a combination of other layers is active. They can be used for more complex button combinations. For example, let's say you want Circle to send a different key for just pressing Circle, L2+Circle, R2+Circle, or L2+R2+Circle. That last one can be done with a combo layer, such as:

    [Scheme]
    Circle=A
    L2 = Hold L2 layer
    R2 = Hold R2 layer

    [Layer.L2]
    Circle=B

    [Layer.R2]
    Circle=C

    [Layer.L2+R2]
    Circle=D

With this setup, when hold both L2 and R2, causing both those layers to be active, the L2+R2 layer is automatically added, causing Circle to press "D". The L2+R2 layer will be removed as soon as let go of either L2 or R2.

Here's some other technical details about combo layers:
* They are specified by 2 or more layer names separated by '+' after the ``[Layer.`` prefix.
* They are added as soon as all their base layers are active, and removed as soon as any of their base layers are removed.
* They can not be directly referenced by name for commands like Add Layer because symbols like '+' are filtered out of command strings.
* In terms of layer order, they stay immediately on top of the top-most of all of their base layers, but no higher.
* In terms of parenting for commands that can specify "parent" or "grandparent" layer, etc, they treat the first of their listed base layers to be their parent (so ``[Layer.L2+R2]`` treats ``[Layer.L2]`` as its parent).

## Menus

While it is possible to use Layers alone to send all the input needed to an MMO, it would require a lot of complex button combinations and sequences you'd need to memorize. **Menus** can make things a lot easier, by instead assigning buttons to add/remove/control Menus and then having the Menus including a large number of Commands.

Each Menu also counts as a **HUD Element**, so the Menu must be made visible by the ``HUD=`` property for an active Layer to actually see it.

Each Menu has a ``Style=`` property that determines its basic structure and appearance, as well as visual properties like colors, shapes, etc of the menu items. Example Menu Styles include List, Slots, Bar, 4Dir, and Grid (which can include a ``GridWidth=`` property to specify the grid shape).

Menus are defined using the category name [Menu.MenuName]. Each Menu Item is defined by a *property name* of just the Menu Item number (with some exceptions covered later). The *property value* for each Menu Item contains a name/label to be displayed followed by colon ``:`` followed by a Command to execute when that Menu Item is chosen. Here's an example of a basic menu:

    [Menu.MainMenu]
    Style = List
    Position = L+10, 25%
    Alignment = L, T
    1=Inventory: Toggle Inventory layer
    2=Book: Toggle Book layer
    3=TBD:
    4=Settings

Notice how Menu Item #3 has no Command, but still contains ``:``, so the label will be shown but nothing will happen if it is used. Menu Item #6 specifies a Sub-Menu, indicated by the absence of ``:``. You could also just have ``:`` followed by a Command if you want no label for the Menu Item.

### Sub-Menus

A sub-menu is created by having a Menu Item *property value* without any ``:`` character, which then has the label double as the sub-menu's name. The sub-menu is defined by the category name ``[Menu.MenuName.SubMenuName]``.  In the earlier example, ``4=Settings`` specified a sub-menu. Here is an example setup for that sub-menu:

    [Menu.MainMenu.Settings]
    1=Profile: Change Profile
    2=Close Overlay

    [Menu.MainMenu.Settings.Close Overlay]
    1=Cancel Quit: ..
    2=Confirm Quit: Quit App

Sub-menus should only specify Menu Items - things like ``Style=`` as well as ``Position=`` and other visible HUD properties will be ignored for all but the "root" menu ([Menu.MainMenu] in this example). Note the ``..`` Command, which, when selected, just backs out of the sub-menu.

### Controlling menus

To actually use a Menu, you will need to assign Menu-controlling commands to Gamepad buttons in ``[Scheme]`` or a ``[Layer.LayerName]`` category.  These commands must specify the name of the Menu they are referring to. Below is an example of controlling the MainMenu example from earlier, including showing/hiding it with the Start button.

    [Scheme]
    Start = Toggle Layer MainMenu

    [Layer.MainMenu]
    HUD = MainMenu
    # Exits all sub-menus and selects Menu Item #1
    Auto = Reset MainMenu
    DPad = Select MainMenu Wrap
    PS_X = Confirm MainMenu
    Circle = Back MainMenu

If you want to make it so pressing Circle when at the root Main Menu also closes the menu entirely (like pressing Start would), you can use this instead:

    Circle = Back or Close MainMenu
*Note that root menus are never actually "closed", just hidden by the HUD= property, so what actually happens with this Command when you press Circle with no sub-menus, is it temporarily becomes the Command "Remove Layer" instead, with the assumption being that removing the layer containing the ``=Back or Close`` Command will result in hiding the Menu and removing button assignments related to it*

### Menu Auto command

Similar to the Auto button for each Controls Layer, you can add an ``Auto=`` property to a Sub-Menu which can be set to a direct input Command to be used whenever that sub-menu is opened (including when it is returned to when backing out of another sub-menu).

*Note that since "root" Menus aren't really ever "opened" or "closed" (just hidden or shown), the ``Auto=`` Command on a root menu will only be run via the ``=Reset`` command or after opening a sub-menu and then backing out to the root Menu again.*

### Menu directional commands

In addition to the numbered menu items and Auto, each Menu can have 4 directional Menu Items specified, labeled as ``L=, R=, U=``, and ``D=`` and tied to using ``=Select <MenuName> Left, Right, Up,`` and ``Down`` respectively. These special Menu Items have their Commands run directly via the ``=Select`` Command rather than the ``=Confirm`` Command, but *only when pressing that direction when there is no numbered Menu Item in that direction any more!*

For example, in a basic List-style menu, normally ``=Select Left`` and ``=Select Right`` wouldn't do anything since all of the Menu Items are in a single vertical list, but if a ``L=`` and/or ``R=`` Menu Item is included, then ``=Select Left`` and/or ``=Select Right``will immediately execute the ``L/R=`` Menu Item Commands.

Even in a List menu, the ``U=`` and ``D=`` Menu Items can also still be used, but only if use Up while the first Menu Item is currently selected, or Down when the last item is currently selected. Similar logic applies to other Menu Styles but may be slightly different for each one.

One key use of these is allowing for "side menus", particularly for the "Slots" Menu Style which is designed to emulate EQOA's ability list (basically a List-style menu but the current selection is always listed first and the entire menu "rotates" as you select Up or Down, like a slot machine). Using "side menus" via ``L=`` and ``R=`` allows swapping to a different column of Menu Items, just like swapping between the Ability List and the Tool Belt in EQOA. Each column of items is technically a sub-menu. Here is an example of how to set something like that up:

    [Menu.Abilities]
    Style = Slots
    L=Hotbar
    R=Spells
    1=Abil1: Ability1
    2=Abil2: Ability2
    ...

    [Menu.Abilities.Spells]
    L=..
    R=.Hotbar
    1=Abil6: Ability6
    2=Abil7: Ability7
    ...

    [Menu.Abilities.Hotbar]
    L=.Spells
    R=..
    1=HB1: Hotbar1
    2=HB2: Hotbar2
    ...

Notice how the sub-menus use ``..`` to specify returning to the the base "Abilities" menu. There is also a single ``.`` in front of another sub-menu's name to specify it is actually a "sibling" menu. For example, if ``[Menu.Abilities.Spells]`` had the property ``R=Hotbar``, then that would reference a "child" sub-menu ``[Menu.Abilities.Spells.Hotbar]`` which doesn't exist. Using ``R=.Hotbar`` instead indicates it wants to open its "sibling" sub-menu ``[Menu.Abilities.Hotbar]``.

### 4Dir Menu Style

This special Menu style is designed after the "Quick Chat" menu in EQOA, which allows for quickly selecting a Menu Item through a series of direction presses without needing to ever use a Confirm button. For this Menu, no numbered Menu Items are specified, only the directional Menu Items are used. So for macros in the style of EQOA, you could define a Menu like this:

    [Menu.Macros]
    Style = 4Dir
    Position = 50%, 10
    U = Responses
    L = Options
    R = Group
    D = Communicate
    
    [Menu.Macros.Group]
    U = Attacking
    L = Creation
    R = Readiness
    D = Important!
    
    [Menu.Macros.Group.Creation]
    U = Invite: /invite
    L = Organization
    R = Need Group: /ooc Looking for group!
    D = Hunting

    [Menu.Macros.Group.Creation.Organization]
    U = Request Roll: /g Roll for loot please!
    L = Loot up!: /g Loot up if you want this.
    R = Want Group?: >Would you like to group?
    D = Roll 100: /rand
    ...

When defining buttons to control such a menu, no ``Confirm=`` is needed. If you want the menu to automatically close once a Command (that doesn't open a sub-menu) is selected like in EQOA, assign a button to ``=Select and Close <MenuName``. 

### Edit Menus at runtime

It can be helpful to allow changing Menu contents while playing the game, such as for quickly creating Macros in a 4Dir style menu. You can do this by assigning the Command ``=Edit <MenuName>`` to a button action, which will edit whichever Menu Item is currently selected, or ``=Edit <MenuName> Up/Down/Left/Right`` for editing directional Menu Items. For example, to work like the Quick Chat menu in EQOA from the above example, where holding the D-Pad for a while allows editing the macros, you could use:

    [Layer.Macros]
    HUD = Macros
    Auto = Reset Macros
    Tap DPad = Select and Close Macros
    LongHold DPad = Edit Macros
    L2 = Remove Layer
    
When the ``=Edit`` command is executed, a dialog box pops up that allows changing the Label or Command, adding new Menu Items or sub-menus or deleting or replacing them, with instructions included in the dialog.

## HUD Elements (Menu graphics)

As mentioned above, Menus are also **HUD Elements**. However, there are also HUD Element types that are *not* Menus. These are created with the category name ``[HUD.HUDElementName]``. You can use this to create a reticle in the middle of the screen while in MouseLook mode (to aim better with a "Use CenterScreen" key, for example), as well as special HUD Elements like Key Bind Array indicators.

Default properties used by all HUD Elements and Menus can be defined in the base ``[HUD]`` category if don't want to specify them for every individual HUD Element. The exceptions being the ``Position=`` property, which should always be specified, and the optional ``Priority=`` property, which determines draw order (higher priority are drawn on top of lower priority, allowed range is -100 to 100 and default is 0).

Like ``Style =`` for a Menu, each HUD element must specify a ``Type =`` entry. Available types include: Rectangle, Rounded Rectangle (needs ``Radius=`` as well), Circle, Bitmap (needs ``Bitmap=`` as well), and ArrowL/R/U/D. These are also used for Menus for the ``ItemType=`` property, which determines how the background for each Menu Item is drawn. There are also some special-case types covered later.

In addition, other properties can be defined that set the size and colors used, including ``Size=`` and/or ``ItemSize=, Alignment=, Font=, FontSize=, FontWeight=, BorderSize=, LabelRGB=, ItemRGB=, BorderRGB=``, and ``TransRGB=`` (which color is treated as a fully-transparent "mask" color). Menus can optionally include a Title Bar with the ``TitleHeight=`` property and a gap between Menu Items (or overlap by using a negative value) with the ``GapSize=`` property.

In order to visually show current selection and possibly "flash" a Menu Item when it is activated, alternate colors (or Bitmaps) can be set for Menus starting with the word "Selected" or "Flash" or the combination "FlashSelected", such as ``SelectedItemRGB=``, ``FlashBorderRGB=``, ``FlashSelectedLabelRGB=``, ``SelectedBitmap=``, and so on.

### Fading and transparency

HUD Elements can also fade in and out when shown or hidden, or Menus can be partially faded out when they haven't been used for a while or are currently "disabled" (by virtue of having no active buttons assigned that can control the Menu), all of which can be controlled with the properties ``MaxAlpha=, FadeInDelay=, FadeInTime=, FadeOutDelay=, FadeOutTime=, InactiveDelay=``, and ``InactiveAlpha=``. All alpha values should be in the range of 0 to 255 (0 fully invisible, 255 fully opaque), and delay times are in milliseconds (1/1000th of a second).

### Alignment

The Hotspot relative position shortcuts L/R/T/B/C are also used for the ``Alignment=`` property. For example, if you specified ``R-10`` for a Menu's X Position, but the Menu is 50 pixels wide, most of it would end up cut off by the right edge of the screen (only the left 10 pixels of the Menu would be shown). Instead, you can use the following to make the *right* edge of the Menu be 10 pixels to the left of the right edge of the screen, and exactly centered on the Y axis:

    [Menu.Macros]
    Position = R-10, CY
    Alignment = R, C

### Bitmaps

As mentioned above, HUD Elements can be set to ``Type=Bitmap`` and Menus can use ``ItemType=Bitmap``, which require specifying the region of a .bmp file to use with ``Bitmap=`` (and optionally ``SelectedBitmap=`` in the case of a Menu to make selected item distinctive).

First, any Bitmaps to be used must be named in the [Bitmaps] category, with each have a name and then a path to a file, like so:

    [Bitmaps]
    MyImage1 = "C:\Images\MyBitmap1.bmp"
    MyImage2 = Bitmaps\MyBitmap2.bmp

*The path specified can be a full path or relative to the location of the overlay's .exe file. At this time, only actual .bmp files are supported, not .png's etc).*

Once a Bitmap is set properly as in the above example, set the HUD Element Type or Menu ItemType to use the bitmap, or a portion of it, like so:

    [HUD.Picture]
    Type = Bitmap
    Bitmap = MyImage1

    [Menu.MyMenu]
    Style = List
    ItemType = Bitmap
    Bitmap = MyImage2: 0, 0, 32, 32
    SelectedBitap = MyImage2: 32, 0, 32, 32
    
The coordinates listed for using a portion of a bitmap are in the format of LeftX, TopY, Width, Height. *The bitmap or bitmap region will be scaled as needed to fit into HUD Element's Size or Menu's ItemSize dimensions if they do not match.*

### Label Icons

In addition to the backdrop of a Menu Item, a Bitmap be used in place of the text label for a Menu Item. This allows having a different image for each individual Menu Item. This image will be copied into the inner area of the Menu Item, meaning the resulting bitmap will be drawn at (ItemSize.x/y - (BorderSize x 2)) size.

The [Icons] Category is used to link each Menu Item's label text to what should be drawn in place of it. For example:

    [Menu.MyMenu]
    Style = List
    1 = Spell1: Alt-1
    2 = Spell2: Alt-2
    ...

    [Bitmaps]
    MyIcons = Bitmaps\MyIcons.bmp

    [Icons]
    Spell2 = MyIcons: 0, 0, 32, 32

Would have a menu where the first Menu Item would have the text label "Spell1", but the second Menu Item would instead show a copy of the (0, 0, 32, 32) region of Bitmaps\MyIcons.bmp displayed instead of text. *Note that linking a label to an icon is case-insensitive and ignores spaces, so "SPELL 2" would link to the same icon as "Spell2".*

### Icons copied from game window

While icons from bitmaps stored on your hard drive might look nicer then plain shapes, they can't dynamically display information that might change during play. In the above example, the "Spell2" label would always show the same icon, but what if you want it to display an icon that matches the in-game icon for whatever spell you actually have memorized in slot 2? To do this, you can specify a rectangular region of the game's window to be copied and act as an icon, which will then be continuously updated (at least once every ``[System]CopyIconFrameTime=`` milliseconds).

To do this, simply omit the name of a Bitmap, and have the coordinates be the portion of the game's window you want copied over, like so:

    [Icons]
    Spell2 = 500, 250, 32, 32

These copy-from coordinates work like Hotspots and can be expressed relative to the screen size rather than direct pixel values, or even a combination, like this:

    [Icons]
    Spell2 = R-20, 20%, 5%, 15

*TIP: This copy technique should work even if the copied-from area of the game's window is being covered up by an overlay Menu or HUD Element, so you can purposefully hide the portion of the game screen you are copying from with an overlay HUD Element, or even the copied-to Menu itself, to avoid having the same icons shown in two places at once on your screen*

### Key Bind Array HUD Elements

Two special HUD element types offset their visual position to named Hotspots with the same name as Key Bind Array elements. These use ``Type = KeyBindArrayLast`` and ``Type = KeyBindArrayDefault`` and must also have a special property to specify which Key Bind Array and Hotspots to use, such as ``KeyBindArray = TargetGroup``. 

``Type=KeyBindArrayLast`` will change to the position matching the last used Key Bind from the array. ``Type=KeyBindArrayDefault`` will change to the position of the set Default Key Bind of the array (initially the first item in the array, but can be changed with the ``=Set <KeyBindArrayName> Default`` command).

For these to work, make sure to add Hotspots in the ``[Hotspots]`` category with the same name as each Key Bind name. These will specify a position offset this HUD Element should jump to when either the "Last" or "Default" Key Bind Array index is changed.

## Other Commands

In addition to keyboard and mouse input and Commands for adding/removing Layers and managing Menus, there are some other special-case commands you can assign to Gamepad buttons and Menu Items.

This includes commands for controlling the overlay app itself, such as ``=Change Profile`` to bring up the Profile selection dialog or ``=Quit App`` to shut down the overlay application.

## Other system features

As mentioned for first starting up, you can have the application automatically launch a game along with whichever Profile you first load. You can also set the Window name for the target game, so the HUD Elements will be moved and resized along with the game window, and force the game window to be a full-screen window instead of "true" full screen if needed so the HUD Elements actually show up over top of the game.

There are various other system options you can set like how long a "tap" vs a "short hold" is, analog stick deadzones and mouse cursor/wheel speed, whether this app should automatically quit when done playing the game, and what the name of this application's window should be (so you could set Discord to believe it is a game, since Discord refuses to recognize old EQ clients as one, which is nice if you want to let people know you are playing EQ). Check the comments in the generated *MMOGO_Core.ini* for more information on these and other settings.

### Global automatic scaling

The absolute (pixel) position/size values used in Hotspots, positions, sizes, etc, can be automatically scaled according to a fixed ``UIScale=`` value and/or according to the current size of the game's window/screen. This can potentially be easier to deal with than using a relative (%) value for every individual position/hotspot/etc. This automatic scaling also applies to some HUD Element properties that otherwise do not have a relative value option, like BorderSize, GapSize, TitleHeight, and FontSize.

To set a fixed scaling value, just use the [System] property ``UIScale=`` with a percentage, like 150% (or 1.5 if you prefer). To enable automatic scaling, first set what the base resolution all your positions/sizes/etc is based on, and then they will be scaled up or down automatically as the game window is resized (assuming you've set ``TargetWindowName=`` properly). The base resolution to scale from is set by the [System] properties ``BaseScaleResolutionX=`` and ``BaseScaleResolutionY=``.

*Note that UIScale and resolution scaling are both applied (multiplied by each other), but that neither affects the relative (%) positions directly set in individual hotspots/positions/etc - only absolute pixel values are scaled by these!*
