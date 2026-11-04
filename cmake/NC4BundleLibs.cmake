# Install-time helper of PackageNC4.cmake: copy the runtime dependencies of NC4_EXECUTABLE
# that are not part of the NetCast 4 firmware into NC4_LIBDIR.

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "linux+elf")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${NC4_OBJDUMP}")

file(GLOB_RECURSE build_libs LIST_DIRECTORIES false "${NC4_BUILD_DIR}/*.so*")
set(build_lib_dirs "")
foreach (lib ${build_libs})
    get_filename_component(dir "${lib}" DIRECTORY)
    list(APPEND build_lib_dirs "${dir}")
endforeach ()
list(REMOVE_DUPLICATES build_lib_dirs)

file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${NC4_EXECUTABLE}"
        RESOLVED_DEPENDENCIES_VAR resolved
        UNRESOLVED_DEPENDENCIES_VAR unresolved
        # the install's own lib/ last: it holds the copies bundled by a previous install
        DIRECTORIES ${build_lib_dirs} "${NC4_PREFIX}/lib" "${NC4_SYSROOT}/opt/nc4/lib" "${NC4_SYSROOT}/usr/lib" "${NC4_SYSROOT}/lib" "${NC4_LIBDIR}"
        # provided by the TV: glibc, libgcc and the LG/PowerVR graphics stack
        PRE_EXCLUDE_REGEXES
            "^ld-linux"
            "^lib(c|m|dl|pthread|rt|resolv|nsl|util|crypt|anl)\\.so"
            "^libgcc_s\\.so"
            "^liblgncopenapi\\.so"
            "^lib(EGL|GLESv1_CM|GLESv2)\\.so"
        )

# The executable's RPATH ($ORIGIN/lib) finds the copies of a previous install first,
# bundle the current files from the build tree or the sysroot instead
set(sources "")
foreach (lib ${resolved})
    get_filename_component(dir "${lib}" DIRECTORY)
    get_filename_component(name "${lib}" NAME)
    if (dir STREQUAL NC4_LIBDIR)
        foreach (candidate ${build_lib_dirs} "${NC4_PREFIX}/lib" "${NC4_SYSROOT}/opt/nc4/lib" "${NC4_SYSROOT}/usr/lib" "${NC4_SYSROOT}/lib")
            if (NOT candidate STREQUAL NC4_LIBDIR AND EXISTS "${candidate}/${name}")
                set(lib "${candidate}/${name}")
                break()
            endif ()
        endforeach ()
    endif ()
    list(APPEND sources "${lib}")
endforeach ()
set(resolved ${sources})

foreach (lib ${resolved})
    execute_process(COMMAND "${NC4_OBJDUMP}" -p "${lib}" OUTPUT_VARIABLE dump)
    if (dump MATCHES "SONAME +([^\n]+)")
        set(soname "${CMAKE_MATCH_1}")
    else ()
        get_filename_component(soname "${lib}" NAME)
    endif ()
    message(STATUS "Bundling: ${soname} (${lib})")
    file(MAKE_DIRECTORY "${NC4_LIBDIR}")
    # copy the file itself, not the symlink
    get_filename_component(real "${lib}" REALPATH)
    configure_file("${real}" "${NC4_LIBDIR}/${soname}" COPYONLY)
endforeach ()

foreach (lib ${unresolved})
    message(WARNING "Unresolved dependency: ${lib}")
endforeach ()

# Make every bundled SONAME a regular file and drop the rest (symlinks, development links,
# static libraries and headers of bundled projects). ss4s modules are dlopen()ed by name.
set(sonames "")
foreach (lib ${resolved})
    execute_process(COMMAND "${NC4_OBJDUMP}" -p "${lib}" OUTPUT_VARIABLE dump)
    if (dump MATCHES "SONAME +([^\n]+)")
        list(APPEND sonames "${CMAKE_MATCH_1}")
    else ()
        get_filename_component(name "${lib}" NAME)
        list(APPEND sonames "${name}")
    endif ()
endforeach ()
foreach (soname ${sonames})
    set(path "${NC4_LIBDIR}/${soname}")
    if (IS_SYMLINK "${path}")
        get_filename_component(real "${path}" REALPATH)
        file(REMOVE "${path}")
        file(COPY_FILE "${real}" "${path}")
    endif ()
endforeach ()
file(GLOB libdir_files LIST_DIRECTORIES false "${NC4_LIBDIR}/*")
foreach (file ${libdir_files})
    get_filename_component(name "${file}" NAME)
    list(FIND sonames "${name}" found)
    if (found EQUAL -1 AND NOT name MATCHES "^ss4s")
        file(REMOVE "${file}")
    endif ()
endforeach ()
file(REMOVE_RECURSE "${NC4_LIBDIR}/static" "${NC4_LIBDIR}/../include")
