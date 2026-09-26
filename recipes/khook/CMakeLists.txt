cmake_minimum_required(VERSION 4.3)
project(khook LANGUAGES C CXX)

set(SAFETYHOOK khook/third_party/safetyhook)

# KHook's own build makes a shared library.
add_library(khook STATIC
    khook/src/detour.cpp
    khook/src/ranges.cpp
    ${SAFETYHOOK}/src/allocator.cpp
    ${SAFETYHOOK}/src/easy.cpp
    ${SAFETYHOOK}/src/inline_hook.cpp
    ${SAFETYHOOK}/src/mid_hook.cpp
    # Each is empty on the other platform.
    ${SAFETYHOOK}/src/os.linux.cpp
    ${SAFETYHOOK}/src/os.windows.cpp
    ${SAFETYHOOK}/src/utility.cpp
    ${SAFETYHOOK}/src/vmt_hook.cpp
    ${SAFETYHOOK}/zydis/Zydis.c
)
target_include_directories(khook PRIVATE khook/include ${SAFETYHOOK}/include ${SAFETYHOOK}/zydis)
target_compile_definitions(khook PRIVATE KHOOK_STANDALONE KHOOK_EXPORT)
