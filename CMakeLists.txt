cmake_minimum_required(VERSION 4.3)

project(voltmod LANGUAGES CXX)

option(VOLTMOD_DISABLE_PCH "Disable precompiled headers (e.g. to favor ccache hits over PCH on MSVC)" OFF)

# conanfile.py owns the framework version and the toolchain passes it in; the host build stamp
# reports the same one. Parsing the recipe covers a configure without the Conan toolchain.
if(NOT VOLTMOD_VERSION)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/conanfile.py" _version_line REGEX "^ +version *= *")
    if(NOT _version_line MATCHES "\"([^\"]+)\"")
        message(FATAL_ERROR "no framework version: pass -DVOLTMOD_VERSION or keep it in conanfile.py")
    endif()
    set(VOLTMOD_VERSION "${CMAKE_MATCH_1}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(VoltModCommon)
include(VoltModPlugin)
include(VoltModTests)

find_package(cpr CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(Sqlpp23 CONFIG REQUIRED)

find_package(hl2sdk-cs2 CONFIG REQUIRED)
find_package(metamod-source CONFIG REQUIRED)

# Header-only dependencies used by tests.
add_library(voltmod-headers INTERFACE)
add_library(VoltMod::Headers ALIAS voltmod-headers)
target_include_directories(voltmod-headers INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(voltmod-headers INTERFACE glaze::glaze magic_enum::magic_enum)
target_compile_features(voltmod-headers INTERFACE cxx_std_23)

# The SDK is every source module except Database and Host: Database is opt-in, and Host
# belongs to the one process-wide host module, not to each plugin.
file(GLOB_RECURSE VOLTMOD_SDK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER VOLTMOD_SDK_SOURCES EXCLUDE REGEX "/src/Database/")
list(FILTER VOLTMOD_SDK_SOURCES EXCLUDE REGEX "/src/Host/")

# Schema offsets differ between the Windows and Linux builds of the game.
if(WIN32)
    list(FILTER VOLTMOD_SDK_SOURCES EXCLUDE REGEX "/src/Schema/Generated/linux/")
else()
    list(FILTER VOLTMOD_SDK_SOURCES EXCLUDE REGEX "/src/Schema/Generated/windows/")
endif()

file(GLOB_RECURSE VOLTMOD_HTTP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Http/*.cpp")
file(GLOB_RECURSE VOLTMOD_DATABASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Database/*.cpp")
file(GLOB_RECURSE VOLTMOD_HOST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Host/*.cpp")

add_library(voltmod-sdk STATIC ${VOLTMOD_SDK_SOURCES})
add_library(VoltMod::Sdk ALIAS voltmod-sdk)

voltmod_set_cxx_defaults(voltmod-sdk)
target_include_directories(voltmod-sdk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Keep cpr out of public headers.
target_link_libraries(voltmod-sdk
    PUBLIC VoltMod::Headers VoltMod::Metamod VoltMod::HL2SDK
    PRIVATE cpr::cpr
)
hl2sdk_attach_engine_sources(voltmod-sdk)
hl2sdk_attach_generated_sources(voltmod-sdk)

if(WIN32)
    target_link_libraries(voltmod-sdk PUBLIC psapi)
endif()

if(NOT VOLTMOD_DISABLE_PCH)
    # Exclude framework headers so source edits do not invalidate the PCH. Use protobuf's stable header.
    target_precompile_headers(voltmod-sdk PRIVATE
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

add_library(voltmod-sqlpp INTERFACE)
target_link_libraries(voltmod-sqlpp INTERFACE sqlpp23::postgresql sqlpp23::mysql sqlpp23::sqlite3)
target_compile_definitions(voltmod-sqlpp INTERFACE $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

add_library(voltmod-database STATIC ${VOLTMOD_DATABASE_SOURCES})
add_library(VoltMod::Database ALIAS voltmod-database)
voltmod_set_cxx_defaults(voltmod-database)
target_include_directories(voltmod-database PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(voltmod-database PUBLIC VoltMod::Sdk voltmod-sqlpp)
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

# The host: the only Metamod plugin in the process. It loads plugin modules itself, so it
# needs the same module-local allocator and ConVar sources any plugin gets.
add_library(voltmod-host MODULE ${VOLTMOD_HOST_SOURCES})
voltmod_set_cxx_defaults(voltmod-host)
target_include_directories(voltmod-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(voltmod-host PRIVATE VoltMod::Sdk)
hl2sdk_attach_plugin_support(voltmod-host)

# Release PDBs for crash dumps.
target_link_options(voltmod-host PRIVATE
    "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/DEBUG;/OPT:REF;/OPT:ICF>"
)

set(VOLTMOD_HOST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/host/${VOLTMOD_PLATFORM_ARCH}")
set_target_properties(voltmod-host PROPERTIES
    PREFIX ""
    OUTPUT_NAME "voltmod"
    LIBRARY_OUTPUT_DIRECTORY "${VOLTMOD_HOST_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${VOLTMOD_HOST_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY "${VOLTMOD_HOST_OUTPUT_DIR}"
)

voltmod_stamp_build_info(voltmod-host "${VOLTMOD_VERSION}")

set(VOLTMOD_HOST_BIN "addons/voltmod/bin/${VOLTMOD_BIN_SUBDIR}")

install(TARGETS voltmod-host
    LIBRARY DESTINATION "${VOLTMOD_HOST_BIN}" COMPONENT host
    RUNTIME DESTINATION "${VOLTMOD_HOST_BIN}" COMPONENT host
)
if(WIN32)
    install(FILES "$<TARGET_PDB_FILE:voltmod-host>"
        DESTINATION "${VOLTMOD_HOST_BIN}" COMPONENT host OPTIONAL)
endif()

# One .vdf and one gamedata directory per server, both the host's.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/host.vdf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/voltmod.vdf" @ONLY NEWLINE_STYLE LF)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/voltmod.vdf"
    DESTINATION "addons/metamod" COMPONENT host)
install(DIRECTORY "${VOLTMOD_GAMEDATA_DIR}/"
    DESTINATION "addons/voltmod/gamedata" COMPONENT host)

# CMakeDeps generates the package config; no export set is needed.
install(TARGETS voltmod-sdk voltmod-database ARCHIVE DESTINATION lib)

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
    add_library(voltmod-api-surface-check OBJECT ${VOLTMOD_API_CHECKS})
    target_compile_definitions(voltmod-api-surface-check PRIVATE VOLTMOD_API_SURFACE_CHECK)
    target_link_libraries(voltmod-api-surface-check PRIVATE VoltMod::Sdk VoltMod::Database)

    # SDK-free translation units. Other sources need HL2SDK or a database client.
    voltmod_add_tests(voltmod-utils-tests
        SOURCES
            src/App/ServiceExchange.cpp
            src/Commands/CommandBuilder.cpp
            src/Commands/CommandRouter.cpp
            src/Commands/CommandSyntax.cpp
            src/Commands/ArgBinding.cpp
            src/Commands/Targeting.cpp
            src/Core/Files/File.cpp
            src/Core/Files/GameBuild.cpp
            src/Core/Files/Paths.cpp
            src/Core/Logger.cpp
            src/Core/LoadSteps.cpp
            src/Core/Signals/SharedLifecycle.cpp
            src/Core/Slots/SteamId.cpp
            src/Core/Text/Strings.cpp
            src/Core/Text/Translations.cpp
            src/Core/Time/Scheduler.cpp
            src/Core/Time/Time.cpp
            src/Players/EffectManager.cpp
            src/Players/PlayerManager.cpp
            src/Players/Policy.cpp
            src/Hooks/PendingConVarQueries.cpp
            src/Host/PluginHost.cpp
            src/Host/PluginOrder.cpp
            src/Workshop/AddonDownloads.cpp
            src/Ui/LayoutPath.cpp
            src/Ui/WriteCache.cpp
            src/Ui/ButtonPressMessage.cpp
            src/Engine/ConVars/ConVarTypes.cpp
            src/Engine/GameData/Bindings.cpp
            src/Engine/GameData/GameDataResolver.cpp
            src/Engine/GameData/ResolvedRecord.cpp
            src/Engine/Memory/BytePattern.cpp
            src/Engine/Memory/SigScanner.cpp
            src/Engine/Memory/VtableLookup.cpp
            # One file in each pair is empty on this platform.
            src/Engine/Memory/LoadedModule_Windows.cpp
            src/Engine/Memory/LoadedModule_Linux.cpp
            src/Engine/Memory/VtableLookup_Windows.cpp
            src/Engine/Memory/VtableLookup_Linux.cpp
            # Use a fake MenuSurface.
            src/Menu/MenuBuilder.cpp
            src/Menu/MenuPresets.cpp
            src/Menu/CenterHtmlRender.cpp
            src/Menu/MenuCursor.cpp
            src/Menu/MenuRouter.cpp
            src/Menu/MenuStack.cpp
            src/Menu/PendingCommit.cpp
    )
    # Module scanning needs the platform loader library.
    if(WIN32)
        target_link_libraries(voltmod-utils-tests PRIVATE psapi)
    endif()

    target_sources(voltmod-utils-tests PRIVATE ${VOLTMOD_DATABASE_SOURCES})
    target_link_libraries(voltmod-utils-tests PRIVATE voltmod-sqlpp)
endif()
