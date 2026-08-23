cmake_minimum_required(VERSION 4.3)

project(cs2-kit VERSION 1.0.0 LANGUAGES CXX)

option(CS2KIT_BUILD_STANDALONE "Build CS2Kit as a standalone project" ${PROJECT_IS_TOP_LEVEL})
option(CS2KIT_ENABLE_POSTGRES "Build the CS2Kit::Database Postgres client (requires libpqxx)" OFF)
option(CS2KIT_DISABLE_PCH "Disable precompiled headers (e.g. to favor ccache hits over PCH on MSVC)" OFF)

# Conan imported targets are directory-scoped; make them visible to sibling
# plugin directories when cs2-kit is vendored into a monorepo.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CS2KitSdk)
include(CS2KitLibrary)
include(CS2Plugin)
include(CS2Tests)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(CS2KIT_ENABLE_POSTGRES)
    find_package(libpqxx CONFIG REQUIRED)
endif()

cs2kit_find_sdk()

# Two libraries, not nine. The module directories under src/ are still the architecture -
# scripts/cs2kit/modgraph.py checks the layering between them - but they were never
# independently selectable, so mirroring them as CMake targets and Conan components only
# meant the same dependency list was written out three times. Database stays separate
# because it is the one thing a plugin can genuinely do without.
file(GLOB_RECURSE CS2KIT_RUNTIME_SOURCES CONFIGURE_DEPENDS
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
list(APPEND CS2KIT_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Runtime.cpp")

file(GLOB_RECURSE CS2KIT_DATABASE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")
file(GLOB_RECURSE CS2KIT_Http_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")

# SDK bodies that ship as source only. entityidentity is needed even though nothing calls
# it directly: entitysystem.cpp references NameMatches, and omitting it fails the Linux .so
# dlopen on an undefined symbol. tier1 is here because hl2sdk dropped its prebuilt libs;
# convar.cpp is excluded because cs2_add_plugin compiles it per module, each module owning
# its own ConVar registration state, and bitbuf/tokenreader because hl2sdk deleted the
# headers they include.
set(CS2KIT_HL2SDK_SOURCES
    "${CS2KIT_HL2SDK_DIR}/entity2/entityidentity.cpp"
    "${CS2KIT_HL2SDK_DIR}/entity2/entitykeyvalues.cpp"
    "${CS2KIT_HL2SDK_DIR}/entity2/entitysystem.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/keyvalues3.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/rangecheckedvar.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/utlbufferutil.cpp"
)

cs2kit_add_library(Runtime
    SOURCES ${CS2KIT_RUNTIME_SOURCES}
    VENDORED ${CS2KIT_HL2SDK_SOURCES} ${CS2KIT_HL2SDK_PROTO_SOURCES}
    LIBS CS2Kit::Metamod CS2Kit::HL2SDK nlohmann_json::nlohmann_json
    PRIVATE_LIBS cpr::cpr)

# cpr never appears in a public header, so it stays off the include path of everything
# that links the kit. src/Http/*.cpp is also kept out of the precompiled header: forcing
# the hl2sdk universe ahead of <cpr/cpr.h> puts windows.h before winsock2.h.
set_source_files_properties(${CS2KIT_Http_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(WIN32)
    target_link_libraries(cs2-kit-runtime PUBLIC psapi)
endif()

if(CS2KIT_ENABLE_POSTGRES)
    cs2kit_add_library(Database
        SOURCES ${CS2KIT_DATABASE_SOURCES}
        DEPS CS2Kit::Runtime
        LIBS libpqxx::pqxx)
    # Consumer feature checks and Database/Api.hpp's #error guard read this; only that
    # header pulls in <pqxx/pqxx>.
    target_compile_definitions(cs2-kit-database PUBLIC CS2KIT_ENABLE_POSTGRES=1)
endif()

# What a plugin links unless it asks for a feature.
add_library(cs2-kit INTERFACE)
add_library(CS2Kit::CS2Kit ALIAS cs2-kit)
target_link_libraries(cs2-kit INTERFACE CS2Kit::Runtime)
if(CS2KIT_ENABLE_POSTGRES)
    target_link_libraries(cs2-kit INTERFACE CS2Kit::Database)
endif()

if(CS2KIT_BUILD_STANDALONE)
    include(CTest)
endif()

# SDK-free utils tests: recompile pure-logic sources instead of linking cs2-kit,
# so the test binary needs no HL2SDK/Metamod link inputs.
if(BUILD_TESTING)
    cs2_add_tests(cs2kit-utils-tests
        SOURCES
            # SDK-free TUs, hand-picked: the rest of src/ needs HL2SDK or pqxx.
            src/Core/EffectManager.cpp
            src/Core/Paths.cpp
            src/Core/PluginManifest.cpp
            src/Core/ScheduledEffect.cpp
            src/Core/Scheduler.cpp
            src/Players/Targeting.cpp
            src/Sdk/ClientCvarPending.cpp
            src/Core/StringUtils.cpp
            src/Core/SteamId.cpp
            src/Core/TimeUtils.cpp
        # For PluginManifest's JSON parsing.
        LIBRARIES nlohmann_json::nlohmann_json
    )
endif()
