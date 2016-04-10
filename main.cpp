
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
#include <glm/gtx/vector_query.hpp>

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
    vk_renderpass &get_renderpass() { return m_renderpass; }
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
    static VkSurfaceFormatKHR get_format(const vk_surface &surface, vk_physical_device *dev)
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

    static int get_queue_family(vk_physical_device *dev, const vk_surface &surface)
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

inline std::ostream &operator<<(std::ostream &os, const glm::vec3 &v)
{
    os << "vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
    return os;
}


class sg_item
{
public:
    sg_item(const vk_device &device)
        : m_device(device)
        , m_pipeline(device)
        , m_descset_layout(device, { })
        , m_pipeline_layout(device, m_descset_layout)
    {
    }

    void init(const vk_renderpass &rpass)
    {
        m_pipeline.add_stage(vk_shader_module::stage::vertex, "vert-ui.spv", "main");
        m_pipeline.add_stage(vk_shader_module::stage::fragment, "frag-ui.spv", "main");

        m_pipeline.set_primitive_mode(vk_graphics_pipeline::triangle_strip, false);
        m_pipeline.set_blending(true);

        m_pipeline.create(rpass, m_pipeline_layout);
    }

    void draw(vk_command_buffer &cmd_buffer)
    {
        cmd_buffer.set_parameter(m_pipeline);

        vkCmdDraw(cmd_buffer.get_handle(), 4, 1, 0, 0);
    }

    const vk_device &m_device;
    vk_graphics_pipeline m_pipeline;
    vk_descriptor_set_layout m_descset_layout;
    vk_pipeline_layout m_pipeline_layout;
};

static const int voxels[] = {
    0, 0, 0,
    1, 0, 0,
    2, 0, 0,
    3, 0, 0,
    4, 0, 0,
    5, 0, 0,
    0, 1, 0,
    1, 1, 0,
    2, 1, 0,
    3, 1, 0,
    4, 1, 0,
    5, 1, 0,
    0, 2, 0,
    1, 2, 0,
    2, 2, 0,
    3, 2, 0,
    4, 2, 0,
    5, 2, 0,
    0, 3, 0,
    1, 3, 0,
    2, 3, 0,
    3, 3, 0,
    4, 3, 0,
    5, 3, 0,
    0, 4, 0,
    1, 4, 0,
    2, 4, 0,
    3, 4, 0,
    4, 4, 0,
    5, 4, 0,
    0, 5, 0,
    1, 5, 0,
    2, 5, 0,
    3, 5, 0,
    4, 5, 0,
    5, 5, 0,
    1, 1, 1,
    0, 1, 0,
    3, 0, 1,
    3, 0, 2,
    3, 0, 3,
    3, 1, 3,
    3, 2, 3,
    3, 3, 3,
    3, 4, 3,
    3, 4, 2,
    3, 4, 1,
};

struct winhnd : public vk_window
{
    struct uniform_data {
        float matrix[16];
    };
    struct vertex { float p[3]; float c[4]; };
    struct instance_data {
        int x, y, z;
    };

    winhnd(display &dpy, const vk_instance &instance, int w, int h)
        : vk_window(dpy, instance, w, h)
        , m_display(dpy)
        , queue(get_device().get_queue(0))
        , cmd_pool(get_device().create_command_pool())
        , cmd_buffer(cmd_pool.create_command_buffer())
        , uniform_buffer(get_device(), vk_buffer::usage::uniform_buffer, sizeof(uniform_data), 0)
        , buf(get_device(), 8)
        , index_buffer(get_device(), vk_buffer::usage::index_buffer, 200, 0)
        , instances_buffer(get_device(), 128)
        , memory(get_device(), vk_device_memory::property::host_visible, 4096, uniform_buffer.get_required_memory_type())
        , descset_layout(get_device(), { { 0, vk_descriptor::type::uniform_buffer, 1, vk_shader_module::stage::vertex } })
        , descpool(get_device(), { { vk_descriptor::type::uniform_buffer, 1 } })
        , descset(descpool.allocate_descriptor_set(descset_layout))
        , pipeline_layout(get_device(), descset_layout)
        , pipeline(get_device())
        , m_time(0)
        , m_angle(0)
        , m_animate(true)
        , m_debug(false)
        , m_ui(get_device())
    {
        VkResult res;

        uint64_t offset = 0;
        print("mem size {}\n",buf.get_required_memory_size());
        buf.bind_memory(&memory, offset);
        buf.map([](void *data) {
            static const vertex vertices[] = {
                { { -1.0f, -1.0f, -1.f, }, { 1, 0, 0, 1, }, },
                { { -1.0f,  1.0f, -1.f, }, { 0, 1, 0, 1, }, },
                { {  1.0f,  1.0f, -1.f, }, { 0, 0, 1, 1, }, },
                { {  1.0f, -1.0f, -1.f, }, { 0, 0, 0, 1, }, },

                { { -1.0f, -1.0f,  1.0f, }, { 1, 0, 0, 1, }, },
                { { -1.0f,  1.0f,  1.0f, }, { 0, 1, 0, 1, }, },
                { {  1.0f,  1.0f,  1.0f, }, { 0, 0, 1, 1, }, },
                { {  1.0f, -1.0f,  1.0f, }, { 0, 0, 0, 1, }, },
            };

            memcpy(data, vertices, sizeof(vertices));
        });
        offset += buf.get_required_memory_size();

        index_buffer.bind_memory(&memory, offset);
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
        offset += index_buffer.get_required_memory_size();

        instances_buffer.bind_memory(&memory, offset);
        instances_buffer.map([&](void *data) {

            memcpy(data, voxels, sizeof(voxels));
        });
        offset += instances_buffer.get_required_memory_size();

        assert(uniform_buffer.get_required_memory_type() == buf.get_required_memory_type());
        assert(uniform_buffer.get_required_memory_type() == index_buffer.get_required_memory_type());
        uniform_buffer.bind_memory(&memory, offset);
        offset += uniform_buffer.get_required_memory_size();

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

        auto binding = pipeline.add_binding(buf, vk_graphics_pipeline::input_rate::vertex);
        pipeline.add_attribute(binding, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
        pipeline.add_attribute(binding, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 12);

        binding = pipeline.add_binding(instances_buffer, vk_graphics_pipeline::input_rate::instance);
        pipeline.add_attribute(binding, 2, VK_FORMAT_R32G32B32_UINT, 0);

        pipeline.set_primitive_mode(vk_graphics_pipeline::triangle_list, false);
        pipeline.set_blending(true);

        pipeline.create(get_renderpass(), pipeline_layout);

        m_ui.init(get_renderpass());


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

        m_camera.projection = glm::perspective<double>(glm::radians(60.f), 1, 0.1f, 256.f);

        m_camera.pos = glm::vec3(-15, 0, -30);
        update_camera_orientation();
        m_camera.view = glm::lookAt(m_camera.pos, m_camera.pos + m_camera.direction, m_camera.up);
        m_mouse_pressed = false;
    }

    void update_camera_orientation()
    {
        auto cy = cos(m_camera.angle.y);
        auto sx = sin(m_camera.angle.x);
        auto sy = sin(m_camera.angle.y);
        auto cx = cos(m_camera.angle.x);
        m_camera.direction = glm::vec3(cy * sx, sy, cy * cx);

        auto right = glm::vec3(sin(m_camera.angle.x - glm::half_pi<float>()), 0, cos(m_camera.angle.x - glm::half_pi<float>()));
        m_camera.up = glm::cross(right, m_camera.direction);
    }

    void update_camera(float time_diff)
    {
        bool update_view = false;
        if (m_mouse_pressed) {
            auto delta = m_cur_mouse_pos - m_mouse_pos;
            delta /= 8.f;

            m_camera.angle.x += delta.x * time_diff;
            m_camera.angle.y += delta.y * time_diff;
            update_camera_orientation();
            update_view = true;
        }

        if (!glm::isNull(m_camera.move, 0.001f)) {
            auto move = m_camera.move * time_diff * 2.f;

            glm::vec3 new_y = glm::normalize(m_camera.direction);
            glm::vec3 new_z = glm::cross(new_y, glm::vec3(0, 1, 0));
            glm::vec3 new_x = glm::cross(new_y, new_z);
            auto transform = glm::mat3(new_x, new_y, new_z);
            move = transform * move;

            m_camera.pos += move;
            update_view = true;
        }

        if (update_view) {
            m_camera.view = glm::lookAt(m_camera.pos, m_camera.pos + m_camera.direction, m_camera.up);
        }
        m_mouse_pos = m_cur_mouse_pos;
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

//         m_angle += 0.5 * time_diff * m_animate;

        update_camera(time_diff);

        glm::mat4 model(1.);
        model = glm::rotate<float>(model, m_angle, glm::vec3(1, 1, 1));
        glm::mat4 matrix = m_camera.projection * m_camera.view * model;

//         fmt::print("{}\n",m_camera_pos.z);
//         fmt::print("{}\n", matrix);

        uniform_buffer.map([&](void *ptr) {
            uniform_data *data = static_cast<uniform_data *>(ptr);

            memcpy(data->matrix, glm::value_ptr(matrix), sizeof(uniform_data::matrix));
        });

        const auto &framebuffer = acquire_next_framebuffer();

        cmd_buffer.begin();

        VkImageMemoryBarrier image_memory_barrier = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, //type
            nullptr, //next
            0, //src access mask
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, //dst access mask
            VK_IMAGE_LAYOUT_UNDEFINED, //old image layout
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, //new image layout
            0, //src queue family index
            0, //dst queue family index
            framebuffer.get_image().get_handle(), //image
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, //subresource range
        };

        vkCmdPipelineBarrier(cmd_buffer.get_handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &image_memory_barrier);

        VkClearValue color_clear, depth_clear;
        color_clear.color = { .float32 = {1.0f, 1.f, 1.f, 1.f} };
        depth_clear.depthStencil = { 1.f, 0 };

        get_renderpass().set_clear_values({ color_clear, depth_clear });
        vk_renderpass_record(get_renderpass(), cmd_buffer, framebuffer) {
            cmd_buffer.set_parameter(pipeline);
            vkCmdBindIndexBuffer(cmd_buffer.get_handle(), index_buffer.get_handle(), 0, VK_INDEX_TYPE_UINT32);

            VkDescriptorSet descsets[] = { descset.get_handle(), };
            vkCmdBindDescriptorSets(cmd_buffer.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout.get_handle(), 0, 1, descsets, 0, nullptr);

            auto viewport = vk_viewport(0, 0, framebuffer.get_width(), framebuffer.get_height());
            cmd_buffer.set_parameter(viewport);

            vkCmdDrawIndexed(cmd_buffer.get_handle(), 36, sizeof(voxels) / 12, 0, 0, 0);

            m_ui.draw(cmd_buffer);
        }

        VkImageMemoryBarrier present_image_memory_barrier = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, //type
            nullptr, //next
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, //src access mask
            VK_ACCESS_MEMORY_READ_BIT, //dst access mask
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, //old image layout
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, //new image layout
            0, //src queue family index
            0, //dst queue family index
            framebuffer.get_image().get_handle(), //image
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, //subresource range
        };
        vkCmdPipelineBarrier(cmd_buffer.get_handle(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                            nullptr, 1, &present_image_memory_barrier);


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
        m_cur_mouse_pos = glm::vec2(x, y);
    }

    void mouse_button(bool pressed)
    {
        m_mouse_pressed = pressed;
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
                case 24: {
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
                m_camera.move.y = 1 * pressed;
                break;
            }
            case 31: {
                m_camera.move.y = -1 * pressed;
                break;
            }
            case 32: {
                m_camera.move.z = -1 * pressed;
                break;
            }
            case 30: {
                m_camera.move.z = 1 * pressed;
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
    vk_vertex_buffer<instance_data> instances_buffer;
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
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec3 move;

        glm::vec3 pos, direction, up;
        glm::vec2 angle;
    } m_camera;
    glm::vec2 m_mouse_pos, m_cur_mouse_pos;
    bool m_mouse_pressed;
    sg_item m_ui;
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
