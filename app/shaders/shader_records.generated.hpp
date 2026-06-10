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

struct WorldFrameRecord {
    float viewProj[16];
    float frustumPlanes[24];
    float cameraPos[3];
    float time;
    uint renderableCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

static const uint SHD_WorldFrameRecord_STRIDE_WORDS = 48u;
WorldFrameRecord shd_load_WorldFrameRecord(uint bufferIndex, uint byteOffset, uint elementIndex) {
    uint wordOffset = elementIndex * SHD_WorldFrameRecord_STRIDE_WORDS;
    WorldFrameRecord result;
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 16u; ++arrayIndex) {
        result.viewProj[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 0u + arrayIndex));
    }
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 24u; ++arrayIndex) {
        result.frustumPlanes[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 16u + arrayIndex));
    }
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 3u; ++arrayIndex) {
        result.cameraPos[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 40u + arrayIndex));
    }
    result.time = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 43u));
    result.renderableCount = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 44u);
    result.pad0 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 45u);
    result.pad1 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 46u);
    result.pad2 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 47u);
    return result;
}

struct WorldRenderableRecord {
    float transform[16];
    float boundsCenter[3];
    float boundsRadius;
    float boundsExtents[3];
    uint materialIndex;
    uint cellIndex;
    uint flags;
    uint pad0;
    uint pad1;
};

static const uint SHD_WorldRenderableRecord_STRIDE_WORDS = 28u;
WorldRenderableRecord shd_load_WorldRenderableRecord(uint bufferIndex, uint byteOffset, uint elementIndex) {
    uint wordOffset = elementIndex * SHD_WorldRenderableRecord_STRIDE_WORDS;
    WorldRenderableRecord result;
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 16u; ++arrayIndex) {
        result.transform[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 0u + arrayIndex));
    }
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 3u; ++arrayIndex) {
        result.boundsCenter[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 16u + arrayIndex));
    }
    result.boundsRadius = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 19u));
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 3u; ++arrayIndex) {
        result.boundsExtents[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 20u + arrayIndex));
    }
    result.materialIndex = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 23u);
    result.cellIndex = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 24u);
    result.flags = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 25u);
    result.pad0 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 26u);
    result.pad1 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 27u);
    return result;
}

struct WorldMaterialRecord {
    float baseColor[4];
    uint textureIndex;
    uint samplerIndex;
    uint flags;
    uint pad0;
};

static const uint SHD_WorldMaterialRecord_STRIDE_WORDS = 8u;
WorldMaterialRecord shd_load_WorldMaterialRecord(uint bufferIndex, uint byteOffset, uint elementIndex) {
    uint wordOffset = elementIndex * SHD_WorldMaterialRecord_STRIDE_WORDS;
    WorldMaterialRecord result;
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 4u; ++arrayIndex) {
        result.baseColor[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 0u + arrayIndex));
    }
    result.textureIndex = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 4u);
    result.samplerIndex = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 5u);
    result.flags = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 6u);
    result.pad0 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 7u);
    return result;
}

struct WorldMeshRecord {
    uint indexCount;
    uint firstIndex;
    uint baseVertex;
    uint pad0;
};

static const uint SHD_WorldMeshRecord_STRIDE_WORDS = 4u;
WorldMeshRecord shd_load_WorldMeshRecord(uint bufferIndex, uint byteOffset, uint elementIndex) {
    uint wordOffset = elementIndex * SHD_WorldMeshRecord_STRIDE_WORDS;
    WorldMeshRecord result;
    result.indexCount = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 0u);
    result.firstIndex = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 1u);
    result.baseVertex = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 2u);
    result.pad0 = gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 3u);
    return result;
}

struct WorldVertexRecord {
    float position[3];
    float normal[3];
    float uv[2];
};

static const uint SHD_WorldVertexRecord_STRIDE_WORDS = 8u;
WorldVertexRecord shd_load_WorldVertexRecord(uint bufferIndex, uint byteOffset, uint elementIndex) {
    uint wordOffset = elementIndex * SHD_WorldVertexRecord_STRIDE_WORDS;
    WorldVertexRecord result;
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 3u; ++arrayIndex) {
        result.position[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 0u + arrayIndex));
    }
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 3u; ++arrayIndex) {
        result.normal[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 3u + arrayIndex));
    }
    [unroll] for (uint arrayIndex = 0u; arrayIndex < 2u; ++arrayIndex) {
        result.uv[arrayIndex] = asfloat(gfx_load_buffer_word(bufferIndex, byteOffset, wordOffset + 6u + arrayIndex));
    }
    return result;
}

struct WorldForwardRootData {
    uint frameBuffer;
    uint frameByteOffset;
    uint renderableBuffer;
    uint renderableByteOffset;
    uint visibleBuffer;
    uint visibleByteOffset;
    uint cellOffsetBuffer;
    uint cellOffsetByteOffset;
    uint materialBuffer;
    uint materialByteOffset;
    uint vertexBuffer;
    uint vertexByteOffset;
    uint cellIndex;
    uint debugFlags;
};

WorldForwardRootData shd_load_WorldForwardRootData_root() {
    WorldForwardRootData result;
    result.frameBuffer = gfx_load_root_word(0u);
    result.frameByteOffset = gfx_load_root_word(1u);
    result.renderableBuffer = gfx_load_root_word(2u);
    result.renderableByteOffset = gfx_load_root_word(3u);
    result.visibleBuffer = gfx_load_root_word(4u);
    result.visibleByteOffset = gfx_load_root_word(5u);
    result.cellOffsetBuffer = gfx_load_root_word(6u);
    result.cellOffsetByteOffset = gfx_load_root_word(7u);
    result.materialBuffer = gfx_load_root_word(8u);
    result.materialByteOffset = gfx_load_root_word(9u);
    result.vertexBuffer = gfx_load_root_word(10u);
    result.vertexByteOffset = gfx_load_root_word(11u);
    result.cellIndex = gfx_load_root_word(12u);
    result.debugFlags = gfx_load_root_word(13u);
    return result;
}

struct WorldCullRootData {
    uint frameBuffer;
    uint frameByteOffset;
    uint renderableBuffer;
    uint renderableByteOffset;
    uint flagsBuffer;
    uint flagsByteOffset;
    uint cellCountBuffer;
    uint cellCountByteOffset;
    uint cellOffsetBuffer;
    uint cellOffsetByteOffset;
    uint visibleBuffer;
    uint visibleByteOffset;
    uint argsBuffer;
    uint argsByteOffset;
    uint meshBuffer;
    uint meshByteOffset;
    uint renderableCount;
    uint cellCount;
    uint meshCount;
};

WorldCullRootData shd_load_WorldCullRootData_root() {
    WorldCullRootData result;
    result.frameBuffer = gfx_load_root_word(0u);
    result.frameByteOffset = gfx_load_root_word(1u);
    result.renderableBuffer = gfx_load_root_word(2u);
    result.renderableByteOffset = gfx_load_root_word(3u);
    result.flagsBuffer = gfx_load_root_word(4u);
    result.flagsByteOffset = gfx_load_root_word(5u);
    result.cellCountBuffer = gfx_load_root_word(6u);
    result.cellCountByteOffset = gfx_load_root_word(7u);
    result.cellOffsetBuffer = gfx_load_root_word(8u);
    result.cellOffsetByteOffset = gfx_load_root_word(9u);
    result.visibleBuffer = gfx_load_root_word(10u);
    result.visibleByteOffset = gfx_load_root_word(11u);
    result.argsBuffer = gfx_load_root_word(12u);
    result.argsByteOffset = gfx_load_root_word(13u);
    result.meshBuffer = gfx_load_root_word(14u);
    result.meshByteOffset = gfx_load_root_word(15u);
    result.renderableCount = gfx_load_root_word(16u);
    result.cellCount = gfx_load_root_word(17u);
    result.meshCount = gfx_load_root_word(18u);
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

struct ShdWorldFrameRecord {
    F32 viewProj[16];
    F32 frustumPlanes[24];
    F32 cameraPos[3];
    F32 time;
    U32 renderableCount;
    U32 pad0;
    U32 pad1;
    U32 pad2;
};
static_assert(offsetof(ShdWorldFrameRecord, viewProj) == 0u, "ShdWorldFrameRecord.viewProj shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, frustumPlanes) == 64u, "ShdWorldFrameRecord.frustumPlanes shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, cameraPos) == 160u, "ShdWorldFrameRecord.cameraPos shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, time) == 172u, "ShdWorldFrameRecord.time shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, renderableCount) == 176u, "ShdWorldFrameRecord.renderableCount shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, pad0) == 180u, "ShdWorldFrameRecord.pad0 shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, pad1) == 184u, "ShdWorldFrameRecord.pad1 shader ABI offset mismatch");
static_assert(offsetof(ShdWorldFrameRecord, pad2) == 188u, "ShdWorldFrameRecord.pad2 shader ABI offset mismatch");
static_assert(sizeof(ShdWorldFrameRecord) == 192u, "ShdWorldFrameRecord shader ABI size mismatch");
static const U32 SHD_WorldFrameRecord_STRIDE_WORDS = 48u;

struct ShdWorldRenderableRecord {
    F32 transform[16];
    F32 boundsCenter[3];
    F32 boundsRadius;
    F32 boundsExtents[3];
    U32 materialIndex;
    U32 cellIndex;
    U32 flags;
    U32 pad0;
    U32 pad1;
};
static_assert(offsetof(ShdWorldRenderableRecord, transform) == 0u, "ShdWorldRenderableRecord.transform shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, boundsCenter) == 64u, "ShdWorldRenderableRecord.boundsCenter shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, boundsRadius) == 76u, "ShdWorldRenderableRecord.boundsRadius shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, boundsExtents) == 80u, "ShdWorldRenderableRecord.boundsExtents shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, materialIndex) == 92u, "ShdWorldRenderableRecord.materialIndex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, cellIndex) == 96u, "ShdWorldRenderableRecord.cellIndex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, flags) == 100u, "ShdWorldRenderableRecord.flags shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, pad0) == 104u, "ShdWorldRenderableRecord.pad0 shader ABI offset mismatch");
static_assert(offsetof(ShdWorldRenderableRecord, pad1) == 108u, "ShdWorldRenderableRecord.pad1 shader ABI offset mismatch");
static_assert(sizeof(ShdWorldRenderableRecord) == 112u, "ShdWorldRenderableRecord shader ABI size mismatch");
static const U32 SHD_WorldRenderableRecord_STRIDE_WORDS = 28u;

struct ShdWorldMaterialRecord {
    F32 baseColor[4];
    U32 textureIndex;
    U32 samplerIndex;
    U32 flags;
    U32 pad0;
};
static_assert(offsetof(ShdWorldMaterialRecord, baseColor) == 0u, "ShdWorldMaterialRecord.baseColor shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMaterialRecord, textureIndex) == 16u, "ShdWorldMaterialRecord.textureIndex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMaterialRecord, samplerIndex) == 20u, "ShdWorldMaterialRecord.samplerIndex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMaterialRecord, flags) == 24u, "ShdWorldMaterialRecord.flags shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMaterialRecord, pad0) == 28u, "ShdWorldMaterialRecord.pad0 shader ABI offset mismatch");
static_assert(sizeof(ShdWorldMaterialRecord) == 32u, "ShdWorldMaterialRecord shader ABI size mismatch");
static const U32 SHD_WorldMaterialRecord_STRIDE_WORDS = 8u;

struct ShdWorldMeshRecord {
    U32 indexCount;
    U32 firstIndex;
    U32 baseVertex;
    U32 pad0;
};
static_assert(offsetof(ShdWorldMeshRecord, indexCount) == 0u, "ShdWorldMeshRecord.indexCount shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMeshRecord, firstIndex) == 4u, "ShdWorldMeshRecord.firstIndex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMeshRecord, baseVertex) == 8u, "ShdWorldMeshRecord.baseVertex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldMeshRecord, pad0) == 12u, "ShdWorldMeshRecord.pad0 shader ABI offset mismatch");
static_assert(sizeof(ShdWorldMeshRecord) == 16u, "ShdWorldMeshRecord shader ABI size mismatch");
static const U32 SHD_WorldMeshRecord_STRIDE_WORDS = 4u;

struct ShdWorldVertexRecord {
    F32 position[3];
    F32 normal[3];
    F32 uv[2];
};
static_assert(offsetof(ShdWorldVertexRecord, position) == 0u, "ShdWorldVertexRecord.position shader ABI offset mismatch");
static_assert(offsetof(ShdWorldVertexRecord, normal) == 12u, "ShdWorldVertexRecord.normal shader ABI offset mismatch");
static_assert(offsetof(ShdWorldVertexRecord, uv) == 24u, "ShdWorldVertexRecord.uv shader ABI offset mismatch");
static_assert(sizeof(ShdWorldVertexRecord) == 32u, "ShdWorldVertexRecord shader ABI size mismatch");
static const U32 SHD_WorldVertexRecord_STRIDE_WORDS = 8u;

struct ShdWorldForwardRootData {
    U32 frameBuffer;
    U32 frameByteOffset;
    U32 renderableBuffer;
    U32 renderableByteOffset;
    U32 visibleBuffer;
    U32 visibleByteOffset;
    U32 cellOffsetBuffer;
    U32 cellOffsetByteOffset;
    U32 materialBuffer;
    U32 materialByteOffset;
    U32 vertexBuffer;
    U32 vertexByteOffset;
    U32 cellIndex;
    U32 debugFlags;
    U32 _pad14;
    U32 _pad15;
};
static_assert(offsetof(ShdWorldForwardRootData, frameBuffer) == 0u, "ShdWorldForwardRootData.frameBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, frameByteOffset) == 4u, "ShdWorldForwardRootData.frameByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, renderableBuffer) == 8u, "ShdWorldForwardRootData.renderableBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, renderableByteOffset) == 12u, "ShdWorldForwardRootData.renderableByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, visibleBuffer) == 16u, "ShdWorldForwardRootData.visibleBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, visibleByteOffset) == 20u, "ShdWorldForwardRootData.visibleByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, cellOffsetBuffer) == 24u, "ShdWorldForwardRootData.cellOffsetBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, cellOffsetByteOffset) == 28u, "ShdWorldForwardRootData.cellOffsetByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, materialBuffer) == 32u, "ShdWorldForwardRootData.materialBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, materialByteOffset) == 36u, "ShdWorldForwardRootData.materialByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, vertexBuffer) == 40u, "ShdWorldForwardRootData.vertexBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, vertexByteOffset) == 44u, "ShdWorldForwardRootData.vertexByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, cellIndex) == 48u, "ShdWorldForwardRootData.cellIndex shader ABI offset mismatch");
static_assert(offsetof(ShdWorldForwardRootData, debugFlags) == 52u, "ShdWorldForwardRootData.debugFlags shader ABI offset mismatch");
static_assert(sizeof(ShdWorldForwardRootData) == 64u, "ShdWorldForwardRootData shader ABI size mismatch");

struct ShdWorldCullRootData {
    U32 frameBuffer;
    U32 frameByteOffset;
    U32 renderableBuffer;
    U32 renderableByteOffset;
    U32 flagsBuffer;
    U32 flagsByteOffset;
    U32 cellCountBuffer;
    U32 cellCountByteOffset;
    U32 cellOffsetBuffer;
    U32 cellOffsetByteOffset;
    U32 visibleBuffer;
    U32 visibleByteOffset;
    U32 argsBuffer;
    U32 argsByteOffset;
    U32 meshBuffer;
    U32 meshByteOffset;
    U32 renderableCount;
    U32 cellCount;
    U32 meshCount;
    U32 _pad19;
    U32 _pad20;
    U32 _pad21;
    U32 _pad22;
    U32 _pad23;
};
static_assert(offsetof(ShdWorldCullRootData, frameBuffer) == 0u, "ShdWorldCullRootData.frameBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, frameByteOffset) == 4u, "ShdWorldCullRootData.frameByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, renderableBuffer) == 8u, "ShdWorldCullRootData.renderableBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, renderableByteOffset) == 12u, "ShdWorldCullRootData.renderableByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, flagsBuffer) == 16u, "ShdWorldCullRootData.flagsBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, flagsByteOffset) == 20u, "ShdWorldCullRootData.flagsByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, cellCountBuffer) == 24u, "ShdWorldCullRootData.cellCountBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, cellCountByteOffset) == 28u, "ShdWorldCullRootData.cellCountByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, cellOffsetBuffer) == 32u, "ShdWorldCullRootData.cellOffsetBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, cellOffsetByteOffset) == 36u, "ShdWorldCullRootData.cellOffsetByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, visibleBuffer) == 40u, "ShdWorldCullRootData.visibleBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, visibleByteOffset) == 44u, "ShdWorldCullRootData.visibleByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, argsBuffer) == 48u, "ShdWorldCullRootData.argsBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, argsByteOffset) == 52u, "ShdWorldCullRootData.argsByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, meshBuffer) == 56u, "ShdWorldCullRootData.meshBuffer shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, meshByteOffset) == 60u, "ShdWorldCullRootData.meshByteOffset shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, renderableCount) == 64u, "ShdWorldCullRootData.renderableCount shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, cellCount) == 68u, "ShdWorldCullRootData.cellCount shader ABI offset mismatch");
static_assert(offsetof(ShdWorldCullRootData, meshCount) == 72u, "ShdWorldCullRootData.meshCount shader ABI offset mismatch");
static_assert(sizeof(ShdWorldCullRootData) == 96u, "ShdWorldCullRootData shader ABI size mismatch");

#endif
