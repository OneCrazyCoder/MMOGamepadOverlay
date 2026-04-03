# MMO Gamepad Overlay

Demonstration video (click to play):

[![MMOGO Demo](http://img.youtube.com/vi/SBlLWR59GGk/0.jpg)](http://www.youtube.com/watch?v=SBlLWR59GGk "MMO Gamepad Overlay app demo")

This application was specifically built to play the classic-style MMO's ***Monsters & Memories*** and ***Adrullan Online Adventures***, as well as the *EverQuest* emulation servers ***Project 1999*** and ***Project Quarm***, using a gamepad. It translates your controller input into keyboard and mouse input, alongside gamepad-controlled menus (displayed in a separate transparent window layered over the game) to enable many game functions with the few buttons available on a gamepad.

The default provided control schemes draw direct inspiration from the only MMORPG ever made exclusively for controller use - ***EverQuest Online Adventures*** for the *PlayStation 2*.

However, it is very highly customizable and can be used with *any* Windows game if you’re looking for a free, open-source, lightweight, portable, and versatile tool for using a gamepad with a game that doesn't natively support one. Just be aware that creating a control scheme from scratch for a new game requires a bit of technical know-how, as it involves editing plain-text files.

This application does ***NOT***:
- Require (or even include) an installation process  
- Modify game files in any way  
- Modify, or even directly read, game memory (RAM)
- Use any .dll code injection (overlays are separate external windows)
- Use any libraries beyond the basic Windows API  
- Expect or solicit payments or donations  
- Use any proprietary or hidden source code

## Download

Get the latest release [here](https://github.com/OneCrazyCoder/MMOGamepadOverlay/releases), including 32-bit (x86) and 64-bit (a64) versions.

*Optional: You will also find there custom UI file packs for some games with the custom profile I personally use for them, made for even better integration between the game and the overlay.*

## Quick Start Guide

1. Unzip the download and place the contained `.exe` wherever you want.
   - Note that it will save some `.ini` files into the same folder you put it in!
2. Launch the saved .exe and select one of the example profiles (or create your own!)
3. Follow the prompts for options like auto-launching the game
4. Launch your game and play it using a gamepad!

For basic control scheme when using an example profile, select Edit->Text Files and open the _Default.ini file listed. At the top of the file should be a large block comment explaining the controls for that game. More comments and details for the default control scheme are included throughout this file.

## Customization

Want to tweak a default control scheme or even build your own? See the [Custom Profile Editing Guide](https://onecrazycoder.github.io/MMOGamepadOverlay/profile-edit-ref.html).

## Contact

Questions? Comments? Concerns? Try the [Discord server](https://discord.gg/btRzWQ4N3N) or post on GitHub Discussions.
