include_guard(GLOBAL)

# Consumer test API:
#   voltmod_add_tests(<name> [SOURCES ...] [FEATURES DATABASE])
# Globs tests/*.cpp and recompiles the SDK-free SOURCES beside them against src/ and
# VoltMod::Headers only. FEATURES DATABASE additionally links VoltMod::Database, so a test
# can open a SQLite database and run the plugin's own migrations. No-op when BUILD_TESTING
# is off.

include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# '[', ']' and ';' break CTest's list of discovered names.
function(voltmod_verify_test_names)
    foreach(source IN LISTS ARGN)
        file(STRINGS "${source}" offenders REGEX "TEST_CASE[A-Z_]*\\(\"[^\"]*[][;]")
        if(offenders)
            message(FATAL_ERROR
                "${source}: TEST_CASE name contains '[', ']' or ';', which CTest discovery "
                "cannot round-trip. Spell it out instead.\n${offenders}")
        endif()
    endforeach()
endfunction()

function(voltmod_add_tests target_name)
    if(NOT BUILD_TESTING)
        return()
    endif()
    cmake_parse_arguments(ARG "" "" "SOURCES;FEATURES" ${ARGN})

    if(NOT TARGET doctest::doctest)
        find_package(doctest REQUIRED)
    endif()
    if(NOT COMMAND doctest_discover_tests)
        include(doctest)
    endif()

    file(GLOB_RECURSE test_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # Needs HL2SDK; built by voltmod-api-surface-check.
    list(FILTER test_cases EXCLUDE REGEX "/tests/Api/")
    voltmod_verify_test_names(${test_cases})

    add_executable("${target_name}"
        ${test_cases}
        "${VOLTMOD_ROOT_DIR}/cmake/DoctestMain.cpp"
        ${ARG_SOURCES}
    )
    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    set(link_targets doctest::doctest VoltMod::Headers)
    voltmod_apply_features(voltmod_add_tests "${target_name}" "${ARG_FEATURES}" link_targets)
    target_link_libraries("${target_name}" PRIVATE ${link_targets})

    doctest_discover_tests("${target_name}")
endfunction()
