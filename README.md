# MMO Gamepad Overlay

Download links for latest built version:
[Windows 32-bit](https://bitbucket.org/TaronM/mmogamepadoverlay/downloads/MMOGamepadOverlay-x86.zip)
[Windows 64-bit](https://bitbucket.org/TaronM/mmogamepadoverlay/downloads/MMOGamepadOverlay-a64.zip)

This application translates game controller input (via DirectInput and/or XInput) into keyboard and mouse input (via the SendInput() Win32 API function), possibly with HUD elements (drawn on a transparent overlay window over top of a game's window) to help visualize what various buttons may do (particularly with the use of customizable on-screen menus).

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

**NOTE: The rest of this file explains how to customize your control scheme by editing .ini files using a text editor. This may seem daunting, but keep in mind that you don't actually have to do this much, or at all, if the default Profiles provided (or maybe shared by other users) work for your needs!**

The application generates a *MMOGO_Core.ini* file which contains some default settings and is used to track what other profiles you have created and their names. You can edit this file and any other *.ini* files it generates with any text editor, or create your own, once you know how they work.

The list of profiles is at the top of *MMOGO_Core.ini*, along with an entry for specifying which one to load automatically, if any. Each Profile *.ini* file can specify a "parent" Profile with a line like:

    ParentProfile = MyBaseProfile
This system is intended to allow for having a "base" profile for a particular game, and then multiple profiles for different characters that use that same base as their parent. You can set up as long of "chain" of parent profiles as you desire, however. It is not necessary to specify the "Core" profile as a ParentProfile at any point, as it will always be loaded first anyway. Profiles are loaded in order from parent to child, and any duplicate properties are overwritten as they are encountered. That means the main profile you are loading will take priority over its parent base, which itself will take priority over any parent it has, and all other files will take priority over "Core".

All of this is set up automatically with the default example profiles generated on first launch.

You can edit MMOGO_Core.ini yourself to add more profiles, or use the menu option File->Profile from within the application to do so with a GUI.

## Profile customization

Each Profile is a *.ini* file (and possibly one or more parent *.ini* file(s) as explained above). These are plain-text files you can edit in Notepad, or whatever you prefer, that contain a list of *properties*. Each property is identified by a *property category* and a *property name* with an associated *property value*.

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

*NOTE: Comments are only supported by placing # and ; at the beginning of a line, you can not add comments at the end of a line, it will instead be considered part of the Property Value.*

## [Scheme] Category
This is the main category for determining how Gamepad input is translated into keyboard and mouse input. With a couple of special exceptions, each *property name* in this category represents a gamepad button (and optionally an action associated with that button like *press*, *tap*, *release*, etc), and each *property value* represents a keyboard key, mouse button, mouse movement, or special command that should be triggered while pressing that button. For example, to assign R2 to act as the right mouse button, you could include:

    [Scheme]
    R2 = RMB
There are various ways supported of specifying gamepad buttons and keys, so you could instead use:

    [Scheme]
    RT = Right-click

If you want the full list, check *Source\GlobalConstants.cpp* in the source code. Note also that you can sometimes assign 4 buttons at once in the case of the D-pad, analog sticks, and face buttons, to certain commands that support it. For example:

    [Scheme]
    DPad = MoveTurn
    LStick = MoveStrafe
    RStick = Mouse
    # Below treats face buttons like a D-pad
    FPad = Move

When the button is specified by itself, it is treated as the action "press and hold", meaning that in the earlier examples, the right mouse button will be pressed and held for as long as R2 is pressed and held. Other actions can be specified instead, and each button can have multiple commands assigned to it at once such as for a tap vs a hold. For example:

    [Scheme]
    R2 = A
    Press R2 = B
    Tap R2 = C
    Release R2 = D
    Hold R2 = E
    Long Hold R2 = F

This is the maximum that could be assigned to one button. In the above example, when R2 is first pressed, 'A' and 'B' keyboard keys would be sent to the game ('A' would be held down but 'B' would just be tapped). If R2 was quickly released, a single tap of the 'C' key would be sent. If R2 was held for a short time, an 'E' tap would be sent once. If R2 was still held for a while after that, a single 'F' tap would be sent as well. No matter how long it is held, even if just briefly tapped, once let go of R2 a single tap of 'D' would be sent to the game, as well as finally releasing 'A'.

Notice how only the button name by itself can be assigned to "hold" a key. All other button actions can only "tap" a key. Certain commands, and *key sequences*, can't be "held" anyway, so assigning one of these to just the button name by itself will make it act the same as the *Press* action (meaning can have 2 *Press* commands on the same button in these cases).

## Sent input and key sequences

In addition to mouse buttons, mouse movement, and keyboard keys, you can also send combination keys using the modifier keys Shift, Ctrl, and Alt, such as:

    R2 = Shift+A
    L2 = Ctrl X
    L1 = Ctrl-Alt-R
These can still be "held" as if they are single keys.

*WARNING: Modifier keys should be used sparingly, as they can interfere with or delay other keys. For example, if you are holding Shift+A and then want to press just 'X', since the Shift key is still being held down, the game would normally interpret it as you pressing 'Shift+X', which may be totally different command. This application specifically avoids this by briefly releasing Shift before pressing X and then re-pressing Shift again as needed, but this can make the controls seem less responsive. Consider re-mapping controls for the game to use Shift/Ctrl/Alt as little as possible for best results!*

You can also specify a sequence of keys to be pressed. For example, you could have a single button press the sequence Shift+2 (to switch to hotbar #2), then 1 (to use hotbutton #1), then Shift+1 (to switch back to hotbar #1), like so:

    R2 = Shift+2, 1, Shift+1

Key sequences can NOT be "held", so holding R2 vs just tapping it will give the same result in the above example.

You can also add delays (specified in milliseconds) into the sequence if needed, such as this sequence to automatically "consider" a target when changing targets

    # 'Delay' or 'Wait' also work
    R1 = F8, pause 100, C
    
*WARNING: Do not use this to fully automate complex tasks, or you're likely to get banned from whichever game you are using this with.*

On a more advanced note, you can also request in the sequence to jump the mouse cursor to a named *hotspot* location to click on it, such as:

    [Hotspots]
    CenterScreen = 50%, 50%

    [Scheme]
    R1 = Point to CenterScreen->LClick

## KeyBinds (aliases)

KeyBinds are basically just aliases or shortcuts for inputs that should be sent by gamepad buttons. Using KeyBinds, instead of saying:

    [Scheme]
    XB_A = Space

You would instead say:

    [KeyBinds]
    Jump = Space

    [Scheme]
    XB_A = Jump

In this example it may not seem worth the effort, but it can be convenient for assigning a KeyBind alias to a full sequence of keys, or for just making your [Scheme] more readable and easily updating your controls in one place if you re-map them within the target game.

There are also some KeyBinds that are specifically checked for and used by the application for certain commands - namely for character movement and self/group targeting, as explained in more detail later.

## Controls Layers
To really unlock the full range of actions in an MMO using a Gamepad, you will need to use Controls Layers. These Layers change what commands are assigned to what buttons while the Layer is active. You can have multiple Layers added at once, and for any given button, the top-most Layer's assignments will take priority. Newly-added Layers are placed on "top" of all previous layers and the base [Scheme].

Layers can be added with the Add Layer command and removed with the Remove Layer command assigned to a gamepad button action. Alternatively, they can be "held" by just using the "Layer" command assigned to a button, which means that the Layer will be added when the button is first pressed, and then automatically removed when the button is released. For example:

    [Scheme]
    # Add "Alt" layer as long as L2 is held
    L2 = Layer Alt
    # Add "MouseLook" layer until another command elsewhere removes it
    R3 = Add Layer MouseLook

If the same Layer is added when it is already active, it just moves that Layer priority to be the top layer instead of adding another copy of it.

Layers are defined the same as [Scheme], with just the category name [Layer.LayerName] instead. So for the above example, you could add:

    [Layer.Alt]
    # L2+Triangle = Jump
    Triangle = Jump

    [Layer.MouseLook]
    MouseLook = On
    # If don't specify Layer name, assume mean remove self
    R3 = Remove Layer
With the above setup, R3 will act as a toggle button turning MouseLook mode on and off, and L2+Triangle will cause the character to jump (via Spacebar).

### The "Auto" Button
Each Layer also has a special 'virtual button' unique to it, that can be assigned commands like any real Gamepad button. This button is called "Auto". It is "pressed" whenever the Layer is added, and then "released" whenever the Layer is removed. This can be particularly useful to assign a button to simultaneously 'hold' a Layer while also holding a key, by having the Layer hold the key instead using its "Auto" button.

For example, let's say you wanted to make pressing and holding Circle on a PS controller act the same as holding the left mouse button, but you also want to make it so while holding Circle, you could use your left thumb on the D-pad to move the cursor around to "drag" the mouse, even though normally the D-pad is used for character movement. You could accomplish this as follows:

    [Scheme]
    D-Pad = Move
    Circle = Layer MouseDrag

    [Layer.MouseDrag]
    Auto = LMB
    D-Pad = Mouse

With this setup, pressing Circle will add the MouseDrag layer, which will click and hold the left mouse button for as long as the layer is active via Auto, while also changing the D-Pad to control the mouse. Releasing Circle will remove the layer, restoring the D-Pad to character movement instead and releasing the left mouse button (since Auto is "released" when the layer is removed).

You can even assign commands to ``Release Auto=`` and ``Tap Auto =`` and so on, like any real button.

### Layer Includes
To save on copying and pasting a lot, each Layer can also "include" the contents of another Layer, if they are mostly the same anyway. For example, this:

    [Layer.MyLayer]
    L1 = Target NPC
    R1 = Target PC
    PS_X = Attack

    [Layer.MyOtherLayer]
    Include = MyLayer
    # Overrides R1 = Target Pc from MyLayer
    R1 = Jump
    R2 = RMB

Would be the same as typing out this:

    [Layer.MyLayer]
    L1 = Target NPC
    R1 = Target PC
    PS_X = Attack

    [Layer.MyOtherLayer]
    L1 = Target NPC
    R1 = Jump
    PS_X = Attack
    R2 = RMB

### Other Layer Properties

In addition to changing button assignments temporarily while they are active, each Layer has a few other properties. This includes turning MouseLook on or off automatically (i.e. holding the right-mouse button down) using ``MouseLook=On`` or ``MouseLook=Off``.

More significantly, each Layer specifies which HUD elements (including Menus) should be visible while that Layer in active. By default, all HUD elements specified by all active Layers (and [Scheme], which can also specify HUD elements to show by default) are shown, however, Layers can also specifically *hide* HUD elements from lower layers, stopping them from being shown (unless yet another, higher layer overrides it by showing it again). This is done via the ``HUD=`` property including a list of HUD element names to show (and optionally the 'Show' and 'Hide' key words), such as:

    [Layer.Macros]
    HUD = MacroMenu
    
    [Layer.MouseLook]
    MouseLook = On
    HUD = Show Reticle
    
    [Layer.TopMost]
    HUD = Hide MacroMenu, Show GroupTarget

## Menus
While it is possible to use Layers alone to send all the input needed to an MMO, it would require a lot of complex button combinations and sequences you'd need to memorize. Menus can make things a lot easier, by instead assigning buttons to add/remove/control menus and then having the menus including various commands, macros, etc.

Each Menu also counts as a HUD element, so the Menu must be made visible by the ``HUD=`` property for an active Layer or in [Scheme] to actually see it.

Each Menu has a ``Style=`` property that determines its basic structure and appearance, as well as visual properties like colors, shapes, etc of the menu items (more on the visuals later). The menu must also have menu items assigned to it of course, which include a label and a command, and possible sub-menus.

Menus are defined using the category name [Menu.MenuName]. Each Menu Item is defined by a property name that depends on that Menu's style. The property value for each Menu Item contains a label followed by colon ``:`` followed by a command to execute (input to send to the target game). Here's an example of a 4-directional Menu, which emulates the style used by EQOA Macros when you pressed L2:

    [Menu.Macros]
    Style = 4Dir
    Position = 50%, 10
    U = Book: B
    L = Skills: K
    R = Ability 2: 2 
    D = Undefined:

Notice how the Down menu item has no command, but still has a colon, so the label will be shown but nothing will happen if you choose that Menu Item.

*Currently no other menu styles are implemented, so will need to update this later to talk about how menu items are specified in other menu styles*

### Sub-Menus

A sub-menu is created by having a Menu Item without the colon specified, thus being just a label. The sub-menu is defined by the category name ``[Menu.MenuName.SubMenuName]``. You can have multiple nested sub-menus, but need to specify the entire "path" as the category name when defining them, such as ``[Menus.Macros.Group.Creation]``. Sub-menus should only specify Menu Items - things like ``Style=`` as well as ``Position=`` and other visible HUD properties will be ignored for all but the "root" menu ("Macros" in this example).

So for macros in the style of EQOA, you could define a Menu like this:

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

### Chat box macros

Note that in the above example, the actual commands are slash commands and chat messages. This is another option in addition to keys and key sequences that can be assigned as a command. Slash commands start with ``/`` and chat messages start with ``>`` (the '>' is replaced with the Return key to switch to the chat box when the command is actually executed). These commands will actually "type" the sequence into the chat box as a series of keyboard key presses, followed by pressing Return to send the macro. This will lock out most other inputs while typing though, so in general it is better to instead create macros using the in-game interface and activate them via key sequences that press "hotbuttons", like ``= Shift+2, 4`` instead.

### Controlling menus

To actually use a Menu, you will need to assign Menu-controlling commands to buttons in ``[Scheme]`` or a ``[Layer.]``.  These commands must specify the name of the Menu they are referring to. The main commands are ``Reset`` (exits sub-menus to return to root menu), ``Select``, ``Confirm``,  and ``Back``.

Here is an example of controlling the 4-Dir style Macros menu example from earlier using L2+DPad

    [Scheme]
    L2 = Layer Macros

    [Layer.Macros]
    HUD = Macros
    Auto = Reset Macros
    DPad = Select Macros

*Note that this example does not cover the 'and Close' option or Confirm or Back since those aren't used in the only style currently implemented, the 4-Dir style*

## HUD Elements

As mentioned above, Menus are also HUD Elements. However, you can have additional HUD Elements that are not Menus. These are created with the category name ``[HUD.HUDElementName]``. You can use this to create a reticle in the middle of the screen while in MouseLook mode, to aim better with the "Use CenterScreen" key, for example, as well as special HUD Elements like the Group Target indicator.

Default properties used by all HUD Elements and Menus can be defined in the base ``[HUD]`` category, saving having to specify them for every individual instance. The exception being the ``Position=`` property, which should be specified for each individual HUD element and Menu.

Like ``Style =`` for a Menu, each HUD element must specify a ``Type =`` entry. Available types include: Rectangle, Rounded Rectangle (needs ``Radius=`` as well), Circle, Bitmap (needs a ``BitmapPath=`` property specifying the .bmp file to use), and ArrowL/R/U/D. These are also used for Menus for the ``ItemType=`` which determines how the background for each Menu Item is drawn.

There are also special HUD types "GroupTarget" and "DefaultTarget", explained further down in relation to the Target Group special commands.

In addition, other properties can be defined that set the size and colors used, including Size and/or ItemSize, Alignment, Font, FontSize, FontWeight, BorderSize, LabelRGB, ItemRGB, BorderRGB, and TransRGB (which color is treated as a fully transparent "mask" color).

HUD Elements can also fade in and out when shown or hidden, which can be controlled with the properties MaxAlpha, FadeInDelay, FadeInTime, FadeOutDelay, FadeOutTime, InactiveDelay, and InactiveAlpha. All alpha values are 0 to 255, and times are in milliseconds (1/1000th of a second).

## Hotspots and positions
Hotspots (positions on the screen of significance, such as where a mouse click should occur) and things such as HUD Element positions and sizes, are specified as X and Y coordinates with each possibly having a relative and/or absolute value. The relative value is related to the size of the target game's window/screen size, and the absolute value is in pixels. They can be specified in the format ``relativeValue% +/- absoluteValue`` and you can optionally specify only one or both. Some relative value's can be specified by shortcuts like L/T/R/B/C/CX/CY instead of numbers.

Some accepted examples of valid positions for reference:

    # Center of the screen/window
    = 50% x 50%
    = 0.5, 0.5
    = CX CY
    # 10 pixels to the left of right edge
    # 5 pixels down from 30.5% of the game window's height
    = R - 10, 30.5% + 5
    # Pixel position 200 x 100 regardless of target size
    = 200 x 100

Note that L/R/T/B/C are also used for the ``Alignment=`` property for HUD Elements and Menus. For example, if you specified ``R-10`` for the position, but the Menu is 50 wide, most of it would end up cut off because the left edge of the menu would be at R-10. Instead, you can use the following to make the *right* edge of the menu be 10 pixels to the left of the right edge of the screen, and exactly centered on the Y axis:

    [Menu.Macros]
    Position = R-10, CY
    Alignment = R, C

## Other assignable commands
In addition to keyboard and mouse input, key sequences, and commands for adding/removing Layers and managing Menus, there are some other special-case commands you can assign to buttons and menu items.

### Character movement commands
These include Move, which is the same as MoveTurn, and MoveStrafe, which can be conveniently assigned to the DPad or an analog stick in a single property. Which actual key is pressed may changed depending on whether or not MouseLook mode is turned on (via ``MouseLook = On`` property set on an active Layer). These require the KeyBinds be assigned for MoveForward, MoveBack, TurnLeft, and TurnRight at minimum, as well as optionally StrafeLeft and StrafeRight if want to use the MoveStrafe option. All of these can have a MouseLook version of them as well for which key to use in MouseLook mode (if it is different), such as the notable common example ``MouseLookMoveForward = LClick``.

### Target Group commands

These commands allow "relative" group targeting. Most MMO's have a specific key assigned to target each group member (and yourself), but with limited buttons, it can be helpful to have buttons for "cycling" through group members rather than having a dedicated button for each group member. To facilitate this, the application remembers the group members "Last", "Origin", and "Default" (i*n all cases, your own character is considered "Group Member #0" and the default setting*) and are used and updated by the following commands:

*NOTE: These first 2 do NOT actually target anyone in the game*
* **Target Group Reset** - Sets just "Origin" to "Default"
* **Target Group Set Default** - Sets "Default" to "Last"

*NOTE: The rest of these all set "Last" and "Origin" to match whomever was targeted*
* **Target Group Default** - Targets "Default"
* **Target Group Last** - Targets "Last" (which may target their pet if they were already targeted)
* **Target Group Next** - Targets "Origin" + 1 group member
* **Target Group Prev** - Targets "Origin" - 1 group member
*  **Target Group Next Wrap** - Same as Next but wraps back to first if used when "Origin" = max
* **Target Group Prev Wrap** - Same as Prev but wraps to max if "Origin" = self (0)

In order for these to work properly, you will need the KeyBinds defined for ``TargetSelf =`` and ``TargetGroup1 =``, ``TargetGroup2=``, etc, up to however many other group members the game supports.

When using these, any HUD Elements with ``Type = GroupTarget`` will be moved to a position corresponding to "Last", and any with ``Type = DefaultTarget`` will be moved to a position corresponding to "Deafult", to help visualize which group member will be targeted next by use of these commands. You will need to define these positions to match how you have your game's UI set up, by adding them under the category ``[Hotspots]`` and using the same names as the aforementioned KeyBinds (TargetSelf and TargetGroup#).

## Other system features
As mentioned for first starting up, you can have the application automatically launch a game along with whichever Profile you first load. You can also set the Window name for the target game, so the HUD Elements will be moved and resized along with the game window, and force the game window to be a full-screen window instead of "true" full screen if needed so the HUD Elements actually show up over top of the game. There are various other system options you can set like how long a "tap" vs a "short hold" is, what the name of the application's window should be (so you could set Discord to believe it is a game, since it refuses to recognize old EQ clients as one, which is nice if you want to let people know you are playing EQ), and so on. Check the comments in the generated *MMOGO_Core.ini* for more information on these settings.
