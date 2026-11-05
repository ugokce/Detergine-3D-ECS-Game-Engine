---
title: ECS
section: Planned systems
order: 20
status: design
summary: The planned archetype entity-component-system (dg::ecs) that will hold all sim state, with deterministic iteration, deferred structural changes, snapshots and hierarchical state hashing.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md, engine/core/include/dg/core/Handle.h, engine/core/include/dg/core/Hash.h, engine/sim/, cmake/DgTargets.cmake
---

## Overview

`dg::ecs` is the planned entity-component-system that every gameplay system runs on. It will hold **all**
simulation state (CLAUDE.md determinism rule 6), so that a snapshot of the ECS world, plus the physics state and the
RNG, is a complete snapshot of the game. That property is what makes rollback, resync, replays, save games and editor
play mode work.

The ECS is engine-owned rather than a third-party library, because it must guarantee stable iteration order,
deterministic entity IDs, snapshots and reflection integration. EnTT and flecs are design references only
([DESIGN.md §2.8](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#28-what-we-build-ourselves-and-why)).

> **Note:** No ECS code exists yet. Until it lands, the sim is the placeholder `dg::sim::Simulation`
> (see [Simulation](../systems/simulation.md)). The full design is
> [DESIGN.md §7](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#7-ecs-dgecs).

## Design

### Storage

* **Archetype storage**: every unique set of component types is an archetype. Its entities live in **chunks** of
  16 KiB, laid out as SoA (one contiguous array per component type), similar to flecs and Unity DOTS.
* **Entities** are `{ u32 index; u32 generation; }`, the same shape as `dg::Handle`. The sim world and the
  presentation world have separate entity spaces.
* **Components** are reflected PODs, plus a few managed types held through handles. Sim components are tagged `Sim`
  through reflection (`DG_REFLECT(Sim)`) and are zero-initialized (determinism rule 8).
* **Tags**, **singletons** (`World::resource<T>()`) and **relationships** (`ChildOf`, prefab instance of) are
  supported.

### Queries and systems

* **Queries** are cached. Iteration order is archetype creation order, then chunk order, then index within the chunk.
  All three are deterministic, so two machines visit entities in the same order.
* **Systems** declare the components they read and write. The scheduler builds a dependency DAG and runs
  non-conflicting systems in parallel. Presentation systems run freely in parallel; sim systems follow the job-system
  rule that parallel work writes disjoint data and reductions happen afterward in a fixed order
  ([DESIGN.md §4.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#44-job-system-dgjobs)).
* Sim systems run in ordered **phases**
  ([DESIGN.md §5.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#53-fixed-tick-scheduler)):

  | Order | Phase |
  |---|---|
  | 1 | `PreTick` |
  | 2 | `Input` |
  | 3 | `ScriptPreUpdate` |
  | 4 | `Gameplay` |
  | 5 | `Physics` (Pre, Step, Post) |
  | 6 | `ScriptPostUpdate` |
  | 7 | `Lifecycle` (spawn and destroy) |
  | 8 | `PostTick` (checksum) |

### Structural changes, events and IDs

* **Command buffers** defer structural changes (adding or removing components, spawning, destroying). They are
  flushed in a deterministic order at phase boundaries, so no system sees a half-changed archetype layout.
* **Events** are typed, per-tick streams. Sim events are part of the deterministic state of that tick.
* **Entity ID allocation** is sequential and deterministic inside the sim world, with FIFO free lists.
  Network-spawned entities are ordered by `(tick, spawner, seq)`
  ([DESIGN.md §5.2](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#52-sources-of-nondeterminism-and-how-each-is-eliminated)).

### Snapshots and hashing

* A **snapshot** serializes and restores all `Sim` components (memcpy-fast per chunk), plus the physics state and
  the RNG. A ring buffer of snapshots serves rollback; full snapshots serve save games and editor play mode.
* The **state hash** walks `Sim` components through reflection in canonical entity and archetype order. It never
  hashes raw bytes or padding. It also covers the entity allocator state, the sim RNG, the physics state hash, the
  tick and registered sim resources.
* **Hierarchical hashing**: the world hash is built from per-archetype-chunk hashes and per-system-group hashes.
  Only chunks that are dirty this tick are rehashed, which keeps per-tick hashing cheap. On a desync, comparing the hash
  trees pinpoints the component type and entity range that diverged
  ([DESIGN.md §15.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#154-state-hashing-desync-detection)).

### Transforms

`LocalTransform`, `WorldTransform` (computed) and `Parent` / `Children` components form the hierarchy. The sim uses
deterministic math types; the presentation layer interpolates between the last two ticks into a float
`RenderTransform`.

## How it will be used

Game developers will mostly use the ECS from Luau ([Luau scripting and C++ game modules](scripting.md)). The planned
style is a system declared with a phase and a query, not a per-object `Update()` callback. This example is the
proposed API from
[DESIGN.md §6.2](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#62-binding-generation)
and does not run today:

```lua
--!strict
-- health.sim.luau (planned API)
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

Components will be declared in C++ with reflection annotations (or from Luau through the binding layer). The
authoring syntax is planned for step 3 and does not compile today:

```cpp
// Planned syntax (DESIGN.md §4.2). DG_REFLECT and DG_PROPERTY do not exist yet.
struct DG_REFLECT(Sim) Health {
    DG_PROPERTY(Serialize, Range(0, 1000), Tooltip("Current HP"))
    dg::fixed32 current = 100;
    DG_PROPERTY(Serialize)
    dg::fixed32 max = 100;
};
```

The rules a game developer must keep:

* Put every piece of gameplay state in a `Sim` component or a registered sim resource. Nothing else is snapshotted.
* Spawn and destroy through the world, never by caching entities in globals; the command buffer orders the changes.
* Iterate with queries, never over a hash map keyed by entity or pointer (determinism rule 4).

## Integration with existing code

* **Entity IDs** will come from `dg::HandlePool` in [Handle.h](engine/core/include/dg/core/Handle.h). Its comments
  already state that it "will back the sim entity allocator": fresh slots are taken in ascending order, freed slots are
  reused FIFO, generations start at 1 and a slot that would overflow its generation is retired. All of this depends
  only on the sequence of create and destroy calls, which is what the ECS needs. See
  [Generational handles](../systems/handles.md).

  ```cpp
  #include "dg/core/Handle.h"

  struct EntityRecord {
      dg::u32 archetype = 0;
      dg::u32 row = 0;
  };

  void fifoReuse() {
      dg::HandlePool<EntityRecord> entities;
      const auto a = entities.create();
      const auto b = entities.create();
      entities.destroy(a);
      entities.destroy(b);
      const auto c = entities.create(); // reuses a's slot first (FIFO), with generation 2
      (void)c;
  }
  ```

* **Component math** uses `dg::fixed32` / `dg::fixed64` for gameplay values and the `dg::Vec3` / `dg::Quat` / `dg::Mat4`
  types for positions, from the math module ([engine/math/](engine/math/)); they are declared directly in namespace
  `dg`, and only detmath has its own namespace (`dg::detmath`) (see [Fixed-point numbers](../systems/fixed-point.md) and
  [Vectors, quaternions and matrices](../systems/vector-math.md)).
* **The RNG** `dg::sim::Rng` ([Rng.h](engine/sim/include/dg/sim/Rng.h)) will become a sim resource. It already exposes
  `rawState()` and `rawIncrement()` for hashing.
* **The sim loop**: `dg::sim::Simulation` ([Simulation.h](engine/sim/include/dg/sim/Simulation.h)) is a placeholder
  with a fixed set of bodies. Step 6 replaces it with a sim world on the ECS; the `step(inputs)`, `tick()` and
  `stateHash()` shape is the contract the runtime loop in
  [Application.cpp](engine/runtime/src/Application.cpp) and `dg determinism` rely on.
* **Hashing**: the state hash uses `dg::Fnv1a64` today ([Hash.h](engine/core/include/dg/core/Hash.h)), which hashes
  scalars only so padding can never leak in. The ECS hash will use XXH3 through reflection.
* **Build**: the module will be `engine/ecs`, declared with `dg_add_module(ecs SIM ...)` so that it gets the
  sim-target property ([DgTargets.cmake](cmake/DgTargets.cmake)).
* **When you change this design or start the module**: read DESIGN.md §7 first. The ECS is sim code, so every
  determinism rule applies, and it must build with `DG_HEADLESS=1`. Changes to iteration order, the snapshot format or
  the hash composition change replay results and the network protocol: re-record the golden hash in
  [SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp), bump the protocol version once the netcode exists, and
  run the determinism replay tests on x64 and ARM64. Update this page and DESIGN.md in the same change.

## Roadmap

From [docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md):

| Step | Scope | Exit criteria |
|---|---|---|
| 3 | Reflection: `dg::refl` runtime and the `dg-reflect` generator MVP | Reflected structs round-trip through a field walker |
| 4 | Serialization: YAML and binary archives, schema versioning | Round-trip tests, stable YAML output |
| 5 | **ECS**: archetype storage, queries, systems and scheduler, command buffers, events, snapshot and restore, hierarchical state hash | Snapshot, restore, identical hash; a benchmark baseline |
| 6 | Sim world: project settings (30 Hz default), input commands, sim world on the ECS, replay record and playback, desync dump | Replays reproduce hashes across configs and CI hosts |
| 7 | Job system: deterministic parallel sim systems | Parallel and serial runs produce identical hashes |

Every new sim system needs a replay-based determinism test (CLAUDE.md, *Build & test*).

## Open risks

* **Determinism of parallel scheduling.** Parallel sim systems must write disjoint data and reduce in entity or chunk
  order. A scheduler bug would show up as a hash mismatch between serial and parallel runs, which is why step 7's exit
  criterion compares them.
* **Snapshot cost versus hashing cost.** Memcpy-fast chunk snapshots and dirty-chunk hashing are both needed to make
  rollback cheap; the benchmark baseline in step 5 is the first measurement.
* **Golden hash churn.** Moving the sim onto the ECS and switching to XXH3 changes the golden hash recorded in
  [SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp). It must be re-recorded deliberately and noted in the commit message.
* **Reflection dependency.** Hashing, snapshots, the inspector and bindings all depend on `dg::refl`, which depends on
  the `dg-reflect` generator. A delay in step 3 delays the ECS.
