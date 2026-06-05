//
// Created by André Leite on 05/06/2026.
//

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define GFX_VULKAN_DEFAULT_FRAMES_IN_FLIGHT 2u
#define GFX_VULKAN_DEFAULT_TEMP_BUFFER_SIZE MB(8)
#define GFX_VULKAN_RESOURCE_TABLE_CAPACITY 256u
#define GFX_VULKAN_RESOURCE_BINDING_TEXTURES 0u
#define GFX_VULKAN_RESOURCE_BINDING_SAMPLERS 1u
#define GFX_VULKAN_DESCRIPTOR_SET_RESOURCE_TABLE 0u
#define GFX_VULKAN_DESCRIPTOR_SET_BUFFERS 1u
#define GFX_VULKAN_RETIRED_BUFFER_CAPACITY 512u
#define GFX_VULKAN_RETIRED_TEXTURE_CAPACITY 256u
#define GFX_VULKAN_RETIRED_SAMPLER_CAPACITY 256u
#define GFX_VULKAN_RETIRED_PIPELINE_CAPACITY 256u

enum GfxVulkanResourceKind {
    GfxVulkanResourceKind_Invalid = 0,
    GfxVulkanResourceKind_Buffer,
    GfxVulkanResourceKind_Texture,
    GfxVulkanResourceKind_Sampler,
};

struct GfxVulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
    U64 size;
    U32 usageFlags;
    GfxMemoryKind memoryKind;
    GfxResourceId resourceId;
    B32 internal;
};

struct GfxVulkanTexture {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkImageLayout layout;
    U32 width;
    U32 height;
    U32 mipCount;
    GfxFormat format;
    U32 usageFlags;
    GfxResourceId resourceId;
    B32 ownsImage;
    B32 internal;
};

struct GfxVulkanSampler {
    VkSampler sampler;
    GfxResourceId resourceId;
};

struct GfxVulkanPipeline {
    GfxPipelineKind kind;
    VkPipeline pipeline;
    GfxRasterState raster;
    U32 threadsPerThreadgroupX;
    U32 threadsPerThreadgroupY;
    U32 threadsPerThreadgroupZ;
};

struct GfxVulkanResourceEntry {
    GfxVulkanResourceKind kind;
    U32 index;
    U32 generation;
    U32 nextFree;
};

struct GfxVulkanRetiredBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
    GfxResourceId resourceId;
    U64 retireSerial;
};

struct GfxVulkanRetiredTexture {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    GfxResourceId resourceId;
    U64 retireSerial;
};

struct GfxVulkanRetiredSampler {
    VkSampler sampler;
    GfxResourceId resourceId;
    U64 retireSerial;
};

struct GfxVulkanRetiredPipeline {
    VkPipeline pipeline;
    U64 retireSerial;
};

struct GfxVulkanPushConstants {
    U32 dataByteOffset;
    U32 passByteOffset;
    U32 _padding0;
    U32 _padding1;
};

struct GfxCommandBuffer {
    GfxFrame* frame;
};

struct GfxFrame {
    GfxDevice* device;
    GfxCommandBuffer commands;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence fence;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkDescriptorPool descriptorPool;
    GfxBuffer tempBuffer;
    void* tempCpu;
    U64 tempSize;
    U64 tempPos;
    U32 imageIndex;
    B32 active;
    B32 submitted;
    U64 submittedSerial;
};

struct GfxDevice {
    GfxBackend backend;
    Arena* arena;
    U32 validationFlags;
    B32 alive;

    HINSTANCE win32Instance;
    HWND win32Window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    U32 graphicsQueueFamily;
    U32 presentQueueFamily;

    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    VkImage* swapchainImages;
    VkImageView* swapchainImageViews;
    U32 swapchainImageCount;

    U32 framesInFlight;
    U32 frameCursor;
    U64 frameSerial;
    U64 completedFrameSerial;
    GfxFrame* frames;
    GfxFrame* activeFrame;
    GfxTexture backbuffer;

    SlotMap buffers;
    SlotMap textures;
    SlotMap samplers;
    SlotMap pipelines;

    GfxVulkanResourceEntry* resourceEntries;
    U32 resourceCount;
    U32 resourceLiveCount;
    U32 resourceCapacity;
    U32 resourceFreeHead;

    GfxVulkanRetiredBuffer* retiredBuffers;
    U32 retiredBufferCount;
    GfxVulkanRetiredTexture* retiredTextures;
    U32 retiredTextureCount;
    GfxVulkanRetiredSampler* retiredSamplers;
    U32 retiredSamplerCount;
    GfxVulkanRetiredPipeline* retiredPipelines;
    U32 retiredPipelineCount;

    VkDescriptorSetLayout resourceSetLayout;
    VkDescriptorPool resourceDescriptorPool;
    VkDescriptorSet resourceSet;
    VkDescriptorSetLayout bufferSetLayout;
    VkPipelineLayout pipelineLayout;

    VkImage nilImage;
    VkDeviceMemory nilImageMemory;
    VkImageView nilImageView;
    VkSampler nilSampler;

    GfxStats stats;
};

static GfxVulkanBuffer g_gfxVulkanNilBuffer = {};
static GfxVulkanTexture g_gfxVulkanNilTexture = {};
static GfxVulkanSampler g_gfxVulkanNilSampler = {};
static GfxVulkanPipeline g_gfxVulkanNilPipeline = {};

static B32 gfx_vulkan_validation_has(GfxDevice* device, GfxValidationFlags flags);
static B32 gfx_vulkan_api_validation_enabled(GfxDevice* device);
static B32 gfx_vulkan_backend_validation_enabled(GfxDevice* device);
static void gfx_vulkan_api_assert(GfxDevice* device, B32 condition);
static B32 gfx_vulkan_window_from_handle(OS_WindowHandle window, HINSTANCE* outInstance, HWND* outWindow);
static VKAPI_ATTR VkBool32 VKAPI_CALL gfx_vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);
static B32 gfx_vulkan_validation_layer_available();
static U32 gfx_vulkan_find_memory_type(GfxDevice* device, U32 typeBits, VkMemoryPropertyFlags properties);
static VkFormat gfx_vulkan_format(GfxFormat format);
static U32 gfx_vulkan_format_bytes_per_pixel(GfxFormat format);
static VkBufferUsageFlags gfx_vulkan_buffer_usage(U32 usageFlags);
static VkImageUsageFlags gfx_vulkan_texture_usage(U32 usageFlags);
static VkImageAspectFlags gfx_vulkan_image_aspect(GfxFormat format);
static VkIndexType gfx_vulkan_index_type(GfxIndexType type);
static VkPrimitiveTopology gfx_vulkan_topology(GfxPrimitiveTopology topology);
static VkFormat gfx_vulkan_vertex_format(GfxVertexFormat format);
static VkCullModeFlags gfx_vulkan_cull_mode(GfxCullMode mode);
static VkFrontFace gfx_vulkan_front_face(GfxFrontFace face);
static VkCompareOp gfx_vulkan_compare_op(GfxCompareOp op);
static VkFilter gfx_vulkan_filter(GfxFilter filter);
static VkSamplerAddressMode gfx_vulkan_address_mode(GfxAddressMode mode);
static GfxVulkanBuffer* gfx_vulkan_resolve_buffer(GfxDevice* device, GfxBuffer handle);
static GfxVulkanTexture* gfx_vulkan_resolve_texture(GfxDevice* device, GfxTexture handle);
static GfxVulkanSampler* gfx_vulkan_resolve_sampler(GfxDevice* device, GfxSampler handle);
static GfxVulkanPipeline* gfx_vulkan_resolve_pipeline(GfxDevice* device, GfxPipeline handle);
static B32 gfx_vulkan_begin_immediate(GfxDevice* device, VkCommandPool* outPool, VkCommandBuffer* outCommandBuffer);
static B32 gfx_vulkan_end_immediate(GfxDevice* device, VkCommandPool commandPool, VkCommandBuffer commandBuffer);
static B32 gfx_vulkan_create_buffer_raw(GfxDevice* device, U64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* outBuffer, VkDeviceMemory* outMemory, void** outMapped);
static void gfx_vulkan_destroy_buffer_item(GfxDevice* device, GfxVulkanBuffer* item);
static void gfx_vulkan_retire_buffer(GfxDevice* device, GfxVulkanBuffer* item);
static void gfx_vulkan_retire_texture(GfxDevice* device, GfxVulkanTexture* item);
static void gfx_vulkan_retire_sampler(GfxDevice* device, GfxVulkanSampler* item);
static void gfx_vulkan_retire_pipeline(GfxDevice* device, GfxVulkanPipeline* item);
static void gfx_vulkan_drain_retired(GfxDevice* device);
static GfxBuffer gfx_vulkan_create_buffer_internal(GfxDevice* device, const GfxBufferDesc* desc, B32 internal);
static B32 gfx_vulkan_copy_buffer_immediate(GfxDevice* device, VkBuffer src, VkBuffer dst, U64 dstOffset, U64 size);
static B32 gfx_vulkan_create_nil_resources(GfxDevice* device);
static void gfx_vulkan_destroy_nil_resources(GfxDevice* device);
static B32 gfx_vulkan_resource_table_init(GfxDevice* device);
static void gfx_vulkan_resource_table_set_texture(GfxDevice* device, GfxResourceId resourceId, VkImageView view);
static void gfx_vulkan_resource_table_set_sampler(GfxDevice* device, GfxResourceId resourceId, VkSampler sampler);
static void gfx_vulkan_resource_table_clear(GfxDevice* device, GfxResourceId resourceId);
static B32 gfx_vulkan_resource_entries_reserve(GfxDevice* device, U32 neededCapacity);
static GfxResourceId gfx_vulkan_register_resource(GfxDevice* device, GfxVulkanResourceKind kind, U32 index, U32 generation);
static B32 gfx_vulkan_create_buffer_set_layout(GfxDevice* device);
static B32 gfx_vulkan_create_pipeline_layout(GfxDevice* device);
static VkDescriptorSet gfx_vulkan_alloc_buffer_set(GfxFrame* frame, GfxBuffer dataBuffer, GfxBuffer passBuffer);
static B32 gfx_vulkan_create_frame_descriptor_pool(GfxDevice* device, GfxFrame* frame);
static B32 gfx_vulkan_create_temp_buffer(GfxDevice* device, GfxFrame* frame, U64 size);
static void gfx_vulkan_destroy_swapchain(GfxDevice* device);
static B32 gfx_vulkan_create_swapchain(GfxDevice* device, U32 width, U32 height);
static B32 gfx_vulkan_create_frame_objects(GfxDevice* device, GfxFrame* frame, U64 tempSize);
static void gfx_vulkan_destroy_frame_objects(GfxDevice* device, GfxFrame* frame);
static VkAttachmentLoadOp gfx_vulkan_load_op(GfxLoadOp op);
static VkAttachmentStoreOp gfx_vulkan_store_op(GfxStoreOp op);

static B32 gfx_vulkan_validation_has(GfxDevice* device, GfxValidationFlags flags) {
    return (device && FLAGS_HAS(device->validationFlags, flags)) ? 1 : 0;
}

static B32 gfx_vulkan_api_validation_enabled(GfxDevice* device) {
    return gfx_vulkan_validation_has(device, GfxValidationFlags_Api);
}

static B32 gfx_vulkan_backend_validation_enabled(GfxDevice* device) {
    return gfx_vulkan_validation_has(device, GfxValidationFlags_Backend);
}

static void gfx_vulkan_api_assert(GfxDevice* device, B32 condition) {
    if (gfx_vulkan_api_validation_enabled(device)) {
        ASSERT_DEBUG(condition);
    }
}

static B32 gfx_vulkan_window_from_handle(OS_WindowHandle window, HINSTANCE* outInstance, HWND* outWindow) {
    if (outInstance) {
        *outInstance = 0;
    }
    if (outWindow) {
        *outWindow = 0;
    }
    if (!window.handle || !outInstance || !outWindow) {
        return 0;
    }

    OS_WINDOWS_GraphicsEntity* entity = (OS_WINDOWS_GraphicsEntity*)window.handle;
    if (entity->type != OS_WINDOWS_GraphicsEntityType_Window || !entity->window.window) {
        return 0;
    }

    *outInstance = entity->window.instance;
    *outWindow = entity->window.window;
    return 1;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL gfx_vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                                VkDebugUtilsMessageTypeFlagsEXT type,
                                                                const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                                void* userData) {
    (void)severity;
    (void)type;
    (void)userData;
    LOG_ERROR("gfx", "Vulkan validation: {}", str8(callbackData && callbackData->pMessage ? callbackData->pMessage : "<no message>"));
    return VK_FALSE;
}

static B32 gfx_vulkan_validation_layer_available() {
    U32 layerCount = 0u;
    vkEnumerateInstanceLayerProperties(&layerCount, 0);
    if (layerCount == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    VkLayerProperties* layers = ARENA_PUSH_ARRAY(scratch.arena, VkLayerProperties, layerCount);
    if (!layers) {
        return 0;
    }
    vkEnumerateInstanceLayerProperties(&layerCount, layers);
    for (U32 i = 0u; i < layerCount; ++i) {
        if (c_str_cmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return 1;
        }
    }
    return 0;
}

static U32 gfx_vulkan_find_memory_type(GfxDevice* device, U32 typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(device->physicalDevice, &memoryProperties);
    for (U32 i = 0u; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0xffffffffu;
}

static VkFormat gfx_vulkan_format(GfxFormat format) {
    switch (format) {
        case GfxFormat_BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case GfxFormat_RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case GfxFormat_RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GfxFormat_D32_Float: return VK_FORMAT_D32_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

static U32 gfx_vulkan_format_bytes_per_pixel(GfxFormat format) {
    switch (format) {
        case GfxFormat_BGRA8_UNorm:
        case GfxFormat_RGBA8_UNorm: return 4u;
        case GfxFormat_RGBA16_Float: return 8u;
        case GfxFormat_D32_Float: return 4u;
        default: return 0u;
    }
}

static VkBufferUsageFlags gfx_vulkan_buffer_usage(U32 usageFlags) {
    VkBufferUsageFlags usage = 0u;
    if (FLAGS_HAS(usageFlags, GfxBufferUsageFlags_Vertex)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxBufferUsageFlags_Index)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxBufferUsageFlags_Uniform)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxBufferUsageFlags_Storage)) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxBufferUsageFlags_CopyDst)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return usage;
}

static VkImageUsageFlags gfx_vulkan_texture_usage(U32 usageFlags) {
    VkImageUsageFlags usage = 0u;
    if (FLAGS_HAS(usageFlags, GfxTextureUsageFlags_ColorTarget)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxTextureUsageFlags_DepthTarget)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxTextureUsageFlags_Sampled)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxTextureUsageFlags_Storage)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (FLAGS_HAS(usageFlags, GfxTextureUsageFlags_CopyDst)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return usage;
}

static VkImageAspectFlags gfx_vulkan_image_aspect(GfxFormat format) {
    return (format == GfxFormat_D32_Float) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

static VkIndexType gfx_vulkan_index_type(GfxIndexType type) {
    return (type == GfxIndexType_U16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
}

static VkPrimitiveTopology gfx_vulkan_topology(GfxPrimitiveTopology topology) {
    (void)topology;
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static VkFormat gfx_vulkan_vertex_format(GfxVertexFormat format) {
    switch (format) {
        case GfxVertexFormat_F32x2: return VK_FORMAT_R32G32_SFLOAT;
        case GfxVertexFormat_F32x3: return VK_FORMAT_R32G32B32_SFLOAT;
        case GfxVertexFormat_F32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GfxVertexFormat_U8x4_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

static VkCullModeFlags gfx_vulkan_cull_mode(GfxCullMode mode) {
    switch (mode) {
        case GfxCullMode_Front: return VK_CULL_MODE_FRONT_BIT;
        case GfxCullMode_Back: return VK_CULL_MODE_BACK_BIT;
        default: return VK_CULL_MODE_NONE;
    }
}

static VkFrontFace gfx_vulkan_front_face(GfxFrontFace face) {
    return (face == GfxFrontFace_CW) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

static VkCompareOp gfx_vulkan_compare_op(GfxCompareOp op) {
    switch (op) {
        case GfxCompareOp_Less: return VK_COMPARE_OP_LESS;
        case GfxCompareOp_LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        default: return VK_COMPARE_OP_ALWAYS;
    }
}

static VkFilter gfx_vulkan_filter(GfxFilter filter) {
    return (filter == GfxFilter_Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

static VkSamplerAddressMode gfx_vulkan_address_mode(GfxAddressMode mode) {
    return (mode == GfxAddressMode_Repeat) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

static GfxVulkanBuffer* gfx_vulkan_resolve_buffer(GfxDevice* device, GfxBuffer handle) {
    GfxVulkanBuffer* item = device ? (GfxVulkanBuffer*)slot_map_get(&device->buffers, handle.index, handle.generation) : 0;
    return item ? item : &g_gfxVulkanNilBuffer;
}

static GfxVulkanTexture* gfx_vulkan_resolve_texture(GfxDevice* device, GfxTexture handle) {
    GfxVulkanTexture* item = device ? (GfxVulkanTexture*)slot_map_get(&device->textures, handle.index, handle.generation) : 0;
    return item ? item : &g_gfxVulkanNilTexture;
}

static GfxVulkanSampler* gfx_vulkan_resolve_sampler(GfxDevice* device, GfxSampler handle) {
    GfxVulkanSampler* item = device ? (GfxVulkanSampler*)slot_map_get(&device->samplers, handle.index, handle.generation) : 0;
    return item ? item : &g_gfxVulkanNilSampler;
}

static GfxVulkanPipeline* gfx_vulkan_resolve_pipeline(GfxDevice* device, GfxPipeline handle) {
    GfxVulkanPipeline* item = device ? (GfxVulkanPipeline*)slot_map_get(&device->pipelines, handle.index, handle.generation) : 0;
    return item ? item : &g_gfxVulkanNilPipeline;
}

static B32 gfx_vulkan_begin_immediate(GfxDevice* device, VkCommandPool* outPool, VkCommandBuffer* outCommandBuffer) {
    if (!device || !outPool || !outCommandBuffer) {
        return 0;
    }

    *outPool = 0;
    *outCommandBuffer = 0;

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily;
    if (vkCreateCommandPool(device->device, &poolInfo, 0, outPool) != VK_SUCCESS) {
        return 0;
    }

    VkCommandBufferAllocateInfo commandInfo = {};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = *outPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(device->device, &commandInfo, outCommandBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(device->device, *outPool, 0);
        *outPool = 0;
        return 0;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(*outCommandBuffer, &beginInfo) != VK_SUCCESS) {
        vkDestroyCommandPool(device->device, *outPool, 0);
        *outPool = 0;
        *outCommandBuffer = 0;
        return 0;
    }

    return 1;
}

static B32 gfx_vulkan_end_immediate(GfxDevice* device, VkCommandPool commandPool, VkCommandBuffer commandBuffer) {
    if (!device || !commandPool || !commandBuffer) {
        return 0;
    }

    B32 result = 0;
    if (vkEndCommandBuffer(commandBuffer) == VK_SUCCESS) {
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1u;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(device->graphicsQueue, 1u, &submitInfo, 0) == VK_SUCCESS) {
            vkQueueWaitIdle(device->graphicsQueue);
            result = 1;
        }
    }

    vkDestroyCommandPool(device->device, commandPool, 0);
    return result;
}

static B32 gfx_vulkan_create_buffer_raw(GfxDevice* device,
                                        U64 size,
                                        VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags properties,
                                        VkBuffer* outBuffer,
                                        VkDeviceMemory* outMemory,
                                        void** outMapped) {
    if (!device || size == 0u || !outBuffer || !outMemory) {
        return 0;
    }

    *outBuffer = 0;
    *outMemory = 0;
    if (outMapped) {
        *outMapped = 0;
    }

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device->device, &bufferInfo, 0, outBuffer) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(device->device, *outBuffer, &requirements);
    U32 memoryType = gfx_vulkan_find_memory_type(device, requirements.memoryTypeBits, properties);
    if (memoryType == 0xffffffffu) {
        vkDestroyBuffer(device->device, *outBuffer, 0);
        *outBuffer = 0;
        return 0;
    }

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device->device, &allocateInfo, 0, outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device->device, *outBuffer, 0);
        *outBuffer = 0;
        return 0;
    }

    if (vkBindBufferMemory(device->device, *outBuffer, *outMemory, 0u) != VK_SUCCESS) {
        vkFreeMemory(device->device, *outMemory, 0);
        vkDestroyBuffer(device->device, *outBuffer, 0);
        *outMemory = 0;
        *outBuffer = 0;
        return 0;
    }

    if (outMapped && FLAGS_HAS(properties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        if (vkMapMemory(device->device, *outMemory, 0u, size, 0u, outMapped) != VK_SUCCESS) {
            vkFreeMemory(device->device, *outMemory, 0);
            vkDestroyBuffer(device->device, *outBuffer, 0);
            *outMemory = 0;
            *outBuffer = 0;
            *outMapped = 0;
            return 0;
        }
    }

    return 1;
}

static void gfx_vulkan_destroy_buffer_item(GfxDevice* device, GfxVulkanBuffer* item) {
    if (!device || !item) {
        return;
    }
    if (item->mapped) {
        vkUnmapMemory(device->device, item->memory);
        item->mapped = 0;
    }
    if (item->buffer) {
        vkDestroyBuffer(device->device, item->buffer, 0);
        item->buffer = 0;
    }
    if (item->memory) {
        vkFreeMemory(device->device, item->memory, 0);
        item->memory = 0;
    }
}

static void gfx_vulkan_destroy_retired_buffer(GfxDevice* device, GfxVulkanRetiredBuffer* item) {
    if (!device || !item) {
        return;
    }
    gfx_vulkan_resource_table_clear(device, item->resourceId);
    item->resourceId = {};
    if (item->mapped) {
        vkUnmapMemory(device->device, item->memory);
        item->mapped = 0;
    }
    if (item->buffer) {
        vkDestroyBuffer(device->device, item->buffer, 0);
        item->buffer = 0;
    }
    if (item->memory) {
        vkFreeMemory(device->device, item->memory, 0);
        item->memory = 0;
    }
}

static void gfx_vulkan_destroy_retired_texture(GfxDevice* device, GfxVulkanRetiredTexture* item) {
    if (!device || !item) {
        return;
    }
    gfx_vulkan_resource_table_clear(device, item->resourceId);
    item->resourceId = {};
    if (item->view) {
        vkDestroyImageView(device->device, item->view, 0);
        item->view = 0;
    }
    if (item->image) {
        vkDestroyImage(device->device, item->image, 0);
        item->image = 0;
    }
    if (item->memory) {
        vkFreeMemory(device->device, item->memory, 0);
        item->memory = 0;
    }
}

static void gfx_vulkan_destroy_retired_sampler(GfxDevice* device, GfxVulkanRetiredSampler* item) {
    if (!device || !item) {
        return;
    }
    gfx_vulkan_resource_table_clear(device, item->resourceId);
    item->resourceId = {};
    if (item->sampler) {
        vkDestroySampler(device->device, item->sampler, 0);
        item->sampler = 0;
    }
}

static void gfx_vulkan_destroy_retired_pipeline(GfxDevice* device, GfxVulkanRetiredPipeline* item) {
    if (!device || !item || !item->pipeline) {
        return;
    }
    vkDestroyPipeline(device->device, item->pipeline, 0);
    item->pipeline = 0;
}

static U64 gfx_vulkan_retire_serial(GfxDevice* device) {
    return device ? device->frameSerial : 0u;
}

static void gfx_vulkan_retire_buffer(GfxDevice* device, GfxVulkanBuffer* item) {
    if (!device || !item || !item->buffer) {
        return;
    }
    if (device->completedFrameSerial >= gfx_vulkan_retire_serial(device)) {
        gfx_vulkan_destroy_buffer_item(device, item);
        gfx_vulkan_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    ASSERT_DEBUG(device->retiredBufferCount < GFX_VULKAN_RETIRED_BUFFER_CAPACITY);
    if (device->retiredBufferCount >= GFX_VULKAN_RETIRED_BUFFER_CAPACITY) {
        LOG_WARNING("gfx", "Vulkan retired buffer queue full; buffer will stay alive until process exit");
        item->buffer = 0;
        item->memory = 0;
        item->mapped = 0;
        return;
    }

    GfxVulkanRetiredBuffer* retired = &device->retiredBuffers[device->retiredBufferCount++];
    retired->buffer = item->buffer;
    retired->memory = item->memory;
    retired->mapped = item->mapped;
    retired->resourceId = item->resourceId;
    retired->retireSerial = gfx_vulkan_retire_serial(device);
    item->buffer = 0;
    item->memory = 0;
    item->mapped = 0;
    item->resourceId = {};
}

static void gfx_vulkan_retire_texture(GfxDevice* device, GfxVulkanTexture* item) {
    if (!device || !item || !item->image) {
        return;
    }
    if (!item->ownsImage) {
        gfx_vulkan_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    if (device->completedFrameSerial >= gfx_vulkan_retire_serial(device)) {
        if (item->view) {
            vkDestroyImageView(device->device, item->view, 0);
            item->view = 0;
        }
        vkDestroyImage(device->device, item->image, 0);
        item->image = 0;
        if (item->memory) {
            vkFreeMemory(device->device, item->memory, 0);
            item->memory = 0;
        }
        gfx_vulkan_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    ASSERT_DEBUG(device->retiredTextureCount < GFX_VULKAN_RETIRED_TEXTURE_CAPACITY);
    if (device->retiredTextureCount >= GFX_VULKAN_RETIRED_TEXTURE_CAPACITY) {
        LOG_WARNING("gfx", "Vulkan retired texture queue full; texture will stay alive until process exit");
        item->image = 0;
        item->memory = 0;
        item->view = 0;
        return;
    }

    GfxVulkanRetiredTexture* retired = &device->retiredTextures[device->retiredTextureCount++];
    retired->image = item->image;
    retired->memory = item->memory;
    retired->view = item->view;
    retired->resourceId = item->resourceId;
    retired->retireSerial = gfx_vulkan_retire_serial(device);
    item->image = 0;
    item->memory = 0;
    item->view = 0;
    item->resourceId = {};
}

static void gfx_vulkan_retire_sampler(GfxDevice* device, GfxVulkanSampler* item) {
    if (!device || !item || !item->sampler) {
        return;
    }
    if (device->completedFrameSerial >= gfx_vulkan_retire_serial(device)) {
        vkDestroySampler(device->device, item->sampler, 0);
        item->sampler = 0;
        gfx_vulkan_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    ASSERT_DEBUG(device->retiredSamplerCount < GFX_VULKAN_RETIRED_SAMPLER_CAPACITY);
    if (device->retiredSamplerCount >= GFX_VULKAN_RETIRED_SAMPLER_CAPACITY) {
        LOG_WARNING("gfx", "Vulkan retired sampler queue full; sampler will stay alive until process exit");
        item->sampler = 0;
        return;
    }

    GfxVulkanRetiredSampler* retired = &device->retiredSamplers[device->retiredSamplerCount++];
    retired->sampler = item->sampler;
    retired->resourceId = item->resourceId;
    retired->retireSerial = gfx_vulkan_retire_serial(device);
    item->sampler = 0;
    item->resourceId = {};
}

static void gfx_vulkan_retire_pipeline(GfxDevice* device, GfxVulkanPipeline* item) {
    if (!device || !item || !item->pipeline) {
        return;
    }
    if (device->completedFrameSerial >= gfx_vulkan_retire_serial(device)) {
        vkDestroyPipeline(device->device, item->pipeline, 0);
        item->pipeline = 0;
        return;
    }
    ASSERT_DEBUG(device->retiredPipelineCount < GFX_VULKAN_RETIRED_PIPELINE_CAPACITY);
    if (device->retiredPipelineCount >= GFX_VULKAN_RETIRED_PIPELINE_CAPACITY) {
        LOG_WARNING("gfx", "Vulkan retired pipeline queue full; pipeline will stay alive until process exit");
        item->pipeline = 0;
        return;
    }

    GfxVulkanRetiredPipeline* retired = &device->retiredPipelines[device->retiredPipelineCount++];
    retired->pipeline = item->pipeline;
    retired->retireSerial = gfx_vulkan_retire_serial(device);
    item->pipeline = 0;
}

static void gfx_vulkan_drain_retired(GfxDevice* device) {
    if (!device || !device->device) {
        return;
    }

    for (U32 i = 0u; i < device->retiredBufferCount;) {
        GfxVulkanRetiredBuffer* item = &device->retiredBuffers[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_vulkan_destroy_retired_buffer(device, item);
        device->retiredBuffers[i] = device->retiredBuffers[--device->retiredBufferCount];
    }

    for (U32 i = 0u; i < device->retiredTextureCount;) {
        GfxVulkanRetiredTexture* item = &device->retiredTextures[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_vulkan_destroy_retired_texture(device, item);
        device->retiredTextures[i] = device->retiredTextures[--device->retiredTextureCount];
    }

    for (U32 i = 0u; i < device->retiredSamplerCount;) {
        GfxVulkanRetiredSampler* item = &device->retiredSamplers[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_vulkan_destroy_retired_sampler(device, item);
        device->retiredSamplers[i] = device->retiredSamplers[--device->retiredSamplerCount];
    }

    for (U32 i = 0u; i < device->retiredPipelineCount;) {
        GfxVulkanRetiredPipeline* item = &device->retiredPipelines[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_vulkan_destroy_retired_pipeline(device, item);
        device->retiredPipelines[i] = device->retiredPipelines[--device->retiredPipelineCount];
    }
}

static B32 gfx_vulkan_copy_buffer_immediate(GfxDevice* device, VkBuffer src, VkBuffer dst, U64 dstOffset, U64 size) {
    VkCommandPool commandPool = 0;
    VkCommandBuffer commandBuffer = 0;
    if (!gfx_vulkan_begin_immediate(device, &commandPool, &commandBuffer)) {
        return 0;
    }

    VkBufferCopy copy = {};
    copy.srcOffset = 0u;
    copy.dstOffset = dstOffset;
    copy.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1u, &copy);

    VkBufferMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.buffer = dst;
    barrier.offset = dstOffset;
    barrier.size = size;

    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    return gfx_vulkan_end_immediate(device, commandPool, commandBuffer);
}

static GfxBuffer gfx_vulkan_create_buffer_internal(GfxDevice* device, const GfxBufferDesc* desc, B32 internal) {
    if (!device || !desc || desc->size == 0u) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid buffer descriptor");
        }
        return {};
    }

    if (desc->initialData && desc->memoryKind == GfxMemoryKind_Device) {
        LOG_ERROR("gfx", "Vulkan device-local initialData must be uploaded through a frame upload path");
        return {};
    }

    VkBufferUsageFlags usage = gfx_vulkan_buffer_usage(desc->usageFlags);
    if (desc->memoryKind == GfxMemoryKind_Upload) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (usage == 0u) {
        usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    VkMemoryPropertyFlags memoryProperties = (desc->memoryKind == GfxMemoryKind_Upload) ?
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) :
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkBuffer buffer = 0;
    VkDeviceMemory memory = 0;
    void* mapped = 0;
    if (!gfx_vulkan_create_buffer_raw(device, desc->size, usage, memoryProperties, &buffer, &memory, &mapped)) {
        LOG_ERROR("gfx", "Vulkan buffer creation failed (size={})", desc->size);
        return {};
    }

    if (desc->initialData && desc->memoryKind == GfxMemoryKind_Upload) {
        ASSERT_DEBUG(mapped != 0);
        if (!mapped) {
            vkFreeMemory(device->device, memory, 0);
            vkDestroyBuffer(device->device, buffer, 0);
            return {};
        }
        MEMCPY(mapped, desc->initialData, desc->size);
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->buffers, &slotItem, &slotIndex, &generation)) {
        if (mapped) {
            vkUnmapMemory(device->device, memory);
        }
        vkFreeMemory(device->device, memory, 0);
        vkDestroyBuffer(device->device, buffer, 0);
        return {};
    }

    GfxVulkanBuffer* item = (GfxVulkanBuffer*)slotItem;
    item->buffer = buffer;
    item->memory = memory;
    item->mapped = mapped;
    item->size = desc->size;
    item->usageFlags = desc->usageFlags;
    item->memoryKind = desc->memoryKind;
    item->resourceId = {};
    item->internal = internal;

    GfxBuffer result = {slotIndex, generation};
    return result;
}

static B32 gfx_vulkan_create_nil_resources(GfxDevice* device) {
    if (!device) {
        return 0;
    }

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {1u, 1u, 1u};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device->device, &imageInfo, 0, &device->nilImage) != VK_SUCCESS) {
        return 0;
    }

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(device->device, device->nilImage, &requirements);
    U32 memoryType = gfx_vulkan_find_memory_type(device, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == 0xffffffffu) {
        return 0;
    }

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device->device, &allocateInfo, 0, &device->nilImageMemory) != VK_SUCCESS) {
        return 0;
    }
    if (vkBindImageMemory(device->device, device->nilImage, device->nilImageMemory, 0u) != VK_SUCCESS) {
        return 0;
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = device->nilImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1u;
    viewInfo.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(device->device, &viewInfo, 0, &device->nilImageView) != VK_SUCCESS) {
        return 0;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(device->device, &samplerInfo, 0, &device->nilSampler) != VK_SUCCESS) {
        return 0;
    }

    VkCommandPool commandPool = 0;
    VkCommandBuffer commandBuffer = 0;
    if (!gfx_vulkan_begin_immediate(device, &commandPool, &commandBuffer)) {
        return 0;
    }

    VkImageMemoryBarrier2 toTransfer = {};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.image = device->nilImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1u;
    toTransfer.subresourceRange.layerCount = 1u;

    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    VkClearColorValue clear = {};
    clear.float32[0] = 1.0f;
    clear.float32[1] = 1.0f;
    clear.float32[2] = 1.0f;
    clear.float32[3] = 1.0f;
    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1u;
    range.layerCount = 1u;
    vkCmdClearColorImage(commandBuffer, device->nilImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1u, &range);

    VkImageMemoryBarrier2 toSampled = {};
    toSampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toSampled.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toSampled.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toSampled.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toSampled.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toSampled.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSampled.image = device->nilImage;
    toSampled.subresourceRange = range;
    dependency.pImageMemoryBarriers = &toSampled;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    return gfx_vulkan_end_immediate(device, commandPool, commandBuffer);
}

static void gfx_vulkan_destroy_nil_resources(GfxDevice* device) {
    if (!device || !device->device) {
        return;
    }
    if (device->nilSampler) {
        vkDestroySampler(device->device, device->nilSampler, 0);
        device->nilSampler = 0;
    }
    if (device->nilImageView) {
        vkDestroyImageView(device->device, device->nilImageView, 0);
        device->nilImageView = 0;
    }
    if (device->nilImage) {
        vkDestroyImage(device->device, device->nilImage, 0);
        device->nilImage = 0;
    }
    if (device->nilImageMemory) {
        vkFreeMemory(device->device, device->nilImageMemory, 0);
        device->nilImageMemory = 0;
    }
}

static B32 gfx_vulkan_resource_table_init(GfxDevice* device) {
    if (!device || !device->nilImageView || !device->nilSampler) {
        return 0;
    }

    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = GFX_VULKAN_RESOURCE_BINDING_TEXTURES;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = GFX_VULKAN_RESOURCE_BINDING_SAMPLERS;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorBindingFlags bindingFlags[2] = {
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = ARRAY_COUNT(bindingFlags);
    bindingFlagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = ARRAY_COUNT(bindings);
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device->device, &layoutInfo, 0, &device->resourceSetLayout) != VK_SUCCESS) {
        return 0;
    }

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[0].descriptorCount = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[1].descriptorCount = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1u;
    poolInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device->device, &poolInfo, 0, &device->resourceDescriptorPool) != VK_SUCCESS) {
        return 0;
    }

    VkDescriptorSetAllocateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = device->resourceDescriptorPool;
    setInfo.descriptorSetCount = 1u;
    setInfo.pSetLayouts = &device->resourceSetLayout;
    if (vkAllocateDescriptorSets(device->device, &setInfo, &device->resourceSet) != VK_SUCCESS) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    VkDescriptorImageInfo* textureInfos = ARENA_PUSH_ARRAY(scratch.arena, VkDescriptorImageInfo, GFX_VULKAN_RESOURCE_TABLE_CAPACITY);
    VkDescriptorImageInfo* samplerInfos = ARENA_PUSH_ARRAY(scratch.arena, VkDescriptorImageInfo, GFX_VULKAN_RESOURCE_TABLE_CAPACITY);
    if (!textureInfos || !samplerInfos) {
        return 0;
    }
    for (U32 i = 0u; i < GFX_VULKAN_RESOURCE_TABLE_CAPACITY; ++i) {
        textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        textureInfos[i].imageView = device->nilImageView;
        samplerInfos[i].sampler = device->nilSampler;
    }

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = device->resourceSet;
    writes[0].dstBinding = GFX_VULKAN_RESOURCE_BINDING_TEXTURES;
    writes[0].descriptorCount = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = textureInfos;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = device->resourceSet;
    writes[1].dstBinding = GFX_VULKAN_RESOURCE_BINDING_SAMPLERS;
    writes[1].descriptorCount = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = samplerInfos;
    vkUpdateDescriptorSets(device->device, ARRAY_COUNT(writes), writes, 0u, 0);

    return 1;
}

static void gfx_vulkan_resource_table_set_texture(GfxDevice* device, GfxResourceId resourceId, VkImageView view) {
    if (!device || !device->resourceSet || resourceId.index == 0u || resourceId.index >= GFX_VULKAN_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view ? view : device->nilImageView;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = device->resourceSet;
    write.dstBinding = GFX_VULKAN_RESOURCE_BINDING_TEXTURES;
    write.dstArrayElement = resourceId.index;
    write.descriptorCount = 1u;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device->device, 1u, &write, 0u, 0);
}

static void gfx_vulkan_resource_table_set_sampler(GfxDevice* device, GfxResourceId resourceId, VkSampler sampler) {
    if (!device || !device->resourceSet || resourceId.index == 0u || resourceId.index >= GFX_VULKAN_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.sampler = sampler ? sampler : device->nilSampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = device->resourceSet;
    write.dstBinding = GFX_VULKAN_RESOURCE_BINDING_SAMPLERS;
    write.dstArrayElement = resourceId.index;
    write.descriptorCount = 1u;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device->device, 1u, &write, 0u, 0);
}

static void gfx_vulkan_resource_table_clear(GfxDevice* device, GfxResourceId resourceId) {
    if (!device || !device->resourceEntries ||
        resourceId.index == 0u ||
        resourceId.index >= device->resourceCount ||
        resourceId.index >= GFX_VULKAN_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    GfxVulkanResourceEntry* entry = &device->resourceEntries[resourceId.index];
    if (entry->kind == GfxVulkanResourceKind_Invalid) {
        return;
    }
    if (entry->kind == GfxVulkanResourceKind_Texture) {
        gfx_vulkan_resource_table_set_texture(device, resourceId, device->nilImageView);
    } else if (entry->kind == GfxVulkanResourceKind_Sampler) {
        gfx_vulkan_resource_table_set_sampler(device, resourceId, device->nilSampler);
    }

    MEMSET(entry, 0, sizeof(*entry));
    entry->nextFree = device->resourceFreeHead;
    device->resourceFreeHead = resourceId.index;
    if (device->resourceLiveCount > 0u) {
        device->resourceLiveCount -= 1u;
    }
    device->stats.resourceTableCount = device->resourceLiveCount;
}

static B32 gfx_vulkan_resource_entries_reserve(GfxDevice* device, U32 neededCapacity) {
    if (!device) {
        return 0;
    }
    if (neededCapacity <= device->resourceCapacity) {
        return 1;
    }
    if (neededCapacity > GFX_VULKAN_RESOURCE_TABLE_CAPACITY) {
        ASSERT_DEBUG(neededCapacity <= GFX_VULKAN_RESOURCE_TABLE_CAPACITY);
        LOG_ERROR("gfx", "Vulkan resource table is full");
        return 0;
    }

    U32 newCapacity = GFX_VULKAN_RESOURCE_TABLE_CAPACITY;
    GfxVulkanResourceEntry* newEntries = ARENA_PUSH_ARRAY(device->arena, GfxVulkanResourceEntry, newCapacity);
    if (!newEntries) {
        return 0;
    }
    MEMSET(newEntries, 0, sizeof(GfxVulkanResourceEntry) * newCapacity);
    if (device->resourceEntries && device->resourceCount > 0u) {
        MEMCPY(newEntries, device->resourceEntries, sizeof(GfxVulkanResourceEntry) * device->resourceCount);
    }

    device->resourceEntries = newEntries;
    device->resourceCapacity = newCapacity;
    return 1;
}

static GfxResourceId gfx_vulkan_register_resource(GfxDevice* device, GfxVulkanResourceKind kind, U32 index, U32 generation) {
    if (!device || generation == 0u) {
        return {};
    }
    if (device->resourceCount == 0u) {
        device->resourceCount = 1u;
    }
    U32 resourceIndex = 0u;
    if (device->resourceFreeHead != 0u) {
        resourceIndex = device->resourceFreeHead;
        GfxVulkanResourceEntry* freeEntry = &device->resourceEntries[resourceIndex];
        device->resourceFreeHead = freeEntry->nextFree;
    } else {
        if (device->resourceCount >= GFX_VULKAN_RESOURCE_TABLE_CAPACITY) {
            ASSERT_DEBUG(device->resourceCount < GFX_VULKAN_RESOURCE_TABLE_CAPACITY);
            LOG_ERROR("gfx", "Vulkan resource table is full");
            return {};
        }
        resourceIndex = device->resourceCount;
    }

    if (!gfx_vulkan_resource_entries_reserve(device, resourceIndex + 1u)) {
        return {};
    }
    if (resourceIndex == device->resourceCount) {
        device->resourceCount += 1u;
    }

    GfxVulkanResourceEntry* entry = &device->resourceEntries[resourceIndex];
    entry->kind = kind;
    entry->index = index;
    entry->generation = generation;
    entry->nextFree = 0u;
    device->resourceLiveCount += 1u;
    device->stats.resourceTableCount = device->resourceLiveCount;

    GfxResourceId result = {resourceIndex};
    return result;
}

static B32 gfx_vulkan_create_buffer_set_layout(GfxDevice* device) {
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = GFX_SHADER_SLOT_DRAW_DATA;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1u;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = GFX_SHADER_SLOT_PASS_DATA;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1u;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = ARRAY_COUNT(bindings);
    layoutInfo.pBindings = bindings;
    return vkCreateDescriptorSetLayout(device->device, &layoutInfo, 0, &device->bufferSetLayout) == VK_SUCCESS;
}

static B32 gfx_vulkan_create_pipeline_layout(GfxDevice* device) {
    VkDescriptorSetLayout layouts[2] = {
        device->resourceSetLayout,
        device->bufferSetLayout,
    };

    VkPushConstantRange push = {};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    push.offset = 0u;
    push.size = sizeof(GfxVulkanPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = ARRAY_COUNT(layouts);
    layoutInfo.pSetLayouts = layouts;
    layoutInfo.pushConstantRangeCount = 1u;
    layoutInfo.pPushConstantRanges = &push;
    return vkCreatePipelineLayout(device->device, &layoutInfo, 0, &device->pipelineLayout) == VK_SUCCESS;
}

static B32 gfx_vulkan_create_frame_descriptor_pool(GfxDevice* device, GfxFrame* frame) {
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 512u;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 256u;
    poolInfo.poolSizeCount = 1u;
    poolInfo.pPoolSizes = &poolSize;
    return vkCreateDescriptorPool(device->device, &poolInfo, 0, &frame->descriptorPool) == VK_SUCCESS;
}

static VkDescriptorSet gfx_vulkan_alloc_buffer_set(GfxFrame* frame, GfxBuffer dataBuffer, GfxBuffer passBuffer) {
    if (!frame || !frame->device || !frame->descriptorPool) {
        return 0;
    }

    GfxDevice* device = frame->device;
    GfxVulkanBuffer* data = gfx_vulkan_resolve_buffer(device, dataBuffer);
    GfxVulkanBuffer* pass = gfx_vulkan_resolve_buffer(device, passBuffer);
    if (!data->buffer || !pass->buffer) {
        return 0;
    }

    VkDescriptorSet set = 0;
    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = frame->descriptorPool;
    allocateInfo.descriptorSetCount = 1u;
    allocateInfo.pSetLayouts = &device->bufferSetLayout;
    if (vkAllocateDescriptorSets(device->device, &allocateInfo, &set) != VK_SUCCESS) {
        return 0;
    }

    VkDescriptorBufferInfo bufferInfos[2] = {};
    bufferInfos[0].buffer = data->buffer;
    bufferInfos[0].offset = 0u;
    bufferInfos[0].range = data->size;
    bufferInfos[1].buffer = pass->buffer;
    bufferInfos[1].offset = 0u;
    bufferInfos[1].range = pass->size;

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = GFX_SHADER_SLOT_DRAW_DATA;
    writes[0].descriptorCount = 1u;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bufferInfos[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = GFX_SHADER_SLOT_PASS_DATA;
    writes[1].descriptorCount = 1u;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &bufferInfos[1];
    vkUpdateDescriptorSets(device->device, ARRAY_COUNT(writes), writes, 0u, 0);

    return set;
}

static B32 gfx_vulkan_create_temp_buffer(GfxDevice* device, GfxFrame* frame, U64 size) {
    GfxBufferDesc tempDesc = {};
    tempDesc.name = "gfx temp buffer";
    tempDesc.size = size;
    tempDesc.usageFlags = GfxBufferUsageFlags_Uniform | GfxBufferUsageFlags_Storage;
    tempDesc.memoryKind = GfxMemoryKind_Upload;
    frame->tempBuffer = gfx_vulkan_create_buffer_internal(device, &tempDesc, 1);

    GfxVulkanBuffer* buffer = gfx_vulkan_resolve_buffer(device, frame->tempBuffer);
    if (!buffer->buffer || !buffer->mapped) {
        return 0;
    }

    frame->tempCpu = buffer->mapped;
    frame->tempSize = size;
    return 1;
}

static void gfx_vulkan_destroy_swapchain(GfxDevice* device) {
    if (!device || !device->device) {
        return;
    }
    if (device->swapchainImageViews) {
        for (U32 i = 0u; i < device->swapchainImageCount; ++i) {
            if (device->swapchainImageViews[i]) {
                vkDestroyImageView(device->device, device->swapchainImageViews[i], 0);
            }
        }
    }
    if (device->swapchain) {
        vkDestroySwapchainKHR(device->device, device->swapchain, 0);
    }
    device->swapchain = 0;
    device->swapchainImages = 0;
    device->swapchainImageViews = 0;
    device->swapchainImageCount = 0u;
}

static B32 gfx_vulkan_create_swapchain(GfxDevice* device, U32 width, U32 height) {
    VkSurfaceCapabilitiesKHR capabilities = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physicalDevice, device->surface, &capabilities) != VK_SUCCESS) {
        return 0;
    }

    U32 formatCount = 0u;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, device->surface, &formatCount, 0);
    if (formatCount == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    VkSurfaceFormatKHR* formats = ARENA_PUSH_ARRAY(scratch.arena, VkSurfaceFormatKHR, formatCount);
    if (!formats) {
        return 0;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, device->surface, &formatCount, formats);

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (U32 i = 0u; i < formatCount; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = formats[i];
            break;
        }
    }

    VkExtent2D extent = {};
    if (capabilities.currentExtent.width != 0xffffffffu) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = width;
        extent.height = height;
        if (extent.width < capabilities.minImageExtent.width) {
            extent.width = capabilities.minImageExtent.width;
        }
        if (extent.height < capabilities.minImageExtent.height) {
            extent.height = capabilities.minImageExtent.height;
        }
        if (extent.width > capabilities.maxImageExtent.width) {
            extent.width = capabilities.maxImageExtent.width;
        }
        if (extent.height > capabilities.maxImageExtent.height) {
            extent.height = capabilities.maxImageExtent.height;
        }
    }

    U32 imageCount = capabilities.minImageCount + 1u;
    if (capabilities.maxImageCount != 0u && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    U32 queueFamilies[2] = {device->graphicsQueueFamily, device->presentQueueFamily};
    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = device->surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = chosenFormat.format;
    swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1u;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    if (device->graphicsQueueFamily != device->presentQueueFamily) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = 2u;
        swapchainInfo.pQueueFamilyIndices = queueFamilies;
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(device->device, &swapchainInfo, 0, &device->swapchain) != VK_SUCCESS) {
        return 0;
    }

    vkGetSwapchainImagesKHR(device->device, device->swapchain, &device->swapchainImageCount, 0);
    device->swapchainImages = ARENA_PUSH_ARRAY(device->arena, VkImage, device->swapchainImageCount);
    device->swapchainImageViews = ARENA_PUSH_ARRAY(device->arena, VkImageView, device->swapchainImageCount);
    if (!device->swapchainImages || !device->swapchainImageViews) {
        return 0;
    }
    MEMSET(device->swapchainImageViews, 0, sizeof(VkImageView) * device->swapchainImageCount);
    vkGetSwapchainImagesKHR(device->device, device->swapchain, &device->swapchainImageCount, device->swapchainImages);

    for (U32 i = 0u; i < device->swapchainImageCount; ++i) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = device->swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = chosenFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0u;
        viewInfo.subresourceRange.levelCount = 1u;
        viewInfo.subresourceRange.baseArrayLayer = 0u;
        viewInfo.subresourceRange.layerCount = 1u;
        if (vkCreateImageView(device->device, &viewInfo, 0, &device->swapchainImageViews[i]) != VK_SUCCESS) {
            return 0;
        }
    }

    device->swapchainFormat = chosenFormat.format;
    device->swapchainExtent = extent;

    GfxVulkanTexture* backbuffer = gfx_vulkan_resolve_texture(device, device->backbuffer);
    backbuffer->width = extent.width;
    backbuffer->height = extent.height;
    backbuffer->mipCount = 1u;
    backbuffer->format = GfxFormat_BGRA8_UNorm;
    backbuffer->usageFlags = GfxTextureUsageFlags_ColorTarget;
    backbuffer->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    return 1;
}

static B32 gfx_vulkan_create_frame_objects(GfxDevice* device, GfxFrame* frame, U64 tempSize) {
    frame->device = device;
    frame->commands.frame = frame;

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device->graphicsQueueFamily;
    if (vkCreateCommandPool(device->device, &poolInfo, 0, &frame->commandPool) != VK_SUCCESS) {
        return 0;
    }

    VkCommandBufferAllocateInfo commandInfo = {};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = frame->commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(device->device, &commandInfo, &frame->commandBuffer) != VK_SUCCESS) {
        return 0;
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device->device, &fenceInfo, 0, &frame->fence) != VK_SUCCESS) {
        return 0;
    }

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device->device, &semaphoreInfo, 0, &frame->imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device->device, &semaphoreInfo, 0, &frame->renderFinishedSemaphore) != VK_SUCCESS) {
        return 0;
    }

    if (!gfx_vulkan_create_frame_descriptor_pool(device, frame)) {
        return 0;
    }

    return gfx_vulkan_create_temp_buffer(device, frame, tempSize);
}

static void gfx_vulkan_destroy_frame_objects(GfxDevice* device, GfxFrame* frame) {
    if (!device || !frame || !device->device) {
        return;
    }
    frame->tempCpu = 0;
    frame->tempBuffer = {};
    if (frame->descriptorPool) {
        vkDestroyDescriptorPool(device->device, frame->descriptorPool, 0);
    }
    if (frame->imageAvailableSemaphore) {
        vkDestroySemaphore(device->device, frame->imageAvailableSemaphore, 0);
    }
    if (frame->renderFinishedSemaphore) {
        vkDestroySemaphore(device->device, frame->renderFinishedSemaphore, 0);
    }
    if (frame->fence) {
        vkDestroyFence(device->device, frame->fence, 0);
    }
    if (frame->commandPool) {
        vkDestroyCommandPool(device->device, frame->commandPool, 0);
    }
    MEMSET(frame, 0, sizeof(*frame));
}

static VkAttachmentLoadOp gfx_vulkan_load_op(GfxLoadOp op) {
    if (op == GfxLoadOp_Load) {
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    if (op == GfxLoadOp_Clear) {
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkAttachmentStoreOp gfx_vulkan_store_op(GfxStoreOp op) {
    return (op == GfxStoreOp_Store) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

B32 gfx_device_create(const GfxDeviceDesc* desc, Arena* arena, GfxDevice** outDevice) {
    if (!desc || !arena || !outDevice) {
        return 0;
    }
    *outDevice = 0;
    if (desc->backend != GfxBackend_Vulkan) {
        LOG_ERROR("gfx", "Only Vulkan backend is implemented in this build");
        return 0;
    }

    HINSTANCE win32Instance = 0;
    HWND win32Window = 0;
    if (!gfx_vulkan_window_from_handle(desc->window, &win32Instance, &win32Window)) {
        LOG_ERROR("gfx", "Window is not a Win32 window");
        return 0;
    }

    GfxDevice* device = ARENA_PUSH_STRUCT(arena, GfxDevice);
    if (!device) {
        return 0;
    }
    MEMSET(device, 0, sizeof(*device));
    device->backend = GfxBackend_Vulkan;
    device->arena = arena;
    device->validationFlags = desc->validationFlags;
    device->win32Instance = win32Instance;
    device->win32Window = win32Window;
    device->framesInFlight = desc->framesInFlight ? desc->framesInFlight : GFX_VULKAN_DEFAULT_FRAMES_IN_FLIGHT;

    B32 backendValidation = FLAGS_HAS(desc->validationFlags, GfxValidationFlags_Backend) && gfx_vulkan_validation_layer_available();
    const char* extensions[3] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    const char* layers[1] = {"VK_LAYER_KHRONOS_validation"};

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Utilities";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = backendValidation ? 3u : 2u;
    instanceInfo.ppEnabledExtensionNames = extensions;
    if (backendValidation) {
        instanceInfo.enabledLayerCount = 1u;
        instanceInfo.ppEnabledLayerNames = layers;
    }
    VkResult instanceResult = vkCreateInstance(&instanceInfo, 0, &device->instance);
    if (instanceResult != VK_SUCCESS) {
        LOG_ERROR("gfx",
                  "Vulkan instance creation failed (VkResult {}); no Vulkan driver/ICD may be installed or usable",
                  (S32)instanceResult);
        return 0;
    }
    if (backendValidation) {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = gfx_vulkan_debug_callback;
        PFN_vkCreateDebugUtilsMessengerEXT createDebugMessenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(device->instance, "vkCreateDebugUtilsMessengerEXT");
        if (createDebugMessenger) {
            createDebugMessenger(device->instance, &debugInfo, 0, &device->debugMessenger);
        }
    }

    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = win32Instance;
    surfaceInfo.hwnd = win32Window;
    if (vkCreateWin32SurfaceKHR(device->instance, &surfaceInfo, 0, &device->surface) != VK_SUCCESS) {
        LOG_ERROR("gfx", "Vulkan Win32 surface creation failed");
        gfx_device_destroy(device);
        return 0;
    }
    U32 physicalDeviceCount = 0u;
    VkResult enumeratePhysicalDevicesResult = vkEnumeratePhysicalDevices(device->instance, &physicalDeviceCount, 0);
    if (enumeratePhysicalDevicesResult != VK_SUCCESS) {
        LOG_ERROR("gfx",
                  "Vulkan physical device enumeration failed (VkResult {}); no Vulkan driver/device may be usable",
                  (S32)enumeratePhysicalDevicesResult);
        gfx_device_destroy(device);
        return 0;
    }
    if (physicalDeviceCount == 0u) {
        LOG_ERROR("gfx", "No Vulkan physical devices available; no Vulkan driver/device may be usable");
        gfx_device_destroy(device);
        return 0;
    }

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        gfx_device_destroy(device);
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    VkPhysicalDevice* physicalDevices = ARENA_PUSH_ARRAY(scratch.arena, VkPhysicalDevice, physicalDeviceCount);
    vkEnumeratePhysicalDevices(device->instance, &physicalDeviceCount, physicalDevices);
    device->graphicsQueueFamily = 0xffffffffu;
    device->presentQueueFamily = 0xffffffffu;

    for (U32 deviceIndex = 0u; deviceIndex < physicalDeviceCount; ++deviceIndex) {
        VkPhysicalDeviceProperties deviceProperties = {};
        vkGetPhysicalDeviceProperties(physicalDevices[deviceIndex], &deviceProperties);
        if (deviceProperties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features vulkan13Features = {};
        vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures = {};
        descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        descriptorIndexingFeatures.pNext = &vulkan13Features;

        VkPhysicalDeviceFeatures2 features = {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &descriptorIndexingFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevices[deviceIndex], &features);
        if (!vulkan13Features.dynamicRendering ||
            !vulkan13Features.synchronization2 ||
            !descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind ||
            !descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending) {
            continue;
        }

        U32 queueFamilyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[deviceIndex], &queueFamilyCount, 0);
        VkQueueFamilyProperties* queueFamilies = ARENA_PUSH_ARRAY(scratch.arena, VkQueueFamilyProperties, queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[deviceIndex], &queueFamilyCount, queueFamilies);

        for (U32 queueIndex = 0u; queueIndex < queueFamilyCount; ++queueIndex) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[deviceIndex], queueIndex, device->surface, &presentSupported);
            if ((queueFamilies[queueIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
                device->physicalDevice = physicalDevices[deviceIndex];
                device->graphicsQueueFamily = queueIndex;
                device->presentQueueFamily = queueIndex;
                break;
            }
        }
        if (device->physicalDevice) {
            break;
        }
    }
    if (!device->physicalDevice) {
        LOG_ERROR("gfx", "No Vulkan 1.3 device with graphics/present queue, dynamicRendering, synchronization2, and descriptor update-after-bind support");
        gfx_device_destroy(device);
        return 0;
    }

    F32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = device->graphicsQueueFamily;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features vulkan13Features = {};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures = {};
    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorIndexingFeatures.pNext = &vulkan13Features;
    descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &descriptorIndexingFeatures;
    deviceInfo.queueCreateInfoCount = 1u;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = ARRAY_COUNT(deviceExtensions);
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    if (vkCreateDevice(device->physicalDevice, &deviceInfo, 0, &device->device) != VK_SUCCESS) {
        LOG_ERROR("gfx", "Vulkan device creation failed");
        gfx_device_destroy(device);
        return 0;
    }
    vkGetDeviceQueue(device->device, device->graphicsQueueFamily, 0u, &device->graphicsQueue);
    device->presentQueue = device->graphicsQueue;
    device->resourceCount = 1u;

    if (!slot_map_init(&device->buffers, arena, sizeof(GfxVulkanBuffer), 128u) ||
        !slot_map_init(&device->textures, arena, sizeof(GfxVulkanTexture), 128u) ||
        !slot_map_init(&device->samplers, arena, sizeof(GfxVulkanSampler), 64u) ||
        !slot_map_init(&device->pipelines, arena, sizeof(GfxVulkanPipeline), 64u)) {
        LOG_ERROR("gfx", "Failed to initialize Vulkan resource pools");
        gfx_device_destroy(device);
        return 0;
    }
    device->retiredBuffers = ARENA_PUSH_ARRAY(arena, GfxVulkanRetiredBuffer, GFX_VULKAN_RETIRED_BUFFER_CAPACITY);
    device->retiredTextures = ARENA_PUSH_ARRAY(arena, GfxVulkanRetiredTexture, GFX_VULKAN_RETIRED_TEXTURE_CAPACITY);
    device->retiredSamplers = ARENA_PUSH_ARRAY(arena, GfxVulkanRetiredSampler, GFX_VULKAN_RETIRED_SAMPLER_CAPACITY);
    device->retiredPipelines = ARENA_PUSH_ARRAY(arena, GfxVulkanRetiredPipeline, GFX_VULKAN_RETIRED_PIPELINE_CAPACITY);
    if (!device->retiredBuffers || !device->retiredTextures || !device->retiredSamplers || !device->retiredPipelines) {
        LOG_ERROR("gfx", "Failed to initialize Vulkan retired resource queues");
        gfx_device_destroy(device);
        return 0;
    }
    if (!gfx_vulkan_create_nil_resources(device)) {
        LOG_ERROR("gfx", "Failed to create Vulkan nil resources");
        gfx_device_destroy(device);
        return 0;
    }
    if (!gfx_vulkan_resource_table_init(device)) {
        LOG_ERROR("gfx", "Failed to initialize Vulkan resource table");
        gfx_device_destroy(device);
        return 0;
    }
    if (!gfx_vulkan_create_buffer_set_layout(device) || !gfx_vulkan_create_pipeline_layout(device)) {
        LOG_ERROR("gfx", "Failed to initialize Vulkan descriptor layouts");
        gfx_device_destroy(device);
        return 0;
    }

    void* backbufferSlot = 0;
    U32 backbufferIndex = 0u;
    U32 backbufferGeneration = 0u;
    if (!slot_map_alloc(&device->textures, &backbufferSlot, &backbufferIndex, &backbufferGeneration)) {
        gfx_device_destroy(device);
        return 0;
    }
    GfxVulkanTexture* backbuffer = (GfxVulkanTexture*)backbufferSlot;
    backbuffer->format = GfxFormat_BGRA8_UNorm;
    backbuffer->usageFlags = GfxTextureUsageFlags_ColorTarget;
    backbuffer->ownsImage = 0;
    backbuffer->internal = 1;
    device->backbuffer.index = backbufferIndex;
    device->backbuffer.generation = backbufferGeneration;

    OS_WindowInfo windowInfo = OS_window_get_info(desc->window);
    U32 width = windowInfo.drawableWidth ? windowInfo.drawableWidth : 1u;
    U32 height = windowInfo.drawableHeight ? windowInfo.drawableHeight : 1u;
    if (!gfx_vulkan_create_swapchain(device, width, height)) {
        LOG_ERROR("gfx", "Vulkan swapchain creation failed");
        gfx_device_destroy(device);
        return 0;
    }

    device->frames = ARENA_PUSH_ARRAY(arena, GfxFrame, device->framesInFlight);
    if (!device->frames) {
        gfx_device_destroy(device);
        return 0;
    }
    MEMSET(device->frames, 0, sizeof(GfxFrame) * device->framesInFlight);

    U64 tempSize = desc->tempBufferSize ? desc->tempBufferSize : GFX_VULKAN_DEFAULT_TEMP_BUFFER_SIZE;
    for (U32 i = 0u; i < device->framesInFlight; ++i) {
        if (!gfx_vulkan_create_frame_objects(device, &device->frames[i], tempSize)) {
            LOG_ERROR("gfx", "Vulkan frame object creation failed");
            gfx_device_destroy(device);
            return 0;
        }
    }

    device->alive = 1;
    *outDevice = device;
    return 1;
}

void gfx_device_destroy(GfxDevice* device) {
    if (!device || !device->instance) {
        return;
    }
    if (device->device) {
        vkDeviceWaitIdle(device->device);
        device->completedFrameSerial = device->frameSerial;
        gfx_vulkan_drain_retired(device);
    }
    if (device->frames) {
        for (U32 i = 0u; i < device->framesInFlight; ++i) {
            gfx_vulkan_destroy_frame_objects(device, &device->frames[i]);
        }
    }
    if (device->device) {
        for (U32 i = 0u; i < device->pipelines.capacity; ++i) {
            if (slot_map_is_occupied(&device->pipelines, i)) {
                GfxVulkanPipeline* item = (GfxVulkanPipeline*)slot_map_item_at(&device->pipelines, i);
                if (item->pipeline) {
                    vkDestroyPipeline(device->device, item->pipeline, 0);
                    item->pipeline = 0;
                }
            }
        }

        for (U32 i = 0u; i < device->samplers.capacity; ++i) {
            if (slot_map_is_occupied(&device->samplers, i)) {
                GfxVulkanSampler* item = (GfxVulkanSampler*)slot_map_item_at(&device->samplers, i);
                if (item->sampler) {
                    vkDestroySampler(device->device, item->sampler, 0);
                    item->sampler = 0;
                }
            }
        }

        for (U32 i = 0u; i < device->textures.capacity; ++i) {
            if (slot_map_is_occupied(&device->textures, i)) {
                GfxVulkanTexture* item = (GfxVulkanTexture*)slot_map_item_at(&device->textures, i);
                if (item->view && item->ownsImage) {
                    vkDestroyImageView(device->device, item->view, 0);
                    item->view = 0;
                }
                if (item->image && item->ownsImage) {
                    vkDestroyImage(device->device, item->image, 0);
                    item->image = 0;
                }
                if (item->memory && item->ownsImage) {
                    vkFreeMemory(device->device, item->memory, 0);
                    item->memory = 0;
                }
            }
        }

        for (U32 i = 0u; i < device->buffers.capacity; ++i) {
            if (slot_map_is_occupied(&device->buffers, i)) {
                GfxVulkanBuffer* item = (GfxVulkanBuffer*)slot_map_item_at(&device->buffers, i);
                gfx_vulkan_destroy_buffer_item(device, item);
            }
        }
    }
    gfx_vulkan_destroy_swapchain(device);
    if (device->pipelineLayout) {
        vkDestroyPipelineLayout(device->device, device->pipelineLayout, 0);
        device->pipelineLayout = 0;
    }
    if (device->bufferSetLayout) {
        vkDestroyDescriptorSetLayout(device->device, device->bufferSetLayout, 0);
        device->bufferSetLayout = 0;
    }
    if (device->resourceDescriptorPool) {
        vkDestroyDescriptorPool(device->device, device->resourceDescriptorPool, 0);
        device->resourceDescriptorPool = 0;
    }
    if (device->resourceSetLayout) {
        vkDestroyDescriptorSetLayout(device->device, device->resourceSetLayout, 0);
        device->resourceSetLayout = 0;
    }
    gfx_vulkan_destroy_nil_resources(device);
    if (device->device) {
        vkDestroyDevice(device->device, 0);
    }
    if (device->surface) {
        vkDestroySurfaceKHR(device->instance, device->surface, 0);
    }
    if (device->debugMessenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(device->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyDebugMessenger) {
            destroyDebugMessenger(device->instance, device->debugMessenger, 0);
        }
    }
    vkDestroyInstance(device->instance, 0);
    device->alive = 0;
}

void gfx_device_resize(GfxDevice* device, U32 width, U32 height) {
    if (!device || !device->device) {
        return;
    }
    if (width == 0u) {
        width = 1u;
    }
    if (height == 0u) {
        height = 1u;
    }
    vkDeviceWaitIdle(device->device);
    gfx_vulkan_destroy_swapchain(device);
    gfx_vulkan_create_swapchain(device, width, height);
}

void gfx_wait_idle(GfxDevice* device) {
    if (device && device->device) {
        vkDeviceWaitIdle(device->device);
        device->completedFrameSerial = device->frameSerial;
        gfx_vulkan_drain_retired(device);
    }
}

GfxBuffer gfx_create_buffer(GfxDevice* device, const GfxBufferDesc* desc) {
    return gfx_vulkan_create_buffer_internal(device, desc, 0);
}

GfxTexture gfx_create_texture(GfxDevice* device, const GfxTextureDesc* desc) {
    if (!device || !desc || desc->width == 0u || desc->height == 0u || desc->format == GfxFormat_Invalid) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid texture descriptor");
        }
        return {};
    }

    VkFormat format = gfx_vulkan_format(desc->format);
    if (format == VK_FORMAT_UNDEFINED) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Unsupported texture format {}", (U32)desc->format);
        }
        return {};
    }

    U32 mipCount = desc->mipCount ? desc->mipCount : 1u;
    VkImageUsageFlags usage = gfx_vulkan_texture_usage(desc->usageFlags);
    if (usage == 0u) {
        usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    VkImage image = 0;
    VkDeviceMemory memory = 0;
    VkImageView view = 0;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {desc->width, desc->height, 1u};
    imageInfo.mipLevels = mipCount;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device->device, &imageInfo, 0, &image) != VK_SUCCESS) {
        LOG_ERROR("gfx", "Vulkan texture creation failed");
        return {};
    }

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(device->device, image, &requirements);
    U32 memoryType = gfx_vulkan_find_memory_type(device, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == 0xffffffffu) {
        vkDestroyImage(device->device, image, 0);
        return {};
    }

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device->device, &allocateInfo, 0, &memory) != VK_SUCCESS) {
        vkDestroyImage(device->device, image, 0);
        return {};
    }
    if (vkBindImageMemory(device->device, image, memory, 0u) != VK_SUCCESS) {
        vkFreeMemory(device->device, memory, 0);
        vkDestroyImage(device->device, image, 0);
        return {};
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = gfx_vulkan_image_aspect(desc->format);
    viewInfo.subresourceRange.levelCount = mipCount;
    viewInfo.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(device->device, &viewInfo, 0, &view) != VK_SUCCESS) {
        vkFreeMemory(device->device, memory, 0);
        vkDestroyImage(device->device, image, 0);
        return {};
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->textures, &slotItem, &slotIndex, &generation)) {
        vkDestroyImageView(device->device, view, 0);
        vkFreeMemory(device->device, memory, 0);
        vkDestroyImage(device->device, image, 0);
        return {};
    }

    GfxVulkanTexture* item = (GfxVulkanTexture*)slotItem;
    item->image = image;
    item->memory = memory;
    item->view = view;
    item->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    item->width = desc->width;
    item->height = desc->height;
    item->mipCount = mipCount;
    item->format = desc->format;
    item->usageFlags = desc->usageFlags;
    item->resourceId = {};
    item->ownsImage = 1;
    item->internal = 0;

    GfxTexture result = {slotIndex, generation};
    return result;
}

GfxSampler gfx_create_sampler(GfxDevice* device, const GfxSamplerDesc* desc) {
    if (!device || !desc) {
        return {};
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = gfx_vulkan_filter(desc->magFilter);
    samplerInfo.minFilter = gfx_vulkan_filter(desc->minFilter);
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = gfx_vulkan_address_mode(desc->addressU);
    samplerInfo.addressModeV = gfx_vulkan_address_mode(desc->addressV);
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    VkSampler sampler = 0;
    if (vkCreateSampler(device->device, &samplerInfo, 0, &sampler) != VK_SUCCESS) {
        return {};
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->samplers, &slotItem, &slotIndex, &generation)) {
        vkDestroySampler(device->device, sampler, 0);
        return {};
    }

    GfxVulkanSampler* item = (GfxVulkanSampler*)slotItem;
    item->sampler = sampler;
    item->resourceId = {};

    GfxSampler result = {slotIndex, generation};
    return result;
}

static VkShaderModule gfx_vulkan_create_shader_module(GfxDevice* device, GfxShaderCode code) {
    if (!device ||
        code.format != GfxShaderFormat_SPIRV ||
        !code.data ||
        code.size == 0u ||
        (code.size & 3u) != 0u) {
        return 0;
    }

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size;
    moduleInfo.pCode = (const U32*)code.data;

    VkShaderModule module = 0;
    if (vkCreateShaderModule(device->device, &moduleInfo, 0, &module) != VK_SUCCESS) {
        return 0;
    }
    return module;
}

GfxPipeline gfx_create_graphics_pipeline(GfxDevice* device, const GfxGraphicsPipelineDesc* desc) {
    if (!device || !desc) {
        return {};
    }
    if (desc->vertexShader.format != GfxShaderFormat_SPIRV ||
        desc->fragmentShader.format != GfxShaderFormat_SPIRV) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Vulkan only supports SPIR-V shaders");
        }
        return {};
    }
    if (!desc->colorFormats || desc->colorFormatCount == 0u || desc->colorFormatCount > GFX_MAX_COLOR_TARGETS) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid graphics pipeline color formats");
        }
        return {};
    }

    VkShaderModule vertexModule = gfx_vulkan_create_shader_module(device, desc->vertexShader);
    VkShaderModule fragmentModule = gfx_vulkan_create_shader_module(device, desc->fragmentShader);
    if (!vertexModule || !fragmentModule) {
        if (vertexModule) {
            vkDestroyShaderModule(device->device, vertexModule, 0);
        }
        if (fragmentModule) {
            vkDestroyShaderModule(device->device, fragmentModule, 0);
        }
        LOG_ERROR("gfx", "Vulkan shader module creation failed");
        return {};
    }

    const char* vertexEntry = desc->vertexShader.entry ? desc->vertexShader.entry : "main";
    const char* fragmentEntry = desc->fragmentShader.entry ? desc->fragmentShader.entry : "main";
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = vertexEntry;
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = fragmentEntry;

    Temp scratch = get_scratch(0, 0u);
    if (!scratch.arena) {
        vkDestroyShaderModule(device->device, fragmentModule, 0);
        vkDestroyShaderModule(device->device, vertexModule, 0);
        return {};
    }
    DEFER_REF(temp_end(&scratch));

    VkVertexInputAttributeDescription* attributes = ARENA_PUSH_ARRAY(scratch.arena, VkVertexInputAttributeDescription, desc->attributeCount);
    VkFormat* colorFormats = ARENA_PUSH_ARRAY(scratch.arena, VkFormat, desc->colorFormatCount);
    VkPipelineColorBlendAttachmentState* blendAttachments = ARENA_PUSH_ARRAY(scratch.arena, VkPipelineColorBlendAttachmentState, desc->colorFormatCount);
    if ((desc->attributeCount != 0u && !attributes) || !colorFormats || !blendAttachments) {
        vkDestroyShaderModule(device->device, fragmentModule, 0);
        vkDestroyShaderModule(device->device, vertexModule, 0);
        return {};
    }

    for (U32 i = 0u; i < desc->attributeCount; ++i) {
        attributes[i].location = desc->attributes[i].location;
        attributes[i].binding = 0u;
        attributes[i].format = gfx_vulkan_vertex_format(desc->attributes[i].format);
        attributes[i].offset = desc->attributes[i].offset;
    }
    for (U32 i = 0u; i < desc->colorFormatCount; ++i) {
        colorFormats[i] = gfx_vulkan_format(desc->colorFormats[i]);
        blendAttachments[i].blendEnable = VK_FALSE;
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                             VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT |
                                             VK_COLOR_COMPONENT_A_BIT;
    }

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0u;
    binding.stride = desc->vertexBuffer.stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1u;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = desc->attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = gfx_vulkan_topology(desc->topology);

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1u;
    viewportState.scissorCount = 1u;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = gfx_vulkan_cull_mode(desc->raster.cullMode);
    raster.frontFace = gfx_vulkan_front_face(desc->raster.frontFace);
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth = {};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc->depth.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc->depth.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = gfx_vulkan_compare_op(desc->depth.compareOp);

    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = desc->colorFormatCount;
    blend.pAttachments = blendAttachments;

    VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = ARRAY_COUNT(dynamicStates);
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = desc->colorFormatCount;
    renderingInfo.pColorAttachmentFormats = colorFormats;
    VkFormat depthFormat = gfx_vulkan_format(desc->depthFormat);
    if (depthFormat != VK_FORMAT_UNDEFINED) {
        renderingInfo.depthAttachmentFormat = depthFormat;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = ARRAY_COUNT(stages);
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = device->pipelineLayout;

    VkPipeline pipeline = 0;
    VkResult result = vkCreateGraphicsPipelines(device->device, 0, 1u, &pipelineInfo, 0, &pipeline);
    vkDestroyShaderModule(device->device, fragmentModule, 0);
    vkDestroyShaderModule(device->device, vertexModule, 0);
    if (result != VK_SUCCESS || !pipeline) {
        LOG_ERROR("gfx", "Vulkan graphics pipeline creation failed");
        return {};
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->pipelines, &slotItem, &slotIndex, &generation)) {
        vkDestroyPipeline(device->device, pipeline, 0);
        return {};
    }

    GfxVulkanPipeline* item = (GfxVulkanPipeline*)slotItem;
    item->kind = GfxPipelineKind_Graphics;
    item->pipeline = pipeline;
    item->raster = desc->raster;

    GfxPipeline handle = {slotIndex, generation};
    return handle;
}

GfxPipeline gfx_create_compute_pipeline(GfxDevice* device, const GfxComputePipelineDesc* desc) {
    if (!device || !desc ||
        desc->shader.format != GfxShaderFormat_SPIRV ||
        desc->threadsPerThreadgroupX == 0u ||
        desc->threadsPerThreadgroupY == 0u ||
        desc->threadsPerThreadgroupZ == 0u) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid compute pipeline descriptor");
        }
        return {};
    }

    VkShaderModule module = gfx_vulkan_create_shader_module(device, desc->shader);
    if (!module) {
        LOG_ERROR("gfx", "Vulkan compute shader module creation failed");
        return {};
    }

    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = desc->shader.entry ? desc->shader.entry : "main";

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = device->pipelineLayout;

    VkPipeline pipeline = 0;
    VkResult result = vkCreateComputePipelines(device->device, 0, 1u, &pipelineInfo, 0, &pipeline);
    vkDestroyShaderModule(device->device, module, 0);
    if (result != VK_SUCCESS || !pipeline) {
        LOG_ERROR("gfx", "Vulkan compute pipeline creation failed");
        return {};
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->pipelines, &slotItem, &slotIndex, &generation)) {
        vkDestroyPipeline(device->device, pipeline, 0);
        return {};
    }

    GfxVulkanPipeline* item = (GfxVulkanPipeline*)slotItem;
    item->kind = GfxPipelineKind_Compute;
    item->pipeline = pipeline;
    item->threadsPerThreadgroupX = desc->threadsPerThreadgroupX;
    item->threadsPerThreadgroupY = desc->threadsPerThreadgroupY;
    item->threadsPerThreadgroupZ = desc->threadsPerThreadgroupZ;

    GfxPipeline handle = {slotIndex, generation};
    return handle;
}

void gfx_destroy_buffer(GfxDevice* device, GfxBuffer buffer) {
    if (!device) {
        return;
    }
    GfxVulkanBuffer* item = gfx_vulkan_resolve_buffer(device, buffer);
    if (!item->buffer || item->internal) {
        return;
    }
    gfx_vulkan_retire_buffer(device, item);
    void* released = 0;
    slot_map_release(&device->buffers, buffer.index, buffer.generation, &released);
}

void gfx_destroy_texture(GfxDevice* device, GfxTexture texture) {
    if (!device) {
        return;
    }
    GfxVulkanTexture* item = gfx_vulkan_resolve_texture(device, texture);
    if (!item->image || item->internal) {
        return;
    }
    gfx_vulkan_retire_texture(device, item);
    void* released = 0;
    slot_map_release(&device->textures, texture.index, texture.generation, &released);
}

void gfx_destroy_sampler(GfxDevice* device, GfxSampler sampler) {
    if (!device) {
        return;
    }
    GfxVulkanSampler* item = gfx_vulkan_resolve_sampler(device, sampler);
    if (!item->sampler) {
        return;
    }
    gfx_vulkan_retire_sampler(device, item);
    void* released = 0;
    slot_map_release(&device->samplers, sampler.index, sampler.generation, &released);
}

void gfx_destroy_pipeline(GfxDevice* device, GfxPipeline pipeline) {
    if (!device) {
        return;
    }
    GfxVulkanPipeline* item = gfx_vulkan_resolve_pipeline(device, pipeline);
    if (!item->pipeline) {
        return;
    }
    gfx_vulkan_retire_pipeline(device, item);
    void* released = 0;
    slot_map_release(&device->pipelines, pipeline.index, pipeline.generation, &released);
}

GfxResourceId gfx_register_texture(GfxDevice* device, GfxTexture texture) {
    if (!device) {
        return {};
    }
    GfxVulkanTexture* item = gfx_vulkan_resolve_texture(device, texture);
    if (!item->view) {
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_vulkan_register_resource(device, GfxVulkanResourceKind_Texture, texture.index, texture.generation);
    gfx_vulkan_resource_table_set_texture(device, item->resourceId, item->view);
    return item->resourceId;
}

GfxResourceId gfx_register_sampler(GfxDevice* device, GfxSampler sampler) {
    if (!device) {
        return {};
    }
    GfxVulkanSampler* item = gfx_vulkan_resolve_sampler(device, sampler);
    if (!item->sampler) {
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_vulkan_register_resource(device, GfxVulkanResourceKind_Sampler, sampler.index, sampler.generation);
    gfx_vulkan_resource_table_set_sampler(device, item->resourceId, item->sampler);
    return item->resourceId;
}

GfxResourceId gfx_register_buffer(GfxDevice* device, GfxBuffer buffer) {
    if (!device) {
        return {};
    }
    GfxVulkanBuffer* item = gfx_vulkan_resolve_buffer(device, buffer);
    if (!item->buffer) {
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_vulkan_register_resource(device, GfxVulkanResourceKind_Buffer, buffer.index, buffer.generation);
    return item->resourceId;
}

GfxFrame* gfx_begin_frame(GfxDevice* device) {
    if (!device || !device->alive || !device->swapchain) {
        return 0;
    }

    GfxFrame* frame = &device->frames[device->frameCursor];
    VkResult fenceResult = vkGetFenceStatus(device->device, frame->fence);
    if (fenceResult == VK_NOT_READY) {
        return 0;
    }
    if (fenceResult != VK_SUCCESS) {
        LOG_ERROR("gfx", "Vulkan frame fence status failed");
        return 0;
    }

    if (frame->submittedSerial > device->completedFrameSerial) {
        device->completedFrameSerial = frame->submittedSerial;
        gfx_vulkan_drain_retired(device);
    }

    VkResult acquireResult = vkAcquireNextImageKHR(device->device,
                                                   device->swapchain,
                                                   0u,
                                                   frame->imageAvailableSemaphore,
                                                   0,
                                                   &frame->imageIndex);
    if (acquireResult == VK_TIMEOUT || acquireResult == VK_NOT_READY) {
        return 0;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return 0;
    }

    vkResetCommandPool(device->device, frame->commandPool, 0u);
    vkResetDescriptorPool(device->device, frame->descriptorPool, 0u);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(frame->commandBuffer, &beginInfo) != VK_SUCCESS) {
        return 0;
    }

    frame->tempPos = 0u;
    frame->active = 1;
    frame->submitted = 0;
    device->activeFrame = frame;
    device->frameCursor = (device->frameCursor + 1u) % device->framesInFlight;
    device->frameSerial += 1u;
    device->stats.drawCount = 0u;
    device->stats.dispatchCount = 0u;
    device->stats.pipelineSwitchCount = 0u;
    device->stats.tempOverflowCount = 0u;
    device->stats.tempBytesUsed = 0u;
    device->stats.resourceTableCount = device->resourceLiveCount;
    device->stats.frameIndex = device->frameSerial;
    return frame;
}

GfxCommandBuffer* gfx_get_command_buffer(GfxFrame* frame) {
    if (!frame || !frame->active) {
        return 0;
    }
    return &frame->commands;
}

GfxTexture gfx_get_backbuffer(GfxFrame* frame) {
    if (!frame || !frame->device) {
        return {};
    }
    return frame->device->backbuffer;
}

GfxTemp gfx_allocate_temp(GfxFrame* frame, U64 size, U64 alignment) {
    GfxTemp result = {};
    if (!frame || !frame->active || size == 0u) {
        return result;
    }
    if (alignment == 0u) {
        alignment = sizeof(void*);
    }
    if (!is_power_of_two(alignment)) {
        return result;
    }

    U64 offset = align_pow2(frame->tempPos, alignment);
    U64 end = offset + size;
    if (end > frame->tempSize) {
        frame->device->stats.tempOverflowCount += 1u;
        return result;
    }

    result.cpu = (U8*)frame->tempCpu + offset;
    result.gpu.buffer = frame->tempBuffer;
    result.gpu.offset = offset;
    result.gpu.size = size;
    frame->tempPos = end;
    frame->device->stats.tempBytesUsed = frame->tempPos;
    return result;
}

B32 gfx_upload_buffer(GfxFrame* frame, GfxBuffer dst, U64 dstOffset, const void* src, U64 size) {
    if (!frame || !frame->device || !src || size == 0u) {
        return 0;
    }

    GfxDevice* device = frame->device;
    GfxVulkanBuffer* buffer = gfx_vulkan_resolve_buffer(device, dst);
    if (!buffer->buffer) {
        return 0;
    }
    if (dstOffset > buffer->size || size > (buffer->size - dstOffset)) {
        gfx_vulkan_api_assert(device, dstOffset <= buffer->size && size <= (buffer->size - dstOffset));
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "gfx_upload_buffer out of bounds");
        }
        return 0;
    }

    if (buffer->mapped) {
        MEMCPY((U8*)buffer->mapped + dstOffset, src, size);
        return 1;
    }

    GfxTemp temp = gfx_allocate_temp(frame, size, 16u);
    ASSERT_DEBUG(temp.cpu != 0 && "gfx_upload_buffer ran out of frame upload memory");
    if (!temp.cpu) {
        LOG_ERROR("gfx", "gfx_upload_buffer ran out of frame upload memory (size={})", size);
        return 0;
    }

    MEMCPY(temp.cpu, src, size);
    GfxVulkanBuffer* uploadBuffer = gfx_vulkan_resolve_buffer(device, temp.gpu.buffer);
    if (!uploadBuffer->buffer) {
        return 0;
    }

    VkBufferCopy copy = {};
    copy.srcOffset = temp.gpu.offset;
    copy.dstOffset = dstOffset;
    copy.size = size;
    vkCmdCopyBuffer(frame->commandBuffer, uploadBuffer->buffer, buffer->buffer, 1u, &copy);

    VkBufferMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.buffer = buffer->buffer;
    barrier.offset = dstOffset;
    barrier.size = size;

    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame->commandBuffer, &dependency);
    return 1;
}

B32 gfx_upload_texture(GfxFrame* frame, GfxTexture dst, const GfxTextureUploadRegion* region, const void* src) {
    if (!frame || !frame->device || !region || !src) {
        return 0;
    }

    GfxDevice* device = frame->device;
    GfxVulkanTexture* texture = gfx_vulkan_resolve_texture(device, dst);
    if (!texture->image || !texture->ownsImage) {
        return 0;
    }

    U32 bytesPerPixel = gfx_vulkan_format_bytes_per_pixel(texture->format);
    U32 mipWidth = texture->width;
    U32 mipHeight = texture->height;
    for (U32 mipIndex = 0u; mipIndex < region->mip; ++mipIndex) {
        mipWidth = (mipWidth > 1u) ? (mipWidth >> 1u) : 1u;
        mipHeight = (mipHeight > 1u) ? (mipHeight >> 1u) : 1u;
    }

    B32 supported = bytesPerPixel != 0u &&
                    region->layer == 0u &&
                    region->layerCount == 1u &&
                    region->z == 0u &&
                    region->depth == 1u;
    B32 inBounds = region->mip < texture->mipCount &&
                   region->width != 0u &&
                   region->height != 0u &&
                   region->x <= mipWidth &&
                   region->y <= mipHeight &&
                   region->width <= (mipWidth - region->x) &&
                   region->height <= (mipHeight - region->y);
    U64 rowBytes = (U64)region->width * bytesPerPixel;
    B32 rowLayout = region->bytesPerRow >= rowBytes &&
                    (region->bytesPerRow % GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT) == 0u &&
                    region->rowsPerImage >= region->height;

    gfx_vulkan_api_assert(device, supported);
    gfx_vulkan_api_assert(device, inBounds);
    gfx_vulkan_api_assert(device, rowLayout);
    if (!supported || !inBounds || !rowLayout) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "gfx_upload_texture invalid upload region");
        }
        return 0;
    }

    U64 sourceBytesPerImage = region->bytesPerRow * region->rowsPerImage;
    if (sourceBytesPerImage == 0u || sourceBytesPerImage / region->rowsPerImage != region->bytesPerRow) {
        if (gfx_vulkan_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "gfx_upload_texture upload size overflow");
        }
        return 0;
    }

    GfxTemp temp = gfx_allocate_temp(frame, sourceBytesPerImage, GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT);
    ASSERT_DEBUG(temp.cpu != 0 && "gfx_upload_texture ran out of frame upload memory");
    if (!temp.cpu) {
        LOG_ERROR("gfx", "gfx_upload_texture ran out of frame upload memory (size={})", sourceBytesPerImage);
        return 0;
    }

    MEMCPY(temp.cpu, src, sourceBytesPerImage);
    GfxVulkanBuffer* uploadBuffer = gfx_vulkan_resolve_buffer(device, temp.gpu.buffer);
    if (!uploadBuffer->buffer) {
        return 0;
    }

    VkImageAspectFlags aspect = gfx_vulkan_image_aspect(texture->format);
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = aspect;
    subresourceRange.baseMipLevel = region->mip;
    subresourceRange.levelCount = 1u;
    subresourceRange.baseArrayLayer = region->layer;
    subresourceRange.layerCount = region->layerCount;

    VkImageMemoryBarrier2 toTransfer = {};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toTransfer.srcStageMask = (texture->layout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toTransfer.srcAccessMask = (texture->layout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0u : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = texture->layout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.image = texture->image;
    toTransfer.subresourceRange = subresourceRange;

    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(frame->commandBuffer, &dependency);

    VkBufferImageCopy copy = {};
    copy.bufferOffset = temp.gpu.offset;
    copy.bufferRowLength = (U32)(region->bytesPerRow / bytesPerPixel);
    copy.bufferImageHeight = region->rowsPerImage;
    copy.imageSubresource.aspectMask = aspect;
    copy.imageSubresource.mipLevel = region->mip;
    copy.imageSubresource.baseArrayLayer = region->layer;
    copy.imageSubresource.layerCount = region->layerCount;
    copy.imageOffset = {(S32)region->x, (S32)region->y, (S32)region->z};
    copy.imageExtent = {region->width, region->height, region->depth};
    vkCmdCopyBufferToImage(frame->commandBuffer,
                           uploadBuffer->buffer,
                           texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1u,
                           &copy);

    VkImageMemoryBarrier2 toSampled = {};
    toSampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toSampled.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toSampled.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toSampled.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toSampled.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toSampled.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSampled.image = texture->image;
    toSampled.subresourceRange = subresourceRange;
    dependency.pImageMemoryBarriers = &toSampled;
    vkCmdPipelineBarrier2(frame->commandBuffer, &dependency);

    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return 1;
}

void gfx_render_pass(GfxCommandBuffer* commands, const GfxRenderPassDesc* desc, const GfxDrawArea* areas, U32 areaCount) {
    if (!commands || !commands->frame || !desc || desc->colorTargetCount == 0u || !desc->colorTargets) {
        return;
    }
    if (areaCount != 0u && !areas) {
        return;
    }

    GfxFrame* frame = commands->frame;
    GfxDevice* device = frame->device;
    if (!frame->active || frame->imageIndex >= device->swapchainImageCount) {
        return;
    }

    const GfxColorTarget* target = &desc->colorTargets[0];
    if (target->texture.index != device->backbuffer.index ||
        target->texture.generation != device->backbuffer.generation) {
        return;
    }

    VkImage image = device->swapchainImages[frame->imageIndex];
    VkImageMemoryBarrier2 toColor = {};
    toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toColor.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toColor.srcAccessMask = 0u;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = (target->loadOp == GfxLoadOp_Load) ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.image = image;
    toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColor.subresourceRange.levelCount = 1u;
    toColor.subresourceRange.layerCount = 1u;

    VkDependencyInfo dependency = {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(frame->commandBuffer, &dependency);

    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = device->swapchainImageViews[frame->imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = gfx_vulkan_load_op(target->loadOp);
    colorAttachment.storeOp = gfx_vulkan_store_op(target->storeOp);
    colorAttachment.clearValue.color.float32[0] = target->clearColor[0];
    colorAttachment.clearValue.color.float32[1] = target->clearColor[1];
    colorAttachment.clearValue.color.float32[2] = target->clearColor[2];
    colorAttachment.clearValue.color.float32[3] = target->clearColor[3];

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = device->swapchainExtent;
    renderingInfo.layerCount = 1u;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(frame->commandBuffer, &renderingInfo);

    GfxPipeline boundPipelineHandle = {};
    GfxBuffer boundDataBuffer = {};
    GfxBuffer boundPassBuffer = {};
    VkDescriptorSet boundBufferSet = 0;
    B32 resourceSetBound = 0;

    for (U32 areaIndex = 0u; areaIndex < areaCount; ++areaIndex) {
        const GfxDrawArea* area = &areas[areaIndex];
        gfx_vulkan_api_assert(device, area->drawCount == 0u || area->draws != 0);
        if (area->drawCount != 0u && !area->draws) {
            continue;
        }

        VkViewport viewport = {};
        viewport.x = area->viewport.x;
        viewport.y = area->viewport.y + area->viewport.height;
        viewport.width = area->viewport.width;
        viewport.height = -area->viewport.height;
        viewport.minDepth = area->viewport.minDepth;
        viewport.maxDepth = area->viewport.maxDepth;
        vkCmdSetViewport(frame->commandBuffer, 0u, 1u, &viewport);

        VkRect2D scissor = {};
        scissor.offset.x = area->scissor.x;
        scissor.offset.y = area->scissor.y;
        scissor.extent.width = area->scissor.width;
        scissor.extent.height = area->scissor.height;
        vkCmdSetScissor(frame->commandBuffer, 0u, 1u, &scissor);

        for (U32 drawIndex = 0u; drawIndex < area->drawCount; ++drawIndex) {
            const GfxDraw* draw = &area->draws[drawIndex];
            if (draw->indexCount == 0u) {
                continue;
            }
            if (draw->drawData.offset > (U64)0xffffffffu ||
                desc->passData.offset > (U64)0xffffffffu) {
                gfx_vulkan_api_assert(device, draw->drawData.offset <= (U64)0xffffffffu);
                gfx_vulkan_api_assert(device, desc->passData.offset <= (U64)0xffffffffu);
                continue;
            }

            GfxVulkanPipeline* pipeline = gfx_vulkan_resolve_pipeline(device, draw->pipeline);
            GfxVulkanBuffer* vertexBuffer = gfx_vulkan_resolve_buffer(device, draw->vertexBuffer);
            GfxVulkanBuffer* indexBuffer = gfx_vulkan_resolve_buffer(device, draw->indexBuffer);
            GfxVulkanBuffer* drawDataBuffer = gfx_vulkan_resolve_buffer(device, draw->drawData.buffer);
            GfxVulkanBuffer* passDataBuffer = gfx_vulkan_resolve_buffer(device, desc->passData.buffer);

            gfx_vulkan_api_assert(device, pipeline->kind == GfxPipelineKind_Graphics);
            if (pipeline->kind != GfxPipelineKind_Graphics ||
                !pipeline->pipeline ||
                !vertexBuffer->buffer ||
                !indexBuffer->buffer ||
                !drawDataBuffer->buffer ||
                !passDataBuffer->buffer) {
                continue;
            }

            if (!resourceSetBound) {
                vkCmdBindDescriptorSets(frame->commandBuffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        device->pipelineLayout,
                                        GFX_VULKAN_DESCRIPTOR_SET_RESOURCE_TABLE,
                                        1u,
                                        &device->resourceSet,
                                        0u,
                                        0);
                resourceSetBound = 1;
            }

            if (boundPipelineHandle.index != draw->pipeline.index ||
                boundPipelineHandle.generation != draw->pipeline.generation) {
                vkCmdBindPipeline(frame->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
                boundPipelineHandle = draw->pipeline;
                device->stats.pipelineSwitchCount += 1u;
            }

            if (boundBufferSet == 0 ||
                boundDataBuffer.index != draw->drawData.buffer.index ||
                boundDataBuffer.generation != draw->drawData.buffer.generation ||
                boundPassBuffer.index != desc->passData.buffer.index ||
                boundPassBuffer.generation != desc->passData.buffer.generation) {
                boundBufferSet = gfx_vulkan_alloc_buffer_set(frame, draw->drawData.buffer, desc->passData.buffer);
                if (!boundBufferSet) {
                    continue;
                }
                boundDataBuffer = draw->drawData.buffer;
                boundPassBuffer = desc->passData.buffer;
                vkCmdBindDescriptorSets(frame->commandBuffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        device->pipelineLayout,
                                        GFX_VULKAN_DESCRIPTOR_SET_BUFFERS,
                                        1u,
                                        &boundBufferSet,
                                        0u,
                                        0);
            }

            VkDeviceSize vertexOffset = draw->vertexByteOffset;
            vkCmdBindVertexBuffers(frame->commandBuffer, 0u, 1u, &vertexBuffer->buffer, &vertexOffset);

            U32 indexSize = (draw->indexType == GfxIndexType_U16) ? 2u : 4u;
            if ((draw->indexByteOffset % indexSize) != 0u) {
                gfx_vulkan_api_assert(device, (draw->indexByteOffset % indexSize) == 0u);
                continue;
            }
            vkCmdBindIndexBuffer(frame->commandBuffer,
                                 indexBuffer->buffer,
                                 draw->indexByteOffset,
                                 gfx_vulkan_index_type(draw->indexType));

            GfxVulkanPushConstants push = {};
            push.dataByteOffset = (U32)draw->drawData.offset;
            push.passByteOffset = (U32)desc->passData.offset;
            vkCmdPushConstants(frame->commandBuffer,
                               device->pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                               0u,
                               sizeof(push),
                               &push);

            U32 instanceCount = draw->instanceCount ? draw->instanceCount : 1u;
            vkCmdDrawIndexed(frame->commandBuffer,
                             draw->indexCount,
                             instanceCount,
                             0u,
                             draw->baseVertex,
                             draw->firstInstance);
            device->stats.drawCount += 1u;
        }
    }

    vkCmdEndRendering(frame->commandBuffer);

    VkImageMemoryBarrier2 toPresent = {};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = 0u;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image = image;
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.levelCount = 1u;
    toPresent.subresourceRange.layerCount = 1u;
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(frame->commandBuffer, &dependency);
}

void gfx_compute_pass(GfxCommandBuffer* commands, const GfxComputePassDesc* desc, const GfxDispatch* dispatches, U32 dispatchCount) {
    if (!commands || !commands->frame || !desc) {
        return;
    }

    GfxFrame* frame = commands->frame;
    GfxDevice* device = frame->device;
    if (!frame->active) {
        return;
    }

    gfx_vulkan_api_assert(device, dispatchCount == 0u || dispatches != 0);
    if (dispatchCount != 0u && !dispatches) {
        return;
    }

    GfxVulkanBuffer* passDataBuffer = gfx_vulkan_resolve_buffer(device, desc->passData.buffer);
    if (!passDataBuffer->buffer) {
        return;
    }

    GfxPipeline boundPipelineHandle = {};
    GfxBuffer boundDataBuffer = {};
    GfxBuffer boundPassBuffer = {};
    VkDescriptorSet boundBufferSet = 0;
    B32 resourceSetBound = 0;

    B32 dispatched = 0;
    for (U32 dispatchIndex = 0u; dispatchIndex < dispatchCount; ++dispatchIndex) {
        const GfxDispatch* dispatch = dispatches + dispatchIndex;
        if (dispatch->groupsX == 0u ||
            dispatch->groupsY == 0u ||
            dispatch->groupsZ == 0u) {
            continue;
        }
        if (dispatch->dispatchData.offset > (U64)0xffffffffu ||
            desc->passData.offset > (U64)0xffffffffu) {
            gfx_vulkan_api_assert(device, dispatch->dispatchData.offset <= (U64)0xffffffffu);
            gfx_vulkan_api_assert(device, desc->passData.offset <= (U64)0xffffffffu);
            continue;
        }

        GfxVulkanPipeline* pipeline = gfx_vulkan_resolve_pipeline(device, dispatch->pipeline);
        GfxVulkanBuffer* dispatchDataBuffer = gfx_vulkan_resolve_buffer(device, dispatch->dispatchData.buffer);

        gfx_vulkan_api_assert(device, pipeline->kind == GfxPipelineKind_Compute);
        if (pipeline->kind != GfxPipelineKind_Compute ||
            !pipeline->pipeline ||
            !dispatchDataBuffer->buffer) {
            continue;
        }

        if (!resourceSetBound) {
            vkCmdBindDescriptorSets(frame->commandBuffer,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    device->pipelineLayout,
                                    GFX_VULKAN_DESCRIPTOR_SET_RESOURCE_TABLE,
                                    1u,
                                    &device->resourceSet,
                                    0u,
                                    0);
            resourceSetBound = 1;
        }

        if (boundPipelineHandle.index != dispatch->pipeline.index ||
            boundPipelineHandle.generation != dispatch->pipeline.generation) {
            vkCmdBindPipeline(frame->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            boundPipelineHandle = dispatch->pipeline;
            device->stats.pipelineSwitchCount += 1u;
        }

        if (boundBufferSet == 0 ||
            boundDataBuffer.index != dispatch->dispatchData.buffer.index ||
            boundDataBuffer.generation != dispatch->dispatchData.buffer.generation ||
            boundPassBuffer.index != desc->passData.buffer.index ||
            boundPassBuffer.generation != desc->passData.buffer.generation) {
            boundBufferSet = gfx_vulkan_alloc_buffer_set(frame, dispatch->dispatchData.buffer, desc->passData.buffer);
            if (!boundBufferSet) {
                continue;
            }
            boundDataBuffer = dispatch->dispatchData.buffer;
            boundPassBuffer = desc->passData.buffer;
            vkCmdBindDescriptorSets(frame->commandBuffer,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    device->pipelineLayout,
                                    GFX_VULKAN_DESCRIPTOR_SET_BUFFERS,
                                    1u,
                                    &boundBufferSet,
                                    0u,
                                    0);
        }

        GfxVulkanPushConstants push = {};
        push.dataByteOffset = (U32)dispatch->dispatchData.offset;
        push.passByteOffset = (U32)desc->passData.offset;
        vkCmdPushConstants(frame->commandBuffer,
                           device->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                           0u,
                           sizeof(push),
                           &push);

        vkCmdDispatch(frame->commandBuffer, dispatch->groupsX, dispatch->groupsY, dispatch->groupsZ);
        device->stats.dispatchCount += 1u;
        dispatched = 1;
    }

    if (dispatched) {
        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        barrier.buffer = passDataBuffer->buffer;
        barrier.offset = desc->passData.offset;
        barrier.size = desc->passData.size ? desc->passData.size : passDataBuffer->size;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = 1u;
        dependency.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame->commandBuffer, &dependency);
    }
}

void gfx_submit(GfxCommandBuffer* commands) {
    if (!commands || !commands->frame) {
        return;
    }

    GfxFrame* frame = commands->frame;
    GfxDevice* device = frame->device;
    if (!device || !frame->active || frame->submitted) {
        return;
    }

    if (vkEndCommandBuffer(frame->commandBuffer) != VK_SUCCESS) {
        return;
    }

    VkResult resetResult = vkResetFences(device->device, 1u, &frame->fence);
    if (resetResult != VK_SUCCESS) {
        LOG_ERROR("gfx", "Vulkan frame fence reset failed ({})", (S32)resetResult);
        ASSERT_DEBUG(resetResult == VK_SUCCESS);
        return;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1u;
    submitInfo.pWaitSemaphores = &frame->imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &frame->commandBuffer;
    submitInfo.signalSemaphoreCount = 1u;
    submitInfo.pSignalSemaphores = &frame->renderFinishedSemaphore;
    VkResult submitResult = vkQueueSubmit(device->graphicsQueue, 1u, &submitInfo, frame->fence);
    if (submitResult != VK_SUCCESS) {
        LOG_ERROR("gfx", "Vulkan queue submit failed ({})", (S32)submitResult);
        ASSERT_DEBUG(submitResult == VK_SUCCESS);

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkDestroyFence(device->device, frame->fence, 0);
        frame->fence = 0;
        if (vkCreateFence(device->device, &fenceInfo, 0, &frame->fence) != VK_SUCCESS) {
            LOG_ERROR("gfx", "Failed to restore Vulkan frame fence after submit failure");
        }
        frame->active = 0;
        frame->submitted = 0;
        if (device->activeFrame == frame) {
            device->activeFrame = 0;
        }
        return;
    }

    frame->submittedSerial = device->frameSerial;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &frame->renderFinishedSemaphore;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &device->swapchain;
    presentInfo.pImageIndices = &frame->imageIndex;
    vkQueuePresentKHR(device->presentQueue, &presentInfo);
    frame->submitted = 1;
}

void gfx_end_frame(GfxFrame* frame) {
    if (!frame || !frame->active) {
        return;
    }
    frame->active = 0;
    if (frame->device && frame->device->activeFrame == frame) {
        frame->device->activeFrame = 0;
    }
}

GfxStats gfx_get_stats(GfxDevice* device) {
    GfxStats result = {};
    if (device) {
        result = device->stats;
    }
    return result;
}
