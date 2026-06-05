#version 450

#define GFX_SHADER_SLOT_DRAW_DATA 8
#define GFX_SHADER_SLOT_PASS_DATA 10
#define GFX_RESOURCE_TABLE_CAPACITY 256
#define GFX_DRAW_DATA_STRIDE_BYTES 32u
#define GFX_MATERIAL_STRIDE_BYTES 32u

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

struct DrawData {
    vec4 offsetScale;
    uint materialIndex;
    uint objectId;
    uint flags;
    uint _padding;
};

struct Material {
    vec4 baseColor;
    uint albedoTexture;
    uint samplerIndex;
    uint flags;
    uint _padding;
};

layout(set = 0, binding = 0) uniform texture2D gfxTextures[GFX_RESOURCE_TABLE_CAPACITY];
layout(set = 0, binding = 1) uniform sampler gfxSamplers[GFX_RESOURCE_TABLE_CAPACITY];

layout(set = 1, binding = GFX_SHADER_SLOT_DRAW_DATA, std430) readonly buffer DrawDataBuffer {
    DrawData draws[];
} drawDataBuffer;

layout(set = 1, binding = GFX_SHADER_SLOT_PASS_DATA, std430) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(push_constant) uniform PushConstants {
    uint dataByteOffset;
    uint passByteOffset;
    uint _padding0;
    uint _padding1;
} pushConstants;

void main() {
    DrawData drawData = drawDataBuffer.draws[pushConstants.dataByteOffset / GFX_DRAW_DATA_STRIDE_BYTES];
    Material material = materialBuffer.materials[(pushConstants.passByteOffset / GFX_MATERIAL_STRIDE_BYTES) + drawData.materialIndex];
    vec4 albedo = texture(sampler2D(gfxTextures[material.albedoTexture], gfxSamplers[material.samplerIndex]), inUv);
    outColor = inColor * material.baseColor * albedo;
}
