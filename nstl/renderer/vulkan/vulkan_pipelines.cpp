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
    // Not implementing full builder for now as we only have 1 compute pipeline
    // But following plan structure for future expansion
    
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
    B32 success = 0;

    do {
        // Descriptor Set Layout
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

        VkResult descriptorLayoutResult =
            vkCreateDescriptorSetLayout(vulkan->device.device, &descriptorLayoutInfo, 0, &descriptorLayout);
        if (descriptorLayoutResult != VK_SUCCESS) break;

        // Descriptor Pool
        VulkanDescriptorAllocator::PoolSizeRatio ratios[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f }
        };
        if (!vulkan_create_descriptor_pool(vulkan->device.device, ratios, 1, 10, &descriptorPool)) {
            break;
        }

        // Allocate Set
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1u;
        allocInfo.pSetLayouts = &descriptorLayout;

        VkResult allocResult = vkAllocateDescriptorSets(vulkan->device.device, &allocInfo, &descriptorSet);
        if (allocResult != VK_SUCCESS) break;

        // Pipeline Layout
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

        VkResult pipelineLayoutResult =
            vkCreatePipelineLayout(vulkan->device.device, &pipelineLayoutInfo, 0, &pipelineLayout);
        if (pipelineLayoutResult != VK_SUCCESS) break;

        // Shader
        const char* shaderFilePath = "shaders/gradient.comp.spv";
        if (!vulkan_load_shader_module(&vulkan->device, shaderFilePath, &shaderModule)) {
            break;
        }

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
        
        VkResult pipelineResult = vkCreateComputePipelines(vulkan->device.device, VK_NULL_HANDLE, 1u, &pipelineInfo, 0, &pipeline);
        if (pipelineResult != VK_SUCCESS) break;

        success = 1;
    } while (0);

    if (shaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan->device.device, shaderModule, 0);
    }

    if (!success) {
        // Cleanup...
        return 0;
    }

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

    if (!isCompute && shaderPath.size >= 8) {
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

