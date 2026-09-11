cmake_minimum_required(VERSION 3.24)
project(metamod-source-test LANGUAGES CXX)

find_package(metamod-source CONFIG REQUIRED)

add_library(khook-test STATIC src/test.cpp)
target_compile_features(khook-test PRIVATE cxx_std_17)
target_link_libraries(khook-test PRIVATE VoltMod::Metamod)
