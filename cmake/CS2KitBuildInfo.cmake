include_guard(GLOBAL)

# For CS2KIT_ROOT_DIR; include_guard(GLOBAL) makes the repeat include free.
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitSdk.cmake")

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
