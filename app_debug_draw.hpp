#pragma once

static void debug_draw_rect(APP_Context* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY, U32 rgba8);
static void debug_draw_box(APP_Context* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY, F32 thickness, U32 rgba8);
static void debug_draw_line(APP_Context* ctx, F32 x0, F32 y0, F32 x1, F32 y1, F32 thickness, U32 rgba8);
static void debug_draw_text(APP_Context* ctx, AppRendererFrame* rendererFrame, F32 x, F32 y, F32 pixelSize, U32 rgba8, StringU8 text);
