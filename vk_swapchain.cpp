
#include <vulkan/vulkan.h>

#include "vk.h"
#include "vk_swapchain.h"
#include "display.h"

vk_swapchain_extension::vk_swapchain_extension(const std::weak_ptr<vk_device> &device)
                      : m_device(device)
{
}

stringview vk_swapchain_extension::get_extension()
{
    return VK_KHR_SWAPCHAIN_EXTENSION_NAME;
}

std::shared_ptr<vk_swapchain> vk_swapchain_extension::create_swapchain(const vk_surface &surface, const VkSurfaceFormatKHR &format)
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
        VK_PRESENT_MODE_FIFO_KHR, //present mode
        false, //clipped
        nullptr, //old swap chain
    };
    VkSwapchainKHR swapchain;
    auto res = vkCreateSwapchainKHR(m_device.lock()->get_handle(), &swapchain_info, nullptr, &swapchain);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create swapchain: {}\n", res);
    }
    return std::make_shared<vk_swapchain>(m_device, swapchain, surface);
}


//--


vk_swapchain::vk_swapchain(const std::weak_ptr<vk_device> &device, VkSwapchainKHR handle, const vk_surface &surface)
            : m_device(device)
            , m_handle(handle)
{
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(device.lock()->get_handle(), m_handle, &image_count, nullptr);

    auto imgs = std::vector<VkImage>(image_count);
    VkResult res = vkGetSwapchainImagesKHR(device.lock()->get_handle(), m_handle, &image_count, imgs.data());
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
    vkDestroySwapchainKHR(m_device.lock()->get_handle(), m_handle, nullptr);
}

uint32_t vk_swapchain::acquire_next_image_index()
{
    uint32_t index;
    VkResult res = vkAcquireNextImageKHR(m_device.lock()->get_handle(), m_handle, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &index);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to aquire swap chain image: {}\n", res);
    }
    return index;
}
