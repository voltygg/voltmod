include_guard(GLOBAL)

# Kit-internal: builds one static library per module, matching the layering the
# dependency scan enforces. Not shipped to consumers - they get the components as
# CMakeDeps targets from package_info().

set(CS2KIT_COMPONENTS "" CACHE INTERNAL "Component names, in dependency order")

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

# cs2kit_add_component(<name>
#     SOURCES <files...>            # the module's TUs
#     VENDORED <files...>           # third-party TUs: warnings off, no PCH, no unity
#     DEPS <CS2Kit::X...>           # sibling components, downward only
#     LIBS <targets...>             # external usage requirements (PUBLIC)
#     [PCH]                         # precompile the SDK header universe for this module
# )
function(cs2kit_add_component name)
    cmake_parse_arguments(ARG "PCH" "" "SOURCES;VENDORED;DEPS;LIBS" ${ARGN})

    string(TOLOWER "${name}" lower)
    set(target "cs2-kit-${lower}")

    add_library("${target}" STATIC ${ARG_SOURCES} ${ARG_VENDORED})
    add_library("CS2Kit::${name}" ALIAS "${target}")

    if(ARG_VENDORED)
        cs2kit_mark_vendored_sources(${ARG_VENDORED})
    endif()

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

    if(ARG_PCH AND NOT CS2KIT_DISABLE_PCH)
        target_precompile_headers("${target}" PRIVATE ${CS2KIT_PCH_HEADERS})
        if(ARG_VENDORED)
            # Vendored/generated TUs keep their own include order.
            set_source_files_properties(${ARG_VENDORED} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endif()

    set(components ${CS2KIT_COMPONENTS} "${name}")
    set(CS2KIT_COMPONENTS "${components}" CACHE INTERNAL "Component names, in dependency order")
endfunction()
