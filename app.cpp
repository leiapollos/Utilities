//
// Created by André Leite on 31/10/2025.
//

#include "app_interface.hpp"
#include "app_tests.hpp"
#include "app_state.hpp"

#include "imgui.h"

#include "app_tests.cpp"

#include <dirent.h>
#include <sys/stat.h>

#define APP_CORE_STATE_VERSION 3u

static U64 app_total_permanent_size(void);
static AppCoreState* app_get_state(AppMemory* memory);
static void app_assign_tests_state(AppCoreState* state);
static AppTestsState* app_get_tests(AppCoreState* state);
static U32 app_select_worker_count(const AppHostContext* host);
static void app_compile_shaders_from_folder(AppPlatform* platform, AppMemory* memory, AppHostContext* host);

static B32 app_initialize(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(memory->permanentStorage != 0);

    AppCoreState* state = app_get_state(memory);
    B32 needsReset = (!memory->isInitialized) || (state->version != APP_CORE_STATE_VERSION);

    if (needsReset) {
        U64 totalSize = app_total_permanent_size();
        totalSize = (totalSize > memory->permanentStorageSize) ? memory->permanentStorageSize : totalSize;
        MEMSET(memory->permanentStorage, 0, totalSize);
        memory->isInitialized = 1;
    }

    state = app_get_state(memory);
    app_assign_tests_state(state);
    AppTestsState* tests = app_get_tests(state);

    thread_context_alloc();
    log_init();
    if (needsReset) {
        state->version = APP_CORE_STATE_VERSION;
        state->desiredWindow.title = "Utilities Hot Reload";
        state->desiredWindow.width = 1280u;
        state->desiredWindow.height = 720u;
        state->windowHandle.handle = 0;
        state->frameCounter = 0;
        state->reloadCount = 0;
        set_log_level(LogLevel_Info);
        StringU8 eventsDomain = str8((const char*) "events", 6);
        set_log_domain_level(eventsDomain, LogLevel_Debug);
    }

    if (!state->jobSystem) {
        state->workerCount = app_select_worker_count(host);
        state->jobSystem = job_system_create(memory->programArena, state->workerCount);
        if (!state->jobSystem) {
            LOG_ERROR("jobs", "Failed to create job system (workers={})", state->workerCount);
            ASSERT_ALWAYS(state->jobSystem != 0);
        } else {
            LOG_INFO("jobs", "Job system ready (workers={})", state->workerCount);
        }
    }

    if (needsReset) {
        app_tests_initialize(memory, state, tests);
    }

    host->reloadCount = state->reloadCount;

    if (!state->windowHandle.handle) {
        OS_WindowDesc desc = {};
        desc.title = state->desiredWindow.title;
        desc.width = state->desiredWindow.width;
        desc.height = state->desiredWindow.height;
        state->windowHandle = PLATFORM_OS_CALL(platform, OS_window_create, desc);
    }

    ASSERT_ALWAYS(state->windowHandle.handle != 0);
    ASSERT_ALWAYS(host->renderer != 0);

    if (state->desiredWindow.width > 0u && state->desiredWindow.height > 0u) {
        PLATFORM_RENDERER_CALL(platform,
                               renderer_imgui_set_window_size,
                               host->renderer,
                               state->desiredWindow.width,
                               state->desiredWindow.height);
    }

    B32 imguiInitOk = PLATFORM_RENDERER_CALL(platform,
                                             renderer_imgui_init,
                                             host->renderer,
                                             state->windowHandle);
    ASSERT_ALWAYS(imguiInitOk != 0);

    app_compile_shaders_from_folder(platform, memory, host);

    return 1;
}

static void app_reload(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(host != 0);

    AppCoreState* state = app_get_state(memory);
    app_assign_tests_state(state);
    AppTestsState* tests = app_get_tests(state);

    state->reloadCount += 1;
    host->reloadCount = state->reloadCount;

    app_tests_reload(memory, state, tests);
}

static void app_update(AppPlatform* platform, AppMemory* memory, AppHostContext* host, const AppInput* input,
                       F32 deltaSeconds) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(memory->permanentStorage != 0);
    ASSERT_ALWAYS(memory->isInitialized != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(input != 0);

    AppCoreState* state = app_get_state(memory);
    AppTestsState* tests = app_get_tests(state);

    state->frameCounter += 1ull;

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_process_events,
                           host->renderer,
                           input->events,
                           input->eventCount);

    for (U32 eventIndex = 0; eventIndex < input->eventCount; ++eventIndex) {
        const OS_GraphicsEvent* evt = input->events + eventIndex;
        ASSERT_ALWAYS(evt != 0);

        switch (evt->type) {
            case OS_GraphicsEventType_WindowShown: {
                if (!state->windowHandle.handle) {
                    state->windowHandle = evt->window;
                }
                if (evt->windowEvent.width != 0u && evt->windowEvent.height != 0u) {
                    state->desiredWindow.width = evt->windowEvent.width;
                    state->desiredWindow.height = evt->windowEvent.height;
                    PLATFORM_RENDERER_CALL(platform,
                                           renderer_imgui_set_window_size,
                                           host->renderer,
                                           state->desiredWindow.width,
                                           state->desiredWindow.height);
                }
            }
            break;

            case OS_GraphicsEventType_WindowClosed:
            case OS_GraphicsEventType_WindowDestroyed: {
                if (state->windowHandle.handle == evt->window.handle) {
                    OS_WindowHandle closedHandle = state->windowHandle;
                    state->windowHandle.handle = 0;
                    host->shouldQuit = 1;
                    PLATFORM_OS_CALL(platform, OS_window_destroy, closedHandle);
                    return;
                }
            }
            break;

            case OS_GraphicsEventType_WindowResized: {
                if (state->windowHandle.handle == evt->window.handle) {
                    state->desiredWindow.width = evt->windowEvent.width;
                    state->desiredWindow.height = evt->windowEvent.height;
                    PLATFORM_RENDERER_CALL(platform,
                                           renderer_on_window_resized,
                                           host->renderer,
                                           state->desiredWindow.width,
                                           state->desiredWindow.height);
                }
            }
            break;

            case OS_GraphicsEventType_MouseMove: {
                LOG_DEBUG("app", "Mouse moved to ({}, {})", evt->mouse.x, evt->mouse.y);
            }
            break;

            case OS_GraphicsEventType_MouseButtonDown:
            case OS_GraphicsEventType_MouseButtonUp:
            case OS_GraphicsEventType_MouseScroll:
            case OS_GraphicsEventType_KeyDown:
            case OS_GraphicsEventType_KeyUp:
            case OS_GraphicsEventType_TextInput:
            default: {
            }
                break;
        }
    }

    ASSERT_ALWAYS(state->windowHandle.handle != 0);

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_set_window_size,
                           host->renderer,
                           state->desiredWindow.width,
                           state->desiredWindow.height);

    app_tests_tick(memory, state, tests, deltaSeconds);

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_begin_frame,
                           host->renderer,
                           deltaSeconds);

    ImGui::ShowDemoWindow(nullptr);
    ImGui::Text("frameTime: %f", static_cast<double>(deltaSeconds));
    ImGui::Text("FPS: %f", static_cast<double>(1.0f / deltaSeconds));
    ImGui::Text("frameNumber: %lld", state->frameCounter);
    {
        ImGui::ColorEdit4("Color", tests->drawColor.v);

        static int selected_fish = -1;
        const char* names[] = { "Bream", "Haddock", "Mackerel", "Pollock", "Tilefish" };
        const char* names2[] = { "Bream2", "Haddock2", "Mackerel2", "Pollock2", "Tilefish2" };
        static bool toggles[] = { true, false, false, false, false };
        if (ImGui::Button("Select.."))
            ImGui::OpenPopup("my_select_popup");
        if (ImGui::BeginPopup("my_select_popup"))
        {
            for (int i = 0; i < IM_ARRAYSIZE(names); i++)
                if (ImGui::Selectable(names[i]))
                    selected_fish = i;
            ImGui::SeparatorText("Aquarium");
            for (int i = 0; i < IM_ARRAYSIZE(names2); i++)
                if (ImGui::Selectable(names2[i]))
                    selected_fish = i;
            ImGui::EndPopup();
        }
        ImGui::Text("Selected fish index: %d", selected_fish);
    }
    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_end_frame,
                           host->renderer);

    U64 frame = state->frameCounter;
    F32 velocity = 0.05f;
    F32 x = frame * velocity;
    F32 red = CLAMP(sin(x) * 0.5f + 0.5f, 0.3f, 0.7f);
    F32 green = CLAMP(cos(x) * 0.5f + 0.5f, 0.3f, 0.7f);
    F32 blue = CLAMP(sin(x + 0.3f) * 0.5f + 0.5f, 0.3f, 0.7f);
    Vec4F32 color = {};
    color.r = red;
    color.g = green;
    color.b = blue;
    color.a = 1.0f;
    PLATFORM_RENDERER_CALL(platform, renderer_draw_color, host->renderer, state->windowHandle, tests->drawColor);
}

static void app_shutdown(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(memory->permanentStorage != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);

    AppCoreState* state = app_get_state(memory);
    AppTestsState* tests = app_get_tests(state);

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_shutdown,
                           host->renderer);

    app_tests_shutdown(memory, state, tests);

    if (state->jobSystem) {
        job_system_destroy(state->jobSystem);
        state->jobSystem = 0;
        state->workerCount = 0;
    }

    if (state->windowHandle.handle) {
        PLATFORM_OS_CALL(platform, OS_window_destroy, state->windowHandle);
        state->windowHandle.handle = 0;
    }
}

// ////////////////////////
// App Helpers

static U64 app_total_permanent_size(void) {
    return sizeof(AppCoreState) + app_tests_permanent_size();
}

static AppCoreState* app_get_state(AppMemory* memory) {
    ASSERT_DEBUG(memory != 0);
    ASSERT_DEBUG(memory->permanentStorage != 0);
    ASSERT_DEBUG(memory->permanentStorageSize >= app_total_permanent_size());
    return (AppCoreState*) memory->permanentStorage;
}

static void app_assign_tests_state(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);
    U8* base = (U8*) state;
    state->tests = (AppTestsState*) (base + sizeof(AppCoreState));
}

static AppTestsState* app_get_tests(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);
    return state->tests;
}

static U32 app_select_worker_count(const AppHostContext* host) {
    ASSERT_ALWAYS(host != 0);
    U32 logicalCores = host->logicalCoreCount;
    if (logicalCores == 0u) {
        ASSERT_ALWAYS(logicalCores != 0u);
        logicalCores = 1u;
    }
    U32 workers = (logicalCores > 1u) ? (logicalCores - 1u) : 1u;
    return workers;
}

static void app_compile_shaders_from_folder(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);
    ASSERT_ALWAYS(host->renderer->backendData != 0);

    AppCoreState* state = app_get_state(memory);
    ASSERT_ALWAYS(state->jobSystem != 0);

    Temp scratch = get_scratch(0, 0);
    ASSERT_ALWAYS(scratch.arena != 0);
    DEFER_REF(temp_end(&scratch));
    Arena* arena = scratch.arena;

    StringU8 shadersDir = str8("shaders");
    DIR* dir = opendir((const char*) shadersDir.data);
    ASSERT_ALWAYS(dir != 0);

    Str8List shaderFiles = {};
    str8list_init(&shaderFiles, arena, 16u);

    struct dirent* entry;
    while ((entry = readdir(dir)) != 0) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        StringU8 fileName = str8((const char*) entry->d_name, C_STR_LEN(entry->d_name));
        
        // Check for .hlsl extension
        if (fileName.size < 5) {
            continue;
        }
        
        StringU8 suffix = str8((const char*)fileName.data + fileName.size - 5, 5);
        if (!str8_equal(suffix, str8(".hlsl"))) {
            continue;
        }
        
        StringU8 shaderPath = str8_concat(arena, shadersDir, str8("/"), fileName);
        
        OS_FileInfo fileInfo = OS_get_file_info((const char*) shaderPath.data);
        if (fileInfo.exists) {
            str8list_push(&shaderFiles, shaderPath);
        }
    }
    closedir(dir);

    ASSERT_ALWAYS(shaderFiles.count > 0u);

    ShaderCompileRequest* requests = ARENA_PUSH_ARRAY(arena, ShaderCompileRequest, shaderFiles.count);
    ShaderHandle* handles = ARENA_PUSH_ARRAY(arena, ShaderHandle, shaderFiles.count);

    ASSERT_ALWAYS(requests != 0);
    ASSERT_ALWAYS(handles != 0);

    for (U64 i = 0u; i < shaderFiles.count; ++i) {
        requests[i].shaderPath = str8_cpy(arena, shaderFiles.items[i]);
        requests[i].outHandle = &handles[i];
        handles[i] = SHADER_HANDLE_INVALID;
    }
    {
        TIME_SCOPE("Shader Compilation");
        PLATFORM_RENDERER_CALL(platform, renderer_compile_shaders, host->renderer, arena, state->jobSystem, requests, (U32) shaderFiles.count);
    }
}


APP_MODULE_EXPORT B32 app_get_entry_points(AppModuleExports* outExports) {
    ASSERT_ALWAYS(outExports != 0);
    MEMSET(outExports, 0, sizeof(AppModuleExports));
    outExports->interfaceVersion = APP_INTERFACE_VERSION;
    outExports->requiredPermanentMemory = app_total_permanent_size();
    outExports->requiredTransientMemory = 0;
    outExports->requiredProgramArenaSize = 0;
    outExports->initialize = app_initialize;
    outExports->reload = app_reload;
    outExports->update = app_update;
    outExports->shutdown = app_shutdown;
    return 1;
}
