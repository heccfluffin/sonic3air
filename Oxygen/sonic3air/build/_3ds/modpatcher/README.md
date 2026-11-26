# Sonic 3 A.I.R. 3DS Mod Patcher

A small tool for installing and managing Sonic 3 A.I.R. mods on your 3DS SD card.
On Windows, use `s3air_3ds_modpatcher.exe` from the release page: it has Python built in, nothing to install (Windows may
warn about an unknown program the first time: **More info** -> **Run anyway**). Elsewhere, `s3air_3ds_modpatcher.py`
needs Python 3.8 or newer and nothing else.

## Using the window

1. Put your 3DS SD card in your computer.
2. Double-click `s3air_3ds_modpatcher.exe` (or `s3air_3ds_modpatcher.py`, or run `python3 s3air_3ds_modpatcher.py`).
3. The SD card is detected automatically. If not, click **Browse...** and pick it.
4. Click **Add mod (.zip)...** or **Add mod folder...** to install mods.
5. Use **Enable / Disable**, **Move up** and **Move down** to choose which mods are on.
   When two mods change the same thing, the one higher in the list wins.
6. **Check SD card** tells you if the game, the data files or the ROM are missing.

## What it does for you

- Finds the `mod.json` inside a download, even when the zip has an extra folder around it
- Unpacks zips into folders, because the 3DS doesn't have enough memory for zipped mods
- Warns about things the 3DS port can't do: custom shaders, very large images, very large music packs
- Copies the mod to `3ds/sonic3air/savedata/mods/` and turns it on
- Writes the same `active-mods.json` file as the game's own Mods menu, so both stay in sync
- Reads `mod.json` the way the game does, so mods with comments or a trailing comma in it install fine

## Safe copying

Copying to an SD card can take a while, and cards get full or pulled out. The patcher makes sure that never leaves
you with a broken game or a lost mod:

- **Progress bar:** big copies run in the background with a progress window, so Windows never shows "Not Responding".
- **Nothing half-copied counts:** every file is copied under a temporary name and only gets its real name once it's
  complete and has the right size.
- **Updating a mod:** the new version is copied next to the mods folder first. Only once it's complete does it take the
  place of the old one, so if anything goes wrong, the old version is still there.
- **Cut off anyway?** The next time the patcher starts, it cleans up: it puts back an old mod version that was in the
  middle of being replaced and removes half-copied files, and tells you what it did.
- **Free space** is checked before copying anything.
- **Check SD card** also checks the size of the game data files, so a damaged or incomplete copy gets caught.
- **Wrong A.I.R. version?** The message links straight to the download of the version the port needs.
- **Two SD cards plugged in?** Pick the right one from the drop-down at the top.
- `.rar` and `.7z` mods can't be unpacked by the patcher: unpack them on your computer and use **Add mod folder...**, or
  install them on the 3DS itself (GameBanana browser or Wi-Fi install in the Mods menu).

## Command line

```bash
python3 s3air_3ds_modpatcher.py --sd E:/ check-sd
```

```bash
python3 s3air_3ds_modpatcher.py --sd E:/ install SomeMod.zip AnotherMod.zip
```

```bash
python3 s3air_3ds_modpatcher.py --sd E:/ list
```

Other commands: `check <zip or folder>`, `enable`, `disable`, `up`, `down` and `remove <mod folder>`.
Leave out `--sd` to auto-detect the SD card.


## Setting up the game files

**Set up game files...** (or `setup --air <zip> [--rom <file>]` on the command line) copies the `data` folder from the
official Sonic 3 A.I.R. download (version 26.03.28.0, as .zip or unpacked folder) and your Sonic 3 & Knuckles ROM to the
SD card. The ROM is searched in your Steam library if you don't pick one, checked with the same size and checksum test
the game uses, and saved as `Sonic_Knuckles_wSonic3.bin` whatever its original name was.
**Check SD card** lists what's in place, including the sound firmware (`3ds/dspfirm.cdc`).


## Conflicts between mods

Whenever you install or enable a mod, the patcher checks it against the other enabled mods, with the same rules as the
game's Mods menu (from the mods' `mod.json` "OtherMods" entries) plus one extra check:

- **Conflict:** a mod says it doesn't work together with another enabled mod -> asks whether to disable the other one
- **Missing mod:** a mod requires another mod that isn't installed or not enabled
- **Order:** a mod needs to be placed above or below another one in the list
- **Version:** a mod needs a newer version of another mod
- **Same files:** two enabled mods replace the same files (sprites, audio, ...): only the higher one in the list is used
  for those -> asks whether to disable the other one (scripts don't count, all mods' scripts get loaded)

Mods with problems are marked with a ⚠ in the list; **Check conflicts** shows all of them.
Command line: `conflicts` lists them, `install --disable-conflicting` / `enable --disable-conflicting` turn the other mods off.
