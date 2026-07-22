find_package(ZLIB REQUIRED)

add_library(bootlogger_core STATIC
    AuditToAllow.cpp
    KernelConfig.cpp
    LoggerUtils.cpp)
target_include_directories(bootlogger_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bootlogger_core PUBLIC ZLIB::ZLIB)
target_compile_options(bootlogger_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bootlogger Logger.cpp)
target_compile_definitions(bootlogger PRIVATE TESTING)
target_link_libraries(bootlogger PRIVATE bootlogger_core libAndroidStub)
target_compile_options(bootlogger PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bootlogger_tests BootloggerTests.cpp)
target_link_libraries(bootlogger_tests PRIVATE bootlogger_core)
target_compile_options(bootlogger_tests PRIVATE -Wall -Wextra -Wpedantic)

if(SAMSUNG_EXT_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  foreach(target bootlogger_core bootlogger bootlogger_tests)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

add_test(NAME bootlogger_unit COMMAND bootlogger_tests)
add_test(NAME bootlogger_smoke COMMAND bootlogger)
set_tests_properties(bootlogger_smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
