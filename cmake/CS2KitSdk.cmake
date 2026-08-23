include_guard(GLOBAL)

get_filename_component(CS2KIT_ROOT_DIR_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(CS2KIT_ROOT_DIR "${CS2KIT_ROOT_DIR_DEFAULT}" CACHE PATH "CS2Kit repository root")
set(CS2KIT_GAMEDATA_DIR "${CS2KIT_ROOT_DIR}/gamedata" CACHE PATH "CS2Kit shared gamedata path")

# Nothing here describes the SDK. hl2sdk-cs2 ships its own build module, which attaches
# the TUs a consumer must compile (hl2sdk_attach_*); everything else it needs to say rides
# on the CS2Kit::HL2SDK imported target.

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

# Warning level for first-party code. The SDK packages carry includes, defines and
# ABI flags as usage requirements, but warnings are the consumer's policy, so every
# target the kit owns opts in explicitly.
function(cs2kit_set_sdk_warnings target)
    target_compile_options("${target}" PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall>"
        "$<$<CXX_COMPILER_ID:MSVC>:/W3>"
    )
endfunction()
