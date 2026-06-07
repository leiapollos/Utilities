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
    float4 offsetScale;
    uint materialIndex;
    uint materialBuffer;
    uint materialByteOffset;
    uint objectId;
    uint flags;
    float animationPhase;
    uint _padding0;
    uint _padding1;
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

struct VertexIn {
    float2 position : POSITION;
    float4 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
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
    result.offsetScale = gfx_load_float4(resourceIndex, byteOffset, 0u);
    uint base = byteOffset / 4u;
    result.materialIndex = gfxBuffers[resourceIndex][base + 4u];
    result.materialBuffer = gfxBuffers[resourceIndex][base + 5u];
    result.materialByteOffset = gfxBuffers[resourceIndex][base + 6u];
    result.objectId = gfxBuffers[resourceIndex][base + 7u];
    result.flags = gfxBuffers[resourceIndex][base + 8u];
    result.animationPhase = asfloat(gfxBuffers[resourceIndex][base + 9u]);
    result._padding0 = gfxBuffers[resourceIndex][base + 10u];
    result._padding1 = gfxBuffers[resourceIndex][base + 11u];
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

ConstantBuffer<MaterialComputeRootData> gfxMetalRootData : register(b8);
ParameterBlock<ResourceTable> gfxMetalResources : register(b9);

MaterialComputeRootData gfx_load_compute_root_data() {
    return gfxMetalRootData;
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
#endif

#else
#error Define one shader backend target.
#endif

#endif
