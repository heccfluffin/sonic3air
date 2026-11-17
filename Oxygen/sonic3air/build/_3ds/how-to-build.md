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

See **[INSTALL.md](INSTALL.md)** for the full player guide (SD card layout, game data, ROM, sound firmware,
troubleshooting). In short:

- `sdmc:/3ds/sonic3air/data/`: the `data` folder of the official PC release, **same version as the port**
  (this branch: **v26.03.28.0 stable**); other versions fail with "Failed to load 'scripts.bin'"
- `sdmc:/3ds/sonic3air/Sonic_Knuckles_wSonic3.bin`: your own Steam ROM, named exactly like this
- `sdmc:/3ds/dspfirm.cdc`: needed for sound (dump it with DSP1)
- `config.json` from the PC release is not needed


## Mods

Use the mod patcher in `modpatcher/` (see its README), or copy mod folders to `3ds/sonic3air/savedata/mods/`
yourself and enable them in the game's Mods menu.

Prefer unpacked mod folders over `.zip` files on 3DS: the engine keeps every file it reads from a zip in RAM,
while files in folders are loaded from the SD card when needed. The patcher unpacks zips automatically.


## Known limitations / to do

- Runs at about 60 FPS in tested gameplay scenes in Citra; real-hardware profiling is in progress
- Rendering is done on the CPU; the GPU (citro3d) presents and places the finished picture
- Memory: the New 3DS gives apps roughly 124-178 MB; large music mods may not fit
- No OpenGL-only features (custom shaders, upscaling filters)
- The bottom screen shows the profiler in development builds; a controls display is planned
