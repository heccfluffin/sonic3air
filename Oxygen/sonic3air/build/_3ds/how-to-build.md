# Sonic 3 A.I.R. for Nintendo 3DS (homebrew)

**Status: experimental.** The port compiles and links, but has not yet been run on real hardware or an emulator.
Target hardware is the **New 3DS / New 2DS XL**; the Old 3DS is not expected to reach full speed.

The 3DS has no OpenGL, so this build uses the engine's pure software renderer, drawing to the 400x240 top screen.


## Building

You need Docker. From this directory (`Oxygen/sonic3air/build/_3ds`):

```bash
./build.sh
```

This uses the official `devkitpro/devkitarm` image (devkitARM, libctru, 3ds-zlib) and the SDL2 copy bundled in
this repository, which includes SDL's 3DS backend. Output: `build/sonic3air.3dsx`.

Without Docker, install devkitPro with the `3ds-dev` and `3ds-zlib` packages and run:

```bash
/opt/devkitpro/portlibs/3ds/bin/arm-none-eabi-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```


### Installable .cia (for FBI)

After `./build.sh`, package the same build as a `.cia` with a Home Menu icon and banner:

```bash
./build-cia.sh
```

This needs `makerom` (from https://github.com/3DSGuy/Project_CTR) and `bannertool`
(from https://github.com/carstene1ns/3ds-bannertool) on your PATH, or in a folder given with `TOOLS_DIR=...`.
Output: `build/sonic3air.cia`. The CIA settings (`cia/sonic3air.rsf`) request the New 3DS's largest memory
mode (178 MB) and 804 MHz, which a `.3dsx` started from the Homebrew Launcher can't always get.

The CIA reads its game data, ROM, saves and mods from the same `sdmc:/3ds/sonic3air/` folder as the `.3dsx`.


## Installing on the SD card

The port contains no game data. You need your own copy of Sonic 3 & Knuckles, and the data files of the
official A.I.R. PC release.

```
SD card
└── 3ds/
    └── sonic3air/
        ├── sonic3air.3dsx                ← from this build
        ├── config.json                   ← from the official PC release (sonic3air.org)
        ├── data/                         ← the whole "data" folder from the official PC release
        │   ├── gamedata.bin
        │   ├── enginedata.bin
        │   ├── audiodata.bin
        │   └── audioremaster.bin         ← optional (remastered music)
        ├── Sonic_Knuckles_wSonic3.bin    ← your own ROM from the Steam release
        └── savedata/                     ← created by the game (settings, saves, mods)
            └── mods/
```

Start it from the Homebrew Launcher. The game version of the data files should match the source version this
port was built from.


## Mods

Use the mod patcher in `modpatcher/` (see its README), or copy mod folders to `3ds/sonic3air/savedata/mods/`
yourself and enable them in the game's Mods menu.

Prefer unpacked mod folders over `.zip` files on 3DS: the engine keeps every file it reads from a zip in RAM,
while files in folders are loaded from the SD card when needed. The patcher unpacks zips automatically.


## Known limitations / to do

- Not yet tested on hardware or in an emulator
- Performance: software rendering plus lemonscript on an 804 MHz ARM11 will need profiling and optimization
  (ideas: draw sprites/planes with the PICA200 GPU through citro3d, use the integer Tremor Ogg decoder,
  cache compiled scripts, lower the audio mixing rate)
- Memory: the New 3DS gives apps roughly 124-178 MB; large music mods may not fit
- No OpenGL-only features (custom shaders, upscaling filters)
- The bottom screen is unused for now
