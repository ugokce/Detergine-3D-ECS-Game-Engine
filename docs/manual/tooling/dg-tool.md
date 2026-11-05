---
title: The dg developer tool
section: Tooling
order: 20
status: implemented
summary: Every dg command and option for setting up, configuring, building, running, testing and documenting the engine, and how dg picks presets and reconfigures.
sources: tools/dg.py, dg, dg.cmd
---

## Overview

`dg` is the engine's command-line front end. It is one Python script, [tools/dg.py](tools/dg.py), that wraps
CMake, CTest, vcpkg and git so you can generate projects, build, run and test with the same command on Windows,
macOS and Linux. It uses only the Python standard library and needs Python 3.9 or newer.

You start it through a launcher script in the repository root:

| Host | Command | Launcher |
|---|---|---|
| macOS, Linux | `./dg <command>` | [dg](dg): `exec python3 "$(dirname "$0")/tools/dg.py" "$@"`, so it works from any directory |
| Windows | `dg <command>` | [dg.cmd](dg.cmd): `py -3` when the Python launcher exists, otherwise `python` |

`python3 tools/dg.py <command>` works too; CI calls it that way. The [launcher GUI](launcher.md) runs the same script,
so the GUI, the command line and CI always do the same thing.

`dg` adds no build logic of its own. Everything it does is a `cmake --preset`, `cmake --build`, `ctest` or `git`
command, printed as `[dg] $ ...` before it runs, so you can always repeat a step by hand. The CMake side is described
in [Build system](build-system.md).

## How to use

### First build

```bash
./dg setup --vcpkg     # check the toolchain and clone the pinned vcpkg into external/vcpkg (once)
./dg run               # build and run the editor in Development
./dg test              # build everything and run all tests
```

### Common tasks

```bash
./dg build                       # build everything, Development
./dg build release player        # build only dg-player, Release
./dg run debug player -- --headless --ticks 300   # arguments after -- go to the app
./dg test -L determinism         # only the determinism-labelled tests
./dg build -D DG_HEADLESS=ON     # change an option; dg reconfigures because the value changed
./dg generate --ide              # create and open an Xcode or Visual Studio project
./dg determinism                 # Debug vs Release state hash comparison
./dg docs --open                 # build the manual and open it
```

### Words: configuration and app

`build`, `run` and `test` take free words in any order. `test` accepts only a configuration.

| Word | Meaning |
|---|---|
| `debug` | `Debug` |
| `dev`, `development` | `Development` (the default when no configuration word is given) |
| `release` | `Release` |
| `editor` | target `dg-editor` |
| `player` | target `dg-player` |
| `server` | target `dg-server` |
| `tests` | target `dg-sim-tests` |

Words are case-insensitive. Any other word is an error.

### Commands

| Command | What it runs |
|---|---|
| `dg setup [--vcpkg] [--yes]` | Checks the toolchain. With `--vcpkg`, clones and bootstraps vcpkg when none is found |
| `dg generate [--ide] [--preset P]` | `cmake --preset P` for this host's default preset; `--ide` picks the IDE preset and opens the project |
| `dg build [config] [app]` | Configures if needed, then `cmake --build` everything or one app |
| `dg run [config] [app] [-- args]` | Builds the app, then runs it with `args`; exits with the app's exit code |
| `dg test [config] [-L label]` | Builds everything, then `ctest --output-on-failure`, optionally filtered by label |
| `dg determinism [--ticks N]` | Builds and runs `dg-player` in Debug and Release and compares the state hashes |
| `dg clean [--all]` | Deletes `build/<preset>/`, or the whole `build/` with `--all` |
| `dg presets` | `cmake --list-presets=all`, then the default presets for this machine |
| `dg gui` | Opens the [Detergine Launcher](launcher.md) |
| `dg hooks [install\|uninstall\|status]` | Manages the git pre-push hook (default action: `status`) |
| `dg docs [--open] [--check] [--changed-since REF]` | Builds or validates the HTML manual |

#### setup

`dg setup` prints one line per tool with `ok` or `MISSING` and the path it found:

| Host | Tools checked | Required |
|---|---|---|
| all | `cmake` (version must be 3.28+), `git`, vcpkg | `cmake`, `git` |
| macOS | `ninja`, `xcodebuild`, `pkg-config` | `xcodebuild`, `pkg-config` |
| Windows | `ninja`, `clang-cl` | none beyond `cmake`, `git` |
| Linux | `ninja`, `clang++`, `pkg-config` | `clang++`, `pkg-config` |

vcpkg counts as found when `VCPKG_ROOT` is set or `external/vcpkg/` exists. When Ninja is missing, `setup` prints the
install command for your host (`brew install ninja`, `winget install Ninja-build.Ninja`, `sudo apt install
ninja-build`). The command fails when a required tool is missing.

With `--vcpkg` and no vcpkg found, `setup` asks for confirmation (skip it with `--yes` or `-y`), then runs
`git clone --depth 1 --branch 2026.10.16 https://github.com/microsoft/vcpkg.git external/vcpkg` and the bootstrap
script with `-disableMetrics`. The tag is `VCPKG_RELEASE` in `dg.py`. Run `dg generate` (or any build) afterwards so
CMake picks vcpkg up.

#### generate

`dg generate` configures `build/<preset>/`. Without `--preset` it uses the host default; with `--ide` it uses the
default IDE preset (`macos-arm64-xcode` or `windows-x64-vs<year>`; Linux has none and fails). After an `--ide`
configure, it opens the first `*.xcodeproj` or `*.sln` in the build tree with `open` (macOS) or the Windows file
association.

#### build and run

`dg build` runs:

```bash
cmake --build build/<preset> --config <Config> --parallel [N] [--target <app>] [--verbose]
```

For `-xcode` presets without `--verbose`, it appends `-- -quiet` because `xcodebuild` otherwise prints every step.

`dg run` chooses the app when you name none: `player` for `android-*` presets and `editor` otherwise. On presets
ending in `-server`, `editor` becomes `server`, whether it was the default or you named it. `dg run` builds only that
target, then runs `build/<preset>/bin/<Config>/<app>` (with `.exe` on Windows). If the target does not exist in the
build tree, for example `dg run` (editor) with `-D DG_HEADLESS=ON` on a desktop preset, where `dg-editor` is not
defined, the build step fails with an unknown-target error from Ninja, `xcodebuild` or MSBuild, and `dg` exits with
that exit code. If the build succeeds but no binary is at that path, `dg` fails with
`... was not produced (is '<app>' built for preset <preset>?)`. The app's command-line flags (`--headless`, `--fast`, `--ticks`,
`--tick-rate`, `--players`, `--seed`, `--level`, `--version`, `--help`) are described in
[Runtime and apps](../systems/runtime-and-apps.md).

#### test

`dg test` builds every target in the configuration, then runs
`ctest --test-dir build/<preset> -C <Config> --output-on-failure [-L <label>]`. Labels in use are `unit`,
`determinism` and `smoke`; see [Testing](testing.md).

#### determinism

`dg determinism` builds `dg-player` in Debug and in Release, runs each as
`dg-player --headless --fast --ticks N` (`--ticks` defaults to `1800`), and reads the `state-hash=0x...` value from its
output. It prints both hashes and fails with `DESYNC` when they differ. This checks that the strict FP flags really
make optimized and unoptimized builds agree. It does not compare architectures; that needs runs on both x64 and ARM64.

#### clean

`dg clean` deletes the resolved preset's build tree. `dg clean --all` deletes `build/`, which also removes the built
manual in `build/docs/`. It never touches `external/vcpkg/`.

#### hooks

`dg hooks install` writes a `pre-push` hook into the directory from `git rev-parse --git-path hooks`. The hook runs
`python3 tools/dg.py docs --check` and then `python3 tools/dg.py test dev -L unit`; bypass it once with
`git push --no-verify`. The hook carries a marker comment (`HOOK_MARKER` in `dg.py`). `install` refuses to
overwrite a `pre-push` hook without that marker, and `uninstall` removes only a hook that has it. `dg hooks` alone
reports whether the hook is installed. See [Continuous integration and git hooks](ci.md).

#### docs

`dg docs` runs [build_docs.py](tools/docs/build_docs.py) and writes the site to `build/docs/`. `--open` opens
`build/docs/index.html` afterwards (`open`, the Windows file association, or `xdg-open`). `--check` validates the
manual without writing anything and exits with the checker's exit code; add `--changed-since REF` to also require
manual updates for changed source files. See [Writing documentation](../contributing/documentation.md).

### Options shared by every command

| Option | Default | Meaning |
|---|---|---|
| `--preset P` | chosen for this machine | CMake configure preset |
| `-D NAME=VALUE` | none | Set a CMake cache variable; repeatable |
| `-j N`, `--jobs N` | `0` (all cores) | Parallel build jobs |
| `--verbose` | off | Show full compiler command lines |
| `--fresh` | off | Discard the CMake cache and reconfigure (`cmake --fresh`) |
| `-- args...` | none | Everything after the first `--` is passed to the app by `dg run` |

All commands accept these options, even where they have no effect (`dg presets --verbose` is valid). `dg` checks that
`cmake` is on `PATH` before any command, including `dg docs`.

### How dg chooses a preset

When you pass no `--preset`, `dg` first reuses a build tree that already exists, then falls back to the host default:

1. **Existing tree.** It checks, in order, the default preset, the default IDE preset, and on Windows also
   `windows-x64-vs2022` and `windows-x64-vs2026`. The first one with a `build/<preset>/CMakeCache.txt` wins. This is
   why `dg build` after `dg generate --ide` builds the IDE project's tree. Linux checks only `linux-x64`.
2. **Host default.**

| Host | Default preset | Default IDE preset |
|---|---|---|
| macOS (arm64 only; Intel Macs are rejected) | `macos-arm64` when `ninja` is on `PATH`, otherwise `macos-arm64-xcode` | `macos-arm64-xcode` |
| Windows | `windows-x64` when `ninja` is on `PATH` and Visual Studio with C++ tools is installed, otherwise `windows-x64-vs<year>` | `windows-x64-vs<year>` |
| Linux | `linux-x64` | none |

`<year>` is `2022` or `2026`, read from the newest Visual Studio with the C++ x64 tools through `vswhere`
(`catalog_productLineVersion`). When it cannot be read, `dg` uses `2022`. `dg generate` does not reuse existing trees:
it always uses the host default (or the IDE default with `--ide`) unless you pass `--preset`.

Presets that are never chosen automatically, such as `linux-x64-server` and `android-arm64`, need `--preset`.

### Windows: loading the Visual Studio environment

The `windows-x64` preset uses Ninja with `clang-cl`, which needs the environment that a Visual Studio developer
prompt provides (include paths, libraries, linker). For that preset only, and only when `VCToolsInstallDir` is not
already set, `dg`:

1. Finds the newest Visual Studio installation with the component `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`
   through `vswhere.exe` in `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\`.
2. Runs `VC\Auxiliary\Build\vcvars64.bat` followed by `set`, and copies every variable into the environment of the
   `cmake` and `ctest` processes.

If no such installation exists it fails and suggests `--ide`. The Visual Studio presets do not need this, because
MSBuild sets up its own environment. Running `dg` from a developer prompt skips the step.

### -D and reconfiguring

`dg build`, `run`, `test` and `determinism` configure only when needed:

* There is no `build/<preset>/CMakeCache.txt` yet, or
* `--fresh` was given (once per invocation), or
* a `-D` value differs from the cached one. `dg` prints `options changed (NAME, ...); reconfiguring`.

Boolean values are normalized before the comparison: `1`, `ON`, `TRUE`, `YES` become `ON`, and `0`, `OFF`, `FALSE`,
`NO` become `OFF` (any case). Other values are compared case-insensitively. A configure passes every `-D` from the
command line, so a reconfigure keeps all of them.

`-D` values stay in the cache. `dg build -D DG_HEADLESS=ON` followed by a plain `dg build` still builds headless; pass
`-D DG_HEADLESS=OFF` or `--fresh` to go back. Edits to `CMakeLists.txt` files need no `dg` action: `cmake --build`
re-runs the configure step by itself.

## How to edit

The tool is one file, [tools/dg.py](tools/dg.py). Its layout:

| Part | Contents |
|---|---|
| Constants | `ROOT`, `BUILD_ROOT`, `VCPKG_RELEASE`, `CONFIGS`, `APPS`, `ENGINE_OPTIONS`, `OPTIONS`, `HOOK_MARKER`, `PRE_PUSH_HOOK` |
| Helpers | `info`, `fail`, `run_cmd`, `have`, `host_key`, `default_preset`, `existing_preset`, `resolve_preset`, `parse_words` |
| Windows | `find_vs`, `vs_product_line`, `build_env` |
| Build steps | `parse_defines`, `read_cache`, `configure`, `ensure_configured`, `build`, `binary_path` |
| Commands | one `cmd_<name>(args)` per command |
| Preset data | `host_presets()`, also used by the launcher |
| `main()` | argument parsing and dispatch |

Rules for changes:

* **The launcher imports `dg`.** [dg_launcher.py](tools/dg_launcher.py) uses `ROOT`, `IS_WINDOWS`, `ENGINE_OPTIONS`,
  `have`, `read_cache`, `host_presets`, `existing_preset`, `default_preset`, `vs_product_line` and `preset_dir`.
  Renaming any of them breaks the GUI.
* **CI runs `dg`.** [ci.yml](.github/workflows/ci.yml) calls `setup --vcpkg --yes`, `test`, `determinism` and
  `build`. Keep those commands and their output stable; `determinism` parses `state-hash=` from `dg-player`.
* **Keep it standard-library only** and compatible with Python 3.9.
* **Changing `VCPKG_RELEASE`** changes package versions for everyone and invalidates the CI vcpkg cache (its key hashes
  `tools/dg.py`). Change `builtin-baseline` in `vcpkg.json` with it.

To add a command, write `cmd_<name>(args)`, register it in `main()` with `add(name, func, words=..., help_text=...)`,
add its options to the returned parser, and add a line to the module docstring (it is the `--help` text). To add an
app word, extend `APPS`. Then update this page, and the launcher if the command should have a button.

## Reference

### Exit codes

| Situation | Exit code |
|---|---|
| Success | `0` |
| A `dg` check failed (`[dg] error: ...`) | `1` |
| Usage error from the argument parser (unknown command or option, non-integer `-j`) | `2` |
| A wrapped command failed | that command's exit code |
| `dg run` | the app's exit code |
| `dg docs --check` | the checker's exit code |
| Ctrl+C | `130` |

### Files dg reads or writes

| Path | Use |
|---|---|
| `CMakePresets.json` | Read by `host_presets()` and `dg presets` |
| `build/<preset>/CMakeCache.txt` | Read to detect existing trees and stale `-D` values |
| `build/<preset>/bin/<Config>/` | Where `dg run` and `dg determinism` find the apps |
| `external/vcpkg/` | Created by `dg setup --vcpkg` |
| `.git/hooks/pre-push` (or the configured hooks path) | Written by `dg hooks install` |
| `build/docs/` | Written by `dg docs` |

## Testing

`dg` has no unit tests of its own. It is exercised on every push: the CI jobs on macOS, Windows, Linux and Android
use only `dg` commands, so a broken command fails CI. When you change `dg.py`, check by hand on the hosts you can
reach:

```bash
./dg --help
./dg setup
./dg presets
./dg build -D DG_HEADLESS=ON && ./dg build -D DG_HEADLESS=OFF   # both should print "reconfiguring"
./dg test dev -L unit
./dg determinism --ticks 300
./dg hooks status
```

If you change preset selection or the Visual Studio environment code, say in the pull request which hosts you ran it
on. Windows-only code paths cannot be checked on macOS.
