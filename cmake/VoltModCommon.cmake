include_guard(GLOBAL)

# Shared by everything else in this directory: where the framework lives, what platform this is,
# and the toolchain fallbacks. Kept separate from VoltModPlugin so VoltModTests and the
# framework-internal VoltModLibrary can have the paths without pulling in the plugin API.

get_filename_component(VOLTMOD_ROOT_DIR_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(VOLTMOD_ROOT_DIR "${VOLTMOD_ROOT_DIR_DEFAULT}" CACHE PATH "VoltMod repository root")
set(VOLTMOD_GAMEDATA_DIR "${VOLTMOD_ROOT_DIR}/gamedata" CACHE PATH "VoltMod shared gamedata path")

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
    set(VOLTMOD_PLATFORM_ARCH "windows-x86_64" CACHE INTERNAL "VoltMod platform architecture")
elseif(UNIX)
    set(VOLTMOD_PLATFORM_ARCH "linux-x86_64" CACHE INTERNAL "VoltMod platform architecture")
else()
    message(FATAL_ERROR "Only Windows and Linux builds are supported.")
endif()

# Language level, symbol visibility and debug-info format shared by every target the framework
# owns - framework libraries and plugins alike. Linkage-specific properties (PIC, OUTPUT_NAME) and
# the cxx_std_23 usage requirement stay with the caller, which knows its own target kind.
# /Z7 rather than /Zi because ccache cannot cache /Zi.
function(voltmod_set_cxx_defaults target)
    set_target_properties("${target}" PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_options("${target}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/Z7>"
    )
endfunction()

# Warning level for first-party code. The SDK packages carry includes, defines and
# ABI flags as usage requirements, but warnings are the consumer's policy, so every
# target the framework owns opts in explicitly.
function(voltmod_set_warnings target)
    target_compile_options("${target}" PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall>"
        "$<$<CXX_COMPILER_ID:MSVC>:/W3>"
    )
endfunction()
