---
title: Skeletal animation
section: Planned systems
order: 70
status: design
summary: The planned dg::anim system: deterministic sim animation from baked tracks, a presented ozz-animation pose pipeline, an animation graph, and GPU skinning.
sources: docs/DESIGN.md, engine/math/include/dg/math/Quat.h, engine/math/include/dg/math/Fixed.h
---

## Overview

> **Important:** `dg::anim` is not implemented. There is no `engine/anim/` module and no ozz-animation dependency yet.
> This page summarizes [DESIGN.md §10](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#10-skeletal-animation-system-dganim)
> and the Animation Viewer in §14.4.

`dg::anim` is a data-oriented skeletal animation runtime built on **ozz-animation** (SoA SIMD sampling, blending, IK and
local-to-model jobs). On top of ozz the engine adds its own animation graph, deterministic sim-side animation data, GPU
skinning, and LOD and budgets for large numbers of characters.

The defining idea is a split into two halves, because gameplay often depends on animation (attack windows, hitbox
positions, root motion, "can cancel after frame 12") but full pose evaluation is too expensive to roll back.

## Design

### Sim animation and presented animation

| | **Sim animation** (deterministic) | **Presented animation** (presentation layer) |
|---|---|---|
| Runs | Every sim tick, inside the sim world; rolled back and re-simulated | Every rendered frame, after the sim tick |
| State | `AnimSimState` component: graph state IDs, state time in integer **ticks**, the transition in progress. Hashed and snapshotted. | Pose buffers, blend weights, IK results, inertialization offsets. Never hashed. |
| Data | **Baked sim tracks**: root motion and selected gameplay bones (hitbox sockets, weapon tip) at the sim tick rate, in fixed point, plus tick-exact **sim events** | Compressed ozz clips at full rate, curves, morph targets, IK |
| Cost | Table lookups only, no blending math; cheap to re-simulate 8+ ticks | SIMD jobs on the job system, with LOD and budgets |
| Output | Root motion → movement, socket transforms → hit queries, events → gameplay | Skinning matrices for the GPU, cosmetic notifies (footsteps, VFX) |

* The presented graph **follows** the sim graph: same state IDs and times, plus cosmetic blends, IK and layers. When a
  rollback or resync changes the sim state, the presented pose catches up with **inertialization** instead of popping.
* `AnimSimState` is optional. Games with no animation-driven gameplay skip the sim half.
* Headless builds contain only the sim half: baked tracks and sim events, no ozz clips or skinning.

### Assets

| Asset | Contents |
|---|---|
| **Skeleton** (`.dgskel`) | Joint hierarchy, bind pose, joint names and stable IDs, optional humanoid rig definition for retargeting, per-joint LOD levels |
| **AnimationClip** (`.dgclip`) | ozz compressed tracks, float curves, morph-target weights, additive flag and reference pose, events and notifies, baked sim tracks |
| **AnimGraph** (`.dganimgraph`, YAML) | State machines, blend trees, layers, parameters, and the sim/presentation split per node |
| **BlendSpace** (1D/2D) | Clips placed on parameter axes (for example speed × direction), triangulated 2D interpolation |
| **AvatarMask** | Per-joint weight mask for layers |
| **RetargetProfile** | Source-to-target rig mapping with per-chain scale and translation modes |

Import uses **ufbx** (FBX) and **fastgltf** (glTF/GLB) and produces skeletons, skinned meshes (up to 8 influences,
normalized and quantized), clips and morph targets, with unit and axis conversion at import. Compression uses ozz
keyframe reduction with per-joint tolerances derived from chain length; the codec sits behind `IAnimCodec`, so ACL can
replace it. Retargeting is baked at import or done live for previews. Large clip sets stream by asset handle within a
per-tier memory budget.

### Per-frame pipeline

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

* One lock-free job chain per character. Character order does not matter because this side is not deterministic.
* Pose buffers come from per-frame linear allocators; clips and skeletons are shared and immutable.
* Procedural layer: two-bone IK, FABRIK/CCD chains, aim and look-at with limits, and foot placement. Foot-placement
  raycasts are **read-only** queries against the sim physics world, made after the sim step and never concurrently.
* Root motion: the sim uses the baked track; the presented pose strips root translation and rotation to avoid moving
  twice.
* Cosmetic ragdolls use a separate presentation Jolt world. Gameplay-relevant ragdolls must be sim entities.

### Animation graph

* Nested **state machines** with transitions on parameters, sim events, end of state and sync markers.
* Blend nodes: 1D/2D blend spaces, additive, masked layers, select by parameter or enum.
* **Sync groups and markers** keep walk, run and sprint cycles phase-aligned.
* **Inertialization** evaluates only the target pose and decays the offset from the source pose. It is cheaper than a
  crossfade and is what hides rollback corrections.
* Sim-side transitions may read only sim parameters and sim events. The graph compiler enforces this.
* The graph compiles at cook time into flat node arrays with no per-frame virtual dispatch. The sim portion compiles into
  a separate deterministic table that headless builds can load.
* **Montages and slots** play one-shot actions, and can be triggered from sim events so they stay rollback-consistent.

### GPU skinning, LOD and events

* Desktop: **compute pre-skinning** into a skinned vertex cache read by depth, G-buffer, shadow and motion-vector passes.
  Linear blend skinning by default, optional dual-quaternion skinning per mesh, sparse morph-target deltas in the same
  pass.
* Mobile: vertex-shader skinning with 4 influences and half-precision matrices.
* Crowds: vertex animation textures for distant characters, switching to full skeletal near the camera.
* **Update-rate LOD** (every frame near, every 2–4 frames mid-distance, crowd representation far), **joint LOD**, and a
  **significance manager** shared with VFX under a per-tier CPU budget.
* **Sim events** (`HitWindowBegin/End`, `CanCancel`, `SpawnProjectile`) are raised inside the sim and are
  rollback-safe by construction. **Presentation notifies** (footsteps, foley, VFX bursts, camera shakes) fire from the
  presented timeline and follow the predicted-event rules when they come from sim events.
* Planned ECS components: `Skeleton`, `SkinnedMesh`, `Animator`, `AnimSimState` (sim), `IKTarget`, `Ragdoll`, `Socket`.

### Tools

The **Animation Viewer** (DESIGN.md §14.4) opens on a skeleton, mesh, clip, blend space or graph, and can run standalone
as `dg-editor --anim-viewer <asset>` (planned flag; the current `dg-editor` rejects unknown arguments). It steps by frame
and by sim tick, overlays baked sim tracks on the presented pose, and includes a compression inspector, skinning debug
views, IK and retarget previews, and LOD previews. Related tools: the Anim Graph Editor, a Live Graph Debugger that
shows rollback corrections, and a Rig/Avatar editor. See [Editor, world-building tools and MCP server](editor-and-mcp.md).

## How it will be used

Gameplay reads sim outputs (root motion, sockets, sim events) inside sim systems. Cosmetic logic lives in view scripts.
The design's Luau example, which does not run yet:

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

Plugins extend the system with graph nodes (`IAnimNode`, with sim and presentation implementations) and viewer panels
(`IAnimViewerPanel`). See [Luau scripting and C++ game modules](scripting.md).

## Integration with existing code

* **Presented math**: `dg::Quat` in [Quat.h](engine/math/include/dg/math/Quat.h) provides `nlerp`, `slerp`, `rotate`,
  `toMat3` and `toMat4`, and `dg::Mat4::trs` builds joint transforms. These are f32 and suitable for the presentation
  side (see [Vectors, quaternions and matrices](../systems/vector-math.md)).
* **Sim data**: baked sim tracks are specified in fixed point. `dg::fixed32` (Q16.16) and `dg::fixed64` (Q32.32) in
  [Fixed.h](engine/math/include/dg/math/Fixed.h) are the candidates (see [Fixed-point numbers](../systems/fixed-point.md)).
  Any sim-side angle math must use `dg::detmath`, never `std::sin` and friends (see
  [Deterministic math (detmath)](../systems/detmath.md)).
* **Tick time**: `AnimSimState` stores time in ticks. The current `dg::sim::Simulation` exposes `tick()` and
  `fixedDeltaTime()`, and the tick rate defaults to 30 Hz (`SimConfig::tickRate`). See
  [Simulation (sim)](../systems/simulation.md).
* **Frame scratch**: pose buffers are meant to come from `dg::LinearAllocator` (see [Memory allocators](../systems/memory.md)).
* `AnimSimState` depends on the ECS and on snapshot/restore, which do not exist yet (see [ECS](ecs.md)).

## Roadmap

Animation is in step 15+ (content systems) of the
[implementation plan](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md),
DESIGN.md phase **6c**: skeleton and clip import, ozz runtime jobs, compute and mobile skinning, the anim graph
(state machines, blend spaces, layers, inertialization), sim anim state with baked tracks and sim events, IK and foot
placement, update-rate and joint LOD, and the Animation Viewer; then the graph editor and debugger, retargeting, morph
targets, ragdolls and crowd VAT. It needs the ECS (step 5), sim world (step 6), job system (step 7), physics (step 9)
and renderer (steps 10–11) first.

## Open risks

| Risk | Planned mitigation |
|---|---|
| Sim and presented animation disagree (hitbox from the baked track vs. what the player sees) | Same source clip and sample times; the viewer overlays sim tracks on the pose; tolerance tests in CI; inertialization hides corrections |
| Sim events authored between ticks | Event validation snaps them to the nearest tick at cook time and flags them |
| Animation CPU cost with many characters | Update-rate and joint LOD, crowd VAT, significance manager with per-tier budgets |
