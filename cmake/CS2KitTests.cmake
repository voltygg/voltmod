include_guard(GLOBAL)

# Consumer-facing test API. Included by cs2-kit's root CMakeLists, so after
# add_subdirectory(vendor/cs2-kit) any project can call:
#   cs2_add_tests(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...])
# SOURCES are the SDK-free TUs to recompile; tests/*.cpp is globbed automatically.

# For CS2KIT_ROOT_DIR; include_guard(GLOBAL) makes the repeat include free.
include("${CMAKE_CURRENT_LIST_DIR}/CS2KitCommon.cmake")

# doctest hands test names to CTest through a CMake list, where '[' and ']' group and
# ';' separates. An unmatched bracket aborts configure with an unrelated-looking
# "add_test called with incorrect number of arguments"; a ';' silently splits one case
# into two bogus entries. Fail here instead, pointing at the file.
function(cs2_verify_test_names)
    foreach(source IN LISTS ARGN)
        file(STRINGS "${source}" offenders REGEX "TEST_CASE[A-Z_]*\\(\"[^\"]*[][;]")
        if(offenders)
            message(FATAL_ERROR
                "${source}: TEST_CASE name contains '[', ']' or ';', which CTest discovery "
                "cannot round-trip. Spell it out instead.\n${offenders}")
        endif()
    endforeach()
endfunction()

# Test executable that recompiles pure-logic TUs rather than linking the plugin or the
# kit, so nothing pulls in HL2SDK/Metamod. One CTest entry per test case.
function(cs2_add_tests target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRS;LIBRARIES" ${ARGN})

    if(NOT TARGET doctest::doctest)
        find_package(doctest REQUIRED)
    endif()
    if(NOT COMMAND doctest_discover_tests)
        include(doctest)
    endif()

    file(GLOB test_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    cs2_verify_test_names(${test_cases})

    add_executable("${target_name}"
        ${test_cases}
        "${CS2KIT_ROOT_DIR}/cmake/DoctestMain.cpp"
        ${ARG_SOURCES}
    )
    target_compile_features("${target_name}" PRIVATE cxx_std_23)
    target_include_directories("${target_name}" PRIVATE
        "${CS2KIT_ROOT_DIR}/include"
        ${ARG_INCLUDE_DIRS}
    )
    target_link_libraries("${target_name}" PRIVATE doctest::doctest ${ARG_LIBRARIES})

    doctest_discover_tests("${target_name}")
endfunction()
