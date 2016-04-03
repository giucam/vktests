
#include "vk_pipeline.h"


vk_descriptor_set::vk_descriptor_set(const vk_device &device, VkDescriptorSet handle)
                 : m_device(device)
                 , m_handle(handle)
{
}


//--


vk_descriptor_set_layout::vk_descriptor_set_layout(const vk_device &device, const std::vector<binding> &bindings)
{
    auto layout_bindings = std::vector<VkDescriptorSetLayoutBinding>();
    layout_bindings.reserve(bindings.size());
    for (const auto &b: bindings) {
        layout_bindings.push_back({ b.binding_id,
                                    (VkDescriptorType)b.type,
                                    b.descriptor_count,
                                    (VkShaderStageFlagBits)b.shader_stages,
                                    nullptr, //TODO FIXME immutable samplers
                                    });
    }
    const VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        (uint32_t)layout_bindings.size(), //bindings count
        layout_bindings.data(), //bindings
    };
    VkResult res = vkCreateDescriptorSetLayout(device.get_handle(), &descriptor_layout_info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create the descriptor set layout: {}\n", res);
    }
}


//--


vk_descriptor_pool::vk_descriptor_pool(const vk_device &device, const std::vector<std::pair<vk_descriptor::type, uint32_t>> &sizes)
                  : m_device(device)
{
    uint32_t max = 0;
    auto descpool_sizes = std::vector<VkDescriptorPoolSize>(sizes.size());
    for (const auto &s: sizes) {
        descpool_sizes.push_back({ (VkDescriptorType)s.first, s.second });
        max += s.second;
    }

    const VkDescriptorPoolCreateInfo descriptor_pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        max, //max sets
        (uint32_t)descpool_sizes.size(), //poolsizecount
        descpool_sizes.data(), //pool sizes
    };

    VkResult res = vkCreateDescriptorPool(device.get_handle(), &descriptor_pool_info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create descriptor pool: {}\n", res);
    }
}

vk_descriptor_pool::~vk_descriptor_pool()
{
    vkDestroyDescriptorPool(m_device.get_handle(), m_handle, nullptr);
}

vk_descriptor_set vk_descriptor_pool::allocate_descriptor_set(const vk_descriptor_set_layout &descset_layout)
{
    VkDescriptorSetLayout descset_layouts[] = {
        descset_layout.get_handle(),
    };
    VkDescriptorSetAllocateInfo descset_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, //type
        nullptr, //next
        m_handle, //descriptor pool
        1, //descriptor set count
        descset_layouts, //descriptor set layouts
    };
    VkDescriptorSet descset;
    VkResult res = vkAllocateDescriptorSets(m_device.get_handle(), &descset_info, &descset);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to allocate descriptor set: {}\n", res);
    }

    return vk_descriptor_set(m_device, descset);
}


//--


vk_pipeline_layout::vk_pipeline_layout(const vk_device &device, const vk_descriptor_set_layout &descset_layout)
                  : m_device(device)
{
    VkDescriptorSetLayout descset_layouts[] = {
        descset_layout.get_handle(),
    };
    const VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        1, //descriptor set layout count
        descset_layouts, //descriptor set layouts
        0, //push constant range count
        nullptr, //push constant ranges
    };
    VkResult res = vkCreatePipelineLayout(device.get_handle(), &pipeline_layout_create_info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create pipeline layout: {}\n", res);
    }
}

vk_pipeline_layout::~vk_pipeline_layout()
{
    vkDestroyPipelineLayout(m_device.get_handle(), m_handle, nullptr);
}


//--


vk_renderpass::vk_renderpass(const vk_device &device, VkFormat format, VkFormat depth_format)
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
        },
        {
            0, //flags
            depth_format, //format
            VK_SAMPLE_COUNT_1_BIT, //samples
            VK_ATTACHMENT_LOAD_OP_CLEAR, //load op
            VK_ATTACHMENT_STORE_OP_DONT_CARE, //store op
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, //stencil load op
            VK_ATTACHMENT_STORE_OP_DONT_CARE, //stencil store op
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, //initial layout
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, //final layout
        },
    };
    VkAttachmentReference color_attachments[] = {
        { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, },
    };
//     VkAttachmentReference resolve_attachments[] = {
//         { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, },
//     };
    const VkAttachmentReference depth_reference = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
//     const uint32_t preserve_attachments[] = { 0 };
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
        sizeof(attachment_desc) / sizeof(VkAttachmentDescription), attachment_desc,
        1, subpass_desc,
        0, nullptr,
    };

    VkResult res = vkCreateRenderPass(device.get_handle(), &create_info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create render pass: {}\n", res);
    }
}

vk_renderpass::~vk_renderpass()
{
    vkDestroyRenderPass(m_device.get_handle(), m_handle, nullptr);
}


//--


vk_graphics_pipeline::vk_graphics_pipeline(const vk_device &device)
                    : m_device(device)
                    , m_topology(topology::triangle_list)
                    , m_primitive_restart(false)
                    , m_polygon_mode(polygon_mode::fill)
                    , m_cull({ VK_CULL_MODE_NONE, front_face::counter_clockwise })
                    , m_blending({ false })
{
}

void vk_graphics_pipeline::add_stage(const vk_shader_module &shader, stringview entrypoint)
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

void vk_graphics_pipeline::add_stage(vk_shader_module::stage s, stringview filename, stringview entrypoint)
{
    add_stage(vk_shader_module(m_device, s, filename), entrypoint);
}

vk_graphics_pipeline::binding vk_graphics_pipeline::add_binding(const vk_buffer &buffer)
{
    m_bindings.emplace_back(buffer);
    return m_bindings.size() - 1;
}

void vk_graphics_pipeline::add_attribute(binding b, uint32_t location, VkFormat format, uint32_t offset)
{
    m_attributes.emplace_back(b.number, location, format, offset);
}

void vk_graphics_pipeline::set_primitive_mode(topology topology, bool primitive_restart_enable)
{
    m_topology = topology;
    m_primitive_restart = primitive_restart_enable;
}

void vk_graphics_pipeline::set_polygon_mode(polygon_mode mode)
{
    m_polygon_mode = mode;
}

void vk_graphics_pipeline::enable_culling(cull_mode mode, front_face front)
{
    m_cull.mode = (VkCullModeFlagBits)mode;
    m_cull.front = front;
}

void vk_graphics_pipeline::disable_culling()
{
    m_cull.mode = VK_CULL_MODE_NONE;
}

void vk_graphics_pipeline::set_blending(bool enabled)
{
    m_blending.enabled = enabled;
}

void vk_graphics_pipeline::create(const vk_renderpass &render_pass, const vk_pipeline_layout &pipeline_layout)
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
        m_cull.mode, //cull mode
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

    VkPipelineDepthStencilStateCreateInfo depthstencil_info = {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        true, //depthTestEnable
        true, //depthWriteEnable
        VK_COMPARE_OP_LESS_OR_EQUAL, //depthCompareOp
        false, //depthBoundsTestEnable
        false, //stencilTestEnable
        { //back
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = VK_STENCIL_OP_KEEP,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .compareMask = 0,
            .writeMask = 0,
            .reference = 0,
        },
        { //front
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = VK_STENCIL_OP_KEEP,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .compareMask = 0,
            .writeMask = 0,
            .reference = 0,
        },
        0.f, //minDepthBounds
        0.f, //maxDepthBounds
    };

    VkPipelineColorBlendAttachmentState colorblend_attachment_info[1] = {
        {
            m_blending.enabled, // blendEnable controls whether blending is enabled for the corresponding color attachment. If blending is not enabled, the source
                                //fragment’s color for that attachment is passed through unmodified
            VK_BLEND_FACTOR_SRC_ALPHA, //srcColorBlendFactor selects which blend factor is used to determine the source factors Sr,Sg,Sb
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, //dstColorBlendFactor selects which blend factor is used to determine the destination factors Dr,Dg,Db
            VK_BLEND_OP_ADD,      //colorBlendOp selects which blend operation is used to calculate the RGB values to write to the color attachment
            VK_BLEND_FACTOR_ZERO, //srcAlphaBlendFactor selects which blend factor is used to determine the source factor Sa
            VK_BLEND_FACTOR_ONE, //dstAlphaBlendFactor selects which blend factor is used to determine the destination factor Da
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
        &depthstencil_info, //pDepthStencilState is a pointer to an instance of the VkPipelineDepthStencilStateCreateInfo structure, or NULL if the pipeline has rasterization disabled or if the subpass of the render pass the pipeline is created against does not use a depth/stencil attachment
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

void vk_graphics_pipeline::set_in_command_buffer(const vk_command_buffer &cmd_buffer) const
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

void vk_graphics_pipeline::get_shader_info(VkPipelineShaderStageCreateInfo *info)
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

void vk_graphics_pipeline::get_bindings_info(VkPipelineVertexInputStateCreateInfo *info, VkVertexInputBindingDescription *binding_desc, VkVertexInputAttributeDescription *attr_desc)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    info->pNext = nullptr;
    info->flags = 0;
    info->vertexBindingDescriptionCount = m_bindings.size();
    info->pVertexBindingDescriptions = binding_desc;
    info->vertexAttributeDescriptionCount = m_attributes.size();
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
        ++attr_desc;
    }
}


//--


vk_framebuffer::vk_framebuffer(const vk_device &device, const vk_image &img, const vk_image_view &depth, const vk_renderpass &rpass)
              : m_device(device)
              , m_image(img)
              , m_view(m_image.create_image_view(vk_image::aspect::color))
{
    VkImageView view_handles[] = { m_view.get_handle(), depth.get_handle() };
    VkFramebufferCreateInfo info = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, //type
        nullptr, //next
        0, //flags
        rpass.get_handle(), //render pass
        sizeof(view_handles) / sizeof(VkImageView), //attachment count
        view_handles, //attachments
        m_image.get_width(), //width
        m_image.get_height(), //height
        1, //layers
    };
    VkResult res = vkCreateFramebuffer(m_device.get_handle(), &info, nullptr, &m_handle);
    if (res != VK_SUCCESS) {
        throw vk_exception("Failed to create framebuffer: {}\n", res);
    }
}
