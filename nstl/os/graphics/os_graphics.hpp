//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Graphics System

struct OS_WindowDesc {
    const char* title;
    U32         width;
    U32         height;
};

struct OS_WindowHandle {
    U64* handle;
};

static B32 OS_graphics_init();
static void OS_graphics_shutdown();

static OS_WindowHandle OS_window_create(OS_WindowDesc desc);
static void OS_window_destroy(OS_WindowHandle window);
static void* OS_window_get_native_handle(OS_WindowHandle window);
static B32 OS_window_is_open(OS_WindowHandle window);
static B32 OS_graphics_pump_events();

