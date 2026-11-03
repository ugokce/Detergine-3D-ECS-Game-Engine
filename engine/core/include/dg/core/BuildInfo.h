#pragma once

#include "dg/core/Platform.h"

namespace dg::build {

inline constexpr const char* kVersion = DG_VERSION_STRING;
inline constexpr const char* kConfig = DG_BUILD_CONFIG; // "Debug", "Development" or "Release"
inline constexpr const char* kPlatform = DG_PLATFORM_NAME;
inline constexpr const char* kArch = DG_ARCH_NAME;
inline constexpr const char* kCompiler = DG_COMPILER_NAME;
inline constexpr bool kHeadless = DG_HEADLESS != 0;

} // namespace dg::build
