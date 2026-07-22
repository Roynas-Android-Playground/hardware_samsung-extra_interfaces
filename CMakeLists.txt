cmake_minimum_required(VERSION 3.16)
project(samsung_ext_host_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAMSUNG_EXT_ENABLE_SANITIZERS "Enable ASan and UBSan for host tests" OFF)

enable_testing()
add_subdirectory(libandroid_stub)
add_subdirectory(debug-tools/bootlogger)
add_subdirectory(aidl/flashlight/default)
add_subdirectory(aidl/battery/default)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/debug-tools/dlopener/CMakeLists.txt")
  add_subdirectory(debug-tools/dlopener)
endif()
