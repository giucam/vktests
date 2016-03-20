
#include "vk.h"

using std::string;
using std::weak_ptr;
using std::shared_ptr;
using std::unique_ptr;
using std::make_unique;
using std::make_shared;
using std::vector;
using fmt::print;


vk_instance::vk_instance(const vector<string> &layer_names, const vector<string> &extension_names)
{
    vector<const char *> layers(layer_names.size());
    for (size_t i = 0; i < layer_names.size(); ++i) {
        layers[i] = layer_names[i].data();
    }

    vector<const char *> extensions(extension_names.size());
    for (size_t i = 0; i < extension_names.size(); ++i) {
        extensions[i] = extension_names[i].data();
    }

    VkApplicationInfo app_info = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
        "vktest", 0,
        "vkengine", 0,
        VK_MAKE_VERSION(1, 0, 3),
    };
    VkInstanceCreateInfo info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0,
        &app_info,
        (uint32_t)layers.size(), layers.data(),
        (uint32_t)extensions.size(), extensions.data(),
    };
    VkResult res = vkCreateInstance(&info, nullptr, &m_instance);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create Vulkan instance: {}", res);
    }

    uint32_t count;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    auto devices = vector<VkPhysicalDevice>(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    m_physical_devices.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        m_physical_devices.at(i).set(devices.at(i));
    }

    print("Found {} physical devices:\n", count);
    int i = 0;
    for (vk_physical_device &dev: m_physical_devices) {
        print("{}: vendor id {}, device name {}\n", i, dev.get_vendor_id(), dev.get_device_name());
    }
}

vk_instance::~vk_instance()
{
    vkDestroyInstance(m_instance, nullptr);
}

vector<vk_layer> vk_instance::get_available_layers()
{
    uint32_t count;
    VkResult result = vkEnumerateInstanceLayerProperties(&count, nullptr);
    if (result != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the number of layers: {}\n", result);
    }
    auto properties = vector<VkLayerProperties>(count);
    result = vkEnumerateInstanceLayerProperties(&count, properties.data());
    if (result != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the layer properties: {}\n", result);
    }

    auto layers = vector<vk_layer>();
    layers.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        layers.push_back(vk_layer(properties[i]));
    }
    return layers;
}


//--

vk_queue::vk_queue(VkQueue queue, uint32_t family_index, uint32_t index)
        : m_handle(queue)
        , m_family_index(family_index)
        , m_index(index)
{
}


//--


vk_command_buffer::vk_command_buffer(VkCommandBuffer buf)
                 : m_handle(buf)
{
}

void vk_command_buffer::begin()
{
    VkCommandBufferBeginInfo info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, //type
        nullptr, //next
        0, //flags
        nullptr, //inheritance info
    };
    VkResult res = vkBeginCommandBuffer(m_handle, &info);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to bagin command buffer: {}\n", res);
    }
}

void vk_command_buffer::end()
{
    vkEndCommandBuffer(m_handle);
}


//--


vk_command_pool::vk_command_pool(const std::shared_ptr<vk_device> &dev, VkCommandPool handle)
               : m_device(dev)
               , m_handle(handle)
{
}

std::shared_ptr<vk_command_buffer> vk_command_pool::create_command_buffer()
{
    VkCommandBuffer cmd_buf;
    VkCommandBufferAllocateInfo cmd_buf_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, //type
        nullptr, //next
        m_handle, //command pool
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, //level
        1, //cmd buffer count
    };
    VkResult res = vkAllocateCommandBuffers(m_device->get_handle(), &cmd_buf_info, &cmd_buf);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to allocate command buffer: {}\n", res);
    }
    return std::make_shared<vk_command_buffer>(cmd_buf);
}

//--


vk_device::~vk_device()
{
    vkDestroyDevice(m_handle, nullptr);
}

shared_ptr<vk_queue> vk_device::get_queue(uint32_t family_index, uint32_t index)
{
    VkQueue queue;
    vkGetDeviceQueue(m_handle, family_index, index, &queue);
    return make_shared<vk_queue>(queue, family_index, index);
}

std::shared_ptr<vk_command_pool> vk_device::create_command_pool(const std::weak_ptr<vk_queue> &queue)
{
    VkCommandPoolCreateInfo command_pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0,
        queue.lock()->get_family_index(), //queue family index
    };
    VkCommandPool cmd_pool;
    VkResult res = vkCreateCommandPool(get_handle(), &command_pool_info, nullptr, &cmd_pool);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create command pool: {}\n", res);
    }
    return make_shared<vk_command_pool>(shared_from_this(), cmd_pool);
}

bool vk_device::is_extension_enabled(stringview extension) const
{
    for (const string &ext: m_extensions) {
        if (extension == ext) {
            return true;
        }
    }
    return false;
}


//--

bool vk_queue_family_properties::is_graphics_capable() const
{
    return m_handle.queueFlags & VK_QUEUE_GRAPHICS_BIT;
}

bool vk_queue_family_properties::is_compute_capable() const
{
    return m_handle.queueFlags & VK_QUEUE_COMPUTE_BIT;
}

bool vk_queue_family_properties::is_transfer_capable() const
{
    return m_handle.queueFlags & VK_QUEUE_TRANSFER_BIT;
}

uint32_t vk_queue_family_properties::queue_count() const
{
    return m_handle.queueCount;
}

//--

vk_physical_device::vk_physical_device()
{
}

shared_ptr<vk_device> vk_physical_device::do_create_device(uint32_t queue_family_index, const std::vector<std::string> &extension_names)
{
    float queue_priorities[1] = { 0.0 };
    VkDeviceQueueCreateInfo queue_info = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        (uint32_t)queue_family_index, //queue family index
        1, //queue count
        queue_priorities, //queue properties
    };
    vector<const char *> layers = {  };
    vector<const char *> extensions(extension_names.size());
    for (size_t i = 0; i < extension_names.size(); ++i) {
        extensions[i] = extension_names[i].data();
    }

    VkDeviceCreateInfo dev_info = {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        1, //queue create info count
        &queue_info, //queue create info
        (uint32_t)layers.size(), //layers count
        layers.data(), //layers names
        (uint32_t)extensions.size(), //extensions count
        extensions.data(), //extensions names
        nullptr, //enabled features
    };

    VkDevice dev;
    VkResult res = vkCreateDevice(m_handle, &dev_info, nullptr, &dev);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create a Vulkan device: {}\n", res);
    }

    class actual_device : public vk_device {};
    auto device = make_shared<actual_device>();
    device->m_handle = dev;
    device->m_physical_device = this;
    static_cast<vk_device *>(device.get())->m_extensions = extension_names;
    return device;
}

void vk_physical_device::set(VkPhysicalDevice dev)
{
    m_handle = dev;
    vkGetPhysicalDeviceProperties(dev, &m_props);

    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    m_queue_properties.resize(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, (VkQueueFamilyProperties *)m_queue_properties.data());
}


//--


vk_layer::vk_layer(const VkLayerProperties &props)
    : m_props(props)
{
}

//--


vk_surface::vk_surface(const std::shared_ptr<window> &window, VkSurfaceKHR surface)
          : m_window(window)
          , m_handle(surface)
{
}


//--


vk_image_view::vk_image_view(const std::weak_ptr<vk_device> &device, VkImageView view)
             : m_device(device)
             , m_handle(view)
{}

vk_image_view::~vk_image_view()
{
    vkDestroyImageView(m_device.lock()->get_handle(), m_handle, nullptr);
}


//--


vk_image::vk_image(const std::weak_ptr<vk_device> &device, VkImage img, const VkExtent3D &extent)
        : m_device(device)
        , m_handle(img)
        , m_extent(extent)
{
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device.lock()->get_handle(), img, &req);
}

vk_image::~vk_image()
{
    vkDestroyImage(m_device.lock()->get_handle(), m_handle, nullptr);
}


std::shared_ptr<vk_image_view> vk_image::create_image_view()
{
    VkImageView view;
    VkImageViewCreateInfo info = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        m_handle, //image
        VK_IMAGE_VIEW_TYPE_2D, //view type
        VK_FORMAT_B8G8R8A8_SRGB, //format
        { //components
            VK_COMPONENT_SWIZZLE_R, //r
            VK_COMPONENT_SWIZZLE_G, //g
            VK_COMPONENT_SWIZZLE_B, //b
            VK_COMPONENT_SWIZZLE_A, //a
        },
        { //sub resource range
            VK_IMAGE_ASPECT_COLOR_BIT, //aspect mask
            0, //base mip level
            0, //level count
            0, //base array layer
            1, //layer count
        },
    };
    VkResult res = vkCreateImageView(m_device.lock()->get_handle(), &info, nullptr, &view);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create image view: {}\n", res);
    }

    return std::make_shared<vk_image_view>(m_device, view);
}


//--


std::ostream &operator<<(std::ostream &os, VkResult v)
{
#define CASE(err) case err: os << #err; break;
    switch (v) {
        CASE(VK_SUCCESS)
        CASE(VK_NOT_READY)
        CASE(VK_TIMEOUT)
        CASE(VK_EVENT_SET)
        CASE(VK_EVENT_RESET)
        CASE(VK_INCOMPLETE)

        CASE(VK_ERROR_OUT_OF_HOST_MEMORY)
        CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
        CASE(VK_ERROR_INITIALIZATION_FAILED)
        CASE(VK_ERROR_DEVICE_LOST)
        CASE(VK_ERROR_MEMORY_MAP_FAILED)
        CASE(VK_ERROR_LAYER_NOT_PRESENT)
        CASE(VK_ERROR_EXTENSION_NOT_PRESENT)
        CASE(VK_ERROR_FEATURE_NOT_PRESENT)
        CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
        CASE(VK_ERROR_TOO_MANY_OBJECTS)
        CASE(VK_ERROR_FORMAT_NOT_SUPPORTED)

        CASE(VK_RESULT_MAX_ENUM)
        CASE(VK_RESULT_RANGE_SIZE)

        CASE(VK_ERROR_VALIDATION_FAILED_EXT)
        CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
        CASE(VK_ERROR_OUT_OF_DATE_KHR)
        CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
        CASE(VK_ERROR_SURFACE_LOST_KHR)
        CASE(VK_SUBOPTIMAL_KHR)
    }
#undef CASE
    return os;
}
