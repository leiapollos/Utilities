#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>

#define GFX_DEFAULT_FRAMES_IN_FLIGHT 2u
#define GFX_DEFAULT_TEMP_BUFFER_SIZE MB(8)
#define GFX_METAL_RESOURCE_TABLE_CAPACITY 256u
#define GFX_METAL_RESOURCE_TABLE_TEXTURE_BASE 0u
#define GFX_METAL_RESOURCE_TABLE_SAMPLER_BASE GFX_METAL_RESOURCE_TABLE_CAPACITY

enum GfxMetalResourceKind {
    GfxMetalResourceKind_Invalid = 0,
    GfxMetalResourceKind_Buffer,
    GfxMetalResourceKind_Texture,
    GfxMetalResourceKind_Sampler,
};

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

struct GfxMetalResourceEntry {
    GfxMetalResourceKind kind;
    U32 index;
    U32 generation;
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
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> commandBuffer;
};

struct GfxDevice {
    GfxBackend backend;
    Arena* arena;
    B32 validationEnabled;
    B32 alive;

    CAMetalLayer* metalLayer;
    id<MTLDevice> metalDevice;
    id<MTLCommandQueue> commandQueue;
    dispatch_semaphore_t frameSemaphore;

    U32 framesInFlight;
    U32 frameCursor;
    U64 frameSerial;
    GfxFrame* frames;
    GfxFrame* activeFrame;

    SlotMap buffers;
    SlotMap textures;
    SlotMap samplers;
    SlotMap pipelines;

    GfxMetalResourceEntry* resourceEntries;
    U32 resourceCount;
    U32 resourceCapacity;
    id<MTLArgumentEncoder> resourceArgumentEncoder;
    id<MTLBuffer> resourceArgumentBuffer;

    GfxTexture backbuffer;
    U32 drawableWidth;
    U32 drawableHeight;

    GfxStats stats;
};

static GfxMetalBuffer g_gfxMetalNilBuffer = {0, 0u, 0u, GfxMemoryKind_Device, {}, 1};
static GfxMetalTexture g_gfxMetalNilTexture = {0, 0u, 0u, 0u, GfxFormat_Invalid, 0u, {}, 0, 1};
static GfxMetalSampler g_gfxMetalNilSampler = {0, {}};
static GfxMetalPipeline g_gfxMetalNilPipeline = {GfxPipelineKind_Graphics, 0, 0, 0, {}, 0u, 0u, 0u};

static NSString* gfx_nsstring_from_cstr(const char* str);
static NSString* gfx_nsstring_from_bytes(const void* data, U64 size);
static MTLPixelFormat gfx_metal_pixel_format(GfxFormat format);
static U32 gfx_metal_color_format_bytes_per_pixel(GfxFormat format);
static MTLLoadAction gfx_metal_load_action(GfxLoadOp op);
static MTLStoreAction gfx_metal_store_action(GfxStoreOp op);
static MTLIndexType gfx_metal_index_type(GfxIndexType type);
static MTLVertexFormat gfx_metal_vertex_format(GfxVertexFormat format);
static MTLSamplerMinMagFilter gfx_metal_filter(GfxFilter filter);
static MTLSamplerAddressMode gfx_metal_address_mode(GfxAddressMode mode);
static MTLCompareFunction gfx_metal_compare_op(GfxCompareOp op);
static MTLCullMode gfx_metal_cull_mode(GfxCullMode mode);
static MTLWinding gfx_metal_front_face(GfxFrontFace face);
static GfxMetalBuffer* gfx_metal_resolve_buffer(GfxDevice* device, GfxBuffer handle);
static GfxMetalTexture* gfx_metal_resolve_texture(GfxDevice* device, GfxTexture handle);
static GfxMetalSampler* gfx_metal_resolve_sampler(GfxDevice* device, GfxSampler handle);
static GfxMetalPipeline* gfx_metal_resolve_pipeline(GfxDevice* device, GfxPipeline handle);
static B32 gfx_metal_upload_buffer_immediate(GfxDevice* device, id<MTLBuffer> dst, U64 dstOffset, const void* src, U64 size);
static GfxBuffer gfx_metal_create_buffer_internal(GfxDevice* device, const GfxBufferDesc* desc, B32 internal);
static B32 gfx_metal_resource_table_init(GfxDevice* device);
static void gfx_metal_resource_table_set_texture(GfxDevice* device, GfxResourceId resourceId, id<MTLTexture> texture);
static void gfx_metal_resource_table_set_sampler(GfxDevice* device, GfxResourceId resourceId, id<MTLSamplerState> sampler);
static void gfx_metal_resource_table_clear(GfxDevice* device, GfxResourceId resourceId);
static B32 gfx_metal_resource_entries_reserve(GfxDevice* device, U32 neededCapacity);
static GfxResourceId gfx_metal_register_resource(GfxDevice* device, GfxMetalResourceKind kind, U32 index, U32 generation);
static void gfx_metal_release_frame_objects(GfxFrame* frame);

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

static U32 gfx_metal_color_format_bytes_per_pixel(GfxFormat format) {
    switch (format) {
        case GfxFormat_BGRA8_UNorm:
        case GfxFormat_RGBA8_UNorm: {
            return 4u;
        }
        case GfxFormat_RGBA16_Float: {
            return 8u;
        }
        default: {
            return 0u;
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

static MTLVertexFormat gfx_metal_vertex_format(GfxVertexFormat format) {
    switch (format) {
        case GfxVertexFormat_F32x2: {
            return MTLVertexFormatFloat2;
        }
        case GfxVertexFormat_F32x3: {
            return MTLVertexFormatFloat3;
        }
        case GfxVertexFormat_F32x4: {
            return MTLVertexFormatFloat4;
        }
        case GfxVertexFormat_U8x4_UNorm: {
            return MTLVertexFormatUChar4Normalized;
        }
        default: {
            return MTLVertexFormatInvalid;
        }
    }
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

static GfxMetalBuffer* gfx_metal_resolve_buffer(GfxDevice* device, GfxBuffer handle) {
    ASSERT_DEBUG(device != 0);
    if (!device) {
        return &g_gfxMetalNilBuffer;
    }
    if (handle.generation == 0u) {
        ASSERT_DEBUG(handle.index == 0u && "Malformed nil gfx buffer handle");
        return &g_gfxMetalNilBuffer;
    }

    GfxMetalBuffer* result = (GfxMetalBuffer*)slot_map_get(&device->buffers, handle.index, handle.generation);
    ASSERT_DEBUG(result != 0 && "Stale gfx buffer handle");
    return result ? result : &g_gfxMetalNilBuffer;
}

static GfxMetalTexture* gfx_metal_resolve_texture(GfxDevice* device, GfxTexture handle) {
    ASSERT_DEBUG(device != 0);
    if (!device) {
        return &g_gfxMetalNilTexture;
    }
    if (handle.generation == 0u) {
        ASSERT_DEBUG(handle.index == 0u && "Malformed nil gfx texture handle");
        return &g_gfxMetalNilTexture;
    }

    GfxMetalTexture* result = (GfxMetalTexture*)slot_map_get(&device->textures, handle.index, handle.generation);
    ASSERT_DEBUG(result != 0 && "Stale gfx texture handle");
    return result ? result : &g_gfxMetalNilTexture;
}

static GfxMetalSampler* gfx_metal_resolve_sampler(GfxDevice* device, GfxSampler handle) {
    ASSERT_DEBUG(device != 0);
    if (!device) {
        return &g_gfxMetalNilSampler;
    }
    if (handle.generation == 0u) {
        ASSERT_DEBUG(handle.index == 0u && "Malformed nil gfx sampler handle");
        return &g_gfxMetalNilSampler;
    }

    GfxMetalSampler* result = (GfxMetalSampler*)slot_map_get(&device->samplers, handle.index, handle.generation);
    ASSERT_DEBUG(result != 0 && "Stale gfx sampler handle");
    return result ? result : &g_gfxMetalNilSampler;
}

static GfxMetalPipeline* gfx_metal_resolve_pipeline(GfxDevice* device, GfxPipeline handle) {
    ASSERT_DEBUG(device != 0);
    if (!device) {
        return &g_gfxMetalNilPipeline;
    }
    if (handle.generation == 0u) {
        ASSERT_DEBUG(handle.index == 0u && "Malformed nil gfx pipeline handle");
        return &g_gfxMetalNilPipeline;
    }

    GfxMetalPipeline* result = (GfxMetalPipeline*)slot_map_get(&device->pipelines, handle.index, handle.generation);
    ASSERT_DEBUG(result != 0 && "Stale gfx pipeline handle");
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
        return {};
    }

    MTLResourceOptions options = (desc->memoryKind == GfxMemoryKind_Device) ? MTLResourceStorageModePrivate : MTLResourceStorageModeShared;
    id<MTLBuffer> buffer = [device->metalDevice newBufferWithLength:(NSUInteger)desc->size options:options];
    if (!buffer) {
        LOG_ERROR("gfx", "Metal buffer creation failed (size={})", desc->size);
        return {};
    }

    if (desc->name) {
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

    MTLArgumentDescriptor* textureArg = [MTLArgumentDescriptor argumentDescriptor];
    MTLArgumentDescriptor* samplerArg = [MTLArgumentDescriptor argumentDescriptor];
    if (!textureArg || !samplerArg) {
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

    NSArray<MTLArgumentDescriptor*>* args = @[textureArg, samplerArg];
    device->resourceArgumentEncoder = [device->metalDevice newArgumentEncoderWithArguments:args];
    if (!device->resourceArgumentEncoder) {
        return 0;
    }

    device->resourceArgumentBuffer = [device->metalDevice newBufferWithLength:[device->resourceArgumentEncoder encodedLength]
                                                                       options:MTLResourceStorageModeShared];
    if (!device->resourceArgumentBuffer) {
        return 0;
    }

    [device->resourceArgumentBuffer setLabel:@"gfx resource table"];
    [device->resourceArgumentEncoder setArgumentBuffer:device->resourceArgumentBuffer offset:0u];
    for (U32 i = 0u; i < GFX_METAL_RESOURCE_TABLE_CAPACITY; ++i) {
        [device->resourceArgumentEncoder setTexture:nil atIndex:GFX_METAL_RESOURCE_TABLE_TEXTURE_BASE + i];
        [device->resourceArgumentEncoder setSamplerState:nil atIndex:GFX_METAL_RESOURCE_TABLE_SAMPLER_BASE + i];
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

static void gfx_metal_resource_table_clear(GfxDevice* device, GfxResourceId resourceId) {
    if (!device || !device->resourceEntries ||
        resourceId.index == 0u ||
        resourceId.index >= device->resourceCount ||
        resourceId.index >= GFX_METAL_RESOURCE_TABLE_CAPACITY) {
        return;
    }

    GfxMetalResourceEntry* entry = &device->resourceEntries[resourceId.index];
    if (entry->kind == GfxMetalResourceKind_Texture) {
        gfx_metal_resource_table_set_texture(device, resourceId, nil);
    } else if (entry->kind == GfxMetalResourceKind_Sampler) {
        gfx_metal_resource_table_set_sampler(device, resourceId, nil);
    }

    *entry = {};
}

static B32 gfx_metal_resource_entries_reserve(GfxDevice* device, U32 neededCapacity) {
    if (!device) {
        return 0;
    }
    if (neededCapacity <= device->resourceCapacity) {
        return 1;
    }
    if (neededCapacity > GFX_METAL_RESOURCE_TABLE_CAPACITY) {
        ASSERT_DEBUG(neededCapacity <= GFX_METAL_RESOURCE_TABLE_CAPACITY);
        LOG_ERROR("gfx", "Metal resource table is full");
        return 0;
    }

    U32 newCapacity = GFX_METAL_RESOURCE_TABLE_CAPACITY;
    GfxMetalResourceEntry* newEntries = ARENA_PUSH_ARRAY(device->arena, GfxMetalResourceEntry, newCapacity);
    if (!newEntries) {
        return 0;
    }
    MEMSET(newEntries, 0, sizeof(GfxMetalResourceEntry) * newCapacity);

    if (device->resourceEntries && device->resourceCount > 0u) {
        MEMCPY(newEntries, device->resourceEntries, sizeof(GfxMetalResourceEntry) * device->resourceCount);
    }

    device->resourceEntries = newEntries;
    device->resourceCapacity = newCapacity;
    return 1;
}

static GfxResourceId gfx_metal_register_resource(GfxDevice* device, GfxMetalResourceKind kind, U32 index, U32 generation) {
    if (!device || generation == 0u) {
        return {};
    }
    if (device->resourceCount == 0u) {
        device->resourceCount = 1u;
    }
    if (device->resourceCount >= GFX_METAL_RESOURCE_TABLE_CAPACITY) {
        ASSERT_DEBUG(device->resourceCount < GFX_METAL_RESOURCE_TABLE_CAPACITY);
        LOG_ERROR("gfx", "Metal resource table is full");
        return {};
    }
    if (!gfx_metal_resource_entries_reserve(device, device->resourceCount + 1u)) {
        return {};
    }

    U32 resourceIndex = device->resourceCount;
    GfxMetalResourceEntry* entry = &device->resourceEntries[resourceIndex];
    entry->kind = kind;
    entry->index = index;
    entry->generation = generation;
    device->resourceCount += 1u;
    device->stats.resourceTableCount = device->resourceCount - 1u;

    GfxResourceId result = {resourceIndex};
    return result;
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

    OS_WindowSurfaceInfo surface = OS_window_get_surface_info(desc->window);
    if (!surface.metalLayerPtr) {
        LOG_ERROR("gfx", "Window has no CAMetalLayer");
        return 0;
    }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)surface.metalLayerPtr;
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
    device->validationEnabled = desc->enableValidation;
    device->alive = 1;
    device->metalLayer = [layer retain];
    device->metalDevice = metalDevice;
    device->commandQueue = commandQueue;
    device->framesInFlight = desc->framesInFlight ? desc->framesInFlight : GFX_DEFAULT_FRAMES_IN_FLIGHT;
    device->frameSemaphore = dispatch_semaphore_create((long)device->framesInFlight);
    device->resourceCount = 1u;

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
    backbuffer->usageFlags = GfxTextureUsageFlags_ColorTarget;
    backbuffer->resourceId = {};
    backbuffer->ownsTexture = 0;
    backbuffer->internal = 1;
    device->backbuffer.index = textureIndex;
    device->backbuffer.generation = textureGeneration;

    [layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
    [layer setFramebufferOnly:YES];
    gfx_device_resize(device, surface.drawableWidth, surface.drawableHeight);

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
    device->drawableWidth = width;
    device->drawableHeight = height;
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
}

GfxBuffer gfx_create_buffer(GfxDevice* device, const GfxBufferDesc* desc) {
    return gfx_metal_create_buffer_internal(device, desc, 0);
}

GfxTexture gfx_create_texture(GfxDevice* device, const GfxTextureDesc* desc) {
    if (!device || !desc || desc->width == 0u || desc->height == 0u || desc->format == GfxFormat_Invalid) {
        return {};
    }

    MTLPixelFormat pixelFormat = gfx_metal_pixel_format(desc->format);
    if (pixelFormat == MTLPixelFormatInvalid) {
        LOG_ERROR("gfx", "Unsupported texture format {}", (U32)desc->format);
        return {};
    }

    MTLTextureDescriptor* textureDesc = [[MTLTextureDescriptor alloc] init];
    textureDesc.textureType = MTLTextureType2D;
    textureDesc.pixelFormat = pixelFormat;
    textureDesc.width = desc->width;
    textureDesc.height = desc->height;
    textureDesc.mipmapLevelCount = desc->mipCount ? desc->mipCount : 1u;
    textureDesc.storageMode = MTLStorageModePrivate;
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

    if (desc->name) {
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

    if (desc->name) {
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
        LOG_ERROR("gfx", "Metal v1 only supports MSL source shaders");
        return {};
    }
    if (!desc->colorFormats || desc->colorFormatCount == 0u ||
        desc->colorFormatCount > GFX_MAX_COLOR_TARGETS) {
        LOG_ERROR("gfx", "Invalid graphics pipeline color formats");
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

    if (desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            pipelineDesc.label = label;
            [label release];
        }
    }

    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    if (!vertexDescriptor) {
        [pipelineDesc release];
        [vertexFunction release];
        [fragmentFunction release];
        [vertexLibrary release];
        [fragmentLibrary release];
        return {};
    }
    vertexDescriptor.layouts[0].stride = desc->vertexBuffer.stride;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    vertexDescriptor.layouts[0].stepRate = 1u;

    for (U32 i = 0; i < desc->attributeCount; ++i) {
        const GfxVertexAttribute* attribute = &desc->attributes[i];
        if (attribute->location >= 31u) {
            continue;
        }
        vertexDescriptor.attributes[attribute->location].format = gfx_metal_vertex_format(attribute->format);
        vertexDescriptor.attributes[attribute->location].offset = attribute->offset;
        vertexDescriptor.attributes[attribute->location].bufferIndex = 0u;
    }
    pipelineDesc.vertexDescriptor = vertexDescriptor;

    for (U32 i = 0; i < desc->colorFormatCount; ++i) {
        pipelineDesc.colorAttachments[i].pixelFormat = gfx_metal_pixel_format(desc->colorFormats[i]);
    }
    if (desc->depthFormat != GfxFormat_Invalid) {
        pipelineDesc.depthAttachmentPixelFormat = gfx_metal_pixel_format(desc->depthFormat);
    }

    error = nil;
    id<MTLRenderPipelineState> pipelineState = [device->metalDevice newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                                                    error:&error];
    [vertexDescriptor release];
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
    gfx_metal_resource_table_clear(device, item->resourceId);
    [item->buffer release];
    item->buffer = 0;
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
    gfx_metal_resource_table_clear(device, item->resourceId);
    if (item->ownsTexture) {
        [item->texture release];
        item->texture = 0;
    }
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
    gfx_metal_resource_table_clear(device, item->resourceId);
    [item->sampler release];
    item->sampler = 0;
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
    if (item->graphicsPipeline) {
        [item->graphicsPipeline release];
        item->graphicsPipeline = 0;
    }
    if (item->computePipeline) {
        [item->computePipeline release];
        item->computePipeline = 0;
    }
    if (item->depthState) {
        [item->depthState release];
        item->depthState = 0;
    }
    void* released = 0;
    slot_map_release(&device->pipelines, pipeline.index, pipeline.generation, &released);
}

GfxResourceId gfx_register_texture(GfxDevice* device, GfxTexture texture) {
    if (!device) {
        return {};
    }
    GfxMetalTexture* item = gfx_metal_resolve_texture(device, texture);
    if (!item->texture) {
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_metal_register_resource(device, GfxMetalResourceKind_Texture, texture.index, texture.generation);
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
    item->resourceId = gfx_metal_register_resource(device, GfxMetalResourceKind_Sampler, sampler.index, sampler.generation);
    gfx_metal_resource_table_set_sampler(device, item->resourceId, item->sampler);
    return item->resourceId;
}

GfxResourceId gfx_register_buffer(GfxDevice* device, GfxBuffer buffer) {
    if (!device) {
        return {};
    }
    GfxMetalBuffer* item = gfx_metal_resolve_buffer(device, buffer);
    if (!item->buffer) {
        return {};
    }
    if (item->resourceId.index != 0u) {
        return item->resourceId;
    }
    item->resourceId = gfx_metal_register_resource(device, GfxMetalResourceKind_Buffer, buffer.index, buffer.generation);
    return item->resourceId;
}

GfxFrame* gfx_begin_frame(GfxDevice* device) {
    if (!device || !device->alive) {
        return 0;
    }

    dispatch_semaphore_wait(device->frameSemaphore, DISPATCH_TIME_FOREVER);

    GfxFrame* frame = &device->frames[device->frameCursor];
    device->frameCursor = (device->frameCursor + 1u) % device->framesInFlight;

    gfx_metal_release_frame_objects(frame);

    id<CAMetalDrawable> drawable = [device->metalLayer nextDrawable];
    if (!drawable) {
        LOG_ERROR("gfx", "Metal layer did not provide a drawable");
        dispatch_semaphore_signal(device->frameSemaphore);
        return 0;
    }

    frame->drawable = [drawable retain];
    frame->commandBuffer = [[device->commandQueue commandBuffer] retain];
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
    device->stats.resourceTableCount = (device->resourceCount > 0u) ? (device->resourceCount - 1u) : 0u;
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
        LOG_ERROR("gfx", "Temp allocation alignment is not a power of two ({})", alignment);
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
    result.gpu.address = 0u;

    frame->tempPos = end;
    frame->device->stats.tempBytesUsed = frame->tempPos;
    return result;
}

void gfx_upload_buffer(GfxFrame* frame, GfxBuffer dst, U64 dstOffset, const void* src, U64 size) {
    if (!frame || !frame->device || !src || size == 0u) {
        return;
    }

    GfxMetalBuffer* buffer = gfx_metal_resolve_buffer(frame->device, dst);
    if (!buffer->buffer) {
        return;
    }
    if (dstOffset > buffer->size || size > (buffer->size - dstOffset)) {
        ASSERT_DEBUG(dstOffset <= buffer->size && size <= (buffer->size - dstOffset));
        LOG_ERROR("gfx", "gfx_upload_buffer out of bounds");
        return;
    }

    if (buffer->memoryKind == GfxMemoryKind_Upload) {
        void* cpu = [buffer->buffer contents];
        ASSERT_DEBUG(cpu != 0);
        if (!cpu) {
            return;
        }

        MEMCPY((U8*)cpu + dstOffset, src, size);
        return;
    }

    ASSERT_DEBUG(frame->commandBuffer != 0);
    if (!frame->commandBuffer) {
        return;
    }

    GfxTemp temp = gfx_allocate_temp(frame, size, 16u);
    ASSERT_DEBUG(temp.cpu != 0 && "gfx_upload_buffer ran out of frame upload memory");
    if (!temp.cpu) {
        LOG_ERROR("gfx", "gfx_upload_buffer ran out of frame upload memory (size={})", size);
        return;
    }

    MEMCPY(temp.cpu, src, size);
    GfxMetalBuffer* uploadBuffer = gfx_metal_resolve_buffer(frame->device, temp.gpu.buffer);
    ASSERT_DEBUG(uploadBuffer->buffer != 0);
    if (!uploadBuffer->buffer) {
        return;
    }

    id<MTLBlitCommandEncoder> blit = [[frame->commandBuffer blitCommandEncoder] retain];
    ASSERT_DEBUG(blit != 0);
    if (!blit) {
        return;
    }

    [blit copyFromBuffer:uploadBuffer->buffer
            sourceOffset:(NSUInteger)temp.gpu.offset
                toBuffer:buffer->buffer
       destinationOffset:(NSUInteger)dstOffset
                    size:(NSUInteger)size];
    [blit endEncoding];
    [blit release];
}

void gfx_upload_texture(GfxFrame* frame, GfxTexture dst, const GfxTextureUploadRegion* region, const void* src) {
    if (!frame || !frame->device || !region || !src) {
        return;
    }

    GfxMetalTexture* texture = gfx_metal_resolve_texture(frame->device, dst);
    if (!texture->texture) {
        return;
    }

    U32 bytesPerPixel = gfx_metal_color_format_bytes_per_pixel(texture->format);
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

    ASSERT_DEBUG(supported);
    ASSERT_DEBUG(inBounds);
    ASSERT_DEBUG(rowLayout);
    if (!supported || !inBounds || !rowLayout) {
        LOG_ERROR("gfx", "gfx_upload_texture invalid upload region");
        return;
    }

    U64 sourceBytesPerImage = region->bytesPerRow * region->rowsPerImage;
    ASSERT_DEBUG(sourceBytesPerImage / region->rowsPerImage == region->bytesPerRow);
    if (sourceBytesPerImage == 0u || sourceBytesPerImage / region->rowsPerImage != region->bytesPerRow) {
        LOG_ERROR("gfx", "gfx_upload_texture upload size overflow");
        return;
    }

    ASSERT_DEBUG(frame->commandBuffer != 0);
    if (!frame->commandBuffer) {
        return;
    }

    GfxTemp temp = gfx_allocate_temp(frame, sourceBytesPerImage, GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT);
    ASSERT_DEBUG(temp.cpu != 0 && "gfx_upload_texture ran out of frame upload memory");
    if (!temp.cpu) {
        LOG_ERROR("gfx", "gfx_upload_texture ran out of frame upload memory (size={})", sourceBytesPerImage);
        return;
    }

    MEMCPY(temp.cpu, src, sourceBytesPerImage);
    GfxMetalBuffer* uploadBuffer = gfx_metal_resolve_buffer(frame->device, temp.gpu.buffer);
    ASSERT_DEBUG(uploadBuffer->buffer != 0);
    if (!uploadBuffer->buffer) {
        return;
    }

    id<MTLBlitCommandEncoder> blit = [[frame->commandBuffer blitCommandEncoder] retain];
    ASSERT_DEBUG(blit != 0);
    if (!blit) {
        return;
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

    ASSERT_DEBUG(desc->colorTargetCount > 0u);
    ASSERT_DEBUG(desc->colorTargetCount <= GFX_MAX_COLOR_TARGETS);
    ASSERT_DEBUG(desc->colorTargetCount == 0u || desc->colorTargets != 0);
    ASSERT_DEBUG(areaCount == 0u || areas != 0);
    if (desc->colorTargetCount == 0u ||
        desc->colorTargetCount > GFX_MAX_COLOR_TARGETS ||
        !desc->colorTargets ||
        (areaCount != 0u && !areas)) {
        return;
    }

    MTLRenderPassDescriptor* passDesc = [[MTLRenderPassDescriptor alloc] init];
    if (!passDesc) {
        return;
    }
    for (U32 i = 0; i < desc->colorTargetCount; ++i) {
        const GfxColorTarget* target = &desc->colorTargets[i];
        GfxMetalTexture* texture = gfx_metal_resolve_texture(device, target->texture);
        if (!texture->texture) {
            [passDesc release];
            return;
        }
        ASSERT_DEBUG(texture->width == desc->width && texture->height == desc->height);
        ASSERT_DEBUG(FLAGS_HAS(texture->usageFlags, GfxTextureUsageFlags_ColorTarget));
        if (texture->width != desc->width ||
            texture->height != desc->height ||
            !FLAGS_HAS(texture->usageFlags, GfxTextureUsageFlags_ColorTarget)) {
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
        ASSERT_DEBUG(depthTexture->width == desc->width && depthTexture->height == desc->height);
        ASSERT_DEBUG(FLAGS_HAS(depthTexture->usageFlags, GfxTextureUsageFlags_DepthTarget));
        if (depthTexture->width != desc->width ||
            depthTexture->height != desc->height ||
            !FLAGS_HAS(depthTexture->usageFlags, GfxTextureUsageFlags_DepthTarget)) {
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

    if (desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            [encoder setLabel:label];
            [label release];
        }
    }

    GfxMetalBuffer* passDataBuffer = gfx_metal_resolve_buffer(device, desc->passData.buffer);
    if (passDataBuffer->buffer &&
        desc->passData.offset <= passDataBuffer->size &&
        desc->passData.size <= passDataBuffer->size - desc->passData.offset) {
        [encoder setVertexBuffer:passDataBuffer->buffer
                           offset:(NSUInteger)desc->passData.offset
                          atIndex:GFX_SHADER_SLOT_PASS_DATA];
        [encoder setFragmentBuffer:passDataBuffer->buffer
                             offset:(NSUInteger)desc->passData.offset
                            atIndex:GFX_SHADER_SLOT_PASS_DATA];
    }

    if (device->resourceArgumentBuffer) {
        [encoder setFragmentBuffer:device->resourceArgumentBuffer
                             offset:0u
                            atIndex:GFX_SHADER_SLOT_RESOURCE_TABLE];

        for (U32 resourceIndex = 1u; resourceIndex < device->resourceCount; ++resourceIndex) {
            GfxMetalResourceEntry* entry = device->resourceEntries ? &device->resourceEntries[resourceIndex] : 0;
            if (!entry || entry->kind != GfxMetalResourceKind_Texture) {
                continue;
            }

            GfxTexture textureHandle = {entry->index, entry->generation};
            GfxMetalTexture* texture = gfx_metal_resolve_texture(device, textureHandle);
            if (texture->texture) {
                [encoder useResource:texture->texture
                                usage:MTLResourceUsageRead
                               stages:MTLRenderStageFragment];
            }
        }
    }

    GfxPipeline boundPipeline = {};

    for (U32 areaIndex = 0; areaIndex < areaCount; ++areaIndex) {
        const GfxDrawArea* area = &areas[areaIndex];
        ASSERT_DEBUG(area->drawCount == 0u || area->draws != 0);
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
            if (draw->indexCount == 0u) {
                continue;
            }

            GfxMetalPipeline* pipeline = gfx_metal_resolve_pipeline(device, draw->pipeline);
            GfxMetalBuffer* vertexBuffer = gfx_metal_resolve_buffer(device, draw->vertexBuffer);
            GfxMetalBuffer* indexBuffer = gfx_metal_resolve_buffer(device, draw->indexBuffer);
            GfxMetalBuffer* drawDataBuffer = gfx_metal_resolve_buffer(device, draw->drawData.buffer);

            ASSERT_DEBUG(pipeline->kind == GfxPipelineKind_Graphics);
            if (pipeline->kind != GfxPipelineKind_Graphics ||
                !pipeline->graphicsPipeline ||
                !vertexBuffer->buffer ||
                !indexBuffer->buffer ||
                !drawDataBuffer->buffer) {
                continue;
            }

            if (boundPipeline.index != draw->pipeline.index || boundPipeline.generation != draw->pipeline.generation) {
                [encoder setRenderPipelineState:pipeline->graphicsPipeline];
                [encoder setCullMode:gfx_metal_cull_mode(pipeline->raster.cullMode)];
                [encoder setFrontFacingWinding:gfx_metal_front_face(pipeline->raster.frontFace)];
                if (pipeline->depthState) {
                    [encoder setDepthStencilState:pipeline->depthState];
                }
                boundPipeline = draw->pipeline;
                device->stats.pipelineSwitchCount += 1u;
            }

            [encoder setVertexBuffer:vertexBuffer->buffer offset:(NSUInteger)draw->vertexByteOffset atIndex:0u];
            [encoder setVertexBuffer:drawDataBuffer->buffer
                               offset:(NSUInteger)draw->drawData.offset
                              atIndex:GFX_SHADER_SLOT_DRAW_DATA];
            [encoder setFragmentBuffer:drawDataBuffer->buffer
                                 offset:(NSUInteger)draw->drawData.offset
                                atIndex:GFX_SHADER_SLOT_DRAW_DATA];

            U32 instanceCount = draw->instanceCount ? draw->instanceCount : 1u;
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:draw->indexCount
                                 indexType:gfx_metal_index_type(draw->indexType)
                               indexBuffer:indexBuffer->buffer
                         indexBufferOffset:(NSUInteger)draw->indexByteOffset
                             instanceCount:instanceCount
                                baseVertex:draw->baseVertex
                              baseInstance:draw->firstInstance];
            device->stats.drawCount += 1u;
        }
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

    ASSERT_DEBUG(dispatchCount == 0u || dispatches != 0);
    if (dispatchCount != 0u && !dispatches) {
        return;
    }

    id<MTLComputeCommandEncoder> encoder = [[frame->commandBuffer computeCommandEncoder] retain];
    if (!encoder) {
        LOG_ERROR("gfx", "Failed to create Metal compute encoder");
        return;
    }

    if (desc->name) {
        NSString* label = gfx_nsstring_from_cstr(desc->name);
        if (label) {
            [encoder setLabel:label];
            [label release];
        }
    }

    GfxMetalBuffer* passDataBuffer = gfx_metal_resolve_buffer(device, desc->passData.buffer);
    if (passDataBuffer->buffer &&
        desc->passData.offset <= passDataBuffer->size &&
        desc->passData.size <= passDataBuffer->size - desc->passData.offset) {
        [encoder setBuffer:passDataBuffer->buffer
                    offset:(NSUInteger)desc->passData.offset
                   atIndex:GFX_SHADER_SLOT_PASS_DATA];
    }

    for (U32 dispatchIndex = 0u; dispatchIndex < dispatchCount; ++dispatchIndex) {
        const GfxDispatch* dispatch = dispatches + dispatchIndex;
        if (dispatch->groupsX == 0u ||
            dispatch->groupsY == 0u ||
            dispatch->groupsZ == 0u) {
            continue;
        }

        GfxMetalPipeline* pipeline = gfx_metal_resolve_pipeline(device, dispatch->pipeline);
        GfxMetalBuffer* dispatchDataBuffer = gfx_metal_resolve_buffer(device, dispatch->dispatchData.buffer);

        ASSERT_DEBUG(pipeline->kind == GfxPipelineKind_Compute || pipeline->graphicsPipeline == 0);
        if (pipeline->kind != GfxPipelineKind_Compute ||
            !pipeline->computePipeline ||
            !dispatchDataBuffer->buffer ||
            dispatch->dispatchData.offset > dispatchDataBuffer->size ||
            dispatch->dispatchData.size > dispatchDataBuffer->size - dispatch->dispatchData.offset) {
            continue;
        }

        [encoder setComputePipelineState:pipeline->computePipeline];
        [encoder setBuffer:dispatchDataBuffer->buffer
                    offset:(NSUInteger)dispatch->dispatchData.offset
                   atIndex:GFX_SHADER_SLOT_DISPATCH_DATA];

        MTLSize threadgroups = MTLSizeMake((NSUInteger)dispatch->groupsX,
                                           (NSUInteger)dispatch->groupsY,
                                           (NSUInteger)dispatch->groupsZ);
        MTLSize threadsPerThreadgroup = MTLSizeMake((NSUInteger)pipeline->threadsPerThreadgroupX,
                                                    (NSUInteger)pipeline->threadsPerThreadgroupY,
                                                    (NSUInteger)pipeline->threadsPerThreadgroupZ);
        [encoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerThreadgroup];
        device->stats.dispatchCount += 1u;
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

    dispatch_semaphore_t semaphore = device->frameSemaphore;
    [frame->commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> commandBuffer) {
        (void)commandBuffer;
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
