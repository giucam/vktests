
#include <vulkan/vulkan.h>

#include "vk.h"
#include "vk_swapchain.h"
#include "display.h"

vk_swapchain_extension::vk_swapchain_extension(const vk_device &device)
                      : m_device(device)
{
}

stringview vk_swapchain_extension::get_extension()
{
    return VK_KHR_SWAPCHAIN_EXTENSION_NAME;
}

vk_swapchain vk_swapchain_extension::create_swapchain(const vk_surface &surface, const VkSurfaceFormatKHR &format)
{
    uint32_t width = surface.get_window().get_width();
    uint32_t height = surface.get_window().get_height();

    VkSwapchainCreateInfoKHR swapchain_info = {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, //type
        nullptr, //next
        0, //flags
        surface.get_handle(), //surface
        2, //min image count
        format.format, //image format
        format.colorSpace, //image color space
        { width, height }, //image extent
        1, //image array layers
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, //image usage
        VK_SHARING_MODE_EXCLUSIVE, //image sharing mode
        0, //queue family index count
        nullptr, //indices, //queue family indices
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, //pre transform
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, //composite alpha
        VK_PRESENT_MODE_MAILBOX_KHR, //present mode
        false, //clipped
        nullptr, //old swap chain
    };
    VkSwapchainKHR swapchain;
    auto res = vkCreateSwapchainKHR(m_device.get_handle(), &swapchain_info, nullptr, &swapchain);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create swapchain: {}\n", res);
    }
    return vk_swapchain(m_device, swapchain, surface);
}


//--


vk_swapchain::vk_swapchain(const vk_device &device, VkSwapchainKHR handle, const vk_surface &surface)
            : m_device(device)
            , m_handle(handle)
            , m_surface(surface)
{
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(device.get_handle(), m_handle, &image_count, nullptr);

    auto imgs = std::vector<VkImage>(image_count);
    VkResult res = vkGetSwapchainImagesKHR(device.get_handle(), m_handle, &image_count, imgs.data());
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the swapchain images: {}\n", res);
    }

    uint32_t width = surface.get_window().get_width();
    uint32_t height = surface.get_window().get_height();
    m_images.reserve(image_count);
    for (VkImage img: imgs) {
        m_images.emplace_back(device, img, (VkExtent3D){ width, height, 1 });
    }
}

vk_swapchain::~vk_swapchain()
{
    vkDestroySwapchainKHR(m_device.get_handle(), m_handle, nullptr);
}

uint32_t vk_swapchain::acquire_next_image_index()
{
    uint32_t index;
    VkResult res = vkAcquireNextImageKHR(m_device.get_handle(), m_handle, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &index);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to aquire swap chain image: {}\n", res);
    }
    return index;
}

void vk_swapchain::present(const vk_queue &queue, uint32_t image_index)
{
    m_surface.m_window.prepare_swap();

    VkResult res;
    VkPresentInfoKHR present_info = {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, //type
        nullptr, //next
        0, //wait semaphores count
        nullptr, //wait semaphores
        1, //swapchain count
        &m_handle, //swapchains
        &image_index, //image indices
        &res, //results
    };
    vkQueuePresentKHR(queue.get_handle(), &present_info);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to present queue: {}\n", res);
    }
}
