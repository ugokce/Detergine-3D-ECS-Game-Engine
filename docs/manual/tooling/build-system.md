---
title: Build system (CMake, presets, configurations, vcpkg)
section: Tooling
order: 10
status: implemented
summary: How the CMake build is organized, which configurations, options and presets exist, how vcpkg is wired in, and how to add a module, app or dependency.
sources: CMakeLists.txt, cmake/, CMakePresets.json, vcpkg.json, engine/CMakeLists.txt, apps/CMakeLists.txt
---

## Overview

Detergine builds with **CMake 3.28+**, usually through the Ninja Multi-Config generator, with third-party packages
from **vcpkg** in manifest mode. You rarely call CMake directly: the [dg developer tool](dg-tool.md) and the
[launcher](launcher.md) pick a preset for your machine and drive `cmake --preset`, `cmake --build` and `ctest` for you.
This page explains what those commands configure underneath.

The build has four layers:

| Layer | File | Responsibility |
|---|---|---|
| Root project | [CMakeLists.txt](CMakeLists.txt) | Declares configurations, loads vcpkg, includes the helper modules, sets output folders, adds `engine/`, `apps/` and (optionally) `tests/` |
| Helper modules | [cmake/](cmake/) | `DgVcpkg`, `DgOptions`, `DgConfigs`, `DgTargets`: toolchain, options, configurations, target helpers |
| Presets | [CMakePresets.json](CMakePresets.json) | One configure preset per platform/generator, plus build and test presets per configuration |
| Packages | [vcpkg.json](vcpkg.json) | The manifest: dependencies and the pinned `builtin-baseline` |

Key guarantees:

* **Every engine target gets the same strict floating-point flags in every configuration.** `dg_configure_target()`
  calls `dg_strict_fp()` for every module, app and test, so Debug and Release builds compute bit-identical simulation
  results (CLAUDE.md determinism rule 3). See [the determinism model](../architecture/determinism.md).
* **Three configurations everywhere**: `Debug`, `Development` and `Release`, on single- and multi-config generators.
* **One output layout**: executables land in `build/<preset>/bin/<Config>/`, static libraries in
  `build/<preset>/lib/<Config>/`.

## How to use

### The everyday path

Use the `dg` tool. It chooses the preset, configures on first use and builds:

```bash
./dg setup --vcpkg     # once: check the toolchain, clone the pinned vcpkg into external/vcpkg
./dg build             # configure (first time) and build everything in Development
./dg build release player
./dg test debug
```

### Plain CMake

Everything `dg` does is also available with plain CMake, because the presets carry all settings:

```bash
cmake --preset macos-arm64                     # configure build/macos-arm64
cmake --build --preset macos-arm64-dev         # build the Development configuration
ctest --preset macos-arm64-dev                 # run the tests (output on failure)
cmake --preset macos-arm64 -DDG_HEADLESS=ON    # change an option in the same build tree
```

`cmake --list-presets=all` (or `./dg presets`) lists the presets your host can use. Configure presets are filtered by
host OS through their `condition`, so you see only the macOS presets on a Mac, plus `android-arm64`, which has no
host condition.

### Configurations

| Configuration | `dg` word | Optimization | Asserts (`DG_ASSERT`) | Use |
|---|---|---|---|---|
| `Debug` | `debug` | none | on | Stepping through code |
| `Development` | `dev` (default) | `RelWithDebInfo` flags (optimized + debug info) | on | Daily work; the default |
| `Release` | `release` | full, plus LTO/IPO when the compiler supports it | off | Shipping builds |

Details that matter:

* `Development` is a custom configuration. [DgConfigs.cmake](cmake/DgConfigs.cmake) creates
  `CMAKE_<LANG>_FLAGS_DEVELOPMENT` and the linker flag variables by copying the `RelWithDebInfo` values. With CMake's
  default flags this defines `NDEBUG`, so the C `assert` macro is off; `DG_ASSERT` is controlled separately by
  `DG_ASSERTS_ENABLED` (see below).
* vcpkg packages ship only Debug and Release variants. `CMAKE_MAP_IMPORTED_CONFIG_DEVELOPMENT` is
  `RelWithDebInfo;Release;""`, so a Development build links the optimized package libraries.
* LTO: `check_ipo_supported()` runs once; when it succeeds, `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE` is `ON`.
* The MSVC runtime is the DLL runtime in all configurations (`MultiThreadedDLL`, `MultiThreadedDebugDLL` in Debug).
* With a single-config generator (for example `cmake -G Ninja` without a preset), `CMAKE_BUILD_TYPE` defaults to
  `Development`. Multi-config generators get `CMAKE_CONFIGURATION_TYPES=Debug;Development;Release`, set both by the
  `base` preset and by the root `CMakeLists.txt` before `project()`.

The `dg_core` module turns the configuration into compile definitions that all code can test
([engine/core/CMakeLists.txt](engine/core/CMakeLists.txt)):

| Definition | Value |
|---|---|
| `DG_VERSION_STRING` | `PROJECT_VERSION` (currently `"0.1.0"`) |
| `DG_BUILD_CONFIG` | `"Debug"`, `"Development"` or `"Release"` |
| `DG_CONFIG_DEBUG`, `DG_CONFIG_DEVELOPMENT`, `DG_CONFIG_RELEASE` | `1` for the active configuration, otherwise `0` |
| `DG_ASSERTS_ENABLED` | `1` except in Release |
| `DG_HEADLESS` | `1` when the `DG_HEADLESS` option is `ON` |

### Options

All options live in [DgOptions.cmake](cmake/DgOptions.cmake). Set them with `-D` on `cmake`, `./dg` or in the
launcher's *Build options* group.

| Option | Default | Effect |
|---|---|---|
| `DG_BUILD_EDITOR` | `ON` | Builds `dg-editor`. Forced off for Android, iOS and `DG_HEADLESS` builds |
| `DG_BUILD_TESTS` | `ON` | Adds `tests/` and enables CTest. `OFF` in the `android-arm64` preset |
| `DG_HEADLESS` | `OFF` | **Headless build profile**: no renderer, audio, UI or editor (DESIGN.md §16.1). Also defines `DG_HEADLESS=1` |
| `DG_WARNINGS_AS_ERRORS` | `OFF` | Sets `COMPILE_WARNING_AS_ERROR` on every engine target. CI turns it on |
| `DG_ENABLE_ASAN` | `OFF` | AddressSanitizer: `/fsanitize=address` on MSVC/clang-cl, `-fsanitize=address -fno-omit-frame-pointer` elsewhere |
| `DG_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer with `-fno-sanitize-recover=undefined` (aborts on the first error). Ignored on MSVC/clang-cl |

`DG_BUILD_SERVER` is not a cache option. It is a normal variable: `ON` on desktop hosts, `OFF` for Android and iOS. It
decides whether `dg-server` is built. Forcing `DG_BUILD_EDITOR` off also uses a normal variable that shadows the cache
entry, so turning `DG_HEADLESS` off again restores your own `DG_BUILD_EDITOR` choice.

At the end of configuration, `dg_print_summary()` prints one status line with the generator, configurations, compiler,
and the editor, server, headless, tests and vcpkg settings. Check it when a build does not contain what you expected.

### Presets

Configure presets (all write to `build/<presetName>/`):

| Preset | Host | Generator | Compiler | vcpkg triplet | Notes |
|---|---|---|---|---|---|
| `macos-arm64` | macOS | Ninja Multi-Config | AppleClang | `arm64-osx-dg` (overlay) | Default on a Mac with Ninja |
| `macos-arm64-xcode` | macOS | Xcode | AppleClang | `arm64-osx-dg` (overlay) | IDE project; default without Ninja |
| `windows-x64` | Windows | Ninja Multi-Config | `clang-cl` | `x64-windows-static-md` | Needs the Visual Studio developer environment (`dg` loads it) |
| `windows-x64-vs2022` | Windows | Visual Studio 17 2022 | ClangCL toolset | `x64-windows-static-md` | IDE project |
| `windows-x64-vs2026` | Windows | Visual Studio 18 2026 | ClangCL toolset | `x64-windows-static-md` | IDE project; needs a CMake that has this generator (4.2 or newer), above the 3.28 baseline |
| `linux-x64` | Linux | Ninja Multi-Config | `clang` / `clang++` | `x64-linux` | |
| `linux-x64-server` | Linux | Ninja Multi-Config | `clang` / `clang++` | `x64-linux` | Inherits `linux-x64`, sets `DG_HEADLESS=ON` |
| `android-arm64` | any | Ninja Multi-Config | NDK clang | `arm64-android` | Toolchain from `$ANDROID_NDK_HOME`; `ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-29`, `DG_BUILD_TESTS=OFF` |

Hidden base presets: `base` (binary dir, configuration list, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`), `ninja` (Ninja
Multi-Config, `CMAKE_DEFAULT_BUILD_TYPE=Development`), and `host-macos` / `host-windows` / `host-linux` (host
conditions). `host-macos` also sets `CMAKE_OSX_ARCHITECTURES=arm64`, `CMAKE_OSX_DEPLOYMENT_TARGET=14.0`,
`VCPKG_TARGET_TRIPLET=arm64-osx-dg` and `VCPKG_OVERLAY_TRIPLETS=${sourceDir}/cmake/triplets`.

Every configure preset except the hidden ones has build presets named `<preset>-debug`, `<preset>-dev` and
`<preset>-release` (for example `windows-x64-vs2026-dev`). Every desktop preset also has test presets with the same
names and `outputOnFailure` enabled. `android-arm64` has build presets only, because Android tests are not built.

### The overlay triplet

The stock `arm64-osx` vcpkg triplet targets the host's macOS version, so packages built on a newer Mac pull in
newer-OS symbols and make `ld` warn. [arm64-osx-dg.cmake](cmake/triplets/arm64-osx-dg.cmake) is the same triplet with
`VCPKG_OSX_DEPLOYMENT_TARGET 14.0`, matching the engine's own deployment target: arm64, dynamic CRT, static
libraries.

### vcpkg

[DgVcpkg.cmake](cmake/DgVcpkg.cmake) runs before `project()` so the toolchain applies:

1. It looks for `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`, then for `external/vcpkg/` (created by
   `dg setup --vcpkg`).
2. When it finds vcpkg, it sets `CMAKE_TOOLCHAIN_FILE` to vcpkg's toolchain. If a different toolchain was already set
   (the Android NDK one from the `android-arm64` preset), it is moved to `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`, so vcpkg
   loads it in turn.
3. It sets the internal cache variable `DG_USING_VCPKG` to `ON` or `OFF`. Without vcpkg no third-party package is available,
   so with the default `DG_BUILD_TESTS=ON` the configure step itself fails at `find_package(GTest CONFIG REQUIRED)`
   in [tests/CMakeLists.txt](tests/CMakeLists.txt). The project configures without vcpkg only with
   `-D DG_BUILD_TESTS=OFF`. Run `./dg setup --vcpkg` or set `VCPKG_ROOT` instead.

The manifest [vcpkg.json](vcpkg.json) currently lists one dependency, `gtest`, and pins
`builtin-baseline` `9eec002df1cd109f8f104cd966a763daf9751489`. `dg setup --vcpkg` clones vcpkg at the release tag
`VCPKG_RELEASE` in [dg.py](tools/dg.py) (currently `2026.10.16`), so every machine and CI runner resolves the same
port versions.

### Generator-specific settings

* **C++ module scanning is off** (`CMAKE_CXX_SCAN_FOR_MODULES OFF`). The engine does not use C++20 modules. With
  scanning on, Ninja + Clang would need `clang-scan-deps` and run an extra pass per source file.
* **Xcode libc++ hardening is disabled** (`CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_STANDARD_LIBRARY_HARDENING "none"`). Xcode
  turns on libc++ debug hardening at `-O0`, which changes libc++'s ABI tags. Debug objects would then mix inline
  `std::vector` code with the vcpkg libraries (built without hardening, like every Ninja build), which showed up as a
  heap overflow inside GoogleTest under ASan. Every build uses one mode.
* Xcode apps get `XCODE_GENERATE_SCHEME ON`, so each app appears as a runnable scheme.
* `USE_FOLDERS` is on. Targets are grouped into `engine`, `apps` and `tests` folders in Xcode and Visual Studio.
* `compile_commands.json` is always exported, for clangd and other tools.

## How to edit

### Where things live

| You want to change | Edit |
|---|---|
| Compiler flags or warnings for every target | `dg_configure_target()` in [DgTargets.cmake](cmake/DgTargets.cmake) |
| The strict FP flags | `dg_strict_fp()` in [DgTargets.cmake](cmake/DgTargets.cmake). Changing them is a determinism change: run the replay tests on x64 and ARM64 |
| A configuration's flags, LTO, the C++ standard | [DgConfigs.cmake](cmake/DgConfigs.cmake) |
| An option | [DgOptions.cmake](cmake/DgOptions.cmake) |
| How vcpkg is found | [DgVcpkg.cmake](cmake/DgVcpkg.cmake) |
| A platform or generator | [CMakePresets.json](CMakePresets.json) |
| Module build order | [engine/CMakeLists.txt](engine/CMakeLists.txt) |
| Which apps are built | [apps/CMakeLists.txt](apps/CMakeLists.txt) |

### Target helpers

| Function | Signature | What it does |
|---|---|---|
| `dg_add_module` | `dg_add_module(<name> [SIM] SOURCES ... [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])` | Creates static library `dg_<name>` with alias `dg::<name>`, public include dir `<module>/include`, IDE folder `engine`; calls `dg_configure_target`, and `dg_sim_target` when `SIM` is given |
| `dg_add_app` | `dg_add_app(<name> SOURCES ... [DEPS ...])` | Creates an executable linking `DEPS` privately, IDE folder `apps`, Xcode scheme; calls `dg_configure_target` |
| `dg_configure_target` | `dg_configure_target(<target>)` | `cxx_std_23` (public), strict FP, warnings, and `COMPILE_WARNING_AS_ERROR` when `DG_WARNINGS_AS_ERRORS` is on |
| `dg_strict_fp` | `dg_strict_fp(<target>)` | clang-cl: `/clang:-ffp-contract=off`; MSVC: `/fp:precise`; GCC/Clang: `-ffp-contract=off -fno-fast-math` |
| `dg_sim_target` | `dg_sim_target(<target>)` | Applies `dg_strict_fp` and sets the target property `DG_SIM_TARGET ON`, which marks sim code for tooling |
| `dg_print_summary` | `dg_print_summary()` | Prints the configuration summary line |

Warnings set by `dg_configure_target`:

| Compiler | Flags |
|---|---|
| MSVC and clang-cl | `/W4 /utf-8 /permissive-` |
| MSVC only | `/Zc:__cplusplus /Zc:preprocessor` |
| GCC/Clang | `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual` |

Strict FP is applied to every target, not only sim targets. An inline math function compiled with different FP flags in
two translation units is an ODR hazard: the linker may keep the FMA-contracted copy for sim code. On clang-cl only
`-ffp-contract=off` is passed, because adding `/fp:precise` makes clang-cl warn that one overrides the other, which is
an error under `DG_WARNINGS_AS_ERRORS`.

### Add an engine module

A module's `CMakeLists.txt` is one call:

```cmake
# Short comment: what the module is for (DESIGN.md section).
dg_add_module(<name>
    SOURCES
        src/Thing.cpp
    PUBLIC_DEPS
        dg::core)
```

1. Create `engine/<name>/` with `include/dg/<name>/` for public headers and `src/` for sources.
2. Add `engine/<name>/CMakeLists.txt` as above. Add `SIM` after the name when the module contains simulation code,
   as [engine/math](engine/math/CMakeLists.txt) and [engine/sim](engine/sim/CMakeLists.txt) do.
3. Add `add_subdirectory(<name>)` to [engine/CMakeLists.txt](engine/CMakeLists.txt) after every module it depends on.
   Lower layers never link higher ones, and sim modules never link presentation modules.
4. Add a test suite with `dg_add_test` in `tests/` (see [Testing](testing.md)) and a manual page in `systems/` with the
   new directory in its `sources`; `dg docs --check` fails until some page covers `engine/<name>/`.

### Add an app

1. Create `apps/<name>/Main.cpp`.
2. Add `dg_add_app(dg-<name> SOURCES <name>/Main.cpp DEPS dg::runtime)` to [apps/CMakeLists.txt](apps/CMakeLists.txt),
   inside an `if()` when it is not built everywhere (as `dg-server` and `dg-editor` are).
3. Make `dg` aware of it: add it to the `APPS` table in [dg.py](tools/dg.py), and to `TARGETS` / `RUN_APPS` in
   [dg_launcher.py](tools/dg_launcher.py). Add a smoke test in [tests/CMakeLists.txt](tests/CMakeLists.txt).

### Add an option

Add the `option()` to [DgOptions.cmake](cmake/DgOptions.cmake), then mirror it in `ENGINE_OPTIONS` in
[dg.py](tools/dg.py) and in `OPTION_DEFAULTS` in [dg_launcher.py](tools/dg_launcher.py), so the launcher shows a
checkbox for it. Update the options table on this page.

### Add a third-party dependency

1. Check the rules in CLAUDE.md: a permissive license (MIT, BSD, Zlib, Apache-2 or public domain), support on all target
   platforms, and a vcpkg port (or a vendored copy in `third_party/` with its LICENSE). No GPL or LGPL.
2. Add the port name to `dependencies` in [vcpkg.json](vcpkg.json).
3. In the module's `CMakeLists.txt`, call `find_package(<Pkg> CONFIG REQUIRED)` and link the imported target through
   `PUBLIC_DEPS` or `PRIVATE_DEPS`.
4. Add a line to DESIGN.md §2 and mention the dependency on the module's manual page.

To move every machine to newer port versions, bump `VCPKG_RELEASE` in [dg.py](tools/dg.py) and `builtin-baseline` in
[vcpkg.json](vcpkg.json) together. Existing `external/vcpkg/` clones are not updated automatically. Delete the folder
and run `./dg setup --vcpkg` again. The CI vcpkg cache key hashes `vcpkg.json` and `tools/dg.py`, so either change
invalidates it.

### Add a preset

Add a configure preset that inherits `ninja` or `base` and the matching `host-*` preset, then add the three build
presets and, if tests build on that platform, the three test presets. `dg presets` and the launcher read
`CMakePresets.json` directly, so the new preset appears there. To make `dg` choose it automatically, change
`default_preset()` in [dg.py](tools/dg.py). Update the preset table on this page.

## Reference

### Root `CMakeLists.txt` order

| Step | Why |
|---|---|
| `cmake_minimum_required(VERSION 3.28)` | Presets version 6 and the features used |
| Set `CMAKE_CONFIGURATION_TYPES` for multi-config generators | Must exist before `project()` |
| `include(DgVcpkg)` | The toolchain file must be set before `project()` |
| `project(Detergine VERSION 0.1.0 LANGUAGES C CXX)` | |
| `include(DgOptions)`, `include(DgConfigs)`, `include(DgTargets)` | Options, configurations, helpers |
| Output directories `bin/`, `bin/`, `lib/` | Runtime and shared libraries in `bin/`, archives in `lib/`; multi-config generators append `/<Config>` |
| `add_subdirectory(engine)`, `add_subdirectory(apps)` | |
| `if(DG_BUILD_TESTS)`: `enable_testing()`, `add_subdirectory(tests)` | |
| `dg_print_summary()` | |

### Build tree layout

| Path | Contents |
|---|---|
| `build/<preset>/CMakeCache.txt` | The configured cache. `dg` reads it to decide whether to reconfigure |
| `build/<preset>/bin/<Config>/` | `dg-editor`, `dg-player`, `dg-server`, test executables |
| `build/<preset>/lib/<Config>/` | Static libraries such as `dg_core` |
| `build/<preset>/compile_commands.json` | Compilation database (Ninja presets) |
| `build/<preset>/*.xcodeproj`, `*.sln` | IDE projects for the Xcode and Visual Studio presets |

## Testing

There is no separate test suite for the CMake code. It is tested by building:

* `./dg test debug`, `./dg test dev` and `./dg test release` configure and build every target, then run CTest. A
  configure or link error in the build system shows up here first.
* `./dg determinism` builds `dg-player` in Debug and Release and compares the final state hashes. If the strict FP flags
  ever differed between configurations, this is the check that fails.
* CI ([ci.yml](.github/workflows/ci.yml)) builds `macos-arm64`, `windows-x64` and `linux-x64` in all three
  configurations, with `DG_WARNINGS_AS_ERRORS=ON` for the Debug build, runs a sanitizer job on `linux-x64`, and
  builds `android-arm64` in Release. See [Continuous integration](ci.md).

After changing a helper in `cmake/`, run `./dg build --fresh` so the cache is rebuilt from scratch, and read the
`dg_print_summary()` line. After changing the presets, check `./dg presets` on each host you touched. Changes to
`dg_strict_fp()` must also pass `ctest -L determinism` on both x64 and ARM64.
