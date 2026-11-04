# LG NetCast 4.x: a self-contained folder for <usb>/nc4-homebrew/hmcpprunner, started with
# webos-userland's "nc4app run hmcpprunner/hmcpprunner.sh".

set_target_properties(hmcpprunner PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
# DT_RPATH, unlike DT_RUNPATH, is searched before the LD_LIBRARY_PATH of TV apps
target_link_options(hmcpprunner PRIVATE -Wl,--disable-new-dtags)

install(TARGETS hmcpprunner DESTINATION .)
install(PROGRAMS deploy/nc4/hmcpprunner.sh DESTINATION .)
install(FILES data/game.dat DESTINATION data)
install(FILES NETCAST.md DESTINATION .)

if (HM_ASSETS_DIR)
    # only what the runtime reads: sprites, backgrounds, fonts, WAV sounds and the datafiles (music, levels)
    foreach (d sprites background fonts datafiles)
        install(DIRECTORY "${HM_ASSETS_DIR}/${d}" DESTINATION assets PATTERN "*.dll" EXCLUDE)
    endforeach ()
    install(DIRECTORY "${HM_ASSETS_DIR}/sound/audio" DESTINATION assets/sound)
else ()
    message(WARNING "HM_ASSETS_DIR not set: copy the repo's 'GameMaker Studio 1.4' folders (sprites, background, fonts, datafiles, sound/audio) to <install>/assets yourself")
endif ()
install(DIRECTORY DESTINATION work)

# Bundle every shared library the TV doesn't have (SDL2, SDL2_mixer, libvorbis...), under their SONAME (the stick is FAT)
install(CODE "
    set(NC4_OBJDUMP \"${CMAKE_OBJDUMP}\")
    set(NC4_SYSROOT \"${CMAKE_SYSROOT}\")
    set(NC4_PREFIX \"${NC4_PREFIX}\")
    if (NOT NC4_PREFIX)
        set(NC4_PREFIX \"${CMAKE_SYSROOT}/opt/nc4\")
    endif ()
    set(NC4_BUILD_DIR \"${CMAKE_BINARY_DIR}\")
    set(NC4_EXECUTABLE \"\${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:hmcpprunner>\")
    set(NC4_LIBDIR \"\${CMAKE_INSTALL_PREFIX}/lib\")
    include(\"${CMAKE_SOURCE_DIR}/cmake/NC4BundleLibs.cmake\")
")
