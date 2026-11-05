---
title: Core foundation: types, platform, logging, asserts, hashing
section: Systems
order: 10
status: implemented
summary: The dg::core basics every module builds on: fixed-width type aliases, platform and build macros, logging, assertions and the incremental state hash.
sources: engine/core/CMakeLists.txt, engine/core/include/dg/core/Types.h, engine/core/include/dg/core/Platform.h, engine/core/include/dg/core/BuildInfo.h, engine/core/include/dg/core/Log.h, engine/core/src/Log.cpp, engine/core/include/dg/core/Assert.h, engine/core/src/Assert.cpp, engine/core/include/dg/core/Hash.h
---

## Overview

`dg::core` is the lowest layer of the engine (Core → Platform/RHI → Engine services → Simulation | Presentation → …).
Every other module links it, and it depends on nothing but the C++23 standard library. This page covers the small,
header-heavy pieces of the module:

| Header | Namespace | Purpose |
|---|---|---|
| [Types.h](engine/core/include/dg/core/Types.h) | `dg` | Fixed-width aliases (`u32`, `i64`, `f32`, `usize`, …) and the little-endian requirement |
| [Platform.h](engine/core/include/dg/core/Platform.h) | macros | Compile-time platform, CPU architecture and compiler detection (`DG_PLATFORM_*`, `DG_ARCH_*`, `DG_COMPILER_*`) |
| [BuildInfo.h](engine/core/include/dg/core/BuildInfo.h) | `dg::build` | Version, configuration, platform, architecture, compiler and headless flag as `constexpr` values |
| [Log.h](engine/core/include/dg/core/Log.h) | `dg::log` | Thread-safe, `std::format`-based logging to stdout/stderr |
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

> **Note:** `Log` and `Hash` are temporary implementations. Logging moves to spdlog sinks and `Fnv1a64` is replaced by
> XXH3 once those vcpkg dependencies land (roadmap step 2). Switching the hash changes every recorded golden hash.

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
`[info] Detergine 0.1.0 (Development, macos-arm64, AppleClang)`.

### Logging

The `dg::log` functions take a `std::format` format string, checked at compile time:

```cpp
#include "dg/core/Log.h"

void ReportLoad(int assetCount, double seconds) {
    dg::log::debug("scanning asset folders");
    dg::log::info("loaded {} assets in {:.3f}s", assetCount, seconds);
    if (assetCount == 0) {
        dg::log::warn("no assets found");
    }
}
```

Each call writes one line, `[<level>] <message>`, where the level tag is `debug`, `info`, `warn`, `error` or `fatal`.
`Debug` and `Info` go to stdout; `Warn`, `Error` and `Fatal` go to stderr. Every line is flushed immediately and
written under a mutex, so lines from different threads never interleave.

There is no `fatal()` helper: `Level::Fatal` is used by the assert handler. To log a preformatted string at any level,
call `dg::log::write(dg::log::Level::Error, message)` directly.

> **Warning:** Logging is IO. Sim code must not log as part of its behaviour (determinism rule 1). Log from the
> runtime, tools and presentation layers.

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

A failed assert writes one `Fatal` line to stderr and calls `std::abort()`:

```text
[fatal] Assertion failed: tickRate <= 240 — tick rates above 240 Hz are not supported (/path/to/Game.cpp:8 in void SetTickRate(dg::u32))
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
| [Log.h](engine/core/include/dg/core/Log.h), [Log.cpp](engine/core/src/Log.cpp) | Level enum, `write`, the format helpers, the output mutex |
| [Assert.h](engine/core/include/dg/core/Assert.h), [Assert.cpp](engine/core/src/Assert.cpp) | The macros and `dg::detail::assertFailed` |
| [Hash.h](engine/core/include/dg/core/Hash.h) | `Fnv1a64` |

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
* **`assertFailed` must not return.** It is `[[noreturn]]` and ends in `std::abort()`.
* **`Fnv1a64` output is golden data.** Changing the constants, the byte order of `updateValue`, or what a caller feeds
  into a hash changes the state hash and fails the golden-hash tests in `dg-core-tests` and `dg-sim-tests`. That is a
  network protocol change (CLAUDE.md, Multiplayer): re-record the goldens deliberately and say so in the commit.

### Extending

* **A new log level** needs an enum value in `Level`, a tag in `levelTag` in `Log.cpp`, and (if callers should use it)
  a template helper in `Log.h`. Levels at or above `Warn` go to stderr because of the `>=` comparison, so keep the
  enum ordered by severity.
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
| `enum class Level : u8 { Debug, Info, Warn, Error, Fatal }` | Severity, in ascending order |
| `void write(Level level, std::string_view message)` | Writes `[<tag>] <message>\n`; `Warn` and above to stderr, the rest to stdout; flushes; thread-safe |
| `template <class... Args> void debug(std::format_string<Args...>, Args&&...)` | `write(Level::Debug, std::format(...))` |
| `info(...)`, `warn(...)`, `error(...)` | Same for `Info`, `Warn`, `Error` |

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

The core module is tested by the `dg-core-tests` GoogleTest executable, built from `tests/core/MemoryTest.cpp` and
`tests/core/HandleTest.cpp` and labelled `unit` and `determinism`:

```bash
./dg test                    # all suites, Development configuration
./dg test debug -L determinism
./dg determinism             # Debug and Release must produce the same sim state hash
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
* **Logging and build info:** exercised by the app smoke tests (`player.smoke`, `server.smoke`, `editor.smoke`), which
  print the startup banner. There is no dedicated unit test for `Log` yet.
