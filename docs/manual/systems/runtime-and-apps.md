---
title: Runtime and apps (dg-editor, dg-player, dg-server)
section: Systems
order: 80
status: partial
summary: The dg::runtime application host (shared command line, logging start-up, fixed-tick main loop, shutdown) and the three executables built on it.
sources: engine/runtime/, apps/
---

## Overview

`dg::runtime` is the application host. It parses the shared engine command line, starts the asynchronous
[engine logger](core-foundation.md#logging) with the app's `--log-level` and `--log-file` settings, runs the
fixed-tick main loop over the [simulation](simulation.md), handles Ctrl+C and `SIGTERM`, prints a machine-readable
final state hash, and drains the log before the process exits. The three executables in `apps/` are thin `main()`
functions on top of it:

| App | Source | Built when | Purpose today | Purpose when finished |
|---|---|---|---|---|
| `dg-player` | [player/Main.cpp](apps/player/Main.cpp) | Always | Runs the sim loop | Game runtime: window, renderer, audio, input |
| `dg-server` | [server/Main.cpp](apps/server/Main.cpp) | `DG_BUILD_SERVER` (desktop and Linux, not Android or iOS) | Runs the sim loop, always headless | Dedicated headless multiplayer server (DESIGN.md §15.7, §16.1) |
| `dg-editor` | [editor/Main.cpp](apps/editor/Main.cpp) | `DG_BUILD_EDITOR` (desktop, not in headless builds) | Placeholder: logs that the UI is missing, then runs the sim loop | ImGui editor (DESIGN.md §14) |

The runtime sits above the sim layer and below the apps. It depends on `dg::core` and `dg::sim` only, so it builds
headless. It is **partial**: there is no window, no renderer, no audio, no level loading and no networking yet, and
`--headless` currently changes only the logged configuration. Inputs come from `dg::sim::scriptedInput()` rather than
from devices or the network.

The loop is the simple form of the fixed-tick scheduler in `docs/DESIGN.md` §5.3: it steps the sim at a
fixed rate and never varies `dt`. Presentation interpolation, input polling and input exchange arrive with the
platform, renderer and [networking](../planned/networking.md) work.

## How to use

### Run an app

Build and run through the [dg tool](../tooling/dg-tool.md). Arguments after `--` go to the app:

```bash
./dg run dev player -- --headless --fast --ticks 1800
./dg run dev server -- --tick-rate 60
./dg run dev editor
./dg run dev player -- --help
```

Without an app name, `./dg run` picks `editor`, or `player` on Android presets, or `server` on `-server` presets. You
can also start a binary directly from `build/<preset>/bin/<Config>/`:

```bash
build/macos-arm64-xcode/bin/Debug/dg-player --headless --fast --ticks 1800
```

Output on a Debug macOS build. Every log line starts with the local time and the level:

```text
[18:14:35.420] [info] dg-player 0.1.0 (Debug, macos-arm64, AppleClang) headless=true tick-rate=30Hz players=2
[18:14:35.423] [info] final tick=1800 state-hash=0x9f504fe2ce4e5ee2 elapsed=0.003s
```

### Real-time and fast mode

By default the loop runs in **real time**: before stepping tick `n` (counted from 0), the loop sleeps on a steady
clock until `start + (n + 1) × (1 / tickRate)`, so the first tick runs one tick period after startup. If a tick runs late, the following ticks run back to back until the loop catches up; ticks
are never skipped. Every `tickRate × 5` ticks (5 seconds of sim time) it logs `tick <n> state-hash 0x<hash>`.

With `--fast` the loop runs ticks back to back with no sleeping and no periodic log line. Use it for tests, replays and
determinism checks. Wall-clock time affects only when a tick runs, never what it computes, so real-time and fast runs
with the same arguments reach the same state hash.

### Command-line flags

All three apps accept the same flags, defined in [Application.cpp](engine/runtime/src/Application.cpp):

| Flag | Value | Default | Effect |
|---|---|---|---|
| `--headless` | none | off (`dg-server`: on) | No window, renderer or audio. Today it only sets `headless=true` in the log line |
| `--fast` | none | off | Run ticks back to back instead of in real time |
| `--ticks <n>` | `u64` | `0` | Stop after `n` ticks. `0` runs until Ctrl+C or `SIGTERM` |
| `--tick-rate <hz>` | `u32`, 1 to 1000 | `30` | Fixed simulation rate |
| `--players <n>` | `u32`, at least 1 | `2` | Number of scripted player slots |
| `--seed <n>` | `u64` | `24301` (`0x5eed`) | Simulation seed |
| `--level <name>` | string | empty | Level to load. Not implemented: the app logs a warning and ignores it |
| `--log-level <name>` | `trace`, `debug`, `info`, `warn` (or `warning`), `error`, `fatal`, `off` | `info` | Log threshold; see [Logging flags](#logging-flags) |
| `--log-file <path>` | string | empty | Also write the log to this file, truncated at start |
| `--version` | none | | Print build information and exit |
| `--help`, `-h` | none | | Print the usage text and exit |

Numbers are parsed with `std::from_chars` in base 10. The whole value must be a number, so `--seed 0x5eed` and
`--ticks 10s` are rejected; write `--seed 24301`. Flags are processed left to right, and a later value overrides an
earlier one. `--help` and `--version` stop parsing as soon as they are reached.

`--version` prints one line: `<app> <version> (<config>, <platform>-<arch>, <compiler>)`, with `, headless build`
appended for `DG_HEADLESS` builds, for example `dg-player 0.1.0 (Debug, macos-arm64, AppleClang)`. `--help` and
`--version` print straight to stdout, not through the logger, so their output has no timestamp or level.

### Logging flags

`--log-level` sets the threshold of the [engine logger](core-foundation.md#log-levels-and-filtering). Level names are
case-sensitive: `--log-level INFO` is rejected with exit code 2. At `off` nothing is logged except an assertion
failure. `--log-file <path>` adds a file sink next to the console. The file is truncated at start, and its lines also
carry the date and the id of the logging thread:

```bash
build/macos-arm64-xcode/bin/Debug/dg-player --fast --ticks 1800 --log-level debug --log-file player.log
```

```text
[2026-11-05 18:14:35.456] [info] [thread 848302] dg-player 0.1.0 (Debug, macos-arm64, AppleClang) headless=false tick-rate=30Hz players=2
```

If the file cannot be opened, the app logs `[<time>] [error] cannot open log file '<path>': <reason>` and exits with
code 2 before the loop starts. The message is written with `log::writeUnfiltered()`, so it appears even with
`--log-level fatal` or `off`.

The order of events in `runMain()` (and in the `dg-editor` `main()`) is:

1. `parseCommandLine()`. A parse error is logged with `log::error` and the app returns 2. The logger is not configured
   yet at this point, so the message uses the logger's lazy default (console, `info`), whatever `--log-level` says.
2. `startLogging(config)`, which calls `log::init()` with `config.logLevel` and `config.logFile`. On failure the app
   calls `log::shutdown()` and returns 2.
3. `run(config)`.
4. `log::shutdown()`, which drains the queue and joins the logger's worker thread before `main()` returns. The
   logger's own `atexit` hook is only a safety net.

### Exit codes

| Code | When |
|---|---|
| `0` | The loop finished (`--ticks` reached, or Ctrl+C or `SIGTERM`), or `--help` / `--version` was printed |
| `2` | Invalid command line: unknown argument, missing value, malformed number, unknown `--log-level`, `--tick-rate` outside 1 to 1000, or `--players 0`. The message goes to stderr as `[<time>] [error] <app>: <message>`. Also returned when the `--log-file` cannot be opened |

A stop signal is not an error: the loop finishes the current tick, prints the final line and returns 0. The handler
only sets a flag that the loop checks before each tick. In real-time mode a signal that arrives while the loop is
sleeping does not wake it early: the sleep runs to its deadline and that tick is still stepped before the loop exits.
The final tick count after Ctrl+C can therefore be one higher than you expect, which matters when you compare hashes
from interrupted runs. Use `--ticks` for runs whose hashes you compare.

> **Note:** `SIGTERM` handling has only been checked on POSIX hosts (macOS). Windows does not raise `SIGTERM` when a
> process is terminated normally, so on Windows only Ctrl+C (`SIGINT`) reaches the handler. Windows behaviour has not
> been verified yet.

### The final state-hash line

Every run ends with exactly one line on stdout. Normally it is an `Info` log line:

```text
[<HH:MM:SS.mmm>] [info] final tick=<n> state-hash=0x<16 hex digits> elapsed=<seconds>s
```

With `--log-level warn`, `error`, `fatal` or `off`, where `Info` is filtered out, `run()` calls `log::flush()` and then
writes the same line without the time and level prefix straight to stdout, so the hash is printed at every log level:

```text
final tick=<n> state-hash=0x<16 hex digits> elapsed=<seconds>s
```

`tick` is the number of ticks stepped, `state-hash` is `Simulation::stateHash()` in lowercase hex, and `elapsed` is
wall-clock seconds with three decimals. `dg determinism` reads `state-hash=(0x[0-9a-f]+)` from this line to compare
Debug and Release builds with a regular-expression search, so the prefix does not matter but the
`final tick=... state-hash=0x...` part is an interface: do not change it without updating [dg.py](tools/dg.py).

### Write your own host

`runMain()` is the whole body of an app's `main()`. Pass your defaults in an `AppConfig`; command-line flags override
them:

```cpp
#include "dg/runtime/Application.h"

int main(int argc, char** argv) {
    return dg::runtime::runMain(argc, argv, {.appName = "my-sim", .fast = true, .tickRate = 60, .players = 4});
}
```

When you need a step between parsing and running, as `dg-editor` does, call `parseCommandLine()`, `startLogging()`,
`run()` and `log::shutdown()` yourself, in that order:

```cpp
#include "dg/core/Log.h"
#include "dg/runtime/Application.h"

int main(int argc, char** argv) {
    dg::runtime::AppConfig config{.appName = "my-tool", .headless = true};
    auto outcome = dg::runtime::parseCommandLine(argc, argv, config);
    if (!outcome) {
        dg::log::error("my-tool: {}", outcome.error());
        return 2;
    }
    if (*outcome == dg::runtime::ParseOutcome::ExitSuccess) {
        return 0; // --help or --version was printed
    }
    if (config.maxTicks == 0) {
        config.maxTicks = 300;
    }
    if (!dg::runtime::startLogging(config)) {
        dg::log::shutdown();
        return 2; // --log-file could not be opened; the reason has been logged
    }
    const int exitCode = dg::runtime::run(config);
    dg::log::shutdown(); // drain the log before main returns
    return exitCode;
}
```

Add the executable in [apps/CMakeLists.txt](apps/CMakeLists.txt) with
`dg_add_app(<name> SOURCES <dir>/Main.cpp DEPS dg::runtime)`.

## How to edit

| File | Contents |
|---|---|
| [Application.h](engine/runtime/include/dg/runtime/Application.h) | `AppConfig`, `ParseOutcome`, `parseCommandLine()`, `startLogging()`, `run()`, `runMain()` |
| [Application.cpp](engine/runtime/src/Application.cpp) | Flag parsing, usage text, build-info line, logging start-up, signal handling, the main loop, the final line |
| [engine/runtime/CMakeLists.txt](engine/runtime/CMakeLists.txt) | `dg_add_module(runtime ...)`, public deps `dg::core` and `dg::sim` |
| [apps/CMakeLists.txt](apps/CMakeLists.txt) | The three `dg_add_app()` targets and their build conditions |
| [apps/](apps/) | One `Main.cpp` per app |

### Invariants

* **The runtime is not sim code, but it feeds the sim.** It may use clocks, signals and threads. It must pass the
  sim nothing except `InputCommand`s, and wall-clock time must never reach `Simulation::step()`.
* **`dg-server` is always headless.** Its default is `headless = true`, and no flag turns presentation on. Keep it that
  way when presentation modules arrive: the server must link no renderer, audio or UI code.
* **The final line format is stable.** `dg determinism` and CI parse it, and it must reach stdout at every
  `--log-level`.
* **Logging is started after parsing and shut down before `main()` returns.** Every app's `main()` (through
  `runMain()` or by hand, as in `dg-editor`) calls `startLogging()` before `run()` and `log::shutdown()` after it, and
  exits with code 2 when `startLogging()` fails. Use `dg::log` for app output; only `--help`, `--version` and the
  bare final line write to stdout directly.
* **Apps stay thin.** Behaviour shared by all apps belongs in `dg::runtime`, not in a `Main.cpp`.

### Add a command-line flag

1. Add a field with its default to `AppConfig` in [Application.h](engine/runtime/include/dg/runtime/Application.h).
2. Parse it in `parseCommandLine()`. Use the `number(...)` helper for numeric values and `value()` for strings, so
   missing and malformed values produce the standard error messages and exit code 2.
3. Validate ranges after the loop, next to the `--tick-rate` and `--players` checks.
4. Add the flag to `printUsage()`.
5. Add it to the flag table on this page. If `dg` or the launcher passes it, update
   [The dg developer tool](../tooling/dg-tool.md) or [Detergine Launcher](../tooling/launcher.md) too.

### Change the loop

The loop rebuilds every player's input with `scriptedInput()` before each `step()`. When real input lands, replace
that with the input pipeline (local devices, then networked confirmed inputs) without changing the tick schedule. A
change that alters what the sim receives changes the final hash of the smoke runs and of `dg determinism`. Any change
to the sim itself is covered in [Simulation](simulation.md#how-to-edit).

## Reference

### `dg::runtime::AppConfig`

| Field | Type | Default | Set by |
|---|---|---|---|
| `appName` | `std::string_view` | `"dg"` | The app's `main()` |
| `headless` | `bool` | `false` | `--headless` |
| `fast` | `bool` | `false` | `--fast` |
| `maxTicks` | `u64` | `0` (run until interrupted) | `--ticks` |
| `tickRate` | `u32` | `30` | `--tick-rate` |
| `players` | `u32` | `2` | `--players` |
| `seed` | `u64` | `0x5eed` | `--seed` |
| `level` | `std::string` | empty | `--level` |
| `logLevel` | `log::Level` | `log::Level::Info` | `--log-level` |
| `logFile` | `std::string` | empty (no file) | `--log-file` |

`run()` builds the simulation from `tickRate` and `seed` only, so the placeholder body count is always the
`SimConfig` default of 64.

### Functions

| Function | Description |
|---|---|
| `std::expected<ParseOutcome, std::string> parseCommandLine(int argc, char** argv, AppConfig& config)` | Applies the flags to `config`. Returns `ParseOutcome::Run`, `ParseOutcome::ExitSuccess` (after `--help` or `--version`), or an error message |
| `std::expected<void, std::string> startLogging(const AppConfig& config)` | `log::init({.level = config.logLevel, .filePath = config.logFile})`. Returns the error (already logged) when the log file cannot be opened |
| `int run(const AppConfig& config)` | Logs the configuration, installs `SIGINT`/`SIGTERM` handlers, runs the loop, prints the final line, returns 0 |
| `int runMain(int argc, char** argv, AppConfig defaults)` | `parseCommandLine()`, `startLogging()`, `run()`, then `log::shutdown()`. Returns 2 on a parse error or a `startLogging()` failure, 0 on `ExitSuccess`, otherwise the result of `run()` |

### App defaults

| App | `AppConfig` passed by `main()` | Extra behaviour |
|---|---|---|
| `dg-player` | `{.appName = "dg-player"}` | None |
| `dg-server` | `{.appName = "dg-server", .headless = true}` | None |
| `dg-editor` | `{.appName = "dg-editor"}` | Its own `main()`: after `startLogging()` it logs `dg-editor: the editor UI is not implemented yet; running the engine loop (Ctrl+C to quit)`, then calls `run()` and `log::shutdown()` |

## Testing

[tests/CMakeLists.txt](tests/CMakeLists.txt) registers one smoke test per app, labelled `smoke`. Each boots the app,
runs 120 ticks in fast mode and must exit with code 0:

| Test | Command | Registered when |
|---|---|---|
| `player.smoke` | `dg-player --headless --fast --ticks 120` | Always |
| `server.smoke` | `dg-server --fast --ticks 120` | `dg-server` is built |
| `editor.smoke` | `dg-editor --fast --ticks 120` | `dg-editor` is built |

```bash
./dg test dev -L smoke   # the app smoke tests
./dg determinism         # dg-player in Debug and Release, 1800 ticks, final hashes must match
```

`dg determinism` builds `dg-player` in Debug and Release, runs `--headless --fast --ticks 1800` (change the count with
`--ticks`), and fails with `DESYNC` when the two final hashes differ. CI runs it on every host after the test suites.
There are no unit tests for `parseCommandLine()` or `startLogging()` yet; check flag changes by hand with `--help`
and a few invalid values. For the logging flags, check that `--log-level warn` still prints the bare final line, that
`--log-file /nonexistent_dir/a.log` exits with code 2, and that `--log-file <path>` writes the file. The logger itself
is covered by the `AsyncLog` tests in `dg-core-tests` (see [Core foundation](core-foundation.md#testing)). See [Testing](../tooling/testing.md) and [Continuous integration](../tooling/ci.md).
