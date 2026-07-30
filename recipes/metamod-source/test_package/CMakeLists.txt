cmake_minimum_required(VERSION 3.24)
project(metamod-source-test LANGUAGES CXX)

find_package(metamod-source CONFIG REQUIRED)

add_library(sourcehook-test STATIC src/test.cpp)
target_compile_features(sourcehook-test PRIVATE cxx_std_17)
target_link_libraries(sourcehook-test PRIVATE CS2Kit::Metamod)
