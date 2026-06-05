#include <metal_stdlib>
using namespace metal;

#define GFX_SHADER_SLOT_DRAW_DATA 8
#define GFX_SHADER_SLOT_RESOURCE_TABLE 9
#define GFX_SHADER_SLOT_PASS_DATA 10
#define GFX_RESOURCE_TABLE_CAPACITY 256
#define GFX_RESOURCE_TABLE_SAMPLER_BASE GFX_RESOURCE_TABLE_CAPACITY

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

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
    uint sampler;
    uint flags;
    uint _padding;
};

struct ResourceTable {
    array<texture2d<float>, GFX_RESOURCE_TABLE_CAPACITY> textures [[id(0)]];
    array<sampler, GFX_RESOURCE_TABLE_CAPACITY> samplers [[id(GFX_RESOURCE_TABLE_SAMPLER_BASE)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]], constant DrawData& drawData [[buffer(GFX_SHADER_SLOT_DRAW_DATA)]]) {
    VertexOut out;
    float2 position = in.position * drawData.offsetScale.z + drawData.offsetScale.xy;
    out.position = float4(position, 0.0, 1.0);
    out.color = in.color;
    out.uv = float2(in.position.x * (1.0 / 1.1) + 0.5, 1.0 - (in.position.y + 0.45));
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant DrawData& drawData [[buffer(GFX_SHADER_SLOT_DRAW_DATA)]],
                              constant ResourceTable& resources [[buffer(GFX_SHADER_SLOT_RESOURCE_TABLE)]],
                              device const Material* materials [[buffer(GFX_SHADER_SLOT_PASS_DATA)]]) {
    uint materialIndex = drawData.materialIndex;
    uint albedoTexture = materials[materialIndex].albedoTexture;
    uint samplerIndex = materials[materialIndex].sampler;
    float4 albedo = resources.textures[albedoTexture].sample(resources.samplers[samplerIndex], in.uv);
    return in.color * materials[materialIndex].baseColor * albedo;
}
