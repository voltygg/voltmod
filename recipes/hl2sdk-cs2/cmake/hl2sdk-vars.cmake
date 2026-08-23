include_guard(GLOBAL)

# CMakeDeps build module, run at find_package(hl2sdk-cs2) time. Publishes what the SDK
# cannot express as usage requirements: a filesystem path and a list of sources.
get_filename_component(_hl2sdk_pkg "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)

# CACHE + FORCE: cs2_add_plugin runs in sibling scopes, and a new package revision must
# overwrite a stale entry.
set(CS2KIT_HL2SDK_DIR "${_hl2sdk_pkg}" CACHE PATH "HL2SDK CS2 path" FORCE)

# Generated when this package was built; cs2-kit compiles them into its own library.
file(GLOB _hl2sdk_protos
    "${_hl2sdk_pkg}/generated/public/*.pb.cc"
    "${_hl2sdk_pkg}/generated/game-shared/*.pb.cc"
)
set(CS2KIT_HL2SDK_PROTO_SOURCES ${_hl2sdk_protos} CACHE STRING "Generated HL2SDK protobuf sources" FORCE)

unset(_hl2sdk_pkg)
unset(_hl2sdk_protos)
