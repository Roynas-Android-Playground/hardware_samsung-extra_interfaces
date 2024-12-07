find_package(ZLIB REQUIRED)
find_package(fmt REQUIRED)

add_executable(bootlogger
    AuditToAllow.cpp
    KernelConfig.cpp
    Logger.cpp)

target_compile_definitions(bootlogger PRIVATE TESTING)
target_link_libraries(bootlogger ZLIB::ZLIB fmt::fmt libAndroidStub)