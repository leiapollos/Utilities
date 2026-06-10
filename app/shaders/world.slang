#include "gfx_shader_abi.slang"
#include "shader_records.generated.hpp"

#if GFX_SHADER_STAGE_GRAPHICS

static const uint WORLD_MATERIAL_FLAG_ALPHA_TEST = 1u;
static const uint WORLD_MATERIAL_FLAG_TEXTURED = 2u;

// transform arrays are column-major: flat index = column * 4 + row.
float4 world_mul_point(float m[16], float3 p) {
    return float4(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                  m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                  m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
                  m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15]);
}

float4 world_mul_vec4(float m[16], float4 v) {
    return float4(m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                  m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                  m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                  m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
}

float3 world_mul_dir(float m[16], float3 d) {
    return float3(m[0] * d.x + m[4] * d.y + m[8] * d.z,
                  m[1] * d.x + m[5] * d.y + m[9] * d.z,
                  m[2] * d.x + m[6] * d.y + m[10] * d.z);
}

struct WorldVertexOut {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialIndex : TEXCOORD1;
};

[shader("vertex")]
WorldVertexOut world_vertex(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    WorldForwardRootData rootData = shd_load_WorldForwardRootData_root();
    WorldFrameRecord frame = shd_load_WorldFrameRecord(rootData.frameBuffer, rootData.frameByteOffset, 0u);

    uint cellBase = gfx_load_buffer_word(rootData.cellOffsetBuffer, rootData.cellOffsetByteOffset, rootData.cellIndex);
    uint renderableIndex = gfx_load_buffer_word(rootData.visibleBuffer, rootData.visibleByteOffset, cellBase + instanceId);
    WorldRenderableRecord renderable = shd_load_WorldRenderableRecord(rootData.renderableBuffer, rootData.renderableByteOffset, renderableIndex);
    WorldVertexRecord vertex = shd_load_WorldVertexRecord(rootData.vertexBuffer, rootData.vertexByteOffset, vertexId);

    float4 worldPos = world_mul_point(renderable.transform, float3(vertex.position[0], vertex.position[1], vertex.position[2]));
    float3 worldNormal = world_mul_dir(renderable.transform, float3(vertex.normal[0], vertex.normal[1], vertex.normal[2]));

    WorldVertexOut output;
    output.position = world_mul_vec4(frame.viewProj, worldPos);
    output.normal = worldNormal;
    output.uv = float2(vertex.uv[0], vertex.uv[1]);
    output.materialIndex = renderable.materialIndex;
    return output;
}

[shader("fragment")]
float4 world_fragment(WorldVertexOut input) : SV_Target0 {
    WorldForwardRootData rootData = shd_load_WorldForwardRootData_root();
    WorldMaterialRecord material = shd_load_WorldMaterialRecord(rootData.materialBuffer, rootData.materialByteOffset, input.materialIndex);

    if ((material.flags & WORLD_MATERIAL_FLAG_ALPHA_TEST) != 0u) {
        uint2 checker = (uint2)floor(input.uv * 8.0);
        if (((checker.x + checker.y) & 1u) == 0u) {
            discard;
        }
    }

    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(float3(0.45, 0.8, 0.35));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float lighting = 0.25 + 0.75 * diffuse;

    float4 baseColor = float4(material.baseColor[0], material.baseColor[1], material.baseColor[2], material.baseColor[3]);
    if ((material.flags & WORLD_MATERIAL_FLAG_TEXTURED) != 0u) {
        baseColor *= gfx_sample_texture2d(material.textureIndex, material.samplerIndex, input.uv);
    }
    return float4(baseColor.rgb * lighting, baseColor.a);
}

#endif
