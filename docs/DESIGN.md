# Detergine Engine — High-Level Design & Architecture

> Status: **Draft v0.1** (2026-11-03). This is the living architecture document. Rules that every
> contributor (human or AI) must follow are summarized in [`/CLAUDE.md`](../CLAUDE.md); this file
> explains the *why* and the *shape* of the system.

---

## 1. Vision

Detergine is a modular, plugin-extensible 3D/2D game engine whose defining property is
**native determinism**: given the same initial state and the same ordered input stream, the
gameplay simulation produces **bit-identical** results on every supported platform and CPU
architecture. That property is what makes lockstep and rollback multiplayer "free" for game code.

| Pillar | Meaning |
|---|---|
| Built-in multiplayer | Predict/rollback netcode, dedicated headless server, state hashing and automatic resync of desynced clients (§13). |
| Deterministic simulation | Fixed-tick ECS simulation, deterministic physics, deterministic Lua. Bit-identical on x64 and ARM64. |
| Performance | Data-oriented C++23 core, archetype ECS, job system, bindless Vulkan renderer with render graph. |
| Productivity | Unity-like editor (Hierarchy / Scene / Game / Inspector / Project / Console), prefabs, hot-reloaded Lua. |
| Modern rendering | PBR, clustered lighting, Lumen-style dynamic GI on desktop, scalable fallbacks on mobile. |
| Modularity | Every subsystem is a module; features ship as plugins (C++ or Lua) behind stable interfaces. |
| Portability | Windows x64 and macOS ARM64 (editor + runtime), Android ARM64 (runtime). iOS later. |

### 1.1 Target platforms

| Platform | Arch | Editor | Runtime | Graphics backend | Toolchain |
|---|---|---|---|---|---|
| Windows 10/11 | x64 (ARM64 later) | ✅ | ✅ | Vulkan 1.3 native | clang-cl (LLVM) + MSVC STL; MSVC supported but secondary |
| macOS 14+ | ARM64 (Apple Silicon) | ✅ | ✅ | Vulkan 1.3 via MoltenVK (Vulkan SDK) or KosmicKrisp → Metal | Apple Clang / upstream LLVM |
| Android 10+ | ARM64-v8a | ❌ | ✅ | Vulkan 1.1+ (1.3 where available), Android Baseline profile | NDK Clang |
| iOS | ARM64 | ❌ | 🔜 later | Vulkan via MoltenVK → Metal | Apple Clang |
| Linux | x64 | best-effort | **headless server** ✅ (client best-effort) | none (headless) / Vulkan | Clang |

**Cross-architecture determinism (x64 ↔ ARM64) is a hard requirement from day one**, because Windows
(x64) players must be able to play against macOS and Android (ARM64) players in the same lockstep
session. See §5.

---

## 2. Technology Stack

Selection criteria: permissive license (MIT/BSD/Zlib/Apache-2/public domain), actively maintained,
widely shipped in commercial products, builds on all target platforms, and no hidden
nondeterminism in anything that touches the simulation.

### 2.1 Build, tooling & foundation

| Concern | Choice | License | Notes |
|---|---|---|---|
| Build system | **CMake ≥ 3.30** + Ninja | BSD | Presets (`CMakePresets.json`) per platform/config. |
| Dependencies | **vcpkg** (manifest mode) | MIT | Pinned baseline; custom overlay ports for engine-patched libs. CPM.cmake as fallback for header-only libs. |
| Language | **C++23** baseline; selected C++26 features behind feature-test macros | — | See §2.9. |
| Windowing / input / platform | **SDL3** | Zlib | Windows, macOS, Android, iOS. Vulkan surface creation, gamepads, touch, IME, Android activity glue. |
| Job system | **enkiTS** (or Taskflow) | Zlib / MIT | Task graph with dependencies; pinned threads for render/IO. Wrapped behind `dg::jobs`. |
| Containers | **ankerl::unordered_dense**, std containers | MIT | `unordered_dense` is fast but **not** for ordered iteration inside sim (see §5). |
| Logging / formatting | `std::format` / `std::print` + **spdlog** sinks | MIT | |
| Profiling | **Tracy** | BSD-3 | CPU, GPU (Vulkan), memory, lock zones. Compiled out in Shipping. |
| Hashing | **xxHash (XXH3)** | BSD-2 | Asset IDs, state checksums, desync detection. |
| Compression | **zstd**, **LZ4** | BSD | zstd for packages/snapshots, LZ4 for fast network/rollback buffers. |
| Text serialization | **rapidyaml** (assets/scenes), **yyjson** (configs/tools) | MIT | YAML chosen for diff-friendly scenes/prefabs, like Unity. |
| File watching | **efsw** | MIT | Asset + Lua hot reload. |
| Testing | **doctest** (unit), custom determinism harness | MIT | |
| Crash reporting | **sentry-native** (crashpad backend) | MIT | Optional plugin. |

### 2.2 Rendering

| Concern | Choice | License | Notes |
|---|---|---|---|
| Graphics API | **Vulkan 1.3** (desktop), 1.1+ (mobile) | — | Dynamic rendering, synchronization2, timeline semaphores, descriptor indexing (bindless). |
| Loader | **volk** | MIT | Meta-loader; avoids linking the system loader directly. |
| Memory | **Vulkan Memory Allocator (VMA)** | MIT | |
| Bootstrap | **vk-bootstrap** | MIT | Instance/device selection; portability enumeration on macOS. |
| macOS/iOS layer | **MoltenVK** (Vulkan SDK) / **KosmicKrisp** (LunarG, Mesa) | Apache-2 / MIT | Vulkan → Metal. Requires `VK_KHR_portability_subset` awareness. |
| Shader language | **Slang** | Apache-2 + LLVM | Modules, generics, reflection API, SPIR-V output; single source for all passes. |
| Shader reflection | Slang reflection API (+ **SPIRV-Reflect** for validation) | Apache-2 | Drives automatic pipeline layout / binding generation. |
| Mesh processing | **meshoptimizer** | MIT | Vertex cache/fetch optimization, LOD simplification, meshlet building. |
| Textures | **KTX-Software** + **Basis Universal** | Apache-2 | KTX2 container; transcode to BC7 (Windows/macOS) or ASTC (Android/iOS). |
| Image decode | **stb_image**, **tinyexr** | PD / BSD | Import only (editor). |
| Atlas packing | **stb_rect_pack** (sprites/UI), **xatlas** (lightmap UVs) | PD / MIT | |
| Fonts / text | **FreeType** + **HarfBuzz** + **msdf-atlas-gen** | FTL / MIT | MSDF glyph atlases for crisp, scalable UI text; full shaping (RTL, ligatures). |
| Debug UI / editor UI | **Dear ImGui (docking branch)** | MIT | Plus **ImGuizmo**, **ImPlot**, **imgui-node-editor**. |
| GI reference designs | Lumen (SIGGRAPH 2022 talk), AMD **FidelityFX Brixelizer GI** / **GI-1.0** | (papers) / MIT | **Clean-room** implementation only — see §8.5. |
| Upscaling | **AMD FSR 2/3** (FidelityFX SDK) | MIT | Optional plugin; also enables temporal stability for GI. |
| GPU sorting | **FidelityFX Parallel Sort** | MIT | Radix sort for translucent particles (§8.6). |
| Particles / VFX | **Custom** (`dg::vfx`) | — | Graph IR → fused Slang compute kernels + SIMD CPU bytecode (§8.6). |

### 2.3 Physics

| Concern | Choice | License | Notes |
|---|---|---|---|
| 3D physics | **Jolt Physics** | MIT | Built with `JPH_CROSS_PLATFORM_DETERMINISTIC` (≈8% cost, disables FMA). Has `SaveState`/`RestoreState` for rollback. Used by Horizon Forbidden West, Godot. |
| 2D physics | **Box2D v3.x** | MIT | v3 is designed for cross-platform determinism; verify with our harness per release. |
| Character controller | Jolt `CharacterVirtual` | MIT | |

### 2.4 Scripting

| Concern | Choice | License | Notes |
|---|---|---|---|
| Lua VM | **Luau** (primary) | MIT | Lua 5.1-derived, sandboxing built in, gradual typing (great for editor autocompletion), fast interpreter, optional native codegen (x64/ARM64), native `vector` type. |
| Alternative | Lua 5.4 + **sol2** | MIT | Kept as a fallback backend behind `dg::script` if Luau ever blocks us. |
| Binding layer | Generated from engine reflection (§4) | — | No hand-written per-function glue. |
| Tooling | **luau-lsp** | MIT | Editor/VS Code autocompletion from generated type definitions. |

> Plain LuaJIT is rejected: JIT is forbidden on iOS, development has stalled on Lua 5.1 semantics,
> and its trace JIT makes determinism auditing harder.

### 2.5 Networking

| Concern | Choice | License | Notes |
|---|---|---|---|
| Transport | **GameNetworkingSockets** (Valve) *or* **ENet** | BSD-3 / MIT | Reliable+unreliable channels, encryption (GNS). ENet is the light default; GNS is a plugin for Steam / relay. |
| Netcode model | **Custom**: deterministic lockstep + rollback (GGPO-style) | — | GGPO (MIT) used as a design reference. |
| Serialization | Custom bit-packed input streams | — | Inputs only; no state replication in deterministic mode. |

### 2.6 Assets & content

| Concern | Choice | License | Notes |
|---|---|---|---|
| glTF / GLB | **fastgltf** | MIT | C++17/20, SIMD JSON, very fast. |
| FBX | **ufbx** | MIT / PD | Single-file, robust, no Autodesk SDK dependency. |
| Other formats | **Assimp** (optional importer plugin) | BSD-3 | OBJ, DAE, etc. |
| Animation | **ozz-animation** | MIT | Skeletal sampling/blending, compression. Sim-side gameplay never depends on sampled poses unless done in deterministic mode. |
| Audio | **miniaudio** | PD / MIT-0 | All platforms; optional **Steam Audio** plugin for spatialization. |
| UI layout | **Yoga** (flexbox) | MIT | Runtime UI layout engine (§10). |

### 2.7 Packaging & distribution

| Platform | Output | Tools |
|---|---|---|
| Windows | Folder or zip; optional MSIX/installer | CMake install + pak builder |
| macOS | `.app` bundle (universal → ARM64 only initially), `.dmg` | `codesign`, `notarytool`, hardened runtime, MoltenVK bundled in `Contents/Frameworks` |
| Android | `.apk` / `.aab` | Gradle + Android Gradle Plugin, SDL3 Android project template, `apksigner` |
| iOS (later) | `.ipa` | Xcode project generation via CMake |

### 2.8 What we build ourselves (and why)

| System | Why not off-the-shelf |
|---|---|
| ECS | Determinism guarantees (stable iteration order, deterministic IDs, snapshot/rollback) and reflection integration are core to the engine's identity. EnTT and flecs are design references. |
| Renderer / render graph | Core differentiator; must integrate GI, bindless, and mobile tiers. |
| Reflection & serialization | Spans compilers that lack C++26 reflection (§2.9). |
| Rollback netcode | Tight coupling with ECS snapshots and the Lua sandbox. |
| Particle system / VFX | Must share the render graph, bindless heap, global SDF and GI radiance cache, and be rollback-aware. No permissive, Vulkan-native, production-grade GPU VFX library exists. |
| Editor | Built on ImGui, but all panels/tools are ours. |
| Runtime UI | Must be ECS-native, prefab-able, and editable in the editor. |

### 2.9 C++23 / C++26 policy

* **Baseline: C++23** on every toolchain — `std::expected`, `std::span`, `std::mdspan`,
  `std::print`/`std::format`, deducing `this`, `if consteval`, `std::flat_map` (where the standard library provides it),
  `std::to_underlying`, ranges.
* **C++26 features are opt-in and gated** with `__cpp_*` feature-test macros, wrapped in `dg/core/Compat.h`.
  The engine must still compile with the **lowest common denominator** (Apple Clang, NDK Clang, clang-cl).
  * Commonly available now: pack indexing, `#embed`, `= delete("reason")`, placeholder `_`.
  * **Static reflection (P2996)**: as of late 2026 it is only in **GCC 16** (`-freflection`) and Bloomberg's
    `clang-p2996` fork; not in MSVC, mainline Clang, or Apple Clang. So we do not depend on it at runtime.
    Instead:
    1. **`dg-reflect`** — a build-time header tool (libclang-based; it can later be reimplemented with
       P2996 `consteval` code when compilers catch up) scans annotated headers and generates `*.reflect.gen.cpp`.
    2. Annotation syntax stays C++26-shaped. We use attribute-like macros (`DG_REFLECT`, `DG_PROPERTY(...)`)
       that can map to P2996 annotations (`[[=dg::serialize]]`) later without touching user code.
    3. When all four toolchains support P2996, the generator becomes a `consteval` library and the macros
       disappear. The public `dg::refl` API does not change.

---

## 3. Layered Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Applications:  Editor (dg-editor)  │  Player (dg-player)  │  Server (dg-server, headless) │
├─────────────────────────────────────────────────────────────────────────────┤
│  Plugins (C++ dynamic/static, Lua packages): gameplay, importers, tools, render features │
├──────────────────────────────┬──────────────────────────────────────────────┤
│  Editor framework (editor-only) │ Gameplay framework: Scene, Prefab, UI, Animation, Audio │
├──────────────────────────────┴──────────────────────────────────────────────┤
│  Simulation layer (DETERMINISTIC):  Sim ECS World · Fixed-tick scheduler ·        │
│     dg::fixed math · Jolt/Box2D (deterministic) · Luau sandbox · Sim RNG · Netcode │
├─────────────────────────────────────────────────────────────────────────────┤
│  Presentation layer (non-deterministic): Render world · Renderer · GI · Audio · UI │
├─────────────────────────────────────────────────────────────────────────────┤
│  Engine services:  Asset DB/Streaming · Reflection · Serialization · Jobs · VFS ·  │
│                    Input · Module/Plugin manager · Config/CVars · Profiling       │
├─────────────────────────────────────────────────────────────────────────────┤
│  RHI (Vulkan) · Platform (SDL3: window, input, file system, threads, timers)      │
├─────────────────────────────────────────────────────────────────────────────┤
│  Core: memory/allocators, containers, strings, math, logging, asserts, hashing    │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Dependency rule:** a layer may only depend on layers below it. `Simulation` must **never** depend on
`Presentation`. The editor depends on everything; nothing depends on the editor.

### 3.1 Repository layout (target)

```
/CLAUDE.md                 Project rules for contributors and AI agents
/docs/                     Design docs (this file), ADRs (docs/adr/NNNN-*.md)
/cmake/                    Toolchain files, helper modules, presets includes
/engine/
  core/                    dg::core   – memory, containers, math, log, assert, hash, compat
  platform/                dg::platform – SDL3 wrapper, file system, threads, time
  reflect/                 dg::refl  – type registry, attributes, generated-code runtime
  serialize/               dg::ser   – YAML/binary archives driven by reflection
  jobs/                    dg::jobs  – task graph over enkiTS
  ecs/                     dg::ecs   – archetype ECS, queries, scheduler, snapshots
  math/                    dg::math  – render math (float SIMD) + dg::fixed / det-float sim math
  sim/                     dg::sim   – fixed-tick loop, sim world, RNG, determinism checks
  physics/                 dg::physics – Jolt + Box2D integration (deterministic config)
  script/                  dg::script  – Luau VM, sandbox, binding generator runtime
  net/                     dg::net   – transport, lockstep, rollback, sessions
  asset/                   dg::asset – asset DB, GUIDs, importers API, cooking, streaming, VFS/pak
  rhi/                     dg::rhi   – Vulkan abstraction (device, resources, cmd, sync, bindless)
  render/                  dg::render – render graph, render world, passes, materials, GI, 2D
  ui/                      dg::ui    – runtime UI (retained, Yoga layout, MSDF text)
  audio/                   dg::audio – miniaudio backend
  anim/                    dg::anim  – ozz-animation integration, state machines
  vfx/                     dg::vfx   – particle graph IR, compiler, GPU/CPU runtimes
  scene/                   dg::scene – scenes, prefabs, transforms, hierarchy
  plugin/                  dg::plugin – module manager, plugin ABI
/editor/                   dg::editor – ImGui editor app and tool panels
/apps/player/              Shipping runtime host
/apps/server/              Headless server/relay
/tools/dg-reflect/         Reflection code generator
/tools/dg-cook/            Asset cooker CLI
/tools/dg-package/         Packaging CLI (Windows/macOS/Android)
/plugins/                  First-party plugins (importers, FSR, Steam, etc.)
/shaders/                  Slang shader modules
/samples/                  Sample projects
/tests/                    Unit, integration, determinism, golden-image tests
/third_party/              Only vendored libs that are not in vcpkg (with LICENSE files)
/platform/android/         Gradle template project
/platform/macos/           Info.plist, entitlements templates
```

---

## 4. Core Systems

### 4.1 Memory

* Allocator interface `dg::IAllocator` with: linear/frame allocators (per frame, per tick),
  pool allocators (components, nodes), TLSF for general heaps, VMA for GPU.
* No raw `new`/`delete` in engine code; ownership via `dg::Unique<T>`, `dg::Ref<T>` (intrusive), handles.
* **Handles over pointers** for anything that crosses system boundaries (entities, assets, GPU resources):
  `Handle<T> { u32 index; u32 generation; }`.
* Memory tags per subsystem; reported in Tracy and the editor profiler.

### 4.2 Reflection (`dg::refl`)

* Global `TypeRegistry` keyed by stable `TypeId` (XXH3 of the fully qualified name — never `typeid` or addresses).
* `TypeInfo`: name, size, alignment, fields (`FieldInfo` with offset, type, attributes), methods,
  enums, base classes, constructors, and version.
* Attributes: `Serialize`, `Transient`, `Range(min,max)`, `Tooltip`, `HideInInspector`, `ReadOnly`,
  `Category`, `ScriptVisible`, `Sim` (marks a component as part of the deterministic state), `Replicated`.
* Producers: `dg-reflect` generated code (default), or P2996 `consteval` producers where available.
* Consumers: serializer, inspector UI, Lua binding generator, prefab override diffing, network/rollback
  snapshotting, and undo/redo.

```cpp
// Authoring syntax (stable across reflection backends)
struct DG_REFLECT(Sim) Health {
    DG_PROPERTY(Serialize, Range(0, 1000), Tooltip("Current HP"))
    dg::fixed32 current = 100;
    DG_PROPERTY(Serialize)
    dg::fixed32 max = 100;
};
```

### 4.3 Serialization (`dg::ser`)

* Reflection-driven `Archive` concept with backends:
  * **YAML text** (editor assets, scenes, prefabs) — stable key ordering, diff/merge-friendly.
  * **Binary** (cooked assets, save games, snapshots) — versioned, little-endian, aligned, zero-copy where possible.
* Schema versioning per type (`TypeInfo::version`) with registered upgrade functions.
* Every serialized object references assets by **GUID**, never by path.

### 4.4 Job system (`dg::jobs`)

* Task graph over enkiTS: `JobGraph` built per frame/tick from system dependencies.
* Thread roles: main (platform/events), game/sim, render submission, N workers, IO, audio.
* **Determinism rule:** parallel sim jobs may only write disjoint data. Reductions (sums, min/max, event merges)
  are performed in a deterministic order (by entity ID / chunk index) after the parallel phase.

### 4.5 Module & plugin system (`dg::plugin`)

* Everything above Core is a **module** with a descriptor:
  ```cpp
  struct ModuleDesc { std::string_view name; Version version; std::span<const Dependency> deps;
                      LoadPhase phase; /* Core, Engine, Editor, Game */ };
  ```
* **Plugin = package** of modules + assets + Lua scripts + a manifest `plugin.dgplugin` (YAML):
  name, version, engine version range, platforms, modules, dependencies, a `deterministic: true|false` flag, and a
  `headless: true|false` flag (whether the plugin is linked into headless server builds, §14.1).
* Load modes:
  * **Editor / development**: dynamic libraries (`.dll` / `.dylib`) with a C-ABI entry point
    `extern "C" DG_PLUGIN_API dg::PluginInterface* dgCreatePlugin(dg::EngineContext*)`. C++ hot reload is supported for
    editor-only and presentation modules.
  * **Shipping / Android / iOS**: **static linking** with generated registration (no dynamic loading on iOS,
    and fewer issues on Android).
* Extension points (service locator + interfaces): `IAssetImporter`, `IRenderFeature` (inserts render-graph passes),
  `IEditorPanel`, `IInspectorDrawer`, `IComponentTool`, `ISystemProvider`, `ITransport`, `IScriptLibrary`, `IPlatformService`.
* Plugins that register **sim systems/components** contribute to the **sim checksum**: a lockstep session
  verifies that all peers run the identical set and version of deterministic plugins (manifest hash exchange).

---

## 5. Determinism Model (the heart of the engine)

### 5.1 Two worlds

```
     Inputs (local + network, per tick)
                │
                ▼
┌─────────────────────────────┐   read-only snapshot / events   ┌──────────────────────────────┐
│   SIM WORLD (deterministic) │ ──────────────────────────────► │ PRESENTATION WORLD (free)    │
│  fixed tick (e.g. 30/60 Hz) │                                  │ variable frame rate          │
│  ECS components tagged Sim  │                                  │ interpolation, VFX, audio,   │
│  Jolt/Box2D deterministic   │                                  │ animation pose, UI, camera   │
│  Luau sim scripts           │                                  │ Luau view scripts            │
└─────────────────────────────┘                                  └──────────────────────────────┘
```

* **Sim world**: owns all gameplay state. Advances only in fixed ticks, from inputs only.
* **Presentation world**: renders sim state with interpolation between the last two ticks; owns cosmetic state.
  It may **read** sim state and **consume sim events**, but may **never write** sim state.
  The only thing that flows back into the sim is **input commands**.
* Single-player games use the same model (rollback-free), which makes replays, save states, and
  "kill-cam" features trivial.

### 5.2 Sources of nondeterminism and how each is eliminated

| Source | Mitigation |
|---|---|
| Floating-point differences x64 ↔ ARM64 (FMA contraction, x87, FTZ/DAZ, compiler reordering) | Sim code is compiled with `-ffp-contract=off`, `-fno-fast-math`, and `/fp:precise` (clang-cl). No x87. FTZ/DAZ state is set explicitly on sim threads. Jolt uses `JPH_CROSS_PLATFORM_DETERMINISTIC`. |
| Transcendentals (`sin`, `cos`, `pow`, `sqrt` OK; `exp`, `atan2`...) differ across libms | `dg::detmath`: our own implementations (or Jolt's) for all non-IEEE-exact functions. `std::` math is forbidden in sim code (enforced by a lint). |
| Gameplay numeric drift / design-friendliness | `dg::fixed32` (Q16.16) and `dg::fixed64` (Q32.32) types are available for gameplay values (HP, currency, timers). Vectors/physics use deterministic float. |
| Iteration order of hash maps / pointer-keyed containers | No `unordered_*` **iteration** in sim. Use sorted vectors, `std::flat_map`, or dense arrays by entity index. ECS chunk/entity order is deterministic. |
| Entity ID allocation | Sequential, deterministic allocator inside the sim world. Free lists are FIFO and deterministic. Network-spawned entities use deterministic `(tick, spawner, seq)` ordering. |
| Random numbers | `dg::sim::Rng` (PCG32/xoshiro128++) stored **in the sim state** and snapshotted. `rand()`, `std::random_device`, and time-based seeds are forbidden. |
| Time | The sim only sees `tick` and the constant `dt`. Wall-clock time is unavailable in the sim. |
| Multithreading | Disjoint writes + ordered reductions (§4.4). Physics islands are solved with a deterministic job order. |
| Uninitialized memory / padding | All sim components are zero-initialized. Checksums hash fields via reflection, never raw padding. |
| Lua hash table iteration (`pairs`), string hash seeds, GC | See §6.3. |
| Platform/OS calls, file IO | Not accessible from sim code (sandbox + module dependency rules). |
| Plugin mismatch | Manifest hash exchange at session start (§4.5). |
| Asset differences | Sim-relevant assets (collision meshes, gameplay data tables) are hashed and verified at session start. |

### 5.3 Fixed-tick scheduler

```
loop:
  poll platform events → build local InputCommand for tick T+inputDelay
  net.exchangeInputs()
  while sim.canAdvance():                // all inputs for next tick confirmed (lockstep) or predicted (rollback)
      sim.step(inputs[tick])             // fixed dt, ordered system phases
      if rollback: snapshots.store(tick)
  alpha = accumulator / dt
  presentation.update(simPrev, simCurr, alpha, frameDt)
  renderer.render(presentation.renderWorld)
```

Sim system phases (each is an ordered list, with explicit ordering constraints):
`PreTick → Input → ScriptPreUpdate → Gameplay → Physics(Pre/Step/Post) → ScriptPostUpdate → Lifecycle(spawn/destroy) → PostTick(checksum)`.

### 5.4 Verification tooling

* **Per-tick state checksum**: XXH3 over all `Sim`-tagged components (reflection-walked, canonical order),
  plus the RNG state and the physics state hash. Peers exchange checksums every N ticks; on mismatch a
  **desync report** (diffable state dump) is written for both peers.
* **Determinism CI**: the same replay files run on `windows-x64`, `macos-arm64`, and `android-arm64` (emulator + device farm),
  and the final checksums must be identical. **A failing determinism test blocks merge.**
* **Replay system**: record initial state + input stream, then replay headless at maximum speed. This gives
  regression tests, bug reproduction, and spectating.

---

## 6. Scripting (Luau)

### 6.1 Roles of Lua

* **Sim scripts** (`*.sim.luau`): deterministic gameplay logic, running inside the sim world.
* **View scripts** (`*.view.luau`): presentation, UI, VFX, audio, camera. Non-deterministic APIs are allowed.
* **Editor scripts** (`*.editor.luau`): editor tooling, custom inspectors, and menu items (later).

### 6.2 Binding generation

* The binding generator walks `dg::refl` for everything marked `ScriptVisible`. It produces:
  * Luau userdata wrappers / fast-call bindings (C++ side).
  * A **Luau type-definition file** (`dg.d.luau`) for luau-lsp autocompletion and type checking in the editor/IDE.
* Components are exposed as **typed views** onto ECS storage (no copies). Entities are opaque handles.
* System-style API (ECS-friendly) is preferred over per-object `Update()` callbacks:

```lua
--!strict
-- health.sim.luau
local Health = dg.component("Health")
local Damage = dg.event("Damage")

return dg.system {
    phase = "Gameplay",
    query = { Health },
    on_event = { [Damage] = function(world, e)
        local h = world:get(e.target, Health)
        h.current = dg.fixed.max(0, h.current - e.amount)
        if h.current == 0 then world:emit("Died", { target = e.target }) end
    end },
}
```

### 6.3 Deterministic Lua sandbox rules (enforced by the VM wrapper)

1. **No gameplay state in Lua globals, upvalues, or module-level tables.** All persistent sim state lives in
   ECS components. Script environments are frozen (`table.freeze`, Luau sandbox) after load.
   *This is what makes rollback possible:* snapshots only need ECS state, never the Lua heap.
2. `pairs()`/`next()` over **hash tables are forbidden in sim scripts** (their order depends on hash seeds and object
   addresses). The sim API provides `dg.ordered(t)` and array-like iteration (`ipairs`, numeric for). This is enforced
   by a custom Luau lint pass plus a debug-mode runtime check.
3. String hash seeds are fixed. `os`, `io`, `debug`, `require` of arbitrary files, `collectgarbage`, `__gc`, and weak tables
   are not available to sim scripts.
4. `math.random` is replaced by the sim RNG. `math.*` transcendentals are replaced by `dg::detmath` implementations.
   `os.time`/`os.clock` are removed.
5. Number semantics: Luau numbers are IEEE doubles (deterministic for + - * / sqrt when FMA is disabled in the VM build).
   Gameplay-critical values should use `dg.fixed` userdata or vectors from the engine.
6. Optional Luau native codegen must be verified deterministic per architecture, or it stays disabled for sim scripts
   (interpreter only).
7. Instruction-count budget per tick per script (the Luau interrupt callback). Exceeding it is a hard error in development.

### 6.4 Hot reload

* View and editor scripts reload instantly.
* Sim scripts reload by rebuilding the sim world from the last snapshot. In networked sessions this is disabled
  (or uses a coordinated reload with a script hash bump).

---

## 7. ECS (`dg::ecs`)

* **Archetype-based** storage (SoA within 16 KiB chunks), similar to flecs/Unity DOTS.
* `Entity = { u32 index; u32 generation; }`. Separate entity spaces for sim and presentation worlds.
* Components are reflected PODs (plus a few managed types through handles). Tags, singletons (`World::resource<T>()`),
  relationships (`ChildOf`, `Prefab instance of`) are supported.
* **Queries** are cached; iteration order = archetype creation order × chunk order × in-chunk index, all deterministic.
* **Systems** declare read/write sets. The scheduler builds a dependency DAG and runs non-conflicting systems in parallel
  (presentation: freely; sim: deterministic, per §4.4).
* **Structural changes** (add/remove components, spawn/destroy) are deferred to command buffers. These are flushed in
  deterministic order at phase boundaries.
* **Events**: typed, per-tick event streams. Sim events are part of the deterministic state for that tick.
* **Snapshots**: whole-world serialize/restore of `Sim` components (memcpy-fast per chunk) plus the physics state
  (`JPH::StateRecorder`) and RNG. Ring buffer for rollback, and full snapshots for save games and editor play mode.
* **Transforms**: `LocalTransform`, `WorldTransform` (computed), `Parent`/`Children`. The sim uses deterministic math types.
  The presentation interpolates into float `RenderTransform`.

---

## 8. Rendering

### 8.1 RHI (`dg::rhi`)

* A thin, Vulkan-shaped abstraction, so the rest of the engine never includes `vulkan.h`.
  It leaves room for a native Metal backend later if MoltenVK limits us.
* **Bindless**: one global descriptor heap (descriptor indexing, or `VK_EXT_descriptor_buffer` where supported).
  Resources are referenced by 32-bit indices in shaders.
* Dynamic rendering, `synchronization2`, timeline semaphores, async compute and transfer queues where available.
* Frames in flight: 2–3. Deferred destruction queue keyed by timeline value.
* Pipeline cache persisted per device/driver. PSO pre-warm list generated during cooking.
* Capability tiers detected at startup: `Mobile`, `Desktop`, `DesktopRT` (ray query available).
  On macOS, the tier is chosen according to what the MoltenVK/KosmicKrisp version exposes
  (portability subset, ray query availability).

### 8.2 Render graph

* A frame graph is declared each frame: passes declare resource reads and writes. The graph handles transient
  allocation (aliasing), barriers, queue assignment, and pass culling.
* Render features (`IRenderFeature`, pluggable) inject passes at named extension points:
  `PreDepth, GBuffer, Lighting, GI, Translucency, PostProcess, UI, Debug`.

### 8.3 Render world & pipelines

* The presentation layer **extracts** render data into a `RenderWorld` (double-buffered) so that rendering runs one
  frame behind the game thread without locks.
* **Desktop pipeline**: GPU-driven visibility (meshlets via meshoptimizer, compute culling — frustum + Hi-Z occlusion,
  `vkCmdDrawIndexedIndirectCount`), visibility buffer or deferred G-buffer, clustered lighting, PBR (metallic/roughness,
  glTF-compatible), cascaded/virtual shadow maps, TAA/FSR, bloom, tone mapping (ACES / AgX), SSAO / GTAO, SSR.
* **Mobile pipeline** (Android, optionally iOS): forward+ with tile-based light culling, subpass-friendly
  (render-pass load/store ops tuned for TBDR), ASTC textures, half-precision shading, baked or probe GI.
* **Apple Silicon** is also a TBDR GPU: both pipelines respect tile-memory-friendly load/store behavior.
  macOS defaults to the desktop pipeline.
* **Materials**: Slang-based material modules with a node-graph editor (later). Material instances are parameter blocks
  in a bindless buffer.

### 8.4 2D rendering

* Sprite renderer with dynamic batching by atlas/material, 2D sorting layers/order (Unity-like), sprite animation,
  9-slice, tilemaps (chunked meshes), 2D lights (optional), and pixel-perfect camera.
* **Texture atlases**: authored as `SpriteAtlas` assets. The cooker packs them with stb_rect_pack, adding padding and
  extrusion, and builds mip chains. Runtime UI glyphs use MSDF atlases.
* The 2D and 3D paths share the same RHI, render graph, and camera stack, so mixed 2.5D scenes are possible.

### 8.5 Global illumination — "Lumen-style" design (clean-room)

> ⚠️ **Licensing:** Unreal Engine source code is under the Unreal Engine EULA and **must not be copied** into this
> project, whether as code, shaders, or line-by-line translations. We implement the *published design* from
> "Lumen: Real-time Global Illumination in Unreal Engine 5" (Wright, Narkowicz, Kelly — SIGGRAPH 2022 Advances course)
> and related public research. We also use AMD's **MIT-licensed** FidelityFX Brixelizer GI / GI-1.0 as code-level references.

**Desktop tier (software ray tracing; hardware RT optional):**

1. **Scene representation for tracing**
   * **Mesh signed distance fields** generated at import (per-mesh SDF bricks, sparse) and merged into a
     **global SDF clipmap** around the camera for long-range tracing.
   * **Surface cache ("cards")**: each mesh is captured from a small set of axis-aligned directions into an atlas
     of albedo, normal, emissive, and depth. This gives cheap material lookup at ray hit points.
2. **Lighting the surface cache**
   * Direct lighting is computed into the card atlas (shadowed via SDF / shadow maps).
   * **Multi-bounce** comes from feeding the previous frame's GI back into the cards (temporal feedback).
   * Updates are amortized: a budgeted number of cards is updated per frame, prioritized by screen size and change.
3. **Final gather**
   * **Screen-space probes** are placed adaptively on the G-buffer (e.g. 1 per 16×16 px, refined at depth and normal edges).
   * Rays are traced in screen space first (HZB), then against mesh SDFs (near) and the global SDF (far).
     On a hit, radiance is looked up in the surface cache.
   * A **world-space radiance cache** (clipmapped probes) serves distant lighting and stabilizes far-field rays.
   * Importance sampling from previous-frame lighting and BRDF. Spatial and temporal filtering of probes, then
     integration into the pixels with bent normals.
4. **Reflections**: rough reflections reuse the probe data; smooth reflections trace dedicated rays with the same hit
   lighting (surface cache), with SSR first.
5. **Hardware RT path** (`DesktopRT` tier, `VK_KHR_ray_query`): it replaces SDF tracing with BLAS/TLAS traces against
   simplified proxy geometry. It keeps the same surface cache and final gather.
6. **Translucency and volumetrics**: a froxel volume samples the radiance cache.

**Mobile / low tier fallbacks:**

* **DDGI-style irradiance probe volumes** updated sparsely (compute), or **baked lightmaps** (xatlas UVs, GPU baker in the
  editor) plus reflection probes. The tier is selected per project and platform in the quality settings.

**Phasing:** Phase A — baked lightmaps and probes; Phase B — DDGI probes; Phase C — SDF plus surface cache plus screen
probes (the "Lumen-style" path); Phase D — hardware RT path.

### 8.6 Particle system & VFX (`dg::vfx`)

A GPU-driven particle system in the class of Niagara and Unity VFX Graph, with a SIMD CPU path for mobile and for
effects that need CPU-side results. Effects are fully **serialized assets**, authored in the built-in Particle Editor
(§12.2).

**Data model — one IR, two authoring views**

* An **Effect** asset (`*.dgvfx`, YAML) contains one or more **Emitters**, plus **exposed parameters**, bounds, LOD
  settings, and scalability settings.
* Each emitter is a **graph** of typed nodes organized into stages: `Spawn → Initialize → Update → Render`, plus
  `Events` (on death, on collision, on timer) that can drive **sub-emitters**.
* The editor offers two views over the **same graph IR**: a **module stack** (Unity Shuriken-style: Emission, Shape,
  Velocity over Life, Color over Life, Noise, Collision, …) for fast authoring, and a **node graph** (VFX Graph-style)
  for custom logic. Modules are themselves saved subgraphs, so users and plugins can author new modules.
* **Particle attributes** (position, velocity, age, lifetime, color, size, rotation, sprite index, plus custom ones) are
  declared by the nodes that use them. The compiler **prunes unused attributes** and picks the storage precision
  (`f32` / `f16` / `unorm8`) per attribute.

**Compilation (cook time)**

* The graph IR is lowered to one of two targets:
  * **GPU**: **Slang compute kernels**, one *fused* kernel per stage per emitter, with no per-module dispatches.
    Constant folding and dead-attribute elimination are done before codegen.
  * **CPU**: a compact **SIMD bytecode** (SoA, 4/8-wide via the engine's SIMD layer), executed by job-system workers.
* The cooker emits both targets where the platform tier allows it, and the runtime picks per emitter by tier and
  budget. Shader variants are precompiled, so **no shader compiler ships in the player**.
* Effects authored or modified **at runtime** (user content, in-game editors) are limited to parameter, curve, and module
  combinations of **precompiled module kernels** (an "uber-module" path), or they use the CPU bytecode path, which
  compiles on any device. They are serialized through the same reflection path as editor-authored effects.

**GPU runtime**

* Per-emitter SoA attribute buffers in a shared pooled heap (bindless). Allocation uses **dead-list / alive-list
  compaction**, so there are no CPU readbacks.
* Each frame: spawn → initialize → update (fused) → compaction → optional sort → `DrawIndirect`, all on **async compute**
  where it is available.
* **Sorting**: GPU radix sort (FidelityFX Parallel Sort, MIT) only for emitters that need back-to-front ordering.
  Others render unsorted (additive) or with weighted-blended OIT.
* **Forces & collision**: gravity/drag/vortex/point attractors, curl noise, **3D vector fields** (texture assets),
  **depth-buffer collision**, and **SDF collision against the global SDF** used by GI (§8.5) — so collision with the
  whole scene is cheap.
* **Events / sub-emitters**: GPU append buffers (death or collision positions) consumed by child emitters in the same
  frame. Optional *async* readback (N frames later) for audio or gameplay-cosmetic triggers.
* **Renderers**: billboard sprites (camera-facing, velocity-aligned, fixed axis) with **flipbook atlases** and motion
  vectors; **ribbons/trails** (GPU-built strips); **mesh particles** (instanced); **decals**; **light particles**
  (fed into clustered lighting with a cap); **soft particles**; **distortion / refraction**.
* **Lighting**: lit particles sample the clustered lights and the **GI radiance cache** (§8.5), with half-resolution
  rendering plus upsampling for heavy translucent effects.

**Performance & scalability**

* **Overdraw reduction**: the cooker generates **trimmed sprite geometry** (tight 4–8 vertex polygons per flipbook frame)
  instead of quads.
* **Fixed or precomputed bounds** per effect for culling; GPU frustum culling per particle in the render stage.
* **Significance & budget manager**: a global particle and GPU-time budget per platform tier. Effects are ranked by
  distance, screen size, and priority. Low-significance effects drop spawn rate, LOD, or are culled.
  **Distance LODs** per emitter.
* **Pooling**: effect instances and their GPU allocations are pooled. Prewarm/warm-up is simulated at load time,
  not on spawn.
* **Batching**: emitters that share a material and renderer type are drawn with one indirect draw.
* **Mobile tier**: CPU SIMD path or simplified GPU kernels, `f16` attributes, no GPU sort (fallback: per-emitter
  distance sorting), TBDR-friendly blending, low-resolution translucency.
* Every emitter reports cost (particle count, memory, kernel and draw time via GPU timestamps) to the profiler and the
  editor.

**Relationship to the simulation**

* Particles are **presentation-only**. They never affect sim state, and they are excluded from state hashing and
  headless builds.
* **Visual consistency across clients**: each effect instance is seeded deterministically from
  `hash(sim entity, spawn tick, event id)`, so all players see the "same" effect (best-effort — GPU float results are
  not bit-guaranteed).
* **Rollback-safe**: effects triggered by *predicted* sim events (§13.8) are spawned as tentative. If the event is
  cancelled after a rollback, the effect is killed or faded; if confirmed, it continues untouched.
  Gameplay-relevant "particles" (bullets, debris that matters) must be sim entities, not VFX.

**Integration**

* `VfxEffect` presentation component (asset GUID, exposed-parameter overrides, autoplay, attach-to-socket).
* `dg.vfx.play(effect, { position = ..., params = { Color = ... } })` from view Luau and C++.
  Exposed parameters can be bound to reflected component fields.
* Effects can live on prefabs. Overrides of exposed parameters use the normal prefab override path (§10).

---

## 9. Asset Pipeline

* **Asset database** (editor): every source file in `Assets/` has a sidecar **`.meta`** file (YAML) holding a stable
  **GUID**, importer type and version, and import settings, like Unity. Moving or renaming a file keeps its GUID.
* **Importers** (`IAssetImporter`, pluggable): glTF/GLB (fastgltf), FBX (ufbx), textures (stb/tinyexr → KTX2),
  audio, fonts, Lua scripts (compiled to Luau bytecode), shaders (Slang → SPIR-V), and prefabs and scenes (YAML).
* **Derived Data Cache (DDC)**: the import output key is `hash(source content, import settings, importer version, platform)`.
  It is cached locally (optionally shared on the network).
* **Cooking** (`dg-cook`): per-platform conversion (BC7 vs ASTC, mesh quantization, shader variants), dependency
  closure from the build's scene list, and output into **`.dgpak`** archives (zstd chunks, TOC by GUID, mmap-friendly).
* **Runtime**: `AssetHandle<T>` with reference counting, async streaming via an IO thread plus job decode, and LRU
  eviction under memory budgets. Texture mip streaming and mesh LOD streaming come later.
* **Sim-relevant assets** (collision meshes, data tables, sim scripts) are flagged. Their content hashes form part of the
  session's determinism manifest.

---

## 10. Scene, Prefabs & Runtime Objects

* **Scene** = a serialized set of entities (YAML in the editor, binary when cooked). Multiple scenes can be loaded
  additively.
* **Prefabs** (Unity-like):
  * Prefab assets contain an entity hierarchy plus components.
  * **Prefab instances** store only **overrides** (property-path diffs computed through reflection), plus
    added/removed components and children.
  * **Nested prefabs** and **prefab variants** (a prefab based on another prefab with overrides).
  * "Apply / Revert overrides" in the Inspector.
* **Runtime-created custom objects**: the same serialization path works at runtime. Game code (C++ or Lua) can
  compose an entity, then `dg.prefab.save(entity, "user://…")` and later instantiate it. This supports user-generated
  content and in-game level editors. Instantiation is deterministic in the sim (spawn ordering per §5.2).
* **Play mode** in the editor: snapshot the edit world → run → restore on stop (no accidental persistence). This
  matches Unity's model.

---

## 11. Runtime UI System (`dg::ui`) & UI Designer

* **Retained-mode, ECS-backed UI**: UI elements are entities with `RectTransform` (Unity-like anchors, pivot, offsets)
  and an optional **Yoga flexbox** layout container (`FlexLayout`), which gives responsive layouts.
* **Responsive features**: anchor presets, safe-area insets (notches), DPI scaling, a reference resolution plus
  match-width/height policy (like Unity's `CanvasScaler`), breakpoints (style overrides per aspect/size class), and
  9-slice images.
* **Styling**: a style-sheet asset (USS-like selectors: type, class, state `:hover`/`:pressed`/`:focus`) applied to
  elements.
* **Widgets**: Panel, Image, Text (HarfBuzz shaping, MSDF), Button, Toggle, Slider, InputField (IME via SDL3),
  ScrollView, Dropdown, ListView (virtualized), and ProgressBar.
* **Input**: unified pointer/touch/gamepad navigation, with focus graphs for controllers.
* **Rendering**: batched by the 2D renderer into the UI render-graph pass. Screen-space, camera-space, and world-space
  canvases.
* **Data binding**: reflected properties bind to view-model components, and Luau view scripts handle logic.
* **UI Designer (editor)**: a WYSIWYG canvas with a device preview (resolution/aspect/safe-area presets for phones,
  tablets, and desktops), drag-and-drop widgets from a palette, anchor/flex gizmos, live style editing, and a hierarchy
  view. UI layouts are saved as prefabs.

---

## 12. Editor (Unity-like)

Built with Dear ImGui (docking + multi-viewport), plus ImGuizmo, ImPlot, and imgui-node-editor. A custom theme
closely matches Unity's dark/light skins.

| Panel | Function |
|---|---|
| **Hierarchy** | Scene tree, drag reparenting, search, prefab coloring (blue/override markers), multi-scene. |
| **Scene View** | Free-fly/orbit camera, W/E/R/T tools (move/rotate/scale/rect), local/global pivot, snapping (grid, vertex, surface), 2D mode, gizmos, picking (GPU ID buffer), overlay toggles (wireframe, GI debug views, colliders, nav). |
| **Game View** | Player camera, resolution/aspect presets, stats overlay, multiple simulated clients (for testing determinism and networking locally). |
| **Inspector** | Reflection-generated component editors, custom drawers (`IInspectorDrawer`), prefab override UI, multi-object editing, add-component search. |
| **Project** | Asset browser over `Assets/` with thumbnails, search/filter by type/label, drag-and-drop into the scene, import settings in the Inspector. |
| **Console** | Logs with filters, collapse, and source jump; Lua errors with stack traces. |
| **Profiler** | Integrated Tracy client view plus engine counters (sim tick ms, rollback count, GPU passes). |
| **Animation / Animator** | Timeline and state-machine graph editor (imgui-node-editor). |
| **UI Designer** | See §11. |
| **Particle Editor** | See §12.2. |
| **Network Debugger** | Ping/jitter/rollback visualizer, per-tick checksum timeline, desync diff viewer. |
| **Build Settings** | Scene list, platform targets (Windows/macOS/Android), quality tiers, Build / Build & Run. |

**Editor infrastructure**: an undo/redo command stack (reflection-based property diff commands), selection service,
editor preferences, keyboard shortcut mapping (Unity defaults), asset thumbnails cache, and the editor scripting API.

### 12.1 Level design / world-building tools

* **Terrain**: heightmap tiles (clipmap rendering), sculpt/smooth/flatten/stamp brushes, splat-map texture painting,
  holes, and a deterministic collision heightfield (Jolt `HeightFieldShape`).
* **Foliage / detail painting**: GPU-instanced scattering with density, slope, and height rules.
* **Mesh/brush modeling** (ProBuilder-like): primitive shapes, face/edge/vertex editing, extrude, bevel, CSG blocking,
  and UV tools — for greyboxing.
* **Splines**: road/river/fence generation along splines.
* **Prefab painting / scatter** and **grid snapping & alignment** tools.
* **Navigation**: navmesh baking with **Recast/Detour** (Zlib), using deterministic pathfinding queries in the sim.
* **Lighting tools**: light probe and reflection probe placement, lightmap baking, and GI debug visualizers.

### 12.2 Particle Editor (built-in)

A dedicated, dockable **Particle Editor** window opens when you double-click a `*.dgvfx` asset. It edits the graph IR
from §8.6.

| Area | Function |
|---|---|
| **Emitter list / system overview** | All emitters in the effect, with enable/solo toggles, ordering, sub-emitter links, and per-emitter cost badges (particles, memory, ms). |
| **Module stack view** | Stage-ordered module stack (Spawn / Initialize / Update / Render / Events). Drag to reorder modules, add them from a searchable library, and enable/disable them. Inline curves and gradients. |
| **Node graph view** | imgui-node-editor canvas over the same IR: typed pins, attribute get/set nodes, math, noise, sampling (textures, curves, SDF, vector fields), custom subgraph modules, comments/groups, and inline value previews. Switching views loses nothing. |
| **Inspector** | Reflection-generated properties of the selected module or node. Every value can be constant, random range, curve, gradient, or bound to an **exposed parameter**. |
| **Curve & gradient editors** | Bezier/linear/constant keys, min-max curve pairs, presets, and copy/paste between properties. Gradients with color + alpha keys and HDR intensity. |
| **Preview viewport** | An isolated preview scene with its own camera, lighting presets (day/night/studio), background/floor toggles, a scene-depth proxy for collision tests, and a **"preview in scene"** mode that drives the instance selected in the Scene View. |
| **Timeline / playback** | Play, pause, single-step, restart, loop, simulation speed (0.01×–10×), scrub to time t (re-simulated deterministically from the fixed seed), and prewarm preview. |
| **Debug overlays** | Bounds (fixed vs measured, with "fit bounds" button), particle count graph, overdraw heatmap, attribute inspector (pick a particle to see its attribute values via readback), wireframe of trimmed sprite geometry, sort-order view. |
| **Profiler strip** | GPU kernel and draw timings per stage, memory per attribute buffer, and budget usage for the selected **platform tier** (preview as Android/mobile). |
| **Scalability / LOD tab** | Per-tier and per-distance overrides (spawn-rate scale, max particles, disabled modules), previewed live. |
| **Flipbook / atlas tool** | Import image sequences or sheets → build a flipbook atlas (stb_rect_pack), set frame rate and blending, and generate trimmed geometry. |
| **Templates & library** | Starter effects (fire, smoke, sparks, magic, trails, rain/snow, explosions) and a user/plugin module library. Save any emitter as a template. |

* **Live editing**: changes recompile only the affected emitter's kernels or bytecode in the background (Slang in the
  editor) and **hot-swap into running play mode and the Game View** without restarting the effect.
* **Validation**: the compiler reports unused attributes, missing bounds, unbounded spawn rates, per-tier budget
  overruns, and modules unsupported on the mobile tier — inline on the offending node.
* Full **undo/redo**, multi-select, copy/paste of modules and nodes across effects, and diff-friendly YAML saves.
* **Extensible**: plugins add modules (`IVfxModule`: graph definition + Slang snippet + CPU bytecode implementation),
  renderers (`IVfxRenderer`), and editor drawers.

---

## 13. Built-in Deterministic Multiplayer Backend (`dg::net`)

The engine ships a complete, **built-in** deterministic netcode stack: the client library, a dedicated session server,
and the tooling. A game gets multiplayer by defining `InputCommand` and writing sim systems; it never writes
networking code. The model is **predict/rollback with server-confirmed inputs**, the same family of architecture as
Photon Quantum 3. That is a design reference only; the implementation is our own.

### 13.1 Topology & roles

```
                      ┌──────────────────────────────────────────────────┐
  Client A ──inputs──►│  dg-server  (headless, one process hosts N rooms) │
  Client B ──inputs──►│  ┌───────────────────────────────────────────┐   │
  Client C ──inputs──►│  │ Room: level "Arena", 30 Hz                │   │
           ◄─confirmed│  │  • Input authority: tick clock, deadlines │   │
              inputs ─│  │  • Checksum validator                     │   │
           ◄─snapshots│  │  • (optional) authoritative headless sim  │   │
                      │  └───────────────────────────────────────────┘   │
                      └──────────────────────────────────────────────────┘
```

| Role | Responsibilities |
|---|---|
| **Client** | Samples local input → sends `InputCommand` for tick `T + inputOffset`. Runs **predicted** ticks with guessed remote inputs. On receiving confirmed inputs, it **rolls back** to the last verified tick and re-simulates. Sends checksums of verified ticks. |
| **Server — input authority** (always on) | Owns the session clock. Collects inputs per tick until a **deadline**, substitutes a default or repeated input for any late or missing player, and broadcasts the **confirmed input set** for that tick. A late input is never "re-opened": the server's set is final, so every client agrees on history. |
| **Server — checksum validator** (always on) | Compares per-tick checksums sent by clients (§13.4) and triggers resync (§13.5). |
| **Server — authoritative simulation** (optional, `mode=verify`) | Runs the **same sim** headlessly on the room's level (§14.1), using only confirmed inputs. It never predicts and never rolls back. It is the ground truth for checksums, snapshots, late joins, and anti-cheat. |

Two server modes:

* **`relay`** (cheap, scales to many rooms per core): the server only confirms inputs and compares client checksums.
  The truth is the **majority** of client checksums. Snapshots for resync or late join are requested from a client
  that agrees with the majority (they are hash-verified before forwarding).
* **`verify`** (recommended for competitive/ranked play): the server runs the authoritative headless sim. The truth is
  always the server's state, and the server serves snapshots itself.

Peer-hosted sessions (one client also runs the server room in-process) use the same code path, for LAN and dev.

### 13.2 Ticks, frames and time synchronization

* **Verified tick**: every input for it is confirmed by the server. The state is final and identical everywhere.
* **Predicted tick**: simulated ahead of the last verified tick using predicted remote inputs. It is rolled back when
  confirmations arrive.
* Clients keep their sim clock **ahead of the server** by about `RTT/2 + jitterBuffer`, so local inputs arrive before
  the deadline. Drift is corrected by **time dilation** (slightly speeding up or slowing down the client's tick
  accumulator, ±5%), never by skipping ticks.
* `inputOffset` (local input delay, 0–N ticks) is configurable per game, trading latency for fewer rollbacks.
* The **max prediction window** (e.g. 10 ticks) is configurable. Beyond it, the client stalls its sim, rather than
  diverging further.

### 13.3 Input pipeline

* `InputCommand` is a reflected struct (`DG_REFLECT(Input)`). It is bit-packed via reflection, with delta-compression
  against the previous tick.
* Clients send the last N unacknowledged inputs redundantly in every packet (unreliable channel), which tolerates loss
  without retransmit delay.
* **Server-side input validation hook** (`IInputValidator`): it clamps or rejects impossible inputs (rate, range)
  before confirmation. This is the first anti-cheat layer.
* **Commands**: rare, larger reliable messages (e.g. "buy item", "chat") that are confirmed into a specific tick, just
  like inputs.
* **Predicting remote inputs**: by default repeat the last confirmed input. Games can override this per input field
  (e.g. reset "fire" to false).

### 13.4 State hashing (desync detection)

* **What is hashed**: all `Sim`-tagged components, walked via reflection in canonical entity/archetype order, plus the
  entity allocator state, the sim RNG, the physics state hash (Jolt/Box2D), the tick number, and registered sim
  resources. XXH3-64 (128-bit optionally).
* **Hierarchical hashing**: the world hash is composed of **per-archetype-chunk hashes** and **per-system-group hashes**.
  Chunk hashes are cached and recomputed only for chunks that are dirty this tick. That keeps per-tick hashing cheap
  enough to run every tick in development and every N ticks (configurable, e.g. every 4) in shipping.
* Clients send `(tick, worldHash)` for **verified ticks only** (predicted state is never compared).
* On mismatch, the server can request the **hash tree** from the divergent client. Comparing it with the reference tree
  pinpoints the exact component type and entity range that diverged, and this goes into the desync report.

### 13.5 Resync of desynced clients

```
 client X hash(T) ≠ reference hash(T)
        │
        ▼
 server marks X DESYNCED at T ──► X: stop sending checksums, keep sending inputs (its input still counts)
        │
        ▼
 reference snapshot S(T') chosen (T' ≥ T, latest verified tick available)
   verify mode: server's own sim snapshot
   relay mode : snapshot uploaded by a majority-agreeing client, hash-checked by server
        │
        ▼
 server streams S(T') to X  (chunked, zstd, delta vs. X's last good baseline if one exists)
        │
        ▼
 X: restore S(T') → verify hash(S) == reference → fast-forward through confirmed inputs T'..now
    (headless catch-up, presentation paused or interpolated) → resume prediction
        │
        ▼
 X sends desync report (hash trees, last inputs, build/plugin manifest) → server logs / uploads for analysis
```

* **Snapshot format**: the same ECS + physics + RNG snapshot used for rollback (§7), serialized to binary and compressed.
  The **presentation world is rebuilt** from the restored sim state. Entities are re-linked by sim entity ID, and
  cosmetic state (particles, audio) is reset or faded.
* **Policy** (`ResyncPolicy`, configurable): max resyncs per minute, then kick. A client that keeps desyncing at the same
  tick on the same build triggers a **bug report**, not a kick loop. A desynced client's checksums are ignored for the
  majority vote until it resyncs successfully.
* The **same path serves late join and reconnect**: a joining client is simply a client whose state is "missing" at T.

### 13.6 Session lifecycle & compatibility

1. Client connects → **handshake** with the determinism manifest: engine build hash, sim-module/plugin hashes,
   sim-asset content hash for the room's level, tick rate, and `InputCommand` schema hash. Any mismatch is rejected
   with a clear error.
2. Room start: the server broadcasts the start tick, initial RNG seed, and player slots. All peers load the level and
   start from identical state.
3. Running: the input/confirm loop (§13.2–13.3), checksums (§13.4), and resync on demand (§13.5).
4. End: the server optionally stores a **replay** (initial snapshot + confirmed inputs). This is compact, and can be
   re-simulated by any headless build for verification, spectating, or bug repro.

### 13.7 Server architecture (`dg-server`)

* **One process, many rooms**. Each room runs on a job-system worker, with a fixed tick timer per room.
* Room capacity and modes (`relay` vs `verify`) are set by room config. In `verify` mode a room loads its level
  through the headless asset path (§14.1).
* **Management API** (HTTP/JSON, plugin-replaceable): create/list/close rooms, health, metrics (Prometheus format),
  and replay download.
* **Matchmaking/lobby** is out of the core. It is a plugin interface (`ILobbyService`) with a simple built-in room
  list for development. External services (Steam, EOS, custom) plug in here.
* **Transport** is behind `ITransport`: ENet (default, UDP), GameNetworkingSockets (encryption, relays), and a WebSocket
  fallback (later). Channels: unreliable-sequenced (inputs), reliable-ordered (commands, snapshots, control).

### 13.8 Client API surface (C++ and Luau)

```lua
-- view-side Luau (presentation): connecting and status
local session = dg.net.connect { address = "eu.example.com:7777", room = "Arena-42" }
session.on_state_changed = function(s) ui.status:set_text(s) end   -- Connecting/Syncing/Running/Resyncing
```

Sim code is **identical** online, offline, and in replays. The sim only sees `world:input(playerSlot)` for the current
tick. Prediction, rollback, and resync are invisible to gameplay code.

Presentation helpers: `IsPredicted(entity)`, a "verified vs predicted" interpolation choice per view component, and
**rollback-safe events** (sim events raised on predicted ticks are delivered to the presentation layer with
`predicted=true`, then confirmed or cancelled, so a sound or VFX plays once and isn't duplicated on resimulation).

### 13.9 Non-deterministic replication (optional plugin)

State replication (server-authoritative snapshots + interpolation) for games that don't fit lockstep, such as MMO-scale
worlds. It is not the default and is not covered by the determinism guarantees.

---

## 14. Build Configurations & Packaging

| Config | Asserts | Optimizations | Tracy | Editor | Notes |
|---|---|---|---|---|---|
| `Debug` | ✅ | ❌ | ✅ | ✅ | Validation layers on. |
| `Development` | ✅ | ✅ | ✅ | ✅ | Default daily config. |
| `Profile` | minimal | ✅ | ✅ | ❌ | Player only. |
| `Shipping` | ❌ | ✅ (+LTO) | ❌ | ❌ | Static plugins, stripped symbols, cooked paks only. |
| `Server` (`DG_HEADLESS`) | ❌ (opt-in) | ✅ (+LTO) | optional | ❌ | Headless dedicated server, level-scoped cook (§14.1). |

* **The determinism compile flags are identical in all configs** for sim translation units (`dg_sim_target()` CMake
  helper). This means Debug and Shipping builds can play together.
* **`dg-package`** CLI (also driven from Build Settings): it cooks for the target platform, builds the player with
  static plugins, and assembles:
  * Windows: `Game.exe` + `*.dgpak` + redistributables.
  * macOS: `Game.app` (ARM64), embeds MoltenVK, `Info.plist`, entitlements, then `codesign` → `notarytool` → `stapler`.
  * Android: generates a Gradle project from `/platform/android` (SDL3 activity), copies paks into `assets/` or an
    install-time asset pack, and builds an `.aab` / `.apk` (ARM64). Keystore signing is configured per project.
* **CI matrix** (GitHub Actions): `windows-x64-clangcl`, `macos-arm64-clang`, `android-arm64-ndk`
  (build + emulator tests), and `linux-x64` (headless server, tools). Determinism replay jobs run across all of them,
  **including the headless server build** (§5.4).

### 14.1 Headless build mode (per-level)

A **headless build** is the simulation without presentation. It produces dedicated servers (`verify` rooms, §13.1),
bots, replay verifiers, and CI determinism runners — all **for a specified set of levels**.

**Compile-time profile (`DG_HEADLESS=1`)**

* Linked: core, platform (no window), reflection/serialization, jobs, ECS, sim, physics, Luau (sim scripts only), net,
  asset runtime, plugin manager, and plugins marked `deterministic: true` or `headless: true`.
* **Excluded**: RHI/renderer, GI, particles/VFX (`*.dgvfx` stripped by the cook), UI, audio, animation pose evaluation (unless a sim system needs it in deterministic
  mode), ImGui, editor, and all `*.view.luau` scripts. A **Null RHI / Null audio** backend satisfies any optional
  references, so gameplay modules don't need `#if`s.
* The sim translation units are built with **the exact same `dg_sim_target()` flags** as the clients. The headless
  binary is part of the determinism contract (x64 and ARM64; Linux, Windows, and macOS).
* There is also a **runtime `--headless` flag** on the normal player and editor builds (no window, no GPU device),
  so developers can run a server room or replay check without a separate build.

**Level-scoped cooking**

* `dg-cook --profile headless --levels Levels/Arena.dgscene,Levels/Duel.dgscene` computes the dependency closure of only
  those levels, and **strips presentation data**: render meshes, textures, materials, shaders, audio, UI, VFX, and
  view-only components on prefabs. It keeps collision shapes, navmesh, data tables, sim scripts (Luau bytecode),
  sim components, and prefabs.
* The output is a small `server.dgpak` and a **sim-asset manifest hash per level**. That hash is the one clients send in
  the handshake (§13.6), so a server can only host levels whose sim content matches the clients' builds.

**Packaging**

```bash
dg-package --target server --platform linux-x64 --levels Levels/Arena.dgscene --config Shipping
dg-package --target server --platform windows-x64 --levels Levels/Arena.dgscene,Levels/Duel.dgscene
```

* Outputs: the `dg-server` binary + `server.dgpak` + `server.yaml` (rooms, ports, tick rate, mode, resync policy).
  An optional **Docker image** (Linux) for fleet deployment.
* Exposed in the editor's **Build Settings → Dedicated Server** tab (level checklist, platform, mode).

**Runtime**

```bash
dg-server --levels Arena,Duel --port 7777 --mode verify --max-rooms 64 --tick-rate 30
dg-server --replay match_0193.dgreplay --verify      # headless re-simulation; exits non-zero on hash mismatch
```

* The editor can launch a **local headless server** in-process or as a child process for multiplayer play-mode testing
  (Game View with N simulated clients, §12).

---

## 15. Roadmap (phased)

| Phase | Deliverables |
|---|---|
| **0 — Foundation** | CMake/vcpkg skeleton, CI matrix (Win/macOS/Android), core lib, SDL3 window, logging, Tracy, the `dg-reflect` MVP, unit tests. |
| **1 — Sim core** | Archetype ECS, fixed-tick loop, deterministic math, RNG, snapshots, checksum, determinism CI with replays (x64 vs ARM64). |
| **2 — Rendering MVP** | RHI (Vulkan on Windows, MoltenVK on macOS, Android), render graph, bindless, glTF import, PBR forward/deferred, ImGui. |
| **3 — Scripting & physics** | Luau sandbox, binding generator, `dg.d.luau`, Jolt + Box2D deterministic integration, hot reload. |
| **4 — Editor MVP** | Docking layout, Hierarchy/Inspector/Scene/Game/Project/Console, gizmos, undo/redo, play mode, scenes, prefabs (+ overrides). |
| **5 — Networking** | Headless build profile + level-scoped cook, `dg-server` (relay → verify mode), confirmed-input protocol, predict/rollback, hierarchical state hashing, snapshot resync / late join, network debugger panel. |
| **6 — Content** | FBX (ufbx), animation (ozz), audio, 2D sprites/atlases/tilemaps, runtime UI and UI Designer. |
| **6b — VFX** | Particle graph IR + YAML serialization, module-stack editor, CPU SIMD runtime → GPU fused-kernel runtime, sprites/ribbons/meshes, sorting, depth/SDF collision, node-graph view, flipbooks, budgets/LOD, profiler integration. |
| **7 — Packaging** | `dg-cook`, `.dgpak`, `dg-package` for Windows/macOS/Android, Build Settings window. |
| **8 — Advanced rendering** | Shadows, post-FX, baked GI → DDGI → Lumen-style SDF/surface-cache GI → HW RT path; mobile tier tuning. |
| **9 — World building** | Terrain, foliage, brush modeling, splines, navmesh. |
| **10 — Ecosystem** | Plugin SDK docs, sample games, iOS port, native Metal backend (if needed). |

---

## 16. Key Risks & Open Questions

| Risk | Mitigation |
|---|---|
| Cross-arch float determinism is fragile (compiler upgrades, new code paths) | Determinism CI gate on every PR; pin compiler versions; isolate sim code in `dg_sim_target`; lint banned functions. |
| MoltenVK/KosmicKrisp feature gaps (ray query, descriptor buffer, mesh shaders) on macOS | Capability tiers; the GI software path doesn't require RT; keep the RHI clean enough for a native Metal backend. |
| C++26 reflection not available on Apple/NDK/MSVC toolchains | `dg-reflect` codegen; a macro surface compatible with future P2996 annotations. |
| Resync storms (a client repeatedly desyncs on a deterministic bug) | `ResyncPolicy` rate limits; repeated same-tick desyncs are reported as bugs with hash-tree diffs; determinism CI includes the headless server build. |
| Server cost in `verify` mode | `relay` mode for casual rooms; headless builds strip presentation; many rooms per process. |
| Rollback cost with heavy physics | Configurable rollback window; lockstep mode; physics LOD for sim entities; profile on Android early. |
| GPU particle cost on mobile/TBDR (overdraw, bandwidth) | CPU SIMD path, trimmed sprite geometry, budgets per tier, low-resolution translucency, profiling on Android from the start of phase 6b. |
| Lumen-style GI complexity | Phased tiers; baked/DDGI first; time-boxed research spikes; AMD MIT references. |
| Scope (an engine of Unity+UE breadth) | Strict phase gating; every module behind interfaces so features can be delayed without refactors. |

**Open questions:**

1. Default tick rate (30 vs 60 Hz), and whether it is per-project configurable. *(Proposed: per-project, default 30.)*
2. Lua-first vs C++-first gameplay template for new projects.
3. ENet vs GameNetworkingSockets as the default transport.
4. Visibility buffer vs deferred G-buffer as the desktop default.
5. Default server mode for new projects (`relay` vs `verify`). *(Proposed: `verify` once the headless build is stable.)*

---

## 17. References

* Lumen — SIGGRAPH 2022 Advances: <https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf>
* Lumen overview (Epic tech blog): <https://www.unrealengine.com/en-US/tech-blog/unreal-engine-5-goes-all-in-on-dynamic-global-illumination-with-lumen>
* GI-1.0 (AMD): <https://arxiv.org/abs/2310.19855>
* Jolt Physics (determinism notes): <https://jrouwe.github.io/JoltPhysics/>
* P2996 Reflection for C++26: <https://isocpp.org/files/papers/P2996R13.html>
* GGPO rollback reference: <https://github.com/pond3r/ggpo>
* Photon Quantum 3 (architecture reference for server-confirmed-input predict/rollback; no code used): <https://doc.photonengine.com/quantum/>
* Luau: <https://luau.org/>
