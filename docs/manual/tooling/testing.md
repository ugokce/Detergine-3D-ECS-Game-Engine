---
title: Testing
section: Tooling
order: 40
status: implemented
summary: How the engine is tested with GoogleTest and CTest, what the unit, determinism and smoke labels mean, how golden hashes and death tests work, and how to run tests under sanitizers.
sources: tests/
---

## Overview

Every engine module is covered by **GoogleTest** suites that run through **CTest**. GoogleTest 1.18.0 comes from
vcpkg (the `gtest` entry in [vcpkg.json](vcpkg.json)), so there is nothing to install by hand. Tests are built
whenever the CMake option `DG_BUILD_TESTS` is `ON`, which is the default.

The test tree mirrors the engine:

| Path | Executable | Covers |
|---|---|---|
| [tests/core/MemoryTest.cpp](tests/core/MemoryTest.cpp), [tests/core/HandleTest.cpp](tests/core/HandleTest.cpp), [tests/core/LogTest.cpp](tests/core/LogTest.cpp) | `dg-core-tests` | Allocators, generational handles and the asynchronous logger (`dg::core`) |
| [tests/math/FixedTest.cpp](tests/math/FixedTest.cpp) | `dg-fixed-tests` | `dg::fixed32` / `dg::fixed64` |
| [tests/math/DetMathTest.cpp](tests/math/DetMathTest.cpp) | `dg-detmath-tests` | `dg::detmath` accuracy, special values and bit-exactness |
| [tests/math/VectorTest.cpp](tests/math/VectorTest.cpp) | `dg-vector-tests` | Vectors, quaternions and matrices |
| [tests/sim/SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp) | `dg-sim-tests` | `dg::sim::Simulation` state hash |
| [tests/support/TestEnvironment.cpp](tests/support/TestEnvironment.cpp) | linked into every executable | Suppresses Windows error dialogs |
| [tests/CMakeLists.txt](tests/CMakeLists.txt) | | `dg_add_test` and the app smoke tests |

Three kinds of test protect different things:

* **Unit tests** check one function or type against its contract, including the failure paths.
* **Determinism tests** check that results are bit-identical across runs, build configurations, compilers and CPU
  architectures. The strongest form is a **golden hash**: a 64-bit hash of a fixed workload, recorded once on macOS
  ARM64 and compared on every platform.
* **Smoke tests** start each app, run 120 fast ticks (`--fast --ticks 120`) and require a clean exit. `dg-player` is
  started with `--headless`, `dg-server` is always headless, and `dg-editor` runs its normal engine loop.

Each `TEST()` is registered with CTest as its own test, named `<executable>.<Suite>.<Test>`, for example
`dg-core-tests.HandlePool.ScriptedCreateDestroySequenceMatchesGoldenHash`. A failure therefore names the exact case,
and you can select any subset with a regular expression or a label.

## How to use

### Run the tests

The usual entry point is the [dg tool](dg-tool.md). `dg test` builds everything for the chosen configuration and then
runs `ctest --output-on-failure`:

```bash
./dg test                    # Development (the default)
./dg test debug              # Debug
./dg test release            # Release: asserts compiled out, assert death tests skipped
./dg test dev -L determinism # only tests with the determinism label
./dg test dev -L smoke       # only the app smoke tests
```

To pick a subset by name, call `ctest` directly on the build tree. The build tree is `build/<preset>/`, and every
preset uses a multi-config generator, so `-C` selects the configuration:

```bash
ctest --test-dir build/macos-arm64 -C Development -N                    # list tests without running them
ctest --test-dir build/macos-arm64 -C Debug -R HandlePool               # every HandlePool test
ctest --test-dir build/macos-arm64 -C Debug -R 'Death' --output-on-failure
ctest --test-dir build/macos-arm64 -C Release -L determinism -j 8
ctest --preset macos-arm64-debug                                        # test preset from CMakePresets.json
```

Test presets exist for the macOS, Windows and Linux configure presets (for example `macos-arm64-dev`,
`windows-x64-release`, `linux-x64-server-debug`). Android has build presets only, because its binaries cannot run on
the build host.

You can also run a test executable directly, which is the quickest way to iterate on one suite. The binaries are in
`build/<preset>/bin/<Config>/`:

```bash
./build/macos-arm64/bin/Debug/dg-fixed-tests --gtest_filter='FixedDivision.*'
./build/macos-arm64/bin/Debug/dg-sim-tests --gtest_list_tests
```

### Labels

| Label | Applied to | Select with |
|---|---|---|
| `unit` | Every test registered by `dg_add_test` | `-L unit` |
| `determinism` | Every test in an executable declared with `LABELS determinism` | `-L determinism` |
| `smoke` | `player.smoke`, `server.smoke`, `editor.smoke` | `-L smoke` |

All five current executables are declared with `LABELS determinism`, so today `-L unit` and `-L determinism` select
the same GoogleTest cases. `-L unit` excludes the smoke tests, which is why the pre-push hook uses it (see
[Continuous integration and git hooks](ci.md)).

### Current suites

The counts below come from `ctest -N` and `--gtest_list_tests` on macOS ARM64. Debug and Development compile asserts
in (`DG_ASSERTS_ENABLED=1`), so they include the assert death tests; Release compiles asserts out and runs the
release-only fallback tests instead.

| Executable | GoogleTest suites | Debug / Development | Release |
|---|---|---|---|
| `dg-core-tests` | `AlignUp`, `DefaultAlignment`, `HeapAllocator`, `LinearAllocator`, `PoolAllocator`, `Handle`, `HandlePool`, `AsyncLog`, `LogLevels`, `AsyncLogDeath` (4 tests; 1 in Release); Debug/Development only: `HeapAllocatorDeath`, `LinearAllocatorDeath`, `PoolAllocatorDeath`; Release only: `UncheckedAllocators` | 54 | 38 |
| `dg-fixed-tests` | `FixedConstruction`, `FixedConversion`, `FixedFromDouble`, `FixedFromFloat`, `FixedWrapping`, `FixedArithmetic`, `FixedMultiply`, `FixedMultiplyTies`, `FixedDivision`, `FixedSqrt`, `FixedReference`, `FixedDetail`, `FixedRounding`, `FixedLerp`, `FixedMinMax`, `FixedFormat`, `FixedGolden`; Debug/Development only: `FixedDeath` | 36 | 34 |
| `dg-detmath-tests` | `DetMathAccuracy`, `DetMathSpecialValues`, `DetMathIdentities`, `DetMathDeterminism` | 17 | 17 |
| `dg-vector-tests` | `Vector`, `Quat`, `Matrix`, `VectorRegression`, `QuatRegression`, `MatrixRegression`, `MathGolden` | 17 | 17 |
| `dg-sim-tests` | `SimDeterminism`; Debug/Development only: `SimulationDeath` | 6 | 4 |
| Smoke tests | `player.smoke`, `server.smoke`, `editor.smoke` | 3 | 3 |
| **Total CTest tests** | | **133** | **113** |

There are 24 death tests in Debug and Development: 20 `DG_ASSERT` death tests in the allocator, fixed-point and sim
suites, plus the four `AsyncLogDeath` tests. `AsyncLogDeath.ForkedChildLogsSynchronouslyAndExitsCleanly` checks an exit
code rather than an assert, so it also runs in Release. The two fork tests are POSIX-only, so on Windows
`AsyncLogDeath` has two tests in Debug and none in Release. `server.smoke` and `editor.smoke` are only registered when their targets exist:
the editor is not built for headless or mobile presets, and the server is not built for Android or iOS.

### Golden hashes

| Test | Golden value | What it pins |
|---|---|---|
| `dg-core-tests.HandlePool.ScriptedCreateDestroySequenceMatchesGoldenHash` | `0x3a7ea63a09700779` | Handle slot reuse order and generations, which become sim entity ids |
| `dg-fixed-tests.FixedGolden.MixedOperationsHashMatchesRecordedValue` | `0x508880CD2D9E3341` | Bit patterns of a mixed batch of fixed-point operations |
| `dg-detmath-tests.DetMathDeterminism.GoldenBitHashMatchesAcrossPlatforms` | `0x1BFF52C5DF7021AE` | Exact output bits of every `detmath` function over awkward and pseudo-random inputs |
| `dg-vector-tests.MathGolden.DeterministicBatchOfOperationsMatchesRecordedBitPatterns` | `0xcdffd04806b21b0c` | Evaluation order of vector, quaternion and matrix operations |
| `dg-sim-tests.SimDeterminism.StateHashMatchesCrossPlatformGolden` | `0x9f504fe2ce4e5ee2` | `Simulation::stateHash()` after 1800 ticks, seed `0x5eed`, 2 players |

All values were recorded on macOS ARM64 (AppleClang). Every platform, architecture and build configuration must
reproduce them exactly. Four of the five tests print the measured value on every run, not only on failure, so you can
compare logs from different machines side by side. The exception is `FixedGolden`, which shows the measured value
only in its failure message.

The math suites generate their pseudo-random inputs with local generators instead of `dg::sim::Rng`: xorshift64* in
`FixedTest.cpp` and `VectorTest.cpp`, SplitMix64 in `DetMathTest.cpp`. A
change to the sim RNG therefore cannot move the math golden values.

## How to edit

### Add a test to an existing suite

Add a `TEST(Suite, Name)` to the matching file. Because tests are discovered at run time, CTest picks it up on the
next `./dg test` without any CMake change. Name suites after the type or behaviour under test (`PoolAllocator`,
`FixedDivision`) and tests as a sentence that states the expected behaviour
(`DifferentSeedProducesDifferentStateHash`). Every bug fix needs a regression test (CLAUDE.md, *Build & test*).
`VectorRegression`, `QuatRegression` and `MatrixRegression` show the convention of grouping fixes for reviewed
findings in a dedicated suite.

### Add a test executable

Declare it in [tests/CMakeLists.txt](tests/CMakeLists.txt) with `dg_add_test`:

```cmake
dg_add_test(dg-sim-tests SOURCES sim/SimDeterminismTest.cpp DEPS dg::sim LABELS determinism)
```

| Argument | Meaning |
|---|---|
| `<name>` | Executable name and CTest prefix. Use `dg-<module>-tests`. |
| `SOURCES` | Test sources, relative to `tests/`. `support/TestEnvironment.cpp` is added automatically. |
| `DEPS` | Engine libraries to link, for example `dg::core`, `dg::math`, `dg::sim`. |
| `LABELS` | Extra CTest labels. `unit` is always added. Add `determinism` for anything that checks bit-exact results. |

`dg_add_test` links `GTest::gtest` and `GTest::gtest_main` (so you do not write `main`), adds `tests/` to the include
path, applies `dg_configure_target` (the same warnings and strict floating-point flags as engine code), and registers
the tests with `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`. PRE_TEST discovery lists the tests when CTest
runs, not at build time, so sanitizer and cross-compiled builds still build even when the binary cannot run on the
build host.

Every new sim system also needs a replay-based determinism test (CLAUDE.md, *Build & test*). Until replays exist,
follow [SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp): run a scripted workload twice, check that the
hashes agree, check that a different seed or input changes the hash, and compare with a golden value.

### Write a death test

A **death test** checks that a `DG_ASSERT` fires for a programmer error. `DG_ASSERT` calls `std::abort` after logging
`Assertion failed: <expression> — <message> (...)` to stderr, and is compiled out in Release. The logger drains its
asynchronous queue before the abort, so the message always reaches the death test's stderr matcher. Follow these
conventions, which every `DG_ASSERT` death test uses:

* Wrap death tests in `#if DG_ASSERTS_ENABLED`. Where Release has defined fallback behaviour, test it under
  `#if !DG_ASSERTS_ENABLED` (see `UncheckedAllocators` in [MemoryTest.cpp](tests/core/MemoryTest.cpp)).
* Put them in a suite named after the type plus `Death` (`PoolAllocatorDeath`, `FixedDeath`), so that
  `ctest -R Death` or `--gtest_filter='*Death.*'` selects all of them.
* Start each test with `GTEST_FLAG_SET(death_test_style, "threadsafe");`. The statement then runs in a freshly
  re-executed child process, so allocations it makes before dying never reach the parent. The only exceptions are the
  two `AsyncLogDeath.ForkedChild...` tests in [LogTest.cpp](tests/core/LogTest.cpp), which use the `fast` style on
  purpose because they test what happens to the logger in a child forked without re-executing.
* Match the specific assert text, not just any crash, so the test only passes when the intended contract fired.
* Put multi-statement misuse in a lambda. `EXPECT_DEATH` is a macro, and commas in the statement would split its
  arguments.

This test from [MemoryTest.cpp](tests/core/MemoryTest.cpp) shows all five rules. `PoolAllocator` has its own
header, so a new file needs all three includes:

```cpp
#include <gtest/gtest.h>

#include "dg/core/Memory.h"
#include "dg/core/PoolAllocator.h"

#if DG_ASSERTS_ENABLED
TEST(PoolAllocatorDeath, RejectsEmptyBlocks) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        const dg::PoolAllocator pool(0, 16, 4, heap);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*pool blocks and chunks must be non-empty");
}
#endif
```

### Windows test environment

On Windows, `abort()` and CRT assertions open a modal "Debug Error" dialog in Debug builds. Nobody can click it on a
CI runner, so a death test would hang the job. [TestEnvironment.cpp](tests/support/TestEnvironment.cpp) is linked into
every test executable and, from a static initializer, calls `SetErrorMode(SEM_FAILCRITICALERRORS |
SEM_NOGPFAULTERRORBOX)`, clears the abort message and fault-report flags with `_set_abort_behavior`, and redirects
`_CRT_WARN`, `_CRT_ERROR` and `_CRT_ASSERT` reports to stderr. The file compiles to nothing on other platforms. Keep
platform code in tests limited to this file.

### Re-record a golden hash

A golden mismatch means the code no longer produces the same bits. Decide which case you are in before touching the
value:

* **Unintended change** (a refactor, a compiler flag, a new platform): this is a determinism bug. Fix the code, do
  not re-record. A mismatch on one platform only is always this case.
* **Intended change** (you changed the sim, the handle reuse rules, a `detmath` algorithm, or an operation's
  evaluation order on purpose): re-record.

To re-record:

1. Make the change on macOS ARM64, the reference platform.
2. Run the suite, for example `./dg test debug -L determinism`. The failure message and the log line print the new
   value.
3. Replace the `kGolden` (or `kGoldenHash`) constant in the test file.
4. Confirm the new value in Debug, Development and Release, and with `./dg determinism`.
5. Update the golden table in `docs/IMPLEMENTATION_PLAN.md`, the table on this page,
   and the page of the system that changed.
6. Say in the commit message that the golden value changed and why. If the cause is a change to the hash
   composition, an `InputCommand` schema or the snapshot format, that also changes the network protocol: bump the
   protocol version and mention it in the commit message (CLAUDE.md, *Multiplayer & headless*).

CI then checks the new value on Windows x64 and Linux x64.

### Run under sanitizers

Two CMake options add sanitizers to every target ([cmake/DgOptions.cmake](cmake/DgOptions.cmake)):

| Option | Flags | Platforms |
|---|---|---|
| `DG_ENABLE_ASAN` | `-fsanitize=address -fno-omit-frame-pointer` (MSVC: `/fsanitize=address`) | All desktop compilers |
| `DG_ENABLE_UBSAN` | `-fsanitize=undefined -fno-sanitize-recover=undefined` | Clang and GCC; ignored with MSVC |

UBSan does not recover, so the first undefined behaviour aborts the test and fails it. Run the suites with both:

```bash
./dg test debug -D DG_ENABLE_ASAN=ON -D DG_ENABLE_UBSAN=ON
./dg test debug -D DG_ENABLE_ASAN=OFF -D DG_ENABLE_UBSAN=OFF   # back to a normal build
```

`-D` values are stored in the build tree's CMake cache, and `dg` reconfigures when they change. The sanitizer setting
therefore stays on for that preset until you turn it off. CI runs the sanitizer build on Linux x64 for every push.

> **Note:** [cmake/DgConfigs.cmake](cmake/DgConfigs.cmake) pins Xcode's libc++ hardening to `none`. Xcode enables it
> at `-O0`, which changes libc++'s ABI tags and mixes incompatible code with the vcpkg-built libraries. ASan reported
> the mix as a heap overflow inside GoogleTest. Do not re-enable it.

## Testing

This page is about the test system itself. To check a change to it:

* After editing [tests/CMakeLists.txt](tests/CMakeLists.txt), run `./dg test debug` and `./dg test release`, then
  `ctest --test-dir build/<preset> -C Debug -N` to confirm that each test is listed under its full
  `<executable>.<Suite>.<Test>` name and that the totals match the table above.
* Check labels with `ctest --test-dir build/<preset> -C Debug -N -L determinism` and `-L smoke`.
* After changing [TestEnvironment.cpp](tests/support/TestEnvironment.cpp), the Windows x64 CI job is the only real
  check: the death tests must finish instead of timing out.
* When you add, remove or rename tests, update the suite table and counts on this page.
