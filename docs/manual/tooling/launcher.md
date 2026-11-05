---
title: Detergine Launcher (GUI)
section: Tooling
order: 30
status: implemented
summary: The tkinter launcher window over dg - its panels, how each button maps to a dg command, where it stores settings, and how to start it.
sources: tools/dg_launcher.py, Launcher.command, Launcher.bat
---

## Overview

The **Detergine Launcher** is a small desktop window for people who prefer buttons to the command line. You pick a
platform preset, a configuration, a build target and the build and run options, then press Build, Run or Test. The
launcher is [tools/dg_launcher.py](tools/dg_launcher.py). It uses only the Python standard library (tkinter).

The launcher does no build work itself. **Every button runs the equivalent [dg command](dg-tool.md)** as a child
process and streams its output into the window. The bottom of the window shows that command, ready to copy, so the
GUI and the command line always behave the same and you can move a GUI workflow into a script.

## How to use

### Starting it

| How | Where |
|---|---|
| `./dg gui` (macOS, Linux) or `dg gui` (Windows) | Any terminal, through the [dg](dg) / [dg.cmd](dg.cmd) launchers |
| Double-click `Launcher.command` | macOS Finder. [Launcher.command](Launcher.command) runs `python3 tools/dg.py gui` from the repository root |
| Double-click `Launcher.bat` | Windows Explorer. [Launcher.bat](Launcher.bat) starts `pyw -3 tools\dg.py gui` (or `pythonw` without the Python launcher), so no console window stays open |
| `python3 tools/dg_launcher.py` | Directly, without `dg` |

`dg gui` prints `[dg] Detergine Launcher is open (close its window to return here)` and blocks until you close the
window. If tkinter is missing it fails with an install hint (`brew install python-tk` or `sudo apt install
python3-tk`). The window is raised to the front once when it opens. This happens on every host; it is
needed mainly on macOS, where a Tk window started from a terminal otherwise opens behind the active app.

### Window layout

The left column holds four groups of settings. The right column holds the action buttons and the output console. The
bottom row shows the command and the status.

**Project**

| Control | Values | Maps to |
|---|---|---|
| Platform preset | Every visible configure preset usable on this host, shown as `displayName  [name]` | `--preset <name>` |
| Configuration | Debug, Development, Release | the `debug` / `dev` / `release` word |
| Build target | all, editor, player, server, tests | the app word for Build (`all` adds none) |

Under these, an orange note warns about the selected preset: Android needs `ANDROID_NDK_HOME` (and says *not set!* when
it is missing) and cannot be run on this machine; a Ninja preset is selected but `ninja` is not installed; or the
headless profile is active, so the editor is not built.

**Build options (CMake)**

| Control | Maps to |
|---|---|
| One checkbox per engine option (`DG_BUILD_EDITOR`, `DG_BUILD_TESTS`, `DG_HEADLESS`, `DG_WARNINGS_AS_ERRORS`, `DG_ENABLE_ASAN`, `DG_ENABLE_UBSAN`) | `-D NAME=ON\|OFF`, only when it differs from the current value |
| Parallel jobs (0 = all cores), 0 to 256 | `-j N` when N > 0 |
| Verbose build output | `--verbose` |
| Fresh configure (discard CMake cache) on next action | `--fresh`, then the box clears itself |

The checkbox labels come from `ENGINE_OPTIONS` in [dg.py](tools/dg.py); the option meanings are on the
[Build system](build-system.md) page.

**Run options**

| Control | Default | Maps to (after `--`) |
|---|---|---|
| App | editor | the app word for Run |
| Headless | off | `--headless` |
| Fast (no real-time pacing) | off | `--fast` |
| Ticks (empty = until stopped) | empty | `--ticks N` |
| Tick rate (Hz) | `30` | `--tick-rate N`, omitted when `30` |
| Players | `2` | `--players N`, omitted when `2` |
| Seed (empty = default) | empty | `--seed N` |
| Level | empty | `--level NAME` |
| Extra arguments | empty | split like a shell command line and appended |

The App list follows the preset and options: only `player` for Android presets; `server` and `player` when
`DG_HEADLESS` is checked or the preset sets it (such as `linux-x64-server`); otherwise `editor`, `player`, `server`.
The list and the *Headless profile* note look at the preset, but the `-D` arguments do not: see
[When options are passed as -D](#when-options-are-passed-as--d).
Extra arguments with unbalanced quotes are ignored silently. The flags themselves are documented in
[Runtime and apps](../systems/runtime-and-apps.md).

**Tests**

| Control | Values | Maps to |
|---|---|---|
| Label filter | all, determinism, smoke | `-L <label>` (`all` adds none) |

**Actions**

| Button | Runs |
|---|---|
| Build | `dg build <config> [<target>] <common>` |
| Run | `dg run <config> <app> <common> [-- <run options>]` (`dg run` always builds first) |
| Test | `dg test <config> <common> [-L <label>]` |
| Check Setup | `dg setup` |
| Generate | `dg generate <common>` |
| Open IDE | `dg generate --ide <common>`, with a Ninja preset swapped for its IDE preset |
| Determinism | `dg determinism <common>` (always the default 1800 ticks) |
| Clean | `dg clean --preset <name>` |
| Open Build Folder | Opens `build/<preset>/` in Finder, Explorer or `xdg-open`; warns if it does not exist yet |
| Stop | Interrupts the running command |

`<common>` is `--preset <name>` plus the `-D`, `-j`, `--verbose` and `--fresh` arguments from *Build options*. Open IDE
maps `macos-arm64` to `macos-arm64-xcode` and `windows-x64` to `windows-x64-vs<year>` (the installed Visual Studio
year, `2022` when unknown). For other Ninja presets, such as `linux-x64`, it prints an error.

Only one command runs at a time. While it runs, Build, Run, Test, Check Setup, Generate, Open IDE, Determinism and
Clean are disabled and Stop is enabled. Open Build Folder, Copy and Clear Output stay usable.

**Console and status**

The console shows each command as `$ ./dg ...` (`dg ...` on Windows) followed by its output, coloured by line:
the rules are checked in this order, and the first match wins:

1. Red: the line starts with `[dg] error` or contains `error:`, `failed` or `desync` (case-insensitive). This includes
   CTest's success summary `100% tests passed, 0 tests failed out of N`, because it contains `failed`.
2. Yellow: the line contains `warning` or starts with `[warn]`.
3. Cyan: the line starts with `[dg]` or `$ ` (commands). This includes `dg determinism`'s success line
   `[dg] Debug and Release are bit-identical ...`.
4. Green: any other line containing `passed` or `bit-identical`.

A run ends with a green `✔ finished` line or a red `✘ exited with code N` line.

* **Hide Xcode simulator noise** (on by default) drops harmless `xcodebuild` messages about out-of-date iOS simulator
  runtimes (the `NOISE` list in the source).
* **Clear Output** empties the console.
* **Command** shows the `dg` command for the current Build settings, updated on every change. **Copy** puts it on the
  clipboard.
* The status line shows *Ready*, *Running: ...*, *Finished successfully* or *Failed (exit code N)*.

### When options are passed as -D

A checkbox becomes a `-D` argument only when its value differs from what the build tree already uses. The launcher
compares against, in order: the value in `build/<preset>/CMakeCache.txt`, the preset's own `cacheVariables`, and the
option's default. So with `macos-arm64` and the default checkboxes, an unchanged form produces a plain
`./dg build dev --preset macos-arm64`, and ticking *Headless* on a configured tree produces `-D DG_HEADLESS=ON`, which
makes `dg` reconfigure (see [the dg tool](dg-tool.md#-d-and-reconfiguring)).

> **Warning:** The checkboxes themselves start from `.dg/launcher.json` or the built-in defaults, not from the preset
> or from `CMakeCache.txt`. When the preset or the build tree uses a different value, the untouched form passes the
> opposite value. Selecting `linux-x64-server` (`DG_HEADLESS=ON`) with *Headless* unticked passes
> `-D DG_HEADLESS=OFF` and configures a non-headless tree, even though the App list and the *Headless profile* note
> follow the preset. Selecting `android-arm64` (`DG_BUILD_TESTS=OFF`) with *Build tests* ticked passes
> `-D DG_BUILD_TESTS=ON`. A tree configured from the command line with `-D DG_HEADLESS=ON` gets
> `-D DG_HEADLESS=OFF` the same way. Check the **Command** line before you build, and set the checkboxes to match the
> preset or tree.

### Stopping and closing

**Stop** sends an interrupt to the whole process group of the running command (`SIGINT` on macOS and Linux,
`CTRL_BREAK_EVENT` on Windows), so `cmake`, the compiler and a running app all receive it. If the command is still
alive after 3 seconds, it is killed. Closing the window saves the settings and kills a running command immediately.

### Settings file

The launcher remembers your choices per checkout in `.dg/launcher.json` in the repository root. It writes the file
when an action starts and when the window closes, and ignores a missing or unreadable file. The `.dg/` folder is in
`.gitignore`.

| Key | Control |
|---|---|
| `preset`, `config`, `target` | Project group |
| `options` | Object of option name to `true` / `false` |
| `jobs`, `verbose` | Build options |
| `app`, `run_headless`, `run_fast`, `ticks`, `tick_rate`, `players`, `seed`, `level`, `extra_args` | Run options |
| `test_label` | Tests |
| `hide_noise` | Console |

*Fresh configure* is never saved. With no saved preset, the launcher selects the preset `dg` would use: an existing
build tree, otherwise the host default.

## How to edit

All code is in [tools/dg_launcher.py](tools/dg_launcher.py), in one `Launcher(tk.Tk)` class:

| Part | Methods |
|---|---|
| Layout | `build_ui`, `build_project_group`, `build_options_group`, `build_run_group`, `build_test_group`, `build_actions`, `build_console` |
| State | `on_change` (app list, notes, command line), `common_args`, `build_args`, `run_args`, `describe` |
| Actions | `do_setup`, `do_generate`, `do_ide`, `do_build`, `do_run`, `do_test`, `do_determinism`, `do_clean`, `open_build_folder` |
| Processes | `start`, `read_output`, `pump_output`, `finish`, `stop`, `force_kill` |
| Settings | `load_settings`, `save_settings` |

Rules for changes:

* **Never add build logic to the launcher.** A new action builds a `dg` argument list and calls `self.start(args)`.
  If `dg` cannot do it yet, add the command to [dg.py](tools/dg.py) first. This keeps the command line, the GUI and CI
  identical.
* `start` runs `sys.executable -u tools/dg.py <args>` from the repository root, with `PYTHONUNBUFFERED=1`, stdout and
  stderr merged, and stdin closed. A reader thread puts lines on a queue; `pump_output` drains it on the Tk thread
  every 50 ms. Touch Tk widgets only from the Tk thread.
* The launcher imports `dg` and uses its helpers (`host_presets`, `read_cache`, `existing_preset`, `default_preset`,
  `vs_product_line`, `preset_dir`, `have`, `ENGINE_OPTIONS`). Keep those names in sync with `dg.py`.
* Adding an engine option: add it to `ENGINE_OPTIONS` in `dg.py` and `OPTION_DEFAULTS` here; the checkbox appears by
  itself. Adding an app: extend `TARGETS` and `RUN_APPS`. Adding a test label: extend `TEST_LABELS`.
* A new persistent control needs a `tk.*Var`, a `trace_add` registration in `__init__` (so the command line updates),
  and a key in `save_settings`. Update the settings table on this page.

## Testing

The launcher has no automated tests. After a change, start it with `./dg gui` and check:

1. The preset list shows only this host's presets, and the Command line changes when you change any control.
2. Build, Run and Test print the same command in the console as in the Command field, and finish with *finished*.
3. Ticking *Headless* switches the App list to `server` and `player` and adds `-D DG_HEADLESS=ON`. After that build,
   unticking it adds `-D DG_HEADLESS=OFF`, because the cache now holds `ON`.
4. Stop interrupts a long build or a `Run` without *Ticks*.
5. After closing and reopening, every setting except *Fresh configure* is restored.

Check on each host you changed behaviour for (macOS, Windows, Linux) and say in the pull request which ones you ran.
