# Notice

**hmcpprunner is an unofficial, fan-made engine/translator. It is not affiliated with or endorsed by Dennaton Games or
Devolver Digital.** *Hotline Miami* and all its code, art, music and data are their property.

* This repository contains **no** Hotline Miami code, art, audio or data. The C++ game code (`gen/`), `data/game.dat`
  and the GameMaker-function headers are generated on your machine by `tools/build.py` from your own copy of the
  project and are git-ignored. Please do not commit or redistribute them or the game assets.
* The upstream GameMaker project (Pi0h1/HotlineMiami.gmx) states that it may be used for free, non-commercial purposes
  only; the same applies to anything you build from it with this tool.
* Third-party code kept in the tree:
  * `third_party/stb/` - stb_image, stb_image_write, stb_vorbis by Sean Barrett (public domain / MIT, see the headers).
  * `third_party/egl-headers/` - Khronos EGL/KHR headers (see the license text in the files).
* Linked at build time (not included): SDL2 (zlib license); on NetCast also SDL2_mixer and the LG NetCast userland stubs
  from the webos-userland project.
