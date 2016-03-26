
#pragma once

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "format.h"
#include "stringview.h"

class window;
class vk_device;
class vk_physical_device;

class vk_exception : public std::exception
{
public:
    template<typename... args>
    explicit vk_exception(args... a)
        : m_err(fmt::format(a...))
    {
    }

    const char *what() const throw()
    {
        return m_err.data();
    }

private:
    std::string m_err;
};

std::ostream &operator<<(std::ostream &os, VkResult v);

class vk_queue
{
public:
    vk_queue(VkQueue queue, uint32_t family_index, uint32_t index);
    vk_queue(const vk_queue &) = delete;
    vk_queue(vk_queue &&);

    VkQueue get_handle() const { return m_handle; }
    uint32_t get_family_index() const { return m_family_index; }
    uint32_t get_index() const { return m_index; }

private:
    VkQueue m_handle;
    uint32_t m_family_index;
    uint32_t m_index;
};

class vk_command_buffer
{
public:
    explicit vk_command_buffer(VkCommandBuffer buf);
    vk_command_buffer(const vk_command_buffer &) = delete;
    vk_command_buffer(vk_command_buffer &&);

    void begin();
    void end();

    template<class T>
    void set_parameter(const T &parameter)
    {
        parameter.set_in_command_buffer(*this);
    }

    VkCommandBuffer get_handle() const { return m_handle; }

private:
    VkCommandBuffer m_handle;
};

class vk_command_pool
{
public:
    vk_command_pool(const vk_device &device, VkCommandPool handle);
    vk_command_pool(const vk_command_pool &) = delete;
    vk_command_pool(vk_command_pool &&);

    vk_command_buffer create_command_buffer();

    VkCommandPool get_handle() const { return m_handle; }

private:
    const vk_device &m_device;
    VkCommandPool m_handle;
};

class vk_device
{
public:
    vk_device(const vk_device &) = delete;
    vk_device(vk_device &&);
    ~vk_device();

    vk_queue get_queue(uint32_t index);
    vk_command_pool create_command_pool();

    template<class T>
    std::shared_ptr<T> get_extension_object() {
        if (!is_extension_enabled(T::get_extension())) {
            throw vk_exception("Cannot create the requested extension object. Extension '{}' not activated.\n", T::get_extension());
        }
        return std::make_shared<T>(*this);
    }
    bool is_extension_enabled(stringview extension) const;

    const vk_physical_device &get_physical_device() const { return m_physical_device; }

    VkDevice get_handle() const { return m_handle; }

private:
    vk_device(const vk_physical_device &phys);

    VkDevice m_handle;
    std::vector<std::string> m_extensions;
    const vk_physical_device &m_physical_device;
    uint32_t m_queue_family_index;
    friend class vk_physical_device;
};

inline bool operator==(const vk_device &a, const vk_device &b) { return a.get_handle() == b.get_handle(); }
inline bool operator!=(const vk_device &a, const vk_device &b) { return a.get_handle() != b.get_handle(); }

class vk_queue_family_properties
{
public:
    bool is_graphics_capable() const;
    bool is_compute_capable() const;
    bool is_transfer_capable() const;
    uint32_t queue_count() const;

private:
    VkQueueFamilyProperties m_handle;
};

class vk_physical_device
{
public:
    vk_physical_device();

    template<class... types>
    vk_device create_device(uint32_t queue_family_index) {
        std::vector<std::string> extensions;
        populate_extensions<types...>(extensions);
        return do_create_device(queue_family_index, extensions);
    }

    uint32_t get_vendor_id() const { return m_props.vendorID; }
    stringview get_device_name() const { return m_props.deviceName; }
    const std::vector<vk_queue_family_properties> &get_queue_family_properties() const { return m_queue_properties; }

    uint32_t get_memory_types_count() const;
    VkMemoryType get_memory_type(uint32_t index) const;

    VkPhysicalDevice get_handle() { return m_handle; }

private:
    void set(VkPhysicalDevice dev);
    vk_device do_create_device(uint32_t queue_family_index, const std::vector<std::string> &extensions);
    template<class first, class second, class... others>
    void populate_extensions(std::vector<std::string> &extensions) {
        extensions.emplace_back(first::get_extension().to_string());
        populate_extensions<second, others...>(extensions);
    }
    template<class first>
    void populate_extensions(std::vector<std::string> &extensions) {
        extensions.emplace_back(first::get_extension().to_string());
    }

    VkPhysicalDevice m_handle;
    VkPhysicalDeviceProperties m_props;
    VkPhysicalDeviceMemoryProperties m_memprops;
    std::vector<vk_queue_family_properties> m_queue_properties;

    friend class vk_instance;
};

class vk_layer
{
public:
    explicit vk_layer(const VkLayerProperties &props);

    stringview get_name() const { return m_props.layerName; }
    stringview get_description() const { return m_props.description; }

private:
    VkLayerProperties m_props;
};

class vk_instance
{
public:
    vk_instance(const std::vector<std::string> &layer_names, const std::vector<std::string> &extensions);
    vk_instance(const vk_instance &) = delete;
    vk_instance(vk_instance &&);

    ~vk_instance();

    VkInstance get_handle() const { return m_instance; }
    std::vector<vk_physical_device> get_physical_devices() const { return m_physical_devices; }
    static std::vector<vk_layer> get_available_layers();

private:
    VkInstance m_instance;
    std::vector<vk_physical_device> m_physical_devices;
};

class vk_surface
{
public:
    vk_surface(const vk_instance &instance, const window &window, VkSurfaceKHR surface);
    vk_surface(const vk_surface &) = delete;
    vk_surface(vk_surface &&s);
    ~vk_surface();

    bool supports_present(vk_physical_device *device, int queue_family) const;
    std::vector<VkSurfaceFormatKHR> get_formats(vk_physical_device *device) const;

    const window &get_window() const { return m_window; }
    VkSurfaceKHR get_handle() const { return m_handle; }

private:
    const vk_instance &m_instance;
    const window &m_window;
    VkSurfaceKHR m_handle;
};

class vk_image_view
{
public:
    vk_image_view(const vk_device &device, VkImageView view);
    vk_image_view(const vk_image_view &) = delete;
    vk_image_view(vk_image_view &&);
    ~vk_image_view();

    VkImageView get_handle() const { return m_handle; }

private:
    const vk_device &m_device;
    VkImageView m_handle;
};

class vk_image
{
public:
    vk_image(const vk_device &device, VkImage img, const VkExtent3D &extent);
    vk_image(const vk_image &) = delete;
    vk_image(vk_image &&);
    ~vk_image();

    uint32_t get_width() const { return m_extent.width; }
    uint32_t get_height() const { return m_extent.height; }
    uint32_t get_depth() const { return m_extent.depth; }

    VkImage get_handle() const { return m_handle; }

    vk_image_view create_image_view() const;

private:
    const vk_device &m_device;
    VkImage m_handle;
    VkExtent3D m_extent;
    bool m_owns_handle;
};

class vk_device_memory
{
public:
    enum class property {
        device_local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        host_visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        host_coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        host_cached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        lazily_allocated = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
    };
    vk_device_memory(const vk_device &device, property props, uint64_t size, uint32_t type_bits);
    ~vk_device_memory();

    void *map(uint64_t offset);
    void unmap();

    VkDeviceMemory get_handle() const { return m_handle; }

private:
    uint32_t get_mem_index(property props, uint32_t type_bits);

    const vk_device &m_device;
    VkDeviceMemory m_handle;
    uint64_t m_size;
    property m_props;
};

#define FLAGS(flags) \
inline int operator&(flags a, flags b) { return (int)a & (int)b; }

FLAGS(vk_device_memory::property)

class vk_buffer
{
public:
    enum class usage {
        transfer_src = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        transfer_dst = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        uniform_texel_bufer = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        storage_texel_buffer = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        uniform_buffer = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        storage_buffer = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        index_buffer = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        vertex_buffer = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        indirect_buffer = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
    };

    vk_buffer(const vk_device &device, usage u, uint64_t size, uint32_t stride);
    ~vk_buffer();

    uint64_t get_required_memory_size() const { return m_mem_reqs.size; }
    uint64_t get_required_memory_alignment() const { return m_mem_reqs.alignment; }
    uint32_t get_required_memory_type() const { return m_mem_reqs.memoryTypeBits; }

    void bind_memory(vk_device_memory *mem, uint64_t offset);
    void map(const std::function<void (void *)> &cp);

    uint32_t stride() const { return m_stride; }
    uint64_t offset() const { return m_mem_offset; }

    VkBuffer get_handle() const { return m_handle; }

private:
    const vk_device &m_device;
    VkBuffer m_handle;
    VkMemoryRequirements m_mem_reqs;
    vk_device_memory *m_mem;
    uint64_t m_mem_offset;
    uint32_t m_stride;
};

template<class vertex>
class vk_vertex_buffer : public vk_buffer
{
public:
    vk_vertex_buffer(const vk_device &device, uint64_t num_elements)
        : vk_buffer(device, vk_buffer::usage::vertex_buffer, num_elements * sizeof(vertex), sizeof(vertex)) {}
};

class vk_shader_module
{
public:
    enum class stage {
        vertex = VK_SHADER_STAGE_VERTEX_BIT,
        fragment = VK_SHADER_STAGE_FRAGMENT_BIT,
        geometry = VK_SHADER_STAGE_GEOMETRY_BIT,
        tessellation_control = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        tessellation_evaluation = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        compute = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    vk_shader_module(const vk_device &device, stage s, const char *code, size_t size);
    vk_shader_module(const vk_device &device, stage s, stringview file);
    ~vk_shader_module();

    const vk_device &get_device() const;
    stage get_stage() const;
    VkShaderModule get_handle() const;

private:
    void create(const vk_device &dev, stage s, const char *code, size_t size);

    struct state;
    std::shared_ptr<const state> m_state;
};

class vk_viewport
{
public:
    vk_viewport(float x, float y, float width, float height, float min_depth = 0.f, float max_depth = 1.f);

    void set_scissor(float x, float y, float width, float height);
    void set_in_command_buffer(const vk_command_buffer &cmd_buf) const;

private:
    VkViewport m_handle;
    VkRect2D m_scissor;
};
