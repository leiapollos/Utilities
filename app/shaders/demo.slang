#include "gfx_shader_abi.slang"

#if GFX_SHADER_STAGE_GRAPHICS
[shader("vertex")]
VertexOut triangle_vertex(VertexIn input) {
    DrawData drawData = gfx_load_draw_data();

    VertexOut output;
    float2 position = input.position * drawData.offsetScale.z + drawData.offsetScale.xy;
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
    return input.color * material.baseColor * albedo;
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

    Material material;
    material.baseColor = float4(0.30 + 0.70 * columnT,
                                0.30 + 0.70 * rowT,
                                1.00 - 0.50 * ((columnT + rowT) * 0.5),
                                1.0);
    material.albedoTexture = data.albedoTexture;
    material.samplerIndex = data.samplerIndex;
    material.flags = 1u;
    material._padding = 0u;
    gfx_store_material(index, material);
}
#endif
