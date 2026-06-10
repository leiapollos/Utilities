//
// Created by André Leite on 10/06/2026.
//

static void debug_draw_rect(APP_Context* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY, U32 rgba8) {
    draw2d_rect(&ctx->core->render2d.draw2d, Draw2DLayer_Debug, minX, minY, maxX, maxY, rgba8);
}

static void debug_draw_box(APP_Context* ctx, F32 minX, F32 minY, F32 maxX, F32 maxY, F32 thickness, U32 rgba8) {
    draw2d_box(&ctx->core->render2d.draw2d, Draw2DLayer_Debug, minX, minY, maxX, maxY, thickness, rgba8);
}

static void debug_draw_line(APP_Context* ctx, F32 x0, F32 y0, F32 x1, F32 y1, F32 thickness, U32 rgba8) {
    draw2d_line(&ctx->core->render2d.draw2d, Draw2DLayer_Debug, x0, y0, x1, y1, thickness, rgba8);
}

static void debug_draw_text(APP_Context* ctx, AppRendererFrame* rendererFrame, F32 x, F32 y, F32 pixelSize, U32 rgba8, StringU8 text) {
    AppRender2DState* render = &ctx->core->render2d;
    if (render->textContext == 0 || render->font.generation == 0u || text.size == 0u) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    TextDrawDesc desc = {};
    desc.font = render->font;
    desc.text = text;
    desc.x = x;
    desc.y = y;
    desc.pixelSize = pixelSize;
    desc.rgba8 = rgba8;
    TextDrawData drawData = text_prepare_draw(render->textContext, scratch.arena, &desc);
    app_renderer_submit_text(ctx, rendererFrame, &drawData, Draw2DLayer_Debug);
}
