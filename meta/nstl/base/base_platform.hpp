//
// Created by André Leite on 31/05/2025.
//

#pragma once

// -----------------------------------------------------------------------------
// OS Detection
// -----------------------------------------------------------------------------

#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || \
    defined(__TOS_WIN__) || defined(__WINDOWS__)
#define PLATFORM_OS_WINDOWS
#elif defined(linux) || defined(__linux) || defined(__linux__) || \
    defined(__gnu_linux__)
#define PLATFORM_OS_LINUX
#elif defined(macintosh) || defined(Macintosh) || \
    (defined(__APPLE__) && defined(__MACH__))
#define PLATFORM_OS_MACOS
#else
#error "Unsupported operating system"
#endif

// -----------------------------------------------------------------------------
// Architecture Detection
// -----------------------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)
#define PLATFORM_ARCH_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define PLATFORM_ARCH_ARM64
#else
#error "Unsupported architecture"
#endif

// -----------------------------------------------------------------------------
// Compiler Detection
// -----------------------------------------------------------------------------

#if defined(_MSC_VER)
#define COMPILER_MSVC
#elif defined(__clang__)
#define COMPILER_CLANG
#elif defined(__GNUC__)
#define COMPILER_GCC
#else
#warning "Unknown compiler"
#endif

// -----------------------------------------------------------------------------
// Build Configuration
// -----------------------------------------------------------------------------

#if defined(DEBUG)
#define BUILD_DEBUG
#else
#define BUILD_RELEASE
#endif



