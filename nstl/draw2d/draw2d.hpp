#pragma once

// Semantic 2D commands in, packed quads + batches out. Pure CPU: no GPU
// types, no gfx calls. The renderer uploads the quads and turns batches into
// draws during pass execution.
//
// All v1 quads sample one R8 atlas as "alpha mask x vertex color": glyphs
// sample their atlas rect, solid shapes sample the atlas's reserved white
// texel. Texture identity is per-batch renderer state, not per-quad data.

#define DRAW2D_DEFAULT_MAX_QUADS_PER_LAYER 16384u
#define DRAW2D_CLIP_STACK_DEPTH 16u

enum Draw2DLayer {
    Draw2DLayer_HUD = 0,
    Draw2DLayer_UI,
    Draw2DLayer_Debug,
    Draw2DLayer_Cursor,
    Draw2DLayer_COUNT,
};

// Bit-identical to TextQuad: pixel-space rect, normalized UV rect, color.
struct Draw2DQuad {
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

struct Draw2DBatch {
    U32 layer;
    U32 firstQuad;
    U32 quadCount;
};

struct Draw2DStats {
    U32 quadsSubmitted;
    U32 quadsClipped;
    U32 quadsDropped;
    U32 batchesBuilt;
};

struct Draw2DResult {
    const Draw2DQuad* quads;
    U32 quadCount;
    const Draw2DBatch* batches;
    U32 batchCount;
};

struct Draw2DContext {
    // Persistent.
    U32 maxQuadsPerLayer;
    B32 loggedOverflow;
    Draw2DStats stats;

    // Frame-lifetime (reset by draw2d_begin; arrays live in the frame arena).
    Arena* frameArena;
    F32 whiteU;
    F32 whiteV;
    Draw2DQuad* layerQuads[Draw2DLayer_COUNT];
    U32 layerQuadCounts[Draw2DLayer_COUNT];
    F32 clipStack[DRAW2D_CLIP_STACK_DEPTH][4];
    U32 clipDepth;
};

void draw2d_begin(Draw2DContext* ctx, Arena* frameArena, F32 whiteU, F32 whiteV);
Draw2DResult draw2d_end(Draw2DContext* ctx);

void draw2d_push_clip(Draw2DContext* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY);
void draw2d_pop_clip(Draw2DContext* ctx);

void draw2d_rect(Draw2DContext* ctx, Draw2DLayer layer, F32 minX, F32 minY, F32 maxX, F32 maxY, U32 rgba8);
// Axis-aligned line segment expanded to a thickness box. Angled lines are a
// later additive feature; the quad record is an axis-aligned box.
void draw2d_line(Draw2DContext* ctx, Draw2DLayer layer, F32 x0, F32 y0, F32 x1, F32 y1, F32 thickness, U32 rgba8);
void draw2d_box(Draw2DContext* ctx, Draw2DLayer layer, F32 minX, F32 minY, F32 maxX, F32 maxY, F32 thickness, U32 rgba8);
// Append pre-built glyph quads (the TextQuad layout) with clipping applied.
void draw2d_glyph_quads(Draw2DContext* ctx, Draw2DLayer layer, const Draw2DQuad* quads, U32 count);
