//
// Created by André Leite on 10/06/2026.
//

static void app_demo_scene_submit(APP_Context* ctx, AppRendererFrame* rendererFrame) {
    AppCoreState* state = ctx->core;
    Draw2DContext* draw2d = &state->render2d.draw2d;
    if (state->render2d.textContext == 0 || state->render2d.font.generation == 0u) {
        return;
    }

    Temp scratch = get_scratch(0, 0);
    if (scratch.arena == 0) {
        return;
    }
    DEFER_REF(temp_end(&scratch));

    F32 panelMinX = 40.0f;
    F32 panelMinY = 40.0f;
    F32 panelMaxX = 980.0f;
    F32 panelMaxY = 420.0f;
    draw2d_rect(draw2d, Draw2DLayer_UI, panelMinX, panelMinY, panelMaxX, panelMaxY, 0x14181CF0u);
    draw2d_box(draw2d, Draw2DLayer_UI, panelMinX, panelMinY, panelMaxX, panelMaxY, 2.0f, 0x3A4148FFu);
    draw2d_line(draw2d, Draw2DLayer_UI, panelMinX + 24.0f, 132.0f, panelMaxX - 24.0f, 132.0f, 2.0f, 0x3A4148FFu);

    TextDrawDesc titleDesc = {};
    titleDesc.font = state->render2d.font;
    titleDesc.text = str8("draw2d + kb_text_shape + FreeType");
    titleDesc.x = panelMinX + 24.0f;
    titleDesc.y = panelMinY + 24.0f;
    titleDesc.pixelSize = 40.0f;
    titleDesc.rgba8 = 0xF4F1E8FFu;
    TextDrawData title = text_prepare_draw(state->render2d.textContext, scratch.arena, &titleDesc);
    app_renderer_submit_text(ctx, rendererFrame, &title, Draw2DLayer_UI);

    TextDrawDesc bodyDesc = {};
    bodyDesc.font = state->render2d.font;
    bodyDesc.text = str8("Hello, text\nOla, acao, coracao\nOl\xC3\xA1, a\xC3\xA7\xC3\xA3o, cora\xC3\xA7\xC3\xA3o\nAVATAR ToYo office ffi fi fl");
    bodyDesc.x = panelMinX + 24.0f;
    bodyDesc.y = 156.0f;
    bodyDesc.pixelSize = 28.0f;
    bodyDesc.rgba8 = 0xD9D4C7FFu;
    TextDrawData body = text_prepare_draw(state->render2d.textContext, scratch.arena, &bodyDesc);
    app_renderer_submit_text(ctx, rendererFrame, &body, Draw2DLayer_UI);

    F32 clipMinX = panelMinX + 24.0f;
    F32 clipMinY = 320.0f;
    F32 clipMaxX = clipMinX + 360.0f;
    F32 clipMaxY = clipMinY + 72.0f;
    draw2d_box(draw2d, Draw2DLayer_UI, clipMinX, clipMinY, clipMaxX, clipMaxY, 1.0f, 0x6B7480FFu);
    draw2d_push_clip(draw2d, clipMinX, clipMinY, clipMaxX, clipMaxY);
    draw2d_rect(draw2d, Draw2DLayer_UI, clipMinX - 40.0f, clipMinY + 12.0f, clipMaxX + 40.0f, clipMinY + 28.0f, 0x4F8A6AFFu);

    TextDrawDesc clippedDesc = {};
    clippedDesc.font = state->render2d.font;
    clippedDesc.text = str8("clipped text runs past the box edge and gets cut");
    clippedDesc.x = clipMinX + 8.0f;
    clippedDesc.y = clipMinY + 34.0f;
    clippedDesc.pixelSize = 24.0f;
    clippedDesc.rgba8 = 0xB9C4D1FFu;
    TextDrawData clipped = text_prepare_draw(state->render2d.textContext, scratch.arena, &clippedDesc);
    app_renderer_submit_text(ctx, rendererFrame, &clipped, Draw2DLayer_UI);
    draw2d_pop_clip(draw2d);

    draw2d_box(draw2d, Draw2DLayer_Debug, panelMaxX - 56.0f, panelMinY + 16.0f, panelMaxX - 16.0f, panelMinY + 56.0f, 2.0f, 0xE2574BFFu);
}
