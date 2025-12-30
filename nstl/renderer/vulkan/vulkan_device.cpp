//
// Created by André Leite on 03/11/2025.
//

static B32 vulkan_check_validation_layer_support(Arena* arena);
static B32 vulkan_check_extension_support(Arena* arena, const char* extensionName);
static B32 vulkan_create_instance(Arena* arena, VulkanDevice* device);
static void vulkan_destroy_instance(VulkanDevice* device);
static B32 vulkan_create_device_internal(Arena* arena, VulkanDevice* device);
static B32 vulkan_init_device_queues(VulkanDevice* device);
static void vulkan_destroy_device_internal(VulkanDevice* device);
static B32 vulkan_create_allocator(VulkanDevice* device);
static B32 vulkan_create_debug_messenger(Arena* arena, VulkanDevice* device);
static void vulkan_destroy_debug_messenger(VulkanDevice* device);

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
// Debug Messenger

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void) pUserData;
    thread_context_alloc();

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

static B32 vulkan_create_debug_messenger(Arena* arena, VulkanDevice* device) {
    if (!device->validationLayersEnabled) {
        return 1;
    }

    PFN_vkCreateDebugUtilsMessengerEXT createDebugMessenger =
            (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(device->instance,
                                                                       "vkCreateDebugUtilsMessengerEXT");

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

    VK_CHECK(createDebugMessenger(device->instance, &createInfo, 0, &device->debugMessenger));

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Debug messenger created successfully");
    return 1;
}

static void vulkan_destroy_debug_messenger(VulkanDevice* device) {
    if (device->debugMessenger == VK_NULL_HANDLE) {
        return;
    }

    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
                device->instance, "vkDestroyDebugUtilsMessengerEXT");

    if (destroyDebugMessenger) {
        destroyDebugMessenger(device->instance, device->debugMessenger, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Debug messenger destroyed");
    }

    device->debugMessenger = VK_NULL_HANDLE;
}

// ////////////////////////
// Instance

static B32 vulkan_create_instance(Arena* arena, VulkanDevice* device) {
    if (ENABLE_VALIDATION_LAYERS && !vulkan_check_validation_layer_support(arena)) {
        LOG_WARNING(VULKAN_LOG_DOMAIN, "Validation layers requested but not available");
        device->validationLayersEnabled = 0;
    } else {
        device->validationLayersEnabled = ENABLE_VALIDATION_LAYERS ? 1 : 0;
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

    if (device->validationLayersEnabled) {
        createInfo.enabledLayerCount = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
        createInfo.pNext = 0;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = 0;
    }

    VK_CHECK(vkCreateInstance(&createInfo, 0, &device->instance));

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan instance created successfully");

    if (!vulkan_create_debug_messenger(arena, device)) {
        LOG_WARNING(VULKAN_LOG_DOMAIN, "Failed to create debug messenger, continuing without validation logging");
    }

    return 1;
}

static void vulkan_destroy_instance(VulkanDevice* device) {
    if (device->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(device->instance, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan instance destroyed");
    }
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

static B32 vulkan_check_device_extension_support(Arena* arena, VkPhysicalDevice physicalDevice,
                                                 const char* extensionName) {
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

static B32 vulkan_create_device_internal(Arena* arena, VulkanDevice* device) {
    device->physicalDevice = vulkan_select_physical_device(arena, device->instance);
    if (device->physicalDevice == VK_NULL_HANDLE) {
        return 0;
    }

    U32 graphicsQueueFamilyIndex = 0u;
    if (!vulkan_find_graphics_queue_family(arena, device->physicalDevice, &graphicsQueueFamilyIndex)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "No graphics queue family found");
        return 0;
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Using graphics queue family {}", graphicsQueueFamilyIndex);

    device->graphicsQueueFamilyIndex = graphicsQueueFamilyIndex;

    const char* DESIRED_DEVICE_EXTENSIONS[] = {
#if defined(PLATFORM_OS_MACOS)
        "VK_KHR_portability_subset", // Required for MoltenVK
#endif
        "VK_KHR_swapchain",
        "VK_KHR_synchronization2",
        "VK_KHR_dynamic_rendering",
    };
    const U32 DESIRED_DEVICE_EXTENSION_COUNT = sizeof(DESIRED_DEVICE_EXTENSIONS) / sizeof(DESIRED_DEVICE_EXTENSIONS[0]);

    const char* enabledDeviceExtensions[16] = {};
    U32 enabledDeviceExtensionCount = 0;

    for (U32 i = 0; i < DESIRED_DEVICE_EXTENSION_COUNT; ++i) {
        if (vulkan_check_device_extension_support(arena, device->physicalDevice, DESIRED_DEVICE_EXTENSIONS[i])) {
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
    queueCreateInfo.queueFamilyIndex = device->graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.shaderInt64 = VK_TRUE;

    VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = {};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2Features.pNext = 0;
    sync2Features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan12Features = {};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.pNext = &sync2Features;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures = {};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamicRenderingFeatures.pNext = &vulkan12Features;
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &dynamicRenderingFeatures;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = enabledDeviceExtensionCount;
    createInfo.ppEnabledExtensionNames = (enabledDeviceExtensionCount > 0) ? enabledDeviceExtensions : 0;

    if (device->validationLayersEnabled) {
        createInfo.enabledLayerCount = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VK_CHECK(vkCreateDevice(device->physicalDevice, &createInfo, 0, &device->device));

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan device created successfully");
    return 1;
}

static B32 vulkan_init_device_queues(VulkanDevice* device) {
    if (!device) {
        return 0;
    }

    if (device->device == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Cannot initialize queues without a logical device");
        return 0;
    }

    const U32 queueIndex = 0u;
    vkGetDeviceQueue(device->device, device->graphicsQueueFamilyIndex, queueIndex, &device->graphicsQueue);

    if (device->graphicsQueue == VK_NULL_HANDLE) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to retrieve graphics queue (family {}, index {})",
                  device->graphicsQueueFamilyIndex, queueIndex);
        return 0;
    }

    LOG_DEBUG(VULKAN_LOG_DOMAIN, "Graphics queue initialized (family {}, index {})", device->graphicsQueueFamilyIndex,
              queueIndex);
    return 1;
}

static void vulkan_destroy_device_internal(VulkanDevice* device) {
    if (device->device != VK_NULL_HANDLE) {
        vkDestroyDevice(device->device, 0);
        LOG_DEBUG(VULKAN_LOG_DOMAIN, "Vulkan device destroyed");
        device->device = VK_NULL_HANDLE;
    }
    device->graphicsQueue = VK_NULL_HANDLE;
    device->graphicsQueueFamilyIndex = 0u;
    device->physicalDevice = VK_NULL_HANDLE;
}

// ////////////////////////
// VMA Allocator

static B32 vulkan_create_allocator(VulkanDevice* device) {
    if (!device || device->device == VK_NULL_HANDLE || device->physicalDevice == VK_NULL_HANDLE) {
        return 0;
    }

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = device->physicalDevice;
    allocatorInfo.device = device->device;
    allocatorInfo.instance = device->instance;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &device->allocator);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create VMA allocator: {}", result);
        return 0;
    }

    LOG_INFO(VULKAN_LOG_DOMAIN, "VMA allocator created successfully");
    return 1;
}

// ////////////////////////
// Public Device API

B32 vulkan_device_init(VulkanDevice* device, Arena* arena) {
    if (!device || !arena) return 0;
    
    device->instance = VK_NULL_HANDLE;
    device->physicalDevice = VK_NULL_HANDLE;
    device->device = VK_NULL_HANDLE;
    device->debugMessenger = VK_NULL_HANDLE;
    device->graphicsQueue = VK_NULL_HANDLE;
    device->graphicsQueueFamilyIndex = 0;
    device->validationLayersEnabled = 0;
    device->allocator = 0;

    if (!vulkan_create_instance(arena, device)) return 0;
    if (!vulkan_create_device_internal(arena, device)) return 0;
    if (!vulkan_init_device_queues(device)) return 0;
    if (!vulkan_create_allocator(device)) return 0;
    
    return 1;
}

void vulkan_device_shutdown(VulkanDevice* device) {
    if (!device) return;
    
    if (device->allocator) {
        vmaDestroyAllocator(device->allocator);
        device->allocator = 0;
    }
    
    vulkan_destroy_device_internal(device);
    vulkan_destroy_debug_messenger(device);
    vulkan_destroy_instance(device);
}
