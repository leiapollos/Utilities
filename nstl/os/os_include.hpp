//
// Created by André Leite on 09/06/2025.
//

#pragma once

#include "core/os_core.hpp"
#include "graphics/os_graphics.hpp"

#if defined(PLATFORM_OS_WINDOWS)
#error "not supported"
#elif defined(PLATFORM_OS_MACOS)
#include "core/macos/os_core_macos.hpp"
#include "graphics/macos/os_graphics_macos.hpp"
#elif defined(PLATFORM_OS_LINUX)
#error "not supported"
#endif
