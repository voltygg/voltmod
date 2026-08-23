cmake_minimum_required(VERSION 3.24)
project(cs2-kit-test LANGUAGES CXX)

# Pulls CS2Kit::CS2Kit / HL2SDK / Metamod plus the build modules behind cs2_add_plugin,
# hl2sdk's included - that is what attaches the per-plugin SDK TUs.
find_package(cs2-kit CONFIG REQUIRED)

cs2_add_plugin(hello-plugin SOURCES src/HelloPlugin.cpp)
