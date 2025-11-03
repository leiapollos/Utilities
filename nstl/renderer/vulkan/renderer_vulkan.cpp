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
        LOG_ERROR("vulkan", "Failed to allocate RendererVulkan");
        return 0;
    }

    vulkan->instance = VK_NULL_HANDLE;
    vulkan->debugMessenger = VK_NULL_HANDLE;
    vulkan->validationLayersEnabled = 0;

    if (!vulkan_create_instance(arena, vulkan)) {
        return 0;
    }

    renderer->backendData = vulkan;
    return 1;
}

void renderer_shutdown(Renderer* renderer) {
    if (!renderer || !renderer->backendData) {
        return;
    }

    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    
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
// Debug Messenger Callback

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
        LOG_WARNING("vulkan", "Debug utils messenger extension not available");
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
    
    LOG_INFO("vulkan", "Debug messenger created successfully");
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
        LOG_INFO("vulkan", "Debug messenger destroyed");
    }
    
    vulkan->debugMessenger = VK_NULL_HANDLE;
}

// ////////////////////////
// Instance Creation

static B32 vulkan_create_instance(Arena* arena, RendererVulkan* vulkan) {
    if (ENABLE_VALIDATION_LAYERS && !vulkan_check_validation_layer_support(arena)) {
        LOG_WARNING("vulkan", "Validation layers requested but not available");
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
    LOG_INFO("vulkan", "Found {} available instance extensions", availableExtensionCount);

    for (U32 i = 0; i < DESIRED_EXTENSION_COUNT; ++i) {
        if (vulkan_check_extension_support(arena, DESIRED_EXTENSIONS[i])) {
            enabledExtensions[enabledExtensionCount] = DESIRED_EXTENSIONS[i];
            enabledExtensionCount += 1;
            LOG_INFO("vulkan", "Extension '{}' is available", DESIRED_EXTENSIONS[i]);
        } else {
            LOG_WARNING("vulkan", "Extension '{}' is not available", DESIRED_EXTENSIONS[i]);
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
    
    LOG_INFO("vulkan", "Vulkan instance created successfully");
    
    if (!vulkan_create_debug_messenger(arena, vulkan)) {
        LOG_WARNING("vulkan", "Failed to create debug messenger, continuing without validation logging");
    }
    
    return 1;
}

static void vulkan_destroy_instance(RendererVulkan* vulkan) {
    vulkan_destroy_debug_messenger(vulkan);
    
    if (vulkan->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(vulkan->instance, 0);
        LOG_INFO("vulkan", "Vulkan instance destroyed");
    }
}
