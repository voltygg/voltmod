include_guard(GLOBAL)

# Consumer test API delivered as a CMakeDeps build module:
#   voltmod_add_tests(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...])
# SOURCES are the SDK-free TUs to recompile; tests/*.cpp is globbed automatically.

# For VOLTMOD_ROOT_DIR; include_guard(GLOBAL) makes the repeat include free.
include("${CMAKE_CURRENT_LIST_DIR}/VoltModCommon.cmake")

# doctest passes names through a CMake list, where brackets group and semicolons
# split entries. Reject them before discovery corrupts the test list.
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

# Recompile pure logic without the plugin, framework, HL2SDK, or Metamod.
function(voltmod_add_tests target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRS;LIBRARIES" ${ARGN})

    if(NOT TARGET doctest::doctest)
        find_package(doctest REQUIRED)
    endif()
    if(NOT COMMAND doctest_discover_tests)
        include(doctest)
    endif()

    file(GLOB test_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    voltmod_verify_test_names(${test_cases})

    add_executable("${target_name}"
        ${test_cases}
        "${VOLTMOD_ROOT_DIR}/cmake/DoctestMain.cpp"
        ${ARG_SOURCES}
    )
    target_compile_features("${target_name}" PRIVATE cxx_std_23)
    target_include_directories("${target_name}" PRIVATE
        "${VOLTMOD_ROOT_DIR}/include"
        ${ARG_INCLUDE_DIRS}
    )
    target_link_libraries("${target_name}" PRIVATE doctest::doctest ${ARG_LIBRARIES})

    doctest_discover_tests("${target_name}")
endfunction()
