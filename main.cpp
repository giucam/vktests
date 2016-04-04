
#include <assert.h>
#include <unistd.h>

#include <exception>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
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

class vk_window
{
public:
    vk_window(display &dpy, const vk_instance &instance, int w, int h)
        : m_window(dpy, w, h, *this)
        , m_instance(instance)
        , m_phys_device(instance.get_physical_devices()[0])
        , m_surface(m_window.create_vk_surface(instance))
        , m_format(get_format(m_surface, &m_phys_device))
        , m_family_queue_index(get_queue_family(&m_phys_device, m_surface))
        , m_device(m_phys_device.create_device<vk_swapchain_extension>(m_family_queue_index))
        , m_swapchain_ext(m_device.get_extension_object<vk_swapchain_extension>())
        , m_swapchain(m_swapchain_ext->create_swapchain(m_surface, m_format))
        , m_depth({ vk_image(m_device, VK_FORMAT_D24_UNORM_S8_UINT, vk_image::usage::depth_stencil_attachment, vk_image::type::t2D, { (uint32_t)w, (uint32_t)h, 1u }),
                    vk_device_memory(m_device, vk_device_memory::property::device_local, m_depth.image.get_required_memory_size(), m_depth.image.get_required_memory_type()),
                    vk_image_view() })
        , m_renderpass(m_device, m_format.format, m_depth.image.get_format())
        , m_cmd_pool(get_device().create_command_pool())
        , m_init_cmd_buf(m_cmd_pool.create_command_buffer())
    {
        print("using queue index {}\n", m_family_queue_index);

        m_depth.image.bind_memory(&m_depth.mem, 0);
        m_depth.view = m_depth.image.create_image_view(vk_image::aspect::depth);

        const auto &imgs = m_swapchain.get_images();
        print("{} images available\n", imgs.size());

        m_framebuffers.reserve(imgs.size());
        for (const vk_image &img: imgs) {
            print("creating buffer {}\n",(void*)&img);
            m_framebuffers.emplace_back(get_device(), img, m_depth.view, m_renderpass);
        }

        m_init_cmd_buf.begin();

        VkImageMemoryBarrier image_memory_barrier = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, //type
            nullptr, //next
            0, //src access mask
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, //dst access mask
            VK_IMAGE_LAYOUT_UNDEFINED, //old image layout
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, //new image layout
            0, //src queue family index
            0, //dst queue family index
            m_depth.image.get_handle(), //image
            { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }, //subresource range
        };

        vkCmdPipelineBarrier(m_init_cmd_buf.get_handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &image_memory_barrier);


    }

    void show() { m_window.show(); }
    void schedule_update() { m_window.update(); }

    uint32_t get_width() const { return m_window.get_width(); }
    uint32_t get_height() const { return m_window.get_height(); }

    const vk_surface &get_surface() const { return m_surface; }
    const vk_device &get_device() const { return m_device; }
    vk_command_buffer &get_init_command_buffer() { return m_init_cmd_buf; }
    const vk_renderpass &get_renderpass() const { return m_renderpass; }
    const vk_framebuffer &acquire_next_framebuffer()
    {
        m_fb_index = m_swapchain.acquire_next_image_index();
        return m_framebuffers[m_fb_index];
    }

    void present_current_framebuffer(const vk_queue &queue)
    {
        m_swapchain.present(queue, m_fb_index);
    }

    virtual void update(double /*time*/) {}
    virtual void mouse_motion(double /*x*/, double /*y*/) {}
    virtual void mouse_button(bool /*pressed*/) {}
    virtual void key(uint32_t /*key*/, bool /*pressed*/) {}

private:
    window m_window;
    const vk_instance &m_instance;
    vk_physical_device m_phys_device;
    vk_surface m_surface;
    VkSurfaceFormatKHR m_format;
    int m_family_queue_index;
    vk_device m_device;
    std::shared_ptr<vk_swapchain_extension> m_swapchain_ext;
    vk_swapchain m_swapchain;
    std::vector<vk_framebuffer> m_framebuffers;
    struct {
        vk_image image;
        vk_device_memory mem;
        vk_image_view view;
    } m_depth;
    vk_renderpass m_renderpass;
    vk_command_pool m_cmd_pool;
    uint32_t m_fb_index;
    vk_command_buffer m_init_cmd_buf;
};

inline std::ostream &operator<<(std::ostream &os, const glm::mat4x4 &m)
{
    for (int i = 0; i < 4; ++i) {
        const auto &col = m[i];
        for (int j = 0; j < 4; ++j) {
            os << col[j] << ' ';
        }
        os << '\n';
    }
    return os;
}

struct winhnd : public vk_window
{
    struct uniform_data {
        float matrix[16];
    };
    struct vertex { float p[3]; float c[4]; };

    winhnd(display &dpy, const vk_instance &instance, int w, int h)
        : vk_window(dpy, instance, w, h)
        , m_display(dpy)
        , queue(get_device().get_queue(0))
        , cmd_pool(get_device().create_command_pool())
        , cmd_buffer(cmd_pool.create_command_buffer())
        , uniform_buffer(get_device(), vk_buffer::usage::uniform_buffer, sizeof(uniform_data), 0)
        , buf(get_device(), 8)
        , index_buffer(get_device(), vk_buffer::usage::index_buffer, 200, 0)
        , memory(get_device(), vk_device_memory::property::host_visible, 2048, uniform_buffer.get_required_memory_type())
        , descset_layout(get_device(), { { 0, vk_descriptor::type::uniform_buffer, 1, vk_shader_module::stage::vertex } })
        , descpool(get_device(), { { vk_descriptor::type::uniform_buffer, 1 } })
        , descset(descpool.allocate_descriptor_set(descset_layout))
        , pipeline_layout(get_device(), descset_layout)
        , pipeline(get_device())
        , m_time(0)
        , m_angle(0)
        , m_animate(true)
        , m_debug(false)
        , m_camera_pos({ 0, 0, 10, 0, 0, 0 })
    {
        VkResult res;

        print("mem size {}\n",buf.get_required_memory_size());
        buf.bind_memory(&memory, 0);
        buf.map([](void *data) {
            static const vertex vertices[] = {
                { { -1.0f, -1.0f, -1.f, }, { 1, 0, 0, 1, }, },
                { { -1.0f,  1.0f, -1.f, }, { 0, 1, 0, 1, }, },
                { {  1.0f,  1.0f, -1.f, }, { 0, 0, 1, 1, }, },
                { {  1.0f, -1.0f, -1.f, }, { 0, 0, 0, 0, }, },

                { { -1.0f, -1.0f,  1.0f, }, { 1, 0, 0, 1, }, },
                { { -1.0f,  1.0f,  1.0f, }, { 0, 1, 0, 1, }, },
                { {  1.0f,  1.0f,  1.0f, }, { 0, 0, 1, 1, }, },
                { {  1.0f, -1.0f,  1.0f, }, { 0, 0, 0, 0, }, },
            };

            memcpy(data, vertices, sizeof(vertices));
        });

        index_buffer.bind_memory(&memory, buf.get_required_memory_size());
        index_buffer.map([](void *data) {
            static const uint32_t indices[] = {
                //front
                0, 1, 2,
                0, 2, 3,

                //right
                3, 2, 6,
                3, 6, 7,

                //top
                4, 0, 3,
                4, 3, 7,

                //left
                4, 5, 0,
                0, 5, 1,

                //bottom
                1, 5, 6,
                1, 6, 2,

                //back
                7, 6, 5,
                7, 5, 4,
            };
            memcpy(data, indices, sizeof(indices));
        });


        assert(uniform_buffer.get_required_memory_type() == buf.get_required_memory_type());
        assert(uniform_buffer.get_required_memory_type() == index_buffer.get_required_memory_type());
        uniform_buffer.bind_memory(&memory, buf.get_required_memory_size() + index_buffer.get_required_memory_size());

        auto fence = vk_fence(get_device());

        struct {
            vk_descriptor::type type() const { return vk_descriptor::type::uniform_buffer; }
            const vk_buffer &buffer() const { return buf; }
            uint64_t offset() const { return 0; }
            uint64_t size() const { return sizeof(uniform_data); }
            const vk_buffer &buf;
        } update_info = { uniform_buffer };
        descset.update(update_info);

        vkDeviceWaitIdle(get_device().get_handle());

        pipeline.add_stage(vk_shader_module::stage::vertex, "vert.spv", "main");
        pipeline.add_stage(vk_shader_module::stage::fragment, "frag.spv", "main");

        auto binding = pipeline.add_binding(buf);
        pipeline.add_attribute(binding, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
        pipeline.add_attribute(binding, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 12);

        pipeline.set_primitive_mode(vk_graphics_pipeline::triangle_list, false);
        pipeline.set_blending(true);

        pipeline.create(get_renderpass(), pipeline_layout);


        get_init_command_buffer().end();

        vkDeviceWaitIdle(get_device().get_handle());

        const VkCommandBuffer cmd_bufs[] = { get_init_command_buffer().get_handle() };
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

        if (m_debug) {
            fmt::print("frame time: {}\n", time_diff);
        }
//             assert(time_diff<30);

        vkDeviceWaitIdle(get_device().get_handle());

        m_angle += 0.5 * time_diff * m_animate;

        m_camera_pos.x += m_camera_pos.move_x * time_diff;
        m_camera_pos.y += m_camera_pos.move_y * time_diff;
        m_camera_pos.z += m_camera_pos.move_z * time_diff;

        glm::mat4 matrix = glm::perspective<double>(2, 1, 0.1f, 256.f);
        matrix = glm::translate<float>(matrix, glm::vec3(m_camera_pos.x, m_camera_pos.y, m_camera_pos.z));
        matrix = glm::rotate<float>(matrix, m_angle, glm::vec3(1, 1, 1));

//         fmt::print("{}\n",m_camera_pos.z);
//         fmt::print("{}\n", matrix);

        uniform_buffer.map([&matrix](void *ptr) {
            uniform_data *data = static_cast<uniform_data *>(ptr);

            memcpy(data->matrix, glm::value_ptr(matrix), sizeof(uniform_data::matrix));
        });

        const auto &framebuffer = acquire_next_framebuffer();

        cmd_buffer.begin();

        VkClearValue clear_values[2];
        clear_values[0].color.float32[0] = 1.0f;
        clear_values[0].color.float32[1] = 1.0f;
        clear_values[0].color.float32[2] = 1.0f;
        clear_values[0].color.float32[3] = 1.0f;

        clear_values[1].depthStencil = { 1.f, 0 };
        VkRenderPassBeginInfo render_pass_begin_info = {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, //type
            nullptr, //next
            get_renderpass().get_handle(), //render pass
            framebuffer.get_handle(), //framebuffer
            { { 0, 0 }, { framebuffer.get_width(), framebuffer.get_height() } }, //render area
            sizeof(clear_values) / sizeof(VkClearValue), //clear value count
            clear_values, //clear values
        };

        vkCmdBeginRenderPass(cmd_buffer.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        cmd_buffer.set_parameter(pipeline);
        vkCmdBindIndexBuffer(cmd_buffer.get_handle(), index_buffer.get_handle(), 0, VK_INDEX_TYPE_UINT32);

        VkDescriptorSet descsets[] = { descset.get_handle(), };
        vkCmdBindDescriptorSets(cmd_buffer.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout.get_handle(), 0, 1, descsets, 0, nullptr);

        auto viewport = vk_viewport(0, 0, framebuffer.get_width(), framebuffer.get_height());
        cmd_buffer.set_parameter(viewport);

        vkCmdDrawIndexed(cmd_buffer.get_handle(), 36, 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd_buffer.get_handle());


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

        cmd_buffer.end();

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

        present_current_framebuffer(queue);
        schedule_update();
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

    void key(uint32_t k, bool pressed)
    {
        if (pressed) {
            fmt::print("key {}\n",k);
            switch (k) {
                case 57: {
                    m_animate = !m_animate;
                    return;
                }
                case 32: {
                    m_debug = !m_debug;
                    return;
                }
                case 16: {
                    m_display.quit();
                    return;
                }
            }
        }

        switch (k) {
            case 17: {
                m_camera_pos.move_z = 2 * pressed;
                break;
            }
            case 31: {
                m_camera_pos.move_z = -2 * pressed;
                break;
            }
        }
    }

    display &m_display;
    vk_queue queue;
    vk_command_pool cmd_pool;
    vk_command_buffer cmd_buffer;
    vk_buffer uniform_buffer;
    vk_vertex_buffer<vertex> buf;
    vk_buffer index_buffer;
    vk_device_memory memory;
    vk_descriptor_set_layout descset_layout;
    vk_descriptor_pool descpool;
    vk_descriptor_set descset;
    vk_pipeline_layout pipeline_layout;
    vk_graphics_pipeline pipeline;
    double m_time;
    double m_angle;
    bool m_animate;
    bool m_debug;
    struct {
        double x, y, z;
        double move_x, move_y, move_z;
    } m_camera_pos;
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

    auto instance = dpy.create_vk_instance({ VK_EXT_DEBUG_REPORT_EXTENSION_NAME });
    auto win = winhnd(dpy, instance, 600, 600);
    win.show();
    win.schedule_update();


    dpy.run();
    return 0;
}
