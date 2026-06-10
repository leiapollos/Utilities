//
// AUTO-GENERATED FILE - DO NOT EDIT
// Generated from: ../app/shaders/shader_records.metadef
//

#pragma once

// ////////////////////////
// Shader Records

#if defined(GFX_SHADER_ABI_SLANG)

struct Draw2DRootData {
    uint quadBuffer;
    uint quadByteOffset;
    uint atlasTexture;
    uint atlasSampler;
    float targetWidth;
    float targetHeight;
};

Draw2DRootData shd_load_Draw2DRootData_root() {
    Draw2DRootData result;
    result.quadBuffer = gfx_load_root_word(0u);
    result.quadByteOffset = gfx_load_root_word(1u);
    result.atlasTexture = gfx_load_root_word(2u);
    result.atlasSampler = gfx_load_root_word(3u);
    result.targetWidth = asfloat(gfx_load_root_word(4u));
    result.targetHeight = asfloat(gfx_load_root_word(5u));
    return result;
}

struct Draw2DQuadRecord {
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

static const uint SHD_Draw2DQuadRecord_STRIDE_WORDS = 9u;
Draw2DQuadRecord shd_load_Draw2DQuadRecord(uint bufferIndex, uint byteOffset, uint elementIndex) {
    uint wordOffset = elementIndex * SHD_Draw2DQuadRecord_STRIDE_WORDS;
    Draw2DQuadRecord result;
    result.minX = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 0u));
    result.minY = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 1u));
    result.maxX = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 2u));
    result.maxY = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 3u));
    result.minU = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 4u));
    result.minV = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 5u));
    result.maxU = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 6u));
    result.maxV = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 7u));
    result.rgba8 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 8u);
    return result;
}

#else

struct ShdDraw2DRootData {
    U32 quadBuffer;
    U32 quadByteOffset;
    U32 atlasTexture;
    U32 atlasSampler;
    F32 targetWidth;
    F32 targetHeight;
    U32 _pad6;
    U32 _pad7;
    U32 _pad8;
    U32 _pad9;
    U32 _pad10;
    U32 _pad11;
    U32 _pad12;
    U32 _pad13;
    U32 _pad14;
    U32 _pad15;
};
static_assert(offsetof(ShdDraw2DRootData, quadBuffer) == 0u, "ShdDraw2DRootData.quadBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DRootData, quadByteOffset) == 4u, "ShdDraw2DRootData.quadByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DRootData, atlasTexture) == 8u, "ShdDraw2DRootData.atlasTexture shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DRootData, atlasSampler) == 12u, "ShdDraw2DRootData.atlasSampler shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DRootData, targetWidth) == 16u, "ShdDraw2DRootData.targetWidth shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DRootData, targetHeight) == 20u, "ShdDraw2DRootData.targetHeight shader ABI offset mismatch");
static_assert(sizeof(ShdDraw2DRootData) == 64u, "ShdDraw2DRootData shader ABI size mismatch");

struct ShdDraw2DQuadRecord {
    F32 minX;
    F32 minY;
    F32 maxX;
    F32 maxY;
    F32 minU;
    F32 minV;
    F32 maxU;
    F32 maxV;
    U32 rgba8;
};
static_assert(offsetof(ShdDraw2DQuadRecord, minX) == 0u, "ShdDraw2DQuadRecord.minX shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, minY) == 4u, "ShdDraw2DQuadRecord.minY shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, maxX) == 8u, "ShdDraw2DQuadRecord.maxX shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, maxY) == 12u, "ShdDraw2DQuadRecord.maxY shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, minU) == 16u, "ShdDraw2DQuadRecord.minU shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, minV) == 20u, "ShdDraw2DQuadRecord.minV shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, maxU) == 24u, "ShdDraw2DQuadRecord.maxU shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, maxV) == 28u, "ShdDraw2DQuadRecord.maxV shader ABI offset mismatch");
static_assert(offsetof(ShdDraw2DQuadRecord, rgba8) == 32u, "ShdDraw2DQuadRecord.rgba8 shader ABI offset mismatch");
static_assert(sizeof(ShdDraw2DQuadRecord) == 36u, "ShdDraw2DQuadRecord shader ABI size mismatch");
static const U32 SHD_Draw2DQuadRecord_STRIDE_WORDS = 9u;

#endif
