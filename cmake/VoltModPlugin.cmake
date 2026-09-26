include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# The Conan package ships a prebuilt host; a framework checkout builds its own.
if(EXISTS "${VOLTMOD_ROOT_DIR}/addons")
    install(DIRECTORY "${VOLTMOD_ROOT_DIR}/addons/" DESTINATION "addons" COMPONENT host)
endif()

# Builds the plugin named in the plugin.json beside it; the generated entry point creates the
# <Namespace>::App from src/App.hpp (admin-system -> AdminSystem).
# SOURCES defaults to src/*.cpp; DATABASE links VoltMod::Database.
function(voltmod_add_plugin target_name)
    cmake_parse_arguments(ARG "DATABASE" "" "SOURCES" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "voltmod_add_plugin(${target_name}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT COMMAND hl2sdk_attach_plugin_support)
        message(FATAL_ERROR
            "hl2sdk-cs2's build module is missing - find_package(voltmod CONFIG REQUIRED) "
            "must run before voltmod_add_plugin().")
    endif()

    _voltmod_check_plugin_json("${target_name}")

    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        )
    endif()

    _voltmod_plugin_namespace("${target_name}" namespace)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/App.hpp")
        message(FATAL_ERROR "voltmod_add_plugin(${target_name}): src/App.hpp must declare ${namespace}::App")
    endif()
    set(plugin_class "${namespace}::App")
    configure_file("${VOLTMOD_ROOT_DIR}/cmake/PluginEntry.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/PluginEntry.cpp" @ONLY)
    list(APPEND ARG_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/PluginEntry.cpp")

    # VOLTMOD_EXPORT on VoltMod_PluginEntry is the only export the host needs.
    voltmod_add_module("${target_name}"
        SOURCES ${ARG_SOURCES}
        OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/${target_name}/${VOLTMOD_PLATFORM_ARCH}"
        INSTALL_DIR "addons/voltmod/plugins/${target_name}"
        COMPONENT "${target_name}"
    )
    target_link_libraries("${target_name}" PRIVATE VoltMod::Sdk)
    hl2sdk_attach_plugin_support("${target_name}")

    set(pch_headers "<VoltMod/Api.hpp>")
    if(ARG_DATABASE)
        target_link_libraries("${target_name}" PRIVATE VoltMod::Database)
        # First: the MariaDB connector needs winsock2.h before windows.h.
        list(PREPEND pch_headers
            "<sqlpp23/sqlpp23.h>"
            "<sqlpp23/postgresql/postgresql.h>"
            "<sqlpp23/mysql/mysql.h>"
            "<sqlpp23/sqlite3/sqlite3.h>"
        )
    endif()

    target_precompile_headers("${target_name}" PRIVATE ${pch_headers})

    # Headers `voltmod build` renders from panorama/screens/.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/panorama/screens")
        cmake_path(GET CMAKE_CURRENT_SOURCE_DIR FILENAME owner)
        target_include_directories("${target_name}" PRIVATE
            "${CMAKE_SOURCE_DIR}/build/panorama/${owner}/include")
    endif()

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        DESTINATION "addons/voltmod/plugins/${target_name}" COMPONENT "${target_name}")

    # configs/ is left out: the installer seeds it once.
    foreach(shipped translations migrations data)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${shipped}")
            install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${shipped}"
                DESTINATION "addons/voltmod/plugins/${target_name}"
                COMPONENT "${target_name}"
            )
        endif()
    endforeach()
endfunction()

# The C++ namespace for a kebab-case plugin name, as the scaffold spells it: each word capitalized.
function(_voltmod_plugin_namespace target_name out_var)
    string(REPLACE "-" ";" words "${target_name}")
    set(result "")
    foreach(word IN LISTS words)
        string(SUBSTRING "${word}" 0 1 first)
        string(SUBSTRING "${word}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(TOLOWER "${rest}" rest)
        string(APPEND result "${first}${rest}")
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# Fail at configure time on a plugin.json the host would refuse.
function(_voltmod_check_plugin_json target_name)
    set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json")
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR "voltmod_add_plugin(${target_name}): ${manifest} is missing")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")

    file(READ "${manifest}" json)
    string(JSON name ERROR_VARIABLE error GET "${json}" name)
    if(error OR NOT name STREQUAL target_name)
        message(FATAL_ERROR "${manifest}: \"name\" must be \"${target_name}\" (${error})")
    endif()
    string(JSON version ERROR_VARIABLE error GET "${json}" version)
    if(error OR version STREQUAL "")
        message(FATAL_ERROR "${manifest}: \"version\" is missing (${error})")
    endif()
endfunction()
