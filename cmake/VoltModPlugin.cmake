include_guard(GLOBAL)

# Consumer plugin API:
#   voltmod_add_plugin(<name> VERSION <v> [SOURCES ...] [FEATURES ...])

include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# Metamod MODULE linking VoltMod::Runtime. SOURCES defaults to src/*.cpp; FEATURES DATABASE
# adds VoltMod::Database; VERSION goes into BuildInfo.hpp.
function(voltmod_add_plugin target_name)
    cmake_parse_arguments(ARG "" "VERSION" "SOURCES;FEATURES" ${ARGN})

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

    add_library("${target_name}" MODULE ${ARG_SOURCES})
    voltmod_set_cxx_defaults("${target_name}")

    # Release PDBs for crash dumps.
    target_link_options("${target_name}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/DEBUG;/OPT:REF;/OPT:ICF>"
    )

    hl2sdk_attach_plugin_support("${target_name}")

    target_include_directories("${target_name}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    set(framework_targets VoltMod::Runtime)
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

    voltmod_install_plugin("${target_name}")
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

    set(CS2_PLUGIN_NAME "${target_name}")
    set(CS2_PLUGIN_BIN_SUBDIR "${VOLTMOD_BIN_SUBDIR}")
    configure_file(
        "${VOLTMOD_ROOT_DIR}/cmake/plugin.vdf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.vdf"
        @ONLY
        NEWLINE_STYLE LF
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.vdf"
        DESTINATION "addons/metamod" COMPONENT "${target_name}")

    # settings.jsonc is rendered per server at deploy.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configs")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/configs/"
            DESTINATION "addons/${target_name}/configs"
            COMPONENT "${target_name}"
            PATTERN "settings.jsonc" EXCLUDE
        )
    endif()

    if(EXISTS "${VOLTMOD_GAMEDATA_DIR}")
        install(DIRECTORY "${VOLTMOD_GAMEDATA_DIR}/"
            DESTINATION "addons/voltmod/gamedata"
            COMPONENT "${target_name}")
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
