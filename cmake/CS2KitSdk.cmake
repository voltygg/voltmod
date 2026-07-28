include_guard(GLOBAL)

get_filename_component(CS2KIT_ROOT_DIR_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(CS2KIT_ROOT_DIR "${CS2KIT_ROOT_DIR_DEFAULT}" CACHE PATH "CS2Kit repository root")
set(CS2KIT_HL2SDK_DIR "${CS2KIT_ROOT_DIR}/vendor/hl2sdk-cs2" CACHE PATH "HL2SDK CS2 path")
set(CS2KIT_MMSOURCE_DIR "${CS2KIT_ROOT_DIR}/vendor/mmsource-2.0" CACHE PATH "Metamod:Source path")
set(CS2KIT_GAMEDATA_DIR "${CS2KIT_ROOT_DIR}/gamedata" CACHE PATH "CS2Kit shared gamedata path")

# Fallbacks when no toolchain sets these; CACHE so sibling plugin dirs see them.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "")
    endif()
endif()

# The `<os>-<arch>` folder name used for plugin output dirs.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x86_64 builds are supported.")
endif()
if(WIN32)
    set(CS2KIT_PLATFORM_ARCH "windows-x86_64" CACHE INTERNAL "CS2Kit platform architecture")
elseif(UNIX)
    set(CS2KIT_PLATFORM_ARCH "linux-x86_64" CACHE INTERNAL "CS2Kit platform architecture")
else()
    message(FATAL_ERROR "Only Windows and Linux builds are supported.")
endif()

# Fail configure with a submodule-init hint when `path` does not exist.
function(cs2kit_require_path path description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${description} not found at ${path}. Run git submodule update --init --recursive.")
    endif()
endfunction()

# Disable warnings on vendored/generated TUs (SYSTEM includes only cover headers).
function(cs2kit_mark_vendored_sources)
    set_source_files_properties(${ARGN} PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
    )
endfunction()

# Define the CS2Kit::Metamod and CS2Kit::HL2SDK interface targets carrying the
# SDK includes, defines, compile flags, and prebuilt libs.
function(cs2kit_configure_sdk)
    cs2kit_require_path("${CS2KIT_HL2SDK_DIR}" "HL2SDK CS2")
    cs2kit_require_path("${CS2KIT_MMSOURCE_DIR}" "Metamod:Source")

    if(NOT TARGET cs2kit_metamod)
        add_library(cs2kit_metamod INTERFACE)
        add_library(CS2Kit::Metamod ALIAS cs2kit_metamod)
    endif()

    target_include_directories(cs2kit_metamod SYSTEM INTERFACE
        "${CS2KIT_MMSOURCE_DIR}/core"
        "${CS2KIT_MMSOURCE_DIR}/core/sourcehook"
    )

    if(NOT TARGET cs2kit_hl2sdk)
        add_library(cs2kit_hl2sdk INTERFACE)
        add_library(CS2Kit::HL2SDK ALIAS cs2kit_hl2sdk)
    endif()

    target_include_directories(cs2kit_hl2sdk SYSTEM INTERFACE
        "${CS2KIT_HL2SDK_DIR}/thirdparty/protobuf-3.21.8/src"
        "${CS2KIT_HL2SDK_DIR}/public"
        "${CS2KIT_HL2SDK_DIR}/public/engine"
        "${CS2KIT_HL2SDK_DIR}/public/mathlib"
        "${CS2KIT_HL2SDK_DIR}/public/tier0"
        "${CS2KIT_HL2SDK_DIR}/public/tier1"
        "${CS2KIT_HL2SDK_DIR}/public/entity2"
        "${CS2KIT_HL2SDK_DIR}/public/game/server"
        "${CS2KIT_HL2SDK_DIR}/game/shared"
        "${CS2KIT_HL2SDK_DIR}/game/server"
        "${CS2KIT_HL2SDK_DIR}/common"
    )

    target_compile_definitions(cs2kit_hl2sdk INTERFACE
        SOURCE_ENGINE=25
        GAME_DLL
        RAD_TELEMETRY_DISABLED
        META_IS_SOURCE2
        X64BITS
        PLATFORM_64BITS
    )

    set(gnu_flags
        -fno-strict-aliasing
        -Wall
    )
    target_compile_options(cs2kit_hl2sdk INTERFACE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:${gnu_flags}>"
        "$<$<CXX_COMPILER_ID:MSVC>:/W3;/utf-8>"
    )

    if(WIN32)
        target_compile_definitions(cs2kit_hl2sdk INTERFACE
            WIN32
            WIN64
            _WINDOWS
            COMPILER_MSVC
            COMPILER_MSVC64
            _CRT_SECURE_NO_DEPRECATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            NOMINMAX
        )

        target_link_libraries(cs2kit_hl2sdk INTERFACE
            "${CS2KIT_HL2SDK_DIR}/lib/public/win64/2015/libprotobuf.lib"
            "${CS2KIT_HL2SDK_DIR}/lib/public/win64/mathlib.lib"
            "${CS2KIT_HL2SDK_DIR}/lib/public/win64/tier0.lib"
            "${CS2KIT_HL2SDK_DIR}/lib/public/win64/interfaces.lib"
            legacy_stdio_definitions.lib
        )
    else()
        target_compile_definitions(cs2kit_hl2sdk INTERFACE
            stricmp=strcasecmp
            _stricmp=strcasecmp
            _snprintf=snprintf
            _vsnprintf=vsnprintf
            HAVE_STDINT_H
            GNUC
            COMPILER_GCC
            LINUX
            _LINUX
            POSIX
            _FILE_OFFSET_BITS=64
            _GLIBCXX_USE_CXX11_ABI=0
        )

        target_link_libraries(cs2kit_hl2sdk INTERFACE
            m
            "${CS2KIT_HL2SDK_DIR}/lib/linux64/mathlib.a"
            "${CS2KIT_HL2SDK_DIR}/lib/linux64/interfaces.a"
            "${CS2KIT_HL2SDK_DIR}/lib/linux64/release/libprotobuf.a"
            "${CS2KIT_HL2SDK_DIR}/lib/linux64/libtier0.so"
            $<$<CXX_COMPILER_ID:Clang>:gcc_eh>
        )

        target_link_options(cs2kit_hl2sdk INTERFACE
            -static-libstdc++
            $<$<CXX_COMPILER_ID:GNU>:-static-libgcc>
        )
    endif()
endfunction()

# Add protoc rules for each proto name in ARGN, compiling ${proto_dir}/<name>.proto
# into ${out_dir}; `proto_paths` is the ;-list of --proto_path import dirs.
# Appends the generated .pb.cc paths to the list variable named by `out_list_var`.
function(_cs2kit_protoc out_list_var protoc_path proto_dir out_dir proto_paths)
    set(path_args)
    foreach(path IN LISTS proto_paths)
        list(APPEND path_args "--proto_path=${path}")
    endforeach()

    set(generated "${${out_list_var}}")
    foreach(proto_name IN LISTS ARGN)
        set(proto_file "${proto_dir}/${proto_name}.proto")
        set(output_cc "${out_dir}/${proto_name}.pb.cc")

        add_custom_command(
            OUTPUT "${output_cc}" "${out_dir}/${proto_name}.pb.h"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
            COMMAND "${protoc_path}" ${path_args} "--cpp_out=${out_dir}" "${proto_file}"
            DEPENDS "${proto_file}" "${protoc_path}"
            COMMENT "Generating ${proto_name}.pb.cc"
            VERBATIM
        )

        list(APPEND generated "${output_cc}")
    endforeach()
    set("${out_list_var}" "${generated}" PARENT_SCOPE)
endfunction()

# Generate .pb.cc/.pb.h from the SDK protos with the SDK's bundled protoc;
# returns the generated sources and their include dirs via the out args.
function(cs2kit_generate_sdk_protobuf out_sources out_includes)
    if(WIN32)
        set(protoc_path "${CS2KIT_HL2SDK_DIR}/devtools/bin/protoc.exe")
    else()
        set(protoc_path "${CS2KIT_HL2SDK_DIR}/devtools/bin/linux/protoc")
    endif()

    cs2kit_require_path("${protoc_path}" "HL2SDK protoc")

    set(common_proto_dir "${CS2KIT_HL2SDK_DIR}/common")
    set(shared_proto_dir "${CS2KIT_HL2SDK_DIR}/game/shared")
    set(google_proto_dir "${CS2KIT_HL2SDK_DIR}/thirdparty/protobuf-3.21.8/src")

    set(generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated/hl2sdk-cs2")
    set(generated_public_dir "${generated_root}/public")
    set(generated_shared_dir "${generated_root}/game/shared")

    set(generated_sources)
    _cs2kit_protoc(generated_sources "${protoc_path}"
        "${common_proto_dir}" "${generated_public_dir}"
        "${common_proto_dir};${google_proto_dir}"
        network_connection networkbasetypes engine_gcmessages valveextensions
        # netmessages carries the engine<->client control messages (Sdk::ClientCvarService);
        # source2_steam_stats is one of its imports, so it has to be generated alongside.
        netmessages source2_steam_stats)
    _cs2kit_protoc(generated_sources "${protoc_path}"
        "${shared_proto_dir}" "${generated_shared_dir}"
        "${common_proto_dir};${shared_proto_dir};${google_proto_dir}"
        usermessages usercmd gameevents)
    # game/shared/cs is the first proto_path so cs_usercmd.proto resolves flat and its
    # generated header sits next to usercmd.pb.h, which it includes.
    _cs2kit_protoc(generated_sources "${protoc_path}"
        "${shared_proto_dir}/cs" "${generated_shared_dir}"
        "${shared_proto_dir}/cs;${shared_proto_dir};${common_proto_dir};${google_proto_dir}"
        cs_usercmd)

    set_source_files_properties(${generated_sources} PROPERTIES GENERATED TRUE)
    set("${out_sources}" "${generated_sources}" PARENT_SCOPE)
    set("${out_includes}" "${generated_public_dir};${generated_shared_dir}" PARENT_SCOPE)
endfunction()
