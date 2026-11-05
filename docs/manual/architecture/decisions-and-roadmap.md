---
title: Decisions and roadmap
section: Architecture
order: 30
status: implemented
summary: The five settled design decisions, the step-by-step implementation plan with each step's status, and the main risks.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md
---

## Overview

This page summarizes two documents in `docs/`:

* `docs/DESIGN.md` §17–§18: the phased roadmap, the key risks, and the decisions that settled its former open
  questions.
* `docs/IMPLEMENTATION_PLAN.md`: the roadmap broken into small, verifiable steps, each with exit criteria and a status.

Those documents are authoritative. When a step's status changes or a decision is revised, update them and this page in
the same commit.

## Settled decisions

Five questions were resolved on 2026-11-05. There are no open questions at the moment.

| # | Topic | Decision | What it means for you |
|---|---|---|---|
| 1 | Tick rate | **30 Hz by default**, configurable per project (`ProjectSettings.sim.tickRate`). | The current apps already default to 30 Hz (`--tick-rate`, `SimConfig::tickRate`). Project settings arrive with implementation step 6. |
| 2 | Gameplay language | **Luau-first.** A complete game must be writable in Luau without any C++. Native C++ gameplay is optional: a static game module whose classes and functions are bound to Luau. C++ runs the engine itself (sim core, physics, rendering, networking backend). | See [Luau scripting and C++ game modules](../planned/scripting.md). |
| 3 | Default transport | **ENet.** GameNetworkingSockets stays an optional plugin (Steam, relays, encryption). | See [Multiplayer networking and headless servers](../planned/networking.md). |
| 4 | Desktop renderer | **Visibility buffer**, with deferred material evaluation over it. The mobile tier stays forward+. | See [Rendering and global illumination](../planned/rendering.md). |
| 5 | Default server mode | **`relay`** until the headless build is stable, then **`verify`** becomes the default for new projects. | In `relay` mode the server is the input authority and takes a checksum majority vote; in `verify` mode it also runs the authoritative headless sim. |

## Implementation plan

Each step ends with green builds in Debug, Development and Release, passing tests, and golden hashes that are either
unchanged or deliberately re-recorded. A step is marked done only when its exit criteria hold on macOS ARM64. Windows
x64 is checked once CI runs (step 2).

| # | Step | Scope | Exit criteria | Status |
|---|---|---|---|---|
| 0 | Build system | CMake presets, `dg` tool, launcher GUI, minimal core/sim/runtime, smoke and determinism tests | `./dg test` green in all configurations; Debug hash equals Release hash | Done |
| 1 | Core foundation | Heap, linear and pool allocators; generational handles; `dg::fixed32`/`fixed64`; `dg::detmath`; vector, quaternion and matrix math | Unit tests green; detmath golden hash recorded; accuracy bounds met; no UB under ASan/UBSan | Done (macOS ARM64) |
| 2 | Dependencies and CI | vcpkg pinned to 2026.10.16, GoogleTest, GitHub Actions CI, pre-push hook, spdlog sinks, Tracy, xxHash | CI green on all hosts; golden hashes identical on x64 and ARM64 | In progress |
| 3 | Reflection | `dg::refl` runtime (TypeRegistry, TypeInfo, attributes), `dg-reflect` generator MVP for `DG_REFLECT` / `DG_PROPERTY` | Reflected structs round-trip through a field walker; the generator runs in the build | Not started |
| 4 | Serialization | Reflection-driven YAML (rapidyaml) and binary archives, schema versioning | Round-trip tests; stable, diff-friendly YAML | Not started |
| 5 | ECS | Archetype storage, queries, systems and scheduler, command buffers, events, snapshot/restore, hierarchical state hash | Snapshot, restore, identical hash; benchmark baseline | Not started |
| 6 | Sim world | Project settings (30 Hz default), input commands, sim world on the ECS, replay record and playback, desync dump | Replays reproduce hashes across configurations and CI hosts | Not started |
| 7 | Job system | enkiTS wrapper; deterministic parallel sim systems | Parallel and serial runs produce identical hashes | Not started |
| 8 | Luau | Luau VM, sim and view sandboxes, determinism lint, binding generator, `dg.d.luau`, `dg_add_game_module` | A Luau-only sample and a C++-module sample both run deterministically | Not started |
| 9 | Physics | Jolt (cross-platform deterministic) and Box2D v3, physics snapshot/restore | Physics replays identical across hosts | Not started |
| 10 | Platform and RHI | SDL3 window and input, Vulkan RHI (volk, VMA, vk-bootstrap; MoltenVK on macOS), render graph, bindless, ImGui | Triangle, then a textured mesh, on macOS and Windows with validation clean | Not started |
| 11 | Renderer MVP | glTF import, visibility-buffer desktop path, forward+ mobile path, PBR, clustered lights | Sample scene renders; golden-image test | Not started |
| 12 | Editor MVP | Docking layout; Hierarchy, Inspector, Scene, Game, Project and Console panels; gizmos; undo/redo; play mode; scenes and prefabs | Build a level, play it, save it, reload it | Not started |
| 13 | MCP server | `dg::mcp` and stdio bridge; scene, entity, component, prefab, script and play tools; undo per call; policy | Claude Code builds and saves a small level through MCP | Not started |
| 14 | Netcode | ENet transport, confirmed-input protocol, predict/rollback, hashing, resync, `dg-server` relay then verify, headless level-scoped cook | Two clients and a server stay in sync; a forced desync is resynced | Not started |
| 15+ | Content systems | Animation, audio, VFX, UI, GI tiers, world-building tools, packaging | Per `docs/DESIGN.md` roadmap phases 6–10 | Not started |

### Step 1 results

Recorded on 2026-11-05 on macOS ARM64 with AppleClang 21:

* 8 suites passed in Debug, Development and Release with warnings as errors, and under ASan and UBSan.
* `dg::detmath` is correctly rounded except in rare near-halfway cases: at most 0.5 ulp, measured exhaustively over
  every finite `f32` input for the single-argument functions, and by sweeps for `atan2` and `pow`. Huge arguments use an
  exact Payne-Hanek reduction, and NaN outputs are canonicalized to `0x7FC00000`.
* `dg::fixed32`/`fixed64` multiply and divide are exact or correctly rounded (ties away from zero) and matched an
  independent 192-bit reference with no mismatches, without `__int128`.
* A review found 11 issues across the four areas, 10 of them fixed with regression tests. For example,
  `kDefaultAlignment` is now fixed at 16 on every platform, `lerp` handles full-range spans, and matrix inverses no
  longer produce NaN for near-singular input.
* The golden hashes were recorded. They are listed with their tests on [Determinism model](determinism.md).

### Step 2 progress

Done:

* vcpkg pinned to release `2026.10.16` with a matching `builtin-baseline` in `vcpkg.json`.
* GoogleTest 1.18.0 through vcpkg. All suites were converted with every original check accounted for, and 20 death
  tests were added for `DG_ASSERT` paths. Debug registers 117 CTest tests.
* The GitHub Actions workflow and the `dg hooks install` pre-push hook. See
  [Continuous integration and git hooks](../tooling/ci.md).
* Build fixes found during the conversion: Xcode's Debug libc++ hardening is pinned to `none` because it is
  ABI-incompatible with the vcpkg-built libraries, and the `arm64-osx-dg` overlay triplet builds packages for macOS
  14.0, the engine's deployment target. See [Build system](../tooling/build-system.md).
* CI status: macOS ARM64, Linux x64 (golden hashes reproduced bit for bit on x64), Linux ASan + UBSan and the Android
  build are green.
* The engine manual in `docs/manual/`, built to HTML with `./dg docs`. CI's `Manual` job validates it and requires doc
  updates for changed sources. See [Writing documentation](../contributing/documentation.md).

Remaining:

* Get Windows x64 CI green.
* spdlog sinks, Tracy, and xxHash. xxHash replaces FNV-1a for state hashes, which changes every recorded golden hash.

## Roadmap phases

The implementation steps follow the phased roadmap in `docs/DESIGN.md` §17:

| Phase | Deliverables |
|---|---|
| 0: Foundation | CMake/vcpkg skeleton, CI matrix, core library, SDL3 window, logging, Tracy, `dg-reflect` MVP, unit tests |
| 1: Sim core | Archetype ECS, fixed-tick loop, deterministic math, RNG, snapshots, checksum, determinism CI with replays |
| 2: Rendering MVP | RHI on Windows, macOS (MoltenVK) and Android, render graph, bindless, glTF import, PBR, ImGui |
| 3: Scripting and physics | Luau sandbox, binding generator, `dg.d.luau`, Jolt and Box2D, hot reload |
| 4: Editor MVP | Docking layout and core panels, gizmos, undo/redo, play mode, scenes, prefabs with overrides |
| 4b: MCP server | `dg::mcp`, stdio bridge, reflection-generated tool schemas, editing and play tools, undo transactions, permission policy |
| 5: Networking | Headless build profile and level-scoped cook, `dg-server`, confirmed-input protocol, predict/rollback, hierarchical hashing, resync and late join |
| 6: Content | FBX import, 2D sprites, atlases and tilemaps, runtime UI and UI Designer |
| 6a: Audio | Audio thread, miniaudio backend, sound events, mixer, rollback-safe playback, then Steam Audio and editor tools |
| 6b: VFX | Particle graph IR, module-stack editor, CPU then GPU runtimes, node-graph view, budgets |
| 6c: Animation | Skeleton and clip import, ozz runtime, GPU skinning, animation graph, sim animation state, IK, Animation Viewer |
| 7: Packaging | `dg-cook`, `.dgpak`, `dg-package` for Windows, macOS and Android, Build Settings window |
| 8: Advanced rendering | Shadows, post-FX, baked GI, DDGI, Lumen-style software GI, hardware ray-tracing path |
| 9: World building | Terrain, foliage, brush modeling, splines, navmesh |
| 10: Ecosystem | Plugin SDK docs, sample games, iOS port, native Metal backend if needed |

Each planned system has its own page under **Planned systems**, starting with the
[Planned systems overview](../planned/overview.md).

## Key risks

| Risk | Mitigation |
|---|---|
| Cross-architecture float determinism is fragile (compiler upgrades, new code paths) | A determinism CI gate on every pull request, pinned compiler versions, sim code isolated in `dg_sim_target`, a banned-function lint |
| MoltenVK/KosmicKrisp feature gaps on macOS | Capability tiers; software GI path without ray tracing; an RHI clean enough for a native Metal backend |
| C++26 reflection missing on Apple, NDK and MSVC toolchains | `dg-reflect` code generation behind macros compatible with future P2996 annotations |
| Resync storms from a deterministic bug | `ResyncPolicy` rate limits; repeated same-tick desyncs reported as bugs with hash-tree diffs |
| Server cost in `verify` mode | `relay` mode for casual rooms; headless builds; many rooms per process |
| Rollback cost with heavy physics | Configurable rollback window, lockstep mode, physics LOD, early Android profiling |
| GPU particle and audio performance on mobile | CPU SIMD particle path and budgets; real-time-safe audio rules and underrun counters |
| Sim and presented animation mismatch | Same source clips and sample times; tolerance tests; inertialization |
| Lumen-style GI complexity | Phased tiers, baked GI and DDGI first, time-boxed research |
| AI agents making unwanted edits through MCP | Undo per call, explicit save, permission levels, project-only file access, activity log |
| MCP specification churn | Pinned revision with version negotiation, isolated protocol adapter, conformance tests |
| Scope | Strict phase gating; every module behind interfaces so features can be delayed |

## Keeping this page current

* When a step changes status, update the table in `docs/IMPLEMENTATION_PLAN.md` and here.
* When a decision changes, record it in an ADR (`docs/adr/NNNN-title.md`), update `docs/DESIGN.md` §18, and update the
  decisions table here.
* When a planned system is implemented, move its page from `planned/` to `systems/` (see
  [Writing documentation](../contributing/documentation.md)).
