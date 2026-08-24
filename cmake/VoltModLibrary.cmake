include_guard(GLOBAL)

# Framework-internal: builds one of the framework's static libraries. Not shipped to consumers -
# they get VoltMod::Runtime and VoltMod::Database as CMakeDeps targets from package_info().

# The third-party headers worth precompiling. Framework headers are deliberately absent:
# editing one must not invalidate the PCH. <google/protobuf/message.h> rather than a
# generated .pb.h so it does not depend on protoc output.
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
    # /Z7 from voltmod_set_cxx_defaults is what puts framework frames in plugin crash-dump PDBs.
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
