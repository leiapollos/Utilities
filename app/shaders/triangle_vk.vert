#version 450

#define GFX_SHADER_SLOT_DRAW_DATA 8
#define GFX_DRAW_DATA_STRIDE_BYTES 32u

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUv;

struct DrawData {
    vec4 offsetScale;
    uint materialIndex;
    uint objectId;
    uint flags;
    uint _padding;
};

layout(set = 1, binding = GFX_SHADER_SLOT_DRAW_DATA, std430) readonly buffer DrawDataBuffer {
    DrawData draws[];
} drawDataBuffer;

layout(push_constant) uniform PushConstants {
    uint dataByteOffset;
    uint passByteOffset;
    uint _padding0;
    uint _padding1;
} pushConstants;

void main() {
    DrawData drawData = drawDataBuffer.draws[pushConstants.dataByteOffset / GFX_DRAW_DATA_STRIDE_BYTES];
    vec2 position = inPosition * drawData.offsetScale.z + drawData.offsetScale.xy;
    gl_Position = vec4(position, 0.0, 1.0);
    outColor = inColor;
    outUv = vec2(inPosition.x * (1.0 / 1.1) + 0.5, 1.0 - (inPosition.y + 0.45));
}
