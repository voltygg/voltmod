cmake_minimum_required(VERSION 3.24)
project(hl2sdk-cs2-test LANGUAGES CXX)

find_package(hl2sdk-cs2 CONFIG REQUIRED)

# The build module must have pointed CS2KIT_HL2SDK_DIR at the package.
if(NOT EXISTS "${CS2KIT_HL2SDK_DIR}/public/eiface.h")
    message(FATAL_ERROR "hl2sdk-vars.cmake did not set CS2KIT_HL2SDK_DIR (got: ${CS2KIT_HL2SDK_DIR})")
endif()

add_library(hl2sdk-test STATIC src/test.cpp)
target_compile_features(hl2sdk-test PRIVATE cxx_std_17)
target_link_libraries(hl2sdk-test PRIVATE CS2Kit::HL2SDK)
