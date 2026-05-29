//
// Created by André Leite on 31/10/2025.
//

#include "app_interface.hpp"
#include "app_tests.hpp"
#include "app_state.hpp"

#include "app_tests.cpp"

#define APP_CORE_STATE_VERSION 19u
#define APP_TESTS_STATE_VERSION 2u

struct AppGfxVertex {
    F32 position[2];
    F32 color[4];
};

struct AppGfxDrawData {
    F32 offsetScale[4];
};

static const char APP_GFX_TRIANGLE_SHADER[] =
R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct DrawData {
    float4 offsetScale;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]], constant DrawData& drawData [[buffer(8)]]) {
    VertexOut out;
    float2 position = in.position * drawData.offsetScale.z + drawData.offsetScale.xy;
    out.position = float4(position, 0.0, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}
)msl";

static const APP_StateDesc* app_state_desc(APP_StateKind kind);
static void* app_state_require(APP_Context* ctx, APP_StateKind kind);
static B32 app_context_from_call(AppHost* host, HOT_StateStore* store, APP_Context* outCtx);
static void app_state_init(APP_Context* ctx, APP_StateKind kind, void* memory);
static U32 app_select_worker_count(const AppHost* host);
static B32 app_ensure_job_system(APP_Context* ctx);
static B32 app_should_run_tests(AppHost* host);
static B32 app_gfx_demo_init(APP_Context* ctx);
static void app_gfx_demo_shutdown(APP_Context* ctx);
static void app_gfx_run_resource_tests(APP_Context* ctx);
static void app_gfx_run_frame_tests(APP_Context* ctx, GfxFrame* frame);
static void app_gfx_demo_frame(APP_Context* ctx);

static B32 app_boot(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();
    set_log_level(LogLevel_Info);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return 0;
    }

    ctx.core->testsEnabled = app_should_run_tests(host);
    if (ctx.core->testsEnabled) {
        LOG_INFO("tests", "Per-frame tests enabled by UTILITIES_RUN_TESTS");
        if (!app_ensure_job_system(&ctx)) {
            return 0;
        }
    }

    if (!app_gfx_demo_init(&ctx)) {
        return 0;
    }

    ASSERT_ALWAYS(host->window.handle != 0);
    return 1;
}

static void app_before_reload(AppHost* host, HOT_StateStore* store) {
    APP_Context ctx = {};
    if (app_context_from_call(host, store, &ctx)) {
        app_gfx_demo_shutdown(&ctx);
    }
}

static B32 app_after_reload(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    log_init();

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return 0;
    }

    ctx.core->reloadCount += 1u;
    ctx.core->testsEnabled = app_should_run_tests(host);

    if (ctx.core->testsEnabled && !app_ensure_job_system(&ctx)) {
        return 0;
    }

    if (!ctx.core->gfxDemoInitialized && !app_gfx_demo_init(&ctx)) {
        return 0;
    }

    app_tests_reload(&ctx, ctx.tests);
    return 1;
}

static void app_frame(AppHost* host, HOT_StateStore* store, const AppInput* input) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);
    ASSERT_ALWAYS(input != 0);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return;
    }

    AppCoreState* state = ctx.core;
    state->windowWidth = host->windowWidth;
    state->windowHeight = host->windowHeight;
    state->frameCounter += 1ull;

    for (U32 eventIndex = 0; eventIndex < input->eventCount; ++eventIndex) {
        const OS_GraphicsEvent* event = input->events + eventIndex;
        ASSERT_ALWAYS(event != 0);

        if (event->tag == OS_GraphicsEvent_Tag_KeyDown &&
            event->keyDown.keyCode == OS_KeyCode_Escape &&
            !event->keyDown.isRepeat) {
            host->shouldQuit = 1;
        }
    }

    if (state->testsEnabled) {
        app_tests_tick(&ctx, ctx.tests, input->deltaSeconds);
    }

    app_gfx_demo_frame(&ctx);
}

static void app_shutdown(AppHost* host, HOT_StateStore* store) {
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(store != 0);

    APP_Context ctx = {};
    if (!app_context_from_call(host, store, &ctx)) {
        return;
    }

    app_tests_shutdown(&ctx, ctx.tests);
    app_gfx_demo_shutdown(&ctx);

    if (ctx.core->jobSystem) {
        job_system_destroy(ctx.core->jobSystem);
        ctx.core->jobSystem = 0;
        ctx.core->workerCount = 0;
    }
}

static B32 app_gfx_demo_init(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    if (state->gfxDemoInitialized) {
        return 1;
    }
    if (!ctx->host->gfxDevice) {
        LOG_ERROR("gfx", "App has no gfx device");
        return 0;
    }

    static const AppGfxVertex vertices[] = {
        {{ 0.0f,  0.55f}, {1.0f, 0.2f, 0.1f, 1.0f}},
        {{-0.55f, -0.45f}, {0.1f, 0.9f, 0.2f, 1.0f}},
        {{ 0.55f, -0.45f}, {0.1f, 0.3f, 1.0f, 1.0f}},
    };

    static const U16 indices[] = {
        0u, 1u, 2u,
    };

    GfxBufferDesc vertexDesc = {};
    vertexDesc.name = "triangle vertices";
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usageFlags = GfxBufferUsageFlags_Vertex;
    vertexDesc.memoryKind = GfxMemoryKind_Device;
    vertexDesc.initialData = vertices;
    state->gfxTriangleVertexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &vertexDesc);

    GfxBufferDesc indexDesc = {};
    indexDesc.name = "triangle indices";
    indexDesc.size = sizeof(indices);
    indexDesc.usageFlags = GfxBufferUsageFlags_Index;
    indexDesc.memoryKind = GfxMemoryKind_Device;
    indexDesc.initialData = indices;
    state->gfxTriangleIndexBuffer = gfx_create_buffer(ctx->host->gfxDevice, &indexDesc);

    GfxVertexAttribute attributes[2] = {};
    attributes[0].location = 0u;
    attributes[0].offset = 0u;
    attributes[0].format = GfxVertexFormat_F32x2;
    attributes[1].location = 1u;
    attributes[1].offset = sizeof(F32) * 2u;
    attributes[1].format = GfxVertexFormat_F32x4;

    GfxFormat colorFormats[1] = {
        GfxFormat_BGRA8_UNorm,
    };

    GfxGraphicsPipelineDesc pipelineDesc = {};
    pipelineDesc.name = "triangle pipeline";
    pipelineDesc.vertexShader.format = GfxShaderFormat_MSL_Source;
    pipelineDesc.vertexShader.data = APP_GFX_TRIANGLE_SHADER;
    pipelineDesc.vertexShader.size = C_STR_LEN(APP_GFX_TRIANGLE_SHADER);
    pipelineDesc.vertexShader.entry = "vertex_main";
    pipelineDesc.fragmentShader = pipelineDesc.vertexShader;
    pipelineDesc.fragmentShader.entry = "fragment_main";
    pipelineDesc.attributes = attributes;
    pipelineDesc.attributeCount = ARRAY_COUNT(attributes);
    pipelineDesc.vertexBuffer.stride = sizeof(AppGfxVertex);
    pipelineDesc.topology = GfxPrimitiveTopology_TriangleList;
    pipelineDesc.raster.cullMode = GfxCullMode_None;
    pipelineDesc.raster.frontFace = GfxFrontFace_CCW;
    pipelineDesc.depth.compareOp = GfxCompareOp_Always;
    pipelineDesc.colorFormats = colorFormats;
    pipelineDesc.colorFormatCount = ARRAY_COUNT(colorFormats);
    pipelineDesc.depthFormat = GfxFormat_Invalid;

    state->gfxTrianglePipeline = gfx_create_graphics_pipeline(ctx->host->gfxDevice, &pipelineDesc);

    if (state->testsEnabled) {
        app_gfx_run_resource_tests(ctx);
    }

    state->gfxDemoInitialized = 1;
    return 1;
}

static void app_gfx_demo_shutdown(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    AppCoreState* state = ctx->core;
    GfxDevice* device = ctx->host ? ctx->host->gfxDevice : 0;
    if (!device) {
        return;
    }

    gfx_wait_idle(device);
    gfx_destroy_pipeline(device, state->gfxTrianglePipeline);
    gfx_destroy_buffer(device, state->gfxTriangleIndexBuffer);
    gfx_destroy_buffer(device, state->gfxTriangleVertexBuffer);

    state->gfxTrianglePipeline = {};
    state->gfxTriangleIndexBuffer = {};
    state->gfxTriangleVertexBuffer = {};
    state->gfxDemoInitialized = 0;
}

static void app_gfx_run_resource_tests(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->host->gfxDevice != 0);

    U32 value = 0x12345678u;
    GfxBufferDesc desc = {};
    desc.name = "gfx test buffer";
    desc.size = sizeof(value);
    desc.usageFlags = GfxBufferUsageFlags_Uniform;
    desc.memoryKind = GfxMemoryKind_Upload;
    desc.initialData = &value;

    GfxBuffer buffer = gfx_create_buffer(ctx->host->gfxDevice, &desc);
    ASSERT_ALWAYS(buffer.generation != 0u);

    GfxResourceId first = gfx_register_buffer(ctx->host->gfxDevice, buffer);
    GfxResourceId second = gfx_register_buffer(ctx->host->gfxDevice, buffer);
    ASSERT_ALWAYS(first.index != 0u);
    ASSERT_ALWAYS(first.index == second.index);

    gfx_destroy_buffer(ctx->host->gfxDevice, buffer);
}

static void app_gfx_run_frame_tests(APP_Context* ctx, GfxFrame* frame) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (!ctx->core->testsEnabled || ctx->core->gfxTestsRan) {
        return;
    }

    GfxTemp aligned = gfx_allocate_temp(frame, 16u, 16u);
    ASSERT_ALWAYS(aligned.cpu != 0);
    ASSERT_ALWAYS((aligned.gpu.offset & 15u) == 0u);

    GfxTemp overflow = gfx_allocate_temp(frame, GB(1), 16u);
    ASSERT_ALWAYS(overflow.cpu == 0);

    ctx->core->gfxTestsRan = 1;
}

static void app_gfx_demo_frame(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);

    if (!ctx->host->gfxDevice || !ctx->core->gfxDemoInitialized) {
        return;
    }
    if (ctx->host->windowWidth == 0u || ctx->host->windowHeight == 0u) {
        return;
    }

    GfxFrame* frame = gfx_begin_frame(ctx->host->gfxDevice);
    if (!frame) {
        return;
    }

    app_gfx_run_frame_tests(ctx, frame);

    GfxCommandBuffer* commands = gfx_get_command_buffer(frame);
    GfxTexture backbuffer = gfx_get_backbuffer(frame);

    GfxTemp drawTemp = gfx_allocate_temp(frame, sizeof(AppGfxDrawData), 16u);
    if (drawTemp.cpu) {
        AppGfxDrawData* drawData = (AppGfxDrawData*)drawTemp.cpu;
        drawData->offsetScale[0] = 0.0f;
        drawData->offsetScale[1] = 0.0f;
        drawData->offsetScale[2] = 0.95f;
        drawData->offsetScale[3] = 1.0f;
    }

    GfxColorTarget colorTarget = {};
    colorTarget.texture = backbuffer;
    colorTarget.loadOp = GfxLoadOp_Clear;
    colorTarget.storeOp = GfxStoreOp_Store;
    colorTarget.clearColor[0] = 0.06f;
    colorTarget.clearColor[1] = 0.08f;
    colorTarget.clearColor[2] = 0.10f;
    colorTarget.clearColor[3] = 1.0f;

    GfxRenderPassDesc pass = {};
    pass.name = "triangle pass";
    pass.colorTargets = &colorTarget;
    pass.colorTargetCount = 1u;
    pass.width = ctx->host->windowWidth;
    pass.height = ctx->host->windowHeight;

    GfxDraw draw = {};
    draw.pipeline = ctx->core->gfxTrianglePipeline;
    draw.vertexBuffer = ctx->core->gfxTriangleVertexBuffer;
    draw.indexBuffer = ctx->core->gfxTriangleIndexBuffer;
    draw.indexCount = 3u;
    draw.instanceCount = 1u;
    draw.indexType = GfxIndexType_U16;
    draw.drawData = drawTemp.gpu;

    GfxDrawArea area = {};
    area.viewport.x = 0.0f;
    area.viewport.y = 0.0f;
    area.viewport.width = (F32)ctx->host->windowWidth;
    area.viewport.height = (F32)ctx->host->windowHeight;
    area.viewport.minDepth = 0.0f;
    area.viewport.maxDepth = 1.0f;
    area.scissor.x = 0;
    area.scissor.y = 0;
    area.scissor.width = ctx->host->windowWidth;
    area.scissor.height = ctx->host->windowHeight;
    area.draws = &draw;
    area.drawCount = 1u;

    gfx_render_pass(commands, &pass, &area, 1u);
    gfx_submit(commands);
    gfx_end_frame(frame);
}

static const APP_StateDesc* app_state_desc(APP_StateKind kind) {
    static const APP_StateDesc descs[APP_State_COUNT] = {
        {APP_STATE_ID('C', 'O', 'R', 'E'), "core", APP_CORE_STATE_VERSION,
         sizeof(AppCoreState), alignof(AppCoreState)},
        {APP_STATE_ID('T', 'E', 'S', 'T'), "tests", APP_TESTS_STATE_VERSION,
         sizeof(AppTestsState), alignof(AppTestsState)},
    };

    if ((U32) kind >= APP_State_COUNT) {
        return 0;
    }
    return &descs[(U32) kind];
}

static void* app_state_require(APP_Context* ctx, APP_StateKind kind) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->store != 0);

    const APP_StateDesc* desc = app_state_desc(kind);
    if (desc == 0) {
        LOG_ERROR("app", "Invalid state kind {}", (U32) kind);
        return 0;
    }

    void* memory = hot_state_store_require(ctx->store, desc->id, desc->version, desc->size, desc->alignment);
    if (memory == 0) {
        LOG_ERROR("app", "State '{}' unavailable (size={} align={})", str8((const char*)desc->name),
                  desc->size, desc->alignment);
        return 0;
    }

    if (hot_state_store_take_needs_init(ctx->store, desc->id)) {
        app_state_init(ctx, kind, memory);
    }

    return memory;
}

static B32 app_context_from_call(AppHost* host, HOT_StateStore* store, APP_Context* outCtx) {
    ASSERT_ALWAYS(outCtx != 0);
    MEMSET(outCtx, 0, sizeof(*outCtx));

    if (host == 0 || store == 0 || !hot_state_store_is_valid(store)) {
        LOG_ERROR("app", "Invalid app call context");
        return 0;
    }

    APP_Context ctx = {};
    ctx.host = host;
    ctx.store = store;

    ctx.core = (AppCoreState*) app_state_require(&ctx, APP_State_Core);
    if (ctx.core == 0) {
        return 0;
    }

    ctx.tests = (AppTestsState*) app_state_require(&ctx, APP_State_Tests);
    if (ctx.tests == 0) {
        return 0;
    }

    *outCtx = ctx;
    return 1;
}

static void app_state_init(APP_Context* ctx, APP_StateKind kind, void* memory) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(memory != 0);

    switch (kind) {
        case APP_State_Core: {
            AppCoreState* core = (AppCoreState*) memory;
            MEMSET(core, 0, sizeof(*core));
            core->windowWidth = ctx->host->windowWidth;
            core->windowHeight = ctx->host->windowHeight;

            StringU8 eventsDomain = str8((const char*) "events", 6);
            set_log_domain_level(eventsDomain, LogLevel_Debug);
        }
        break;

        case APP_State_Tests: {
            app_tests_initialize(ctx, (AppTestsState*) memory);
        }
        break;

        default: {
            ASSERT_ALWAYS(false && "Invalid app state kind");
        }
        break;
    }
}

static U32 app_select_worker_count(const AppHost* host) {
    ASSERT_ALWAYS(host != 0);
    U32 logicalCores = host->logicalCoreCount;
    if (logicalCores == 0u) {
        ASSERT_ALWAYS(logicalCores != 0u);
        logicalCores = 1u;
    }
    U32 workers = (logicalCores > 1u) ? (logicalCores - 1u) : 1u;
    return workers;
}

static B32 app_ensure_job_system(APP_Context* ctx) {
    ASSERT_ALWAYS(ctx != 0);
    ASSERT_ALWAYS(ctx->host != 0);
    ASSERT_ALWAYS(ctx->core != 0);
    ASSERT_ALWAYS(ctx->host->stateArena != 0);

    AppCoreState* state = ctx->core;
    if (state->jobSystem) {
        return 1;
    }

    state->workerCount = app_select_worker_count(ctx->host);
    state->jobSystem = job_system_create(ctx->host->stateArena, state->workerCount);
    if (!state->jobSystem) {
        LOG_ERROR("jobs", "Failed to create job system (workers={})", state->workerCount);
        ASSERT_ALWAYS(state->jobSystem != 0);
        return 0;
    }

    LOG_INFO("jobs", "Job system ready (workers={})", state->workerCount);
    return 1;
}

static B32 app_should_run_tests(AppHost* host) {
    ASSERT_ALWAYS(host != 0);

    Temp scratch = get_scratch(0, 0);
    if (!scratch.arena) {
        return 0;
    }
    DEFER_REF(temp_end(&scratch));

    StringU8 value = APP_OS_CALL(host, OS_get_environment_variable, scratch.arena, str8("UTILITIES_RUN_TESTS"));
    if (!value.data || value.size == 0u) {
        return 0;
    }

    U8 first = value.data[0];
    if (first == (U8)'1' || first == (U8)'t' || first == (U8)'T' ||
        first == (U8)'y' || first == (U8)'Y') {
        return 1;
    }
    return 0;
}

APP_EXPORT B32 app_load(AppLoadParams* params, AppCode* outCode) {
    ASSERT_ALWAYS(params != 0);
    ASSERT_ALWAYS(outCode != 0);

    if (params == 0 || outCode == 0) {
        return 0;
    }
    if (params->size != sizeof(AppLoadParams) || params->abiVersion != APP_ABI_VERSION) {
        return 0;
    }
    if (params->host == 0 || params->store == 0) {
        return 0;
    }

    MEMSET(outCode, 0, sizeof(*outCode));
    outCode->size = sizeof(*outCode);
    outCode->abiVersion = APP_ABI_VERSION;
    outCode->schemaVersion = APP_STATE_SCHEMA_VERSION;
    outCode->boot = app_boot;
    outCode->before_reload = app_before_reload;
    outCode->after_reload = app_after_reload;
    outCode->frame = app_frame;
    outCode->shutdown = app_shutdown;
    return 1;
}
