//
// Created by André Leite on 26/07/2025.
//

#include "core/os_core.cpp"
#include "graphics/os_graphics.cpp"

#if defined(PLATFORM_OS_WINDOWS)
#include "core/windows/os_core_windows.cpp"
#include "graphics/windows/os_graphics_windows.cpp"
#include "audio/windows/os_audio_windows.cpp"
#elif defined(PLATFORM_OS_MACOS)
#include "core/macos/os_core_macos.cpp"
#include "graphics/macos/os_graphics_macos.mm"
#include "audio/macos/os_audio_macos.mm"
#elif defined(PLATFORM_OS_LINUX)
#error "not supported"
#endif
