
cmake_minimum_required(VERSION 3.0)

find_package(PkgConfig "0.22" REQUIRED)
# pkg_check_modules(Vulkan vulkan REQUIRED)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -g -std=c++14 -Werror=return-type")

include_directories(${Vulkan_INCLUDE_DIRS})

find_program(GLSLCOMPILER glslangValidator)
function (add_shader _sources _input _output)
    if (NOT GLSLCOMPILER)
        message(FATAL "glslangValidator not found.")
    endif()

    add_custom_command(OUTPUT ${_output} COMMAND ${GLSLCOMPILER} ARGS -V -o ${_output} ${CMAKE_SOURCE_DIR}/${_input} DEPENDS ${CMAKE_SOURCE_DIR}/${_input})
    list(APPEND ${_sources} "${_output}")
    set(${_sources} ${${_sources}} PARENT_SCOPE)
endfunction()

set(srcs main.cpp vk.cpp vk_pipeline.cpp vk_swapchain.cpp stringview.cpp format.cc display.cpp xcb_platform.cpp wl_platform.cpp event_loop.cpp)

add_shader(srcs vktest.vert vert.spv)
add_shader(srcs vktest.frag frag.spv)
add_shader(srcs ui.vert vert-ui.spv)
add_shader(srcs ui.frag frag-ui.spv)

add_executable(vktest main.cpp ${srcs})
target_link_libraries(vktest xcb wayland-client ${Vulkan_LIBRARIES} vulkan)
