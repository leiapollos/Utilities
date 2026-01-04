//
// Created by André Leite on 03/11/2025.
//

#include "image_loader.cpp"
#include "mesh_loader_cgltf.cpp"

#include "renderer.cpp"

#if defined(RENDERER_BACKEND_VULKAN)
#include "vulkan/renderer_vulkan.cpp"
#elif defined(RENDERER_BACKEND_METAL)
#error "not supported"
#elif defined(RENDERER_BACKEND_OPENGL)
#error "not supported"
#else
#error "No renderer backend defined"
#endif
