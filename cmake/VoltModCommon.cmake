include_guard(GLOBAL)

# Shared paths, platform names, and first-party compile settings.

get_filename_component(VOLTMOD_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(VOLTMOD_GAMEDATA_DIR "${VOLTMOD_ROOT_DIR}/gamedata")

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

# A module the game process loads: the loader, the host, or a plugin the host loads. Built into
# OUTPUT_DIR and installed to INSTALL_DIR under COMPONENT.
function(voltmod_add_module target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR;INSTALL_DIR;COMPONENT" "SOURCES" ${ARGN})

    add_library("${target}" MODULE ${ARG_SOURCES})
    voltmod_set_cxx_defaults("${target}")
    target_include_directories("${target}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    # Release PDBs for crash dumps.
    target_link_options("${target}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/DEBUG;/OPT:REF;/OPT:ICF>"
    )

    set_target_properties("${target}" PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIR}"
        PDB_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIR}"
    )

    install(TARGETS "${target}"
        LIBRARY DESTINATION "${ARG_INSTALL_DIR}" COMPONENT "${ARG_COMPONENT}"
        RUNTIME DESTINATION "${ARG_INSTALL_DIR}" COMPONENT "${ARG_COMPONENT}"
    )
    if(WIN32)
        install(FILES "$<TARGET_PDB_FILE:${target}>"
            DESTINATION "${ARG_INSTALL_DIR}" COMPONENT "${ARG_COMPONENT}" OPTIONAL)
    endif()
endfunction()
