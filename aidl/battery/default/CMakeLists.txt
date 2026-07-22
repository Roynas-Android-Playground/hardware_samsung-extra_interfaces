add_library(smartcharge_policy STATIC SmartChargePolicy.cpp)
target_include_directories(smartcharge_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(smartcharge_policy PRIVATE -Wall -Wextra -Wpedantic)

add_executable(smartcharge_policy_tests SmartChargePolicyTests.cpp)
target_link_libraries(smartcharge_policy_tests PRIVATE smartcharge_policy)
target_compile_options(smartcharge_policy_tests PRIVATE -Wall -Wextra -Wpedantic)

if(SAMSUNG_EXT_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  foreach(target smartcharge_policy smartcharge_policy_tests)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

add_test(NAME smartcharge_policy COMMAND smartcharge_policy_tests)
