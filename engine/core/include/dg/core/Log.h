#pragma once

#include "dg/core/Types.h"

#include <format>
#include <string_view>
#include <utility>

namespace dg::log {

enum class Level : u8 { Debug, Info, Warn, Error, Fatal };

// Thread-safe. Warn and above go to stderr, the rest to stdout. Replaced by spdlog sinks once vcpkg lands.
void write(Level level, std::string_view message);

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace dg::log
