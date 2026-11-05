---
title: Architecture overview
section: Architecture
order: 10
status: partial
summary: The engine's layers, the module dependency rules, the sim and presentation worlds, the repository layout, and which modules exist today.
sources: CMakeLists.txt, cmake/DgTargets.cmake, engine/CMakeLists.txt, apps/CMakeLists.txt, engine/core/CMakeLists.txt, engine/math/CMakeLists.txt, engine/sim/CMakeLists.txt, engine/runtime/CMakeLists.txt
---

## Overview

Detergine is built as a stack of layers. Each layer may depend only on the layers below it. On top of that layering
sits the engine's central split: a **sim world** that is deterministic and owns all gameplay state, and a
**presentation world** that draws and plays it but never changes it.

This page describes the target architecture from `docs/DESIGN.md` §3–§5 and marks what exists today. Four engine modules
and three app hosts are built: `core`, `math`, `sim`, `runtime`, and `dg-editor`, `dg-player`, `dg-server`. Everything
else on this page is designed but not implemented yet.

## Layers

From top to bottom:

| Layer | Contents | Status |
|---|---|---|
| Applications | `dg-editor`, `dg-player`, `dg-server` (headless) | Hosts exist; they run the simulation loop only. |
| Plugins | C++ (dynamic in the editor, static in shipping builds) and Luau packages: gameplay, importers, tools, render features | Design |
| Editor framework / Gameplay framework | Editor-only tooling; scenes, prefabs, UI, animation, audio | Design |
| **Simulation** (deterministic) | Sim ECS world, fixed-tick scheduler, `dg::fixed` math, Jolt/Box2D, Luau sandbox, sim RNG, netcode | `dg::sim` placeholder, `dg::math`, `dg::sim::Rng` |
| **Presentation** (non-deterministic) | Render world, renderer, GI, audio, UI | Design |
| Engine services | Asset DB and streaming, reflection, serialization, jobs, VFS, input, module and plugin manager, config, profiling | Design |
| RHI and Platform | Vulkan RHI; SDL3 for windows, input, file system, threads, timers | Design |
| Core | Memory and allocators, containers, strings, math, logging, asserts, hashing | `dg::core`, `dg::math` |

```text
  Applications          dg-editor | dg-player | dg-server
  ----------------------------------------------------------------
  Plugins               gameplay, importers, tools, render features
  ----------------------------------------------------------------
  Editor framework      | Gameplay framework (scene, prefab, UI, anim, audio)
  ----------------------------------------------------------------
  SIMULATION            deterministic: ECS, fixed tick, physics, Luau sim, RNG
  PRESENTATION          free: render world, renderer, GI, audio, UI
  ----------------------------------------------------------------
  Engine services       assets, reflection, serialization, jobs, plugins
  ----------------------------------------------------------------
  RHI (Vulkan) . Platform (SDL3)
  ----------------------------------------------------------------
  Core                  memory, containers, math, log, assert, hash
```

## Dependency rules

These rules come from `CLAUDE.md` and `docs/DESIGN.md` §3. A change that breaks one is rejected in review.

* **Lower layers never include higher ones.** Core knows nothing about the simulation; the simulation knows nothing
  about applications.
* **Simulation never depends on Presentation**, not even optionally. Sim code must build and run headless
  (`DG_HEADLESS=1`), with no renderer, audio or UI.
* **Nothing depends on the editor.** Editor-only code lives in `editor/` or in `#if DG_EDITOR` blocks inside
  `*.editor.cpp` files, and compiles out of the player.
* **Everything is a module.** New features go into a module or plugin behind an interface (for example
  `IAssetImporter`, `IRenderFeature`, `IEditorPanel`, `ISystemProvider`), not into `core/`.
* **Handles over pointers** across system boundaries: `Entity`, `AssetHandle<T>`, `rhi::Handle<T>`. Today
  [Generational handles](../systems/handles.md) provide `dg::Handle<T>` and `dg::HandlePool<T>`.
* **Assets are referenced by GUID**, never by path, in serialized data.
* **Platform-specific code** stays in `engine/platform/`, or in `#if` blocks in clearly marked backend files.
* **Vulkan stays inside `engine/rhi/`.** Rendering code uses `dg::rhi` and the render graph.

### How the build enforces layering

The modules are added in dependency order in [engine/CMakeLists.txt](engine/CMakeLists.txt): `core`, `math`, `sim`,
`runtime`. Each module is a static library created by `dg_add_module` in [DgTargets.cmake](cmake/DgTargets.cmake),
named `dg_<name>` with the alias `dg::<name>`. A module can only use the headers of the modules it lists in
`PUBLIC_DEPS` or `PRIVATE_DEPS`, so an upward include fails to compile instead of slipping through.

| Module | Target | Depends on | Sim target | Contents |
|---|---|---|---|---|
| `core` | `dg::core` | (none) | no | Types, platform detection, build info, asserts, logging, FNV-1a hashing, allocators, handles. See [Core foundation](../systems/core-foundation.md), [Memory allocators](../systems/memory.md), [Generational handles](../systems/handles.md). |
| `math` | `dg::math` | `dg::core` | yes | `fixed32`/`fixed64`, `dg::detmath`, vectors, quaternions, matrices. See [Fixed-point numbers](../systems/fixed-point.md), [Deterministic math (detmath)](../systems/detmath.md), [Vectors, quaternions and matrices](../systems/vector-math.md). |
| `sim` | `dg::sim` | `dg::core` | yes | `Simulation` (placeholder until the ECS), `Rng`, `InputCommand`. See [Simulation](../systems/simulation.md). |
| `runtime` | `dg::runtime` | `dg::core`, `dg::sim` | no | Shared command line and the fixed-tick main loop. See [Runtime and apps](../systems/runtime-and-apps.md). |

A module created with the `SIM` keyword is marked as a **sim target** through `dg_sim_target`. Every engine target
already gets the strict floating-point flags; the sim marking lets tooling, such as a future banned-function lint, find
deterministic code. See [Determinism model](determinism.md).

The apps are created by `dg_add_app` in [apps/CMakeLists.txt](apps/CMakeLists.txt). All three link only `dg::runtime`.
`dg-server` is skipped on Android and iOS, and `dg-editor` is skipped on Android, iOS and in headless builds.

## The two worlds

```text
       Inputs (local + network, per tick)
                  |
                  v
  +-------------------------------+   read-only state / events   +-------------------------------+
  |  SIM WORLD (deterministic)    | ---------------------------> |  PRESENTATION WORLD (free)    |
  |  fixed tick, 30 Hz default    |                              |  variable frame rate          |
  |  components tagged Sim        |                              |  interpolation, VFX, audio,   |
  |  deterministic physics        |                              |  animation pose, UI, camera   |
  |  Luau sim scripts             |                              |  Luau view scripts            |
  +-------------------------------+                              +-------------------------------+
                  ^                                                              |
                  +--------------------- InputCommands only ---------------------+
```

* The **sim world** owns all gameplay state. It advances only in fixed ticks, and only from inputs. It sees the tick
  number, the constant `dt`, and one **InputCommand** per player slot, nothing else. Its state can be hashed,
  snapshotted, rolled back and replayed.
* The **presentation world** renders the sim state, interpolating between the last two ticks, and owns cosmetic state
  (particles, sounds, camera, animation poses). It may read sim state and consume sim events, but **it never writes sim
  state**. The only path back into the sim is input commands.
* Single-player games use the same model, which makes replays, save states and kill-cams straightforward.
* Presentation that reacts to sim events must handle **predicted** events, which a rollback can confirm or cancel, so
  sounds and effects are not duplicated.

Today the sim world is the placeholder `dg::sim::Simulation` (a set of bouncing bodies driven by scripted inputs), and
there is no presentation world yet. The planned design is on [ECS](../planned/ecs.md),
[Multiplayer networking and headless servers](../planned/networking.md), and
[Rendering and global illumination](../planned/rendering.md).

### The fixed-tick loop

The designed loop (`docs/DESIGN.md` §5.3) polls platform events, builds the local input command, exchanges inputs over
the network, steps the sim for every tick whose inputs are available, and then updates presentation with an
interpolation factor. Sim systems run in ordered phases:

`PreTick → Input → ScriptPreUpdate → Gameplay → Physics(Pre/Step/Post) → ScriptPostUpdate → Lifecycle → PostTick(checksum)`

What exists today is a simpler loop in `dg::runtime::run`: build scripted inputs for every player slot, call
`Simulation::step`, and either sleep until the next tick or, with `--fast`, run ticks back to back. See
[Runtime and apps](../systems/runtime-and-apps.md).

## Repository layout

### Today

| Path | Contents |
|---|---|
| `engine/core/`, `engine/math/`, `engine/sim/`, `engine/runtime/` | Engine modules. Public headers in `engine/<module>/include/dg/<module>/`, sources in `src/`. |
| `apps/editor/`, `apps/player/`, `apps/server/` | The three app hosts, one `Main.cpp` each. |
| `tests/` | Five GoogleTest executables (`dg-core-tests` from `core/`; `dg-fixed-tests`, `dg-detmath-tests` and `dg-vector-tests` from `math/`; `dg-sim-tests` from `sim/`), the player, server and editor smoke tests, and `support/` for shared test setup. |
| `cmake/` | Build options, configurations, target helpers, vcpkg integration, and the `arm64-osx-dg` overlay triplet. |
| `tools/` | `dg.py` (the developer tool), `dg_launcher.py` (the GUI), and `docs/` (this manual's builder). |
| `docs/` | `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, and `manual/` (this manual's Markdown). |
| `.github/` | The CI workflow and its helper scripts. |
| `dg`, `dg.cmd`, `Launcher.command`, `Launcher.bat` | Launchers for `dg` and the GUI. |
| `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json` | Root build, presets, and the vcpkg manifest. |
| `external/vcpkg/` | Created by `dg setup --vcpkg`; not part of the repository. |
| `build/` | Build trees (`build/<preset>/`) and the generated manual (`build/docs/`). |

### Designed

`docs/DESIGN.md` §3.1 defines the target layout. The engine modules still to come are:

| Module | Namespace | Purpose |
|---|---|---|
| `platform/` | `dg::platform` | SDL3 wrapper, file system, threads, time |
| `reflect/` | `dg::refl` | Type registry, attributes, generated-code runtime |
| `serialize/` | `dg::ser` | YAML and binary archives driven by reflection |
| `jobs/` | `dg::jobs` | Task graph over enkiTS |
| `ecs/` | `dg::ecs` | Archetype ECS, queries, scheduler, snapshots |
| `physics/` | `dg::physics` | Jolt and Box2D integration in deterministic configuration |
| `script/` | `dg::script` | Luau VM, sandbox, binding generator runtime |
| `net/` | `dg::net` | Transport, lockstep, rollback, sessions |
| `asset/` | `dg::asset` | Asset DB, GUIDs, importer API, cooking, streaming, VFS/pak |
| `rhi/` | `dg::rhi` | Vulkan abstraction |
| `render/` | `dg::render` | Render graph, render world, passes, materials, GI, 2D |
| `ui/`, `audio/`, `anim/`, `vfx/`, `scene/`, `plugin/` | `dg::ui`, `dg::audio`, `dg::anim`, `dg::vfx`, `dg::scene`, `dg::plugin` | Runtime UI, audio, animation, particles, scenes and prefabs, module manager |

Outside `engine/`, the design adds `editor/` (with `editor/mcp/`), `apps/mcp-bridge/`, `tools/dg-reflect/`,
`tools/dg-cook/`, `tools/dg-package/`, `plugins/`, `shaders/`, `samples/`, `third_party/`, and `platform/android/` and
`platform/macos/` templates. See [Planned systems overview](../planned/overview.md).

## Extending the architecture

* **A new engine module**: add `engine/<name>/` with `include/dg/<name>/` and `src/`, create it with
  `dg_add_module(<name> [SIM] SOURCES ... PUBLIC_DEPS ...)`, and add it to [engine/CMakeLists.txt](engine/CMakeLists.txt)
  after the modules it depends on. Use `SIM` for anything that runs in the simulation. Add a test suite with
  `dg_add_test` in [tests/CMakeLists.txt](tests/CMakeLists.txt).
* **A new app**: add `apps/<name>/Main.cpp` and a `dg_add_app` line in [apps/CMakeLists.txt](apps/CMakeLists.txt). If
  `dg` should build and run it, add it to `APPS` in [dg.py](tools/dg.py).
* **Documentation**: a new module needs a page in `docs/manual/systems/` listing its files in `sources` (the docs check
  fails for an undocumented `engine/` directory), a link from this page, and glossary entries for new terms. See
  [Writing documentation](../contributing/documentation.md).
* **Design changes**: architectural decisions go into `docs/adr/NNNN-title.md`, and `docs/DESIGN.md` is updated when a
  decision changes the design.
