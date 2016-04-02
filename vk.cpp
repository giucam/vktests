
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

vk_instance::vk_instance(vk_instance &&i)
           : m_instance(i.m_instance)
           , m_physical_devices(std::move(i.m_physical_devices))
{
    fmt::print("!!! MOVE instance !!!\n");
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


vk_command_pool::vk_command_pool(const vk_device &dev, VkCommandPool handle)
               : m_device(dev)
               , m_handle(handle)
{
}

vk_command_buffer vk_command_pool::create_command_buffer()
{
    VkCommandBuffer cmd_buf;
    VkCommandBufferAllocateInfo cmd_buf_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, //type
        nullptr, //next
        m_handle, //command pool
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, //level
        1, //cmd buffer count
    };
    VkResult res = vkAllocateCommandBuffers(m_device.get_handle(), &cmd_buf_info, &cmd_buf);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to allocate command buffer: {}\n", res);
    }
    return vk_command_buffer(cmd_buf);
}

//--

vk_device::vk_device(const vk_physical_device &phys)
         : m_physical_device(phys)
{
}

vk_device::vk_device(vk_device &&d)
         : m_handle(d.m_handle)
         , m_extensions(std::move(d.m_extensions))
         , m_physical_device(d.m_physical_device)
         , m_queue_family_index(d.m_queue_family_index)
{
}

vk_device::~vk_device()
{
    vkDestroyDevice(m_handle, nullptr);
}

vk_queue vk_device::get_queue(uint32_t index)
{
    VkQueue queue;
    vkGetDeviceQueue(m_handle, m_queue_family_index, index, &queue);
    return vk_queue(queue, m_queue_family_index, index);
}

vk_command_pool vk_device::create_command_pool()
{
    VkCommandPoolCreateInfo command_pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0,
        m_queue_family_index, //queue family index
    };
    VkCommandPool cmd_pool;
    VkResult res = vkCreateCommandPool(get_handle(), &command_pool_info, nullptr, &cmd_pool);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create command pool: {}\n", res);
    }
    return vk_command_pool(*this, cmd_pool);
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

vk_device vk_physical_device::do_create_device(uint32_t queue_family_index, const std::vector<std::string> &extension_names)
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

    auto device = vk_device(*this);
    device.m_handle = dev;
    device.m_queue_family_index = queue_family_index;
    device.m_extensions = extension_names;
    return device;
}

void vk_physical_device::set(VkPhysicalDevice dev)
{
    m_handle = dev;
    vkGetPhysicalDeviceProperties(dev, &m_props);
    vkGetPhysicalDeviceMemoryProperties(dev, &m_memprops);

    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    m_queue_properties.resize(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, (VkQueueFamilyProperties *)m_queue_properties.data());
}

uint32_t vk_physical_device::get_memory_types_count() const
{
    return m_memprops.memoryTypeCount;
}

VkMemoryType vk_physical_device::get_memory_type(uint32_t index) const
{
    return m_memprops.memoryTypes[index];
}


//--


vk_layer::vk_layer(const VkLayerProperties &props)
    : m_props(props)
{
}

//--


vk_surface::vk_surface(const vk_instance &instance, const window &window, VkSurfaceKHR surface)
          : m_instance(instance)
          , m_window(window)
          , m_handle(surface)
{
}

vk_surface::vk_surface(vk_surface &&s)
          : m_instance(s.m_instance)
          , m_window(s.m_window)
          , m_handle(s.m_handle)
{
    fmt::print("!!! MOVE surf !!!!\n");
}

vk_surface::~vk_surface()
{
    vkDestroySurfaceKHR(m_instance.get_handle(), m_handle, nullptr);
}

bool vk_surface::supports_present(vk_physical_device *device, int queue_family) const
{
    VkBool32 supports_present = false;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(device->get_handle(), queue_family, m_handle, &supports_present) == VK_SUCCESS) {
        return supports_present;
    }
    return false;
}

std::vector<VkSurfaceFormatKHR> vk_surface::get_formats(vk_physical_device *dev) const
{
    uint32_t format_count;
    VkResult res = vkGetPhysicalDeviceSurfaceFormatsKHR(dev->get_handle(), m_handle, &format_count, nullptr);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the number of surface formats: {}\n", res);
    }
    auto formats = std::vector<VkSurfaceFormatKHR>(format_count);
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(dev->get_handle(), m_handle, &format_count, formats.data());
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to retrieve the surface formats: {}\n", res);
    }
    return formats;
}


//--


vk_image_view::vk_image_view(const vk_device &device, VkImageView view)
             : m_device(device)
             , m_handle(view)
{}

vk_image_view::vk_image_view(vk_image_view &&i)
             : m_device(i.m_device)
             , m_handle(i.m_handle)
{
}

vk_image_view::~vk_image_view()
{
    vkDestroyImageView(m_device.get_handle(), m_handle, nullptr);
}


//--


vk_image::vk_image(const vk_device &device, VkImage img, const VkExtent3D &extent)
        : m_device(device)
        , m_handle(img)
        , m_extent(extent)
        , m_owns_handle(false)
{
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device.get_handle(), img, &req);
}

vk_image::vk_image(vk_image &&i)
        : m_device(i.m_device)
        , m_handle(i.m_handle)
        , m_extent(i.m_extent)
        , m_owns_handle(i.m_owns_handle)
{
}

vk_image::~vk_image()
{
    if (m_owns_handle) {
        vkDestroyImage(m_device.get_handle(), m_handle, nullptr);
    }
}

vk_image_view vk_image::create_image_view() const
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
    VkResult res = vkCreateImageView(m_device.get_handle(), &info, nullptr, &view);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create image view: {}\n", res);
    }

    return vk_image_view(m_device, view);
}


//--


vk_device_memory::vk_device_memory(const vk_device &device, property props, uint64_t size, uint32_t type_bits)
                : m_device(device)
                , m_size(size)
                , m_props(props)
{
    // if size is 0 vkAllocateMemory may fail with VK_ERROR_OUT_OF_DEVICE_MEMORY
    if (size == 0)
        size = 1;

    const VkMemoryAllocateInfo mem_alloc = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, //type
        nullptr, //next,
        size, //size
        get_mem_index(props, type_bits), //memory type index
    };
    VkResult res = vkAllocateMemory(device.get_handle(), &mem_alloc, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create vulkan device memory: {}\n", res);
    }
}

vk_device_memory::vk_device_memory(vk_device_memory &&mem)
                : m_device(mem.m_device)
                , m_handle(mem.m_handle)
                , m_size(mem.m_size)
                , m_props(std::move(mem.m_props))
{
}

vk_device_memory::~vk_device_memory()
{
    vkFreeMemory(m_device.get_handle(), m_handle, nullptr);
}

void *vk_device_memory::map(uint64_t offset)
{
    if (!(m_props & property::host_visible)) {
        throw vk_exception("Attempted to map vulkan device memory without the host_visible property.\n");
    }
    void *ptr;
    VkResult res = vkMapMemory(m_device.get_handle(), m_handle, offset, m_size - offset, 0, &ptr);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to map vulkan device memory: err {}, offset {}, size {}\n", res, offset, m_size - offset);
    }
    return ptr;
}

void vk_device_memory::unmap()
{
    vkUnmapMemory(m_device.get_handle(), m_handle);
}

uint32_t vk_device_memory::get_mem_index(property props, uint32_t type_bits)
{
    const vk_physical_device &phys = m_device.get_physical_device();

    // Search memtypes to find first index with those properties
    for (uint32_t i = 0; i < 32; i++) {
        if ((type_bits & 1) == 1) {
            // Type is available, does it match user properties?
            if ((phys.get_memory_type(i).propertyFlags & (uint32_t)props) == (uint32_t)props) {
                return i;
            }
        }
        type_bits >>= 1;
    }
    throw vk_exception("No suitable memory type found with the requested properties: {}\n", (int)props);
}


//--


vk_buffer::vk_buffer(const vk_device &device, usage u, uint64_t size, uint32_t stride)
         : m_device(device)
         , m_mem(nullptr)
         , m_stride(stride)
{
    const VkBufferCreateInfo buf_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        size, //size
        (VkBufferUsageFlags)u, //usage
        VK_SHARING_MODE_EXCLUSIVE, //sharing mode
        0, //queue family index count
        nullptr, //queue family indices
    };
    VkResult res = vkCreateBuffer(device.get_handle(), &buf_info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create vulkan buffer: {}\n", res);
    }
    vkGetBufferMemoryRequirements(device.get_handle(), m_handle, &m_mem_reqs);
}

vk_buffer::vk_buffer(vk_buffer &&buf)
         : m_device(buf.m_device)
         , m_handle(buf.m_handle)
         , m_mem_reqs(buf.m_mem_reqs)
         , m_mem(buf.m_mem)
         , m_mem_offset(buf.m_mem_offset)
         , m_stride(buf.m_stride)
{
}

vk_buffer::~vk_buffer()
{
    vkDestroyBuffer(m_device.get_handle(), m_handle, nullptr);
}

void vk_buffer::bind_memory(vk_device_memory *memory, uint64_t offset)
{
    VkResult res = vkBindBufferMemory(m_device.get_handle(), m_handle, memory->get_handle(), offset);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to bind buffer memory: {}\n", res);
    }
    m_mem = memory;
    m_mem_offset = offset;
}

void vk_buffer::map(const std::function<void (void *)> &cb)
{
    void *data = m_mem->map(m_mem_offset);
    cb(data);
    m_mem->unmap();
}


//--


struct vk_shader_module::state
{
    state(const vk_device &dev, stage s, VkShaderModule h)
        : device(dev)
        , stg(s)
        , handle(h)
    {
    }

    ~state()
    {
        vkDestroyShaderModule(device.get_handle(), handle, nullptr);
    }

    const vk_device &device;
    vk_shader_module::stage stg;
    VkShaderModule handle;
};

vk_shader_module::vk_shader_module(const vk_device &dev, stage s, const char *code, size_t size)
{
    create(dev, s, code, size);
}

vk_shader_module::vk_shader_module(const vk_device &dev, stage s, stringview file)
{
    FILE *fp = fopen(file.to_string().data(), "rb");
    if (!fp) {
        throw vk_exception("Failed to open the shader file for read: {}\n", file);
    }

    fseek(fp, 0L, SEEK_END);
    size_t size = ftell(fp);

    fseek(fp, 0L, SEEK_SET);

    char *code = new char[size];
    if (!fread(code, size, 1, fp)) {
        delete[] code;
        throw vk_exception("Failed to read the shader file: {}\n", file);
    }
    fclose(fp);

    try {
        create(dev, s, code, size);
    } catch (...) {
        delete[] code;
        throw;
    }

    delete[] code;
}

vk_shader_module::~vk_shader_module()
{
}

const vk_device &vk_shader_module::get_device() const
{
    return m_state->device;
}

vk_shader_module::stage vk_shader_module::get_stage() const
{
    return m_state->stg;
}

VkShaderModule vk_shader_module::get_handle() const
{
    return m_state->handle;
}

void vk_shader_module::create(const vk_device &dev, stage s, const char *code, size_t size)
{
    VkShaderModuleCreateInfo info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        size, //size
        (const uint32_t *)code, //code
    };
    VkShaderModule handle;
    VkResult res = vkCreateShaderModule(dev.get_handle(), &info, nullptr, &handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create shader module: {}\n", res);
    }

    m_state = std::make_shared<state>(dev, s, handle);
}


//--


vk_viewport::vk_viewport(float x, float y, float width, float height, float min_depth, float max_depth)
           : m_handle({ x, y, width, height, min_depth, max_depth })
           , m_scissor({ { (int32_t)x, (int32_t)y }, { (uint32_t)width, (uint32_t)height } })
{
}

void vk_viewport::set_scissor(float x, float y, float width, float height)
{
    m_scissor = { { (int32_t)x, (int32_t)y }, { (uint32_t)width, (uint32_t)height } };
}

void vk_viewport::set_in_command_buffer(const vk_command_buffer &cmd_buffer) const
{
    vkCmdSetViewport(cmd_buffer.get_handle(), 0, 1, &m_handle);
    vkCmdSetScissor(cmd_buffer.get_handle(), 0, 1, &m_scissor);
}


//--


vk_fence::vk_fence(const vk_device &device)
        : m_device(device)
{
    VkFenceCreateInfo fence_info = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0,
    };
    VkResult res = vkCreateFence(device.get_handle(), &fence_info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create fence: {}\n", res);
    }
}

vk_fence::~vk_fence()
{
    vkDestroyFence(m_device.get_handle(), m_handle, nullptr);
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
        CASE(VK_ERROR_INVALID_SHADER_NV)
    }
#undef CASE
    return os;
}
