#include "gfx_shader_abi.slang"

#if GFX_SHADER_STAGE_GRAPHICS
[shader("vertex")]
VertexOut triangle_vertex(VertexIn input) {
    DrawRootData rootData = gfx_load_draw_root_data();

    VertexOut output;
    float objectPhase = (float)rootData.objectId * 0.173;
    float scale = rootData.offsetScale.z * (1.0 + 0.055 * sin(rootData.animationPhase * 2.0 + objectPhase));
    float2 wobble = float2(sin(rootData.animationPhase * 1.7 + objectPhase),
                           cos(rootData.animationPhase * 1.3 + objectPhase)) * 0.014;
    float2 position = input.position * scale + rootData.offsetScale.xy + wobble;
    output.position = float4(position, rootData.offsetScale.w, 1.0);
    output.color = input.color;
    output.uv = float2(input.position.x * (1.0 / 1.1) + 0.5, 1.0 - (input.position.y + 0.45));
    return output;
}

[shader("fragment")]
float4 triangle_fragment(VertexOut input) : SV_Target0 {
    DrawRootData rootData = gfx_load_draw_root_data();
    Material material = gfx_load_material(rootData, rootData.materialIndex);
    float4 albedo = gfx_sample_albedo(material.albedoTexture, material.samplerIndex, input.uv);
    float brightness = 0.90 + 0.10 * sin(rootData.animationPhase * 3.0 + (float)rootData.objectId * 0.051);
    float4 color = input.color * material.baseColor * albedo;
    float depthShade = 1.20 - rootData.offsetScale.w * 0.45;
    color.rgb *= brightness * depthShade;

    float leftEdge = input.uv.x - (0.5 - input.uv.y * 0.5);
    float rightEdge = (0.5 + input.uv.y * 0.5) - input.uv.x;
    float bottomEdge = 1.0 - input.uv.y;
    float edgeDistance = min(bottomEdge, min(leftEdge, rightEdge));
    float edgeWidth = max(fwidth(edgeDistance) * 1.8, 0.0015);
    float outline = 1.0 - smoothstep(0.0, edgeWidth, edgeDistance);
    color.rgb = lerp(color.rgb, float3(0.015, 0.018, 0.022), outline * 0.85);
    return color;
}
#endif

#if GFX_SHADER_STAGE_COMPUTE
[shader("compute")]
[numthreads(64, 1, 1)]
void demo_material_compute(uint3 dispatchThreadId : SV_DispatchThreadID) {
    MaterialComputeRootData rootData = gfx_load_compute_root_data();
    uint index = dispatchThreadId.x;
    if (index >= rootData.materialCount) {
        return;
    }

    Material source = gfx_load_source_material(rootData, index);
    float columnT = source.baseColor.x;
    float rowT = source.baseColor.y;
    float waveA = 0.5 + 0.5 * sin(rootData.animationPhase * 1.7 + columnT * 6.28318);
    float waveB = 0.5 + 0.5 * cos(rootData.animationPhase * 1.3 + rowT * 6.28318);
    float waveC = 0.5 + 0.5 * sin(rootData.animationPhase * 1.1 + (columnT + rowT) * 3.14159);

    Material material;
    material.baseColor = float4(0.25 + 0.45 * columnT + 0.20 * waveA,
                                0.25 + 0.45 * rowT + 0.20 * waveB,
                                0.55 + 0.30 * waveC,
                                1.0);
    material.albedoTexture = source.albedoTexture;
    material.samplerIndex = source.samplerIndex;
    material.flags = source.flags;
    material._padding = 0u;
    gfx_store_material(rootData, index, material);
}
#endif
