---
title: Determinism model
section: Architecture
order: 20
status: partial
summary: The determinism contract, every rule and the reason behind it, how the build and the math libraries enforce it, the golden hashes, and a checklist for writing deterministic code.
sources: cmake/DgTargets.cmake, engine/core/include/dg/core/Hash.h, engine/sim/include/dg/sim/Rng.h, engine/sim/src/Simulation.cpp, tests/sim/SimDeterminismTest.cpp, .github/workflows/ci.yml
---

## Overview

Detergine's defining guarantee is **determinism**: given the same initial state and the same ordered stream of input
commands, the simulation produces **bit-identical** state on every supported platform, CPU architecture (x64 and
ARM64), compiler, and build configuration (Debug, Development, Release).

Everything multiplayer depends on this. In lockstep and rollback networking, peers exchange only inputs, never state.
If two machines compute even one different bit, their games drift apart, a **desync**. Replays, save states, kill-cams
and the planned server-side verification all rely on the same guarantee.

Determinism is a contract between the engine and every piece of sim code. The engine provides strict floating-point
compilation, deterministic math, a deterministic RNG and deterministic handle allocation. Sim code must follow the rules
on this page. Tests and CI check both sides with **golden hashes**: hash values recorded once that every platform must
reproduce exactly.

> **Note:** Status is *partial*. The compile flags, `dg::detmath`, `dg::fixed32`/`dg::fixed64`, `dg::sim::Rng`, FIFO
> handle reuse, the golden-hash tests and the Debug-vs-Release check exist today. Replay files, per-tick checksum
> exchange, desync reports, reflection-walked component hashing, explicit FTZ/DAZ control on sim threads, the
> banned-function lint and the Luau sandbox are designed (`docs/DESIGN.md` §5) but not built.

### What counts as sim code

The rules apply to the **sim layer**: `engine/sim`, the sim paths of `engine/ecs`, `engine/physics`, the lockstep part
of `engine/net`, `*.sim.luau` scripts, and any component tagged `Sim`. Today that means `engine/sim/` and
`engine/math/`, the two modules built with the `SIM` keyword. Presentation code (rendering, audio, VFX, UI) has no
determinism requirement, but it may never write sim state.

## The rules and why they exist

These are the non-negotiable rules from `CLAUDE.md`. Each one closes a specific source of divergence.

| # | Rule | Why |
|---|---|---|
| 1 | No wall-clock time, no OS calls, no file IO, no threads you spawn yourself. The sim sees only `tick`, the fixed `dt`, and inputs. | Clocks, OS answers, file contents and thread timing differ between machines and between runs. Anything that is not part of the input stream cannot be reproduced by another peer. |
| 2 | No `std::` transcendental math (`sin`, `cos`, `atan2`, `exp`, `pow`, ...). Use `dg::detmath`. `+ - * /` and `sqrt` are fine. | IEEE 754 specifies `+ - * /` and `sqrt` exactly, but not transcendentals. Every C library (glibc, Apple libm, the MSVC CRT, Android bionic) rounds them differently. |
| 3 | No `-ffast-math`, no FMA contraction. Do not override the strict FP flags. | Fast-math lets the compiler reorder and simplify float expressions. FMA contraction fuses `a * b + c` into one instruction with one rounding instead of two. Compilers contract `a * b + c` into an FMA by default whenever the target has an FMA instruction: always on ARM64, but on x64 only when FMA is enabled (`-mfma`, `-march=haswell`, ...), so the same source gives different bits. |
| 4 | No iteration over `unordered_*` containers, and no ordering by pointer or address. | Hash-table order depends on the hash function, the standard library and insertion history. Addresses depend on the allocator, ASLR and the platform. Neither is reproducible. |
| 5 | No `rand()`, `std::random_device`, or time seeds. Use `dg::sim::Rng`. | `rand()` differs between C libraries and is global state; `random_device` and time seeds are nondeterministic by design. |
| 6 | All sim state lives in ECS components or registered sim resources. No hidden statics or caches holding gameplay state. | Snapshots, rollback and resync can only capture and restore state they know about. A static survives a rollback and silently diverges. |
| 7 | Parallel sim work writes disjoint data only. Reductions and event merges happen afterward, in deterministic order. | Thread scheduling differs between runs. A float sum done in a different order rounds differently. |
| 8 | Zero-initialize sim components. Checksums hash reflected fields, never raw bytes or padding. | Uninitialized memory and padding bytes hold garbage that differs between runs, so hashing raw bytes reports desyncs that are not real. |
| 9 | The presentation layer never writes sim state. The only path into the sim is `InputCommand`s. | Presentation runs at a variable frame rate and differs per machine. If it could write sim state, that state would no longer be a function of the inputs. |
| 10 | Luau sim scripts: no globals or upvalue state, no `pairs`/`next` over hash tables (use `dg.ordered` / `ipairs`), no `os`/`io`/`debug`/`collectgarbage`/`__gc`/weak tables; use `dg.rng` and `dg.math` only. | Same reasons as rules 1, 4, 5 and 6, applied to the scripting VM: table iteration order, GC timing and global state are not reproducible. |
| 11 | Sim code builds and runs headless (`DG_HEADLESS=1`) and never references presentation modules, even optionally. | The headless `dg-server` runs the same sim as the clients and is part of the determinism contract. |

Any change to sim code must pass the determinism tests (`ctest -L determinism`, or `./dg test -L determinism`) on both
x64 and ARM64. If you cannot run them on both architectures, say so in the pull request.

## How the engine enforces it

### Strict floating point on every target

[DgTargets.cmake](cmake/DgTargets.cmake) applies strict FP flags through `dg_strict_fp`, which `dg_configure_target`
calls for **every** engine target: modules, apps and tests, not only sim targets.

| Compiler | Flags |
|---|---|
| Clang, AppleClang, GCC | `-ffp-contract=off -fno-fast-math` |
| clang-cl | `/clang:-ffp-contract=off` |
| MSVC | `/fp:precise` |

clang-cl's default FP model is already precise but still contracts to FMA. Adding `/fp:precise` as well makes clang-cl
warn that `-ffp-contract=off` overrides it, which is an error under warnings-as-errors, so clang-cl gets only the
contraction flag.

The flags are engine-wide because of inline code. An inline math function in a header (for example a `Vec3`
operator) is compiled separately in every translation unit that uses it. If a non-sim translation unit compiled it with
FMA contraction, the linker could keep that copy for the sim too: one function, two possible machine-code versions, an
ODR hazard. With one set of flags everywhere, every copy is the same.

The flags are also identical in all configurations, so Debug and Release builds compute the same simulation and can
play together.

`dg_sim_target` (used by `dg_add_module(<name> SIM ...)`) applies the same flags again and sets the `DG_SIM_TARGET`
target property, which lets tooling find deterministic targets. `dg::math` and `dg::sim` are sim targets.

### Deterministic math

* **`dg::detmath`** ([DetMath.h](engine/math/include/dg/math/DetMath.h)) provides `sin`, `cos`, `sincos`, `tan`,
  `asin`, `acos`, `atan`, `atan2`, `exp`, `exp2`, `log`, `log2` and `pow` for `f32`, plus IEEE-exact helpers (`sqrt`,
  `floor`, `ceil`, `round`, `trunc`, `abs`, `fmod`) so sim code never needs `<cmath>`. They are built only from
  exactly specified operations and defined out of line in `DetMath.cpp`, so a caller's compile flags cannot change
  them. They are correctly rounded except in rare near-halfway cases (at most 0.5 ulp measured). Every NaN result is the
  canonical quiet NaN `0x7FC00000`, because x64 and ARM64 produce different NaN bit patterns. See
  [Deterministic math (detmath)](../systems/detmath.md).
* **`dg::fixed32`** (Q16.16) and **`dg::fixed64`** (Q32.32) ([Fixed.h](engine/math/include/dg/math/Fixed.h)) are
  integer-backed fixed-point types for gameplay values such as health, currency and timers. Their rounding and overflow
  behaviour is exactly specified, and they do not depend on `__int128`, which MSVC and clang-cl lack. See
  [Fixed-point numbers](../systems/fixed-point.md).
* **Vectors, quaternions and matrices** are `f32` types compiled under the same strict flags. See
  [Vectors, quaternions and matrices](../systems/vector-math.md).

detmath assumes the default FP environment: round-to-nearest-even, with no flush-to-zero or denormals-are-zero. The
design sets FTZ/DAZ explicitly on sim threads; that is not implemented yet.

### Deterministic random numbers

`dg::sim::Rng` ([Rng.h](engine/sim/include/dg/sim/Rng.h)) is PCG32 (XSH-RR) and the only random source allowed in sim
code. Its state is part of the sim state, so it is hashed and will be snapshotted. `nextBounded(bound)` rejects biased
values instead of using a plain modulo, and `nextUnitFloat()` builds a value in [0, 1) from 24 random bits, which is
exact in `f32` and therefore identical everywhere.

### FIFO handle reuse

`dg::HandlePool<T>` ([Handle.h](engine/core/include/dg/core/Handle.h)) will back the sim entity allocator, so handle
values are part of the sim state. Every allocation decision depends only on the sequence of create and destroy calls,
never on addresses:

* fresh slots are taken in ascending index order;
* freed slots are reused **FIFO**, oldest first;
* generations start at 1 and grow by one per destroy, and a slot that would pass its maximum generation is retired
  instead of wrapping.

A LIFO free list would also be deterministic, but FIFO maximizes the time before a slot is reused, so stale handles are
caught for longer. See [Generational handles](../systems/handles.md).

### State hashing

`dg::sim::Simulation::stateHash()` hashes the tick counter, the RNG state and increment, and every body's position and
velocity, using each value's exact bit pattern. The hash is `dg::Fnv1a64` ([Hash.h](engine/core/include/dg/core/Hash.h)),
whose `updateValue` accepts scalars only, so struct padding can never leak into a hash (rule 8).

FNV-1a is a stand-in. When xxHash lands (implementation plan step 2), state hashes switch to XXH3, which changes every
recorded golden hash. The designed checksum walks all `Sim`-tagged components through reflection in canonical order and
adds the physics state hash (`docs/DESIGN.md` §5.4).

## Golden hashes

A **golden hash** is a hash of a fixed workload's results, recorded once on macOS ARM64 (AppleClang) and written into
a test as a constant. Every platform, compiler and configuration must reproduce it exactly. A mismatch means that code
path is not deterministic on that platform, or that someone changed the results.

| Suite | Test | Workload | Golden |
|---|---|---|---|
| `dg-core-tests` | `HandlePool.ScriptedCreateDestroySequenceMatchesGoldenHash` | 4000 scripted creates and destroys; hashes every issued handle and the final pool | `0x3a7ea63a09700779` |
| `dg-fixed-tests` | `FixedGolden.MixedOperationsHashMatchesRecordedValue` | About 100k mixed `fixed32` and `fixed64` operations; hashes the raw bits of every result | `0x508880CD2D9E3341` |
| `dg-detmath-tests` | `DetMathDeterminism.GoldenBitHashMatchesAcrossPlatforms` | Every detmath function over awkward and pseudo-random inputs, `atan2`/`pow`/`fmod` over a grid, and 4096 `pow` calls | `0x1BFF52C5DF7021AE` |
| `dg-vector-tests` | `MathGolden.DeterministicBatchOfOperationsMatchesRecordedBitPatterns` | A batch of vector, quaternion and matrix operations; hashes the bit patterns | `0xcdffd04806b21b0c` |
| `dg-sim-tests` | `SimDeterminism.StateHashMatchesCrossPlatformGolden` | 1800 ticks at 30 Hz, seed `0x5eed`, two players with scripted inputs | `0x9f504fe2ce4e5ee2` |

The sim golden equals the final state hash that `dg-player --headless --fast --ticks 1800` prints with its default
seed and player count, so you can check it by hand:

```bash
./dg run player -- --headless --fast --ticks 1800
# [info] final tick=1800 state-hash=0x9f504fe2ce4e5ee2 elapsed=...
```

The handle, detmath, vector and sim golden tests print their hash on every run, even when they pass, so you can compare runs from different machines. The fixed-point golden prints its digest only on failure.

### When a golden hash changes

* **By accident**: your change made results platform- or configuration-dependent, or changed behaviour you did not
  mean to change. Find and fix the cause; do not re-record.
* **On purpose** (the simulation, the math or the handle rules changed deliberately): re-record the constant from the
  value printed in the failure output, say so in the commit message, and update the golden table on this page and in
  `docs/IMPLEMENTATION_PLAN.md`. A changed hash composition or snapshot format also changes the network protocol once
  networking exists, which needs a protocol version bump.

## How CI proves cross-architecture determinism

The CI workflow ([ci.yml](.github/workflows/ci.yml)) runs on every push to `main` and every pull request:

| Job | Host | What it proves |
|---|---|---|
| Manual | `ubuntu-24.04` | `build_docs.py --check --changed-since <base>`: validates pages, links and source coverage, and fails when a documented source changed without a change to its page (unless the commit carries a `Docs: none (<reason>)` trailer). Then builds the HTML manual and uploads it as the `detergine-manual` artifact. |
| macOS ARM64 | `macos-15`, preset `macos-arm64` | `dg test` in Debug (warnings as errors), Development and Release, then `dg determinism`. |
| Windows x64 | `windows-2025`, preset `windows-x64` (Ninja + clang-cl) | The same steps on x64 with the MSVC standard library. |
| Linux x64 | `ubuntu-24.04`, preset `linux-x64` (Clang 19) | The same steps on x64 with libstdc++. |
| Linux x64 ASan + UBSan | `ubuntu-24.04` | `dg test debug` with both sanitizers and warnings as errors. |
| Android ARM64 | `ubuntu-24.04`, preset `android-arm64` | A Release build only; tests do not run yet. |

Two checks combine into the cross-architecture proof:

1. **The golden-hash tests** run on ARM64 (macOS) and x64 (Windows, Linux) in all three configurations. Because the
   constants were recorded on ARM64, a passing x64 job proves x64 produces the same bits.
2. **`dg determinism`** builds `dg-player` in Debug and Release, runs each with `--headless --fast --ticks 1800`, and
   fails with `DESYNC` unless the two `state-hash=` lines match. This catches configuration-dependent code on each
   host.

> **Warning:** As of implementation plan step 2, CI is green on macOS ARM64, Linux x64 (the golden hashes reproduce
> bit for bit on x64), Linux ASan + UBSan and the Android build. Windows x64 (clang-cl with the MSVC standard library)
> is not yet green, so the x64 goldens are verified only with Clang and libstdc++ on Linux. Android ARM64 builds but does not run tests. Replay-file
> determinism jobs across all platforms, including the headless server, are designed (`docs/DESIGN.md` §5.4) but not
> built.

## Checklist for deterministic code

Before you commit sim code, check every item:

* The code uses only the tick, the fixed `dt`, input commands and sim state. No clocks, environment variables,
  files, sockets or OS queries.
* No `<cmath>` transcendentals: `dg::detmath::sin`, `cos`, `atan2`, `pow` and so on instead of `std::` versions.
* No new compile flags on the target, and no `#pragma` that changes FP behaviour. Sim modules use
  `dg_add_module(<name> SIM ...)`.
* No iteration over `std::unordered_map` or `std::unordered_set`. Lookups are fine; iteration order is not.
* Nothing is sorted, compared or hashed by pointer value.
* Random numbers come from a `dg::sim::Rng` that is part of the sim state, never `rand()` or a time seed.
* No `static` or global variables hold gameplay state; no caches that survive a rollback.
* Every member of a sim struct has a default initializer; hashes go through `updateValue` on scalar fields, never
  raw bytes of a struct.
* Parallel work writes disjoint data; reductions run afterward in a fixed order.
* Integer conversions use fixed-width types (`i8`, `u32`, ...), never `char`, `wchar_t` or `long`, whose sign or
  width differs between platforms.
* The code builds with `DG_HEADLESS=ON` and includes no presentation headers.
* `./dg test -L determinism` and `./dg determinism` pass, and any golden hash you changed on purpose is
  re-recorded and documented.

### Example

```cpp
#include "dg/core/Types.h"
#include "dg/math/DetMath.h"
#include "dg/math/Fixed.h"
#include "dg/sim/Rng.h"

namespace game {

// Sim state: every field has an initializer, and the RNG lives in the state, not in a static.
struct Turret {
    dg::fixed32 heat{};
    dg::f32 angle = 0.0f;
    dg::sim::Rng rng{0x5eedu};
};

void aimAt(Turret& turret, dg::f32 dx, dg::f32 dy) {
    turret.angle = dg::detmath::atan2(dy, dx); // never std::atan2
    turret.angle += turret.rng.nextUnitFloat() * 0.1f - 0.05f;
    turret.heat += dg::fixed32(1) / dg::fixed32(4); // exactly 0.25 on every platform
}

} // namespace game
```

Related pages: [Architecture overview](overview.md) for the sim and presentation worlds,
[Simulation](../systems/simulation.md) for the current sim, and [Testing](../tooling/testing.md) for running the
suites.
