include_guard(GLOBAL)

# Consumer plugin API:
#   voltmod_add_plugin(<name> [SOURCES ...] [FEATURES ...])

include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# A plugin the voltmod host loads, linking VoltMod::Sdk. Its plugin.json, beside this
# CMakeLists.txt, names it, versions it and lists the plugins it depends on; the host reads the
# installed copy. SOURCES defaults to src/*.cpp; FEATURES DATABASE adds VoltMod::Database.
function(voltmod_add_plugin target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;FEATURES" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "voltmod_add_plugin(${target_name}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT COMMAND hl2sdk_attach_plugin_support)
        message(FATAL_ERROR
            "hl2sdk-cs2's build module is missing - find_package(voltmod CONFIG REQUIRED) "
            "must run before voltmod_add_plugin().")
    endif()

    _voltmod_read_plugin_version("${target_name}" version)

    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        )
    endif()

    # VOLTMOD_EXPORT on VoltMod_PluginEntry is the only export the host needs.
    voltmod_add_module("${target_name}"
        SOURCES ${ARG_SOURCES}
        VERSION "${version}"
        OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/${target_name}/${VOLTMOD_PLATFORM_ARCH}"
        INSTALL_DIR "addons/voltmod/plugins/${target_name}"
        COMPONENT "${target_name}"
    )

    set(framework_targets VoltMod::Sdk)
    set(pch_headers "<VoltMod/Api.hpp>")
    voltmod_apply_features(voltmod_add_plugin "${target_name}" "${ARG_FEATURES}" framework_targets)

    if("DATABASE" IN_LIST ARG_FEATURES)
        # Ahead of any framework header: the MariaDB connector needs winsock2.h before the
        # windows.h an SDK header pulls in.
        list(PREPEND pch_headers
            "<sqlpp23/sqlpp23.h>"
            "<sqlpp23/postgresql/postgresql.h>"
            "<sqlpp23/mysql/mysql.h>"
            "<sqlpp23/sqlite3/sqlite3.h>"
        )
    endif()

    target_link_libraries("${target_name}" PRIVATE ${framework_targets})

    if(NOT VOLTMOD_DISABLE_PCH)
        target_precompile_headers("${target_name}" PRIVATE ${pch_headers})
    endif()

    # `voltmod build` renders panorama/screens/ there first; nothing generated is committed.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/panorama/screens")
        get_filename_component(owner "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
        target_include_directories("${target_name}" PRIVATE
            "${CMAKE_SOURCE_DIR}/build/panorama/${owner}/include")
    endif()

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        DESTINATION "addons/voltmod/plugins/${target_name}" COMPONENT "${target_name}")

    # settings.jsonc is rendered per server at deploy.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configs")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/configs/"
            DESTINATION "addons/voltmod/plugins/${target_name}/configs"
            COMPONENT "${target_name}"
            PATTERN "settings.jsonc" EXCLUDE
        )
    endif()

    _voltmod_install_packaged_host()
endfunction()

# The host refuses a plugin whose plugin.json name is not its directory, so fail here instead.
function(_voltmod_read_plugin_version target_name out_var)
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
    set("${out_var}" "${version}" PARENT_SCOPE)
endfunction()

# A packaged framework ships the built host under addons/, offered as the same `host` install
# component a framework checkout builds.
function(_voltmod_install_packaged_host)
    get_property(done GLOBAL PROPERTY VOLTMOD_PACKAGED_HOST_INSTALLED)
    if(done OR NOT EXISTS "${VOLTMOD_ROOT_DIR}/addons")
        return()
    endif()
    set_property(GLOBAL PROPERTY VOLTMOD_PACKAGED_HOST_INSTALLED TRUE)
    install(DIRECTORY "${VOLTMOD_ROOT_DIR}/addons/" DESTINATION "addons" COMPONENT host)
endfunction()
