#include "dg/core/Assert.h"
#include "dg/core/Log.h"
#include "dg/core/Platform.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// ThreadSanitizer does not support fork() from a multithreaded process, so the fork tests below skip under TSan.
// They run everywhere else, including ASan/UBSan builds.
#if defined(__SANITIZE_THREAD__)
    #define DG_TEST_UNDER_TSAN 1
#elif defined(__has_feature)
    #if __has_feature(thread_sanitizer)
        #define DG_TEST_UNDER_TSAN 1
    #endif
#endif
#ifndef DG_TEST_UNDER_TSAN
    #define DG_TEST_UNDER_TSAN 0
#endif

namespace {

namespace fs = std::filesystem;

// Counts how often it is formatted, to prove that disabled levels skip std::format entirely.
std::atomic<int> gFormatCount{0};
struct Counted {};

} // namespace

template <>
struct std::formatter<Counted> : std::formatter<std::string_view> {
    auto format(const Counted&, std::format_context& context) const {
        gFormatCount.fetch_add(1, std::memory_order_relaxed);
        return std::formatter<std::string_view>::format("counted", context);
    }
};

namespace {

// Each CTest entry runs in its own process, so a per-test file name is enough to keep tests apart.
fs::path tempLogPath(std::string_view name) {
    const fs::path directory = fs::temp_directory_path() / "dg-log-tests";
    fs::create_directories(directory);
    fs::path path = directory / std::format("{}-{}.log", name, DG_BUILD_CONFIG);
    fs::remove(path);
    return path;
}

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    return lines;
}

// Parses "t<thread> n<index>" at the end of a log line without the CRT scanf family (deprecated on Windows).
bool parseThreadLine(std::string_view line, int& t, int& n) {
    const size_t at = line.rfind(" t");
    if (at == std::string_view::npos) {
        return false;
    }
    const char* first = line.data() + at + 2;
    const char* last = line.data() + line.size();
    auto [afterT, errorT] = std::from_chars(first, last, t);
    if (errorT != std::errc{} || last - afterT < 3 || afterT[0] != ' ' || afterT[1] != 'n') {
        return false;
    }
    auto [afterN, errorN] = std::from_chars(afterT + 2, last, n);
    return errorN == std::errc{} && afterN == last;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

dg::log::Config fileOnly(const fs::path& path) {
    return {.level = dg::log::Level::Trace, .console = false, .filePath = path.string()};
}

// Leaves the global logger in its default state whatever a test does.
struct LoggerReset {
    ~LoggerReset() {
        dg::log::shutdown();
        dg::log::setLevel(dg::log::Level::Info);
    }
};

} // namespace

TEST(AsyncLog, WritesEveryMessageInOrder) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("in-order");
    ASSERT_TRUE(dg::log::init(fileOnly(path)));
    for (int i = 0; i < 2000; ++i) {
        dg::log::info("message {}", i);
    }
    dg::log::shutdown();

    const std::vector<std::string> lines = readLines(path);
    ASSERT_EQ(lines.size(), 2000u);
    for (int i = 0; i < 2000; ++i) {
        EXPECT_TRUE(lines[static_cast<size_t>(i)].ends_with(std::format("message {}", i))) << lines[static_cast<size_t>(i)];
    }
}

TEST(AsyncLog, FlushBlocksUntilEarlierMessagesAreWritten) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("flush");
    ASSERT_TRUE(dg::log::init(fileOnly(path)));
    for (int i = 0; i < 500; ++i) {
        dg::log::debug("queued {}", i);
    }
    dg::log::flush(); // the logger keeps running; only the flush guarantees the file is complete

    const std::vector<std::string> lines = readLines(path);
    ASSERT_EQ(lines.size(), 500u);
    EXPECT_TRUE(lines.back().ends_with("queued 499")) << lines.back();
}

TEST(AsyncLog, ErrorsAreWrittenBeforeTheCallReturns) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("error-sync");
    ASSERT_TRUE(dg::log::init(fileOnly(path)));
    dg::log::info("before");
    dg::log::error("boom {}", 42); // no flush() or shutdown(): Error must flush by itself

    const std::vector<std::string> lines = readLines(path);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_TRUE(lines[1].ends_with("boom 42")) << lines[1];
}

TEST(AsyncLog, LevelFilterSkipsFormatting) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("filter");
    ASSERT_TRUE(dg::log::init({.level = dg::log::Level::Warn, .console = false, .filePath = path.string()}));
    EXPECT_FALSE(dg::log::enabled(dg::log::Level::Info));
    EXPECT_TRUE(dg::log::enabled(dg::log::Level::Warn));
    EXPECT_FALSE(dg::log::enabled(dg::log::Level::Off));

    gFormatCount = 0;
    dg::log::info("{}", Counted{});
    dg::log::debug("{}", Counted{});
    EXPECT_EQ(gFormatCount.load(), 0);
    dg::log::warn("{}", Counted{});
    EXPECT_EQ(gFormatCount.load(), 1);
    dg::log::error("kept");
    dg::log::shutdown();

    const std::vector<std::string> lines = readLines(path);
    ASSERT_EQ(lines.size(), 2u);
    // File lines are "[date time] [level] [thread id] message".
    EXPECT_NE(lines[0].find("[warn] "), std::string::npos) << lines[0];
    EXPECT_TRUE(lines[0].ends_with(" counted")) << lines[0];
    EXPECT_NE(lines[1].find("[error] "), std::string::npos) << lines[1];
    EXPECT_TRUE(lines[1].ends_with(" kept")) << lines[1];
}

TEST(AsyncLog, UsesEngineLevelNames) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("names");
    ASSERT_TRUE(dg::log::init(fileOnly(path)));
    dg::log::trace("t");
    dg::log::debug("d");
    dg::log::info("i");
    dg::log::warn("w");
    dg::log::error("e");
    dg::log::fatal("f");
    dg::log::shutdown();

    const std::vector<std::string> lines = readLines(path);
    ASSERT_EQ(lines.size(), 6u);
    const char* expected[] = {"[trace] [", "[debug] [", "[info] [", "[warn] [", "[error] [", "[fatal] ["};
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_NE(lines[i].find(expected[i]), std::string::npos) << lines[i];
    }
}

TEST(AsyncLog, ManyThreadsLoseNothingWhenTheQueueBlocks) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("threads");
    // A tiny queue forces producers to wait for the worker, which is exactly when a blocking policy matters.
    ASSERT_TRUE(
        dg::log::init({.level = dg::log::Level::Info, .console = false, .filePath = path.string(), .queueSize = 64}));
    constexpr int kThreads = 8;
    constexpr int kPerThread = 2000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int n = 0; n < kPerThread; ++n) {
                dg::log::info("t{} n{}", t, n);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    dg::log::shutdown();

    const std::vector<std::string> lines = readLines(path);
    ASSERT_EQ(lines.size(), static_cast<size_t>(kThreads * kPerThread));
    std::vector<int> next(kThreads, 0); // each thread's own messages must stay in order
    for (const std::string& line : lines) {
        int t = -1;
        int n = -1;
        ASSERT_TRUE(parseThreadLine(line, t, n)) << line;
        ASSERT_GE(t, 0);
        ASSERT_LT(t, kThreads);
        EXPECT_EQ(n, next[static_cast<size_t>(t)]) << line;
        next[static_cast<size_t>(t)] = n + 1;
    }
}

// Regression: the old overrun_oldest policy could overwrite a queued flush request, after which every flush(), error()
// and assertion waited for the full flush timeout (5 s), forever. Flushes and errors must stay fast during and after a
// flood that fills the queue, and Trace..Warn messages must really be dropped rather than block the producers.
TEST(AsyncLog, NonBlockingQueueDropsMessagesButNeverFlushesOrErrors) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("drop-when-full");
    ASSERT_TRUE(dg::log::init({.level = dg::log::Level::Info,
                               .console = false,
                               .filePath = path.string(),
                               .queueSize = 4,
                               .blockWhenFull = false}));
    constexpr int kThreads = 4;
    constexpr int kPerThread = 20000;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &go] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            for (int n = 0; n < kPerThread; ++n) {
                dg::log::info("t{} n{}", t, n);
            }
        });
    }
    go = true;
    for (int i = 0; i < 6; ++i) {
        const auto start = std::chrono::steady_clock::now();
        dg::log::flush();
        EXPECT_LT(millisecondsSince(start), 2000.0) << "flush " << i << " during the flood";
        dg::log::error("error during flood {}", i);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (int i = 0; i < 3; ++i) {
        const auto start = std::chrono::steady_clock::now();
        dg::log::error("idle error {}", i);
        EXPECT_LT(millisecondsSince(start), 1000.0) << "idle error " << i;
    }

    // error() returned, so every error line is already in the file; no shutdown needed.
    const std::vector<std::string> lines = readLines(path);
    EXPECT_LT(lines.size(), static_cast<size_t>(kThreads * kPerThread)) << "the queue never dropped anything";
    for (int i = 0; i < 6; ++i) {
        const std::string expected = std::format("error during flood {}", i);
        EXPECT_TRUE(std::ranges::any_of(lines, [&](const std::string& line) { return line.ends_with(expected); }))
            << expected;
    }
    ASSERT_GE(lines.size(), 3u);
    EXPECT_TRUE(lines.back().ends_with("idle error 2")) << lines.back();
}

TEST(AsyncLog, RestartsWithANewConfiguration) {
    const LoggerReset reset;
    const fs::path first = tempLogPath("restart-first");
    const fs::path second = tempLogPath("restart-second");
    ASSERT_TRUE(dg::log::init(fileOnly(first)));
    dg::log::info("one");
    ASSERT_TRUE(dg::log::init(fileOnly(second))); // init on a running logger drains the old one first
    dg::log::info("two");
    dg::log::shutdown();

    const std::vector<std::string> a = readLines(first);
    const std::vector<std::string> b = readLines(second);
    ASSERT_EQ(a.size(), 1u);
    ASSERT_EQ(b.size(), 1u);
    EXPECT_TRUE(a[0].ends_with("one"));
    EXPECT_TRUE(b[0].ends_with("two"));
}

TEST(AsyncLog, LoggingAfterShutdownStillWorks) {
    const LoggerReset reset;
    const fs::path path = tempLogPath("after-shutdown");
    ASSERT_TRUE(dg::log::init(fileOnly(path)));
    dg::log::shutdown();
    testing::internal::CaptureStdout();
    dg::log::info("falls back to the console"); // must neither crash nor restart the worker
    // No flush(): the fallback is synchronous, which also proves no new worker took the message.
    const std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("[info] falls back to the console\n"), std::string::npos) << out;
    EXPECT_TRUE(readLines(path).empty());
}

// Tools read the two streams separately (dg determinism parses stdout), so the split is part of the contract.
TEST(AsyncLog, ConsoleSplitsStdoutAndStderrByLevel) {
    const LoggerReset reset;
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    ASSERT_TRUE(dg::log::init({.level = dg::log::Level::Trace}));
    dg::log::trace("to-out-trace");
    dg::log::info("to-out-info");
    dg::log::warn("to-err-warn");
    dg::log::error("to-err-error");
    dg::log::flush();
    const std::string out = testing::internal::GetCapturedStdout();
    const std::string err = testing::internal::GetCapturedStderr();
    EXPECT_NE(out.find("[trace] to-out-trace\n"), std::string::npos) << out;
    EXPECT_NE(out.find("[info] to-out-info\n"), std::string::npos) << out;
    EXPECT_EQ(out.find("to-err"), std::string::npos) << out;
    EXPECT_NE(err.find("[warn] to-err-warn\n"), std::string::npos) << err;
    EXPECT_NE(err.find("[error] to-err-error\n"), std::string::npos) << err;
    EXPECT_EQ(err.find("to-out"), std::string::npos) << err;
    EXPECT_EQ(out.find('\r'), std::string::npos); // console lines end in a plain \n (text mode adds \r on Windows)
}

TEST(AsyncLog, UnopenableLogFileIsReportedAndLoggingContinues) {
    const LoggerReset reset;
    const fs::path directory = fs::temp_directory_path() / "dg-log-tests";
    fs::create_directories(directory);
    testing::internal::CaptureStderr();
    // A directory cannot be opened as a log file on any platform.
    const auto started = dg::log::init({.filePath = directory.string()});
    dg::log::warn("still logging");
    dg::log::flush();
    const std::string err = testing::internal::GetCapturedStderr();
    ASSERT_FALSE(started.has_value());
    EXPECT_NE(started.error().find("cannot open log file"), std::string::npos) << started.error();
    // Reported through the logger itself, with the normal timestamped format.
    EXPECT_NE(err.find("] [error] cannot open log file"), std::string::npos) << err;
    EXPECT_NE(err.find("] [warn] still logging"), std::string::npos) << err;
}

TEST(LogLevels, ParseAndNameRoundTrip) {
    using dg::log::Level;
    for (Level level : {Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error, Level::Fatal, Level::Off}) {
        const std::optional<Level> parsed = dg::log::parseLevel(dg::log::levelName(level));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, level);
    }
    EXPECT_EQ(dg::log::parseLevel("warning"), Level::Warn);
    EXPECT_FALSE(dg::log::parseLevel("loud").has_value());
    EXPECT_FALSE(dg::log::parseLevel("INFO").has_value());
}

#if DG_ASSERTS_ENABLED
// The async queue must be drained before abort(): the assertion message is the most important line in the log.
TEST(AsyncLogDeath, AssertionMessageSurvivesAbort) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        [] {
            (void)dg::log::init({});
            for (int i = 0; i < 5000; ++i) {
                dg::log::info("filler {}", i);
            }
            DG_ASSERT_MSG(false, "last words before abort");
        }(),
        "Assertion failed: false.*last words before abort");
}

// Regression: assertFailed used the filtered write(), so --log-level off made assertion failures abort silently.
TEST(AsyncLogDeath, AssertionMessageSurvivesLevelOff) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        [] {
            (void)dg::log::init({.level = dg::log::Level::Off});
            DG_ASSERT_MSG(false, "heard even at level off");
        }(),
        "Assertion failed: false.*heard even at level off");
}
#endif

#if !DG_PLATFORM_WINDOWS
// Regression: a forked child inherited a logger whose worker thread does not exist there. error() then waited 5 s and
// lost its message, and exit() in the child ended in std::terminate when the atexit shutdown joined the missing
// thread. Later, on Linux only, the child hung forever: it unlocked the lifecycle rwlock the fork handler had locked,
// and glibc treats an unlock from a different thread id as a reader unlock. The "fast" death-test style forks without
// exec, exactly like that.
TEST(AsyncLogDeath, ForkedChildLogsSynchronouslyAndExitsCleanly) {
    if (DG_TEST_UNDER_TSAN) {
        GTEST_SKIP() << "ThreadSanitizer does not support fork() with live threads";
    }
    const LoggerReset reset;
    GTEST_FLAG_SET(death_test_style, "fast");
    ASSERT_TRUE(dg::log::init({.level = dg::log::Level::Info}));
    dg::log::info("parent is logging");
    dg::log::flush();
    EXPECT_EXIT(
        {
            const auto start = std::chrono::steady_clock::now();
            dg::log::error("child error line");
            if (millisecondsSince(start) < 1000.0) {
                std::fputs("child error was fast\n", stderr);
            }
            std::exit(3);
        },
        testing::ExitedWithCode(3), "child error line.*child error was fast");
    dg::log::info("parent still logging"); // the parent's worker is unaffected
    dg::log::flush();
}

    #if DG_ASSERTS_ENABLED
TEST(AsyncLogDeath, ForkedChildAssertionMessageSurvivesAbort) {
    if (DG_TEST_UNDER_TSAN) {
        GTEST_SKIP() << "ThreadSanitizer does not support fork() with live threads";
    }
    const LoggerReset reset;
    GTEST_FLAG_SET(death_test_style, "fast");
    ASSERT_TRUE(dg::log::init({}));
    dg::log::info("parent is logging");
    dg::log::flush();
    EXPECT_DEATH(DG_ASSERT_MSG(false, "child last words"), "Assertion failed: false.*child last words");
}
    #endif
#endif
