#pragma once

#include "dg/core/Types.h"

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Engine logging: an asynchronous spdlog logger behind a small std::format API (docs/manual/systems/core-foundation.md).
//
// Callers format on their own thread and hand the finished line to a queue; one background worker writes it to the
// sinks (console, optional file, logcat on Android). Messages from one thread keep their order. Error and Fatal
// messages are flushed before the call returns, so the last words before a crash or abort() always reach the sinks.
// That makes error() and fatal() blocking calls that wait for the whole queue to drain: keep them out of per-frame
// hot paths. Trace..Warn only enqueue.
// spdlog itself is an implementation detail of Log.cpp and never appears in engine headers.
//
// Logging is presentation-side: sim code may log, but nothing it logs may influence the simulation.

namespace dg::log {

enum class Level : u8 { Trace, Debug, Info, Warn, Error, Fatal, Off };

struct Config {
    Level level = Level::Info;   // messages below this level are discarded before they are formatted
    bool console = true;         // Trace..Info to stdout, Warn..Fatal to stderr
    std::string filePath{};      // optional log file, truncated at init; empty = no file
    usize queueSize = 8192;      // capacity of the async queue, in messages
    // true: a full queue makes callers wait (nothing is lost). false: Trace..Warn messages that find the queue full
    // are dropped, so callers never wait for them; Error/Fatal messages and flush() are never dropped.
    bool blockWhenFull = true;
};

// Starts (or restarts) asynchronous logging. Call it early in main, before other threads log. Logging also starts
// on first use with a default Config, so code that runs before main still logs. Returns an error if a requested sink
// (the log file) cannot be opened; logging still runs without that sink and the error has already been logged.
std::expected<void, std::string> init(const Config& config = {});

// Drains the queue, joins the worker thread and flushes every sink. Runs automatically at exit. Messages logged after
// shutdown are written synchronously to the console so nothing is lost during teardown.
void shutdown();

// Blocks until every message logged before the call has been written and the sinks are flushed.
void flush();

void setLevel(Level level);
[[nodiscard]] Level level();
[[nodiscard]] bool enabled(Level level);

// Accepts trace, debug, info, warn (or warning), error, fatal, off. Case-sensitive.
[[nodiscard]] std::optional<Level> parseLevel(std::string_view name);
[[nodiscard]] std::string_view levelName(Level level);

// Thread-safe. Error and Fatal block until the message is written (see flush()). If the worker is stuck for longer
// than a few seconds, Error and Fatal messages are also written synchronously to stderr, so they are never lost.
void write(Level level, std::string_view message);

// write() without the level filter: the message is written even at Level::Off. Used for assertion failures, which
// must always reach the console before abort().
void writeUnfiltered(Level level, std::string_view message);

template <typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Trace)) {
        write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Debug)) {
        write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Info)) {
        write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Warn)) {
        write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Error)) {
        write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }
}

// Logs and flushes; does not terminate. Use DG_ASSERT / DG_VERIFY for programmer errors.
template <typename... Args>
void fatal(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Fatal)) {
        write(Level::Fatal, std::format(fmt, std::forward<Args>(args)...));
    }
}

} // namespace dg::log
