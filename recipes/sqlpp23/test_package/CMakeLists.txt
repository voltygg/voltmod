cmake_minimum_required(VERSION 3.24)
project(sqlpp23-test LANGUAGES CXX)

find_package(Sqlpp23 CONFIG REQUIRED)

add_executable(sqlpp23-test src/test.cpp)
target_compile_features(sqlpp23-test PRIVATE cxx_std_23)
target_link_libraries(sqlpp23-test PRIVATE
    sqlpp23::postgresql sqlpp23::mysql sqlpp23::sqlite3)
