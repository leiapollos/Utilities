#ifndef GFX_SHADER_ABI_SLANG
#define GFX_SHADER_ABI_SLANG

static const uint GFX_SHADER_SLOT_ROOT_DATA = 8u;
static const uint GFX_SHADER_SLOT_RESOURCE_TABLE = 9u;
static const uint GFX_RESOURCE_TABLE_CAPACITY = 256u;
static const uint GFX_RESOURCE_BINDING_TEXTURES = 0u;
static const uint GFX_RESOURCE_BINDING_SAMPLERS = 1u;
static const uint GFX_RESOURCE_BINDING_BUFFERS = 2u;

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

uint gfx_load_root_word(uint wordOffset) {
    uint base = gfxPushConstants.rootByteOffset / 4u;
    return gfxBuffers[gfxPushConstants.rootResource][base + wordOffset];
}

uint gfx_load_buffer_word(uint resourceIndex, uint byteOffset, uint wordOffset) {
    uint base = byteOffset / 4u + wordOffset;
    return gfxBuffers[resourceIndex][base];
}

void gfx_store_buffer_word(uint resourceIndex, uint byteOffset, uint wordOffset, uint value) {
    uint base = byteOffset / 4u + wordOffset;
    gfxBuffers[resourceIndex][base] = value;
}

float4 gfx_load_buffer_float4(uint resourceIndex, uint byteOffset, uint wordOffset) {
    return asfloat(uint4(gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 0u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 1u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 2u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 3u)));
}

float4 gfx_sample_texture2d(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfxTextures[textureIndex].Sample(gfxSamplers[samplerIndex], uv);
}

#elif GFX_SHADER_TARGET_METAL

#if GFX_SHADER_STAGE_GRAPHICS
struct GfxResourceTable {
    Texture2D<float4> textures[GFX_RESOURCE_TABLE_CAPACITY];
    SamplerState samplers[GFX_RESOURCE_TABLE_CAPACITY];
    StructuredBuffer<uint> buffers[GFX_RESOURCE_TABLE_CAPACITY];
};

struct GfxGraphicsRootWords {
    uint4 words0;
    uint4 words1;
    uint4 words2;
    uint4 words3;
};

ConstantBuffer<GfxGraphicsRootWords> gfxRootData : register(b8);
ParameterBlock<GfxResourceTable> gfxResources : register(b9);

uint gfx_load_root_word(uint index) {
    if (index < 4u) {
        return gfxRootData.words0[index];
    }
    if (index < 8u) {
        return gfxRootData.words1[index - 4u];
    }
    if (index < 12u) {
        return gfxRootData.words2[index - 8u];
    }
    return gfxRootData.words3[index - 12u];
}

uint gfx_load_buffer_word(uint resourceIndex, uint byteOffset, uint wordOffset) {
    uint base = byteOffset / 4u + wordOffset;
    return gfxResources.buffers[resourceIndex][base];
}

float4 gfx_load_buffer_float4(uint resourceIndex, uint byteOffset, uint wordOffset) {
    return asfloat(uint4(gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 0u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 1u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 2u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 3u)));
}

float4 gfx_sample_texture2d(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfxResources.textures[textureIndex].Sample(gfxResources.samplers[samplerIndex], uv);
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
struct GfxResourceTable {
    Texture2D<float4> textures[GFX_RESOURCE_TABLE_CAPACITY];
    SamplerState samplers[GFX_RESOURCE_TABLE_CAPACITY];
    RWStructuredBuffer<uint> buffers[GFX_RESOURCE_TABLE_CAPACITY];
};

struct GfxComputeRootWords {
    uint4 words0;
    uint4 words1;
    uint4 words2;
    uint4 words3;
    uint4 words4;
    uint4 words5;
};

ConstantBuffer<GfxComputeRootWords> gfxRootData : register(b8);
ParameterBlock<GfxResourceTable> gfxResources : register(b9);

uint gfx_load_root_word(uint index) {
    if (index < 4u) {
        return gfxRootData.words0[index];
    }
    if (index < 8u) {
        return gfxRootData.words1[index - 4u];
    }
    if (index < 12u) {
        return gfxRootData.words2[index - 8u];
    }
    if (index < 16u) {
        return gfxRootData.words3[index - 12u];
    }
    if (index < 20u) {
        return gfxRootData.words4[index - 16u];
    }
    return gfxRootData.words5[index - 20u];
}

uint gfx_load_buffer_word(uint resourceIndex, uint byteOffset, uint wordOffset) {
    uint base = byteOffset / 4u + wordOffset;
    return gfxResources.buffers[resourceIndex][base];
}

void gfx_store_buffer_word(uint resourceIndex, uint byteOffset, uint wordOffset, uint value) {
    uint base = byteOffset / 4u + wordOffset;
    gfxResources.buffers[resourceIndex][base] = value;
}

float4 gfx_load_buffer_float4(uint resourceIndex, uint byteOffset, uint wordOffset) {
    return asfloat(uint4(gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 0u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 1u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 2u),
                         gfx_load_buffer_word(resourceIndex, byteOffset, wordOffset + 3u)));
}

float4 gfx_sample_texture2d(uint textureIndex, uint samplerIndex, float2 uv) {
    return gfxResources.textures[textureIndex].Sample(gfxResources.samplers[samplerIndex], uv);
}
#endif

#else
#error Define one shader backend target.
#endif

#endif
