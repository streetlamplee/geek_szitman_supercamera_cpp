cmake_minimum_required(VERSION 3.10)
project(CameraApp CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Use PkgConfig to find libusb-1.0, which is a more reliable method on Linux
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB_1 REQUIRED libusb-1.0)

add_executable(camera_app main.cpp)

# Use the variables provided by pkg-config
target_include_directories(camera_app PRIVATE ${LIBUSB_1_INCLUDE_DIRS})
target_link_libraries(camera_app PRIVATE ${LIBUSB_1_LIBRARIES} jpeg)

install(TARGETS camera_app DESTINATION bin)