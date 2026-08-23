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
include(CS2KitComponent)
include(CS2Plugin)
include(CS2Tests)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(CS2KIT_ENABLE_POSTGRES)
    find_package(libpqxx CONFIG REQUIRED)
endif()

cs2kit_find_sdk()

# One static library per module. The layering is enforced by the dependency scan in
# scripts/; DEPS below must stay a DAG, downward only.
foreach(module Core Utils Http Sdk Players Commands Menu Database App)
    file(GLOB_RECURSE CS2KIT_${module}_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/${module}/*.cpp")
endforeach()
# Runtime.cpp is the composition root - the flat service container plus the one
# ambient pointer the lower modules reach it through - so it belongs with App.
list(APPEND CS2KIT_App_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Runtime.cpp")

# SDK bodies that ship as source only, compiled into the Sdk component. entityidentity
# is needed even though nothing calls it directly: entitysystem.cpp references
# NameMatches, and omitting it fails the Linux .so dlopen on an undefined symbol. tier1
# is here because hl2sdk dropped its prebuilt libs; convar.cpp is excluded because
# cs2_add_plugin compiles it per module, each module owning its own ConVar registration
# state, and bitbuf/tokenreader because hl2sdk deleted the headers they include.
set(CS2KIT_HL2SDK_SOURCES
    "${CS2KIT_HL2SDK_DIR}/entity2/entityidentity.cpp"
    "${CS2KIT_HL2SDK_DIR}/entity2/entitykeyvalues.cpp"
    "${CS2KIT_HL2SDK_DIR}/entity2/entitysystem.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/keyvalues3.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/rangecheckedvar.cpp"
    "${CS2KIT_HL2SDK_DIR}/tier1/utlbufferutil.cpp"
)

# HL2SDK and nlohmann are third-party, not CS2Kit modules, so Core still depends on no
# layer: ConsoleLogger writes through the engine console and the manifest parser is JSON.
cs2kit_add_component(Core
    SOURCES ${CS2KIT_Core_SOURCES}
    LIBS CS2Kit::Metamod CS2Kit::HL2SDK nlohmann_json::nlohmann_json)

cs2kit_add_component(Utils
    SOURCES ${CS2KIT_Utils_SOURCES}
    DEPS CS2Kit::Core
    LIBS nlohmann_json::nlohmann_json)

cs2kit_add_component(Http
    SOURCES ${CS2KIT_Http_SOURCES}
    DEPS CS2Kit::Utils
    LIBS cpr::cpr)

# The SDK bodies and the generated protobufs ride here: this is the only component
# that speaks to the engine, and they are what it speaks through.
cs2kit_add_component(Sdk PCH
    SOURCES ${CS2KIT_Sdk_SOURCES}
    VENDORED ${CS2KIT_HL2SDK_SOURCES} ${CS2KIT_HL2SDK_PROTO_SOURCES}
    DEPS CS2Kit::Core CS2Kit::Utils)

cs2kit_add_component(Players PCH
    SOURCES ${CS2KIT_Players_SOURCES}
    DEPS CS2Kit::Sdk CS2Kit::Core CS2Kit::Utils)

cs2kit_add_component(Commands PCH
    SOURCES ${CS2KIT_Commands_SOURCES}
    DEPS CS2Kit::Players CS2Kit::Sdk CS2Kit::Core CS2Kit::Utils)

cs2kit_add_component(Menu PCH
    SOURCES ${CS2KIT_Menu_SOURCES}
    DEPS CS2Kit::Players CS2Kit::Sdk CS2Kit::Core CS2Kit::Utils)

if(CS2KIT_ENABLE_POSTGRES)
    cs2kit_add_component(Database
        SOURCES ${CS2KIT_Database_SOURCES}
        DEPS CS2Kit::Core CS2Kit::Utils
        LIBS libpqxx::pqxx)
    # Consumer feature checks and Database/Api.hpp's #error guard read this; only that
    # header pulls in <pqxx/pqxx>.
    target_compile_definitions(cs2-kit-database PUBLIC CS2KIT_ENABLE_POSTGRES=1)
endif()

# The composition root: the only component allowed to depend on all the others.
cs2kit_add_component(App PCH
    SOURCES ${CS2KIT_App_SOURCES}
    DEPS CS2Kit::Commands CS2Kit::Menu CS2Kit::Players CS2Kit::Sdk CS2Kit::Http
         CS2Kit::Core CS2Kit::Utils)

if(WIN32)
    target_link_libraries(cs2-kit-sdk PUBLIC psapi)
endif()

# What a plugin links unless it names components: everything, App first so the
# composition root's objects are pulled before the layers they reference.
add_library(cs2-kit INTERFACE)
add_library(CS2Kit::CS2Kit ALIAS cs2-kit)
target_link_libraries(cs2-kit INTERFACE CS2Kit::App)
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
            src/Utils/StringUtils.cpp
            src/Utils/SteamId.cpp
            src/Utils/TimeUtils.cpp
        # For PluginManifest's JSON parsing.
        LIBRARIES nlohmann_json::nlohmann_json
    )
endif()
