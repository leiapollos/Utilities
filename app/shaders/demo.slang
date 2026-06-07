#include "gfx_shader_abi.slang"

#if GFX_SHADER_STAGE_GRAPHICS
[shader("vertex")]
VertexOut triangle_vertex(VertexIn input) {
    DrawData drawData = gfx_load_draw_data();

    VertexOut output;
    float objectPhase = (float)drawData.objectId * 0.173;
    float scale = drawData.offsetScale.z * (1.0 + 0.055 * sin(drawData.animationPhase * 2.0 + objectPhase));
    float2 wobble = float2(sin(drawData.animationPhase * 1.7 + objectPhase),
                           cos(drawData.animationPhase * 1.3 + objectPhase)) * 0.014;
    float2 position = input.position * scale + drawData.offsetScale.xy + wobble;
    output.position = float4(position, 0.0, 1.0);
    output.color = input.color;
    output.uv = float2(input.position.x * (1.0 / 1.1) + 0.5, 1.0 - (input.position.y + 0.45));
    return output;
}

[shader("fragment")]
float4 triangle_fragment(VertexOut input) : SV_Target0 {
    DrawData drawData = gfx_load_draw_data();
    Material material = gfx_load_material(drawData.materialIndex);
    float4 albedo = gfx_sample_albedo(material.albedoTexture, material.samplerIndex, input.uv);
    float brightness = 0.90 + 0.10 * sin(drawData.animationPhase * 3.0 + (float)drawData.objectId * 0.051);
    float4 color = input.color * material.baseColor * albedo;
    color.rgb *= brightness;
    return color;
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
[shader("compute")]
[numthreads(64, 1, 1)]
void demo_material_compute(uint3 dispatchThreadId : SV_DispatchThreadID) {
    MaterialComputeData data = gfx_load_compute_data();
    uint index = dispatchThreadId.x;
    if (index >= data.materialCount) {
        return;
    }

    uint column = index % data.columns;
    uint row = index / data.columns;
    float columnT = (data.columns > 1u) ? ((float)column / (float)(data.columns - 1u)) : 0.0;
    float rowT = (data.rows > 1u) ? ((float)row / (float)(data.rows - 1u)) : 0.0;
    float waveA = 0.5 + 0.5 * sin(data.animationPhase * 1.7 + columnT * 6.28318);
    float waveB = 0.5 + 0.5 * cos(data.animationPhase * 1.3 + rowT * 6.28318);
    float waveC = 0.5 + 0.5 * sin(data.animationPhase * 1.1 + (columnT + rowT) * 3.14159);

    Material material;
    material.baseColor = float4(0.25 + 0.45 * columnT + 0.20 * waveA,
                                0.25 + 0.45 * rowT + 0.20 * waveB,
                                0.55 + 0.30 * waveC,
                                1.0);
    material.albedoTexture = data.albedoTexture;
    material.samplerIndex = data.samplerIndex;
    material.flags = 1u;
    material._padding = 0u;
    gfx_store_material(index, material);
}
#endif
