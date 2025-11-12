#include "renderer_vulkan.hpp"
#include "backends/imgui_impl_vulkan.h"

static void renderer_vulkan_imgui_set_context(RendererVulkan* vulkan) {
    if (vulkan && vulkan->imguiContext) {
        ImGui::SetCurrentContext(vulkan->imguiContext);
    }
}

static void renderer_vulkan_imgui_check_vk_result(VkResult result) {
    VK_CHECK(result);
}

static PFN_vkVoidFunction renderer_vulkan_imgui_load_function(const char* functionName, void* userData) {
    VkInstance instance = (VkInstance) userData;
    return vkGetInstanceProcAddr(instance, functionName);
}

static B32 renderer_vulkan_imgui_create_descriptor_pool(RendererVulkan* vulkan) {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = ARRAY_COUNT(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(vulkan->device, &poolInfo, 0, &vulkan->imguiDescriptorPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "Failed to create ImGui descriptor pool (vkCreateDescriptorPool = {})", result);
        vulkan->imguiDescriptorPool = VK_NULL_HANDLE;
        return 0;
    }

    return 1;
}

static ImGuiMouseButton renderer_vulkan_imgui_translate_mouse_button(enum OS_MouseButton button) {
    switch (button) {
        case OS_MouseButton_Left:
            return ImGuiMouseButton_Left;
        case OS_MouseButton_Right:
            return ImGuiMouseButton_Right;
        case OS_MouseButton_Middle:
            return ImGuiMouseButton_Middle;
        default:
            return ImGuiMouseButton_Left;
    }
}

static void renderer_vulkan_imgui_update_modifier_keys(ImGuiIO* io, U32 modifiers) {
    io->AddKeyEvent(ImGuiMod_Shift, FLAGS_HAS(modifiers, OS_KeyModifiers_Shift));
    io->AddKeyEvent(ImGuiMod_Ctrl, FLAGS_HAS(modifiers, OS_KeyModifiers_Control));
    io->AddKeyEvent(ImGuiMod_Alt, FLAGS_HAS(modifiers, OS_KeyModifiers_Alt));
    io->AddKeyEvent(ImGuiMod_Super, FLAGS_HAS(modifiers, OS_KeyModifiers_Super));
}

static B32 renderer_vulkan_imgui_event_matches_window(RendererVulkan* vulkan, const OS_GraphicsEvent* event) {
    if (!vulkan || !event) {
        return 0;
    }

    if (!vulkan->imguiWindow.handle) {
        return 1;
    }

    return (event->window.handle == vulkan->imguiWindow.handle) ? 1 : 0;
}

static ImGuiKey renderer_vulkan_imgui_translate_os_keycode_to_imgui_key_(enum OS_KeyCode keyCode) {
    switch (keyCode) {
        case OS_KeyCode_A: return ImGuiKey_A;
        case OS_KeyCode_B: return ImGuiKey_B;
        case OS_KeyCode_C: return ImGuiKey_C;
        case OS_KeyCode_D: return ImGuiKey_D;
        case OS_KeyCode_E: return ImGuiKey_E;
        case OS_KeyCode_F: return ImGuiKey_F;
        case OS_KeyCode_G: return ImGuiKey_G;
        case OS_KeyCode_H: return ImGuiKey_H;
        case OS_KeyCode_I: return ImGuiKey_I;
        case OS_KeyCode_J: return ImGuiKey_J;
        case OS_KeyCode_K: return ImGuiKey_K;
        case OS_KeyCode_L: return ImGuiKey_L;
        case OS_KeyCode_M: return ImGuiKey_M;
        case OS_KeyCode_N: return ImGuiKey_N;
        case OS_KeyCode_O: return ImGuiKey_O;
        case OS_KeyCode_P: return ImGuiKey_P;
        case OS_KeyCode_Q: return ImGuiKey_Q;
        case OS_KeyCode_R: return ImGuiKey_R;
        case OS_KeyCode_S: return ImGuiKey_S;
        case OS_KeyCode_T: return ImGuiKey_T;
        case OS_KeyCode_U: return ImGuiKey_U;
        case OS_KeyCode_V: return ImGuiKey_V;
        case OS_KeyCode_W: return ImGuiKey_W;
        case OS_KeyCode_X: return ImGuiKey_X;
        case OS_KeyCode_Y: return ImGuiKey_Y;
        case OS_KeyCode_Z: return ImGuiKey_Z;
        
        case OS_KeyCode_0: return ImGuiKey_0;
        case OS_KeyCode_1: return ImGuiKey_1;
        case OS_KeyCode_2: return ImGuiKey_2;
        case OS_KeyCode_3: return ImGuiKey_3;
        case OS_KeyCode_4: return ImGuiKey_4;
        case OS_KeyCode_5: return ImGuiKey_5;
        case OS_KeyCode_6: return ImGuiKey_6;
        case OS_KeyCode_7: return ImGuiKey_7;
        case OS_KeyCode_8: return ImGuiKey_8;
        case OS_KeyCode_9: return ImGuiKey_9;
        
        case OS_KeyCode_Minus: return ImGuiKey_Minus;
        case OS_KeyCode_Equal: return ImGuiKey_Equal;
        case OS_KeyCode_LeftBracket: return ImGuiKey_LeftBracket;
        case OS_KeyCode_RightBracket: return ImGuiKey_RightBracket;
        case OS_KeyCode_Semicolon: return ImGuiKey_Semicolon;
        case OS_KeyCode_Apostrophe: return ImGuiKey_Apostrophe;
        case OS_KeyCode_Comma: return ImGuiKey_Comma;
        case OS_KeyCode_Period: return ImGuiKey_Period;
        case OS_KeyCode_Slash: return ImGuiKey_Slash;
        case OS_KeyCode_Backslash: return ImGuiKey_Backslash;
        case OS_KeyCode_GraveAccent: return ImGuiKey_GraveAccent;
        
        case OS_KeyCode_F1: return ImGuiKey_F1;
        case OS_KeyCode_F2: return ImGuiKey_F2;
        case OS_KeyCode_F3: return ImGuiKey_F3;
        case OS_KeyCode_F4: return ImGuiKey_F4;
        case OS_KeyCode_F5: return ImGuiKey_F5;
        case OS_KeyCode_F6: return ImGuiKey_F6;
        case OS_KeyCode_F7: return ImGuiKey_F7;
        case OS_KeyCode_F8: return ImGuiKey_F8;
        case OS_KeyCode_F9: return ImGuiKey_F9;
        case OS_KeyCode_F10: return ImGuiKey_F10;
        case OS_KeyCode_F11: return ImGuiKey_F11;
        case OS_KeyCode_F12: return ImGuiKey_F12;
        
        case OS_KeyCode_Escape: return ImGuiKey_Escape;
        case OS_KeyCode_Tab: return ImGuiKey_Tab;
        case OS_KeyCode_CapsLock: return ImGuiKey_CapsLock;
        case OS_KeyCode_Space: return ImGuiKey_Space;
        case OS_KeyCode_Enter: return ImGuiKey_Enter;
        case OS_KeyCode_Backspace: return ImGuiKey_Backspace;
        case OS_KeyCode_Delete: return ImGuiKey_Delete;
        
        case OS_KeyCode_LeftArrow: return ImGuiKey_LeftArrow;
        case OS_KeyCode_RightArrow: return ImGuiKey_RightArrow;
        case OS_KeyCode_UpArrow: return ImGuiKey_UpArrow;
        case OS_KeyCode_DownArrow: return ImGuiKey_DownArrow;
        
        case OS_KeyCode_Home: return ImGuiKey_Home;
        case OS_KeyCode_End: return ImGuiKey_End;
        case OS_KeyCode_PageUp: return ImGuiKey_PageUp;
        case OS_KeyCode_PageDown: return ImGuiKey_PageDown;
        
        case OS_KeyCode_LeftShift: return ImGuiKey_LeftShift;
        case OS_KeyCode_RightShift: return ImGuiKey_RightShift;
        case OS_KeyCode_LeftControl: return ImGuiKey_LeftCtrl;
        case OS_KeyCode_RightControl: return ImGuiKey_RightCtrl;
        case OS_KeyCode_LeftAlt: return ImGuiKey_LeftAlt;
        case OS_KeyCode_RightAlt: return ImGuiKey_RightAlt;
        case OS_KeyCode_LeftSuper: return ImGuiKey_LeftSuper;
        case OS_KeyCode_RightSuper: return ImGuiKey_RightSuper;
        
        case OS_KeyCode_Shift: return ImGuiKey_LeftShift;
        case OS_KeyCode_Control: return ImGuiKey_LeftCtrl;
        case OS_KeyCode_Alt: return ImGuiKey_LeftAlt;
        case OS_KeyCode_Super: return ImGuiKey_LeftSuper;
        
        default:
            return ImGuiKey_None;
    }
}

static void renderer_vulkan_imgui_compute_display_info(RendererVulkan* vulkan,
                                                       F32* outFramebufferWidth,
                                                       F32* outFramebufferHeight,
                                                       F32* outLogicalWidth,
                                                       F32* outLogicalHeight,
                                                       F32* outScaleX,
                                                       F32* outScaleY) {
    F32 framebufferWidth = (F32) ((vulkan->swapchain.extent.width != 0u)
                                      ? vulkan->swapchain.extent.width
                                      : vulkan->drawExtent.width);
    F32 framebufferHeight = (F32) ((vulkan->swapchain.extent.height != 0u)
                                       ? vulkan->swapchain.extent.height
                                       : vulkan->drawExtent.height);

    if (framebufferWidth <= 0.0f) {
        framebufferWidth = 1.0f;
    }
    if (framebufferHeight <= 0.0f) {
        framebufferHeight = 1.0f;
    }

    F32 frameWidth = (vulkan->imguiWindowExtent.width > 0u)
                         ? (F32) vulkan->imguiWindowExtent.width
                         : framebufferWidth;
    if (frameWidth <= 0.0f) {
        frameWidth = framebufferWidth;
    }
    F32 scale = framebufferWidth / frameWidth;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    F32 logicalWidth = frameWidth;
    F32 logicalHeight = framebufferHeight / scale;

    F32 scaleX = scale;
    F32 scaleY = scale;

    if (outFramebufferWidth) {
        *outFramebufferWidth = framebufferWidth;
    }
    if (outFramebufferHeight) {
        *outFramebufferHeight = framebufferHeight;
    }
    if (outLogicalWidth) {
        *outLogicalWidth = logicalWidth;
    }
    if (outLogicalHeight) {
        *outLogicalHeight = logicalHeight;
    }
    if (outScaleX) {
        *outScaleX = scaleX;
    }
    if (outScaleY) {
        *outScaleY = scaleY;
    }
}

B32 renderer_vulkan_imgui_init(RendererVulkan* vulkan, OS_WindowHandle window) {
    if (!vulkan || vulkan->device == VK_NULL_HANDLE || vulkan->graphicsQueue == VK_NULL_HANDLE) {
        return 0;
    }

    if (vulkan->imguiInitialized) {
        vulkan->imguiWindow = window;
        return 1;
    }

    if (!renderer_vulkan_imgui_create_descriptor_pool(vulkan)) {
        return 0;
    }

    vulkan->imguiContext = ImGui::CreateContext();
    renderer_vulkan_imgui_set_context(vulkan);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    vulkan->imguiColorAttachmentFormats[0] = VK_FORMAT_B8G8R8A8_SRGB;
    vulkan->imguiPipelineInfo = {};
    vulkan->imguiPipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    vulkan->imguiPipelineInfo.colorAttachmentCount = 1;
    vulkan->imguiPipelineInfo.pColorAttachmentFormats = vulkan->imguiColorAttachmentFormats;
    vulkan->imguiPipelineInfo.pNext = VK_NULL_HANDLE;
    vulkan->imguiPipelineInfo.viewMask = 0;
    vulkan->imguiPipelineInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    vulkan->imguiPipelineInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4, renderer_vulkan_imgui_load_function, vulkan->instance);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_4;
    initInfo.Instance = vulkan->instance;
    initInfo.PhysicalDevice = vulkan->physicalDevice;
    initInfo.Device = vulkan->device;
    initInfo.QueueFamily = vulkan->graphicsQueueFamilyIndex;
    initInfo.Queue = vulkan->graphicsQueue;
    initInfo.DescriptorPool = vulkan->imguiDescriptorPool;
    initInfo.DescriptorPoolSize = 0;
    initInfo.MinImageCount = (vulkan->swapchain.imageCount > 0u) ? vulkan->swapchain.imageCount : VULKAN_FRAME_OVERLAP;
    initInfo.ImageCount = initInfo.MinImageCount;
    initInfo.CheckVkResultFn = renderer_vulkan_imgui_check_vk_result;
    initInfo.Allocator = 0;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.UseDynamicRendering = VK_TRUE;
    initInfo.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = vulkan->imguiPipelineInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        LOG_ERROR(VULKAN_LOG_DOMAIN, "ImGui_ImplVulkan_Init failed");
        vkDestroyDescriptorPool(vulkan->device, vulkan->imguiDescriptorPool, 0);
        vulkan->imguiDescriptorPool = VK_NULL_HANDLE;
        vulkan->imguiContext = 0;
        ImGui::DestroyContext();
        return 0;
    }

    vulkan->imguiWindow = window;
    vulkan->imguiMinImageCount = initInfo.MinImageCount;
    vulkan->imguiInitialized = 1;
    return 1;
}

void renderer_vulkan_imgui_shutdown(RendererVulkan* vulkan) {
    if (!vulkan || !vulkan->imguiInitialized) {
        return;
    }

    if (vulkan->device != VK_NULL_HANDLE) {
        VK_CHECK(vkDeviceWaitIdle(vulkan->device));
    }

    renderer_vulkan_imgui_set_context(vulkan);
    ImGui_ImplVulkan_Shutdown();

    if (vulkan->imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan->device, vulkan->imguiDescriptorPool, 0);
        vulkan->imguiDescriptorPool = VK_NULL_HANDLE;
    }

    if (vulkan->imguiContext) {
        ImGui::DestroyContext(vulkan->imguiContext);
        vulkan->imguiContext = 0;
    }

    vulkan->imguiWindow.handle = 0;
    vulkan->imguiInitialized = 0;
}

void renderer_vulkan_imgui_process_events(RendererVulkan* vulkan, const OS_GraphicsEvent* events, U32 eventCount) {
    if (!vulkan || !vulkan->imguiInitialized || !events || eventCount == 0u) {
        return;
    }

    renderer_vulkan_imgui_set_context(vulkan);
    ImGuiIO& io = ImGui::GetIO();

    for (U32 index = 0; index < eventCount; ++index) {
        const OS_GraphicsEvent* evt = &events[index];
        if (!renderer_vulkan_imgui_event_matches_window(vulkan, evt)) {
            continue;
        }

        switch (evt->type) {
            case OS_GraphicsEventType_WindowShown: {
                io.AddFocusEvent(true);
                vulkan->imguiWindow = evt->window;
            } break;

            case OS_GraphicsEventType_WindowClosed:
            case OS_GraphicsEventType_WindowDestroyed: {
                io.AddFocusEvent(false);
                vulkan->imguiWindow.handle = 0;
            } break;

            case OS_GraphicsEventType_MouseMove: {
                F32 framebufferWidth = 0.0f;
                F32 framebufferHeight = 0.0f;
                F32 logicalWidth = 0.0f;
                F32 logicalHeight = 0.0f;
                F32 scaleX = 1.0f;
                F32 scaleY = 1.0f;
                renderer_vulkan_imgui_compute_display_info(vulkan,
                                                           &framebufferWidth,
                                                           &framebufferHeight,
                                                           &logicalWidth,
                                                           &logicalHeight,
                                                           &scaleX,
                                                           &scaleY);

                if (!evt->mouse.isInWindow) {
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                } else {
                    F32 x = evt->mouse.x;
                    F32 y = logicalHeight - evt->mouse.y;
                    io.AddMousePosEvent(x, y);
                }
            } break;

            case OS_GraphicsEventType_MouseButtonDown:
            case OS_GraphicsEventType_MouseButtonUp: {
                ImGuiMouseButton button = renderer_vulkan_imgui_translate_mouse_button(evt->mouse.button);
                B32 isDown = (evt->type == OS_GraphicsEventType_MouseButtonDown) ? 1 : 0;

                if (evt->mouse.isInWindow) {
                    F32 framebufferWidth = 0.0f;
                    F32 framebufferHeight = 0.0f;
                    F32 logicalWidth = 0.0f;
                    F32 logicalHeight = 0.0f;
                    F32 scaleX = 1.0f;
                    F32 scaleY = 1.0f;
                    renderer_vulkan_imgui_compute_display_info(vulkan,
                                                               &framebufferWidth,
                                                               &framebufferHeight,
                                                               &logicalWidth,
                                                               &logicalHeight,
                                                               &scaleX,
                                                               &scaleY);
                    F32 x = evt->mouse.x;
                    F32 y = logicalHeight - evt->mouse.y;
                    io.AddMousePosEvent(x, y);
                }

                io.AddMouseButtonEvent(button, isDown != 0);
                renderer_vulkan_imgui_update_modifier_keys(&io, evt->mouse.modifiers);
            } break;

            case OS_GraphicsEventType_MouseScroll: {
                io.AddMouseWheelEvent(evt->mouse.deltaX, evt->mouse.deltaY);
                renderer_vulkan_imgui_update_modifier_keys(&io, evt->mouse.modifiers);
            } break;

            case OS_GraphicsEventType_KeyDown:
            case OS_GraphicsEventType_KeyUp: {
                B32 isDown = (evt->type == OS_GraphicsEventType_KeyDown) ? 1 : 0;
                ImGuiKey key = renderer_vulkan_imgui_translate_os_keycode_to_imgui_key_(evt->key.keyCode);
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, isDown != 0);
                    io.SetKeyEventNativeData(key, (int) evt->key.keyCode, 0);
                }
                renderer_vulkan_imgui_update_modifier_keys(&io, evt->key.modifiers);
            } break;

            case OS_GraphicsEventType_TextInput: {
                if (evt->text.codepoint != 0u) {
                    io.AddInputCharacter(evt->text.codepoint);
                }
            } break;

            default:
                break;
        }
    }
}

void renderer_vulkan_imgui_begin_frame(RendererVulkan* vulkan, F32 deltaSeconds) {
    if (!vulkan || !vulkan->imguiInitialized) {
        return;
    }

    renderer_vulkan_imgui_set_context(vulkan);

    ImGuiIO& io = ImGui::GetIO();
    if (deltaSeconds > 0.0f) {
        io.DeltaTime = deltaSeconds;
    } else {
        io.DeltaTime = 1.0f / 60.0f;
    }

    F32 framebufferWidth = 0.0f;
    F32 framebufferHeight = 0.0f;
    F32 logicalWidth = 0.0f;
    F32 logicalHeight = 0.0f;
    F32 scaleX = 1.0f;
    F32 scaleY = 1.0f;
    renderer_vulkan_imgui_compute_display_info(vulkan,
                                               &framebufferWidth,
                                               &framebufferHeight,
                                               &logicalWidth,
                                               &logicalHeight,
                                               &scaleX,
                                               &scaleY);

    io.DisplaySize = ImVec2(logicalWidth, logicalHeight);
    io.DisplayFramebufferScale = ImVec2(scaleX, scaleY);

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void renderer_vulkan_imgui_end_frame(RendererVulkan* vulkan) {
    if (!vulkan || !vulkan->imguiInitialized) {
        return;
    }

    renderer_vulkan_imgui_set_context(vulkan);
    ImGui::Render();
}

void renderer_vulkan_imgui_on_swapchain_updated(RendererVulkan* vulkan) {
    if (!vulkan || !vulkan->imguiInitialized) {
        return;
    }

    renderer_vulkan_imgui_set_context(vulkan);

    if (vulkan->swapchain.format.format != VK_FORMAT_UNDEFINED) {
        vulkan->imguiColorAttachmentFormats[0] = vulkan->swapchain.format.format;
    }

    vulkan->imguiPipelineInfo = {};
    vulkan->imguiPipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    vulkan->imguiPipelineInfo.colorAttachmentCount = 1;
    vulkan->imguiPipelineInfo.pColorAttachmentFormats = vulkan->imguiColorAttachmentFormats;
    vulkan->imguiPipelineInfo.pNext = VK_NULL_HANDLE;
    vulkan->imguiPipelineInfo.viewMask = 0;
    vulkan->imguiPipelineInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    vulkan->imguiPipelineInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    ImGui_ImplVulkan_PipelineInfo pipelineInfo = {};
    pipelineInfo.RenderPass = VK_NULL_HANDLE;
    pipelineInfo.Subpass = 0;
    pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.PipelineRenderingCreateInfo = vulkan->imguiPipelineInfo;
    ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);

    if (vulkan->swapchain.imageCount > 0u) {
        vulkan->imguiMinImageCount = vulkan->swapchain.imageCount;
        ImGui_ImplVulkan_SetMinImageCount(vulkan->imguiMinImageCount);
    }
}

void renderer_vulkan_imgui_render(RendererVulkan* vulkan, VkCommandBuffer cmd, VkImageView targetImageView,
                                  VkExtent2D extent) {
    if (!vulkan || !vulkan->imguiInitialized || cmd == VK_NULL_HANDLE || targetImageView == VK_NULL_HANDLE) {
        return;
    }

    renderer_vulkan_imgui_set_context(vulkan);

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->TotalVtxCount == 0) {
        return;
    }

    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = targetImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    vkCmdEndRendering(cmd);
}

void renderer_vulkan_imgui_set_window_size(RendererVulkan* vulkan, U32 width, U32 height) {
    if (!vulkan) {
        return;
    }

    vulkan->imguiWindowExtent.width = width;
    vulkan->imguiWindowExtent.height = height;
}

