cmake_minimum_required(VERSION 3.24)
project(hl2sdk-cs2-test LANGUAGES CXX)

find_package(hl2sdk-cs2 CONFIG REQUIRED)

# The build module must have shipped and defined its source-attaching API.
if(NOT COMMAND hl2sdk_attach_plugin_support)
    message(FATAL_ERROR "hl2sdk-cs2 shipped no hl2sdk-sources.cmake build module")
endif()

add_library(hl2sdk-test STATIC src/test.cpp)
target_compile_features(hl2sdk-test PRIVATE cxx_std_17)
target_link_libraries(hl2sdk-test PRIVATE VoltMod::HL2SDK)

# Compiles the generated protobufs and the SDK bodies out of the package, which is the
# half of it that usage requirements cannot carry.
hl2sdk_attach_generated_sources(hl2sdk-test)
hl2sdk_attach_engine_sources(hl2sdk-test)
