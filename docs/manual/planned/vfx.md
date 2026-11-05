---
title: Particles and VFX
section: Planned systems
order: 90
status: design
summary: The planned dg::vfx system: one graph IR per effect, compiled to fused GPU compute kernels and SIMD CPU bytecode, presentation-only and rollback-aware.
sources: docs/DESIGN.md, engine/core/include/dg/core/Hash.h
---

## Overview

> **Important:** `dg::vfx` is not implemented. There is no `engine/vfx/` module, no particle editor, and no `*.dgvfx`
> asset type yet. This page summarizes [DESIGN.md §8.6](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#86-particle-system--vfx-dgvfx)
> and the Particle Editor in §14.2.

`dg::vfx` is a GPU-driven particle system in the class of Niagara and Unity VFX Graph, with a SIMD CPU path for mobile
and for effects that need CPU-side results. Effects are serialized assets authored in the built-in Particle Editor.

Three rules from CLAUDE.md shape everything below:

* VFX is **presentation-only**. It never reads input or writes sim state, it is excluded from state hashes and headless
  builds, and it seeds its RNG from `hash(sim entity, spawn tick, event id)`.
* The module-stack view and the node-graph view edit **one IR**. Never add data that only one view can represent.
* Hot paths stay GPU-driven: fused kernels per stage, no per-frame CPU readback, no per-particle CPU work on the GPU
  path. No shader compiler ships in the player.

## Design

### Data model: one IR, two views

* An **Effect** asset (`*.dgvfx`, YAML) holds one or more **Emitters**, plus exposed parameters, bounds, LOD and
  scalability settings.
* Each emitter is a **graph** of typed nodes in stages: `Spawn → Initialize → Update → Render`, plus `Events` (on death,
  on collision, on timer) that drive sub-emitters.
* The editor shows the same **graph IR** as a **module stack** (Emission, Shape, Velocity over Life, Color over Life,
  Noise, Collision, …) and as a **node graph**. Modules are saved subgraphs, so users and plugins can author new ones.
* **Particle attributes** (position, velocity, age, lifetime, color, size, rotation, sprite index, custom) are declared
  by the nodes that use them. The compiler prunes unused attributes and chooses `f32`, `f16` or `unorm8` storage per
  attribute.

### Compilation (cook time)

| Target | Output | Runs on |
|---|---|---|
| GPU | Slang compute kernels, **one fused kernel per stage per emitter** (no per-module dispatches), after constant folding and dead-attribute elimination | Async compute where available |
| CPU | Compact **SIMD bytecode** (SoA, 4/8-wide) | Job-system workers |

The cooker emits both targets where the platform tier allows, and the runtime picks per emitter by tier and budget.
Shader variants are precompiled. Effects created or changed at runtime are limited to parameter, curve and module
combinations of precompiled module kernels (the "uber-module" path), or they run on the CPU bytecode path, which
compiles on any device.

### GPU runtime

* Per-emitter SoA attribute buffers in a shared, pooled, bindless heap, with **dead-list / alive-list compaction**, so
  there are no CPU readbacks.
* Frame order: spawn → initialize → update (fused) → compaction → optional sort → `DrawIndirect`.
* Sorting with FidelityFX Parallel Sort (MIT) only for emitters that need back-to-front order; others draw unsorted
  (additive) or with weighted-blended OIT.
* Forces and collision: gravity, drag, vortex, attractors, curl noise, 3D vector fields, depth-buffer collision, and SDF
  collision against the global SDF built for GI (see [Rendering and global illumination](rendering.md)).
* Sub-emitters consume GPU append buffers (death or collision positions) in the same frame. Async readback N frames later
  is optional, for cosmetic audio triggers.
* Renderers: billboards with flipbook atlases and motion vectors, ribbons and trails, mesh particles, decals, light
  particles (capped, fed into clustered lighting), soft particles, distortion.
* Lit particles sample clustered lights and the GI radiance cache; heavy translucency renders at half resolution.

### Performance and scalability

* The cooker generates **trimmed sprite geometry** (4–8 vertex polygons per flipbook frame) to cut overdraw.
* Fixed or precomputed bounds per effect for culling, plus per-particle GPU frustum culling.
* A **significance and budget manager** (shared with animation) ranks effects by distance, screen size and priority under
  a per-tier particle and GPU-time budget; low-significance effects drop spawn rate or LOD or are culled.
* Pooled effect instances; prewarm is simulated at load time, not on spawn.
* Emitters sharing a material and renderer type draw with one indirect draw.
* Mobile tier: CPU SIMD path or simplified kernels, `f16` attributes, no GPU sort, low-resolution translucency.
* Every emitter reports particle count, memory and GPU timings to the profiler and the editor.

### Relationship to the simulation

* Visual consistency across clients comes from the deterministic seed. Results are best effort, because GPU float
  results are not bit-guaranteed.
* **Rollback-safe**: effects triggered by predicted sim events spawn as tentative. If a rollback cancels the event, the
  effect is killed or faded; if confirmed, it continues.
* Gameplay-relevant "particles" (bullets, debris that matters) must be sim entities, not VFX.

## How it will be used

* Put a `VfxEffect` presentation component on an entity (asset GUID, exposed-parameter overrides, autoplay,
  attach-to-socket), or put it on a prefab and override parameters through the normal prefab override path.
* Play effects from view Luau or C++. The design's call shape is
  `dg.vfx.play(effect, { position = ..., params = { Color = ... } })`. Exposed parameters can be bound to reflected
  component fields.
* Author effects in the **Particle Editor** (opens on double-click of a `*.dgvfx` asset): emitter list with cost badges,
  module stack and node graph over the same IR, curve and gradient editors, an isolated preview viewport, a timeline that
  scrubs by deterministic re-simulation from a fixed seed, debug overlays (bounds, overdraw heatmap, attribute
  inspector), a per-tier profiler strip, a scalability tab, a flipbook tool, and templates. Edits recompile only the
  affected emitter and hot-swap into play mode.
* Extend it with plugins: modules implement `IVfxModule` with **both** a Slang snippet and a CPU-bytecode
  implementation (or declare the mobile tier unsupported); renderers implement `IVfxRenderer`.

All names in this section are planned; none exist in the code yet.

## Integration with existing code

* **Seeding**: the seed formula `hash(sim entity, spawn tick, event id)` can be expressed with today's `dg::Fnv1a64`
  in [Hash.h](engine/core/include/dg/core/Hash.h). FNV-1a is a stand-in until xxHash replaces it (implementation plan
  step 2), so treat the exact values as unstable. There is no ECS entity type yet, so the entity is passed as raw bits
  here.

```cpp
#include "dg/core/Hash.h"

// Presentation-side seed for one effect or sound instance: hash(sim entity, spawn tick, event id).
dg::u64 presentationSeed(dg::u64 entityBits, dg::u64 spawnTick, dg::u32 eventId) {
    dg::Fnv1a64 hash;
    hash.updateValue(entityBits);
    hash.updateValue(spawnTick);
    hash.updateValue(eventId);
    return hash.digest();
}
```

* **Never use `dg::sim::Rng` in VFX**. Its state is sim state and is hashed; drawing from it in presentation code would
  change the sim.
* **Headless**: `DG_HEADLESS` / `dg::build::kHeadless` exist; the VFX module must not be linked into headless builds.
* VFX depends on the RHI, render graph and bindless heap (see [Rendering and global illumination](rendering.md)), the
  job system for the CPU path, and the asset pipeline for cooking (see
  [Assets, cooking and packaging](assets-and-packaging.md)). None of these exist yet.

## Roadmap

VFX is in step 15+ of the
[implementation plan](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md),
DESIGN.md phase **6b**: graph IR and YAML serialization, the module-stack editor, the CPU SIMD runtime, then the GPU
fused-kernel runtime, sprites, ribbons and meshes, sorting, depth and SDF collision, the node-graph view, flipbooks,
budgets and LOD, and profiler integration.

## Open risks

| Risk | Planned mitigation |
|---|---|
| GPU particle cost on mobile/TBDR GPUs (overdraw, bandwidth) | CPU SIMD path, trimmed sprite geometry, per-tier budgets, low-resolution translucency, Android profiling from the start of phase 6b |
| The two editor views drifting apart | Both views edit one IR; rule in CLAUDE.md forbids view-only data |
| Effects duplicated after rollback | Tentative spawning for predicted events, cancel or fade on rollback |
