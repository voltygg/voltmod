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
find_package(magic_enum CONFIG REQUIRED)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Engine/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Entities/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Events/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Messaging/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Players/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Hooks/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Unsafe/*.cpp"
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
    LIBS VoltMod::Metamod VoltMod::HL2SDK nlohmann_json::nlohmann_json magic_enum::magic_enum
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

    # tests/ is not part of the Conan package's exports_sources, so this check - like
    # voltmod-utils-tests below - only exists for a source checkout (BUILD_TESTING is
    # forced off for a `conan create` cache build).
    if(BUILD_TESTING)
        # Each Api.hpp aggregate must compile as the only VoltMod include in its translation unit,
        # and <VoltMod/Api.hpp> itself must never reach nlohmann or the Menu-building surface - see
        # tests/Api/*SurfaceTest.cpp for what each TU checks. Linking VoltMod::Runtime (nothing here
        # calls into it) gives these TUs the same include paths, compile definitions and generated
        # HL2SDK headers voltmod-runtime itself needs, without repeating that setup; a passing build
        # is the whole check.
        add_library(voltmod-api-surface-check OBJECT
            tests/Api/RootApiSurfaceTest.cpp
            tests/Api/EntitiesApiSurfaceTest.cpp
            tests/Api/HooksApiSurfaceTest.cpp
            tests/Api/MenuApiSurfaceTest.cpp
            tests/Api/UnsafeApiSurfaceTest.cpp
        )
        target_compile_features(voltmod-api-surface-check PRIVATE cxx_std_23)
        target_compile_definitions(voltmod-api-surface-check PRIVATE VOLTMOD_API_SURFACE_CHECK)
        target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Runtime)

        if(VOLTMOD_ENABLE_POSTGRES)
            target_sources(voltmod-api-surface-check PRIVATE tests/Api/DatabaseApiSurfaceTest.cpp)
            target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Database)
        endif()
    endif()
endif()

# Recompile pure logic so tests do not link HL2SDK or Metamod.
if(BUILD_TESTING)
    # The gamedata parser and the module scanners are SDK-free but reach the OS loader.
    set(VOLTMOD_TEST_LIBRARIES nlohmann_json::nlohmann_json magic_enum::magic_enum)
    if(WIN32)
        list(APPEND VOLTMOD_TEST_LIBRARIES psapi)
    endif()

    voltmod_add_tests(voltmod-utils-tests
        SOURCES
            # Other implementation files require HL2SDK or pqxx.
            src/Commands/CommandBuilder.cpp
            src/Commands/CommandRouter.cpp
            src/Core/Capabilities.cpp
            src/Core/EffectManager.cpp
            src/Core/LoadReport.cpp
            src/Core/Logger.cpp
            src/Core/Paths.cpp
            src/App/PluginManifest.cpp
            # The field cache and its query seam; the ISchemaSystem walk behind it lives in
            # SchemaQuery.cpp, which is the SDK half and stays out of the tests.
            src/Entities/SchemaResolve.cpp
            src/Core/Scheduler.cpp
            src/Players/PlayerManager.cpp
            src/Players/Policy.cpp
            src/Commands/Targeting.cpp
            src/Hooks/ClientCvarPending.cpp
            src/Engine/Bindings.cpp
            src/Engine/ConVarSnapshots.cpp
            src/Engine/ConVarTypes.cpp
            src/Engine/GameData.cpp
            src/Engine/GameDataFile.cpp
            src/Engine/SigScanner.cpp
            src/Engine/VtableLookup.cpp
            # The install policy behind VtableHook; the SourceHook half lives in VOLTMOD_VHOOK.
            src/Unsafe/VtableHook.cpp
            src/Core/Strings.cpp
            src/Core/SteamId.cpp
            src/Core/Time.cpp
            src/Core/Translations.cpp
            # MenuRenderer is plain data (MenuView/MenuOption) + Translations, so it stays SDK-free.
            # MenuBuilder.hpp (and everything built on it - MenuBuilderRows.cpp, MenuOption.cpp,
            # MenuPresets.cpp) is not: Action/EffectDescriptor's ActionContext holds Controller by
            # value, which needs Pawn/Entity/Field<Vector,...> complete - true of MenuBuilder even
            # before this phase, since a context row has always taken an Action by const reference.
            src/Menu/MenuRenderer.cpp
        # ClientCvarPendingTests, GameDataFileTests, ConVarTests and FieldRefTests reach headers
        # under src/.
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        LIBRARIES ${VOLTMOD_TEST_LIBRARIES}
    )
endif()
