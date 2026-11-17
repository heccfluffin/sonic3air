# Sonic 3 A.I.R. on New 3DS — install guide

This is an unofficial port of [Sonic 3 A.I.R.](https://sonic3air.org) to the **New Nintendo 3DS / New 3DS XL /
New 2DS XL**. It does not run on the original (Old) 3DS / 2DS models, which don't have enough memory and CPU speed.

The port contains **no game content**. You need two things the port can't include:
your own copy of the game's ROM, and the data files from the official A.I.R. PC release.


## What you need

| What | Where it comes from |
|---|---|
| A New 3DS / New 3DS XL / New 2DS XL with custom firmware (Luma3DS) | Follow the official guide at [3ds.hacks.guide](https://3ds.hacks.guide) |
| `sonic3air.cia` (or `sonic3air.3dsx`) | From this port's download / GitHub Actions build |
| The `data` folder of Sonic 3 A.I.R. **version 26.03.28.0** | The Windows download from [sonic3air.org](https://sonic3air.org) |
| The game ROM, named **exactly** `Sonic_Knuckles_wSonic3.bin` | Your own copy of the game on Steam (see step 3) |
| `dspfirm.cdc` (needed for sound) | Dumped from your own console (see step 1) |


> **`3ds` is not the same folder as `Nintendo 3DS`.** All paths in this guide are in the folder named `3ds`
> (lowercase) in the top level of the SD card. `Nintendo 3DS` is the console's own system data: leave it alone.
> If there's no `3ds` folder yet, create it next to `Nintendo 3DS`.


## Step 1: Sound firmware (one time only)

Homebrew needs a dump of the console's sound firmware, otherwise the game has no audio.
If you have played other homebrew games with sound, you most likely have it already:
check that the file `sdmc:/3ds/dspfirm.cdc` exists on your SD card.

If it's missing: run the **DSP1** homebrew app once (it's in Universal-Updater, or on GitHub as "DSP1").
It creates `sdmc:/3ds/dspfirm.cdc` for you.


## Step 2: Game data files

1. Download Sonic 3 A.I.R. for Windows from [sonic3air.org](https://sonic3air.org).
   **It must be version 26.03.28.0**, the same version this port is built from.
2. Unpack the download on your PC and find its `data` folder.
3. Copy the whole `data` folder to `sdmc:/3ds/sonic3air/data/` on your SD card.

The folder must contain at least these files:

```
data/
├── audiodata.bin
├── enginedata.bin
├── gamedata.bin
├── metadata.json
└── scripts.bin
```

`audioremaster.bin` is optional (the remastered soundtrack). If the PC release has it, you can copy it as well.
You don't need any other files from the PC download: the `.exe`, `.dll` and `config.json` files are not used.


## Step 3: The game ROM

> **The ROM file must be named exactly:**
>
> ```
> Sonic_Knuckles_wSonic3.bin
> ```
>
> Copy the name from above to avoid typos: capital **S** and **K**, underscores (`_`) instead of spaces,
> a lowercase **w** before `Sonic3`, and the ending `.bin`.
> The game only looks for this name, so a ROM called e.g. `Sonic 3 & Knuckles.bin` or `s3k.bin` is not found
> until you rename it.

Sonic 3 A.I.R. needs the Sonic 3 & Knuckles ROM from the Steam release of the game.
Other ROM dumps get rejected because their checksum doesn't match, even when they have the right name.

If you own the game on Steam, the file is in the game's install folder, for example in the
**"SEGA Mega Drive & Genesis Classics"** collection under `uncompressed ROMs`.
(In Steam: right-click the game → Manage → Browse local files.)

Copy it to `sdmc:/3ds/sonic3air/Sonic_Knuckles_wSonic3.bin`.

**Windows tip:** Windows hides known file endings by default. If you rename the file there and type `.bin`
yourself, it can end up as `Sonic_Knuckles_wSonic3.bin.bin`, which the game doesn't find.
Turn on File Explorer → View → "File name extensions" to see the full name.

Please don't share this file with others: it's the copyrighted game itself, and everyone needs their own copy.


## Step 4: Check the SD card layout

When you're done, your SD card should look like this:

```
SD card
└── 3ds/
    ├── dspfirm.cdc                       ← step 1 (sound)
    └── sonic3air/
        ├── data/                         ← step 2
        │   ├── audiodata.bin
        │   ├── enginedata.bin
        │   ├── gamedata.bin
        │   ├── metadata.json
        │   └── scripts.bin
        ├── Sonic_Knuckles_wSonic3.bin    ← step 3
        └── savedata/                     ← created by the game on first start
```

The game creates `savedata/` itself. Your settings, save games, the log file and mods go there.


## Step 5: Install and start

**With Universal-Updater (easiest, and gets you updates):**

1. Open **Universal-Updater** on the 3DS (get it from [its releases](https://github.com/Universal-Team/Universal-Updater/releases) if you don't have it).
2. Open the store list (the "UniStore" menu) and add a new UniStore, either by scanning this QR code with the camera:

   ![Sonic 3 A.I.R. store QR code](universal-updater/store-qr.png)

   or by entering this URL:

   ```
   https://github.com/heccfluffin/sonic3air/releases/latest/download/sonic3air-3ds.unistore
   ```
3. Select the new store "Sonic 3 A.I.R. for New 3DS", pick **sonic3air.cia (recommended)** and install it.
4. Start **Sonic 3 A.I.R.** from the HOME Menu.

To update later: open the store in Universal-Updater. When a new version is out, it shows up there;
install it the same way. Your saves and settings are kept.
Universal-Updater only installs the app: the `data` folder and the ROM (steps 2 and 3) stay your job.

**CIA with FBI:**

1. Copy `sonic3air.cia` anywhere on the SD card, for example to `sdmc:/cias/`.
2. Open **FBI** on the 3DS → SD → find the file → "Install and delete CIA" (or "Install CIA").
3. Start **Sonic 3 A.I.R.** from the HOME Menu.

The CIA is recommended because it gets the New 3DS's full memory and CPU speed.

**3dsx (alternative):** copy `sonic3air.3dsx` to `sdmc:/3ds/sonic3air/` and start it from the Homebrew Launcher.
When started this way, the game may not get as much memory, so the CIA is the better choice.

**Updating:** use Universal-Updater (see above), or install the new CIA the same way. It replaces the old version, and your saves and settings
in `savedata/` are kept.


## Controls

| 3DS | In the game |
|---|---|
| Circle Pad / D-Pad | Move |
| A, B, X, Y | Jump (all of them); in menus A confirms and B goes back |
| START | Pause / start |
| SELECT | Back |
| L / ZL, R / ZR | L and R |


## Mods

Use the mod patcher (`s3air_3ds_modpatcher.py`, see its README) with the SD card in your PC.
It copies mods to the right folder, unpacks zip files, and turns them on.
You can also copy mod folders to `sdmc:/3ds/sonic3air/savedata/mods/` yourself and enable them in the game's Mods menu.

Prefer unpacked mod folders over `.zip` files on the 3DS: the game keeps everything it reads from a zip in RAM,
which is limited on the console. The patcher unpacks zips for you.


## Troubleshooting

| Problem | What to do |
|---|---|
| "Game data not found" | The `data` folder is missing or in the wrong place: it must be `sdmc:/3ds/sonic3air/data/` (step 2). |
| "Failed to load 'scripts.bin'" | The data files are from another game version. Use the data from **version 26.03.28.0** (step 2). |
| The game asks for the ROM / "ROM could not be loaded" | Check that the file is named **exactly** `Sonic_Knuckles_wSonic3.bin` (not `.bin.bin`), is directly in `sdmc:/3ds/sonic3air/`, and comes from the Steam version (step 3). |
| No sound | `sdmc:/3ds/dspfirm.cdc` is missing (step 1). |
| The game doesn't start at all on an Old 3DS / 2DS | Only New 3DS models are supported. |
| Anything else | The game writes a log to `sdmc:/3ds/sonic3air/savedata/logfile.txt`. Include it when you report a problem. |


## Playing in an emulator (Citra / Azahar)

1. Turn on **New 3DS mode** in the emulator's system settings.
2. Open the emulator's SD card folder (File → Open Citra Folder → `sdmc`).
   Inside it, create the `3ds/sonic3air/` folder and copy the files there exactly as in step 4.
3. Install the CIA via File → Install CIA, then start it from the game list.

Performance in emulators depends a lot on the emulator's CPU clock setting and on your PC,
so it doesn't tell you how fast the game runs on a real console.


## Current state of the port

- Development builds show a performance profiler on the bottom screen. It doesn't change how the game plays.
- Rendering is done mostly on the CPU; the 3DS's GPU shows the finished picture.
  Performance work is ongoing, so speed differs between zones.
