find_package(absl REQUIRED)

add_library(libAndroidStub INTERFACE)
target_include_directories(libAndroidStub INTERFACE include)
target_link_libraries(libAndroidStub INTERFACE absl::log absl::log_initialize)