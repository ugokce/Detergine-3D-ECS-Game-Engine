#pragma once

// Compile-time platform, CPU architecture and compiler detection. Every macro is always defined (0 or 1), so use
// `#if DG_PLATFORM_MACOS`, never `#ifdef`.

#if defined(_WIN32)
    #define DG_PLATFORM_WINDOWS 1
    #define DG_PLATFORM_NAME "windows"
#elif defined(__ANDROID__)
    #define DG_PLATFORM_ANDROID 1
    #define DG_PLATFORM_NAME "android"
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define DG_PLATFORM_IOS 1
        #define DG_PLATFORM_NAME "ios"
    #else
        #define DG_PLATFORM_MACOS 1
        #define DG_PLATFORM_NAME "macos"
    #endif
#elif defined(__linux__)
    #define DG_PLATFORM_LINUX 1
    #define DG_PLATFORM_NAME "linux"
#else
    #error "Unsupported platform"
#endif

#ifndef DG_PLATFORM_WINDOWS
    #define DG_PLATFORM_WINDOWS 0
#endif
#ifndef DG_PLATFORM_ANDROID
    #define DG_PLATFORM_ANDROID 0
#endif
#ifndef DG_PLATFORM_IOS
    #define DG_PLATFORM_IOS 0
#endif
#ifndef DG_PLATFORM_MACOS
    #define DG_PLATFORM_MACOS 0
#endif
#ifndef DG_PLATFORM_LINUX
    #define DG_PLATFORM_LINUX 0
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define DG_ARCH_X64 1
    #define DG_ARCH_ARM64 0
    #define DG_ARCH_NAME "x64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define DG_ARCH_X64 0
    #define DG_ARCH_ARM64 1
    #define DG_ARCH_NAME "arm64"
#else
    #error "Unsupported CPU architecture (x64 and ARM64 only)"
#endif

#if defined(__clang__)
    #define DG_COMPILER_CLANG 1
    #define DG_COMPILER_MSVC 0
    #define DG_COMPILER_GCC 0
    #if defined(__apple_build_version__)
        #define DG_COMPILER_NAME "AppleClang"
    #elif defined(_MSC_VER)
        #define DG_COMPILER_NAME "clang-cl"
    #else
        #define DG_COMPILER_NAME "Clang"
    #endif
#elif defined(_MSC_VER)
    #define DG_COMPILER_CLANG 0
    #define DG_COMPILER_MSVC 1
    #define DG_COMPILER_GCC 0
    #define DG_COMPILER_NAME "MSVC"
#elif defined(__GNUC__)
    #define DG_COMPILER_CLANG 0
    #define DG_COMPILER_MSVC 0
    #define DG_COMPILER_GCC 1
    #define DG_COMPILER_NAME "GCC"
#else
    #error "Unsupported compiler"
#endif
