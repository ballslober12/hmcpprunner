# Hotline Miami on LG NetCast 4.x (MT5398, PowerVR SGX 544MP, GLES 1.1)

**Status: written against the SDK headers/sources you provided and checked on a PC; never run on a TV.** Expect to iterate.

## What the NetCast build does differently
| Area | PC build | NetCast build (`-DHM_NC4=ON`, automatic with `NC4_SDK`) |
|---|---|---|
| Video | SDL renderer, 1366x768 frame | SDL nc4 video driver (EGL/GLES 1.1 renderer). The game still thinks in 1366x768, but the frame texture is **960x540** (`--res WxH` / `HM_RES` to change) and the GPU scales it to the 1280x720 window |
| Audio | built-in mixer, whole OGGs decoded to RAM | **SDL_mixer** like cube2005: WAV effects as chunks, **music streamed from disk** (one music stream at a time; a second concurrent stream falls back to a decoded chunk). `HM_AUDIO_RATE` (44100), `HM_AUDIO_BUF` (2048) |
| Gamepad | SDL game controller | `LGNC_GAMEPAD_RegisterCallback` evdev events (xpad = Xbox 360 mapping, HID pads = generic mapping) |
| Files | music copied/linked into work dir | music/WAVs are read straight from `assets/datafiles` (no 200 MB copy to the FAT stick) |
| Compile | -O1 | -O2, `-static-libstdc++ -static-libgcc`, RPATH `$ORIGIN/lib` |

## Build (same layout as the cube2005 port)
```sh
export NC4_SDK=/path/to/arm-lgtv-linux-gnueabi_sdk-buildroot
cmake -S . -B build-nc4 -DCMAKE_TOOLCHAIN_FILE=cmake/NC4Toolchain.cmake \
      -DHM_GAME_DIR="/path/to/HotlineMiami.gmx/Source/GameMaker Studio 1.4" \
      -DNC4_PREFIX=/saer/nc4-prefix   # optional extra prefix with lgncopenapi/EGL/GLES (and SDL2) \
      -DCMAKE_INSTALL_PREFIX=/path/to/usb/nc4-homebrew/hmcpprunner
cmake --build build-nc4 -j && cmake --install build-nc4
```
Libraries are searched in `<sysroot>/opt/nc4` and in an extra prefix given with `-DNC4_PREFIX=/path` (or the `NC4_PREFIX` environment variable), e.g. where you installed the webos-userland stubs. Needed (same as cube): SDL2 built with `-DSDL_NC4=ON`, **SDL2_mixer (with Ogg Vorbis)**, and the webos-userland `nc4` stubs/headers (`lgncopenapi.pc`).
The SDK's GCC must support C++17 (GCC 8+; `stdc++fs` is linked automatically for GCC < 9).
Install copies only what the runtime reads: sprites, background, fonts, datafiles, `sound/audio` (about 270 MB; the stick is FAT, files < 4 GB are fine).

Run it like cube: `nc4app run hmcpprunner/hmcpprunner.sh` (the app manager's `--service/--auid` arguments are passed through). Log: `hmcpprunner.log`.

## Gamepad
* The game's "360 gamepad" mode is driven by emulating `GMXInput.dll` (`pad.cpp`). On start the port creates the `xbox` flag file when a pad is present (waits up to 1.5 s on the TV for the hotplug callback); `--pad` forces it, `--no-pad` disables it. The menu entry *Options -> 360 GAMEPAD* still toggles it.
* Layout is the game's own Xbox layout: left stick move, right stick aim, triggers shoot/pick up, A/B/X/Y, bumpers, Start/Back.
* Not every pad reports like an Xbox 360 pad. If something is wrong, run with `HM_PAD_LOG=1` (set it in `hmcpprunner.sh`), press the buttons, and send me `work/pad.log`; mapping is a small `switch` in `runtime/pad.cpp`.
* Rumble is ignored on the TV. The Magic Remote pointer still acts as the mouse (keyboard/mouse mode).

## Known risks on this hardware
* **RAM:** the TV reported ~26 MB free (722 MB total). Textures load lazily; SDL's GLES renderer pads textures to powers of two (a 1366x950 gore surface becomes 2048x1024). If it gets killed, lower `--res` (e.g. 800x450) first.
* **Speed:** untested. Logic is cheap (<2 ms/frame on a PC); rendering dominates. PNG sprites are decoded at runtime the first time they're used, so expect hitches when entering a room.
* GLES 1.1 blend: the menu vignette uses `dst*(1-src)`; if the GPU lacks `GL_OES_blend_func_separate` a plain variant is used automatically.
* `bm_max` blending (not used by the menu/vignette) needs `GL_EXT_blend_minmax`.
