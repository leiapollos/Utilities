#include "gfx_shader_abi.slang"
#include "shader_records.generated.hpp"

#if GFX_SHADER_STAGE_GRAPHICS
struct Draw2DVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
};

float4 draw2d_decode_rgba8(uint rgba8) {
    float scale = 1.0 / 255.0;
    return float4((float)((rgba8 >> 24u) & 255u) * scale,
                  (float)((rgba8 >> 16u) & 255u) * scale,
                  (float)((rgba8 >> 8u) & 255u) * scale,
                  (float)(rgba8 & 255u) * scale);
}

[shader("vertex")]
Draw2DVertexOut draw2d_vertex(uint vertexId : SV_VertexID) {
    Draw2DRootData rootData = shd_load_Draw2DRootData_root();
    uint quadIndex = vertexId >> 2u;
    uint corner = vertexId & 3u;
    Draw2DQuadRecord quad = shd_load_Draw2DQuadRecord(rootData.quadBuffer, rootData.quadByteOffset, quadIndex);

    bool right = (corner == 1u || corner == 2u);
    bool bottom = (corner == 2u || corner == 3u);
    float2 pixel = float2(right ? quad.maxX : quad.minX,
                          bottom ? quad.maxY : quad.minY);
    float2 uv = float2(right ? quad.maxU : quad.minU,
                       bottom ? quad.maxV : quad.minV);

    Draw2DVertexOut output;
    output.position = float4(pixel.x / rootData.targetWidth * 2.0 - 1.0,
                             1.0 - pixel.y / rootData.targetHeight * 2.0,
                             0.0,
                             1.0);
    output.uv = uv;
    output.color = draw2d_decode_rgba8(quad.rgba8);
    return output;
}

[shader("fragment")]
float4 draw2d_fragment(Draw2DVertexOut input) : SV_Target0 {
    Draw2DRootData rootData = shd_load_Draw2DRootData_root();
    float coverage = gfx_sample_texture2d(rootData.atlasTexture, rootData.atlasSampler, input.uv).r;
    return float4(input.color.rgb, input.color.a * coverage);
}
#endif
