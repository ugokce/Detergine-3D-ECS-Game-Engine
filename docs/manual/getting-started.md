---
title: Getting started
section: Start here
order: 20
status: implemented
summary: Install the toolchain, bootstrap vcpkg, build the engine, run each app, and run the tests on macOS, Windows or Linux.
sources: README.md, dg, dg.cmd, Launcher.command, Launcher.bat
---

## Overview

Every everyday task goes through one tool, **`dg`**, a dependency-free Python script ([dg.py](tools/dg.py)) that wraps
CMake presets. The same commands work on every host. On macOS and Linux you run it as `./dg`; on Windows you run
`dg` (the `dg.cmd` launcher). This page uses `./dg`.

The short version:

```bash
./dg setup --vcpkg    # check the toolchain and bootstrap the pinned vcpkg (once)
./dg run debug        # build and run the editor host in Debug
./dg test             # build and run every test in Development
```

The rest of this page explains each step. The full command reference is on [The dg developer tool](tooling/dg-tool.md).

## Requirements

### All hosts

| Tool | Version | Why |
|---|---|---|
| CMake | 3.28 or newer | The build system. `dg` refuses to run without it on `PATH`, and `dg setup` fails on an older version. |
| Python | 3.9 or newer | Runs `dg`, the launcher, and the documentation builder. |
| Git | any recent | Cloning vcpkg and the git hooks. |
| Ninja | optional | Faster builds. Without it, `dg` uses the IDE generator (Xcode or Visual Studio). |

### Per host

| Host | Compiler and extra tools |
|---|---|
| macOS (Apple Silicon only) | Xcode or the Xcode command line tools, and `pkg-config` (`brew install pkgconf`), which vcpkg needs. Optional: `brew install ninja`. `dg` stops with an error on an Intel Mac. |
| Windows 10/11 x64 | Visual Studio 2022 or 2026 with the **C++ Clang tools for Windows** component (it provides `clang-cl`). Optional: `winget install Ninja-build.Ninja`. |
| Linux x64 | Clang 19 or newer (older Clang cannot use libstdc++'s `std::expected`), Ninja and `pkg-config` (`sudo apt install ninja-build pkg-config`). Linux is mainly a headless-server and CI host: the editor host builds there, but Linux is a best-effort editor platform and has no IDE project generator. |

The engine has no other system dependencies today. Third-party libraries come from vcpkg; the manifest
`vcpkg.json` currently lists `gtest` (GoogleTest) and `spdlog` (the engine logger, built on `std::format` without the
fmt library).

## Set up the toolchain

```bash
./dg setup
```

`dg setup` prints one line per tool (`ok` or `MISSING`) and fails if a required tool is missing. Required tools are
`cmake` and `git` everywhere, plus `xcodebuild` and `pkg-config` on macOS, and `clang++` and `pkg-config` on Linux. A
missing Ninja is only reported, with the install command for your host. It also reports whether it found vcpkg.

### Bootstrap vcpkg

```bash
./dg setup --vcpkg
```

If no vcpkg is found, this clones Microsoft's vcpkg at the pinned release tag **2026.10.16** into `external/vcpkg` and
bootstraps it (with metrics disabled). It asks for confirmation first; add `--yes` (or `-y`) to skip the prompt, as CI
does. Every machine and CI runner therefore resolves the same package versions.

`dg` looks for vcpkg in this order:

1. The `VCPKG_ROOT` environment variable, if it points at a vcpkg checkout.
2. `external/vcpkg` inside the repository.

If you already use vcpkg elsewhere, set `VCPKG_ROOT` and skip `--vcpkg`. vcpkg is required: without it, configuring
fails because the engine logger needs spdlog (and the tests need GoogleTest), both from vcpkg. Turning tests off with
`-D DG_BUILD_TESTS=OFF` does not avoid it.

> **Tip:** After bootstrapping vcpkg into an existing build tree, reconfigure with `./dg generate` (or add `--fresh`
> to any command) so CMake picks up the vcpkg toolchain.

## Build

```bash
./dg build                 # everything, Development configuration
./dg build debug           # everything, Debug
./dg build release player  # only dg-player, Release
```

`dg build` configures the build tree first when it does not exist yet, then builds. Its words can come in any order:

* a **configuration**: `debug`, `dev` (or `development`), or `release`. The default is `dev`.
* an **app**: `editor` (`dg-editor`), `player` (`dg-player`), `server` (`dg-server`), or `tests` (`dg-sim-tests`
  only). Without an app, every target is built, including all test suites.

| Configuration | What it is |
|---|---|
| Debug | No optimization, asserts on, full debug info. |
| Development | Optimized with debug info and asserts. The default daily configuration. |
| Release | Fully optimized with link-time optimization where supported, no asserts. Used for shipping builds. |

The determinism compile flags are the same in all three, so Debug and Release builds compute identical simulations.

### Presets and build trees

`dg` picks a CMake preset for your machine unless you pass `--preset`:

| Host | Default preset | When |
|---|---|---|
| macOS | `macos-arm64` | Ninja is installed. |
| macOS | `macos-arm64-xcode` | No Ninja, or `--ide`. |
| Windows | `windows-x64` | Ninja and a Visual Studio with C++ tools are installed (Ninja + `clang-cl`). |
| Windows | `windows-x64-vs2022` / `windows-x64-vs2026` | No Ninja, or `--ide`. The year matches the newest installed Visual Studio. |
| Linux | `linux-x64` | Always. `linux-x64-server` is the headless profile (`DG_HEADLESS=ON`). |

If a build tree already exists for one of your host's presets, `dg` reuses it, so `./dg build` after
`./dg generate --ide` builds the IDE tree. Run `./dg presets` to list all presets and the default for this machine.

Every preset uses a multi-config generator, so one tree holds Debug, Development and Release. Binaries land in
`build/<preset>/bin/<Config>/`, for example `build/macos-arm64/bin/Development/dg-player`.

On the `windows-x64` preset, `dg` loads the Visual Studio developer environment (`vcvars64.bat`) for you, so you can
build from a normal terminal.

### Build options

Pass engine CMake options with `-D NAME=VALUE`. The tree is reconfigured only when a value differs from the cached one.

| Option | Default | Effect |
|---|---|---|
| `DG_BUILD_EDITOR` | `ON` | Build `dg-editor`. Forced off on Android, iOS and in headless builds. |
| `DG_BUILD_TESTS` | `ON` | Build the GoogleTest suites and smoke tests. Off in the `android-arm64` preset. |
| `DG_HEADLESS` | `OFF` | Headless profile: no renderer, audio, UI or editor. |
| `DG_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors. CI turns it on. |
| `DG_ENABLE_ASAN` | `OFF` | AddressSanitizer. |
| `DG_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer, aborting on the first error (not on MSVC). |

Other options that work on every command: `-j N` (parallel jobs), `--verbose` (full compiler command lines), and
`--fresh` (discard the CMake cache and reconfigure). See [Build system](tooling/build-system.md) for the details.

## Run the apps

```bash
./dg run                    # build and run the default app (the editor host) in Development
./dg run debug player       # dg-player in Debug
./dg run player -- --headless --fast --ticks 1800
```

`dg run` builds the app first and then starts it. Everything after `--` goes to the app. The default app is the editor,
except on an Android preset (player) and on a preset whose name ends in `-server` (server).

| App | Binary | What it does today |
|---|---|---|
| Editor | `dg-editor` | Logs that the editor UI is not implemented yet, then runs the engine loop until Ctrl+C. |
| Player | `dg-player` | Runs the fixed-tick simulation loop with scripted inputs. |
| Server | `dg-server` | The same loop, always headless. No flag can turn presentation on. |

All three share one command line ([Application.h](engine/runtime/include/dg/runtime/Application.h)):

| Flag | Default | Effect |
|---|---|---|
| `--headless` | off (on for `dg-server`) | No window, renderer or audio. |
| `--fast` | off | Run ticks back to back instead of in real time. |
| `--ticks <n>` | `0` | Stop after `n` ticks. `0` runs until Ctrl+C or SIGTERM. |
| `--tick-rate <hz>` | `30` | Fixed simulation rate, 1 to 1000. |
| `--players <n>` | `2` | Number of scripted player slots, at least 1. |
| `--seed <n>` | `24301` (`0x5eed`) | Simulation seed, as a decimal number. |
| `--level <name>` | none | Level to load. Accepted but ignored with a warning until level loading exists. |
| `--log-level <name>` | `info` | Log threshold: `trace`, `debug`, `info`, `warn`, `error`, `fatal` or `off`. |
| `--log-file <path>` | none | Also write the log to this file (truncated at start). |
| `--version` | | Print the version, configuration, platform, architecture and compiler, then exit. |
| `--help`, `-h` | | Print the usage, then exit. |

An invalid argument, or a `--log-file` that cannot be opened, prints an error and exits with code 2. In real-time mode the app logs the state hash every five
seconds of simulated time. Every run ends with a line like this:

```text
[18:14:35.423] [info] final tick=1800 state-hash=0x9f504fe2ce4e5ee2 elapsed=0.003s
```

Each log line starts with the local time and the level. With `--log-level warn` or higher, the final line is
printed without that prefix, so the hash is always on stdout.

That is the state hash of the simulation after 1800 ticks with the default seed and two players. It must be the same
on every machine and in every configuration. See [Runtime and apps](systems/runtime-and-apps.md) for how the apps work.

## Run the tests

```bash
./dg test                  # build, then run every test (Development)
./dg test debug            # in Debug
./dg test -L determinism   # only tests with the "determinism" label
./dg test -L smoke         # only the app smoke tests
./dg determinism           # Debug vs Release: the final state hashes must be identical
```

`dg test` builds everything in the chosen configuration and then runs `ctest --output-on-failure`. Each GoogleTest
case is its own CTest test, named `<suite>.<TestSuite>.<Test>`.

| Label | Tests |
|---|---|
| `unit` | Every GoogleTest case in the five suites: `dg-core-tests`, `dg-fixed-tests`, `dg-detmath-tests`, `dg-vector-tests`, `dg-sim-tests`. |
| `determinism` | The same five suites, which include the golden-hash checks. |
| `smoke` | `player.smoke`, `server.smoke`, `editor.smoke`: each app runs 120 ticks with `--fast` and must exit cleanly. |

`./dg determinism` builds `dg-player` in Debug and Release, runs each with `--headless --fast --ticks 1800` (change the
count with `--ticks N`), and fails with `DESYNC` unless both print the same `state-hash`.

The golden hashes and why they matter are explained in [Determinism model](architecture/determinism.md). The test
layout is described in [Testing](tooling/testing.md).

## Open the project in an IDE

```bash
./dg generate --ide
```

This configures the IDE preset and opens the generated project: an Xcode project on macOS (`macos-arm64-xcode`) or a
Visual Studio solution on Windows (`windows-x64-vs2022` or `windows-x64-vs2026`). Each project has the Debug,
Development and Release configurations, and the apps get Xcode schemes. Linux has no IDE generator; open the repository
in an editor with CMake support and use the `linux-x64` preset.

Editors with CMake Tools support (VS Code, CLion, Visual Studio) can also use the configure, build and test presets in
`CMakePresets.json` directly, for example `macos-arm64-dev`.

## Use the launcher

```bash
./dg gui
```

The **Detergine Launcher** is a small GUI over `dg`: preset, configuration, target, build options, run options and
test label, with buttons for every action and live build output. You can also double-click
`Launcher.command` on macOS or `Launcher.bat` on Windows. Every button runs the
equivalent `dg` command and shows it, so the GUI never behaves differently from the command line.

The launcher needs tkinter. With Homebrew Python on macOS, run `brew install python-tk`; on Linux, run
`sudo apt install python3-tk`. See [Detergine Launcher](tooling/launcher.md).

## Install the git hook

```bash
./dg hooks install     # add the pre-push hook
./dg hooks             # show whether it is installed
./dg hooks uninstall   # remove it
```

The pre-push hook runs `dg docs --check` and then `dg test dev -L unit` before anything leaves your machine. Bypass it
once with `git push --no-verify`. `dg hooks install` refuses to overwrite a pre-push hook it did not create. See
[Continuous integration and git hooks](tooling/ci.md).

## Build this manual

```bash
./dg docs           # build the HTML manual into build/docs/
./dg docs --open    # build it and open it in a browser
./dg docs --check   # validate pages, links and source coverage without writing anything
./dg docs --check --changed-since origin/main   # also require page updates for changed sources, as CI does
```

`--changed-since <ref>` fails when a source file listed in a page's `sources` changed since `<ref>` but that page did
not. If a change really needs no documentation, add a `Docs: none (<reason>)` line to the commit message. See
[Writing documentation](contributing/documentation.md).

## Clean up

```bash
./dg clean          # delete the build tree of the current preset
./dg clean --all    # delete the whole build/ directory
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `CMake not found on PATH (3.28+ required)` | Install CMake 3.28 or newer and make sure it is on `PATH`. |
| `macOS builds target Apple Silicon (arm64) only` | The engine does not support Intel Macs. |
| CMake cannot find `GTest` or `spdlog` when configuring | Run `./dg setup --vcpkg`, then `./dg generate --fresh`. |
| `Visual Studio 2022 with C++ tools not found (needed for clang-cl)` | Install Visual Studio with the C++ and Clang tools components, or use `./dg generate --ide`. |
| `no IDE generator for Linux` | Expected: use the `linux-x64` preset with your editor's CMake support. |
| `the launcher needs tkinter` | Install tkinter as described above. |
| `... was not produced (is 'editor' built for preset ...?)` | The app is disabled for that preset, for example the editor in a headless or Android build. |
