#pragma once

struct EngRendererFrame {
    GfxFrame* frame;
    GfxCommandBuffer* commands;
};

static EngRendererFrame g_engRendererFrame;

static void eng_renderer_watch_files(EngContext* ctx);
static void eng_renderer_resource_cache_reset(EngContext* ctx);
static void eng_renderer_shutdown(EngContext* ctx);

static EngRendererFrame* eng_renderer_begin_frame(EngContext* ctx);
static void eng_renderer_submit_text(EngContext* ctx, EngRendererFrame* rendererFrame, const TextDrawData* drawData, Draw2DLayer layer);
static void eng_renderer_apply_text_uploads(EngContext* ctx, EngRendererFrame* rendererFrame, const TextAtlasUpload* uploads, U32 uploadCount);
static void eng_renderer_end_frame(EngContext* ctx, EngRendererFrame* rendererFrame);
