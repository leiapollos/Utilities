//
// Created by André Leite on 03/11/2025.
//

// ////////////////////////
// Lifetime

B32 renderer_init(Arena* arena, Renderer* renderer) {
    if (!arena || !renderer) {
        ASSERT_ALWAYS(false && "Invalid arguments");
        return 0;
    }

    RendererVulkan* vulkan = ARENA_PUSH_STRUCT(arena, RendererVulkan);
    if (!vulkan) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate RendererVulkan");
        return 0;
    }

    vulkan->arena = arena;
    vulkan->instance = VK_NULL_HANDLE;
    vulkan->physicalDevice = VK_NULL_HANDLE;
    vulkan->device = VK_NULL_HANDLE;
    vulkan->debugMessenger = VK_NULL_HANDLE;
    vulkan->graphicsQueue = VK_NULL_HANDLE;
    vulkan->graphicsQueueFamilyIndex = 0u;
    vulkan->validationLayersEnabled = 0;
    vulkan->surface = VK_NULL_HANDLE;
    vulkan->swapchain.handle = VK_NULL_HANDLE;
    vulkan->swapchain.images = 0;
    vulkan->swapchain.imageSemaphores = 0;
    vulkan->swapchain.imageCount = 0u;
    vulkan->swapchain.imageCapacity = 0u;
    vulkan->swapchain.format = {};
    vulkan->swapchain.extent = {};
    vulkan->swapchainImageIndex = 0u;
    vulkan->currentFrameIndex = 0u;
    
    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        vulkan->frames[i].commandPool = VK_NULL_HANDLE;
        vulkan->frames[i].commandBuffer = VK_NULL_HANDLE;
        vulkan->frames[i].swapchainSemaphore = VK_NULL_HANDLE;
        vulkan->frames[i].renderFence = VK_NULL_HANDLE;
        vulkan->frames[i].imageIndex = 0u;
    }

    if (!vulkan_create_instance(arena, vulkan)) {
        ASSERT_ALWAYS(false && "Failed to create Vulkan instance");
    }

    if (!vulkan_create_device(arena, vulkan)) {
        ASSERT_ALWAYS(false && "Failed to create Vulkan device");
    }

    if (!vulkan_init_device_queues(vulkan)) {
        ASSERT_ALWAYS(false && "Failed to initialize Vulkan queues");
    }

    if (!vulkan_create_frames(vulkan)) {
        ASSERT_ALWAYS(false && "Failed to create Vulkan frames");
    }

    LOG_INFO(VULKAN_LOG_DOMAIN, "Vulkan renderer initialized successfully");

    renderer->backendData = vulkan;
    return 1;
}

void renderer_shutdown(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return;
    }

    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    
    if (vulkan->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan->device);
    }
    
    vulkan_destroy_swapchain(vulkan);
    vulkan_destroy_frames(vulkan);
    vulkan_destroy_device(vulkan);
    vulkan_destroy_debug_messenger(vulkan);
    vulkan_destroy_surface(vulkan);
    vulkan_destroy_instance(vulkan);

    renderer->backendData = 0;
}

// ////////////////////////
// Validation Layers

static B32 vulkan_check_validation_layer_support(Arena* arena) {
    U32 layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, 0);

    if (layerCount == 0) {
        return 0;
    }

    Temp temp = temp_begin(arena);
    DEFER_REF(temp_end(&temp));

    VkLayerProperties* availableLayers = ARENA_PUSH_ARRAY(arena, VkLayerProperties, layerCount);
    if (!availableLayers) {
        return 0;
    }

    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    U32 validationLayerCount = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
    B32 allFound = 1;
    for (U32 i = 0; i < validationLayerCount; ++i) {
        StringU8 desiredLayer = str8(VALIDATION_LAYERS[i]);
        B32 layerFound = 0;
        for (U32 j = 0; j < layerCount; ++j) {
            StringU8 availableLayer = str8(availableLayers[j].layerName);
            if (str8_equal(desiredLayer, availableLayer)) {
                layerFound = 1;
                break;
            }
        }
        if (!layerFound) {
            allFound = 0;
            break;
        }
    }

    return allFound;
}

// ////////////////////////
// Extension Support

static B32 vulkan_check_extension_support(Arena* arena, const char* extensionName) {
    U32 extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(0, &extensionCount, 0);

    if (extensionCount == 0) {
        return 0;
    }

    Temp temp = temp_begin(arena);
    DEFER_REF(temp_end(&temp));

    VkExtensionProperties* extensions = ARENA_PUSH_ARRAY(arena, VkExtensionProperties, extensionCount);
    if (!extensions) {
        return 0;
    }

    vkEnumerateInstanceExtensionProperties(0, &extensionCount, extensions);

    StringU8 desiredExtension = str8(extensionName);
    B32 found = 0;
    for (U32 i = 0; i < extensionCount; ++i) {
        StringU8 availableExtension = str8(extensions[i].extensionName);
        if (str8_equal(desiredExtension, availableExtension)) {
            found = 1;
            break;
        }
    }

    return found;
}

// ////////////////////////
// Instance

static B32 vulkan_create_instance(Arena* arena, RendererVulkan* vulkan) {
    if (ENABLE_VALIDATION_LAYERS && !vulkan_check_validation_layer_support(arena)) {
        LOG_WARNING(VULKAN_LOG_DOMAIN, "Validation layers requested but not available");
        vulkan->validationLayersEnabled = 0;
    } else {
        vulkan->validationLayersEnabled = ENABLE_VALIDATION_LAYERS ? 1 : 0;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Utilities";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    const char* DESIRED_EXTENSIONS[] = {
#if defined(PLATFORM_OS_MACOS)
        "VK_KHR_portability_enumeration",
        "VK_EXT_metal_surface",
#endif
        "VK_KHR_surface",
        "VK_EXT_debug_utils",
    };
    const U32 DESIRED_EXTENSION_COUNT = sizeof(DESIRED_EXTENSIONS) / sizeof(DESIRED_EXTENSIONS[0]);

    const char* enabledExtensions[16] = {};
    U32 enabledExtensionCount = 0;

    U32 availableExtensionCount = 0;
    vkEnumerateInstanceExtensionProperties(0, &availableExtensionCount, 0);
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Found {} available instance extensions", availableExtensionCount);

    for (U32 i = 0; i < DESIRED_EXTENSION_COUNT; ++i) {
        if (vulkan_check_extension_support(arena, DESIRED_EXTENSIONS[i])) {
            enabledExtensions[enabledExtensionCount] = DESIRED_EXTENSIONS[i];
            enabledExtensionCount += 1;
            LOG_DEBUG(VULKAN_LOG_DOMAIN, "Extension '{}' is available", DESIRED_EXTENSIONS[i]);
        } else {
            LOG_WARNING(VULKAN_LOG_DOMAIN, "Extension '{}' is not available", DESIRED_EXTENSIONS[i]);
        }
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if defined(PLATFORM_OS_MACOS)
    createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = enabledExtensionCount;
    createInfo.ppEnabledExtensionNames = (enabledExtensionCount > 0) ? enabledExtensions : 0;

    if (vulkan->validationLayersEnabled) {
        createInfo.enabledLayerCount = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
        createInfo.pNext = 0;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = 0;
    }

    VK_CHECK(vkCreateInstance(&createInfo, 0, &vulkan->instance));
    
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan instance created successfully");
    
    if (!vulkan_create_debug_messenger(arena, vulkan)) {
        LOG_WARNING(VULKAN_LOG_DOMAIN, "Failed to create debug messenger, continuing without validation logging");
    }
    
    return 1;
}

static void vulkan_destroy_instance(RendererVulkan* vulkan) {
    if (vulkan->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(vulkan->instance, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan instance destroyed");
    }
}

// ////////////////////////
// Debug Messenger

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    (void) pUserData;
    
    StringU8 message = str8(pCallbackData->pMessage);
    
    if (FLAGS_HAS(messageSeverity, VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        LOG_ERROR("vulkan_validation", "{}", message);
    } else if (FLAGS_HAS(messageSeverity, VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) {
        LOG_WARNING("vulkan_validation", "{}", message);
    } else if (FLAGS_HAS(messageSeverity, VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)) {
        LOG_INFO("vulkan_validation", "{}", message);
    } else {
        LOG_DEBUG("vulkan_validation", "{}", message);
    }
    
    return VK_FALSE;
}

static B32 vulkan_create_debug_messenger(Arena* arena, RendererVulkan* vulkan) {
    if (!vulkan->validationLayersEnabled) {
        return 1;
    }
    
    PFN_vkCreateDebugUtilsMessengerEXT createDebugMessenger = 
        (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(vulkan->instance, "vkCreateDebugUtilsMessengerEXT");
    
    if (!createDebugMessenger) {
        LOG_WARNING(VULKAN_LOG_DOMAIN, "Debug utils messenger extension not available");
        return 0;
    }
    
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = vulkan_debug_callback;
    createInfo.pUserData = 0;
    
    VK_CHECK(createDebugMessenger(vulkan->instance, &createInfo, 0, &vulkan->debugMessenger));
    
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Debug messenger created successfully");
    return 1;
}

static void vulkan_destroy_debug_messenger(RendererVulkan* vulkan) {
    if (vulkan->debugMessenger == VK_NULL_HANDLE) {
        return;
    }
    
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger = 
        (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(vulkan->instance, "vkDestroyDebugUtilsMessengerEXT");
    
    if (destroyDebugMessenger) {
        destroyDebugMessenger(vulkan->instance, vulkan->debugMessenger, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Debug messenger destroyed");
    }
    
    vulkan->debugMessenger = VK_NULL_HANDLE;
}

// ////////////////////////
// Physical Device

static VkPhysicalDevice vulkan_select_physical_device(Arena* arena, VkInstance instance) {
    U32 deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, 0);
    
    if (deviceCount == 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "No physical devices found");
        return VK_NULL_HANDLE;
    }
    
    Temp temp = temp_begin(arena);
    DEFER_REF(temp_end(&temp));
    
    VkPhysicalDevice* devices = ARENA_PUSH_ARRAY(arena, VkPhysicalDevice, deviceCount);
    if (!devices) {
        return VK_NULL_HANDLE;
    }
    
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, devices));
    
    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
    VkPhysicalDevice fallbackDevice = devices[0];
    
    for (U32 i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selectedDevice = devices[i];
            LOG_DEBUG(VULKAN_LOG_DOMAIN, "Selected discrete GPU: {}", properties.deviceName);
            break;
        }
    }
    
    if (selectedDevice == VK_NULL_HANDLE) {
        selectedDevice = fallbackDevice;
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(selectedDevice, &properties);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Selected device: {}", properties.deviceName);
    }
    
    return selectedDevice;
}

// ////////////////////////
// Queue

static B32 vulkan_find_graphics_queue_family(Arena* arena, VkPhysicalDevice physicalDevice, U32* outIndex) {
    if (!outIndex) {
        return 0;
    }

    U32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, 0);
    
    if (queueFamilyCount == 0) {
        return 0;
    }
    
    Temp temp = temp_begin(arena);
    DEFER_REF(temp_end(&temp));
    
    VkQueueFamilyProperties* queueFamilies = ARENA_PUSH_ARRAY(arena, VkQueueFamilyProperties, queueFamilyCount);
    if (!queueFamilies) {
        return 0;
    }
    
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);
    
    for (U32 i = 0; i < queueFamilyCount; ++i) {
        if (FLAGS_HAS(queueFamilies[i].queueFlags, VK_QUEUE_GRAPHICS_BIT)) {
            *outIndex = i;
            return 1;
        }
    }
    
    return 0;
}

// ////////////////////////
// Device Extension Support

static B32 vulkan_check_device_extension_support(Arena* arena, VkPhysicalDevice physicalDevice, const char* extensionName) {
    U32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, 0);
    
    if (extensionCount == 0) {
        return 0;
    }
    
    Temp temp = temp_begin(arena);
    DEFER_REF(temp_end(&temp));
    
    VkExtensionProperties* extensions = ARENA_PUSH_ARRAY(arena, VkExtensionProperties, extensionCount);
    if (!extensions) {
        return 0;
    }
    
    vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, extensions);
    
    StringU8 desiredExtension = str8(extensionName);
    B32 found = 0;
    for (U32 i = 0; i < extensionCount; ++i) {
        StringU8 availableExtension = str8(extensions[i].extensionName);
        if (str8_equal(desiredExtension, availableExtension)) {
            found = 1;
            break;
        }
    }
    
    return found;
}

// ////////////////////////
// Device

static B32 vulkan_create_device(Arena* arena, RendererVulkan* vulkan) {
    vulkan->physicalDevice = vulkan_select_physical_device(arena, vulkan->instance);
    if (vulkan->physicalDevice == VK_NULL_HANDLE) {
        return 0;
    }
    
    U32 graphicsQueueFamilyIndex = 0u;
    if (!vulkan_find_graphics_queue_family(arena, vulkan->physicalDevice, &graphicsQueueFamilyIndex)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "No graphics queue family found");
        return 0;
    }
    
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Using graphics queue family {}", graphicsQueueFamilyIndex);

    vulkan->graphicsQueueFamilyIndex = graphicsQueueFamilyIndex;
    
    const char* DESIRED_DEVICE_EXTENSIONS[] = {
#if defined(PLATFORM_OS_MACOS)
        "VK_KHR_portability_subset",  // Required for MoltenVK
#endif
        "VK_KHR_swapchain",
        "VK_KHR_synchronization2",
    };
    const U32 DESIRED_DEVICE_EXTENSION_COUNT = sizeof(DESIRED_DEVICE_EXTENSIONS) / sizeof(DESIRED_DEVICE_EXTENSIONS[0]);
    
    const char* enabledDeviceExtensions[16] = {};
    U32 enabledDeviceExtensionCount = 0;
    
    for (U32 i = 0; i < DESIRED_DEVICE_EXTENSION_COUNT; ++i) {
        if (vulkan_check_device_extension_support(arena, vulkan->physicalDevice, DESIRED_DEVICE_EXTENSIONS[i])) {
            enabledDeviceExtensions[enabledDeviceExtensionCount] = DESIRED_DEVICE_EXTENSIONS[i];
            enabledDeviceExtensionCount += 1;
            LOG_DEBUG(VULKAN_LOG_DOMAIN, "Device extension '{}' is available", DESIRED_DEVICE_EXTENSIONS[i]);
        } else {
            LOG_WARNING(VULKAN_LOG_DOMAIN, "Device extension '{}' is not available", DESIRED_DEVICE_EXTENSIONS[i]);
        }
    }
    
    F32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = vulkan->graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    
    VkPhysicalDeviceFeatures deviceFeatures = {};
    
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = {};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2Features.pNext = 0;
    sync2Features.synchronization2 = VK_TRUE;
    
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &sync2Features;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = enabledDeviceExtensionCount;
    createInfo.ppEnabledExtensionNames = (enabledDeviceExtensionCount > 0) ? enabledDeviceExtensions : 0;
    
    if (vulkan->validationLayersEnabled) {
        createInfo.enabledLayerCount = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
    } else {
        createInfo.enabledLayerCount = 0;
    }
    
    VK_CHECK(vkCreateDevice(vulkan->physicalDevice, &createInfo, 0, &vulkan->device));

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan device created successfully");
    return 1;
}

static B32 vulkan_init_device_queues(RendererVulkan* vulkan) {
    if (!vulkan) {
        return 0;
    }

    if (vulkan->device == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Cannot initialize queues without a logical device");
        return 0;
    }

    const U32 queueIndex = 0u;
    vkGetDeviceQueue(vulkan->device, vulkan->graphicsQueueFamilyIndex, queueIndex, &vulkan->graphicsQueue);

    if (vulkan->graphicsQueue == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to retrieve graphics queue (family {}, index {})", vulkan->graphicsQueueFamilyIndex, queueIndex);
        return 0;
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Graphics queue initialized (family {}, index {})", vulkan->graphicsQueueFamilyIndex, queueIndex);
    return 1;
}

static void vulkan_destroy_device(RendererVulkan* vulkan) {
    if (vulkan->device != VK_NULL_HANDLE) {
        vkDestroyDevice(vulkan->device, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan device destroyed");
        vulkan->device = VK_NULL_HANDLE;
    }
    vulkan->graphicsQueue = VK_NULL_HANDLE;
    vulkan->graphicsQueueFamilyIndex = 0u;
    vulkan->physicalDevice = VK_NULL_HANDLE;
}

// ////////////////////////
// Frames

static B32 vulkan_create_frames(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device == VK_NULL_HANDLE) {
        return 0;
    }

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &vulkan->frames[i];

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = vulkan->graphicsQueueFamilyIndex;

        VK_CHECK(vkCreateCommandPool(vulkan->device, &poolInfo, 0, &frame->commandPool));

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame->commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(vulkan->device, &allocInfo, &frame->commandBuffer));

        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created frame {} command pool and command buffer", i);
    }

    if (!vulkan_create_sync_structures(vulkan)) {
        return 0;
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created {} frames for frame overlap", VULKAN_FRAME_OVERLAP);
    return 1;
}

static B32 vulkan_create_surface(OS_WindowHandle window, RendererVulkan* vulkan) {
    if (!vulkan) {
        return 0;
    }

    if (vulkan->surface != VK_NULL_HANDLE) {
        return 1;
    }

    if (vulkan->instance == VK_NULL_HANDLE) {
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
        (PFN_vkCreateMetalSurfaceEXT) vkGetInstanceProcAddr(vulkan->instance, "vkCreateMetalSurfaceEXT");

    if (!createSurface) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "vkCreateMetalSurfaceEXT is not available");
        return 0;
    }

    VkMetalSurfaceCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    createInfo.pLayer = (const CAMetalLayer*) surfaceInfo.metalLayerPtr;

    VK_CHECK(createSurface(vulkan->instance, &createInfo, 0, &vulkan->surface));
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created Metal surface");
    return 1;
#else
    (void) surfaceInfo;
    LOG_ERROR(VULKAN_LOG_DOMAIN, "Surface creation not implemented for this platform");
    return 0;
#endif
}

static void vulkan_destroy_surface(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->surface == VK_NULL_HANDLE) {
        return;
    }

    if (vulkan->instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vulkan->instance, vulkan->surface, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Surface destroyed");
    }

    vulkan->surface = VK_NULL_HANDLE;
}

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

static VkSemaphoreCreateInfo vulkan_semaphore_create_info(VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.flags = flags;
    return createInfo;
}

static VkFenceCreateInfo vulkan_fence_create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = flags;
    return createInfo;
}

static B32 vulkan_create_sync_structures(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device == VK_NULL_HANDLE) {
        return 0;
    }

    VkSemaphoreCreateInfo semaphoreInfo = vulkan_semaphore_create_info(0);

    VkFenceCreateInfo fenceInfo = vulkan_fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &vulkan->frames[i];

        VK_CHECK(vkCreateSemaphore(vulkan->device, &semaphoreInfo, 0, &frame->swapchainSemaphore));
        VK_CHECK(vkCreateFence(vulkan->device, &fenceInfo, 0, &frame->renderFence));

        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created sync structures for frame {}", i);
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created sync structures for all frames");
    return 1;
}

static B32 vulkan_create_swapchain(RendererVulkan* vulkan, OS_WindowHandle window) {
    if (!vulkan) {
        return 0;
    }

    if (vulkan->device == VK_NULL_HANDLE || vulkan->physicalDevice == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Cannot create swapchain without logical and physical devices");
        return 0;
    }

    if (!vulkan_create_surface(window, vulkan)) {
        return 0;
    }

    VkBool32 presentSupported = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(vulkan->physicalDevice,
                                                  vulkan->graphicsQueueFamilyIndex,
                                                  vulkan->surface,
                                                  &presentSupported));
    if (!presentSupported) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Selected queue family does not support presentation");
        return 0;
    }

    VkSurfaceCapabilitiesKHR capabilities = {};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->physicalDevice,
                                                       vulkan->surface,
                                                       &capabilities));

    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));

    U32 formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->physicalDevice,
                                                  vulkan->surface,
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
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->physicalDevice,
                                                  vulkan->surface,
                                                  &formatCount,
                                                  formats));
    VkSurfaceFormatKHR surfaceFormat = vulkan_choose_surface_format(formats, formatCount);

    U32 presentModeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->physicalDevice,
                                                       vulkan->surface,
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
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->physicalDevice,
                                                       vulkan->surface,
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
    createInfo.surface = vulkan->surface;
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

    VK_CHECK(vkCreateSwapchainKHR(vulkan->device, &createInfo, 0, &vulkan->swapchain.handle));

    if (oldSwapchain != VK_NULL_HANDLE) {
        vulkan_destroy_swapchain(vulkan);
    }

    U32 retrievedImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(vulkan->device, vulkan->swapchain.handle, &retrievedImageCount, 0));
    if (retrievedImageCount == 0) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Swapchain returned no images");
        return 0;
    }

    if (vulkan->swapchain.imageCapacity < retrievedImageCount) {
        vulkan->swapchain.images = ARENA_PUSH_ARRAY(vulkan->arena, RendererVulkanSwapchainImage, retrievedImageCount);
        if (!vulkan->swapchain.images) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate swapchain image storage");
            return 0;
        }
        vulkan->swapchain.imageSemaphores = ARENA_PUSH_ARRAY(vulkan->arena, VkSemaphore, retrievedImageCount);
        if (!vulkan->swapchain.imageSemaphores) {
            LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate swapchain image semaphores");
            return 0;
        }
        vulkan->swapchain.imageCapacity = retrievedImageCount;
    }

    VkImage* rawImages = ARENA_PUSH_ARRAY(scratch.arena, VkImage, retrievedImageCount);
    if (!rawImages) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to allocate temporary image array");
        return 0;
    }
    VK_CHECK(vkGetSwapchainImagesKHR(vulkan->device, vulkan->swapchain.handle, &retrievedImageCount, rawImages));

    VkSemaphoreCreateInfo semaphoreInfo = vulkan_semaphore_create_info(0);

    for (U32 i = 0; i < retrievedImageCount; ++i) {
        vulkan->swapchain.images[i].handle = rawImages[i];
        vulkan->swapchain.images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (i >= oldImageCount || vulkan->swapchain.imageSemaphores[i] == VK_NULL_HANDLE) {
            VK_CHECK(vkCreateSemaphore(vulkan->device, &semaphoreInfo, 0, &vulkan->swapchain.imageSemaphores[i]));
        }

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = rawImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(vulkan->device, &viewInfo, 0, &vulkan->swapchain.images[i].view));
    }

    vulkan->swapchain.imageCount = retrievedImageCount;
    vulkan->swapchain.format = surfaceFormat;
    vulkan->swapchain.extent = extent;
    vulkan->swapchainImageIndex = 0u;

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Swapchain created with {} images ({}x{})",
              retrievedImageCount, extent.width, extent.height);
    return 1;
}

static void vulkan_destroy_swapchain(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device == VK_NULL_HANDLE) {
        return;
    }

    if (vulkan->swapchain.images) {
        for (U32 i = 0; i < vulkan->swapchain.imageCount; ++i) {
            if (vulkan->swapchain.images[i].view != VK_NULL_HANDLE) {
                vkDestroyImageView(vulkan->device, vulkan->swapchain.images[i].view, 0);
                vulkan->swapchain.images[i].view = VK_NULL_HANDLE;
            }
            vulkan->swapchain.images[i].handle = VK_NULL_HANDLE;
            vulkan->swapchain.images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    if (vulkan->swapchain.imageSemaphores) {
        for (U32 i = 0; i < vulkan->swapchain.imageCount; ++i) {
            if (vulkan->swapchain.imageSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(vulkan->device, vulkan->swapchain.imageSemaphores[i], 0);
                vulkan->swapchain.imageSemaphores[i] = VK_NULL_HANDLE;
            }
        }
    }

    if (vulkan->swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan->device, vulkan->swapchain.handle, 0);
        vulkan->swapchain.handle = VK_NULL_HANDLE;
    }

    vulkan->swapchain.imageCount = 0u;
    vulkan->swapchain.extent = {};
    vulkan->swapchain.format = {};
    vulkan->swapchainImageIndex = 0u;

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Swapchain destroyed");
}

static void vulkan_destroy_frames(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device == VK_NULL_HANDLE) {
        return;
    }

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &vulkan->frames[i];

        if (frame->renderFence != VK_NULL_HANDLE) {
            vkDestroyFence(vulkan->device, frame->renderFence, 0);
            frame->renderFence = VK_NULL_HANDLE;
        }
        if (frame->swapchainSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(vulkan->device, frame->swapchainSemaphore, 0);
            frame->swapchainSemaphore = VK_NULL_HANDLE;
        }
        if (frame->commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vulkan->device, frame->commandPool, 0);
            frame->commandPool = VK_NULL_HANDLE;
            frame->commandBuffer = VK_NULL_HANDLE;
        }
        frame->imageIndex = 0u;
    }

    vulkan->currentFrameIndex = 0u;
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Destroyed all frame command pools, buffers, and sync structures");
}

static VkCommandBufferBeginInfo vulkan_command_buffer_begin_info(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    return beginInfo;
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
    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
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

static void vulkan_transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier = vulkan_image_memory_barrier2(image, currentLayout, newLayout);
    VkDependencyInfo depInfo = vulkan_dependency_info(1, &barrier);
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

static VkSemaphoreSubmitInfo vulkan_semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.pNext = 0;
    submitInfo.semaphore = semaphore;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    submitInfo.value = 1;
    return submitInfo;
}

static VkCommandBufferSubmitInfo vulkan_command_buffer_submit_info(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.pNext = 0;
    info.commandBuffer = cmd;
    info.deviceMask = 0;
    return info;
}

static VkSubmitInfo2 vulkan_submit_info2(VkCommandBufferSubmitInfo* cmd,
                                         VkSemaphoreSubmitInfo* signalSemaphoreInfo,
                                         VkSemaphoreSubmitInfo* waitSemaphoreInfo) {
    VkSubmitInfo2 info = {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.pNext = 0;
    info.waitSemaphoreInfoCount = (waitSemaphoreInfo == 0) ? 0 : 1;
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;
    info.signalSemaphoreInfoCount = (signalSemaphoreInfo == 0) ? 0 : 1;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;
    return info;
}

void renderer_vulkan_draw_color(RendererVulkan* vulkan, OS_WindowHandle window, Vec3F32 color) {
    if (!vulkan || !window.handle) {
        return;
    }

    if (vulkan->device == VK_NULL_HANDLE || vulkan->graphicsQueue == VK_NULL_HANDLE) {
        return;
    }

    if (vulkan->swapchain.handle == VK_NULL_HANDLE) {
        if (!vulkan_create_swapchain(vulkan, window)) {
            return;
        }
    }

    RendererVulkanFrame* frame = &vulkan->frames[vulkan->currentFrameIndex];

    VK_CHECK(vkWaitForFences(vulkan->device, 1, &frame->renderFence, VK_TRUE, SECONDS_TO_NANOSECONDS(1)));
    VK_CHECK(vkResetFences(vulkan->device, 1, &frame->renderFence));

    U32 imageIndex = 0u;
    VK_CHECK(vkAcquireNextImageKHR(vulkan->device,
                                   vulkan->swapchain.handle,
                                   SECONDS_TO_NANOSECONDS(1),
                                   frame->swapchainSemaphore,
                                   VK_NULL_HANDLE,
                                   &imageIndex));

    frame->imageIndex = imageIndex;

    if (imageIndex >= vulkan->swapchain.imageCount) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Swapchain returned invalid image index {} (count {})",
                  imageIndex, vulkan->swapchain.imageCount);
        return;
    }

    RendererVulkanSwapchainImage* image = &vulkan->swapchain.images[imageIndex];

    VK_CHECK(vkResetCommandBuffer(frame->commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo = vulkan_command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(frame->commandBuffer, &beginInfo));

    vulkan_transition_image(frame->commandBuffer, image->handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    VkImageSubresourceRange clearRange = vulkan_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    VkClearColorValue clearColor = {{color.r, color.g, color.b, 1.0f}};
    vkCmdClearColorImage(frame->commandBuffer,
                         image->handle,
                         VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor,
                         1,
                         &clearRange);

    vulkan_transition_image(frame->commandBuffer, image->handle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(frame->commandBuffer));

    VkSemaphoreSubmitInfo waitSemaphoreInfo = vulkan_semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frame->swapchainSemaphore);
    VkSemaphoreSubmitInfo signalSemaphoreInfo = vulkan_semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, vulkan->swapchain.imageSemaphores[imageIndex]);
    VkCommandBufferSubmitInfo cmdBufferInfo = vulkan_command_buffer_submit_info(frame->commandBuffer);
    VkSubmitInfo2 submitInfo = vulkan_submit_info2(&cmdBufferInfo, &signalSemaphoreInfo, &waitSemaphoreInfo);

    VK_CHECK(vkQueueSubmit2(vulkan->graphicsQueue, 1, &submitInfo, frame->renderFence));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &vulkan->swapchain.imageSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vulkan->swapchain.handle;
    presentInfo.pImageIndices = &frame->imageIndex;

    VK_CHECK(vkQueuePresentKHR(vulkan->graphicsQueue, &presentInfo));

    vulkan->currentFrameIndex = (vulkan->currentFrameIndex + 1u) % VULKAN_FRAME_OVERLAP;
}
