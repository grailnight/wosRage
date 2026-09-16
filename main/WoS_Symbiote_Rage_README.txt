WoS Symbiote Rage 1.0.0
Author: grailnight

A Rage mode for the black suit in Spider-Man: Web of Shadows (PC v1.1).
Press R: Spider-Man grabs his head like in Spider-Man 3, symbiote tendrils
burst from his shoulders, and for 20 seconds his ground combos become the
black suit's wall pounds with tendril effects. Hold the left mouse button
for Tendril Whip.


REQUIREMENTS
- Spider-Man: Web of Shadows PC, version 1.1 (32-bit, Direct3D 9).
- Keyboard and mouse controls with attack on the left mouse button.
- No other mods are required.


INSTALLATION
1. Extract the archive into the game folder, next to
   Spider-Man Web of Shadows.exe. You should get:
     WoS Rage Loader.exe
     WoS_Rage\WoS_Rage.dll
     WoS_Rage\WoS_Rage.ini
2. Start the game with "WoS Rage Loader.exe" instead of the game EXE.
   If the game is already running, start the loader anyway: it attaches
   Rage to the running game.
3. Load a save and press R.

No game files are replaced. Without the loader the game starts vanilla.


CONTROLS
R                       Start Rage (20 seconds).
Left mouse (click)      Ground attacks become black-suit wall pounds:
                        single left/right, double and triple pound.
Left mouse (hold)       Tendril Whip, repeated while held.

Standing still, R first plays the SM3 head grab while the tendrils grow;
a symbiote shockwave and tendril burst mark the moment Rage begins.
Pressing R on the move starts Rage at once. In the red suit, R switches
to the black suit first using your own suit key from the game settings.

During Rage:
- tendrils sway on the upper back and lash forward on attacks;
- every blow throws a symbiote splash, a tendril burst, a ground
  shockwave, a flash and a fist trail;
- ichor drips from the tendrils and stains the ground;
- a RAGE bar with the remaining seconds is shown above the bottom-right HUD.


SETTINGS
WoS_Rage\WoS_Rage.ini: activation key, duration, automatic suit switch,
head-grab length, Tendril Whip hold time. Restart the game after editing.


UNINSTALL
Delete "WoS Rage Loader.exe" and the WoS_Rage folder.


KNOWN LIMITATIONS
- Ground attacks only. Air and wall attacks are unchanged.
- Rage changes the black suit's attacks in memory only; nothing is saved.
  Restarting the game removes every change.
- The wall-pound animations use the ground attacks' timing, damage and
  sound. The game's own hit sparks are replaced by the mod's effects.
- Tendril Whip on hold works by re-clicking for you; the first attack of
  a hold is still a pound.
- The loader starts the game EXE directly. If you launch through another
  launcher or with a mod that needs its own EXE, start the game that way
  first, then run the loader to attach.
- Mods that replace Spider-Man's animation set (Spiderman_rvb.als) can
  make Rage disable itself. The reason is written to WoS_Rage\WoS_Rage.log.
- The loader loads a DLL into the game process, which some antivirus
  programs flag. The full source is published with the mod.


TROUBLESHOOTING
Check WoS_Rage\WoS_Rage.log. A normal start contains
"WoS_Rage READY" and, after a save is loaded, "Scan: ALS copies=...".
"Signature mismatch" means a different game version: the mod stays off.


CREDITS
Uses only the game's own animations, materials and effect functions at
runtime. No game assets are included in this archive.
