
#include <assert.h>
#include <unistd.h>

#include <exception>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "stringview.h"
#include "display.h"
#include "format.h"
#include "vk.h"
#include "vk_pipeline.h"
#include "vk_swapchain.h"

using std::string;
using std::weak_ptr;
using std::shared_ptr;
using std::unique_ptr;
using std::make_unique;
using std::make_shared;
using std::vector;
using fmt::print;


VkSurfaceFormatKHR get_format(const vk_surface &surface, vk_physical_device *dev)
{
    auto formats = surface.get_formats(dev);
    auto format = formats.at(0);
    print("Found {} formats, using {}\n", formats.size(), format.format);

    VkSurfaceCapabilitiesKHR surface_caps;
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->get_handle(), surface.get_handle(), &surface_caps);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to get physical device surface capabilities: {}\n");
    }

    uint32_t present_mode_count;
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(dev->get_handle(), surface.get_handle(), &present_mode_count, nullptr);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to get the number of physical device surface present modes: {}\n");
    }
    auto present_modes = vector<VkPresentModeKHR>(present_mode_count);
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(dev->get_handle(), surface.get_handle(), &present_mode_count, present_modes.data());
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to get the physical device surface present modes: {}\n");
    }
    print("Found {} present modes available\n", present_modes.size());

    return format;
}

int get_queue_family(vk_physical_device *dev, const vk_surface &surface)
{
    int family_queue_index = -1;
    auto queues_props = dev->get_queue_family_properties();
    for (size_t i = 0; i < queues_props.size(); ++i) {
        if (queues_props.at(i).is_graphics_capable() && surface.supports_present(dev, i)) {
            family_queue_index = i;
            break;
        }
    }

    if (family_queue_index < 0) {
        throw vk_exception("Cannot find graphics queue.\n");
    }
    return family_queue_index;
}


struct winhnd
{
    struct uniform_data {
        float matrix[16];
    };
    struct vertex { float p[3]; float c[4]; };

    winhnd(display &dpy, int w, int h)
        : win(dpy, w, h, *this)
        , m_display(dpy)
        , vk(dpy.create_vk_instance({ VK_EXT_DEBUG_REPORT_EXTENSION_NAME }))
        , phys_device(vk.get_physical_devices()[0])
        , surface(win.create_vk_surface(vk))
        , format(get_format(surface, &phys_device))
        , family_queue_index(get_queue_family(&phys_device, surface))
        , device(phys_device.create_device<vk_swapchain_extension>(family_queue_index))
        , queue(device.get_queue(0))
        , swapchain_ext(device.get_extension_object<vk_swapchain_extension>())
        , swapchain(swapchain_ext->create_swapchain(surface, format))
        , cmd_pool(device.create_command_pool())
        , cmd_buffer(cmd_pool.create_command_buffer())
        , uniform_buffer(device, vk_buffer::usage::uniform_buffer, sizeof(uniform_data), 0)
        , buf(device, 3)
        , memory(device, vk_device_memory::property::host_visible, 1024, uniform_buffer.get_required_memory_type())
        , render_pass(device, format.format)
        , descset_layout(device, { { 0, vk_descriptor::type::uniform_buffer, 1, vk_shader_module::stage::vertex } })
        , descpool(device, { { vk_descriptor::type::uniform_buffer, 1 } })
        , descset(descpool.allocate_descriptor_set(descset_layout))
        , pipeline_layout(device, descset_layout)
        , pipeline(device)
        , m_angle(0)
    {
        VkResult res;

        print("using queue index {}\n", family_queue_index);



        auto init_cmd_buf = cmd_pool.create_command_buffer();

        init_cmd_buf.begin();

        print("mem size {}\n",buf.get_required_memory_size());
        buf.bind_memory(&memory, 0);
        buf.map([](void *data) {
            static const vertex vertices[] = {
                { { -1.0f, -1.0f,  0.f, }, { 1, 0, 0, 1, }, },
                { {  0.0f,  1.0f,  0.f,  }, { 0, 1, 0, 0, }, },
                { {  1.0f, -1.0f,  0.f, }, { 0, 0, 1, 1, }, },
            };

            memcpy(data, vertices, sizeof(vertices));
        });



        assert(uniform_buffer.get_required_memory_type() == buf.get_required_memory_type());
        uniform_buffer.bind_memory(&memory, buf.get_required_memory_size());








        auto fence = vk_fence(device);

        struct {
            vk_descriptor::type type() const { return vk_descriptor::type::uniform_buffer; }
            const vk_buffer &buffer() const { return buf; }
            uint64_t offset() const { return 0; }
            uint64_t size() const { return sizeof(uniform_data); }
            const vk_buffer &buf;
        } update_info = { uniform_buffer };
        descset.update(update_info);



        vkDeviceWaitIdle(device.get_handle());

        const auto &imgs = swapchain.get_images();
        print("{} images available\n", imgs.size());



        buffers.reserve(imgs.size());
        for (const vk_image &img: imgs) {
            print("creating buffer {}\n",(void*)&img);
            buffers.emplace_back(device, img, render_pass);
        }


        pipeline.add_stage(vk_shader_module::stage::vertex, "vert.spv", "main");
        pipeline.add_stage(vk_shader_module::stage::fragment, "frag.spv", "main");

        auto binding = pipeline.add_binding(buf);
        pipeline.add_attribute(binding, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
        pipeline.add_attribute(binding, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 12);

        pipeline.set_primitive_mode(vk_graphics_pipeline::triangle_list, false);
        pipeline.set_blending(true);

        pipeline.create(render_pass, pipeline_layout);













        init_cmd_buf.end();

        vkDeviceWaitIdle(device.get_handle());

        const VkCommandBuffer cmd_bufs[] = { init_cmd_buf.get_handle() };
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

        res = vkQueueSubmit(queue.get_handle(), 1, &submit_info, VK_NULL_HANDLE);
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to submit queue: {}\n", res);
        }

        res = vkQueueWaitIdle(queue.get_handle());
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to wait queue: {}\n", res);
        }

    }

    void update(double time)
    {
        double time_diff = m_time < 1 ? 0 : time - m_time;
        m_time = time;

        fmt::print("frame time: {}\n", time_diff);
//             assert(time_diff<30);

        m_angle += 0.5 * time_diff;
        glm::mat4 matrix = glm::ortho<double>(-2, 2, -2, 2, 1, 0);
        matrix = glm::rotate<float>(matrix, m_angle, glm::vec3(0,0,1));

        uniform_buffer.map([&matrix](void *ptr) {
            uniform_data *data = static_cast<uniform_data *>(ptr);

            memcpy(data->matrix, glm::value_ptr(matrix), sizeof(uniform_data::matrix));
        });

        uint32_t index = swapchain.acquire_next_image_index();
        const auto &framebuffer = buffers[index];

//             print("draw on {}\n",index);

        cmd_buffer.begin();

        VkClearValue clear_values[1];
        clear_values[0].color.float32[0] = 1.0f;
        clear_values[0].color.float32[1] = 1.0f;
        clear_values[0].color.float32[2] = 1.0f;
        clear_values[0].color.float32[3] = 1.0f;
        VkRenderPassBeginInfo render_pass_begin_info = {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, //type
            nullptr, //next
            render_pass.get_handle(), //render pass
            framebuffer.get_handle(), //framebuffer
            { { 0, 0 }, { framebuffer.get_width(), framebuffer.get_height() } }, //render area
            1, //clear value count
            clear_values, //clear values
        };

        vkCmdBeginRenderPass(cmd_buffer.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        cmd_buffer.set_parameter(pipeline);

        VkDescriptorSet descsets[] = { descset.get_handle(), };
        vkCmdBindDescriptorSets(cmd_buffer.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout.get_handle(), 0, 1, descsets, 0, nullptr);


        auto viewport = vk_viewport(0, 0, framebuffer.get_width(), framebuffer.get_height());
        cmd_buffer.set_parameter(viewport);




        vkCmdDraw(cmd_buffer.get_handle(), 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd_buffer.get_handle());


        cmd_buffer.end();


        VkImageMemoryBarrier image_memory_barrier = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, //type
            nullptr, //next
            0, //src access mask
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, //dst access mask
            VK_IMAGE_LAYOUT_UNDEFINED, //old image layout
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, //new image layout
            0, //src queue family index
            0, //dst queue family index
            framebuffer.get_image().get_handle(), //image
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, //subresource range
        };

        vkCmdPipelineBarrier(cmd_buffer.get_handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &image_memory_barrier);


        VkPipelineStageFlags pipe_stage_flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkCommandBuffer cmd_buf_raw = cmd_buffer.get_handle();
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
        VkResult res = vkQueueSubmit(queue.get_handle(), 1, &submit_draw_info, VK_NULL_HANDLE);
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to submit queue: {}\n", res);
        }

        // == SWAP ==


    //     vc->model.render(vc, &vc->buffers[index]);
    //
        swapchain.present(queue, index);

        win.update();
    }

    void mouse_motion(double x, double y)
    {
//             print("motion {} {}\n", x, y);
    }

    void mouse_button(bool pressed)
    {
//             print("button {}\n",pressed);
        m_display.quit();
    }

    void show()
    {
        win.show();
    }

    window win;
    display &m_display;
    vk_instance vk;
    vk_physical_device phys_device;
    vk_surface surface;
    VkSurfaceFormatKHR format;
    int family_queue_index;
    vk_device device;
    vk_queue queue;
    std::shared_ptr<vk_swapchain_extension> swapchain_ext;
    vk_swapchain swapchain;
    vk_command_pool cmd_pool;
    vk_command_buffer cmd_buffer;
    vk_buffer uniform_buffer;
    vk_vertex_buffer<vertex> buf;
    vk_device_memory memory;
    std::vector<vk_framebuffer> buffers;
    vk_renderpass render_pass;
    vk_descriptor_set_layout descset_layout;
    vk_descriptor_pool descpool;
    vk_descriptor_set descset;
    vk_pipeline_layout pipeline_layout;
    vk_graphics_pipeline pipeline;
    double m_time;
    double m_angle;
};



int main(int argc, char **argv)
{
    auto plat = platform::xcb;
    if (argc > 1 && stringview(argv[1]) == "wl") {
        plat = platform::wayland;
    }


    auto layers = vk_instance::get_available_layers();
    print("Found {} available layers\n", layers.size());
    int i = 0;
    for (vk_layer &layer: layers) {
        print("{}: {} == {}\n", i++, layer.get_name(), layer.get_description());
    }

    auto dpy = display(plat);

    auto win = winhnd(dpy, 600, 600);
    win.show();
    win.win.update();


    dpy.run();
    return 0;
}
