#ifndef DEMO_SHADER_SUPPORT_SLANG
#define DEMO_SHADER_SUPPORT_SLANG

#include "gfx_shader_abi.slang"

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

#if GFX_SHADER_STAGE_GRAPHICS
DrawRootData gfx_load_draw_root_data() {
    DrawRootData result;
    result.vertexBuffer = gfx_load_root_word(0u);
    result.vertexByteOffset = gfx_load_root_word(1u);
    result.objectBuffer = gfx_load_root_word(2u);
    result.objectByteOffset = gfx_load_root_word(3u);
    result.visibleIndexBuffer = gfx_load_root_word(4u);
    result.visibleIndexByteOffset = gfx_load_root_word(5u);
    result.materialBuffer = gfx_load_root_word(6u);
    result.materialByteOffset = gfx_load_root_word(7u);
    result.animationPhase = asfloat(gfx_load_root_word(8u));
    result._padding0 = gfx_load_root_word(9u);
    result._padding1 = gfx_load_root_word(10u);
    result._padding2 = gfx_load_root_word(11u);
    result._padding3 = gfx_load_root_word(12u);
    result._padding4 = gfx_load_root_word(13u);
    result._padding5 = gfx_load_root_word(14u);
    result._padding6 = gfx_load_root_word(15u);
    return result;
}

Material gfx_load_material(DrawRootData rootData, uint materialIndex) {
    uint resourceIndex = rootData.materialBuffer;
    uint baseByteOffset = rootData.materialByteOffset;
    uint baseWordOffset = materialIndex * GFX_MATERIAL_STRIDE_WORDS;

    Material result;
    result.baseColor = gfx_load_buffer_float4(resourceIndex, baseByteOffset, baseWordOffset);
    result.albedoTexture = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 4u);
    result.samplerIndex = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 5u);
    result.flags = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 6u);
    result._padding = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 7u);
    return result;
}

DemoVertex gfx_load_demo_vertex(DrawRootData rootData, uint vertexIndex) {
    uint resourceIndex = rootData.vertexBuffer;
    uint baseByteOffset = rootData.vertexByteOffset;
    uint baseWordOffset = vertexIndex * GFX_DEMO_VERTEX_STRIDE_WORDS;

    DemoVertex result;
    result.position = asfloat(uint2(gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 0u),
                                    gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 1u)));
    result.color = gfx_load_buffer_float4(resourceIndex, baseByteOffset, baseWordOffset + 2u);
    return result;
}

uint gfx_load_visible_object_index(DrawRootData rootData, uint instanceIndex) {
    return gfx_load_buffer_word(rootData.visibleIndexBuffer,
                                rootData.visibleIndexByteOffset,
                                instanceIndex);
}

DemoObject gfx_load_demo_object_from_resource(uint resourceIndex, uint byteOffset, uint objectIndex) {
    uint baseWordOffset = objectIndex * GFX_DEMO_OBJECT_STRIDE_WORDS;

    DemoObject result;
    result.offsetScale = gfx_load_buffer_float4(resourceIndex, byteOffset, baseWordOffset);
    result.phaseOffset = asfloat(gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 4u));
    result.materialIndex = gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 5u);
    result.objectId = gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 6u);
    result.flags = gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 7u);
    return result;
}

DemoObject gfx_load_demo_object(DrawRootData rootData, uint objectIndex) {
    return gfx_load_demo_object_from_resource(rootData.objectBuffer, rootData.objectByteOffset, objectIndex);
}

float4 gfx_sample_albedo(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfx_sample_texture2d(textureIndex, samplerIndex, uv);
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
MaterialComputeRootData gfx_load_compute_root_data() {
    MaterialComputeRootData result;
    result.materialCount = gfx_load_root_word(0u);
    result.sourceMaterialBuffer = gfx_load_root_word(1u);
    result.sourceMaterialByteOffset = gfx_load_root_word(2u);
    result.materialBuffer = gfx_load_root_word(3u);
    result.materialByteOffset = gfx_load_root_word(4u);
    result.animationPhase = asfloat(gfx_load_root_word(5u));
    result._padding0 = gfx_load_root_word(6u);
    result._padding1 = gfx_load_root_word(7u);
    return result;
}

CullBoundsComputeRootData gfx_load_cull_bounds_compute_root_data() {
    CullBoundsComputeRootData result;
    result.cullSourceCount = gfx_load_root_word(0u);
    result.cullSourceBuffer = gfx_load_root_word(1u);
    result.cullSourceByteOffset = gfx_load_root_word(2u);
    result.cullObjectBuffer = gfx_load_root_word(3u);
    result.cullObjectByteOffset = gfx_load_root_word(4u);
    result._padding0 = gfx_load_root_word(5u);
    result._padding1 = gfx_load_root_word(6u);
    result._padding2 = gfx_load_root_word(7u);
    return result;
}

VisibilityComputeRootData gfx_load_visibility_compute_root_data() {
    VisibilityComputeRootData result;
    result.binCount = gfx_load_root_word(0u);
    result.groupCount = gfx_load_root_word(1u);
    result.cullObjectBuffer = gfx_load_root_word(2u);
    result.cullObjectByteOffset = gfx_load_root_word(3u);
    result.sourceIndexBuffer = gfx_load_root_word(4u);
    result.sourceIndexByteOffset = gfx_load_root_word(5u);
    result.visibleIndexBuffer = gfx_load_root_word(6u);
    result.visibleIndexByteOffset = gfx_load_root_word(7u);
    result.indirectArgsBuffer = gfx_load_root_word(8u);
    result.indirectArgsByteOffset = gfx_load_root_word(9u);
    result.binBuffer = gfx_load_root_word(10u);
    result.binByteOffset = gfx_load_root_word(11u);
    result.groupBuffer = gfx_load_root_word(12u);
    result.groupByteOffset = gfx_load_root_word(13u);
    result.groupCountBuffer = gfx_load_root_word(14u);
    result.groupCountByteOffset = gfx_load_root_word(15u);
    result.groupOffsetBuffer = gfx_load_root_word(16u);
    result.groupOffsetByteOffset = gfx_load_root_word(17u);
    result._padding0 = gfx_load_root_word(18u);
    result._padding1 = gfx_load_root_word(19u);
    result._padding2 = gfx_load_root_word(20u);
    result._padding3 = gfx_load_root_word(21u);
    result._padding4 = gfx_load_root_word(22u);
    result._padding5 = gfx_load_root_word(23u);
    return result;
}

Material gfx_load_source_material(MaterialComputeRootData rootData, uint materialIndex) {
    uint resourceIndex = rootData.sourceMaterialBuffer;
    uint baseByteOffset = rootData.sourceMaterialByteOffset;
    uint baseWordOffset = materialIndex * GFX_MATERIAL_STRIDE_WORDS;

    Material result;
    result.baseColor = gfx_load_buffer_float4(resourceIndex, baseByteOffset, baseWordOffset);
    result.albedoTexture = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 4u);
    result.samplerIndex = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 5u);
    result.flags = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 6u);
    result._padding = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 7u);
    return result;
}

void gfx_store_material(MaterialComputeRootData rootData, uint materialIndex, Material material) {
    uint resourceIndex = rootData.materialBuffer;
    uint baseByteOffset = rootData.materialByteOffset;
    uint baseWordOffset = materialIndex * GFX_MATERIAL_STRIDE_WORDS;
    uint4 baseColor = asuint(material.baseColor);

    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 0u, baseColor.x);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 1u, baseColor.y);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 2u, baseColor.z);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 3u, baseColor.w);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 4u, material.albedoTexture);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 5u, material.samplerIndex);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 6u, material.flags);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 7u, material._padding);
}

CullSource gfx_load_demo_cull_source(CullBoundsComputeRootData rootData, uint sourceIndex) {
    uint resourceIndex = rootData.cullSourceBuffer;
    uint baseByteOffset = rootData.cullSourceByteOffset;
    uint baseWordOffset = sourceIndex * GFX_DEMO_CULL_SOURCE_STRIDE_WORDS;

    CullSource result;
    result.localMin = asfloat(uint2(gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 0u),
                                    gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 1u)));
    result.localMax = asfloat(uint2(gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 2u),
                                    gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 3u)));
    result.offsetScale = gfx_load_buffer_float4(resourceIndex, baseByteOffset, baseWordOffset + 4u);
    result.objectIndex = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 8u);
    result.flags = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 9u);
    result.maxAnimationScale = asfloat(gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 10u));
    result.wobbleExtent = asfloat(gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 11u));
    return result;
}

void gfx_store_demo_cull_object(CullBoundsComputeRootData rootData, uint cullObjectIndex, CullObject object) {
    uint resourceIndex = rootData.cullObjectBuffer;
    uint baseByteOffset = rootData.cullObjectByteOffset;
    uint baseWordOffset = cullObjectIndex * GFX_DEMO_CULL_OBJECT_STRIDE_WORDS;
    uint2 clipMin = asuint(object.clipMin);
    uint2 clipMax = asuint(object.clipMax);

    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 0u, clipMin.x);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 1u, clipMin.y);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 2u, clipMax.x);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 3u, clipMax.y);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 4u, asuint(object.depthMin));
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 5u, asuint(object.depthMax));
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 6u, object.objectIndex);
    gfx_store_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 7u, object.flags);
}

CullObject gfx_load_demo_cull_object_from_resource(uint resourceIndex, uint byteOffset, uint objectIndex) {
    uint baseWordOffset = objectIndex * GFX_DEMO_CULL_OBJECT_STRIDE_WORDS;

    CullObject result;
    result.clipMin = asfloat(uint2(gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 0u),
                                   gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 1u)));
    result.clipMax = asfloat(uint2(gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 2u),
                                   gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 3u)));
    result.depthMin = asfloat(gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 4u));
    result.depthMax = asfloat(gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 5u));
    result.objectIndex = gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 6u);
    result.flags = gfx_load_buffer_word(resourceIndex, byteOffset, baseWordOffset + 7u);
    return result;
}

CullObject gfx_load_demo_cull_object(VisibilityComputeRootData rootData, uint cullObjectIndex) {
    return gfx_load_demo_cull_object_from_resource(rootData.cullObjectBuffer,
                                                  rootData.cullObjectByteOffset,
                                                  cullObjectIndex);
}

VisibilityBin gfx_load_visibility_bin(VisibilityComputeRootData rootData, uint binIndex) {
    uint resourceIndex = rootData.binBuffer;
    uint baseByteOffset = rootData.binByteOffset;
    uint baseWordOffset = binIndex * GFX_DEMO_VISIBILITY_BIN_STRIDE_WORDS;

    VisibilityBin result;
    result.sourceIndexByteOffset = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 0u);
    result.sourceIndexCount = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 1u);
    result.visibleIndexByteOffset = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 2u);
    result.indirectArgsByteOffset = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 3u);
    result.groupStart = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 4u);
    result.groupCount = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 5u);
    result._padding2 = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 6u);
    result._padding3 = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 7u);
    return result;
}

VisibilityGroup gfx_load_visibility_group(VisibilityComputeRootData rootData, uint groupIndex) {
    uint resourceIndex = rootData.groupBuffer;
    uint baseByteOffset = rootData.groupByteOffset;
    uint baseWordOffset = groupIndex * GFX_DEMO_VISIBILITY_GROUP_STRIDE_WORDS;

    VisibilityGroup result;
    result.sourceIndexByteOffset = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 0u);
    result.sourceIndexCount = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 1u);
    result.visibleIndexByteOffset = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 2u);
    result.binIndex = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 3u);
    result._padding0 = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 4u);
    result._padding1 = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 5u);
    result._padding2 = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 6u);
    result._padding3 = gfx_load_buffer_word(resourceIndex, baseByteOffset, baseWordOffset + 7u);
    return result;
}

uint gfx_load_source_cull_object_index(VisibilityComputeRootData rootData, uint sourceIndexByteOffset) {
    return gfx_load_buffer_word(rootData.sourceIndexBuffer,
                                rootData.sourceIndexByteOffset + sourceIndexByteOffset,
                                0u);
}

void gfx_store_visible_object_index(VisibilityComputeRootData rootData, uint visibleIndexByteOffset, uint objectIndex) {
    gfx_store_buffer_word(rootData.visibleIndexBuffer,
                          rootData.visibleIndexByteOffset + visibleIndexByteOffset,
                          0u,
                          objectIndex);
}

uint gfx_load_visibility_group_count(VisibilityComputeRootData rootData, uint groupIndex) {
    return gfx_load_buffer_word(rootData.groupCountBuffer, rootData.groupCountByteOffset, groupIndex);
}

void gfx_store_visibility_group_count(VisibilityComputeRootData rootData, uint groupIndex, uint count) {
    gfx_store_buffer_word(rootData.groupCountBuffer, rootData.groupCountByteOffset, groupIndex, count);
}

uint gfx_load_visibility_group_offset(VisibilityComputeRootData rootData, uint groupIndex) {
    return gfx_load_buffer_word(rootData.groupOffsetBuffer, rootData.groupOffsetByteOffset, groupIndex);
}

void gfx_store_visibility_group_offset(VisibilityComputeRootData rootData, uint groupIndex, uint offset) {
    gfx_store_buffer_word(rootData.groupOffsetBuffer, rootData.groupOffsetByteOffset, groupIndex, offset);
}

void gfx_store_draw_indexed_indirect_args(VisibilityComputeRootData rootData,
                                          uint indirectArgsByteOffset,
                                          DrawIndexedIndirectArgs args) {
    uint baseByteOffset = rootData.indirectArgsByteOffset + indirectArgsByteOffset;
    gfx_store_buffer_word(rootData.indirectArgsBuffer, baseByteOffset, 0u, args.indexCount);
    gfx_store_buffer_word(rootData.indirectArgsBuffer, baseByteOffset, 1u, args.instanceCount);
    gfx_store_buffer_word(rootData.indirectArgsBuffer, baseByteOffset, 2u, args.firstIndex);
    gfx_store_buffer_word(rootData.indirectArgsBuffer, baseByteOffset, 3u, uint(args.baseVertex));
    gfx_store_buffer_word(rootData.indirectArgsBuffer, baseByteOffset, 4u, args.firstInstance);
}
#endif

#endif
