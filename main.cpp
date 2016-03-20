
#include <assert.h>
#include <unistd.h>

#include <exception>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "stringview.h"
#include "display.h"
#include "format.h"
#include "vk.h"
#include "vk_swapchain.h"

using std::string;
using std::weak_ptr;
using std::shared_ptr;
using std::unique_ptr;
using std::make_unique;
using std::make_shared;
using std::vector;
using fmt::print;










int main(int argc, char **argv)
{
    auto plat = platform::xcb;
    if (argc > 1 && stringview(argv[1]) == "wl") {
        plat = platform::wayland;
    }

    auto dpy = display::create(plat);
    auto win = dpy->create_window(200, 200);
    win->show();

    auto layers = vk_instance::get_available_layers();
    print("Found {} available layers\n", layers.size());
    int i = 0;
    for (vk_layer &layer: layers) {
        print("{}: {} == {}\n", i++, layer.get_name(), layer.get_description());
    }

    auto vk = dpy->create_vk_instance({ VK_EXT_DEBUG_REPORT_EXTENSION_NAME });


//     PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallback;
//     PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallback;
//     CreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(vk.get_handle(), "vkCreateDebugReportCallbackEXT");
//     if (!CreateDebugReportCallback) {
//         throw vk_exception("No debug callback\n");
//     }
//
//     VkDebugReportCallbackEXT msg_callback;
//     VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
//     dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
//     dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
//     dbgCreateInfo.pfnCallback = [](VkFlags msgFlags, VkDebugReportObjectTypeEXT objType,
//                                     uint64_t srcObject, size_t location, int32_t msgCode,
//                                     const char *pLayerPrefix, const char *pMsg, void *pUserData) -> VkBool32
//     {
// //         print("msg: {}\n", pMsg);
//         return false;
//     };
//     dbgCreateInfo.pUserData = NULL;
//     dbgCreateInfo.pNext = NULL;
//     VkResult err = CreateDebugReportCallback(vk.get_handle(), &dbgCreateInfo, NULL, &msg_callback);

    VkResult res;

    auto phys_device = vk->get_physical_devices()[0];

    int family_queue_index = -1;
    auto queues_props = phys_device.get_queue_family_properties();
    for (size_t i = 0; i < queues_props.size(); ++i) {
        if (queues_props.at(i).is_graphics_capable()) {
            family_queue_index = i;
            break;
        }
    }

    if (family_queue_index < 0) {
        throw vk_exception("Cannot find graphics queue.\n");
    }

    print("using queue {}\n", family_queue_index);

    auto device = phys_device.create_device<vk_swapchain_extension>(family_queue_index);
    auto queue = device->get_queue(0);

    auto swapchain_ext = device->get_extension_object<vk_swapchain_extension>();


    auto cmd_pool = device->create_command_pool(queue);


    auto init_cmd_buf = cmd_pool->create_command_buffer();

    init_cmd_buf->begin();

    auto surface = win->create_vk_surface(vk, device.get());

    VkBool32 supports_present = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(phys_device.get_handle(), 0, surface->get_handle(), &supports_present);
    if (!supports_present) {
        throw vk_exception("Queue does not support present\n");
    }

    uint32_t format_count;
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device.get_handle(), surface->get_handle(), &format_count, nullptr);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the number of surface formats: {}\n");
    }
    auto formats = vector<VkSurfaceFormatKHR>(format_count);
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device.get_handle(), surface->get_handle(), &format_count, formats.data());
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the surface formats: {}\n");
    }
    VkSurfaceFormatKHR format = formats.at(0);
    print("Found {} formats, using {}\n", formats.size(), format.format);

    VkSurfaceCapabilitiesKHR surface_caps;
    res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device.get_handle(), surface->get_handle(), &surface_caps);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to get physical device surface capabilities: {}\n");
    }

    uint32_t present_mode_count;
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device.get_handle(), surface->get_handle(), &present_mode_count, nullptr);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to get the number of physical device surface present modes: {}\n");
    }
    auto present_modes = vector<VkPresentModeKHR>(present_mode_count);
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device.get_handle(), surface->get_handle(), &present_mode_count, present_modes.data());
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to get the physical device surface present modes: {}\n");
    }
    print("Found {} present modes available\n", present_modes.size());






    VkAttachmentDescription attachment_desc[] = {
        {
            0, //flags
            format.format, //format
            VK_SAMPLE_COUNT_1_BIT, //samples
            VK_ATTACHMENT_LOAD_OP_CLEAR, //load op
            VK_ATTACHMENT_STORE_OP_STORE, //store op
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, //stencil load op
            VK_ATTACHMENT_STORE_OP_DONT_CARE, //stencil store op
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, //initial layout
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, //final layout
        }
    };
    VkAttachmentReference color_attachments[] = {
        { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, },
    };
    VkAttachmentReference resolve_attachments[] = {
        { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, },
    };
    const VkAttachmentReference depth_reference = {
        VK_ATTACHMENT_UNUSED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const uint32_t preserve_attachments[] = { 0 };
    VkSubpassDescription subpass_desc[] = {
        {
            0, //flags
            VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
            0, //input attachments count
            nullptr, //input attachments
            1, //color attachments count
            color_attachments, //color_attachments
            nullptr, //resolve attachments
            &depth_reference, //depth stencil attachments
            0, //preserve attachments count
            nullptr, //preserve attachments
        },
    };
    VkRenderPassCreateInfo create_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0,
        1, attachment_desc,
        1, subpass_desc,
        0, nullptr,
    };

    VkRenderPass render_pass;
    res = vkCreateRenderPass(device->get_handle(), &create_info, nullptr, &render_pass);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create render pass: {}\n", res);
    }


    VkFenceCreateInfo fence_info = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0,
    };
    VkFence fence;
    res = vkCreateFence(device->get_handle(), &fence_info, nullptr, &fence);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create fence: {}\n", res);
    }






    auto swap_chain = swapchain_ext->create_swapchain(surface, format);
    auto imgs = swap_chain->get_images();
    print("{} images available\n", imgs.size());

    class vk_framebuffer
    {
    public:
        vk_framebuffer(const weak_ptr<vk_device> &device, vk_image &img, VkRenderPass rpass)
            : m_device(device)
            , m_image(img)
            , m_view(m_image.create_image_view())
        {
            VkImageView view_handle = m_view->get_handle();
            VkFramebufferCreateInfo info = {
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, //type
                nullptr, //next
                0, //flags
                rpass, //render pass
                1, //attachment count
                &view_handle, //attachments
                m_image.get_width(), //width
                m_image.get_height(), //height
                1, //layers
            };
            VkResult res = vkCreateFramebuffer(m_device.lock()->get_handle(), &info, nullptr, &m_handle);
            if (res != VK_SUCCESS) {
                throw vk_exception("Failed to create framebuffer: {}\n", res);
            }
        }

        uint32_t get_width() const { return m_image.get_width(); }
        uint32_t get_height() const { return m_image.get_height(); }

        vk_image *get_image() const { return &m_image; }
        VkFramebuffer get_handle() const { return m_handle; }

    private:
        weak_ptr<vk_device> m_device;
        vk_image &m_image;
        shared_ptr<vk_image_view> m_view;
        VkFramebuffer m_handle;
    };

    auto buffers = vector<vk_framebuffer>();
    buffers.reserve(imgs.size());
    for (vk_image &img: imgs) {
        print("creating buffer {}\n",(void*)&img);
        buffers.emplace_back(device, img, render_pass);
    }


    uint32_t index = swap_chain->acquire_next_image_index();
    vk_framebuffer &buffer = buffers[index];


    VkImageMemoryBarrier image_memory_barrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, //type
        nullptr, //next
        0, //src access mask
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, //dst access mask
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, //old image layout
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, //new image layout
        0, //src queue family index
        0, //dst queue family index
        buffer.get_image()->get_handle(), //image
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, //subresource range
    };

    vkCmdPipelineBarrier(init_cmd_buf->get_handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_memory_barrier);

    init_cmd_buf->end();

    const VkCommandBuffer cmd_bufs[] = { init_cmd_buf->get_handle() };
    VkSubmitInfo submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO, //type
        nullptr, //next
        0, //wait semaphores count
        nullptr, //wait semaphores
        nullptr, //wait dst stage mask
        1, //command buffers count
        cmd_bufs, //command buffers
        0, //signal semaphores count
        nullptr, //signal semaphores
    };

    res = vkQueueSubmit(queue->get_handle(), 1, &submit_info, VK_NULL_HANDLE);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to submit queue: {}\n", res);
    }

    res = vkQueueWaitIdle(queue->get_handle());
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to wait queue: {}\n", res);
    }


    // === RENDER ===

    auto cmd_buffer = cmd_pool->create_command_buffer();
    cmd_buffer->begin();

    VkClearValue clear_values[1];
    clear_values[0].color.float32[0] = 1.0f;
    clear_values[0].color.float32[1] = 0.2f;
    clear_values[0].color.float32[2] = 0.2f;
    clear_values[0].color.float32[3] = 1.0f;
    VkRenderPassBeginInfo render_pass_begin_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, //type
        nullptr, //next
        render_pass, //render pass
        buffer.get_handle(), //framebuffer
        { { 0, 0 }, { buffer.get_width(), buffer.get_height() } }, //render area
        1, //clear value count
        clear_values, //clear values
    };

    print("pass {} fb {}\n",(void*)render_pass,(void*)buffer.get_handle());

    vkCmdBeginRenderPass(cmd_buffer->get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    cmd_buffer->end();


    VkPipelineStageFlags pipe_stage_flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkCommandBuffer cmd_buf_raw = cmd_buffer->get_handle();
    VkSubmitInfo submit_draw_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO, //type
        nullptr, //next
        0, //wait semaphore count
        nullptr, //wait semaphores
        &pipe_stage_flags, //wait dst stage mask
        1, //command buffer count
        &cmd_buf_raw, //command buffers
        0, //signal semaphores count
        nullptr, //signal semaphores
    };
    res = vkQueueSubmit(queue->get_handle(), 1, &submit_draw_info, VK_NULL_HANDLE);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to submit queue: {}\n", res);
    }

    // == SWAP ==


//     vc->model.render(vc, &vc->buffers[index]);
//

    VkSwapchainKHR swapchain_raw = swap_chain->get_handle();
    VkPresentInfoKHR present_info = {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, //type
        nullptr, //next
        0, //wait semaphores count
        nullptr, //wait semaphores
        1, //swapchain count
        &swapchain_raw, //swapchains
        &index, //image indices
        &res, //results
    };
    vkQueuePresentKHR(queue->get_handle(), &present_info);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to present queue: {}\n", res);
    }

sleep(10);
    return 0;
}
