//
// Created by André Leite on 26/07/2025.
//

#pragma once

enum OS_WINDOWS_GraphicsEntityType : U64 {
    OS_WINDOWS_GraphicsEntityType_Invalid = 0,
    OS_WINDOWS_GraphicsEntityType_Window = 1,
};

struct OS_WINDOWS_GraphicsEntity {
    OS_WINDOWS_GraphicsEntity* next;
    OS_WINDOWS_GraphicsEntity* activeNext;
    OS_WINDOWS_GraphicsEntityType type;

    union {
        struct {
            HWND window;
            HINSTANCE instance;
        } window;
    };
};

static OS_WINDOWS_GraphicsEntity* alloc_OS_graphics_entity();
static void free_OS_graphics_entity(OS_WINDOWS_GraphicsEntity* entity);

struct OS_WINDOWS_GraphicsState {
    HINSTANCE instance;
    B32 initialized;
    Arena* entityArena;
    Arena* eventArena;
    OS_WINDOWS_GraphicsEntity* freeEntities;
    OS_WINDOWS_GraphicsEntity* activeEntities;
    OS_GraphicsEventQueue eventQueue;
    B32 windowClassRegistered;
};

extern OS_WINDOWS_GraphicsState g_OS_WindowsGraphicsState;
