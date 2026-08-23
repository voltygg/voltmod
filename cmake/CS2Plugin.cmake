include_guard(GLOBAL)

# Consumer-facing plugin API. Reaches consumers as a CMakeDeps build module, so
# after find_package(cs2-kit CONFIG REQUIRED) any project can call:
#   cs2_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...]
#                  [COMPONENTS ...] [PCH_HEADERS ...] [UNITY]
#                  [VERSION <v>] [DESCRIPTION <text>]
#                  [DEPENDS <spec>...] [REQUIRES <spec>...])

# CS2KitSdk provides CS2KIT_ROOT_DIR / _PLATFORM_ARCH / _GAMEDATA_DIR plus
# cs2kit_mark_vendored_sources and cs2kit_set_sdk_warnings; include_guard(GLOBAL)
# makes the repeat free.
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitSdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitBuildInfo.cmake")

# Create a Metamod plugin MODULE linked against CS2Kit, with output dirs and
# install rules. SOURCES defaults to a glob of src/*.cpp; INCLUDE_DIRS and
# LIBRARIES are appended to the defaults. PCH_HEADERS extends the plugin's
# precompiled header (e.g. "<pqxx/pqxx>"). UNITY enables jumbo compilation -
# it needs file-unique names for namespace-scope statics (self-registration
# blocks); Registry<T> items still work, every source is compiled and linked.
#
# COMPONENTS narrows what the plugin links: naming `Sdk Menu` pulls those and whatever
# they depend on, and nothing else - a movement plugin stops carrying libpqxx. Omit it to
# link CS2Kit::CS2Kit, which is everything.
#
# VERSION/DESCRIPTION/DEPENDS/REQUIRES fill the generated manifest; VERSION defaults to
# the repo's version.txt.
function(cs2_add_plugin target_name)
    cmake_parse_arguments(ARG "UNITY" "VERSION;DESCRIPTION"
        "SOURCES;INCLUDE_DIRS;LIBRARIES;COMPONENTS;PCH_HEADERS;DEPENDS;REQUIRES" ${ARGN})

    # Set by the hl2sdk-cs2 build module; the plugin compiles two TUs out of that tree.
    if(NOT CS2KIT_HL2SDK_DIR)
        message(FATAL_ERROR
            "CS2KIT_HL2SDK_DIR is unset - find_package(cs2-kit CONFIG REQUIRED) must run "
            "before cs2_add_plugin() so hl2sdk-cs2 is pulled in with it.")
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

    target_sources("${target_name}" PRIVATE
        "${CS2KIT_HL2SDK_DIR}/public/tier0/memoverride.cpp"
        "${CS2KIT_HL2SDK_DIR}/tier1/convar.cpp"
    )
    cs2kit_mark_vendored_sources(
        "${CS2KIT_HL2SDK_DIR}/public/tier0/memoverride.cpp"
        "${CS2KIT_HL2SDK_DIR}/tier1/convar.cpp"
    )
    # memoverride.cpp replaces global operator new/delete and convar.cpp defines what
    # convar.h declares; neither survives a unity TU or a force-included PCH.
    set_source_files_properties(
        "${CS2KIT_HL2SDK_DIR}/public/tier0/memoverride.cpp"
        "${CS2KIT_HL2SDK_DIR}/tier1/convar.cpp"
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON
    )

    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${ARG_INCLUDE_DIRS}
    )

    if(ARG_COMPONENTS)
        set(kit_targets)
        foreach(component IN LISTS ARG_COMPONENTS)
            if(NOT TARGET "CS2Kit::${component}")
                message(FATAL_ERROR
                    "cs2_add_plugin(${target_name} COMPONENTS ${component}): no such CS2Kit "
                    "component. Known: Core Utils Http Sdk Players Commands Menu Database App.")
            endif()
            list(APPEND kit_targets "CS2Kit::${component}")
        endforeach()
    else()
        set(kit_targets CS2Kit::CS2Kit)
    endif()

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

    cs2kit_set_sdk_warnings("${target_name}")
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
