#include "dg/core/Log.h"

#include "dg/core/Platform.h"

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#if DG_PLATFORM_ANDROID
    #include <spdlog/sinks/android_sink.h>
#endif
#if !DG_PLATFORM_WINDOWS
    #include <pthread.h> // pthread_atfork, see installProcessHooks()
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace dg::log {

namespace {

constexpr std::string_view kLevelNames[] = {"trace", "debug", "info", "warn", "error", "fatal", "off"};

// How long flush() waits for the worker. It only expires if the worker is stuck (e.g. stdout is a pipe nobody reads);
// Error and Fatal messages are then also written synchronously, so they are never lost.
constexpr std::chrono::seconds kFlushTimeout{5};

spdlog::level::level_enum toSpdlog(Level level) {
    switch (level) {
        case Level::Trace: return spdlog::level::trace;
        case Level::Debug: return spdlog::level::debug;
        case Level::Info: return spdlog::level::info;
        case Level::Warn: return spdlog::level::warn;
        case Level::Error: return spdlog::level::err;
        case Level::Fatal: return spdlog::level::critical;
        case Level::Off: return spdlog::level::off;
    }
    return spdlog::level::off;
}

Level fromSpdlog(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace: return Level::Trace;
        case spdlog::level::debug: return Level::Debug;
        case spdlog::level::info: return Level::Info;
        case spdlog::level::warn: return Level::Warn;
        case spdlog::level::err: return Level::Error;
        case spdlog::level::critical: return Level::Fatal;
        default: return Level::Off;
    }
}

// "%*" prints the engine's level names (fatal rather than spdlog's "critical", warn rather than "warning").
class LevelFlag final : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override {
        const std::string_view name = levelName(fromSpdlog(msg.level));
        dest.append(name.data(), name.data() + name.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<LevelFlag>();
    }
};

// The console sink writes with fwrite to text-mode streams, which already turn "\n" into "\r\n" on Windows; it needs a
// plain "\n" eol or every line would end in "\r\r\n". The file sink opens its file in binary mode and keeps SPDLOG_EOL.
std::unique_ptr<spdlog::formatter> makeFormatter(const std::string& pattern, std::string eol = SPDLOG_EOL) {
    auto formatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::local, std::move(eol));
    formatter->add_flag<LevelFlag>('*').set_pattern(pattern);
    return formatter;
}

// Trace..Info go to stdout and Warn..Fatal to stderr, as tools and tests read the two streams separately. The worker
// flushes after every line so stdout and stderr stay interleaved in order.
class ConsoleSink final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t line;
        formatter_->format(msg, line);
        std::FILE* stream = msg.level >= spdlog::level::warn ? stderr : stdout;
        std::fwrite(line.data(), 1, line.size(), stream);
        std::fflush(stream);
    }

    void flush_() override {
        std::fflush(stdout);
        std::fflush(stderr);
    }
};

// Always the last sink. The worker flushes sinks in order, so when this one is flushed every earlier sink has written
// and flushed everything queued before the flush request. Each request gets a ticket in queue order; flush() waits
// until the fence has completed that many flushes. Counting is exact because flush requests are never dropped: they
// are posted with the block policy, and no producer uses overrun_oldest (which would overwrite queued entries of any
// kind, flush requests included). Automatic flushing is off so only flush() produces requests.
class FlushFence final : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
public:
    bool waitFor(u64 ticket) {
        std::unique_lock lock(mutex);
        return condition.wait_for(lock, kFlushTimeout, [&] { return completed >= ticket; });
    }

protected:
    void sink_it_(const spdlog::details::log_msg&) override {}

    void flush_() override {
        {
            const std::scoped_lock lock(mutex);
            ++completed;
        }
        condition.notify_all();
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
    u64 completed = 0;
};

struct State {
    // Shared by write() and flush(); exclusive for init() and shutdown(), which replace the logger.
    std::shared_mutex lifecycle;
    std::shared_ptr<spdlog::details::thread_pool> pool;
    // Both loggers share the pool and the sinks, so their messages reach the sinks in queue order. `logger` uses the
    // configured overflow policy (block or discard_new); `urgent` always blocks and carries Error/Fatal messages and
    // flush requests, which therefore can never be dropped.
    std::shared_ptr<spdlog::async_logger> logger;
    std::shared_ptr<spdlog::async_logger> urgent;
    std::vector<spdlog::sink_ptr> sinks;
    std::shared_ptr<FlushFence> fence;

    std::mutex ticketMutex; // assigns flush tickets in the same order the requests enter the queue
    u64 nextTicket = 0;

    std::atomic<Level> level{Level::Info};
    std::atomic<bool> started{false}; // init() has run at least once (explicitly or lazily)
    std::atomic<bool> stopped{false}; // shutdown() ran; no lazy restart during process teardown

    std::mutex fallbackMutex;
    std::once_flag hooksOnce;

    // Loggers inherited by a forked child, whose worker thread does not exist there. Destroying them would join that
    // missing thread (std::terminate), so they are parked here forever.
    std::vector<std::shared_ptr<void>> abandoned;
};

// Intentionally never destroyed, so logging keeps working from other static destructors at exit.
State& state() {
    static State* const instance = new State();
    return *instance;
}

// Synchronous console output, used after shutdown, in a forked child, and when a flush times out (a write before
// the first init starts the logger lazily instead).
void writeFallback(Level level, std::string_view message) {
    State& s = state();
    const std::string line = std::format("[{}] {}\n", levelName(level), message);
    const std::scoped_lock lock(s.fallbackMutex);
    std::FILE* stream = level >= Level::Warn ? stderr : stdout;
    std::fwrite(line.data(), 1, line.size(), stream);
    std::fflush(stream);
}

// Requires the exclusive lifecycle lock.
void stopLocked(State& s) {
    if (!s.logger) {
        return;
    }
    // Queued messages keep the logger alive; destroying the last reference to the pool posts a terminate message
    // behind them and joins the worker after it has written everything.
    s.logger.reset();
    s.urgent.reset();
    s.pool.reset();
    for (const spdlog::sink_ptr& sink : s.sinks) {
        sink->flush();
    }
    s.sinks.clear();
    s.fence.reset();
}

// Requires the exclusive lifecycle lock. Returns a description of every requested sink that could not be opened; the
// logger still starts without it.
std::optional<std::string> startLocked(State& s, const Config& config) {
    stopLocked(s);
    std::optional<std::string> problem;

    std::vector<spdlog::sink_ptr> sinks;
    if (config.console) {
        auto console = std::make_shared<ConsoleSink>();
        console->set_formatter(makeFormatter("[%H:%M:%S.%e] [%*] %v", "\n"));
        sinks.push_back(std::move(console));
    }
    if (!config.filePath.empty()) {
        try {
            auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath, true);
            file->set_formatter(makeFormatter("[%Y-%m-%d %H:%M:%S.%e] [%*] [thread %t] %v"));
            sinks.push_back(std::move(file));
        } catch (const spdlog::spdlog_ex& failure) {
            problem = std::format("cannot open log file '{}': {}", config.filePath, failure.what());
        }
    }
#if DG_PLATFORM_ANDROID
    auto logcat = std::make_shared<spdlog::sinks::android_sink_mt>("Detergine");
    logcat->set_formatter(makeFormatter("%v")); // logcat records time, level and thread itself
    sinks.push_back(std::move(logcat));
#endif
    s.fence = std::make_shared<FlushFence>();
    sinks.push_back(s.fence);

    const usize queueSize = config.queueSize > 0 ? config.queueSize : 1;
    s.pool = std::make_shared<spdlog::details::thread_pool>(queueSize, 1);
    // discard_new rather than overrun_oldest: overrun_oldest overwrites whatever entry is oldest, including the
    // urgent logger's Error/Fatal messages and flush requests. discard_new only ever drops the message being posted.
    s.logger = std::make_shared<spdlog::async_logger>(
        "dg", sinks.begin(), sinks.end(), s.pool,
        config.blockWhenFull ? spdlog::async_overflow_policy::block : spdlog::async_overflow_policy::discard_new);
    s.urgent = std::make_shared<spdlog::async_logger>("dg-urgent", sinks.begin(), sinks.end(), s.pool,
                                                      spdlog::async_overflow_policy::block);
    for (spdlog::async_logger* logger : {s.logger.get(), s.urgent.get()}) {
        logger->set_level(spdlog::level::trace); // filtering happens in dg::log, before formatting
        logger->flush_on(spdlog::level::off);    // only flush() requests a flush (keeps fence tickets exact)
        logger->set_error_handler([](const std::string& failure) { writeFallback(Level::Error, failure); });
    }
    s.sinks = std::move(sinks);
    s.nextTicket = 0;
    return problem;
}

#if !DG_PLATFORM_WINDOWS
// fork() copies only the calling thread, so a child inherits a logger whose worker does not exist: queued messages
// would never be written and destroying the pool would join a missing thread. The child therefore parks the inherited
// logger and falls back to synchronous console output (init() may start a fresh logger in the child). Holding the
// lifecycle and fallback locks across fork() keeps them consistent in both processes.
void forkPrepare() {
    State& s = state();
    s.lifecycle.lock();
    s.fallbackMutex.lock();
}

void forkParent() {
    State& s = state();
    s.fallbackMutex.unlock();
    s.lifecycle.unlock();
}

void forkChild() {
    State& s = state();
    if (s.logger) {
        s.abandoned.push_back(std::move(s.logger));
        s.abandoned.push_back(std::move(s.urgent));
        s.abandoned.push_back(std::move(s.pool));
        s.abandoned.push_back(std::move(s.fence));
        for (spdlog::sink_ptr& sink : s.sinks) {
            s.abandoned.push_back(std::move(sink));
        }
        s.sinks.clear();
        s.stopped.store(true, std::memory_order_relaxed);
    }
    s.fallbackMutex.unlock();
    s.lifecycle.unlock();
}
#endif

void installProcessHooks(State& s) {
    std::call_once(s.hooksOnce, [] {
        std::atexit([] { shutdown(); });
#if !DG_PLATFORM_WINDOWS
        pthread_atfork(forkPrepare, forkParent, forkChild);
#endif
    });
}

// Posts a flush request and waits for it. Requires the shared lifecycle lock and a running logger. Returns false if
// the worker did not finish in time.
bool flushLocked(State& s) {
    u64 ticket = 0;
    {
        const std::scoped_lock ticketLock(s.ticketMutex);
        ticket = ++s.nextTicket;
        s.urgent->flush();
    }
    if (!s.fence->waitFor(ticket)) {
        writeFallback(Level::Warn, "log flush timed out; some messages may not have been written yet");
        return false;
    }
    return true;
}

void lazyInit() {
    State& s = state();
    const std::unique_lock lock(s.lifecycle);
    if (s.started.load(std::memory_order_acquire)) {
        return;
    }
    (void)startLocked(s, Config{.level = s.level.load(std::memory_order_relaxed)});
    s.started.store(true, std::memory_order_release);
    installProcessHooks(s);
}

} // namespace

std::expected<void, std::string> init(const Config& config) {
    State& s = state();
    // Registered before the first start, so a fork can never observe a running logger without the handlers.
    installProcessHooks(s);
    std::optional<std::string> problem;
    {
        const std::unique_lock lock(s.lifecycle);
        s.level.store(config.level, std::memory_order_relaxed);
        problem = startLocked(s, config);
        s.stopped.store(false, std::memory_order_relaxed);
        s.started.store(true, std::memory_order_release);
    }
    if (problem) {
        // Unfiltered: a broken log file must be reported even at --log-level fatal/off. Through the new logger, so it
        // gets the normal format and sinks.
        writeUnfiltered(Level::Error, *problem);
        return std::unexpected(std::move(*problem));
    }
    return {};
}

void shutdown() {
    State& s = state();
    const std::unique_lock lock(s.lifecycle);
    stopLocked(s);
    s.stopped.store(true, std::memory_order_relaxed);
}

void flush() {
    State& s = state();
    const std::shared_lock lock(s.lifecycle);
    if (!s.logger) {
        std::fflush(stdout);
        std::fflush(stderr);
        return;
    }
    (void)flushLocked(s);
}

void setLevel(Level newLevel) {
    state().level.store(newLevel, std::memory_order_relaxed);
}

Level level() {
    return state().level.load(std::memory_order_relaxed);
}

bool enabled(Level messageLevel) {
    return messageLevel != Level::Off && messageLevel >= state().level.load(std::memory_order_relaxed);
}

std::optional<Level> parseLevel(std::string_view name) {
    if (name == "warning") {
        return Level::Warn;
    }
    for (usize i = 0; i < std::size(kLevelNames); ++i) {
        if (kLevelNames[i] == name) {
            return static_cast<Level>(i);
        }
    }
    return std::nullopt;
}

std::string_view levelName(Level messageLevel) {
    const auto index = static_cast<usize>(messageLevel);
    return index < std::size(kLevelNames) ? kLevelNames[index] : "?";
}

void write(Level messageLevel, std::string_view message) {
    if (enabled(messageLevel)) {
        writeUnfiltered(messageLevel, message);
    }
}

void writeUnfiltered(Level messageLevel, std::string_view message) {
    if (messageLevel == Level::Off) {
        return;
    }
    State& s = state();
    if (!s.started.load(std::memory_order_acquire) && !s.stopped.load(std::memory_order_relaxed)) {
        lazyInit();
    }
    bool written = true;
    {
        const std::shared_lock lock(s.lifecycle);
        if (!s.logger) {
            writeFallback(messageLevel, message);
            return;
        }
        if (messageLevel < Level::Error) {
            s.logger->log(toSpdlog(messageLevel), message);
            return;
        }
        s.urgent->log(toSpdlog(messageLevel), message);
        written = flushLocked(s);
    }
    if (!written) {
        // The worker is stuck (e.g. blocked on a full stdout pipe). A duplicate line later beats a lost assertion.
        writeFallback(messageLevel, message);
    }
}

} // namespace dg::log
