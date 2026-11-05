---
title: Glossary
section: Reference
order: 10
status: implemented
summary: The engine dictionary: every Detergine term, acronym and concept in one place, from tick and golden hash to Q16.16 and FIFO reuse, with links to the pages that explain it.
---

This is the Detergine dictionary. Each term has a short, precise definition and links to the pages where it matters.
Terms for systems that are not built yet say so. Every term on this page also appears in the [A–Z index](../a-z.html).
When you add a term, keep the entries alphabetical inside their letter group (see
[Writing documentation](../contributing/documentation.md#keeping-the-manual-current)).

## A

### ADR

An *architecture decision record*: a short document in `docs/adr/NNNN-title.md` that records one architectural
decision and its reasons. A decision that changes the design also updates `docs/DESIGN.md`. See
[Decisions and roadmap](../architecture/decisions-and-roadmap.md#keeping-this-page-current).

### AppConfig

`dg::runtime::AppConfig`, the struct that holds an app's settings (name, headless, fast, tick count, tick rate,
players, seed, level). An app's `main()` passes its defaults, and command-line flags override them. See
[Runtime and apps](../systems/runtime-and-apps.md#reference).

### Archetype

In the planned ECS, the set of component types an entity has. All entities with the same archetype are stored
together in chunks, one contiguous array per component type. See [ECS](../planned/ecs.md#storage).

### Arena

A memory region that hands out allocations by bumping an offset and frees them only in bulk. In Detergine this is
`dg::LinearAllocator`. See [Linear allocator](#linear-allocator) and
[Memory allocators](../systems/memory.md#scratch-memory-linearallocator).

### ARM64

The 64-bit ARM CPU architecture (AArch64), used by Apple Silicon Macs, Android and iOS devices. macOS ARM64 is the
reference platform on which every golden hash is recorded. See [Determinism model](../architecture/determinism.md).

### ASan

AddressSanitizer, a compiler instrumentation that detects out-of-bounds accesses, use-after-free and leaks at run time.
Enable it with `-D DG_ENABLE_ASAN=ON`; CI runs the Debug suite under ASan and UBSan on Linux x64. See
[Testing](../tooling/testing.md#run-under-sanitizers).

### AssetHandle

`AssetHandle<T>`, the planned reference-counted handle through which serialized data and runtime code refer to an
asset by GUID, never by path. See [Assets, cooking and packaging](../planned/assets-and-packaging.md#asset-database).

### Authoritative sim

The server role in `verify` mode that runs the same simulation headlessly from confirmed inputs. It never predicts or
rolls back, and it is the ground truth for checksums, resync snapshots and late join. Planned. See
[Multiplayer networking](../planned/networking.md#roles-and-server-modes).

## B

### Bindless resources

A rendering model in which all GPU resources live in one global descriptor heap and shaders refer to them by 32-bit
index instead of per-draw bindings. The planned Vulkan RHI is bindless. See
[Rendering](../planned/rendering.md#rhi).

### Bit-identical

Equal in every bit, not merely close. Detergine's core guarantee is that the simulation produces bit-identical state on
every platform, architecture, compiler and build configuration. See
[Determinism model](../architecture/determinism.md#overview).

### Box2D

Box2D v3, the MIT-licensed 2D physics library the planned `dg::physics` module integrates. v3 is designed for
cross-platform determinism, which the engine verifies per release. See [Physics](../planned/physics.md).

### Build tree

The CMake output directory for one preset, `build/<preset>/`. It holds `CMakeCache.txt`, the binaries in
`bin/<Config>/` and the libraries in `lib/<Config>/`. Because every preset uses a multi-config generator, one tree
holds all three configurations. See [Build system](../tooling/build-system.md#build-tree-layout).

### builtin-baseline

The vcpkg commit pinned in `vcpkg.json` that fixes which port versions every machine resolves. It is changed together
with `VCPKG_RELEASE` in `tools/dg.py`. See [Build system](../tooling/build-system.md#vcpkg).

## C

### Canonical NaN

The single NaN bit pattern `0x7FC00000` that `dg::detmath` returns for every NaN result. x64 and ARM64 produce
different default NaN bits, so canonicalizing them keeps state hashes identical. See
[Deterministic math](../systems/detmath.md#canonical-nan).

### Capability tier

A class of GPU features detected at startup (`Mobile`, `Desktop`, `DesktopRT`) that selects rendering and VFX paths.
Planned. See [Rendering](../planned/rendering.md#rhi).

### Checksum validator

The server role, always on, that compares the state hashes clients send for verified ticks and triggers a resync on a
mismatch. Planned. See [Multiplayer networking](../planned/networking.md#roles-and-server-modes).

### Chunk

In the planned ECS, a 16 KiB block that stores the entities of one archetype in SoA layout. Chunks are also the unit
of hierarchical state hashing and snapshots. See [ECS](../planned/ecs.md#storage).

### clang-cl

Clang's MSVC-compatible compiler driver. The `windows-x64` preset builds with Ninja and clang-cl against the MSVC
standard library; it gets only `/clang:-ffp-contract=off` as its strict FP flag. See
[Build system](../tooling/build-system.md#presets).

### Clean-room implementation

An implementation written only from public papers and talks, without looking at or translating proprietary source
code. The planned Lumen-style GI must be clean-room; Unreal and Unity code may never be copied. See
[Rendering](../planned/rendering.md#global-illumination-clean-room-lumen-style).

### Column-major

A matrix layout in which each column is stored contiguously. `dg::Mat3` and `dg::Mat4` are column-major, matching the
Vulkan, GLSL and Slang `column_major` layout, so a `Mat4` can be copied to the GPU unchanged. See
[Vectors, quaternions and matrices](../systems/vector-math.md#matrices).

### Command

In the planned netcode, a rare, larger reliable message (buy an item, chat) that the server confirms into a specific
tick like an input. Not to be confused with a [command buffer](#command-buffer) or a `dg` command. See
[Multiplayer networking](../planned/networking.md#inputs).

### Command buffer

In the planned ECS, a queue of deferred structural changes (spawn, destroy, add or remove components) that is flushed
in deterministic order at phase boundaries. See [ECS](../planned/ecs.md#structural-changes-events-and-ids).

### Configuration

One of the three build configurations, `Debug`, `Development` and `Release`, selected with the `debug`, `dev` and
`release` words of `dg`. All three use the same strict FP flags, so they compute identical simulations. See
[Build system](../tooling/build-system.md#configurations).

### Confirmed input set

The inputs of all players for one tick, as broadcast by the server's input authority. A confirmed tick is final and
never reopened. Planned. See [Multiplayer networking](../planned/networking.md#roles-and-server-modes).

### Cook

The planned conversion of source assets into platform-specific runtime data by `dg-cook`: texture compression, mesh
quantization, shader variants and `.dgpak` archives. A headless cook keeps only sim content. See
[Assets, cooking and packaging](../planned/assets-and-packaging.md#cooking-and-dgpak).

### CTest

CMake's test driver. Every GoogleTest case is registered as its own CTest test named
`<executable>.<Suite>.<Test>`, selectable by name (`-R`) or label (`-L`). See [Testing](../tooling/testing.md).

## D

### DDGI

Dynamic diffuse global illumination: irradiance probe volumes updated sparsely on the GPU. It is the planned GI tier
for mobile and lower-end hardware. See [Rendering](../planned/rendering.md#global-illumination-clean-room-lumen-style).

### Death test

A GoogleTest case that checks that a `DG_ASSERT` fires for a programmer error by running the statement in a child
process and matching the `Assertion failed` message. Death tests exist only when asserts are enabled. See
[Testing](../tooling/testing.md#write-a-death-test).

### Debug

The unoptimized build configuration, with asserts on and full debug info. CI builds it with warnings as errors. See
[Configuration](#configuration).

### Derived data cache

The planned local (optionally shared) cache of import results, keyed by the hash of the source content, import
settings, importer version and platform. See
[Assets, cooking and packaging](../planned/assets-and-packaging.md#derived-data-cache).

### Desync

A divergence between two simulations that should be identical: at some tick their state hashes differ. `dg
determinism` reports `DESYNC` when Debug and Release disagree; the planned netcode repairs a desynced client with a
resync. See [Determinism model](../architecture/determinism.md#overview).

### Determinism

The property that the same initial state and the same ordered input stream always produce bit-identical sim state, on
every platform, architecture, compiler and configuration. It is a contract between the engine and all sim code. See
[Determinism model](../architecture/determinism.md).

### Determinism label

The CTest label `determinism`, given to every test executable declared with `LABELS determinism`. Run it with
`./dg test -L determinism`. See [Testing](../tooling/testing.md#labels).

### Determinism manifest

The planned set of hashes that peers compare in the session handshake: engine build, sim modules and plugins, the
sim-asset hash of the level, the tick rate and the `InputCommand` schema. Any mismatch rejects the join. See
[Multiplayer networking](../planned/networking.md#session-lifecycle).

### detmath

`dg::detmath`, the engine's own `f32` transcendental functions (`sin`, `cos`, `atan2`, `exp`, `log`, `pow`, ...), built
only from exactly specified operations so they return the same bits everywhere. Sim code must use them instead of
`std::` math. See [Deterministic math](../systems/detmath.md).

### Development

The default build configuration: optimized with `RelWithDebInfo` flags, with debug info and asserts on. See
[Configuration](#configuration).

### dg

The engine's command-line developer tool, a standard-library-only Python script (`tools/dg.py`) that wraps CMake,
CTest, vcpkg and git. Run it as `./dg` on macOS and Linux or `dg` on Windows. See
[The dg developer tool](../tooling/dg-tool.md).

### dg determinism

The `dg` command that builds `dg-player` in Debug and Release, runs each for 1800 headless fast ticks, and fails with
`DESYNC` unless the final state hashes match. It checks configuration independence on one host, not across
architectures. See [The dg developer tool](../tooling/dg-tool.md#determinism).

### dg docs

The `dg` command that builds this manual into `build/docs/`. `--check` validates front matter, sections, links and
source coverage; `--changed-since <ref>` also requires page updates for changed sources. See
[Writing documentation](../contributing/documentation.md#how-to-use).

### dg-editor

The editor app. Today it logs that the UI is not implemented and runs the engine loop; the planned editor is an ImGui
application modeled on Unity. It is not built for Android, iOS or headless builds. See
[Runtime and apps](../systems/runtime-and-apps.md) and [Editor and MCP](../planned/editor-and-mcp.md).

### dg-player

The game runtime app. Today it runs the fixed-tick simulation loop with scripted inputs; its final state hash after 1800
ticks is the sim golden hash. See [Runtime and apps](../systems/runtime-and-apps.md).

### dg-server

The dedicated server app, always headless. Today it runs the same loop as `dg-player`; it will host multiplayer rooms
in `relay` or `verify` mode. Not built for Android or iOS. See [Runtime and apps](../systems/runtime-and-apps.md) and
[Multiplayer networking](../planned/networking.md#the-server).

### dg.d.luau

The planned Luau type-definition file generated from reflection for every script-visible API, used by luau-lsp for
autocompletion and type checking. It is regenerated whenever a script-visible API changes. See
[Luau scripting](../planned/scripting.md#binding-generation).

### dg_add_module

The CMake helper that creates an engine module as static library `dg_<name>` with alias `dg::<name>`. The `SIM`
keyword marks it as a sim target. Its siblings are `dg_add_app` for executables and `dg_add_test` for test
executables. See [Build system](../tooling/build-system.md#target-helpers).

### DG_ASSERT

The assertion macro for programmer errors. It logs `Assertion failed` and aborts in Debug and Development, and is
compiled out (its condition is not evaluated) in Release. `DG_ASSERT_MSG` adds a message; `DG_VERIFY` still evaluates
its condition in Release. See [Core foundation](../systems/core-foundation.md#assertions).

### DG_HEADLESS

The CMake option and compile definition for the headless build profile: no renderer, audio, UI or editor. Sim code must
always build with `DG_HEADLESS=1`. See [Headless build](#headless-build).

### dg_sim_target

The CMake function that applies the strict FP flags to a target and sets its `DG_SIM_TARGET` property, so tooling can
find deterministic code. `dg_add_module(<name> SIM ...)` calls it. See [Sim target](#sim-target).

### dg_strict_fp

The CMake function that adds the strict floating-point flags (`-ffp-contract=off -fno-fast-math`, `/fp:precise`, or
`/clang:-ffp-contract=off` for clang-cl). `dg_configure_target` calls it for every engine target. See
[Strict floating point](#strict-floating-point).

### .dgpak

The planned cooked archive format: zstd-compressed chunks, a table of contents by GUID, and an mmap-friendly layout.
See [Assets, cooking and packaging](../planned/assets-and-packaging.md#cooking-and-dgpak).

### .dgvfx

The planned effect asset type: a YAML-serialized, reflected particle graph IR. See [Particles and VFX](../planned/vfx.md).

### Docs: none

A commit-message trailer, `Docs: none (<reason>)`, that tells the docs check a change needs no manual update. It
applies to every changed source in the checked range, so use it only when nothing in the range needs documentation.
See [Continuous integration](../tooling/ci.md#read-a-failure).

## E

### ECS

Entity-component-system: an architecture in which entities are IDs, components are plain data attached to them, and
systems process all entities with a given set of components. Detergine's planned sim world is an archetype ECS that
holds all sim state. See [ECS](../planned/ecs.md).

### Effect

A planned VFX asset (`*.dgvfx`) holding one or more emitters plus exposed parameters, bounds, LOD and scalability
settings. Each **emitter** is a graph of typed nodes in the stages Spawn, Initialize, Update and Render. See
[Particles and VFX](../planned/vfx.md#data-model-one-ir-two-views).

### ENet

The UDP networking library chosen as the default multiplayer transport (decision 3). GameNetworkingSockets stays an
optional plugin. See [Decisions and roadmap](../architecture/decisions-and-roadmap.md#settled-decisions).

### Entity

An object in the planned ECS, identified by `{ u32 index; u32 generation; }`, the same shape as `dg::Handle`. Sim
entity IDs are allocated sequentially with FIFO reuse, so they are deterministic. See [ECS](../planned/ecs.md#storage).

### Extension point

A named slot in the planned render graph (`PreDepth`, `GBuffer`, `Lighting`, `GI`, `Translucency`, `PostProcess`, `UI`,
`Debug`) where an `IRenderFeature` injects its passes. See
[Rendering](../planned/rendering.md#render-graph-and-extension-points).

## F

### Fast mode

Running the main loop with `--fast`: ticks run back to back with no sleeping and no periodic log line. It gives the same
state hash as real-time mode and is used for tests and determinism checks. See
[Runtime and apps](../systems/runtime-and-apps.md#real-time-and-fast-mode).

### Fast-math

Compiler options (`-ffast-math`, `/fp:fast`) that let the compiler reorder and simplify floating-point expressions.
They make results platform-dependent and are banned in Detergine (determinism rule 3). See
[Strict floating point](#strict-floating-point).

### FIFO reuse

The rule that `HandlePool` reuses freed slots oldest first. It makes handle values depend only on the create and
destroy sequence and maximizes the time before a slot is reused, so stale handles are caught longer. See
[Generational handles](../systems/handles.md#slot-assignment-rules).

### Final state-hash line

The last line every app prints, `[info] final tick=<n> state-hash=0x<hex> elapsed=<s>s`. `dg determinism` and CI parse
it, so its format is a stable interface. See
[Runtime and apps](../systems/runtime-and-apps.md#the-final-state-hash-line).

### Fixed delta time

The constant time step of one tick, `1 / tickRate` seconds (1/30 s by default). Sim code sees only this `dt`, never
wall-clock time. See [Simulation](../systems/simulation.md#reference).

### Fixed point

A number format that stores a value as an integer scaled by a fixed power of two, so arithmetic is integer-exact and
platform-independent. Detergine provides `fixed32` (Q16.16) and `fixed64` (Q32.32) for gameplay values. See
[Fixed-point numbers](../systems/fixed-point.md).

### fixed32

`dg::fixed32`, a Q16.16 fixed-point number (`Fixed<i32, 16>`) with range [-32768, 32768) and resolution 2^-16. See
[Fixed-point numbers](../systems/fixed-point.md).

### fixed64

`dg::fixed64`, a Q32.32 fixed-point number (`Fixed<i64, 32>`) with range [-2^31, 2^31) and resolution 2^-32. Use it
for values that can grow, such as scores and currency. See [Fixed-point numbers](../systems/fixed-point.md).

### FMA contraction

The compiler fusing `a * b + c` into one fused multiply-add instruction with one rounding instead of two. Compilers do
it by default on ARM64 but only with FMA enabled on x64, so it is disabled everywhere with `-ffp-contract=off`. See
[Determinism model](../architecture/determinism.md#strict-floating-point-on-every-target).

### FNV-1a

The 64-bit Fowler-Noll-Vo hash implemented as `dg::Fnv1a64`, used today for sim state hashes and golden-hash tests.
It is a stand-in that XXH3 will replace, which changes every golden hash. See
[Core foundation](../systems/core-foundation.md#hashing-state).

### Forward+

A forward renderer with tile-based light culling. It is the planned mobile rendering path, tuned for tile-based GPUs.
See [Rendering](../planned/rendering.md#mobile-pipeline-forward).

### FTZ/DAZ

Flush-to-zero and denormals-are-zero, CPU modes that replace denormal floats with zero. detmath assumes both are off
(the platform default); setting them explicitly on sim threads is designed but not built. See
[Deterministic math](../systems/detmath.md#assumed-floating-point-environment).

## G

### Game module

An optional, planned static library of native C++ gameplay code (classes, components, systems) added with
`dg_add_game_module`. It is built with the engine's determinism flags and bound to Luau through reflection. See
[Luau scripting](../planned/scripting.md#c-game-modules).

### Generation

The counter stored in a handle that records how many times its slot had been reused when the handle was issued.
Generations start at 1 and grow by one per destroy; generation 0 means invalid. A stale handle's generation no longer
matches its slot. See [Generational handles](../systems/handles.md#slot-assignment-rules).

### Generational slot map

A container that stores objects in slots and issues (index, generation) handles to them, so a lookup through a stale
handle fails instead of reaching a newer object. `dg::HandlePool` is one. See
[Generational handles](../systems/handles.md).

### GI

Global illumination: indirect lighting from light bouncing between surfaces. The planned GI ships in tiers, from baked
lightmaps and DDGI probes to a clean-room, Lumen-style software ray tracer and a hardware RT path. See
[Rendering](../planned/rendering.md#global-illumination-clean-room-lumen-style).

### Golden hash

A hash of a fixed workload's results, recorded once on macOS ARM64 and written into a test as a constant. Every
platform, compiler and configuration must reproduce it exactly; a mismatch on one platform is a determinism bug, never
a reason to re-record. See [Determinism model](../architecture/determinism.md#golden-hashes) and
[Testing](../tooling/testing.md#re-record-a-golden-hash).

### GoogleTest

The C++ test framework (version 1.18.0, from vcpkg) used by every engine test executable. See
[Testing](../tooling/testing.md).

### Graph IR

The planned single intermediate representation of a VFX effect. The module-stack view and the node-graph view in the
Particle Editor are two views of the same IR, so no data may exist that only one view can show. See
[Particles and VFX](../planned/vfx.md#data-model-one-ir-two-views).

### GUID

A globally unique identifier. Every asset gets a stable GUID in its `.meta` sidecar, and serialized data refers to
assets by GUID, never by path. Planned. See
[Assets, cooking and packaging](../planned/assets-and-packaging.md#asset-database).

## H

### Handle

A small value that refers to an object owned by a system, used instead of a pointer across system boundaries.
`dg::Handle<Tag>` is 8 bytes (slot index and generation); a lookup through a stale handle fails cleanly instead of
touching freed memory. See [Generational handles](../systems/handles.md).

### HandlePool

`dg::HandlePool<T, MaxGeneration>`, the generational slot map that owns objects of type `T`, issues handles with
deterministic values, and keeps object addresses stable. It will back the sim entity allocator. See
[Generational handles](../systems/handles.md).

### Headless build

A build with `DG_HEADLESS=ON` that contains no renderer, audio, UI or editor. The `linux-x64-server` preset sets it,
`dg-server` always runs headless, and sim code must always build this way (determinism rule 11). See
[Build system](../tooling/build-system.md#options).

### Headless editor

The planned `dg-editor --headless --mcp` mode, which the MCP bridge starts when no editor is open, rendering offscreen
for screenshots when a GPU exists. See [Editor and MCP](../planned/editor-and-mcp.md#mcp-server-architecture).

### Hierarchical hashing

The planned state-hash scheme in which the world hash is built from per-chunk and per-system-group hashes. Only dirty
chunks are rehashed, and comparing hash trees pinpoints the component type and entity range that diverged. See
[ECS](../planned/ecs.md#snapshots-and-hashing).

### Hot reload

Replacing scripts while the editor or game runs. View and editor scripts reload instantly; sim scripts reload by
rebuilding the sim world from the last snapshot. Planned. See [Luau scripting](../planned/scripting.md#hot-reload).

### HRTF

Head-related transfer function: filtering that makes sound appear to come from a 3D direction over headphones. The
planned audio engine renders HRTF binaural audio through Steam Audio. See [3D audio](../planned/audio.md).

## I

### IAllocator

`dg::IAllocator`, the interface every engine allocator implements: power-of-two alignment, `nullptr` for zero size
and on exhaustion, never throws, and `deallocate` with the same size and alignment as `allocate`. See
[Memory allocators](../systems/memory.md#the-iallocator-contract).

### Inertialization

An animation blending technique that switches to the new pose at once and decays the offset from the old pose over
time, instead of evaluating both poses during a crossfade. Planned for presented animation. See
[Skeletal animation](../planned/animation.md).

### Input authority

The server role, always on, that owns the session clock, collects inputs for each tick until a deadline, substitutes
inputs for late players, and broadcasts the confirmed input set. Planned. See
[Multiplayer networking](../planned/networking.md#roles-and-server-modes).

### InputCommand

`dg::sim::InputCommand`, one player's input for one tick, and the only data that flows into the sim (determinism rule
9). Today it is a placeholder (`moveX`, `moveY`, `buttons`); it will become a reflected schema whose layout is part of
the network protocol. See [Simulation](../systems/simulation.md#feed-inputs).

## J

### Jolt Physics

The MIT-licensed 3D physics library the planned `dg::physics` module integrates in its cross-platform deterministic
configuration (`JPH_CROSS_PLATFORM_DETERMINISTIC`), with state save and restore for rollback. See
[Physics](../planned/physics.md).

## K

### kDefaultAlignment

The allocator default alignment, fixed at 16 bytes on every platform instead of `alignof(std::max_align_t)` (8 or 16
depending on the target), so arena layouts are identical everywhere. See
[Memory allocators](../systems/memory.md#why-kdefaultalignment-is-16).

## L

### Label

A CTest tag used to select tests with `-L`. The labels in use are `unit`, `determinism` and `smoke`. See
[Testing](../tooling/testing.md#labels).

### Late join

A client joining a session that is already running. In the planned netcode it uses the resync path: the client
receives a snapshot of a verified tick and fast-forwards through the confirmed inputs. Reconnect works the same way. See
[Multiplayer networking](../planned/networking.md#desync-detection-and-resync).

### Launcher

The Detergine Launcher, a tkinter window over `dg` (`./dg gui`, `Launcher.command`, `Launcher.bat`). Every button runs
the equivalent `dg` command and shows it, so the GUI never behaves differently from the command line. See
[Detergine Launcher](../tooling/launcher.md).

### Layer

One level of the engine's architecture: Core, Platform/RHI, Engine services, Simulation and Presentation, Gameplay
framework, Editor and Apps. A layer may depend only on layers below it, and Simulation never depends on Presentation.
See [Architecture overview](../architecture/overview.md#layers).

### Level-scoped build

A planned headless cook that includes only the sim content of the levels it lists (`dg-cook --profile headless
--levels ...`) and strips presentation assets. Its output includes a sim-asset manifest hash per level. See
[Assets, cooking and packaging](../planned/assets-and-packaging.md#headless-level-scoped-cooks).

### LIFO recycling

Reusing the most recently freed block first. `dg::PoolAllocator` recycles blocks LIFO, because that block is the most
likely to still be in cache. `HandlePool` slots use FIFO instead. See
[Memory allocators](../systems/memory.md#many-equal-sized-objects-poolallocator).

### Linear allocator

`dg::LinearAllocator`, a bump-pointer arena of fixed capacity, owned or borrowed. Individual frees do nothing; memory is
released after a marker with `rewind` or all at once with `reset`. See
[Memory allocators](../systems/memory.md#scratch-memory-linearallocator).

### Lockstep

A multiplayer model in which peers exchange only inputs and every peer advances the same deterministic simulation in
step. It works only if the sim is bit-identical everywhere, which is why determinism is the engine's core guarantee. See
[Determinism model](../architecture/determinism.md#overview).

### LTO

Link-time optimization (interprocedural optimization, IPO): optimization across translation units at link time. The
Release configuration enables it when the compiler supports it. See
[Build system](../tooling/build-system.md#configurations).

### Luau

Roblox's typed, sandboxable dialect of Lua 5.1, the engine's primary gameplay language (decision 2). A complete game
must be writable in Luau without C++. Planned. See [Luau scripting](../planned/scripting.md).

## M

### Marker

`dg::LinearAllocator::Marker`, a saved arena position. `rewind(marker)` frees everything allocated after it. See
[Memory allocators](../systems/memory.md#scratch-memory-linearallocator).

### Max prediction window

The planned limit on how many ticks a client may run ahead of the confirmed inputs (for example 10). When it is
reached, the client sim stalls instead of diverging further. See
[Multiplayer networking](../planned/networking.md#ticks-and-time).

### MCP

Model Context Protocol: an open JSON-RPC 2.0 protocol through which AI agents call tools and read resources. The
planned editor embeds an MCP server (`dg::mcp`) so agents can edit projects with undo, permissions and an activity log.
See [Editor and MCP](../planned/editor-and-mcp.md#mcp-server-architecture).

### Module

A unit of engine code in `engine/<name>/`, built as static library `dg_<name>` (alias `dg::<name>`) with its public
headers in `include/dg/<name>/`. Every feature lives in a module or plugin behind an interface. See
[Architecture overview](../architecture/overview.md#how-the-build-enforces-layering).

### MSDF

Multi-channel signed distance field: a glyph atlas format that keeps text sharp at any scale. The planned runtime UI
renders text from MSDF atlases. See [Runtime UI](../planned/ui.md).

## N

### Ninja Multi-Config

The CMake generator used by the Ninja presets. One build tree holds Debug, Development and Release. See
[Build system](../tooling/build-system.md#presets).

## O

### ODR hazard

A violation risk of C++'s one-definition rule: the same inline function compiled differently in two translation units,
where the linker keeps only one copy. Detergine applies the strict FP flags to every target so an FMA-contracted copy
can never end up in sim code. See [Determinism model](../architecture/determinism.md#strict-floating-point-on-every-target).

### Overlay triplet

`arm64-osx-dg`, a custom vcpkg triplet in `cmake/triplets/` that builds macOS packages for deployment target 14.0, the
engine's own target. See [Build system](../tooling/build-system.md#the-overlay-triplet).

### ozz-animation

The MIT-licensed skeletal animation library (SoA SIMD sampling, blending, IK) that the planned `dg::anim` presented
animation runtime builds on. See [Skeletal animation](../planned/animation.md).

## P

### Payne-Hanek reduction

An exact range-reduction algorithm for trigonometric functions with huge arguments. detmath uses an integer-only
version with a 128-bit window of 2/π bits for arguments from 2^19 up. See
[Deterministic math](../systems/detmath.md#how-to-edit).

### PCG32

A small, fast permuted congruential random number generator (XSH-RR output). `dg::sim::Rng` is PCG32. See
[Sim RNG](#sim-rng).

### Phase

One step of the ordered sim tick: `PreTick`, `Input`, `ScriptPreUpdate`, `Gameplay`, `Physics` (Pre, Step, Post),
`ScriptPostUpdate`, `Lifecycle`, `PostTick` (checksum). Planned with the ECS. See
[Architecture overview](../architecture/overview.md#the-fixed-tick-loop).

### Placeholder simulation

Today's `dg::sim::Simulation`: 64 bouncing bodies driven by scripted inputs. It is not a gameplay feature; it gives the
build, loop, smoke tests and determinism checks real floating-point work until the ECS sim world replaces it. See
[Simulation](../systems/simulation.md).

### Play mode

The planned editor mode that snapshots the edit world, runs the game, and restores the snapshot on stop, so nothing
changed during play persists by accident. See [Editor and MCP](../planned/editor-and-mcp.md#panels).

### Plugin

A planned unit of optional functionality with a C-ABI entry point (`dgCreatePlugin`), loaded dynamically in the editor
and linked statically in shipping and mobile builds. Plugins that add sim systems must declare `deterministic: true`.
See [Architecture overview](../architecture/overview.md#layers).

### Pool allocator

`dg::PoolAllocator`, an allocator of fixed-size blocks carved from chunks, with an intrusive free list and LIFO
recycling. See [Memory allocators](../systems/memory.md#many-equal-sized-objects-poolallocator).

### Pre-push hook

The git hook installed with `./dg hooks install`. Before each push it runs `dg docs --check` and then
`dg test dev -L unit`; `git push --no-verify` skips it once. See [Continuous integration](../tooling/ci.md#the-pre-push-hook).

### Predict/rollback

The planned multiplayer model: a client predicts remote inputs and simulates ahead, then, when the server's confirmed
inputs arrive, restores an earlier snapshot and resimulates with the real inputs. See
[Multiplayer networking](../planned/networking.md).

### Predicted event

A sim event raised on a predicted tick. Presentation receives it with `predicted=true` and must handle its later
confirmation or cancellation, so a rollback does not duplicate sounds or effects. See
[Architecture overview](../architecture/overview.md#the-two-worlds).

### Predicted tick

A tick a client runs ahead of confirmation using predicted remote inputs. It is rolled back when confirmed inputs
differ. See [Verified tick](#verified-tick) and [Multiplayer networking](../planned/networking.md#ticks-and-time).

### Presentation world

The non-deterministic side of the engine: rendering, audio, VFX, UI, camera and animation poses at a variable frame
rate. It may read sim state and consume sim events but never writes sim state; its only path into the sim is input
commands. See [Architecture overview](../architecture/overview.md#the-two-worlds).

### Preset

A named CMake configuration in `CMakePresets.json`, such as `macos-arm64`, `windows-x64` or `linux-x64-server`.
Configure presets choose the generator, compiler and triplet; build and test presets add `-debug`, `-dev` and
`-release`. `dg` picks one per host. See [Build system](../tooling/build-system.md#presets) and
[The dg developer tool](../tooling/dg-tool.md#how-dg-chooses-a-preset).

### Protocol version

The planned network protocol version number. Any change to an `InputCommand` schema, the hash composition or the
snapshot format changes the protocol, so it must be bumped and mentioned in the commit message. See
[Multiplayer networking](../planned/networking.md#open-risks).

## Q

### Q16.16

A fixed-point format with 16 integer bits and 16 fraction bits in 32 bits; `dg::fixed32`. See
[Fixed-point numbers](../systems/fixed-point.md#overview).

### Q32.32

A fixed-point format with 32 integer bits and 32 fraction bits in 64 bits; `dg::fixed64`. See
[Fixed-point numbers](../systems/fixed-point.md#overview).

### Quat

`dg::Quat`, the engine's unit-quaternion rotation type (`x, y, z, w`), using the Hamilton product and active,
right-handed rotations. See [Vectors, quaternions and matrices](../systems/vector-math.md#quaternions).

## R

### Real-time mode

The default main-loop mode, which sleeps until each tick's deadline so the sim runs at its tick rate. Late ticks are
run back to back to catch up, never skipped. See
[Runtime and apps](../systems/runtime-and-apps.md#real-time-and-fast-mode).

### Reflection

Run-time type information generated by the planned `dg::refl` module and the `dg-reflect` tool from `DG_REFLECT` and
`DG_PROPERTY` annotations. It will drive serialization, state hashing, the inspector, Luau bindings and MCP schemas.
Not built yet (roadmap step 3). See [Decisions and roadmap](../architecture/decisions-and-roadmap.md#implementation-plan).

### Relay mode

The `relay` server mode: the server is the input authority and decides truth by a majority vote of client checksums.
It is cheap and is the default until the headless build is stable. Planned. See
[Multiplayer networking](../planned/networking.md#roles-and-server-modes).

### Render graph

The planned per-frame graph of render passes, each declaring the resources it reads and writes. The graph aliases
transient memory, inserts barriers, assigns queues and culls unused passes. See
[Rendering](../planned/rendering.md#render-graph-and-extension-points).

### Render world

The planned double-buffered copy of render data that the presentation layer extracts each frame, so the render thread
runs one frame behind the game thread without locks. See [Rendering](../planned/rendering.md#render-world).

### Replay

A recording of an initial snapshot plus the confirmed input stream, which any headless build can re-simulate to the
same state hashes. Replay files are designed but not built. See
[Multiplayer networking](../planned/networking.md#session-lifecycle).

### Resync

The planned repair of a desynced client: the server sends a snapshot of a verified tick, the client restores and
checks it, then fast-forwards through the confirmed inputs. `ResyncPolicy` limits how often it may happen. See
[Multiplayer networking](../planned/networking.md#desync-detection-and-resync).

### Retirement

What happens to a `HandlePool` slot destroyed at its maximum generation: it never re-enters the free queue, so a stale
handle can never match a newer object. See [Generational handles](../systems/handles.md#slot-assignment-rules).

### Reversed-Z

A depth mapping in which the near plane maps to 1 and the far plane to 0, which puts floating-point depth precision at
far distances. Every `dg::Mat4` projection uses it. See
[Vectors, quaternions and matrices](../systems/vector-math.md#coordinate-and-projection-conventions).

### RHI

Render hardware interface: the planned thin layer over Vulkan (`dg::rhi`, in `engine/rhi/`). No code outside it may
include `vulkan.h`. See [Rendering](../planned/rendering.md#rhi).

### Rollback

Restoring the sim to an earlier snapshot and resimulating from there, for example when confirmed inputs differ from
predicted ones. Anything that must survive a rollback must be sim state (determinism rule 6). See
[Predict/rollback](#predictrollback).

### Rollback-safe

Presentation behaviour that copes with rollback: effects and sounds triggered by predicted sim events start as
tentative and are killed or faded if the event is cancelled. Planned for VFX and audio. See
[Particles and VFX](../planned/vfx.md#relationship-to-the-simulation).

## S

### Saturate

An overflow rule that clamps a result to the type's `min()` or `max()` instead of wrapping. Fixed-point division,
narrowing conversions, `fromDouble` and literals saturate. See
[Fixed-point numbers](../systems/fixed-point.md#arithmetic-and-overflow).

### Scripted input

`dg::sim::scriptedInput(tick, playerSlot)`, a pure function that produces a repeating pattern of moves and jumps for
each player slot. The apps and tests use it until a real input pipeline exists. See
[Simulation](../systems/simulation.md#feed-inputs).

### Sim code

Code in the sim layer, which must follow the determinism rules: `engine/sim`, the sim paths of `engine/ecs`,
`engine/physics`, the lockstep part of `engine/net`, `*.sim.luau` scripts and any component tagged `Sim`. Today that is
`engine/sim/` and `engine/math/`. See [Determinism model](../architecture/determinism.md#what-counts-as-sim-code).

### Sim event

An event raised by sim code during a tick. Sim events are part of that tick's deterministic state; presentation
consumes them but must handle predicted ones. See [Predicted event](#predicted-event).

### Sim RNG

`dg::sim::Rng`, a PCG32 generator and the only random source allowed in sim code. Its state is part of the sim state,
so it is hashed and snapshotted. See [Simulation](../systems/simulation.md#draw-random-numbers).

### Sim script

A planned `*.sim.luau` script that runs deterministic gameplay logic inside the sim world, under a sandbox that forbids
globals, hash-table iteration, the OS and the GC. **View scripts** (`*.view.luau`) and **editor scripts**
(`*.editor.luau`) are not deterministic. See [Luau scripting](../planned/scripting.md#script-roles).

### Sim target

A CMake target marked as deterministic code by `dg_sim_target`, which sets its `DG_SIM_TARGET` property. Modules
created with `dg_add_module(<name> SIM ...)` are sim targets; `dg::math` and `dg::sim` are the two today. See
[Architecture overview](../architecture/overview.md#how-the-build-enforces-layering).

### Sim world

The deterministic side of the engine that owns all gameplay state. It advances only in fixed ticks and sees only the
tick number, the fixed `dt` and one input command per player slot; its state can be hashed, snapshotted, rolled back
and replayed. See [Architecture overview](../architecture/overview.md#the-two-worlds).

### Sim-relevant asset

A planned asset whose content affects the sim (collision meshes, gameplay data tables, sim scripts). Its content hash
is part of the determinism manifest. See
[Assets, cooking and packaging](../planned/assets-and-packaging.md#sim-relevant-assets).

### Significance manager

A planned budget manager, shared by VFX and animation, that ranks instances by distance, screen size and priority and
lowers the update rate or LOD of the less significant ones. See [Particles and VFX](../planned/vfx.md).

### Slang

The shading language the planned renderer, materials and VFX kernels are written in, compiled to SPIR-V at cook time.
See [Rendering](../planned/rendering.md#materials).

### Smoke test

A CTest test (label `smoke`) that starts an app, runs 120 fast ticks and requires exit code 0: `player.smoke`,
`server.smoke` and `editor.smoke`. See [Testing](../tooling/testing.md#labels).

### Snapshot

A planned serialized copy of all sim state (`Sim` components, physics state, RNG) at one tick. A ring buffer of
snapshots serves rollback; full snapshots serve save games, play mode and resync. See
[ECS](../planned/ecs.md#snapshots-and-hashing).

### Sources

The `sources` front-matter key of a manual page: the repository files and directories that page documents. Every
engine module, app and tool must be covered by some page, and `--changed-since` uses the list to require page updates.
See [Writing documentation](../contributing/documentation.md#page-front-matter).

### State hash

A 64-bit hash of the complete sim state, computed from exact bit patterns of scalar fields and never from raw struct
bytes. Today `Simulation::stateHash()` hashes the tick, the RNG and every body with FNV-1a; the planned hash walks all
`Sim` components and is hierarchical. See [Determinism model](../architecture/determinism.md#state-hashing).

### Status badge

The label under a manual page title: `implemented`, `partial`, `design` or `planned`. It tells you whether the page
describes code that exists. See [Home](../index.md#status-badges).

### Strict floating point

The compile flags that make float results reproducible: no fast-math and no FMA contraction (`-ffp-contract=off
-fno-fast-math`, `/fp:precise`, or `/clang:-ffp-contract=off`). Every engine target gets them in every configuration.
See [Determinism model](../architecture/determinism.md#strict-floating-point-on-every-target).

### Surface cache

The planned GI structure of mesh "cards", captured from a few directions into an atlas of albedo, normal, emissive and
depth, so ray hits can be lit cheaply. See
[Rendering](../planned/rendering.md#global-illumination-clean-room-lumen-style).

## T

### Tick

One fixed step of the simulation, and the tick number that counts them from 0. The sim advances only in whole ticks of
the fixed `dt`; wall-clock time decides only when a tick runs, never what it computes. See
[Runtime and apps](../systems/runtime-and-apps.md#real-time-and-fast-mode).

### Tick rate

The number of ticks per second: 30 Hz by default (decision 1), 1 to 1000 with `--tick-rate`, and later per project in
`ProjectSettings.sim.tickRate`. See [Decisions and roadmap](../architecture/decisions-and-roadmap.md#settled-decisions).

### Ties away from zero

The rounding rule of every fixed-point operation: round to nearest, and when exactly halfway, round to the value
farther from zero (2.5 to 3, -2.5 to -3). See [Fixed-point numbers](../systems/fixed-point.md#overview).

### Time dilation

The planned adjustment of up to ±5% to a client's tick accumulator that corrects clock drift against the server
without skipping ticks. See [Multiplayer networking](../planned/networking.md#ticks-and-time).

### Triplet

A vcpkg target description (architecture, OS, CRT linkage, library type), such as `x64-linux`,
`x64-windows-static-md`, `arm64-android` or the overlay triplet `arm64-osx-dg`. Each configure preset names one. See
[Build system](../tooling/build-system.md#presets).

## U

### UBSan

UndefinedBehaviorSanitizer, a compiler instrumentation that detects undefined behaviour such as signed overflow or
misaligned access. Detergine builds it with `-fno-sanitize-recover=undefined`, so the first error aborts. Enable it with
`-D DG_ENABLE_UBSAN=ON` (not on MSVC). See [Testing](../tooling/testing.md#run-under-sanitizers).

### ulp

Unit in the last place: the gap between a floating-point value and the next representable one. An error of 0.5 ulp
means the result is the nearest representable value; detmath measures at most 0.5 ulp. See
[Deterministic math](../systems/detmath.md#accuracy).

### Unit test

A test that checks one function or type against its contract, including failure paths. Every test registered with
`dg_add_test` gets the `unit` label. See [Testing](../tooling/testing.md#overview).

## V

### vcpkg

Microsoft's C++ package manager, used in manifest mode (`vcpkg.json`) and pinned to release 2026.10.16.
`./dg setup --vcpkg` clones it into `external/vcpkg/`. See [Build system](../tooling/build-system.md#vcpkg).

### Verified tick

A tick for which the server has confirmed every player's input. Its state is final and identical on every peer, and
only verified ticks are checksummed. Planned. See [Multiplayer networking](../planned/networking.md#ticks-and-time).

### Verify mode

The `verify` server mode: the server also runs the authoritative headless sim, which is the ground truth for checksums
and snapshots. It is meant for competitive play and becomes the default once the headless build is stable. Planned. See
[Multiplayer networking](../planned/networking.md#roles-and-server-modes).

### VFX

Visual effects, here the planned `dg::vfx` particle system. It is presentation-only: it never reads input or writes sim
state, it is excluded from state hashes and headless builds, and it seeds its RNG from the sim entity, spawn tick and
event id. See [Particles and VFX](../planned/vfx.md).

### Virtual voice

In the planned audio engine, a sound instance that exists logically but is not mixed. Only the most audible voices by
audibility and priority get a real voice. See [3D audio](../planned/audio.md).

### Visibility buffer

A rendering technique in which the geometry pass writes only a triangle and instance ID per pixel and materials are
evaluated afterwards in deferred passes. It is the planned desktop pipeline (decision 4). See
[Rendering](../planned/rendering.md#desktop-pipeline-visibility-buffer).

## W

### Warnings as errors

The `DG_WARNINGS_AS_ERRORS` option, which sets `COMPILE_WARNING_AS_ERROR` on every engine target. CI turns it on, so
every change must compile without warnings. See [Build system](../tooling/build-system.md#options).

### Wrap

An overflow rule that keeps only the low bits of a result, as two's complement integers do. Fixed-point `+`, `-`, `*`,
unary minus and integer construction wrap. See
[Fixed-point numbers](../systems/fixed-point.md#arithmetic-and-overflow).

## X

### x64

The 64-bit x86 CPU architecture (x86-64, AMD64) of Windows and Linux PCs. With ARM64 it is one of the only two
architectures the engine compiles for, and golden hashes must match on both. See
[Core foundation](../systems/core-foundation.md#overview).

### XXH3

The fast 64-bit hash from the xxHash library that will replace FNV-1a for state hashes (roadmap step 2). The switch
changes every recorded golden hash. See [Core foundation](../systems/core-foundation.md#extending).

## Y

### Yoga

The MIT-licensed flexbox layout library the planned runtime UI uses for `FlexLayout` containers. See
[Runtime UI](../planned/ui.md).
