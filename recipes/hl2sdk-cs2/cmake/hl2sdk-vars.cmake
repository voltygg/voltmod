include_guard(GLOBAL)

# Shipped as a CMakeDeps build module: runs at find_package(hl2sdk-cs2) time
# (also transitively via find_package(cs2-kit)) and points the kit's existing
# cache variables at this package. FORCE beats CS2KitSdk.cmake's submodule
# defaults, so CS2Plugin.cmake and cs2kit_configure_sdk work unchanged in
# package mode - the package tree mirrors the upstream SDK layout.
get_filename_component(_hl2sdk_pkg "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(CS2KIT_HL2SDK_DIR "${_hl2sdk_pkg}" CACHE PATH "HL2SDK CS2 path" FORCE)
unset(_hl2sdk_pkg)
