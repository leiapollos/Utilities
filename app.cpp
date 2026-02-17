//
// Created by André Leite on 31/10/2025.
//

#include "app_interface.hpp"
#include "app_tests.hpp"
#include "app_state.hpp"

#include "imgui.h"

#include "app/app_camera.cpp"
#include "app_tests.cpp"
#include "app/app_scene_sponza.cpp"
#include "app/app_scene_radiance_2d.cpp"

#include <dirent.h>
#include <sys/stat.h>

#define APP_CORE_STATE_VERSION 8u

static U64 app_total_permanent_size(void);
static AppCoreState* app_get_state(AppMemory* memory);
static void app_assign_tests_state(AppCoreState* state);
static AppTestsState* app_get_tests(AppCoreState* state);
static U32 app_select_worker_count(const AppHostContext* host);
static void app_compile_shaders_from_folder(AppPlatform* platform, AppMemory* memory, AppHostContext* host);
static void app_request_close(AppPlatform* platform, AppHostContext* host, AppCoreState* state);
static void app_switch_scene(AppCoreState* state, AppSceneKind scene);
static void app_apply_scene_switch(AppCoreState* state);
static const char* app_scene_name(AppSceneKind scene);

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
        state->activeScene = AppSceneKind_Sponza;
        state->pendingSceneSwitch = AppSceneKind_Sponza;

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

    app_scene_sponza_init(platform, memory, host, state);
    app_scene_radiance_2d_init(platform, memory, host, state);

    if (needsReset) {
        state->camera.position = {{0.0f, 0.5f, 0.0f}};
        state->camera.velocity = {{0.0f, 0.0f, 0.0f}};
        camera_init(&state->camera);
        state->camera.sensitivity = 0.005f;
        state->camera.moveSpeed = 1.0f;
        app_scene_sponza_on_enter(state);
    }

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

static void app_request_close(AppPlatform* platform, AppHostContext* host, AppCoreState* state) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(state != 0);

    if (!state->windowHandle.handle) {
        host->shouldQuit = 1;
        return;
    }

    OS_WindowHandle closedHandle = state->windowHandle;
    state->windowHandle.handle = 0;
    host->shouldQuit = 1;
    PLATFORM_OS_CALL(platform, OS_window_destroy, closedHandle);
}

static void app_switch_scene(AppCoreState* state, AppSceneKind scene) {
    ASSERT_ALWAYS(state != 0);
    if (scene >= AppSceneKind_COUNT) {
        return;
    }
    state->pendingSceneSwitch = scene;
}

static void app_apply_scene_switch(AppCoreState* state) {
    ASSERT_ALWAYS(state != 0);
    if (state->pendingSceneSwitch == state->activeScene) {
        return;
    }

    AppSceneKind nextScene = state->pendingSceneSwitch;
    state->activeScene = nextScene;

    if (nextScene == AppSceneKind_Sponza) {
        app_scene_sponza_on_enter(state);
    } else if (nextScene == AppSceneKind_Radiance2D) {
        app_scene_radiance_2d_on_enter(state);
    }
}

static const char* app_scene_name(AppSceneKind scene) {
    if (scene == AppSceneKind_Sponza) {
        return "Sponza";
    }
    if (scene == AppSceneKind_Radiance2D) {
        return "Radiance2D";
    }
    return "Unknown";
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

        B32 imguiWantCaptureMouse = ImGui::GetIO().WantCaptureMouse ? 1 : 0;

        switch (evt->tag) {
            case OS_GraphicsEvent_Tag_WindowShown: {
                if (!state->windowHandle.handle) {
                    state->windowHandle = evt->window;
                }
                if (evt->windowShown.width != 0u && evt->windowShown.height != 0u) {
                    state->desiredWindow.width = evt->windowShown.width;
                    state->desiredWindow.height = evt->windowShown.height;
                    PLATFORM_RENDERER_CALL(platform,
                                           renderer_imgui_set_window_size,
                                           host->renderer,
                                           state->desiredWindow.width,
                                           state->desiredWindow.height);
                }
            }
            break;

            case OS_GraphicsEvent_Tag_WindowClosed:
            case OS_GraphicsEvent_Tag_WindowDestroyed: {
                if (state->windowHandle.handle == evt->window.handle) {
                    app_request_close(platform, host, state);
                    return;
                }
            }
            break;

            case OS_GraphicsEvent_Tag_WindowResized: {
                if (state->windowHandle.handle == evt->window.handle) {
                    state->desiredWindow.width = evt->windowResized.width;
                    state->desiredWindow.height = evt->windowResized.height;
                    RendererResizeDesc resizeDesc =
                        renderer_resize_desc(state->desiredWindow.width, state->desiredWindow.height);
                    PLATFORM_RENDERER_CALL(platform,
                                           renderer_resize,
                                           host->renderer,
                                           &resizeDesc);
                }
            }
            break;

            case OS_GraphicsEvent_Tag_KeyDown: {
                if (evt->keyDown.keyCode == OS_KeyCode_F2 && !evt->keyDown.isRepeat) {
                    AppSceneKind toggled = (state->activeScene == AppSceneKind_Sponza)
                                               ? AppSceneKind_Radiance2D
                                               : AppSceneKind_Sponza;
                    app_switch_scene(state, toggled);
                }
            }
            break;

            default: {
            }
            break;
        }

        if (state->activeScene == AppSceneKind_Sponza) {
            app_scene_sponza_handle_event(evt, state, imguiWantCaptureMouse);
        } else if (state->activeScene == AppSceneKind_Radiance2D) {
            app_scene_radiance_2d_handle_event(evt, state, imguiWantCaptureMouse);
        }
    }

    ASSERT_ALWAYS(state->windowHandle.handle != 0);

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_set_window_size,
                           host->renderer,
                           state->desiredWindow.width,
                           state->desiredWindow.height);

    app_apply_scene_switch(state);
    app_tests_tick(memory, state, tests, deltaSeconds);

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_begin_frame,
                           host->renderer,
                           deltaSeconds);

    ImGui::Text("frameTime: %f", (double) deltaSeconds);
    ImGui::Text("FPS: %f", (double) (1.0f / MAX(deltaSeconds, 0.000001f)));
    ImGui::Text("frameNumber: %lld", state->frameCounter);

    int selectedScene = (int) state->activeScene;
    const char* sceneNames[AppSceneKind_COUNT] = {"Sponza", "Radiance2D"};
    if (ImGui::Combo("Scene", &selectedScene, sceneNames, AppSceneKind_COUNT)) {
        app_switch_scene(state, (AppSceneKind) selectedScene);
    }

    ImGui::Text("Active Scene: %s", app_scene_name(state->activeScene));

    if (state->activeScene == AppSceneKind_Sponza) {
        app_scene_sponza_build_ui(state);
    } else if (state->activeScene == AppSceneKind_Radiance2D) {
        app_scene_radiance_2d_build_ui(state);
    }

    PLATFORM_RENDERER_CALL(platform,
                           renderer_imgui_end_frame,
                           host->renderer);

    app_apply_scene_switch(state);

    if (state->activeScene == AppSceneKind_Sponza) {
        app_scene_sponza_render(platform, host, state, deltaSeconds);
    } else if (state->activeScene == AppSceneKind_Radiance2D) {
        app_scene_radiance_2d_render(platform, host, state, deltaSeconds);
    }
}

static void app_shutdown(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(memory->permanentStorage != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);

    AppCoreState* state = app_get_state(memory);
    AppTestsState* tests = app_get_tests(state);

    app_scene_radiance_2d_shutdown(platform, host, state);
    app_scene_sponza_shutdown(platform, host, state);

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

        if (fileName.size < 5) {
            continue;
        }

        StringU8 suffix = str8((const char*) fileName.data + fileName.size - 5, 5);
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
        PLATFORM_RENDERER_CALL(platform,
                               renderer_compile_shaders,
                               host->renderer,
                               arena,
                               state->jobSystem,
                               requests,
                               (U32) shaderFiles.count);
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
