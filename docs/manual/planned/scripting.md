---
title: Luau scripting and C++ game modules
section: Planned systems
order: 30
status: design
summary: The planned Luau-first gameplay layer (dg::script) with deterministic sim scripts, free view scripts, bindings generated from reflection, and optional static C++ game modules.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md, engine/math/include/dg/math/DetMath.h, engine/math/include/dg/math/Fixed.h, engine/sim/, cmake/DgTargets.cmake, cmake/DgOptions.cmake, cmake/triplets/, vcpkg.json
---

## Overview

Gameplay in Detergine is **Luau-first**: a developer must be able to write a complete game in Luau without any C++
(decision 2 in
[DESIGN.md §18](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#18-key-risks--open-questions)).
C++ runs the engine itself, and a project can add native gameplay code as an optional **game module**.

Luau was chosen over plain Lua and LuaJIT because it has sandboxing built in, gradual typing for editor
autocompletion, a fast interpreter, optional native code generation for x64 and ARM64, and a native `vector` type.
LuaJIT is rejected: JIT is forbidden on iOS and its trace JIT makes determinism auditing harder. Lua 5.4 with sol2
stays a fallback backend behind `dg::script`
([DESIGN.md §2.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#24-scripting)).

> **Note:** No scripting code exists yet, and Luau is not in [vcpkg.json](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/vcpkg.json). Every `dg.*` name on this page
> is the proposed API from
> [DESIGN.md §6](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#6-scripting-luau)
> and can change before step 8.

## Design

### Script roles

The file suffix says where a script runs
([DESIGN.md §6.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#61-roles-of-lua)):

| Suffix | Role | Deterministic | In headless builds |
|---|---|---|---|
| `.sim.luau` | **Sim scripts**: gameplay logic inside the sim world | yes, sandboxed | yes |
| `.view.luau` | **View scripts**: presentation, UI, VFX, audio, camera | no | no |
| `.editor.luau` | **Editor scripts**: tooling, custom inspectors, menu items (later) | no | no |

Every engine-provided and template script starts with `--!strict`.

### Binding generation

There is no hand-written glue
([DESIGN.md §6.2](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#62-binding-generation)).
A generator walks `dg::refl` for everything marked `ScriptVisible` and produces:

* Luau userdata wrappers and fast-call bindings on the C++ side.
* **`dg.d.luau`**, a type-definition file that luau-lsp uses for autocompletion and type checking in the editor and
  IDEs. It is regenerated whenever a script-visible API changes.

Components are exposed as **typed views** onto ECS storage, with no copies. Entities are opaque handles.

### The deterministic sandbox

The VM wrapper enforces these rules for sim scripts
([DESIGN.md §6.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#63-deterministic-lua-sandbox-rules-enforced-by-the-vm-wrapper)):

| Rule | Why |
|---|---|
| No gameplay state in globals, upvalues or module-level tables; environments are frozen after load (`table.freeze`) | Snapshots capture only ECS state, never the Lua heap |
| No `pairs` / `next` over hash tables; use `dg.ordered(t)`, `ipairs` or a numeric `for` | Hash-table order depends on hash seeds and addresses. Enforced by a custom lint and a debug-mode runtime check |
| Fixed string hash seeds; no `os`, `io`, `debug`, arbitrary `require`, `collectgarbage`, `__gc` or weak tables | These expose the OS, the clock or GC timing |
| `math.random` is replaced by the sim RNG (`dg.rng`); `math.*` transcendentals by `dg::detmath` (`dg.math`) | Same results on every platform (determinism rules 2 and 5) |
| Gameplay-critical numbers use `dg.fixed` userdata or engine vectors | Luau numbers are doubles; `+ - * /` and `sqrt` are deterministic only with FMA disabled in the VM build |
| Native codegen stays off for sim scripts unless verified per architecture | Interpreter only by default |
| Instruction budget per script per tick (Luau interrupt callback) | Exceeding it is a hard error in development |

### Hot reload

View and editor scripts reload instantly. Sim scripts reload by rebuilding the sim world from the last snapshot. In
networked sessions sim reload is disabled, or done as a coordinated reload with a script hash bump
([DESIGN.md §6.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#64-hot-reload)).

### C++ game modules

When a project needs native code (hot loops, custom sim systems, middleware glue), it adds a **game module**
([DESIGN.md §6.5](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#65-native-c-gameplay-modules-optional)):

* Classes, components, systems and free functions in the project's `Source/` folder, annotated with `DG_REFLECT`,
  `DG_PROPERTY` and `DG_FUNCTION(ScriptVisible)`.
* `dg_add_game_module(<Project>Game SOURCES ...)` compiles them into a **static library** with the engine's
  determinism flags. `dg-reflect` generates the reflection data and the Luau bindings, so native types appear in
  `dg.d.luau` next to the engine API.
* The library is linked into the player, the headless server and the project's editor build. Shipping builds never
  load game code at runtime.
* Native sim systems follow every determinism rule and contribute to the session's determinism manifest, like Luau
  sim scripts.

## How it will be used

A sim system declares its phase and query and reacts to events. The example is from
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

A view script handles everything non-deterministic, for example connecting to a session
([DESIGN.md §15.8](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#158-client-api-surface-c-and-luau)):

```lua
-- view-side Luau (planned API)
local session = dg.net.connect { address = "eu.example.com:7777", room = "Arena-42" }
session.on_state_changed = function(s) ui.status:set_text(s) end
```

The workflow for a game developer:

1. Write gameplay as `.sim.luau` systems that read player input with `world:input(slot)`. Keep all state in
   components.
2. Write cameras, UI, VFX and audio in `.view.luau` scripts. They may read sim state and sim events, never write them.
3. Use the editor or VS Code with luau-lsp and `dg.d.luau` for completion. The determinism lint flags forbidden
   constructs before the game runs.
4. Only if profiling demands it, move a hot system into a C++ game module. Luau code keeps calling it through the
   generated bindings.

## Integration with existing code

* **Deterministic math for `dg.math` and `dg.fixed`** exists today: `dg::detmath` (`sin`, `cos`, `tan`, `asin`,
  `acos`, `atan`, `atan2`, `exp`, `log`, `pow` and more) in [DetMath.h](engine/math/include/dg/math/DetMath.h), and
  `dg::fixed32` (Q16.16) and `dg::fixed64` (Q32.32) in [Fixed.h](engine/math/include/dg/math/Fixed.h). See
  [Deterministic math](../systems/detmath.md) and [Fixed-point numbers](../systems/fixed-point.md).
  Every `dg::detmath` function takes and returns `f32` (for example `f32 sin(f32 x)`), while Luau numbers are `f64`.
  The `dg.math` binding therefore either narrows each argument to `f32` and widens the result, which is
  deterministic but loses precision, or needs `f64` detmath implementations. DESIGN.md does not decide this yet.
* **`dg.rng`** will wrap `dg::sim::Rng` ([Rng.h](engine/sim/include/dg/sim/Rng.h)), a PCG32 generator with
  `nextU32()`, `nextBounded(bound)` and `nextUnitFloat()`, whose state is part of the sim state.

  ```cpp
  #include "dg/sim/Rng.h"

  dg::u32 rollDie(dg::sim::Rng& rng) {
      return rng.nextBounded(6) + 1; // uniform in [1, 6], no modulo bias
  }
  ```

* **`world:input(slot)`** will return the reflected successor of `dg::sim::InputCommand`
  ([InputCommand.h](engine/sim/include/dg/sim/InputCommand.h)), which today has `moveX`, `moveY` and a `buttons`
  bitmask with `buttons::kJump`.
* **Strict FP for the VM.** Every engine target is already built with `-ffp-contract=off -fno-fast-math` (or the
  clang-cl / MSVC equivalents) by `dg_configure_target()` in [DgTargets.cmake](cmake/DgTargets.cmake). The Luau VM
  must be built with the same flags. vcpkg ports are compiled with their triplet's flags, not the engine's, and the
  overlay triplet in [cmake/triplets/](cmake/triplets/) sets no FP flags. The stock `luau` vcpkg port exposes no FP or
  FMA option either (its only feature is `tool`), so the VM needs an overlay port or a vendored copy in `third_party/`
  that applies the strict flags. That is a dependency decision that needs an ADR and a DESIGN.md §2 note.
* **`dg_add_game_module`** will join `dg_add_module` and `dg_add_app` in [DgTargets.cmake](cmake/DgTargets.cmake).
  It does not exist yet.
* **Headless**: with `DG_HEADLESS=1` ([DgOptions.cmake](cmake/DgOptions.cmake)) only sim scripts are loaded; view
  scripts are stripped by the headless cook.
* **When you change this design or start the module**: read DESIGN.md §6 and the Lua rule (rule 10) in CLAUDE.md
  first. Bindings are generated from reflection, so a change to a `ScriptVisible` API means regenerating `dg.d.luau`,
  not editing glue by hand. Anything that changes what sim scripts can observe or compute (the `dg.math` precision,
  the RNG, the input schema) changes replay results: re-record golden hashes and run the determinism replay tests on
  x64 and ARM64. Update this page and DESIGN.md in the same change.

## Roadmap

Step 8 in
[docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md):
the Luau VM with sim and view sandboxes, the determinism lint, the binding generator from reflection, `dg.d.luau`,
and `dg_add_game_module`. The exit criterion is that a Luau-only sample game and a C++-module sample both run
deterministically.

It depends on step 3 (reflection, which drives bindings), step 5 (ECS, which the typed component views point into)
and step 6 (the sim world and input commands). Editor scripts come later, with the editor (step 12).

## Open risks

* **VM floating point.** Luau numbers are doubles. Determinism holds for `+ - * /` and `sqrt` only if the VM itself
  is compiled without FMA contraction on every platform. The vcpkg build of Luau must be checked for this.
* **Native codegen.** Luau's native code generator is a different code path per architecture. It stays disabled for
  sim scripts until it is proven bit-identical on x64 and ARM64.
* **Hidden state.** Frozen environments and the lint must catch every way to keep state outside components (upvalues,
  module tables, metatables). A miss breaks rollback silently, so the replay tests must exercise scripts.
* **Hot reload in sessions.** Reloading sim scripts in a networked session requires a coordinated reload and a script
  hash bump; uncoordinated reload would desync peers.
