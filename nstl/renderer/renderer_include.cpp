//
// Created by André Leite on 03/11/2025.
//

#include "renderer.cpp"

#include "../../third_party/dear_imgui/imgui_unity.cpp"

#if defined(RENDERER_BACKEND_VULKAN)
#include "vulkan/renderer_vulkan.cpp"
#include "vulkan/renderer_vulkan_imgui.cpp"
#elif defined(RENDERER_BACKEND_METAL)
#error "not supported"
#elif defined(RENDERER_BACKEND_OPENGL)
#error "not supported"
#else
#error "No renderer backend defined"
#endif
