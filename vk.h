
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

    void begin();
    void end();

    VkCommandBuffer get_handle() const { return m_handle; }

private:
    VkCommandBuffer m_handle;
};

class vk_command_pool
{
public:
    vk_command_pool(const std::shared_ptr<vk_device> &device, VkCommandPool handle);

    std::shared_ptr<vk_command_buffer> create_command_buffer();

    VkCommandPool get_handle() const { return m_handle; }

private:
    std::shared_ptr<vk_device> m_device;
    VkCommandPool m_handle;
};

class vk_device : public std::enable_shared_from_this<vk_device>
{
public:
    ~vk_device();

    std::shared_ptr<vk_queue> get_queue(uint32_t family_index, uint32_t index);
    std::shared_ptr<vk_command_pool> create_command_pool(const std::weak_ptr<vk_queue> &queue);

    template<class T>
    std::shared_ptr<T> get_extension_object() {
        if (!is_extension_enabled(T::get_extension())) {
            throw vk_exception("Cannot create the requested extension object. Extension '{}' not activated.\n", T::get_extension());
        }
        return std::make_shared<T>((std::weak_ptr<vk_device>)shared_from_this());
    }
    bool is_extension_enabled(stringview extension) const;

    vk_physical_device *get_physical_device() const { return m_physical_device; }

    VkDevice get_handle() { return m_handle; }

private:
    vk_device() {}

    VkDevice m_handle;
    std::vector<std::string> m_extensions;
    vk_physical_device *m_physical_device;
    friend class vk_physical_device;
};

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
    std::shared_ptr<vk_device> create_device(uint32_t queue_family_index) {
        std::vector<std::string> extensions;
        populate_extensions<types...>(extensions);
        return do_create_device(queue_family_index, extensions);
    }

    uint32_t get_vendor_id() const { return m_props.vendorID; }
    stringview get_device_name() const { return m_props.deviceName; }
    const std::vector<vk_queue_family_properties> &get_queue_family_properties() const { return m_queue_properties; }

    VkPhysicalDevice get_handle() { return m_handle; }

private:
    void set(VkPhysicalDevice dev);
    std::shared_ptr<vk_device> do_create_device(uint32_t queue_family_index, const std::vector<std::string> &extensions);
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
    vk_surface(const std::shared_ptr<window> &window, VkSurfaceKHR surface);

    std::shared_ptr<window> get_window() const { return m_window; }
    VkSurfaceKHR get_handle() const { return m_handle; }

private:
    std::shared_ptr<window> m_window;
    VkSurfaceKHR m_handle;
};

class vk_image_view
{
public:
    vk_image_view(const std::weak_ptr<vk_device> &device, VkImageView view);
    ~vk_image_view();

    VkImageView get_handle() const { return m_handle; }

private:
    std::weak_ptr<vk_device> m_device;
    VkImageView m_handle;
};

class vk_image
{
public:
    vk_image(const std::weak_ptr<vk_device> &device, VkImage img, const VkExtent3D &extent);
    ~vk_image();

    uint32_t get_width() const { return m_extent.width; }
    uint32_t get_height() const { return m_extent.height; }
    uint32_t get_depth() const { return m_extent.depth; }

    VkImage get_handle() const { return m_handle; }

    std::shared_ptr<vk_image_view> create_image_view();

private:
    std::weak_ptr<vk_device> m_device;
    VkImage m_handle;
    VkExtent3D m_extent;
};

