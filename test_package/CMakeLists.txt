cmake_minimum_required(VERSION 3.24)
project(cs2-kit-test LANGUAGES CXX)

# Pulls CS2Kit::CS2Kit / HL2SDK / Metamod plus the cs2_add_plugin build modules;
# hl2sdk's points CS2KIT_HL2SDK_DIR at its package for the per-plugin TUs.
find_package(cs2-kit CONFIG REQUIRED)

cs2_add_plugin(hello-plugin SOURCES src/HelloPlugin.cpp)
