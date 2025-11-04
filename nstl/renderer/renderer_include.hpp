//
// Created by André Leite on 03/11/2025.
//

#pragma once

#include "renderer.hpp"

#if defined(RENDERER_BACKEND_VULKAN)
#define VMA_IMPLEMENTATION
#include "vulkan/renderer_vulkan.hpp"
#elif defined(RENDERER_BACKEND_METAL)
#error "not supported"
#elif defined(RENDERER_BACKEND_OPENGL)
#error "not supported"
#else
#error "No renderer backend defined"
#endif

