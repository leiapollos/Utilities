//
// Created by André Leite on 03/11/2025.
//

struct GradientPushConstants {
    Vec4F32 tileBorderColor;
};

struct DxcThreadState {
    IDxcCompiler3* compiler;
    IDxcUtils* utils;
    B32 initialized;
};

thread_local DxcThreadState g_tlsDxcState = {0, 0, 0};

static B32 vulkan_get_dxc_instances(IDxcCompiler3** outCompiler, IDxcUtils** outUtils) {
    if (!outCompiler || !outUtils) {
        return 0;
    }

    if (!g_tlsDxcState.initialized) {
        HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&g_tlsDxcState.compiler));
        if (FAILED(hr) || !g_tlsDxcState.compiler) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create DXC compiler instance (thread {}): {}",
                      OS_get_thread_id_u32(), hr);
            return 0;
        }

        hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&g_tlsDxcState.utils));
        if (FAILED(hr) || !g_tlsDxcState.utils) {
            if (g_tlsDxcState.compiler) {
                g_tlsDxcState.compiler->Release();
                g_tlsDxcState.compiler = 0;
            }
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create DXC utils instance (thread {}): {}",
                      OS_get_thread_id_u32(), hr);
            return 0;
        }

        g_tlsDxcState.initialized = 1;
    }

    *outCompiler = g_tlsDxcState.compiler;
    *outUtils = g_tlsDxcState.utils;
    return 1;
}

// ////////////////////////
// Shader Module Loading

static B32 vulkan_load_shader_module(VulkanDevice* device, const char* filePath, VkShaderModule* outModule) {
    if (!device || !filePath || !outModule || device->device == VK_NULL_HANDLE) {
        return 0;
    }

    *outModule = VK_NULL_HANDLE;

    OS_Handle shaderFile = OS_file_open(filePath, OS_FileOpenMode_Read);
    if (!shaderFile.handle) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to open shader file '{}'", filePath);
        return 0;
    }

    OS_FileMapping mapping = OS_file_map_ro(shaderFile);
    OS_file_close(shaderFile);

    if (!mapping.ptr || mapping.length == 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to map shader file '{}'", filePath);
        if (mapping.ptr) {
            OS_file_unmap(mapping);
        }
        return 0;
    }

    if ((mapping.length % sizeof(U32)) != 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Shader file '{}' size ({}) is not a multiple of 4 bytes", filePath,
                  mapping.length);
        OS_file_unmap(mapping);
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));

    U64 wordCount = mapping.length / sizeof(U32);
    U32* code = ARENA_PUSH_ARRAY(scratch.arena, U32, wordCount);
    if (!code) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate temporary buffer for shader '{}'", filePath);
        OS_file_unmap(mapping);
        return 0;
    }

    MEMMOVE(code, mapping.ptr, mapping.length);
    OS_file_unmap(mapping);

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = 0;
    createInfo.flags = 0;
    createInfo.codeSize = mapping.length;
    createInfo.pCode = code;

    VkResult result = vkCreateShaderModule(device->device, &createInfo, 0, outModule);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "vkCreateShaderModule failed for '{}': {}", filePath, result);
        *outModule = VK_NULL_HANDLE;
        return 0;
    }

    return 1;
}

// ////////////////////////
// Pipeline Builder

struct PipelineBuilder {
    // Dynamic Rendering Info
    VkPipelineRenderingCreateInfo renderingInfo;
    VkFormat colorAttachmentFormats[8];
    
    // Shader Stages
    VkPipelineShaderStageCreateInfo shaderStages[8];
    U32 shaderStageCount;
    
    // State
    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineLayout pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
};

static void pipeline_builder_clear(PipelineBuilder* builder) {
    MEMSET(builder, 0, sizeof(PipelineBuilder));
}

static void pipeline_builder_set_shaders(PipelineBuilder* builder, VkShaderModule vertShader, VkShaderModule fragShader) {
    builder->shaderStageCount = 0;
    
    if (vertShader != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo* stage = &builder->shaderStages[builder->shaderStageCount++];
        stage->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage->stage = VK_SHADER_STAGE_VERTEX_BIT;
        stage->module = vertShader;
        stage->pName = "main";
    }
    
    if (fragShader != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo* stage = &builder->shaderStages[builder->shaderStageCount++];
        stage->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stage->module = fragShader;
        stage->pName = "main";
    }
}

static void pipeline_builder_set_input_topology(PipelineBuilder* builder, VkPrimitiveTopology topology) {
    builder->inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    builder->inputAssembly.topology = topology;
    builder->inputAssembly.primitiveRestartEnable = VK_FALSE;
}

static void pipeline_builder_set_polygon_mode(PipelineBuilder* builder, VkPolygonMode mode) {
    builder->rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    builder->rasterizer.polygonMode = mode;
    builder->rasterizer.lineWidth = 1.0f;
}

static void pipeline_builder_set_cull_mode(PipelineBuilder* builder, VkCullModeFlags cullMode, VkFrontFace frontFace) {
    builder->rasterizer.cullMode = cullMode;
    builder->rasterizer.frontFace = frontFace;
}

static void pipeline_builder_set_multisampling_none(PipelineBuilder* builder) {
    builder->multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    builder->multisampling.sampleShadingEnable = VK_FALSE;
    builder->multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    builder->multisampling.minSampleShading = 1.0f;
    builder->multisampling.pSampleMask = 0;
    builder->multisampling.alphaToCoverageEnable = VK_FALSE;
    builder->multisampling.alphaToOneEnable = VK_FALSE;
}

static void pipeline_builder_disable_blending(PipelineBuilder* builder) {
    builder->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    builder->colorBlendAttachment.blendEnable = VK_FALSE;
}

static void pipeline_builder_enable_blending_additive(PipelineBuilder* builder) {
    builder->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    builder->colorBlendAttachment.blendEnable = VK_TRUE;
    builder->colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    builder->colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    builder->colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    builder->colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    builder->colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    builder->colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

static void pipeline_builder_enable_blending_alphablend(PipelineBuilder* builder) {
    builder->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    builder->colorBlendAttachment.blendEnable = VK_TRUE;
    builder->colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    builder->colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    builder->colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    builder->colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    builder->colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    builder->colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

static void pipeline_builder_set_color_attachment_format(PipelineBuilder* builder, VkFormat format) {
    builder->colorAttachmentFormats[0] = format;
    builder->renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    builder->renderingInfo.colorAttachmentCount = 1;
    builder->renderingInfo.pColorAttachmentFormats = builder->colorAttachmentFormats;
}

static void pipeline_builder_set_depth_format(PipelineBuilder* builder, VkFormat format) {
    builder->renderingInfo.depthAttachmentFormat = format;
}

static void pipeline_builder_enable_depth_test(PipelineBuilder* builder, B32 depthWriteEnable, VkCompareOp op) {
    builder->depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    builder->depthStencil.depthTestEnable = VK_TRUE;
    builder->depthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
    builder->depthStencil.depthCompareOp = op;
    builder->depthStencil.depthBoundsTestEnable = VK_FALSE;
    builder->depthStencil.stencilTestEnable = VK_FALSE;
    builder->depthStencil.minDepthBounds = 0.0f;
    builder->depthStencil.maxDepthBounds = 1.0f;
}

static void pipeline_builder_disable_depth_test(PipelineBuilder* builder) {
    builder->depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    builder->depthStencil.depthTestEnable = VK_FALSE;
    builder->depthStencil.depthWriteEnable = VK_FALSE;
    builder->depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
    builder->depthStencil.depthBoundsTestEnable = VK_FALSE;
    builder->depthStencil.stencilTestEnable = VK_FALSE;
}

static VkPipeline pipeline_builder_build(PipelineBuilder* builder, VkDevice device) {
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &builder->colorBlendAttachment;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicInfo = {};
    dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicInfo.dynamicStateCount = 2;
    dynamicInfo.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &builder->renderingInfo;
    pipelineInfo.stageCount = builder->shaderStageCount;
    pipelineInfo.pStages = builder->shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &builder->inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &builder->rasterizer;
    pipelineInfo.pMultisampleState = &builder->multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &builder->depthStencil;
    pipelineInfo.pDynamicState = &dynamicInfo;
    pipelineInfo.layout = builder->pipelineLayout;

    VkPipeline newPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, 0, &newPipeline);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create graphics pipeline: {}", result);
        return VK_NULL_HANDLE;
    }
    return newPipeline;
}

static B32 vulkan_init_draw_pipeline(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE) {
        return 0;
    }

    if (vulkan->pipelines.gradientPipeline != VK_NULL_HANDLE) {
        return 1;
    }

    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shaderModule = VK_NULL_HANDLE;

    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0u;
    binding.descriptorCount = 1u;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binding.pImmutableSamplers = 0;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {};
    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 1u;
    descriptorLayoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(vulkan->device.device, &descriptorLayoutInfo, 0, &descriptorLayout) != VK_SUCCESS) {
        goto cleanup_fail;
    }

    {
        VkDescriptorType types[] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        F32 ratios[] = { 1.0f };
        if (!vulkan_create_descriptor_pool_(vulkan->device.device, types, ratios, 1, 10, &descriptorPool)) {
            goto cleanup_fail;
        }
    }

    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1u;
        allocInfo.pSetLayouts = &descriptorLayout;

        if (vkAllocateDescriptorSets(vulkan->device.device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
            goto cleanup_fail;
        }
    }

    {
        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0u;
        pushConstantRange.size = sizeof(GradientPushConstants);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1u;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1u;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(vulkan->device.device, &pipelineLayoutInfo, 0, &pipelineLayout) != VK_SUCCESS) {
            goto cleanup_fail;
        }
    }

    if (!vulkan_load_shader_module(&vulkan->device, "shaders/gradient.comp.spv", &shaderModule)) {
        goto cleanup_fail;
    }

    {
        VkPipelineShaderStageCreateInfo stageInfo = {};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = 0;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.stage = stageInfo;
        
        if (vkCreateComputePipelines(vulkan->device.device, VK_NULL_HANDLE, 1u, &pipelineInfo, 0, &pipeline) != VK_SUCCESS) {
            goto cleanup_fail;
        }
    }

    vkDestroyShaderModule(vulkan->device.device, shaderModule, 0);

    vulkan->pipelines.drawImageDescriptorLayout = descriptorLayout;
    vulkan->pipelines.gradientPipelineLayout = pipelineLayout;
    vulkan->pipelines.gradientPipeline = pipeline;
    vulkan->drawImageDescriptorPool = descriptorPool;
    vulkan->drawImageDescriptorSet = descriptorSet;

    // Defer cleanup
    vkdefer_destroy_VkDescriptorSetLayout(&vulkan->deferCtx.globalBuf, descriptorLayout);
    vkdefer_destroy_VkDescriptorPool(&vulkan->deferCtx.globalBuf, descriptorPool);
    vkdefer_free_descriptor_set(&vulkan->deferCtx.globalBuf, descriptorPool, descriptorSet);
    vkdefer_destroy_VkPipelineLayout(&vulkan->deferCtx.globalBuf, pipelineLayout);
    vkdefer_destroy_VkPipeline(&vulkan->deferCtx.globalBuf, pipeline);

    return 1;

cleanup_fail:
    if (shaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan->device.device, shaderModule, 0);
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan->device.device, pipeline, 0);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan->device.device, pipelineLayout, 0);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan->device.device, descriptorPool, 0);
    }
    if (descriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan->device.device, descriptorLayout, 0);
    }
    return 0;
}

static B32 vulkan_update_draw_pipeline(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE) {
        return 0;
    }

    if (vulkan->drawImageDescriptorSet == VK_NULL_HANDLE || vulkan->drawImage.imageView == VK_NULL_HANDLE) {
        return 0;
    }
    
    VulkanDescriptorWriter writer = {};
    vulkan_descriptor_writer_write_image(&writer, 0, vulkan->drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    vulkan_descriptor_writer_update_set(&vulkan->device, &writer, vulkan->drawImageDescriptorSet);

    return 1;
}

static void vulkan_dispatch_gradient(RendererVulkan* vulkan, VkCommandBuffer cmd, Vec4F32 color) {
    if (!vulkan || cmd == VK_NULL_HANDLE) return;
    
    if (vulkan->pipelines.gradientPipeline == VK_NULL_HANDLE) return;
    
    if (vulkan->drawExtent.width == 0u || vulkan->drawExtent.height == 0u) return;

    U32 groupCountX = (vulkan->drawExtent.width + 15u) / 16u;
    U32 groupCountY = (vulkan->drawExtent.height + 15u) / 16u;

    if (groupCountX == 0u || groupCountY == 0u) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan->pipelines.gradientPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan->pipelines.gradientPipelineLayout, 0u, 1u, &vulkan->drawImageDescriptorSet, 0u, 0);

    GradientPushConstants pushConstants = {};
    pushConstants.tileBorderColor = color;

    vkCmdPushConstants(cmd, vulkan->pipelines.gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(pushConstants), &pushConstants);
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1u);
}

// ////////////////////////
// Mesh Pipeline

static B32 vulkan_init_mesh_pipeline(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE) {
        return 0;
    }

    if (vulkan->pipelines.meshPipeline != VK_NULL_HANDLE) {
        return 1;
    }

    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    if (!vulkan_load_shader_module(&vulkan->device, "shaders/mesh.vert.spv", &vertShader)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to load mesh vertex shader");
        goto cleanup_fail;
    }

    if (!vulkan_load_shader_module(&vulkan->device, "shaders/mesh.frag.spv", &fragShader)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to load mesh fragment shader");
        goto cleanup_fail;
    }

    {
        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(GPUDrawPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(vulkan->device.device, &layoutInfo, 0, &pipelineLayout) != VK_SUCCESS) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create mesh pipeline layout");
            goto cleanup_fail;
        }
    }

    {
        PipelineBuilder builder;
        pipeline_builder_clear(&builder);
        pipeline_builder_set_shaders(&builder, vertShader, fragShader);
        pipeline_builder_set_input_topology(&builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pipeline_builder_set_polygon_mode(&builder, VK_POLYGON_MODE_FILL);
        pipeline_builder_set_cull_mode(&builder, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pipeline_builder_set_multisampling_none(&builder);
        pipeline_builder_enable_blending_alphablend(&builder);
        pipeline_builder_enable_depth_test(&builder, 1, VK_COMPARE_OP_GREATER_OR_EQUAL);
        pipeline_builder_set_color_attachment_format(&builder, vulkan->drawImage.imageFormat != VK_FORMAT_UNDEFINED 
            ? vulkan->drawImage.imageFormat 
            : VK_FORMAT_R16G16B16A16_SFLOAT);
        pipeline_builder_set_depth_format(&builder, VK_FORMAT_D32_SFLOAT);
        builder.pipelineLayout = pipelineLayout;

        pipeline = pipeline_builder_build(&builder, vulkan->device.device);
        if (pipeline == VK_NULL_HANDLE) {
            goto cleanup_fail;
        }
    }

    vkDestroyShaderModule(vulkan->device.device, vertShader, 0);
    vkDestroyShaderModule(vulkan->device.device, fragShader, 0);

    vulkan->pipelines.meshPipelineLayout = pipelineLayout;
    vulkan->pipelines.meshPipeline = pipeline;

    vkdefer_destroy_VkPipelineLayout(&vulkan->deferCtx.globalBuf, pipelineLayout);
    vkdefer_destroy_VkPipeline(&vulkan->deferCtx.globalBuf, pipeline);

    LOG_INFO(VULKAN_LOG_DOMAIN, "Mesh graphics pipeline initialized");
    return 1;

cleanup_fail:
    if (vertShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan->device.device, vertShader, 0);
    }
    if (fragShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan->device.device, fragShader, 0);
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan->device.device, pipeline, 0);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan->device.device, pipelineLayout, 0);
    }
    return 0;
}

static void vulkan_draw_mesh(RendererVulkan* vulkan, VkCommandBuffer cmd, GPUMeshBuffers* mesh, 
                             Mat4x4F32 worldMatrix, U32 indexCount, F32 alpha) {
    if (!vulkan || cmd == VK_NULL_HANDLE || !mesh) {
        return;
    }
    if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
        return;
    }

    VkPipeline pipeline = (alpha < 1.0f) ? vulkan->transparentPipeline : vulkan->opaquePipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (F32)vulkan->drawExtent.width;
    viewport.height = (F32)vulkan->drawExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = vulkan->drawExtent.width;
    scissor.extent.height = vulkan->drawExtent.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    GPUDrawPushConstants pushConstants = {};
    pushConstants.worldMatrix = worldMatrix;
    pushConstants.vertexBuffer = mesh->vertexBufferAddress;
    pushConstants.alpha = alpha;
    
    vkCmdPushConstants(cmd,
                       vulkan->materialPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 
                       0,
                       sizeof(GPUDrawPushConstants),
                       &pushConstants);
    
    vkCmdBindIndexBuffer(cmd, mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

// Shader Compilation (DXC)

B32 renderer_vulkan_compile_shader_to_result(RendererVulkan* vulkan, Arena* arena, StringU8 shaderPath,
                                             ShaderCompileResult* outResult) {
    if (!vulkan || str8_is_nil(shaderPath) || !outResult) {
        return 0;
    }

    MEMSET(outResult, 0, sizeof(ShaderCompileResult));

    if (shaderPath.size < 5) {
        return 0;
    }

    StringU8 suffix = str8((const char*) shaderPath.data + shaderPath.size - 5, 5);
    if (!str8_equal(suffix, str8(".hlsl"))) {
        return 0;
    }

    IDxcCompiler3* dxcCompiler = 0;
    IDxcUtils* dxcUtils = 0;
    if (!vulkan_get_dxc_instances(&dxcCompiler, &dxcUtils)) {
        return 0;
    }

    OS_Handle shaderFile = OS_file_open((const char*) shaderPath.data, OS_FileOpenMode_Read);
    if (!shaderFile.handle) {
        return 0;
    }

    OS_FileMapping mapping = OS_file_map_ro(shaderFile);
    OS_file_close(shaderFile);

    if (!mapping.ptr || mapping.length == 0) {
        if (mapping.ptr) {
            OS_file_unmap(mapping);
        }
        return 0;
    }

    U64 shaderSize = mapping.length;

    if (shaderSize > 0xFFFFFFFFULL) {
        OS_file_unmap(mapping);
        return 0;
    }

    Arena* excludes[1] = {arena};
    Temp scratch = get_scratch(excludes, ARRAY_COUNT(excludes));
    DEFER_REF(temp_end(&scratch));
    Arena* scratchArena = scratch.arena;

    U8* shaderSource = ARENA_PUSH_ARRAY(scratchArena, U8, shaderSize);
    if (!shaderSource) {
        OS_file_unmap(mapping);
        return 0;
    }

    MEMMOVE(shaderSource, mapping.ptr, shaderSize);
    OS_file_unmap(mapping);

    IDxcBlobEncoding* sourceBlob = 0;
    HRESULT hr = dxcUtils->CreateBlobFromPinned(shaderSource, (U32) shaderSize, DXC_CP_UTF8, &sourceBlob);
    if (FAILED(hr) || !sourceBlob) {
        return 0;
    }

    const wchar_t* targetProfile = L"vs_6_0";
    B32 isCompute = 0;

    if (shaderPath.size >= 10) {
        StringU8 computeSuffix = str8((const char*) shaderPath.data + shaderPath.size - 10, 10);
        if (str8_equal(computeSuffix, str8(".comp.hlsl"))) {
            targetProfile = L"cs_6_0";
            isCompute = 1;
        }
    }

    B32 isFragment = 0;
    if (!isCompute && shaderPath.size >= 10) {
        StringU8 fragSuffix = str8((const char*) shaderPath.data + shaderPath.size - 10, 10);
        if (str8_equal(fragSuffix, str8(".frag.hlsl"))) {
            targetProfile = L"ps_6_0";
            isFragment = 1;
        }
    }

    if (!isCompute && !isFragment && shaderPath.size >= 8) {
        for (U64 i = 0; i <= shaderPath.size - 8; ++i) {
            StringU8 substr = str8((const char*) shaderPath.data + i, 8);
            if (str8_equal(substr, str8("fragment"))) {
                targetProfile = L"ps_6_0";
                break;
            }
        }
    }

    const wchar_t* args[] = {
        L"-spirv",
        L"-T", targetProfile,
        L"-E", L"main",
    };
    const U32 argCount = sizeof(args) / sizeof(args[0]);

    IDxcIncludeHandler* includeHandler = 0;
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr) || !includeHandler) {
        sourceBlob->Release();
        return 0;
    }

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    IDxcResult* compileResult = 0;
    hr = dxcCompiler->Compile(&sourceBuffer, args, argCount, includeHandler, IID_PPV_ARGS(&compileResult));

    sourceBlob->Release();
    includeHandler->Release();

    if (FAILED(hr) || !compileResult) {
        return 0;
    }

    HRESULT status = 0;
    hr = compileResult->GetStatus(&status);
    if (FAILED(hr)) {
        compileResult->Release();
        return 0;
    }

    if (FAILED(status)) {
        IDxcBlobUtf8* errors = 0;
        hr = compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), 0);
        if (SUCCEEDED(hr) && errors && errors->GetStringLength() > 0) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "DXC compilation errors for shader {}:\n{}",
                      shaderPath, errors->GetStringPointer());
            errors->Release();
        }
        compileResult->Release();
        return 0;
    }

    IDxcBlob* spirvBlob = 0;
    hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&spirvBlob), 0);
    if (FAILED(hr) || !spirvBlob) {
        compileResult->Release();
        return 0;
    }

    U64 spirvSize = spirvBlob->GetBufferSize();
    U32* spirvCode = ARENA_PUSH_ARRAY(scratchArena, U32, (spirvSize + sizeof(U32) - 1) / sizeof(U32));
    if (!spirvCode) {
        spirvBlob->Release();
        compileResult->Release();
        return 0;
    }

    MEMMOVE(spirvCode, spirvBlob->GetBufferPointer(), spirvSize);
    spirvBlob->Release();
    compileResult->Release();

    StringU8 basePath = str8((const char*) shaderPath.data, shaderPath.size - 5);
    StringU8 outputPath = str8_concat(scratchArena, basePath, str8(".spv"));

    OS_Handle outputFile = OS_file_open((const char*) outputPath.data, OS_FileOpenMode_Create);
    if (outputFile.handle) {
        RangeU64 writeRange = {0, spirvSize};
        OS_file_write(outputFile, writeRange, spirvCode);
        OS_file_close(outputFile);
    }

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvSize;
    createInfo.pCode = spirvCode;

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult vkResult = vkCreateShaderModule(vulkan->device.device, &createInfo, 0, &shaderModule);
    if (vkResult != VK_SUCCESS) {
        return 0;
    }

    outResult->module = (void*) shaderModule;
    outResult->path = shaderPath;
    outResult->valid = 1;
    outResult->handle = 0;

    return 1;
}

static void vulkan_merge_shader_results_wrapper(void* backendData, Arena* arena, const ShaderCompileResult* results,
                                                U32 resultCount, const ShaderCompileRequest* requests,
                                                U32 requestCount) {
    RendererVulkan* vulkan = (RendererVulkan*) backendData;
    if (!vulkan || !results || resultCount == 0u) {
        return;
    }
    
    // ... implementation ...
    // Since I didn't copy this implementation, I should.
    // It was in renderer_vulkan.cpp.
    
    if (vulkan->pipelines.shaderCount + resultCount > vulkan->pipelines.shaderCapacity) {
        U32 newCapacity = vulkan->pipelines.shaderCount + resultCount;
        if (newCapacity < vulkan->pipelines.shaderCapacity * 2u) {
            newCapacity = vulkan->pipelines.shaderCapacity * 2u;
        }
        if (newCapacity < 16u) {
            newCapacity = 16u;
        }

        RendererVulkanShader* newShaders = (RendererVulkanShader*) arena_push(vulkan->arena,
                                                                              sizeof(RendererVulkanShader) *
                                                                              newCapacity,
                                                                              alignof(RendererVulkanShader));
        if (!newShaders) {
            return;
        }

        if (vulkan->pipelines.shaders) {
            MEMMOVE(newShaders, vulkan->pipelines.shaders, sizeof(RendererVulkanShader) * vulkan->pipelines.shaderCount);
        }
        vulkan->pipelines.shaders = newShaders;
        vulkan->pipelines.shaderCapacity = newCapacity;
    }

    U32 baseIndex = vulkan->pipelines.shaderCount;
    for (U32 i = 0; i < resultCount; ++i) {
        if (!results[i].valid) {
            continue;
        }

        U32 shaderIndex = baseIndex;
        RendererVulkanShader* shader = &vulkan->pipelines.shaders[shaderIndex];
        shader->module = (VkShaderModule) results[i].module;
        shader->path = str8_cpy(vulkan->arena, results[i].path);
        baseIndex++;

        for (U32 reqIdx = 0; reqIdx < requestCount; ++reqIdx) {
            if (str8_equal(requests[reqIdx].shaderPath, results[i].path)) {
                if (requests[reqIdx].outHandle) {
                    *requests[reqIdx].outHandle = (ShaderHandle)(shaderIndex + 1u);
                }
                break;
            }
        }

        vkdefer_destroy_VkShaderModule(&vulkan->deferCtx.globalBuf, (VkShaderModule) results[i].module);
    }

    vulkan->pipelines.shaderCount = baseIndex;
}

static B32 vulkan_compile_shader_wrapper(void* backendData, Arena* arena, StringU8 shaderPath,
                                         ShaderCompileResult* outResult) {
    RendererVulkan* vulkan = (RendererVulkan*) backendData;
    return renderer_vulkan_compile_shader_to_result(vulkan, arena, shaderPath, outResult);
}

