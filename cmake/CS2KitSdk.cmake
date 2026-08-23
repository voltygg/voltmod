include_guard(GLOBAL)

get_filename_component(CS2KIT_ROOT_DIR_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(CS2KIT_ROOT_DIR "${CS2KIT_ROOT_DIR_DEFAULT}" CACHE PATH "CS2Kit repository root")
set(CS2KIT_GAMEDATA_DIR "${CS2KIT_ROOT_DIR}/gamedata" CACHE PATH "CS2Kit shared gamedata path")

# CS2KIT_HL2SDK_DIR and CS2KIT_HL2SDK_PROTO_SOURCES are not declared here: the
# hl2sdk-cs2 package sets them from its own build module (cmake/hl2sdk-vars.cmake),
# which is the only thing that knows where the package landed.

# Fallbacks when no toolchain sets these; CACHE so sibling plugin dirs see them.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "")
    endif()
endif()

# The `<os>-<arch>` folder name used for plugin output dirs.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x86_64 builds are supported.")
endif()
if(WIN32)
    set(CS2KIT_PLATFORM_ARCH "windows-x86_64" CACHE INTERNAL "CS2Kit platform architecture")
elseif(UNIX)
    set(CS2KIT_PLATFORM_ARCH "linux-x86_64" CACHE INTERNAL "CS2Kit platform architecture")
else()
    message(FATAL_ERROR "Only Windows and Linux builds are supported.")
endif()

# Disable warnings on vendored/generated TUs (SYSTEM includes only cover headers).
function(cs2kit_mark_vendored_sources)
    set_source_files_properties(${ARGN} PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
    )
endfunction()

# Warning level for first-party code. The SDK packages carry includes, defines and
# ABI flags as usage requirements, but warnings are the consumer's policy, so every
# target the kit owns opts in explicitly.
function(cs2kit_set_sdk_warnings target)
    target_compile_options("${target}" PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall>"
        "$<$<CXX_COMPILER_ID:MSVC>:/W3>"
    )
endfunction()

# Locate the SDK packages and define CS2Kit::HL2SDK / CS2Kit::Metamod.
#
# A macro, not a function: find_package must run in the caller's scope so the
# imported targets and the cache vars hl2sdk-vars.cmake sets land there too.
macro(cs2kit_find_sdk)
    find_package(hl2sdk-cs2 CONFIG QUIET)
    find_package(metamod-source CONFIG QUIET)

    if(NOT hl2sdk-cs2_FOUND OR NOT metamod-source_FOUND)
        message(FATAL_ERROR
            "hl2sdk-cs2 and metamod-source come from Conan, not from a checkout.\n"
            "  conan config install https://github.com/voltygg/cs2-kit.git -sf conan\n"
            "  conan install . -pr:a <profile> --build=missing")
    endif()

    # Set by the hl2sdk-cs2 build module. Empty means an older package revision that
    # predates it, so the SDK tree and the generated protobufs cannot be located.
    if(NOT CS2KIT_HL2SDK_DIR OR NOT CS2KIT_HL2SDK_PROTO_SOURCES)
        message(FATAL_ERROR
            "hl2sdk-cs2 was found but shipped no hl2sdk-vars.cmake. "
            "Update it: conan install . --update")
    endif()
endmacro()
