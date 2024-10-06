cmake_minimum_required(VERSION 3.5)
project(debug_tools)

set(CMAKE_CXX_STANDARD 17)
add_subdirectory(libandroid_stub)
add_subdirectory(debug-tools/bootlogger)
add_subdirectory(debug-tools/dlopener)