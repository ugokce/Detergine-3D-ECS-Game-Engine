---
title: Planned systems overview
section: Planned systems
order: 10
status: design
summary: Every engine system that is designed but not built yet, with its status, roadmap step and the page or design section that describes it.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md, engine/CMakeLists.txt, engine/sim/, engine/runtime/include/dg/runtime/Application.h, cmake/DgTargets.cmake, cmake/DgOptions.cmake, vcpkg.json
---

## Overview

Detergine is built in small, verifiable steps. Today the repository contains the foundation only: core types,
allocators, generational handles, deterministic math, a placeholder fixed-tick simulation, the three app hosts, the
build system and the test suites (see [Simulation](../systems/simulation.md) and
[Runtime and apps](../systems/runtime-and-apps.md)). Everything else in the engine is designed in
[docs/DESIGN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md) and scheduled in
[docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md).

The pages in this section summarize those designs for game developers and engine contributors. Each one follows the
same structure: **Design** (what DESIGN.md decides), **How it will be used** (the intended workflow, Luau first),
**Integration with existing code** (how it connects to what is in the repository now), **Roadmap** and **Open risks**.

> **Important:** Nothing on a *Planned systems* page exists in code yet unless the page says so explicitly. API names
> such as `dg.system`, `world:input` or `dg_add_game_module` are taken from DESIGN.md and can change before they are
> implemented. When a system lands, its page moves to *Systems* and is rewritten in the system page template
> (see [Writing documentation](../contributing/documentation.md)).

## Design

The engine is organized in layers. A layer may only depend on layers below it, and the deterministic **simulation
layer** never depends on the **presentation layer**
([DESIGN.md §3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#3-layered-architecture)).
Planned systems fall on one side of that line or the other, and the side decides which rules apply:

| Side | Rules | Systems |
|---|---|---|
| Simulation (deterministic) | CLAUDE.md determinism rules 1 to 11: fixed tick, `dg::detmath`, sim RNG, no hidden state, builds headless | ECS sim world, Luau sim scripts, C++ game modules, physics, netcode, sim animation state |
| Presentation (free) | Never writes sim state; excluded from state hashes and headless builds | Rendering, GI, presented animation, audio, VFX, runtime UI |
| Tools (editor-only) | Compiles out of the player; nothing depends on it | Editor, world-building tools, MCP server, cooker, packager |

### Planned systems with their own page

| System | Page | One-line description | Status | Roadmap step |
|---|---|---|---|---|
| ECS | [ECS](ecs.md) | Archetype entity-component-system with deterministic iteration, command buffers, events, snapshots and hierarchical state hashing | design | 5 (after 3 Reflection, 4 Serialization) |
| Scripting | [Luau scripting and C++ game modules](scripting.md) | Luau-first gameplay in sandboxed sim and view scripts, bindings generated from reflection, optional static C++ game modules | design | 8 |
| Physics | [Physics](physics.md) | Jolt (3D) and Box2D v3 (2D) in cross-platform deterministic configuration, snapshotted for rollback | design | 9 |
| Networking | [Multiplayer networking and headless servers](networking.md) | Built-in predict/rollback over server-confirmed inputs, `dg-server` relay and verify modes, desync detection and resync | design | 14 |
| Rendering | [Rendering and global illumination](rendering.md) | Vulkan RHI, render graph, bindless resources, visibility-buffer desktop and forward+ mobile pipelines, tiered Lumen-style GI | design | 10, 11, then 15+ for GI |
| Animation | [Skeletal animation](animation.md) | ozz-animation runtime with an animation graph, GPU skinning, LOD, and a deterministic sim-side animation state with baked tracks | design | 15+ |
| Audio | [3D audio](audio.md) | Presentation-only event-based audio with HRTF spatialization, acoustics, a bus mixer and rollback-safe playback | design | 15+ |
| VFX | [Particles and VFX](vfx.md) | Presentation-only particle graph IR compiled to fused GPU kernels and a SIMD CPU bytecode path | design | 15+ |
| Runtime UI | [Runtime UI](ui.md) | Retained, ECS-backed UI with Yoga flexbox layout, MSDF text, styling and a WYSIWYG UI Designer | design | 15+ |
| Editor and MCP | [Editor, world-building tools and MCP server](editor-and-mcp.md) | Unity-like ImGui editor, level design tools, and an embedded MCP server for AI agents | design | 12 (editor), 13 (MCP), 15+ (world building) |
| Assets and packaging | [Assets, cooking and packaging](assets-and-packaging.md) | GUID-based asset database with `.meta` files, importers, derived data cache, `dg-cook`, `.dgpak` archives and `dg-package` | design | 11 (glTF import), 15+ (cook and package) |

### Designed systems without a page of their own yet

These are designed in DESIGN.md and covered only there (or as part of a page above) until they are built.

| System | One-line description | Status | Roadmap step | Design |
|---|---|---|---|---|
| Reflection (`dg::refl`) | `TypeRegistry` keyed by a stable `TypeId`, `DG_REFLECT` / `DG_PROPERTY` annotations processed by the `dg-reflect` generator | design | 3 | [§4.2](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#42-reflection-dgrefl) |
| Serialization (`dg::ser`) | Reflection-driven YAML and binary archives with per-type schema versioning | design | 4 | [§4.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#43-serialization-dgser) |
| Sim world | Project settings, reflected input commands, the sim world on the ECS, replay record and playback, desync dumps | design | 6 | [§5.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#53-fixed-tick-scheduler) |
| Job system (`dg::jobs`) | Task graph over enkiTS; parallel sim jobs write disjoint data and reduce in a fixed order | design | 7 | [§4.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#44-job-system-dgjobs) |
| Module and plugin system (`dg::plugin`) | `ModuleDesc`, `plugin.dgplugin` manifests, C-ABI `dgCreatePlugin`, dynamic in the editor and static in shipping | design | not scheduled as its own step | [§4.5](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#45-module--plugin-system-dgplugin) |
| Platform (`dg::platform`) | SDL3 window, input, file system, threads and timers behind one module | design | 10 | [§2.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#21-build-tooling--foundation) |
| Scenes and prefabs (`dg::scene`) | YAML scenes, prefab instances that store only overrides, nested prefabs and variants, editor play mode | design | 12 | [§12](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#12-scene-prefabs--runtime-objects) |
| 2D rendering | Sprite batching, sorting layers, 9-slice, tilemaps, sprite atlases; shares the 3D RHI and render graph | design | 15+ | [§8.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#84-2d-rendering) |
| Non-deterministic replication | Optional plugin for server-authoritative state replication; outside the determinism guarantees | design | not scheduled | [§15.9](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#159-non-deterministic-replication-optional-plugin) |

## How it will be used

The intended game developer workflow ties the planned systems together:

1. Create a project and open it in the editor ([Editor and MCP](editor-and-mcp.md)). Assets under `Assets/` get
   `.meta` files with GUIDs ([Assets and packaging](assets-and-packaging.md)).
2. Define components and the `InputCommand` for the game, and write gameplay as Luau sim systems
   ([Scripting](scripting.md)) that run on the ECS sim world ([ECS](ecs.md)), using deterministic physics
   ([Physics](physics.md)).
3. Write presentation in Luau view scripts: cameras, UI, VFX and audio react to sim state and sim events.
4. Get multiplayer without network code: the same sim runs online, offline and in replays, and only sees
   `world:input(slot)` ([Networking](networking.md)).
5. Cook and package for Windows, macOS and Android, and build a level-scoped headless server
   ([Assets and packaging](assets-and-packaging.md)).

Luau is the primary gameplay language (decision 2 in
[DESIGN.md §18](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#18-key-risks--open-questions)).
A complete game must be possible without any C++.

## Integration with existing code

Every planned system builds on the foundation that exists today:

| Existing code | Used by |
|---|---|
| `dg::Handle` / `dg::HandlePool` with a FIFO free list ([Handle.h](engine/core/include/dg/core/Handle.h)) | The ECS entity allocator, asset handles, RHI handles |
| `dg::fixed32`, `dg::fixed64`, `dg::detmath`, `Vec3` / `Quat` / `Mat4` ([engine/math/](engine/math/)) | Sim components, Luau `dg.fixed` and `dg.math`, physics glue, sim animation tracks |
| `dg::sim::Rng` (PCG32) and `dg::sim::InputCommand` ([engine/sim/](engine/sim/)) | The sim world RNG resource and the reflected input schema |
| `dg::sim::Simulation` (placeholder) | Replaced by the ECS-based sim world in steps 5 and 6 |
| `dg::runtime::AppConfig` and the `--headless`, `--fast`, `--ticks`, `--level` flags ([Application.h](engine/runtime/include/dg/runtime/Application.h)) | Headless servers, replay verification, level loading |
| `dg_add_module(<name> SIM ...)` and `dg_sim_target()` ([DgTargets.cmake](cmake/DgTargets.cmake)) | Every new sim module (ECS, physics, script, net) |
| The `DG_HEADLESS` option ([DgOptions.cmake](cmake/DgOptions.cmake)) | The headless build profile that excludes presentation modules |

The modules that exist today are `core`, `math`, `sim` and `runtime` (see [engine/CMakeLists.txt](engine/CMakeLists.txt)).
New modules are added there in dependency order, and the only third-party dependency in
[vcpkg.json](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/vcpkg.json) today is `gtest`.

When you start one of these systems, read its DESIGN.md section first, add the module in its own folder under
`engine/`, and change the page status from `design` to `partial` or `implemented`. A page with that status must
switch to the system-page template (Overview, How to use, How to edit, Testing), as described in
[Writing documentation](../contributing/documentation.md). Sim modules also need a replay-based determinism test.

## Roadmap

The step numbers come from
[docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md).
Steps 0 (build system) and 1 (core foundation) are done; step 2 (dependencies and CI) is in progress. The longer-term
phases are in
[DESIGN.md §17](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#17-roadmap-phased).

| Step | Scope | Status |
|---|---|---|
| 0 | Build system | done |
| 1 | Core foundation | done (macOS ARM64) |
| 2 | Dependencies and CI (spdlog sinks, Tracy, xxHash still to do) | in progress |
| 3 | Reflection | not started |
| 4 | Serialization | not started |
| 5 | ECS | not started |
| 6 | Sim world | not started |
| 7 | Job system | not started |
| 8 | Luau | not started |
| 9 | Physics | not started |
| 10 | Platform and RHI | not started |
| 11 | Renderer MVP | not started |
| 12 | Editor MVP | not started |
| 13 | MCP server | not started |
| 14 | Netcode | not started |
| 15+ | Content systems: animation, audio, VFX, UI, GI tiers, world-building tools, packaging | not started |

## Open risks

The full risk table is in
[DESIGN.md §18](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#18-key-risks--open-questions).
The risks that cut across every planned system:

* **Cross-architecture float determinism is fragile.** Compiler upgrades and new code paths can break bit-identical
  results on x64 and ARM64. The mitigation is the determinism CI gate, pinned compilers, sim code isolated in
  `dg_sim_target()`, and a lint for banned functions (the lint is planned).
* **Scope.** The design has the breadth of a large commercial engine. Strict phase gating and interfaces around every
  module let features be delayed without refactors.
* **The state hash changes.** `dg::Fnv1a64` is a stand-in for XXH3 ([Hash.h](engine/core/include/dg/core/Hash.h)).
  Switching to xxHash in step 2 changes every recorded golden hash, and every later change to what the sim hashes
  (ECS, physics, RNG) does too.
