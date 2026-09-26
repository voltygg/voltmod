cmake_minimum_required(VERSION 4.3)
project(khook-test LANGUAGES CXX)

find_package(khook CONFIG REQUIRED)

add_executable(khook-test src/test.cpp)
target_link_libraries(khook-test PRIVATE khook::khook)
