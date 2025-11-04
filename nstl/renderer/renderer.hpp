//
// Created by André Leite on 03/11/2025.
//

#pragma once

// ////////////////////////
// Renderer System

struct Renderer {
    void* backendData;
};

B32 renderer_init(Arena* arena, Renderer* renderer);
void renderer_shutdown(Renderer* renderer);
void renderer_draw_color(Renderer* renderer, OS_WindowHandle window, Vec3F32 color);
