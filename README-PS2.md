# Touhou 7 (Perfect Cherry Blossom) — PlayStation 2 port (experimental)

A PlayStation 2 build of the portable `th07` decompilation, using ps2dev / ps2sdk / gsKit.
**Untested on real hardware; written without access to an emulator.** Expect bugs.

## Credits / licence

This is a derivative work. All game logic comes from the people who did the real work:

* [some100/th07](https://github.com/some100/th07) (`portable` branch) – the SDL2 portable decompilation this port is built on (CC0).
* [N0zoM1z0/th07](https://github.com/N0zoM1z0/th07) – the th07 decompilation project.
* [GensokyoClub/th06](https://github.com/GensokyoClub/th06) and [GensokyoClub/th08](https://github.com/GensokyoClub/th08) – the th06/th08 decompilations that th07's structure and naming derive from.
* Touhou Project / 東方妖々夢 are by ZUN (Team Shanghai Alice). This repository contains **no game data**.
* [sammwyy/th06-ps2](https://github.com/sammwyy/th06-ps2) and [makmal0962/th06-ps2](https://github.com/makmal0962/th06-ps2) – th06 PS2 ports; the native libpad input and the audsrv audio set-up follow their approach.
* PS2 toolchain: [ps2dev](https://github.com/ps2dev), ps2sdk, gsKit, ps2sdk-ports (SDL2, SDL2_ttf, SDL2_image).

Files added for the PS2: `src/graphics/PS2GS.*` (GS backend), `src/ps2audio.*` (SDL2 based replacement for the
miniaudio subset the game uses), `Makefile.ps2`, this file. Small `#ifdef __PS2__` changes elsewhere.

## Building

    export PS2DEV=/usr/local/ps2dev PS2SDK=$PS2DEV/ps2sdk GSKIT=$PS2DEV/gsKit
    export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin
    make -f Makefile.ps2            # th07.elf        (CT32=1 for a 32 bit frame buffer)

## Running

Put `th07.elf` next to **your own legitimate copy** of the game data and start it (host:, USB, mass: ...):

* `th07.dat`, `thbgm.dat` (and the `.cfg`/`.dat` files the game creates)
* `msgothic.ttc` – required for text (MS Gothic, or any Japanese TTF renamed to this name)

Progress is printed to the EE console (`th07 PS2: ...`, `LOG:`, `FATAL:`). The screen flashes dark blue for 1.5 s on
start: if you see that and then black, the GS backend works and the problem is data files / paths (see the log).
Controls: standard pad (cross = shoot, circle = bomb, ...).

## Known limitations

* Fog is not implemented (distant stage geometry is not faded).
* `ReadPixels` (replay thumbnails / screenshots) returns black.
* Colour op ADD and REPLACE are approximated; separate alpha/colour ops only partially.
* MIDI output and 16 bit colour options are not available.
* Audio is mixed in the SDL audio thread; BGM is streamed from `thbgm.dat` (enable "preload BGM" if it stutters).
* Memory (32 MB) and VRAM (4 MB, textures are swapped by gsKit's texture manager) are tight.
