include_guard(GLOBAL)

# Consumer plugin API:
#   voltmod_add_plugin(<name> VERSION <v> [SOURCES ...] [FEATURES ...]
#                      [DEPENDS ...] [OPTIONAL_DEPENDS ...])

include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# A module the voltmod host loads, linking VoltMod::Sdk. SOURCES defaults to src/*.cpp;
# FEATURES DATABASE adds VoltMod::Database; VERSION goes into BuildInfo.hpp; DEPENDS and
# OPTIONAL_DEPENDS name other plugins and go into plugin.json, which orders the host's loading.
function(voltmod_add_plugin target_name)
    cmake_parse_arguments(ARG "" "VERSION" "SOURCES;FEATURES;DEPENDS;OPTIONAL_DEPENDS" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "voltmod_add_plugin(${target_name}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT ARG_VERSION)
        message(FATAL_ERROR "voltmod_add_plugin(${target_name}) requires VERSION")
    endif()

    if(NOT COMMAND hl2sdk_attach_plugin_support)
        message(FATAL_ERROR
            "hl2sdk-cs2's build module is missing - find_package(voltmod CONFIG REQUIRED) "
            "must run before voltmod_add_plugin().")
    endif()

    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        )
    endif()

    # The host loads this module by path. VOLTMOD_EXPORT on VoltMod_PluginEntry is the only
    # export it needs, so there is no export list or --export-dynamic here.
    add_library("${target_name}" MODULE ${ARG_SOURCES})
    voltmod_set_cxx_defaults("${target_name}")

    # Release PDBs for crash dumps.
    target_link_options("${target_name}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/DEBUG;/OPT:REF;/OPT:ICF>"
    )

    hl2sdk_attach_plugin_support("${target_name}")

    target_include_directories("${target_name}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

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

    voltmod_stamp_build_info("${target_name}" "${ARG_VERSION}")

    # `voltmod build` renders panorama/screens/ there first; nothing generated is committed.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/panorama/screens")
        get_filename_component(owner "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
        target_include_directories("${target_name}" PRIVATE
            "${CMAKE_SOURCE_DIR}/build/panorama/${owner}/include")
    endif()

    set(output_dir "${CMAKE_BINARY_DIR}/plugins/${target_name}/${VOLTMOD_PLATFORM_ARCH}")
    set_target_properties("${target_name}" PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
        PDB_OUTPUT_DIRECTORY "${output_dir}"
    )

    voltmod_write_plugin_manifest("${target_name}" "${ARG_VERSION}"
        "${ARG_DEPENDS}" "${ARG_OPTIONAL_DEPENDS}")
    voltmod_install_plugin("${target_name}")
endfunction()

# plugin.json is what the host scans: it names the plugin's module and orders the load.
function(voltmod_write_plugin_manifest target_name version depends optional_depends)
    set(VOLTMOD_PLUGIN_NAME "${target_name}")
    set(VOLTMOD_PLUGIN_VERSION "${version}")
    _voltmod_json_array(VOLTMOD_PLUGIN_DEPENDENCIES ${depends})
    _voltmod_json_array(VOLTMOD_PLUGIN_OPTIONAL_DEPENDENCIES ${optional_depends})
    configure_file(
        "${VOLTMOD_ROOT_DIR}/cmake/plugin.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/plugin.json"
        @ONLY
        NEWLINE_STYLE LF
    )
endfunction()

# The members of a JSON array: a CMake list is ';'-separated, and an empty one must stay empty.
function(_voltmod_json_array out_var)
    set(members "")
    foreach(name IN LISTS ARGN)
        list(APPEND members "\"${name}\"")
    endforeach()
    list(JOIN members ", " joined)
    set("${out_var}" "${joined}" PARENT_SCOPE)
endfunction()

# Install a server-ready addon bundle under the target component.
function(voltmod_install_plugin target_name)
    set(addon_bin "addons/${target_name}/bin/${VOLTMOD_BIN_SUBDIR}")

    install(TARGETS "${target_name}"
        LIBRARY DESTINATION "${addon_bin}" COMPONENT "${target_name}"
        RUNTIME DESTINATION "${addon_bin}" COMPONENT "${target_name}"
    )

    if(WIN32)
        install(FILES "$<TARGET_PDB_FILE:${target_name}>"
            DESTINATION "${addon_bin}" COMPONENT "${target_name}" OPTIONAL)
    endif()

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugin.json"
        DESTINATION "addons/${target_name}" COMPONENT "${target_name}")

    # settings.jsonc is rendered per server at deploy.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configs")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/configs/"
            DESTINATION "addons/${target_name}/configs"
            COMPONENT "${target_name}"
            PATTERN "settings.jsonc" EXCLUDE
        )
    endif()
endfunction()

# BuildInfo.hpp is re-stamped on every build.
function(voltmod_stamp_build_info target_name version)
    set(include_dir "${CMAKE_BINARY_DIR}/voltmod-buildinfo/${target_name}/include")
    set(header "${include_dir}/VoltMod/BuildInfo.hpp")

    add_custom_target("${target_name}-buildinfo"
        COMMAND "${CMAKE_COMMAND}"
            -D "TEMPLATE_FILE=${VOLTMOD_ROOT_DIR}/cmake/BuildInfo.hpp.in"
            -D "OUTPUT_FILE=${header}"
            -D "VERSION=${version}"
            -D "REPO_DIR=${CMAKE_SOURCE_DIR}"
            -P "${VOLTMOD_ROOT_DIR}/cmake/GitBuildInfoScript.cmake"
        BYPRODUCTS "${header}"
        COMMENT "Stamping ${target_name} build info"
        VERBATIM
    )

    add_dependencies("${target_name}" "${target_name}-buildinfo")
    target_include_directories("${target_name}" PRIVATE "${include_dir}")
endfunction()

# A packaged framework ships the built host under addons/; offer it as an install component so
# a consumer stages it the same way it stages a plugin. A framework checkout builds it instead.
if(EXISTS "${VOLTMOD_ROOT_DIR}/addons")
    install(DIRECTORY "${VOLTMOD_ROOT_DIR}/addons/" DESTINATION "addons" COMPONENT host)
endif()
