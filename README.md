# MMO Gamepad Overlay

Check the [Downloads](https://bitbucket.org/TaronM/mmogamepadoverlay/downloads/) section for latest built versions. Download MMOGamepadOverlay-a64.zip for the 64-bit version and MMOGamepadOverlay-x86.zip for the 32-bit version. There are also example UI files for some MMO's you can optionally download if you want to use the same UI with them that I personally use.

Demonstration video (click to play):

[![MMOGO in M&M Stress Test](http://img.youtube.com/vi/SBlLWR59GGk/0.jpg)](http://www.youtube.com/watch?v=SBlLWR59GGk "MMO Gamepad Overlay app demo")

This application translates DInput & XInput game controller input into keyboard and mouse input, possibly with extra HUD elements and menus (displayed in a separate transparent window overlaying the game window) to help keep track of what various buttons may do.

This application does ***NOT***:
* Require (or even include) an installation process
* Modify game files in any way
* Modify, or even directly read, game memory
* Inject any code into other processes
* Use any libraries beyond the basic Windows API
* Contain any art assets (besides the app icon)
* Expect or solicit payments/donations
* Use any proprietary or hidden source code

Although there are plenty of other methods to translate gamepad input into KB/M input, including Steam, this particular application was specifically designed for playing the *EverQuest* emulation servers *Project 1999* and *Project Quarm*, and eventually the upcoming *Monsters & Memories* and *EverCraft*, with a control scheme inspired by the only MMORPG ever exclusively designed for playing with a controller - *EQOA* for the PlayStation 2. It thus has specific features related to these games that are difficult to reproduce with other, more general-purpose options..

Nothing says it can't also be used for other games though, as it is customizable.

## Basic operation

Place the executable wherever is convenient, keeping in mind that it will generate and read text files with the *.ini* extension in the same folder in which it is placed. When run for the first time, you will be prompted to create a **Profile**, which is associated with one or more *.ini* files that customize how the application looks and behaves. Multiple example profiles are provided to pick from as a base template. You can just choose to have a single profile and auto-load it every time, or have different ones for different games or even different characters for the same game. You will then be prompted if you want your profile to also automatically launch an associated game (if it is the first profile loaded when launching the app), for convenience.

After that, load up the game and you should be able to use the controller to move your character, move the mouse, and perform actions. How that all functions depends on the game and the settings in your loaded Profile.

## Gamepad conflict for Windows 10+

Some games (notably *Monsters & Memories* pre-alpha tests as of this writing) may actually respond to gamepad input already in Windows 10+, but not in any useful way. In fact, Windows itself can respond to gamepad input such as in the start menu by using this new UWP "feature". This can be problematic because this gamepad overlay can not stop other applications or Windows from detecting gamepad input, causing buttons you press to result in extra actions you did not intend (in M&M's case, pressing "A" on an XBox controller can cause it to click on the last button you clicked with the mouse, for example, in addition to whatever you actually assign "A" to do here).

One way to stop this is using a utility called [HidHide](https://github.com/nefarius/HidHide) (which, if you are using a PlayStation controller and something like DS4Windows, you may already have installed anyway). Search your computer for "HidHide Configuration" and run that app if you have this installed. There, you can set it to "hide" your gamepads from the game in question, so they will ONLY respond to the mouse and keyboard input sent by this application (and your actual mouse and keyboard). HidHide can't stop Windows itself from responding to gamepad input though.

For some applications (but sadly not *Monsters & Memories* last time I tried this) it can work to just disable this "feature" in Windows, if your Windows is updated enough to allow that, through a Registry edit. [Here](https://github.com/microsoft/microsoft-ui-xaml/issues/1495#issuecomment-745586048) is a description of how to do it. In case that link dies at some point, the brief version is to make a Registry key ``
HKLM\Software\Microsoft\Input\Settings\ControllerProcessor\ControllerToVKMapping`` and add a DWORD to it called ``Enabled`` and set its value to 0. This will only disable Windows and some "UWP" apps from using the gamepad for basic functionality - it will not prevent games that natively support gamepads for full game play, or utilities like this application or Steam from remapping a gamepad to keyboard & mouse input.

## Basic use and default control scheme

The rest of this Readme file explains how to customize your control scheme by editing .ini files using a text editor. This may seem daunting, but keep in mind that **you don't actually have to learn any of the rest of this stuff if the default profiles provided (or maybe shared by other users) work for your needs!** If you'd like a simpler user guide for just playing a supported MMO with the app using a default included control scheme, try this video (click the image to play): [![MMOGO in M&M Stress Test](http://img.youtube.com/vi/dGfhbFy53Rk/0.jpg)](http://www.youtube.com/watch?v=dGfhbFy53Rk "MMO Gamepad Overlay User Guide")**

## Profile Setup

The application generates a *MMOGO_Core.ini* file which contains some default settings and is used to track what other profiles you have created and their names. You can edit this file and any other *.ini* files it generates with any text editor, or create your own, once you know how they work.

The list of profiles is at the top of *MMOGO_Core.ini*, along with an entry for specifying which one to load automatically, if any. Each profile *.ini* file can specify a "parent" Profile with a line like:

    ParentProfile = MyBaseProfile
    
This system is intended to allow for having a "base" profile for a particular game, and then multiple profiles for different characters that use that same base as their parent. You can set up as long of a chain of parent profiles as you desire. It is not necessary to specify the "Core" profile as a ParentProfile at any point, as it will always be loaded first anyway. Profiles are loaded in order from parent to child, and any duplicate properties are overwritten as they are encountered. That means the specific profile you are loading will take priority over its parent base, which itself will take priority over any parent it has, and all other files will take priority over "Core".

All of this is set up automatically with the default example profiles generated on first launch.

You can edit MMOGO_Core.ini yourself to add more profiles, or use the menu option File->Profile from within the application to do so with a GUI.

## Profile customization

Each profile is a *.ini* file (and possibly one or more parent *.ini* file(s) as explained above). These are plain-text files you can edit in Notepad/etc that contain a list of **Properties**. Each property is identified by a *section* and a *property name* with an associated *property value*.

If the same *section*+*property name* is encountered more than once, the most recent one will override any previous ones (which allows for having "default values" specified in Core or a parent Profile that are then overwritten by a specific child Profile). However, the same *property name* can be used in more than one *section* and will be considered different properties. There is also a special unnamed "root" section at the top of each file before the first section label is encountered, which is where the ParentProfile property mentioned earlier is placed.

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

*NOTE: Comments are only supported by placing # and ; at the **beginning** of a line, you can NOT add comments at the end of a line, it will instead be considered part of the Property Value.*

## [Scheme] Section

This is the main section for determining how gamepad input is translated into keyboard and mouse input. With a couple of special exceptions, each *property name* in this section represents a gamepad button (and optionally an action associated with that button like *press*, *tap*, *release*, etc), and each *property value* represents a **Command** for the application to execute when that button is used.

Commands are usually input to send to the game, such as keyboard keys, mouse buttons, and mouse movement. For example, to assign R2 to act as the right mouse button, you could include:

    [Scheme]
    R2 = RMB

There are various ways supported of specifying gamepad buttons and keys, so you could instead use:

    [Scheme]
    RT = Right-click

If you want the full list, check *Source\GlobalConstants.cpp* in the source code.

### Multi-button assignment

You can assign 4 buttons at once in the case of the D-pad, analog sticks, and the "face buttons" (ABXY or X/Square/Triangle/Circle). For example:

    [Scheme]
    DPad = MoveTurn
    LStick = MoveStrafe
    RStick = Mouse
    # Below treats face buttons like a D-pad
    FPad = Move

Each of these (including the analog sticks) are otherwise treated as 4 separate buttons like "LStickUp" or "DPadDown", etc. when want to assign each direction to a separate command.

### Button actions

When the *property name* is simply the button name by itself like in the above examples, it is treated as the action "press and hold". So in the earlier ``R2 = RMB`` example, the right mouse button will be pressed when R2 is, held for as long as R2 is held, and released when R2 is released.

Other "button actions" can be specified instead, and each button can have multiple commands assigned to it at once such as for a "tap" vs a "hold." For example:

    [Scheme]
    R2 = A
    Press R2 = B
    Tap R2 = C
    Release R2 = D
    Hold R2 400 = E

This example demonstrates the maximum number of commands that could be assigned to a single button. When R2 is first pressed the 'B', and 'A' keyboard keys would be sent to the game in that order ('A' would be held down but 'B' would just be tapped and immediately released). If R2 was quickly released, a single tap of the 'C' key would be sent. If R2 was held for at least 400 milliseconds, an 'E' tap would be sent once. No matter how long it is held, even if just briefly tapped, once let go of R2 a single tap of 'D' would be sent to the game, as well as finally releasing 'A'.

You can only have one "Hold" action assigned per button, but how long it must be held to trigger can be optionally set by adding a number at the end of the property name. If no number is added, the value ``[System]/ButtonHoldTime`` will be used.

Notice how only the base ``R2=`` property can actually hold a key down for more than a split second. That is the special property of this base 'press and hold' button action. All other button actions can only "tap" a key (press and then immediately release it). Many special commands can't be "held" anyway, so assigning one of these to just ``R2=`` will make it act the same as assigning it to ``Press R2=``, and some are can *only* be assigned to the base button action because they relate to "holding" for a duration.

## Commands

As mentioned above, commands assigned to buttons can be as simple as the name of a keyboard key or mouse button, as well as mouse movement (such as ``=Mouse Up`` or ``=Mouse Left``).

Not mentioned yet is mouse wheel movement, which can be set with commands such as ``=MouseWheel Up Smooth`` or ``=MouseWheel Down Stepped`` or ``MouseWheel Down 1``. "Smooth" vs "Stepped" affects whether or not movement of less than one 'notch' at a time is sent to the application (stepped is the default if unspecified). Specifying a number instead means only want the wheel to move one time (by the number of "notches" specified) even when the button assigned to the command is held continuously.

### Combination keys

A command can also be a keyboard key or mouse button combined with a *modifier* key - Shift, Ctrl, Alt, or the Windows key - such as:

    R2 = Shift+A
    L2 = Ctrl X
    L1 = Ctrl-Alt-R
    R1 = Win-Plus
These can still be "held" as if they are single keys.

*WARNING: Modifier keys should be used sparingly, as they can interfere with or delay other keys. For example, if you are holding Shift+A and then want to press just 'X', since the Shift key is still being held down, the game would normally interpret it as you pressing 'Shift+X', which may be totally different command. This application specifically avoids this by briefly releasing Shift before pressing X and then re-pressing Shift again as needed, but this can make the controls seem less responsive due to the delays needed to make sure each release and re-press are processed in the correct order. Consider re-mapping controls for the game to use Shift/Ctrl/Alt as little as possible for best results!*

### Key Sequence

You can also specify a sequence of keys to be pressed. For example, you could have a single button press the sequence Shift+2 (to switch to hotbar #2), then 1 (to use hotbutton #1), then Shift+1 (to switch back to hotbar #1), like so:

    R2 = Shift+2, 1, Shift+1

Key sequences can NOT be "held", so holding R2 vs just tapping it will give the same result in the above example.

You can also add pauses (specified in milliseconds) into the sequence if needed, such as this sequence to automatically "consider" a target when changing targets:

    # 'Delay' or 'Wait' also work
    R1 = F8, pause 100, C
    
*WARNING: Do NOT use this to fully automate complex tasks, or you're likely to get banned from whichever game you are using this with!*

#### Mouse jump in key sequence

On a more advanced note, you can also request in the sequence to jump the mouse cursor to a named **Hotspot** location (defined in [Hotspots]) to click on it, such as:

    [Hotspots]
    CenterScreen = 50%, 50%

    [Scheme]
    R1 = Point to CenterScreen, LClick
    R2 = LClick at CenterScreen->RClick
    
### Chat box macros

While a Key Sequence could technically be used to type a message directly into the game's chat box, it is easier to directly use a *Slash Command* or *Say String* command to do this.

Slash Commands start with ``/`` and Say Strings (chat messages) start with ``>`` (the '>' is replaced with the Return key to switch to the chat box when the command is actually executed). These commands will actually "type" the sequence into the chat box as a series of keyboard key presses, followed by pressing Return to send the macro. For example:

    [Scheme]
    Hold R1 = /who
    Hold R2 = /g Roll for loot please!
    Hold L1 = >Would you like to group?

This will lock out most other inputs while typing though, so in general it is better to instead create macros using the in-game interface and activate them via key sequences, such as the earlier example for activating hotbar buttons.

## Key Binds (aliases)

Key Binds are basically just aliases or shortcuts for any of the above commands. Using Key Binds, instead of using:

    [Scheme]
    XB_A = Space
    Hold R1 = /who
    Hold R2 = /g Roll for loot please!
    XB_X = LClick at CenterScreen->RClick

You would instead use:

    [KeyBinds]
    Jump = Space
    Who = /who
    RollForLootPls = /g Roll for loot please!
    UseCenterScreen = LClick at CenterScreen->RClick

    [Scheme]
    XB_A = Jump
    Hold R1 = Who
    Hold R2 = RollForLootPls
    XB_X = UseCenterScreen

Key binds can also be used within key sequences, including in other key bind assignments (as long as they don't reference each other in an infinite loop). For instance, instead of the earlier example of the key sequence assignment ``R1 = F8, pause 100, C`` you could use:

    R1 = TargetCycleNPC, pause 100, Consider

or even:

    [KeyBinds]
    TargetNPC = TargetCycleNPC, pause 100, Consider

    [Scheme]
    R1 = TargetNPC

*TIP: Using multiple key binds in a sequence is the only way to have multiple chat box strings within a single command - otherwise any '/' or '>' characters after the first are just considered part of the same single string rather than a separate Slash Command or Say String.*

These examples may not seem worth the effort just for added readability, but there are multiple other uses for key binds over just directly assigning things to keyboard keys. If nothing else, it can be convenient when using the same input in multiple places to only have to change the one Key Bind if you change your in-game bindings.

### Special Key Binds

A few Key Bind names are specifically checked for by the program and used directly as more than just aliases. These include:

    SwapWindowMode =
    AutoRun =
    MoveForward =
    MoveBack =
    TurnLeft =
    TurnRight =
    StrafeLeft =
    StrafeRight =

``SwapWindowMode=`` is used in the code for attempting to force the target game into full-screen-windowed mode (as opposed to *true* full screen mode which would prevent the overlay from being visible), which can be set in the [System] section with the flag ``ForceFullScreenWindow = Yes`` (and optionally ``StartInFullScreenWindow = Yes``). It is typically set to ``=Alt+Enter``.

The Move/Turn/Strafe commands are used when assign buttons to ``=Move`` (same as ``=MoveTurn``) or ``=MoveStrafe``, or directly to ``=Strafe Left``, ``=Move Back`` etc. AutoRun is used alongside these to fix issues like accidentally immediately cancelling auto-run when it is assigned to L3 due to small stick wiggles while release the stick.

*Note that assigning a button to a Move command or one of the above key binds is different than just assigning it directly to the actual keyboard key the game uses for movement (besides just the ability to assign 4 directions at once). These commands make use of extra functionality like the [Gamepad] properties ``MoveDeadzone=``, ``MoveStraightBias=``, and ``CancelAutoRunDeadzone=`` for finer control when assigned to an analog stick, plus specialized code for fixing issues with interactions between movement and other actions such as chat box macros. It is recommended you avoid directly assigning buttons to the movement keys or auto-run key and use the above commands/key binds instead.*

### Key Bind Arrays

If multiple Key Binds have the same name except for a number on the end of the name, they will also be stored as a **Key Bind Array**. For example:

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

**Hotspots** are positions on the screen of significance, such as where a mouse click should occur in a Key Sequence as mentioned before. Positions of Hotspots, HUD Elements, Icons, and so on are specified as X and Y coordinates with Y=0 representing the top and X=0 representing the left side. Some properties use 4 coordinates to represent a rectangle, arranged as X (left edge), Y (top edge), Width, and then Height.

Each of the 2-4 coordinates can have up to 3 values - the *Anchor*, the *Fixed Offset*, and the *Scaling Offset*, in that order. *It is NOT required to specify all 3,* or even more than one per coordinate. 

The anchor represents the starting point as a relative position (usually to the target game's window/screen size), and is expressed as either percent (like 50%, or 0.5 if you prefer) or by special shortcuts like L/T/R/B/C/CX/CY/W/H instead of numbers. If no anchor is specified, it is assumed to be 0% (the top-left corner).

After the anchor (if there is one) is the offsets in pixels. If you specify only one offset (or only one number overall) it will be the *scaling offset*, which will be multiplied by the global  [System] property ``UIScale=`` (which itself can be automatically set for some games by use of ``UIScaleRegKey=``). If you specify two offsets, the first will be a *fixed offset* (NOT multiplied by UIScale) and the second will be the scaling offset (which you can just set to 0 if you don't need one).

Each value after the first one within a coordinate should be separated by a '+' or `-' sign, and full coordinates are separated from each other by a comma or 'x' (i.e. "10 x 5" or "10, 5")

Some accepted examples of valid positions for reference:

    # Center of the screen/window
    = 50% x 50%
    = 0.5, CY
    # Pixel position 200x by 100y, multiplied by UIScale
    = 200 x 100
    # Same but ignoring UIScale
    = 200+0, 100+0
    # 10 pixels to the left of right edge, and
    # 5 pixels down from 30.5% of the game window's height
    # (10 and 5 will be multiplied by UIScale)
    = R - 10, 30.5% + 5
    # BR corner offset -50x and -75y (un-scaled)
    # then another 10 (scaled) up from there
    = R-50 + 0, B -75 -10
    # Rectangle with full height but 50%-75% of the width
    = 50%, 0, 25%, H

*Properties that only have a single value such as BorderSize, TitleHeight, and FontSize are always considered "scaling" and are thus affected by UIScale!*

## Controls Layers

To really unlock the full range of actions in an MMO using a Gamepad, you will almost certainly need to assign more than one function to a single button through the use of button combinations or different "modes" of control. This can be accomplished through the use of **Controls Layers**. These layers change what Commands are assigned to what buttons while the layer is active.

You can have multiple Controls Layers added at once. They can be thought of as stacked on top of each other, and for any given button, the top-most layer's assignments will take priority by covering up the button assignments from the layers below it. If the top-most layer has nothing assigned to a button, the next layer below it will be checked for an assignment for that button, and so on. Which layers are on top of which depend on various factors covered later, but in general newly-added layers are placed on top of older ones and thus take priority.

Layers can be added with the ``=Add <LayerName>`` command and removed with the ``=Remove Layer`` (removes layer containing the command) or ``Remove <LayerName>`` command.

Layers are defined the same as [Scheme], with just the section name [Layer.LayerName] instead. Here is a simple example of how to utilize adding and removing a layer:

    [Scheme]
    Square = Jump
    Triangle = Consider
    L2 = Add Alternate layer

    [Layer.Alternate]
    Square = Duck
    R2 = Remove this layer

In this example, Square will jump by default. Pressing L2 will add the "Alternate" layer. At that point, Square will Duck instead, but since it doesn't assign anything to Triangle, Triangle will continue to Consider. Pressing R2 will remove the Layer, meaning Square will once again jump.

You can remove one layer while adding another in a single command using ``Replace this layer with <LayerName>`` or ``Replace <LayerName> with <LayerName>``. You can also both add or remove a layer in a single command, depending on if it is already active or not, with the single command ``=Toggle <LayerName> layer``.

*If you want a button to literally do nothing, including blocking lower layers' assignments for that button, set it to ``= Do nothing``. Above you could set ``Triangle = Do Nothing`` in the Alternate layer to prevent Triangle from using Consider while that layer is active. Leaving the button assignment blank (just ``Triangle=``) is the same as not mentioning the button at all and would still just allow Triangle to use Consider.*

*There can only be one of each named layer active at once, so trying to add a layer with the same name again will simply update its position as if it was newly added, but not actually remove or re-add it or add another copy of it!*

### Overriding buttons and the "Just" prefix

By default, assigning something to **any** action for a button blocks **all** commands assigned to that button from lower layers. For example, with this setup:

    [Scheme]
    Tap R1 = TargetNPC
    Hold R1 = Consider
    L2 = Add Alternate layer

    [Layer.Alternate]
    Tap R1 = TargetPC
    R2 = Remove this layer

Once you had added the Alternate layer with L2, briefly holding R1 will no longer Consider a target. R1 is now exclusively set to "TargetPC" when tapped because that is the only command assigned to R1 on that layer.

You could of course manually add ``Hold R1 = Consider`` to the Alternate layer, but you could also specify that you only want to override the Tap action and let other actions, like Hold, still defer to lower layers' settings by adding the key word **Just** in front of the property name, like this:

    [Layer.Alternate]
    Just Tap R1 = TargetPC
    R2 = Remove this layer

Now when tap R1 "TargetPC" will be used instead of "TargetNPC", but holding R1 will still use "Consider" from [Scheme].

### Held Layers

Rather than manually removing a layer with Remove/Toggle/Replace, you can have a layer that is added when you first press a button and then automatically removed when you let go of that button. A layer added in this way is considered a *Held Layer* while active.

Held Layers are most useful for allowing button combinations by holding some kind of "modifier button" to temporarily change what other buttons do.

Here is a modification of the earlier example but using a held layer instead:

    [Scheme]
    Square = Jump
    L2 = Hold Alternate layer

    [Layer.Alternate]
    Square = Duck

In this case, rather then pressing L2, then Square to Duck, and then pressing R2 again to restore normal controls, you can just press and hold L2, tap Square to duck, then release L2. In other words, L2+Square = Duck in this control scheme.

*Similar to held keys, a held layer can ONLY be assigned to the button without any actions specified, like the above ``L2=`` example. It is not valid to assign something like ``Tap L2=Hold layer``.*

### The "Auto" Button

Each layer has a special 'virtual button' unique to it, that can be assigned commands like any real gamepad button. This button is called "Auto". It is "pressed" whenever the layer is added, and then "released" whenever the layer is removed.

This Auto Button can be particularly useful in order to assign a button to simultaneously 'hold' a layer while also holding a key, by having held layer hold the key down using its Auto Button.

For example, let's say you wanted to make pressing and holding Circle on a PS controller act the same as holding the left mouse button, but you also want to make it so while holding Circle, you could use your left thumb on the D-pad to move the cursor around to "drag" the mouse, even though normally the D-pad is used for character movement. You could accomplish this as follows:

    [Scheme]
    D-Pad = Move
    Circle = Hold MouseDrag layer

    [Layer.MouseDrag]
    Auto = LMB
    D-Pad = Mouse

With this setup, pressing Circle will add the MouseDrag layer, which will click and hold the left mouse button for as long as the layer is active via Auto, while also changing the D-Pad to control the mouse. Releasing Circle will remove the layer, restoring the D-Pad to character movement instead and releasing the left mouse button (since Auto is "released" when the Layer is removed).

You can even assign commands to ``Press Auto=``, ``Release Auto=``,  ``Tap Auto =`` and so on, like any real button. Even ``Hold Auto ### =`` triggers once the layer has been active for ### milliseconds. ``With Auto=`` just acts an extra version of "Press" with no special properties, but allows up to 3 commands to trigger when a layer is first added ("With Auto", then "Press Auto", then just "Auto").

### Layer Mouse= property

Layers (and the root [Scheme]) can change how the mouse is treated by using ``Mouse=Cursor`` (normal), ``Mouse=LookTurn`` (holding the right-mouse button down to keep standard MouseLook mode active), ``Mouse=LookOnly`` (holding the left-mouse button down for alternate MouseLook in games that support it), or ``Mouse=Hide`` ("hide" the cursor by jumping it to the corner of the screen). The top-most layer with a ``Mouse=`` property specified dictates the mouse mode used, with the special exception of ``Mouse=HideOrLook`` which changes what it does based on the layers beneath it.

### Layer HUD= property

Each layer (including [Scheme]) specifies which **HUD Elements** (including **Menus**) should be visible while that Layer in active. Layers can also specifically *hide* HUD Elements that were requested to be shown by lower layers, stopping them from being shown (unless yet another, higher layer overrides the *hide*). This is done via the ``HUD=`` property including a list of HUD element names to show (and optionally the 'Show' and 'Hide' key words), such as:

    [Layer.MainMenu]
    HUD = MainMenu
    
    [Layer.MouseLook]
    Mouse = Look
    HUD = Show Reticle
    
    [Layer.TopMost]
    HUD = Hide MainMenu, Show TargetGroupLast

### Layer Hotspots= property

Very similar to the HUD= property, each layer can enable or disable **Hotspot Arrays** that can be used via the ``=Select Hotspot <direction>`` or ``Select Hotspot or MouseWheel`` command (the latter of which scrolls the mouse wheel up and down if there are no hotspots available in that direction - handy for scrolling through a tall loot window!). Just like ``HUD=``, each layer can disable Hotspot Arrays enabled in the layers below it, though layers above can override that yet again.

Hotspot Arrays are defined much like Key Bind Arrays - a list of Hotspots with the same name but just different number on the end, starting with 1, like so:

    [Layer.LootMode]
    Hotspots = LootWindow, Disable Standard

    [Hotspots]
    LootWindow1=32x240
    LootWindow2=32x281
    LootWindow3=32x322
    ...
*Multiple elements of a Hotspot Array can be defined at once to make it easier to add or adjust a whole array quickly. More on this in the later section "Hotspot Array and Copy Icon ranges".*

### Layer Parent= property

A layer can optionally set a *parent layer* with the ``Parent=`` property, followed by the name of another layer. This makes the layer a *child layer* of the specified parent. Parent and child layers have the following properties:
* When a child layer is added, if its parent doesn't exist yet, the parent is automatically added first.
* When its parent is about to be removed, the child is removed automatically first.
* When a child layer is added, it is placed directly above the parent layer (and any other children of that same parent), bypassing normal layer ordering rules (see below).
* A parent layer can have a parent of its own, and so on for as long of a chain as you want.
* Combo layers, Held layers, and [Scheme] ignore the ``Parent=`` property since they have their own special rules for automatic removal and layer order. They can, however, act as parents to other layers.

### Combo layers

These special layers can not be manually added, but are instead automatically added and removed whenever a combination of other layers is active. They can be used for more complex button combinations. For example, let's say you want Circle to send a different key for pressing Circle by itself, L2+Circle, R2+Circle, or L2+R2+Circle. That last one can be done with a combo layer, such as:

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

With this setup, when hold both L2 and R2, causing both those layers to be active, the L2+R2 layer is automatically added, causing Circle to press "D" instead of "C", "B", or "A". The L2+R2 layer will be removed as soon as let go of either L2 or R2.

Here's some other technical details about combo layers:
* They are specified by 2 or more layer names separated by '+' after the ``[Layer.`` prefix.
* They are added as soon as all their base layers are active, and removed as soon as any of their base layers are removed.
* They can not be directly referenced by name for commands like Add Layer because symbols like '+' are filtered out of command strings. However, they can be named in other layers' and ``Parent=`` property.
* They have their own special layer ordering rule (see below).

### Layer ordering and the Priority= property

As mentioned before, layers can be thought of as being stacked on top of each other in a specific order. This order determines what button assignments are active as well as other properties like Mouse and HUD. with higher layer properties and button assignments taking priority. The order of layers in the stack is thus very important for determining behavior of the overlay.

While mostly based on factors like type of layer (normal added layer, held layer, or combo layer) and parenting, one additional sorting factor you can add is a ``Priority=#`` property for any layer, with a value from -100 to 100. If this property is not set, a layer has a default priority value of 0.

Layers follow these rules when determining order:

* [Scheme] always exists as the lowest layer
* Held Layers are placed on top of normal added layers (and all of their children and combo layers)
* Child layers are placed on top of their parent layer, regardless of the parent layer type, but below any layers that are not descended from that same parent
* If 2 layers would have the same position after the above, then the layer with the higher ``Priority=`` property will be placed above any with a lower priority set, and below any with a higher priority set
* If 2 layers still have the same position (same priority), then newly-added layers will be placed on top of older layers
* Combo Layers act like a child layer, except they select the top-most of their base layers as their parent when it comes to ordering
* Combo layers ignore the priority property and timing of being added, so in the case they would otherwise end up in the same position they will be sorted according to the relative order of their other (non-top-most) base layer(s).

To demonstrate how these rules shake out, here's a potential layer order from top to bottom:

      Combo Layer (B+E)
      Combo Layer (A+E)
    Held LayerE
      Child of D
    Held LayerD
    Added LayerC
        Child of A+B Combo
      Combo Layer (A+B)
    Added LayerB
      Child of A (Priority=2)
      Child of A (Priority=1)
    Added LayerA
    [Scheme]

### "When Signal" commands

Now that layers and the importance of their order is covered, it is time for the exception to the rule - Signal Commands. These commands run whenever a "signal" is sent out, and **ignore layer order.** The layer containing the command must still be actively added to the stack though, of course.

To add a Signal Command to a layer (or the root [Scheme]), use the syntax ``When Signal = Command``. Most commands can be used, though not ones that must be held like "Hold Layer".

There are two types of signals that are sent out for these commands to use - initially pressing a Gamepad button, or using a Key Bind by name (including as part of a Key Sequence) - yet another useful aspect of using Key Binds instead of referencing keys directly.

To run a command when a button is pressed, even when another, higher layer has something assigned to that button, use the syntax ``When Press ButtonName = Command``. Example: ``When press L2 = Remove this layer``.

To run a command when a Key Bind is used, use the syntax ``When KeyBindName = Command``. Examples: ``When Sit = Remove this layer`` or ``When MoveForward = Remove this layer``.

As should be obvious from the examples, the main use case for this feature is a layer removing itself when certain other game actions are used or buttons are pressed, without having to be the front-most layer with a button assignment that removes itself and performs an action with the same button. This can save on having to make a bunch of utility layers with lots of Press Auto and Auto commands to handle this.

Some other things to note:

* Signal Commands are lower priority than other command assignments and may slightly delayed depending on how many other actions may take priority. They should not be depended on when order of execution is important.

* You can actually make a Key Bind that does nothing other than act as a signal for other commands by setting it to be ``=Signal Only`` (or just leaving it blank).

* A key bind only sends out a signal when it is referenced by its name, not by anything it is assigned to. For example, if you have the key bind "Jump = Space", any command assigned to  "When Jump=" will run when you press a button assigned to "= Jump", but will NOT run when you press a button assigned directly to "= Space"!

* The app will not check and warn you if you set up an infinite loop of Key Binds signalling each other. For example, if you added both "When Camp = Sit" and "When Sit = Camp", then using either Sit or Camp could lead to an infinite loop alternating between sitting and camping (at least until a layer owning one of those commands is removed). So watch out for that!

## Menus

While it is possible to use layers alone to send all the input needed to an MMO, it would require a lot of complex button combinations and sequences you'd need to memorize. **Menus** can make things a lot easier, by instead assigning buttons to add/remove/control menus and then having the menus include a large number of commands.

Each menu also counts as a **HUD Element**, so the Menu must be made visible by the ``HUD=`` property for an active layer to actually see it.

Each menu has a ``Style=`` property that determines its basic structure and appearance, as well as visual properties like colors, shapes, etc of the menu items. Example Menu Styles include List, Slots, Bar, 4Dir, Grid (which can include a ``GridWidth=`` property to specify the grid shape), and Hotspots (which requires a ``Hotspots=`` property specifying a Hotspot Array to determine the center point of each menu item).

Menus are defined using the section name [Menu.MenuName]. Each *Menu Item* is defined by a *property name* of just the Menu Item number (with some exceptions covered later). The *property value* for each Menu Item contains a name/label to be displayed followed by colon ``:`` followed by a command to execute when that menu item is chosen. Here's an example of a basic menu:

    [Menu.MainMenu]
    Style = List
    Position = L+10, 25%
    Alignment = L, T
    1=Inventory: Inventory
    2=Book: Book
    3=TBD:
    4=Settings

Notice how Menu Item #3 has no Command, but still contains ``:``, so the label will be shown but nothing will happen if it is used. Menu Item #4 specifies a **Sub-Menu**, indicated by the absence of ``:``. You could also just have ``:`` followed by a command if you want no label for the Menu Item.

### Sub-Menus

A sub-menu is created by having a menu item *property value* without any ``:`` character, which then has the label double as the sub-menu's name. The sub-menu is defined by the section name ``[Menu.MenuName.SubMenuName]``.  In the earlier example, ``4=Settings`` specified a sub-menu. Here is an example setup for that sub-menu:

    [Menu.MainMenu.Settings]
    1=Profile: Change Profile
    2=Close Overlay

    [Menu.MainMenu.Settings.Close Overlay]
    1=Cancel Quit: ..
    2=Confirm Quit: Quit App

Sub-menus should only specify menu items - things like ``Style=`` as well as ``Position=`` and other visible HUD properties will be ignored for all but the "root" menu (``[Menu.MainMenu]`` in this example). Note the ``..`` command, which, when selected, just backs out of the sub-menu, returning to the previous menu.

### Controlling menus

To actually use a menu, you will need to assign menu-controlling commands to gamepad buttons in ``[Scheme]`` or a ``[Layer.LayerName]`` section.  These commands must specify the name of the menu they are referring to. Below is an example of controlling the MainMenu example from earlier, including showing/hiding it with the Start button.

    [Scheme]
    Start = Toggle Layer MainMenu

    [Layer.MainMenu]
    HUD = MainMenu
    # Exits all sub-menus and selects Menu Item #1
    Auto = Reset MainMenu
    DPad = Select MainMenu Wrap
    PS_X = Confirm MainMenu
    Circle = Back MainMenu

### Closing a menu and the Back= command

Technically, menus are never actually opened, closed, or disabled. They are always there, it is just a matter of whether or not they are visible (via the ``HUD=`` command on an active layer) and whether any buttons currently are assigned to control them. This is why when you want to "open" a menu, it makes sense to use ``=Reset <MenuName>`` as the first command on it, as it will otherwise still be in whatever state it was last left in.

You can give the appearance of a menu being closed or disabled by hiding the menu and/or making sure no buttons are assigned to control it (in fact, a menu will automatically fade to its "inactive" transparency if it detects no buttons are currently assigned to control it, to help indicate it currently disabled).

In order to essentially close a menu when trying to back out of one via the ``=Back <MenuName>`` command, you can take advantage of a special command property you can add to any menu or sub-menu named "Back". This command is run whenever the Back command is used on that menu.

For the earlier example of MainMenu being controlled by Layer.MainMenu, when the user presses Circle from the root menu, the menu could essentially close by removing the MainMenu layer, which will hide the menu (because the ``HUD=`` property making it visible will be gone) and stop controlling it (because the various button assignments related to it will be gone). Here's how:

    [Menu.MainMenu]
    Style = List
    Position = L+10, 25%
    Alignment = L, T
    Back = Remove MainMenu layer
    1=Inventory: Inventory
    2=Book: Book
    3=TBD:
    4=Settings

### Menu Auto command

Similar to the "Auto" button for each *Controls Layer* and a counterpoint to the Back property above, you can add an ``Auto=`` property to a menu or sub-menu which can be set to a direct input command to be used whenever that sub-menu becomes active. This command will trigger when changing sub-menus (including returning to one from using "Back" or "Reset") or when a menu has just been made visible/enabled when it previously was not.

### Menu directional commands

In addition to the numbered menu items, each menu can have 4 directional menu items specified, labeled as ``L=, R=, U=``, and ``D=`` and tied to using ``=Select <MenuName> Left, Right, Up,`` and ``Down`` respectively. These special menu items have their commands run directly via the ``=Select <MenuName>`` command rather than the ``=Confirm <MenuName>`` command, but *only when there is no numbered menu item in the direction pressed!*

For example, in a basic list-style menu, normally ``=Select Left`` and ``=Select Right`` doesn't do anything since all of the menu items are in a single vertical list, but if a ``L=`` and/or ``R=`` property is included, then ``=Select Left`` and/or ``=Select Right``will immediately execute the ``L/R=`` commands.

Even in a list-style menu, the ``U=`` and ``D=`` menu items can also still be used, but only if use Up while the first menu item is currently selected, or Down when the last item is currently selected. Similar logic applies to other menu styles, but may be slightly different for each one.

One key use of these is allowing for *side menus* such as in a List or Slots style menu, by using``L=`` and ``R=`` to instantly swap to a different selection of menu items without needing to add visible sub-menu items. These are still technically a sub-menu, but accessed in a different way. Here is an example of how to use these in an EQOA-like abilities menu:

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

### Slots Menu Style

This menu style is designed to emulate EQOA's "Ability List" and "Tool Bar" and is a great candidate for "side menus" as explained above. It is basically a list-style menu, but the current selection is always listed first and the entire menu "rotates" as you select Up or Down, like the reels in old slot machines (hence the name).

In order to help better keep track of what item is actually selected when the entire menu is moving, this style of menu allows for an alternate, second label for each item. This alternate label is only displayed for the currently-selected item and is drawn off to one side of the rest of the menu. You can control the size of this alternate label area by adding the property ``AltLabelWidth=`` to this menu's section.

To specify what label should be displayed in the alternate label, when setting the menu item properties, start with the alternate label first, then the pipe (``|``) symbol, then the normal label, then colon (``:``), and then the command. Like this:

    [Menu.Abilities]
    AltLabelWidth = 108
    1 = SpellName1 | Spell1: CastSpell1
    2 = SpellName2 | Spell2: CastSpell2
    ...

*The alternate label can be replaced with an image, including possibly one copied from the game's window dynamically, just like normal labels, as covered later.*

### 4Dir Menu Style

This special Menu style is designed after the "Quick Chat" menu in EQOA, which allows for quickly selecting a menu item through a series of direction presses without needing to ever use press a *confirm* button. For this menu, no numbered menu items are specified, only the directional menu items discussed above are used. So for macros in the style of EQOA, you could define a Menu like this:

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

When defining buttons to control such a menu, no ``Confirm=`` is needed. If you want the menu to automatically close once a command (that doesn't open a sub-menu) is selected, like in EQOA, assign a button to ``=Select and Close <MenuName>``. 

### Edit Menus at runtime

It can be helpful to allow changing menu contents while playing the game, such as for quickly creating macros in a 4Dir style menu. You can do this by assigning the Command ``=Edit <MenuName>`` to a button action, which will edit whichever menu item is currently selected, or ``=Edit <MenuName> Up/Down/Left/Right`` for editing directional menu items. For example, to work like the Quick Chat menu in EQOA from the above example, where holding the D-Pad for a while allows editing the macros, you could use:

    [Layer.Macros]
    HUD = Macros
    Auto = Reset Macros
    Tap DPad = Select and Close Macros
    LongHold DPad = Edit Macros
    Tap L2 = Remove Layer
    
When the ``=Edit`` command is executed, a dialog box pops up that allows changing the label or command, adding new menu items or sub-menus or deleting or replacing them, with instructions included in the dialog.

## HUD Elements (Menu graphics)

All menus are also **HUD Elements**. However, there are also HUD Element types that are *not* menus. These are created with the section name ``[HUD.HUDElementName]``. You can use this to create a reticle in the middle of the screen while in Mouse Look mode (to aim better with a "Use Center Screen" key, for example), as well as special HUD Elements like Key Bind Array indicators.

Default properties used by all HUD elements and menus can be defined in the base ``[HUD]`` section to save time defining them for every individual HUD element. The exceptions being the ``Position=`` and ``Priority=`` properties, which should likely be different for each element. Priority determines draw order (higher priority are drawn on top of lower priority, allowed range is -100 to 100 and default is 0).

Like ``Style =`` for a menu, each non-menu HUD element must specify a ``Type =`` entry. Available types include: Rectangle, Rounded Rectangle (needs ``Radius=`` as well), Circle, Bitmap (needs ``Bitmap=`` as well), and ArrowL/R/U/D. These are also used for menus for the ``ItemType=`` property, which determines how the background for each menu item is drawn. There are also some special-case types covered later.

Various properties can be defined that set the size and colors used, including ``Size=`` and/or ``ItemSize=, Alignment=, Font=, FontSize=, FontWeight=, BorderSize=, LabelRGB=, ItemRGB=, BorderRGB=``, and ``TransRGB=`` (which color is treated as a fully-transparent "mask" color). Menus can optionally include a title bar with the ``TitleHeight=`` property and a gap between menu items (or overlap by using a negative value) with the ``GapSize=`` property.

### Selected and flashing menu items

In order to visually show current selection and possibly "flash" a menu item when it is activated, alternate colors (or Bitmaps) can be set for menus starting with the word "Selected" or "Flash" or the combination "FlashSelected", such as ``SelectedItemRGB=``, ``FlashBorderRGB=``, ``FlashSelectedLabelRGB=``, ``SelectedBitmap=``, and so on.

### Fading and transparency

HUD elements can also fade in and out when shown or hidden, or menus can be partially faded out when they haven't been used for a while or are currently disabled (by virtue of having no active buttons assigned that can control the menu), all of which can be controlled with the properties ``MaxAlpha=, FadeInDelay=, FadeInTime=, FadeOutDelay=, FadeOutTime=, InactiveDelay=``, and ``InactiveAlpha=``. All alpha values should be in the range of 0 to 255 (0 fully invisible, 255 fully opaque), and delay times are in milliseconds (1/1000th of a second).

### Alignment

The relative position shortcuts L/R/T/B/C used for Hotspots are also used for the ``Alignment=`` property. For example, if you specified ``R-10`` for a menu's X Position, but the Menu is 50 pixels wide, most of it would end up cut off by the right edge of the screen (only the left 10 pixels of the menu would be shown). Instead, you can use the following to make the *right* edge of the menu be 10 pixels to the left of the right edge of the screen, and exactly centered on the Y axis:

    [Menu.Macros]
    Position = R-10, CY
    Alignment = R, C

### Bitmaps

A bitmap is an uncompressed pixel image format, generally with the file extension .bmp. As mentioned above, HUD elements can be set to ``Type=Bitmap`` and menus can use ``ItemType=Bitmap``, which require specifying the region of a .bmp file to use with ``Bitmap=`` (and optionally ``SelectedBitmap=`` in the case of a menu to make selected item distinctive).

First, any Bitmaps to be used must be named in the [Bitmaps] section, with each have a name and then a path to a file, like so:

    [Bitmaps]
    MyImage1 = "C:\Images\MyBitmap1.bmp"
    MyImage2 = Bitmaps\MyBitmap2.bmp

*The path specified can be a full path or relative to the location of the overlay's .exe file. At this time, only actual .bmp files are supported, not .png's etc).*

Once a Bitmap is set properly as in the above example, set the HUD element's ``Type=`` or menu's ``ItemType=`` to use the bitmap, or a portion of it, like so:

    [HUD.Picture]
    Type = Bitmap
    Bitmap = MyImage1

    [Menu.MyMenu]
    Style = List
    ItemType = Bitmap
    Bitmap = MyImage2: 0, 0, 32, 32
    SelectedBitap = MyImage2: 50%, 0, 50%, H
    
*The bitmap or bitmap region will be scaled as needed to fit into the HUD element's Size or menu's ItemSize dimensions if they do not match.*

### Label Icons

In addition to the backdrop of a menu item, a bitmap be used in place of the text label for a menu item. This allows having a different image for each individual menu item. This image will be copied into the inner area of the menu item, meaning the resulting bitmap will be drawn at (ItemSize.x/y - (BorderSize x 2)) size.

The [Icons] section is used to link each menu item's label text to what should be drawn in place of it. For example:

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

To do this, simply omit the name of a Bitmap in the [Icons] property for the menu item label you want to replace, and have the coordinates be the portion of the game's window you want copied over, like so:

    [Icons]
    Spell2 = 500, 250, 32, 32

These copy-from coordinates work like Hotspots and can include anchor, fixed offset, and scaled offset, such as:

    [Icons]
    Spell2 = R-20, 20%+10-5, 5%, 15

*TIP: When using this feature, it may be desirable to have the copied-from area of the game's window be covered up by a HUD element from the overlay to avoid having the same icons show up in two places at once on your screen (the copy in the overlay + the original icon in the game's built-in UI). This requires an alternate copy method that avoids copying from the overlay itself, getting the icon hidden underneath it instead. Which copy method works may differ from game to game. You can set the method using the [System] property "IconCopyMethod". See the comments in MMOGO_Core.ini for details on possible values for this property.*

### Hotspot & KeyBind HUD Elements

Some special HUD element types offset their visual position to match a hotspot, or a Hotspot Array with the same name as the Key Bind Array. These use ``Type = Hotspot``, ``Type = KeyBindArrayLast`` and ``Type = KeyBindArrayDefault``. They must also have a special property to specify which Hotspot / Key Bind Array / Hotspot Array to use, such as ``Hotspots = TargetGroup`` (``Hotspot=``, ``Array=``, and ``KeyBindArray=`` also work and mean the same thing here).

``Type=Hotspot`` just initially offsets itself by the given hotspot and stays there. ``Type=KeyBindArrayLast`` will change to the position matching the last used key bind from the array. ``Type=KeyBindArrayDefault`` will change to the position of the set *default* key bind of the array (initially the first item in the array, but can be changed with the ``=Set <KeyBindArrayName> Default`` command).

For the key bind array types to work, make sure to create both a hotspot array in a  ``[Hotspots]`` section and a key bind array in a ``[KeyBinds]`` section with matching names and ranges of numbers.

## Other Commands and Features

This final section covers various extra features that don't fit into the main categories above. 

### System commands

These commands affect the overlay app directly rather than the game you are using it with, and can be assigned to menu items or gamepad buttons like any other command.

* ``=Change Profile`` - brings up profile select dialog
* ``=Quit App`` - closes the overlay application

### Hotspot Array and Copy Icon ranges

When defining Hotspot Arrays and regions to copy from a game window to use as icons, it can be a pain to change every individual hotspot/icon associated with in-game UI window when you want to move that UI window in the game. To help with this, these elements can use a base *anchor* element with the other elements defined as just *offsets* to the anchor, meaning they can all be moved at once by only moving the anchor.

To create an anchor, define a hotspot or icon with no number after its name. At that point, any hotspots or other copy-from-target icons with the same name but with a number at the end of the name will be treated as an offset (in the case of icons you must also leave off the width and height for the offset versions). For example:

    [Hotspots]
    LootWindow = 32x240
    LootWindow1 = +0, +0
    # Will actually be at 32x281
    LootWindow2 = +0, +41

    [Icons]
    Spell = 17, 7, 36, 28
    Spell1 = +0, +0
    # Will actually copy from 17, 36, 36, 28
    Spell2 = +0, +29

In addition, to save on typing you can specify multiple hotspot/icon offsets in a single line by using the format ``Name##-##``, with the first number being the first index in the array and the number after the ``-`` being the last index in the array. When using this format, each element in the range (including the first) will be offset from *the previous element in the array*, and ignore the base anchor position (in fact, you do not need to define a base anchor position at all in this case). For example:

    [Hotspots]
    LootWindow = 32x240
    # Define 8-tall left column starting at 32x240 then each 41 apart in Y
    LootWindow1 = +0 x +0
    LootWindow2-8 = +0 x +41
    # Define right column @ 73x240 then 41 each in Y
    LootWindow9 = +41 x +0
    LootWindow10-16 = +0 x +41

    [Icons]
    # Copy from a column of 8 spell icons 29 apart in Y
    Spell1 = 17, 7, 36, 28
    Spell2-8 = +0, +29


*Note that the + signs in front of each number for the offsets don't do anything, I just put them there to help remind me that these are offsets rather than direct positions.*

### Quick remap of gamepad buttons

If you want to completely swap some button assignments in your profile, but not go through every layer/etc to change each button's command assignments one-by-one, you can swap them quickly using the following syntax:

    [Gamepad]
    Triangle = Circle
    Circle = Triangle

The above would swap everything the Circle button and Triangle button do throughout the rest of the profile. The name on the left of the = sign is which button you will actually physically press, and the name on the right is which button you want to act as if it was pressed instead.

### Experimental command: Lock Movement

This acts a potential alternative to ``=AutoRun`` that allows continuous movement in other directions - namely strafing or walking backwards. You could think of it as Auto-Strafe-Run. It looks at what movement commands are currently being held active by other buttons (analog sticks) at the same time the command is executed to "lock" the same movement direction to be automatically held after that point, until cancelled.

For MoveForward, or when not holding any directions, it will use the standard auto-run key (unless there is none defined, then it will hold down the MoveForward key continuously). For turning left/right, it will NOT lock them (so as not to just spin in circles) unless are also in right-click Mouse Look mode at the time (where it assumes the turn keys will cause strafing instead).

This locked movement mode will cancel once release and re-press any direction on the same axis as any locked direction. In other words, if you lock walking backwards, you can press left or right to strafe while continuing to move back automatically, but pressing forward or back again will cancel it. If you lock moving slant, re-pressing any direction will cancel it entirely.

*WARNING: For directions that can't just use the AutoRun key, the game itself may stop you from continuing to move in the direction desired while using the chat message box - particularly if type one of the movement keys (i.e. WASD) as part of your message. To help avoid this, consider re-mapping your movement keys to ones you aren't likely to use while typing.*

### Experimental Mouse Mode: AutoLook

This mouse mode can be used by setting ``Mouse = AutoLook`` in a layer (and no higher layers having a different Mouse mode set). It attempts to emulate modern console game camera control by combining ``=LookTurn`` and ``=LookOnly``. It only works for games that support rotating camera+character by holding the right mouse button on an empty part of the screen, as well as just rotating camera (without affecting character) by holding the left mouse button. It is targeted for use with a 3rd-person camera mode and may not function as expected in some games while in 1st-person.

With this mode the left mouse button will be held while no movement commands are being sent, and the right mouse button will be held while movement commands are also being sent. This allows panning the camera around and viewing your own character from different angles while stationary, yet steering your character while moving, just like in most modern 3rd-person action games on consoles.

The tricky part is while using AutoRun, which the app has no way of knowing for sure your character is still doing or not (there are multiple ways to cancel AutoRun that the app won't necessarily know about). Therefore it assumes that any time you send the AutoRun key that your character begins moving forward, and that your character will continue moving forward until you send MoveBack or release and re-send MoveForward commands, and use the right mouse button during this to allow for steering.

If you prefer, you can use ``Mouse=AutoRunLook`` which instead treats AutoRun (and Lock Movement) the same as being stationary and always uses the LookOnly camera mode whenever you are not actively moving your character. This allows freely looking around for threats while auto-running without changing your direction of movement, but removes the ability to steer your character via mouse movement while auto-running. You could assign each option to different layers and switch between them by holding a button.

### Other system features

As mentioned for first starting up, you can have the application automatically launch a game along with whichever Profile you first load. You can also set the Window name for the target game, so the HUD elements will be moved and resized along with the game window, and force the game window to be a full-screen window instead of "true" full screen if needed so the HUD elements can actually show up over top of the game.

There are various other system options you can set like how long a "tap" vs a "short hold" is, mouse cursor/wheel speed, gamepad deadzones and thresholds, whether this app should automatically quit when done playing the game, and what the name of this application's window should be (so you could set Discord to believe it is a game, which is nice if you want to let people know you are playing EQ since Discord doesn't recognize old EQ clients for some reason). Check the comments in the generated *MMOGO_Core.ini* for more information on these and other settings.

## Contact

Questions? Comments? Concerns? Best bet is the [Discord server](https://discord.gg/btRzWQ4N3N).
