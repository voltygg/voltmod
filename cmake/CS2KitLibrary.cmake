include_guard(GLOBAL)

# Kit-internal: builds one of the kit's static libraries. Not shipped to consumers -
# they get CS2Kit::Runtime and CS2Kit::Database as CMakeDeps targets from package_info().

# The third-party headers worth precompiling. Kit headers are deliberately absent:
# editing one must not invalidate the PCH. <google/protobuf/message.h> rather than a
# generated .pb.h so it does not depend on protoc output.
set(CS2KIT_PCH_HEADERS
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

# cs2kit_add_library(<name>
#     SOURCES <files...>       # first-party TUs
#     DEPS <CS2Kit::X...>      # sibling kit libraries (PUBLIC)
#     LIBS <targets...>        # external usage requirements (PUBLIC)
#     PRIVATE_LIBS <targets...># externals that never appear in a public header
# )
function(cs2kit_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;LIBS;PRIVATE_LIBS" ${ARGN})

    string(TOLOWER "${name}" lower)
    set(target "cs2-kit-${lower}")

    add_library("${target}" STATIC ${ARG_SOURCES})
    add_library("CS2Kit::${name}" ALIAS "${target}")

    target_compile_features("${target}" PUBLIC cxx_std_23)
    set_target_properties("${target}" PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "${target}"
    )

    # /Z7 so kit frames appear in plugin crash-dump PDBs (see cs2_add_plugin).
    target_compile_options("${target}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/Z7>"
    )
    cs2kit_set_sdk_warnings("${target}")

    target_include_directories("${target}"
        PUBLIC
            "$<BUILD_INTERFACE:${CS2KIT_ROOT_DIR}/include>"
        PRIVATE
            "${CS2KIT_ROOT_DIR}/src"
    )

    target_link_libraries("${target}" PUBLIC ${ARG_DEPS} ${ARG_LIBS})
    target_link_libraries("${target}" PRIVATE ${ARG_PRIVATE_LIBS})

    if(NOT CS2KIT_DISABLE_PCH)
        target_precompile_headers("${target}" PRIVATE ${CS2KIT_PCH_HEADERS})
    endif()
endfunction()
