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
    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                        ? VK_IMAGE_ASPECT_DEPTH_BIT
                                        : VK_IMAGE_ASPECT_COLOR_BIT;
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
