cmake_minimum_required(VERSION 3.24)
project(voltmod-test LANGUAGES CXX)

# Pulls VoltMod::VoltMod / HL2SDK / Metamod plus the build modules behind voltmod_add_plugin,
# hl2sdk's included - that is what attaches the per-plugin SDK TUs.
find_package(voltmod CONFIG REQUIRED)

voltmod_add_plugin(hello-plugin VERSION 1.0.0 SOURCES src/HelloPlugin.cpp)
