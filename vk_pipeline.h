
#pragma once

#include "vk.h"

class vk_descriptor
{
public:
    enum class type {
        sampler = VK_DESCRIPTOR_TYPE_SAMPLER,
        combined_image_sampler = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        sampled_image = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        storage_image = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        uniform_texel_buffer = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        storage_texel_buffer = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        uniform_buffer = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        storage_buffer = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        uniform_buffer_dynamic = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        storage_buffer_dynamic = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
        input_attachment = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
    };
};

class vk_descriptor_set
{
public:
    vk_descriptor_set(const vk_device &device, VkDescriptorSet handle);

    template<class T>
    void update(const T &update_info)
    {
        VkDescriptorBufferInfo descset_buffer_info = {
            update_info.buffer().get_handle(),
            update_info.offset(),
            update_info.size(),
        };

        VkWriteDescriptorSet descset_writes[] = {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, //type
            nullptr, //next
            m_handle, //descriptor set
            0, //binding
            0, //starting array element
            1, //descriptor count
            to_vktype(update_info.type()), //descriptor type
            nullptr, //image info
            &descset_buffer_info, //buffer info
            nullptr, //texel buffer view
        };
        vkUpdateDescriptorSets(m_device.get_handle(), 1, descset_writes, 0, nullptr);
    }

    VkDescriptorSet get_handle() const { return m_handle; }

private:
    VkDescriptorType to_vktype(vk_descriptor::type v) const { return (VkDescriptorType)v; }

    const vk_device &m_device;
    VkDescriptorSet m_handle;
};

class vk_descriptor_set_layout
{
public:
    struct binding
    {
        uint32_t binding_id;
        vk_descriptor::type type;
        uint32_t descriptor_count;
        vk_shader_module::stage shader_stages;
    };

    vk_descriptor_set_layout(const vk_device &device, const std::vector<binding> &bindings);

    VkDescriptorSetLayout get_handle() const { return m_handle; }

private:
    VkDescriptorSetLayout m_handle;
};

class vk_descriptor_pool
{
public:
    vk_descriptor_pool(const vk_device &device, const std::vector<std::pair<vk_descriptor::type, uint32_t>> &sizes);
    ~vk_descriptor_pool();

    vk_descriptor_set allocate_descriptor_set(const vk_descriptor_set_layout &descset_layout);

    VkDescriptorPool get_handle() const { return m_handle; }

private:
    const vk_device &m_device;
    VkDescriptorPool m_handle;
};

class vk_pipeline_layout
{
public:
    vk_pipeline_layout(const vk_device &device, const vk_descriptor_set_layout &descset_layout);
    ~vk_pipeline_layout();

    VkPipelineLayout get_handle() const { return m_handle; }

private:
    VkPipelineLayout m_handle;
    const vk_device &m_device;
};

class vk_renderpass
{
public:
    vk_renderpass(const vk_device &device, VkFormat format);
    ~vk_renderpass();

    VkRenderPass get_handle() const { return m_handle; }

private:
    VkRenderPass m_handle;
    const vk_device &m_device;
};

class vk_graphics_pipeline
{
public:
    enum topology {
        point_list = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        line_list = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        line_strip = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        triangle_list = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        triangle_strip = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        triangle_fan = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        line_list_with_adjacency = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
        line_strip_with_adjacency = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
        triangle_list_with_adjacency = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
        triangle_strip_with_adjacency = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
        patch_list = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    };
    enum class polygon_mode {
        fill = VK_POLYGON_MODE_FILL,
        line = VK_POLYGON_MODE_LINE,
        point = VK_POLYGON_MODE_POINT,
    };
    enum class cull_mode {
        front = VK_CULL_MODE_FRONT_BIT,
        back = VK_CULL_MODE_BACK_BIT,
        front_and_back = VK_CULL_MODE_FRONT_AND_BACK,
    };
    enum class front_face {
        counter_clockwise = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        clockwise = VK_FRONT_FACE_CLOCKWISE,
    };

    class binding
    {
        binding() {}
        binding(uint32_t n) : number(n) {}
        uint32_t number;

        friend vk_graphics_pipeline;
    };

    explicit vk_graphics_pipeline(const vk_device &device);

    void add_stage(const vk_shader_module &shader, stringview entrypoint);
    void add_stage(vk_shader_module::stage s, stringview filename, stringview entrypoint);

    binding add_binding(const vk_buffer &buffer);
    void add_attribute(binding b, uint32_t location, VkFormat format, uint32_t offset);
    void set_primitive_mode(topology topology, bool primitive_restart_enable);

    void set_polygon_mode(polygon_mode mode);
    void enable_culling(cull_mode mode, front_face front);
    void disable_culling();
    void set_blending(bool enabled);

    VkPipeline get_handle() const { return m_handle; }

    void create(const vk_renderpass &render_pass, const vk_pipeline_layout &pipeline_layout);
    void set_in_command_buffer(const vk_command_buffer &cmd_buffer) const;

private:
    void get_shader_info(VkPipelineShaderStageCreateInfo *info);
    void get_bindings_info(VkPipelineVertexInputStateCreateInfo *info, VkVertexInputBindingDescription *binding_desc, VkVertexInputAttributeDescription *attr_desc);

    const vk_device &m_device;
    VkPipeline m_handle;
    struct shader_stage {
        shader_stage(const vk_shader_module &module, const std::string &ep)
            : shader(module)
            , entrypoint(ep)
        {}
        vk_shader_module shader;
        std::string entrypoint;
    };
    std::vector<shader_stage> m_stages;
    struct attribute {
        attribute(uint32_t b, uint32_t loc, VkFormat f, uint32_t off)
            : bind(b)
            , location(loc)
            , format(f)
            , offset(off)
        {}
        uint32_t bind;
        uint32_t location;
        VkFormat format;
        uint32_t offset;
    };
    std::vector<attribute> m_attributes;
    struct binding_state {
        binding_state(const vk_buffer &buf) : buffer(buf) {}
        const vk_buffer &buffer;
    };
    std::vector<binding_state> m_bindings;

    topology m_topology;
    bool m_primitive_restart;
    polygon_mode m_polygon_mode;
    struct {
        VkCullModeFlagBits mode;
        front_face front;
    } m_cull;

    struct {
        bool enabled;
    } m_blending;
};

class vk_framebuffer
{
public:
    vk_framebuffer(const vk_device &device, const vk_image &img, const vk_renderpass &rpass);

    uint32_t get_width() const { return m_image.get_width(); }
    uint32_t get_height() const { return m_image.get_height(); }

    const vk_image &get_image() const { return m_image; }
    VkFramebuffer get_handle() const { return m_handle; }

private:
    const vk_device &m_device;
    const vk_image &m_image;
    vk_image_view m_view;
    VkFramebuffer m_handle;
};
