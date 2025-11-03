//
// Created by André Leite on 03/11/2025.
//

void renderer_draw_color(Renderer* renderer, OS_WindowHandle window, Vec3F32 color) {
    if (!renderer || !renderer->backendData) {
        return;
    }

#if defined(RENDERER_BACKEND_VULKAN)
    RendererVulkan* vulkan = (RendererVulkan*) renderer->backendData;
    renderer_vulkan_draw_color(vulkan, window, color);
#else
    (void) window;
    (void) color;
#endif
}

