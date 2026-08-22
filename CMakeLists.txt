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
include(CS2Plugin)
include(CS2Tests)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(CS2KIT_ENABLE_POSTGRES)
    find_package(libpqxx CONFIG REQUIRED)
endif()

cs2kit_configure_sdk()
cs2kit_generate_sdk_protobuf(CS2KIT_PROTO_SOURCES CS2KIT_PROTO_INCLUDE_DIRS)

file(GLOB_RECURSE CS2KIT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
if(NOT CS2KIT_ENABLE_POSTGRES)
    list(FILTER CS2KIT_SOURCES EXCLUDE REGEX "src/Database/")
endif()

# SDK bodies that ship as source only. entityidentity is needed even though nothing
# calls it directly: entitysystem.cpp references NameMatches, and omitting it fails
# the Linux .so dlopen on an undefined symbol. tier1 is here because hl2sdk dropped
# its prebuilt libs; convar.cpp is excluded because cs2_add_plugin compiles it per
# module, each module owning its own ConVar registration state, and bitbuf/tokenreader
# because hl2sdk deleted the headers they include.
set(CS2KIT_HL2SDK_SOURCES
    "${CS2KIT_HL2SDK_DIR}/entity2/entityidentity.cpp"
    "${CS2KIT_HL2SDK_DIR}/entity2/entitykeyvalues.cpp"
    "${CS2KIT_HL2SDK_DIR}/entity2/entitysystem.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/keyvalues3.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/rangecheckedvar.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/utlbufferutil.cpp"
)

add_library(cs2-kit STATIC
    ${CS2KIT_SOURCES}
    ${CS2KIT_HL2SDK_SOURCES}
    ${CS2KIT_PROTO_SOURCES}
)
add_library(CS2Kit::CS2Kit ALIAS cs2-kit)

cs2kit_mark_vendored_sources(${CS2KIT_HL2SDK_SOURCES} ${CS2KIT_PROTO_SOURCES})

if(NOT CS2KIT_DISABLE_PCH)
    # Third-party headers only: editing a kit header must not invalidate the PCH.
    # <google/protobuf/message.h> rather than the generated .pb.h so it does not
    # depend on protoc outputs. A TU the SDK macros break opts out per-file with
    # SKIP_PRECOMPILE_HEADERS.
    target_precompile_headers(cs2-kit PRIVATE
        <ISmmPlugin.h>
        <eiface.h>
        <icvar.h>
        <igameevents.h>
        <entity2/entitysystem.h>
        <tier1/convar.h>
        <google/protobuf/message.h>
        <nlohmann/json.hpp>
        <chrono>
        <format>
        <functional>
        <memory>
        <string>
        <string_view>
        <unordered_map>
        <vector>
    )
    # Vendored/generated TUs keep their own include order.
    set_source_files_properties(${CS2KIT_HL2SDK_SOURCES} ${CS2KIT_PROTO_SOURCES}
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

target_compile_features(cs2-kit PUBLIC cxx_std_23)
set_target_properties(cs2-kit PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    OUTPUT_NAME cs2-kit
)

# /Z7 so kit frames appear in plugin crash-dump PDBs (see cs2_add_plugin).
target_compile_options(cs2-kit PRIVATE
    "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/Z7>"
)

target_include_directories(cs2-kit
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${CS2KIT_PROTO_INCLUDE_DIRS}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(cs2-kit
    PUBLIC
        CS2Kit::Metamod
        CS2Kit::HL2SDK
        cpr::cpr
        nlohmann_json::nlohmann_json
)

if(CS2KIT_ENABLE_POSTGRES)
    # PUBLIC link: cs2-kit is static, so DB-using plugins need pqxx at link time.
    # PUBLIC define: consumer feature checks and Database/Api.hpp's #error guard
    # read the macro; only that header pulls in <pqxx/pqxx>.
    target_link_libraries(cs2-kit PUBLIC libpqxx::pqxx)
    target_compile_definitions(cs2-kit PUBLIC CS2KIT_ENABLE_POSTGRES=1)
endif()

if(WIN32)
    target_link_libraries(cs2-kit PUBLIC psapi)
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
            src/Utils/StringUtils.cpp
            src/Utils/SteamId.cpp
            src/Utils/TimeUtils.cpp
        # For PluginManifest's JSON parsing.
        LIBRARIES nlohmann_json::nlohmann_json
    )
endif()
