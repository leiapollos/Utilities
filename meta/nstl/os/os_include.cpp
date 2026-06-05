//
// Created by André Leite on 26/07/2025.
//

#if defined(PLATFORM_OS_WINDOWS)
#include "core/windows/os_core_windows.cpp"
#elif defined(PLATFORM_OS_MACOS)
#include "core/macos/os_core_macos.cpp"
#elif defined(PLATFORM_OS_LINUX)
#error "not supported"
#endif
