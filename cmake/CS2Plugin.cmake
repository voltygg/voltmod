include_guard(GLOBAL)

# Consumer-facing plugin API. Included by cs2-kit's root CMakeLists, so after
# add_subdirectory(vendor/cs2-kit) any project can call:
#   cs2_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...]
#                  [PCH_HEADERS ...] [UNITY])

# CS2KitSdk provides CS2KIT_ROOT_DIR / _HL2SDK_DIR / _PLATFORM_ARCH / _GAMEDATA_DIR
# and cs2kit_mark_vendored_sources; include_guard(GLOBAL) makes the repeat free.
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitSdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitBuildInfo.cmake")

# Create a Metamod plugin MODULE linked against CS2Kit, with output dirs and
# install rules. SOURCES defaults to a glob of src/*.cpp; INCLUDE_DIRS and
# LIBRARIES are appended to the defaults. PCH_HEADERS extends the plugin's
# precompiled header (e.g. "<pqxx/pqxx>"). UNITY enables jumbo compilation -
# it needs file-unique names for namespace-scope statics (self-registration
# blocks); Registry<T> items still work, every source is compiled and linked.
function(cs2_add_plugin target_name)
    cmake_parse_arguments(ARG "UNITY" "" "SOURCES;INCLUDE_DIRS;LIBRARIES;PCH_HEADERS" ${ARGN})

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

    target_link_libraries("${target_name}" PRIVATE
        CS2Kit::CS2Kit
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

    cs2_install_plugin("${target_name}")
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
