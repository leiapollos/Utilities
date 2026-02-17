//
// Created by André Leite on 03/11/2025.
//

// ////////////////////////
// Struct Helpers

static VkImageCreateInfo vulkan_image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent) {
    VkImageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = nullptr;

    info.imageType = VK_IMAGE_TYPE_2D;

    info.format = format;
    info.extent = extent;

    info.mipLevels = 1;
    info.arrayLayers = 1;

    info.samples = VK_SAMPLE_COUNT_1_BIT;

    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usageFlags;

    return info;
}

static VkImageViewCreateInfo vulkan_image_view_create_info(VkFormat format, VkImage image,
                                                           VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.pNext = nullptr;

    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.image = image;
    info.format = format;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.aspectMask = aspectFlags;

    return info;
}

static VkImageSubresourceRange vulkan_image_subresource_range(VkImageAspectFlags aspectMask) {
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    return subresourceRange;
}

static VkImageMemoryBarrier2 vulkan_image_memory_barrier2(VkImage image,
                                                          VkImageLayout oldLayout,
                                                          VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.pNext = VK_NULL_HANDLE;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    B32 isDepth = (oldLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
                   newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
                   oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                   newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                   oldLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
                   newLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    VkImageAspectFlags aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange = vulkan_image_subresource_range(aspectMask);
    barrier.image = image;
    return barrier;
}

static VkDependencyInfo vulkan_dependency_info(U32 imageMemoryBarrierCount,
                                               const VkImageMemoryBarrier2* pImageMemoryBarriers) {
    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = 0;
    depInfo.imageMemoryBarrierCount = imageMemoryBarrierCount;
    depInfo.pImageMemoryBarriers = pImageMemoryBarriers;
    return depInfo;
}

// ////////////////////////
// Command Helpers

static void vulkan_transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout,
                                    VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier = vulkan_image_memory_barrier2(image, currentLayout, newLayout);
    VkDependencyInfo depInfo = vulkan_dependency_info(1, &barrier);
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

static void vulkan_copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize,
                                       VkExtent2D dstSize) {
    VkImageBlit2 blitRegion = {};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.pNext = nullptr;

    blitRegion.srcOffsets[1].x = SafeCast_U32_S32(srcSize.width);
    blitRegion.srcOffsets[1].y = SafeCast_U32_S32(srcSize.height);
    blitRegion.srcOffsets[1].z = 1;

    blitRegion.dstOffsets[1].x = SafeCast_U32_S32(dstSize.width);
    blitRegion.dstOffsets[1].y = SafeCast_U32_S32(dstSize.height);
    blitRegion.dstOffsets[1].z = 1;

    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo = {};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.pNext = nullptr;
    blitInfo.dstImage = destination;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.srcImage = source;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(cmd, &blitInfo);
}

static RendererVulkanAllocatedBuffer vulkan_create_buffer(VmaAllocator allocator, U64 allocSize,
                                                          VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
static void vulkan_destroy_buffer(VmaAllocator allocator, RendererVulkanAllocatedBuffer* buffer);
static RendererVulkanAllocatedImage vulkan_create_image(RendererVulkan* vulkan,
                                                        VkExtent3D size,
                                                        VkFormat format,
                                                        VkImageUsageFlags usage);
static void vulkan_destroy_image(RendererVulkan* vulkan, RendererVulkanAllocatedImage* img);

static void vulkan_log_allocator_stats_(RendererVulkan* vulkan, const char* label) {
    if (!vulkan || vulkan->device.allocator == 0 || vulkan->device.physicalDevice == VK_NULL_HANDLE) {
        return;
    }

    VmaTotalStatistics stats = {};
    vmaCalculateStatistics(vulkan->device.allocator, &stats);
    LOG_INFO(VULKAN_LOG_DOMAIN,
             "VMA {} total: block={} MB, allocated={} MB, blocks={}, allocations={}",
             label ? label : "stats",
             (stats.total.statistics.blockBytes / MB(1)),
             (stats.total.statistics.allocationBytes / MB(1)),
             stats.total.statistics.blockCount,
             stats.total.statistics.allocationCount);

    VkPhysicalDeviceMemoryProperties memProps = {};
    vkGetPhysicalDeviceMemoryProperties(vulkan->device.physicalDevice, &memProps);

    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(vulkan->device.allocator, budgets);
    for (U32 heapIndex = 0; heapIndex < memProps.memoryHeapCount; ++heapIndex) {
        const VkMemoryHeap* heap = &memProps.memoryHeaps[heapIndex];
        const VmaBudget* budget = &budgets[heapIndex];
        const char* heapKind = FLAGS_HAS(heap->flags, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "device-local" : "host-visible";
        LOG_INFO(VULKAN_LOG_DOMAIN,
                 "VMA heap {} ({}): usage={} MB, budget={} MB, heapSize={} MB, block={} MB",
                 heapIndex,
                 heapKind,
                 (budget->usage / MB(1)),
                 (budget->budget / MB(1)),
                 (heap->size / MB(1)),
                 (budget->statistics.blockBytes / MB(1)));
    }
}

// ////////////////////////
// Draw Image Management

static VkFormat vulkan_select_draw_image_format(VulkanDevice* device) {
    if (!device || device->physicalDevice == VK_NULL_HANDLE) {
        return VK_FORMAT_UNDEFINED;
    }

    static const VkFormat candidates[] = {
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
    };

    const VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

    for (U32 i = 0; i < ARRAY_COUNT(candidates); ++i) {
        VkFormat format = candidates[i];
        VkFormatProperties props = {};
        vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & requiredFeatures) == requiredFeatures) {
            if (format != VK_FORMAT_R32G32B32A32_SFLOAT) {
                LOG_WARNING(VULKAN_LOG_DOMAIN,
                            "Falling back to draw image format {} due to limited GPU support",
                            (U32) format);
            }
            return format;
        }
    }

    LOG_ERROR(VULKAN_LOG_DOMAIN,
              "Failed to find supported draw image format with storage/color/transfer capabilities");
    return VK_FORMAT_UNDEFINED;
}

static void vulkan_reset_draw_image(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }

    vulkan->drawImage.image = VK_NULL_HANDLE;
    vulkan->drawImage.imageView = VK_NULL_HANDLE;
    vulkan->drawImage.allocation = 0;
    vulkan->drawImage.imageExtent = {};
    vulkan->drawImage.imageFormat = VK_FORMAT_UNDEFINED;
}

static B32 vulkan_create_draw_image(RendererVulkan* vulkan, VkExtent3D extent) {
    if (!vulkan || vulkan->device.allocator == 0) {
        return 0;
    }
    
    // Cleanup old image if exists
    if (vulkan->drawImage.image != VK_NULL_HANDLE) {
         vkDestroyImageView(vulkan->device.device, vulkan->drawImage.imageView, 0);
         vmaDestroyImage(vulkan->device.allocator, vulkan->drawImage.image, vulkan->drawImage.allocation);
         vulkan_reset_draw_image(vulkan);
    }

    VkFormat drawImageFormat = vulkan_select_draw_image_format(&vulkan->device);
    if (drawImageFormat == VK_FORMAT_UNDEFINED) {
        return 0;
    }

    vulkan->drawImage.imageFormat = drawImageFormat;
    vulkan->drawImage.imageExtent = extent;
    vulkan->drawExtent.width = extent.width;
    vulkan->drawExtent.height = extent.height;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vulkan_image_create_info(vulkan->drawImage.imageFormat, drawImageUsages,
                                                           extent);

    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkResult drawImageResult = vmaCreateImage(vulkan->device.allocator,
                                              &rimg_info,
                                              &rimg_allocinfo,
                                              &vulkan->drawImage.image,
                                              &vulkan->drawImage.allocation,
                                              nullptr);
    if (drawImageResult != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN,
                  "Failed to create draw image (format={}, extent={}x{}): {}",
                  (U32) vulkan->drawImage.imageFormat,
                  extent.width,
                  extent.height,
                  drawImageResult);
        vulkan_reset_draw_image(vulkan);
        return 0;
    }

    VkImageViewCreateInfo rview_info = vulkan_image_view_create_info(vulkan->drawImage.imageFormat,
                                                                     vulkan->drawImage.image,
                                                                     VK_IMAGE_ASPECT_COLOR_BIT);

    VkResult drawImageViewResult = vkCreateImageView(vulkan->device.device,
                                                     &rview_info,
                                                     nullptr,
                                                     &vulkan->drawImage.imageView);
    if (drawImageViewResult != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN,
                  "Failed to create draw image view: {}",
                  drawImageViewResult);
        vmaDestroyImage(vulkan->device.allocator, vulkan->drawImage.image, vulkan->drawImage.allocation);
        vulkan_reset_draw_image(vulkan);
        return 0;
    }
    
    // Note: We are managing draw image lifecycle manually to handle resizing correctly
    // without complex defer logic. It is destroyed above if it exists, and on shutdown
    // we should manually destroy it.

    VmaAllocationInfo allocInfo = {};
    vmaGetAllocationInfo(vulkan->device.allocator, vulkan->drawImage.allocation, &allocInfo);
    LOG_INFO(VULKAN_LOG_DOMAIN,
             "Created draw image: format={}, extent={}x{}, alloc={} MB",
             (U32)vulkan->drawImage.imageFormat,
             extent.width,
             extent.height,
             (allocInfo.size / MB(1)));
    vulkan_log_allocator_stats_(vulkan, "after draw image");

    return 1;
}

static void vulkan_destroy_draw_image(RendererVulkan* vulkan) {
    if (!vulkan) return;
    
    if (vulkan->drawImage.image != VK_NULL_HANDLE) {
         if (vulkan->drawImage.imageView != VK_NULL_HANDLE) {
             vkDestroyImageView(vulkan->device.device, vulkan->drawImage.imageView, 0);
         }
         vmaDestroyImage(vulkan->device.allocator, vulkan->drawImage.image, vulkan->drawImage.allocation);
         vulkan_reset_draw_image(vulkan);
    }
}

static void vulkan_destroy_radiance_images(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }

    for (U32 i = 0; i < VULKAN_RADIANCE_2D_MAX_CASCADES; ++i) {
        if (vulkan->radianceCascadeImages[i].image != VK_NULL_HANDLE) {
            vulkan_destroy_image(vulkan, &vulkan->radianceCascadeImages[i]);
        }
    }

    vulkan->radianceCascadeImageCount = 0u;
    vulkan->radianceGridWidth = 0u;
    vulkan->radianceGridHeight = 0u;
    vulkan->radianceImagesInitialized = 0;
}

static B32 vulkan_upload_pixels_to_image(RendererVulkan* vulkan,
                                         RendererVulkanAllocatedImage* image,
                                         const void* pixels,
                                         U32 width,
                                         U32 height,
                                         VkImageLayout initialLayout) {
    if (!vulkan || !image || !pixels) {
        return 0;
    }
    if (image->image == VK_NULL_HANDLE || image->imageView == VK_NULL_HANDLE) {
        return 0;
    }
    if (image->imageExtent.width != width || image->imageExtent.height != height) {
        return 0;
    }

    U64 dataSize = (U64) width * (U64) height * 4u;
    RendererVulkanAllocatedBuffer stagingBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                                                        dataSize,
                                                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                                        VMA_MEMORY_USAGE_CPU_TO_GPU);
    if (stagingBuffer.buffer == VK_NULL_HANDLE) {
        return 0;
    }

    MEMMOVE(stagingBuffer.info.pMappedData, pixels, dataSize);

    B32 success = 0;
    VkCommandBuffer cmd = vulkan_immediate_begin(vulkan, &vulkan->immSubmit);
    if (cmd != VK_NULL_HANDLE) {
        vulkan_transition_image(cmd, image->image, initialLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(cmd,
                               stagingBuffer.buffer,
                               image->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &copyRegion);

        vulkan_transition_image(cmd,
                                image->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vulkan_immediate_end(vulkan, &vulkan->immSubmit);
        success = 1;
    }

    vulkan_destroy_buffer(vulkan->device.allocator, &stagingBuffer);
    return success;
}

static B32 vulkan_ensure_radiance_images(RendererVulkan* vulkan, U32 gridWidth, U32 gridHeight, U32 cascadeCount) {
    if (!vulkan || gridWidth == 0u || gridHeight == 0u) {
        return 0;
    }

    U32 clampedCascadeCount = CLAMP(cascadeCount, 1u, VULKAN_RADIANCE_2D_MAX_CASCADES);
    if (vulkan->radianceImagesInitialized &&
        vulkan->radianceGridWidth == gridWidth &&
        vulkan->radianceGridHeight == gridHeight &&
        vulkan->radianceCascadeImageCount == clampedCascadeCount) {
        return 1;
    }

    vulkan_destroy_radiance_images(vulkan);

    VkFormat imageFormat = (vulkan->drawImage.imageFormat != VK_FORMAT_UNDEFINED)
                               ? vulkan->drawImage.imageFormat
                               : VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VkExtent3D imageExtent = {};
    imageExtent.width = gridWidth;
    imageExtent.height = gridHeight;
    imageExtent.depth = 1u;

    for (U32 i = 0; i < clampedCascadeCount; ++i) {
        RendererVulkanAllocatedImage image = vulkan_create_image(vulkan, imageExtent, imageFormat, usage);
        if (image.image == VK_NULL_HANDLE || image.imageView == VK_NULL_HANDLE) {
            vulkan_destroy_radiance_images(vulkan);
            return 0;
        }
        vulkan->radianceCascadeImages[i] = image;
    }

    VkCommandBuffer cmd = vulkan_immediate_begin(vulkan, &vulkan->immSubmit);
    if (cmd != VK_NULL_HANDLE) {
        for (U32 i = 0; i < clampedCascadeCount; ++i) {
            vulkan_transition_image(cmd,
                                    vulkan->radianceCascadeImages[i].image,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        vulkan_immediate_end(vulkan, &vulkan->immSubmit);
    } else {
        vulkan_destroy_radiance_images(vulkan);
        return 0;
    }

    vulkan->radianceCascadeImageCount = clampedCascadeCount;
    vulkan->radianceGridWidth = gridWidth;
    vulkan->radianceGridHeight = gridHeight;
    vulkan->radianceImagesInitialized = 1;
    return 1;
}

// ////////////////////////
// Buffer Management

static RendererVulkanAllocatedBuffer vulkan_create_buffer(VmaAllocator allocator, U64 allocSize,
                                                          VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    RendererVulkanAllocatedBuffer newBuffer = {};
    VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo,
                             &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
    return newBuffer;
}

static void vulkan_destroy_buffer(VmaAllocator allocator, RendererVulkanAllocatedBuffer* buffer) {
    if (!buffer || buffer->buffer == VK_NULL_HANDLE) {
        return;
    }
    vmaDestroyBuffer(allocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;
}

// ////////////////////////
// Depth Image Management

static void vulkan_reset_depth_image(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    vulkan->depthImage.image = VK_NULL_HANDLE;
    vulkan->depthImage.imageView = VK_NULL_HANDLE;
    vulkan->depthImage.allocation = 0;
    vulkan->depthImage.imageExtent = {};
    vulkan->depthImage.imageFormat = VK_FORMAT_UNDEFINED;
}

static B32 vulkan_create_depth_image(RendererVulkan* vulkan, VkExtent3D extent) {
    if (!vulkan || vulkan->device.allocator == 0) {
        return 0;
    }

    if (vulkan->depthImage.image != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan->device.device, vulkan->depthImage.imageView, 0);
        vmaDestroyImage(vulkan->device.allocator, vulkan->depthImage.image, vulkan->depthImage.allocation);
        vulkan_reset_depth_image(vulkan);
    }

    vulkan->depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    vulkan->depthImage.imageExtent = extent;

    VkImageUsageFlags depthImageUsages = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info = vulkan_image_create_info(vulkan->depthImage.imageFormat, depthImageUsages, extent);

    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkResult depthImageResult = vmaCreateImage(vulkan->device.allocator,
                                               &dimg_info,
                                               &dimg_allocinfo,
                                               &vulkan->depthImage.image,
                                               &vulkan->depthImage.allocation,
                                               nullptr);
    if (depthImageResult != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create depth image: {}", depthImageResult);
        vulkan_reset_depth_image(vulkan);
        return 0;
    }

    VkImageViewCreateInfo dview_info = vulkan_image_view_create_info(vulkan->depthImage.imageFormat,
                                                                     vulkan->depthImage.image,
                                                                     VK_IMAGE_ASPECT_DEPTH_BIT);

    VkResult depthImageViewResult = vkCreateImageView(vulkan->device.device,
                                                      &dview_info,
                                                      nullptr,
                                                      &vulkan->depthImage.imageView);
    if (depthImageViewResult != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create depth image view: {}", depthImageViewResult);
        vmaDestroyImage(vulkan->device.allocator, vulkan->depthImage.image, vulkan->depthImage.allocation);
        vulkan_reset_depth_image(vulkan);
        return 0;
    }

    VmaAllocationInfo allocInfo = {};
    vmaGetAllocationInfo(vulkan->device.allocator, vulkan->depthImage.allocation, &allocInfo);
    LOG_INFO(VULKAN_LOG_DOMAIN,
             "Created depth image: extent={}x{}, alloc={} MB",
             extent.width,
             extent.height,
             (allocInfo.size / MB(1)));
    vulkan_log_allocator_stats_(vulkan, "after depth image");

    return 1;
}

static void vulkan_destroy_depth_image(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    if (vulkan->depthImage.image != VK_NULL_HANDLE) {
        if (vulkan->depthImage.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan->device.device, vulkan->depthImage.imageView, 0);
        }
        vmaDestroyImage(vulkan->device.allocator, vulkan->depthImage.image, vulkan->depthImage.allocation);
        vulkan_reset_depth_image(vulkan);
    }
}

// ////////////////////////
// Shadow Map Management

static void vulkan_reset_shadow_map(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    vulkan->shadowMap.image = VK_NULL_HANDLE;
    vulkan->shadowMap.imageView = VK_NULL_HANDLE;
    vulkan->shadowMap.allocation = 0;
    vulkan->shadowMap.imageExtent = {};
    vulkan->shadowMap.imageFormat = VK_FORMAT_UNDEFINED;
}

static B32 vulkan_create_shadow_map(RendererVulkan* vulkan, U32 resolution) {
    if (!vulkan || vulkan->device.allocator == 0) {
        return 0;
    }

    if (vulkan->shadowMap.image != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan->device.device, vulkan->shadowMap.imageView, 0);
        vmaDestroyImage(vulkan->device.allocator, vulkan->shadowMap.image, vulkan->shadowMap.allocation);
        vulkan_reset_shadow_map(vulkan);
    }

    vulkan->shadowMapResolution = resolution;
    vulkan->shadowMap.imageFormat = VK_FORMAT_D32_SFLOAT;
    vulkan->shadowMap.imageExtent = {resolution, resolution, 1};

    VkImageUsageFlags shadowMapUsages = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo imgInfo = vulkan_image_create_info(vulkan->shadowMap.imageFormat, shadowMapUsages,
                                                          vulkan->shadowMap.imageExtent);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkResult result = vmaCreateImage(vulkan->device.allocator, &imgInfo, &allocInfo,
                                     &vulkan->shadowMap.image, &vulkan->shadowMap.allocation, nullptr);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create shadow map: {}", result);
        vulkan_reset_shadow_map(vulkan);
        return 0;
    }

    VkImageViewCreateInfo viewInfo = vulkan_image_view_create_info(vulkan->shadowMap.imageFormat,
                                                                    vulkan->shadowMap.image,
                                                                    VK_IMAGE_ASPECT_DEPTH_BIT);

    result = vkCreateImageView(vulkan->device.device, &viewInfo, nullptr, &vulkan->shadowMap.imageView);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create shadow map view: {}", result);
        vmaDestroyImage(vulkan->device.allocator, vulkan->shadowMap.image, vulkan->shadowMap.allocation);
        vulkan_reset_shadow_map(vulkan);
        return 0;
    }

    VmaAllocationInfo allocStats = {};
    vmaGetAllocationInfo(vulkan->device.allocator, vulkan->shadowMap.allocation, &allocStats);
    LOG_INFO(VULKAN_LOG_DOMAIN,
             "Created {}x{} shadow map (alloc={} MB)",
             resolution,
             resolution,
             (allocStats.size / MB(1)));
    vulkan_log_allocator_stats_(vulkan, "after shadow map");
    return 1;
}

static void vulkan_destroy_shadow_map(RendererVulkan* vulkan) {
    if (!vulkan) {
        return;
    }
    if (vulkan->shadowMap.image != VK_NULL_HANDLE) {
        if (vulkan->shadowMap.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan->device.device, vulkan->shadowMap.imageView, 0);
        }
        vmaDestroyImage(vulkan->device.allocator, vulkan->shadowMap.image, vulkan->shadowMap.allocation);
        vulkan_reset_shadow_map(vulkan);
    }
}

static VkSampler vulkan_create_shadow_sampler(VulkanDevice* device) {
    if (!device || device->device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(device->device, &samplerInfo, 0, &sampler);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create shadow sampler: {}", result);
        return VK_NULL_HANDLE;
    }

    return sampler;
}

// ////////////////////////
// Texture Image Creation

static RendererVulkanAllocatedImage vulkan_create_image(RendererVulkan* vulkan,
                                                         VkExtent3D size,
                                                         VkFormat format,
                                                         VkImageUsageFlags usage) {
    RendererVulkanAllocatedImage newImage = {};
    if (!vulkan || vulkan->device.allocator == 0) {
        return newImage;
    }

    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo imgInfo = vulkan_image_create_info(format, usage, size);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkResult result = vmaCreateImage(vulkan->device.allocator,
                                     &imgInfo,
                                     &allocInfo,
                                     &newImage.image,
                                     &newImage.allocation,
                                     0);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create image: {}", result);
        return newImage;
    }

    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo viewInfo = vulkan_image_view_create_info(format, newImage.image, aspectFlag);

    result = vkCreateImageView(vulkan->device.device, &viewInfo, 0, &newImage.imageView);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create image view: {}", result);
        vmaDestroyImage(vulkan->device.allocator, newImage.image, newImage.allocation);
        newImage.image = VK_NULL_HANDLE;
        newImage.allocation = 0;
        return newImage;
    }

    return newImage;
}

static RendererVulkanAllocatedImage vulkan_create_image_data(RendererVulkan* vulkan,
                                                              void* pixels,
                                                              VkExtent3D size,
                                                              VkFormat format,
                                                              VkImageUsageFlags usage) {
    RendererVulkanAllocatedImage newImage = {};
    if (!vulkan || !pixels) {
        return newImage;
    }

    U64 dataSize = (U64)size.width * (U64)size.height * (U64)size.depth * 4;

    RendererVulkanAllocatedBuffer stagingBuffer = vulkan_create_buffer(
        vulkan->device.allocator,
        dataSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    MEMMOVE(stagingBuffer.info.pMappedData, pixels, dataSize);

    newImage = vulkan_create_image(vulkan,
                                   size,
                                   format,
                                   usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    if (newImage.image == VK_NULL_HANDLE) {
        vulkan_destroy_buffer(vulkan->device.allocator, &stagingBuffer);
        return newImage;
    }

    VkCommandBuffer cmd = vulkan_immediate_begin(vulkan, &vulkan->immSubmit);
    if (cmd != VK_NULL_HANDLE) {
        vulkan_transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;

        vkCmdCopyBufferToImage(cmd,
                               stagingBuffer.buffer,
                               newImage.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &copyRegion);

        vulkan_transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vulkan_immediate_end(vulkan, &vulkan->immSubmit);
    }

    vulkan_destroy_buffer(vulkan->device.allocator, &stagingBuffer);

    return newImage;
}

static void vulkan_destroy_image(RendererVulkan* vulkan, RendererVulkanAllocatedImage* img) {
    if (!vulkan || !img) {
        return;
    }
    if (img->imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan->device.device, img->imageView, 0);
    }
    if (img->image != VK_NULL_HANDLE) {
        vmaDestroyImage(vulkan->device.allocator, img->image, img->allocation);
    }
    img->image = VK_NULL_HANDLE;
    img->imageView = VK_NULL_HANDLE;
    img->allocation = 0;
}

static VkSampler vulkan_create_sampler(VulkanDevice* device, VkFilter filter) {
    if (!device || device->device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(device->device, &samplerInfo, 0, &sampler);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create sampler: {}", result);
        return VK_NULL_HANDLE;
    }

    return sampler;
}

// ////////////////////////
// Mesh Upload

static GPUMeshBuffers vulkan_upload_mesh(RendererVulkan* vulkan,
                                         const U32* indices, U32 indexCount,
                                         const Vertex* vertices, U32 vertexCount) {
    GPUMeshBuffers newMesh = {};
    if (!vulkan || !indices || !vertices || indexCount == 0 || vertexCount == 0) {
        return newMesh;
    }

    U64 vertexBufferSize = vertexCount * sizeof(Vertex);
    U64 indexBufferSize = indexCount * sizeof(U32);

    newMesh.vertexBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                                vertexBufferSize,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                VMA_MEMORY_USAGE_GPU_ONLY);

    VkBufferDeviceAddressInfo deviceAddressInfo = {};
    deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAddressInfo.buffer = newMesh.vertexBuffer.buffer;
    newMesh.vertexBufferAddress = vkGetBufferDeviceAddress(vulkan->device.device, &deviceAddressInfo);

    newMesh.indexBuffer = vulkan_create_buffer(vulkan->device.allocator,
                                               indexBufferSize,
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                               VMA_MEMORY_USAGE_GPU_ONLY);

    RendererVulkanAllocatedBuffer staging = vulkan_create_buffer(vulkan->device.allocator,
                                                                 vertexBufferSize + indexBufferSize,
                                                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                                 VMA_MEMORY_USAGE_CPU_ONLY);

    void* data = staging.info.pMappedData;
    MEMMOVE(data, vertices, vertexBufferSize);
    MEMMOVE((U8*)data + vertexBufferSize, indices, indexBufferSize);

    VkCommandBuffer cmd = vulkan_immediate_begin(vulkan, &vulkan->immSubmit);
    if (cmd != VK_NULL_HANDLE) {
        VkBufferCopy vertexCopy = {};
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, newMesh.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy = {};
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, newMesh.indexBuffer.buffer, 1, &indexCopy);
        
        vulkan_immediate_end(vulkan, &vulkan->immSubmit);
    }

    vulkan_destroy_buffer(vulkan->device.allocator, &staging);

    return newMesh;
}

static void vulkan_destroy_mesh(RendererVulkan* vulkan, GPUMeshBuffers* mesh) {
    if (!vulkan || !mesh) {
        return;
    }

    vkdefer_destroy_VmaBuffer(&vulkan->deferCtx.globalBuf, mesh->vertexBuffer.buffer, mesh->vertexBuffer.allocation);
    vkdefer_destroy_VmaBuffer(&vulkan->deferCtx.globalBuf, mesh->indexBuffer.buffer, mesh->indexBuffer.allocation);
    
    mesh->vertexBuffer.buffer = VK_NULL_HANDLE;
    mesh->vertexBuffer.allocation = VK_NULL_HANDLE;
    mesh->indexBuffer.buffer = VK_NULL_HANDLE;
    mesh->indexBuffer.allocation = VK_NULL_HANDLE;
    mesh->vertexBufferAddress = 0;
}
