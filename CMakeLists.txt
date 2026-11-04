# hmcpprunner: Hotline Miami (GameMaker Studio 1.4) -> C++/SDL2 port. Builds for Linux, Windows and LG NetCast 4.x.
#
# The game code/data are generated from YOUR copy of the game at configure time (nothing of it is in this repo):
#   -DHM_GAME_DIR="/path/to/HotlineMiami.gmx/Source/GameMaker Studio 1.4"
#
# Linux:     cmake -S . -B build -DHM_GAME_DIR="..." && cmake --build build -j
# Windows:   MSYS2 MinGW-w64 (recommended): cmake -S . -B build -G Ninja -DHM_GAME_DIR="..." && cmake --build build
#            or MSVC + vcpkg SDL2: cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# NetCast 4: export NC4_SDK=/path/to/arm-lgtv-linux-gnueabi_sdk-buildroot
#            cmake -S . -B build-nc4 -DCMAKE_TOOLCHAIN_FILE=cmake/NC4Toolchain.cmake \
#                  -DHM_GAME_DIR="/path/to/HotlineMiami.gmx/Source/GameMaker Studio 1.4" \
#                  -DCMAKE_INSTALL_PREFIX=<usb>/nc4-homebrew/hmcpprunner
#            cmake --build build-nc4 -j && cmake --install build-nc4
#   (SDL2 with the nc4 video/audio driver, SDL2_mixer and the webos-userland nc4 stubs are expected in <sysroot>/opt/nc4,
#    the same layout the cube2005 port uses; see NETCAST.md)
cmake_minimum_required(VERSION 3.16)
project(hmcpprunner C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NC4_SDK)
  set(HM_NC4_DEFAULT ON)
else()
  set(HM_NC4_DEFAULT OFF)
endif()
option(HM_NC4 "Build for LG NetCast 4.x (LGNC gamepad, SDL nc4 driver, 960x540 internal resolution)" ${HM_NC4_DEFAULT})
option(HM_MIXER "Play audio through SDL_mixer (streams music from disk) instead of the built-in mixer" ${HM_NC4})
set(HM_ASSETS_DIR "" CACHE PATH "GameMaker Studio 1.4 dir of the HotlineMiami.gmx repo; copied on install (NetCast); defaults to HM_GAME_DIR")

# -O1 keeps the compile time of the ~80k generated lines reasonable on a PC; the TV's single Cortex-A9 wants -O2
if(NOT MSVC)
  if(HM_NC4)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2")
  else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O1")
  endif()
endif()

set(STB_DIR "${CMAKE_SOURCE_DIR}/third_party/stb")
find_package(PkgConfig QUIET)
if(HM_NC4)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  # a static-only SDL2 (as built by the nc4 preset) needs its private dependencies too
  if(NOT EXISTS "${SDL2_LIBDIR}/libSDL2.so" AND EXISTS "${SDL2_LIBDIR}/libSDL2.a")
    set(SDL2_LIBRARIES ${SDL2_STATIC_LIBRARIES})
    message(STATUS "SDL2 is static-only: linking ${SDL2_LIBRARIES}")
  endif()
  # lgncopenapi.pc of the stub package has prefix=/usr baked in, so look the headers/library up directly
  find_path(LGNC_INCLUDE_DIR lgnc.h PATHS ${NC4_PREFIX} ${CMAKE_SYSROOT}/usr ${CMAKE_SYSROOT}/opt/nc4
            PATH_SUFFIXES include/lgncopenapi include REQUIRED NO_DEFAULT_PATH)
  find_library(LGNC_LIBRARY lgncopenapi PATHS ${NC4_PREFIX} ${CMAKE_SYSROOT}/usr ${CMAKE_SYSROOT}/opt/nc4
               PATH_SUFFIXES lib REQUIRED NO_DEFAULT_PATH)
  message(STATUS "lgncopenapi: ${LGNC_INCLUDE_DIR} ${LGNC_LIBRARY}")
else()
  find_package(SDL2 QUIET)
endif()
set(SDL_INC "" CACHE STRING "extra SDL2 include dirs (only if find_package(SDL2) fails)")
set(SDL_LIB "" CACHE FILEPATH "SDL2 library to link (only if find_package(SDL2) fails)")

# --- Game-derived sources -------------------------------------------------------------------------------------
# gen/*.cpp, data/game.dat and runtime/{builtins,funcs}.h are NOT part of this repository: they are produced from your own
# copy of the game (the GameMaker Studio 1.4 project in Pi0h1/HotlineMiami.gmx) by tools/build.py.
set(HM_GAME_DIR "" CACHE PATH "'GameMaker Studio 1.4' folder of your HotlineMiami.gmx clone (contains HotlineMiami.project.gmx)")
option(HM_REGEN "Re-run tools/build.py even if gen/ already exists" OFF)
if(HM_REGEN OR NOT EXISTS "${CMAKE_SOURCE_DIR}/gen/ids.h" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/runtime/funcs.h")
  if(NOT HM_GAME_DIR OR NOT EXISTS "${HM_GAME_DIR}/HotlineMiami.project.gmx")
    message(FATAL_ERROR "Game sources not generated yet. Configure with -DHM_GAME_DIR=\"/path/to/HotlineMiami.gmx/Source/GameMaker Studio 1.4\" "
                        "(the folder that contains HotlineMiami.project.gmx); see README.md.")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  message(STATUS "Transpiling the game (tools/build.py), this takes a minute ...")
  execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build.py "${HM_GAME_DIR}" "${CMAKE_SOURCE_DIR}"
                  RESULT_VARIABLE HM_GEN_RC OUTPUT_VARIABLE HM_GEN_OUT ERROR_VARIABLE HM_GEN_OUT)
  if(NOT HM_GEN_RC EQUAL 0)
    message(FATAL_ERROR "tools/build.py failed:\n${HM_GEN_OUT}")
  endif()
endif()
if(NOT HM_ASSETS_DIR)
  set(HM_ASSETS_DIR "${HM_GAME_DIR}" CACHE PATH "" FORCE)
endif()
file(GLOB GEN_SRC ${CMAKE_SOURCE_DIR}/gen/*.cpp)
set(HM_SRC runtime/main.cpp runtime/rt.cpp runtime/value.cpp runtime/draw.cpp runtime/funcs.cpp runtime/pad.cpp)
if(HM_MIXER)
  list(APPEND HM_SRC runtime/audio_mixer.cpp)
else()
  list(APPEND HM_SRC runtime/audio.cpp ${STB_DIR}/stb_vorbis.c)
  if(NOT MSVC)
    set_source_files_properties(${STB_DIR}/stb_vorbis.c PROPERTIES COMPILE_FLAGS "-w -O2")
  endif()
endif()
add_executable(hmcpprunner ${HM_SRC} ${GEN_SRC})
if(MSVC)
  # ~80k generated lines: big object files, UTF-8 sources
  target_compile_options(hmcpprunner PRIVATE /bigobj /utf-8 /wd4244 /wd4267 /wd4305 /wd4996)
  target_compile_definitions(hmcpprunner PRIVATE _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  set_source_files_properties(${STB_DIR}/stb_vorbis.c PROPERTIES COMPILE_FLAGS "/w")
endif()
if(WIN32)
  option(HM_WIN_CONSOLE "Keep a console window on Windows (stderr log)" ON)
  if(NOT HM_WIN_CONSOLE AND NOT MSVC)
    target_link_options(hmcpprunner PRIVATE -mwindows)
  endif()
  if(MINGW)
    # standalone exe: no libgcc/libstdc++/winpthread DLLs to ship
    target_link_options(hmcpprunner PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
target_include_directories(hmcpprunner PRIVATE ${CMAKE_SOURCE_DIR}/runtime ${STB_DIR} ${SDL_INC})

if(TARGET SDL2::SDL2 AND NOT HM_NC4)
  target_link_libraries(hmcpprunner PRIVATE SDL2::SDL2)
elseif(SDL2_FOUND)
  target_include_directories(hmcpprunner PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_directories(hmcpprunner PRIVATE ${SDL2_LIBRARY_DIRS})
  target_link_libraries(hmcpprunner PRIVATE ${SDL2_LIBRARIES})
else()
  target_link_libraries(hmcpprunner PRIVATE ${SDL_LIB})
endif()
find_package(Threads REQUIRED)
target_link_libraries(hmcpprunner PRIVATE Threads::Threads)

if(HM_MIXER)
  pkg_check_modules(SDL2_MIXER SDL2_mixer)
  if(NOT SDL2_MIXER_FOUND)
    message(FATAL_ERROR "SDL2_mixer not found (NC4_PREFIX=${NC4_PREFIX}). Install it there, or configure with -DHM_MIXER=OFF to use the built-in audio (decodes whole music files into RAM: risky on the TV).")
  endif()
  target_include_directories(hmcpprunner PRIVATE ${SDL2_MIXER_INCLUDE_DIRS})
  target_link_directories(hmcpprunner PRIVATE ${SDL2_MIXER_LIBRARY_DIRS})
  target_link_libraries(hmcpprunner PRIVATE ${SDL2_MIXER_LIBRARIES})
endif()

if(HM_NC4)
  target_compile_definitions(hmcpprunner PRIVATE HM_NC4)
  # lgnc_egl.h includes <EGL/egl.h>: the stub package installs it next to lgncopenapi/ (NC4_PREFIX/include or <sysroot>/opt/nc4/include)
  get_filename_component(LGNC_PARENT_INCLUDE "${LGNC_INCLUDE_DIR}" DIRECTORY)
  target_include_directories(hmcpprunner PRIVATE ${LGNC_INCLUDE_DIR} ${LGNC_PARENT_INCLUDE})
  if(NC4_PREFIX)
    target_include_directories(hmcpprunner PRIVATE ${NC4_PREFIX}/include)
  endif()
  # last resort if no EGL headers are installed anywhere (Khronos headers, from the webos-userland stubs)
  target_compile_options(hmcpprunner PRIVATE -idirafter ${CMAKE_SOURCE_DIR}/third_party/egl-headers)
  # GCC 14 floods the log with ABI notes for std::vector iterators on ARM
  target_compile_options(hmcpprunner PRIVATE -Wno-psabi)
  target_compile_definitions(hmcpprunner PRIVATE LGNC_PLATFORM_NC4=1)
  target_link_libraries(hmcpprunner PRIVATE ${LGNC_LIBRARY} dl pthread)
  # GCC < 9 keeps std::filesystem in a separate library
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(hmcpprunner PRIVATE stdc++fs)
  endif()
  # the TV's app environment preloads libraries that pull in the firmware's old libstdc++
  target_link_options(hmcpprunner PRIVATE -static-libstdc++ -static-libgcc)
  include(cmake/PackageNC4.cmake)
endif()

if(NOT HM_NC4)
  # cmake --install build --prefix <dir>: the game + data (+ SDL2.dll on Windows). Assets are NOT included (point --assets at your repo clone).
  install(TARGETS hmcpprunner RUNTIME DESTINATION .)
  install(FILES data/game.dat DESTINATION data)
  install(FILES README.md DESTINATION .)
  if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
    install(FILES $<TARGET_RUNTIME_DLLS:hmcpprunner> DESTINATION . OPTIONAL)
  endif()
endif()
