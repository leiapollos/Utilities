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
// Vulkan Renderer Backend

static const U32 VULKAN_FRAME_OVERLAP = 2u;

struct ImGuiContext;

struct RendererVulkanFrame {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapchainSemaphore;
    VkFence renderFence;
    U32 imageIndex;
};

struct RendererVulkanAllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct RendererVulkanSwapchainImage {
    VkImage handle;
    VkImageView view;
    VkImageLayout layout;
};

struct RendererVulkanSwapchain {
    VkSwapchainKHR handle;
    RendererVulkanSwapchainImage* images;
    VkSemaphore* imageSemaphores;
    U32 imageCount;
    U32 imageCapacity;
    VkSurfaceFormatKHR format;
    VkExtent2D extent;
};

struct RendererVulkanShader {
    VkShaderModule module;
    StringU8 path;
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
    RendererVulkanFrame frames[VULKAN_FRAME_OVERLAP];
    U32 currentFrameIndex;

    VmaAllocator allocator;
    VkDeferCtx deferCtx;
    U8* deferPerFrameMem;
    U8* deferGlobalMem;

    RendererVulkanAllocatedImage drawImage;
    VkExtent2D drawExtent;
    VkDescriptorSetLayout drawImageDescriptorLayout;
    VkDescriptorPool drawImageDescriptorPool;
    VkDescriptorSet drawImageDescriptorSet;
    VkPipelineLayout gradientPipelineLayout;
    VkPipeline gradientPipeline;

    VkDescriptorPool imguiDescriptorPool;
    ImGuiContext* imguiContext;
    OS_WindowHandle imguiWindow;
    VkPipelineRenderingCreateInfoKHR imguiPipelineInfo;
    VkExtent2D imguiWindowExtent;
    VkFormat imguiColorAttachmentFormats[1];
    U32 imguiMinImageCount;
    B32 imguiInitialized;

    RendererVulkanShader* shaders;
    U32 shaderCount;
    U32 shaderCapacity;
};

#if defined(NDEBUG)
static const B32 ENABLE_VALIDATION_LAYERS = 0;
#else
static const B32 ENABLE_VALIDATION_LAYERS = 1;
#endif

static const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};

void renderer_vulkan_draw_color(RendererVulkan* vulkan, OS_WindowHandle window, Vec4F32 color);
B32 renderer_vulkan_compile_shader_to_result(RendererVulkan* vulkan, Arena* arena, StringU8 shaderPath,
                                             ShaderCompileResult* outResult);
B32 renderer_vulkan_imgui_init(RendererVulkan* vulkan, OS_WindowHandle window);
void renderer_vulkan_imgui_shutdown(RendererVulkan* vulkan);
void renderer_vulkan_imgui_process_events(RendererVulkan* vulkan, const OS_GraphicsEvent* events, U32 eventCount);
void renderer_vulkan_imgui_begin_frame(RendererVulkan* vulkan, F32 deltaSeconds);
void renderer_vulkan_imgui_end_frame(RendererVulkan* vulkan);
void renderer_vulkan_imgui_on_swapchain_updated(RendererVulkan* vulkan);
void renderer_vulkan_imgui_render(RendererVulkan* vulkan, VkCommandBuffer cmd, VkImageView targetImageView,
                                  VkExtent2D extent);
void renderer_vulkan_imgui_set_window_size(RendererVulkan* vulkan, U32 width, U32 height);
