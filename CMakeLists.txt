cmake_minimum_required(VERSION 4.3)

project(voltmod LANGUAGES CXX)

option(VOLTMOD_BUILD_STANDALONE "Build VoltMod as a standalone project" ${PROJECT_IS_TOP_LEVEL})
option(VOLTMOD_ENABLE_POSTGRES "Build the VoltMod::Database Postgres client (requires libpqxx)" OFF)
option(VOLTMOD_DISABLE_PCH "Disable precompiled headers (e.g. to favor ccache hits over PCH on MSVC)" OFF)

# Conan imported targets are directory-scoped; make them visible to sibling
# plugin directories when voltmod is vendored into a monorepo.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(VoltModCommon)
include(VoltModLibrary)
include(VoltModPlugin)
include(VoltModTests)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(VOLTMOD_ENABLE_POSTGRES)
    find_package(libpqxx CONFIG REQUIRED)
endif()

find_package(hl2sdk-cs2 CONFIG REQUIRED)
find_package(metamod-source CONFIG REQUIRED)

# Two libraries, not nine. The module directories under src/ are still the architecture -
# scripts/voltmod/modgraph.py checks the layering between them - but they were never
# independently selectable, so mirroring them as CMake targets and Conan components only
# meant the same dependency list was written out three times. Database stays separate
# because it is the one thing a plugin can genuinely do without.
file(GLOB_RECURSE VOLTMOD_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Sdk/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Players/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Commands/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Menu/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.cpp"
)
# Runtime.cpp is the composition root - the flat service container plus the one ambient
# pointer the lower modules reach it through.
list(APPEND VOLTMOD_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Runtime.cpp")

file(GLOB_RECURSE VOLTMOD_DATABASE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")
file(GLOB_RECURSE VOLTMOD_Http_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")

voltmod_add_library(Runtime
    SOURCES ${VOLTMOD_RUNTIME_SOURCES}
    LIBS VoltMod::Metamod VoltMod::HL2SDK nlohmann_json::nlohmann_json
    PRIVATE_LIBS cpr::cpr)

# Which SDK bodies exist, where they live and how they must be compiled is the hl2sdk-cs2
# package's business; it attaches them, warnings and PCH exclusions included.
hl2sdk_attach_engine_sources(voltmod-runtime)
hl2sdk_attach_generated_sources(voltmod-runtime)

# cpr never appears in a public header, so it stays off the include path of everything
# that links the framework. src/Http/*.cpp is also kept out of the precompiled header: forcing
# the hl2sdk universe ahead of <cpr/cpr.h> puts windows.h before winsock2.h.
set_source_files_properties(${VOLTMOD_Http_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(WIN32)
    target_link_libraries(voltmod-runtime PUBLIC psapi)
endif()

if(VOLTMOD_ENABLE_POSTGRES)
    voltmod_add_library(Database
        SOURCES ${VOLTMOD_DATABASE_SOURCES}
        DEPS VoltMod::Runtime
        LIBS libpqxx::pqxx)
    # Consumer feature checks and Database/Api.hpp's #error guard read this; only that
    # header pulls in <pqxx/pqxx>.
    target_compile_definitions(voltmod-database PUBLIC VOLTMOD_ENABLE_POSTGRES=1)
endif()

# What a plugin links unless it asks for a feature.
add_library(voltmod INTERFACE)
add_library(VoltMod::VoltMod ALIAS voltmod)
target_link_libraries(voltmod INTERFACE VoltMod::Runtime)
if(VOLTMOD_ENABLE_POSTGRES)
    target_link_libraries(voltmod INTERFACE VoltMod::Database)
endif()

# Installation is CMake's to describe, so the Conan recipe only has to run it. Note there
# is deliberately no install(EXPORT): CMakeDeps generates the package config, and an
# installed export set would compete with it.
install(TARGETS voltmod-runtime ARCHIVE DESTINATION lib)
if(TARGET voltmod-database)
    install(TARGETS voltmod-database ARCHIVE DESTINATION lib)
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION include)
# cmake/ lands next to gamedata/ so VOLTMOD_ROOT_DIR (the cmake dir's parent) resolves
# plugin.vdf.in, DoctestMain.cpp and gamedata exactly as it does in the repo.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" DESTINATION cmake)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gamedata/" DESTINATION gamedata)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates/plugin/" DESTINATION templates/plugin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION licenses)

if(VOLTMOD_BUILD_STANDALONE)
    include(CTest)
endif()

# SDK-free utils tests: recompile pure-logic sources instead of linking voltmod,
# so the test binary needs no HL2SDK/Metamod link inputs.
if(BUILD_TESTING)
    voltmod_add_tests(voltmod-utils-tests
        SOURCES
            # SDK-free TUs, hand-picked: the rest of src/ needs HL2SDK or pqxx.
            src/Commands/CommandSpec.cpp
            src/Core/EffectManager.cpp
            src/Core/Paths.cpp
            src/Core/PluginManifest.cpp
            src/Core/ScheduledEffect.cpp
            src/Core/Scheduler.cpp
            src/Players/Player.cpp
            src/Players/PlayerManager.cpp
            src/Players/Targeting.cpp
            src/Sdk/ClientCvarPending.cpp
            src/Core/StringUtils.cpp
            src/Core/SteamId.cpp
            src/Core/TimeUtils.cpp
        # For PluginManifest's JSON parsing.
        LIBRARIES nlohmann_json::nlohmann_json
    )
endif()
