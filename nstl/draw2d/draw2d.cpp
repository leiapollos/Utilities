static const F32 DRAW2D_NO_CLIP[4] = {-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f};

static const F32* draw2d_current_clip(Draw2DContext* ctx) {
    if (ctx->clipDepth == 0u) {
        return DRAW2D_NO_CLIP;
    }
    return ctx->clipStack[ctx->clipDepth - 1u];
}

static Draw2DQuad* draw2d_push_quad(Draw2DContext* ctx, Draw2DLayer layer) {
    if (!ctx || !ctx->frameArena || (U32)layer >= Draw2DLayer_COUNT) {
        return 0;
    }

    if (!ctx->layerQuads[layer]) {
        ctx->layerQuads[layer] = ARENA_PUSH_ARRAY(ctx->frameArena, Draw2DQuad, ctx->maxQuadsPerLayer);
        if (!ctx->layerQuads[layer]) {
            return 0;
        }
    }
    if (ctx->layerQuadCounts[layer] >= ctx->maxQuadsPerLayer) {
        ctx->stats.quadsDropped += 1u;
        if (!ctx->loggedOverflow) {
            LOG_WARNING("draw2d", "Quad capacity exceeded ({} per layer); dropping quads", ctx->maxQuadsPerLayer);
            ctx->loggedOverflow = 1;
        }
        return 0;
    }

    Draw2DQuad* quad = ctx->layerQuads[layer] + ctx->layerQuadCounts[layer];
    ctx->layerQuadCounts[layer] += 1u;
    ctx->stats.quadsSubmitted += 1u;
    return quad;
}

static B32 draw2d_clip_quad(Draw2DContext* ctx, Draw2DQuad* quad) {
    const F32* clip = draw2d_current_clip(ctx);
    F32 minX = MAX(quad->minX, clip[0]);
    F32 minY = MAX(quad->minY, clip[1]);
    F32 maxX = MIN(quad->maxX, clip[2]);
    F32 maxY = MIN(quad->maxY, clip[3]);
    if (minX >= maxX || minY >= maxY) {
        ctx->stats.quadsClipped += 1u;
        return 0;
    }
    if (minX != quad->minX || maxX != quad->maxX) {
        F32 width = quad->maxX - quad->minX;
        F32 uWidth = quad->maxU - quad->minU;
        F32 newMinU = quad->minU + (minX - quad->minX) / width * uWidth;
        F32 newMaxU = quad->maxU - (quad->maxX - maxX) / width * uWidth;
        quad->minU = newMinU;
        quad->maxU = newMaxU;
        quad->minX = minX;
        quad->maxX = maxX;
    }
    if (minY != quad->minY || maxY != quad->maxY) {
        F32 height = quad->maxY - quad->minY;
        F32 vHeight = quad->maxV - quad->minV;
        F32 newMinV = quad->minV + (minY - quad->minY) / height * vHeight;
        F32 newMaxV = quad->maxV - (quad->maxY - maxY) / height * vHeight;
        quad->minV = newMinV;
        quad->maxV = newMaxV;
        quad->minY = minY;
        quad->maxY = maxY;
    }
    return 1;
}

static void draw2d_append_solid(Draw2DContext* ctx, Draw2DLayer layer, F32 minX, F32 minY, F32 maxX, F32 maxY, U32 rgba8) {
    if (minX >= maxX || minY >= maxY) {
        return;
    }

    Draw2DQuad quad = {};
    quad.minX = minX;
    quad.minY = minY;
    quad.maxX = maxX;
    quad.maxY = maxY;
    quad.minU = ctx->whiteU;
    quad.minV = ctx->whiteV;
    quad.maxU = ctx->whiteU;
    quad.maxV = ctx->whiteV;
    quad.rgba8 = rgba8;
    if (!draw2d_clip_quad(ctx, &quad)) {
        return;
    }

    Draw2DQuad* dst = draw2d_push_quad(ctx, layer);
    if (dst) {
        *dst = quad;
    }
}

void draw2d_begin(Draw2DContext* ctx, Arena* frameArena, F32 whiteU, F32 whiteV) {
    if (!ctx) {
        return;
    }
    if (ctx->maxQuadsPerLayer == 0u) {
        ctx->maxQuadsPerLayer = DRAW2D_DEFAULT_MAX_QUADS_PER_LAYER;
    }

    ctx->frameArena = frameArena;
    ctx->whiteU = whiteU;
    ctx->whiteV = whiteV;
    ctx->clipDepth = 0u;
    ctx->stats = {};
    for (U32 layer = 0u; layer < Draw2DLayer_COUNT; ++layer) {
        ctx->layerQuads[layer] = 0;
        ctx->layerQuadCounts[layer] = 0u;
    }
}

Draw2DResult draw2d_end(Draw2DContext* ctx) {
    Draw2DResult result = {};
    if (!ctx || !ctx->frameArena) {
        return result;
    }

    U32 totalQuads = 0u;
    U32 batchCount = 0u;
    for (U32 layer = 0u; layer < Draw2DLayer_COUNT; ++layer) {
        totalQuads += ctx->layerQuadCounts[layer];
        if (ctx->layerQuadCounts[layer] != 0u) {
            batchCount += 1u;
        }
    }
    if (totalQuads == 0u) {
        return result;
    }

    Draw2DQuad* quads = ARENA_PUSH_ARRAY(ctx->frameArena, Draw2DQuad, totalQuads);
    Draw2DBatch* batches = ARENA_PUSH_ARRAY(ctx->frameArena, Draw2DBatch, batchCount);
    if (!quads || !batches) {
        return result;
    }

    U32 quadCursor = 0u;
    U32 batchCursor = 0u;
    for (U32 layer = 0u; layer < Draw2DLayer_COUNT; ++layer) {
        U32 count = ctx->layerQuadCounts[layer];
        if (count == 0u) {
            continue;
        }
        MEMCPY(quads + quadCursor, ctx->layerQuads[layer], sizeof(Draw2DQuad) * count);
        batches[batchCursor].layer = layer;
        batches[batchCursor].firstQuad = quadCursor;
        batches[batchCursor].quadCount = count;
        quadCursor += count;
        batchCursor += 1u;
    }

    ctx->stats.batchesBuilt = batchCount;
    result.quads = quads;
    result.quadCount = totalQuads;
    result.batches = batches;
    result.batchCount = batchCount;
    return result;
}

void draw2d_push_clip(Draw2DContext* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY) {
    if (!ctx || ctx->clipDepth >= DRAW2D_CLIP_STACK_DEPTH) {
        return;
    }

    const F32* current = draw2d_current_clip(ctx);
    F32* clip = ctx->clipStack[ctx->clipDepth];
    clip[0] = MAX(minX, current[0]);
    clip[1] = MAX(minY, current[1]);
    clip[2] = MIN(maxX, current[2]);
    clip[3] = MIN(maxY, current[3]);
    ctx->clipDepth += 1u;
}

void draw2d_pop_clip(Draw2DContext* ctx) {
    if (ctx && ctx->clipDepth != 0u) {
        ctx->clipDepth -= 1u;
    }
}

void draw2d_rect(Draw2DContext* ctx, Draw2DLayer layer, F32 minX, F32 minY, F32 maxX, F32 maxY, U32 rgba8) {
    if (!ctx) {
        return;
    }
    draw2d_append_solid(ctx, layer, minX, minY, maxX, maxY, rgba8);
}

void draw2d_line(Draw2DContext* ctx, Draw2DLayer layer, F32 x0, F32 y0, F32 x1, F32 y1, F32 thickness, U32 rgba8) {
    if (!ctx || thickness <= 0.0f) {
        return;
    }

    F32 half = thickness * 0.5f;
    if (y0 == y1) {
        F32 minX = MIN(x0, x1);
        F32 maxX = MAX(x0, x1);
        draw2d_append_solid(ctx, layer, minX, y0 - half, maxX, y0 + half, rgba8);
    } else if (x0 == x1) {
        F32 minY = MIN(y0, y1);
        F32 maxY = MAX(y0, y1);
        draw2d_append_solid(ctx, layer, x0 - half, minY, x0 + half, maxY, rgba8);
    }
}

void draw2d_box(Draw2DContext* ctx, Draw2DLayer layer, F32 minX, F32 minY, F32 maxX, F32 maxY, F32 thickness, U32 rgba8) {
    if (!ctx || thickness <= 0.0f || minX >= maxX || minY >= maxY) {
        return;
    }

    draw2d_append_solid(ctx, layer, minX, minY, maxX, minY + thickness, rgba8);
    draw2d_append_solid(ctx, layer, minX, maxY - thickness, maxX, maxY, rgba8);
    draw2d_append_solid(ctx, layer, minX, minY + thickness, minX + thickness, maxY - thickness, rgba8);
    draw2d_append_solid(ctx, layer, maxX - thickness, minY + thickness, maxX, maxY - thickness, rgba8);
}

void draw2d_glyph_quads(Draw2DContext* ctx, Draw2DLayer layer, const Draw2DQuad* quads, U32 count, F32 offsetX, F32 offsetY) {
    if (!ctx || !quads) {
        return;
    }

    for (U32 quadIndex = 0u; quadIndex < count; ++quadIndex) {
        Draw2DQuad quad = quads[quadIndex];
        quad.minX += offsetX;
        quad.minY += offsetY;
        quad.maxX += offsetX;
        quad.maxY += offsetY;
        if (!draw2d_clip_quad(ctx, &quad)) {
            continue;
        }
        Draw2DQuad* dst = draw2d_push_quad(ctx, layer);
        if (!dst) {
            return;
        }
        *dst = quad;
    }
}
