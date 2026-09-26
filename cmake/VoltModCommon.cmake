include_guard(GLOBAL)

file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/.." VOLTMOD_ROOT_DIR)
set(VOLTMOD_GAMEDATA_DIR "${VOLTMOD_ROOT_DIR}/gamedata")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x86_64 builds are supported.")
endif()

# <name>.windows.cpp and <name>.linux.cpp build only on their own platform.
if(WIN32)
    set(VOLTMOD_PLATFORM "windows")
    set(VOLTMOD_OTHER_PLATFORM "linux")
    set(VOLTMOD_BIN_SUBDIR "win64")  # the server's addon binary directory
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VOLTMOD_PLATFORM "linux")
    set(VOLTMOD_OTHER_PLATFORM "windows")
    set(VOLTMOD_BIN_SUBDIR "linuxsteamrt64")
else()
    message(FATAL_ERROR "Only Windows and Linux builds are supported.")
endif()
set(VOLTMOD_PLATFORM_ARCH "${VOLTMOD_PLATFORM}-x86_64")  # build output directory name

# First-party targets only. Embedded (/Z7) debug info: ccache caches it, and framework frames land in plugin PDBs.
function(voltmod_set_cxx_defaults target)
    target_compile_features("${target}" PUBLIC cxx_std_23)
    set_target_properties("${target}" PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        MSVC_DEBUG_INFORMATION_FORMAT Embedded
    )
    target_compile_options("${target}" PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/W3,-Wall>")
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
