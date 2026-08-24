include_guard(GLOBAL)

# Framework-only helper for the Runtime and Database static libraries.

# Keep framework headers out of the PCH so ordinary edits do not invalidate it.
# Use protobuf's stable public header rather than generated output.
set(VOLTMOD_PCH_HEADERS
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

# voltmod_add_library(<name>
#     SOURCES <files...>       # first-party TUs
#     DEPS <VoltMod::X...>      # sibling framework libraries (PUBLIC)
#     LIBS <targets...>        # external usage requirements (PUBLIC)
#     PRIVATE_LIBS <targets...># externals that never appear in a public header
# )
function(voltmod_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;LIBS;PRIVATE_LIBS" ${ARGN})

    string(TOLOWER "${name}" lower)
    set(target "voltmod-${lower}")

    add_library("${target}" STATIC ${ARG_SOURCES})
    add_library("VoltMod::${name}" ALIAS "${target}")

    target_compile_features("${target}" PUBLIC cxx_std_23)
# /Z7 includes framework frames in plugin crash-dump PDBs.
    voltmod_set_cxx_defaults("${target}")
    set_target_properties("${target}" PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "${target}"
    )
    voltmod_set_warnings("${target}")

    target_include_directories("${target}"
        PUBLIC
            "$<BUILD_INTERFACE:${VOLTMOD_ROOT_DIR}/include>"
        PRIVATE
            "${VOLTMOD_ROOT_DIR}/src"
    )

    target_link_libraries("${target}" PUBLIC ${ARG_DEPS} ${ARG_LIBS})
    target_link_libraries("${target}" PRIVATE ${ARG_PRIVATE_LIBS})

    if(NOT VOLTMOD_DISABLE_PCH)
        target_precompile_headers("${target}" PRIVATE ${VOLTMOD_PCH_HEADERS})
    endif()
endfunction()
