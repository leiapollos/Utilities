#include "gfx_common.cpp"

#if defined(PLATFORM_OS_MACOS)
#include "metal/gfx_metal.mm"
#elif defined(PLATFORM_OS_WINDOWS)
#include "vulkan/gfx_vulkan_include.hpp"
#include "vulkan/gfx_vulkan.cpp"
#else
#error "gfx backend not implemented for this platform"
#endif
