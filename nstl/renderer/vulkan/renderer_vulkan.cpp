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

    vulkan->instance = VK_NULL_HANDLE;
    vulkan->physicalDevice = VK_NULL_HANDLE;
    vulkan->device = VK_NULL_HANDLE;
    vulkan->debugMessenger = VK_NULL_HANDLE;
    vulkan->graphicsQueue = VK_NULL_HANDLE;
    vulkan->graphicsQueueFamilyIndex = 0u;
    vulkan->validationLayersEnabled = 0;
    vulkan->currentFrameIndex = 0u;
    
    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        vulkan->frames[i].commandPool = VK_NULL_HANDLE;
        vulkan->frames[i].mainCommandBuffer = VK_NULL_HANDLE;
        vulkan->frames[i].swapchainSemaphore = VK_NULL_HANDLE;
        vulkan->frames[i].renderSemaphore = VK_NULL_HANDLE;
        vulkan->frames[i].renderFence = VK_NULL_HANDLE;
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
    
    vulkan_destroy_frames(vulkan);
    vulkan_destroy_device(vulkan);
    vulkan_destroy_debug_messenger(vulkan);
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
#endif
        "VK_KHR_surface",
        "VK_MVK_macos_surface",
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
    
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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

        VK_CHECK(vkAllocateCommandBuffers(vulkan->device, &allocInfo, &frame->mainCommandBuffer));

        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created frame {} command pool and command buffer", i);
    }

    if (!vulkan_create_sync_structures(vulkan)) {
        return 0;
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created {} frames for frame overlap", VULKAN_FRAME_OVERLAP);
    return 1;
}

static B32 vulkan_create_sync_structures(RendererVulkan* vulkan) {
    if (!vulkan || vulkan->device == VK_NULL_HANDLE) {
        return 0;
    }

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (U32 i = 0; i < VULKAN_FRAME_OVERLAP; ++i) {
        RendererVulkanFrame* frame = &vulkan->frames[i];

        VK_CHECK(vkCreateSemaphore(vulkan->device, &semaphoreInfo, 0, &frame->swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(vulkan->device, &semaphoreInfo, 0, &frame->renderSemaphore));
        VK_CHECK(vkCreateFence(vulkan->device, &fenceInfo, 0, &frame->renderFence));

        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created sync structures for frame {}", i);
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Created sync structures for all frames");
    return 1;
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
        if (frame->renderSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(vulkan->device, frame->renderSemaphore, 0);
            frame->renderSemaphore = VK_NULL_HANDLE;
        }
        if (frame->swapchainSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(vulkan->device, frame->swapchainSemaphore, 0);
            frame->swapchainSemaphore = VK_NULL_HANDLE;
        }
        if (frame->commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vulkan->device, frame->commandPool, 0);
            frame->commandPool = VK_NULL_HANDLE;
            frame->mainCommandBuffer = VK_NULL_HANDLE;
        }
    }

    vulkan->currentFrameIndex = 0u;
    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Destroyed all frame command pools, buffers, and sync structures");
}

