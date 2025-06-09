//
// Created by André Leite on 09/06/2025.
//

#pragma once

#include "../platform.h"

#include "core/core.h"

#if defined(PLATFORM_OS_WINDOWS)
#error "not supported"
#elif defined(PLATFORM_OS_LINUX) || defined(PLATFORM_OS_MACOS)
#include "core/posix/core_posix.h"
#endif