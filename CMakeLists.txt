
cmake_minimum_required(VERSION 3.0)

find_package(PkgConfig "0.22" REQUIRED)
# pkg_check_modules(Vulkan vulkan REQUIRED)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -g -std=c++14 -Werror=return-type")

include_directories(${Vulkan_INCLUDE_DIRS})

find_program(GLSLCOMPILER glslangValidator)
function (add_shader _source _output)
    if (NOT GLSLCOMPILER)
        message(FATAL "glslangValidator not found.")
    endif()

    add_custom_command(OUTPUT ${_output} COMMAND ${GLSLCOMPILER} ARGS -V -o ${_output} ${CMAKE_SOURCE_DIR}/${_source} DEPENDS ${CMAKE_SOURCE_DIR}/${_source})
endfunction()

add_shader(vktest.vert vert.spv)
add_shader(vktest.frag frag.spv)

add_executable(vktest main.cpp vk.cpp vk_pipeline.cpp vk_swapchain.cpp stringview.cpp format.cc display.cpp xcb_platform.cpp wl_platform.cpp event_loop.cpp vert.spv frag.spv)
target_link_libraries(vktest xcb wayland-client ${Vulkan_LIBRARIES} vulkan)
