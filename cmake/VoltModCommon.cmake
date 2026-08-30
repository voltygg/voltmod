include_guard(GLOBAL)

# Shared paths, platform names, and first-party compile settings.

# Not cached: a cached path outlives the package it pointed at.
get_filename_component(VOLTMOD_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(VOLTMOD_GAMEDATA_DIR "${VOLTMOD_ROOT_DIR}/gamedata")
set(VOLTMOD_PANORAMA_DIR "${VOLTMOD_ROOT_DIR}/panorama")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x86_64 builds are supported.")
endif()

# Build output directory name, and the server's addon binary directory.
if(WIN32)
    set(VOLTMOD_PLATFORM_ARCH "windows-x86_64")
    set(VOLTMOD_BIN_SUBDIR "win64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VOLTMOD_PLATFORM_ARCH "linux-x86_64")
    set(VOLTMOD_BIN_SUBDIR "linuxsteamrt64")
else()
    message(FATAL_ERROR "Only Windows and Linux builds are supported.")
endif()

# First-party targets only; SDK usage requirements set none of this.
# /Z7, not /Zi: ccache can cache it and framework frames land in plugin PDBs.
function(voltmod_set_cxx_defaults target)
    target_compile_features("${target}" PUBLIC cxx_std_23)
    set_target_properties("${target}" PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_options("${target}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/Z7>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall>"
        "$<$<CXX_COMPILER_ID:MSVC>:/W3>"
    )
endfunction()
