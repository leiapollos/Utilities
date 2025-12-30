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

struct VulkanDescriptorAllocator;
struct RendererVulkanAllocatedBuffer;

struct RendererVulkanFrame {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore swapchainSemaphore;
    VkFence renderFence;
    U32 imageIndex;
    VulkanDescriptorAllocator* frameDescriptors;
    RendererVulkanAllocatedBuffer* sceneBuffer;
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

struct RendererVulkanAllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct GPUMeshBuffers {
    RendererVulkanAllocatedBuffer indexBuffer;
    RendererVulkanAllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

struct GPUDrawPushConstants {
    Mat4x4F32 worldMatrix;
    VkDeviceAddress vertexBuffer;
    F32 alpha;
    F32 _padding[3];
};

struct GPUMesh {
    GPUMeshBuffers gpu;
    U32 indexCount;
};



// ////////////////////////
// Material Types

typedef enum MaterialType {
    MaterialType_Opaque,
    MaterialType_Transparent,
    MaterialType_COUNT
} MaterialType;

struct MaterialConstants {
    Vec4F32 colorFactor;
    Vec4F32 metalRoughFactor;
    Vec4F32 _pad[14];
};

struct Material {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet descriptorSet;
    MaterialType type;
};

static const U32 VULKAN_DESCRIPTOR_ALLOCATOR_MAX_RATIOS = 8;

struct VulkanDescriptorAllocator {
    Arena* arena;
    
    VkDescriptorType types[VULKAN_DESCRIPTOR_ALLOCATOR_MAX_RATIOS];
    F32 ratios[VULKAN_DESCRIPTOR_ALLOCATOR_MAX_RATIOS];
    U32 ratioCount;
    
    VkDescriptorPool* pools;
    U32 poolCount;
    U32 poolCapacity;
    U32 currentPoolIndex;
    
    U32 setsPerPool;
};

struct RendererVulkanShader {
    VkShaderModule module;
    StringU8 path;
};

struct VulkanPipelines {
    VkDescriptorSetLayout drawImageDescriptorLayout;
    VkPipelineLayout gradientPipelineLayout;
    VkPipeline gradientPipeline;
    
    VkPipelineLayout meshPipelineLayout;
    VkPipeline meshPipeline;
    
    RendererVulkanShader* shaders;
    U32 shaderCount;
    U32 shaderCapacity;
};

struct ImmediateSubmitContext {
    VkFence fence;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

struct RendererVulkan {
    Arena* arena;
    VulkanDevice device;
    VulkanSwapchain swapchain;
    VulkanCommands commands;
    VulkanPipelines pipelines;
    
    VkDeferCtx deferCtx;
    U8* deferPerFrameMem;
    U8* deferGlobalMem;
    
    ImmediateSubmitContext immSubmit;

    RendererVulkanAllocatedImage drawImage;
    RendererVulkanAllocatedImage depthImage;
    VkExtent2D drawExtent;
    
    Arena* frameArena;
    
    VkDescriptorPool drawImageDescriptorPool;
    VkDescriptorSet drawImageDescriptorSet;
    
    RendererVulkanAllocatedImage whiteImage;
    RendererVulkanAllocatedImage blackImage;
    RendererVulkanAllocatedImage errorImage;
    
    VkSampler samplerLinear;
    VkSampler samplerNearest;
    
    VkDescriptorSetLayout sceneDataLayout;
    VkDescriptorSetLayout materialLayout;
    
    VkPipelineLayout materialPipelineLayout;
    VkPipeline opaquePipeline;
    VkPipeline transparentPipeline;
    
    VkDescriptorPool globalDescriptorPool;
    Material defaultMaterial;
    RendererVulkanAllocatedBuffer defaultMaterialBuffer;

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
