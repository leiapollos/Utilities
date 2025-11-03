//
// Created by André Leite on 03/11/2025.
//

#pragma once

#include <vulkan/vulkan.h>

// ////////////////////////
// Vulkan Check Macro

#define VK_CHECK(result) \
do { \
    VkResult vkResult = (result); \
    if (vkResult != VK_SUCCESS) { \
        LOG_ERROR("vulkan", "Vulkan error: {} ({}:{})", vkResult, __FILE__, __LINE__); \
        ASSERT_ALWAYS(false && "Vulkan call failed"); \
    } \
} while (false)

// ////////////////////////
// Vulkan Renderer Backend

struct RendererVulkan {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkQueue graphicsQueue;
    U32 graphicsQueueFamilyIndex;
    B32 validationLayersEnabled;
};

#if defined(NDEBUG)
static const B32 ENABLE_VALIDATION_LAYERS = 0;
#else
static const B32 ENABLE_VALIDATION_LAYERS = 1;
#endif

static const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};


static B32 vulkan_check_validation_layer_support(Arena* arena);
static B32 vulkan_check_extension_support(Arena* arena, const char* extensionName);

static B32 vulkan_create_instance(Arena* arena, RendererVulkan* vulkan);
static void vulkan_destroy_instance(RendererVulkan* vulkan);

static B32 vulkan_create_device(Arena* arena, RendererVulkan* vulkan);
static B32 vulkan_init_device_queues(RendererVulkan* vulkan);
static void vulkan_destroy_device(RendererVulkan* vulkan);

static B32 vulkan_create_debug_messenger(Arena* arena, RendererVulkan* vulkan);
static void vulkan_destroy_debug_messenger(RendererVulkan* vulkan);