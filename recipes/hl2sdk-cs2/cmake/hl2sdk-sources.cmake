include_guard(GLOBAL)

# CMakeDeps build module that attaches SDK sources requiring consumer-side
# compilation and per-source properties. Other usage requirements stay on the
# imported target.

get_filename_component(_hl2sdk_pkg "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
# Cache the source root so sibling plugin directories can see it. INTERNAL also
# lets a new package revision replace a stale value.
set(HL2SDK_CS2_ROOT "${_hl2sdk_pkg}" CACHE INTERNAL "hl2sdk-cs2 package root")
unset(_hl2sdk_pkg)

# These third-party sources require their own include order and namespace state,
# so disable warnings, PCH, and unity builds for them.
function(_hl2sdk_add_sources target)
    target_sources("${target}" PRIVATE ${ARGN})
    set_source_files_properties(${ARGN} PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
    )
endfunction()

# Shared-library SDK bodies. entityidentity provides NameMatches for
# entitysystem; tier1 no longer ships prebuilt. convar.cpp remains per plugin.
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

# Generated protobuf sources keep the package independent of compiler ABI.
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

# Each module needs its own allocator overrides and ConVar registration state.
function(hl2sdk_attach_plugin_support target)
    _hl2sdk_add_sources("${target}"
        "${HL2SDK_CS2_ROOT}/public/tier0/memoverride.cpp"
        "${HL2SDK_CS2_ROOT}/tier1/convar.cpp"
    )
endfunction()
