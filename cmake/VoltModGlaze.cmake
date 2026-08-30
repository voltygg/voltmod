include_guard(GLOBAL)

# Glaze's Conan recipe appends /Zc:preprocessor to its interface on MSVC. That flag propagates to
# every target linking glaze::glaze - including voltmod-runtime, which hl2sdk_attach_engine_sources
# populates with SourceHook and HL2SDK translation units whose macros predate the conforming
# preprocessor. Limiting the scope does not help (the SDK sources sit on the same target), and the
# shared PCH has to be built in one preprocessor mode anyway, so strip it. Glaze itself compiles
# cleanly without the flag; only its macro-based glz::meta path needs it, and reflection makes that
# path unnecessary here.
function(voltmod_normalize_glaze)
    if(NOT MSVC OR NOT TARGET glaze::glaze)
        return()
    endif()

    get_target_property(_glaze_options glaze::glaze INTERFACE_COMPILE_OPTIONS)
    if(NOT _glaze_options)
        return()
    endif()

    # CMakeDeps wraps the flag in nested per-config and per-language generator expressions, and
    # CMake stores the result as a `;`-list that splits *inside* those expressions - so removing a
    # list element leaves an unbalanced `>` that reaches the compiler as a bogus source file.
    # Replace the flag text in place instead, which leaves the expression structure untouched.
    string(REPLACE "/Zc:preprocessor" "" _glaze_options "${_glaze_options}")
    set_target_properties(glaze::glaze PROPERTIES INTERFACE_COMPILE_OPTIONS "${_glaze_options}")
endfunction()

# Consumer builds include this module before their own find_package(glaze) may have run, so apply
# it here too when the target already exists. The function is idempotent.
voltmod_normalize_glaze()
