include_guard(GLOBAL)

# Consumer plugin API:
#   voltmod_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...]
#                  [FEATURES ...] [PCH_HEADERS ...]
#                  VERSION <v> [DESCRIPTION <text>])

# VoltModCommon defines the shared paths and warning helper.
include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# Create a Metamod MODULE. SOURCES defaults to `src/*.cpp`.
#
# FEATURES DATABASE adds VoltMod::Database and libpqxx. Runtime is always linked.
#
# VERSION is required; it and DESCRIPTION become the plugin's build metadata.
function(voltmod_add_plugin target_name)
    cmake_parse_arguments(ARG "" "VERSION;DESCRIPTION"
        "SOURCES;INCLUDE_DIRS;LIBRARIES;FEATURES;PCH_HEADERS" ${ARGN})

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
    target_compile_features("${target_name}" PRIVATE cxx_std_23)
    voltmod_set_cxx_defaults("${target_name}")

    # Ship release PDBs for crash dumps; common settings provide /Z7.
    target_link_options("${target_name}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/DEBUG;/OPT:REF;/OPT:ICF>"
    )

    hl2sdk_attach_plugin_support("${target_name}")

    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${ARG_INCLUDE_DIRS}
    )

    set(kit_targets VoltMod::Runtime)
    foreach(feature IN LISTS ARG_FEATURES)
        if(feature STREQUAL "DATABASE")
            if(NOT TARGET VoltMod::Database)
                message(FATAL_ERROR
                    "voltmod_add_plugin(${target_name} FEATURES DATABASE): voltmod was built "
                    "without Postgres. Set -o voltmod/*:with_postgres=True.")
            endif()
            list(APPEND kit_targets VoltMod::Database)
        else()
            message(FATAL_ERROR
                "voltmod_add_plugin(${target_name} FEATURES ${feature}): no such feature. "
                "Known: DATABASE.")
        endif()
    endforeach()

    target_link_libraries("${target_name}" PRIVATE
        ${kit_targets}
        ${ARG_LIBRARIES}
    )

    if(NOT VOLTMOD_DISABLE_PCH)
        target_precompile_headers("${target_name}" PRIVATE
            "<VoltMod/Api.hpp>"
            ${ARG_PCH_HEADERS}
        )
    endif()

    voltmod_set_warnings("${target_name}")
    voltmod_stamp_build_info("${target_name}" "${ARG_VERSION}")

    set(output_dir "${CMAKE_BINARY_DIR}/plugins/${target_name}/${VOLTMOD_PLATFORM_ARCH}")
    set_target_properties("${target_name}" PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target_name}"
        LIBRARY_OUTPUT_DIRECTORY "${output_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${output_dir}"
        ARCHIVE_OUTPUT_DIRECTORY "${output_dir}"
        PDB_OUTPUT_DIRECTORY "${output_dir}"
        SKIP_BUILD_RPATH TRUE
        BUILD_RPATH ""
        INSTALL_RPATH ""
    )

    voltmod_install_plugin("${target_name}")
endfunction()

# Install a server-ready addon bundle under the target component.
function(voltmod_install_plugin target_name)
    if(WIN32)
        set(bin_subdir "win64")
    else()
        set(bin_subdir "linuxsteamrt64")
    endif()
    set(addon_bin "addons/${target_name}/bin/${bin_subdir}")

    # COMPONENT is required for each artifact kind.
    install(TARGETS "${target_name}"
        LIBRARY DESTINATION "${addon_bin}" COMPONENT "${target_name}"
        RUNTIME DESTINATION "${addon_bin}" COMPONENT "${target_name}"
    )

# Install debug symbols when present.
    if(WIN32)
        install(FILES "$<TARGET_PDB_FILE:${target_name}>"
            DESTINATION "${addon_bin}" COMPONENT "${target_name}" OPTIONAL)
    endif()

    set(CS2_PLUGIN_NAME "${target_name}")
    set(CS2_PLUGIN_BIN_SUBDIR "${bin_subdir}")
    configure_file(
        "${VOLTMOD_ROOT_DIR}/cmake/plugin.vdf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.vdf"
        @ONLY
        NEWLINE_STYLE LF
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.vdf"
        DESTINATION "addons/metamod" COMPONENT "${target_name}")

    # Deployment renders settings.jsonc per server.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configs")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/configs/"
            DESTINATION "addons/${target_name}/configs"
            COMPONENT "${target_name}"
            PATTERN "settings.jsonc" EXCLUDE
        )
    endif()

    # Workshop Tools compiles Panorama sources and the client mounts them, not the server.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/panorama")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/panorama/"
            DESTINATION "addons/${target_name}/panorama"
            COMPONENT "${target_name}")
    endif()

    # Install the shared Panorama layout used by the menu driver. A plugin may install an
    # id-compatible copy above it and select it with Menus.UsePanorama.
    if(EXISTS "${VOLTMOD_PANORAMA_DIR}")
        install(DIRECTORY "${VOLTMOD_PANORAMA_DIR}/"
            DESTINATION "addons/voltmod/panorama"
            COMPONENT "${target_name}")
    endif()

    if(EXISTS "${VOLTMOD_GAMEDATA_DIR}")
        install(DIRECTORY "${VOLTMOD_GAMEDATA_DIR}/"
            DESTINATION "addons/voltmod/gamedata"
            COMPONENT "${target_name}")
    endif()
endfunction()

function(voltmod_stamp_build_info target_name version)
    set(stamp_target "${target_name}-buildinfo")
    set(include_dir "${CMAKE_BINARY_DIR}/voltmod-buildinfo/${target_name}/include")
    set(header "${include_dir}/VoltMod/BuildInfo.hpp")

    if(NOT TARGET "${stamp_target}")
        add_custom_target("${stamp_target}"
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
    endif()

    add_dependencies("${target_name}" "${stamp_target}")
    target_include_directories("${target_name}" PRIVATE "${include_dir}")
endfunction()
