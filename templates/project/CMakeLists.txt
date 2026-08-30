cmake_minimum_required(VERSION 4.3)

project($project LANGUAGES CXX)

include(CTest)

# VoltMod targets plus voltmod_add_plugin/voltmod_add_tests, from the Conan package.
find_package(voltmod CONFIG REQUIRED)

# Plugins are registered below by `new_plugin.py` (add_subdirectory per plugin).
