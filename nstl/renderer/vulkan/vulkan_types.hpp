//
// Created by André Leite on 03/11/2025.
//

#pragma once

#include "vulkan/vulkan_core.h"
#if defined(PLATFORM_OS_MACOS)
#ifndef VK_USE_PLATFORM_MACOS_MVK
#define VK_USE_PLATFORM_MACOS_MVK
#endif
#ifndef VK_USE_PLATFORM_METAL_EXT
#define VK_USE_PLATFORM_METAL_EXT
#endif
#endif

#include <vulkan/vulkan.h>

#if defined(COMPILER_CLANG)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#endif
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <dxc/dxcapi.h>
#if defined(COMPILER_CLANG)
#pragma clang diagnostic pop
#endif

#define VKDEFER_ENABLE_VMA
#define VKDEFER_IMPLEMENTATION
#include "vkdefer.h"

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
// Types

static const U32 VULKAN_FRAME_OVERLAP = 2u;

struct ImGuiContext;

struct VulkanDevice {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkQueue graphicsQueue;
    U32 graphicsQueueFamilyIndex;
    B32 validationLayersEnabled;
    VmaAllocator allocator;
};

struct RendererVulkanSwapchainImage {
    VkImage handle;
    VkImageView view;
    VkImageLayout layout;
};

struct VulkanSwapchain {
    VkSurfaceKHR surface;
    VkSwapchainKHR handle;
    RendererVulkanSwapchainImage* images;
    VkSemaphore* imageSemaphores;
    U32 imageCount;
    U32 imageCapacity;
    VkSurfaceFormatKHR format;
    VkExtent2D extent;
};

struct RendererVulkanFrame {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapchainSemaphore;
    VkFence renderFence;
    U32 imageIndex;
};

struct VulkanCommands {
    RendererVulkanFrame frames[VULKAN_FRAME_OVERLAP];
    U32 currentFrameIndex;
};

struct RendererVulkanAllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct VulkanDescriptorAllocator {
    struct PoolSizeRatio {
        VkDescriptorType type;
        F32 ratio;
    };

    VkDescriptorPool currentPool;
    // We might need an arena for pools tracking if we want to destroy them later
    // or just use a simplified approach for now
};

struct RendererVulkanShader {
    VkShaderModule module;
    StringU8 path;
};

struct VulkanPipelines {
    VkDescriptorSetLayout drawImageDescriptorLayout;
    VkPipelineLayout gradientPipelineLayout;
    VkPipeline gradientPipeline;
    RendererVulkanShader* shaders;
    U32 shaderCount;
    U32 shaderCapacity;
};

struct RendererVulkan {
    Arena* arena;
    VulkanDevice device;
    VulkanSwapchain swapchain;
    VulkanCommands commands;
    VulkanPipelines pipelines;
    
    // Defer
    VkDeferCtx deferCtx;
    U8* deferPerFrameMem;
    U8* deferGlobalMem;

    // Resources
    RendererVulkanAllocatedImage drawImage;
    VkExtent2D drawExtent;
    
    // Descriptors
    VkDescriptorPool drawImageDescriptorPool;
    VkDescriptorSet drawImageDescriptorSet;

    // ImGui - could also be moved to a struct
    VkDescriptorPool imguiDescriptorPool;
    ImGuiContext* imguiContext;
    OS_WindowHandle imguiWindow;
    VkPipelineRenderingCreateInfoKHR imguiPipelineInfo;
    VkExtent2D imguiWindowExtent;
    VkFormat imguiColorAttachmentFormats[1];
    U32 imguiMinImageCount;
    B32 imguiInitialized;
};

#if defined(NDEBUG)
static const B32 ENABLE_VALIDATION_LAYERS = 0;
#else
static const B32 ENABLE_VALIDATION_LAYERS = 1;
#endif

static const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};
