//
// Created by André Leite on 03/11/2025.
//

// External dependencies (Forward declarations)
static B32 vulkan_create_draw_image(RendererVulkan* vulkan, VkExtent3D extent);
static B32 vulkan_update_draw_pipeline(RendererVulkan* vulkan);
// Defined in renderer_vulkan_imgui.cpp (included via renderer_vulkan.cpp)
// static void renderer_vulkan_imgui_on_swapchain_updated(RendererVulkan* vulkan);
static VkImageViewCreateInfo vulkan_image_view_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
static VkSemaphoreCreateInfo vulkan_semaphore_create_info(VkSemaphoreCreateFlags flags);

// ////////////////////////
// Surface

static B32 vulkan_create_surface(OS_WindowHandle window, VulkanDevice* device, VkSurfaceKHR* outSurface) {
    if (!device) {
        return 0;
    }

    if (*outSurface != VK_NULL_HANDLE) {
        return 1;
    }

    if (device->instance == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Cannot create surface without a Vulkan instance");
        return 0;
    }

    OS_WindowSurfaceInfo surfaceInfo = OS_window_get_surface_info(window);
    if (!surfaceInfo.metalLayerPtr) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Window does not expose a Metal layer for surface creation");
        return 0;
    }

#if defined(PLATFORM_OS_MACOS)
    PFN_vkCreateMetalSurfaceEXT createSurface =
            (PFN_vkCreateMetalSurfaceEXT) vkGetInstanceProcAddr(device->instance, "vkCreateMetalSurfaceEXT");

    if (!createSurface) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "vkCreateMetalSurfaceEXT is not available");
        return 0;
    }

    VkMetalSurfaceCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    createInfo.pLayer = (const CAMetalLayer*) surfaceInfo.metalLayerPtr;

    VK_CHECK(createSurface(device->instance, &createInfo, 0, outSurface));
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created Metal surface");
    return 1;
#else
    (void) surfaceInfo;
    LOG_ERROR(VULKAN_LOG_DOMAIN, "Surface creation not implemented for this platform");
    return 0;
#endif
}

static void vulkan_destroy_surface(VulkanDevice* device, VkSurfaceKHR* surface) {
    if (!device || *surface == VK_NULL_HANDLE) {
        return;
    }

    if (device->instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(device->instance, *surface, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Surface destroyed");
    }

    *surface = VK_NULL_HANDLE;
}

// ////////////////////////
// Swapchain Selection Helpers

static VkSurfaceFormatKHR vulkan_choose_surface_format(const VkSurfaceFormatKHR* formats,
                                                       U32 count) {
    VkSurfaceFormatKHR desired = {};
    desired.format = VK_FORMAT_B8G8R8A8_SRGB;
    desired.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkSurfaceFormatKHR fallback = formats[0];

    for (U32 i = 0; i < count; ++i) {
        if (formats[i].format == desired.format &&
            formats[i].colorSpace == desired.colorSpace) {
            return formats[i];
        }
    }

    return fallback;
}

static VkPresentModeKHR vulkan_choose_present_mode(const VkPresentModeKHR* modes, U32 count) {
#if defined(PLATFORM_BUILD_DEBUG)
    VkPresentModeKHR desired = VK_PRESENT_MODE_FIFO_KHR;
#else
    VkPresentModeKHR desired = VK_PRESENT_MODE_MAILBOX_KHR;
#endif
    VkPresentModeKHR fallback = VK_PRESENT_MODE_FIFO_KHR;

    for (U32 i = 0; i < count; ++i) {
        if (modes[i] == desired) {
            return desired;
        }
    }

    return fallback;
}

static VkExtent2D vulkan_choose_extent(const VkSurfaceCapabilitiesKHR* capabilities) {
    VkExtent2D current = capabilities->currentExtent;
    if (current.width != UINT32_MAX && current.height != UINT32_MAX) {
        return current;
    }

    VkExtent2D clamped = capabilities->minImageExtent;

    if (capabilities->maxImageExtent.width > 0 &&
        clamped.width > capabilities->maxImageExtent.width) {
        clamped.width = capabilities->maxImageExtent.width;
    }
    if (capabilities->maxImageExtent.height > 0 &&
        clamped.height > capabilities->maxImageExtent.height) {
        clamped.height = capabilities->maxImageExtent.height;
    }

    if (clamped.width == 0) {
        clamped.width = 640u;
    }
    if (clamped.height == 0) {
        clamped.height = 480u;
    }

    return clamped;
}

// ////////////////////////
// Swapchain

static void vulkan_destroy_swapchain(VulkanSwapchain* swapchain, VkDevice device) {
    if (!swapchain || device == VK_NULL_HANDLE) {
        return;
    }

    if (swapchain->images) {
        for (U32 i = 0; i < swapchain->imageCount; ++i) {
            if (swapchain->images[i].view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, swapchain->images[i].view, 0);
                swapchain->images[i].view = VK_NULL_HANDLE;
            }
            swapchain->images[i].handle = VK_NULL_HANDLE;
            swapchain->images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    if (swapchain->imageSemaphores) {
        for (U32 i = 0; i < swapchain->imageCount; ++i) {
            if (swapchain->imageSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, swapchain->imageSemaphores[i], 0);
                swapchain->imageSemaphores[i] = VK_NULL_HANDLE;
            }
        }
    }

    if (swapchain->handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain->handle, 0);
        swapchain->handle = VK_NULL_HANDLE;
    }

    swapchain->imageCount = 0u;
    swapchain->extent = {};
    swapchain->format = {};

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Swapchain destroyed");
}

static B32 vulkan_create_swapchain(RendererVulkan* vulkan, OS_WindowHandle window) {
    if (!vulkan_create_surface(window, &vulkan->device, &vulkan->swapchain.surface)) {
        return 0;
    }

    VkBool32 presentSupported = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(vulkan->device.physicalDevice,
                                                  vulkan->device.graphicsQueueFamilyIndex,
                                                  vulkan->swapchain.surface,
                                                  &presentSupported));
    if (!presentSupported) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Selected queue family does not support presentation");
        return 0;
    }

    VkSurfaceCapabilitiesKHR capabilities = {};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->device.physicalDevice,
                                                       vulkan->swapchain.surface,
                                                       &capabilities));

    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));

    U32 formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->device.physicalDevice,
                                                  vulkan->swapchain.surface,
                                                  &formatCount,
                                                  0));
    if (formatCount == 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "No surface formats available");
        return 0;
    }

    VkSurfaceFormatKHR* formats = ARENA_PUSH_ARRAY(scratch.arena, VkSurfaceFormatKHR, formatCount);
    if (!formats) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate surface formats array");
        return 0;
    }
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->device.physicalDevice,
                                                  vulkan->swapchain.surface,
                                                  &formatCount,
                                                  formats));
    VkSurfaceFormatKHR surfaceFormat = vulkan_choose_surface_format(formats, formatCount);

    U32 presentModeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->device.physicalDevice,
                                                       vulkan->swapchain.surface,
                                                       &presentModeCount,
                                                       0));
    if (presentModeCount == 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "No present modes available");
        return 0;
    }

    VkPresentModeKHR* presentModes = ARENA_PUSH_ARRAY(scratch.arena, VkPresentModeKHR, presentModeCount);
    if (!presentModes) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate present modes array");
        return 0;
    }
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->device.physicalDevice,
                                                       vulkan->swapchain.surface,
                                                       &presentModeCount,
                                                       presentModes));
    VkPresentModeKHR presentMode = vulkan_choose_present_mode(presentModes, presentModeCount);

    VkExtent2D extent = vulkan_choose_extent(&capabilities);

    U32 imageCount = capabilities.minImageCount + 1u;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    if (imageCount < VULKAN_FRAME_OVERLAP) {
        imageCount = VULKAN_FRAME_OVERLAP;
    }

    VkSwapchainKHR oldSwapchain = vulkan->swapchain.handle;
    U32 oldImageCount = vulkan->swapchain.imageCount;

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vulkan->swapchain.surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = 0;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(vulkan->device.device, &createInfo, 0, &newSwapchain));

    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan->device.device, oldSwapchain, 0);

        if (vulkan->swapchain.images) {
            for (U32 i = 0; i < vulkan->swapchain.imageCount; ++i) {
                if (vulkan->swapchain.images[i].view != VK_NULL_HANDLE) {
                    vkDestroyImageView(vulkan->device.device, vulkan->swapchain.images[i].view, 0);
                    vulkan->swapchain.images[i].view = VK_NULL_HANDLE;
                }
            }
        }

        if (vulkan->swapchain.imageSemaphores) {
            for (U32 i = 0; i < vulkan->swapchain.imageCount; ++i) {
                if (vulkan->swapchain.imageSemaphores[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(vulkan->device.device, vulkan->swapchain.imageSemaphores[i], 0);
                    vulkan->swapchain.imageSemaphores[i] = VK_NULL_HANDLE;
                }
            }
        }
    }

    vulkan->swapchain.handle = newSwapchain;
    

    U32 retrievedImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(vulkan->device.device, vulkan->swapchain.handle, &retrievedImageCount, 0));
    if (retrievedImageCount == 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Swapchain returned no images");
        return 0;
    }

    if (vulkan->swapchain.imageCapacity < retrievedImageCount) {
        vulkan->swapchain.images = ARENA_PUSH_ARRAY(vulkan->arena, RendererVulkanSwapchainImage, retrievedImageCount);
        if (!vulkan->swapchain.images) {
            return 0;
        }
        vulkan->swapchain.imageSemaphores = ARENA_PUSH_ARRAY(vulkan->arena, VkSemaphore, retrievedImageCount);
        if (!vulkan->swapchain.imageSemaphores) {
            return 0;
        }
        vulkan->swapchain.imageCapacity = retrievedImageCount;
    }

    VkImage* rawImages = ARENA_PUSH_ARRAY(scratch.arena, VkImage, retrievedImageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(vulkan->device.device, vulkan->swapchain.handle, &retrievedImageCount, rawImages));

    VkSemaphoreCreateInfo semaphoreInfo = vulkan_semaphore_create_info(0);

    for (U32 i = 0; i < retrievedImageCount; ++i) {
        vulkan->swapchain.images[i].handle = rawImages[i];
        vulkan->swapchain.images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;

        // Create semaphore if needed (reusing slot or new)
        // If we didn't destroy them, we might leak if we overwrite?
        // Original code destroyed them. So we should create new ones.
        if (vulkan->swapchain.imageSemaphores[i] == VK_NULL_HANDLE) {
             VK_CHECK(vkCreateSemaphore(vulkan->device.device, &semaphoreInfo, 0, &vulkan->swapchain.imageSemaphores[i]));
        }

        VkImageViewCreateInfo viewInfo = vulkan_image_view_create_info(surfaceFormat.format, rawImages[i],
                                                                       VK_IMAGE_ASPECT_COLOR_BIT);

        VK_CHECK(vkCreateImageView(vulkan->device.device, &viewInfo, 0, &vulkan->swapchain.images[i].view));
    }

    vulkan->swapchain.imageCount = retrievedImageCount;
    vulkan->swapchain.format = surfaceFormat;
    vulkan->swapchain.extent = extent;

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Swapchain created with {} images ({}x{})",
              retrievedImageCount, extent.width, extent.height);

    VkExtent3D drawImageExtent = {
        extent.width,
        extent.height,
        1
    };

    if (!vulkan_create_draw_image(vulkan, drawImageExtent)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create draw image");
        return 0;
    }
    
    if (!vulkan_update_draw_pipeline(vulkan)) {
         LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to update draw pipeline descriptors for swapchain image");
         return 0;
    }
    
    renderer_vulkan_imgui_on_swapchain_updated(vulkan);

    return 1;
}
