
#pragma once

#include <memory>

#include "stringview.h"

struct VkSurfaceFormatKHR;

class vk_device;
class vk_surface;
class vk_swapchain;

class vk_swapchain_extension
{
public:
    vk_swapchain_extension(const std::weak_ptr<vk_device> &device);

    static stringview get_extension();

    std::shared_ptr<vk_swapchain> create_swapchain(const vk_surface &surface, const VkSurfaceFormatKHR &format);

private:
    std::weak_ptr<vk_device> m_device;
};

class vk_swapchain
{
public:
    vk_swapchain(const std::weak_ptr<vk_device> &device, VkSwapchainKHR, const vk_surface &surface);
    ~vk_swapchain();

    const std::vector<vk_image> &get_images() const { return m_images; }
    uint32_t acquire_next_image_index();

    VkSwapchainKHR get_handle() const { return m_handle; }

private:
    std::weak_ptr<vk_device> m_device;
    VkSwapchainKHR m_handle;
    std::vector<vk_image> m_images;
};
