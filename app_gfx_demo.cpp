//
// Created by André Leite on 31/10/2025.
//

#define APP_GFX_DEMO_RENDERER_DATA_VERSION 7u
#define APP_GFX_DEMO_TEXTURE_PATH "app/textures/demo.ppm"
#define APP_GFX_DEMO_DRAW_COLUMNS 12u
#define APP_GFX_DEMO_DRAW_ROWS 8u
#define APP_GFX_DEMO_DRAW_COUNT (APP_GFX_DEMO_DRAW_COLUMNS * APP_GFX_DEMO_DRAW_ROWS)
#define APP_GFX_DEMO_CULL_STRESS_OBJECT_COUNT 1024u
#define APP_GFX_DEMO_OBJECT_COUNT (APP_GFX_DEMO_DRAW_COUNT + APP_GFX_DEMO_CULL_STRESS_OBJECT_COUNT)
#define APP_GFX_DEMO_MATERIAL_COUNT APP_GFX_DEMO_DRAW_COUNT
#define APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP 64u
#define APP_GFX_DEMO_CULL_BOUNDS_THREADS_PER_GROUP 128u
#define APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP 128u
#define APP_GFX_DEMO_VISIBILITY_MAX_GROUPS_PER_BIN 128u
#define APP_GFX_DEMO_MAX_FRAME_DELTA_SECONDS 0.05f
#define APP_GFX_DEMO_DRAW_PHASE_STEP 0.071f
#define APP_GFX_DEMO_OVERLAP_EXTENT_X 0.46f
#define APP_GFX_DEMO_OVERLAP_EXTENT_Y 0.34f
#define APP_GFX_DEMO_OVERLAP_SCALE 0.27f
#define APP_GFX_DEMO_DEPTH_NEAR 0.10f
#define APP_GFX_DEMO_DEPTH_RANGE 0.82f
#define APP_GFX_DEMO_CULL_VERTEX_MIN_X -0.55f
#define APP_GFX_DEMO_CULL_VERTEX_MAX_X 0.55f
#define APP_GFX_DEMO_CULL_VERTEX_MIN_Y -0.45f
#define APP_GFX_DEMO_CULL_VERTEX_MAX_Y 0.55f
#define APP_GFX_DEMO_CULL_MAX_ANIMATION_SCALE 1.055f
#define APP_GFX_DEMO_CULL_WOBBLE_EXTENT 0.014f
#define APP_IMAGE_RGBA8_BYTES_PER_PIXEL 4u

struct AppGfxVertex {
    F32 position[2];
    F32 color[4];
};

struct AppGfxDemoObject {
    F32 offset[2];
    F32 scale;
    F32 depth;
    F32 phaseOffset;
    U32 materialIndex;
    U32 objectId;
    U32 flags;
};

struct AppGfxGpuObject {
    F32 offsetScale[4];
    F32 phaseOffset;
    U32 materialIndex;
    U32 objectId;
    U32 flags;
};

struct AppGfxGpuCullSource {
    F32 localMin[2];
    F32 localMax[2];
    F32 offsetScale[4];
    U32 objectIndex;
    U32 flags;
    F32 maxAnimationScale;
    F32 wobbleExtent;
};

struct AppGfxGpuCullObject {
    F32 clipMin[2];
    F32 clipMax[2];
    F32 depthMin;
    F32 depthMax;
    U32 objectIndex;
    U32 flags;
};

typedef enum AppGfxDemoObjectFlags {
    AppGfxDemoObjectFlags_None        = 0u,
    AppGfxDemoObjectFlags_AlphaTest   = (1u << 0u),
    AppGfxDemoObjectFlags_Transparent = (1u << 1u),
} AppGfxDemoObjectFlags;

typedef enum AppGfxDrawBinKind {
    AppGfxDrawBinKind_Opaque = 0u,
    AppGfxDrawBinKind_AlphaTest,
    AppGfxDrawBinKind_Transparent,
    AppGfxDrawBinKind_COUNT,
} AppGfxDrawBinKind;

struct AppGfxDrawBin {
    GfxDraw* draws;
    U32 drawCount;
};

struct AppGfxDemoComputePacket {
    GfxComputePassDesc pass;
    GfxComputeWrite writes[2];
    GfxResourceUse resourceUses[5];
    GfxDispatch dispatch;
};

struct AppGfxDemoRenderPacket {
    GfxRenderPassDesc pass;
    GfxColorTarget colorTarget;
    GfxDepthTarget depthTarget;
    GfxResourceUse resourceUses[6];
    GfxDrawArea areas[AppGfxDrawBinKind_COUNT];
    U32 areaCount;
};

typedef enum AppTrianglePipelineKind {
    AppTrianglePipelineKind_Opaque = 0u,
    AppTrianglePipelineKind_Transparent,
    AppTrianglePipelineKind_COUNT,
} AppTrianglePipelineKind;

static const AppGfxVertex APP_GFX_DEMO_VERTICES[] = {
    {{ 0.0f,  0.55f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.55f, -0.45f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{ 0.55f, -0.45f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};

static const U16 APP_GFX_DEMO_INDICES[] = {
    0u, 1u, 2u,
};

struct AppGfxDrawRootData {
    U32 vertexBuffer;
    U32 vertexByteOffset;
    U32 objectBuffer;
    U32 objectByteOffset;
    U32 visibleIndexBuffer;
    U32 visibleIndexByteOffset;
    U32 materialBuffer;
    U32 materialByteOffset;
    F32 animationPhase;
    U32 _padding0;
    U32 _padding1;
    U32 _padding2;
    U32 _padding3;
    U32 _padding4;
    U32 _padding5;
    U32 _padding6;
};

struct AppGfxMaterial {
    F32 baseColor[4];
    U32 albedoTexture;
    U32 samplerIndex;
    U32 flags;
    U32 _padding;
};

struct AppGfxMaterialComputeRootData {
    U32 materialCount;
    U32 sourceMaterialBuffer;
    U32 sourceMaterialByteOffset;
    U32 materialBuffer;
    U32 materialByteOffset;
    F32 animationPhase;
    U32 _padding0;
    U32 _padding1;
};

struct AppGfxCullBoundsComputeRootData {
    U32 cullSourceCount;
    U32 cullSourceBuffer;
    U32 cullSourceByteOffset;
    U32 cullObjectBuffer;
    U32 cullObjectByteOffset;
    U32 _padding0;
    U32 _padding1;
    U32 _padding2;
};

struct AppGfxVisibilityBin {
    U32 sourceIndexByteOffset;
    U32 sourceIndexCount;
    U32 visibleIndexByteOffset;
    U32 indirectArgsByteOffset;
    U32 groupStart;
    U32 groupCount;
    U32 _padding2;
    U32 _padding3;
};

struct AppGfxVisibilityGroup {
    U32 sourceIndexByteOffset;
    U32 sourceIndexCount;
    U32 visibleIndexByteOffset;
    U32 binIndex;
    U32 _padding0;
    U32 _padding1;
    U32 _padding2;
    U32 _padding3;
};

struct AppGfxVisibilityComputeRootData {
    U32 binCount;
    U32 groupCount;
    U32 cullObjectBuffer;
    U32 cullObjectByteOffset;
    U32 sourceIndexBuffer;
    U32 sourceIndexByteOffset;
    U32 visibleIndexBuffer;
    U32 visibleIndexByteOffset;
    U32 indirectArgsBuffer;
    U32 indirectArgsByteOffset;
    U32 binBuffer;
    U32 binByteOffset;
    U32 groupBuffer;
    U32 groupByteOffset;
    U32 groupCountBuffer;
    U32 groupCountByteOffset;
    U32 groupOffsetBuffer;
    U32 groupOffsetByteOffset;
    U32 _padding0;
    U32 _padding1;
    U32 _padding2;
    U32 _padding3;
    U32 _padding4;
    U32 _padding5;
};

static_assert(sizeof(AppGfxDrawRootData) == 64u, "DrawRootData shader ABI mismatch");
static_assert(sizeof(AppGfxVertex) == 24u, "Demo vertex shader ABI mismatch");
static_assert(sizeof(AppGfxGpuObject) == 32u, "Demo object shader ABI mismatch");
static_assert(sizeof(AppGfxGpuCullSource) == 48u, "Demo cull source shader ABI mismatch");
static_assert(sizeof(AppGfxGpuCullObject) == 32u, "Demo cull object shader ABI mismatch");
static_assert(sizeof(AppGfxMaterial) == 32u, "Material shader ABI mismatch");
static_assert(sizeof(AppGfxMaterialComputeRootData) == 32u, "MaterialComputeRootData shader ABI mismatch");
static_assert(sizeof(AppGfxCullBoundsComputeRootData) == 32u, "CullBoundsComputeRootData shader ABI mismatch");
static_assert(sizeof(AppGfxVisibilityBin) == 32u, "VisibilityBin shader ABI mismatch");
static_assert(sizeof(AppGfxVisibilityGroup) == 32u, "VisibilityGroup shader ABI mismatch");
static_assert(sizeof(AppGfxVisibilityComputeRootData) == 96u, "VisibilityComputeRootData shader ABI mismatch");
static_assert(sizeof(GfxDrawIndexedIndirectArgs) == 20u, "DrawIndexedIndirectArgs shader ABI mismatch");
#define APP_SHADER_ABI_OFFSET(type, member, byteOffset) \
    static_assert(offsetof(type, member) == (byteOffset), #type "." #member " shader ABI offset mismatch")
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, vertexBuffer, 0u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, vertexByteOffset, 4u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, objectBuffer, 8u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, objectByteOffset, 12u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, visibleIndexBuffer, 16u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, visibleIndexByteOffset, 20u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, materialBuffer, 24u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, materialByteOffset, 28u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, animationPhase, 32u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding0, 36u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding1, 40u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding2, 44u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding3, 48u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding4, 52u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding5, 56u);
APP_SHADER_ABI_OFFSET(AppGfxDrawRootData, _padding6, 60u);
APP_SHADER_ABI_OFFSET(AppGfxVertex, position, 0u);
APP_SHADER_ABI_OFFSET(AppGfxVertex, color, 8u);
APP_SHADER_ABI_OFFSET(AppGfxGpuObject, offsetScale, 0u);
APP_SHADER_ABI_OFFSET(AppGfxGpuObject, phaseOffset, 16u);
APP_SHADER_ABI_OFFSET(AppGfxGpuObject, materialIndex, 20u);
APP_SHADER_ABI_OFFSET(AppGfxGpuObject, objectId, 24u);
APP_SHADER_ABI_OFFSET(AppGfxGpuObject, flags, 28u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, localMin, 0u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, localMax, 8u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, offsetScale, 16u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, objectIndex, 32u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, flags, 36u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, maxAnimationScale, 40u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullSource, wobbleExtent, 44u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullObject, clipMin, 0u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullObject, clipMax, 8u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullObject, depthMin, 16u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullObject, depthMax, 20u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullObject, objectIndex, 24u);
APP_SHADER_ABI_OFFSET(AppGfxGpuCullObject, flags, 28u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, baseColor, 0u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, albedoTexture, 16u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, samplerIndex, 20u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, flags, 24u);
APP_SHADER_ABI_OFFSET(AppGfxMaterial, _padding, 28u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, materialCount, 0u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, sourceMaterialBuffer, 4u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, sourceMaterialByteOffset, 8u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, materialBuffer, 12u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, materialByteOffset, 16u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, animationPhase, 20u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, _padding0, 24u);
APP_SHADER_ABI_OFFSET(AppGfxMaterialComputeRootData, _padding1, 28u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, cullSourceCount, 0u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, cullSourceBuffer, 4u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, cullSourceByteOffset, 8u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, cullObjectBuffer, 12u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, cullObjectByteOffset, 16u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, _padding0, 20u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, _padding1, 24u);
APP_SHADER_ABI_OFFSET(AppGfxCullBoundsComputeRootData, _padding2, 28u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, sourceIndexByteOffset, 0u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, sourceIndexCount, 4u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, visibleIndexByteOffset, 8u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, indirectArgsByteOffset, 12u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, groupStart, 16u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, groupCount, 20u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, _padding2, 24u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityBin, _padding3, 28u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, sourceIndexByteOffset, 0u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, sourceIndexCount, 4u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, visibleIndexByteOffset, 8u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, binIndex, 12u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, _padding0, 16u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, _padding1, 20u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, _padding2, 24u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityGroup, _padding3, 28u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, binCount, 0u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupCount, 4u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, cullObjectBuffer, 8u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, cullObjectByteOffset, 12u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, sourceIndexBuffer, 16u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, sourceIndexByteOffset, 20u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, visibleIndexBuffer, 24u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, visibleIndexByteOffset, 28u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, indirectArgsBuffer, 32u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, indirectArgsByteOffset, 36u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, binBuffer, 40u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, binByteOffset, 44u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupBuffer, 48u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupByteOffset, 52u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupCountBuffer, 56u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupCountByteOffset, 60u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupOffsetBuffer, 64u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, groupOffsetByteOffset, 68u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, _padding0, 72u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, _padding1, 76u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, _padding2, 80u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, _padding3, 84u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, _padding4, 88u);
APP_SHADER_ABI_OFFSET(AppGfxVisibilityComputeRootData, _padding5, 92u);
APP_SHADER_ABI_OFFSET(GfxDrawIndexedIndirectArgs, indexCount, 0u);
APP_SHADER_ABI_OFFSET(GfxDrawIndexedIndirectArgs, instanceCount, 4u);
APP_SHADER_ABI_OFFSET(GfxDrawIndexedIndirectArgs, firstIndex, 8u);
APP_SHADER_ABI_OFFSET(GfxDrawIndexedIndirectArgs, baseVertex, 12u);
APP_SHADER_ABI_OFFSET(GfxDrawIndexedIndirectArgs, firstInstance, 16u);
#undef APP_SHADER_ABI_OFFSET

struct AppImageRGBA8 {
    U32 width;
    U32 height;
    U8* pixels;
    U64 bytesPerRow;
};

struct AppDecodedImageHeader {
    U32 width;
    U32 height;
    U64 bytesPerRow;
};

struct AppPPMToken {
    const U8* data;
    U64 size;
};

struct AppPPMCursor {
    const U8* at;
    const U8* end;
};

enum AppGfxDemoLoadLog {
    AppGfxDemoLoadLog_Started = (1u << 0u),
    AppGfxDemoLoadLog_GeometryCreated = (1u << 1u),
    AppGfxDemoLoadLog_GeometryUploaded = (1u << 2u),
    AppGfxDemoLoadLog_TrianglePipeline = (1u << 3u),
    AppGfxDemoLoadLog_ComputePipeline = (1u << 4u),
    AppGfxDemoLoadLog_TextureUploaded = (1u << 5u),
    AppGfxDemoLoadLog_Ready = (1u << 6u),
    AppGfxDemoLoadLog_Targets = (1u << 7u),
};

enum AppArtifactTypeId {
    AppArtifactTypeId_TrianglePipeline = 1u,
    AppArtifactTypeId_ComputePipeline = 2u,
    AppArtifactTypeId_DecodedTexture = 3u,
};

static B32 app_gfx_demo_register_artifact_types(APP_Context* ctx);
static void app_gfx_demo_watch_files(APP_Context* ctx);
static void app_gfx_demo_resource_cache_reset(APP_Context* ctx);
static B32 app_decode_ppm_rgba8(Arena* arena, const void* data, U64 size, AppImageRGBA8* outImage);
static B32 app_build_triangle_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes);
static B32 app_publish_triangle_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                 ArtifactValue buildValue,
                                                 ArtifactValue* outValue,
                                                 U64* outBytes);
static B32 app_build_compute_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes);
static B32 app_publish_compute_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                ArtifactValue buildValue,
                                                ArtifactValue* outValue,
                                                U64* outBytes);
static B32 app_build_decoded_texture_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes);
static B32 app_publish_decoded_texture_artifact(ArtifactPublishContext* artifactCtx,
                                               ArtifactValue buildValue,
                                               ArtifactValue* outValue,
                                               U64* outBytes);
static void app_destroy_decoded_texture_artifact(void* userData, ArtifactValue value);
static const char* app_triangle_pipeline_kind_label(AppTrianglePipelineKind kind);
static const char* app_triangle_pipeline_name(AppTrianglePipelineKind kind);
static GfxPipeline* app_triangle_pipeline_slot(AppCoreState* state, AppTrianglePipelineKind kind);
static ArtifactKey* app_triangle_pipeline_artifact_key_slot(AppCoreState* state, AppTrianglePipelineKind kind);
static B32 app_gfx_demo_create_triangle_pipeline(APP_Context* ctx, ContentHash vertexHash, ContentHash fragmentHash, AppTrianglePipelineKind kind, GfxPipeline* outPipeline);
static B32 app_gfx_demo_create_compute_pipeline(APP_Context* ctx, ContentHash shaderHash, const char* name, const char* entry, U32 threadsPerThreadgroupX, GfxPipeline* outPipeline);
static B32 app_gfx_demo_init(APP_Context* ctx);
static B32 app_gfx_seed_demo_renderer_data(APP_Context* ctx);
static void app_gfx_demo_log_once(AppCoreState* state, U32 bit, const char* message);
static void app_gfx_try_create_demo_buffers(APP_Context* ctx);
static void app_gfx_upload_demo_geometry(APP_Context* ctx, GfxFrame* frame);
static void app_gfx_upload_demo_objects(APP_Context* ctx, GfxFrame* frame);
static void app_gfx_upload_demo_visibility_sources(APP_Context* ctx, GfxFrame* frame);
static void app_gfx_try_update_triangle_pipelines(APP_Context* ctx);
static void app_gfx_try_update_demo_compute_pipeline(APP_Context* ctx);
static void app_gfx_try_update_demo_gpu_data_pipelines(APP_Context* ctx);
static void app_gfx_upload_demo_texture(APP_Context* ctx, GfxFrame* frame);
static void app_gfx_upload_demo_material_sources(APP_Context* ctx, GfxFrame* frame);
static GfxResourceUse app_gfx_buffer_resource_use(GfxBuffer buffer, U32 accessFlags, U32 shaderStages);
static GfxResourceUse app_gfx_texture_resource_use(GfxTexture texture, U32 accessFlags, U32 shaderStages);
static B32 app_gfx_build_demo_material_compute_packet(APP_Context* ctx, GfxFrame* frame, AppGfxDemoComputePacket* outPacket);
static B32 app_gfx_build_demo_cull_bounds_compute_packet(APP_Context* ctx, GfxFrame* frame, AppGfxDemoComputePacket* outPacket);
static void app_gfx_write_demo_visibility_root_data(AppCoreState* state, AppGfxVisibilityComputeRootData* rootData);
static B32 app_gfx_build_demo_visibility_count_packet(APP_Context* ctx, GfxTemp rootTemp, AppGfxDemoComputePacket* outPacket);
static B32 app_gfx_build_demo_visibility_prefix_packet(APP_Context* ctx, GfxTemp rootTemp, AppGfxDemoComputePacket* outPacket);
static B32 app_gfx_build_demo_visibility_compact_packet(APP_Context* ctx, GfxTemp rootTemp, AppGfxDemoComputePacket* outPacket);
static void app_gfx_destroy_demo_targets(APP_Context* ctx);
static B32 app_gfx_ensure_demo_targets(APP_Context* ctx);
static AppGfxDrawBinKind app_gfx_demo_draw_bin_kind(const AppGfxDemoObject* object);
static AppGfxGpuObject app_gfx_demo_gpu_object_from_object(const AppGfxDemoObject* object);
static AppGfxGpuCullSource app_gfx_demo_cull_source_from_object(const AppGfxDemoObject* object, U32 objectIndex);
static B32 app_gfx_demo_transparent_draw_before(const AppGfxDemoObject* a, const AppGfxDemoObject* b);
static void app_gfx_sort_demo_transparent_indices(const AppGfxDemoObject* objects, U32* indices, U32 count);
static B32 app_gfx_build_demo_draw_bins(APP_Context* ctx,
                                        GfxFrame* frame,
                                        Arena* arena,
                                        B32 materialsReady,
                                        B32 visibilityReady,
                                        AppGfxDrawBin* outBins,
                                        U32 outBinCount,
                                        U32* outDrawCount);
static GfxDrawArea app_gfx_demo_draw_area(U32 width, U32 height, const GfxDraw* draws, U32 drawCount);
static U32 app_gfx_demo_draw_areas(U32 width,
                                   U32 height,
                                   const AppGfxDrawBin* bins,
                                   U32 binCount,
                                   GfxDrawArea* outAreas,
                                   U32 outAreaCapacity);
static B32 app_gfx_build_demo_render_packet(APP_Context* ctx,
                                            const AppGfxDrawBin* bins,
                                            U32 binCount,
                                            GfxTexture colorTexture,
                                            B32 useDepth,
                                            B32 offscreen,
                                            AppGfxDemoRenderPacket* outPacket);
static void app_gfx_demo_shutdown(APP_Context* ctx);
static void app_gfx_demo_frame(APP_Context* ctx, F32 deltaSeconds);

static void app_gfx_demo_resource_cache_reset(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    state->gfxDemo.shaders.triangleVertex = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.triangleFragment = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.materialCompute = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.cullBoundsCompute = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.visibilityCountCompute = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.visibilityPrefixCompute = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.visibilityCompactCompute = FILE_HANDLE_ZERO;
    state->gfxDemo.shaders.textureSource = FILE_HANDLE_ZERO;
    state->gfxDemo.pipelines.triangleOpaqueArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.triangleTransparentArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.materialComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.cullBoundsComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.visibilityCountComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.visibilityPrefixComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.visibilityCompactComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.upload.textureDecodeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.upload.decodedTextureHash = CONTENT_HASH_ZERO;
}

static B32 app_gfx_demo_register_artifact_types(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (!state->resources.artifactCache) {
        return 0;
    }

    ArtifactTypeDesc triangleType = {};
    triangleType.typeId = AppArtifactTypeId_TrianglePipeline;
    triangleType.name = str8("triangle pipeline");
    triangleType.buildProc = app_build_triangle_pipeline_artifact;
    triangleType.publishProc = app_publish_triangle_pipeline_artifact;
    triangleType.evictionTargetCount = 16u;
    triangleType.evictionMaxIdleFrames = 240u;

    ArtifactTypeDesc computeType = {};
    computeType.typeId = AppArtifactTypeId_ComputePipeline;
    computeType.name = str8("demo compute pipeline");
    computeType.buildProc = app_build_compute_pipeline_artifact;
    computeType.publishProc = app_publish_compute_pipeline_artifact;
    computeType.evictionTargetCount = 16u;
    computeType.evictionMaxIdleFrames = 240u;

    ArtifactTypeDesc decodedType = {};
    decodedType.typeId = AppArtifactTypeId_DecodedTexture;
    decodedType.name = str8("decoded demo texture");
    decodedType.buildProc = app_build_decoded_texture_artifact;
    decodedType.publishProc = app_publish_decoded_texture_artifact;
    decodedType.destroyProc = app_destroy_decoded_texture_artifact;
    decodedType.userData = state->resources.contentStore;
    decodedType.evictionTargetCount = 32u;
    decodedType.evictionMaxIdleFrames = 240u;

    return artifact_register_type(state->resources.artifactCache, &triangleType) &&
           artifact_register_type(state->resources.artifactCache, &computeType) &&
           artifact_register_type(state->resources.artifactCache, &decodedType);
}

static void app_gfx_demo_watch_files(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (!state->resources.fileStream) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 exeDir = OS_get_executable_directory(scratch.arena);
    StringU8 vertexShaderPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_TRIANGLE_VERTEX_RUNTIME_PATH));
    StringU8 fragmentShaderPath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_TRIANGLE_FRAGMENT_RUNTIME_PATH));
    StringU8 computePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DEMO_MATERIAL_COMPUTE_RUNTIME_PATH));
    StringU8 cullBoundsComputePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DEMO_CULL_BOUNDS_COMPUTE_RUNTIME_PATH));
    StringU8 visibilityCountComputePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DEMO_VISIBILITY_COUNT_COMPUTE_RUNTIME_PATH));
    StringU8 visibilityPrefixComputePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DEMO_VISIBILITY_PREFIX_COMPUTE_RUNTIME_PATH));
    StringU8 visibilityCompactComputePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_SHADER_DEMO_VISIBILITY_COMPACT_COMPUTE_RUNTIME_PATH));
    StringU8 texturePath = str8_concat(scratch.arena, exeDir, str8("/../" APP_GFX_DEMO_TEXTURE_PATH));

    state->gfxDemo.shaders.triangleVertex = file_watch(state->resources.fileStream, vertexShaderPath, 0u);
    state->gfxDemo.shaders.triangleFragment = file_watch(state->resources.fileStream, fragmentShaderPath, 0u);
    state->gfxDemo.shaders.materialCompute = file_watch(state->resources.fileStream, computePath, 0u);
    state->gfxDemo.shaders.cullBoundsCompute = file_watch(state->resources.fileStream, cullBoundsComputePath, 0u);
    state->gfxDemo.shaders.visibilityCountCompute = file_watch(state->resources.fileStream, visibilityCountComputePath, 0u);
    state->gfxDemo.shaders.visibilityPrefixCompute = file_watch(state->resources.fileStream, visibilityPrefixComputePath, 0u);
    state->gfxDemo.shaders.visibilityCompactCompute = file_watch(state->resources.fileStream, visibilityCompactComputePath, 0u);
    state->gfxDemo.shaders.textureSource = file_watch(state->resources.fileStream, texturePath, 0u);
}

static B32 app_ppm_is_space(U8 c) {
    return c == (U8)' ' ||
           c == (U8)'\n' ||
           c == (U8)'\r' ||
           c == (U8)'\t' ||
           c == (U8)'\v' ||
           c == (U8)'\f';
}

static void app_ppm_skip_space(AppPPMCursor* cursor) {
    ASSERT_ALWAYS(cursor != 0);

    for (;;) {
        while (cursor->at < cursor->end && app_ppm_is_space(*cursor->at)) {
            cursor->at += 1;
        }

        if (cursor->at < cursor->end && *cursor->at == (U8)'#') {
            while (cursor->at < cursor->end && *cursor->at != (U8)'\n') {
                cursor->at += 1;
            }
            continue;
        }

        break;
    }
}

static B32 app_ppm_read_token(AppPPMCursor* cursor, AppPPMToken* outToken) {
    ASSERT_ALWAYS(cursor != 0);
    ASSERT_ALWAYS(outToken != 0);

    app_ppm_skip_space(cursor);
    if (cursor->at >= cursor->end) {
        return 0;
    }

    const U8* start = cursor->at;
    while (cursor->at < cursor->end &&
           !app_ppm_is_space(*cursor->at) &&
           *cursor->at != (U8)'#') {
        cursor->at += 1;
    }

    outToken->data = start;
    outToken->size = (U64)(cursor->at - start);
    return outToken->size != 0u;
}

static B32 app_ppm_token_is(AppPPMToken token, const char* text) {
    ASSERT_ALWAYS(text != 0);

    U64 index = 0u;
    while (text[index] != 0) {
        if (index >= token.size || token.data[index] != (U8)text[index]) {
            return 0;
        }
        index += 1;
    }

    return index == token.size;
}

static B32 app_ppm_read_u32(AppPPMCursor* cursor, U32* outValue) {
    ASSERT_ALWAYS(cursor != 0);
    ASSERT_ALWAYS(outValue != 0);

    AppPPMToken token = {};
    if (!app_ppm_read_token(cursor, &token)) {
        return 0;
    }

    U64 value = 0u;
    for (U64 i = 0u; i < token.size; ++i) {
        U8 c = token.data[i];
        if (c < (U8)'0' || c > (U8)'9') {
            return 0;
        }

        value = value * 10u + (U64)(c - (U8)'0');
        if (value > 0xFFFFFFFFu) {
            return 0;
        }
    }

    *outValue = (U32)value;
    return 1;
}

static B32 app_decode_ppm_rgba8(Arena* arena, const void* data, U64 size, AppImageRGBA8* outImage) {
    if (outImage != 0) {
        *outImage = {};
    }
    if (arena == 0 || data == 0 || size == 0u || outImage == 0) {
        return 0;
    }

    AppPPMCursor cursor = {};
    cursor.at = (const U8*)data;
    cursor.end = cursor.at + size;

    AppPPMToken magic = {};
    U32 width = 0u;
    U32 height = 0u;
    U32 maxValue = 0u;
    if (!app_ppm_read_token(&cursor, &magic) ||
        !app_ppm_token_is(magic, "P3") ||
        !app_ppm_read_u32(&cursor, &width) ||
        !app_ppm_read_u32(&cursor, &height) ||
        !app_ppm_read_u32(&cursor, &maxValue) ||
        width == 0u ||
        height == 0u ||
        maxValue == 0u ||
        maxValue > 255u) {
        return 0;
    }

    if ((U64)width > ((U64)-1) / APP_IMAGE_RGBA8_BYTES_PER_PIXEL) {
        return 0;
    }

    U64 tightRowBytes = (U64)width * APP_IMAGE_RGBA8_BYTES_PER_PIXEL;
    U64 bytesPerRow = align_pow2(tightRowBytes, GFX_TEXTURE_UPLOAD_BYTES_PER_ROW_ALIGNMENT);
    if ((U64)height > ((U64)-1) / bytesPerRow) {
        return 0;
    }

    U64 pixelBytes = bytesPerRow * height;
    U8* pixels = ARENA_PUSH_ARRAY(arena, U8, pixelBytes);
    if (!pixels) {
        return 0;
    }
    MEMSET(pixels, 0, pixelBytes);

    for (U32 y = 0u; y < height; ++y) {
        for (U32 x = 0u; x < width; ++x) {
            U32 r = 0u;
            U32 g = 0u;
            U32 b = 0u;
            if (!app_ppm_read_u32(&cursor, &r) ||
                !app_ppm_read_u32(&cursor, &g) ||
                !app_ppm_read_u32(&cursor, &b) ||
                r > maxValue ||
                g > maxValue ||
                b > maxValue) {
                return 0;
            }

            U64 offset = (U64)y * bytesPerRow + (U64)x * APP_IMAGE_RGBA8_BYTES_PER_PIXEL;
            pixels[offset + 0u] = (U8)((r * 255u + maxValue / 2u) / maxValue);
            pixels[offset + 1u] = (U8)((g * 255u + maxValue / 2u) / maxValue);
            pixels[offset + 2u] = (U8)((b * 255u + maxValue / 2u) / maxValue);
            pixels[offset + 3u] = 255u;
        }
    }

    outImage->width = width;
    outImage->height = height;
    outImage->pixels = pixels;
    outImage->bytesPerRow = bytesPerRow;
    return 1;
}

struct AppTrianglePipelineArtifactData {
    ContentHash vertexHash;
    ContentHash fragmentHash;
    AppTrianglePipelineKind kind;
};

struct AppComputePipelineArtifactData {
    ContentHash shaderHash;
    const char* name;
    const char* entry;
    U32 threadsPerThreadgroupX;
    U32 threadsPerThreadgroupY;
    U32 threadsPerThreadgroupZ;
    U32 _padding;
};

struct AppDecodedTextureArtifactData {
    ContentHash sourceHash;
};

static const char* app_triangle_pipeline_kind_label(AppTrianglePipelineKind kind) {
    const char* result = "triangle pipeline opaque variant";
    if (kind == AppTrianglePipelineKind_Transparent) {
        result = "triangle pipeline transparent variant";
    }
    return result;
}

static const char* app_triangle_pipeline_name(AppTrianglePipelineKind kind) {
    const char* result = "triangle opaque pipeline";
    if (kind == AppTrianglePipelineKind_Transparent) {
        result = "triangle transparent pipeline";
    }
    return result;
}

static GfxPipeline* app_triangle_pipeline_slot(AppCoreState* state, AppTrianglePipelineKind kind) {
    ASSERT_ALWAYS(state != 0);
    GfxPipeline* result = &state->gfxDemo.pipelines.triangleOpaque;
    if (kind == AppTrianglePipelineKind_Transparent) {
        result = &state->gfxDemo.pipelines.triangleTransparent;
    }
    return result;
}

static ArtifactKey* app_triangle_pipeline_artifact_key_slot(AppCoreState* state, AppTrianglePipelineKind kind) {
    ASSERT_ALWAYS(state != 0);
    ArtifactKey* result = &state->gfxDemo.pipelines.triangleOpaqueArtifactKey;
    if (kind == AppTrianglePipelineKind_Transparent) {
        result = &state->gfxDemo.pipelines.triangleTransparentArtifactKey;
    }
    return result;
}

static B32 app_build_triangle_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppTrianglePipelineArtifactData)) {
        return 0;
    }

    const AppTrianglePipelineArtifactData* data = (const AppTrianglePipelineArtifactData*)artifactCtx->requestData;
    if ((U32)data->kind >= AppTrianglePipelineKind_COUNT) {
        return 0;
    }

    ContentView vertexView = content_view_hash(artifactCtx->content, data->vertexHash);
    ContentView fragmentView = content_view_hash(artifactCtx->content, data->fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u || !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    outValue->u64[0] = data->vertexHash.hash[0];
    outValue->u64[1] = data->vertexHash.hash[1];
    outValue->u64[2] = data->fragmentHash.hash[0];
    outValue->u64[3] = data->fragmentHash.hash[1];
    if (outBytes) {
        *outBytes = vertexView.size + fragmentView.size;
    }
    return 1;
}

static B32 app_publish_triangle_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                 ArtifactValue buildValue,
                                                 ArtifactValue* outValue,
                                                 U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppTrianglePipelineArtifactData)) {
        return 0;
    }

    const AppTrianglePipelineArtifactData* data = (const AppTrianglePipelineArtifactData*)artifactCtx->requestData;
    if ((U32)data->kind >= AppTrianglePipelineKind_COUNT) {
        return 0;
    }

    ContentHash vertexHash = {{buildValue.u64[0], buildValue.u64[1]}};
    ContentHash fragmentHash = {{buildValue.u64[2], buildValue.u64[3]}};
    ContentView vertexView = content_view_hash(artifactCtx->content, vertexHash);
    ContentView fragmentView = content_view_hash(artifactCtx->content, fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u ||
        !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    *outValue = buildValue;
    if (outBytes) {
        *outBytes = vertexView.size + fragmentView.size;
    }
    return 1;
}

static B32 app_gfx_demo_create_triangle_pipeline(APP_Context* ctx,
                                                 ContentHash vertexHash,
                                                 ContentHash fragmentHash,
                                                 AppTrianglePipelineKind kind,
                                                 GfxPipeline* outPipeline) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPipeline != 0);

    *outPipeline = {};
    if (ctx->host->gfxDevice == 0 ||
        ctx->core->resources.contentStore == 0 ||
        (U32)kind >= AppTrianglePipelineKind_COUNT) {
        return 0;
    }

    ContentView vertexView = content_view_hash(ctx->core->resources.contentStore, vertexHash);
    ContentView fragmentView = content_view_hash(ctx->core->resources.contentStore, fragmentHash);
    if (!vertexView.valid || vertexView.size == 0u ||
        !fragmentView.valid || fragmentView.size == 0u) {
        return 0;
    }

    GfxFormat colorFormats[1] = {
        GfxFormat_BGRA8_UNorm,
    };
    GfxColorBlendState blendStates[1] = {};
    blendStates[0].blendEnabled = (kind == AppTrianglePipelineKind_Transparent) ? 1 : 0;
    blendStates[0].srcColorFactor = (kind == AppTrianglePipelineKind_Transparent) ?
                                    GfxBlendFactor_SrcAlpha :
                                    GfxBlendFactor_One;
    blendStates[0].dstColorFactor = (kind == AppTrianglePipelineKind_Transparent) ?
                                    GfxBlendFactor_OneMinusSrcAlpha :
                                    GfxBlendFactor_Zero;
    blendStates[0].colorOp = GfxBlendOp_Add;
    blendStates[0].srcAlphaFactor = GfxBlendFactor_One;
    blendStates[0].dstAlphaFactor = (kind == AppTrianglePipelineKind_Transparent) ?
                                    GfxBlendFactor_OneMinusSrcAlpha :
                                    GfxBlendFactor_Zero;
    blendStates[0].alphaOp = GfxBlendOp_Add;
    blendStates[0].writeFlags = GfxColorWriteFlags_RGBA;

    GfxGraphicsPipelineDesc pipelineDesc = {};
    pipelineDesc.name = app_triangle_pipeline_name(kind);
#if defined(PLATFORM_OS_WINDOWS)
    pipelineDesc.vertexShader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.vertexShader.entry = APP_SHADER_TRIANGLE_VERTEX_ENTRY;
    pipelineDesc.fragmentShader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.fragmentShader.entry = APP_SHADER_TRIANGLE_FRAGMENT_ENTRY;
#else
    pipelineDesc.vertexShader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.vertexShader.entry = APP_SHADER_TRIANGLE_VERTEX_ENTRY;
    pipelineDesc.fragmentShader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.fragmentShader.entry = APP_SHADER_TRIANGLE_FRAGMENT_ENTRY;
#endif
    pipelineDesc.vertexShader.data = vertexView.data;
    pipelineDesc.vertexShader.size = vertexView.size;
    pipelineDesc.fragmentShader.data = fragmentView.data;
    pipelineDesc.fragmentShader.size = fragmentView.size;
    pipelineDesc.topology = GfxPrimitiveTopology_TriangleList;
    pipelineDesc.raster.cullMode = GfxCullMode_None;
    pipelineDesc.raster.frontFace = GfxFrontFace_CCW;
    pipelineDesc.depth.depthTestEnabled = 1;
    pipelineDesc.depth.depthWriteEnabled = (kind == AppTrianglePipelineKind_Transparent) ? 0 : 1;
    pipelineDesc.depth.compareOp = GfxCompareOp_LessEqual;
    pipelineDesc.colorFormats = colorFormats;
    pipelineDesc.colorFormatCount = ARRAY_COUNT(colorFormats);
    pipelineDesc.blendStates = blendStates;
    pipelineDesc.blendStateCount = ARRAY_COUNT(blendStates);
    pipelineDesc.depthFormat = GfxFormat_D32_Float;

    GfxPipeline pipeline = gfx_create_graphics_pipeline(ctx->host->gfxDevice, &pipelineDesc);
    if (pipeline.generation == 0u) {
        return 0;
    }

    *outPipeline = pipeline;
    return 1;
}

static B32 app_build_compute_pipeline_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppComputePipelineArtifactData)) {
        return 0;
    }

    const AppComputePipelineArtifactData* data = (const AppComputePipelineArtifactData*)artifactCtx->requestData;
    ContentView shaderView = content_view_hash(artifactCtx->content, data->shaderHash);
    if (!shaderView.valid || shaderView.size == 0u) {
        return 0;
    }

    *outValue = app_content_hash_to_value(data->shaderHash);
    if (outBytes) {
        *outBytes = shaderView.size;
    }
    return 1;
}

static B32 app_publish_compute_pipeline_artifact(ArtifactPublishContext* artifactCtx,
                                                ArtifactValue buildValue,
                                                ArtifactValue* outValue,
                                                U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppComputePipelineArtifactData)) {
        return 0;
    }

    const AppComputePipelineArtifactData* data = (const AppComputePipelineArtifactData*)artifactCtx->requestData;
    if (data->name == 0 ||
        data->entry == 0 ||
        data->entry[0] == 0 ||
        data->threadsPerThreadgroupX == 0u ||
        data->threadsPerThreadgroupY == 0u ||
        data->threadsPerThreadgroupZ == 0u) {
        return 0;
    }

    ContentHash shaderHash = app_content_hash_from_value(buildValue);
    ContentView shaderView = content_view_hash(artifactCtx->content, shaderHash);
    if (!shaderView.valid || shaderView.size == 0u) {
        return 0;
    }

    *outValue = buildValue;
    if (outBytes) {
        *outBytes = shaderView.size;
    }
    return 1;
}

static B32 app_gfx_demo_create_compute_pipeline(APP_Context* ctx,
                                                ContentHash shaderHash,
                                                const char* name,
                                                const char* entry,
                                                U32 threadsPerThreadgroupX,
                                                GfxPipeline* outPipeline) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPipeline != 0);

    *outPipeline = {};
    if (ctx->host->gfxDevice == 0 ||
        ctx->core->resources.contentStore == 0 ||
        name == 0 ||
        entry == 0 ||
        entry[0] == 0 ||
        threadsPerThreadgroupX == 0u) {
        return 0;
    }

    ContentView shaderView = content_view_hash(ctx->core->resources.contentStore, shaderHash);
    if (!shaderView.valid || shaderView.size == 0u) {
        return 0;
    }

    GfxComputePipelineDesc pipelineDesc = {};
    pipelineDesc.name = name;
#if defined(PLATFORM_OS_WINDOWS)
    pipelineDesc.shader.format = GfxShaderFormat_SPIRV;
    pipelineDesc.shader.entry = entry;
#else
    pipelineDesc.shader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.shader.entry = entry;
#endif
    pipelineDesc.shader.data = shaderView.data;
    pipelineDesc.shader.size = shaderView.size;
    pipelineDesc.threadsPerThreadgroupX = threadsPerThreadgroupX;
    pipelineDesc.threadsPerThreadgroupY = 1u;
    pipelineDesc.threadsPerThreadgroupZ = 1u;

    GfxPipeline pipeline = gfx_create_compute_pipeline(ctx->host->gfxDevice, &pipelineDesc);
    if (pipeline.generation == 0u) {
        return 0;
    }

    *outPipeline = pipeline;
    return 1;
}

static B32 app_build_decoded_texture_artifact(ArtifactBuildContext* artifactCtx, ArtifactValue* outValue, U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue ||
        artifactCtx->requestDataSize != sizeof(AppDecodedTextureArtifactData)) {
        return 0;
    }

    const AppDecodedTextureArtifactData* data = (const AppDecodedTextureArtifactData*)artifactCtx->requestData;
    ContentView sourceView = content_view_hash(artifactCtx->content, data->sourceHash);
    if (!sourceView.valid || sourceView.size == 0u) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    AppImageRGBA8 image = {};
    if (!app_decode_ppm_rgba8(scratch.arena, sourceView.data, sourceView.size, &image)) {
        return 0;
    }

    U64 pixelBytes = image.bytesPerRow * image.height;
    U64 blobSize = sizeof(AppDecodedImageHeader) + pixelBytes;
    U8* blob = ARENA_PUSH_ARRAY(scratch.arena, U8, blobSize);
    if (!blob) {
        return 0;
    }

    AppDecodedImageHeader header = {};
    header.width = image.width;
    header.height = image.height;
    header.bytesPerRow = image.bytesPerRow;
    MEMCPY(blob, &header, sizeof(header));
    MEMCPY(blob + sizeof(header), image.pixels, pixelBytes);

    ContentHash hash = content_submit_bytes(artifactCtx->content, CONTENT_KEY_ZERO, blob, blobSize, str8("decoded demo texture"));
    if (content_hash_is_zero(hash)) {
        return 0;
    }

    *outValue = app_content_hash_to_value(hash);
    if (outBytes) {
        *outBytes = blobSize;
    }
    return 1;
}

static B32 app_publish_decoded_texture_artifact(ArtifactPublishContext* artifactCtx,
                                               ArtifactValue buildValue,
                                               ArtifactValue* outValue,
                                               U64* outBytes) {
    if (!artifactCtx || !artifactCtx->content || !outValue) {
        return 0;
    }

    ContentHash hash = app_content_hash_from_value(buildValue);
    ContentView view = content_view_hash(artifactCtx->content, hash);
    if (!view.valid || !content_retain_hash(artifactCtx->content, hash)) {
        return 0;
    }

    *outValue = buildValue;
    if (outBytes) {
        *outBytes = view.size;
    }
    return 1;
}

static void app_destroy_decoded_texture_artifact(void* userData, ArtifactValue value) {
    ContentStore* content = (ContentStore*)userData;
    ContentHash hash = app_content_hash_from_value(value);
    if (content && !content_hash_is_zero(hash)) {
        content_release_hash(content, hash);
    }
}

static B32 app_gfx_demo_init(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemo.runtime.initialized) {
        return app_gfx_seed_demo_renderer_data(ctx);
    }
    if (!ctx->host->gfxDevice) {
        LOG_ERROR("gfx", "App has no gfx device");
        return 0;
    }
    if (!app_resource_cache_init(ctx)) {
        return 0;
    }
    if (!app_gfx_seed_demo_renderer_data(ctx)) {
        return 0;
    }

    state->gfxDemo.upload.materialDirty = 1;
    state->gfxDemo.runtime.initialized = 1;
    app_gfx_demo_log_once(state, AppGfxDemoLoadLog_Started, "Demo resources requested");
    return 1;
}

static AppGfxGpuObject app_gfx_demo_gpu_object_from_object(const AppGfxDemoObject* object) {
    ASSERT_ALWAYS(object != 0);

    AppGfxGpuObject result = {};
    result.offsetScale[0] = object->offset[0];
    result.offsetScale[1] = object->offset[1];
    result.offsetScale[2] = object->scale;
    result.offsetScale[3] = object->depth;
    result.phaseOffset = object->phaseOffset;
    result.materialIndex = object->materialIndex;
    result.objectId = object->objectId;
    result.flags = object->flags;
    return result;
}

static AppGfxGpuCullSource app_gfx_demo_cull_source_from_object(const AppGfxDemoObject* object, U32 objectIndex) {
    ASSERT_ALWAYS(object != 0);

    AppGfxGpuCullSource result = {};
    result.localMin[0] = APP_GFX_DEMO_CULL_VERTEX_MIN_X;
    result.localMin[1] = APP_GFX_DEMO_CULL_VERTEX_MIN_Y;
    result.localMax[0] = APP_GFX_DEMO_CULL_VERTEX_MAX_X;
    result.localMax[1] = APP_GFX_DEMO_CULL_VERTEX_MAX_Y;
    result.offsetScale[0] = object->offset[0];
    result.offsetScale[1] = object->offset[1];
    result.offsetScale[2] = object->scale;
    result.offsetScale[3] = object->depth;
    result.objectIndex = objectIndex;
    result.flags = object->flags;
    result.maxAnimationScale = APP_GFX_DEMO_CULL_MAX_ANIMATION_SCALE;
    result.wobbleExtent = APP_GFX_DEMO_CULL_WOBBLE_EXTENT;
    return result;
}

static B32 app_gfx_seed_demo_renderer_data(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    B32 rendererDataCurrent = (state->gfxDemo.renderer.dataVersion == APP_GFX_DEMO_RENDERER_DATA_VERSION &&
                               state->gfxDemo.renderer.objects != 0 &&
                               state->gfxDemo.renderer.objectCount == APP_GFX_DEMO_OBJECT_COUNT &&
                               state->gfxDemo.renderer.gpuObjects != 0 &&
                               state->gfxDemo.renderer.gpuObjectCount == APP_GFX_DEMO_OBJECT_COUNT &&
                               state->gfxDemo.renderer.gpuCullSources != 0 &&
                               state->gfxDemo.renderer.gpuCullSourceCount == APP_GFX_DEMO_OBJECT_COUNT &&
                               state->gfxDemo.renderer.visibilitySourceIndices != 0 &&
                               state->gfxDemo.renderer.visibilitySourceIndexCount == APP_GFX_DEMO_OBJECT_COUNT &&
                               state->gfxDemo.renderer.visibilityBins != 0 &&
                               state->gfxDemo.renderer.visibilityBinCount == AppGfxDrawBinKind_COUNT &&
                               state->gfxDemo.renderer.visibilityGroups != 0 &&
                               state->gfxDemo.renderer.visibilityGroupCount != 0u &&
                               state->gfxDemo.renderer.materialSources != 0 &&
                               state->gfxDemo.renderer.materialSourceCount == APP_GFX_DEMO_MATERIAL_COUNT) ? 1 : 0;
    if (rendererDataCurrent) {
        return 1;
    }
    if (state->resources.arena == 0) {
        return 0;
    }

    AppGfxDemoObject* objects = state->gfxDemo.renderer.objects;
    AppGfxGpuObject* gpuObjects = state->gfxDemo.renderer.gpuObjects;
    AppGfxGpuCullSource* cullSources = state->gfxDemo.renderer.gpuCullSources;
    AppGfxMaterial* materials = state->gfxDemo.renderer.materialSources;
    U32* visibilitySourceIndices = state->gfxDemo.renderer.visibilitySourceIndices;
    AppGfxVisibilityBin* visibilityBins = state->gfxDemo.renderer.visibilityBins;
    AppGfxVisibilityGroup* visibilityGroups = state->gfxDemo.renderer.visibilityGroups;
    if (objects == 0 || state->gfxDemo.renderer.objectCount != APP_GFX_DEMO_OBJECT_COUNT) {
        objects = ARENA_PUSH_ARRAY(state->resources.arena, AppGfxDemoObject, APP_GFX_DEMO_OBJECT_COUNT);
        if (objects == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo object data");
            return 0;
        }
    }
    if (gpuObjects == 0 || state->gfxDemo.renderer.gpuObjectCount != APP_GFX_DEMO_OBJECT_COUNT) {
        gpuObjects = ARENA_PUSH_ARRAY(state->resources.arena, AppGfxGpuObject, APP_GFX_DEMO_OBJECT_COUNT);
        if (gpuObjects == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo GPU object data");
            return 0;
        }
    }
    if (cullSources == 0 || state->gfxDemo.renderer.gpuCullSourceCount != APP_GFX_DEMO_OBJECT_COUNT) {
        cullSources = ARENA_PUSH_ARRAY(state->resources.arena, AppGfxGpuCullSource, APP_GFX_DEMO_OBJECT_COUNT);
        if (cullSources == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo GPU cull source data");
            return 0;
        }
    }
    if (materials == 0 || state->gfxDemo.renderer.materialSourceCount != APP_GFX_DEMO_MATERIAL_COUNT) {
        materials = ARENA_PUSH_ARRAY(state->resources.arena, AppGfxMaterial, APP_GFX_DEMO_MATERIAL_COUNT);
        if (materials == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo material data");
            return 0;
        }
    }
    if (visibilitySourceIndices == 0 ||
        state->gfxDemo.renderer.visibilitySourceIndexCount != APP_GFX_DEMO_OBJECT_COUNT) {
        visibilitySourceIndices = ARENA_PUSH_ARRAY(state->resources.arena, U32, APP_GFX_DEMO_OBJECT_COUNT);
        if (visibilitySourceIndices == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo visibility source indices");
            return 0;
        }
    }
    if (visibilityBins == 0 ||
        state->gfxDemo.renderer.visibilityBinCount != AppGfxDrawBinKind_COUNT) {
        visibilityBins = ARENA_PUSH_ARRAY(state->resources.arena, AppGfxVisibilityBin, AppGfxDrawBinKind_COUNT);
        if (visibilityBins == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo visibility bins");
            return 0;
        }
    }

    MEMSET(objects, 0, sizeof(AppGfxDemoObject) * APP_GFX_DEMO_OBJECT_COUNT);
    MEMSET(gpuObjects, 0, sizeof(AppGfxGpuObject) * APP_GFX_DEMO_OBJECT_COUNT);
    MEMSET(cullSources, 0, sizeof(AppGfxGpuCullSource) * APP_GFX_DEMO_OBJECT_COUNT);
    MEMSET(materials, 0, sizeof(AppGfxMaterial) * APP_GFX_DEMO_MATERIAL_COUNT);
    MEMSET(visibilitySourceIndices, 0, sizeof(U32) * APP_GFX_DEMO_OBJECT_COUNT);
    MEMSET(visibilityBins, 0, sizeof(AppGfxVisibilityBin) * AppGfxDrawBinKind_COUNT);

    for (U32 row = 0u; row < APP_GFX_DEMO_DRAW_ROWS; ++row) {
        for (U32 column = 0u; column < APP_GFX_DEMO_DRAW_COLUMNS; ++column) {
            U32 index = row * APP_GFX_DEMO_DRAW_COLUMNS + column;
            F32 columnT = (APP_GFX_DEMO_DRAW_COLUMNS > 1u) ?
                          ((F32)column / (F32)(APP_GFX_DEMO_DRAW_COLUMNS - 1u)) :
                          0.0f;
            F32 rowT = (APP_GFX_DEMO_DRAW_ROWS > 1u) ?
                       ((F32)row / (F32)(APP_GFX_DEMO_DRAW_ROWS - 1u)) :
                       0.0f;
            F32 centeredX = columnT * 2.0f - 1.0f;
            F32 centeredY = rowT * 2.0f - 1.0f;
            F32 radialDepth = MIN((centeredX * centeredX + centeredY * centeredY) * 0.5f, 1.0f);

            AppGfxDemoObject* object = objects + index;
            object->offset[0] = centeredX * APP_GFX_DEMO_OVERLAP_EXTENT_X;
            object->offset[1] = centeredY * APP_GFX_DEMO_OVERLAP_EXTENT_Y;
            object->scale = APP_GFX_DEMO_OVERLAP_SCALE;
            object->depth = APP_GFX_DEMO_DEPTH_NEAR + radialDepth * APP_GFX_DEMO_DEPTH_RANGE;
            object->phaseOffset = (F32)index * APP_GFX_DEMO_DRAW_PHASE_STEP;
            object->materialIndex = index;
            object->objectId = index;
            object->flags = AppGfxDemoObjectFlags_None;
            if ((index % 11u) == 5u) {
                object->flags = AppGfxDemoObjectFlags_Transparent;
            } else if ((index % 7u) == 0u) {
                object->flags = AppGfxDemoObjectFlags_AlphaTest;
            }

            gpuObjects[index] = app_gfx_demo_gpu_object_from_object(object);
            cullSources[index] = app_gfx_demo_cull_source_from_object(object, index);

            AppGfxMaterial* material = materials + index;
            material->baseColor[0] = columnT;
            material->baseColor[1] = rowT;
            material->baseColor[2] = 0.0f;
            material->baseColor[3] = 1.0f;
            material->albedoTexture = 0u;
            material->samplerIndex = 0u;
            material->flags = 0u;
            material->_padding = 0u;
        }
    }

    for (U32 stressIndex = 0u; stressIndex < APP_GFX_DEMO_CULL_STRESS_OBJECT_COUNT; ++stressIndex) {
        U32 index = APP_GFX_DEMO_DRAW_COUNT + stressIndex;
        F32 laneX = (F32)(stressIndex & 31u);
        F32 laneY = (F32)((stressIndex >> 5u) & 31u);

        AppGfxDemoObject* object = objects + index;
        object->offset[0] = 2.25f + laneX * 0.035f;
        object->offset[1] = -1.85f + laneY * 0.025f;
        object->scale = APP_GFX_DEMO_OVERLAP_SCALE;
        object->depth = APP_GFX_DEMO_DEPTH_NEAR + 0.5f * APP_GFX_DEMO_DEPTH_RANGE;
        object->phaseOffset = (F32)index * APP_GFX_DEMO_DRAW_PHASE_STEP;
        object->materialIndex = stressIndex % APP_GFX_DEMO_MATERIAL_COUNT;
        object->objectId = index;
        object->flags = ((stressIndex & 3u) == 0u) ?
                        AppGfxDemoObjectFlags_AlphaTest :
                        AppGfxDemoObjectFlags_None;

        gpuObjects[index] = app_gfx_demo_gpu_object_from_object(object);
        cullSources[index] = app_gfx_demo_cull_source_from_object(object, index);
    }

    U32 binCounts[AppGfxDrawBinKind_COUNT] = {};
    for (U32 objectIndex = 0u; objectIndex < APP_GFX_DEMO_OBJECT_COUNT; ++objectIndex) {
        AppGfxDrawBinKind binKind = app_gfx_demo_draw_bin_kind(objects + objectIndex);
        ++binCounts[binKind];
    }

    U32 visibilityGroupCount = 0u;
    for (U32 binIndex = 0u; binIndex < AppGfxDrawBinKind_COUNT; ++binIndex) {
        U32 binGroupCount = (binCounts[binIndex] + APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP - 1u) /
                            APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP;
        ASSERT_ALWAYS(binGroupCount <= APP_GFX_DEMO_VISIBILITY_MAX_GROUPS_PER_BIN);
        visibilityGroupCount += binGroupCount;
    }

    if (visibilityGroups == 0 ||
        state->gfxDemo.renderer.visibilityGroupCount != visibilityGroupCount) {
        visibilityGroups = ARENA_PUSH_ARRAY(state->resources.arena, AppGfxVisibilityGroup, visibilityGroupCount);
        if (visibilityGroups == 0) {
            LOG_ERROR("gfx", "Failed to allocate demo visibility groups");
            return 0;
        }
    }
    MEMSET(visibilityGroups, 0, sizeof(AppGfxVisibilityGroup) * visibilityGroupCount);

    U32 binStarts[AppGfxDrawBinKind_COUNT] = {};
    U32 totalSourceCount = 0u;
    U32 groupStart = 0u;
    for (U32 binIndex = 0u; binIndex < AppGfxDrawBinKind_COUNT; ++binIndex) {
        binStarts[binIndex] = totalSourceCount;
        totalSourceCount += binCounts[binIndex];
        U32 binGroupCount = (binCounts[binIndex] + APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP - 1u) /
                            APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP;

        AppGfxVisibilityBin* bin = visibilityBins + binIndex;
        bin->sourceIndexByteOffset = binStarts[binIndex] * sizeof(U32);
        bin->sourceIndexCount = binCounts[binIndex];
        bin->visibleIndexByteOffset = binStarts[binIndex] * sizeof(U32);
        bin->indirectArgsByteOffset = binIndex * sizeof(GfxDrawIndexedIndirectArgs);
        bin->groupStart = groupStart;
        bin->groupCount = binGroupCount;
        bin->_padding2 = 0u;
        bin->_padding3 = 0u;
        groupStart += binGroupCount;
    }

    ASSERT_ALWAYS(totalSourceCount == APP_GFX_DEMO_OBJECT_COUNT);
    ASSERT_ALWAYS(groupStart == visibilityGroupCount);

    U32 binWrites[AppGfxDrawBinKind_COUNT] = {};
    for (U32 objectIndex = 0u; objectIndex < APP_GFX_DEMO_OBJECT_COUNT; ++objectIndex) {
        AppGfxDrawBinKind binKind = app_gfx_demo_draw_bin_kind(objects + objectIndex);
        U32 sourceIndex = binStarts[binKind] + binWrites[binKind];
        visibilitySourceIndices[sourceIndex] = objectIndex;
        ++binWrites[binKind];
    }

    AppGfxVisibilityBin* transparentBin = visibilityBins + AppGfxDrawBinKind_Transparent;
    if (transparentBin->sourceIndexCount != 0u) {
        U32 transparentStart = transparentBin->sourceIndexByteOffset / sizeof(U32);
        app_gfx_sort_demo_transparent_indices(objects,
                                              visibilitySourceIndices + transparentStart,
                                              transparentBin->sourceIndexCount);
    }

    for (U32 binIndex = 0u; binIndex < AppGfxDrawBinKind_COUNT; ++binIndex) {
        const AppGfxVisibilityBin* bin = visibilityBins + binIndex;
        for (U32 groupIndex = 0u; groupIndex < bin->groupCount; ++groupIndex) {
            U32 globalGroupIndex = bin->groupStart + groupIndex;
            U32 sourceIndexOffset = groupIndex * APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP;
            U32 sourceIndexCount = bin->sourceIndexCount - sourceIndexOffset;
            if (sourceIndexCount > APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP) {
                sourceIndexCount = APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP;
            }

            AppGfxVisibilityGroup* group = visibilityGroups + globalGroupIndex;
            group->sourceIndexByteOffset = bin->sourceIndexByteOffset + sourceIndexOffset * sizeof(U32);
            group->sourceIndexCount = sourceIndexCount;
            group->visibleIndexByteOffset = bin->visibleIndexByteOffset;
            group->binIndex = binIndex;
            group->_padding0 = 0u;
            group->_padding1 = 0u;
            group->_padding2 = 0u;
            group->_padding3 = 0u;
        }
    }

    state->gfxDemo.renderer.objects = objects;
    state->gfxDemo.renderer.objectCount = APP_GFX_DEMO_OBJECT_COUNT;
    state->gfxDemo.renderer.gpuObjects = gpuObjects;
    state->gfxDemo.renderer.gpuObjectCount = APP_GFX_DEMO_OBJECT_COUNT;
    state->gfxDemo.renderer.gpuCullSources = cullSources;
    state->gfxDemo.renderer.gpuCullSourceCount = APP_GFX_DEMO_OBJECT_COUNT;
    state->gfxDemo.renderer.visibilitySourceIndices = visibilitySourceIndices;
    state->gfxDemo.renderer.visibilitySourceIndexCount = APP_GFX_DEMO_OBJECT_COUNT;
    state->gfxDemo.renderer.visibilityBins = visibilityBins;
    state->gfxDemo.renderer.visibilityBinCount = AppGfxDrawBinKind_COUNT;
    state->gfxDemo.renderer.visibilityGroups = visibilityGroups;
    state->gfxDemo.renderer.visibilityGroupCount = visibilityGroupCount;
    state->gfxDemo.renderer.materialSources = materials;
    state->gfxDemo.renderer.materialSourceCount = APP_GFX_DEMO_MATERIAL_COUNT;
    state->gfxDemo.renderer.materialCount = APP_GFX_DEMO_MATERIAL_COUNT;
    state->gfxDemo.renderer.dataVersion = APP_GFX_DEMO_RENDERER_DATA_VERSION;
    state->gfxDemo.upload.objectUploaded = 0;
    state->gfxDemo.upload.objectDirty = 1;
    state->gfxDemo.upload.cullSourceUploaded = 0;
    state->gfxDemo.upload.cullSourceDirty = 1;
    state->gfxDemo.upload.visibilitySourceUploaded = 0;
    state->gfxDemo.upload.visibilitySourceDirty = 1;
    state->gfxDemo.upload.visibilityBinUploaded = 0;
    state->gfxDemo.upload.visibilityBinDirty = 1;
    state->gfxDemo.upload.visibilityGroupUploaded = 0;
    state->gfxDemo.upload.visibilityGroupDirty = 1;
    state->gfxDemo.upload.materialSourceUploaded = 0;
    state->gfxDemo.upload.materialSourceDirty = 1;
    return 1;
}

static void app_gfx_demo_log_once(AppCoreState* state, U32 bit, const char* message) {
    if (state == 0 || message == 0 || FLAGS_HAS(state->gfxDemo.runtime.loadLogMask, bit)) {
        return;
    }

    LOG_INFO("gfx", "{}", str8(message));
    state->gfxDemo.runtime.loadLogMask |= bit;
}

static void app_gfx_try_create_demo_buffers(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemo.runtime.geometryCreated || ctx->host->gfxDevice == 0) {
        return;
    }
    if (state->gfxDemo.renderer.gpuObjectCount == 0u ||
        state->gfxDemo.renderer.gpuObjects == 0 ||
        state->gfxDemo.renderer.gpuCullSourceCount == 0u ||
        state->gfxDemo.renderer.gpuCullSources == 0 ||
        state->gfxDemo.renderer.visibilitySourceIndexCount == 0u ||
        state->gfxDemo.renderer.visibilitySourceIndices == 0 ||
        state->gfxDemo.renderer.visibilityBinCount == 0u ||
        state->gfxDemo.renderer.visibilityBins == 0 ||
        state->gfxDemo.renderer.visibilityGroupCount == 0u ||
        state->gfxDemo.renderer.visibilityGroups == 0 ||
        state->gfxDemo.renderer.materialSourceCount == 0u ||
        state->gfxDemo.renderer.materialSources == 0) {
        return;
    }

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "triangle vertices";
    vertexDesc.size = sizeof(APP_GFX_DEMO_VERTICES);
    vertexDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    vertexDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer vertexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &vertexDesc);
    GfxResourceId vertexBufferId = gfx_register_buffer(ctx->host->gfxDevice, vertexBuffer);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "triangle indices";
    indexDesc.size = sizeof(APP_GFX_DEMO_INDICES);
    indexDesc.usageFlags = GfxBufferUsageFlags_Index | GfxBufferUsageFlags_CopyDst;
    indexDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer indexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &indexDesc);

    GfxBufferDesc objectDesc = {};
    objectDesc.name = "demo objects";
    objectDesc.size = sizeof(AppGfxGpuObject) * state->gfxDemo.renderer.gpuObjectCount;
    objectDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    objectDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer objectBuffer = gfx_create_buffer(ctx->host->gfxDevice, &objectDesc);
    GfxResourceId objectBufferId = gfx_register_buffer(ctx->host->gfxDevice, objectBuffer);

    GfxBufferDesc cullSourceDesc = {};
    cullSourceDesc.name = "demo cull sources";
    cullSourceDesc.size = sizeof(AppGfxGpuCullSource) * state->gfxDemo.renderer.gpuCullSourceCount;
    cullSourceDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    cullSourceDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer cullSourceBuffer = gfx_create_buffer(ctx->host->gfxDevice, &cullSourceDesc);
    GfxResourceId cullSourceBufferId = gfx_register_buffer(ctx->host->gfxDevice, cullSourceBuffer);

    GfxBufferDesc cullObjectDesc = {};
    cullObjectDesc.name = "demo cull objects";
    cullObjectDesc.size = sizeof(AppGfxGpuCullObject) * state->gfxDemo.renderer.gpuCullSourceCount;
    cullObjectDesc.usageFlags = GfxBufferUsageFlags_Storage;
    cullObjectDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer cullObjectBuffer = gfx_create_buffer(ctx->host->gfxDevice, &cullObjectDesc);
    GfxResourceId cullObjectBufferId = gfx_register_buffer(ctx->host->gfxDevice, cullObjectBuffer);

    GfxBufferDesc materialSourceDesc = {};
    materialSourceDesc.name = "demo material sources";
    materialSourceDesc.size = sizeof(AppGfxMaterial) * state->gfxDemo.renderer.materialSourceCount;
    materialSourceDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    materialSourceDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer materialSourceBuffer = gfx_create_buffer(ctx->host->gfxDevice, &materialSourceDesc);
    GfxResourceId materialSourceBufferId = gfx_register_buffer(ctx->host->gfxDevice, materialSourceBuffer);

    GfxBufferDesc materialDesc = {};
    materialDesc.name = "demo materials";
    materialDesc.size = sizeof(AppGfxMaterial) * state->gfxDemo.renderer.materialCount;
    materialDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    materialDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer materialBuffer = gfx_create_buffer(ctx->host->gfxDevice, &materialDesc);
    GfxResourceId materialBufferId = gfx_register_buffer(ctx->host->gfxDevice, materialBuffer);

    GfxBufferDesc visibilitySourceIndexDesc = {};
    visibilitySourceIndexDesc.name = "demo visibility source indices";
    visibilitySourceIndexDesc.size = sizeof(U32) * state->gfxDemo.renderer.visibilitySourceIndexCount;
    visibilitySourceIndexDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    visibilitySourceIndexDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer visibilitySourceIndexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &visibilitySourceIndexDesc);
    GfxResourceId visibilitySourceIndexBufferId = gfx_register_buffer(ctx->host->gfxDevice, visibilitySourceIndexBuffer);

    GfxBufferDesc visibilityBinDesc = {};
    visibilityBinDesc.name = "demo visibility bins";
    visibilityBinDesc.size = sizeof(AppGfxVisibilityBin) * state->gfxDemo.renderer.visibilityBinCount;
    visibilityBinDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    visibilityBinDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer visibilityBinBuffer = gfx_create_buffer(ctx->host->gfxDevice, &visibilityBinDesc);
    GfxResourceId visibilityBinBufferId = gfx_register_buffer(ctx->host->gfxDevice, visibilityBinBuffer);

    GfxBufferDesc visibilityGroupDesc = {};
    visibilityGroupDesc.name = "demo visibility groups";
    visibilityGroupDesc.size = sizeof(AppGfxVisibilityGroup) * state->gfxDemo.renderer.visibilityGroupCount;
    visibilityGroupDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_CopyDst;
    visibilityGroupDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer visibilityGroupBuffer = gfx_create_buffer(ctx->host->gfxDevice, &visibilityGroupDesc);
    GfxResourceId visibilityGroupBufferId = gfx_register_buffer(ctx->host->gfxDevice, visibilityGroupBuffer);

    GfxBufferDesc visibilityGroupCountDesc = {};
    visibilityGroupCountDesc.name = "demo visibility group counts";
    visibilityGroupCountDesc.size = sizeof(U32) * state->gfxDemo.renderer.visibilityGroupCount;
    visibilityGroupCountDesc.usageFlags = GfxBufferUsageFlags_Storage;
    visibilityGroupCountDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer visibilityGroupCountBuffer = gfx_create_buffer(ctx->host->gfxDevice, &visibilityGroupCountDesc);
    GfxResourceId visibilityGroupCountBufferId = gfx_register_buffer(ctx->host->gfxDevice, visibilityGroupCountBuffer);

    GfxBufferDesc visibilityGroupOffsetDesc = {};
    visibilityGroupOffsetDesc.name = "demo visibility group offsets";
    visibilityGroupOffsetDesc.size = sizeof(U32) * state->gfxDemo.renderer.visibilityGroupCount;
    visibilityGroupOffsetDesc.usageFlags = GfxBufferUsageFlags_Storage;
    visibilityGroupOffsetDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer visibilityGroupOffsetBuffer = gfx_create_buffer(ctx->host->gfxDevice, &visibilityGroupOffsetDesc);
    GfxResourceId visibilityGroupOffsetBufferId = gfx_register_buffer(ctx->host->gfxDevice, visibilityGroupOffsetBuffer);

    GfxBufferDesc visibleIndexDesc = {};
    visibleIndexDesc.name = "demo visible indices";
    visibleIndexDesc.size = sizeof(U32) * state->gfxDemo.renderer.visibilitySourceIndexCount;
    visibleIndexDesc.usageFlags = GfxBufferUsageFlags_Storage;
    visibleIndexDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer visibleIndexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &visibleIndexDesc);
    GfxResourceId visibleIndexBufferId = gfx_register_buffer(ctx->host->gfxDevice, visibleIndexBuffer);

    GfxBufferDesc indirectArgsDesc = {};
    indirectArgsDesc.name = "demo indirect args";
    indirectArgsDesc.size = sizeof(GfxDrawIndexedIndirectArgs) * state->gfxDemo.renderer.visibilityBinCount;
    indirectArgsDesc.usageFlags = GfxBufferUsageFlags_Storage | GfxBufferUsageFlags_Indirect;
    indirectArgsDesc.memoryKind = GfxMemoryKind_Device;
    GfxBuffer indirectArgsBuffer = gfx_create_buffer(ctx->host->gfxDevice, &indirectArgsDesc);
    GfxResourceId indirectArgsBufferId = gfx_register_buffer(ctx->host->gfxDevice, indirectArgsBuffer);

    state->gfxDemo.runtime.geometryCreated = vertexBuffer.generation != 0u &&
                                    vertexBufferId.index != 0u &&
                                    indexBuffer.generation != 0u &&
                                    objectBuffer.generation != 0u &&
                                    objectBufferId.index != 0u &&
                                    cullSourceBuffer.generation != 0u &&
                                    cullSourceBufferId.index != 0u &&
                                    cullObjectBuffer.generation != 0u &&
                                    cullObjectBufferId.index != 0u &&
                                    materialSourceBuffer.generation != 0u &&
                                    materialSourceBufferId.index != 0u &&
                                    materialBuffer.generation != 0u &&
                                    materialBufferId.index != 0u &&
                                    visibilitySourceIndexBuffer.generation != 0u &&
                                    visibilitySourceIndexBufferId.index != 0u &&
                                    visibilityBinBuffer.generation != 0u &&
                                    visibilityBinBufferId.index != 0u &&
                                    visibilityGroupBuffer.generation != 0u &&
                                    visibilityGroupBufferId.index != 0u &&
                                    visibilityGroupCountBuffer.generation != 0u &&
                                    visibilityGroupCountBufferId.index != 0u &&
                                    visibilityGroupOffsetBuffer.generation != 0u &&
                                    visibilityGroupOffsetBufferId.index != 0u &&
                                    visibleIndexBuffer.generation != 0u &&
                                    visibleIndexBufferId.index != 0u &&
                                    indirectArgsBuffer.generation != 0u &&
                                    indirectArgsBufferId.index != 0u;
    if (state->gfxDemo.runtime.geometryCreated) {
        state->gfxDemo.gpu.triangleVertexBuffer = vertexBuffer;
        state->gfxDemo.gpu.triangleVertexBufferId = vertexBufferId;
        state->gfxDemo.gpu.triangleIndexBuffer = indexBuffer;
        state->gfxDemo.gpu.objectBuffer = objectBuffer;
        state->gfxDemo.gpu.objectBufferId = objectBufferId;
        state->gfxDemo.gpu.cullSourceBuffer = cullSourceBuffer;
        state->gfxDemo.gpu.cullSourceBufferId = cullSourceBufferId;
        state->gfxDemo.gpu.cullObjectBuffer = cullObjectBuffer;
        state->gfxDemo.gpu.cullObjectBufferId = cullObjectBufferId;
        state->gfxDemo.gpu.materialSourceBuffer = materialSourceBuffer;
        state->gfxDemo.gpu.materialSourceBufferId = materialSourceBufferId;
        state->gfxDemo.gpu.materialBuffer = materialBuffer;
        state->gfxDemo.gpu.materialBufferId = materialBufferId;
        state->gfxDemo.gpu.visibilitySourceIndexBuffer = visibilitySourceIndexBuffer;
        state->gfxDemo.gpu.visibilitySourceIndexBufferId = visibilitySourceIndexBufferId;
        state->gfxDemo.gpu.visibilityBinBuffer = visibilityBinBuffer;
        state->gfxDemo.gpu.visibilityBinBufferId = visibilityBinBufferId;
        state->gfxDemo.gpu.visibilityGroupBuffer = visibilityGroupBuffer;
        state->gfxDemo.gpu.visibilityGroupBufferId = visibilityGroupBufferId;
        state->gfxDemo.gpu.visibilityGroupCountBuffer = visibilityGroupCountBuffer;
        state->gfxDemo.gpu.visibilityGroupCountBufferId = visibilityGroupCountBufferId;
        state->gfxDemo.gpu.visibilityGroupOffsetBuffer = visibilityGroupOffsetBuffer;
        state->gfxDemo.gpu.visibilityGroupOffsetBufferId = visibilityGroupOffsetBufferId;
        state->gfxDemo.gpu.visibleIndexBuffer = visibleIndexBuffer;
        state->gfxDemo.gpu.visibleIndexBufferId = visibleIndexBufferId;
        state->gfxDemo.gpu.indirectArgsBuffer = indirectArgsBuffer;
        state->gfxDemo.gpu.indirectArgsBufferId = indirectArgsBufferId;
        state->gfxDemo.upload.materialSourceDirty = 1;
        state->gfxDemo.upload.materialDirty = 1;
        state->gfxDemo.upload.objectDirty = 1;
        state->gfxDemo.upload.cullSourceDirty = 1;
        state->gfxDemo.upload.visibilitySourceDirty = 1;
        state->gfxDemo.upload.visibilityBinDirty = 1;
        state->gfxDemo.upload.visibilityGroupDirty = 1;
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_GeometryCreated, "Demo GPU buffers created");
    } else {
        gfx_destroy_buffer(ctx->host->gfxDevice, indirectArgsBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, visibleIndexBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, visibilityGroupOffsetBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, visibilityGroupCountBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, visibilityGroupBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, visibilityBinBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, visibilitySourceIndexBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, materialBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, materialSourceBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, cullObjectBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, cullSourceBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, objectBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, indexBuffer);
        gfx_destroy_buffer(ctx->host->gfxDevice, vertexBuffer);
    }
}

static void app_gfx_upload_demo_geometry(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemo.runtime.geometryUploaded || !state->gfxDemo.runtime.geometryCreated || frame == 0) {
        return;
    }

    B32 uploadedVertices = gfx_upload_buffer(frame,
                                             state->gfxDemo.gpu.triangleVertexBuffer,
                                             0u,
                                             APP_GFX_DEMO_VERTICES,
                                             sizeof(APP_GFX_DEMO_VERTICES));
    B32 uploadedIndices = gfx_upload_buffer(frame,
                                            state->gfxDemo.gpu.triangleIndexBuffer,
                                            0u,
                                            APP_GFX_DEMO_INDICES,
                                            sizeof(APP_GFX_DEMO_INDICES));
    state->gfxDemo.runtime.geometryUploaded = uploadedVertices && uploadedIndices;
    if (state->gfxDemo.runtime.geometryUploaded) {
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_GeometryUploaded, "Demo geometry upload recorded");
    }
}

static void app_gfx_upload_demo_objects(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (frame == 0 || !state->gfxDemo.runtime.geometryCreated) {
        return;
    }

    if (state->gfxDemo.upload.objectDirty &&
        state->gfxDemo.renderer.gpuObjects != 0 &&
        state->gfxDemo.renderer.gpuObjectCount != 0u &&
        state->gfxDemo.gpu.objectBuffer.generation != 0u &&
        state->gfxDemo.gpu.objectBufferId.index != 0u) {
        U64 objectBytes = sizeof(AppGfxGpuObject) * state->gfxDemo.renderer.gpuObjectCount;
        state->gfxDemo.upload.objectUploaded = gfx_upload_buffer(frame,
                                                         state->gfxDemo.gpu.objectBuffer,
                                                         0u,
                                                         state->gfxDemo.renderer.gpuObjects,
                                                         objectBytes);
        if (state->gfxDemo.upload.objectUploaded) {
            state->gfxDemo.upload.objectDirty = 0;
        }
    }

    if (state->gfxDemo.upload.cullSourceDirty &&
        state->gfxDemo.renderer.gpuCullSources != 0 &&
        state->gfxDemo.renderer.gpuCullSourceCount != 0u &&
        state->gfxDemo.gpu.cullSourceBuffer.generation != 0u &&
        state->gfxDemo.gpu.cullSourceBufferId.index != 0u) {
        U64 cullSourceBytes = sizeof(AppGfxGpuCullSource) * state->gfxDemo.renderer.gpuCullSourceCount;
        state->gfxDemo.upload.cullSourceUploaded = gfx_upload_buffer(frame,
                                                             state->gfxDemo.gpu.cullSourceBuffer,
                                                             0u,
                                                             state->gfxDemo.renderer.gpuCullSources,
                                                             cullSourceBytes);
        if (state->gfxDemo.upload.cullSourceUploaded) {
            state->gfxDemo.upload.cullSourceDirty = 0;
        }
    }
}

static void app_gfx_upload_demo_visibility_sources(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (frame == 0 || !state->gfxDemo.runtime.geometryCreated) {
        return;
    }

    if (state->gfxDemo.upload.visibilitySourceDirty &&
        state->gfxDemo.renderer.visibilitySourceIndices != 0 &&
        state->gfxDemo.renderer.visibilitySourceIndexCount != 0u &&
        state->gfxDemo.gpu.visibilitySourceIndexBuffer.generation != 0u &&
        state->gfxDemo.gpu.visibilitySourceIndexBufferId.index != 0u) {
        U64 sourceBytes = sizeof(U32) * state->gfxDemo.renderer.visibilitySourceIndexCount;
        state->gfxDemo.upload.visibilitySourceUploaded = gfx_upload_buffer(frame,
                                                                   state->gfxDemo.gpu.visibilitySourceIndexBuffer,
                                                                   0u,
                                                                   state->gfxDemo.renderer.visibilitySourceIndices,
                                                                   sourceBytes);
        if (state->gfxDemo.upload.visibilitySourceUploaded) {
            state->gfxDemo.upload.visibilitySourceDirty = 0;
        }
    }

    if (state->gfxDemo.upload.visibilityBinDirty &&
        state->gfxDemo.renderer.visibilityBins != 0 &&
        state->gfxDemo.renderer.visibilityBinCount != 0u &&
        state->gfxDemo.gpu.visibilityBinBuffer.generation != 0u &&
        state->gfxDemo.gpu.visibilityBinBufferId.index != 0u) {
        U64 binBytes = sizeof(AppGfxVisibilityBin) * state->gfxDemo.renderer.visibilityBinCount;
        state->gfxDemo.upload.visibilityBinUploaded = gfx_upload_buffer(frame,
                                                               state->gfxDemo.gpu.visibilityBinBuffer,
                                                               0u,
                                                               state->gfxDemo.renderer.visibilityBins,
                                                               binBytes);
        if (state->gfxDemo.upload.visibilityBinUploaded) {
            state->gfxDemo.upload.visibilityBinDirty = 0;
        }
    }

    if (state->gfxDemo.upload.visibilityGroupDirty &&
        state->gfxDemo.renderer.visibilityGroups != 0 &&
        state->gfxDemo.renderer.visibilityGroupCount != 0u &&
        state->gfxDemo.gpu.visibilityGroupBuffer.generation != 0u &&
        state->gfxDemo.gpu.visibilityGroupBufferId.index != 0u) {
        U64 groupBytes = sizeof(AppGfxVisibilityGroup) * state->gfxDemo.renderer.visibilityGroupCount;
        state->gfxDemo.upload.visibilityGroupUploaded = gfx_upload_buffer(frame,
                                                                 state->gfxDemo.gpu.visibilityGroupBuffer,
                                                                 0u,
                                                                 state->gfxDemo.renderer.visibilityGroups,
                                                                 groupBytes);
        if (state->gfxDemo.upload.visibilityGroupUploaded) {
            state->gfxDemo.upload.visibilityGroupDirty = 0;
        }
    }
}

static void app_gfx_try_update_triangle_pipelines(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resources.fileStream == 0 || state->resources.artifactCache == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView vertexShaderView = file_view(state->resources.fileStream, state->gfxDemo.shaders.triangleVertex);
    FileView fragmentShaderView = file_view(state->resources.fileStream, state->gfxDemo.shaders.triangleFragment);
    if (vertexShaderView.status != FileStatus_Ready ||
        fragmentShaderView.status != FileStatus_Ready ||
        content_hash_is_zero(vertexShaderView.hash) ||
        content_hash_is_zero(fragmentShaderView.hash)) {
        return;
    }

    for (U32 kindIndex = 0u; kindIndex < AppTrianglePipelineKind_COUNT; ++kindIndex) {
        AppTrianglePipelineKind kind = (AppTrianglePipelineKind)kindIndex;
        ArtifactKey key = app_artifact_key_from_label(app_triangle_pipeline_kind_label(kind));
        key = artifact_key_mix(key, app_artifact_key_from_content("triangle pipeline vertex", vertexShaderView.hash));
        key = artifact_key_mix(key, app_artifact_key_from_content("triangle pipeline fragment", fragmentShaderView.hash));

        ArtifactKey* currentKey = app_triangle_pipeline_artifact_key_slot(state, kind);
        if (artifact_key_equal(key, *currentKey)) {
            artifact_touch(state->resources.artifactCache, AppArtifactTypeId_TrianglePipeline, key, state->frameCounter);
            continue;
        }

        AppTrianglePipelineArtifactData artifactData = {};
        artifactData.vertexHash = vertexShaderView.hash;
        artifactData.fragmentHash = fragmentShaderView.hash;
        artifactData.kind = kind;

        ArtifactResult artifact = artifact_get(state->resources.artifactCache,
                                               AppArtifactTypeId_TrianglePipeline,
                                               key,
                                               1u,
                                               &artifactData,
                                               sizeof(artifactData),
                                               ArtifactGetFlags_HighPriority,
                                               0u);
        if (artifact.status == ArtifactStatus_Ready &&
            !FLAGS_HAS(artifact.flags, ArtifactResultFlags_Stale)) {
            ContentHash artifactVertexHash = {{artifact.value.u64[0], artifact.value.u64[1]}};
            ContentHash artifactFragmentHash = {{artifact.value.u64[2], artifact.value.u64[3]}};
            GfxPipeline newPipeline = {};
            if (!app_gfx_demo_create_triangle_pipeline(ctx,
                                                       artifactVertexHash,
                                                       artifactFragmentHash,
                                                       kind,
                                                       &newPipeline)) {
                continue;
            }

            if (!artifact_retain(state->resources.artifactCache, AppArtifactTypeId_TrianglePipeline, key)) {
                gfx_destroy_pipeline(ctx->host->gfxDevice, newPipeline);
                continue;
            }

            GfxPipeline* currentPipeline = app_triangle_pipeline_slot(state, kind);
            GfxPipeline oldPipeline = *currentPipeline;
            if (!artifact_key_is_zero(*currentKey)) {
                artifact_release(state->resources.artifactCache,
                                 AppArtifactTypeId_TrianglePipeline,
                                 *currentKey);
            }
            if (oldPipeline.generation != 0u) {
                gfx_destroy_pipeline(ctx->host->gfxDevice, oldPipeline);
            }
            *currentPipeline = newPipeline;
            *currentKey = key;
        }
    }

    if (state->gfxDemo.pipelines.triangleOpaque.generation != 0u &&
        state->gfxDemo.pipelines.triangleTransparent.generation != 0u) {
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_TrianglePipeline, "Demo triangle pipelines ready");
    }
}

static void app_gfx_try_update_demo_compute_pipeline(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resources.fileStream == 0 || state->resources.artifactCache == 0 || ctx->host->gfxDevice == 0) {
        return;
    }

    FileView shaderView = file_view(state->resources.fileStream, state->gfxDemo.shaders.materialCompute);
    if (shaderView.status != FileStatus_Ready || content_hash_is_zero(shaderView.hash)) {
        return;
    }

    ArtifactKey key = app_artifact_key_from_content("demo material compute pipeline", shaderView.hash);
    if (artifact_key_equal(key, state->gfxDemo.pipelines.materialComputeArtifactKey)) {
        artifact_touch(state->resources.artifactCache, AppArtifactTypeId_ComputePipeline, key, state->frameCounter);
        return;
    }

    AppComputePipelineArtifactData artifactData = {};
    artifactData.shaderHash = shaderView.hash;
    artifactData.name = "demo material compute pipeline";
    artifactData.entry = APP_SHADER_DEMO_MATERIAL_COMPUTE_ENTRY;
    artifactData.threadsPerThreadgroupX = APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP;
    artifactData.threadsPerThreadgroupY = 1u;
    artifactData.threadsPerThreadgroupZ = 1u;

    ArtifactResult artifact = artifact_get(state->resources.artifactCache,
                                           AppArtifactTypeId_ComputePipeline,
                                           key,
                                           1u,
                                           &artifactData,
                                           sizeof(artifactData),
                                           ArtifactGetFlags_HighPriority,
                                           0u);
    if (artifact.status == ArtifactStatus_Ready &&
        !FLAGS_HAS(artifact.flags, ArtifactResultFlags_Stale)) {
        ContentHash artifactShaderHash = app_content_hash_from_value(artifact.value);
        GfxPipeline newPipeline = {};
        if (!app_gfx_demo_create_compute_pipeline(ctx,
                                                  artifactShaderHash,
                                                  "demo material compute pipeline",
                                                  APP_SHADER_DEMO_MATERIAL_COMPUTE_ENTRY,
                                                  APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP,
                                                  &newPipeline)) {
            return;
        }

        if (!artifact_retain(state->resources.artifactCache, AppArtifactTypeId_ComputePipeline, key)) {
            gfx_destroy_pipeline(ctx->host->gfxDevice, newPipeline);
            return;
        }

        GfxPipeline oldPipeline = state->gfxDemo.pipelines.materialCompute;
        if (!artifact_key_is_zero(state->gfxDemo.pipelines.materialComputeArtifactKey)) {
            artifact_release(state->resources.artifactCache,
                             AppArtifactTypeId_ComputePipeline,
                             state->gfxDemo.pipelines.materialComputeArtifactKey);
        }
        if (oldPipeline.generation != 0u) {
            gfx_destroy_pipeline(ctx->host->gfxDevice, oldPipeline);
        }
        state->gfxDemo.pipelines.materialCompute = newPipeline;
        state->gfxDemo.pipelines.materialComputeArtifactKey = key;
        state->gfxDemo.upload.materialDirty = 1;
        if (state->gfxDemo.pipelines.cullBoundsCompute.generation != 0u &&
            state->gfxDemo.pipelines.visibilityCountCompute.generation != 0u &&
            state->gfxDemo.pipelines.visibilityPrefixCompute.generation != 0u &&
            state->gfxDemo.pipelines.visibilityCompactCompute.generation != 0u) {
            app_gfx_demo_log_once(state, AppGfxDemoLoadLog_ComputePipeline, "Demo compute pipelines ready");
        }
    }
}

static B32 app_gfx_try_update_demo_gpu_data_pipeline_(APP_Context* ctx,
                                                     FileHandle shader,
                                                     ArtifactKey* currentKey,
                                                     GfxPipeline* currentPipeline,
                                                     const char* label,
                                                     const char* name,
                                                     const char* entry,
                                                     U32 threadsPerThreadgroupX) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(currentKey != 0);
    ASSERT_ALWAYS(currentPipeline != 0);
    ASSERT_ALWAYS(label != 0);
    ASSERT_ALWAYS(name != 0);
    ASSERT_ALWAYS(entry != 0);
    ASSERT_ALWAYS(threadsPerThreadgroupX != 0u);

    AppCoreState* state = ctx->core;
    if (state->resources.fileStream == 0 || state->resources.artifactCache == 0 || ctx->host->gfxDevice == 0) {
        return 0;
    }

    FileView shaderView = file_view(state->resources.fileStream, shader);
    if (shaderView.status != FileStatus_Ready || content_hash_is_zero(shaderView.hash)) {
        return 0;
    }

    ArtifactKey key = app_artifact_key_from_content(label, shaderView.hash);
    if (artifact_key_equal(key, *currentKey)) {
        artifact_touch(state->resources.artifactCache, AppArtifactTypeId_ComputePipeline, key, state->frameCounter);
        return currentPipeline->generation != 0u;
    }

    AppComputePipelineArtifactData artifactData = {};
    artifactData.shaderHash = shaderView.hash;
    artifactData.name = name;
    artifactData.entry = entry;
    artifactData.threadsPerThreadgroupX = threadsPerThreadgroupX;
    artifactData.threadsPerThreadgroupY = 1u;
    artifactData.threadsPerThreadgroupZ = 1u;

    ArtifactResult artifact = artifact_get(state->resources.artifactCache,
                                           AppArtifactTypeId_ComputePipeline,
                                           key,
                                           1u,
                                           &artifactData,
                                           sizeof(artifactData),
                                           ArtifactGetFlags_HighPriority,
                                           0u);
    if (artifact.status == ArtifactStatus_Ready &&
        !FLAGS_HAS(artifact.flags, ArtifactResultFlags_Stale)) {
        ContentHash artifactShaderHash = app_content_hash_from_value(artifact.value);
        GfxPipeline newPipeline = {};
        if (!app_gfx_demo_create_compute_pipeline(ctx,
                                                  artifactShaderHash,
                                                  name,
                                                  entry,
                                                  threadsPerThreadgroupX,
                                                  &newPipeline)) {
            return currentPipeline->generation != 0u;
        }

        if (!artifact_retain(state->resources.artifactCache, AppArtifactTypeId_ComputePipeline, key)) {
            gfx_destroy_pipeline(ctx->host->gfxDevice, newPipeline);
            return currentPipeline->generation != 0u;
        }

        GfxPipeline oldPipeline = *currentPipeline;
        if (!artifact_key_is_zero(*currentKey)) {
            artifact_release(state->resources.artifactCache,
                             AppArtifactTypeId_ComputePipeline,
                             *currentKey);
        }
        if (oldPipeline.generation != 0u) {
            gfx_destroy_pipeline(ctx->host->gfxDevice, oldPipeline);
        }
        *currentPipeline = newPipeline;
        *currentKey = key;
    }

    return currentPipeline->generation != 0u;
}

static void app_gfx_try_update_demo_gpu_data_pipelines(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    B32 cullBoundsReady = app_gfx_try_update_demo_gpu_data_pipeline_(ctx,
                                                                    state->gfxDemo.shaders.cullBoundsCompute,
                                                                    &state->gfxDemo.pipelines.cullBoundsComputeArtifactKey,
                                                                    &state->gfxDemo.pipelines.cullBoundsCompute,
                                                                    "demo cull bounds compute pipeline",
                                                                    "demo cull bounds compute pipeline",
                                                                    APP_SHADER_DEMO_CULL_BOUNDS_COMPUTE_ENTRY,
                                                                    APP_GFX_DEMO_CULL_BOUNDS_THREADS_PER_GROUP);
    B32 countReady = app_gfx_try_update_demo_gpu_data_pipeline_(ctx,
                                                               state->gfxDemo.shaders.visibilityCountCompute,
                                                               &state->gfxDemo.pipelines.visibilityCountComputeArtifactKey,
                                                               &state->gfxDemo.pipelines.visibilityCountCompute,
                                                               "demo visibility count compute pipeline",
                                                               "demo visibility count compute pipeline",
                                                               APP_SHADER_DEMO_VISIBILITY_COUNT_COMPUTE_ENTRY,
                                                               APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP);
    B32 prefixReady = app_gfx_try_update_demo_gpu_data_pipeline_(ctx,
                                                                state->gfxDemo.shaders.visibilityPrefixCompute,
                                                                &state->gfxDemo.pipelines.visibilityPrefixComputeArtifactKey,
                                                                &state->gfxDemo.pipelines.visibilityPrefixCompute,
                                                                "demo visibility prefix compute pipeline",
                                                                "demo visibility prefix compute pipeline",
                                                                APP_SHADER_DEMO_VISIBILITY_PREFIX_COMPUTE_ENTRY,
                                                                APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP);
    B32 compactReady = app_gfx_try_update_demo_gpu_data_pipeline_(ctx,
                                                                 state->gfxDemo.shaders.visibilityCompactCompute,
                                                                 &state->gfxDemo.pipelines.visibilityCompactComputeArtifactKey,
                                                                 &state->gfxDemo.pipelines.visibilityCompactCompute,
                                                                 "demo visibility compact compute pipeline",
                                                                 "demo visibility compact compute pipeline",
                                                                 APP_SHADER_DEMO_VISIBILITY_COMPACT_COMPUTE_ENTRY,
                                                                 APP_GFX_DEMO_VISIBILITY_THREADS_PER_GROUP);
    if (state->gfxDemo.pipelines.materialCompute.generation != 0u &&
        cullBoundsReady &&
        countReady &&
        prefixReady &&
        compactReady) {
        app_gfx_demo_log_once(state, AppGfxDemoLoadLog_ComputePipeline, "Demo compute pipelines ready");
    }
}

static void app_gfx_upload_demo_texture(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->resources.fileStream == 0 ||
        state->resources.artifactCache == 0 ||
        state->resources.contentStore == 0 ||
        ctx->host->gfxDevice == 0 ||
        frame == 0) {
        return;
    }

    if (state->gfxDemo.gpu.sampler.generation == 0u) {
        GfxSamplerDesc samplerDesc = {};
        samplerDesc.name = "demo sampler";
        samplerDesc.minFilter = GfxFilter_Nearest;
        samplerDesc.magFilter = GfxFilter_Nearest;
        samplerDesc.addressU = GfxAddressMode_Repeat;
        samplerDesc.addressV = GfxAddressMode_Repeat;
        state->gfxDemo.gpu.sampler = gfx_create_sampler(ctx->host->gfxDevice, &samplerDesc);
    }
    if (state->gfxDemo.gpu.samplerId.index == 0u) {
        GfxResourceId samplerId = gfx_register_sampler(ctx->host->gfxDevice, state->gfxDemo.gpu.sampler);
        if (samplerId.index != 0u) {
            state->gfxDemo.gpu.samplerId = samplerId;
            state->gfxDemo.upload.materialSourceDirty = 1;
        }
    }

    FileView textureView = file_view(state->resources.fileStream, state->gfxDemo.shaders.textureSource);
    if (textureView.status != FileStatus_Ready || content_hash_is_zero(textureView.hash)) {
        return;
    }

    ArtifactKey key = app_artifact_key_from_content("decoded demo texture", textureView.hash);
    if (artifact_key_equal(key, state->gfxDemo.upload.textureDecodeArtifactKey) &&
        state->gfxDemo.upload.textureUploaded) {
        artifact_touch(state->resources.artifactCache, AppArtifactTypeId_DecodedTexture, key, state->frameCounter);
        return;
    }

    AppDecodedTextureArtifactData artifactData = {};
    artifactData.sourceHash = textureView.hash;
    ArtifactResult artifact = artifact_get(state->resources.artifactCache,
                                           AppArtifactTypeId_DecodedTexture,
                                           key,
                                           1u,
                                           &artifactData,
                                           sizeof(artifactData),
                                           ArtifactGetFlags_None,
                                           0u);
    if (artifact.status != ArtifactStatus_Ready) {
        if (FLAGS_HAS(artifact.flags, ArtifactResultFlags_ErrorCached)) {
            state->gfxDemo.upload.textureFailedGeneration = textureView.generation;
        }
        return;
    }

    ContentHash decodedHash = app_content_hash_from_value(artifact.value);
    ContentView decodedContent = content_view_hash(state->resources.contentStore, decodedHash);
    if (!decodedContent.valid || decodedContent.size < sizeof(AppDecodedImageHeader)) {
        state->gfxDemo.upload.textureFailedGeneration = textureView.generation;
        return;
    }

    const AppDecodedImageHeader* image = (const AppDecodedImageHeader*)decodedContent.data;
    const U8* pixels = decodedContent.data + sizeof(AppDecodedImageHeader);
    U64 pixelBytes = image->bytesPerRow * image->height;
    if (decodedContent.size < sizeof(AppDecodedImageHeader) + pixelBytes) {
        state->gfxDemo.upload.textureFailedGeneration = textureView.generation;
        return;
    }

    GfxTextureDesc textureDesc = {};
    textureDesc.name = "demo texture";
    textureDesc.width = image->width;
    textureDesc.height = image->height;
    textureDesc.mipCount = 1u;
    textureDesc.format = GfxFormat_RGBA8_UNorm;
    textureDesc.usageFlags = GfxTextureUsageFlags_Sampled | GfxTextureUsageFlags_CopyDst;
    textureDesc.storageKind = GfxTextureStorageKind_Device;
    GfxTexture newTexture = gfx_create_texture(ctx->host->gfxDevice, &textureDesc);
    GfxResourceId newTextureId = gfx_register_texture(ctx->host->gfxDevice, newTexture);
    if (newTextureId.index == 0u) {
        gfx_destroy_texture(ctx->host->gfxDevice, newTexture);
        state->gfxDemo.upload.textureFailedGeneration = textureView.generation;
        return;
    }

    GfxTextureUploadRegion region = {};
    region.layerCount = 1u;
    region.width = image->width;
    region.height = image->height;
    region.depth = 1u;
    region.bytesPerRow = image->bytesPerRow;
    region.rowsPerImage = image->height;

    B32 uploaded = gfx_upload_texture(frame, newTexture, &region, pixels);
    if (!uploaded) {
        gfx_destroy_texture(ctx->host->gfxDevice, newTexture);
        state->gfxDemo.upload.textureFailedGeneration = textureView.generation;
        return;
    }

    GfxTexture oldTexture = state->gfxDemo.gpu.texture;
    state->gfxDemo.gpu.texture = newTexture;
    state->gfxDemo.gpu.textureId = newTextureId;
    state->gfxDemo.upload.textureGeneration = textureView.generation;
    state->gfxDemo.upload.textureFailedGeneration = 0u;
    state->gfxDemo.upload.textureDecodeArtifactKey = key;
    state->gfxDemo.upload.decodedTextureHash = decodedHash;
    state->gfxDemo.upload.textureUploaded = 1;
    state->gfxDemo.upload.materialSourceDirty = 1;
    state->gfxDemo.upload.materialDirty = 1;
    artifact_touch(state->resources.artifactCache, AppArtifactTypeId_DecodedTexture, key, state->frameCounter);
    gfx_destroy_texture(ctx->host->gfxDevice, oldTexture);
    app_gfx_demo_log_once(state, AppGfxDemoLoadLog_TextureUploaded, "Demo texture upload recorded");
}

static void app_gfx_upload_demo_material_sources(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (frame == 0 ||
        !state->gfxDemo.upload.materialSourceDirty ||
        state->gfxDemo.renderer.materialSources == 0 ||
        state->gfxDemo.renderer.materialSourceCount == 0u ||
        state->gfxDemo.gpu.materialSourceBuffer.generation == 0u ||
        state->gfxDemo.gpu.textureId.index == 0u ||
        state->gfxDemo.gpu.samplerId.index == 0u) {
        return;
    }

    for (U32 materialIndex = 0u; materialIndex < state->gfxDemo.renderer.materialSourceCount; ++materialIndex) {
        AppGfxMaterial* material = state->gfxDemo.renderer.materialSources + materialIndex;
        material->albedoTexture = state->gfxDemo.gpu.textureId.index;
        material->samplerIndex = state->gfxDemo.gpu.samplerId.index;
    }

    U64 materialBytes = sizeof(AppGfxMaterial) * state->gfxDemo.renderer.materialSourceCount;
    B32 uploaded = gfx_upload_buffer(frame,
                                     state->gfxDemo.gpu.materialSourceBuffer,
                                     0u,
                                     state->gfxDemo.renderer.materialSources,
                                     materialBytes);
    if (!uploaded) {
        return;
    }

    state->gfxDemo.upload.materialSourceUploaded = 1;
    state->gfxDemo.upload.materialSourceDirty = 0;
    state->gfxDemo.upload.materialDirty = 1;
}

static GfxResourceUse app_gfx_buffer_resource_use(GfxBuffer buffer, U32 accessFlags, U32 shaderStages) {
    GfxResourceUse result = {};
    result.kind = GfxResourceUseKind_Buffer;
    result.accessFlags = accessFlags;
    result.shaderStages = shaderStages;
    result.buffer = buffer;
    return result;
}

static GfxResourceUse app_gfx_texture_resource_use(GfxTexture texture, U32 accessFlags, U32 shaderStages) {
    GfxResourceUse result = {};
    result.kind = GfxResourceUseKind_Texture;
    result.accessFlags = accessFlags;
    result.shaderStages = shaderStages;
    result.texture = texture;
    return result;
}

static B32 app_gfx_build_demo_material_compute_packet(APP_Context* ctx, GfxFrame* frame, AppGfxDemoComputePacket* outPacket) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPacket != 0);

    MEMSET(outPacket, 0, sizeof(*outPacket));
    AppCoreState* state = ctx->core;
    if (state->gfxDemo.renderer.materialCount == 0u || frame == 0) {
        return 0;
    }

    if (state->gfxDemo.pipelines.materialCompute.generation == 0u ||
        state->gfxDemo.gpu.materialSourceBuffer.generation == 0u ||
        state->gfxDemo.gpu.materialSourceBufferId.index == 0u ||
        !state->gfxDemo.upload.materialSourceUploaded ||
        state->gfxDemo.gpu.materialBuffer.generation == 0u ||
        state->gfxDemo.gpu.materialBufferId.index == 0u) {
        return 0;
    }

    GfxTemp dispatchTemp = gfx_allocate_temp(frame, sizeof(AppGfxMaterialComputeRootData), 16u);
    if (dispatchTemp.cpu == 0) {
        return 0;
    }

    AppGfxMaterialComputeRootData* rootData = (AppGfxMaterialComputeRootData*)dispatchTemp.cpu;
    rootData->materialCount = state->gfxDemo.renderer.materialCount;
    rootData->sourceMaterialBuffer = state->gfxDemo.gpu.materialSourceBufferId.index;
    rootData->sourceMaterialByteOffset = 0u;
    rootData->materialBuffer = state->gfxDemo.gpu.materialBufferId.index;
    rootData->materialByteOffset = 0u;
    rootData->animationPhase = state->gfxDemo.renderer.animationSeconds;
    rootData->_padding0 = 0u;
    rootData->_padding1 = 0u;

    outPacket->writes[0].slice.buffer = state->gfxDemo.gpu.materialBuffer;
    outPacket->writes[0].slice.size = sizeof(AppGfxMaterial) * state->gfxDemo.renderer.materialCount;
    outPacket->resourceUses[0] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.materialSourceBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[1] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.materialBuffer,
                                                             GfxResourceAccessFlags_ShaderWrite,
                                                             GfxShaderStageFlags_Compute);
    outPacket->pass.name = "demo material compute pass";
    outPacket->pass.writes = outPacket->writes;
    outPacket->pass.writeCount = 1u;
    outPacket->pass.resourceUses = outPacket->resourceUses;
    outPacket->pass.resourceUseCount = 2u;
    outPacket->dispatch.pipeline = state->gfxDemo.pipelines.materialCompute;
    outPacket->dispatch.rootData = dispatchTemp.gpu;
    outPacket->dispatch.groupsX = (state->gfxDemo.renderer.materialCount + APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP - 1u) /
                                  APP_GFX_DEMO_COMPUTE_THREADS_PER_GROUP;
    outPacket->dispatch.groupsY = 1u;
    outPacket->dispatch.groupsZ = 1u;
    return 1;
}

static B32 app_gfx_dispatch_demo_materials(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (commands == 0) {
        return 0;
    }

    AppGfxDemoComputePacket packet = {};
    if (!app_gfx_build_demo_material_compute_packet(ctx, frame, &packet)) {
        return 0;
    }

    gfx_compute_pass(commands, &packet.pass, &packet.dispatch, 1u);
    ctx->core->gfxDemo.upload.materialsReady = 1;
    ctx->core->gfxDemo.upload.materialDirty = 0;
    return 1;
}

static B32 app_gfx_build_demo_cull_bounds_compute_packet(APP_Context* ctx, GfxFrame* frame, AppGfxDemoComputePacket* outPacket) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPacket != 0);

    MEMSET(outPacket, 0, sizeof(*outPacket));
    AppCoreState* state = ctx->core;
    if (frame == 0 || state->gfxDemo.renderer.gpuCullSourceCount == 0u) {
        return 0;
    }

    if (state->gfxDemo.pipelines.cullBoundsCompute.generation == 0u ||
        state->gfxDemo.gpu.cullSourceBuffer.generation == 0u ||
        state->gfxDemo.gpu.cullSourceBufferId.index == 0u ||
        !state->gfxDemo.upload.cullSourceUploaded ||
        state->gfxDemo.upload.cullSourceDirty ||
        state->gfxDemo.gpu.cullObjectBuffer.generation == 0u ||
        state->gfxDemo.gpu.cullObjectBufferId.index == 0u) {
        return 0;
    }

    GfxTemp dispatchTemp = gfx_allocate_temp(frame, sizeof(AppGfxCullBoundsComputeRootData), 16u);
    if (dispatchTemp.cpu == 0) {
        return 0;
    }

    AppGfxCullBoundsComputeRootData* rootData = (AppGfxCullBoundsComputeRootData*)dispatchTemp.cpu;
    rootData->cullSourceCount = state->gfxDemo.renderer.gpuCullSourceCount;
    rootData->cullSourceBuffer = state->gfxDemo.gpu.cullSourceBufferId.index;
    rootData->cullSourceByteOffset = 0u;
    rootData->cullObjectBuffer = state->gfxDemo.gpu.cullObjectBufferId.index;
    rootData->cullObjectByteOffset = 0u;
    rootData->_padding0 = 0u;
    rootData->_padding1 = 0u;
    rootData->_padding2 = 0u;

    outPacket->writes[0].slice.buffer = state->gfxDemo.gpu.cullObjectBuffer;
    outPacket->writes[0].slice.size = sizeof(AppGfxGpuCullObject) * state->gfxDemo.renderer.gpuCullSourceCount;
    outPacket->resourceUses[0] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.cullSourceBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[1] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.cullObjectBuffer,
                                                             GfxResourceAccessFlags_ShaderWrite,
                                                             GfxShaderStageFlags_Compute);
    outPacket->pass.name = "demo cull bounds compute pass";
    outPacket->pass.writes = outPacket->writes;
    outPacket->pass.writeCount = 1u;
    outPacket->pass.resourceUses = outPacket->resourceUses;
    outPacket->pass.resourceUseCount = 2u;
    outPacket->dispatch.pipeline = state->gfxDemo.pipelines.cullBoundsCompute;
    outPacket->dispatch.rootData = dispatchTemp.gpu;
    outPacket->dispatch.groupsX = (state->gfxDemo.renderer.gpuCullSourceCount + APP_GFX_DEMO_CULL_BOUNDS_THREADS_PER_GROUP - 1u) /
                                  APP_GFX_DEMO_CULL_BOUNDS_THREADS_PER_GROUP;
    outPacket->dispatch.groupsY = 1u;
    outPacket->dispatch.groupsZ = 1u;
    return 1;
}

static B32 app_gfx_dispatch_demo_cull_bounds(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (commands == 0) {
        return 0;
    }

    AppGfxDemoComputePacket packet = {};
    if (!app_gfx_build_demo_cull_bounds_compute_packet(ctx, frame, &packet)) {
        return 0;
    }

    gfx_compute_pass(commands, &packet.pass, &packet.dispatch, 1u);
    return 1;
}

static void app_gfx_write_demo_visibility_root_data(AppCoreState* state, AppGfxVisibilityComputeRootData* rootData) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(rootData != 0);

    rootData->binCount = state->gfxDemo.renderer.visibilityBinCount;
    rootData->groupCount = state->gfxDemo.renderer.visibilityGroupCount;
    rootData->cullObjectBuffer = state->gfxDemo.gpu.cullObjectBufferId.index;
    rootData->cullObjectByteOffset = 0u;
    rootData->sourceIndexBuffer = state->gfxDemo.gpu.visibilitySourceIndexBufferId.index;
    rootData->sourceIndexByteOffset = 0u;
    rootData->visibleIndexBuffer = state->gfxDemo.gpu.visibleIndexBufferId.index;
    rootData->visibleIndexByteOffset = 0u;
    rootData->indirectArgsBuffer = state->gfxDemo.gpu.indirectArgsBufferId.index;
    rootData->indirectArgsByteOffset = 0u;
    rootData->binBuffer = state->gfxDemo.gpu.visibilityBinBufferId.index;
    rootData->binByteOffset = 0u;
    rootData->groupBuffer = state->gfxDemo.gpu.visibilityGroupBufferId.index;
    rootData->groupByteOffset = 0u;
    rootData->groupCountBuffer = state->gfxDemo.gpu.visibilityGroupCountBufferId.index;
    rootData->groupCountByteOffset = 0u;
    rootData->groupOffsetBuffer = state->gfxDemo.gpu.visibilityGroupOffsetBufferId.index;
    rootData->groupOffsetByteOffset = 0u;
    rootData->_padding0 = 0u;
    rootData->_padding1 = 0u;
    rootData->_padding2 = 0u;
    rootData->_padding3 = 0u;
    rootData->_padding4 = 0u;
    rootData->_padding5 = 0u;
}

static B32 app_gfx_build_demo_visibility_count_packet(APP_Context* ctx, GfxTemp rootTemp, AppGfxDemoComputePacket* outPacket) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPacket != 0);

    MEMSET(outPacket, 0, sizeof(*outPacket));
    AppCoreState* state = ctx->core;
    outPacket->writes[0].slice.buffer = state->gfxDemo.gpu.visibilityGroupCountBuffer;
    outPacket->writes[0].slice.size = sizeof(U32) * state->gfxDemo.renderer.visibilityGroupCount;
    outPacket->resourceUses[0] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.cullObjectBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[1] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilitySourceIndexBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[2] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityGroupBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[3] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityGroupCountBuffer,
                                                             GfxResourceAccessFlags_ShaderWrite,
                                                             GfxShaderStageFlags_Compute);
    outPacket->pass.name = "demo visibility count pass";
    outPacket->pass.writes = outPacket->writes;
    outPacket->pass.writeCount = 1u;
    outPacket->pass.resourceUses = outPacket->resourceUses;
    outPacket->pass.resourceUseCount = 4u;
    outPacket->dispatch.pipeline = state->gfxDemo.pipelines.visibilityCountCompute;
    outPacket->dispatch.rootData = rootTemp.gpu;
    outPacket->dispatch.groupsX = state->gfxDemo.renderer.visibilityGroupCount;
    outPacket->dispatch.groupsY = 1u;
    outPacket->dispatch.groupsZ = 1u;
    return 1;
}

static B32 app_gfx_build_demo_visibility_prefix_packet(APP_Context* ctx, GfxTemp rootTemp, AppGfxDemoComputePacket* outPacket) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPacket != 0);

    MEMSET(outPacket, 0, sizeof(*outPacket));
    AppCoreState* state = ctx->core;
    outPacket->writes[0].slice.buffer = state->gfxDemo.gpu.visibilityGroupOffsetBuffer;
    outPacket->writes[0].slice.size = sizeof(U32) * state->gfxDemo.renderer.visibilityGroupCount;
    outPacket->writes[1].slice.buffer = state->gfxDemo.gpu.indirectArgsBuffer;
    outPacket->writes[1].slice.size = sizeof(GfxDrawIndexedIndirectArgs) * state->gfxDemo.renderer.visibilityBinCount;
    outPacket->resourceUses[0] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityBinBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[1] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityGroupCountBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[2] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityGroupOffsetBuffer,
                                                             GfxResourceAccessFlags_ShaderWrite,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[3] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.indirectArgsBuffer,
                                                             GfxResourceAccessFlags_ShaderWrite,
                                                             GfxShaderStageFlags_Compute);
    outPacket->pass.name = "demo visibility prefix pass";
    outPacket->pass.writes = outPacket->writes;
    outPacket->pass.writeCount = 2u;
    outPacket->pass.resourceUses = outPacket->resourceUses;
    outPacket->pass.resourceUseCount = 4u;
    outPacket->dispatch.pipeline = state->gfxDemo.pipelines.visibilityPrefixCompute;
    outPacket->dispatch.rootData = rootTemp.gpu;
    outPacket->dispatch.groupsX = state->gfxDemo.renderer.visibilityBinCount;
    outPacket->dispatch.groupsY = 1u;
    outPacket->dispatch.groupsZ = 1u;
    return 1;
}

static B32 app_gfx_build_demo_visibility_compact_packet(APP_Context* ctx, GfxTemp rootTemp, AppGfxDemoComputePacket* outPacket) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outPacket != 0);

    MEMSET(outPacket, 0, sizeof(*outPacket));
    AppCoreState* state = ctx->core;
    outPacket->writes[0].slice.buffer = state->gfxDemo.gpu.visibleIndexBuffer;
    outPacket->writes[0].slice.size = sizeof(U32) * state->gfxDemo.renderer.visibilitySourceIndexCount;
    outPacket->resourceUses[0] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.cullObjectBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[1] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilitySourceIndexBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[2] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityGroupBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[3] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibilityGroupOffsetBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Compute);
    outPacket->resourceUses[4] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibleIndexBuffer,
                                                             GfxResourceAccessFlags_ShaderWrite,
                                                             GfxShaderStageFlags_Compute);
    outPacket->pass.name = "demo visibility compact pass";
    outPacket->pass.writes = outPacket->writes;
    outPacket->pass.writeCount = 1u;
    outPacket->pass.resourceUses = outPacket->resourceUses;
    outPacket->pass.resourceUseCount = 5u;
    outPacket->dispatch.pipeline = state->gfxDemo.pipelines.visibilityCompactCompute;
    outPacket->dispatch.rootData = rootTemp.gpu;
    outPacket->dispatch.groupsX = state->gfxDemo.renderer.visibilityGroupCount;
    outPacket->dispatch.groupsY = 1u;
    outPacket->dispatch.groupsZ = 1u;
    return 1;
}

static B32 app_gfx_dispatch_demo_visibility(APP_Context* ctx, GfxCommandBuffer* commands, GfxFrame* frame, B32 cullBoundsReady) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (commands == 0 ||
        frame == 0 ||
        !cullBoundsReady ||
        state->gfxDemo.renderer.visibilityBinCount == 0u ||
        state->gfxDemo.renderer.visibilityGroupCount == 0u ||
        state->gfxDemo.renderer.visibilitySourceIndexCount == 0u) {
        return 0;
    }

    if (state->gfxDemo.pipelines.visibilityCountCompute.generation == 0u ||
        state->gfxDemo.pipelines.visibilityPrefixCompute.generation == 0u ||
        state->gfxDemo.pipelines.visibilityCompactCompute.generation == 0u ||
        state->gfxDemo.gpu.cullObjectBuffer.generation == 0u ||
        state->gfxDemo.gpu.cullObjectBufferId.index == 0u ||
        state->gfxDemo.gpu.visibilitySourceIndexBuffer.generation == 0u ||
        state->gfxDemo.gpu.visibilitySourceIndexBufferId.index == 0u ||
        !state->gfxDemo.upload.visibilitySourceUploaded ||
        state->gfxDemo.upload.visibilitySourceDirty ||
        state->gfxDemo.gpu.visibilityBinBuffer.generation == 0u ||
        state->gfxDemo.gpu.visibilityBinBufferId.index == 0u ||
        !state->gfxDemo.upload.visibilityBinUploaded ||
        state->gfxDemo.upload.visibilityBinDirty ||
        state->gfxDemo.gpu.visibilityGroupBuffer.generation == 0u ||
        state->gfxDemo.gpu.visibilityGroupBufferId.index == 0u ||
        !state->gfxDemo.upload.visibilityGroupUploaded ||
        state->gfxDemo.upload.visibilityGroupDirty ||
        state->gfxDemo.gpu.visibilityGroupCountBuffer.generation == 0u ||
        state->gfxDemo.gpu.visibilityGroupCountBufferId.index == 0u ||
        state->gfxDemo.gpu.visibilityGroupOffsetBuffer.generation == 0u ||
        state->gfxDemo.gpu.visibilityGroupOffsetBufferId.index == 0u ||
        state->gfxDemo.gpu.visibleIndexBuffer.generation == 0u ||
        state->gfxDemo.gpu.visibleIndexBufferId.index == 0u ||
        state->gfxDemo.gpu.indirectArgsBuffer.generation == 0u ||
        state->gfxDemo.gpu.indirectArgsBufferId.index == 0u) {
        return 0;
    }

    GfxTemp dispatchTemp = gfx_allocate_temp(frame, sizeof(AppGfxVisibilityComputeRootData), 16u);
    if (dispatchTemp.cpu == 0) {
        return 0;
    }
    app_gfx_write_demo_visibility_root_data(state, (AppGfxVisibilityComputeRootData*)dispatchTemp.cpu);

    AppGfxDemoComputePacket countPacket = {};
    AppGfxDemoComputePacket prefixPacket = {};
    AppGfxDemoComputePacket compactPacket = {};
    if (!app_gfx_build_demo_visibility_count_packet(ctx, dispatchTemp, &countPacket) ||
        !app_gfx_build_demo_visibility_prefix_packet(ctx, dispatchTemp, &prefixPacket) ||
        !app_gfx_build_demo_visibility_compact_packet(ctx, dispatchTemp, &compactPacket)) {
        return 0;
    }

    gfx_compute_pass(commands, &countPacket.pass, &countPacket.dispatch, 1u);
    gfx_compute_pass(commands, &prefixPacket.pass, &prefixPacket.dispatch, 1u);
    gfx_compute_pass(commands, &compactPacket.pass, &compactPacket.dispatch, 1u);
    return 1;
}

static void app_gfx_destroy_demo_targets(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    if (device != 0) {
        gfx_destroy_texture(device, state->gfxDemo.gpu.depth);
        gfx_destroy_texture(device, state->gfxDemo.gpu.offscreenColor);
    }

    state->gfxDemo.gpu.depth = {};
    state->gfxDemo.gpu.offscreenColor = {};
    state->gfxDemo.gpu.targetWidth = 0u;
    state->gfxDemo.gpu.targetHeight = 0u;
}

static B32 app_gfx_ensure_demo_targets(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host->gfxDevice;
    U32 width = ctx->host->windowWidth;
    U32 height = ctx->host->windowHeight;
    if (device == 0 || width == 0u || height == 0u) {
        return 0;
    }

    if (state->gfxDemo.gpu.targetWidth == width &&
        state->gfxDemo.gpu.targetHeight == height &&
        state->gfxDemo.gpu.offscreenColor.generation != 0u &&
        state->gfxDemo.gpu.depth.generation != 0u) {
        return 1;
    }

    GfxTextureDesc colorDesc = {};
    colorDesc.name = "demo offscreen color";
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.mipCount = 1u;
    colorDesc.format = GfxFormat_BGRA8_UNorm;
    colorDesc.usageFlags = GfxTextureUsageFlags_ColorTarget;
    colorDesc.storageKind = GfxTextureStorageKind_Device;
    GfxTexture color = gfx_create_texture(device, &colorDesc);

    GfxTextureDesc depthDesc = {};
    depthDesc.name = "demo depth";
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.mipCount = 1u;
    depthDesc.format = GfxFormat_D32_Float;
    depthDesc.usageFlags = GfxTextureUsageFlags_DepthTarget;
    depthDesc.storageKind = GfxTextureStorageKind_Transient;
    GfxTexture depth = gfx_create_texture(device, &depthDesc);

    if (color.generation == 0u || depth.generation == 0u) {
        gfx_destroy_texture(device, depth);
        gfx_destroy_texture(device, color);
        return 0;
    }

    GfxTexture oldColor = state->gfxDemo.gpu.offscreenColor;
    GfxTexture oldDepth = state->gfxDemo.gpu.depth;
    state->gfxDemo.gpu.offscreenColor = color;
    state->gfxDemo.gpu.depth = depth;
    state->gfxDemo.gpu.targetWidth = width;
    state->gfxDemo.gpu.targetHeight = height;
    gfx_destroy_texture(device, oldDepth);
    gfx_destroy_texture(device, oldColor);
    app_gfx_demo_log_once(state, AppGfxDemoLoadLog_Targets, "Demo offscreen targets ready");
    return 1;
}

static AppGfxDrawBinKind app_gfx_demo_draw_bin_kind(const AppGfxDemoObject* object) {
    ASSERT_ALWAYS(object != 0);

    AppGfxDrawBinKind result = AppGfxDrawBinKind_Opaque;
    if (FLAGS_HAS(object->flags, AppGfxDemoObjectFlags_Transparent)) {
        result = AppGfxDrawBinKind_Transparent;
    } else if (FLAGS_HAS(object->flags, AppGfxDemoObjectFlags_AlphaTest)) {
        result = AppGfxDrawBinKind_AlphaTest;
    }
    return result;
}

static B32 app_gfx_demo_transparent_draw_before(const AppGfxDemoObject* a, const AppGfxDemoObject* b) {
    ASSERT_ALWAYS(a != 0);
    ASSERT_ALWAYS(b != 0);

    B32 result = 0;
    if (a->depth > b->depth) {
        result = 1;
    } else if (a->depth == b->depth && a->objectId < b->objectId) {
        result = 1;
    }
    return result;
}

static void app_gfx_sort_demo_transparent_indices(const AppGfxDemoObject* objects, U32* indices, U32 count) {
    ASSERT_ALWAYS(objects != 0);
    ASSERT_ALWAYS(indices != 0 || count == 0u);

    for (U32 index = 1u; index < count; ++index) {
        U32 value = indices[index];
        const AppGfxDemoObject* valueObject = objects + value;
        U32 at = index;
        while (at > 0u) {
            const AppGfxDemoObject* previousObject = objects + indices[at - 1u];
            if (!app_gfx_demo_transparent_draw_before(valueObject, previousObject)) {
                break;
            }

            indices[at] = indices[at - 1u];
            --at;
        }
        indices[at] = value;
    }
}

static B32 app_gfx_write_demo_bin_draw(AppCoreState* state,
                                       GfxTemp rootTemp,
                                       AppGfxDrawRootData* rootDataBase,
                                       AppGfxDrawBinKind binKind,
                                       const AppGfxVisibilityBin* visibilityBin,
                                       GfxDraw* draw) {
    ASSERT_ALWAYS(state != 0);
    ASSERT_ALWAYS(rootDataBase != 0);
    ASSERT_ALWAYS(visibilityBin != 0);
    ASSERT_ALWAYS(draw != 0);
    ASSERT_ALWAYS((U32)binKind < AppGfxDrawBinKind_COUNT);

    if (visibilityBin->sourceIndexCount == 0u ||
        state->gfxDemo.gpu.visibleIndexBufferId.index == 0u ||
        state->gfxDemo.gpu.indirectArgsBuffer.generation == 0u) {
        return 0;
    }

    U64 rootDataOffset = sizeof(AppGfxDrawRootData) * (U32)binKind;

    AppGfxDrawRootData* rootData = rootDataBase + (U32)binKind;
    rootData->vertexBuffer = state->gfxDemo.gpu.triangleVertexBufferId.index;
    rootData->vertexByteOffset = 0u;
    rootData->objectBuffer = state->gfxDemo.gpu.objectBufferId.index;
    rootData->objectByteOffset = 0u;
    rootData->visibleIndexBuffer = state->gfxDemo.gpu.visibleIndexBufferId.index;
    rootData->visibleIndexByteOffset = visibilityBin->visibleIndexByteOffset;
    rootData->materialBuffer = state->gfxDemo.gpu.materialBufferId.index;
    rootData->materialByteOffset = 0u;
    rootData->animationPhase = state->gfxDemo.renderer.animationSeconds;
    rootData->_padding0 = 0u;
    rootData->_padding1 = 0u;
    rootData->_padding2 = 0u;
    rootData->_padding3 = 0u;
    rootData->_padding4 = 0u;
    rootData->_padding5 = 0u;
    rootData->_padding6 = 0u;

    GfxGpuSlice rootDataSlice = rootTemp.gpu;
    rootDataSlice.offset += rootDataOffset;
    rootDataSlice.size = sizeof(AppGfxDrawRootData);

    *draw = {};
    draw->kind = GfxDrawKind_IndirectIndexed;
    draw->pipeline = (binKind == AppGfxDrawBinKind_Transparent) ?
                     state->gfxDemo.pipelines.triangleTransparent :
                     state->gfxDemo.pipelines.triangleOpaque;
    draw->indexBuffer = state->gfxDemo.gpu.triangleIndexBuffer;
    draw->indexType = GfxIndexType_U16;
    draw->rootData = rootDataSlice;
    draw->indirectArgs.buffer = state->gfxDemo.gpu.indirectArgsBuffer;
    draw->indirectArgs.offset = visibilityBin->indirectArgsByteOffset;
    draw->indirectArgs.size = sizeof(GfxDrawIndexedIndirectArgs);
    return 1;
}

static B32 app_gfx_build_demo_draw_bins(APP_Context* ctx,
                                        GfxFrame* frame,
                                        Arena* arena,
                                        B32 materialsReady,
                                        B32 visibilityReady,
                                        AppGfxDrawBin* outBins,
                                        U32 outBinCount,
                                        U32* outDrawCount) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(outBins != 0);
    ASSERT_ALWAYS(outDrawCount != 0);

    for (U32 binIndex = 0u; binIndex < outBinCount; ++binIndex) {
        outBins[binIndex] = {};
    }
    *outDrawCount = 0u;

    AppCoreState* state = ctx->core;
    if (frame == 0 ||
        arena == 0 ||
        state->gfxDemo.renderer.objects == 0 ||
        state->gfxDemo.renderer.objectCount == 0u ||
        state->gfxDemo.renderer.visibilityBins == 0 ||
        state->gfxDemo.renderer.visibilityBinCount < AppGfxDrawBinKind_COUNT ||
        outBinCount < AppGfxDrawBinKind_COUNT ||
        !state->gfxDemo.runtime.geometryUploaded ||
        !state->gfxDemo.upload.objectUploaded ||
        state->gfxDemo.upload.objectDirty ||
        !materialsReady ||
        !visibilityReady ||
        state->gfxDemo.gpu.triangleVertexBufferId.index == 0u ||
        state->gfxDemo.gpu.objectBufferId.index == 0u ||
        state->gfxDemo.gpu.visibleIndexBufferId.index == 0u ||
        state->gfxDemo.gpu.indirectArgsBuffer.generation == 0u ||
        state->gfxDemo.gpu.materialBufferId.index == 0u ||
        state->gfxDemo.pipelines.triangleOpaque.generation == 0u ||
        state->gfxDemo.pipelines.triangleTransparent.generation == 0u) {
        return 0;
    }

    GfxTemp rootTemp = gfx_allocate_temp(frame, sizeof(AppGfxDrawRootData) * AppGfxDrawBinKind_COUNT, 16u);
    AppGfxDrawRootData* rootDataBase = (AppGfxDrawRootData*)rootTemp.cpu;
    if (rootDataBase == 0) {
        return 0;
    }

    GfxDraw* binDraws = ARENA_PUSH_ARRAY(arena, GfxDraw, AppGfxDrawBinKind_COUNT);
    if (binDraws == 0) {
        return 0;
    }

    for (U32 binIndex = 0u; binIndex < AppGfxDrawBinKind_COUNT; ++binIndex) {
        const AppGfxVisibilityBin* visibilityBin = state->gfxDemo.renderer.visibilityBins + binIndex;
        if (visibilityBin->sourceIndexCount == 0u) {
            continue;
        }

        outBins[binIndex].draws = binDraws + binIndex;
        if (!app_gfx_write_demo_bin_draw(state,
                                         rootTemp,
                                         rootDataBase,
                                         (AppGfxDrawBinKind)binIndex,
                                         visibilityBin,
                                         outBins[binIndex].draws)) {
            return 0;
        }
        outBins[binIndex].drawCount = 1u;
        *outDrawCount += 1u;
    }
    return 1;
}

static GfxDrawArea app_gfx_demo_draw_area(U32 width, U32 height, const GfxDraw* draws, U32 drawCount) {
    GfxDrawArea area = {};
    area.viewport.x = 0.0f;
    area.viewport.y = 0.0f;
    area.viewport.width = (F32)width;
    area.viewport.height = (F32)height;
    area.viewport.minDepth = 0.0f;
    area.viewport.maxDepth = 1.0f;
    area.scissor.x = 0;
    area.scissor.y = 0;
    area.scissor.width = width;
    area.scissor.height = height;
    area.draws = draws;
    area.drawCount = drawCount;
    return area;
}

static U32 app_gfx_demo_draw_areas(U32 width,
                                   U32 height,
                                   const AppGfxDrawBin* bins,
                                   U32 binCount,
                                   GfxDrawArea* outAreas,
                                   U32 outAreaCapacity) {
    ASSERT_ALWAYS(bins != 0);
    ASSERT_ALWAYS(outAreas != 0);
    ASSERT_ALWAYS(binCount <= AppGfxDrawBinKind_COUNT);
    ASSERT_ALWAYS(outAreaCapacity >= binCount);

    U32 areaCount = 0u;
    for (U32 binIndex = 0u; binIndex < binCount; ++binIndex) {
        const AppGfxDrawBin* bin = bins + binIndex;
        if (bin->drawCount == 0u) {
            continue;
        }

        ASSERT_ALWAYS(bin->draws != 0);
        outAreas[areaCount] = app_gfx_demo_draw_area(width, height, bin->draws, bin->drawCount);
        ++areaCount;
    }
    return areaCount;
}

static B32 app_gfx_build_demo_render_packet(APP_Context* ctx,
                                            const AppGfxDrawBin* bins,
                                            U32 binCount,
                                            GfxTexture colorTexture,
                                            B32 useDepth,
                                            B32 offscreen,
                                            AppGfxDemoRenderPacket* outPacket) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(bins != 0);
    ASSERT_ALWAYS(outPacket != 0);

    MEMSET(outPacket, 0, sizeof(*outPacket));
    AppCoreState* state = ctx->core;
    U32 width = ctx->host->windowWidth;
    U32 height = ctx->host->windowHeight;
    outPacket->areaCount = app_gfx_demo_draw_areas(width,
                                                   height,
                                                   bins,
                                                   binCount,
                                                   outPacket->areas,
                                                   ARRAY_COUNT(outPacket->areas));

    outPacket->resourceUses[0] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.triangleVertexBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Vertex);
    outPacket->resourceUses[1] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.objectBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Vertex);
    outPacket->resourceUses[2] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.visibleIndexBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Vertex);
    outPacket->resourceUses[3] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.materialBuffer,
                                                             GfxResourceAccessFlags_ShaderRead,
                                                             GfxShaderStageFlags_Fragment);
    outPacket->resourceUses[4] = app_gfx_texture_resource_use(state->gfxDemo.gpu.texture,
                                                              GfxResourceAccessFlags_ShaderRead,
                                                              GfxShaderStageFlags_Fragment);
    outPacket->resourceUses[5] = app_gfx_buffer_resource_use(state->gfxDemo.gpu.indirectArgsBuffer,
                                                             GfxResourceAccessFlags_IndirectRead,
                                                             GfxShaderStageFlags_Vertex);

    outPacket->depthTarget.texture = state->gfxDemo.gpu.depth;
    outPacket->depthTarget.loadOp = GfxLoadOp_Clear;
    outPacket->depthTarget.storeOp = GfxStoreOp_DontCare;
    outPacket->depthTarget.clearDepth = 1.0f;

    outPacket->colorTarget.loadOp = GfxLoadOp_Clear;
    outPacket->colorTarget.storeOp = GfxStoreOp_Store;
    outPacket->colorTarget.texture = colorTexture;
    outPacket->colorTarget.clearColor[3] = 1.0f;
    if (offscreen) {
        outPacket->colorTarget.clearColor[0] = 0.02f;
        outPacket->colorTarget.clearColor[1] = 0.03f;
        outPacket->colorTarget.clearColor[2] = 0.04f;
        outPacket->pass.name = "demo offscreen pass";
        outPacket->pass.depthTarget = useDepth ? &outPacket->depthTarget : 0;
    } else {
        outPacket->colorTarget.clearColor[0] = 0.06f;
        outPacket->colorTarget.clearColor[1] = 0.08f;
        outPacket->colorTarget.clearColor[2] = 0.10f;
        outPacket->pass.name = "demo visible pass";
        outPacket->pass.depthTarget = useDepth ? &outPacket->depthTarget : 0;
    }

    outPacket->pass.colorTargets = &outPacket->colorTarget;
    outPacket->pass.colorTargetCount = 1u;
    outPacket->pass.resourceUses = outPacket->resourceUses;
    outPacket->pass.resourceUseCount = ARRAY_COUNT(outPacket->resourceUses);
    return 1;
}

static void app_gfx_demo_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    app_resource_cache_shutdown(ctx);

    if (device != 0) {
        app_gfx_destroy_demo_targets(ctx);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.triangleOpaque);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.triangleTransparent);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.materialCompute);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.cullBoundsCompute);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.visibilityCountCompute);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.visibilityPrefixCompute);
        gfx_destroy_pipeline(device, state->gfxDemo.pipelines.visibilityCompactCompute);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.triangleIndexBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.triangleVertexBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.objectBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.cullSourceBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.cullObjectBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.materialSourceBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.materialBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.visibilitySourceIndexBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.visibilityBinBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.visibilityGroupBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.visibilityGroupCountBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.visibilityGroupOffsetBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.visibleIndexBuffer);
        gfx_destroy_buffer(device, state->gfxDemo.gpu.indirectArgsBuffer);
        gfx_destroy_sampler(device, state->gfxDemo.gpu.sampler);
        gfx_destroy_texture(device, state->gfxDemo.gpu.texture);
    }

    state->gfxDemo.pipelines.triangleOpaque = {};
    state->gfxDemo.pipelines.triangleTransparent = {};
    state->gfxDemo.pipelines.materialCompute = {};
    state->gfxDemo.pipelines.cullBoundsCompute = {};
    state->gfxDemo.pipelines.visibilityCountCompute = {};
    state->gfxDemo.pipelines.visibilityPrefixCompute = {};
    state->gfxDemo.pipelines.visibilityCompactCompute = {};
    state->gfxDemo.gpu.triangleIndexBuffer = {};
    state->gfxDemo.gpu.triangleVertexBuffer = {};
    state->gfxDemo.gpu.triangleVertexBufferId = {};
    state->gfxDemo.gpu.objectBuffer = {};
    state->gfxDemo.gpu.objectBufferId = {};
    state->gfxDemo.gpu.cullSourceBuffer = {};
    state->gfxDemo.gpu.cullSourceBufferId = {};
    state->gfxDemo.gpu.cullObjectBuffer = {};
    state->gfxDemo.gpu.cullObjectBufferId = {};
    state->gfxDemo.renderer.objects = 0;
    state->gfxDemo.renderer.objectCount = 0u;
    state->gfxDemo.renderer.gpuObjects = 0;
    state->gfxDemo.renderer.gpuObjectCount = 0u;
    state->gfxDemo.renderer.gpuCullSources = 0;
    state->gfxDemo.renderer.gpuCullSourceCount = 0u;
    state->gfxDemo.renderer.visibilitySourceIndices = 0;
    state->gfxDemo.renderer.visibilitySourceIndexCount = 0u;
    state->gfxDemo.renderer.visibilityBins = 0;
    state->gfxDemo.renderer.visibilityBinCount = 0u;
    state->gfxDemo.renderer.visibilityGroups = 0;
    state->gfxDemo.renderer.visibilityGroupCount = 0u;
    state->gfxDemo.renderer.materialSources = 0;
    state->gfxDemo.renderer.materialSourceCount = 0u;
    state->gfxDemo.gpu.materialSourceBuffer = {};
    state->gfxDemo.gpu.materialSourceBufferId = {};
    state->gfxDemo.gpu.materialBuffer = {};
    state->gfxDemo.gpu.materialBufferId = {};
    state->gfxDemo.gpu.visibilitySourceIndexBuffer = {};
    state->gfxDemo.gpu.visibilitySourceIndexBufferId = {};
    state->gfxDemo.gpu.visibilityBinBuffer = {};
    state->gfxDemo.gpu.visibilityBinBufferId = {};
    state->gfxDemo.gpu.visibilityGroupBuffer = {};
    state->gfxDemo.gpu.visibilityGroupBufferId = {};
    state->gfxDemo.gpu.visibilityGroupCountBuffer = {};
    state->gfxDemo.gpu.visibilityGroupCountBufferId = {};
    state->gfxDemo.gpu.visibilityGroupOffsetBuffer = {};
    state->gfxDemo.gpu.visibilityGroupOffsetBufferId = {};
    state->gfxDemo.gpu.visibleIndexBuffer = {};
    state->gfxDemo.gpu.visibleIndexBufferId = {};
    state->gfxDemo.gpu.indirectArgsBuffer = {};
    state->gfxDemo.gpu.indirectArgsBufferId = {};
    state->gfxDemo.gpu.texture = {};
    state->gfxDemo.gpu.offscreenColor = {};
    state->gfxDemo.gpu.depth = {};
    state->gfxDemo.gpu.sampler = {};
    state->gfxDemo.gpu.textureId = {};
    state->gfxDemo.gpu.samplerId = {};
    state->gfxDemo.gpu.targetWidth = 0u;
    state->gfxDemo.gpu.targetHeight = 0u;
    state->gfxDemo.renderer.dataVersion = 0u;
    state->gfxDemo.renderer.materialCount = 0u;
    state->gfxDemo.upload.materialSourceUploaded = 0;
    state->gfxDemo.upload.materialSourceDirty = 0;
    state->gfxDemo.upload.textureUploaded = 0;
    state->gfxDemo.upload.materialsReady = 0;
    state->gfxDemo.upload.materialDirty = 0;
    state->gfxDemo.upload.objectUploaded = 0;
    state->gfxDemo.upload.objectDirty = 0;
    state->gfxDemo.upload.cullSourceUploaded = 0;
    state->gfxDemo.upload.cullSourceDirty = 0;
    state->gfxDemo.upload.visibilitySourceUploaded = 0;
    state->gfxDemo.upload.visibilitySourceDirty = 0;
    state->gfxDemo.upload.visibilityBinUploaded = 0;
    state->gfxDemo.upload.visibilityBinDirty = 0;
    state->gfxDemo.upload.visibilityGroupUploaded = 0;
    state->gfxDemo.upload.visibilityGroupDirty = 0;
    state->gfxDemo.pipelines.triangleOpaqueArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.triangleTransparentArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.materialComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.cullBoundsComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.visibilityCountComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.visibilityPrefixComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.pipelines.visibilityCompactComputeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.upload.textureDecodeArtifactKey = ARTIFACT_KEY_ZERO;
    state->gfxDemo.upload.decodedTextureHash = CONTENT_HASH_ZERO;
    state->gfxDemo.upload.textureGeneration = 0u;
    state->gfxDemo.upload.textureFailedGeneration = 0u;
    state->gfxDemo.runtime.geometryCreated = 0;
    state->gfxDemo.runtime.geometryUploaded = 0;
    state->gfxDemo.runtime.ready = 0;
    state->gfxDemo.runtime.loadLogMask = 0u;
    state->gfxDemo.runtime.initialized = 0;
}

static void app_gfx_demo_frame(APP_Context* ctx, F32 deltaSeconds) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (!ctx->host->gfxDevice) {
        return;
    }
    if (ctx->host->windowWidth == 0u || ctx->host->windowHeight == 0u) {
        return;
    }

    if (deltaSeconds < 0.0f) {
        deltaSeconds = 0.0f;
    }
    if (deltaSeconds > APP_GFX_DEMO_MAX_FRAME_DELTA_SECONDS) {
        deltaSeconds = APP_GFX_DEMO_MAX_FRAME_DELTA_SECONDS;
    }
    ctx->core->gfxDemo.renderer.animationSeconds += deltaSeconds;

    if (!app_gfx_demo_init(ctx)) {
        return;
    }

#if defined(PLATFORM_BUILD_DEBUG)
    app_gfx_try_build_dev_shaders(ctx);
#endif

#if defined(PLATFORM_BUILD_DEBUG)
    if (ctx->core->resources.fileStream) {
        file_stream_tick(ctx->core->resources.fileStream, OS_get_time_nanoseconds(), 16u);
    }
#endif
    if (ctx->core->resources.artifactCache) {
        artifact_cache_tick(ctx->core->resources.artifactCache, ctx->core->frameCounter, 16u, 16u);
    }

    Temp scratch = get_scratch(0, 0);

    GfxFrame* frame = gfx_begin_frame(ctx->host->gfxDevice);
    if (!frame) {
        if (scratch.arena != 0) {
            temp_end(&scratch);
        }
        return;
    }

    GfxCommandBuffer* commands = gfx_get_command_buffer(frame);
    GfxTexture backbuffer = gfx_get_backbuffer(frame);

    if (!ctx->core->gfxDemo.runtime.geometryCreated) {
        app_gfx_try_create_demo_buffers(ctx);
    }
    if (ctx->core->gfxDemo.runtime.geometryCreated && !ctx->core->gfxDemo.runtime.geometryUploaded) {
        app_gfx_upload_demo_geometry(ctx, frame);
    }
    if (ctx->core->gfxDemo.runtime.geometryUploaded) {
        app_gfx_upload_demo_objects(ctx, frame);
        app_gfx_upload_demo_visibility_sources(ctx, frame);
        app_gfx_try_update_triangle_pipelines(ctx);
        app_gfx_try_update_demo_compute_pipeline(ctx);
        app_gfx_try_update_demo_gpu_data_pipelines(ctx);
        app_gfx_upload_demo_texture(ctx, frame);
        app_gfx_upload_demo_material_sources(ctx, frame);
        if (ctx->core->resources.artifactCache) {
            artifact_cache_tick(ctx->core->resources.artifactCache, ctx->core->frameCounter, 16u, 16u);
        }
    }

    B32 targetsReady = app_gfx_ensure_demo_targets(ctx);
    B32 materialsReady = app_gfx_dispatch_demo_materials(ctx, commands, frame);
    B32 cullBoundsReady = app_gfx_dispatch_demo_cull_bounds(ctx, commands, frame);
    B32 visibilityReady = app_gfx_dispatch_demo_visibility(ctx, commands, frame, cullBoundsReady);

    AppGfxDrawBin drawBins[AppGfxDrawBinKind_COUNT] = {};
    U32 drawCount = 0u;
    if (targetsReady && scratch.arena != 0) {
        app_gfx_build_demo_draw_bins(ctx,
                                     frame,
                                     scratch.arena,
                                     materialsReady,
                                     visibilityReady,
                                     drawBins,
                                     ARRAY_COUNT(drawBins),
                                     &drawCount);
    }

    if (targetsReady) {
        AppGfxDemoRenderPacket offscreenPacket = {};
        if (app_gfx_build_demo_render_packet(ctx,
                                             drawBins,
                                             ARRAY_COUNT(drawBins),
                                             ctx->core->gfxDemo.gpu.offscreenColor,
                                             targetsReady,
                                             1,
                                             &offscreenPacket)) {
            gfx_render_pass(commands,
                            &offscreenPacket.pass,
                            offscreenPacket.areas,
                            offscreenPacket.areaCount);
        }
    }

    AppGfxDemoRenderPacket visiblePacket = {};
    if (app_gfx_build_demo_render_packet(ctx,
                                         drawBins,
                                         ARRAY_COUNT(drawBins),
                                         backbuffer,
                                         targetsReady,
                                         0,
                                         &visiblePacket)) {
        gfx_render_pass(commands,
                        &visiblePacket.pass,
                        visiblePacket.areas,
                        visiblePacket.areaCount);
    }
    if (drawCount != 0u && !ctx->core->gfxDemo.runtime.ready) {
        ctx->core->gfxDemo.runtime.ready = 1;
        app_gfx_demo_log_once(ctx->core, AppGfxDemoLoadLog_Ready, "Demo resources ready");
    }

    if (scratch.arena != 0) {
        temp_end(&scratch);
    }
    gfx_submit(commands);
    gfx_end_frame(frame);
    if (ctx->core->resources.artifactCache) {
        artifact_cache_evict(ctx->core->resources.artifactCache, ctx->core->frameCounter, 128u);
    }
}
