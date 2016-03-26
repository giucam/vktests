
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


class vk_pipeline_layout
{
public:
    vk_pipeline_layout(const vk_device &device)
        : m_device(device)
    {
        const VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, //type
            nullptr, //next
            0, //flags
            0, //bindings count
            nullptr, //bindings
        };
        VkDescriptorSetLayout desc_layout;
        VkResult res = vkCreateDescriptorSetLayout(device.get_handle(), &descriptor_layout_info, nullptr, &desc_layout);
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to create the descriptor set layout: {}\n", res);
        }
        const VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, //type
            nullptr, //next
            0, //flags
            1, //descriptor set layout count
            &desc_layout, //descriptor set layouts
            0, //push constant range count
            nullptr, //push constant ranges
        };
        res = vkCreatePipelineLayout(device.get_handle(), &pipeline_layout_create_info, nullptr, &m_handle);
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to create pipeline layout: {}\n", res);
        }
    }

    ~vk_pipeline_layout()
    {
        vkDestroyPipelineLayout(m_device.get_handle(), m_handle, nullptr);
    }

    VkPipelineLayout get_handle() const { return m_handle; }

private:
    VkPipelineLayout m_handle;
    const vk_device &m_device;
};

class vk_renderpass
{
public:
    vk_renderpass(const vk_device &device, VkFormat format)
        : m_device(device)
    {
        VkAttachmentDescription attachment_desc[] = {
            {
                0, //flags
                format, //format
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

        VkResult res = vkCreateRenderPass(device.get_handle(), &create_info, nullptr, &m_handle);
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to create render pass: {}\n", res);
        }
    }

    ~vk_renderpass()
    {
        vkDestroyRenderPass(m_device.get_handle(), m_handle, nullptr);
    }

    VkRenderPass get_handle() const { return m_handle; }

private:
    VkRenderPass m_handle;
    const vk_device &m_device;
};

class vk_framebuffer
{
public:
    vk_framebuffer(const vk_device &device, const vk_image &img, const vk_renderpass &rpass)
        : m_device(device)
        , m_image(img)
        , m_view(m_image.create_image_view())
    {
        VkImageView view_handle = m_view.get_handle();
        VkFramebufferCreateInfo info = {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, //type
            nullptr, //next
            0, //flags
            rpass.get_handle(), //render pass
            1, //attachment count
            &view_handle, //attachments
            m_image.get_width(), //width
            m_image.get_height(), //height
            1, //layers
        };
        VkResult res = vkCreateFramebuffer(m_device.get_handle(), &info, nullptr, &m_handle);
        if (res != VK_SUCCESS) {
            throw vk_exception("Failed to create framebuffer: {}\n", res);
        }
    }

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

class vk_fence
{
public:
    explicit vk_fence(const vk_device &device)
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

    ~vk_fence()
    {
        vkDestroyFence(m_device.get_handle(), m_handle, nullptr);
    }

    VkFence get_handle() const { return m_handle; }

private:
    VkFence m_handle;
    const vk_device &m_device;
};


vk_surface create_surface(window &window, const vk_instance &instance, vk_physical_device *dev, VkSurfaceFormatKHR *format)
{
    auto surface = window.create_vk_surface(instance);

    auto formats = surface.get_formats(dev);
    *format = formats.at(0);
    print("Found {} formats, using {}\n", formats.size(), format->format);

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

    return surface;
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




int main(int argc, char **argv)
{
    auto plat = platform::xcb;
    if (argc > 1 && stringview(argv[1]) == "wl") {
        plat = platform::wayland;
    }

    auto dpy = display(plat);
    auto win = dpy.create_window(200, 200);
    win.show();

    auto layers = vk_instance::get_available_layers();
    print("Found {} available layers\n", layers.size());
    int i = 0;
    for (vk_layer &layer: layers) {
        print("{}: {} == {}\n", i++, layer.get_name(), layer.get_description());
    }

    auto vk = dpy.create_vk_instance({ VK_EXT_DEBUG_REPORT_EXTENSION_NAME });



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

    auto phys_device = vk.get_physical_devices()[0];
    VkSurfaceFormatKHR format;
    auto surface = create_surface(win, vk, &phys_device, &format);

    int family_queue_index = get_queue_family(&phys_device, surface);
    print("using queue {}\n", family_queue_index);

    auto device = phys_device.create_device<vk_swapchain_extension>(family_queue_index);
    auto queue = device.get_queue(0);

    auto swapchain_ext = device.get_extension_object<vk_swapchain_extension>();


    auto cmd_pool = device.create_command_pool();
    auto init_cmd_buf = cmd_pool.create_command_buffer();

    init_cmd_buf.begin();

    struct vertex { float p[3]; };

    auto buf = vk_vertex_buffer<vertex>(device, 3);
    print("mem size {}\n",buf.get_required_memory_size());
    auto memory = vk_device_memory(device, vk_device_memory::property::host_visible,
                                   buf.get_required_memory_size(), buf.get_required_memory_type());
    buf.bind_memory(&memory, 0);
    buf.map([](void *data) {
        static const vertex vertices[] = {
            { -1.0f, -1.0f,  0.25f, },
            {  0.0f,  1.0f,  1.0f,  },
            {  1.0f, -1.0f,  0.25f, },
        };

        memcpy(data, vertices, sizeof(vertices));
    });




    auto pipeline_layout = vk_pipeline_layout(device);

    auto render_pass = vk_renderpass(device, format.format);


    auto fence = vk_fence(device);





    auto swap_chain = swapchain_ext->create_swapchain(surface, format);
    const auto &imgs = swap_chain.get_images();
    print("{} images available\n", imgs.size());



    auto buffers = vector<vk_framebuffer>();
    buffers.reserve(imgs.size());
    for (const vk_image &img: imgs) {
        print("creating buffer {}\n",(void*)&img);
        buffers.emplace_back(device, img, render_pass);
    }


    uint32_t index = swap_chain.acquire_next_image_index();
    vk_framebuffer &framebuffer = buffers[index];




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
            none = VK_CULL_MODE_NONE,
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

        explicit vk_graphics_pipeline(const vk_device &device)
            : m_device(device)
            , m_topology(topology::triangle_list)
            , m_polygon_mode(polygon_mode::fill)
            , m_cull({ cull_mode::back, front_face::counter_clockwise })
        {
        }

        void add_stage(const vk_shader_module &shader, stringview entrypoint)
        {
            if (m_device != shader.get_device()) {
                throw vk_exception("Trying to insert a shader in a program with a different device.");
            }
            for (const shader_stage &stg: m_stages) {
                if (stg.shader.get_stage() == shader.get_stage()) {
                    throw vk_exception("Shader program stage {} already set.", (int)shader.get_stage());
                }
            }

            m_stages.emplace_back(shader, entrypoint.to_string());
        }

        void add_stage(vk_shader_module::stage s, stringview filename, stringview entrypoint)
        {
            add_stage(vk_shader_module(m_device, s, filename), entrypoint);
        }

        binding add_binding(const vk_buffer &buffer)
        {
            m_bindings.emplace_back(buffer);
            return m_bindings.size() - 1;
        }

        void add_attribute(binding b, uint32_t location, VkFormat format, uint32_t offset)
        {
            m_attributes.emplace_back(b.number, location, format, offset);
        }

        void set_primitive_mode(topology topology, bool primitive_restart_enable)
        {
            m_topology = topology;
            m_primitive_restart = primitive_restart_enable;
        }

        void set_polygon_mode(polygon_mode mode)
        {
            m_polygon_mode = mode;
        }

        void set_cull_mode(cull_mode mode, front_face front)
        {
            m_cull.mode = mode;
            m_cull.front= front;
        }

        VkPipeline get_handle() const { return m_handle; }

        void create(const vk_renderpass &render_pass, const vk_pipeline_layout &pipeline_layout)
        {
            auto shader_stages = std::vector<VkPipelineShaderStageCreateInfo>(m_stages.size());
            get_shader_info(shader_stages.data());

            auto vs_binding_desc = std::vector<VkVertexInputBindingDescription>(m_bindings.size());
            auto vs_attribute_desc = std::vector<VkVertexInputAttributeDescription>(m_attributes.size());
            VkPipelineVertexInputStateCreateInfo vertex_state_info;
            get_bindings_info(&vertex_state_info, vs_binding_desc.data(), vs_attribute_desc.data());

            VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, //type
                nullptr, //next
                0, //flags
                (VkPrimitiveTopology)m_topology, //topology
                m_primitive_restart, //primitive restart enable
            };

            VkPipelineViewportStateCreateInfo viewport_info = {
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, //type
                nullptr, //type
                0, //flags
                1, //viewport count
                nullptr, //vieports
                1, //scissor count
                nullptr, //scissors
            };

            VkPipelineRasterizationStateCreateInfo rasterization_info = {
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, //type
                nullptr, //next
                0, //flags
                false, //depth clamp enable
                false, //rasterizer discard enable
                (VkPolygonMode)m_polygon_mode, //polygon mode
                (VkCullModeFlagBits)m_cull.mode, //cull mode
                (VkFrontFace)m_cull.front, //front face
                false, //depth bias enable
                0, //depth bias constant factor
                0, //depth bias clamp
                0, //depth bias slope factor
                0, //line width
            };

            VkPipelineMultisampleStateCreateInfo multisample_info = {
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, //type
                nullptr, //next
                0, //flags
                VK_SAMPLE_COUNT_1_BIT, //rasterizationSamples is a VkSampleCountFlagBits specifying the number of samples per pixel used in rasterization.
                false,                 //sampleShadingEnable specifies that fragment shading executes per-sample if VK_TRUE, or per-fragment if VK_FALSE,
                                    //as described in Sample Shading.
                0,                     //minSampleShading is the minimum fraction of sample shading, as described in Sample Shading.
                nullptr,               //pSampleMask is a bitmask of static coverage information that is ANDed with the coverage information generated during
                                    //rasterization, as described in Sample Mask.
                0,                     //alphaToCoverageEnable controls whether a temporary coverage value is generated based on the alpha component of the
                                    //fragment’s first color output as specified in the Multisample Coverage section.
                0,                     //alphaToOneEnable controls whether the alpha component of the fragment’s first color output is replaced with one as
                                    //described in Multisample Coverage.
            };

            VkPipelineColorBlendAttachmentState colorblend_attachment_info[1] = {
                {
                    false,                // blendEnable controls whether blending is enabled for the corresponding color attachment. If blending is not enabled, the source
                                        //fragment’s color for that attachment is passed through unmodified
                    VK_BLEND_FACTOR_ZERO, //srcColorBlendFactor selects which blend factor is used to determine the source factors Sr,Sg,Sb
                    VK_BLEND_FACTOR_ZERO, //dstColorBlendFactor selects which blend factor is used to determine the destination factors Dr,Dg,Db
                    VK_BLEND_OP_ADD,      //colorBlendOp selects which blend operation is used to calculate the RGB values to write to the color attachment
                    VK_BLEND_FACTOR_ZERO, //srcAlphaBlendFactor selects which blend factor is used to determine the source factor Sa
                    VK_BLEND_FACTOR_ZERO, //dstAlphaBlendFactor selects which blend factor is used to determine the destination factor Da
                    VK_BLEND_OP_ADD,      //alphaBlendOp selects which blend operation is use to calculate the alpha values to write to the color attachment
                    0xf,                  //colorWriteMask is a bitmask selecting which of the R, G, B, and/or A components are enabled for writing, as described later in this chapter
                },
            };

            VkPipelineColorBlendStateCreateInfo colorblend_info = {
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, //Type is the type of this structure.
                nullptr,                                                  //pNext is NULL or a pointer to an extension-specific structure.
                0,                                                        //flags is reserved for future use.
                0,                                                        //logicOpEnable controls whether to apply Logical Operations.
                VK_LOGIC_OP_CLEAR,                                        //logicOp selects which logical operation to apply.
                1,                                                        //attachmentCount is the number of VkPipelineColorBlendAttachmentState elements in pAttachments.
                                                                        //This value must equal the colorAttachmentCount for the subpass in which this pipeline is used.
                colorblend_attachment_info,                               //pAttachments: pointer to array of per target attachment states
                {0,0,0,0},                                                //blendConstants is an array of four values used as the R, G, B, and A components of the blend
                                                                        //constant that are used in blending, depending on the blend factor.
            };

            VkDynamicState dynamic_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
            VkPipelineDynamicStateCreateInfo dynamicstate_info = {
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, //sType is the type of this structure
                nullptr, //pNext is NULL or a pointer to an extension-specific structure
                0, //flags is reserved for future use
                2, // dynamicStateCount is the number of elements in the pDynamicStates array
                dynamic_states, //vpDynamicStates is an array of VkDynamicState enums which indicate which pieces of pipeline state will use the values from dynamic state commands rather than from the pipeline state creation info.
            };

            VkGraphicsPipelineCreateInfo pipeline_create_info = {
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, //type
                nullptr, //next
                0, //flags
                (uint32_t)shader_stages.size(), //shader stage count
                shader_stages.data(), //shader stages
                &vertex_state_info, //vertex input state
                &input_assembly_info, //input assemby state
                nullptr, //tessellation state
                &viewport_info, //viewport state
                &rasterization_info, //rasterization state
                &multisample_info, //pMultisampleState is a pointer to an instance of the VkPipelineMultisampleStateCreateInfo, or NULL if the pipeline has rasterization disabled.
                nullptr, //pDepthStencilState is a pointer to an instance of the VkPipelineDepthStencilStateCreateInfo structure, or NULL if the pipeline has rasterization disabled or if the subpass of the render pass the pipeline is created against does not use a depth/stencil attachment
                &colorblend_info, // pColorBlendState is a pointer to an instance of the VkPipelineColorBlendStateCreateInfo structure, or NULL if the pipeline has rasterization disabled or if the subpass of the render pass the pipeline is created against does not use any color attachments
                &dynamicstate_info, //pDynamicState is a pointer to VkPipelineDynamicStateCreateInfo and is used to indicate which properties of the pipeline state object are dynamic and can be changed independently of the pipeline state. This can be NULL, which means no state in the pipeline is considered dynamic
                pipeline_layout.get_handle(), //layout is the description of binding locations used by both the pipeline and descriptor sets used with the pipeline
                render_pass.get_handle(), //renderPass is a handle to a render pass object describing the environment in which the pipeline will be used; the pipeline can be used with an instance of any render pass compatible with the one provided. See Render Pass Compatibility for more information
                0, //subpass is the index of the subpass in renderPass where this pipeline will be used
                VK_NULL_HANDLE, //basePipelineHandle is a pipeline to derive from
                0, //basePipelineIndex is an index into the pCreateInfos parameter to use as a pipeline to derive from
            };

            VkResult res = vkCreateGraphicsPipelines(m_device.get_handle(), nullptr, 1, &pipeline_create_info, nullptr, &m_handle);
            if (res != VK_SUCCESS) {
                throw vk_exception("Failed to create the graphics pipeline: {}\n", res);
            }
        }

        void bind(const vk_command_buffer &cmd_buffer)
        {
            vkCmdBindPipeline(cmd_buffer.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_handle);

            auto offsets = std::vector<VkDeviceSize>(m_bindings.size());
            auto buffers = std::vector<VkBuffer>(m_bindings.size());
            int i = 0;
            for (const auto &bind: m_bindings) {
                offsets[i] = 0;
                buffers[i] = bind.buffer.get_handle();
            }
            vkCmdBindVertexBuffers(cmd_buffer.get_handle(), 0, m_bindings.size(), buffers.data(), offsets.data());
        }

    private:
        void get_shader_info(VkPipelineShaderStageCreateInfo *info)
        {
            for (const shader_stage &stg: m_stages) {
                info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                info->pNext = nullptr;
                info->flags = 0;
                info->stage = (VkShaderStageFlagBits)stg.shader.get_stage();
                info->module = stg.shader.get_handle();
                info->pName = stg.entrypoint.data();
                info->pSpecializationInfo = nullptr;
                ++info;
            }
        }

        void get_bindings_info(VkPipelineVertexInputStateCreateInfo *info, VkVertexInputBindingDescription *binding_desc, VkVertexInputAttributeDescription *attr_desc)
        {
            info->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            info->pNext = nullptr;
            info->flags = 0;
            info->vertexBindingDescriptionCount = m_bindings.size();
            info->pVertexBindingDescriptions = binding_desc;
            info->vertexAttributeDescriptionCount = m_bindings.size();
            info->pVertexAttributeDescriptions = attr_desc;

            int i = 0;
            for (const binding_state &bind: m_bindings) {
                binding_desc->binding = i++;
                binding_desc->stride = bind.buffer.stride();
                binding_desc->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                ++binding_desc;
            }

            for (const attribute &attr: m_attributes) {
                attr_desc->location = attr.location;
                attr_desc->binding = attr.bind;
                attr_desc->format = attr.format;
                attr_desc->offset = attr.offset;
            }
        }

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
            cull_mode mode;
            front_face front;
        } m_cull;
    };

    auto pipeline = vk_graphics_pipeline(device);
    pipeline.add_stage(vk_shader_module::stage::vertex, "vert.spv", "main");
    pipeline.add_stage(vk_shader_module::stage::fragment, "frag.spv", "main");

    auto binding = pipeline.add_binding(buf);
    pipeline.add_attribute(binding, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

    pipeline.set_primitive_mode(vk_graphics_pipeline::triangle_list, false);

    pipeline.create(render_pass, pipeline_layout);











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

    vkCmdPipelineBarrier(init_cmd_buf.get_handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &image_memory_barrier);

    init_cmd_buf.end();

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


    // === RENDER ===

    auto cmd_buffer = cmd_pool.create_command_buffer();
    cmd_buffer.begin();

    VkClearValue clear_values[1];
    clear_values[0].color.float32[0] = 1.0f;
    clear_values[0].color.float32[1] = 0.2f;
    clear_values[0].color.float32[2] = 0.2f;
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

    print("pass {} fb {}\n",(void*)render_pass.get_handle(),(void*)framebuffer.get_handle());

    vkCmdBeginRenderPass(cmd_buffer.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    pipeline.bind(cmd_buffer);

//     vkCmdBindDescriptorSets(cmd_buffer.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &desc_layout, 0, nullptr);
    VkViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.height = (float)framebuffer.get_width();
    viewport.width = (float)framebuffer.get_height();
    viewport.minDepth = (float)0.0f;
    viewport.maxDepth = (float)1.0f;
    vkCmdSetViewport(cmd_buffer.get_handle(), 0, 1, &viewport);

    VkRect2D scissor;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent.width = framebuffer.get_width();
    scissor.extent.height = framebuffer.get_height();
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    vkCmdSetScissor(cmd_buffer.get_handle(), 0, 1, &scissor);



    vkCmdDraw(cmd_buffer.get_handle(), 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd_buffer.get_handle());


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
    res = vkQueueSubmit(queue.get_handle(), 1, &submit_draw_info, VK_NULL_HANDLE);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to submit queue: {}\n", res);
    }

    // == SWAP ==


//     vc->model.render(vc, &vc->buffers[index]);
//

    VkSwapchainKHR swapchain_raw = swap_chain.get_handle();
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
    vkQueuePresentKHR(queue.get_handle(), &present_info);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to present queue: {}\n", res);
    }

sleep(10);
    return 0;
}
