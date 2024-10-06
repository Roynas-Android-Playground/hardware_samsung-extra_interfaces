find_package(ZLIB REQUIRED)

add_executable(bootlogger
    AuditToAllow.cpp
    KernelConfig.cpp
    Logger.cpp)

target_link_libraries(bootlogger ZLIB::ZLIB libAndroidStub)