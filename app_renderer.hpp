#pragma once

struct AppRendererFrame;

static B32 app_renderer_register_artifact_types(APP_Context* ctx);
static void app_renderer_watch_files(APP_Context* ctx);
static void app_renderer_resource_cache_reset(APP_Context* ctx);
static void app_renderer_shutdown(APP_Context* ctx);

static AppRendererFrame* app_renderer_begin_frame(APP_Context* ctx);
static void app_renderer_submit_text(APP_Context* ctx, AppRendererFrame* rendererFrame, const TextDrawData* drawData, Draw2DLayer layer);
static void app_renderer_apply_text_uploads(APP_Context* ctx, AppRendererFrame* rendererFrame, const TextAtlasUpload* uploads, U32 uploadCount);
static void app_renderer_end_frame(APP_Context* ctx, AppRendererFrame* rendererFrame);
