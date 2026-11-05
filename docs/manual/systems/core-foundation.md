---
title: Core foundation: types, platform, logging, asserts, hashing
section: Systems
order: 10
status: implemented
summary: The dg::core basics every module builds on: fixed-width type aliases, platform and build macros, the asynchronous spdlog-based engine logger, assertions and the incremental state hash.
sources: engine/core/CMakeLists.txt, engine/core/include/dg/core/Types.h, engine/core/include/dg/core/Platform.h, engine/core/include/dg/core/BuildInfo.h, engine/core/include/dg/core/Log.h, engine/core/src/Log.cpp, engine/core/include/dg/core/Assert.h, engine/core/src/Assert.cpp, engine/core/include/dg/core/Hash.h, tests/core/LogTest.cpp
---

## Overview

`dg::core` is the lowest layer of the engine (Core → Platform/RHI → Engine services → Simulation | Presentation → …).
Every other module links it. Its only third-party dependency is [spdlog](https://github.com/gabime/spdlog), which is
private to `Log.cpp`: no engine header includes it, and other modules never see its headers or compile definitions.
This page covers the small, header-heavy pieces of the module:

| Header | Namespace | Purpose |
|---|---|---|
| [Types.h](engine/core/include/dg/core/Types.h) | `dg` | Fixed-width aliases (`u32`, `i64`, `f32`, `usize`, …) and the little-endian requirement |
| [Platform.h](engine/core/include/dg/core/Platform.h) | macros | Compile-time platform, CPU architecture and compiler detection (`DG_PLATFORM_*`, `DG_ARCH_*`, `DG_COMPILER_*`) |
| [BuildInfo.h](engine/core/include/dg/core/BuildInfo.h) | `dg::build` | Version, configuration, platform, architecture, compiler and headless flag as `constexpr` values |
| [Log.h](engine/core/include/dg/core/Log.h) | `dg::log` | The engine logger: a `std::format` API over an asynchronous spdlog logger (console, optional file, logcat on Android) |
| [Assert.h](engine/core/include/dg/core/Assert.h) | macros | `DG_ASSERT`, `DG_ASSERT_MSG`, `DG_VERIFY` for programmer errors |
| [Hash.h](engine/core/include/dg/core/Hash.h) | `dg` | `Fnv1a64`, the incremental 64-bit hash used for sim state hashes and golden-hash tests |

The allocators and generational handles in the same module have their own pages:
[Memory allocators](memory.md) and [Generational handles](handles.md).

Key guarantees:

* **Every platform macro is always defined**, to `0` or `1`. Test them with `#if`, never `#ifdef`.
* **Only x64 and ARM64, only little-endian.** Any other architecture or endianness fails to compile, so serialized
  data, snapshots and state hashes can assume one byte order.
* **Asserts are on in Debug and Development and compiled out in Release.** The expression inside `DG_VERIFY` is
  evaluated in every configuration.
* **Hashing is deterministic if you feed it the right data.** `Fnv1a64::updateValue` hashes the exact bit pattern of
  one scalar and rejects structs at compile time. The raw-byte `update` must never be given struct memory (padding
  would leak in), and pointers must never be hashed (determinism rule 4).
* **Logging never loses the last words.** `error()` and `fatal()` return only after their line has been written and
  every sink flushed, and a failed assert is written even at `Level::Off`, before `std::abort()`. With the default
  queue policy no message is ever dropped, and everything still queued at a normal exit is drained.

> **Note:** `Hash` is a temporary implementation. `Fnv1a64` is replaced by XXH3 once that vcpkg dependency lands
> (roadmap step 2). Switching the hash changes every recorded golden hash.

## How to use

### Fixed-width types

Include `dg/core/Types.h` and use the aliases instead of `int`, `long` or `size_t` in engine code:

| Alias | Type | Alias | Type |
|---|---|---|---|
| `u8` | `std::uint8_t` | `i8` | `std::int8_t` |
| `u16` | `std::uint16_t` | `i16` | `std::int16_t` |
| `u32` | `std::uint32_t` | `i32` | `std::int32_t` |
| `u64` | `std::uint64_t` | `i64` | `std::int64_t` |
| `f32` | `float` (asserted 4 bytes) | `f64` | `double` (asserted 8 bytes) |
| `usize` | `std::size_t` | | |

```cpp
#include "dg/core/Types.h"

namespace dg {
struct TickInfo {
    u64 tick = 0;
    u32 playerCount = 0;
};
} // namespace dg
```

### Platform detection

`dg/core/Platform.h` defines exactly one platform macro as `1` and the rest as `0`. Platform-specific code belongs in
`engine/platform/` or in clearly marked backend files (CLAUDE.md), so most code only reads the name strings:

```cpp
#include "dg/core/Platform.h"

#if DG_PLATFORM_WINDOWS
// Windows-only backend code
#elif DG_PLATFORM_MACOS || DG_PLATFORM_IOS
// Apple backend code
#endif

#if DG_ARCH_ARM64 && DG_COMPILER_CLANG
// ARM64 + Clang specific path
#endif
```

### Build information

`dg/core/BuildInfo.h` turns the macros into typed constants. The runtime prints them in its startup banner:

```cpp
#include "dg/core/BuildInfo.h"
#include "dg/core/Log.h"

void PrintBanner() {
    dg::log::info("Detergine {} ({}, {}-{}, {}){}", dg::build::kVersion, dg::build::kConfig, dg::build::kPlatform,
                  dg::build::kArch, dg::build::kCompiler, dg::build::kHeadless ? " headless" : "");
}
```

On an Apple Silicon Mac with the default configuration this prints a line like
`[18:14:35.420] [info] Detergine 0.1.0 (Development, macos-arm64, AppleClang)`.

### Logging

`dg::log` ([Log.h](engine/core/include/dg/core/Log.h)) is the engine logger. Use it for all engine output, never
`printf`, `std::cout` or spdlog directly. The helpers take a `std::format` format string, checked at compile time:

```cpp
#include "dg/core/Log.h"

#include <string>
#include <vector>

void ReportLoad(const std::vector<std::string>& assets, double seconds) {
    dg::log::debug("scanning asset folders");
    dg::log::info("loaded {} assets in {:.3f}s", assets.size(), seconds);
    if (assets.empty()) {
        dg::log::warn("no assets found");
    }
    // Guard work that exists only to build a log message.
    if (dg::log::enabled(dg::log::Level::Trace)) {
        std::string names;
        for (const std::string& asset : assets) {
            names += asset;
            names += ' ';
        }
        dg::log::trace("assets: {}", names);
    }
}
```

Each helper checks `enabled(level)` before it calls `std::format`, so a filtered message is never formatted. Its
arguments are still evaluated, as for any function call, which is why the example guards the loop with `enabled()`.

`error()` and `fatal()` log like the other helpers, but they do not return until the line has been written and every
sink flushed (see [Threading, ordering and cost](#threading-ordering-and-cost)). `fatal()` does not terminate the
process: use `DG_ASSERT` or `DG_VERIFY` for programmer errors. To log a string you have already formatted, call
`dg::log::write(level, message)`. `dg::log::writeUnfiltered(level, message)` does the same without the level filter. It
exists for the assert handler, and other code should rarely need it.

> **Warning:** Logging is presentation-side. Sim code may log, for example while you investigate a desync, but nothing
> it logs may influence the simulation (determinism rule 1). Never feed log output, or `enabled()` and `level()`, back
> into sim state.

### Starting and stopping the logger

The logger starts by itself on first use, with a default `Config` (console only, 8192-message blocking queue) and the
current threshold (`Info` unless `setLevel()` changed it). Code that runs before `main()`, such as a static
initializer, can therefore log. An app normally starts the logger explicitly, early in `main()` before it starts other
threads, and stops it before it returns:

```cpp
#include "dg/core/Log.h"

int main() {
    if (!dg::log::init({.level = dg::log::Level::Debug, .filePath = "game.log"})) {
        // The reason ("cannot open log file 'game.log': ...") has already been logged as an error,
        // and logging still runs on the console.
        dg::log::shutdown();
        return 2;
    }
    dg::log::debug("starting");
    dg::log::flush();    // returns once "starting" is on the console and in game.log
    dg::log::shutdown(); // drains the queue and joins the worker thread
    return 0;
}
```

* **`init(config)`** starts the logger, or restarts it: a logger that is already running is drained and stopped first,
  then a new one starts with the new sinks and threshold. It returns `std::unexpected` with a description when the log
  file cannot be opened. The error has then already been logged as an `Error` through the new logger, in the normal
  format and regardless of the threshold (it uses `writeUnfiltered()`), and logging continues on the remaining sinks.
  The result is not `[[nodiscard]]`; write `(void)dg::log::init(...)` when you ignore it on purpose.
* **`shutdown()`** drains the queue, joins the worker thread and flushes every sink. A second call does nothing. It
  also runs from a `std::atexit` handler that is registered the first time the logger starts, so the queue is drained
  at a normal exit even if nobody called it. The handler does not run after `std::abort()`, `std::_Exit()` or a crash,
  which is why `Error` and `Fatal` messages are flushed synchronously.
* **After `shutdown()`** the logger does not restart by itself. Each message is written synchronously to the console
  as `[<level>] <message>`, without timestamp or file, so static destructors and other teardown code can still log.
  Call `init()` to start asynchronous logging again.
* **`flush()`** blocks until every message already queued has been written and every sink has been flushed. When no
  logger is running (before the first message, or after `shutdown()`), it only flushes stdout and stderr.

Apps built on `dg::runtime` do not call `init()` themselves: `runtime::startLogging()` maps `--log-level` and
`--log-file` onto a `Config`, and `runMain()` calls `log::shutdown()` before it returns. See
[Runtime and apps](runtime-and-apps.md#logging-flags).

### Log levels and filtering

| Level | Helper | Console stream | Notes |
|---|---|---|---|
| `Trace` | `trace()` | stdout | Very detailed tracing |
| `Debug` | `debug()` | stdout | |
| `Info` | `info()` | stdout | The default threshold |
| `Warn` | `warn()` | stderr | |
| `Error` | `error()` | stderr | Blocks until written |
| `Fatal` | `fatal()` | stderr | Blocks until written; does not terminate |
| `Off` | none | | Only a threshold: disables every message except assertion failures |

A message is written when `enabled(level)` is true: `level` is not `Off` and is at or above the threshold.
`Config::level` sets the threshold at `init()`, and `setLevel(level)` changes it at any time, from any thread. It is
an atomic, so the change applies to the next call on every thread. `level()` returns the current threshold.

`parseLevel(name)` accepts exactly `trace`, `debug`, `info`, `warn`, `warning`, `error`, `fatal` and `off`, in lower
case, and returns `std::nullopt` for anything else (`INFO` is rejected). `levelName(level)` returns the lower-case
name (`warn` for `Warn`), so `parseLevel(levelName(l)) == l` for every level.

### Sinks and line formats

| Sink | Enabled when | Line format | Example |
|---|---|---|---|
| Console | `Config::console` (default `true`) | `[HH:MM:SS.mmm] [<level>] <message>` | `[18:14:35.420] [info] dg-player 0.1.0 (Debug, macos-arm64, AppleClang) ...` |
| File | `Config::filePath` is not empty | `[YYYY-MM-DD HH:MM:SS.mmm] [<level>] [thread <id>] <message>` | `[2026-11-05 18:14:35.456] [info] [thread 848302] dg-player 0.1.0 ...` |
| Logcat | Android builds, always | `<message>`, tag `Detergine` | logcat adds time, priority and thread itself |
| Synchronous fallback | After `shutdown()`, in a forked child, after a flush timeout | `[<level>] <message>` | `[info] falls back to the console` |

* Times are local time with milliseconds. The level names are the engine's own (`warn`, `fatal`), not spdlog's
  (`warning`, `critical`).
* The **console** sink sends `Trace`, `Debug` and `Info` to stdout and `Warn`, `Error` and `Fatal` to stderr. Tools
  read the two streams separately (`dg determinism` parses stdout), so the split is part of the contract. The worker
  flushes the stream after every line, so stdout and stderr stay in order when they go to the same terminal. Lines end
  in `\n`; on Windows the text-mode console stream turns that into `\r\n`.
* The **file** is truncated by `init()`. Its lines end in the platform's end of line (`SPDLOG_EOL`: `\r\n` on Windows,
  `\n` elsewhere) because spdlog opens the file in binary mode. The thread id is the OS id of the thread that logged
  the message, not of the worker. The file is buffered and flushed only by `flush()`, `error()`, `fatal()` and
  `shutdown()`, so a file you read while the app runs can lag behind the console.
* On **Android**, the logcat sink maps the levels to the logcat priorities `VERBOSE`, `DEBUG`, `INFO`, `WARN`,
  `ERROR` and `FATAL`. The console sink stays on as well (stdout and stderr are usually discarded on Android).
  `engine/core/CMakeLists.txt` links the NDK `log` library for it.
* If a sink fails while writing (for example a full disk), spdlog's error message is written to stderr as an
  `[error] ...` fallback line and logging continues.

### Queue policies

The queue holds `Config::queueSize` messages (default 8192; `0` is treated as `1`). `Config::blockWhenFull` decides
what happens when it is full:

| `blockWhenFull` | `Trace`..`Warn` message, queue full | `Error`, `Fatal`, `flush()`, queue full |
|---|---|---|
| `true` (default) | The caller waits for a free slot. Nothing is lost | The caller waits |
| `false` | The new message is dropped (spdlog `discard_new`); the caller does not wait | Never dropped: the caller waits |

Use `false` only where stalling a thread is worse than losing debug output, for example a thread that emits traces at
a very high rate. `dg::log` does not report how many messages were dropped.

```cpp
#include "dg/core/Log.h"

void StartNonBlockingLog() {
    (void)dg::log::init({.level = dg::log::Level::Trace,
                         .console = false,
                         .filePath = "trace.log",
                         .queueSize = 65536,
                         .blockWhenFull = false});
}
```

### Threading, ordering and cost

* **Every function is thread-safe.** One worker thread, owned by `dg::log`, writes all sinks. Callers format on their
  own thread and copy the finished line into the queue.
* **Ordering.** Messages from one thread reach every sink in the order they were logged. Messages from different
  threads appear in the order they entered the queue, and lines never interleave. `Error` and `Fatal` messages use
  the same queue, so they stay behind everything their thread logged before.
* **Restarts.** `init()` and `shutdown()` exclude all writers while they swap the logger. A call that races with them
  waits, then goes to the new logger (or to the synchronous fallback after a shutdown). Call `init()` before other
  threads log, so no message reaches the old sinks.
* **Stuck worker.** `flush()`, `error()` and `fatal()` wait at most 5 seconds for the worker. That only expires when
  the worker is stuck, for example on a stdout pipe nobody reads. The caller then writes
  `[warn] log flush timed out; some messages may not have been written yet` to stderr, and an `Error` or `Fatal`
  message is also written synchronously. The line may then appear twice, but it is never lost.
* **`fork()`** (POSIX only). A child process inherits a logger whose worker thread does not exist in the child.
  `pthread_atfork` handlers park the inherited logger, which is never destroyed so nothing joins the missing thread,
  and switch the child to the synchronous fallback. The child may call `init()` to start its own asynchronous logger.
  Windows has no `fork()` and registers no handlers.

What a call costs, measured on an Apple Silicon Mac (Release build, idle queue, short messages):

| Call | Cost | Why |
|---|---|---|
| Filtered message | About 3 ns | One atomic load and compare; nothing is formatted |
| `trace()`..`warn()` | Well under 1 µs | `std::format`, one copy and a queue lock. With the blocking policy and a full queue the caller waits for the worker |
| `error()`, `fatal()` | About 5 to 25 µs on an idle queue | The caller waits until the worker has written the whole queue and flushed every sink |

`error()` and `fatal()` therefore get much slower when the queue is long or a sink is slow (a terminal, a network
drive). Keep them out of per-frame hot paths, and use `warn()` for conditions that can repeat every frame. The console
sink flushes after every line, so a slow terminal limits throughput; the file sink is buffered.

### Assertions

Use asserts for **programmer errors**, conditions that are bugs if they ever fail. Use `std::expected<T, dg::Error>`
for recoverable failures such as a missing file (CLAUDE.md, C++ conventions).

```cpp
#include "dg/core/Assert.h"
#include "dg/core/Types.h"

dg::u32 gTickRate = 30;

void SetTickRate(dg::u32 tickRate) {
    DG_ASSERT(tickRate > 0);
    DG_ASSERT_MSG(tickRate <= 240, "tick rates above 240 Hz are not supported");
    gTickRate = tickRate;
}

bool Flush(); // has a side effect that must happen in every configuration

void Shutdown() {
    DG_VERIFY(Flush()); // Flush() runs in Release too; only the check is removed
}
```

| Macro | Checked builds (`DG_ASSERTS_ENABLED=1`) | Release (`DG_ASSERTS_ENABLED=0`) |
|---|---|---|
| `DG_ASSERT(cond)` | Evaluates `cond`; on failure logs and aborts | Expands to `((void)0)`: `cond` is **not** evaluated |
| `DG_ASSERT_MSG(cond, msg)` | Same, and appends `msg` to the report | Expands to `((void)0)` |
| `DG_VERIFY(cond)` | Same as `DG_ASSERT(cond)` | Expands to `((void)(cond))`: `cond` **is** evaluated |

A failed assert writes one `Fatal` line to stderr through `dg::log::writeUnfiltered()` and then calls `std::abort()`.
The line is written even at `--log-level off`, and because `Fatal` messages flush synchronously, the asynchronous queue
is drained before the process dies:

```text
[18:14:35.420] [fatal] Assertion failed: tickRate <= 240 — tick rates above 240 Hz are not supported (/path/to/Game.cpp:8 in void SetTickRate(dg::u32))
```

The file, line and function come from `std::source_location`. The file is the path as it was passed to the compiler,
which is an absolute path in CMake builds. The exact function spelling depends on the compiler.

> **Important:** Never put a side effect inside `DG_ASSERT` or `DG_ASSERT_MSG`. It disappears in Release, and a
> Release build that behaves differently from Debug is a bug. `./dg determinism` catches it only when the difference
> changes the final sim state hash of a headless `dg-player` run; it does not exercise other code.

### Hashing state

`Fnv1a64` is an incremental hasher. Feed it scalars with `updateValue` or raw bytes with `update`, then read
`digest()`. The sim's `stateHash()` and the golden-hash tests use it:

```cpp
#include "dg/core/Hash.h"
#include "dg/core/Types.h"

dg::u64 HashPositions(const dg::i64* xs, const dg::i64* ys, dg::usize count, dg::u64 tick) {
    dg::Fnv1a64 hash;
    hash.updateValue(tick);
    for (dg::usize i = 0; i < count; ++i) {
        hash.updateValue(xs[i]);
        hash.updateValue(ys[i]);
    }
    return hash.digest();
}
```

Hash structs field by field, never as raw bytes. `updateValue` only accepts scalar types (`std::is_scalar_v`), which
makes the mistake of hashing a whole struct, padding included, a compile error (determinism rule 8). It does not stop
you passing the struct's bytes to `update` (for example through `std::as_bytes`), so never do that. `std::is_scalar_v`
also admits pointers: `updateValue(ptr)` compiles but hashes an address, which differs between runs. All members are
`constexpr`, so a hash can also be computed at compile time.

## How to edit

### Where the code lives

| File | Contents |
|---|---|
| [engine/core/CMakeLists.txt](engine/core/CMakeLists.txt) | Declares the `dg_core` static library (alias `dg::core`) with `dg_add_module` and its public compile definitions |
| [Types.h](engine/core/include/dg/core/Types.h) | Type aliases and the `f32`/`f64` size and little-endian `static_assert`s |
| [Platform.h](engine/core/include/dg/core/Platform.h) | Preprocessor detection; `#error` on unsupported platforms, architectures and compilers |
| [BuildInfo.h](engine/core/include/dg/core/BuildInfo.h) | `dg::build` constants built from the CMake definitions and `Platform.h` |
| [Log.h](engine/core/include/dg/core/Log.h) | `Level`, `Config` and the public API; the format helpers are templates here. No spdlog |
| [Log.cpp](engine/core/src/Log.cpp) | Everything spdlog: the worker pool, the two async loggers, the sinks, the flush fence, lazy start, the `atexit` and `fork` hooks, the synchronous fallback |
| [Assert.h](engine/core/include/dg/core/Assert.h), [Assert.cpp](engine/core/src/Assert.cpp) | The macros and `dg::detail::assertFailed`, which logs with `writeUnfiltered` |
| [Hash.h](engine/core/include/dg/core/Hash.h) | `Fnv1a64` |

### How the logger works

spdlog is confined to [Log.cpp](engine/core/src/Log.cpp). [engine/core/CMakeLists.txt](engine/core/CMakeLists.txt)
finds it with `find_package(spdlog CONFIG REQUIRED)` and links `spdlog::spdlog` through `PRIVATE_DEPS`. `dg_core` is a
static library, so programs that link it also link `libspdlog`, but they never compile against spdlog's headers or
see its compile definitions. The vcpkg entry is `spdlog` with `"default-features": false`, which gives spdlog 1.17.0
as a compiled library built on `std::format` (`SPDLOG_USE_STD_FORMAT`), with no dependency on the fmt library (see
[Build system](../tooling/build-system.md#vcpkg)).

Inside `Log.cpp`:

* A single `State` object holds everything. It is allocated once and never destroyed, so logging keeps working from
  other static destructors at exit.
* `startLocked()` builds the sinks, then one `spdlog::details::thread_pool` with `queueSize` slots and **one** worker
  thread, and two `spdlog::async_logger`s on that pool. `dg` carries `Trace`..`Warn` with the configured overflow
  policy (`block` or `discard_new`). `dg-urgent` always uses `block` and carries `Error`/`Fatal` messages and every
  flush request. Both loggers share the pool and the sinks, so all their messages reach the sinks in queue order. The
  loggers are not registered in spdlog's global registry.
* Filtering happens in `dg::log`, before formatting: both spdlog loggers are set to `trace`, and their automatic
  flushing is off (`flush_on(off)`).
* spdlog's own asynchronous flush only posts a request and returns. `FlushFence`, always the last sink, makes
  `flush()` blocking: each request gets a ticket in queue order, the worker flushes the sinks in order, and when it
  reaches the fence every earlier sink has written and flushed everything queued before the request. The fence counts
  completed flushes, and `flush()` waits until the count reaches its ticket (5 seconds at most).
* A `std::shared_mutex` guards the lifecycle: `write()` and `flush()` take it shared, `init()` and `shutdown()` take
  it exclusively while they replace the logger.

### Invariants a logging change must keep

* **spdlog stays private.** No engine header and no other module includes spdlog. A new capability goes into
  `Log.cpp` behind a `Config` field or a `dg::log` function.
* **Flush tickets stay exact.** Every flush request must be posted with the `block` policy (through `urgent`), and
  nothing else may request a flush: no `flush_on()` level, no periodic `flush_every()`. Never use spdlog's
  `overrun_oldest` policy. It overwrites the oldest queued entry whatever posted it, including flush requests and
  `Error` lines, and a single lost flush request leaves the fence one ticket behind, so every later `flush()`,
  `error()` and assertion waits for the full timeout. `FlushFence` must remain the last sink.
* **`Error` and `Fatal` flush before they return,** and assertion failures bypass the level filter. The death tests
  depend on both.
* **The console split and format are an interface.** `Warn` and above go to stderr; `dg determinism` parses stdout.
  Console lines end in a plain `\n`.
* **Never log through the logger while holding the exclusive lifecycle lock.** `init()` reports an unopenable file
  only after it has released the lock.
* **Platform code stays guarded.** The `pthread_atfork` hooks are inside `#if !DG_PLATFORM_WINDOWS` and the logcat
  sink inside `#if DG_PLATFORM_ANDROID`, both in `Log.cpp`.

### Add a sink

1. In `startLocked()` in [Log.cpp](engine/core/src/Log.cpp), create the sink and add it to `sinks` before the fence is
   appended. Use an `_mt` sink or `base_sink<std::mutex>`.
2. Give it a formatter with `makeFormatter(pattern)`. `%*` prints the engine's level name. Pass `"\n"` as the end of
   line for text-mode streams, as the console sink does.
3. If the sink is optional, add a field with a default to `Config` in [Log.h](engine/core/include/dg/core/Log.h). If it
   is platform-specific, guard it with a `DG_PLATFORM_*` check and link any system library in
   [engine/core/CMakeLists.txt](engine/core/CMakeLists.txt), as the logcat sink does with `log`.
4. A sink runs on the worker thread and must never log through `dg::log`: the worker would wait for itself.
5. Add a test to [LogTest.cpp](tests/core/LogTest.cpp) and describe the sink in the tables on this page.

### Build definitions

The configuration macros are not set in any header. `engine/core/CMakeLists.txt` adds them as `PUBLIC` compile
definitions of `dg_core`, so every target that links `dg::core` sees the same values:

| Definition | Value |
|---|---|
| `DG_VERSION_STRING` | `"${PROJECT_VERSION}"` from the top-level `project()` (currently `0.1.0`) |
| `DG_BUILD_CONFIG` | `"Debug"`, `"Development"` or `"Release"` |
| `DG_CONFIG_DEBUG`, `DG_CONFIG_DEVELOPMENT`, `DG_CONFIG_RELEASE` | `1` for the active configuration, `0` otherwise |
| `DG_ASSERTS_ENABLED` | `1` in Debug and Development, `0` in Release |
| `DG_HEADLESS` | `1` when CMake option `DG_HEADLESS` is on, else `0` |

The configurations themselves are defined in `cmake/DgConfigs.cmake`; see [Build system](../tooling/build-system.md).

> **Warning:** A translation unit that does not link `dg::core` sees `DG_ASSERTS_ENABLED` as undefined, which `#if`
> treats as `0`, so its asserts are silently off. Always link `dg::core` instead of adding its include directory by
> hand.

### Invariants a change must keep

* **Platform macros stay always-defined.** When you add a platform, define its macro as `1` in the detection chain,
  add a `#ifndef … #define … 0` fallback, and add a `DG_PLATFORM_NAME`. Keep the `#error` for unknown targets.
* **The architecture list stays x64 + ARM64.** The determinism guarantee is only verified on those two; a new
  architecture needs golden-hash runs before it is allowed.
* **`DG_ASSERT` stays side-effect free in Release.** Do not change the Release expansion to evaluate `cond`; code
  relies on asserts costing nothing in shipping builds.
* **The assert failure message format is part of the tests.** Death tests match `"Assertion failed.*<message>"`, so
  keep the `Assertion failed:` prefix.
* **`assertFailed` must not return.** It is `[[noreturn]]` and ends in `std::abort()`. It logs with
  `log::writeUnfiltered()`, so that `--log-level off` cannot hide why the process aborts.
* **`Fnv1a64` output is golden data.** Changing the constants, the byte order of `updateValue`, or what a caller feeds
  into a hash changes the state hash and fails the golden-hash tests in `dg-core-tests` and `dg-sim-tests`. That is a
  network protocol change (CLAUDE.md, Multiplayer): re-record the goldens deliberately and say so in the commit.

### Extending

* **A new log level** needs an enum value in `Level`, a name in `kLevelNames` (same position), cases in `toSpdlog()`
  and `fromSpdlog()` in `Log.cpp`, and, if callers should use it, a template helper in `Log.h`. Keep the enum ordered
  by severity with `Off` last: `>=` comparisons decide the stderr split (`Warn` and above), the synchronous flush
  (`Error` and above) and the filter. Extend `LogLevels.ParseAndNameRoundTrip` and `AsyncLog.UsesEngineLevelNames`.
* **A new log sink** is added in `Log.cpp` only; see [Add a sink](#add-a-sink).
* **A new build constant** goes into `BuildInfo.h` as an `inline constexpr`, backed by a `PUBLIC` definition in
  `engine/core/CMakeLists.txt`.
* **Replacing `Fnv1a64` with XXH3** (planned): keep the `update` / `updateValue` / `digest` interface so callers do not
  change, add xxHash to `vcpkg.json` and DESIGN.md §2, and re-record every golden hash in one commit.
* **A new file in `engine/core/`** must be added to the `sources` front matter of the page that documents it (this
  page, [Memory allocators](memory.md) or [Generational handles](handles.md)). Coverage is checked per file, so an
  unlisted file fails `./dg docs --check`.

## Reference

### `dg::build` (BuildInfo.h)

| Constant | Type | Source |
|---|---|---|
| `kVersion` | `const char*` | `DG_VERSION_STRING` |
| `kConfig` | `const char*` | `DG_BUILD_CONFIG` |
| `kPlatform` | `const char*` | `DG_PLATFORM_NAME` |
| `kArch` | `const char*` | `DG_ARCH_NAME` |
| `kCompiler` | `const char*` | `DG_COMPILER_NAME` |
| `kHeadless` | `bool` | `DG_HEADLESS != 0` |

### Platform macros (Platform.h)

| Macro | Defined to `1` when | Otherwise |
|---|---|---|
| `DG_PLATFORM_WINDOWS` | `_WIN32` | `0` |
| `DG_PLATFORM_ANDROID` | `__ANDROID__` | `0` |
| `DG_PLATFORM_IOS` | `__APPLE__` and `TARGET_OS_IPHONE` | `0` |
| `DG_PLATFORM_MACOS` | `__APPLE__` and not `TARGET_OS_IPHONE` | `0` |
| `DG_PLATFORM_LINUX` | `__linux__` (and not Android) | `0` |
| `DG_ARCH_X64` | `__x86_64__` or `_M_X64` | `0` |
| `DG_ARCH_ARM64` | `__aarch64__` or `_M_ARM64` | `0` |
| `DG_COMPILER_CLANG` | `__clang__` (includes AppleClang and clang-cl) | `0` |
| `DG_COMPILER_MSVC` | `_MSC_VER` without `__clang__` | `0` |
| `DG_COMPILER_GCC` | `__GNUC__` without `__clang__` | `0` |

| String macro | Values |
|---|---|
| `DG_PLATFORM_NAME` | `"windows"`, `"android"`, `"ios"`, `"macos"`, `"linux"` |
| `DG_ARCH_NAME` | `"x64"`, `"arm64"` |
| `DG_COMPILER_NAME` | `"AppleClang"`, `"clang-cl"`, `"Clang"`, `"MSVC"`, `"GCC"` |

Detection order matters: Android is checked before Linux, and iOS/macOS share `__APPLE__`.

### `dg::log` (Log.h)

| Declaration | Description |
|---|---|
| `enum class Level : u8 { Trace, Debug, Info, Warn, Error, Fatal, Off }` | Severity in ascending order; `Off` is only a threshold |
| `struct Config` | Settings for `init()`; fields below |
| `std::expected<void, std::string> init(const Config& config = {})` | Starts or restarts asynchronous logging. Returns an error if the log file cannot be opened (also logged as an `Error`; logging continues without the file) |
| `void shutdown()` | Drains the queue, joins the worker, flushes the sinks. Later messages are written synchronously. Also runs at exit |
| `void flush()` | Blocks until every queued message is written and every sink flushed (at most 5 s) |
| `void setLevel(Level level)`, `Level level()` | Set or read the threshold (atomic) |
| `bool enabled(Level level)` | `level != Off && level >= threshold` |
| `std::optional<Level> parseLevel(std::string_view name)` | `trace`, `debug`, `info`, `warn`, `warning`, `error`, `fatal`, `off`; case-sensitive |
| `std::string_view levelName(Level level)` | The lower-case name; `"?"` for a value outside the enum |
| `void write(Level level, std::string_view message)` | Logs a preformatted message if `enabled(level)`. `Error` and `Fatal` block until written |
| `void writeUnfiltered(Level level, std::string_view message)` | As `write`, without the level filter (still nothing for `Level::Off`). Used by `assertFailed` |
| `template <typename... Args> void trace(std::format_string<Args...> fmt, Args&&... args)` | If `enabled(Level::Trace)`: `write(Level::Trace, std::format(fmt, args...))` |
| `debug(...)`, `info(...)`, `warn(...)`, `error(...)`, `fatal(...)` | Same for `Debug`, `Info`, `Warn`, `Error`, `Fatal`. `fatal` does not terminate |

| `Config` field | Type | Default | Meaning |
|---|---|---|---|
| `level` | `Level` | `Level::Info` | Threshold; messages below it are discarded before they are formatted |
| `console` | `bool` | `true` | Console sink: `Trace`..`Info` to stdout, `Warn`..`Fatal` to stderr |
| `filePath` | `std::string` | empty | Log file, truncated at `init()`; empty means no file |
| `queueSize` | `usize` | `8192` | Queue capacity in messages (`0` is treated as `1`) |
| `blockWhenFull` | `bool` | `true` | `true`: callers wait for a free slot. `false`: `Trace`..`Warn` messages that find the queue full are dropped; `Error`, `Fatal` and `flush()` never are |

### Assertions (Assert.h)

| Name | Description |
|---|---|
| `DG_ASSERT(cond)` | Aborts with the stringified condition if `cond` is false; compiled out when `DG_ASSERTS_ENABLED` is `0` |
| `DG_ASSERT_MSG(cond, msg)` | As `DG_ASSERT`, with a `const char*` message |
| `DG_VERIFY(cond)` | As `DG_ASSERT` in checked builds; in Release still evaluates `cond` |
| `[[noreturn]] void dg::detail::assertFailed(const char* expression, const char* message, std::source_location location)` | Logs at `Level::Fatal` and calls `std::abort()`; `message` may be `nullptr`. Called only by the macros |

### `dg::Fnv1a64` (Hash.h)

| Member | Description |
|---|---|
| `constexpr void update(std::span<const std::byte> bytes)` | FNV-1a step per byte: `state ^= byte; state *= prime` |
| `template <class T> requires std::is_scalar_v<T> constexpr void updateValue(T value)` | Hashes the exact bytes of `value` via `std::bit_cast` (native little-endian order) |
| `constexpr u64 digest() const` | The current hash; does not reset the state, so you can keep updating |

The offset basis is `14695981039346656037` and the prime is `1099511628211` (the standard 64-bit FNV-1a constants).
An empty hasher's `digest()` is the offset basis.

## Testing

The core module is tested by the `dg-core-tests` GoogleTest executable, built from
[MemoryTest.cpp](tests/core/MemoryTest.cpp), [HandleTest.cpp](tests/core/HandleTest.cpp) and
[LogTest.cpp](tests/core/LogTest.cpp) and labelled `unit` and `determinism`:

```bash
./dg test                    # all suites, Development configuration
./dg test debug -L determinism
./dg determinism             # Debug and Release must produce the same sim state hash
ctest --test-dir build/macos-arm64-xcode -C Debug -R 'AsyncLog|LogLevels' --output-on-failure   # logging only
```

What covers the pieces on this page:

* **Asserts:** the `*Death` tests in [MemoryTest.cpp](tests/core/MemoryTest.cpp) run only when
  `DG_ASSERTS_ENABLED` is `1` and check that misuse aborts with `Assertion failed.*<message>`. The
  `UncheckedAllocators` test runs only in Release and checks that the same misuse returns `nullptr` instead. Run the
  tests in both Debug and Release to cover both branches.
* **Hashing:** `HandlePool.ScriptedCreateDestroySequenceMatchesGoldenHash` in
  [HandleTest.cpp](tests/core/HandleTest.cpp) and the sim determinism tests hash with `Fnv1a64` and compare against
  recorded golden values, so any change to the hash shows up there. See [Testing](../tooling/testing.md) for what
  golden hashes mean and how to re-record them.
* **Types and platform:** the `static_assert`s in `Types.h` and the `#error` branches in `Platform.h` are checked by
  every build. CI builds macOS ARM64, Windows x64, Linux x64 and Android ARM64 (see
  [Continuous integration](../tooling/ci.md)).
* **Logging:** [LogTest.cpp](tests/core/LogTest.cpp) holds three suites. Each CTest entry runs in its own process,
  and every test that starts a logger in the test process restores the default logger state when it ends
  (`LoggerReset`); the two `threadsafe` death tests start theirs only in the child. Most tests log to a file in
  `<temp>/dg-log-tests/` and read it back.

| Test | Checks |
|---|---|
| `AsyncLog.WritesEveryMessageInOrder` | 2000 messages arrive complete and in order |
| `AsyncLog.FlushBlocksUntilEarlierMessagesAreWritten` | After `flush()` the file is complete while the logger keeps running |
| `AsyncLog.ErrorsAreWrittenBeforeTheCallReturns` | `error()` flushes by itself |
| `AsyncLog.LevelFilterSkipsFormatting` | Filtered messages are never formatted; `enabled()` semantics |
| `AsyncLog.UsesEngineLevelNames` | `trace`..`fatal` tags in the output |
| `AsyncLog.ManyThreadsLoseNothingWhenTheQueueBlocks` | 8 threads, a 64-slot queue: no loss, per-thread order kept |
| `AsyncLog.NonBlockingQueueDropsMessagesButNeverFlushesOrErrors` | With `blockWhenFull = false` and a 4-slot queue, messages are dropped, but `flush()` stays under 2 s and `error()` under 1 s, and every error line is written |
| `AsyncLog.RestartsWithANewConfiguration` | A second `init()` drains the old logger and switches files |
| `AsyncLog.LoggingAfterShutdownStillWorks` | The synchronous console fallback after `shutdown()` |
| `AsyncLog.ConsoleSplitsStdoutAndStderrByLevel` | The stdout/stderr split, and no `\r` in console output |
| `AsyncLog.UnopenableLogFileIsReportedAndLoggingContinues` | `init()` returns the error, logs it in the normal format and keeps logging |
| `LogLevels.ParseAndNameRoundTrip` | `parseLevel` / `levelName` round trip, `warning`, case sensitivity |
| `AsyncLogDeath.AssertionMessageSurvivesAbort` | The assert message is written behind 5000 queued messages before `abort()` (asserts enabled) |
| `AsyncLogDeath.AssertionMessageSurvivesLevelOff` | The assert message is written at `Level::Off` (asserts enabled) |
| `AsyncLogDeath.ForkedChildLogsSynchronouslyAndExitsCleanly` | A forked child logs `error()` fast and exits without `std::terminate` (POSIX only; also in Release) |
| `AsyncLogDeath.ForkedChildAssertionMessageSurvivesAbort` | A forked child's assert message is written (POSIX only, asserts enabled) |

The fork tests use the `fast` death-test style on purpose: it forks without re-executing the binary, which is the
situation they test. The two timing bounds in `NonBlockingQueueDropsMessagesButNeverFlushesOrErrors` are far above the
normal cost (milliseconds) and far below the 5-second flush timeout, which is what the test guards against. The app
smoke tests (`player.smoke`, `server.smoke`, `editor.smoke`) also exercise logging and the build-info banner.

> **Note:** The logger has been built and tested on macOS ARM64 only, including a ThreadSanitizer run of the
> `AsyncLog` and `LogLevels` tests. The Windows end-of-line handling and the Android logcat sink have not been run
> yet.
