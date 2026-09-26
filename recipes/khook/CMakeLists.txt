cmake_minimum_required(VERSION 4.4)
project(khook LANGUAGES C CXX SPDX_LICENSE "Zlib AND BSL-1.0 AND MIT")

set(SAFETYHOOK "${CMAKE_CURRENT_SOURCE_DIR}/khook/third_party/safetyhook")
# os.linux.cpp and os.windows.cpp are each empty on the other platform.
file(GLOB SAFETYHOOK_SOURCES "${SAFETYHOOK}/src/*.cpp")

# KHook's own build makes a shared library.
add_library(khook STATIC khook/src/detour.cpp khook/src/ranges.cpp ${SAFETYHOOK_SOURCES} "${SAFETYHOOK}/zydis/Zydis.c")
target_include_directories(khook PRIVATE khook/include "${SAFETYHOOK}/include" "${SAFETYHOOK}/zydis")
target_compile_definitions(khook PRIVATE KHOOK_STANDALONE KHOOK_EXPORT)
