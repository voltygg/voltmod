include_guard(GLOBAL)

get_filename_component(_hl2sdk_pkg "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
# INTERNAL shares the root across directories and replaces stale package revisions.
set(HL2SDK_CS2_ROOT "${_hl2sdk_pkg}" CACHE INTERNAL "hl2sdk-cs2 package root")
unset(_hl2sdk_pkg)

# PCH and unity builds disturb these sources' include order and namespace state.
function(_hl2sdk_add_sources target)
    target_sources("${target}" PRIVATE ${ARGN})
    set_source_files_properties(${ARGN} PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
    )
endfunction()

# entitysystem.cpp needs NameMatches from entityidentity.cpp; convar.cpp stays per plugin.
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

# Consumer-side compilation avoids a compiler ABI dependency in the package.
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

# Allocator overrides and ConVar registration state must remain module-local.
function(hl2sdk_attach_plugin_support target)
    _hl2sdk_add_sources("${target}"
        "${HL2SDK_CS2_ROOT}/public/tier0/memoverride.cpp"
        "${HL2SDK_CS2_ROOT}/tier1/convar.cpp"
    )
endfunction()
