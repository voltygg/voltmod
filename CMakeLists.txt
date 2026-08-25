cmake_minimum_required(VERSION 4.3)

project(voltmod LANGUAGES CXX)

option(VOLTMOD_BUILD_STANDALONE "Build VoltMod as a standalone project" ${PROJECT_IS_TOP_LEVEL})
option(VOLTMOD_ENABLE_POSTGRES "Build the VoltMod::Database Postgres client (requires libpqxx)" OFF)
option(VOLTMOD_DISABLE_PCH "Disable precompiled headers (e.g. to favor ccache hits over PCH on MSVC)" OFF)

# Imported targets must be global when sibling plugin directories consume them.
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

# Source modules define include boundaries, while Runtime and optional Database
# are the link units.
file(GLOB_RECURSE VOLTMOD_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Sdk/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Players/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Commands/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Menu/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.cpp"
)
# Runtime.cpp is the service composition root.
list(APPEND VOLTMOD_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Runtime.cpp")

file(GLOB_RECURSE VOLTMOD_DATABASE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")
file(GLOB_RECURSE VOLTMOD_Http_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")

voltmod_add_library(Runtime
    SOURCES ${VOLTMOD_RUNTIME_SOURCES}
    LIBS VoltMod::Metamod VoltMod::HL2SDK nlohmann_json::nlohmann_json
    PRIVATE_LIBS cpr::cpr)

# The HL2SDK package attaches its required sources and compile properties.
hl2sdk_attach_engine_sources(voltmod-runtime)
hl2sdk_attach_generated_sources(voltmod-runtime)

# Keep cpr private. HTTP sources skip the PCH to preserve winsock2/windows.h order.
set_source_files_properties(${VOLTMOD_Http_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(WIN32)
    target_link_libraries(voltmod-runtime PUBLIC psapi)
endif()

if(VOLTMOD_ENABLE_POSTGRES)
    voltmod_add_library(Database
        SOURCES ${VOLTMOD_DATABASE_SOURCES}
        DEPS VoltMod::Runtime
        LIBS libpqxx::pqxx)
# Database/Api.hpp uses this definition to guard pqxx-dependent API.
    target_compile_definitions(voltmod-database PUBLIC VOLTMOD_ENABLE_POSTGRES=1)
endif()

# Default plugin target.
add_library(voltmod INTERFACE)
add_library(VoltMod::VoltMod ALIAS voltmod)
target_link_libraries(voltmod INTERFACE VoltMod::Runtime)
if(VOLTMOD_ENABLE_POSTGRES)
    target_link_libraries(voltmod INTERFACE VoltMod::Database)
endif()

# CMakeDeps generates the package config, so do not install an export set.
install(TARGETS voltmod-runtime ARCHIVE DESTINATION lib)
if(TARGET voltmod-database)
    install(TARGETS voltmod-database ARCHIVE DESTINATION lib)
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION include)
# Preserve the relative layout used by VOLTMOD_ROOT_DIR.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" DESTINATION cmake)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gamedata/" DESTINATION gamedata)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates/plugin/" DESTINATION templates/plugin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION licenses)

if(VOLTMOD_BUILD_STANDALONE)
    include(CTest)
endif()

# Recompile pure logic so tests do not link HL2SDK or Metamod.
if(BUILD_TESTING)
    voltmod_add_tests(voltmod-utils-tests
        SOURCES
            # Other implementation files require HL2SDK or pqxx.
            src/Commands/CommandSpec.cpp
            src/Core/EffectManager.cpp
            src/Core/Paths.cpp
            src/Core/PluginManifest.cpp
            src/Core/ScheduledEffect.cpp
            src/Core/Scheduler.cpp
            src/Players/Player.cpp
            src/Players/PlayerManager.cpp
            src/Players/Targeting.cpp
            src/Sdk/Client/ClientCvarPending.cpp
            src/Core/StringUtils.cpp
            src/Core/SteamId.cpp
            src/Core/TimeUtils.cpp
        # For PluginManifest's JSON parsing.
        LIBRARIES nlohmann_json::nlohmann_json
    )
endif()
