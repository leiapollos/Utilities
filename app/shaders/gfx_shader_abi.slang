#ifndef GFX_SHADER_ABI_SLANG
#define GFX_SHADER_ABI_SLANG

static const uint GFX_SHADER_SLOT_DRAW_DATA = 8u;
static const uint GFX_SHADER_SLOT_DISPATCH_DATA = GFX_SHADER_SLOT_DRAW_DATA;
static const uint GFX_SHADER_SLOT_RESOURCE_TABLE = 9u;
static const uint GFX_SHADER_SLOT_PASS_DATA = 10u;
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

struct DrawData {
    float4 offsetScale;
    uint materialIndex;
    uint objectId;
    uint flags;
    uint _padding;
};

struct Material {
    float4 baseColor;
    uint albedoTexture;
    uint samplerIndex;
    uint flags;
    uint _padding;
};

struct MaterialComputeData {
    uint materialCount;
    uint columns;
    uint rows;
    uint albedoTexture;
    uint samplerIndex;
    uint _padding0;
    uint _padding1;
    uint _padding2;
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
    uint dataByteOffset;
    uint passByteOffset;
    uint dataResource;
    uint passResource;
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

DrawData gfx_load_draw_data() {
    uint resourceIndex = gfxPushConstants.dataResource;
    uint byteOffset = gfxPushConstants.dataByteOffset;

    DrawData result;
    result.offsetScale = gfx_load_float4(resourceIndex, byteOffset, 0u);
    uint base = byteOffset / 4u;
    result.materialIndex = gfxBuffers[resourceIndex][base + 4u];
    result.objectId = gfxBuffers[resourceIndex][base + 5u];
    result.flags = gfxBuffers[resourceIndex][base + 6u];
    result._padding = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

MaterialComputeData gfx_load_compute_data() {
    uint resourceIndex = gfxPushConstants.dataResource;
    uint base = gfxPushConstants.dataByteOffset / 4u;

    MaterialComputeData result;
    result.materialCount = gfxBuffers[resourceIndex][base + 0u];
    result.columns = gfxBuffers[resourceIndex][base + 1u];
    result.rows = gfxBuffers[resourceIndex][base + 2u];
    result.albedoTexture = gfxBuffers[resourceIndex][base + 3u];
    result.samplerIndex = gfxBuffers[resourceIndex][base + 4u];
    result._padding0 = gfxBuffers[resourceIndex][base + 5u];
    result._padding1 = gfxBuffers[resourceIndex][base + 6u];
    result._padding2 = gfxBuffers[resourceIndex][base + 7u];
    return result;
}

Material gfx_load_material(uint materialIndex) {
    uint resourceIndex = gfxPushConstants.passResource;
    uint base = gfxPushConstants.passByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;

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

void gfx_store_material(uint materialIndex, Material material) {
    uint resourceIndex = gfxPushConstants.passResource;
    uint base = gfxPushConstants.passByteOffset / 4u + materialIndex * GFX_MATERIAL_STRIDE_WORDS;
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
};

ConstantBuffer<DrawData> gfxMetalDrawData : register(b8);
ParameterBlock<ResourceTable> gfxMetalResources : register(b9);
StructuredBuffer<Material> gfxMetalMaterials : register(t10);

DrawData gfx_load_draw_data() {
    return gfxMetalDrawData;
}

Material gfx_load_material(uint materialIndex) {
    return gfxMetalMaterials[materialIndex];
}

float4 gfx_sample_albedo(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfxMetalResources.textures[textureIndex].Sample(gfxMetalResources.samplers[samplerIndex], uv);
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
ConstantBuffer<MaterialComputeData> gfxMetalComputeData : register(b8);
RWStructuredBuffer<Material> gfxMetalMaterialWrites : register(u10);

MaterialComputeData gfx_load_compute_data() {
    return gfxMetalComputeData;
}

void gfx_store_material(uint materialIndex, Material material) {
    gfxMetalMaterialWrites[materialIndex] = material;
}
#endif

#else
#error Define one shader backend target.
#endif

#endif
