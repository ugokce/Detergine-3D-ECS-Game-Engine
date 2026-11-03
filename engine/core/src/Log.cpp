#include "dg/core/Log.h"

#include <cstdio>
#include <mutex>

namespace dg::log {

namespace {

std::mutex& outputMutex() {
    static std::mutex mutex;
    return mutex;
}

constexpr std::string_view levelTag(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "warn";
        case Level::Error: return "error";
        case Level::Fatal: return "fatal";
    }
    return "?";
}

} // namespace

void write(Level level, std::string_view message) {
    std::FILE* stream = level >= Level::Warn ? stderr : stdout;
    const std::string line = std::format("[{}] {}\n", levelTag(level), message);

    const std::scoped_lock lock(outputMutex());
    std::fwrite(line.data(), 1, line.size(), stream);
    std::fflush(stream);
}

} // namespace dg::log
