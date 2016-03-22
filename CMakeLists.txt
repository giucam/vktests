
cmake_minimum_required(VERSION 3.0)

find_package(PkgConfig "0.22" REQUIRED)
# pkg_check_modules(Vulkan vulkan REQUIRED)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -g -std=c++14 -Werror=return-type")

include_directories(${Vulkan_INCLUDE_DIRS})

add_executable(vktest main.cpp vk.cpp vk_swapchain.cpp stringview.cpp format.cc display.cpp xcb_platform.cpp wl_platform.cpp)
target_link_libraries(vktest xcb wayland-client ${Vulkan_LIBRARIES} vulkan)
