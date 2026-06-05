#pragma once

#define GFX_MAX_COLOR_TARGETS 4u
#define GFX_SHADER_SLOT_DRAW_DATA 8u
#define GFX_SHADER_SLOT_DISPATCH_DATA GFX_SHADER_SLOT_DRAW_DATA
#define GFX_SHADER_SLOT_RESOURCE_TABLE 9u
#define GFX_SHADER_SLOT_PASS_DATA 10u
#define GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT 256u

struct GfxDevice;
struct GfxFrame;
struct GfxCommandBuffer;

struct GfxBuffer {
    U32 index;
    U32 generation;
};

struct GfxTexture {
    U32 index;
    U32 generation;
};

struct GfxSampler {
    U32 index;
    U32 generation;
};

struct GfxPipeline {
    U32 index;
    U32 generation;
};

struct GfxResourceId {
    U32 index;
};

struct GfxGpuSlice {
    GfxBuffer buffer;
    U64 offset;
    U64 size;
    U64 address;
};

struct GfxTemp {
    void* cpu;
    GfxGpuSlice gpu;
};

enum GfxBackend {
    GfxBackend_None = 0,
    GfxBackend_Metal,
    GfxBackend_Vulkan,
};

enum GfxFormat {
    GfxFormat_Invalid = 0,
    GfxFormat_BGRA8_UNorm,
    GfxFormat_RGBA8_UNorm,
    GfxFormat_RGBA16_Float,
    GfxFormat_D32_Float,
};

enum GfxMemoryKind {
    GfxMemoryKind_Device = 0,
    GfxMemoryKind_Upload,
};

enum GfxBufferUsageFlags {
    GfxBufferUsageFlags_None    = 0,
    GfxBufferUsageFlags_Vertex  = (1u << 0),
    GfxBufferUsageFlags_Index   = (1u << 1),
    GfxBufferUsageFlags_Uniform = (1u << 2),
    GfxBufferUsageFlags_Storage = (1u << 3),
    GfxBufferUsageFlags_CopyDst = (1u << 4),
};

enum GfxTextureUsageFlags {
    GfxTextureUsageFlags_None        = 0,
    GfxTextureUsageFlags_ColorTarget = (1u << 0),
    GfxTextureUsageFlags_DepthTarget = (1u << 1),
    GfxTextureUsageFlags_Sampled     = (1u << 2),
    GfxTextureUsageFlags_Storage     = (1u << 3),
    GfxTextureUsageFlags_Transient   = (1u << 4),
    GfxTextureUsageFlags_CopyDst     = (1u << 5),
};

enum GfxLoadOp {
    GfxLoadOp_Load = 0,
    GfxLoadOp_Clear,
    GfxLoadOp_DontCare,
};

enum GfxStoreOp {
    GfxStoreOp_Store = 0,
    GfxStoreOp_DontCare,
};

enum GfxIndexType {
    GfxIndexType_U16 = 0,
    GfxIndexType_U32,
};

enum GfxShaderFormat {
    GfxShaderFormat_MSL_Source = 0,
    GfxShaderFormat_MetalLib,
    GfxShaderFormat_SPIRV,
};

enum GfxPipelineKind {
    GfxPipelineKind_Graphics = 0,
    GfxPipelineKind_Compute,
};

enum GfxPrimitiveTopology {
    GfxPrimitiveTopology_TriangleList = 0,
};

enum GfxVertexFormat {
    GfxVertexFormat_F32x2 = 0,
    GfxVertexFormat_F32x3,
    GfxVertexFormat_F32x4,
    GfxVertexFormat_U8x4_UNorm,
};

enum GfxFilter {
    GfxFilter_Nearest = 0,
    GfxFilter_Linear,
};

enum GfxAddressMode {
    GfxAddressMode_ClampToEdge = 0,
    GfxAddressMode_Repeat,
};

enum GfxCullMode {
    GfxCullMode_None = 0,
    GfxCullMode_Front,
    GfxCullMode_Back,
};

enum GfxFrontFace {
    GfxFrontFace_CCW = 0,
    GfxFrontFace_CW,
};

enum GfxCompareOp {
    GfxCompareOp_Always = 0,
    GfxCompareOp_Less,
    GfxCompareOp_LessEqual,
};

enum GfxValidationFlags {
    GfxValidationFlags_None       = 0,
    GfxValidationFlags_Api        = (1u << 0),
    GfxValidationFlags_Backend    = (1u << 1),
    GfxValidationFlags_GpuMarkers = (1u << 2),
};

struct GfxDeviceDesc {
    GfxBackend backend;
    OS_WindowHandle window;
    U32 framesInFlight;
    U64 tempBufferSize;
    U32 validationFlags;
};

struct GfxBufferDesc {
    const char* name;
    U64 size;
    U32 usageFlags;
    GfxMemoryKind memoryKind;
    const void* initialData;
};

struct GfxTextureDesc {
    const char* name;
    U32 width;
    U32 height;
    U32 mipCount;
    GfxFormat format;
    U32 usageFlags;
};

struct GfxTextureUploadRegion {
    U32 mip;
    U32 layer;
    U32 layerCount;
    U32 x;
    U32 y;
    U32 z;
    U32 width;
    U32 height;
    U32 depth;
    U64 bytesPerRow;
    U32 rowsPerImage;
};

struct GfxSamplerDesc {
    const char* name;
    GfxFilter minFilter;
    GfxFilter magFilter;
    GfxAddressMode addressU;
    GfxAddressMode addressV;
};

struct GfxShaderCode {
    GfxShaderFormat format;
    const void* data;
    U64 size;
    const char* entry;
};

struct GfxVertexAttribute {
    U32 location;
    U32 offset;
    GfxVertexFormat format;
};

struct GfxVertexBufferLayout {
    U32 stride;
};

struct GfxRasterState {
    GfxCullMode cullMode;
    GfxFrontFace frontFace;
};

struct GfxDepthState {
    B32 depthTestEnabled;
    B32 depthWriteEnabled;
    GfxCompareOp compareOp;
};

struct GfxGraphicsPipelineDesc {
    const char* name;
    GfxShaderCode vertexShader;
    GfxShaderCode fragmentShader;
    const GfxVertexAttribute* attributes;
    U32 attributeCount;
    GfxVertexBufferLayout vertexBuffer;
    GfxPrimitiveTopology topology;
    GfxRasterState raster;
    GfxDepthState depth;
    const GfxFormat* colorFormats;
    U32 colorFormatCount;
    GfxFormat depthFormat;
};

struct GfxComputePipelineDesc {
    const char* name;
    GfxShaderCode shader;
    U32 threadsPerThreadgroupX;
    U32 threadsPerThreadgroupY;
    U32 threadsPerThreadgroupZ;
};

struct GfxViewport {
    F32 x;
    F32 y;
    F32 width;
    F32 height;
    F32 minDepth;
    F32 maxDepth;
};

struct GfxRect {
    S32 x;
    S32 y;
    U32 width;
    U32 height;
};

struct GfxDraw {
    GfxPipeline pipeline;
    GfxBuffer vertexBuffer;
    GfxBuffer indexBuffer;
    U64 vertexByteOffset;
    U64 indexByteOffset;
    U32 indexCount;
    U32 instanceCount;
    S32 baseVertex;
    U32 firstInstance;
    GfxIndexType indexType;
    GfxGpuSlice drawData;
};

struct GfxDrawArea {
    GfxViewport viewport;
    GfxRect scissor;
    const GfxDraw* draws;
    U32 drawCount;
};

struct GfxDispatch {
    GfxPipeline pipeline;
    GfxGpuSlice dispatchData;
    U32 groupsX;
    U32 groupsY;
    U32 groupsZ;
};

struct GfxColorTarget {
    GfxTexture texture;
    GfxLoadOp loadOp;
    GfxStoreOp storeOp;
    F32 clearColor[4];
};

struct GfxDepthTarget {
    GfxTexture texture;
    GfxLoadOp loadOp;
    GfxStoreOp storeOp;
    F32 clearDepth;
};

struct GfxRenderPassDesc {
    const char* name;
    const GfxColorTarget* colorTargets;
    U32 colorTargetCount;
    const GfxDepthTarget* depthTarget;
    GfxGpuSlice passData;
    U32 width;
    U32 height;
};

struct GfxComputePassDesc {
    const char* name;
    GfxGpuSlice passData;
};

struct GfxStats {
    U32 drawCount;
    U32 dispatchCount;
    U32 pipelineSwitchCount;
    U32 resourceTableCount;
    U32 tempOverflowCount;
    U64 tempBytesUsed;
    U64 frameIndex;
};

UTILITIES_SHARED_API B32 gfx_device_create(const GfxDeviceDesc* desc, Arena* arena, GfxDevice** outDevice);
UTILITIES_SHARED_API void gfx_device_destroy(GfxDevice* device);
UTILITIES_SHARED_API void gfx_device_resize(GfxDevice* device, U32 width, U32 height);
UTILITIES_SHARED_API void gfx_wait_idle(GfxDevice* device);

UTILITIES_SHARED_API GfxBuffer gfx_create_buffer(GfxDevice* device, const GfxBufferDesc* desc);
UTILITIES_SHARED_API GfxTexture gfx_create_texture(GfxDevice* device, const GfxTextureDesc* desc);
UTILITIES_SHARED_API GfxSampler gfx_create_sampler(GfxDevice* device, const GfxSamplerDesc* desc);
UTILITIES_SHARED_API GfxPipeline gfx_create_graphics_pipeline(GfxDevice* device, const GfxGraphicsPipelineDesc* desc);
UTILITIES_SHARED_API GfxPipeline gfx_create_compute_pipeline(GfxDevice* device, const GfxComputePipelineDesc* desc);
UTILITIES_SHARED_API void gfx_destroy_buffer(GfxDevice* device, GfxBuffer buffer);
UTILITIES_SHARED_API void gfx_destroy_texture(GfxDevice* device, GfxTexture texture);
UTILITIES_SHARED_API void gfx_destroy_sampler(GfxDevice* device, GfxSampler sampler);
UTILITIES_SHARED_API void gfx_destroy_pipeline(GfxDevice* device, GfxPipeline pipeline);

UTILITIES_SHARED_API GfxResourceId gfx_register_texture(GfxDevice* device, GfxTexture texture);
UTILITIES_SHARED_API GfxResourceId gfx_register_sampler(GfxDevice* device, GfxSampler sampler);
UTILITIES_SHARED_API GfxResourceId gfx_register_buffer(GfxDevice* device, GfxBuffer buffer);

UTILITIES_SHARED_API GfxFrame* gfx_begin_frame(GfxDevice* device);
UTILITIES_SHARED_API GfxCommandBuffer* gfx_get_command_buffer(GfxFrame* frame);
UTILITIES_SHARED_API GfxTexture gfx_get_backbuffer(GfxFrame* frame);
UTILITIES_SHARED_API GfxTemp gfx_allocate_temp(GfxFrame* frame, U64 size, U64 alignment);
UTILITIES_SHARED_API B32 gfx_upload_buffer(GfxFrame* frame, GfxBuffer dst, U64 dstOffset, const void* src, U64 size);
UTILITIES_SHARED_API B32 gfx_upload_texture(GfxFrame* frame, GfxTexture dst, const GfxTextureUploadRegion* region, const void* src);

UTILITIES_SHARED_API void gfx_render_pass(GfxCommandBuffer* commands, const GfxRenderPassDesc* desc, const GfxDrawArea* areas, U32 areaCount);
UTILITIES_SHARED_API void gfx_compute_pass(GfxCommandBuffer* commands, const GfxComputePassDesc* desc, const GfxDispatch* dispatches, U32 dispatchCount);
UTILITIES_SHARED_API void gfx_submit(GfxCommandBuffer* commands);
UTILITIES_SHARED_API void gfx_end_frame(GfxFrame* frame);
UTILITIES_SHARED_API GfxStats gfx_get_stats(GfxDevice* device);
