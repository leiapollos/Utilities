#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>

#define GFX_DEFAULT_FRAMES_IN_FLIGHT 2u
#define GFX_DEFAULT_TEMP_BUFFER_SIZE MB(8)
#define GFX_METAL_RESOURCE_TABLE_CAPACITY 256u
#define GFX_METAL_RESOURCE_TABLE_TEXTURE_BASE 0u
#define GFX_METAL_RESOURCE_TABLE_SAMPLER_BASE GFX_METAL_RESOURCE_TABLE_CAPACITY
#define GFX_METAL_RESOURCE_TABLE_BUFFER_BASE (GFX_METAL_RESOURCE_TABLE_CAPACITY * 2u)
#define GFX_METAL_INITIAL_RETIRED_BUFFER_CAPACITY 512u
#define GFX_METAL_INITIAL_RETIRED_TEXTURE_CAPACITY 256u
#define GFX_METAL_INITIAL_RETIRED_SAMPLER_CAPACITY 256u
#define GFX_METAL_INITIAL_RETIRED_PIPELINE_CAPACITY 256u

struct GfxMetalBuffer {
    id<MTLBuffer> buffer;
    U64 size;
    U32 usageFlags;
    GfxMemoryKind memoryKind;
    GfxResourceId resourceId;
    B32 internal;
};

struct GfxMetalTexture {
    id<MTLTexture> texture;
    U32 width;
    U32 height;
    U32 mipCount;
    GfxFormat format;
    GfxTextureStorageKind storageKind;
    U32 usageFlags;
    GfxResourceId resourceId;
    B32 ownsTexture;
    B32 internal;
};

struct GfxMetalSampler {
    id<MTLSamplerState> sampler;
    GfxResourceId resourceId;
};

struct GfxMetalPipeline {
    GfxPipelineKind kind;
    id<MTLRenderPipelineState> graphicsPipeline;
    id<MTLComputePipelineState> computePipeline;
    id<MTLDepthStencilState> depthState;
    GfxRasterState raster;
    U32 threadsPerThreadgroupX;
    U32 threadsPerThreadgroupY;
    U32 threadsPerThreadgroupZ;
};

struct GfxMetalRetiredBuffer {
    id<MTLBuffer> buffer;
    GfxResourceId resourceId;
    U64 retireSerial;
};

struct GfxMetalRetiredTexture {
    id<MTLTexture> texture;
    GfxResourceId resourceId;
    U64 retireSerial;
};

struct GfxMetalRetiredSampler {
    id<MTLSamplerState> sampler;
    GfxResourceId resourceId;
    U64 retireSerial;
};

struct GfxMetalRetiredPipeline {
    id<MTLRenderPipelineState> graphicsPipeline;
    id<MTLComputePipelineState> computePipeline;
    id<MTLDepthStencilState> depthState;
    U64 retireSerial;
};

struct GfxDevice;

struct GfxCommandBuffer {
    GfxFrame* frame;
};

struct GfxFrame {
    GfxDevice* device;
    GfxCommandBuffer commands;
    GfxBuffer tempBuffer;
    void* tempCpu;
    U64 tempSize;
    U64 tempPos;
    U32 slotIndex;
    B32 active;
    B32 submitted;
    U64 submittedSerial;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> commandBuffer;
};

struct GfxDevice {
    GfxBackend backend;
    Arena* arena;
    U32 validationFlags;
    B32 alive;

    CAMetalLayer* metalLayer;
    id<MTLDevice> metalDevice;
    id<MTLCommandQueue> commandQueue;
    dispatch_semaphore_t frameSemaphore;

    U32 framesInFlight;
    U32 frameCursor;
    U64 frameSerial;
    U64 completedFrameSerial;
    GfxFrame* frames;
    GfxFrame* activeFrame;

    SlotMap buffers;
    SlotMap textures;
    SlotMap samplers;
    SlotMap pipelines;

    GfxResourceTable resourceTable;
    id<MTLArgumentEncoder> resourceArgumentEncoder;
    id<MTLBuffer> resourceArgumentBuffer;

    GfxMetalRetiredBuffer* retiredBuffers;
    U32 retiredBufferCount;
    U32 retiredBufferCapacity;
    GfxMetalRetiredTexture* retiredTextures;
    U32 retiredTextureCount;
    U32 retiredTextureCapacity;
    GfxMetalRetiredSampler* retiredSamplers;
    U32 retiredSamplerCount;
    U32 retiredSamplerCapacity;
    GfxMetalRetiredPipeline* retiredPipelines;
    U32 retiredPipelineCount;
    U32 retiredPipelineCapacity;

    GfxTexture backbuffer;

    GfxStats stats;
};

static GfxMetalBuffer g_gfxMetalNilBuffer = {0, 0u, 0u, GfxMemoryKind_Device, {}, 1};
static GfxMetalTexture g_gfxMetalNilTexture = {0, 0u, 0u, 0u, GfxFormat_Invalid, GfxTextureStorageKind_Device, 0u, {}, 0, 1};
static GfxMetalSampler g_gfxMetalNilSampler = {0, {}};
static GfxMetalPipeline g_gfxMetalNilPipeline = {GfxPipelineKind_Graphics, 0, 0, 0, {}, 0u, 0u, 0u};

static NSString* gfx_nsstring_from_cstr(const char* str);
static NSString* gfx_nsstring_from_bytes(const void* data, U64 size);
static MTLPixelFormat gfx_metal_pixel_format(GfxFormat format);
static MTLLoadAction gfx_metal_load_action(GfxLoadOp op);
static MTLStoreAction gfx_metal_store_action(GfxStoreOp op);
static MTLIndexType gfx_metal_index_type(GfxIndexType type);
static MTLSamplerMinMagFilter gfx_metal_filter(GfxFilter filter);
static MTLSamplerAddressMode gfx_metal_address_mode(GfxAddressMode mode);
static MTLCompareFunction gfx_metal_compare_op(GfxCompareOp op);
static MTLCullMode gfx_metal_cull_mode(GfxCullMode mode);
static MTLWinding gfx_metal_front_face(GfxFrontFace face);
static MTLBlendFactor gfx_metal_blend_factor(GfxBlendFactor factor);
static MTLBlendOperation gfx_metal_blend_op(GfxBlendOp op);
static MTLColorWriteMask gfx_metal_color_write_mask(U32 writeFlags);
static MTLResourceUsage gfx_metal_resource_usage(U32 accessFlags);
static MTLRenderStages gfx_metal_render_stages(U32 shaderStages, U32 accessFlags);
static B32 gfx_metal_validation_has(GfxDevice* device, GfxValidationFlags flags);
static B32 gfx_metal_api_validation_enabled(GfxDevice* device);
static B32 gfx_metal_backend_validation_enabled(GfxDevice* device);
static B32 gfx_metal_gpu_markers_enabled(GfxDevice* device);
static void gfx_metal_api_assert(GfxDevice* device, B32 condition);
static CAMetalLayer* gfx_metal_layer_from_window(OS_WindowHandle window);
static GfxMetalBuffer* gfx_metal_resolve_buffer(GfxDevice* device, GfxBuffer handle);
static GfxMetalTexture* gfx_metal_resolve_texture(GfxDevice* device, GfxTexture handle);
static GfxMetalSampler* gfx_metal_resolve_sampler(GfxDevice* device, GfxSampler handle);
static GfxMetalPipeline* gfx_metal_resolve_pipeline(GfxDevice* device, GfxPipeline handle);
static B32 gfx_metal_upload_buffer_immediate(GfxDevice* device, id<MTLBuffer> dst, U64 dstOffset, const void* src, U64 size);
static void gfx_metal_retire_buffer(GfxDevice* device, GfxMetalBuffer* item);
static void gfx_metal_retire_texture(GfxDevice* device, GfxMetalTexture* item);
static void gfx_metal_retire_sampler(GfxDevice* device, GfxMetalSampler* item);
static void gfx_metal_retire_pipeline(GfxDevice* device, GfxMetalPipeline* item);
static void gfx_metal_drain_retired(GfxDevice* device);
static B32 gfx_metal_retired_buffers_reserve(GfxDevice* device, U32 neededCapacity);
static B32 gfx_metal_retired_textures_reserve(GfxDevice* device, U32 neededCapacity);
static B32 gfx_metal_retired_samplers_reserve(GfxDevice* device, U32 neededCapacity);
static B32 gfx_metal_retired_pipelines_reserve(GfxDevice* device, U32 neededCapacity);
static GfxBuffer gfx_metal_create_buffer_internal(GfxDevice* device, const GfxBufferDesc* desc, B32 internal);
static B32 gfx_metal_resource_table_init(GfxDevice* device);
static void gfx_metal_resource_table_set_texture(GfxDevice* device, GfxResourceId resourceId, id<MTLTexture> texture);
static void gfx_metal_resource_table_set_sampler(GfxDevice* device, GfxResourceId resourceId, id<MTLSamplerState> sampler);
static void gfx_metal_resource_table_set_buffer(GfxDevice* device, GfxResourceId resourceId, id<MTLBuffer> buffer);
static void gfx_metal_resource_table_clear(GfxDevice* device, GfxResourceId resourceId);
static GfxResourceId gfx_metal_register_resource(GfxDevice* device, GfxResourceKind kind, U32 index, U32 generation);
static void gfx_metal_release_frame_objects(GfxFrame* frame);
static void gfx_metal_render_use_resource(GfxDevice* device, id<MTLRenderCommandEncoder> encoder, const GfxResourceUse* use);
static void gfx_metal_compute_use_resource(GfxDevice* device, id<MTLComputeCommandEncoder> encoder, const GfxResourceUse* use);

static NSString* gfx_nsstring_from_cstr(const char* str) {
    if (!str) {
        return 0;
    }
    return [[NSString alloc] initWithUTF8String:str];
}

static NSString* gfx_nsstring_from_bytes(const void* data, U64 size) {
    if (!data || size == 0u) {
        return 0;
    }
    return [[NSString alloc] initWithBytes:data length:(NSUInteger)size encoding:NSUTF8StringEncoding];
}

static MTLPixelFormat gfx_metal_pixel_format(GfxFormat format) {
    switch (format) {
        case GfxFormat_R8_UNorm: {
            return MTLPixelFormatR8Unorm;
        }
        case GfxFormat_BGRA8_UNorm: {
            return MTLPixelFormatBGRA8Unorm;
        }
        case GfxFormat_RGBA8_UNorm: {
            return MTLPixelFormatRGBA8Unorm;
        }
        case GfxFormat_RGBA16_Float: {
            return MTLPixelFormatRGBA16Float;
        }
        case GfxFormat_D32_Float: {
            return MTLPixelFormatDepth32Float;
        }
        default: {
            return MTLPixelFormatInvalid;
        }
    }
}

static MTLLoadAction gfx_metal_load_action(GfxLoadOp op) {
    switch (op) {
        case GfxLoadOp_Load: {
            return MTLLoadActionLoad;
        }
        case GfxLoadOp_Clear: {
            return MTLLoadActionClear;
        }
        case GfxLoadOp_DontCare: {
            return MTLLoadActionDontCare;
        }
        default: {
            return MTLLoadActionDontCare;
        }
    }
}

static MTLStoreAction gfx_metal_store_action(GfxStoreOp op) {
    switch (op) {
        case GfxStoreOp_Store: {
            return MTLStoreActionStore;
        }
        case GfxStoreOp_DontCare: {
            return MTLStoreActionDontCare;
        }
        default: {
            return MTLStoreActionDontCare;
        }
    }
}

static MTLIndexType gfx_metal_index_type(GfxIndexType type) {
    return (type == GfxIndexType_U16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

static MTLSamplerMinMagFilter gfx_metal_filter(GfxFilter filter) {
    return (filter == GfxFilter_Nearest) ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

static MTLSamplerAddressMode gfx_metal_address_mode(GfxAddressMode mode) {
    return (mode == GfxAddressMode_Repeat) ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
}

static MTLCompareFunction gfx_metal_compare_op(GfxCompareOp op) {
    switch (op) {
        case GfxCompareOp_Less: {
            return MTLCompareFunctionLess;
        }
        case GfxCompareOp_LessEqual: {
            return MTLCompareFunctionLessEqual;
        }
        case GfxCompareOp_Always:
        default: {
            return MTLCompareFunctionAlways;
        }
    }
}

static MTLCullMode gfx_metal_cull_mode(GfxCullMode mode) {
    switch (mode) {
        case GfxCullMode_Front: {
            return MTLCullModeFront;
        }
        case GfxCullMode_Back: {
            return MTLCullModeBack;
        }
        case GfxCullMode_None:
        default: {
            return MTLCullModeNone;
        }
    }
}

static MTLWinding gfx_metal_front_face(GfxFrontFace face) {
    return (face == GfxFrontFace_CW) ? MTLWindingClockwise : MTLWindingCounterClockwise;
}

static MTLBlendFactor gfx_metal_blend_factor(GfxBlendFactor factor) {
    switch (factor) {
        case GfxBlendFactor_Zero: {
            return MTLBlendFactorZero;
        }
        case GfxBlendFactor_SrcAlpha: {
            return MTLBlendFactorSourceAlpha;
        }
        case GfxBlendFactor_OneMinusSrcAlpha: {
            return MTLBlendFactorOneMinusSourceAlpha;
        }
        case GfxBlendFactor_One:
        default: {
            return MTLBlendFactorOne;
        }
    }
}

static MTLBlendOperation gfx_metal_blend_op(GfxBlendOp op) {
    (void)op;
    return MTLBlendOperationAdd;
}

static MTLColorWriteMask gfx_metal_color_write_mask(U32 writeFlags) {
    MTLColorWriteMask result = 0;
    if (FLAGS_HAS(writeFlags, GfxColorWriteFlags_R)) {
        result |= MTLColorWriteMaskRed;
    }
    if (FLAGS_HAS(writeFlags, GfxColorWriteFlags_G)) {
        result |= MTLColorWriteMaskGreen;
    }
    if (FLAGS_HAS(writeFlags, GfxColorWriteFlags_B)) {
        result |= MTLColorWriteMaskBlue;
    }
    if (FLAGS_HAS(writeFlags, GfxColorWriteFlags_A)) {
        result |= MTLColorWriteMaskAlpha;
    }
    return result;
}

static MTLResourceUsage gfx_metal_resource_usage(U32 accessFlags) {
    MTLResourceUsage result = 0;
    if (FLAGS_HAS(accessFlags, GfxResourceAccessFlags_ShaderRead) ||
        FLAGS_HAS(accessFlags, GfxResourceAccessFlags_IndirectRead)) {
        result |= MTLResourceUsageRead;
    }
    if (FLAGS_HAS(accessFlags, GfxResourceAccessFlags_ShaderWrite)) {
        result |= MTLResourceUsageWrite;
    }
    if (result == 0) {
        result = MTLResourceUsageRead;
    }
    return result;
}

static MTLRenderStages gfx_metal_render_stages(U32 shaderStages, U32 accessFlags) {
    MTLRenderStages result = 0;
    if (FLAGS_HAS(shaderStages, GfxShaderStageFlags_Vertex)) {
        result |= MTLRenderStageVertex;
    }
    if (FLAGS_HAS(shaderStages, GfxShaderStageFlags_Fragment)) {
        result |= MTLRenderStageFragment;
    }
    if (result == 0 && FLAGS_HAS(accessFlags, GfxResourceAccessFlags_IndirectRead)) {
        result = MTLRenderStageVertex;
    }
    if (result == 0) {
        result = MTLRenderStageVertex | MTLRenderStageFragment;
    }
    return result;
}

static B32 gfx_metal_validation_has(GfxDevice* device, GfxValidationFlags flags) {
    return (device != 0 && FLAGS_HAS(device->validationFlags, flags)) ? 1 : 0;
}

static B32 gfx_metal_api_validation_enabled(GfxDevice* device) {
    return gfx_metal_validation_has(device, GfxValidationFlags_Api);
}

static B32 gfx_metal_backend_validation_enabled(GfxDevice* device) {
    return gfx_metal_validation_has(device, GfxValidationFlags_Backend);
}

static B32 gfx_metal_gpu_markers_enabled(GfxDevice* device) {
    return gfx_metal_validation_has(device, GfxValidationFlags_GpuMarkers);
}

static void gfx_metal_api_assert(GfxDevice* device, B32 condition) {
    if (gfx_metal_api_validation_enabled(device)) {
        ASSERT_DEBUG(condition);
    }
}

static CAMetalLayer* gfx_metal_layer_from_window(OS_WindowHandle window) {
    if (!window.handle) {
        return 0;
    }

    OS_MACOS_GraphicsEntity* entity = (OS_MACOS_GraphicsEntity*)window.handle;
    if (entity->type != OS_MACOS_GraphicsEntityType_Window || !entity->window.window) {
        return 0;
    }

    CAMetalLayer* result = 0;
    @autoreleasepool {
        NSView* contentView = [entity->window.window contentView];
        CALayer* layer = contentView ? [contentView layer] : 0;
        if (layer && [layer isKindOfClass:[CAMetalLayer class]]) {
            result = (CAMetalLayer*)layer;
        }
    }

    return result;
}

static GfxMetalBuffer* gfx_metal_resolve_buffer(GfxDevice* device, GfxBuffer handle) {
    gfx_metal_api_assert(device, device != 0);
    if (!device) {
        return &g_gfxMetalNilBuffer;
    }
    if (handle.generation == 0u) {
        gfx_metal_api_assert(device, handle.index == 0u && "Malformed nil gfx buffer handle");
        return &g_gfxMetalNilBuffer;
    }

    GfxMetalBuffer* result = (GfxMetalBuffer*)slot_map_get(&device->buffers, handle.index, handle.generation);
    gfx_metal_api_assert(device, result != 0 && "Stale gfx buffer handle");
    return result ? result : &g_gfxMetalNilBuffer;
}

static GfxMetalTexture* gfx_metal_resolve_texture(GfxDevice* device, GfxTexture handle) {
    gfx_metal_api_assert(device, device != 0);
    if (!device) {
        return &g_gfxMetalNilTexture;
    }
    if (handle.generation == 0u) {
        gfx_metal_api_assert(device, handle.index == 0u && "Malformed nil gfx texture handle");
        return &g_gfxMetalNilTexture;
    }

    GfxMetalTexture* result = (GfxMetalTexture*)slot_map_get(&device->textures, handle.index, handle.generation);
    gfx_metal_api_assert(device, result != 0 && "Stale gfx texture handle");
    return result ? result : &g_gfxMetalNilTexture;
}

static GfxMetalSampler* gfx_metal_resolve_sampler(GfxDevice* device, GfxSampler handle) {
    gfx_metal_api_assert(device, device != 0);
    if (!device) {
        return &g_gfxMetalNilSampler;
    }
    if (handle.generation == 0u) {
        gfx_metal_api_assert(device, handle.index == 0u && "Malformed nil gfx sampler handle");
        return &g_gfxMetalNilSampler;
    }

    GfxMetalSampler* result = (GfxMetalSampler*)slot_map_get(&device->samplers, handle.index, handle.generation);
    gfx_metal_api_assert(device, result != 0 && "Stale gfx sampler handle");
    return result ? result : &g_gfxMetalNilSampler;
}

static GfxMetalPipeline* gfx_metal_resolve_pipeline(GfxDevice* device, GfxPipeline handle) {
    gfx_metal_api_assert(device, device != 0);
    if (!device) {
        return &g_gfxMetalNilPipeline;
    }
    if (handle.generation == 0u) {
        gfx_metal_api_assert(device, handle.index == 0u && "Malformed nil gfx pipeline handle");
        return &g_gfxMetalNilPipeline;
    }

    GfxMetalPipeline* result = (GfxMetalPipeline*)slot_map_get(&device->pipelines, handle.index, handle.generation);
    gfx_metal_api_assert(device, result != 0 && "Stale gfx pipeline handle");
    return result ? result : &g_gfxMetalNilPipeline;
}

static B32 gfx_metal_upload_buffer_immediate(GfxDevice* device, id<MTLBuffer> dst, U64 dstOffset, const void* src, U64 size) {
    ASSERT_DEBUG(device != 0);
    ASSERT_DEBUG(dst != 0);
    ASSERT_DEBUG(src != 0);
    ASSERT_DEBUG(size != 0u);

    if (!device || !dst || !src || size == 0u) {
        return 0;
    }

    id<MTLBuffer> staging = [device->metalDevice newBufferWithLength:(NSUInteger)size
                                                              options:MTLResourceStorageModeShared];
    if (!staging) {
        LOG_ERROR("gfx", "Metal staging buffer creation failed (size={})", size);
        return 0;
    }

    void* stagingCpu = [staging contents];
    ASSERT_DEBUG(stagingCpu != 0);
    if (!stagingCpu) {
        [staging release];
        return 0;
    }
    MEMCPY(stagingCpu, src, size);

    id<MTLCommandBuffer> commandBuffer = [[device->commandQueue commandBuffer] retain];
    if (!commandBuffer) {
        [staging release];
        return 0;
    }

    id<MTLBlitCommandEncoder> blit = [[commandBuffer blitCommandEncoder] retain];
    if (!blit) {
        [commandBuffer release];
        [staging release];
        return 0;
    }

    [blit copyFromBuffer:staging
            sourceOffset:0u
                toBuffer:dst
       destinationOffset:(NSUInteger)dstOffset
                    size:(NSUInteger)size];
    [blit endEncoding];
    [blit release];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    B32 result = ([commandBuffer status] == MTLCommandBufferStatusCompleted) ? 1 : 0;
    if (!result) {
        LOG_ERROR("gfx", "Metal immediate buffer upload failed");
    }

    [commandBuffer release];
    [staging release];
    return result;
}

static GfxBuffer gfx_metal_create_buffer_internal(GfxDevice* device, const GfxBufferDesc* desc, B32 internal) {
    if (!device || !desc || desc->size == 0u) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid buffer descriptor");
        }
        return {};
    }

    MTLResourceOptions options = (desc->memoryKind == GfxMemoryKind_Device) ? MTLResourceStorageModePrivate : MTLResourceStorageModeShared;
    id<MTLBuffer> buffer = [device->metalDevice newBufferWithLength:(NSUInteger)desc->size options:options];
    if (!buffer) {
        LOG_ERROR("gfx", "Metal buffer creation failed (size={})", desc->size);
        return {};
    }

    if (gfx_metal_gpu_markers_enabled(device) && desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            [buffer setLabel:label];
            [label release];
        }
    }

    if (desc->initialData && desc->memoryKind == GfxMemoryKind_Upload) {
        void* dst = [buffer contents];
        ASSERT_DEBUG(dst != 0);
        if (!dst) {
            [buffer release];
            return {};
        }
        MEMCPY(dst, desc->initialData, desc->size);
    } else if (desc->initialData) {
        if (!gfx_metal_upload_buffer_immediate(device, buffer, 0u, desc->initialData, desc->size)) {
            [buffer release];
            return {};
        }
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->buffers, &slotItem, &slotIndex, &generation)) {
        [buffer release];
        return {};
    }

    GfxMetalBuffer* item = (GfxMetalBuffer*)slotItem;
    item->buffer = buffer;
    item->size = desc->size;
    item->usageFlags = desc->usageFlags;
    item->memoryKind = desc->memoryKind;
    item->resourceId = {};
    item->internal = internal;

    GfxBuffer result = {slotIndex, generation};
    return result;
}

static B32 gfx_metal_resource_table_init(GfxDevice* device) {
    ASSERT_DEBUG(device != 0);
    if (!device || !device->metalDevice) {
        return 0;
    }
    if (!gfx_resource_table_init(&device->resourceTable, device->arena, GFX_METAL_RESOURCE_TABLE_CAPACITY)) {
        return 0;
    }

    MTLArgumentDescriptor* textureArg = [MTLArgumentDescriptor argumentDescriptor];
    MTLArgumentDescriptor* samplerArg = [MTLArgumentDescriptor argumentDescriptor];
    MTLArgumentDescriptor* bufferArg = [MTLArgumentDescriptor argumentDescriptor];
    if (!textureArg || !samplerArg || !bufferArg) {
        return 0;
    }

    textureArg.dataType = MTLDataTypeTexture;
    textureArg.index = GFX_METAL_RESOURCE_TABLE_TEXTURE_BASE;
    textureArg.arrayLength = GFX_METAL_RESOURCE_TABLE_CAPACITY;
    textureArg.textureType = MTLTextureType2D;
    textureArg.access = MTLBindingAccessReadOnly;

    samplerArg.dataType = MTLDataTypeSampler;
    samplerArg.index = GFX_METAL_RESOURCE_TABLE_SAMPLER_BASE;
    samplerArg.arrayLength = GFX_METAL_RESOURCE_TABLE_CAPACITY;

    bufferArg.dataType = MTLDataTypePointer;
    bufferArg.index = GFX_METAL_RESOURCE_TABLE_BUFFER_BASE;
    bufferArg.arrayLength = GFX_METAL_RESOURCE_TABLE_CAPACITY;
    bufferArg.access = MTLBindingAccessReadWrite;

    NSArray<MTLArgumentDescriptor*>* args = @[textureArg, samplerArg, bufferArg];
    device->resourceArgumentEncoder = [device->metalDevice newArgumentEncoderWithArguments:args];
    if (!device->resourceArgumentEncoder) {
        return 0;
    }

    device->resourceArgumentBuffer = [device->metalDevice newBufferWithLength:[device->resourceArgumentEncoder encodedLength]
                                                                       options:MTLResourceStorageModeShared];
    if (!device->resourceArgumentBuffer) {
        return 0;
    }

    if (gfx_metal_gpu_markers_enabled(device)) {
        [device->resourceArgumentBuffer setLabel:@"gfx resource table"];
    }
    [device->resourceArgumentEncoder setArgumentBuffer:device->resourceArgumentBuffer offset:0u];
    for (U32 i = 0u; i < GFX_METAL_RESOURCE_TABLE_CAPACITY; ++i) {
        [device->resourceArgumentEncoder setTexture:nil atIndex:GFX_METAL_RESOURCE_TABLE_TEXTURE_BASE + i];
        [device->resourceArgumentEncoder setSamplerState:nil atIndex:GFX_METAL_RESOURCE_TABLE_SAMPLER_BASE + i];
        [device->resourceArgumentEncoder setBuffer:nil offset:0u atIndex:GFX_METAL_RESOURCE_TABLE_BUFFER_BASE + i];
    }

    return 1;
}

static void gfx_metal_resource_table_set_texture(GfxDevice* device, GfxResourceId resourceId, id<MTLTexture> texture) {
    if (!device || !device->resourceArgumentEncoder ||
        resourceId.index == 0u ||
        resourceId.index >= GFX_METAL_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    [device->resourceArgumentEncoder setTexture:texture
                                        atIndex:GFX_METAL_RESOURCE_TABLE_TEXTURE_BASE + resourceId.index];
}

static void gfx_metal_resource_table_set_sampler(GfxDevice* device, GfxResourceId resourceId, id<MTLSamplerState> sampler) {
    if (!device || !device->resourceArgumentEncoder ||
        resourceId.index == 0u ||
        resourceId.index >= GFX_METAL_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    [device->resourceArgumentEncoder setSamplerState:sampler
                                             atIndex:GFX_METAL_RESOURCE_TABLE_SAMPLER_BASE + resourceId.index];
}

static void gfx_metal_resource_table_set_buffer(GfxDevice* device, GfxResourceId resourceId, id<MTLBuffer> buffer) {
    if (!device || !device->resourceArgumentEncoder ||
        resourceId.index == 0u ||
        resourceId.index >= GFX_METAL_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    [device->resourceArgumentEncoder setBuffer:buffer
                                        offset:0u
                                       atIndex:GFX_METAL_RESOURCE_TABLE_BUFFER_BASE + resourceId.index];
}

static void gfx_metal_resource_table_clear(GfxDevice* device, GfxResourceId resourceId) {
    if (!device) {
        return;
    }

    GfxResourceTableEntry* entry = gfx_resource_table_get(&device->resourceTable, resourceId);
    if (!entry) {
        return;
    }
    if (entry->kind == GfxResourceKind_Texture) {
        gfx_metal_resource_table_set_texture(device, resourceId, nil);
    } else if (entry->kind == GfxResourceKind_Sampler) {
        gfx_metal_resource_table_set_sampler(device, resourceId, nil);
    } else if (entry->kind == GfxResourceKind_Buffer) {
        gfx_metal_resource_table_set_buffer(device, resourceId, nil);
    }

    gfx_resource_table_release(&device->resourceTable, resourceId);
    device->stats.resourceTableCount = device->resourceTable.liveCount;
}

static GfxResourceId gfx_metal_register_resource(GfxDevice* device, GfxResourceKind kind, U32 index, U32 generation) {
    if (!device) {
        return {};
    }
    GfxResourceId result = gfx_resource_table_alloc(&device->resourceTable, kind, index, generation);
    device->stats.resourceTableCount = device->resourceTable.liveCount;
    return result;
}

static void gfx_metal_render_use_resource(GfxDevice* device, id<MTLRenderCommandEncoder> encoder, const GfxResourceUse* use) {
    if (!device || !encoder || !use) {
        return;
    }

    MTLResourceUsage usage = gfx_metal_resource_usage(use->accessFlags);
    MTLRenderStages stages = gfx_metal_render_stages(use->shaderStages, use->accessFlags);
    if (use->kind == GfxResourceUseKind_Texture) {
        GfxMetalTexture* texture = gfx_metal_resolve_texture(device, use->texture);
        if (texture->texture) {
            [encoder useResource:texture->texture usage:usage stages:stages];
        }
    } else {
        GfxMetalBuffer* buffer = gfx_metal_resolve_buffer(device, use->buffer);
        if (buffer->buffer) {
            [encoder useResource:buffer->buffer usage:usage stages:stages];
        }
    }
}

static void gfx_metal_compute_use_resource(GfxDevice* device, id<MTLComputeCommandEncoder> encoder, const GfxResourceUse* use) {
    if (!device || !encoder || !use) {
        return;
    }

    MTLResourceUsage usage = gfx_metal_resource_usage(use->accessFlags);
    if (use->kind == GfxResourceUseKind_Texture) {
        GfxMetalTexture* texture = gfx_metal_resolve_texture(device, use->texture);
        if (texture->texture) {
            [encoder useResource:texture->texture usage:usage];
        }
    } else {
        GfxMetalBuffer* buffer = gfx_metal_resolve_buffer(device, use->buffer);
        if (buffer->buffer) {
            [encoder useResource:buffer->buffer usage:usage];
        }
    }
}

static U64 gfx_metal_retire_serial(GfxDevice* device) {
    return device ? device->frameSerial : 0u;
}

static void gfx_metal_release_retired_buffer(GfxDevice* device, GfxMetalRetiredBuffer* item) {
    if (device && item) {
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
    }
    if (item && item->buffer) {
        [item->buffer release];
        item->buffer = nil;
    }
}

static void gfx_metal_release_retired_texture(GfxDevice* device, GfxMetalRetiredTexture* item) {
    if (device && item) {
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
    }
    if (item && item->texture) {
        [item->texture release];
        item->texture = nil;
    }
}

static void gfx_metal_release_retired_sampler(GfxDevice* device, GfxMetalRetiredSampler* item) {
    if (device && item) {
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
    }
    if (item && item->sampler) {
        [item->sampler release];
        item->sampler = nil;
    }
}

static void gfx_metal_release_retired_pipeline(GfxMetalRetiredPipeline* item) {
    if (!item) {
        return;
    }
    if (item->graphicsPipeline) {
        [item->graphicsPipeline release];
        item->graphicsPipeline = nil;
    }
    if (item->computePipeline) {
        [item->computePipeline release];
        item->computePipeline = nil;
    }
    if (item->depthState) {
        [item->depthState release];
        item->depthState = nil;
    }
}

static B32 gfx_metal_retired_buffers_reserve(GfxDevice* device, U32 neededCapacity) {
    if (!device) {
        return 0;
    }
    if (neededCapacity <= device->retiredBufferCapacity) {
        return 1;
    }

    U32 newCapacity = device->retiredBufferCapacity ? device->retiredBufferCapacity * 2u : GFX_METAL_INITIAL_RETIRED_BUFFER_CAPACITY;
    while (newCapacity < neededCapacity) {
        newCapacity *= 2u;
    }
    GfxMetalRetiredBuffer* newItems = ARENA_PUSH_ARRAY(device->arena, GfxMetalRetiredBuffer, newCapacity);
    if (!newItems) {
        return 0;
    }
    MEMSET(newItems, 0, sizeof(GfxMetalRetiredBuffer) * newCapacity);
    if (device->retiredBuffers && device->retiredBufferCount != 0u) {
        MEMCPY(newItems, device->retiredBuffers, sizeof(GfxMetalRetiredBuffer) * device->retiredBufferCount);
    }
    device->retiredBuffers = newItems;
    device->retiredBufferCapacity = newCapacity;
    return 1;
}

static B32 gfx_metal_retired_textures_reserve(GfxDevice* device, U32 neededCapacity) {
    if (!device) {
        return 0;
    }
    if (neededCapacity <= device->retiredTextureCapacity) {
        return 1;
    }

    U32 newCapacity = device->retiredTextureCapacity ? device->retiredTextureCapacity * 2u : GFX_METAL_INITIAL_RETIRED_TEXTURE_CAPACITY;
    while (newCapacity < neededCapacity) {
        newCapacity *= 2u;
    }
    GfxMetalRetiredTexture* newItems = ARENA_PUSH_ARRAY(device->arena, GfxMetalRetiredTexture, newCapacity);
    if (!newItems) {
        return 0;
    }
    MEMSET(newItems, 0, sizeof(GfxMetalRetiredTexture) * newCapacity);
    if (device->retiredTextures && device->retiredTextureCount != 0u) {
        MEMCPY(newItems, device->retiredTextures, sizeof(GfxMetalRetiredTexture) * device->retiredTextureCount);
    }
    device->retiredTextures = newItems;
    device->retiredTextureCapacity = newCapacity;
    return 1;
}

static B32 gfx_metal_retired_samplers_reserve(GfxDevice* device, U32 neededCapacity) {
    if (!device) {
        return 0;
    }
    if (neededCapacity <= device->retiredSamplerCapacity) {
        return 1;
    }

    U32 newCapacity = device->retiredSamplerCapacity ? device->retiredSamplerCapacity * 2u : GFX_METAL_INITIAL_RETIRED_SAMPLER_CAPACITY;
    while (newCapacity < neededCapacity) {
        newCapacity *= 2u;
    }
    GfxMetalRetiredSampler* newItems = ARENA_PUSH_ARRAY(device->arena, GfxMetalRetiredSampler, newCapacity);
    if (!newItems) {
        return 0;
    }
    MEMSET(newItems, 0, sizeof(GfxMetalRetiredSampler) * newCapacity);
    if (device->retiredSamplers && device->retiredSamplerCount != 0u) {
        MEMCPY(newItems, device->retiredSamplers, sizeof(GfxMetalRetiredSampler) * device->retiredSamplerCount);
    }
    device->retiredSamplers = newItems;
    device->retiredSamplerCapacity = newCapacity;
    return 1;
}

static B32 gfx_metal_retired_pipelines_reserve(GfxDevice* device, U32 neededCapacity) {
    if (!device) {
        return 0;
    }
    if (neededCapacity <= device->retiredPipelineCapacity) {
        return 1;
    }

    U32 newCapacity = device->retiredPipelineCapacity ? device->retiredPipelineCapacity * 2u : GFX_METAL_INITIAL_RETIRED_PIPELINE_CAPACITY;
    while (newCapacity < neededCapacity) {
        newCapacity *= 2u;
    }
    GfxMetalRetiredPipeline* newItems = ARENA_PUSH_ARRAY(device->arena, GfxMetalRetiredPipeline, newCapacity);
    if (!newItems) {
        return 0;
    }
    MEMSET(newItems, 0, sizeof(GfxMetalRetiredPipeline) * newCapacity);
    if (device->retiredPipelines && device->retiredPipelineCount != 0u) {
        MEMCPY(newItems, device->retiredPipelines, sizeof(GfxMetalRetiredPipeline) * device->retiredPipelineCount);
    }
    device->retiredPipelines = newItems;
    device->retiredPipelineCapacity = newCapacity;
    return 1;
}

static void gfx_metal_retire_buffer(GfxDevice* device, GfxMetalBuffer* item) {
    if (!device || !item || !item->buffer) {
        return;
    }

    U64 serial = gfx_metal_retire_serial(device);
    if (device->completedFrameSerial >= serial) {
        [item->buffer release];
        item->buffer = nil;
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    if (!gfx_metal_retired_buffers_reserve(device, device->retiredBufferCount + 1u)) {
        LOG_WARNING("gfx", "Metal retired buffer queue allocation failed; buffer will stay alive until process exit");
        item->buffer = nil;
        return;
    }

    GfxMetalRetiredBuffer* retired = &device->retiredBuffers[device->retiredBufferCount++];
    retired->buffer = item->buffer;
    retired->resourceId = item->resourceId;
    retired->retireSerial = serial;
    item->buffer = nil;
    item->resourceId = {};
}

static void gfx_metal_retire_texture(GfxDevice* device, GfxMetalTexture* item) {
    if (!device || !item || !item->texture) {
        return;
    }
    if (!item->ownsTexture) {
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }

    U64 serial = gfx_metal_retire_serial(device);
    if (device->completedFrameSerial >= serial) {
        [item->texture release];
        item->texture = nil;
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    if (!gfx_metal_retired_textures_reserve(device, device->retiredTextureCount + 1u)) {
        LOG_WARNING("gfx", "Metal retired texture queue allocation failed; texture will stay alive until process exit");
        item->texture = nil;
        return;
    }

    GfxMetalRetiredTexture* retired = &device->retiredTextures[device->retiredTextureCount++];
    retired->texture = item->texture;
    retired->resourceId = item->resourceId;
    retired->retireSerial = serial;
    item->texture = nil;
    item->resourceId = {};
}

static void gfx_metal_retire_sampler(GfxDevice* device, GfxMetalSampler* item) {
    if (!device || !item || !item->sampler) {
        return;
    }

    U64 serial = gfx_metal_retire_serial(device);
    if (device->completedFrameSerial >= serial) {
        [item->sampler release];
        item->sampler = nil;
        gfx_metal_resource_table_clear(device, item->resourceId);
        item->resourceId = {};
        return;
    }
    if (!gfx_metal_retired_samplers_reserve(device, device->retiredSamplerCount + 1u)) {
        LOG_WARNING("gfx", "Metal retired sampler queue allocation failed; sampler will stay alive until process exit");
        item->sampler = nil;
        return;
    }

    GfxMetalRetiredSampler* retired = &device->retiredSamplers[device->retiredSamplerCount++];
    retired->sampler = item->sampler;
    retired->resourceId = item->resourceId;
    retired->retireSerial = serial;
    item->sampler = nil;
    item->resourceId = {};
}

static void gfx_metal_retire_pipeline(GfxDevice* device, GfxMetalPipeline* item) {
    if (!device || !item || (!item->graphicsPipeline && !item->computePipeline && !item->depthState)) {
        return;
    }

    U64 serial = gfx_metal_retire_serial(device);
    if (device->completedFrameSerial >= serial) {
        GfxMetalRetiredPipeline retired = {
            .graphicsPipeline = item->graphicsPipeline,
            .computePipeline = item->computePipeline,
            .depthState = item->depthState,
            .retireSerial = serial,
        };
        gfx_metal_release_retired_pipeline(&retired);
        item->graphicsPipeline = nil;
        item->computePipeline = nil;
        item->depthState = nil;
        return;
    }
    if (!gfx_metal_retired_pipelines_reserve(device, device->retiredPipelineCount + 1u)) {
        LOG_WARNING("gfx", "Metal retired pipeline queue allocation failed; pipeline will stay alive until process exit");
        item->graphicsPipeline = nil;
        item->computePipeline = nil;
        item->depthState = nil;
        return;
    }

    GfxMetalRetiredPipeline* retired = &device->retiredPipelines[device->retiredPipelineCount++];
    retired->graphicsPipeline = item->graphicsPipeline;
    retired->computePipeline = item->computePipeline;
    retired->depthState = item->depthState;
    retired->retireSerial = serial;
    item->graphicsPipeline = nil;
    item->computePipeline = nil;
    item->depthState = nil;
}

static void gfx_metal_drain_retired(GfxDevice* device) {
    if (!device) {
        return;
    }

    for (U32 i = 0u; i < device->retiredBufferCount;) {
        GfxMetalRetiredBuffer* item = &device->retiredBuffers[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_metal_release_retired_buffer(device, item);
        device->retiredBuffers[i] = device->retiredBuffers[--device->retiredBufferCount];
    }

    for (U32 i = 0u; i < device->retiredTextureCount;) {
        GfxMetalRetiredTexture* item = &device->retiredTextures[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_metal_release_retired_texture(device, item);
        device->retiredTextures[i] = device->retiredTextures[--device->retiredTextureCount];
    }

    for (U32 i = 0u; i < device->retiredSamplerCount;) {
        GfxMetalRetiredSampler* item = &device->retiredSamplers[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_metal_release_retired_sampler(device, item);
        device->retiredSamplers[i] = device->retiredSamplers[--device->retiredSamplerCount];
    }

    for (U32 i = 0u; i < device->retiredPipelineCount;) {
        GfxMetalRetiredPipeline* item = &device->retiredPipelines[i];
        if (item->retireSerial > device->completedFrameSerial) {
            i += 1u;
            continue;
        }
        gfx_metal_release_retired_pipeline(item);
        device->retiredPipelines[i] = device->retiredPipelines[--device->retiredPipelineCount];
    }
}

static void gfx_metal_release_frame_objects(GfxFrame* frame) {
    if (!frame) {
        return;
    }
    if (frame->commandBuffer) {
        [frame->commandBuffer release];
        frame->commandBuffer = 0;
    }
    if (frame->drawable) {
        [frame->drawable release];
        frame->drawable = 0;
    }
}

B32 gfx_device_create(const GfxDeviceDesc* desc, Arena* arena, GfxDevice** outDevice) {
    if (!desc || !arena || !outDevice) {
        return 0;
    }
    *outDevice = 0;

    if (desc->backend != GfxBackend_Metal) {
        LOG_ERROR("gfx", "Only Metal backend is implemented in this build");
        return 0;
    }

    CAMetalLayer* layer = gfx_metal_layer_from_window(desc->window);
    if (!layer) {
        LOG_ERROR("gfx", "Window has no CAMetalLayer");
        return 0;
    }

    id<MTLDevice> metalDevice = [layer device];
    if (metalDevice) {
        [metalDevice retain];
    } else {
        metalDevice = MTLCreateSystemDefaultDevice();
        if (metalDevice) {
            [layer setDevice:metalDevice];
        }
    }

    if (!metalDevice) {
        LOG_ERROR("gfx", "No Metal device available");
        return 0;
    }

    id<MTLCommandQueue> commandQueue = [metalDevice newCommandQueue];
    if (!commandQueue) {
        [metalDevice release];
        LOG_ERROR("gfx", "Failed to create Metal command queue");
        return 0;
    }

    GfxDevice* device = ARENA_PUSH_STRUCT(arena, GfxDevice);
    if (!device) {
        [commandQueue release];
        [metalDevice release];
        return 0;
    }
    MEMSET(device, 0, sizeof(*device));

    device->backend = GfxBackend_Metal;
    device->arena = arena;
    device->validationFlags = desc->validationFlags;
    device->alive = 1;
    device->metalLayer = [layer retain];
    device->metalDevice = metalDevice;
    device->commandQueue = commandQueue;
    device->framesInFlight = desc->framesInFlight ? desc->framesInFlight : GFX_DEFAULT_FRAMES_IN_FLIGHT;
    device->frameSemaphore = dispatch_semaphore_create((long)device->framesInFlight);

    if (device->framesInFlight == 0u) {
        device->framesInFlight = GFX_DEFAULT_FRAMES_IN_FLIGHT;
    }
    if (!device->frameSemaphore) {
        gfx_device_destroy(device);
        return 0;
    }

    if (!gfx_metal_resource_table_init(device)) {
        LOG_ERROR("gfx", "Failed to initialize Metal resource table");
        gfx_device_destroy(device);
        return 0;
    }

    if (!gfx_metal_retired_buffers_reserve(device, GFX_METAL_INITIAL_RETIRED_BUFFER_CAPACITY) ||
        !gfx_metal_retired_textures_reserve(device, GFX_METAL_INITIAL_RETIRED_TEXTURE_CAPACITY) ||
        !gfx_metal_retired_samplers_reserve(device, GFX_METAL_INITIAL_RETIRED_SAMPLER_CAPACITY) ||
        !gfx_metal_retired_pipelines_reserve(device, GFX_METAL_INITIAL_RETIRED_PIPELINE_CAPACITY)) {
        LOG_ERROR("gfx", "Failed to allocate Metal retired resource queues");
        gfx_device_destroy(device);
        return 0;
    }

    if (!slot_map_init(&device->buffers, arena, sizeof(GfxMetalBuffer), 128u) ||
        !slot_map_init(&device->textures, arena, sizeof(GfxMetalTexture), 128u) ||
        !slot_map_init(&device->samplers, arena, sizeof(GfxMetalSampler), 64u) ||
        !slot_map_init(&device->pipelines, arena, sizeof(GfxMetalPipeline), 64u)) {
        LOG_ERROR("gfx", "Failed to initialize gfx resource pools");
        gfx_device_destroy(device);
        return 0;
    }

    device->frames = ARENA_PUSH_ARRAY(arena, GfxFrame, device->framesInFlight);
    if (!device->frames) {
        gfx_device_destroy(device);
        return 0;
    }
    MEMSET(device->frames, 0, sizeof(GfxFrame) * device->framesInFlight);

    U64 tempSize = desc->tempBufferSize ? desc->tempBufferSize : GFX_DEFAULT_TEMP_BUFFER_SIZE;
    for (U32 i = 0; i < device->framesInFlight; ++i) {
        GfxFrame* frame = &device->frames[i];
        frame->device = device;
        frame->commands.frame = frame;
        frame->slotIndex = i;

        GfxBufferDesc tempDesc = {};
        tempDesc.name = "gfx temp buffer";
        tempDesc.size = tempSize;
        tempDesc.usageFlags = GfxBufferUsageFlags_Uniform | GfxBufferUsageFlags_Storage;
        tempDesc.memoryKind = GfxMemoryKind_Upload;
        frame->tempBuffer = gfx_metal_create_buffer_internal(device, &tempDesc, 1);

        GfxMetalBuffer* tempBuffer = gfx_metal_resolve_buffer(device, frame->tempBuffer);
        if (!tempBuffer || !tempBuffer->buffer) {
            gfx_device_destroy(device);
            return 0;
        }
        frame->tempCpu = [tempBuffer->buffer contents];
        frame->tempSize = tempSize;
    }

    void* textureSlot = 0;
    U32 textureIndex = 0u;
    U32 textureGeneration = 0u;
    if (!slot_map_alloc(&device->textures, &textureSlot, &textureIndex, &textureGeneration)) {
        gfx_device_destroy(device);
        return 0;
    }
    GfxMetalTexture* backbuffer = (GfxMetalTexture*)textureSlot;
    backbuffer->format = GfxFormat_BGRA8_UNorm;
    backbuffer->storageKind = GfxTextureStorageKind_Device;
    backbuffer->usageFlags = GfxTextureUsageFlags_ColorTarget;
    backbuffer->resourceId = {};
    backbuffer->ownsTexture = 0;
    backbuffer->internal = 1;
    device->backbuffer.index = textureIndex;
    device->backbuffer.generation = textureGeneration;

    [layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
    [layer setFramebufferOnly:YES];
    OS_WindowInfo windowInfo = OS_window_get_info(desc->window);
    gfx_device_resize(device, windowInfo.drawableWidth, windowInfo.drawableHeight);

    *outDevice = device;
    return 1;
}

void gfx_device_destroy(GfxDevice* device) {
    if (!device || !device->alive) {
        return;
    }

    gfx_wait_idle(device);

    if (device->frames) {
        for (U32 i = 0; i < device->framesInFlight; ++i) {
            gfx_metal_release_frame_objects(&device->frames[i]);
        }
    }

    for (U32 i = 0; i < device->buffers.capacity; ++i) {
        if (slot_map_is_occupied(&device->buffers, i)) {
            GfxMetalBuffer* item = (GfxMetalBuffer*)slot_map_item_at(&device->buffers, i);
            if (item && item->buffer) {
                [item->buffer release];
                item->buffer = 0;
            }
        }
    }

    for (U32 i = 0; i < device->textures.capacity; ++i) {
        if (slot_map_is_occupied(&device->textures, i)) {
            GfxMetalTexture* item = (GfxMetalTexture*)slot_map_item_at(&device->textures, i);
            if (item && item->ownsTexture && item->texture) {
                [item->texture release];
                item->texture = 0;
            }
        }
    }

    for (U32 i = 0; i < device->samplers.capacity; ++i) {
        if (slot_map_is_occupied(&device->samplers, i)) {
            GfxMetalSampler* item = (GfxMetalSampler*)slot_map_item_at(&device->samplers, i);
            if (item && item->sampler) {
                [item->sampler release];
                item->sampler = 0;
            }
        }
    }

    for (U32 i = 0; i < device->pipelines.capacity; ++i) {
        if (slot_map_is_occupied(&device->pipelines, i)) {
            GfxMetalPipeline* item = (GfxMetalPipeline*)slot_map_item_at(&device->pipelines, i);
            if (item && item->graphicsPipeline) {
                [item->graphicsPipeline release];
                item->graphicsPipeline = 0;
            }
            if (item && item->computePipeline) {
                [item->computePipeline release];
                item->computePipeline = 0;
            }
            if (item && item->depthState) {
                [item->depthState release];
                item->depthState = 0;
            }
        }
    }

    if (device->resourceArgumentBuffer) {
        [device->resourceArgumentBuffer release];
        device->resourceArgumentBuffer = 0;
    }
    if (device->resourceArgumentEncoder) {
        [device->resourceArgumentEncoder release];
        device->resourceArgumentEncoder = 0;
    }
    if (device->commandQueue) {
        [device->commandQueue release];
        device->commandQueue = 0;
    }
    if (device->metalDevice) {
        [device->metalDevice release];
        device->metalDevice = 0;
    }
    if (device->metalLayer) {
        [device->metalLayer release];
        device->metalLayer = 0;
    }
    if (device->frameSemaphore) {
        dispatch_release(device->frameSemaphore);
        device->frameSemaphore = 0;
    }

    device->alive = 0;
}

void gfx_device_resize(GfxDevice* device, U32 width, U32 height) {
    if (!device || !device->metalLayer) {
        return;
    }
    if (width == 0u) {
        width = 1u;
    }
    if (height == 0u) {
        height = 1u;
    }

    CGSize drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);
    [device->metalLayer setDrawableSize:drawableSize];
}

void gfx_wait_idle(GfxDevice* device) {
    if (!device || !device->frameSemaphore || device->framesInFlight == 0u) {
        return;
    }

    for (U32 i = 0; i < device->framesInFlight; ++i) {
        dispatch_semaphore_wait(device->frameSemaphore, DISPATCH_TIME_FOREVER);
    }
    for (U32 i = 0; i < device->framesInFlight; ++i) {
        dispatch_semaphore_signal(device->frameSemaphore);
    }
    device->completedFrameSerial = device->frameSerial;
    gfx_metal_drain_retired(device);
}

GfxBuffer gfx_create_buffer(GfxDevice* device, const GfxBufferDesc* desc) {
    return gfx_metal_create_buffer_internal(device, desc, 0);
}

GfxTexture gfx_create_texture(GfxDevice* device, const GfxTextureDesc* desc) {
    if (!device || !desc || desc->width == 0u || desc->height == 0u || desc->format == GfxFormat_Invalid) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid texture descriptor");
        }
        return {};
    }

    GfxTextureDescValidation descValidation = gfx_validate_texture_desc_storage(desc);
    if (!descValidation.storageKindValid ||
        !descValidation.transientAttachmentOnly ||
        !descValidation.transientSingleMip) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid texture storage descriptor");
        }
        return {};
    }

    MTLPixelFormat pixelFormat = gfx_metal_pixel_format(desc->format);
    if (pixelFormat == MTLPixelFormatInvalid) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Unsupported texture format {}", (U32)desc->format);
        }
        return {};
    }

    MTLTextureDescriptor* textureDesc = [[MTLTextureDescriptor alloc] init];
    textureDesc.textureType = MTLTextureType2D;
    textureDesc.pixelFormat = pixelFormat;
    textureDesc.width = desc->width;
    textureDesc.height = desc->height;
    textureDesc.mipmapLevelCount = desc->mipCount ? desc->mipCount : 1u;
    textureDesc.storageMode = (gfx_texture_is_transient_storage_kind(desc->storageKind)) ?
        MTLStorageModeMemoryless :
        MTLStorageModePrivate;
    textureDesc.usage = MTLTextureUsageUnknown;

    if (FLAGS_HAS(desc->usageFlags, GfxTextureUsageFlags_ColorTarget) ||
        FLAGS_HAS(desc->usageFlags, GfxTextureUsageFlags_DepthTarget)) {
        textureDesc.usage |= MTLTextureUsageRenderTarget;
    }
    if (FLAGS_HAS(desc->usageFlags, GfxTextureUsageFlags_Sampled)) {
        textureDesc.usage |= MTLTextureUsageShaderRead;
    }
    if (FLAGS_HAS(desc->usageFlags, GfxTextureUsageFlags_Storage)) {
        textureDesc.usage |= MTLTextureUsageShaderWrite;
    }

    id<MTLTexture> texture = [device->metalDevice newTextureWithDescriptor:textureDesc];
    [textureDesc release];
    if (!texture) {
        LOG_ERROR("gfx", "Metal texture creation failed");
        return {};
    }

    if (gfx_metal_gpu_markers_enabled(device) && desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            [texture setLabel:label];
            [label release];
        }
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->textures, &slotItem, &slotIndex, &generation)) {
        [texture release];
        return {};
    }

    GfxMetalTexture* item = (GfxMetalTexture*)slotItem;
    item->texture = texture;
    item->width = desc->width;
    item->height = desc->height;
    item->mipCount = desc->mipCount ? desc->mipCount : 1u;
    item->format = desc->format;
    item->storageKind = desc->storageKind;
    item->usageFlags = desc->usageFlags;
    item->resourceId = {};
    item->ownsTexture = 1;

    GfxTexture result = {slotIndex, generation};
    return result;
}

GfxSampler gfx_create_sampler(GfxDevice* device, const GfxSamplerDesc* desc) {
    if (!device || !desc) {
        return {};
    }

    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = gfx_metal_filter(desc->minFilter);
    samplerDesc.magFilter = gfx_metal_filter(desc->magFilter);
    samplerDesc.sAddressMode = gfx_metal_address_mode(desc->addressU);
    samplerDesc.tAddressMode = gfx_metal_address_mode(desc->addressV);

    if (gfx_metal_gpu_markers_enabled(device) && desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            samplerDesc.label = label;
            [label release];
        }
    }

    id<MTLSamplerState> sampler = [device->metalDevice newSamplerStateWithDescriptor:samplerDesc];
    [samplerDesc release];
    if (!sampler) {
        return {};
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->samplers, &slotItem, &slotIndex, &generation)) {
        [sampler release];
        return {};
    }

    GfxMetalSampler* item = (GfxMetalSampler*)slotItem;
    item->sampler = sampler;
    item->resourceId = {};

    GfxSampler result = {slotIndex, generation};
    return result;
}

GfxPipeline gfx_create_graphics_pipeline(GfxDevice* device, const GfxGraphicsPipelineDesc* desc) {
    if (!device || !desc) {
        return {};
    }
    if (desc->vertexShader.format != GfxShaderFormat_MSL_Source ||
        desc->fragmentShader.format != GfxShaderFormat_MSL_Source) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Metal v1 only supports MSL source shaders");
        }
        return {};
    }
    if (!desc->colorFormats ||
        !desc->blendStates ||
        desc->colorFormatCount == 0u ||
        desc->colorFormatCount > GFX_MAX_COLOR_TARGETS ||
        desc->blendStateCount != desc->colorFormatCount) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid graphics pipeline color/blend formats");
        }
        return {};
    }

    NSError* error = nil;
    NSString* vertexSource = gfx_nsstring_from_bytes(desc->vertexShader.data, desc->vertexShader.size);
    NSString* fragmentSource = gfx_nsstring_from_bytes(desc->fragmentShader.data, desc->fragmentShader.size);
    if (!vertexSource || !fragmentSource) {
        if (vertexSource) {
            [vertexSource release];
        }
        if (fragmentSource) {
            [fragmentSource release];
        }
        return {};
    }

    id<MTLLibrary> vertexLibrary = [device->metalDevice newLibraryWithSource:vertexSource options:nil error:&error];
    [vertexSource release];
    if (!vertexLibrary) {
        LOG_ERROR("gfx", "Vertex shader compile failed: {}",
                  str8(error ? [[error localizedDescription] UTF8String] : "<unknown>"));
        [fragmentSource release];
        return {};
    }

    error = nil;
    id<MTLLibrary> fragmentLibrary = [device->metalDevice newLibraryWithSource:fragmentSource options:nil error:&error];
    [fragmentSource release];
    if (!fragmentLibrary) {
        LOG_ERROR("gfx", "Fragment shader compile failed: {}",
                  str8(error ? [[error localizedDescription] UTF8String] : "<unknown>"));
        [vertexLibrary release];
        return {};
    }

    NSString* vertexEntry = gfx_nsstring_from_cstr(desc->vertexShader.entry);
    NSString* fragmentEntry = gfx_nsstring_from_cstr(desc->fragmentShader.entry);
    id<MTLFunction> vertexFunction = vertexEntry ? [vertexLibrary newFunctionWithName:vertexEntry] : 0;
    id<MTLFunction> fragmentFunction = fragmentEntry ? [fragmentLibrary newFunctionWithName:fragmentEntry] : 0;
    if (vertexEntry) {
        [vertexEntry release];
    }
    if (fragmentEntry) {
        [fragmentEntry release];
    }

    if (!vertexFunction || !fragmentFunction) {
        LOG_ERROR("gfx", "Pipeline shader entry point missing");
        if (vertexFunction) {
            [vertexFunction release];
        }
        if (fragmentFunction) {
            [fragmentFunction release];
        }
        [vertexLibrary release];
        [fragmentLibrary release];
        return {};
    }

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vertexFunction;
    pipelineDesc.fragmentFunction = fragmentFunction;

    if (gfx_metal_gpu_markers_enabled(device) && desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            pipelineDesc.label = label;
            [label release];
        }
    }

    for (U32 i = 0; i < desc->colorFormatCount; ++i) {
        const GfxColorBlendState* blendState = desc->blendStates + i;
        pipelineDesc.colorAttachments[i].pixelFormat = gfx_metal_pixel_format(desc->colorFormats[i]);
        pipelineDesc.colorAttachments[i].blendingEnabled = blendState->blendEnabled ? YES : NO;
        pipelineDesc.colorAttachments[i].sourceRGBBlendFactor = gfx_metal_blend_factor(blendState->srcColorFactor);
        pipelineDesc.colorAttachments[i].destinationRGBBlendFactor = gfx_metal_blend_factor(blendState->dstColorFactor);
        pipelineDesc.colorAttachments[i].rgbBlendOperation = gfx_metal_blend_op(blendState->colorOp);
        pipelineDesc.colorAttachments[i].sourceAlphaBlendFactor = gfx_metal_blend_factor(blendState->srcAlphaFactor);
        pipelineDesc.colorAttachments[i].destinationAlphaBlendFactor = gfx_metal_blend_factor(blendState->dstAlphaFactor);
        pipelineDesc.colorAttachments[i].alphaBlendOperation = gfx_metal_blend_op(blendState->alphaOp);
        pipelineDesc.colorAttachments[i].writeMask = gfx_metal_color_write_mask(blendState->writeFlags);
    }
    if (desc->depthFormat != GfxFormat_Invalid) {
        pipelineDesc.depthAttachmentPixelFormat = gfx_metal_pixel_format(desc->depthFormat);
    }

    error = nil;
    id<MTLRenderPipelineState> pipelineState = [device->metalDevice newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                                                    error:&error];
    [pipelineDesc release];
    [vertexFunction release];
    [fragmentFunction release];
    [vertexLibrary release];
    [fragmentLibrary release];

    if (!pipelineState) {
        LOG_ERROR("gfx", "Render pipeline creation failed: {}",
                  str8(error ? [[error localizedDescription] UTF8String] : "<unknown>"));
        return {};
    }

    id<MTLDepthStencilState> depthState = 0;
    if (desc->depth.depthTestEnabled || desc->depth.depthWriteEnabled) {
        MTLDepthStencilDescriptor* depthDesc = [[MTLDepthStencilDescriptor alloc] init];
        depthDesc.depthCompareFunction = gfx_metal_compare_op(desc->depth.compareOp);
        depthDesc.depthWriteEnabled = desc->depth.depthWriteEnabled ? YES : NO;
        depthState = [device->metalDevice newDepthStencilStateWithDescriptor:depthDesc];
        [depthDesc release];
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->pipelines, &slotItem, &slotIndex, &generation)) {
        [pipelineState release];
        if (depthState) {
            [depthState release];
        }
        return {};
    }

    GfxMetalPipeline* item = (GfxMetalPipeline*)slotItem;
    item->kind = GfxPipelineKind_Graphics;
    item->graphicsPipeline = pipelineState;
    item->computePipeline = 0;
    item->depthState = depthState;
    item->raster = desc->raster;
    item->threadsPerThreadgroupX = 0u;
    item->threadsPerThreadgroupY = 0u;
    item->threadsPerThreadgroupZ = 0u;

    GfxPipeline result = {slotIndex, generation};
    return result;
}

GfxPipeline gfx_create_compute_pipeline(GfxDevice* device, const GfxComputePipelineDesc* desc) {
    if (!device || !desc ||
        desc->shader.format != GfxShaderFormat_MSL_Source ||
        desc->threadsPerThreadgroupX == 0u ||
        desc->threadsPerThreadgroupY == 0u ||
        desc->threadsPerThreadgroupZ == 0u) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid compute pipeline descriptor");
        }
        return {};
    }

    NSError* error = nil;
    NSString* source = gfx_nsstring_from_bytes(desc->shader.data, desc->shader.size);
    if (!source) {
        return {};
    }

    id<MTLLibrary> library = [device->metalDevice newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (!library) {
        LOG_ERROR("gfx", "Compute shader compile failed: {}",
                  str8(error ? [[error localizedDescription] UTF8String] : "<unknown>"));
        return {};
    }

    NSString* entry = gfx_nsstring_from_cstr(desc->shader.entry);
    id<MTLFunction> function = entry ? [library newFunctionWithName:entry] : 0;
    if (entry) {
        [entry release];
    }
    if (!function) {
        LOG_ERROR("gfx", "Compute shader entry point missing");
        [library release];
        return {};
    }

    error = nil;
    id<MTLComputePipelineState> pipelineState = [device->metalDevice newComputePipelineStateWithFunction:function
                                                                                                    error:&error];
    [function release];
    [library release];
    if (!pipelineState) {
        LOG_ERROR("gfx", "Compute pipeline creation failed: {}",
                  str8(error ? [[error localizedDescription] UTF8String] : "<unknown>"));
        return {};
    }

    void* slotItem = 0;
    U32 slotIndex = 0u;
    U32 generation = 0u;
    if (!slot_map_alloc(&device->pipelines, &slotItem, &slotIndex, &generation)) {
        [pipelineState release];
        return {};
    }

    GfxMetalPipeline* item = (GfxMetalPipeline*)slotItem;
    item->kind = GfxPipelineKind_Compute;
    item->graphicsPipeline = 0;
    item->computePipeline = pipelineState;
    item->depthState = 0;
    item->raster = {};
    item->threadsPerThreadgroupX = desc->threadsPerThreadgroupX;
    item->threadsPerThreadgroupY = desc->threadsPerThreadgroupY;
    item->threadsPerThreadgroupZ = desc->threadsPerThreadgroupZ;

    GfxPipeline result = {slotIndex, generation};
    return result;
}

void gfx_destroy_buffer(GfxDevice* device, GfxBuffer buffer) {
    if (!device) {
        return;
    }
    GfxMetalBuffer* item = gfx_metal_resolve_buffer(device, buffer);
    if (!item->buffer || item->internal) {
        return;
    }
    gfx_metal_retire_buffer(device, item);
    void* released = 0;
    slot_map_release(&device->buffers, buffer.index, buffer.generation, &released);
}

void gfx_destroy_texture(GfxDevice* device, GfxTexture texture) {
    if (!device) {
        return;
    }
    GfxMetalTexture* item = gfx_metal_resolve_texture(device, texture);
    if (!item->texture || item->internal) {
        return;
    }
    gfx_metal_retire_texture(device, item);
    void* released = 0;
    slot_map_release(&device->textures, texture.index, texture.generation, &released);
}

void gfx_destroy_sampler(GfxDevice* device, GfxSampler sampler) {
    if (!device) {
        return;
    }
    GfxMetalSampler* item = gfx_metal_resolve_sampler(device, sampler);
    if (!item->sampler) {
        return;
    }
    gfx_metal_retire_sampler(device, item);
    void* released = 0;
    slot_map_release(&device->samplers, sampler.index, sampler.generation, &released);
}

void gfx_destroy_pipeline(GfxDevice* device, GfxPipeline pipeline) {
    if (!device) {
        return;
    }
    GfxMetalPipeline* item = gfx_metal_resolve_pipeline(device, pipeline);
    if (!item->graphicsPipeline && !item->computePipeline && !item->depthState) {
        return;
    }
    gfx_metal_retire_pipeline(device, item);
    void* released = 0;
    slot_map_release(&device->pipelines, pipeline.index, pipeline.generation, &released);
}

GfxResourceId gfx_register_buffer(GfxDevice* device, GfxBuffer buffer) {
    if (!device) {
        return {};
    }
    GfxMetalBuffer* item = gfx_metal_resolve_buffer(device, buffer);
    if (!item->buffer) {
        return {};
    }
    U32 shaderVisibleFlags = GfxBufferUsageFlags_Uniform | GfxBufferUsageFlags_Storage;
    if ((item->usageFlags & shaderVisibleFlags) == 0u) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "gfx_register_buffer requires uniform or storage buffer");
        }
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_metal_register_resource(device, GfxResourceKind_Buffer, buffer.index, buffer.generation);
    gfx_metal_resource_table_set_buffer(device, item->resourceId, item->buffer);
    return item->resourceId;
}

GfxResourceId gfx_register_texture(GfxDevice* device, GfxTexture texture) {
    if (!device) {
        return {};
    }
    GfxMetalTexture* item = gfx_metal_resolve_texture(device, texture);
    if (!item->texture) {
        return {};
    }
    if (gfx_texture_is_transient_storage_kind(item->storageKind) ||
        !FLAGS_HAS(item->usageFlags, GfxTextureUsageFlags_Sampled)) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "gfx_register_texture requires sampled device texture");
        }
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_metal_register_resource(device, GfxResourceKind_Texture, texture.index, texture.generation);
    gfx_metal_resource_table_set_texture(device, item->resourceId, item->texture);
    return item->resourceId;
}

GfxResourceId gfx_register_sampler(GfxDevice* device, GfxSampler sampler) {
    if (!device) {
        return {};
    }
    GfxMetalSampler* item = gfx_metal_resolve_sampler(device, sampler);
    if (!item->sampler) {
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_metal_register_resource(device, GfxResourceKind_Sampler, sampler.index, sampler.generation);
    gfx_metal_resource_table_set_sampler(device, item->resourceId, item->sampler);
    return item->resourceId;
}

GfxFrame* gfx_begin_frame(GfxDevice* device) {
    if (!device || !device->alive) {
        return 0;
    }

    dispatch_semaphore_wait(device->frameSemaphore, DISPATCH_TIME_FOREVER);

    GfxFrame* frame = &device->frames[device->frameCursor];
    device->frameCursor = (device->frameCursor + 1u) % device->framesInFlight;

    if (frame->submittedSerial > device->completedFrameSerial) {
        device->completedFrameSerial = frame->submittedSerial;
        gfx_metal_drain_retired(device);
    }

    gfx_metal_release_frame_objects(frame);

    id<CAMetalDrawable> drawable = [device->metalLayer nextDrawable];
    if (!drawable) {
        LOG_ERROR("gfx", "Metal layer did not provide a drawable");
        dispatch_semaphore_signal(device->frameSemaphore);
        return 0;
    }

    frame->drawable = [drawable retain];
    if (gfx_metal_backend_validation_enabled(device)) {
        MTLCommandBufferDescriptor* commandBufferDesc = [[MTLCommandBufferDescriptor alloc] init];
        commandBufferDesc.errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus;
        frame->commandBuffer = [[device->commandQueue commandBufferWithDescriptor:commandBufferDesc] retain];
        [commandBufferDesc release];
    } else {
        frame->commandBuffer = [[device->commandQueue commandBuffer] retain];
    }
    if (!frame->commandBuffer) {
        gfx_metal_release_frame_objects(frame);
        dispatch_semaphore_signal(device->frameSemaphore);
        return 0;
    }

    id<MTLTexture> drawableTexture = [drawable texture];
    GfxMetalTexture* backbuffer = gfx_metal_resolve_texture(device, device->backbuffer);
    if (backbuffer) {
        backbuffer->texture = drawableTexture;
        backbuffer->width = (U32)[drawableTexture width];
        backbuffer->height = (U32)[drawableTexture height];
        backbuffer->mipCount = 1u;
        backbuffer->format = GfxFormat_BGRA8_UNorm;
        backbuffer->storageKind = GfxTextureStorageKind_Device;
        backbuffer->usageFlags = GfxTextureUsageFlags_ColorTarget;
    }

    frame->tempPos = 0u;
    frame->active = 1;
    frame->submitted = 0;
    device->activeFrame = frame;
    device->frameSerial += 1u;
    device->stats.drawCount = 0u;
    device->stats.dispatchCount = 0u;
    device->stats.pipelineSwitchCount = 0u;
    device->stats.tempOverflowCount = 0u;
    device->stats.tempBytesUsed = 0u;
    device->stats.resourceTableCount = device->resourceTable.liveCount;
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
        if (gfx_metal_api_validation_enabled(frame->device)) {
            LOG_ERROR("gfx", "Temp allocation alignment is not a power of two ({})", alignment);
        }
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

    GfxMetalBuffer* buffer = gfx_metal_resolve_buffer(frame->device, dst);
    if (!buffer->buffer) {
        return 0;
    }
    if (dstOffset > buffer->size || size > (buffer->size - dstOffset)) {
        gfx_metal_api_assert(frame->device, dstOffset <= buffer->size && size <= (buffer->size - dstOffset));
        if (gfx_metal_api_validation_enabled(frame->device)) {
            LOG_ERROR("gfx", "gfx_upload_buffer out of bounds");
        }
        return 0;
    }

    if (buffer->memoryKind == GfxMemoryKind_Upload) {
        void* cpu = [buffer->buffer contents];
        ASSERT_DEBUG(cpu != 0);
        if (!cpu) {
            return 0;
        }

        MEMCPY((U8*)cpu + dstOffset, src, size);
        return 1;
    }

    ASSERT_DEBUG(frame->commandBuffer != 0);
    if (!frame->commandBuffer) {
        return 0;
    }

    GfxTemp temp = gfx_allocate_temp(frame, size, 16u);
    ASSERT_DEBUG(temp.cpu != 0 && "gfx_upload_buffer ran out of frame upload memory");
    if (!temp.cpu) {
        LOG_ERROR("gfx", "gfx_upload_buffer ran out of frame upload memory (size={})", size);
        return 0;
    }

    MEMCPY(temp.cpu, src, size);
    GfxMetalBuffer* uploadBuffer = gfx_metal_resolve_buffer(frame->device, temp.gpu.buffer);
    ASSERT_DEBUG(uploadBuffer->buffer != 0);
    if (!uploadBuffer->buffer) {
        return 0;
    }

    id<MTLBlitCommandEncoder> blit = [[frame->commandBuffer blitCommandEncoder] retain];
    ASSERT_DEBUG(blit != 0);
    if (!blit) {
        return 0;
    }

    [blit copyFromBuffer:uploadBuffer->buffer
            sourceOffset:(NSUInteger)temp.gpu.offset
                toBuffer:buffer->buffer
       destinationOffset:(NSUInteger)dstOffset
                    size:(NSUInteger)size];
    [blit endEncoding];
    [blit release];
    return 1;
}

B32 gfx_upload_texture(GfxFrame* frame, GfxTexture dst, const GfxTextureUploadRegion* region, const void* src) {
    if (!frame || !frame->device || !region || !src) {
        return 0;
    }

    GfxMetalTexture* texture = gfx_metal_resolve_texture(frame->device, dst);
    if (!texture->texture) {
        return 0;
    }
    if (gfx_texture_is_transient_storage_kind(texture->storageKind) ||
        !FLAGS_HAS(texture->usageFlags, GfxTextureUsageFlags_CopyDst)) {
        if (gfx_metal_api_validation_enabled(frame->device)) {
            LOG_ERROR("gfx", "gfx_upload_texture requires copy-dst device texture");
        }
        return 0;
    }

    GfxTextureUploadValidation validation = {};
    B32 validRegion = gfx_validate_texture_upload_region(texture->format,
                                                         texture->width,
                                                         texture->height,
                                                         texture->mipCount,
                                                         region,
                                                         &validation);
    gfx_metal_api_assert(frame->device, validation.supported);
    gfx_metal_api_assert(frame->device, validation.inBounds);
    gfx_metal_api_assert(frame->device, validation.rowLayout);
    gfx_metal_api_assert(frame->device, validation.sizeValid);
    if (!validRegion) {
        if (gfx_metal_api_validation_enabled(frame->device)) {
            LOG_ERROR("gfx", "gfx_upload_texture invalid upload region");
        }
        return 0;
    }

    U64 sourceBytesPerImage = validation.sourceBytesPerImage;

    ASSERT_DEBUG(frame->commandBuffer != 0);
    if (!frame->commandBuffer) {
        return 0;
    }

    GfxTemp temp = gfx_allocate_temp(frame, sourceBytesPerImage, GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT);
    ASSERT_DEBUG(temp.cpu != 0 && "gfx_upload_texture ran out of frame upload memory");
    if (!temp.cpu) {
        LOG_ERROR("gfx", "gfx_upload_texture ran out of frame upload memory (size={})", sourceBytesPerImage);
        return 0;
    }

    MEMCPY(temp.cpu, src, sourceBytesPerImage);
    GfxMetalBuffer* uploadBuffer = gfx_metal_resolve_buffer(frame->device, temp.gpu.buffer);
    ASSERT_DEBUG(uploadBuffer->buffer != 0);
    if (!uploadBuffer->buffer) {
        return 0;
    }

    id<MTLBlitCommandEncoder> blit = [[frame->commandBuffer blitCommandEncoder] retain];
    ASSERT_DEBUG(blit != 0);
    if (!blit) {
        return 0;
    }

    MTLOrigin origin = MTLOriginMake((NSUInteger)region->x,
                                    (NSUInteger)region->y,
                                    (NSUInteger)region->z);
    MTLSize size = MTLSizeMake((NSUInteger)region->width,
                               (NSUInteger)region->height,
                               (NSUInteger)region->depth);
    [blit copyFromBuffer:uploadBuffer->buffer
            sourceOffset:(NSUInteger)temp.gpu.offset
       sourceBytesPerRow:(NSUInteger)region->bytesPerRow
     sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
              sourceSize:size
               toTexture:texture->texture
        destinationSlice:(NSUInteger)region->layer
        destinationLevel:(NSUInteger)region->mip
       destinationOrigin:origin];
    [blit endEncoding];
    [blit release];
    return 1;
}

void gfx_render_pass(GfxCommandBuffer* commands, const GfxRenderPassDesc* desc, const GfxDrawArea* areas, U32 areaCount) {
    if (!commands || !commands->frame || !desc) {
        return;
    }

    GfxFrame* frame = commands->frame;
    GfxDevice* device = frame->device;
    if (!frame->active || !frame->commandBuffer) {
        return;
    }

    gfx_metal_api_assert(device, desc->colorTargetCount > 0u);
    gfx_metal_api_assert(device, desc->colorTargetCount <= GFX_MAX_COLOR_TARGETS);
    gfx_metal_api_assert(device, desc->colorTargetCount == 0u || desc->colorTargets != 0);
    gfx_metal_api_assert(device, areaCount == 0u || areas != 0);
    gfx_metal_api_assert(device, desc->resourceUseCount == 0u || desc->resourceUses != 0);
    if (desc->colorTargetCount == 0u ||
        desc->colorTargetCount > GFX_MAX_COLOR_TARGETS ||
        !desc->colorTargets ||
        (areaCount != 0u && !areas) ||
        (desc->resourceUseCount != 0u && !desc->resourceUses)) {
        if (gfx_metal_api_validation_enabled(device)) {
            LOG_ERROR("gfx", "Invalid render pass descriptor");
        }
        return;
    }

    MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];
    if (!passDesc) {
        return;
    }
    U32 passWidth = 0u;
    U32 passHeight = 0u;
    for (U32 i = 0; i < desc->colorTargetCount; ++i) {
        const GfxColorTarget* target = &desc->colorTargets[i];
        GfxMetalTexture* texture = gfx_metal_resolve_texture(device, target->texture);
        if (!texture->texture) {
            [passDesc release];
            return;
        }
        if (passWidth == 0u || passHeight == 0u) {
            passWidth = texture->width;
            passHeight = texture->height;
        }
        B32 targetMatchesExtent = (texture->width == passWidth && texture->height == passHeight) ? 1 : 0;
        B32 transientRulesOk = 1;
        if (gfx_texture_is_transient_storage_kind(texture->storageKind)) {
            transientRulesOk = (target->loadOp != GfxLoadOp_Load &&
                                target->storeOp == GfxStoreOp_DontCare) ? 1 : 0;
        }
        gfx_metal_api_assert(device, targetMatchesExtent);
        gfx_metal_api_assert(device, FLAGS_HAS(texture->usageFlags, GfxTextureUsageFlags_ColorTarget));
        gfx_metal_api_assert(device, transientRulesOk);
        if (!targetMatchesExtent ||
            !FLAGS_HAS(texture->usageFlags, GfxTextureUsageFlags_ColorTarget) ||
            !transientRulesOk) {
            if (gfx_metal_api_validation_enabled(device)) {
                LOG_ERROR("gfx", "Invalid color attachment");
            }
            [passDesc release];
            return;
        }
        passDesc.colorAttachments[i].texture = texture->texture;
        passDesc.colorAttachments[i].loadAction = gfx_metal_load_action(target->loadOp);
        passDesc.colorAttachments[i].storeAction = gfx_metal_store_action(target->storeOp);
        passDesc.colorAttachments[i].clearColor = MTLClearColorMake(target->clearColor[0],
                                                                    target->clearColor[1],
                                                                    target->clearColor[2],
                                                                    target->clearColor[3]);
    }

    if (desc->depthTarget) {
        GfxMetalTexture* depthTexture = gfx_metal_resolve_texture(device, desc->depthTarget->texture);
        if (!depthTexture->texture) {
            [passDesc release];
            return;
        }
        if (passWidth == 0u || passHeight == 0u) {
            passWidth = depthTexture->width;
            passHeight = depthTexture->height;
        }
        B32 targetMatchesExtent = (depthTexture->width == passWidth && depthTexture->height == passHeight) ? 1 : 0;
        B32 transientRulesOk = 1;
        if (gfx_texture_is_transient_storage_kind(depthTexture->storageKind)) {
            transientRulesOk = (desc->depthTarget->loadOp != GfxLoadOp_Load &&
                                desc->depthTarget->storeOp == GfxStoreOp_DontCare) ? 1 : 0;
        }
        gfx_metal_api_assert(device, targetMatchesExtent);
        gfx_metal_api_assert(device, FLAGS_HAS(depthTexture->usageFlags, GfxTextureUsageFlags_DepthTarget));
        gfx_metal_api_assert(device, transientRulesOk);
        if (!targetMatchesExtent ||
            !FLAGS_HAS(depthTexture->usageFlags, GfxTextureUsageFlags_DepthTarget) ||
            !transientRulesOk) {
            if (gfx_metal_api_validation_enabled(device)) {
                LOG_ERROR("gfx", "Invalid depth attachment");
            }
            [passDesc release];
            return;
        }
        passDesc.depthAttachment.texture = depthTexture->texture;
        passDesc.depthAttachment.loadAction = gfx_metal_load_action(desc->depthTarget->loadOp);
        passDesc.depthAttachment.storeAction = gfx_metal_store_action(desc->depthTarget->storeOp);
        passDesc.depthAttachment.clearDepth = desc->depthTarget->clearDepth;
    }

    id<MTLRenderCommandEncoder> encoder = [[frame->commandBuffer renderCommandEncoderWithDescriptor:passDesc] retain];
    [passDesc release];
    if (!encoder) {
        LOG_ERROR("gfx", "Failed to create Metal render encoder");
        return;
    }

    B32 pushedDebugGroup = 0;
    if (gfx_metal_gpu_markers_enabled(device) && desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            [encoder setLabel:label];
            [encoder pushDebugGroup:label];
            pushedDebugGroup = 1;
            [label release];
        }
    }

    if (device->resourceArgumentBuffer) {
        [encoder setVertexBuffer:device->resourceArgumentBuffer
                           offset:0u
                          atIndex:GFX_SHADER_SLOT_RESOURCE_TABLE];
        [encoder setFragmentBuffer:device->resourceArgumentBuffer
                             offset:0u
                            atIndex:GFX_SHADER_SLOT_RESOURCE_TABLE];

        for (U32 resourceIndex = 0u; resourceIndex < desc->resourceUseCount; ++resourceIndex) {
            gfx_metal_render_use_resource(device, encoder, desc->resourceUses + resourceIndex);
        }
    }

    GfxPipeline boundPipeline = {};

    for (U32 areaIndex = 0; areaIndex < areaCount; ++areaIndex) {
        const GfxDrawArea* area = &areas[areaIndex];
        gfx_metal_api_assert(device, area->drawCount == 0u || area->draws != 0);
        if (area->drawCount != 0u && !area->draws) {
            continue;
        }

        MTLViewport viewport = {};
        viewport.originX = area->viewport.x;
        viewport.originY = area->viewport.y;
        viewport.width = area->viewport.width;
        viewport.height = area->viewport.height;
        viewport.znear = area->viewport.minDepth;
        viewport.zfar = area->viewport.maxDepth;
        [encoder setViewport:viewport];

        MTLScissorRect scissor = {};
        scissor.x = (NSUInteger)area->scissor.x;
        scissor.y = (NSUInteger)area->scissor.y;
        scissor.width = area->scissor.width;
        scissor.height = area->scissor.height;
        [encoder setScissorRect:scissor];

        for (U32 drawIndex = 0; drawIndex < area->drawCount; ++drawIndex) {
            const GfxDraw* draw = &area->draws[drawIndex];
            if (draw->kind != GfxDrawKind_DirectIndexed &&
                draw->kind != GfxDrawKind_IndirectIndexed) {
                gfx_metal_api_assert(device, 0 && "Invalid gfx draw kind");
                continue;
            }
            if (draw->kind == GfxDrawKind_DirectIndexed &&
                (draw->indexCount == 0u || draw->instanceCount == 0u)) {
                continue;
            }
            if (draw->kind == GfxDrawKind_IndirectIndexed &&
                draw->indirectArgs.size < sizeof(GfxDrawIndexedIndirectArgs)) {
                gfx_metal_api_assert(device, draw->indirectArgs.size >= sizeof(GfxDrawIndexedIndirectArgs));
                continue;
            }

            GfxMetalPipeline* pipeline = gfx_metal_resolve_pipeline(device, draw->pipeline);
            GfxMetalBuffer* indexBuffer = gfx_metal_resolve_buffer(device, draw->indexBuffer);
            GfxMetalBuffer* rootDataBuffer = gfx_metal_resolve_buffer(device, draw->rootData.buffer);
            GfxMetalBuffer* indirectBuffer = (draw->kind == GfxDrawKind_IndirectIndexed) ?
                                             gfx_metal_resolve_buffer(device, draw->indirectArgs.buffer) :
                                             &g_gfxMetalNilBuffer;

            gfx_metal_api_assert(device, pipeline->kind == GfxPipelineKind_Graphics || pipeline->graphicsPipeline == 0);
            if (pipeline->kind != GfxPipelineKind_Graphics ||
                !pipeline->graphicsPipeline ||
                !indexBuffer->buffer ||
                !rootDataBuffer->buffer ||
                (draw->kind == GfxDrawKind_IndirectIndexed && !indirectBuffer->buffer)) {
                continue;
            }
            if (draw->kind == GfxDrawKind_IndirectIndexed &&
                !FLAGS_HAS(indirectBuffer->usageFlags, GfxBufferUsageFlags_Indirect)) {
                gfx_metal_api_assert(device, FLAGS_HAS(indirectBuffer->usageFlags, GfxBufferUsageFlags_Indirect));
                continue;
            }

            if (!gfx_pipeline_handle_equal(boundPipeline, draw->pipeline)) {
                [encoder setRenderPipelineState:pipeline->graphicsPipeline];
                [encoder setCullMode:gfx_metal_cull_mode(pipeline->raster.cullMode)];
                [encoder setFrontFacingWinding:gfx_metal_front_face(pipeline->raster.frontFace)];
                if (pipeline->depthState) {
                    [encoder setDepthStencilState:pipeline->depthState];
                }
                boundPipeline = draw->pipeline;
                device->stats.pipelineSwitchCount += 1u;
            }

            [encoder setVertexBuffer:rootDataBuffer->buffer
                               offset:(NSUInteger)draw->rootData.offset
                              atIndex:GFX_SHADER_SLOT_ROOT_DATA];
            [encoder setFragmentBuffer:rootDataBuffer->buffer
                                 offset:(NSUInteger)draw->rootData.offset
                                atIndex:GFX_SHADER_SLOT_ROOT_DATA];

            if (draw->kind == GfxDrawKind_IndirectIndexed) {
                [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                     indexType:gfx_metal_index_type(draw->indexType)
                                   indexBuffer:indexBuffer->buffer
                             indexBufferOffset:(NSUInteger)draw->indexByteOffset
                                indirectBuffer:indirectBuffer->buffer
                          indirectBufferOffset:(NSUInteger)draw->indirectArgs.offset];
            } else {
                [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                    indexCount:draw->indexCount
                                     indexType:gfx_metal_index_type(draw->indexType)
                                   indexBuffer:indexBuffer->buffer
                             indexBufferOffset:(NSUInteger)draw->indexByteOffset
                                 instanceCount:draw->instanceCount
                                    baseVertex:draw->baseVertex
                                  baseInstance:draw->firstInstance];
            }
            device->stats.drawCount += 1u;
        }
    }

    if (pushedDebugGroup) {
        [encoder popDebugGroup];
    }
    [encoder endEncoding];
    [encoder release];
}

void gfx_compute_pass(GfxCommandBuffer* commands, const GfxComputePassDesc* desc, const GfxDispatch* dispatches, U32 dispatchCount) {
    if (!commands || !commands->frame || !desc) {
        return;
    }

    GfxFrame* frame = commands->frame;
    GfxDevice* device = frame->device;
    if (!frame->active || !frame->commandBuffer) {
        return;
    }

    gfx_metal_api_assert(device, dispatchCount == 0u || dispatches != 0);
    gfx_metal_api_assert(device, desc->writeCount == 0u || desc->writes != 0);
    gfx_metal_api_assert(device, desc->resourceUseCount == 0u || desc->resourceUses != 0);
    if ((dispatchCount != 0u && !dispatches) ||
        (desc->writeCount != 0u && !desc->writes) ||
        (desc->resourceUseCount != 0u && !desc->resourceUses)) {
        return;
    }

    id<MTLComputeCommandEncoder> encoder = [[frame->commandBuffer computeCommandEncoder] retain];
    if (!encoder) {
        LOG_ERROR("gfx", "Failed to create Metal compute encoder");
        return;
    }

    B32 pushedDebugGroup = 0;
    if (gfx_metal_gpu_markers_enabled(device) && desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            [encoder setLabel:label];
            [encoder pushDebugGroup:label];
            pushedDebugGroup = 1;
            [label release];
        }
    }

    if (device->resourceArgumentBuffer) {
        [encoder setBuffer:device->resourceArgumentBuffer
                    offset:0u
                   atIndex:GFX_SHADER_SLOT_RESOURCE_TABLE];

        for (U32 resourceIndex = 0u; resourceIndex < desc->resourceUseCount; ++resourceIndex) {
            gfx_metal_compute_use_resource(device, encoder, desc->resourceUses + resourceIndex);
        }
    }

    for (U32 dispatchIndex = 0u; dispatchIndex < dispatchCount; ++dispatchIndex) {
        const GfxDispatch* dispatch = dispatches + dispatchIndex;
        if (dispatch->groupsX == 0u ||
            dispatch->groupsY == 0u ||
            dispatch->groupsZ == 0u) {
            continue;
        }

        GfxMetalPipeline* pipeline = gfx_metal_resolve_pipeline(device, dispatch->pipeline);
        GfxMetalBuffer* rootDataBuffer = gfx_metal_resolve_buffer(device, dispatch->rootData.buffer);

        gfx_metal_api_assert(device, pipeline->kind == GfxPipelineKind_Compute || pipeline->graphicsPipeline == 0);
        if (pipeline->kind != GfxPipelineKind_Compute ||
            !pipeline->computePipeline ||
            !rootDataBuffer->buffer) {
            continue;
        }

        [encoder setComputePipelineState:pipeline->computePipeline];
        [encoder setBuffer:rootDataBuffer->buffer
                    offset:(NSUInteger)dispatch->rootData.offset
                   atIndex:GFX_SHADER_SLOT_ROOT_DATA];

        MTLSize threadgroups = MTLSizeMake((NSUInteger)dispatch->groupsX,
                                           (NSUInteger)dispatch->groupsY,
                                           (NSUInteger)dispatch->groupsZ);
        MTLSize threadsPerThreadgroup = MTLSizeMake((NSUInteger)pipeline->threadsPerThreadgroupX,
                                                    (NSUInteger)pipeline->threadsPerThreadgroupY,
                                                    (NSUInteger)pipeline->threadsPerThreadgroupZ);
        [encoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerThreadgroup];
        device->stats.dispatchCount += 1u;
    }

    if (pushedDebugGroup) {
        [encoder popDebugGroup];
    }
    [encoder endEncoding];
    [encoder release];
}

void gfx_submit(GfxCommandBuffer* commands) {
    if (!commands || !commands->frame) {
        return;
    }

    GfxFrame* frame = commands->frame;
    GfxDevice* device = frame->device;
    if (!device || !frame->active || !frame->commandBuffer) {
        return;
    }

    if (frame->drawable) {
        [frame->commandBuffer presentDrawable:frame->drawable];
    }

    frame->submittedSerial = device->frameSerial;
    dispatch_semaphore_t semaphore = device->frameSemaphore;
    B32 backendValidationEnabled = gfx_metal_backend_validation_enabled(device);
    [frame->commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> commandBuffer) {
        if (backendValidationEnabled && [commandBuffer status] == MTLCommandBufferStatusError) {
            NSError* error = [commandBuffer error];
            LOG_ERROR("gfx", "Metal command buffer failed (status={}): {}",
                      (U32)[commandBuffer status],
                      str8(error ? [[error localizedDescription] UTF8String] : "<unknown>"));
        }
        dispatch_semaphore_signal(semaphore);
    }];

    [frame->commandBuffer commit];
    frame->submitted = 1;
}

void gfx_end_frame(GfxFrame* frame) {
    if (!frame || !frame->active) {
        return;
    }

    if (!frame->submitted && frame->device && frame->device->frameSemaphore) {
        dispatch_semaphore_signal(frame->device->frameSemaphore);
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
