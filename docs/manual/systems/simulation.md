---
title: Simulation (sim)
section: Systems
order: 70
status: partial
summary: The deterministic fixed-tick simulation module: a placeholder body simulation, the PCG32 sim RNG, input commands and the cross-platform golden state hash, until the ECS sim world replaces it.
sources: engine/sim/, tests/sim/
---

## Overview

`dg::sim` is the deterministic layer of the engine: the **sim world** that owns all gameplay state and advances only in
fixed ticks, from inputs only (see [Determinism model](../architecture/determinism.md) and
`docs/DESIGN.md` §5). Every peer in a multiplayer session, and every replay, runs this layer and must reach
bit-identical state on x64 and ARM64.

Today the module is a **placeholder**. The ECS that will hold real sim state does not exist yet (roadmap step 5,
[ECS](../planned/ecs.md)), so `dg::sim::Simulation` simulates a fixed set of 64 bouncing bodies instead. The bodies are
not a gameplay feature. They exist so that the build, the fixed-tick loop in [the runtime](runtime-and-apps.md), the
smoke tests and the cross-configuration and cross-platform determinism checks have real floating-point work to verify.

What exists now:

| Piece | Header | Status |
|---|---|---|
| `Simulation`, `SimConfig`: fixed-tick step and state hash over placeholder bodies | [Simulation.h](engine/sim/include/dg/sim/Simulation.h) | Placeholder, replaced by the ECS sim world |
| `Rng`: PCG32 (XSH-RR), the only random source allowed in sim code | [Rng.h](engine/sim/include/dg/sim/Rng.h) | Implemented, intended to stay |
| `InputCommand`, `scriptedInput()`: per-player, per-tick input | [InputCommand.h](engine/sim/include/dg/sim/InputCommand.h) | Placeholder layout until reflected input schemas |

What is missing, and planned:

* The ECS sim world (DESIGN.md §7): archetype storage, `Sim`-tagged components, ordered system phases
  (`PreTick → Input → ScriptPreUpdate → Gameplay → Physics → ScriptPostUpdate → Lifecycle → PostTick`), command
  buffers, events, snapshots and restore (roadmap steps 5 and 6).
* Reflected `InputCommand` schemas, replay record and playback, and desync dumps (roadmap step 6).
* A reflection-walked, hierarchical per-chunk state hash on XXH3 (DESIGN.md §5.4 and §15.4). Today's hash is a flat
  FNV-1a over the placeholder state.
* Physics, Luau sim scripts and parallel sim systems (roadmap steps 7 to 9).

The module is built with `dg_add_module(sim SIM ...)` in [CMakeLists.txt](engine/sim/CMakeLists.txt), which makes it a
**sim target**. Every engine target, sim or not, already gets the strict floating-point flags (no fast-math, no FMA
contraction) from `dg_configure_target()` in [DgTargets.cmake](cmake/DgTargets.cmake). The `SIM` keyword calls
`dg_sim_target()`, which additionally sets the `DG_SIM_TARGET` property so tooling and future banned-function lints
can find sim code. It depends only on `dg::core`, so it builds and runs headless.

## How to use

### Step a simulation

Create a `Simulation` from a `SimConfig`, then call `step()` once per tick with one `InputCommand` per player slot.
`stateHash()` returns a 64-bit hash of the complete state. Two runs with the same seed and the same input stream
produce the same hash on every platform and build configuration.

```cpp
#include "dg/sim/Simulation.h"

#include <array>
#include <cstdio>

int main() {
    dg::sim::Simulation simulation({.tickRate = 30, .seed = 0x5eed});
    std::array<dg::sim::InputCommand, 2> inputs{};

    for (dg::u64 t = 0; t < 1800; ++t) {
        for (dg::u32 slot = 0; slot < inputs.size(); ++slot) {
            inputs[slot] = dg::sim::scriptedInput(simulation.tick(), slot);
        }
        simulation.step(inputs);
    }

    std::printf("tick %llu hash 0x%016llx\n", static_cast<unsigned long long>(simulation.tick()),
                static_cast<unsigned long long>(simulation.stateHash()));
}
```

This program prints `tick 1800 hash 0x9f504fe2ce4e5ee2`, which is the golden hash (see [Testing](#testing)). You can
reach the same state from the command line, because the apps use the same defaults:

```bash
./dg run dev player -- --headless --fast --ticks 1800
# [info] final tick=1800 state-hash=0x9f504fe2ce4e5ee2 elapsed=0.002s
```

### Draw random numbers

Sim code gets randomness only from `dg::sim::Rng` (CLAUDE.md determinism rule 5). The generator is part of the sim
state: `Simulation` owns one, seeds it from `SimConfig::seed`, and hashes its state. Never construct an `Rng` from a
time value or from `std::random_device`.

```cpp
#include "dg/sim/Rng.h"

dg::u32 rollDie(dg::sim::Rng& rng) {
    return rng.nextBounded(6) + 1; // 1..6, no modulo bias
}

dg::f32 randomSpread(dg::sim::Rng& rng) {
    return rng.nextUnitFloat() * 2.0f - 1.0f; // [-1, 1)
}
```

`nextUnitFloat()` uses the top 24 bits of a draw, so every value is exactly representable in `f32` and identical on
every platform.

### Feed inputs

An **input command** (`InputCommand`) is the per-player, per-tick input. It is the only data that flows into the sim
(determinism rule 9). Until the input pipeline exists, the apps and tests use `scriptedInput(tick, playerSlot)`, a
pure `constexpr` function that produces a repeating pattern of moves and jumps. Different slots are offset by 17 ticks,
so players do not move in lockstep.

The placeholder `step()` maps player slot `i` onto body `i % bodyCount`: `moveX` and `moveY` add an impulse of 0.75
along x and z, and `buttons::kJump` sets the vertical speed to 6 when the body is on the ground.

## How to edit

The code is small and lives in four files:

| File | Contents |
|---|---|
| [Simulation.h](engine/sim/include/dg/sim/Simulation.h) | `SimConfig`, `Simulation` |
| [Simulation.cpp](engine/sim/src/Simulation.cpp) | Placeholder body physics (gravity, damping, bounces), the RNG kick, `stateHash()` |
| [Rng.h](engine/sim/include/dg/sim/Rng.h) | PCG32, header-only and `constexpr` |
| [InputCommand.h](engine/sim/include/dg/sim/InputCommand.h) | `InputCommand`, `buttons::kJump`, `scriptedInput()` |

### Invariants

Everything in `engine/sim` is sim code and follows the determinism rules in CLAUDE.md. In particular:

* Use only `+ - * /`, `sqrt` and comparisons on floats, or `dg::detmath` (see [Deterministic math](detmath.md)). The
  placeholder physics deliberately uses nothing else.
* Keep all state in members that `stateHash()` covers. Today that is `currentTick`, the RNG state and increment, and
  the six `f32` fields of every body, in body order. A member that changes behaviour but is not hashed hides desyncs.
* Hash values, never raw struct bytes. `Fnv1a64::updateValue()` accepts only scalars, so padding cannot leak in (see
  [Core foundation](core-foundation.md)).
* Do not include anything from presentation modules, the platform layer or the OS. The module must build with
  `DG_HEADLESS=1`.
* `SimConfig::tickRate` and `SimConfig::bodyCount` must be greater than zero. The constructor checks both with
  `DG_ASSERT` (programmer errors, not recoverable failures).

### What changes the golden hash

Any change to the simulated behaviour or to the hash composition changes the state hash after 1800 ticks: new
constants, a different order of operations, an extra RNG draw, a different `scriptedInput()` pattern, a new hashed
field, or a different `SimConfig` default. A change that is not meant to alter behaviour (a refactor, a comment) must
keep the golden hash unchanged. If it moves, the change is not as neutral as it looks.

The planned switch from FNV-1a to XXH3 (roadmap step 2) will change this golden and every other recorded hash.

### Re-recording the golden hash

Re-record only when you changed the sim on purpose:

1. Run the determinism tests: `./dg test dev -L determinism`.
2. `SimDeterminism.StateHashMatchesCrossPlatformGolden` fails and prints the measured value, for example
   `state hash after 1800 ticks is 0x..., expected the cross-platform golden value 0x9f504fe2ce4e5ee2`. The test also
   prints `state-hash after 1800 ticks: 0x...` on every run.
3. Check that Debug and Release agree: `./dg determinism` must report that both are bit-identical.
4. Replace `kGoldenHash` in [SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp) with the new value.
5. Say in the commit message that the sim golden hash was re-recorded and why. Update the golden-hash table in
   `docs/IMPLEMENTATION_PLAN.md` and the value quoted on this page.
6. Push and let CI confirm that every host reproduces the new value. CI runs the check on macOS ARM64, Linux x64 and
   Windows x64. If you
   could not check both x64 and ARM64, say so in the PR.

> **Warning:** Never re-record to make a failure on one platform go away. A golden that differs between platforms is a
> determinism bug: find the operation that diverges instead.

### Moving to the ECS

When the ECS sim world lands (roadmap steps 5 and 6), `Simulation` stops owning bodies and drives the ECS world and
its system phases instead. `Rng` stays and becomes a registered sim resource. `InputCommand` becomes a reflected schema;
changing its layout then changes the network protocol, and the protocol version has to be bumped. Rewrite this page
when that happens and set its status to `implemented`.

## Reference

### `dg::sim::SimConfig`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `tickRate` | `u32` | `30` | Ticks per second. The fixed delta time is `1.0f / tickRate`. Must be greater than 0. |
| `seed` | `u64` | `0x5eed` | Seed for the sim `Rng` |
| `bodyCount` | `u32` | `64` | Number of placeholder bodies. Must be greater than 0. |

### `dg::sim::Simulation`

| Member | Description |
|---|---|
| `explicit Simulation(const SimConfig& config)` | Seeds the RNG and places the bodies at random positions inside the arena |
| `void step(std::span<const InputCommand> inputs)` | Advances exactly one tick. `inputs` holds one command per player slot, applied in slot order |
| `u64 tick() const` | Number of ticks stepped so far (starts at 0) |
| `f32 fixedDeltaTime() const` | `1.0f / tickRate` |
| `u64 stateHash() const` | FNV-1a 64 over the tick, the RNG state and increment, and every body's position and velocity bits |

Placeholder constants in [Simulation.cpp](engine/sim/src/Simulation.cpp): gravity −9.81, arena half extent 10,
restitution 0.6, horizontal damping 0.995 per tick, move impulse 0.75, jump speed 6, and one random velocity kick on a
random body every 30 ticks.

### `dg::sim::Rng`

| Member | Description |
|---|---|
| `constexpr explicit Rng(u64 seed = 0x853c49e6748fea9b, u64 stream = 0xda3e39cb94b95bdb)` | PCG32 seeding: the stream selects one of 2^63 independent sequences |
| `constexpr u32 nextU32()` | Next 32-bit output (XSH-RR) |
| `constexpr u32 nextBounded(u32 bound)` | Uniform in `[0, bound)` by rejection, without modulo bias. `bound` must be greater than 0 |
| `constexpr f32 nextUnitFloat()` | Uniform in `[0, 1)` from 24 random bits |
| `constexpr u64 rawState() const` | Internal state, for hashing and snapshots |
| `constexpr u64 rawIncrement() const` | Stream increment, for hashing and snapshots |

### `dg::sim::InputCommand`

| Field | Type | Meaning |
|---|---|---|
| `moveX` | `i8` | −1, 0 or 1 |
| `moveY` | `i8` | −1, 0 or 1 |
| `buttons` | `u16` | Bit set. `buttons::kJump` is bit 0 |

`constexpr InputCommand scriptedInput(u64 tick, u32 playerSlot)`: with `phase = tick + playerSlot * 17`, it returns
`moveX = (phase / 45) % 3 - 1`, `moveY = (phase / 70) % 3 - 1`, and `kJump` when `phase % 90 == 0`.

## Testing

[SimDeterminismTest.cpp](tests/sim/SimDeterminismTest.cpp) builds the `dg-sim-tests` executable, labelled `unit` and
`determinism`. Each case runs 1800 ticks (one minute at 30 Hz) with seed `0x5eed` and two scripted players.

| Test | Checks |
|---|---|
| `SimDeterminism.SameSeedAndInputsProduceSameStateHash` | Two identical runs give the same hash |
| `SimDeterminism.DifferentSeedProducesDifferentStateHash` | Seed `0x5eee` gives a different hash |
| `SimDeterminism.DifferentInputsProduceDifferentStateHash` | Three players instead of two gives a different hash |
| `SimDeterminism.StateHashMatchesCrossPlatformGolden` | The hash equals the **golden hash** `0x9f504fe2ce4e5ee2`, recorded on macOS ARM64 (AppleClang) |
| `SimulationDeath.ZeroTickRateAsserts` | `tickRate = 0` triggers `DG_ASSERT` (only when asserts are enabled) |
| `SimulationDeath.ZeroBodyCountAsserts` | `bodyCount = 0` triggers `DG_ASSERT` (only when asserts are enabled) |

Run them:

```bash
./dg test dev -L determinism   # all determinism suites, including dg-sim-tests
./dg run dev tests             # build and run dg-sim-tests directly
./dg determinism               # dg-player in Debug and Release must reach the same final hash
```

The golden hash must be reproduced exactly by every platform, architecture and build configuration. CI runs the check
on macOS ARM64, Linux x64 and Windows x64, and also runs `dg determinism`. The golden hash is confirmed on
macOS ARM64, Linux x64 and Windows x64. The app smoke tests (`player.smoke`,
`server.smoke`, `editor.smoke`) step this simulation through the runtime loop; see
[Runtime and apps](runtime-and-apps.md) and [Testing](../tooling/testing.md).
