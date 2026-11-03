# CLAUDE.md — Detergine Engine

Detergine is a **natively deterministic**, modular 3D/2D game engine. Its core systems, physics, and rendering are
in **C++23** with a **Vulkan** renderer. Gameplay is written in **Luau (Lua)** or C++ on a deterministic ECS
simulation, which makes lockstep and rollback multiplayer work out of the box. The editor is ImGui-based and modeled
on Unity.

**Full architecture: [`docs/DESIGN.md`](docs/DESIGN.md). Read the relevant section before changing a subsystem.**
Architectural decisions go into `docs/adr/NNNN-title.md`. Update `DESIGN.md` when a decision changes the design.

## Targets

| Platform | Arch | Editor | Runtime | Graphics |
|---|---|---|---|---|
| Windows 10/11 | x64 | yes | yes | Vulkan 1.3 |
| macOS 14+ | ARM64 (Apple Silicon), native | yes | yes | Vulkan via MoltenVK / KosmicKrisp |
| Android 10+ | ARM64 | no | yes | Vulkan 1.1+ |
| iOS | ARM64 | no | later | Vulkan via MoltenVK |

Every change must build on **Windows x64 and macOS ARM64**. Runtime modules must also build for **Android ARM64**.
Never add platform-specific code outside `engine/platform/` (or `#if` blocks in clearly marked backend files).

## Tech stack (summary — see DESIGN.md §2)

CMake + Ninja + vcpkg (manifest) · SDL3 · Vulkan (volk, VMA, vk-bootstrap) · Slang shaders · Dear ImGui (docking) +
ImGuizmo/ImPlot · Jolt Physics (cross-platform deterministic) · Box2D v3 · Luau · enkiTS · fastgltf · ufbx ·
meshoptimizer · KTX2/Basis Universal · FreeType/HarfBuzz/msdf-atlas-gen · Yoga · miniaudio · ozz-animation ·
Recast/Detour · ENet / GameNetworkingSockets · rapidyaml · zstd/LZ4 · xxHash · Tracy · spdlog · doctest.

Adding a new third-party dependency requires a permissive license (MIT/BSD/Zlib/Apache-2/PD), all-platform support,
a vcpkg port (or vendored copy in `third_party/` with its LICENSE), and a line in DESIGN.md §2. **No GPL/LGPL in the engine.**

## The determinism rules (non-negotiable)

The sim must produce **bit-identical** results on x64 and ARM64. Code in the **sim layer**
(`engine/sim`, `engine/ecs` sim paths, `engine/physics`, `engine/net` lockstep, `*.sim.luau`, and any component tagged `Sim`)
must follow these rules:

1. **No wall-clock time, no OS calls, no file IO, no threads you spawn yourself.** The sim sees only `tick`, fixed `dt`, and inputs.
2. **No `std::` transcendental math** (`sin`, `cos`, `atan2`, `exp`, `pow`, …). Use `dg::detmath`. `+ - * / sqrt` are fine.
3. **No `-ffast-math`, no FMA contraction.** Sim targets are declared with `dg_sim_target()` in CMake, which sets the
   strict FP flags. Do not override them.
4. **No iteration over `unordered_*` containers, and no ordering by pointer/address.** Use sorted/dense containers or entity order.
5. **No `rand()`, `std::random_device`, or time seeds.** Use `dg::sim::Rng`, which lives in the sim state.
6. **All sim state lives in ECS components** (or registered sim resources) so that snapshots and rollback capture it.
   No hidden statics or caches holding gameplay state.
7. **Parallel sim work writes disjoint data only.** Reductions and event merges happen afterward, in deterministic order.
8. **Zero-initialize sim components.** Checksums hash reflected fields, never raw bytes or padding.
9. **The presentation layer never writes sim state.** The only path into the sim is `InputCommand`s.
10. **Lua sim scripts:** no globals/upvalue state, no `pairs`/`next` over hash tables (use `dg.ordered` / `ipairs`),
    no `os`/`io`/`debug`/`collectgarbage`/`__gc`/weak tables, and use `dg.rng` and `dg.math` only.

Any change that touches sim code must pass the **determinism replay tests** on both x64 and ARM64. If you cannot
run them locally on both architectures, say so explicitly in the PR/summary.

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

- `--!strict` at the top of every engine-provided and template script.
- File suffix shows where it runs: `*.sim.luau` (deterministic), `*.view.luau` (presentation), `*.editor.luau` (editor tooling).
- Prefer ECS-style `dg.system { phase, query, ... }` over per-object update callbacks.
- Bindings are **generated** from reflection (`ScriptVisible`). Don't hand-write glue. Regenerate `dg.d.luau` when APIs change.

## Build & test

```bash
cmake --preset macos-arm64-dev      # or windows-x64-dev, android-arm64-dev
cmake --build --preset macos-arm64-dev
ctest --preset macos-arm64-dev      # unit + integration
ctest --preset macos-arm64-dev -L determinism
```

(These presets are the target layout; create them as the build system lands. Keep this section in sync with reality.)

- Every bug fix comes with a regression test. Every new sim system comes with a replay-based determinism test.
- Renderer changes: add or update a golden-image test where practical, and run with Vulkan validation layers clean.
- Don't claim something works on a platform you didn't build or test it on.

## Legal / IP

- **Never copy Unreal Engine code or shaders** (Unreal EULA). The Lumen-style GI is a clean-room implementation
  based on public papers and talks (DESIGN.md §8.5). The same applies to Unity code and assets.
- The editor is *inspired by* Unity's layout and workflows. Don't copy Unity's icons, trademarks, or proprietary assets.

## Working agreements for AI agents

- Read the relevant DESIGN.md section first. If a request conflicts with these rules, especially the determinism rules,
  stop and say so instead of working around it.
- Keep changes scoped to one module where possible. Don't reorganize directories or swap dependencies without an ADR.
- Prefer extending through interfaces/plugins over modifying core modules.
- Don't commit or push unless asked. Commit messages: imperative mood, `<module>: <summary>` (e.g. `ecs: add chunk snapshot`).
