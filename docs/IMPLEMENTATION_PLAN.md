# Detergine — Step-by-Step Implementation Plan

This turns the roadmap in [DESIGN.md §17](DESIGN.md) into small, verifiable steps. Each step ends with green builds
(Debug / Development / Release), passing tests, and an unchanged or deliberately re-recorded determinism golden hash.
A step is only marked done when its exit criteria hold on macOS ARM64. Windows x64 is checked once CI exists (step 2).

| # | Step | Scope | Exit criteria | Status |
|---|---|---|---|---|
| 0 | Build system | CMake presets, `dg` tool, launcher GUI, minimal core/sim/runtime, smoke + determinism tests | `./dg test` green in all configs; Debug == Release hash | ✅ done |
| 1 | **Core foundation** | Allocators (heap, linear, pool), generational handles, deterministic math (`dg::fixed32/64`, `dg::detmath`), vector/quaternion/matrix math, temporary test harness | Unit tests green; detmath golden-bit hash recorded; accuracy bounds met; no UB under UBSan/ASan | ✅ done (macOS ARM64) |
| 2 | Dependencies & CI | vcpkg pinned to release 2026.10.16 ✅, **GoogleTest** replaces the harness ✅, GitHub Actions CI (macOS ARM64, Windows x64, Linux x64, ASan/UBSan, Android build) ✅ written, pre-push hook ✅, **spdlog async engine logger** ✅; still to do: Tracy, xxHash (replaces FNV) | CI green on all hosts; **golden hashes identical on x64 and ARM64** | 🚧 in progress |
| 3 | Reflection | `dg::refl` runtime (TypeRegistry, TypeInfo, attributes), `dg-reflect` generator MVP for `DG_REFLECT` / `DG_PROPERTY` | Reflected structs round-trip through a field walker; generator runs in the build | ⏳ |
| 4 | Serialization | Reflection-driven YAML (rapidyaml) and binary archives, schema versioning | Round-trip tests; stable, diff-friendly YAML output | ⏳ |
| 5 | ECS | Archetype storage, queries, systems + scheduler, command buffers, events, snapshot/restore, hierarchical state hash | Snapshot → restore → identical hash; benchmark baseline | ⏳ |
| 6 | Sim world | Project settings (30 Hz default tick rate), input commands, sim world on ECS, replay record/playback, desync dump | Replays reproduce hashes across configs and CI hosts | ⏳ |
| 7 | Job system | enkiTS wrapper, deterministic parallel sim systems (disjoint writes, ordered reductions) | Parallel and serial runs produce identical hashes | ⏳ |
| 8 | Luau | Luau VM + sim/view sandboxes, determinism lint, binding generator from reflection, `dg.d.luau`, `dg_add_game_module` (static C++ game module → Luau) | A Luau-only sample game and a C++-module sample both run deterministically | ⏳ |
| 9 | Physics | Jolt (cross-platform deterministic) + Box2D v3 integration, physics snapshot/restore | Physics replays identical across hosts | ⏳ |
| 10 | Platform + RHI | SDL3 window/input, Vulkan RHI (volk, VMA, vk-bootstrap; MoltenVK on macOS), render graph, bindless, ImGui | Triangle → textured mesh on macOS + Windows, validation clean | ⏳ |
| 11 | Renderer MVP | glTF import, visibility-buffer desktop path, forward+ mobile path, PBR, clustered lights | Sample scene renders; golden-image test | ⏳ |
| 12 | Editor MVP | Docking layout, Hierarchy / Inspector / Scene / Game / Project / Console, gizmos, undo/redo, play mode, scenes + prefabs | Build a level, play it, save it, reload it | ⏳ |
| 13 | MCP server | `dg::mcp` + stdio bridge, scene/entity/component/prefab/script/play tools, undo per call, policy | Claude Code builds and saves a small level through MCP | ⏳ |
| 14 | Netcode | ENet transport, confirmed-input protocol, predict/rollback, hashing, resync, `dg-server` relay → verify, headless level-scoped cook | Two clients + server stay in sync; forced desync is resynced | ⏳ |
| 15+ | Content systems | Animation (§10), audio (§9), VFX (§8.6), UI (§13), GI tiers (§8.5), world-building tools, packaging | Per DESIGN.md roadmap phases 6–10 | ⏳ |

## Step 1 — Core foundation (details)

* `engine/core`: `dg::IAllocator`, `HeapAllocator` (aligned), `LinearAllocator` (arena with markers/reset),
  `PoolAllocator` (fixed-size blocks); `dg::Handle<T>` + `HandlePool<T>` (generational, **FIFO free list** so that ID
  reuse is deterministic).
* `engine/math` (strict-FP module):
  * `dg::fixed32` (Q16.16) and `dg::fixed64` (Q32.32) with exactly specified rounding and overflow behavior. They are
    portable, with no `__int128` dependency (MSVC/clang-cl).
  * `dg::detmath`: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `pow` built only from IEEE-exact
    operations (+ − × ÷, `sqrt`, comparisons, exact conversions), implemented out of line so callers' compile flags
    can't change them.
  * `Vec2/3/4`, `Quat`, `Mat3/4` (f32) with the operations the sim and renderer need first.
* Engine-wide strict floating point: `-ffp-contract=off` / `/fp:precise` on **every** engine target, not only sim
  targets. Inline math in headers would otherwise get different code in sim and non-sim translation units, and the
  linker may keep either copy (an ODR hazard).
* `tests/`: one test executable per module. These started on a tiny temporary harness; step 2 converted them to
  GoogleTest.

### Step 1 results (2026-11-05, macOS ARM64, AppleClang 21)

* **Tests:** 8 suites, all passing in Debug / Development / Release with warnings as errors, and under ASan + UBSan.
* **Accuracy:** `dg::detmath` is correctly rounded except in rare near-halfway cases (max 0.5 ulp, measured exhaustively
  over every finite f32 input for sin/cos/tan/asin/acos/atan/exp/exp2/log/log2; sweeps for atan2/pow). Huge arguments
  use an exact Payne-Hanek reduction. NaN outputs are canonicalized (0x7FC00000) because x64 and ARM64 produce
  different NaN bits.
* **Fixed point:** `dg::fixed32/64` multiply and divide are exact or correctly rounded (ties away from zero), and
  matched an independent 192-bit reference with no mismatches. There is no `__int128`.
* **Review:** 11 confirmed findings across the four areas, 10 fixed with regression tests. Highlights: the platform
  dependent `kDefaultAlignment` is now fixed at 16; `lerp` handles full-range spans; matrix inverses no longer emit NaN
  for near-singular input.
* **Golden hashes** (every platform must reproduce these; checked on x64 in step 2):

  | Suite | Golden |
  |---|---|
  | `dg-core-tests` (handle pool workload) | `0x3a7ea63a09700779` |
  | `dg-fixed-tests` | `0x508880CD2D9E3341` |
  | `dg-detmath-tests` | `0x1BFF52C5DF7021AE` |
  | `dg-vector-tests` | `0xcdffd04806b21b0c` |
  | `sim.determinism` | `0x9f504fe2ce4e5ee2` (unchanged by the engine-wide strict FP switch) |

* **Not yet verified:** Windows x64 (clang-cl / MSVC STL) and Android builds. That waits for CI in step 2.

### Step 2 progress (2026-11-05)

* **Done:**
  * vcpkg pinned to release `2026.10.16` with a matching `builtin-baseline`.
  * GoogleTest 1.18.0 through vcpkg. All suites are converted, independent reviewers mapped all 710 original checks
    with nothing lost, and 20 death tests were added for `DG_ASSERT` paths.
  * 117 individually registered CTest tests (Debug).
  * A GitHub Actions CI workflow and the `dg hooks install` pre-push hook.
* **Build-system fixes found while converting:**
  * Xcode's Debug libc++ hardening is pinned to `none`, because it is ABI-incompatible with vcpkg-built libraries
    (caught as a heap overflow inside GoogleTest under ASan).
  * The `arm64-osx-dg` overlay triplet builds packages for macOS 14.0, the engine's deployment target.
* **CI status:** macOS ARM64, **Linux x64 (golden hashes reproduced bit for bit on x64)**, Linux ASan + UBSan and
  Android are green, and **Windows x64 (clang-cl) is green**: every golden hash reproduces on ARM64 and both x64
  toolchains.
* **Engine manual:** `docs/manual/` → HTML via `./dg docs`. CI's `Manual` job validates it and requires doc updates for
  changed sources (CLAUDE.md, *Documentation is part of every change*).
* **Engine logger:** `dg::log` now runs on spdlog 1.17 in asynchronous mode (one worker, blocking queue by default,
  synchronous Error/Fatal flush, console/file/logcat sinks, `--log-level` / `--log-file` app flags).
* **Remaining:**
  * Tracy, xxHash.
