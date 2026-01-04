//
// Created by André Leite on 30/12/2025.
//

// ////////////////////////
// Material Pipeline Creation

static B32 vulkan_init_material_pipelines(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device.device == VK_NULL_HANDLE) {
        return 0;
    }

    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        VkResult result = vkCreateDescriptorSetLayout(vulkan->device.device, &layoutInfo, 0, &vulkan->sceneDataLayout);
        if (result != VK_SUCCESS) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create scene data descriptor layout: {}", result);
            return 0;
        }
    }

    {
        VkDescriptorSetLayoutBinding bindings[2] = {};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;

        VkResult result = vkCreateDescriptorSetLayout(vulkan->device.device, &layoutInfo, 0, &vulkan->materialLayout);
        if (result != VK_SUCCESS) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create material descriptor layout: {}", result);
            vkDestroyDescriptorSetLayout(vulkan->device.device, vulkan->sceneDataLayout, 0);
            vulkan->sceneDataLayout = VK_NULL_HANDLE;
            return 0;
        }
    }

    {
        VkDescriptorSetLayout layouts[] = { vulkan->sceneDataLayout, vulkan->materialLayout };

        VkPushConstantRange pushConstant = {};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(GPUDrawPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = layouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        VkResult result = vkCreatePipelineLayout(vulkan->device.device, &layoutInfo, 0, &vulkan->materialPipelineLayout);
        if (result != VK_SUCCESS) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create material pipeline layout: {}", result);
            vkDestroyDescriptorSetLayout(vulkan->device.device, vulkan->materialLayout, 0);
            vkDestroyDescriptorSetLayout(vulkan->device.device, vulkan->sceneDataLayout, 0);
            return 0;
        }
    }

    VkFormat drawFormat = vulkan_select_draw_image_format(&vulkan->device);
    if (drawFormat == VK_FORMAT_UNDEFINED) {
        drawFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;

    if (!vulkan_load_shader_module(&vulkan->device, "shaders/mesh.vert.spv", &vertShader)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to load material vertex shader");
        goto cleanup_fail;
    }

    if (!vulkan_load_shader_module(&vulkan->device, "shaders/mesh.frag.spv", &fragShader)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to load material fragment shader");
        vkDestroyShaderModule(vulkan->device.device, vertShader, 0);
        goto cleanup_fail;
    }

    {
        PipelineBuilder builder;
        pipeline_builder_clear(&builder);
        pipeline_builder_set_shaders(&builder, vertShader, fragShader);
        pipeline_builder_set_input_topology(&builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pipeline_builder_set_polygon_mode(&builder, VK_POLYGON_MODE_FILL);
        pipeline_builder_set_cull_mode(&builder, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pipeline_builder_set_multisampling_none(&builder);
        pipeline_builder_disable_blending(&builder);
        pipeline_builder_enable_depth_test(&builder, 1, VK_COMPARE_OP_LESS_OR_EQUAL);
        pipeline_builder_set_color_attachment_format(&builder, drawFormat);
        pipeline_builder_set_depth_format(&builder, VK_FORMAT_D32_SFLOAT);
        builder.pipelineLayout = vulkan->materialPipelineLayout;

        vulkan->opaquePipeline = pipeline_builder_build(&builder, vulkan->device.device);
        if (vulkan->opaquePipeline == VK_NULL_HANDLE) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create opaque material pipeline");
            vkDestroyShaderModule(vulkan->device.device, vertShader, 0);
            vkDestroyShaderModule(vulkan->device.device, fragShader, 0);
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
        pipeline_builder_enable_depth_test(&builder, 0, VK_COMPARE_OP_LESS_OR_EQUAL);
        pipeline_builder_set_color_attachment_format(&builder, drawFormat);
        pipeline_builder_set_depth_format(&builder, VK_FORMAT_D32_SFLOAT);
        builder.pipelineLayout = vulkan->materialPipelineLayout;

        vulkan->transparentPipeline = pipeline_builder_build(&builder, vulkan->device.device);
        if (vulkan->transparentPipeline == VK_NULL_HANDLE) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create transparent material pipeline");
            vkDestroyPipeline(vulkan->device.device, vulkan->opaquePipeline, 0);
            vulkan->opaquePipeline = VK_NULL_HANDLE;
            vkDestroyShaderModule(vulkan->device.device, vertShader, 0);
            vkDestroyShaderModule(vulkan->device.device, fragShader, 0);
            goto cleanup_fail;
        }
    }

    vkDestroyShaderModule(vulkan->device.device, vertShader, 0);
    vkDestroyShaderModule(vulkan->device.device, fragShader, 0);

    {
        VkDescriptorType types[] = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        };
        F32 ratios[] = { 1.0f, 1.0f };
        if (!vulkan_create_descriptor_pool_(vulkan->device.device, types, ratios, 2, 200, &vulkan->globalDescriptorPool)) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create global descriptor pool");
            goto cleanup_fail;
        }
    }

    vulkan->defaultMaterialBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                                          sizeof(MaterialConstants),
                                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                          VMA_MEMORY_USAGE_CPU_TO_GPU);
    {
        MaterialConstants* matConsts = (MaterialConstants*)vulkan->defaultMaterialBuffer.info.pMappedData;
        MEMSET(matConsts, 0, sizeof(MaterialConstants));
        matConsts->colorFactor.r = 1.0f;
        matConsts->colorFactor.g = 1.0f;
        matConsts->colorFactor.b = 1.0f;
        matConsts->colorFactor.a = 1.0f;
        matConsts->metalRoughFactor.r = 0.0f;
        matConsts->metalRoughFactor.g = 1.0f;
    }

    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = vulkan->globalDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &vulkan->materialLayout;

        VkResult result = vkAllocateDescriptorSets(vulkan->device.device, &allocInfo, &vulkan->defaultMaterial.descriptorSet);
        if (result != VK_SUCCESS) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate default material descriptor set: {}", result);
            goto cleanup_fail;
        }

        VulkanDescriptorWriter writer = {};
        vulkan_descriptor_writer_write_buffer(&writer, 0, vulkan->defaultMaterialBuffer.buffer,
                                               sizeof(MaterialConstants), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        vulkan_descriptor_writer_write_image(&writer, 1, vulkan->errorImage.imageView, vulkan->samplerNearest,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        vulkan_descriptor_writer_update_set(&vulkan->device, &writer, vulkan->defaultMaterial.descriptorSet);

        vulkan->defaultMaterial.pipeline = vulkan->opaquePipeline;
        vulkan->defaultMaterial.layout = vulkan->materialPipelineLayout;
        vulkan->defaultMaterial.type = MaterialType_Opaque;
    }

    vkdefer_destroy_VkDescriptorPool(&vulkan->deferCtx.globalBuf, vulkan->globalDescriptorPool);
    vkdefer_destroy_VkDescriptorSetLayout(&vulkan->deferCtx.globalBuf, vulkan->sceneDataLayout);
    vkdefer_destroy_VkDescriptorSetLayout(&vulkan->deferCtx.globalBuf, vulkan->materialLayout);
    vkdefer_destroy_VkPipelineLayout(&vulkan->deferCtx.globalBuf, vulkan->materialPipelineLayout);
    vkdefer_destroy_VkPipeline(&vulkan->deferCtx.globalBuf, vulkan->opaquePipeline);
    vkdefer_destroy_VkPipeline(&vulkan->deferCtx.globalBuf, vulkan->transparentPipeline);

    LOG_INFO(VULKAN_LOG_DOMAIN, "Material pipelines initialized");
    return 1;

cleanup_fail:
    if (vulkan->materialPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan->device.device, vulkan->materialPipelineLayout, 0);
        vulkan->materialPipelineLayout = VK_NULL_HANDLE;
    }
    if (vulkan->materialLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan->device.device, vulkan->materialLayout, 0);
        vulkan->materialLayout = VK_NULL_HANDLE;
    }
    if (vulkan->sceneDataLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan->device.device, vulkan->sceneDataLayout, 0);
        vulkan->sceneDataLayout = VK_NULL_HANDLE;
    }
    return 0;
}

static void vulkan_destroy_material_pipelines(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    if (vulkan->defaultMaterialBuffer.buffer != VK_NULL_HANDLE) {
        vulkan_destroy_buffer(vulkan->device.allocator, &vulkan->defaultMaterialBuffer);
    }
    vulkan->opaquePipeline = VK_NULL_HANDLE;
    vulkan->transparentPipeline = VK_NULL_HANDLE;
    vulkan->materialPipelineLayout = VK_NULL_HANDLE;
    vulkan->sceneDataLayout = VK_NULL_HANDLE;
    vulkan->materialLayout = VK_NULL_HANDLE;
    vulkan->globalDescriptorPool = VK_NULL_HANDLE;
}

static void material_fill(GPUMaterial* mat,
                          RendererVulkan* vulkan,
                          MaterialType type,
                          VkDescriptorSet descriptorSet) {
    if (!mat || !vulkan) {
        return;
    }
    mat->type = type;
    mat->descriptorSet = descriptorSet;
    mat->layout = vulkan->materialPipelineLayout;

    if (type == MaterialType_Transparent) {
        mat->pipeline = vulkan->transparentPipeline;
    } else {
        mat->pipeline = vulkan->opaquePipeline;
    }
}

// ////////////////////////
// Default Resources

static B32 vulkan_init_default_resources(RendererVulkan* vulkan) {
    if (!vulkan) {
        return 0;
    }

    U32 whitePixel = 0xFFFFFFFF;
    VkExtent3D size1x1 = { 1, 1, 1 };
    vulkan->whiteImage = vulkan_create_image_data(vulkan,
                                                   &whitePixel,
                                                   size1x1,
                                                   VK_FORMAT_R8G8B8A8_UNORM,
                                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    if (vulkan->whiteImage.image == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create white default texture");
        return 0;
    }

    U32 blackPixel = 0xFF000000;
    vulkan->blackImage = vulkan_create_image_data(vulkan,
                                                   &blackPixel,
                                                   size1x1,
                                                   VK_FORMAT_R8G8B8A8_UNORM,
                                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    if (vulkan->blackImage.image == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create black default texture");
        return 0;
    }

    U32 checkerboard[16 * 16];
    for (U32 y = 0; y < 16; ++y) {
        for (U32 x = 0; x < 16; ++x) {
            B32 isWhite = ((x / 2) + (y / 2)) % 2 == 0;
            checkerboard[y * 16 + x] = isWhite ? 0xFFFFFFFF : 0xFF000000;
        }
    }
    VkExtent3D size16x16 = { 16, 16, 1 };
    vulkan->errorImage = vulkan_create_image_data(vulkan,
                                                   checkerboard,
                                                   size16x16,
                                                   VK_FORMAT_R8G8B8A8_UNORM,
                                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    if (vulkan->errorImage.image == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create error checkerboard texture");
        return 0;
    }

    vulkan->samplerLinear = vulkan_create_sampler(&vulkan->device, VK_FILTER_LINEAR);
    if (vulkan->samplerLinear == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create linear sampler");
        return 0;
    }

    vulkan->samplerNearest = vulkan_create_sampler(&vulkan->device, VK_FILTER_NEAREST);
    if (vulkan->samplerNearest == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create nearest sampler");
        return 0;
    }

    vkdefer_destroy_VkSampler(&vulkan->deferCtx.globalBuf, vulkan->samplerLinear);
    vkdefer_destroy_VkSampler(&vulkan->deferCtx.globalBuf, vulkan->samplerNearest);

    LOG_INFO(VULKAN_LOG_DOMAIN, "Default resources initialized");
    return 1;
}

static void vulkan_destroy_default_resources(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    vulkan_destroy_image(vulkan, &vulkan->whiteImage);
    vulkan_destroy_image(vulkan, &vulkan->blackImage);
    vulkan_destroy_image(vulkan, &vulkan->errorImage);
}

