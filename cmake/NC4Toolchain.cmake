# Toolchain for LG NetCast 4.x TVs (openlgtv/buildroot-nc4 SDK).
#
#   export NC4_SDK=/path/to/arm-lgtv-linux-gnueabi_sdk-buildroot
#   cmake -B build-nc4 -DCMAKE_TOOLCHAIN_FILE=cmake/NC4Toolchain.cmake
#
# SDL2 with the nc4 video driver and the webos-userland nc4 stubs (liblgncopenapi,
# EGL, GLES) are expected in <sysroot>/opt/nc4.

if (NOT DEFINED ENV{NC4_SDK})
    message(FATAL_ERROR "Set NC4_SDK to the NetCast 4 buildroot SDK")
endif ()
set(NC4_SDK $ENV{NC4_SDK})

# The TVs' Cortex-A9 has NEON, the toolchain just defaults to -mfpu=vfpv3. Set the default
# flags before Buildroot's toolchain file does, so external projects (opus...) get them too.
if (NOT DEFINED CMAKE_C_FLAGS)
    set(CMAKE_C_FLAGS "-mfpu=neon -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"
            CACHE STRING "NetCast 4 CFLAGS")
endif ()
if (NOT DEFINED CMAKE_CXX_FLAGS)
    set(CMAKE_CXX_FLAGS "-mfpu=neon -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"
            CACHE STRING "NetCast 4 CXXFLAGS")
endif ()

include(${NC4_SDK}/share/buildroot/toolchainfile.cmake)

# SDL2 (nc4 driver), SDL2_mixer and the webos-userland nc4 stubs are looked up in <sysroot>/opt/nc4 and, if given,
# in an extra prefix: -DNC4_PREFIX=/path or the NC4_PREFIX environment variable (e.g. where "cmake --install" put
# the webos-userland stubs).
set(NC4_SYSROOT_PREFIX "${CMAKE_SYSROOT}/opt/nc4")
if (NOT NC4_PREFIX AND DEFINED ENV{NC4_PREFIX})
    set(NC4_PREFIX "$ENV{NC4_PREFIX}")
endif ()
set(NC4_PREFIXES "${NC4_PREFIX}" "${NC4_SYSROOT_PREFIX}")
list(FILTER NC4_PREFIXES EXCLUDE REGEX "^$")
set(NC4_PREFIX "${NC4_PREFIX}" CACHE PATH "Extra install prefix of NetCast SDL2/SDL2_mixer/webos-userland libraries")
set(_pc "")
foreach (_p ${NC4_PREFIXES})
    set(_pc "${_pc}${_p}/lib/pkgconfig:${_p}/share/pkgconfig:")
    list(APPEND CMAKE_FIND_ROOT_PATH "${_p}")
endforeach ()
set(ENV{PKG_CONFIG_PATH} "${_pc}")
# libSDL2 links liblgncopenapi.so.2, which is only on the TV
set(_rl "")
foreach (_p ${NC4_PREFIXES})
    set(_rl "${_rl} -Wl,-rpath-link,${_p}/lib")
endforeach ()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_rl}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_rl}")
