include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# '[', ']' and ';' break CTest's list of discovered names.
function(_voltmod_check_test_names)
    foreach(source IN LISTS ARGN)
        file(STRINGS "${source}" offenders REGEX "TEST_CASE[A-Z_]*\\(\"[^\"]*[][;]")
        if(offenders)
            message(FATAL_ERROR
                "${source}: TEST_CASE name contains '[', ']' or ';', which CTest discovery "
                "cannot round-trip. Spell it out instead.\n${offenders}")
        endif()
    endforeach()
endfunction()

# A doctest binary from tests/*.cpp plus SOURCES, linking VoltMod::Portable; DATABASE adds
# VoltMod::Database. SOURCES must build without the game SDK.
function(voltmod_add_tests target_name)
    if(NOT BUILD_TESTING)
        return()
    endif()
    cmake_parse_arguments(ARG "DATABASE" "" "SOURCES;DEFINITIONS" ${ARGN})

    if(NOT TARGET doctest::doctest)
        find_package(doctest REQUIRED)
    endif()
    if(NOT COMMAND doctest_discover_tests)
        include(doctest)
    endif()

    file(GLOB_RECURSE test_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # Needs HL2SDK; built by voltmod-api-surface-check.
    list(FILTER test_cases EXCLUDE REGEX "/tests/Api/")
    _voltmod_check_test_names(${test_cases})

    add_executable("${target_name}"
        ${test_cases}
        "${VOLTMOD_ROOT_DIR}/cmake/DoctestMain.cpp"
        ${ARG_SOURCES}
    )
    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries("${target_name}" PRIVATE doctest::doctest VoltMod::Portable)
    if(ARG_DATABASE)
        target_link_libraries("${target_name}" PRIVATE VoltMod::Database)
    endif()
    target_compile_definitions("${target_name}" PRIVATE ${ARG_DEFINITIONS})

    doctest_discover_tests("${target_name}")
endfunction()
