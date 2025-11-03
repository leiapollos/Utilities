//
// Created by André Leite on 03/11/2025.
//

#pragma once

#if defined(PLATFORM_OS_MACOS)
#ifndef VK_USE_PLATFORM_MACOS_MVK
#define VK_USE_PLATFORM_MACOS_MVK
#endif
#endif

#include <vulkan/vulkan.h>

static const StringU8 VULKAN_LOG_DOMAIN = str8("vulkan");

// ////////////////////////
// Vulkan Check Macro

#define VK_CHECK(result) \
do { \
    VkResult vkResult = (result); \
    if (vkResult != VK_SUCCESS) { \
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Vulkan error: {} ({}:{})", vkResult, __FILE__, __LINE__); \
        ASSERT_ALWAYS(false && "Vulkan call failed"); \
    } \
} while (false)

// ////////////////////////
// Vulkan Renderer Backend

static const U32 VULKAN_FRAME_OVERLAP = 2u;

struct RendererVulkanFrame {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapchainSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
    U32 imageIndex;
};

struct RendererVulkanSwapchainImage {
    VkImage handle;
    VkImageView view;
    VkImageLayout layout;
};

struct RendererVulkanSwapchain {
    VkSwapchainKHR handle;
    RendererVulkanSwapchainImage* images;
    U32 imageCount;
    U32 imageCapacity;
    VkSurfaceFormatKHR format;
    VkExtent2D extent;
};

struct RendererVulkan {
    Arena* arena;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkQueue graphicsQueue;
    U32 graphicsQueueFamilyIndex;
    B32 validationLayersEnabled;
    VkSurfaceKHR surface;
    RendererVulkanSwapchain swapchain;
    U32 swapchainImageIndex;
    RendererVulkanFrame frames[VULKAN_FRAME_OVERLAP];
    U32 currentFrameIndex;
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

static B32 vulkan_create_surface(OS_WindowHandle window, RendererVulkan* vulkan);
static void vulkan_destroy_surface(RendererVulkan* vulkan);

static B32 vulkan_create_swapchain(RendererVulkan* vulkan, OS_WindowHandle window);
static void vulkan_destroy_swapchain(RendererVulkan* vulkan);

static B32 vulkan_create_frames(RendererVulkan* vulkan);
static B32 vulkan_create_sync_structures(RendererVulkan* vulkan);
static void vulkan_destroy_frames(RendererVulkan* vulkan);

static B32 vulkan_create_debug_messenger(Arena* arena, RendererVulkan* vulkan);
static void vulkan_destroy_debug_messenger(RendererVulkan* vulkan);