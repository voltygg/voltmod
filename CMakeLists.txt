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

# Modules define include boundaries; Runtime and optional Database provide link targets.
file(GLOB_RECURSE VOLTMOD_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Engine/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Entities/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Events/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Messaging/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Players/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Hooks/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Workshop/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Unsafe/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Commands/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Menu/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.cpp"
)
list(APPEND VOLTMOD_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Runtime.cpp")

file(GLOB_RECURSE VOLTMOD_DATABASE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")
file(GLOB_RECURSE VOLTMOD_Http_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")

voltmod_add_library(Runtime
    SOURCES ${VOLTMOD_RUNTIME_SOURCES}
    LIBS VoltMod::Metamod VoltMod::HL2SDK nlohmann_json::nlohmann_json magic_enum::magic_enum
    PRIVATE_LIBS cpr::cpr)

hl2sdk_attach_engine_sources(voltmod-runtime)
hl2sdk_attach_generated_sources(voltmod-runtime)

# Preserve the winsock2/windows.h include order in HTTP sources.
set_source_files_properties(${VOLTMOD_Http_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(WIN32)
    target_link_libraries(voltmod-runtime PUBLIC psapi)
endif()

if(VOLTMOD_ENABLE_POSTGRES)
    voltmod_add_library(Database
        SOURCES ${VOLTMOD_DATABASE_SOURCES}
        DEPS VoltMod::Runtime
        LIBS libpqxx::pqxx)
    # Guards the pqxx-dependent API in Database/Api.hpp.
    target_compile_definitions(voltmod-database PUBLIC VOLTMOD_ENABLE_POSTGRES=1)
endif()

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
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" DESTINATION cmake)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gamedata/" DESTINATION gamedata)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/panorama/" DESTINATION panorama)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates/plugin/" DESTINATION templates/plugin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION licenses)

if(VOLTMOD_BUILD_STANDALONE)
    include(CTest)

    # Conan excludes tests/ and disables BUILD_TESTING for cache builds.
    if(BUILD_TESTING)
        # Verify each Api.hpp compiles alone and the root API does not expose
        # nlohmann or Menu builders. Runtime supplies their build settings and
        # generated HL2SDK headers, but they do not call it; compiling is the test.
        add_library(voltmod-api-surface-check OBJECT
            tests/Api/RootApiSurfaceTest.cpp
            tests/Api/EntitiesApiSurfaceTest.cpp
            tests/Api/HooksApiSurfaceTest.cpp
            tests/Api/UiApiSurfaceTest.cpp
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

# Recompile pure logic without linking HL2SDK or Metamod.
if(BUILD_TESTING)
    # SDK-free gamedata and module scanning still need the OS loader.
    set(VOLTMOD_TEST_LIBRARIES nlohmann_json::nlohmann_json magic_enum::magic_enum)
    if(WIN32)
        list(APPEND VOLTMOD_TEST_LIBRARIES psapi)
    endif()

    voltmod_add_tests(voltmod-utils-tests
        SOURCES
            # Other implementations require HL2SDK or pqxx.
            src/Commands/CommandBuilder.cpp
            src/Commands/CommandRouter.cpp
            src/Commands/CommandSyntax.cpp
            src/Commands/ArgBinding.cpp
            src/Core/Capabilities.cpp
            src/Core/EffectManager.cpp
            src/Core/LoadReport.cpp
            src/Core/Logger.cpp
            src/Core/Paths.cpp
            # The field cache and query seam are SDK-free; SchemaQuery.cpp is not.
            src/Entities/SchemaResolve.cpp
            src/Core/Scheduler.cpp
            src/Players/PlayerManager.cpp
            src/Players/Policy.cpp
            src/Commands/Targeting.cpp
            src/Hooks/ClientCvarPending.cpp
            src/Workshop/AddonRequirements.cpp
            src/Ui/LayoutName.cpp
            src/Ui/UiWriteCache.cpp
            src/Ui/UiClickRouting.cpp
            src/Ui/ClickPayload.cpp
            src/Engine/Bindings.cpp
            src/Engine/ConVarTypes.cpp
            src/Engine/GameData.cpp
            src/Engine/GameDataFile.cpp
            src/Engine/BytePattern.cpp
            src/Engine/SigScanner.cpp
            src/Engine/VtableLookup.cpp
            # VtableHook policy is SDK-free; VOLTMOD_VHOOK owns the SourceHook half.
            src/Unsafe/VtableHook.cpp
            src/Core/Strings.cpp
            src/Core/SteamId.cpp
            src/Core/Time.cpp
            src/Core/Translations.cpp
            # The row model is plain values and MenuSession is an abstract class with no engine
            # behind it, so rows, flows, the center-HTML markup and the layout's id contract all
            # recompile here against a fake session.
            src/Menu/MenuBuilder.cpp
            src/Menu/MenuPresets.cpp
            src/Menu/CenterHtmlRender.cpp
            src/Menu/PanoramaIds.cpp
            # The cursor is index arithmetic over rows that answer whether they take it, so it
            # runs here against a table rather than a session.
            src/Menu/MenuCursor.cpp
            # The step debounce takes its delay as a callable, so the policy runs here against a
            # hand-fired clock rather than a scheduler.
            src/Menu/PendingCommit.cpp
        # Some tests include private headers from src/.
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        LIBRARIES ${VOLTMOD_TEST_LIBRARIES}
    )
endif()
