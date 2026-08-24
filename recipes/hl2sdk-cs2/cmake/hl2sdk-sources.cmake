include_guard(GLOBAL)

# CMakeDeps build module, run at find_package(hl2sdk-cs2) time.
#
# Includes, defines, libs and ABI flags ride on the VoltMod::HL2SDK imported target. What a
# package cannot express that way is the set of SDK translation units a consumer has to
# compile into its own binary - so this module attaches them by function instead of
# publishing a path and a file list for every consumer to assemble. Interface sources
# would not do: these TUs need per-source properties that propagated sources cannot carry.

get_filename_component(_hl2sdk_pkg "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
# CACHE INTERNAL, not a plain variable: the functions below run in sibling directory
# scopes (each plugin has its own CMakeLists), where a directory-scoped variable set here
# would not be visible. INTERNAL implies FORCE, so a new package revision overwrites a
# stale entry rather than being ignored.
set(HL2SDK_CS2_ROOT "${_hl2sdk_pkg}" CACHE INTERNAL "hl2sdk-cs2 package root")
unset(_hl2sdk_pkg)

# Third-party TUs: warnings off (SYSTEM includes only silence headers), and out of both the
# precompiled header and any unity batch - each carries its own include order, and several
# define namespace-scope state that must not be merged with a neighbour.
function(_hl2sdk_add_sources target)
    target_sources("${target}" PRIVATE ${ARGN})
    set_source_files_properties(${ARGN} PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
    )
endfunction()

# The SDK bodies that belong in the shared library, once per build.
#
# entityidentity is here even though nothing calls it directly: entitysystem.cpp references
# NameMatches, and omitting it fails the Linux .so dlopen on an undefined symbol. tier1 is
# here because hl2sdk dropped its prebuilt libs. convar.cpp is deliberately absent - see
# hl2sdk_attach_plugin_support. bitbuf and tokenreader are absent because hl2sdk deleted
# the headers they include.
function(hl2sdk_attach_engine_sources target)
    _hl2sdk_add_sources("${target}"
        "${HL2SDK_CS2_ROOT}/entity2/entityidentity.cpp"
        "${HL2SDK_CS2_ROOT}/entity2/entitykeyvalues.cpp"
        "${HL2SDK_CS2_ROOT}/entity2/entitysystem.cpp"
        "${HL2SDK_CS2_ROOT}/tier1/keyvalues3.cpp"
        "${HL2SDK_CS2_ROOT}/tier1/rangecheckedvar.cpp"
        "${HL2SDK_CS2_ROOT}/tier1/utlbufferutil.cpp"
    )
endfunction()

# The .pb.cc protoc produced when this package was built. Shipped as source rather than a
# prebuilt lib so the package stays os/arch-only.
function(hl2sdk_attach_generated_sources target)
    file(GLOB _generated
        "${HL2SDK_CS2_ROOT}/generated/public/*.pb.cc"
        "${HL2SDK_CS2_ROOT}/generated/game-shared/*.pb.cc"
    )
    if(NOT _generated)
        message(FATAL_ERROR "hl2sdk-cs2 shipped no generated protobuf sources at ${HL2SDK_CS2_ROOT}/generated")
    endif()
    _hl2sdk_add_sources("${target}" ${_generated})
endfunction()

# The two TUs every loadable module needs its own copy of: memoverride.cpp replaces global
# operator new/delete, and convar.cpp defines what convar.h declares, giving each module its
# own ConVar registration state. Sharing either through the static library would make one
# module's allocator or convar table serve them all.
function(hl2sdk_attach_plugin_support target)
    _hl2sdk_add_sources("${target}"
        "${HL2SDK_CS2_ROOT}/public/tier0/memoverride.cpp"
        "${HL2SDK_CS2_ROOT}/tier1/convar.cpp"
    )
endfunction()
