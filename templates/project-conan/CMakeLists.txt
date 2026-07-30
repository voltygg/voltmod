cmake_minimum_required(VERSION 3.24)

project($project VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

# CS2Kit::CS2Kit plus the cs2_add_plugin/cs2_add_tests helpers, all from the
# Conan package - no submodules.
find_package(cs2-kit CONFIG REQUIRED)

# Plugins are registered below by `new_plugin.py` (add_subdirectory per plugin).
