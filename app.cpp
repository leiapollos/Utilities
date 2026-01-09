//
// Created by André Leite on 31/10/2025.
//

#include "app_interface.hpp"
#include "app_tests.hpp"
#include "app_state.hpp"

#include "imgui.h"

#include "app/app_camera.cpp"
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

    if (!state->sceneLoaded) {
        if (scene_load_from_file(memory->programArena, "assets/sponza.glb", &state->scene)) {
            LOG_INFO("app", "Scene loaded: {} meshes, {} materials, {} nodes, {} images",
                     state->scene.meshCount, state->scene.materialCount, 
                     state->scene.nodeCount, state->scene.imageCount);
            
            if (PLATFORM_RENDERER_CALL(platform, renderer_upload_scene, 
                                       host->renderer, memory->programArena, 
                                       &state->scene, &state->gpuScene)) {
                state->sceneLoaded = 1;
                state->meshScale = 0.01f;  // Sponza is large, scale down
                state->meshColor = {{1.0f, 1.0f, 1.0f, 1.0f}};
            } else {
                LOG_ERROR("app", "Failed to upload scene to GPU");
            }
        } else {
            LOG_ERROR("app", "Failed to load Sponza scene");
        }
    }

    if (needsReset) {
        state->camera.position    = {{0.0f, 0.5f, 0.0f}};  // Inside Sponza courtyard
        state->camera.velocity    = {{0.0f, 0.0f, 0.0f}};
        camera_init(&state->camera);
        state->camera.sensitivity = 0.005f;
        state->camera.moveSpeed   = 1.0f;  // Adjusted for scaled scene
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
                    OS_WindowHandle closedHandle = state->windowHandle;
                    state->windowHandle.handle = 0;
                    host->shouldQuit = 1;
                    PLATFORM_OS_CALL(platform, OS_window_destroy, closedHandle);
                    return;
                }
            }
            break;

            case OS_GraphicsEvent_Tag_WindowResized: {
                if (state->windowHandle.handle == evt->window.handle) {
                    state->desiredWindow.width = evt->windowResized.width;
                    state->desiredWindow.height = evt->windowResized.height;
                    PLATFORM_RENDERER_CALL(platform,
                                           renderer_on_window_resized,
                                           host->renderer,
                                           state->desiredWindow.width,
                                           state->desiredWindow.height);
                }
            }
            break;

            case OS_GraphicsEvent_Tag_MouseMove: {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    F32 deltaPitch = -evt->mouseMove.deltaY * state->camera.sensitivity;
                    F32 deltaYaw   =  evt->mouseMove.deltaX * state->camera.sensitivity;
                    camera_rotate(&state->camera, deltaPitch, deltaYaw);
                }
            }
            break;

            case OS_GraphicsEvent_Tag_KeyDown: {
                OS_KeyCode key = evt->keyDown.keyCode;
                     if (key == OS_KeyCode_W) { state->camera.velocity.z = -1.0f; }
                else if (key == OS_KeyCode_S) { state->camera.velocity.z =  1.0f; }
                else if (key == OS_KeyCode_A) { state->camera.velocity.x = -1.0f; }
                else if (key == OS_KeyCode_D) { state->camera.velocity.x =  1.0f; }
            }
            break;

            case OS_GraphicsEvent_Tag_KeyUp: {
                OS_KeyCode key = evt->keyUp.keyCode;
                     if (key == OS_KeyCode_W || key == OS_KeyCode_S) { state->camera.velocity.z = 0.0f; }
                else if (key == OS_KeyCode_A || key == OS_KeyCode_D) { state->camera.velocity.x = 0.0f; }
            }
            break;

            case OS_GraphicsEvent_Tag_MouseButtonDown:
            case OS_GraphicsEvent_Tag_MouseButtonUp:
            case OS_GraphicsEvent_Tag_MouseScroll:
            case OS_GraphicsEvent_Tag_TextInput:
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

        if (state->meshLoaded) {
            int tempMeshCount = (int)state->meshCount;
            ImGui::InputInt("Mesh Count", &tempMeshCount);
            state->meshCount = (tempMeshCount < 0) ? 0 : (U32)tempMeshCount;
            ImGui::SliderFloat("Mesh Scale", &state->meshScale, 0.01f, 1.0f, "%.3f");
            ImGui::ColorEdit4("Mesh Color", state->meshColor.v);
            ImGui::SliderFloat("Mesh Spacing", &state->meshSpacing, 0.1f, 2.0f, "%.2f");
        }

        ImGui::SeparatorText("Camera");
        ImGui::Text("Position: (%.2f, %.2f, %.2f)",
            (double)state->camera.position.x, (double)state->camera.position.y, (double)state->camera.position.z);
        QuatF32 q = state->camera.orientation;
        ImGui::Text("Orientation: (%.3f, %.3f, %.3f, %.3f)", (double)q.x, (double)q.y, (double)q.z, (double)q.w);
        ImGui::SliderFloat("Sensitivity", &state->camera.sensitivity, 0.001f, 0.02f, "%.4f");
        ImGui::SliderFloat("Move Speed", &state->camera.moveSpeed, 0.001f, 2.0f, "%.3f");

        static int selected_fish = -1;
        const char* names[] = { "Bream", "Haddock", "Mackerel", "Pollock", "Tilefish" };
        const char* names2[] = { "Bream2", "Haddock2", "Mackerel2", "Pollock2", "Tilefish2" };
        static B32 toggles[] = { true, false, false, false, false };
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

    camera_update(&state->camera, deltaSeconds);

    F32 fovY = DEG_TO_RAD(70.0f);
    F32 aspect = (F32)state->desiredWindow.width / (F32)state->desiredWindow.height;
    F32 zNear = 0.001f;  // Reduced for 0.01x scaled scene
    F32 zFar = 100.0f;
    
    Mat4x4F32 projection = mat4_perspective(fovY, aspect, zNear, zFar);
    Mat4x4F32 view = camera_get_view_matrix(&state->camera);
    
    SceneData scene = {};
    scene.view = view;
    scene.proj = projection;
    scene.viewproj = view * projection;
    
    scene.ambientColor.r = 0.1f;
    scene.ambientColor.g = 0.1f;
    scene.ambientColor.b = 0.1f;
    scene.ambientColor.a = 1.0f;
    scene.sunDirection.x = 0.0f;
    scene.sunDirection.y = 1.0f;
    scene.sunDirection.z = 0.5f;
    scene.sunDirection.w = 0.0f;
    scene.sunColor.r = 1.0f;
    scene.sunColor.g = 1.0f;
    scene.sunColor.b = 1.0f;
    scene.sunColor.a = 1.0f;

    RenderObject* renderObjects = 0;
    U32 renderObjectCount = 0;

    if (state->sceneLoaded && state->scene.nodeCount > 0) {
        U32 totalSurfaces = 0;
        for (U32 n = 0; n < state->scene.nodeCount; ++n) {
            SceneNode* node = &state->scene.nodes[n];
            if (node->meshIndex >= 0 && (U32)node->meshIndex < state->gpuScene.meshCount) {
                MeshAssetData* meshData = &state->scene.meshes[node->meshIndex];
                totalSurfaces += (meshData->surfaceCount > 0) ? meshData->surfaceCount : 1;
            }
        }
        
        if (totalSurfaces > 0) {
            renderObjects = ARENA_PUSH_ARRAY(host->frameArena, RenderObject, totalSurfaces);
            if (renderObjects) {
                F32 scale = state->meshScale;
                Mat4x4F32 scaleMatrix = mat4_identity();
                scaleMatrix.v[0][0] = scale;
                scaleMatrix.v[1][1] = scale;
                scaleMatrix.v[2][2] = scale;
                
                for (U32 n = 0; n < state->scene.nodeCount; ++n) {
                    SceneNode* node = &state->scene.nodes[n];
                    if (node->meshIndex < 0 || (U32)node->meshIndex >= state->gpuScene.meshCount) {
                        continue;
                    }
                    
                    MeshAssetData* meshData = &state->scene.meshes[node->meshIndex];
                    Mat4x4F32 worldTransform = scaleMatrix * node->worldTransform;
                    MeshHandle meshHandle = state->gpuScene.meshes[node->meshIndex];
                    
                    if (meshData->surfaceCount == 0) {
                        RenderObject* obj = &renderObjects[renderObjectCount++];
                        obj->mesh = meshHandle;
                        obj->transform = worldTransform;
                        obj->color = state->meshColor;
                        obj->material = MATERIAL_HANDLE_INVALID;
                        obj->firstIndex = 0;
                        obj->indexCount = 0;
                    } else {
                        for (U32 s = 0; s < meshData->surfaceCount; ++s) {
                            MeshSurface* surface = &meshData->surfaces[s];
                            RenderObject* obj = &renderObjects[renderObjectCount++];
                            obj->mesh = meshHandle;
                            obj->transform = worldTransform;
                            obj->color = state->meshColor;
                            obj->firstIndex = surface->startIndex;
                            obj->indexCount = surface->count;
                            obj->material = MATERIAL_HANDLE_INVALID;
                            if (surface->materialIndex < state->gpuScene.materialCount) {
                                obj->material = state->gpuScene.materials[surface->materialIndex];
                            }
                        }
                    }
                }
            }
        }
    }

    PLATFORM_RENDERER_CALL(platform, renderer_draw, host->renderer, state->windowHandle, &scene, renderObjects, renderObjectCount);
}

static void app_shutdown(AppPlatform* platform, AppMemory* memory, AppHostContext* host) {
    ASSERT_ALWAYS(platform != 0);
    ASSERT_ALWAYS(memory != 0);
    ASSERT_ALWAYS(memory->permanentStorage != 0);
    ASSERT_ALWAYS(host != 0);
    ASSERT_ALWAYS(host->renderer != 0);

    AppCoreState* state = app_get_state(memory);
    AppTestsState* tests = app_get_tests(state);

    if (state->sceneLoaded) {
        PLATFORM_RENDERER_CALL(platform, renderer_destroy_scene, host->renderer, &state->gpuScene);
        state->sceneLoaded = 0;
    }

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
