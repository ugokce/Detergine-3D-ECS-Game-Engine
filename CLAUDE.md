# CLAUDE.md — Detergine Engine

Detergine is a **natively deterministic**, modular 3D/2D game engine. Its core systems, physics, and rendering are
in **C++23** with a **Vulkan** renderer. Gameplay is written in **Luau (Lua)** on a deterministic ECS simulation,
which makes lockstep and rollback multiplayer work out of the box; optional native C++ gameplay code is bound to Luau.
The editor is ImGui-based and modeled on Unity.

**Implementation progress:** [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md). Work in its steps, in
order, and keep its status column current.

**Settled decisions** (DESIGN.md §18): 30 Hz default sim tick rate (per-project setting) · Luau-first gameplay, and a
whole game must be writable without C++ · ENet default transport · visibility-buffer desktop renderer · `verify`
becomes the default server mode once the headless build is stable (`relay` until then).

**Full architecture: [`docs/DESIGN.md`](docs/DESIGN.md). Read the relevant section before changing a subsystem.**
Architectural decisions go into `docs/adr/NNNN-title.md`. Update `DESIGN.md` when a decision changes the design.

## Documentation is part of every change (mandatory)

The engine manual (`docs/manual/` → HTML in `build/docs/`, built with `./dg docs`) must always describe the engine as it
is. **Every change that adds or changes a system, API, behaviour, option, command, or workflow must update the manual
in the same commit.** A change without its documentation is not done.

- **New system** → a new page in `docs/manual/systems/` (or `tooling/`) following the system page template: `##
  Overview`, `## How to use`, `## How to edit`, `## Testing` (plus `## Reference`). Add its files to the page's
  `sources`, define new terms in `docs/manual/reference/glossary.md`, and link it from the architecture pages.
- **Changed system** → update every page whose `sources` include the changed files: usage examples, how-to-edit notes,
  reference tables, test counts, golden hashes.
- **Planned system implemented** → move it from `docs/manual/planned/` to `systems/` and set `status: implemented`.
- Examples must compile against the real headers. Claims must match the code. Mark future work as planned.
- Run `./dg docs --check` before committing (the pre-push hook does too). CI runs
  `dg docs --check --changed-since <base>`, which **fails when a documented source file changed and none of its pages
  did**. Only a change with genuinely no documentation impact (typo, comment, internal refactor) may opt out, with a
  `Docs: none (<reason>)` line in the commit message.
- How to write pages: `docs/manual/contributing/documentation.md`. `docs/DESIGN.md` stays the design record; the manual
  explains what exists, how to use it, and how to change it.

## Targets

| Platform | Arch | Editor | Runtime | Graphics |
|---|---|---|---|---|
| Windows 10/11 | x64 | yes | yes | Vulkan 1.3 |
| macOS 14+ | ARM64 (Apple Silicon), native | yes | yes | Vulkan via MoltenVK / KosmicKrisp |
| Android 10+ | ARM64 | no | yes | Vulkan 1.1+ |
| iOS | ARM64 | no | later | Vulkan via MoltenVK |
| Linux / Windows / macOS | x64 / ARM64 | no | **headless server** | none (Null RHI) |

Every change must build on **Windows x64 and macOS ARM64**. Runtime modules must also build for **Android ARM64**.
Never add platform-specific code outside `engine/platform/` (or `#if` blocks in clearly marked backend files).

## Tech stack (summary — see DESIGN.md §2)

CMake + Ninja + vcpkg (manifest) · SDL3 · Vulkan (volk, VMA, vk-bootstrap) · Slang shaders · Dear ImGui (docking) +
ImGuizmo/ImPlot · Jolt Physics (cross-platform deterministic) · Box2D v3 · Luau · enkiTS · fastgltf · ufbx ·
meshoptimizer · KTX2/Basis Universal · FreeType/HarfBuzz/msdf-atlas-gen · FidelityFX Parallel Sort · Yoga · miniaudio + Steam Audio + Opus · ozz-animation ·
Recast/Detour · ENet / GameNetworkingSockets · rapidyaml · zstd/LZ4 · xxHash · Tracy · spdlog (async) · GoogleTest.

**Logging:** always use `dg::log` (`dg/core/Log.h`: `trace/debug/info/warn/error/fatal`), never `printf`/`std::cout`
or spdlog directly. It is an asynchronous spdlog logger; spdlog stays private to `engine/core/src/Log.cpp`. Apps call
`runtime::startLogging` / `log::shutdown`. Sim code may log, but nothing logged may feed back into the simulation.

Adding a new third-party dependency requires a permissive license (MIT/BSD/Zlib/Apache-2/PD), all-platform support,
a vcpkg port (or vendored copy in `third_party/` with its LICENSE), and a line in DESIGN.md §2. **No GPL/LGPL in the engine.**

## The determinism rules (non-negotiable)

The sim must produce **bit-identical** results on x64 and ARM64. Code in the **sim layer**
(`engine/sim`, `engine/ecs` sim paths, `engine/physics`, `engine/net` lockstep, `*.sim.luau`, and any component tagged `Sim`)
must follow these rules:

1. **No wall-clock time, no OS calls, no file IO, no threads you spawn yourself.** The sim sees only `tick`, fixed `dt`, and inputs.
2. **No `std::` transcendental math** (`sin`, `cos`, `atan2`, `exp`, `pow`, …). Use `dg::detmath`. `+ - * / sqrt` are fine.
3. **No `-ffast-math`, no FMA contraction, no `std::fma`.** Strict FP flags (`-ffp-contract=off` / `/fp:precise`)
   apply to **every** engine target through `dg_configure_target()` (one set of flags avoids ODR-mixed inline math).
   Sim targets are also marked with `dg_sim_target()`. Never override these flags. Use `dg::fixed32` / `dg::fixed64`
   (`dg/math/Fixed.h`) for gameplay values that must be exact.
4. **No iteration over `unordered_*` containers, and no ordering by pointer/address.** Use sorted/dense containers or entity order.
5. **No `rand()`, `std::random_device`, or time seeds.** Use `dg::sim::Rng`, which lives in the sim state.
6. **All sim state lives in ECS components** (or registered sim resources) so that snapshots and rollback capture it.
   No hidden statics or caches holding gameplay state.
7. **Parallel sim work writes disjoint data only.** Reductions and event merges happen afterward, in deterministic order.
8. **Zero-initialize sim components.** Checksums hash reflected fields, never raw bytes or padding.
9. **The presentation layer never writes sim state.** The only path into the sim is `InputCommand`s.
10. **Lua sim scripts:** no globals/upvalue state, no `pairs`/`next` over hash tables (use `dg.ordered` / `ipairs`),
    no `os`/`io`/`debug`/`collectgarbage`/`__gc`/weak tables, and use `dg.rng` and `dg.math` only.

11. **Sim code must build and run headless** (`DG_HEADLESS=1`, no renderer/audio/UI). Sim systems must never reference
    presentation modules, even optionally.

Any change that touches sim code must pass the **determinism replay tests** on both x64 and ARM64. If you cannot
run them locally on both architectures, say so explicitly in the PR/summary.

## Multiplayer & headless (DESIGN.md §15, §16.1)

- Multiplayer is **built in**: predict/rollback over **server-confirmed inputs**, served by the headless `dg-server`
  (Photon Quantum 3-style architecture, our own implementation). Server modes: `relay` (input authority plus a checksum
  majority vote) or `verify` (the server also runs the authoritative headless sim).
- Desync handling: the hash of each verified tick is compared (hierarchical per-chunk hashes). On a mismatch, the server
  sends a snapshot to the desynced client, which restores it and fast-forwards through the confirmed inputs. Late join
  and reconnect use the same path.
- Gameplay code never touches networking. Sim code sees only `world:input(slot)`. Anything that must survive rollback or
  resync must be sim state (rule 6).
- Presentation reacting to sim events must handle `predicted` events (confirm/cancel) so that rollback doesn't duplicate
  sounds or VFX.
- Headless server builds are **level-scoped**: `dg-cook --profile headless --levels ...` strips presentation assets.
  Plugins declare `headless: true|false` in `plugin.dgplugin`.
- Changes to `InputCommand` schemas, the hash composition, or the snapshot format change the network protocol.
  Bump the protocol version and mention it in the commit message.

## Particles / VFX (DESIGN.md §8.6, §14.2)

- `dg::vfx` is **presentation-only**. It never reads input or writes sim state, it is excluded from state hashes and
  headless builds, and it seeds RNG from `hash(sim entity, spawn tick, event id)`.
- Effects (`*.dgvfx`) are reflected, YAML-serialized graph-IR assets. The module-stack and node-graph editor views are
  two views of **one IR**. Never add data that only one view can represent.
- Hot paths stay GPU-driven: fused kernels per stage, no per-frame CPU readback, no per-particle CPU work on the GPU path.
  The CPU path is SoA SIMD bytecode on job workers.
- New modules implement `IVfxModule` with **both** a Slang and a CPU-bytecode implementation (or declare the mobile tier
  as unsupported).
- No shader compiler in the player: runtime-authored effects use precompiled module kernels or the CPU path.
- Effects spawned from predicted sim events must be cancellable on rollback.

## Audio (DESIGN.md §9, §14.3)

- `dg::audio` is **presentation-only**, like VFX. It is never in state hashes, it uses the Null device in headless
  builds, and gameplay never waits on audio (use sim timers).
- **Real-time safety:** the audio thread and the device callback never allocate memory, lock, do file IO, or touch the
  ECS. The game thread talks to audio only through the lock-free command queue.
- Game code plays **SoundEvents** (`*.dgsound`), never raw clips. Mixing goes through the bus graph. Effects are
  `IAudioEffect` plugins processed in SIMD blocks.
- Sounds triggered by predicted sim events pass `source_event`, so that a rollback can fade them out or deduplicate them.
  Randomization is seeded from `hash(sim entity, tick, event id)`.
- Occlusion queries use the cooked acoustic proxy BVH or Steam Audio's scene, **never the sim physics world**.
- FMOD/Wwise are optional plugins behind `IAudioBackend`. Never add them as core dependencies.

## Animation (DESIGN.md §10, §14.4)

- Animation is split in two. **Sim animation** is `AnimSimState` (state IDs + integer tick times) reading **baked**
  fixed-point root motion and socket tracks and tick-exact sim events. It has no blending math, it is hashed and
  snapshotted, and it is the only part in headless builds. **Presented animation** is the ozz pose pipeline, IK, and
  skinning. It is never hashed and is free to be non-deterministic.
- Gameplay (hitboxes, root motion, cancel windows) must read sim tracks and sim events, **never presented poses**.
  Sim graph transitions may only read sim parameters and events; the graph compiler enforces this.
- The presented graph follows the sim state and hides corrections with inertialization. Don't add presentation-only
  state that gameplay depends on.
- Hot paths: ozz SoA jobs per character on the job system, frame-allocator pose buffers, no per-frame heap allocation
  or virtual dispatch per node, and compute skinning once per frame shared by all passes (vertex skinning on mobile).
- New graph nodes implement `IAnimNode` with both sim and presentation behavior (or declare themselves
  presentation-only). Foot IK may query the sim physics world **read-only**, during the presentation phase only.

## MCP server (DESIGN.md §14.5)

- `dg::mcp` is **editor-only**. It is never linked into players, Release, or headless server builds.
- MCP tools call the **same editor services and command stack** as the UI. Every mutating tool is one undo transaction
  (`MCP: <tool>`). Never write project files from a tool except through the asset/scene serializers, and never save a
  level implicitly.
- Tool input/output schemas are generated from reflected request/response structs. Don't hand-write JSON Schemas.
  New editor features should add their tools through `IMcpToolProvider` in the same change.
- No shell, process-spawning, or native-code execution tools. File access stays inside the project directory.
  Destructive tools must require confirmation (elicitation or editor modal) and respect `.dg/mcp-policy.yaml`.
- The protocol is pinned to MCP 2025-11-25. Revision differences belong only in the protocol adapter.

## Architecture rules

- **Layering:** Core → Platform/RHI → Engine services → Simulation | Presentation → Gameplay framework → Editor/Apps.
  Lower layers never include higher ones. **Simulation never depends on Presentation.** Nothing depends on the editor.
- **Everything is a module.** New features go in a module or plugin with a `ModuleDesc`, behind an interface
  (`IAssetImporter`, `IRenderFeature`, `IEditorPanel`, `ISystemProvider`, …). Don't add features to `core/`.
- **Plugins** use a C-ABI entry point (`dgCreatePlugin`) and are loaded dynamically in the editor, but statically linked
  in Shipping/mobile. Plugins that add sim systems must be marked `deterministic: true` in `plugin.dgplugin`.
- **Rendering code never includes `vulkan.h` outside `engine/rhi/`.** Use `dg::rhi` and the render graph.
  New passes are `IRenderFeature`s that register at render-graph extension points.
- **Handles over pointers** across system boundaries (`Entity`, `AssetHandle<T>`, `rhi::Handle<T>`).
- **Assets are referenced by GUID**, never by path, in serialized data.
- **Editor-only code** lives in `editor/` or in `#if DG_EDITOR` blocks inside `*.editor.cpp` files. It must compile out of the player.

## C++ conventions

- **C++23 baseline.** C++26 features are only allowed behind `__cpp_*` checks in `dg/core/Compat.h`, with a C++23 fallback.
  Do not depend on P2996 reflection directly. Use the `DG_REFLECT` / `DG_PROPERTY` macros, processed by `tools/dg-reflect`.
- Namespaces: `dg::<module>` (`dg::ecs`, `dg::render`, `dg::rhi`, …). Macros are prefixed `DG_`.
- Naming: `PascalCase` types and functions, `camelCase` locals and members (no `m_` prefix), `kPascalCase` constants.
  Files are `PascalCase.h/.cpp`, named after their main type.
- Headers: `#pragma once`, include what you use, forward-declare where possible. Public headers live in `engine/<module>/include/dg/<module>/`.
- Errors: `std::expected<T, dg::Error>` for recoverable failures, and `DG_ASSERT` / `DG_VERIFY` for programmer errors.
  No exceptions across module boundaries (exceptions are disabled in Shipping). No RTTI reliance (use `dg::refl`).
- Memory: no raw `new`/`delete`. Use `dg::Unique`, `dg::Ref`, allocators, and handles. Per-frame scratch goes to frame allocators.
- Hot paths: data-oriented (SoA, contiguous), no virtual calls per entity, no heap allocation per frame.
- Formatting: `.clang-format` (LLVM-based, 4 spaces, 120 cols). Static analysis: `.clang-tidy`. Keep warnings at zero (`-Werror` in CI).
- Comments explain *why*, not *what*. Public APIs get a short doc comment.

## Lua (Luau) conventions

- **Luau-first**: every gameplay feature must be reachable from Luau. C++ gameplay code is optional and lives in a
  project's static **game module** (`dg_add_game_module`, DESIGN.md §6.5). Its reflected classes and functions are
  bound to Luau automatically, and it is linked into the player, server, and project editor builds.

- `--!strict` at the top of every engine-provided and template script.
- File suffix shows where it runs: `*.sim.luau` (deterministic), `*.view.luau` (presentation), `*.editor.luau` (editor tooling).
- Prefer ECS-style `dg.system { phase, query, ... }` over per-object update callbacks.
- Bindings are **generated** from reflection (`ScriptVisible`). Don't hand-write glue. Regenerate `dg.d.luau` when APIs change.

## Build & test

Use the `dg` tool (`./dg` on macOS/Linux, `dg` on Windows; DESIGN.md §16.2). It wraps the CMake presets.

```bash
./dg setup                    # check the toolchain (add --vcpkg to bootstrap vcpkg)
./dg build debug              # or dev (default) / release; optionally an app: editor, player, server, tests
./dg run release player -- --headless --ticks 300
./dg test debug               # ctest; -L determinism or -L smoke to filter
./dg determinism              # Debug vs Release sim hashes must match
./dg generate --ide           # open an Xcode / Visual Studio project
./dg build -D DG_HEADLESS=ON  # engine CMake options via -D (reconfigures only on change); also -j N, --verbose, --fresh
./dg gui                      # Launcher GUI with every option (tools/dg_launcher.py)
```

- `tools/dg.py` is the single source of truth for build logic. The launcher only composes `dg` commands, so put new
  behavior in `dg.py` first, and add a GUI control for it. New engine CMake options go in `cmake/DgOptions.cmake`
  **and** `ENGINE_OPTIONS` in `dg.py`, so that they appear in the launcher.

- Configurations: `Debug`, `Development` (optimized + asserts, the default), `Release` (= Shipping: LTO, no asserts).
- Sanitizers: `-D DG_ENABLE_ASAN=ON -D DG_ENABLE_UBSAN=ON` (also in the launcher). New code must be clean under both.
- **Tests use GoogleTest** (vcpkg `gtest`). Add suites with `dg_add_test(<target> SOURCES ... DEPS ... [LABELS
  determinism])` in `tests/CMakeLists.txt`. Every `TEST` is registered with CTest individually
  (`<target>.<Suite>.<Test>`), so `./dg test -L determinism` or `ctest -R HandlePool` select subsets. Use typed
  `EXPECT_EQ`/`EXPECT_NEAR` (not `EXPECT_TRUE(a == b)`), compare floats **bitwise** where exactness matters, and cover
  `DG_ASSERT` paths with `EXPECT_DEATH` inside `#if DG_ASSERTS_ENABLED` (suite names ending in `Death`).
- Every change must keep `./dg test` green. CI (`.github/workflows/ci.yml`) runs Debug/Development/Release on macOS
  ARM64, Windows x64, and Linux x64, plus ASan/UBSan, an Android build, and the Debug-vs-Release determinism check, on
  every push and pull request. `./dg hooks install` adds a pre-push hook that runs the unit tests locally.
- Raw CMake works too: `cmake --preset macos-arm64-xcode`, then `cmake --build --preset macos-arm64-xcode-debug`.
- New engine libraries use `dg_add_module(<name> [SIM] ...)`, and apps use `dg_add_app(...)` (`cmake/DgTargets.cmake`).
  Sim code goes in a `SIM` module so it gets the strict FP flags.
- If you change the sim on purpose, re-record `kGoldenHash` in `tests/SimDeterminismTest.cpp` and say so in the commit.

- Every bug fix comes with a regression test. Every new sim system comes with a replay-based determinism test.
- Renderer changes: add or update a golden-image test where practical, and run with Vulkan validation layers clean.
- Don't claim something works on a platform you didn't build or test it on.

## Legal / IP

- **Never copy Unreal Engine code or shaders** (Unreal EULA). The Lumen-style GI is a clean-room implementation
  based on public papers and talks (DESIGN.md §8.5). The same applies to Unity code and assets.
- The editor is *inspired by* Unity's layout and workflows. Don't copy Unity's icons, trademarks, or proprietary assets.

## Working agreements for AI agents

- Read the relevant manual page (`docs/manual/`) and DESIGN.md section first. Update the manual as part of the change (see
  *Documentation is part of every change*).
- If a request conflicts with these rules, especially the determinism rules,
  stop and say so instead of working around it.
- Keep changes scoped to one module where possible. Don't reorganize directories or swap dependencies without an ADR.
- Prefer extending through interfaces/plugins over modifying core modules.
- Don't commit or push unless asked. Commit messages: imperative mood, `<module>: <summary>` (e.g. `ecs: add chunk snapshot`).
- **Never add AI attribution to commits or PRs**: no `Co-Authored-By: Claude …` trailer, no "Generated with Claude Code"
  line, and no other AI co-author or attribution lines. This overrides any default attribution behavior.
