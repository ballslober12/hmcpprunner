# hmcpprunner another slop porject made by claude bye

A C++17 + SDL2 port of the GameMaker Studio 1.4 project in
[Pi0h1/HotlineMiami.gmx](https://github.com/Pi0h1/HotlineMiami.gmx) (`Source/GameMaker Studio 1.4`).

Instead of hand-translating ~47,000 lines of GML, the port is a **GML → C++ transpiler plus an SDL2 runtime**:

# Supported platforms
netcast
webos comming soon
windows
linux

| Part | What it does |
|---|---|
| `tools/gml.py` | GML lexer + parser (handles the 8.x/Studio quirks: `=` as comparison, `begin/end`, optional `;`, `with`, `switch`, `x[| i]`, `i++` in expressions, unterminated `/*` …) |
| `tools/cgen.py`, `tools/build.py` | Turns all 861 objects (events), 425 scripts, and 159 rooms (incl. creation code) into C++ in `gen/`, and exports sprites/backgrounds/fonts/sounds/rooms to `data/game.dat` |
| `runtime/` | The engine: dynamic `Value`, instances + event loop (begin step / alarms / keyboard / mouse / step / movement / collisions / end step / draw), views, tiles, depth-sorted rendering, surfaces, blend modes, bitmap fonts, audio mixer (WAV + OGG), paths, `mp_grid` A*, ds_lists, file I/O, `game_save`/`game_load`, and all 183 built-in functions the game calls |

`gen/` and `data/game.dat` are produced by the build from your copy of the game (see below); they are not in the repository.

## Build

**Bring your own game.** This repo contains only the translator and engine. Clone the upstream project and point CMake at
its `Source/GameMaker Studio 1.4` folder; the build transpiles it (needs Python 3, takes about a minute) into `gen/` and
`data/game.dat` (git-ignored, never commit them).

```sh
git clone https://github.com/Pi0h1/HotlineMiami.gmx
```
Needs a C++17 compiler, CMake >= 3.16, Python 3 and SDL2 development files. The ~80k generated lines take a few minutes the first time.

**Linux** (`libsdl2-dev`):
```sh
cmake -S . -B build -DHM_GAME_DIR="$PWD/../HotlineMiami.gmx/Source/GameMaker Studio 1.4" && cmake --build build -j
```
**Windows** (recommended: [MSYS2](https://www.msys2.org) MinGW-w64 shell with `mingw-w64-x86_64-{gcc,cmake,ninja,SDL2,python}`):
```sh
cmake -S . -B build -G Ninja -DHM_GAME_DIR="..." && cmake --build build
cmake --install build --prefix dist      # hmcpprunner.exe (+ SDL2.dll) + data/game.dat
```
MSVC + vcpkg SDL2 is supported by the CMake file (`/bigobj`, UTF-8) but untested. `-DHM_WIN_CONSOLE=OFF` hides the console window.
**LG NetCast 4.x**: see [NETCAST.md](NETCAST.md).

Optional on any platform: `-DHM_MIXER=ON` plays audio through SDL_mixer (streams music instead of decoding whole files into RAM).
Re-run the transpiler after changing `tools/` with `-DHM_REGEN=ON`.

## Run

The port ships **no game assets**. Point it at your own checkout of the repo:

```sh
./build/hmcpprunner --assets "/path/to/HotlineMiami.gmx/Source/GameMaker Studio 1.4"
```

Optional: `--work <dir>` (writable save/options dir; the `datafiles/` content is copied/symlinked there, default `./hm_work`), `--data <game.dat>`.

Controls are the original ones (WASD, mouse, Space, Shift, Enter, Esc …).

### Test/debug switches
`--headless` (hidden window, software renderer), `--frames N`, `--room <index>` (starts via the menu so global state exists), `--shot out.png`,
env: `HM_KEYS="68:60-140"` (hold vk 68 from frame 60 to 140), `HM_MOUSE`, `HM_PRINT_OBJ=objPlayer`, `HM_PRINT_GLOBALS=rightkey`, `HM_DUMP_SURFACES=2`.

## Status

Verified here (headless): main menu renders pixel-for-pixel like the original (animated gradient, palm trees, surface-rendered logo/menu text); **all 159 rooms** load and run without crashing; level rooms render (tiles, furniture, gore surfaces, dialogue portraits); player movement + collisions work.

Known gaps / approximations (none of these were testable without a GPU/controller/long play sessions):
- **Collisions use axis-aligned bounding boxes.** 1372 of 1504 sprites use rectangle masks (exact); the 132 *precise-mask* sprites are approximated. Rotated masks are not modelled.
- `bm_subtract`/`bm_max` use SDL custom blend modes → needs a GPU renderer (OpenGL/D3D/Metal). On SDL's software renderer those draws are skipped (the screen vignette disappears).
- `view_angle` (the menu's slight camera tilt), `room_set_persistent`, `draw_set_blend_mode_ext` are not implemented.
- Steam, achievements/leaderboards, AlienFX and the video-capture DLLs are stubbed (`external_call` returns 0). The XInput DLL is emulated (see below).
- `game_save`/`game_load` use a custom snapshot format (instances, globals, tiles); ds_lists/paths/surfaces are not part of it.
- Not play-tested end-to-end: boss fights, every cutscene, audio mix balance, and performance on real hardware.

## Gamepad and NetCast
The XInput path of the game (`GMXInput.dll`) is emulated on top of SDL game controllers (PC) or LGNC_GAMEPAD (TV); the `xbox` flag is created automatically when a pad is connected (`--pad` / `--no-pad`).
Options: `--res WxH` (internal resolution, default 1366x768), `--pad`, `--no-pad`; tests: `HM_PAD="1-400:lx=30000,ly=-30000,btn=4096"` scripts a pad.
LG NetCast 4.x build: see [NETCAST.md](NETCAST.md).

## License
Original code: MIT (see [LICENSE](LICENSE)). *Hotline Miami* is © Dennaton Games / Devolver Digital; the upstream project may only be used for **free, non-commercial** purposes and so may anything you generate from it. This repo contains no game code, art or audio - see [NOTICE.md](NOTICE.md).
