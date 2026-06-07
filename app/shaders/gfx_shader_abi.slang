#ifndef GFX_SHADER_ABI_SLANG
#define GFX_SHADER_ABI_SLANG

static const uint GFX_SHADER_SLOT_ROOT_DATA = 8u;
static const uint GFX_SHADER_SLOT_RESOURCE_TABLE = 9u;
static const uint GFX_RESOURCE_TABLE_CAPACITY = 256u;
static const uint GFX_RESOURCE_BINDING_TEXTURES = 0u;
static const uint GFX_RESOURCE_BINDING_SAMPLERS = 1u;
static const uint GFX_RESOURCE_BINDING_BUFFERS = 2u;
static const uint GFX_MATERIAL_STRIDE_BYTES = 32u;
static const uint GFX_MATERIAL_STRIDE_WORDS = GFX_MATERIAL_STRIDE_BYTES / 4u;
static const uint GFX_DEMO_VERTEX_STRIDE_BYTES = 24u;
static const uint GFX_DEMO_VERTEX_STRIDE_WORDS = GFX_DEMO_VERTEX_STRIDE_BYTES / 4u;
static const uint GFX_DEMO_OBJECT_STRIDE_BYTES = 32u;
static const uint GFX_DEMO_OBJECT_STRIDE_WORDS = GFX_DEMO_OBJECT_STRIDE_BYTES / 4u;
static const uint GFX_DEMO_CULL_SOURCE_STRIDE_BYTES = 48u;
static const uint GFX_DEMO_CULL_SOURCE_STRIDE_WORDS = GFX_DEMO_CULL_SOURCE_STRIDE_BYTES / 4u;
static const uint GFX_DEMO_CULL_OBJECT_STRIDE_BYTES = 32u;
static const uint GFX_DEMO_CULL_OBJECT_STRIDE_WORDS = GFX_DEMO_CULL_OBJECT_STRIDE_BYTES / 4u;
static const uint GFX_DEMO_VISIBILITY_BIN_STRIDE_BYTES = 32u;
static const uint GFX_DEMO_VISIBILITY_BIN_STRIDE_WORDS = GFX_DEMO_VISIBILITY_BIN_STRIDE_BYTES / 4u;
static const uint GFX_DEMO_VISIBILITY_GROUP_STRIDE_BYTES = 32u;
static const uint GFX_DEMO_VISIBILITY_GROUP_STRIDE_WORDS = GFX_DEMO_VISIBILITY_GROUP_STRIDE_BYTES / 4u;
static const uint GFX_DRAW_INDEXED_INDIRECT_ARGS_STRIDE_BYTES = 20u;
static const uint GFX_DEMO_VISIBILITY_THREADS_PER_GROUP = 128u;
static const uint GFX_DEMO_DRAW_FLAG_ALPHA_TEST = 1u << 0u;
static const uint GFX_DEMO_DRAW_FLAG_TRANSPARENT = 1u << 1u;

#ifndef GFX_SHADER_TARGET_VULKAN
#define GFX_SHADER_TARGET_VULKAN 0
#endif
#ifndef GFX_SHADER_TARGET_METAL
#define GFX_SHADER_TARGET_METAL 0
#endif
#ifndef GFX_SHADER_STAGE_GRAPHICS
#define GFX_SHADER_STAGE_GRAPHICS 0
#endif
#ifndef GFX_SHADER_STAGE_COMPUTE
#define GFX_SHADER_STAGE_COMPUTE 0
#endif

struct DrawRootData {
    uint vertexBuffer;
    uint vertexByteOffset;
    uint objectBuffer;
    uint objectByteOffset;
    uint visibleIndexBuffer;
    uint visibleIndexByteOffset;
    uint materialBuffer;
    uint materialByteOffset;
    float animationPhase;
    uint _padding0;
    uint _padding1;
    uint _padding2;
    uint _padding3;
    uint _padding4;
    uint _padding5;
    uint _padding6;
};

struct Material {
    float4 baseColor;
    uint albedoTexture;
    uint samplerIndex;
    uint flags;
    uint _padding;
};

struct MaterialComputeRootData {
    uint materialCount;
    uint sourceMaterialBuffer;
    uint sourceMaterialByteOffset;
    uint materialBuffer;
    uint materialByteOffset;
    float animationPhase;
    uint _padding0;
    uint _padding1;
};

struct CullBoundsComputeRootData {
    uint cullSourceCount;
    uint cullSourceBuffer;
    uint cullSourceByteOffset;
    uint cullObjectBuffer;
    uint cullObjectByteOffset;
    uint _padding0;
    uint _padding1;
    uint _padding2;
};

struct VisibilityBin {
    uint sourceIndexByteOffset;
    uint sourceIndexCount;
    uint visibleIndexByteOffset;
    uint indirectArgsByteOffset;
    uint groupStart;
    uint groupCount;
    uint _padding2;
    uint _padding3;
};

struct VisibilityGroup {
    uint sourceIndexByteOffset;
    uint sourceIndexCount;
    uint visibleIndexByteOffset;
    uint binIndex;
    uint _padding0;
    uint _padding1;
    uint _padding2;
    uint _padding3;
};

struct VisibilityComputeRootData {
    uint binCount;
    uint groupCount;
    uint cullObjectBuffer;
    uint cullObjectByteOffset;
    uint sourceIndexBuffer;
    uint sourceIndexByteOffset;
    uint visibleIndexBuffer;
    uint visibleIndexByteOffset;
    uint indirectArgsBuffer;
    uint indirectArgsByteOffset;
    uint binBuffer;
    uint binByteOffset;
    uint groupBuffer;
    uint groupByteOffset;
    uint groupCountBuffer;
    uint groupCountByteOffset;
    uint groupOffsetBuffer;
    uint groupOffsetByteOffset;
    uint _padding0;
    uint _padding1;
    uint _padding2;
    uint _padding3;
    uint _padding4;
    uint _padding5;
};

struct DrawIndexedIndirectArgs {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint firstInstance;
};

struct DemoVertex {
    float2 position;
    float4 color;
};

struct DemoObject {
    float4 offsetScale;
    float phaseOffset;
    uint materialIndex;
    uint objectId;
    uint flags;
};

struct CullSource {
    float2 localMin;
    float2 localMax;
    float4 offsetScale;
    uint objectIndex;
    uint flags;
    float maxAnimationScale;
    float wobbleExtent;
};

struct CullObject {
    float2 clipMin;
    float2 clipMax;
    float depthMin;
    float depthMax;
    uint objectIndex;
    uint flags;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialIndex : TEXCOORD1;
    nointerpolation uint objectId : TEXCOORD2;
    nointerpolation uint flags : TEXCOORD3;
    nointerpolation float animationPhase : TEXCOORD4;
    nointerpolation float depth : TEXCOORD5;
};

#if GFX_SHADER_TARGET_VULKAN

struct GfxPushConstants {
    uint rootByteOffset;
    uint rootResource;
};

[[vk::binding(GFX_RESOURCE_BINDING_TEXTURES, 0)]]
Texture2D<float4> gfxTextures[GFX_RESOURCE_TABLE_CAPACITY];

[[vk::binding(GFX_RESOURCE_BINDING_SAMPLERS, 0)]]
SamplerState gfxSamplers[GFX_RESOURCE_TABLE_CAPACITY];

[[vk::binding(GFX_RESOURCE_BINDING_BUFFERS, 0)]]
RWStructuredBuffer<uint> gfxBuffers[GFX_RESOURCE_TABLE_CAPACITY];

[[vk::push_constant]]
ConstantBuffer<GfxPushConstants> gfxPushConstants;

float4 gfx_load_float4(uint resourceIndex, uint byteOffset, uint wordOffset) {
    uint base = byteOffset / 4u + wordOffset;
    return asfloat(uint4(gfxBuffers[resourceIndex][base + 0u],
                         gfxBuffers[resourceIndex][base + 1u],
                         gfxBuffers[resourceIndex][base + 2u],
                         gfxBuffers[resourceIndex][base + 3u]));
}

DrawRootData gfx_load_draw_root_data() {
    uint resourceIndex = gfxPushConstants.rootResource;
    uint byteOffset = gfxPushConstants.rootByteOffset;

    DrawRootData result;
    uint base = byteOffset / 4u;
    result.vertexBuffer = gfxBuffers[resourceIndex][base + 0u];
    result.vertexByteOffset = gfxBuffers[resourceIndex][base + 1u];
    result.objectBuffer = gfxBuffers[resourceIndex][base + 2u];
    result.objectByteOffset = gfxBuffers[resourceIndex][base + 3u];
    result.visibleIndexBuffer = gfxBuffers[resourceIndex][base + 4u];
    result.visibleIndexByteOffset = gfxBuffers[resourceIndex][base + 5u];
    result.materialBuffer = gfxBuffers[resourceIndex][base + 6u];
    result.materialByteOffset = gfxBuffers[resourceIndex][base + 7u];
    result.animationPhase = asfloat(gfxBuffers[resourceIndex][base + 8u]);
    result._padding0 = gfxBuffers[resourceIndex][base + 9u];
    result._padding1 = gfxBuffers[resourceIndex][base + 10u];
    result._padding2 = gfxBuffers[resourceIndex][base + 11u];
    result._padding3 = gfxBuffers[resourceIndex][base + 12u];
    result._padding4 = gfxBuffers[resourceIndex][base + 13u];
    result._padding5 = gfxBuffers[resourceIndex][base + 14u];
    result._padding6 = gfxBuffers[resourceIndex][base + 15u];
    return result;
}

MaterialComputeRootData gfx_load_compute_root_data() {
    uint resourceIndex = gfxPushConstants.rootResource;
    uint base = gfxPushConstants.rootByteOffset / 4u;

    MaterialComputeRootData result;
    result.materialCount = gfxBuffers[resourceIndex][base + 0u];
    result.sourceMaterialBuffer = gfxBuffers[resourceIndex][base + 1u];
    result.sourceMaterialByteOffset = gfxBuffers[resourceIndex][base + 2u];
    result.materialBuffer = gfxBuffers[resourceIndex][base + 3u];
    result.materialByteOffset = gfxBuffers[resourceIndex][base + 4u];
    result.animationPhase = asfloat(gfxBuffers[resourceIndex][base + 5u]);
    result._padding0 = gfxBuffers[resourceIndex][base + 6u];
    result._padding1 = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

CullBoundsComputeRootData gfx_load_cull_bounds_compute_root_data() {
    uint resourceIndex = gfxPushConstants.rootResource;
    uint base = gfxPushConstants.rootByteOffset / 4u;

    CullBoundsComputeRootData result;
    result.cullSourceCount = gfxBuffers[resourceIndex][base + 0u];
    result.cullSourceBuffer = gfxBuffers[resourceIndex][base + 1u];
    result.cullSourceByteOffset = gfxBuffers[resourceIndex][base + 2u];
    result.cullObjectBuffer = gfxBuffers[resourceIndex][base + 3u];
    result.cullObjectByteOffset = gfxBuffers[resourceIndex][base + 4u];
    result._padding0 = gfxBuffers[resourceIndex][base + 5u];
    result._padding1 = gfxBuffers[resourceIndex][base + 6u];
    result._padding2 = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

VisibilityComputeRootData gfx_load_visibility_compute_root_data() {
    uint resourceIndex = gfxPushConstants.rootResource;
    uint base = gfxPushConstants.rootByteOffset / 4u;

    VisibilityComputeRootData result;
    result.binCount = gfxBuffers[resourceIndex][base + 0u];
    result.groupCount = gfxBuffers[resourceIndex][base + 1u];
    result.cullObjectBuffer = gfxBuffers[resourceIndex][base + 2u];
    result.cullObjectByteOffset = gfxBuffers[resourceIndex][base + 3u];
    result.sourceIndexBuffer = gfxBuffers[resourceIndex][base + 4u];
    result.sourceIndexByteOffset = gfxBuffers[resourceIndex][base + 5u];
    result.visibleIndexBuffer = gfxBuffers[resourceIndex][base + 6u];
    result.visibleIndexByteOffset = gfxBuffers[resourceIndex][base + 7u];
    result.indirectArgsBuffer = gfxBuffers[resourceIndex][base + 8u];
    result.indirectArgsByteOffset = gfxBuffers[resourceIndex][base + 9u];
    result.binBuffer = gfxBuffers[resourceIndex][base + 10u];
    result.binByteOffset = gfxBuffers[resourceIndex][base + 11u];
    result.groupBuffer = gfxBuffers[resourceIndex][base + 12u];
    result.groupByteOffset = gfxBuffers[resourceIndex][base + 13u];
    result.groupCountBuffer = gfxBuffers[resourceIndex][base + 14u];
    result.groupCountByteOffset = gfxBuffers[resourceIndex][base + 15u];
    result.groupOffsetBuffer = gfxBuffers[resourceIndex][base + 16u];
    result.groupOffsetByteOffset = gfxBuffers[resourceIndex][base + 17u];
    result._padding0 = gfxBuffers[resourceIndex][base + 18u];
    result._padding1 = gfxBuffers[resourceIndex][base + 19u];
    result._padding2 = gfxBuffers[resourceIndex][base + 20u];
    result._padding3 = gfxBuffers[resourceIndex][base + 21u];
    result._padding4 = gfxBuffers[resourceIndex][base + 22u];
    result._padding5 = gfxBuffers[resourceIndex][base + 23u];
    return result;
}

Material gfx_load_material(DrawRootData rootData, uint materialIndex) {
    uint resourceIndex = rootData.materialBuffer;
    uint base = rootData.materialByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;

    Material result;
    result.baseColor = asfloat(uint4(gfxBuffers[resourceIndex][base + 0u],
                                     gfxBuffers[resourceIndex][base + 1u],
                                     gfxBuffers[resourceIndex][base + 2u],
                                     gfxBuffers[resourceIndex][base + 3u]));
    result.albedoTexture = gfxBuffers[resourceIndex][base + 4u];
    result.samplerIndex = gfxBuffers[resourceIndex][base + 5u];
    result.flags = gfxBuffers[resourceIndex][base + 6u];
    result._padding = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

DemoVertex gfx_load_demo_vertex(DrawRootData rootData, uint vertexIndex) {
    uint resourceIndex = rootData.vertexBuffer;
    uint base = rootData.vertexByteOffset / 4u + vertexIndex * GFX_DEMO_VERTEX_STRIDE_WORDS;

    DemoVertex result;
    result.position = asfloat(uint2(gfxBuffers[resourceIndex][base + 0u],
                                    gfxBuffers[resourceIndex][base + 1u]));
    result.color = asfloat(uint4(gfxBuffers[resourceIndex][base + 2u],
                                 gfxBuffers[resourceIndex][base + 3u],
                                 gfxBuffers[resourceIndex][base + 4u],
                                 gfxBuffers[resourceIndex][base + 5u]));
    return result;
}

uint gfx_load_visible_object_index(DrawRootData rootData, uint instanceIndex) {
    uint resourceIndex = rootData.visibleIndexBuffer;
    uint base = rootData.visibleIndexByteOffset / 4u + instanceIndex;
    return gfxBuffers[resourceIndex][base];
}

DemoObject gfx_load_demo_object_from_resource(uint resourceIndex, uint byteOffset, uint objectIndex) {
    uint base = byteOffset / 4u + objectIndex * GFX_DEMO_OBJECT_STRIDE_WORDS;

    DemoObject result;
    result.offsetScale = asfloat(uint4(gfxBuffers[resourceIndex][base + 0u],
                                       gfxBuffers[resourceIndex][base + 1u],
                                       gfxBuffers[resourceIndex][base + 2u],
                                       gfxBuffers[resourceIndex][base + 3u]));
    result.phaseOffset = asfloat(gfxBuffers[resourceIndex][base + 4u]);
    result.materialIndex = gfxBuffers[resourceIndex][base + 5u];
    result.objectId = gfxBuffers[resourceIndex][base + 6u];
    result.flags = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

DemoObject gfx_load_demo_object(DrawRootData rootData, uint objectIndex) {
    return gfx_load_demo_object_from_resource(rootData.objectBuffer, rootData.objectByteOffset, objectIndex);
}

CullObject gfx_load_demo_cull_object_from_resource(uint resourceIndex, uint byteOffset, uint objectIndex) {
    uint base = byteOffset / 4u + objectIndex * GFX_DEMO_CULL_OBJECT_STRIDE_WORDS;

    CullObject result;
    result.clipMin = asfloat(uint2(gfxBuffers[resourceIndex][base + 0u],
                                   gfxBuffers[resourceIndex][base + 1u]));
    result.clipMax = asfloat(uint2(gfxBuffers[resourceIndex][base + 2u],
                                   gfxBuffers[resourceIndex][base + 3u]));
    result.depthMin = asfloat(gfxBuffers[resourceIndex][base + 4u]);
    result.depthMax = asfloat(gfxBuffers[resourceIndex][base + 5u]);
    result.objectIndex = gfxBuffers[resourceIndex][base + 6u];
    result.flags = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

CullObject gfx_load_demo_cull_object(VisibilityComputeRootData rootData, uint cullObjectIndex) {
    return gfx_load_demo_cull_object_from_resource(rootData.cullObjectBuffer,
                                                  rootData.cullObjectByteOffset,
                                                  cullObjectIndex);
}

CullSource gfx_load_demo_cull_source(CullBoundsComputeRootData rootData, uint sourceIndex) {
    uint resourceIndex = rootData.cullSourceBuffer;
    uint base = rootData.cullSourceByteOffset / 4u + sourceIndex * GFX_DEMO_CULL_SOURCE_STRIDE_WORDS;

    CullSource result;
    result.localMin = asfloat(uint2(gfxBuffers[resourceIndex][base + 0u],
                                    gfxBuffers[resourceIndex][base + 1u]));
    result.localMax = asfloat(uint2(gfxBuffers[resourceIndex][base + 2u],
                                    gfxBuffers[resourceIndex][base + 3u]));
    result.offsetScale = asfloat(uint4(gfxBuffers[resourceIndex][base + 4u],
                                       gfxBuffers[resourceIndex][base + 5u],
                                       gfxBuffers[resourceIndex][base + 6u],
                                       gfxBuffers[resourceIndex][base + 7u]));
    result.objectIndex = gfxBuffers[resourceIndex][base + 8u];
    result.flags = gfxBuffers[resourceIndex][base + 9u];
    result.maxAnimationScale = asfloat(gfxBuffers[resourceIndex][base + 10u]);
    result.wobbleExtent = asfloat(gfxBuffers[resourceIndex][base + 11u]);
    return result;
}

void gfx_store_demo_cull_object(CullBoundsComputeRootData rootData, uint cullObjectIndex, CullObject object) {
    uint resourceIndex = rootData.cullObjectBuffer;
    uint base = rootData.cullObjectByteOffset / 4u + cullObjectIndex * GFX_DEMO_CULL_OBJECT_STRIDE_WORDS;
    uint2 clipMin = asuint(object.clipMin);
    uint2 clipMax = asuint(object.clipMax);

    gfxBuffers[resourceIndex][base + 0u] = clipMin.x;
    gfxBuffers[resourceIndex][base + 1u] = clipMin.y;
    gfxBuffers[resourceIndex][base + 2u] = clipMax.x;
    gfxBuffers[resourceIndex][base + 3u] = clipMax.y;
    gfxBuffers[resourceIndex][base + 4u] = asuint(object.depthMin);
    gfxBuffers[resourceIndex][base + 5u] = asuint(object.depthMax);
    gfxBuffers[resourceIndex][base + 6u] = object.objectIndex;
    gfxBuffers[resourceIndex][base + 7u] = object.flags;
}

Material gfx_load_source_material(MaterialComputeRootData rootData, uint materialIndex) {
    uint resourceIndex = rootData.sourceMaterialBuffer;
    uint base = rootData.sourceMaterialByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;

    Material result;
    result.baseColor = asfloat(uint4(gfxBuffers[resourceIndex][base + 0u],
                                     gfxBuffers[resourceIndex][base + 1u],
                                     gfxBuffers[resourceIndex][base + 2u],
                                     gfxBuffers[resourceIndex][base + 3u]));
    result.albedoTexture = gfxBuffers[resourceIndex][base + 4u];
    result.samplerIndex = gfxBuffers[resourceIndex][base + 5u];
    result.flags = gfxBuffers[resourceIndex][base + 6u];
    result._padding = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

void gfx_store_material(MaterialComputeRootData rootData, uint materialIndex, Material material) {
    uint resourceIndex = rootData.materialBuffer;
    uint base = rootData.materialByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;
    uint4 baseColor = asuint(material.baseColor);

    gfxBuffers[resourceIndex][base + 0u] = baseColor.x;
    gfxBuffers[resourceIndex][base + 1u] = baseColor.y;
    gfxBuffers[resourceIndex][base + 2u] = baseColor.z;
    gfxBuffers[resourceIndex][base + 3u] = baseColor.w;
    gfxBuffers[resourceIndex][base + 4u] = material.albedoTexture;
    gfxBuffers[resourceIndex][base + 5u] = material.samplerIndex;
    gfxBuffers[resourceIndex][base + 6u] = material.flags;
    gfxBuffers[resourceIndex][base + 7u] = material._padding;
}

VisibilityBin gfx_load_visibility_bin(VisibilityComputeRootData rootData, uint binIndex) {
    uint resourceIndex = rootData.binBuffer;
    uint base = rootData.binByteOffset / 4u + binIndex * GFX_DEMO_VISIBILITY_BIN_STRIDE_WORDS;

    VisibilityBin result;
    result.sourceIndexByteOffset = gfxBuffers[resourceIndex][base + 0u];
    result.sourceIndexCount = gfxBuffers[resourceIndex][base + 1u];
    result.visibleIndexByteOffset = gfxBuffers[resourceIndex][base + 2u];
    result.indirectArgsByteOffset = gfxBuffers[resourceIndex][base + 3u];
    result.groupStart = gfxBuffers[resourceIndex][base + 4u];
    result.groupCount = gfxBuffers[resourceIndex][base + 5u];
    result._padding2 = gfxBuffers[resourceIndex][base + 6u];
    result._padding3 = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

VisibilityGroup gfx_load_visibility_group(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupBuffer;
    uint base = rootData.groupByteOffset / 4u + groupIndex * GFX_DEMO_VISIBILITY_GROUP_STRIDE_WORDS;

    VisibilityGroup result;
    result.sourceIndexByteOffset = gfxBuffers[resourceIndex][base + 0u];
    result.sourceIndexCount = gfxBuffers[resourceIndex][base + 1u];
    result.visibleIndexByteOffset = gfxBuffers[resourceIndex][base + 2u];
    result.binIndex = gfxBuffers[resourceIndex][base + 3u];
    result._padding0 = gfxBuffers[resourceIndex][base + 4u];
    result._padding1 = gfxBuffers[resourceIndex][base + 5u];
    result._padding2 = gfxBuffers[resourceIndex][base + 6u];
    result._padding3 = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

uint gfx_load_source_cull_object_index(VisibilityComputeRootData rootData, uint sourceIndexByteOffset) {
    uint resourceIndex = rootData.sourceIndexBuffer;
    uint base = (rootData.sourceIndexByteOffset + sourceIndexByteOffset) / 4u;
    return gfxBuffers[resourceIndex][base];
}

void gfx_store_visible_object_index(VisibilityComputeRootData rootData, uint visibleIndexByteOffset, uint objectIndex) {
    uint resourceIndex = rootData.visibleIndexBuffer;
    uint base = (rootData.visibleIndexByteOffset + visibleIndexByteOffset) / 4u;
    gfxBuffers[resourceIndex][base] = objectIndex;
}

uint gfx_load_visibility_group_count(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupCountBuffer;
    uint base = rootData.groupCountByteOffset / 4u + groupIndex;
    return gfxBuffers[resourceIndex][base];
}

void gfx_store_visibility_group_count(VisibilityComputeRootData rootData, uint groupIndex, uint count) {
    uint resourceIndex = rootData.groupCountBuffer;
    uint base = rootData.groupCountByteOffset / 4u + groupIndex;
    gfxBuffers[resourceIndex][base] = count;
}

uint gfx_load_visibility_group_offset(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupOffsetBuffer;
    uint base = rootData.groupOffsetByteOffset / 4u + groupIndex;
    return gfxBuffers[resourceIndex][base];
}

void gfx_store_visibility_group_offset(VisibilityComputeRootData rootData, uint groupIndex, uint offset) {
    uint resourceIndex = rootData.groupOffsetBuffer;
    uint base = rootData.groupOffsetByteOffset / 4u + groupIndex;
    gfxBuffers[resourceIndex][base] = offset;
}

void gfx_store_draw_indexed_indirect_args(VisibilityComputeRootData rootData,
                                          uint indirectArgsByteOffset,
                                          DrawIndexedIndirectArgs args) {
    uint resourceIndex = rootData.indirectArgsBuffer;
    uint base = (rootData.indirectArgsByteOffset + indirectArgsByteOffset) / 4u;
    gfxBuffers[resourceIndex][base + 0u] = args.indexCount;
    gfxBuffers[resourceIndex][base + 1u] = args.instanceCount;
    gfxBuffers[resourceIndex][base + 2u] = args.firstIndex;
    gfxBuffers[resourceIndex][base + 3u] = uint(args.baseVertex);
    gfxBuffers[resourceIndex][base + 4u] = args.firstInstance;
}

float4 gfx_sample_albedo(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfxTextures[textureIndex].Sample(gfxSamplers[samplerIndex], uv);
}

#elif GFX_SHADER_TARGET_METAL

#if GFX_SHADER_STAGE_GRAPHICS
struct ResourceTable {
    Texture2D<float4> textures[GFX_RESOURCE_TABLE_CAPACITY];
    SamplerState samplers[GFX_RESOURCE_TABLE_CAPACITY];
    StructuredBuffer<uint> buffers[GFX_RESOURCE_TABLE_CAPACITY];
};

ConstantBuffer<DrawRootData> gfxMetalRootData : register(b8);
ParameterBlock<ResourceTable> gfxMetalResources : register(b9);

DrawRootData gfx_load_draw_root_data() {
    return gfxMetalRootData;
}

float4 gfx_load_float4(uint resourceIndex, uint byteOffset, uint wordOffset) {
    uint base = byteOffset / 4u + wordOffset;
    return asfloat(uint4(gfxMetalResources.buffers[resourceIndex][base + 0u],
                         gfxMetalResources.buffers[resourceIndex][base + 1u],
                         gfxMetalResources.buffers[resourceIndex][base + 2u],
                         gfxMetalResources.buffers[resourceIndex][base + 3u]));
}

Material gfx_load_material(DrawRootData rootData, uint materialIndex) {
    uint resourceIndex = rootData.materialBuffer;
    uint base = rootData.materialByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;

    Material result;
    result.baseColor = gfx_load_float4(resourceIndex, rootData.materialByteOffset, materialIndex * GFX_MATERIAL_STRIDE_WORDS);
    result.albedoTexture = gfxMetalResources.buffers[resourceIndex][base + 4u];
    result.samplerIndex = gfxMetalResources.buffers[resourceIndex][base + 5u];
    result.flags = gfxMetalResources.buffers[resourceIndex][base + 6u];
    result._padding = gfxMetalResources.buffers[resourceIndex][base + 7u];
    return result;
}

DemoVertex gfx_load_demo_vertex(DrawRootData rootData, uint vertexIndex) {
    uint resourceIndex = rootData.vertexBuffer;
    uint base = rootData.vertexByteOffset / 4u + vertexIndex * GFX_DEMO_VERTEX_STRIDE_WORDS;

    DemoVertex result;
    result.position = asfloat(uint2(gfxMetalResources.buffers[resourceIndex][base + 0u],
                                    gfxMetalResources.buffers[resourceIndex][base + 1u]));
    result.color = asfloat(uint4(gfxMetalResources.buffers[resourceIndex][base + 2u],
                                 gfxMetalResources.buffers[resourceIndex][base + 3u],
                                 gfxMetalResources.buffers[resourceIndex][base + 4u],
                                 gfxMetalResources.buffers[resourceIndex][base + 5u]));
    return result;
}

uint gfx_load_visible_object_index(DrawRootData rootData, uint instanceIndex) {
    uint resourceIndex = rootData.visibleIndexBuffer;
    uint base = rootData.visibleIndexByteOffset / 4u + instanceIndex;
    return gfxMetalResources.buffers[resourceIndex][base];
}

DemoObject gfx_load_demo_object_from_resource(uint resourceIndex, uint byteOffset, uint objectIndex) {
    uint base = byteOffset / 4u + objectIndex * GFX_DEMO_OBJECT_STRIDE_WORDS;

    DemoObject result;
    result.offsetScale = asfloat(uint4(gfxMetalResources.buffers[resourceIndex][base + 0u],
                                       gfxMetalResources.buffers[resourceIndex][base + 1u],
                                       gfxMetalResources.buffers[resourceIndex][base + 2u],
                                       gfxMetalResources.buffers[resourceIndex][base + 3u]));
    result.phaseOffset = asfloat(gfxMetalResources.buffers[resourceIndex][base + 4u]);
    result.materialIndex = gfxMetalResources.buffers[resourceIndex][base + 5u];
    result.objectId = gfxMetalResources.buffers[resourceIndex][base + 6u];
    result.flags = gfxMetalResources.buffers[resourceIndex][base + 7u];
    return result;
}

DemoObject gfx_load_demo_object(DrawRootData rootData, uint objectIndex) {
    return gfx_load_demo_object_from_resource(rootData.objectBuffer, rootData.objectByteOffset, objectIndex);
}

float4 gfx_sample_albedo(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfxMetalResources.textures[textureIndex].Sample(gfxMetalResources.samplers[samplerIndex], uv);
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
struct ResourceTable {
    Texture2D<float4> textures[GFX_RESOURCE_TABLE_CAPACITY];
    SamplerState samplers[GFX_RESOURCE_TABLE_CAPACITY];
    RWStructuredBuffer<uint> buffers[GFX_RESOURCE_TABLE_CAPACITY];
};

struct ComputeRootWords {
    uint4 words0;
    uint4 words1;
    uint4 words2;
    uint4 words3;
    uint4 words4;
    uint4 words5;
};

ConstantBuffer<ComputeRootWords> gfxMetalRootData : register(b8);
ParameterBlock<ResourceTable> gfxMetalResources : register(b9);

uint gfx_load_compute_root_word(uint index) {
    if (index < 4u) {
        return gfxMetalRootData.words0[index];
    }
    if (index < 8u) {
        return gfxMetalRootData.words1[index - 4u];
    }
    if (index < 12u) {
        return gfxMetalRootData.words2[index - 8u];
    }
    if (index < 16u) {
        return gfxMetalRootData.words3[index - 12u];
    }
    if (index < 20u) {
        return gfxMetalRootData.words4[index - 16u];
    }
    return gfxMetalRootData.words5[index - 20u];
}

MaterialComputeRootData gfx_load_compute_root_data() {
    MaterialComputeRootData result;
    result.materialCount = gfx_load_compute_root_word(0u);
    result.sourceMaterialBuffer = gfx_load_compute_root_word(1u);
    result.sourceMaterialByteOffset = gfx_load_compute_root_word(2u);
    result.materialBuffer = gfx_load_compute_root_word(3u);
    result.materialByteOffset = gfx_load_compute_root_word(4u);
    result.animationPhase = asfloat(gfx_load_compute_root_word(5u));
    result._padding0 = gfx_load_compute_root_word(6u);
    result._padding1 = gfx_load_compute_root_word(7u);
    return result;
}

CullBoundsComputeRootData gfx_load_cull_bounds_compute_root_data() {
    CullBoundsComputeRootData result;
    result.cullSourceCount = gfx_load_compute_root_word(0u);
    result.cullSourceBuffer = gfx_load_compute_root_word(1u);
    result.cullSourceByteOffset = gfx_load_compute_root_word(2u);
    result.cullObjectBuffer = gfx_load_compute_root_word(3u);
    result.cullObjectByteOffset = gfx_load_compute_root_word(4u);
    result._padding0 = gfx_load_compute_root_word(5u);
    result._padding1 = gfx_load_compute_root_word(6u);
    result._padding2 = gfx_load_compute_root_word(7u);
    return result;
}

VisibilityComputeRootData gfx_load_visibility_compute_root_data() {
    VisibilityComputeRootData result;
    result.binCount = gfx_load_compute_root_word(0u);
    result.groupCount = gfx_load_compute_root_word(1u);
    result.cullObjectBuffer = gfx_load_compute_root_word(2u);
    result.cullObjectByteOffset = gfx_load_compute_root_word(3u);
    result.sourceIndexBuffer = gfx_load_compute_root_word(4u);
    result.sourceIndexByteOffset = gfx_load_compute_root_word(5u);
    result.visibleIndexBuffer = gfx_load_compute_root_word(6u);
    result.visibleIndexByteOffset = gfx_load_compute_root_word(7u);
    result.indirectArgsBuffer = gfx_load_compute_root_word(8u);
    result.indirectArgsByteOffset = gfx_load_compute_root_word(9u);
    result.binBuffer = gfx_load_compute_root_word(10u);
    result.binByteOffset = gfx_load_compute_root_word(11u);
    result.groupBuffer = gfx_load_compute_root_word(12u);
    result.groupByteOffset = gfx_load_compute_root_word(13u);
    result.groupCountBuffer = gfx_load_compute_root_word(14u);
    result.groupCountByteOffset = gfx_load_compute_root_word(15u);
    result.groupOffsetBuffer = gfx_load_compute_root_word(16u);
    result.groupOffsetByteOffset = gfx_load_compute_root_word(17u);
    result._padding0 = gfx_load_compute_root_word(18u);
    result._padding1 = gfx_load_compute_root_word(19u);
    result._padding2 = gfx_load_compute_root_word(20u);
    result._padding3 = gfx_load_compute_root_word(21u);
    result._padding4 = gfx_load_compute_root_word(22u);
    result._padding5 = gfx_load_compute_root_word(23u);
    return result;
}

Material gfx_load_source_material(MaterialComputeRootData rootData, uint materialIndex) {
    uint resourceIndex = rootData.sourceMaterialBuffer;
    uint base = rootData.sourceMaterialByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;

    Material result;
    result.baseColor = asfloat(uint4(gfxMetalResources.buffers[resourceIndex][base + 0u],
                                     gfxMetalResources.buffers[resourceIndex][base + 1u],
                                     gfxMetalResources.buffers[resourceIndex][base + 2u],
                                     gfxMetalResources.buffers[resourceIndex][base + 3u]));
    result.albedoTexture = gfxMetalResources.buffers[resourceIndex][base + 4u];
    result.samplerIndex = gfxMetalResources.buffers[resourceIndex][base + 5u];
    result.flags = gfxMetalResources.buffers[resourceIndex][base + 6u];
    result._padding = gfxMetalResources.buffers[resourceIndex][base + 7u];
    return result;
}

void gfx_store_material(MaterialComputeRootData rootData, uint materialIndex, Material material) {
    uint resourceIndex = rootData.materialBuffer;
    uint base = rootData.materialByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;
    uint4 baseColor = asuint(material.baseColor);

    gfxMetalResources.buffers[resourceIndex][base + 0u] = baseColor.x;
    gfxMetalResources.buffers[resourceIndex][base + 1u] = baseColor.y;
    gfxMetalResources.buffers[resourceIndex][base + 2u] = baseColor.z;
    gfxMetalResources.buffers[resourceIndex][base + 3u] = baseColor.w;
    gfxMetalResources.buffers[resourceIndex][base + 4u] = material.albedoTexture;
    gfxMetalResources.buffers[resourceIndex][base + 5u] = material.samplerIndex;
    gfxMetalResources.buffers[resourceIndex][base + 6u] = material.flags;
    gfxMetalResources.buffers[resourceIndex][base + 7u] = material._padding;
}

VisibilityBin gfx_load_visibility_bin(VisibilityComputeRootData rootData, uint binIndex) {
    uint resourceIndex = rootData.binBuffer;
    uint base = rootData.binByteOffset / 4u + binIndex * GFX_DEMO_VISIBILITY_BIN_STRIDE_WORDS;

    VisibilityBin result;
    result.sourceIndexByteOffset = gfxMetalResources.buffers[resourceIndex][base + 0u];
    result.sourceIndexCount = gfxMetalResources.buffers[resourceIndex][base + 1u];
    result.visibleIndexByteOffset = gfxMetalResources.buffers[resourceIndex][base + 2u];
    result.indirectArgsByteOffset = gfxMetalResources.buffers[resourceIndex][base + 3u];
    result.groupStart = gfxMetalResources.buffers[resourceIndex][base + 4u];
    result.groupCount = gfxMetalResources.buffers[resourceIndex][base + 5u];
    result._padding2 = gfxMetalResources.buffers[resourceIndex][base + 6u];
    result._padding3 = gfxMetalResources.buffers[resourceIndex][base + 7u];
    return result;
}

VisibilityGroup gfx_load_visibility_group(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupBuffer;
    uint base = rootData.groupByteOffset / 4u + groupIndex * GFX_DEMO_VISIBILITY_GROUP_STRIDE_WORDS;

    VisibilityGroup result;
    result.sourceIndexByteOffset = gfxMetalResources.buffers[resourceIndex][base + 0u];
    result.sourceIndexCount = gfxMetalResources.buffers[resourceIndex][base + 1u];
    result.visibleIndexByteOffset = gfxMetalResources.buffers[resourceIndex][base + 2u];
    result.binIndex = gfxMetalResources.buffers[resourceIndex][base + 3u];
    result._padding0 = gfxMetalResources.buffers[resourceIndex][base + 4u];
    result._padding1 = gfxMetalResources.buffers[resourceIndex][base + 5u];
    result._padding2 = gfxMetalResources.buffers[resourceIndex][base + 6u];
    result._padding3 = gfxMetalResources.buffers[resourceIndex][base + 7u];
    return result;
}

CullObject gfx_load_demo_cull_object_from_resource(uint resourceIndex, uint byteOffset, uint objectIndex) {
    uint base = byteOffset / 4u + objectIndex * GFX_DEMO_CULL_OBJECT_STRIDE_WORDS;

    CullObject result;
    result.clipMin = asfloat(uint2(gfxMetalResources.buffers[resourceIndex][base + 0u],
                                   gfxMetalResources.buffers[resourceIndex][base + 1u]));
    result.clipMax = asfloat(uint2(gfxMetalResources.buffers[resourceIndex][base + 2u],
                                   gfxMetalResources.buffers[resourceIndex][base + 3u]));
    result.depthMin = asfloat(gfxMetalResources.buffers[resourceIndex][base + 4u]);
    result.depthMax = asfloat(gfxMetalResources.buffers[resourceIndex][base + 5u]);
    result.objectIndex = gfxMetalResources.buffers[resourceIndex][base + 6u];
    result.flags = gfxMetalResources.buffers[resourceIndex][base + 7u];
    return result;
}

CullObject gfx_load_demo_cull_object(VisibilityComputeRootData rootData, uint cullObjectIndex) {
    return gfx_load_demo_cull_object_from_resource(rootData.cullObjectBuffer,
                                                  rootData.cullObjectByteOffset,
                                                  cullObjectIndex);
}

CullSource gfx_load_demo_cull_source(CullBoundsComputeRootData rootData, uint sourceIndex) {
    uint resourceIndex = rootData.cullSourceBuffer;
    uint base = rootData.cullSourceByteOffset / 4u + sourceIndex * GFX_DEMO_CULL_SOURCE_STRIDE_WORDS;

    CullSource result;
    result.localMin = asfloat(uint2(gfxMetalResources.buffers[resourceIndex][base + 0u],
                                    gfxMetalResources.buffers[resourceIndex][base + 1u]));
    result.localMax = asfloat(uint2(gfxMetalResources.buffers[resourceIndex][base + 2u],
                                    gfxMetalResources.buffers[resourceIndex][base + 3u]));
    result.offsetScale = asfloat(uint4(gfxMetalResources.buffers[resourceIndex][base + 4u],
                                       gfxMetalResources.buffers[resourceIndex][base + 5u],
                                       gfxMetalResources.buffers[resourceIndex][base + 6u],
                                       gfxMetalResources.buffers[resourceIndex][base + 7u]));
    result.objectIndex = gfxMetalResources.buffers[resourceIndex][base + 8u];
    result.flags = gfxMetalResources.buffers[resourceIndex][base + 9u];
    result.maxAnimationScale = asfloat(gfxMetalResources.buffers[resourceIndex][base + 10u]);
    result.wobbleExtent = asfloat(gfxMetalResources.buffers[resourceIndex][base + 11u]);
    return result;
}

void gfx_store_demo_cull_object(CullBoundsComputeRootData rootData, uint cullObjectIndex, CullObject object) {
    uint resourceIndex = rootData.cullObjectBuffer;
    uint base = rootData.cullObjectByteOffset / 4u + cullObjectIndex * GFX_DEMO_CULL_OBJECT_STRIDE_WORDS;
    uint2 clipMin = asuint(object.clipMin);
    uint2 clipMax = asuint(object.clipMax);

    gfxMetalResources.buffers[resourceIndex][base + 0u] = clipMin.x;
    gfxMetalResources.buffers[resourceIndex][base + 1u] = clipMin.y;
    gfxMetalResources.buffers[resourceIndex][base + 2u] = clipMax.x;
    gfxMetalResources.buffers[resourceIndex][base + 3u] = clipMax.y;
    gfxMetalResources.buffers[resourceIndex][base + 4u] = asuint(object.depthMin);
    gfxMetalResources.buffers[resourceIndex][base + 5u] = asuint(object.depthMax);
    gfxMetalResources.buffers[resourceIndex][base + 6u] = object.objectIndex;
    gfxMetalResources.buffers[resourceIndex][base + 7u] = object.flags;
}

uint gfx_load_source_cull_object_index(VisibilityComputeRootData rootData, uint sourceIndexByteOffset) {
    uint resourceIndex = rootData.sourceIndexBuffer;
    uint base = (rootData.sourceIndexByteOffset + sourceIndexByteOffset) / 4u;
    return gfxMetalResources.buffers[resourceIndex][base];
}

void gfx_store_visible_object_index(VisibilityComputeRootData rootData, uint visibleIndexByteOffset, uint objectIndex) {
    uint resourceIndex = rootData.visibleIndexBuffer;
    uint base = (rootData.visibleIndexByteOffset + visibleIndexByteOffset) / 4u;
    gfxMetalResources.buffers[resourceIndex][base] = objectIndex;
}

uint gfx_load_visibility_group_count(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupCountBuffer;
    uint base = rootData.groupCountByteOffset / 4u + groupIndex;
    return gfxMetalResources.buffers[resourceIndex][base];
}

void gfx_store_visibility_group_count(VisibilityComputeRootData rootData, uint groupIndex, uint count) {
    uint resourceIndex = rootData.groupCountBuffer;
    uint base = rootData.groupCountByteOffset / 4u + groupIndex;
    gfxMetalResources.buffers[resourceIndex][base] = count;
}

uint gfx_load_visibility_group_offset(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupOffsetBuffer;
    uint base = rootData.groupOffsetByteOffset / 4u + groupIndex;
    return gfxMetalResources.buffers[resourceIndex][base];
}

void gfx_store_visibility_group_offset(VisibilityComputeRootData rootData, uint groupIndex, uint offset) {
    uint resourceIndex = rootData.groupOffsetBuffer;
    uint base = rootData.groupOffsetByteOffset / 4u + groupIndex;
    gfxMetalResources.buffers[resourceIndex][base] = offset;
}

void gfx_store_draw_indexed_indirect_args(VisibilityComputeRootData rootData,
                                          uint indirectArgsByteOffset,
                                          DrawIndexedIndirectArgs args) {
    uint resourceIndex = rootData.indirectArgsBuffer;
    uint base = (rootData.indirectArgsByteOffset + indirectArgsByteOffset) / 4u;
    gfxMetalResources.buffers[resourceIndex][base + 0u] = args.indexCount;
    gfxMetalResources.buffers[resourceIndex][base + 1u] = args.instanceCount;
    gfxMetalResources.buffers[resourceIndex][base + 2u] = args.firstIndex;
    gfxMetalResources.buffers[resourceIndex][base + 3u] = uint(args.baseVertex);
    gfxMetalResources.buffers[resourceIndex][base + 4u] = args.firstInstance;
}
#endif

#else
#error Define one shader backend target.
#endif

#endif
