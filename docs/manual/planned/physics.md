---
title: Physics
section: Planned systems
order: 40
status: design
summary: The planned dg::physics module that integrates Jolt Physics (3D) and Box2D v3 (2D) in cross-platform deterministic configuration, as part of the sim state.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md, engine/sim/, cmake/DgTargets.cmake, cmake/triplets/, vcpkg.json, tests/sim/SimDeterminismTest.cpp
---

## Overview

`dg::physics` is the planned module that runs rigid-body physics inside the deterministic sim. It integrates two
permissively licensed libraries rather than a physics engine of its own
([DESIGN.md §2.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#23-physics)):

| Concern | Library | License | Why |
|---|---|---|---|
| 3D physics | **Jolt Physics** | MIT | Has a cross-platform deterministic build (`JPH_CROSS_PLATFORM_DETERMINISTIC`, about 8% slower, disables FMA) and `SaveState` / `RestoreState` for rollback |
| 2D physics | **Box2D v3.x** | MIT | v3 is designed for cross-platform determinism; it is verified with the engine's harness for each release |
| Character controller | Jolt `CharacterVirtual` | MIT | Kinematic character movement on top of Jolt |

Physics is sim code: it sits in the simulation layer, follows every determinism rule, and its state is part of every
snapshot and state hash.

> **Note:** No physics code exists yet, and neither Jolt nor Box2D is in [vcpkg.json](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/vcpkg.json). The bodies in
> today's placeholder `dg::sim::Simulation` (gravity, damping and bouncing inside a box) are a stand-in so the
> determinism tests have something to run; they are not the physics module.

## Design

### Determinism

* Every engine target gets strict floating point from `dg_strict_fp()` in [DgTargets.cmake](cmake/DgTargets.cmake),
  which `dg_configure_target()` calls: `-ffp-contract=off -fno-fast-math` on clang and AppleClang,
  `/clang:-ffp-contract=off` on clang-cl, and `/fp:precise` on MSVC (cl.exe). clang-cl deliberately does not get
  `/fp:precise`, because it then warns that `-ffp-contract=off` overrides it, which is an error under warnings-as-errors.
  The design also requires Jolt to be built with `JPH_CROSS_PLATFORM_DETERMINISTIC` and FTZ/DAZ state to be set
  explicitly on sim threads
  ([DESIGN.md §5.2](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#52-sources-of-nondeterminism-and-how-each-is-eliminated)).
  The flag list in DESIGN.md §5.2 itself is out of date on clang-cl; the build file is the authority.
* Physics uses **deterministic float**, not fixed point. Gameplay values (HP, timers, currency) may use
  `dg::fixed32` / `dg::fixed64`; vectors and physics do not.
* Transcendental functions come from `dg::detmath` or from Jolt's own implementations, never from `std::`.
* Physics islands are solved in a deterministic job order. Parallel work writes disjoint data only
  ([DESIGN.md §4.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#44-job-system-dgjobs)).

### Place in the tick

Physics runs as the `Physics` phase of the fixed-tick scheduler, split into Pre, Step and Post, between `Gameplay` and
`ScriptPostUpdate`
([DESIGN.md §5.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#53-fixed-tick-scheduler)).
It steps with the sim's fixed `dt`, never with frame time.

### State, snapshots and hashing

* An ECS snapshot includes the physics state, recorded through Jolt's `JPH::StateRecorder`
  ([DESIGN.md §7](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#7-ecs-dgecs)).
  Rollback restores it with the components and the RNG.
* The per-tick world hash includes a **physics state hash** for Jolt and Box2D
  ([DESIGN.md §15.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#154-state-hashing-desync-detection)).
* Collision meshes are **sim-relevant assets**. Their content hashes are part of the session's determinism manifest,
  so peers with different collision data are rejected at the handshake
  ([DESIGN.md §11](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#11-asset-pipeline)).

### Sim physics and presentation physics

* The **sim physics world** holds everything gameplay depends on.
* **Cosmetic** physics, such as ragdolls and partial-ragdoll hit reactions, runs in a separate *presentation* Jolt
  world that is never hashed. Gameplay-relevant ragdolls (rare) must be sim entities in the sim world
  ([DESIGN.md §10](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#10-skeletal-animation-system-dganim)).
* Presented animation may query the sim physics world (for foot placement) during the presentation phase, which runs
  after the sim step and never concurrently with it. Audio occlusion uses its own acoustic proxy BVH built at
  cook time and never queries the sim physics world.

### Related designs

* Terrain provides a deterministic collision heightfield (Jolt `HeightFieldShape`)
  ([DESIGN.md §14.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#141-level-design--world-building-tools)).
* Headless builds link physics; it is part of the sim
  ([DESIGN.md §16.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#161-headless-build-mode-per-level)).

## How it will be used

DESIGN.md does not define the gameplay-facing physics API yet. What it fixes is the model a game developer works in:

* Physics bodies, shapes and character controllers are sim components on sim entities, created and destroyed through
  the ECS like any other component ([ECS](ecs.md)).
* Gameplay reacts to physics in `.sim.luau` systems that run after the `Physics` phase
  ([Luau scripting and C++ game modules](scripting.md)), and moves characters from player input read with
  `world:input(slot)`.
* Visual-only effects that need physics (debris, ragdolls) belong in the presentation world, so they cost nothing on
  rollback and are not in headless builds.
* Collision assets are flagged sim-relevant, cooked into the headless server pak, and hashed into the determinism
  manifest ([Assets, cooking and packaging](assets-and-packaging.md)).

## Integration with existing code

* **Build**: the module will be `engine/physics`, declared with `dg_add_module(physics SIM ...)` in
  [DgTargets.cmake](cmake/DgTargets.cmake) and added to [engine/CMakeLists.txt](engine/CMakeLists.txt) after the ECS.
  Both libraries are already listed in DESIGN.md §2.3, as CLAUDE.md requires for new dependencies.
* **Deterministic flags for Jolt**: the engine's FP flags apply only to engine targets, not to vcpkg ports. The stock
  `joltphysics` port pinned in the repository (5.6.0, port version 1) hard-codes `-DCROSS_PLATFORM_DETERMINISTIC=OFF`
  and has no feature that turns it on (its features are `debugrenderer`, `profiler` and `rtti`), so a plain
  [vcpkg.json](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/vcpkg.json) entry would give a
  non-deterministic Jolt. The integration needs an overlay port (alongside the overlay triplet in
  [cmake/triplets/](cmake/triplets/)) or a vendored copy in `third_party/` that sets `CROSS_PLATFORM_DETERMINISTIC=ON`.
  The `JPH_CROSS_PLATFORM_DETERMINISTIC` define must also reach every engine translation unit that includes Jolt
  headers, or the headers and the library disagree. Choosing between an overlay port and a vendored copy is a
  dependency decision that needs an ADR.
* **Math**: engine code that converts between Jolt types and `dg::Vec3` / `dg::Quat`
  ([engine/math/](engine/math/)) can only use exact operations or `dg::detmath`.
* **Replacing the placeholder**: when physics lands, the toy bodies in
  [Simulation.cpp](engine/sim/src/Simulation.cpp) go away. Today's `dg::sim::Simulation` advances like this:

  ```cpp
  #include "dg/sim/Simulation.h"

  #include <array>

  dg::u64 runTicks(dg::u64 ticks) {
      dg::sim::Simulation simulation({.tickRate = 30, .seed = 0x5eed});
      std::array<dg::sim::InputCommand, 2> inputs{};
      for (dg::u64 t = 0; t < ticks; ++t) {
          for (dg::u32 slot = 0; slot < inputs.size(); ++slot) {
              inputs[slot] = dg::sim::scriptedInput(simulation.tick(), slot);
          }
          simulation.step(inputs);
      }
      return simulation.stateHash();
  }
  ```

  The `step` / `stateHash` contract stays; the physics state joins the hash, which re-records the golden value in
  [SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp).
* **When you change this design or start the module**: read DESIGN.md §2.3, §5 and §15.4 first. Keep the module in
  the sim layer with no presentation dependency, so it builds with `DG_HEADLESS=1`. Any change to what goes into the
  physics state or its hash changes the snapshot and hash composition, which changes the network protocol: bump the
  protocol version, re-record the golden hash, and run the determinism replay tests on x64 and ARM64. Update this page
  and DESIGN.md in the same change.

## Roadmap

Step 9 in
[docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md):
Jolt (cross-platform deterministic) and Box2D v3 integration, plus physics snapshot and restore. The exit criterion is
that physics replays are identical across hosts. It needs the ECS (step 5), the sim world with replays (step 6) and the
job system (step 7). In
[DESIGN.md §17](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#17-roadmap-phased)
this is phase 3 (scripting and physics). Terrain heightfields and ragdolls come with world building and animation.

## Open risks

* **Rollback cost with heavy physics.** Resimulating several ticks of a large physics scene is expensive. Mitigations:
  a configurable rollback window, a lockstep mode, physics LOD for sim entities, and early profiling on Android
  ([DESIGN.md §18](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#18-key-risks--open-questions)).
* **Box2D determinism per release.** Box2D v3 is designed for cross-platform determinism but is not assumed to be;
  every version bump has to pass the engine's cross-host replay tests.
* **Library build flags.** A Jolt or Box2D build that differs between hosts (FMA contraction, missing determinism
  define) breaks bit-identical results without any compile error. The replay tests on x64 and ARM64 are the only
  reliable check.
* **Hashing cost.** Hashing the full state every tick may be too slow for shipping; DESIGN.md §15.4 allows hashing
  every N ticks there (for example every 4).
