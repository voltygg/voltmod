cmake_minimum_required(VERSION 4.3)

project($project LANGUAGES CXX)

include(CTest)

# VoltMod targets plus voltmod_add_plugin/voltmod_add_tests, from the Conan package.
find_package(voltmod CONFIG REQUIRED)

# `voltmod new-plugin` adds one add_subdirectory(plugins/<name>) per plugin below.
