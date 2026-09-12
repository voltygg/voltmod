cmake_minimum_required(VERSION 4.3)

project(voltmod LANGUAGES CXX)

option(VOLTMOD_ENABLE_DATABASE "Build the VoltMod::Database module (requires sqlpp23)" OFF)
option(VOLTMOD_DISABLE_PCH "Disable precompiled headers (e.g. to favor ccache hits over PCH on MSVC)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(VoltModCommon)
include(VoltModPlugin)
include(VoltModTests)

find_package(cpr CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

if(VOLTMOD_ENABLE_DATABASE)
    find_package(Sqlpp23 CONFIG REQUIRED)
endif()

find_package(hl2sdk-cs2 CONFIG REQUIRED)
find_package(metamod-source CONFIG REQUIRED)

# Header-only dependencies used by tests.
add_library(voltmod-headers INTERFACE)
add_library(VoltMod::Headers ALIAS voltmod-headers)
target_include_directories(voltmod-headers INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(voltmod-headers INTERFACE glaze::glaze magic_enum::magic_enum)
target_compile_features(voltmod-headers INTERFACE cxx_std_23)

# Runtime includes every source module except Database.
file(GLOB_RECURSE VOLTMOD_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER VOLTMOD_RUNTIME_SOURCES EXCLUDE REGEX "/src/Database/")
file(GLOB_RECURSE VOLTMOD_HTTP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")
file(GLOB_RECURSE VOLTMOD_DATABASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")

add_library(voltmod-runtime STATIC ${VOLTMOD_RUNTIME_SOURCES})
add_library(VoltMod::Runtime ALIAS voltmod-runtime)

voltmod_set_cxx_defaults(voltmod-runtime)
target_include_directories(voltmod-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Keep cpr out of public headers.
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
    # Exclude framework headers so source edits do not invalidate the PCH. Use protobuf's stable header.
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

if(VOLTMOD_ENABLE_DATABASE)
    add_library(voltmod-database STATIC ${VOLTMOD_DATABASE_SOURCES})
    add_library(VoltMod::Database ALIAS voltmod-database)
    voltmod_set_cxx_defaults(voltmod-database)
    target_include_directories(voltmod-database PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(voltmod-database
        PUBLIC VoltMod::Runtime sqlpp23::postgresql sqlpp23::mysql sqlpp23::sqlite3)
    # NOMINMAX and a lean windows.h keep the MariaDB header's winsock2.h usable.
    target_compile_definitions(voltmod-database PUBLIC
        VOLTMOD_ENABLE_DATABASE=1
        $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>
    )
    if(NOT VOLTMOD_DISABLE_PCH)
        target_precompile_headers(voltmod-database PRIVATE
            <sqlpp23/sqlpp23.h>
            <sqlpp23/postgresql/postgresql.h>
            <sqlpp23/mysql/mysql.h>
            <sqlpp23/sqlite3/sqlite3.h>
            <format>
            <string>
            <vector>
        )
    endif()
endif()

# CMakeDeps generates the package config; no export set is needed.
install(TARGETS voltmod-runtime ARCHIVE DESTINATION lib)
if(TARGET voltmod-database)
    install(TARGETS voltmod-database ARCHIVE DESTINATION lib)
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION include)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" DESTINATION cmake)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gamedata/" DESTINATION gamedata)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates/plugin/" DESTINATION templates/plugin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION licenses)

# Cache builds have no tests and disable BUILD_TESTING.
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    # Each Api.hpp must compile on its own.
    file(GLOB VOLTMOD_API_CHECKS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/Api/*.cpp")
    if(NOT VOLTMOD_ENABLE_DATABASE)
        list(FILTER VOLTMOD_API_CHECKS EXCLUDE REGEX "/DatabaseApiSurfaceTest\\.cpp$")
    endif()
    add_library(voltmod-api-surface-check OBJECT ${VOLTMOD_API_CHECKS})
    target_compile_definitions(voltmod-api-surface-check PRIVATE VOLTMOD_API_SURFACE_CHECK)
    target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Runtime)
    if(VOLTMOD_ENABLE_DATABASE)
        target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Database)
    endif()

    # SDK-free translation units. Other sources need HL2SDK or a database client.
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
            src/Core/SharedLifecycle.cpp
            src/Core/Strings.cpp
            src/Core/SteamId.cpp
            src/Core/Time.cpp
            src/Core/Translations.cpp
            src/Players/PlayerManager.cpp
            src/Players/Policy.cpp
            src/Hooks/PendingConVarQueries.cpp
            src/Workshop/AddonRequirements.cpp
            src/Ui/LayoutName.cpp
            src/Ui/SentWrites.cpp
            src/Ui/UiClickRouting.cpp
            src/Ui/ClickMessage.cpp
            src/Engine/ConVars/ConVarTypes.cpp
            src/Engine/GameData/Bindings.cpp
            src/Engine/GameData/GameData.cpp
            src/Engine/GameData/GameDataFile.cpp
            src/Engine/Memory/BytePattern.cpp
            src/Engine/Memory/SigScanner.cpp
            src/Engine/Memory/VtableLookup.cpp
            # One file in each pair is empty on this platform.
            src/Engine/Memory/ModuleImage_Windows.cpp
            src/Engine/Memory/ModuleImage_Linux.cpp
            src/Engine/Memory/VtableLookup_Windows.cpp
            src/Engine/Memory/VtableLookup_Linux.cpp
            # Use a fake MenuSurface.
            src/Menu/MenuBuilder.cpp
            src/Menu/MenuPresets.cpp
            src/Menu/CenterHtmlRender.cpp
            src/Menu/MenuCursor.cpp
            src/Menu/MenuStack.cpp
            src/Menu/PendingCommit.cpp
    )
    # Module scanning needs the platform loader library.
    if(WIN32)
        target_link_libraries(voltmod-utils-tests PRIVATE psapi)
    endif()

    if(VOLTMOD_ENABLE_DATABASE)
        target_sources(voltmod-utils-tests PRIVATE src/Database/Database.cpp src/Database/Migrator.cpp)
        target_link_libraries(voltmod-utils-tests PRIVATE sqlpp23::postgresql sqlpp23::mysql sqlpp23::sqlite3)
        # NOMINMAX/WIN32_LEAN_AND_MEAN keep the MariaDB header's winsock2.h usable (see voltmod-database).
        target_compile_definitions(voltmod-utils-tests PRIVATE
            VOLTMOD_ENABLE_DATABASE=1
            $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>
        )
    endif()
endif()
