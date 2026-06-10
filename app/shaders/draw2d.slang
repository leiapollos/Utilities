#include "gfx_shader_abi.slang"

#if GFX_SHADER_STAGE_GRAPHICS
static const uint DRAW2D_QUAD_STRIDE_WORDS = 9u;

struct Draw2DRootData {
    uint quadBuffer;
    uint quadByteOffset;
    uint atlasTexture;
    uint atlasSampler;
    float targetWidth;
    float targetHeight;
};

struct Draw2DQuad {
    float minX;
    float minY;
    float maxX;
    float maxY;
    float minU;
    float minV;
    float maxU;
    float maxV;
    uint rgba8;
};

struct Draw2DVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
};

Draw2DRootData draw2d_load_root_data() {
    Draw2DRootData result;
    result.quadBuffer = gfx_load_root_word(0u);
    result.quadByteOffset = gfx_load_root_word(1u);
    result.atlasTexture = gfx_load_root_word(2u);
    result.atlasSampler = gfx_load_root_word(3u);
    result.targetWidth = asfloat(gfx_load_root_word(4u));
    result.targetHeight = asfloat(gfx_load_root_word(5u));
    return result;
}

Draw2DQuad draw2d_load_quad(Draw2DRootData rootData, uint quadIndex) {
    uint wordOffset = quadIndex * DRAW2D_QUAD_STRIDE_WORDS;

    Draw2DQuad result;
    result.minX = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 0u));
    result.minY = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 1u));
    result.maxX = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 2u));
    result.maxY = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 3u));
    result.minU = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 4u));
    result.minV = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 5u));
    result.maxU = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 6u));
    result.maxV = asfloat(gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 7u));
    result.rgba8 = gfx_load_buffer_word(rootData.quadBuffer, rootData.quadByteOffset, wordOffset + 8u);
    return result;
}

float4 draw2d_decode_rgba8(uint rgba8) {
    float scale = 1.0 / 255.0;
    return float4((float)((rgba8 >> 24u) & 255u) * scale,
                  (float)((rgba8 >> 16u) & 255u) * scale,
                  (float)((rgba8 >> 8u) & 255u) * scale,
                  (float)(rgba8 & 255u) * scale);
}

[shader("vertex")]
Draw2DVertexOut draw2d_vertex(uint vertexId : SV_VertexID) {
    Draw2DRootData rootData = draw2d_load_root_data();
    uint quadIndex = vertexId >> 2u;
    uint corner = vertexId & 3u;
    Draw2DQuad quad = draw2d_load_quad(rootData, quadIndex);

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
    Draw2DRootData rootData = draw2d_load_root_data();
    float coverage = gfx_sample_texture2d(rootData.atlasTexture, rootData.atlasSampler, input.uv).r;
    return float4(input.color.rgb, input.color.a * coverage);
}
#endif
