include_guard(GLOBAL)

# Consumer-facing plugin API. Reaches consumers as a CMakeDeps build module, so
# after find_package(cs2-kit CONFIG REQUIRED) any project can call:
#   cs2_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...]
#                  [FEATURES ...] [PCH_HEADERS ...] [UNITY]
#                  [VERSION <v>] [DESCRIPTION <text>]
#                  [DEPENDS <spec>...] [REQUIRES <spec>...])

# CS2KIT_ROOT_DIR / _PLATFORM_ARCH / _GAMEDATA_DIR and cs2kit_set_warnings.
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitCommon.cmake")

# Create a Metamod plugin MODULE linked against CS2Kit, with output dirs and
# install rules. SOURCES defaults to a glob of src/*.cpp; INCLUDE_DIRS and
# LIBRARIES are appended to the defaults. PCH_HEADERS extends the plugin's
# precompiled header (e.g. "<pqxx/pqxx>"). UNITY enables jumbo compilation -
# it needs file-unique names for namespace-scope statics (self-registration
# blocks); Registry<T> items still work, every source is compiled and linked.
#
# FEATURES names the optional parts of the kit this plugin needs. DATABASE adds
# CS2Kit::Database (and libpqxx); without it a movement plugin carries neither. The
# always-present CS2Kit::Runtime is linked either way.
#
# VERSION/DESCRIPTION/DEPENDS/REQUIRES fill the generated manifest; VERSION defaults to
# the repo's version.txt.
function(cs2_add_plugin target_name)
    cmake_parse_arguments(ARG "UNITY" "VERSION;DESCRIPTION"
        "SOURCES;INCLUDE_DIRS;LIBRARIES;FEATURES;PCH_HEADERS;DEPENDS;REQUIRES" ${ARGN})

    # Shipped by the hl2sdk-cs2 build module, which arrives with cs2-kit.
    if(NOT COMMAND hl2sdk_attach_plugin_support)
        message(FATAL_ERROR
            "hl2sdk-cs2's build module is missing - find_package(cs2-kit CONFIG REQUIRED) "
            "must run before cs2_add_plugin().")
    endif()

    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        )
    endif()

    add_library("${target_name}" MODULE ${ARG_SOURCES})
    target_compile_features("${target_name}" PRIVATE cxx_std_23)
    set_target_properties("${target_name}" PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    # Release PDBs for crash dumps; /Z7 rather than /Zi because ccache can't cache /Zi.
    target_compile_options("${target_name}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/Z7>"
    )
    target_link_options("${target_name}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/DEBUG;/OPT:REF;/OPT:ICF>"
    )

    # The per-module SDK TUs, with the warning, PCH and unity exclusions they need.
    hl2sdk_attach_plugin_support("${target_name}")

    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${ARG_INCLUDE_DIRS}
    )

    set(kit_targets CS2Kit::Runtime)
    foreach(feature IN LISTS ARG_FEATURES)
        if(feature STREQUAL "DATABASE")
            if(NOT TARGET CS2Kit::Database)
                message(FATAL_ERROR
                    "cs2_add_plugin(${target_name} FEATURES DATABASE): cs2-kit was built "
                    "without Postgres. Set -o cs2-kit/*:with_postgres=True.")
            endif()
            list(APPEND kit_targets CS2Kit::Database)
        else()
            message(FATAL_ERROR
                "cs2_add_plugin(${target_name} FEATURES ${feature}): no such feature. "
                "Known: DATABASE.")
        endif()
    endforeach()

    target_link_libraries("${target_name}" PRIVATE
        ${kit_targets}
        ${ARG_LIBRARIES}
    )

    if(NOT CS2KIT_DISABLE_PCH)
        # <CS2Kit/Api.hpp> drags the whole hl2sdk/Metamod/protobuf header universe
        # (~200k LOC) into nearly every plugin TU; precompiling it is the big win.
        target_precompile_headers("${target_name}" PRIVATE
            "<CS2Kit/Api.hpp>"
            ${ARG_PCH_HEADERS}
        )
    endif()

    if(ARG_UNITY)
        # Batch of 8 keeps a jumbo TU quick to rebuild and preserves parallelism.
        set_target_properties("${target_name}" PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE 8
        )
    endif()

    cs2kit_set_warnings("${target_name}")
    cs2kit_stamp_build_info("${target_name}")

    # Route artifacts to build/plugins/<name>/<platform_arch>/, no rpath, no lib
    # prefix. Ninja is single-config, so no per-config output-dir variants.
    set(output_dir "${CMAKE_BINARY_DIR}/plugins/${target_name}/${CS2KIT_PLATFORM_ARCH}")
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

    cs2_write_plugin_manifest("${target_name}"
        "${ARG_VERSION}" "${ARG_DESCRIPTION}" "${ARG_DEPENDS}" "${ARG_REQUIRES}")
    cs2_install_plugin("${target_name}")
endfunction()

# Append one JSON dependency object for "<name>[>=<version>]" to out_var.
function(_cs2_dependency_json out_var spec required)
    if(spec MATCHES "^(.+)>=(.+)$")
        set(name "${CMAKE_MATCH_1}")
        set(minimum "${CMAKE_MATCH_2}")
    else()
        set(name "${spec}")
        set(minimum "")
    endif()
    string(STRIP "${name}" name)
    string(STRIP "${minimum}" minimum)

    set(entries ${${out_var}})
    list(APPEND entries
        "
    {\"name\": \"${name}\", \"minVersion\": \"${minimum}\", \"required\": ${required}}")
    set("${out_var}" "${entries}" PARENT_SCOPE)
endfunction()

# Generate <name>.manifest.json, which LoadStandardConfig adopts. Unmet DEPENDS warn and
# unmet REQUIRES log an error; neither aborts the load, since .vdf order is Metamod's.
function(cs2_write_plugin_manifest target_name version description depends requires)
    if(NOT version)
        # version.txt is what the build stamp reads, so one bump moves both. Projects
        # without one (the kit's own test_package) fall back to their project() version.
        if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
            file(READ "${CMAKE_SOURCE_DIR}/version.txt" version)
            string(STRIP "${version}" version)
        elseif(PROJECT_VERSION)
            set(version "${PROJECT_VERSION}")
        else()
            set(version "0.0.0")
        endif()
    endif()

    set(dependency_entries)
    foreach(spec IN LISTS depends)
        _cs2_dependency_json(dependency_entries "${spec}" "false")
    endforeach()
    foreach(spec IN LISTS requires)
        _cs2_dependency_json(dependency_entries "${spec}" "true")
    endforeach()
    string(JOIN "," CS2_PLUGIN_DEPENDENCIES ${dependency_entries})
    if(dependency_entries)
        string(APPEND CS2_PLUGIN_DEPENDENCIES "
  ")
    endif()

    set(CS2_PLUGIN_NAME "${target_name}")
    set(CS2_PLUGIN_VERSION "${version}")
    set(CS2_PLUGIN_DESCRIPTION "${description}")
    configure_file(
        "${CS2KIT_ROOT_DIR}/cmake/plugin.manifest.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.manifest.json"
        @ONLY
        NEWLINE_STYLE LF
    )
endfunction()

# install() rules for one plugin's deploy bundle, under a component named after the
# target. `cmake --install --component <target>` is the single source of the addons/
# layout consumed by deploy tooling.
function(cs2_install_plugin target_name)
    if(WIN32)
        set(bin_subdir "win64")
    else()
        set(bin_subdir "linuxsteamrt64")
    endif()
    set(addon_bin "addons/${target_name}/bin/${bin_subdir}")

    # Only the loadable module ships (no ARCHIVE import .lib). COMPONENT must repeat
    # per kind: a MODULE's .dll/.so is the LIBRARY artifact, else it lands in the
    # default "Unspecified" component.
    install(TARGETS "${target_name}"
        LIBRARY DESTINATION "${addon_bin}" COMPONENT "${target_name}"
        RUNTIME DESTINATION "${addon_bin}" COMPONENT "${target_name}"
    )

    # Debug symbols when the build produced them (Release usually does not).
    if(WIN32)
        install(FILES "$<TARGET_PDB_FILE:${target_name}>"
            DESTINATION "${addon_bin}" COMPONENT "${target_name}" OPTIONAL)
    endif()

    # Generated, platform-correct VDF (the bin subdir is part of its "file" value).
    set(CS2_PLUGIN_NAME "${target_name}")
    set(CS2_PLUGIN_BIN_SUBDIR "${bin_subdir}")
    configure_file(
        "${CS2KIT_ROOT_DIR}/cmake/plugin.vdf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.vdf"
        @ONLY
        NEWLINE_STYLE LF
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.vdf"
        DESTINATION "addons/metamod" COMPONENT "${target_name}")

    # Beside the configs, where LoadStandardConfig looks for it.
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.manifest.json"
        DESTINATION "addons/${target_name}" COMPONENT "${target_name}")

    # Configs except settings.jsonc (rendered per-server at deploy time).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configs")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/configs/"
            DESTINATION "addons/${target_name}/configs"
            COMPONENT "${target_name}"
            PATTERN "settings.jsonc" EXCLUDE
        )
    endif()

    # Shared cs2-kit gamedata (location owned by cs2-kit, not hardcoded here).
    if(EXISTS "${CS2KIT_GAMEDATA_DIR}")
        install(DIRECTORY "${CS2KIT_GAMEDATA_DIR}/"
            DESTINATION "addons/cs2-kit/gamedata"
            COMPONENT "${target_name}")
    endif()
endfunction()

# Stamped values are repo-wide, so all plugins share one custom target and one
# generated <CS2Kit/BuildInfo.hpp>. Called by cs2_add_plugin.
function(cs2kit_stamp_build_info target_name)
    set(include_dir "${CMAKE_BINARY_DIR}/cs2kit-buildinfo/include")
    set(header "${include_dir}/CS2Kit/BuildInfo.hpp")

    if(NOT TARGET cs2kit-buildinfo)
        add_custom_target(cs2kit-buildinfo
            COMMAND "${CMAKE_COMMAND}"
                -D "TEMPLATE_FILE=${CS2KIT_ROOT_DIR}/cmake/BuildInfo.hpp.in"
                -D "OUTPUT_FILE=${header}"
                -D "VERSION_FILE=${CMAKE_SOURCE_DIR}/version.txt"
                -D "REPO_DIR=${CMAKE_SOURCE_DIR}"
                -P "${CS2KIT_ROOT_DIR}/cmake/GitBuildInfoScript.cmake"
            BYPRODUCTS "${header}"
            COMMENT "Stamping CS2Kit build info"
            VERBATIM
        )
    endif()

    add_dependencies("${target_name}" cs2kit-buildinfo)
    target_include_directories("${target_name}" PRIVATE "${include_dir}")
endfunction()
