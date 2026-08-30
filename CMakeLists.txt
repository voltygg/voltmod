cmake_minimum_required(VERSION 4.3)

project(voltmod LANGUAGES CXX)

option(VOLTMOD_ENABLE_POSTGRES "Build the VoltMod::Database Postgres client (requires libpqxx)" OFF)
option(VOLTMOD_DISABLE_PCH "Disable precompiled headers (e.g. to favor ccache hits over PCH on MSVC)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(VoltModCommon)
include(VoltModPlugin)
include(VoltModTests)

find_package(cpr CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

if(VOLTMOD_ENABLE_POSTGRES)
    find_package(libpqxx CONFIG REQUIRED)
endif()

find_package(hl2sdk-cs2 CONFIG REQUIRED)
find_package(metamod-source CONFIG REQUIRED)

# Include dir plus the header-only deps; all a test binary links.
add_library(voltmod-headers INTERFACE)
add_library(VoltMod::Headers ALIAS voltmod-headers)
target_include_directories(voltmod-headers INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(voltmod-headers INTERFACE glaze::glaze magic_enum::magic_enum)
target_compile_features(voltmod-headers INTERFACE cxx_std_23)

# Runtime is every source module except Database.
file(GLOB_RECURSE VOLTMOD_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER VOLTMOD_RUNTIME_SOURCES EXCLUDE REGEX "/src/Database/")
file(GLOB_RECURSE VOLTMOD_HTTP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")
file(GLOB_RECURSE VOLTMOD_DATABASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")

add_library(voltmod-runtime STATIC ${VOLTMOD_RUNTIME_SOURCES})
add_library(VoltMod::Runtime ALIAS voltmod-runtime)

voltmod_set_cxx_defaults(voltmod-runtime)
target_include_directories(voltmod-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# cpr stays out of public headers.
target_link_libraries(voltmod-runtime
    PUBLIC VoltMod::Headers VoltMod::Metamod VoltMod::HL2SDK
    PRIVATE cpr::cpr
)
hl2sdk_attach_engine_sources(voltmod-runtime)
hl2sdk_attach_generated_sources(voltmod-runtime)

if(WIN32)
    target_link_libraries(voltmod-runtime PUBLIC psapi)
endif()

if(NOT VOLTMOD_DISABLE_PCH)
    # No framework headers, so edits keep the PCH; protobuf's stable header, not generated ones.
    target_precompile_headers(voltmod-runtime PRIVATE
        <ISmmPlugin.h>
        <eiface.h>
        <icvar.h>
        <igameevents.h>
        <entity2/entitysystem.h>
        <tier1/convar.h>
        <google/protobuf/message.h>
        <glaze/json.hpp>
        <chrono>
        <format>
        <functional>
        <memory>
        <string>
        <string_view>
        <unordered_map>
        <vector>
    )
    # HTTP sources need winsock2.h before windows.h.
    set_source_files_properties(${VOLTMOD_HTTP_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

if(VOLTMOD_ENABLE_POSTGRES)
    add_library(voltmod-database STATIC ${VOLTMOD_DATABASE_SOURCES})
    add_library(VoltMod::Database ALIAS voltmod-database)
    voltmod_set_cxx_defaults(voltmod-database)
    target_include_directories(voltmod-database PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(voltmod-database PUBLIC VoltMod::Runtime libpqxx::pqxx)
    # Guards the pqxx-dependent API in Database/Api.hpp.
    target_compile_definitions(voltmod-database PUBLIC VOLTMOD_ENABLE_POSTGRES=1)
    if(NOT VOLTMOD_DISABLE_PCH)
        target_precompile_headers(voltmod-database PRIVATE <pqxx/pqxx> <format> <string> <vector>)
    endif()
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

# Dev-only plugins under tools/. Off so Conan builds never compile or install one; our presets set it.
option(VOLTMOD_BUILD_TOOLS "Build the dev-only tool plugins under tools/." OFF)
if(VOLTMOD_BUILD_TOOLS)
    add_subdirectory(tools/schema-dump)
endif()

# Cache builds have no tests/ and BUILD_TESTING off.
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    # Each Api.hpp must compile alone; compiling is the test.
    file(GLOB VOLTMOD_API_CHECKS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/Api/*.cpp")
    if(NOT VOLTMOD_ENABLE_POSTGRES)
        list(FILTER VOLTMOD_API_CHECKS EXCLUDE REGEX "/DatabaseApiSurfaceTest\\.cpp$")
    endif()
    add_library(voltmod-api-surface-check OBJECT ${VOLTMOD_API_CHECKS})
    target_compile_definitions(voltmod-api-surface-check PRIVATE VOLTMOD_API_SURFACE_CHECK)
    target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Runtime)
    if(VOLTMOD_ENABLE_POSTGRES)
        target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Database)
    endif()

    # The SDK-free TUs; everything else needs HL2SDK or pqxx.
    voltmod_add_tests(voltmod-utils-tests
        SOURCES
            src/Commands/CommandBuilder.cpp
            src/Commands/CommandRouter.cpp
            src/Commands/CommandSyntax.cpp
            src/Commands/ArgBinding.cpp
            src/Commands/Targeting.cpp
            src/Core/Capabilities.cpp
            src/Core/EffectManager.cpp
            src/Core/File.cpp
            src/Core/LoadReport.cpp
            src/Core/Logger.cpp
            src/Core/Paths.cpp
            src/Core/Scheduler.cpp
            src/Core/Strings.cpp
            src/Core/SteamId.cpp
            src/Core/Time.cpp
            src/Core/Translations.cpp
            src/Players/PlayerManager.cpp
            src/Players/Policy.cpp
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
            # One of these is empty on this platform.
            src/Engine/ModuleImage_Windows.cpp
            src/Engine/ModuleImage_Linux.cpp
            src/Engine/VtableLookup.cpp
            # VOLTMOD_VHOOK owns the SourceHook half.
            src/Unsafe/VtableHook.cpp
            # Run against a fake MenuSession.
            src/Menu/MenuBuilder.cpp
            src/Menu/MenuPresets.cpp
            src/Menu/CenterHtmlRender.cpp
            src/Menu/PanoramaIds.cpp
            src/Menu/MenuCursor.cpp
            src/Menu/PendingCommit.cpp
    )
    # Module scanning needs the OS loader.
    if(WIN32)
        target_link_libraries(voltmod-utils-tests PRIVATE psapi)
    endif()
endif()
