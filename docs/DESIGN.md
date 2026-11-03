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
| Built-in multiplayer | Predict/rollback netcode, dedicated headless server, state hashing and automatic resync of desynced clients (§15). |
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
| MCP server | **Custom** JSON-RPC 2.0 layer (yyjson) + **cpp-httplib** for Streamable HTTP | — / MIT | Pinned to MCP 2025-11-25 behind a protocol adapter; no official C++ MCP SDK exists (§14.5). |
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
| Animation runtime | **ozz-animation** | MIT | SoA SIMD sampling, blending, IK, and local-to-model jobs, plus compression. Presentation-side poses; the sim uses baked tracks (§10.1). |
| Animation compression (optional) | **ACL** (Animation Compression Library) | MIT | Alternative clip codec behind `IAnimCodec`, if it measures better than ozz's. |
| Audio device / decode | **miniaudio** | PD / MIT-0 | Device IO (WASAPI, CoreAudio, AAudio), WAV/FLAC/MP3 decoding. Mixer, events, and spatializer are ours (§9). |
| Spatial audio | **Steam Audio** | Apache-2.0 | HRTF binaural, occlusion/transmission, real-time and baked reflections, pathing. Windows/macOS/Android/iOS. |
| Audio codecs | **libopus** (+ opusfile), **stb_vorbis** | BSD-3 / PD | Opus is the default cooked format; Vorbis for legacy imports. |
| Audio middleware (optional) | FMOD / Wwise backends | proprietary (user-licensed) | Plugins only, behind `IAudioBackend`. Never a core dependency. |
| UI layout | **Yoga** (flexbox) | MIT | Runtime UI layout engine (§12). |

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
  audio/                   dg::audio – event runtime, mixer, spatializer, Steam Audio + miniaudio backends
  anim/                    dg::anim  – sim anim state + baked tracks, ozz runtime, anim graph, IK, GPU skinning
  vfx/                     dg::vfx   – particle graph IR, compiler, GPU/CPU runtimes
  scene/                   dg::scene – scenes, prefabs, transforms, hierarchy
  plugin/                  dg::plugin – module manager, plugin ABI
/editor/                   dg::editor – ImGui editor app and tool panels
/editor/mcp/               dg::mcp    – MCP server for AI agents (editor-only, §14.5)
/apps/mcp-bridge/          dg-mcp stdio bridge launched by MCP clients
/apps/player/              Shipping runtime host
/apps/server/              Headless server/relay
/dg, /dg.cmd               Developer tool launchers (macOS/Linux, Windows) → tools/dg.py (§16.2)
/CMakePresets.json         Configure/build/test presets per platform
/tools/dg.py               Project generation, build, run, test, determinism check
/tools/dg_launcher.py      Launcher GUI over dg.py (`dg gui`, Launcher.command / Launcher.bat)
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
  `headless: true|false` flag (whether the plugin is linked into headless server builds, §16.1).
* Load modes:
  * **Editor / development**: dynamic libraries (`.dll` / `.dylib`) with a C-ABI entry point
    `extern "C" DG_PLUGIN_API dg::PluginInterface* dgCreatePlugin(dg::EngineContext*)`. C++ hot reload is supported for
    editor-only and presentation modules.
  * **Shipping / Android / iOS**: **static linking** with generated registration (no dynamic loading on iOS,
    and fewer issues on Android).
* Extension points (service locator + interfaces): `IAssetImporter`, `IRenderFeature` (inserts render-graph passes),
  `IEditorPanel`, `IInspectorDrawer`, `IComponentTool`, `IMcpToolProvider`, `ISystemProvider`, `ITransport`, `IScriptLibrary`, `IPlatformService`.
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
(§14.2).

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
* **Rollback-safe**: effects triggered by *predicted* sim events (§15.8) are spawned as tentative. If the event is
  cancelled after a rollback, the effect is killed or faded; if confirmed, it continues untouched.
  Gameplay-relevant "particles" (bullets, debris that matters) must be sim entities, not VFX.

**Integration**

* `VfxEffect` presentation component (asset GUID, exposed-parameter overrides, autoplay, attach-to-socket).
* `dg.vfx.play(effect, { position = ..., params = { Color = ... } })` from view Luau and C++.
  Exposed parameters can be bound to reflected component fields.
* Effects can live on prefabs. Overrides of exposed parameters use the normal prefab override path (§12).

## 9. 3D Audio System (`dg::audio`)

A built-in, middleware-class 3D audio engine: event-based sound design, spatialization with HRTF, environmental
acoustics, and a mixer bus graph. Like VFX, it is **presentation-only** and never affects the sim. Third-party
middleware (FMOD, Wwise) is supported only as optional plugins behind the same interfaces; it is never required.

### 9.1 Layers

```
 Game / Luau view scripts ── dg.audio.play(event, …) ──┐
 ECS presentation components (Listener, Emitter, Zone) ─┤
                                                        ▼
 ┌──────────────────────── Audio API (game thread) ─────────────────────────┐
 │ Event instances · parameters · 3D attributes · snapshots · voice requests │
 └───────────────────────── lock-free command queue ─────────────────────────┘
                                                        ▼
 ┌──────────────────────── Audio engine (audio thread) ──────────────────────┐
 │ Event runtime (containers, randomization) → Voice manager (virtualization) │
 │ → Spatializer (panning / HRTF, attenuation, Doppler, occlusion, reverb)    │
 │ → Mixer bus graph (effects, sends, ducking, snapshots) → Master limiter    │
 └────────────────────────────────────────────────────────────────────────────┘
                                                        ▼
 Device backend: miniaudio (WASAPI · CoreAudio · AAudio/OpenSL ES) │ Null device (headless)
```

* **Threading**: the game thread only writes commands to a lock-free SPSC queue (play, stop, set parameter, set 3D
  attributes). The **mixer runs on its own high-priority audio thread**, and the device callback only copies finished
  buffers. Neither the audio thread nor the callback allocates memory, takes locks, does file IO, or touches the ECS.
* **Streaming IO** runs on the engine's IO thread into lock-free ring buffers.
* The **backend is behind `IAudioBackend`**. miniaudio is the default (device IO plus WAV/FLAC/MP3 decoding).
  FMOD and Wwise backends are user-licensed plugins.

### 9.2 Assets

| Asset | Contents |
|---|---|
| **AudioClip** (`.wav/.flac/.ogg/.mp3` source) | Imported, then cooked per platform: **Opus** (default compressed), **Vorbis**, **ADPCM** (low-CPU, mobile SFX), or PCM. Load mode per clip: *decompress on load*, *compressed in memory*, or *streamed*. Loudness analysis (LUFS) at import for normalization. Loop points and markers (cue points) are preserved. |
| **SoundEvent** (`*.dgsound`, YAML) | The unit game code plays. A tree of **containers**: Single, Random (weighted, no-repeat window), Sequence, Switch (by parameter value), Blend (crossfade by parameter), and Layer (play simultaneously). Per-node volume/pitch randomization, delays, fades, a loop count, and an instance limit with a steal policy (oldest/quietest/farthest). Cooldown. Priority. Output bus. Attenuation preset. **Parameters** (RTPC-like) that map to volume, pitch, filter cutoff, send levels, and blend positions through curves. |
| **AttenuationPreset** | Distance curve (log / linear / inverse / custom curve), min/max distance, spread vs distance, low-pass vs distance (air absorption), and reverb-send vs distance. Cone (inner/outer angle, outer gain, outer low-pass). |
| **MixerGraph** (`*.dgmix`) | Bus hierarchy, effects chains, sends, and snapshots (§9.4). |
| **ReverbPreset / Impulse Response** | Algorithmic reverb parameters or a convolution IR (cooked, partitioned FFT). |
| **AcousticMaterial** | Absorption (low/mid/high), scattering, and transmission. Assigned to geometry for occlusion and reflections (§9.3). |
| **MusicTrack / MusicSet** | Interactive music: tempo/meter grid, segments, layered stems, transition rules (on beat/bar/marker, crossfade, stinger). |

All of these are reflected, YAML-serialized assets referenced by GUID. They can be put on prefabs, and their exposed
parameters use normal prefab overrides.

### 9.3 Spatialization & acoustics

* **Listeners**: one or more `AudioListener` components (split-screen support, with weighted per-listener mixing). The
  listener usually follows the camera, with an option to use a character position for distance and the camera for
  orientation.
* **Positioning**: each emitter has a 3D position, velocity (for **Doppler**, with a per-event scale and clamp),
  orientation (for cones), and spread. Shape emitters (sphere, box, line, spline) give the closest-point position for
  rivers, crowds, or wind along walls.
* **Panning tiers**:
  * **Speakers**: VBAP-style panning for stereo, 5.1, and 7.1 (configured by the output device).
  * **Headphones**: **HRTF binaural rendering** via **Steam Audio** (Apache-2.0; Windows, macOS, Android, iOS),
    with optional custom SOFA HRTFs. It falls back to built-in stereo panning plus ITD/ILD approximation on the low tier
    or when the voice budget is exceeded.
* **Occlusion & obstruction**:
  * Low tier: a few rays per emitter against a dedicated **acoustic proxy BVH**, built at cook time from colliders or
    marked meshes (it never queries the sim physics world from the audio side). The result drives volume and low-pass.
  * High tier: Steam Audio's ray-traced occlusion and **transmission** through acoustic materials (thin walls muffle,
    thick walls block).
  * Rays are amortized over frames by emitter priority, and results are smoothed to avoid zipper noise.
* **Reverb & environment**:
  * **Reverb zones** (`AudioZone` component: box/sphere/convex volume with a blend distance and priority) select a
    reverb preset and per-bus send levels. Overlapping zones crossfade.
  * High tier: **Steam Audio real-time or baked reflections** (impulse responses per probe, baked in the editor) and
    **baked pathing / propagation** so sound bends around corners and through doorways.
* **Ambience**: ambient beds (2D, stereo/quad) plus 3D one-shot scatter around the listener (random position within a
  radius, distance/height rules, density driven by parameters such as time of day or weather).

### 9.4 Mixer

* **Bus graph** (default template): `Master → {Music, SFX → {Weapons, Footsteps, Impacts, …}, Voice, Ambience, UI}`.
  Each bus has volume, pitch, mute/solo, an **effects chain**, and sends to aux buses (reverb, echo).
* **Built-in effects**: parametric EQ, low/high-pass, compressor, limiter (always on Master), sidechain ducker, delay,
  algorithmic reverb, convolution reverb, chorus, and distortion. Effects are `IAudioEffect` plugins, processed in
  SIMD blocks.
* **Snapshots**: named mixer states (e.g. *Paused*, *Underwater*, *LowHealth*) that blend in and out over time, with
  priorities. They are triggered from game code or by zones.
* **Ducking**: sidechain rules (Voice ducks Music and SFX), plus automatic HDR-style "loudest sounds win" priority
  when many sounds are active.
* **Player settings**: per-bus user volume sliders, mono downmix, dynamic range presets (TV / headphones / night mode),
  and subtitles hooked into Voice events.

### 9.5 Voice management & performance

* **Virtual voices**: every playing event instance exists logically. Only the top N by **audibility × priority** get
  real voices. Virtual voices keep advancing their playback position (or restart on becoming audible, per event
  setting), so sounds don't "start from the beginning" when you walk back into range.
* **Real voice budgets per platform tier** (e.g. desktop 128, mobile 32), plus separate HRTF voice and occlusion-ray
  budgets.
* **Distance culling** before anything is decoded. Decoding happens only for real voices. Streamed clips use a limited
  number of streams per tier.
* **SIMD mixing** (SSE/AVX2 on x64, NEON on ARM64) in fixed-size blocks (e.g. 256 or 512 frames). Sample-rate
  conversion is done per voice only when needed. 32-bit float mixing internally.
* **Latency**: configurable device buffer sizes per platform. AAudio low-latency mode on Android when it is available.
* **Device changes** (headphones plugged in, Bluetooth, default device switch) are handled live, switching the panning
  tier automatically.
* **Profiling**: real/virtual voice counts, CPU time per bus and effect, stream buffer health, and occlusion ray counts,
  shown in Tracy and the editor profiler.

### 9.6 Relationship to the simulation & networking

* Audio is **presentation-only**. It is not in state hashes. Headless builds link a **Null audio device** and the cook
  strips audio assets (§16.1).
* **Rollback-safe playback** (§15.8): a sound triggered by a *predicted* sim event **starts immediately**, because
  latency matters more than correctness for audio. It is tagged with the event's ID. If a rollback **cancels** the event,
  the sound **fades out quickly** (about 30–80 ms, configurable per event). If the event is **confirmed**, nothing happens.
  If re-simulation raises the same event again (same ID), it is **deduplicated**, not replayed.
* **Same variation on every client**: random container choices and pitch/volume randomization are seeded from
  `hash(sim entity, tick, event id)`, so players hear the same footstep variation (best effort).
* **Gameplay must never depend on audio** (e.g. "wait until the sound finishes" must be expressed as a sim timer).

### 9.7 Integration & API

* ECS presentation components: `AudioListener`, `AudioEmitter` (event GUID, autoplay, parameters, attenuation override,
  attach-to-socket), `AudioZone`, `AcousticGeometry` (mesh/collider + material), `MusicPlayer`.
* Animation events (footsteps, etc.) and VFX events (§8.6) can trigger sound events directly.
* Luau (view scripts) and C++:

```lua
--!strict
-- footsteps.view.luau
local Footstep = dg.audio.event("SFX/Footstep")

return dg.view_system {
    on_sim_event = { Landed = function(view, e)
        dg.audio.play(Footstep, {
            position = view:position(e.entity),
            params = { Surface = e.surface, Speed = e.speed },
            source_event = e,          -- ties playback to the sim event for rollback cancel/dedupe
        })
    end },
}
```

* Exposed parameters can be **bound to reflected component fields** (e.g. engine RPM → `Rpm` parameter), and they are
  updated once per frame from the presentation world.

## 10. Skeletal Animation System (`dg::anim`)

A data-oriented skeletal animation runtime built on **ozz-animation** (SoA SIMD sampling, blending, IK, and
local-to-model jobs), with our own animation graph, deterministic sim-side animation data, GPU skinning, and LOD and
budgeting for large character counts. It is authored and inspected in the Animation Viewer (§14.4).

### 10.1 Sim animation vs. presented animation

Gameplay often depends on animation: attack windows, hitbox positions, root motion, "can cancel after frame 12".
Full pose evaluation with float blending is too expensive to run in the sim and roll back, so animation is split in two:

| | **Sim animation** (deterministic, §5) | **Presented animation** (presentation layer) |
|---|---|---|
| Runs | Every sim tick, inside the sim world; rolled back and resimulated | Every rendered frame, after the sim tick |
| State | `AnimSimState` component: graph state IDs, state time in **ticks** (integer), and the transition in progress, all of it hashed and snapshotted | Full pose buffers, blend weights, IK results, inertialization offsets; never hashed |
| Data | **Baked sim tracks** (cook time): root motion and selected gameplay bones (hitbox sockets, weapon tip) at the sim tick rate, in fixed-point; tick-exact **sim events** (hit frames, cancel windows) | Compressed ozz clips at full rate, plus curves, morph targets, and IK |
| Cost | Table lookups only, no blending math, so it is cheap to resimulate 8+ ticks | SIMD jobs on the job system, with LOD and budgets |
| Output | Root motion → movement, socket transforms → hit queries, events → gameplay | Skinning matrices for the GPU, plus cosmetic notifies (footsteps → audio §9, VFX §8.6) |

* The presented graph **follows** the sim graph: it is driven by the same state IDs and times, and only adds what is
  cosmetic (blends, IK, layers that don't affect gameplay). When a rollback or resync (§15.5) changes the sim state, the
  presented pose catches up using **inertialization** instead of popping.
* Games that need **no** animation-driven gameplay can skip the sim side entirely (`AnimSimState` is optional).
* Headless builds (§16.1) contain only the sim side: baked tracks and sim events, and no ozz clips or skinning.

### 10.2 Assets & import pipeline

| Asset | Contents |
|---|---|
| **Skeleton** (`.dgskel`) | Joint hierarchy, bind pose, joint names and stable IDs, an optional **humanoid rig definition** (Unity-Avatar-like bone mapping for retargeting), and per-joint LOD levels. |
| **AnimationClip** (`.dgclip`) | ozz compressed tracks (translation/rotation/scale; per-joint error tolerance), float curves (e.g. `FootPlant`, `WeaponTrail`), the morph-target weight track, additive flag + reference pose, **events/notifies** (presentation and sim), and the **baked sim tracks** (§10.1). |
| **AnimGraph** (`.dganimgraph`, YAML) | State machines, blend trees, layers, parameters, and the sim/presentation split per node (§10.4). |
| **BlendSpace** (1D/2D) | Sample clips placed on parameter axes (e.g. speed × direction), with triangulated 2D interpolation. |
| **AvatarMask** | A per-joint weight mask for layers (upper body, face, additive aim). |
| **RetargetProfile** | Source → target rig mapping, with per-chain scale and translation modes. |

* **Import**: FBX via **ufbx** and glTF/GLB via **fastgltf** (§2.6) produce skeleton, skinned mesh (up to 8 influences,
  normalized and quantized), clips, and morph targets. Unit and axis conversion happens at import time.
* **Compression**: ozz keyframe reduction with per-joint tolerances derived from joint chain length (error is measured at
  a virtual vertex distance, so fingers can be loose and hips strict). The codec sits behind `IAnimCodec`, so
  **ACL** (Animation Compression Library, MIT) can be used as an alternative if it measures better.
* **Retargeting** is baked at import time (cheap at runtime) or done live for previews and runtime-assembled characters.
* **Clip streaming**: large clip sets (cinematics) stream in by asset handle. The working set has a memory budget per
  platform tier.

### 10.3 Runtime pipeline (per frame)

```
 sim tick:   AnimSimState advance → root motion + sim events + socket lookups   (deterministic, tiny)
                                       │ (read-only)
 frame:      Graph update (params from components/Luau, follow sim state)
             → Sampling jobs (ozz SamplingJob, per active clip, SoA SIMD)
             → Blending jobs (layers, masks, additive)  → Inertialization
             → Local-to-model (ozz LocalToModelJob)       → Procedural: IK / look-at / foot placement
             → Skinning matrices (model × inverse bind) → upload to the GPU bone buffer (bindless)
             → GPU skinning (compute)  → skinned vertex cache used by all passes
```

* **Parallelism**: one job chain per character, scheduled on the job system (§4.4), with no locks. Character ordering
  is irrelevant because the presentation side is not deterministic.
* **Memory**: pose buffers come from per-frame linear allocators. Clips and skeletons are shared and immutable, so there
  are no per-frame heap allocations.
* **Procedural layer**: two-bone IK (limbs), FABRIK/CCD chains (tails, spines), aim and look-at with limits, and **foot
  placement** (ground raycasts with pelvis adjustment and foot-plant curves). Raycasts are **read-only queries against
  the sim physics world** during the presentation phase, which runs after the sim step and never concurrently with it.
* **Root motion**: the sim uses the baked track. The presented pose removes root translation and rotation so the
  character doesn't double-move.
* **Physics-driven**: cosmetic **ragdolls** and partial ragdolls (hit reactions) use a separate *presentation* Jolt
  world, with pose matching for get-up blends. Gameplay-relevant ragdolls (rare) must be sim entities in the sim physics
  world.

### 10.4 Animation graph

* **State machines** (nested), with transitions on conditions (parameters, sim events, end of state, sync markers),
  configurable blend curves, and interrupt rules.
* **Blend nodes**: 1D and 2D blend spaces, additive, layered with masks, and select/switch by parameter or enum.
* **Sync groups and markers**: foot-sync markers keep walk/run/sprint cycles phase-aligned while blending.
* **Inertialization**: transitions can use inertial blending (evaluate only the target pose and decay the offset from the
  source pose) instead of crossfading two full poses. This is cheaper, looks better for responsive gameplay, and is what
  hides rollback and resync corrections.
* **Parameters** come from reflected component fields (bindings) or are set from view Luau scripts / C++. Sim-side
  transitions may only read **sim** parameters and sim events, which is enforced when the graph is compiled.
* The graph compiles at cook time into a flat, data-oriented program: node arrays, no per-frame virtual dispatch.
  The sim portion is compiled into a separate deterministic table that headless builds can load.
* **Montages/slots**: one-shot actions (attacks, emotes) are played into named slots with blend-in/out, and can be
  triggered from sim events so they stay rollback-consistent.

### 10.5 GPU skinning & deformation

* **Compute pre-skinning** (desktop default): a compute pass skins each visible skinned mesh once per frame into a
  **skinned vertex cache**. Depth, G-buffer, shadow, and motion-vector passes all read the cache, which also keeps the
  previous frame's positions for motion vectors and TAA. This works with the GPU-driven meshlet path (§8.3).
* **Linear blend skinning** by default, with optional **dual-quaternion skinning** per mesh (it avoids candy-wrapper
  artifacts on twisting joints).
* **Morph targets / blend shapes**: sparse delta buffers applied in the same compute pass. Weights come from clip
  tracks and from code (facial animation, correctives).
* **Mobile tier**: vertex-shader skinning (4 influences, half-precision matrices), so no extra pass or bandwidth is
  spent. Morph targets are limited per mesh.
* **Crowds**: **vertex animation textures / baked animation instancing** for hundreds or thousands of distant characters
  (GPU-sampled, no CPU pose), with automatic switching from full skeletal near the camera.

### 10.6 Performance: LOD & budgets

* **Update-rate LOD**: near characters update every frame. Mid-distance ones update every 2–4 frames and interpolate.
  Far ones use a crowd/VAT representation. Off-screen ones update only what gameplay or audio needs (root, notifies).
* **Joint LOD**: per-skeleton joint sets per LOD (fingers and face drop first), and fewer IK solvers at distance.
* **Significance manager** (shared with VFX §8.6): ranks characters by screen size, distance, and gameplay importance
  (player, target) under a per-platform **animation CPU budget**.
* **Profiling**: per-character evaluation cost, sampled clip count, compute skinning time, and bone buffer size, shown in
  Tracy, the editor profiler, and the Animation Viewer stats (§14.4).

### 10.7 Events, notifies & integration

* **Sim events** (baked, tick-exact): `HitWindowBegin/End`, `CanCancel`, `SpawnProjectile`… They are raised inside the
  sim and are deterministic and rollback-safe by construction.
* **Presentation notifies**: footsteps, cloth/foley sounds, VFX bursts, camera shakes. They fire from the presented
  timeline and use the predicted-event rules (§15.8) when they originate from sim events.
* **ECS components**: `Skeleton` (asset + pose handle), `SkinnedMesh`, `Animator` (graph asset + parameters),
  `AnimSimState` (sim), `IKTarget`, `Ragdoll`, and `Socket` attachments (props and weapons follow joints).
* **Luau** (view scripts):

```lua
--!strict
local Locomotion = dg.anim.param("Speed")
return dg.view_system {
    query = { dg.component("Animator"), dg.component("Velocity") },
    update = function(view, e, animator, velocity)
        animator:set(Locomotion, dg.math.length(velocity.value))
    end,
}
```

---

## 11. Asset Pipeline

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

## 12. Scene, Prefabs & Runtime Objects

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

## 13. Runtime UI System (`dg::ui`) & UI Designer

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

## 14. Editor (Unity-like)

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
| **Animation Viewer / Anim Graph** | See §14.4. |
| **UI Designer** | See §13. |
| **Particle Editor** | See §14.2. |
| **Audio Mixer / Sound Event Editor** | See §14.3. |
| **MCP** | AI agent connections, activity log, permissions — see §14.5. |
| **Network Debugger** | Ping/jitter/rollback visualizer, per-tick checksum timeline, desync diff viewer. |
| **Build Settings** | Scene list, platform targets (Windows/macOS/Android), quality tiers, Build / Build & Run. |

**Editor infrastructure**: an undo/redo command stack (reflection-based property diff commands), selection service,
editor preferences, keyboard shortcut mapping (Unity defaults), asset thumbnails cache, and the editor scripting API.

### 14.1 Level design / world-building tools

* **Terrain**: heightmap tiles (clipmap rendering), sculpt/smooth/flatten/stamp brushes, splat-map texture painting,
  holes, and a deterministic collision heightfield (Jolt `HeightFieldShape`).
* **Foliage / detail painting**: GPU-instanced scattering with density, slope, and height rules.
* **Mesh/brush modeling** (ProBuilder-like): primitive shapes, face/edge/vertex editing, extrude, bevel, CSG blocking,
  and UV tools — for greyboxing.
* **Splines**: road/river/fence generation along splines.
* **Prefab painting / scatter** and **grid snapping & alignment** tools.
* **Navigation**: navmesh baking with **Recast/Detour** (Zlib), using deterministic pathfinding queries in the sim.
* **Lighting tools**: light probe and reflection probe placement, lightmap baking, and GI debug visualizers.

### 14.2 Particle Editor (built-in)

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

### 14.3 Audio tools (built-in)

| Tool | Function |
|---|---|
| **Sound Event Editor** | Opened by double-clicking a `*.dgsound`. A container tree (Random/Sequence/Switch/Blend/Layer) with drag-and-drop clips, per-node randomization, parameter → property curves (shared curve widget), instance limits, and bus routing. **Preview** with a 2D/3D position pad (drag the emitter around a virtual listener), live parameter sliders, and a "play N times" button to audition randomization. |
| **Audio Mixer** | Unity-style mixer window: channel strips per bus with faders, peak/RMS/LUFS meters, mute/solo, effects chains (add/reorder/bypass), sends, and a snapshot list with blend preview. Changes made **during play mode** can be kept or reverted on stop. |
| **Attenuation Editor** | Distance/low-pass/spread/reverb-send curves with a top-down preview, and the cone editor. |
| **Music Editor** | Segment timeline on a beat/bar grid, stem layers, markers, transition matrix, and stinger setup. |
| **Clip import settings** | Waveform view, loop points and markers, format/quality per platform with estimated size, and loudness normalization. |
| **Scene View gizmos** | Min/max attenuation spheres, cones, shape emitters, reverb zone volumes and blend distances, and acoustic geometry colored by material. |
| **Acoustics baking** | Paint or assign acoustic materials, place reflection/pathing probes, and bake Steam Audio reflections and pathing per level (stored as level-scoped derived data). |
| **Audio Debugger** | Live list of event instances (real/virtual, audibility, bus, position), occlusion rays drawn in the Scene View, a per-emitter "why is this not audible?" readout, voice/CPU budget graphs per platform tier, and a rollback trace showing cancelled and deduplicated sounds. |

### 14.4 Animation Viewer & animation tools (built-in)

The **Animation Viewer** opens when you double-click a skeleton, skinned mesh, clip, blend space, or anim graph. It is a
dockable editor window, and it can also run standalone (`dg-editor --anim-viewer <asset>`) for animators who only need
to review content.

| Area | Function |
|---|---|
| **Viewport** | Isolated preview scene: orbit/fly camera, lighting presets, ground grid, floor shadow, and background options. Mesh / skeleton / both display modes. Bone drawing (octahedral, lines, axes, names), selection of joints by clicking. |
| **Playback & timeline** | Play/pause, step by frame **and by sim tick**, scrub, loop/ping-pong, speed 0.05×–4×, range selection, and frame/time/tick display. Timeline tracks for events and notifies (drag to move, add from a palette), float curves, morph weights, and **baked sim tracks** shown against the full-rate pose. |
| **Clip & asset browser** | Clips for the current skeleton with search and tags, drag a clip into the viewport to preview it, and favorites. |
| **Blend preview** | Pick two clips (or a blend space) and a blend weight, or drag a point on the 2D blend-space pad. Sync-marker alignment display, additive preview over a base pose. |
| **Root motion** | Trajectory drawing (position and facing per tick), toggle for in-place vs. root-motion playback, and a comparison of the baked sim track vs. the evaluated root. |
| **Compression inspector** | Raw vs. compressed size per clip, an error heatmap per joint, ghost overlay (source pose vs. compressed pose), and tolerance sliders with live recompression and size readout. |
| **Skinning & mesh debug** | Bone-weight heatmap per selected joint, influence-count view, bind-pose check, dual-quaternion vs. linear comparison, morph target sliders. |
| **IK & procedural preview** | Draggable IK targets, foot-placement preview on a slope or steps proxy, look-at target. |
| **Retarget preview** | Play a clip on a different skeleton through a RetargetProfile, side by side with the source, with per-chain adjustment. |
| **LOD preview** | Joint LOD and update-rate LOD at a simulated distance, and the crowd/VAT representation. |
| **Stats** | Joint count, clip count, evaluation time, skinning time, memory, and per-platform-tier budget usage. |

**Related tools**

* **Anim Graph Editor** (imgui-node-editor): state machines, blend trees, layers, and parameters, with sim nodes visually
  marked. Compile errors (e.g. a sim transition reading a presentation parameter) appear inline.
* **Live Graph Debugger**: select a character in Play mode to see active states, transition progress, blend weights,
  parameter values, and the last sim events. You can pause and scrub its recent history, and see when a rollback
  corrected its sim animation state.
* **Rig / Avatar editor**: humanoid bone mapping (auto-map by name, then manual fix), joint limits for IK and ragdolls,
  and ragdoll body setup.
* **Event authoring** validation flags sim events that fall between sim ticks (they snap to the nearest tick at cook
  time) and notifies that have no handler.
* Everything is undoable and saved as diff-friendly YAML. Plugins can add viewer panels (`IAnimViewerPanel`) and graph
  nodes (`IAnimNode`, with sim and presentation implementations).

### 14.5 MCP server — AI agent integration (`dg::mcp`)

The editor embeds a **Model Context Protocol (MCP) server**, so AI agents such as Claude Code (or any MCP client) can
inspect and edit a project through the engine itself: create and modify entities and prefabs, set up animations, write
and attach Luau gameplay logic, run play mode to verify, and save levels. Agents work **through the same editor
commands a human uses**. Every change is reflected in the open editor, is undoable, and ends up as diff-friendly YAML
and Luau files in the repository.

**Architecture**

```
 Claude Code / MCP client
   │  stdio (JSON-RPC 2.0)                  │  Streamable HTTP (127.0.0.1 only, bearer token)
   ▼                                        ▼
 dg-mcp (bridge process) ── local IPC ──► dg::mcp server (inside dg-editor, editor-only module)
   │ starts `dg-editor --headless --mcp`      │ protocol layer: initialize, tools, resources, prompts,
   │ if no editor is running                  │   progress, cancellation, tasks, list-changed notifications
                                              │ request queue → marshalled onto the editor main thread
                                              ▼
                       Editor services: command stack (undo/redo) · selection · asset DB · scene/prefab
                       serializer · anim graph compiler · Luau checker · play mode · viewport capture
```

* **Protocol implementation**: our own, small JSON-RPC 2.0 layer (yyjson) **pinned to MCP spec revision 2025-11-25**,
  with version negotiation in `initialize`. Spec differences live in a protocol adapter, so later revisions (the
  current drafts change sessions and move tasks into an extension) can be added without touching tool code. Community
  C++ MCP SDKs exist, but none is official; they were evaluated and not adopted, to keep the dependency surface small.
* **Transports**:
  * **stdio** (default for Claude Code): the client launches `dg-mcp`, a tiny bridge that connects to the running editor
    over a local socket (Unix domain socket / Windows named pipe in `.dg/`). If no editor is open, it starts a
    **headless editor** on the project (`dg-editor --headless --mcp`; offscreen rendering for screenshots when a GPU is
    available).
  * **Streamable HTTP** for clients that prefer it: it binds to **127.0.0.1 only** on a random port, requires a per-session
    bearer token written to `.dg/mcp.json`, and validates the `Origin` header (DNS-rebinding protection, as the spec
    requires). It is off by default.
* **Threading**: requests are parsed on an IO thread and executed on the editor main thread through a command queue.
  Long operations (asset import, lightmap/acoustics bakes, builds, determinism checks) report **progress
  notifications**, honor **cancellation**, and are exposed as MCP tasks where the client supports them.
* **Setup**: `dg mcp install` writes a project-scoped `.mcp.json` for Claude Code (launching `dg mcp serve`, which locates
  or builds `dg-mcp`). `dg mcp status` shows connections. The Launcher (§16.2) has matching buttons.

**Tools** (namespaced; JSON Schemas for inputs and outputs are **generated from `dg::refl`**, so every reflected component
and property is editable with validated types, ranges, and enums, and new components appear automatically)

| Area | Tools |
|---|---|
| **Levels & scenes** | `level.list`, `level.open`, `level.new`, `level.save`, `level.save_as`, `level.get_hierarchy` (paginated, filterable), `level.diff` (unsaved changes vs. disk) |
| **Entities & components** | `entity.create` (empty, from components, or from a prefab), `entity.delete`, `entity.duplicate`, `entity.set_parent`, `entity.find` (by name, tag, component, or spatial query), `component.add`, `component.remove`, `component.get`, `component.set` (property paths + typed values), `transform.set` / `transform.align` (snap, align to surface) |
| **Prefabs** | `prefab.create_from_entity`, `prefab.instantiate`, `prefab.list_overrides`, `prefab.apply_overrides`, `prefab.revert_overrides`, `prefab.create_variant` |
| **Assets** | `asset.search`, `asset.info`, `asset.import` (files inside the project only), `material.create` / `material.set`, `asset.thumbnail` (image) |
| **Animation** | `anim.list_clips`, `anim.clip_info`, `anim.graph_create`, `anim.graph_edit` (add states, transitions, parameters, blend spaces, layers — as a batch), `anim.graph_validate` (compiler diagnostics, incl. sim/presentation rules §10.4), `anim.assign_animator`, `anim.add_event`, `anim.preview` (Animation Viewer render at time t → image) |
| **Logic (Luau)** | `script.api` (slices of the generated `dg.d.luau`), `script.create` (`*.sim.luau` / `*.view.luau` from content), `script.read`, `script.edit` (text patch), `script.check` (Luau type check + **determinism lint** §6.3 → diagnostics), `script.attach` (register a system or bind it to an entity/prefab) |
| **VFX & audio** | `vfx.create_from_template`, `vfx.set_parameter`, `audio.create_event`, `audio.assign_emitter` |
| **Play & verify** | `play.start`, `play.stop`, `play.step` (N ticks), `play.state_hash`, `play.set_input` (scripted inputs), `verify.determinism` (headless replay twice → compare hashes), `editor.console` (recent logs, filters), `editor.screenshot` (Scene/Game view or a camera → MCP image content) |
| **Editor** | `edit.batch` (several edits as one atomic undo step), `editor.undo`, `editor.redo`, `editor.select`, `editor.focus` (so the human sees what the agent changed) |
| **Build** (full permission) | `build.run` (`dg build`), `build.test` |

**Resources & prompts**

* Resources: `dg://project/manifest`, `dg://levels/{guid}` (level YAML), `dg://schema/components` (reflected JSON Schemas),
  `dg://api/luau` (`dg.d.luau`), `dg://docs/agent-guide` (engine conventions for agents: determinism rules, sim vs. view
  scripts, prefab workflow), and `dg://console/recent`. Level and asset resources support **subscriptions**, so agents
  are notified when a human edits the same level.
* Prompts (templates surfaced as client slash commands): `blockout_level`, `create_character` (mesh + skeleton + animator +
  controller script), `add_gameplay_system`, `fix_desync`.
* The `initialize` result carries **server instructions** with the recommended loop: read the schema and API → edit in
  batches → `script.check` / `anim.graph_validate` → `play.step` + `editor.screenshot` to verify →
  `verify.determinism` → `level.save`.

**Safety model**

* **Nothing reaches disk until `level.save` (or an asset-creating tool) runs**, and everything written is ordinary,
  reviewable project files (YAML, Luau). Git is the final review step.
* **Every mutating call is one undo transaction**, labeled `MCP: <tool>` in the editor's history, so a human can undo any
  agent action. `edit.batch` is all-or-nothing.
* **Permission levels** per project (`.dg/mcp-policy.yaml`, editable in the MCP panel): `read-only`, `edit` (default:
  mutations allowed, saving needs confirmation), and `full` (save, import, build, play). There is also a per-tool
  allow/deny list.
* **Confirmations**: destructive operations (deleting assets, overwriting a level, builds) ask the human, either through
  MCP **elicitation** when the client supports it or with an editor modal.
* **Sandboxing**: file access is limited to the project directory (`Assets/`, `Scripts/`, `Levels/`). There is **no
  shell or arbitrary native-code execution tool**. Luau written by agents runs only inside the normal script sandbox
  (§6.3).
* **Play mode**: edits made while playing apply to the play world and are discarded on stop (with the same "keep
  changes" option as the editor). Tool results always say which world they touched. MCP edits are disabled while
  connected to a networked session.
* **Never shipped**: `dg::mcp` is an editor-only module. It is excluded from players, Release builds, and headless server
  builds. The `--mcp` flag only exists on the editor binary.

**MCP panel (editor)**

Connection status and connected clients. A live **activity log** of every tool call (arguments, result, duration, and
affected entities highlighted in the Hierarchy and Scene View), with **undo per call**. Permission level and tool toggles.
A **pause agent** switch that queues incoming mutating calls until resumed.

**Extensibility**: plugins register tools, resources, and prompts through `IMcpToolProvider`. Tool schemas come from
reflected request and response structs, so a plugin's tools get the same validation, undo, and permission handling.

---

## 15. Built-in Deterministic Multiplayer Backend (`dg::net`)

The engine ships a complete, **built-in** deterministic netcode stack: the client library, a dedicated session server,
and the tooling. A game gets multiplayer by defining `InputCommand` and writing sim systems; it never writes
networking code. The model is **predict/rollback with server-confirmed inputs**, the same family of architecture as
Photon Quantum 3. That is a design reference only; the implementation is our own.

### 15.1 Topology & roles

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
| **Server — checksum validator** (always on) | Compares per-tick checksums sent by clients (§15.4) and triggers resync (§15.5). |
| **Server — authoritative simulation** (optional, `mode=verify`) | Runs the **same sim** headlessly on the room's level (§16.1), using only confirmed inputs. It never predicts and never rolls back. It is the ground truth for checksums, snapshots, late joins, and anti-cheat. |

Two server modes:

* **`relay`** (cheap, scales to many rooms per core): the server only confirms inputs and compares client checksums.
  The truth is the **majority** of client checksums. Snapshots for resync or late join are requested from a client
  that agrees with the majority (they are hash-verified before forwarding).
* **`verify`** (recommended for competitive/ranked play): the server runs the authoritative headless sim. The truth is
  always the server's state, and the server serves snapshots itself.

Peer-hosted sessions (one client also runs the server room in-process) use the same code path, for LAN and dev.

### 15.2 Ticks, frames and time synchronization

* **Verified tick**: every input for it is confirmed by the server. The state is final and identical everywhere.
* **Predicted tick**: simulated ahead of the last verified tick using predicted remote inputs. It is rolled back when
  confirmations arrive.
* Clients keep their sim clock **ahead of the server** by about `RTT/2 + jitterBuffer`, so local inputs arrive before
  the deadline. Drift is corrected by **time dilation** (slightly speeding up or slowing down the client's tick
  accumulator, ±5%), never by skipping ticks.
* `inputOffset` (local input delay, 0–N ticks) is configurable per game, trading latency for fewer rollbacks.
* The **max prediction window** (e.g. 10 ticks) is configurable. Beyond it, the client stalls its sim, rather than
  diverging further.

### 15.3 Input pipeline

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

### 15.4 State hashing (desync detection)

* **What is hashed**: all `Sim`-tagged components, walked via reflection in canonical entity/archetype order, plus the
  entity allocator state, the sim RNG, the physics state hash (Jolt/Box2D), the tick number, and registered sim
  resources. XXH3-64 (128-bit optionally).
* **Hierarchical hashing**: the world hash is composed of **per-archetype-chunk hashes** and **per-system-group hashes**.
  Chunk hashes are cached and recomputed only for chunks that are dirty this tick. That keeps per-tick hashing cheap
  enough to run every tick in development and every N ticks (configurable, e.g. every 4) in shipping.
* Clients send `(tick, worldHash)` for **verified ticks only** (predicted state is never compared).
* On mismatch, the server can request the **hash tree** from the divergent client. Comparing it with the reference tree
  pinpoints the exact component type and entity range that diverged, and this goes into the desync report.

### 15.5 Resync of desynced clients

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

### 15.6 Session lifecycle & compatibility

1. Client connects → **handshake** with the determinism manifest: engine build hash, sim-module/plugin hashes,
   sim-asset content hash for the room's level, tick rate, and `InputCommand` schema hash. Any mismatch is rejected
   with a clear error.
2. Room start: the server broadcasts the start tick, initial RNG seed, and player slots. All peers load the level and
   start from identical state.
3. Running: the input/confirm loop (§15.2–13.3), checksums (§15.4), and resync on demand (§15.5).
4. End: the server optionally stores a **replay** (initial snapshot + confirmed inputs). This is compact, and can be
   re-simulated by any headless build for verification, spectating, or bug repro.

### 15.7 Server architecture (`dg-server`)

* **One process, many rooms**. Each room runs on a job-system worker, with a fixed tick timer per room.
* Room capacity and modes (`relay` vs `verify`) are set by room config. In `verify` mode a room loads its level
  through the headless asset path (§16.1).
* **Management API** (HTTP/JSON, plugin-replaceable): create/list/close rooms, health, metrics (Prometheus format),
  and replay download.
* **Matchmaking/lobby** is out of the core. It is a plugin interface (`ILobbyService`) with a simple built-in room
  list for development. External services (Steam, EOS, custom) plug in here.
* **Transport** is behind `ITransport`: ENet (default, UDP), GameNetworkingSockets (encryption, relays), and a WebSocket
  fallback (later). Channels: unreliable-sequenced (inputs), reliable-ordered (commands, snapshots, control).

### 15.8 Client API surface (C++ and Luau)

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

### 15.9 Non-deterministic replication (optional plugin)

State replication (server-authoritative snapshots + interpolation) for games that don't fit lockstep, such as MMO-scale
worlds. It is not the default and is not covered by the determinism guarantees.

---

## 16. Build Configurations & Packaging

| Config | Asserts | Optimizations | Tracy | Editor | Notes |
|---|---|---|---|---|---|
| `Debug` | ✅ | ❌ | ✅ | ✅ | Validation layers on. |
| `Development` | ✅ | ✅ | ✅ | ✅ | Default daily config. |
| `Profile` (planned) | minimal | ✅ | ✅ | ❌ | Player only. |
| `Release` (= Shipping) | ❌ | ✅ (+LTO) | ❌ | ❌ | Static plugins, stripped symbols, cooked paks only. |
| `Server` profile (`DG_HEADLESS`, any config) | ❌ (opt-in) | ✅ (+LTO) | optional | ❌ | Headless dedicated server, level-scoped cook (§16.1). |

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

### 16.1 Headless build mode (per-level)

A **headless build** is the simulation without presentation. It produces dedicated servers (`verify` rooms, §15.1),
bots, replay verifiers, and CI determinism runners — all **for a specified set of levels**.

**Compile-time profile (`DG_HEADLESS=1`)**

* Linked: core, platform (no window), reflection/serialization, jobs, ECS, sim, physics, Luau (sim scripts only), net,
  asset runtime, plugin manager, and plugins marked `deterministic: true` or `headless: true`.
* **Excluded**: RHI/renderer, GI, particles/VFX (`*.dgvfx` stripped by the cook), UI, audio, presented animation (ozz clips, skinning; the sim's baked
  anim tracks and events stay, §10.1), ImGui, editor, and all `*.view.luau` scripts. A **Null RHI / Null audio** backend satisfies any optional
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
  the handshake (§15.6), so a server can only host levels whose sim content matches the clients' builds.

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
  (Game View with N simulated clients, §14).

### 16.2 Developer workflow (project generation, build & run)

The canonical build description is **CMake presets** (`CMakePresets.json`). `dg` is a thin, dependency-free Python 3.9+
tool on top of them, so that the same few commands work on every host:

| Command | Effect |
|---|---|
| `dg setup [--vcpkg]` | Checks CMake/Git/compilers/Ninja; optionally clones and bootstraps vcpkg into `external/vcpkg`. |
| `dg generate [--ide]` | Configures the host's default preset. `--ide` creates and opens an Xcode (macOS) or Visual Studio 2022 + ClangCL (Windows) project with Debug/Development/Release configurations. |
| `dg build [debug\|dev\|release] [editor\|player\|server\|tests]` | Builds everything or one app. Configures first if needed. |
| `dg run [config] [app] [-- args]` | Builds, then runs the app (default: editor on desktop). |
| `dg test [config] [-L label]` | Builds, then runs ctest (`determinism`, `smoke` labels). |
| `dg determinism` | Runs the player headless in Debug and Release and fails unless the final state hashes are identical. |
| `dg clean [--all]`, `dg presets` | Deletes build trees; lists presets. |
| `dg gui` | Opens the **Detergine Launcher** (below). |
| `dg mcp install \| serve \| status` | Writes the project's `.mcp.json` for Claude Code, runs the stdio bridge, or shows connections (§14.5). |

Common options on every command: `--preset P`, `-D NAME=VALUE` (engine CMake options such as `DG_HEADLESS`,
`DG_BUILD_EDITOR`, `DG_BUILD_TESTS`, `DG_WARNINGS_AS_ERRORS`; the tree is reconfigured only when a value changes),
`-j N`, `--verbose`, `--fresh`.

**Detergine Launcher** (`tools/dg_launcher.py`, tkinter, standard library only): a small GUI with the platform preset
(filtered to the host), configuration, build target, all engine build options, parallel jobs, verbose/fresh toggles,
run options (app, headless, fast, ticks, tick rate, players, seed, level, extra arguments), and the test label filter.
Buttons cover Build, Run, Test, Check Setup, Generate, Open IDE, Determinism, Clean and Open Build Folder, plus Stop.
The output streams live with colored errors and warnings. **Every action runs the equivalent `dg` command, which is
shown and copyable**, so the GUI never diverges from the CLI. Settings persist per user in `.dg/launcher.json`.
`Launcher.command` (macOS) and `Launcher.bat` (Windows) open it with a double-click.

* **Presets**: `macos-arm64` (Ninja), `macos-arm64-xcode`, `windows-x64` (Ninja + clang-cl; `dg` loads the VS developer
  environment automatically), `windows-x64-vs2022`, `linux-x64`, `linux-x64-server` (headless profile), `android-arm64`
  (NDK via `ANDROID_NDK_HOME`). Each has `-debug`, `-dev`, and `-release` build and test presets for IDE integrations
  (VS Code CMake Tools, CLion, Visual Studio).
* All presets use **multi-config generators**, so one build tree holds Debug, Development, and Release. Switching
  configuration never requires regenerating. Binaries land in `build/<preset>/bin/<Config>/`.
* The host default is Ninja when it is installed, and otherwise the IDE generator (Xcode / Visual Studio), so a fresh
  machine builds with no extra installs.
* vcpkg is picked up automatically from `VCPKG_ROOT` or `external/vcpkg`. Platform toolchains (the Android NDK) are
  chain-loaded through it.
* `tests/SimDeterminismTest.cpp` holds a **golden state hash** recorded on macOS ARM64. Every platform and configuration
  must reproduce it; this is the seed of the determinism CI (§5.4).

---

## 17. Roadmap (phased)

| Phase | Deliverables |
|---|---|
| **0 — Foundation** | CMake/vcpkg skeleton, CI matrix (Win/macOS/Android), core lib, SDL3 window, logging, Tracy, the `dg-reflect` MVP, unit tests. |
| **1 — Sim core** | Archetype ECS, fixed-tick loop, deterministic math, RNG, snapshots, checksum, determinism CI with replays (x64 vs ARM64). |
| **2 — Rendering MVP** | RHI (Vulkan on Windows, MoltenVK on macOS, Android), render graph, bindless, glTF import, PBR forward/deferred, ImGui. |
| **3 — Scripting & physics** | Luau sandbox, binding generator, `dg.d.luau`, Jolt + Box2D deterministic integration, hot reload. |
| **4 — Editor MVP** | Docking layout, Hierarchy/Inspector/Scene/Game/Project/Console, gizmos, undo/redo, play mode, scenes, prefabs (+ overrides). |
| **4b — MCP server** | `dg::mcp` protocol layer + stdio bridge, reflection-generated tool schemas, level/entity/component/prefab/asset tools, Luau script tools with determinism lint, play/step/screenshot/verify tools, undo transactions, permission policy, MCP panel, `dg mcp install`; then animation, VFX, and audio tools, plus HTTP transport. Each later phase adds MCP tools for its features. |
| **5 — Networking** | Headless build profile + level-scoped cook, `dg-server` (relay → verify mode), confirmed-input protocol, predict/rollback, hierarchical state hashing, snapshot resync / late join, network debugger panel. |
| **6 — Content** | FBX (ufbx), 2D sprites/atlases/tilemaps, runtime UI and UI Designer. |
| **6c — Animation** | Skeleton/clip import (ufbx, fastgltf), ozz runtime jobs, compute skinning (+ mobile vertex skinning), anim graph (state machines, blend spaces, layers, inertialization), sim anim state with baked tracks and sim events, IK and foot placement, update-rate and joint LOD, Animation Viewer, then the graph editor/debugger, retargeting, morph targets, ragdolls, and crowd VAT. |
| **6a — Audio** | Audio thread + miniaudio backend, clips/Opus cooking, SoundEvents + containers, attenuation, bus mixer + snapshots, virtual voices, rollback-safe playback; then Steam Audio HRTF/occlusion, reverb zones, baked acoustics, music system, and the audio editor tools (§14.3). |
| **6b — VFX** | Particle graph IR + YAML serialization, module-stack editor, CPU SIMD runtime → GPU fused-kernel runtime, sprites/ribbons/meshes, sorting, depth/SDF collision, node-graph view, flipbooks, budgets/LOD, profiler integration. |
| **7 — Packaging** | `dg-cook`, `.dgpak`, `dg-package` for Windows/macOS/Android, Build Settings window. |
| **8 — Advanced rendering** | Shadows, post-FX, baked GI → DDGI → Lumen-style SDF/surface-cache GI → HW RT path; mobile tier tuning. |
| **9 — World building** | Terrain, foliage, brush modeling, splines, navmesh. |
| **10 — Ecosystem** | Plugin SDK docs, sample games, iOS port, native Metal backend (if needed). |

---

## 18. Key Risks & Open Questions

| Risk | Mitigation |
|---|---|
| Cross-arch float determinism is fragile (compiler upgrades, new code paths) | Determinism CI gate on every PR; pin compiler versions; isolate sim code in `dg_sim_target`; lint banned functions. |
| MoltenVK/KosmicKrisp feature gaps (ray query, descriptor buffer, mesh shaders) on macOS | Capability tiers; the GI software path doesn't require RT; keep the RHI clean enough for a native Metal backend. |
| C++26 reflection not available on Apple/NDK/MSVC toolchains | `dg-reflect` codegen; a macro surface compatible with future P2996 annotations. |
| Resync storms (a client repeatedly desyncs on a deterministic bug) | `ResyncPolicy` rate limits; repeated same-tick desyncs are reported as bugs with hash-tree diffs; determinism CI includes the headless server build. |
| Server cost in `verify` mode | `relay` mode for casual rooms; headless builds strip presentation; many rooms per process. |
| Rollback cost with heavy physics | Configurable rollback window; lockstep mode; physics LOD for sim entities; profile on Android early. |
| GPU particle cost on mobile/TBDR (overdraw, bandwidth) | CPU SIMD path, trimmed sprite geometry, budgets per tier, low-resolution translucency, profiling on Android from the start of phase 6b. |
| Audio glitches from priority inversion or allocation on the audio thread | Real-time-safe rules (no locks/allocations/IO on the audio thread), lock-free queues, an underrun counter in CI soak tests, and Android low-latency testing on real devices. |
| Sim/presentation animation mismatch (a hitbox from the baked track vs. what the player sees) | Same source clip and sample times; the viewer overlays sim tracks on the presented pose; tolerance tests in CI; inertialization hides corrections instead of popping. |
| Lumen-style GI complexity | Phased tiers; baked/DDGI first; time-boxed research spikes; AMD MIT references. |
| AI agents making unwanted or destructive edits through MCP | Undo transaction per call; nothing saved without `level.save`; permission levels and confirmations; project-only file access; no shell or native-code tools; activity log; pause switch. |
| MCP spec churn (sessions, tasks moving to extensions in newer drafts) | Pinned revision with version negotiation; differences isolated in the protocol adapter; conformance tests against the official MCP inspector. |
| Scope (an engine of Unity+UE breadth) | Strict phase gating; every module behind interfaces so features can be delayed without refactors. |

**Open questions:**

1. Default tick rate (30 vs 60 Hz), and whether it is per-project configurable. *(Proposed: per-project, default 30.)*
2. Lua-first vs C++-first gameplay template for new projects.
3. ENet vs GameNetworkingSockets as the default transport.
4. Visibility buffer vs deferred G-buffer as the desktop default.
5. Default server mode for new projects (`relay` vs `verify`). *(Proposed: `verify` once the headless build is stable.)*

---

## 19. References

* Lumen — SIGGRAPH 2022 Advances: <https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf>
* Lumen overview (Epic tech blog): <https://www.unrealengine.com/en-US/tech-blog/unreal-engine-5-goes-all-in-on-dynamic-global-illumination-with-lumen>
* GI-1.0 (AMD): <https://arxiv.org/abs/2310.19855>
* Jolt Physics (determinism notes): <https://jrouwe.github.io/JoltPhysics/>
* P2996 Reflection for C++26: <https://isocpp.org/files/papers/P2996R13.html>
* GGPO rollback reference: <https://github.com/pond3r/ggpo>
* Photon Quantum 3 (architecture reference for server-confirmed-input predict/rollback; no code used): <https://doc.photonengine.com/quantum/>
* Luau: <https://luau.org/>
* Model Context Protocol specification (2025-11-25): <https://modelcontextprotocol.io/specification/2025-11-25>
