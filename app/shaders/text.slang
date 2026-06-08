#include "gfx_shader_abi.slang"

#if GFX_SHADER_STAGE_GRAPHICS
static const uint TEXT_QUAD_STRIDE_WORDS = 9u;

struct TextDrawRootData {
    uint quadBuffer;
    uint quadByteOffset;
    uint atlasTexture;
    uint atlasSampler;
    float targetWidth;
    float targetHeight;
    uint _padding0;
    uint _padding1;
    uint _padding2;
    uint _padding3;
    uint _padding4;
    uint _padding5;
    uint _padding6;
    uint _padding7;
    uint _padding8;
    uint _padding9;
};

struct TextQuad {
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

struct TextVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
};

TextDrawRootData text_load_root_data() {
    TextDrawRootData result;
    result.quadBuffer = gfx_load_root_word(0u);
    result.quadByteOffset = gfx_load_root_word(1u);
    result.atlasTexture = gfx_load_root_word(2u);
    result.atlasSampler = gfx_load_root_word(3u);
    result.targetWidth = asfloat(gfx_load_root_word(4u));
    result.targetHeight = asfloat(gfx_load_root_word(5u));
    result._padding0 = gfx_load_root_word(6u);
    result._padding1 = gfx_load_root_word(7u);
    result._padding2 = gfx_load_root_word(8u);
    result._padding3 = gfx_load_root_word(9u);
    result._padding4 = gfx_load_root_word(10u);
    result._padding5 = gfx_load_root_word(11u);
    result._padding6 = gfx_load_root_word(12u);
    result._padding7 = gfx_load_root_word(13u);
    result._padding8 = gfx_load_root_word(14u);
    result._padding9 = gfx_load_root_word(15u);
    return result;
}

TextQuad text_load_quad(TextDrawRootData rootData, uint quadIndex) {
    uint wordOffset = quadIndex * TEXT_QUAD_STRIDE_WORDS;

    TextQuad result;
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

float4 text_decode_rgba8(uint rgba8) {
    float scale = 1.0 / 255.0;
    return float4((float)((rgba8 >> 24u) & 255u) * scale,
                  (float)((rgba8 >> 16u) & 255u) * scale,
                  (float)((rgba8 >> 8u) & 255u) * scale,
                  (float)(rgba8 & 255u) * scale);
}

[shader("vertex")]
TextVertexOut text_vertex(uint vertexId : SV_VertexID) {
    TextDrawRootData rootData = text_load_root_data();
    uint quadIndex = vertexId >> 2u;
    uint corner = vertexId & 3u;
    TextQuad quad = text_load_quad(rootData, quadIndex);

    bool right = (corner == 1u || corner == 2u);
    bool bottom = (corner == 2u || corner == 3u);
    float2 pixel = float2(right ? quad.maxX : quad.minX,
                          bottom ? quad.maxY : quad.minY);
    float2 uv = float2(right ? quad.maxU : quad.minU,
                       bottom ? quad.maxV : quad.minV);

    TextVertexOut output;
    output.position = float4(pixel.x / rootData.targetWidth * 2.0 - 1.0,
                             1.0 - pixel.y / rootData.targetHeight * 2.0,
                             0.0,
                             1.0);
    output.uv = uv;
    output.color = text_decode_rgba8(quad.rgba8);
    return output;
}

[shader("fragment")]
float4 text_fragment(TextVertexOut input) : SV_Target0 {
    TextDrawRootData rootData = text_load_root_data();
    float coverage = gfx_sample_texture2d(rootData.atlasTexture, rootData.atlasSampler, input.uv).r;
    return float4(input.color.rgb, input.color.a * coverage);
}
#endif
