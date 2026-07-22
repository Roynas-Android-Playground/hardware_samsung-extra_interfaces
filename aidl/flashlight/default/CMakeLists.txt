add_library(flashlight_codec STATIC FlashlightCodec.cpp)
target_include_directories(flashlight_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(flashlight_codec PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flashlight_codec_tests FlashlightCodecTests.cpp)
target_link_libraries(flashlight_codec_tests PRIVATE flashlight_codec)
target_compile_options(flashlight_codec_tests PRIVATE -Wall -Wextra -Wpedantic)

if(SAMSUNG_EXT_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  foreach(target flashlight_codec flashlight_codec_tests)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

add_test(NAME flashlight_codec COMMAND flashlight_codec_tests)
