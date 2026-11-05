---
title: Multiplayer networking and headless servers
section: Planned systems
order: 50
status: design
summary: The planned built-in netcode (dg::net) with predict/rollback over server-confirmed inputs, the headless dg-server in relay and verify modes, desync detection and snapshot resync.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md, apps/server/Main.cpp, engine/runtime/src/Application.cpp, engine/sim/, engine/core/include/dg/core/BuildInfo.h, cmake/DgOptions.cmake, tests/CMakeLists.txt
---

## Overview

Multiplayer is built into Detergine. A game gets multiplayer by defining its `InputCommand` and writing sim systems;
it never writes networking code. The model is **predict/rollback over server-confirmed inputs**, served by the
headless `dg-server`. It is the same family of architecture as Photon Quantum 3, which is a design reference only;
the implementation is the engine's own
([DESIGN.md §15](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#15-built-in-deterministic-multiplayer-backend-dgnet)).

This works because the sim is deterministic: peers exchange only inputs, and every peer computes the same state from
the same input history. The state itself is sent only to repair a peer that diverged, or to let a late joiner in.

> **Note:** No networking code exists yet. Today `dg-server` is a headless host that runs the placeholder sim loop
> with scripted inputs (see [Runtime and apps](../systems/runtime-and-apps.md)). ENet is not in
> [vcpkg.json](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/vcpkg.json) yet.

## Design

### Roles and server modes

([DESIGN.md §15.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#151-topology--roles))

| Role | Responsibilities |
|---|---|
| **Client** | Sends its `InputCommand` for tick `T + inputOffset`. Runs predicted ticks with guessed remote inputs, and rolls back and resimulates when confirmed inputs arrive. Sends checksums of verified ticks. |
| **Input authority** (server, always on) | Owns the session clock. Collects inputs per tick until a deadline, substitutes a default or repeated input for late or missing players, and broadcasts the **confirmed input set**. A confirmed tick is final and never reopened. |
| **Checksum validator** (server, always on) | Compares client checksums and triggers resync. |
| **Authoritative sim** (server, `verify` mode only) | Runs the same sim headlessly from confirmed inputs. Never predicts, never rolls back. Ground truth for checksums, snapshots, late join and anti-cheat. |

| Mode | Truth | Use |
|---|---|---|
| **`relay`** | The majority of client checksums; resync snapshots come from a majority-agreeing client and are hash-checked | Cheap; many rooms per core; casual play |
| **`verify`** | The server's own headless sim | Competitive and ranked play |

`relay` is the default until the headless build is stable; then `verify` becomes the default for new projects
(decision 5 in
[DESIGN.md §18](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#18-key-risks--open-questions)).
Peer-hosted sessions run the server room in-process on one client, for LAN and development.

### Ticks and time

([DESIGN.md §15.2](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#152-ticks-frames-and-time-synchronization))

* A **verified tick** has every input confirmed by the server; its state is final and identical everywhere. A
  **predicted tick** runs ahead with predicted remote inputs and is rolled back when confirmations arrive.
* Clients run ahead of the server by about `RTT/2 + jitterBuffer`. Drift is corrected by **time dilation** of up to
  ±5% on the tick accumulator, never by skipping ticks.
* `inputOffset` (0 to N ticks of local input delay) trades latency for fewer rollbacks. The **max prediction window**
  (for example 10 ticks) stalls the client sim instead of letting it diverge further.
* The default tick rate is 30 Hz, configurable per project (`ProjectSettings.sim.tickRate`, decision 1).

### Inputs

([DESIGN.md §15.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#153-input-pipeline))

* `InputCommand` is a reflected struct (`DG_REFLECT(Input)`), bit-packed through reflection and delta-compressed
  against the previous tick.
* Each packet repeats the last N unacknowledged inputs on an unreliable channel, which tolerates loss without
  retransmit delay.
* `IInputValidator` on the server clamps or rejects impossible inputs before confirmation: the first anti-cheat layer.
* **Commands** are rare, larger reliable messages (buy item, chat) confirmed into a specific tick like inputs.
* Remote inputs are predicted by repeating the last confirmed input; games can override this per field.

### Desync detection and resync

([DESIGN.md §15.4](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#154-state-hashing-desync-detection),
[§15.5](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#155-resync-of-desynced-clients))

* The hash covers all `Sim` components in canonical order, the entity allocator, the sim RNG, the physics state hash,
  the tick and registered sim resources, using XXH3-64. It is **hierarchical**: per-chunk and per-system-group hashes,
  recomputed only for dirty chunks. Development hashes every tick; shipping every N ticks.
* Clients send `(tick, worldHash)` for verified ticks only. On a mismatch the server can request the client's hash
  tree, which pinpoints the diverging component type and entity range for the desync report.
* **Resync**: the server marks the client desynced, picks a reference snapshot at a verified tick (its own in `verify`
  mode, a majority client's in `relay` mode), and streams it chunked and zstd-compressed. The client restores it,
  checks its hash, fast-forwards headless through the confirmed inputs, and resumes prediction. The presentation world
  is rebuilt from the restored sim state.
* **Late join and reconnect** use the same path: a joining client is a client whose state is missing.
* `ResyncPolicy` limits resyncs per minute. Repeated desyncs at the same tick on the same build produce a bug report
  instead of a kick loop.

### Session lifecycle

([DESIGN.md §15.6](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#156-session-lifecycle--compatibility))

1. **Handshake** with the determinism manifest: engine build hash, sim-module and plugin hashes, the sim-asset hash
   of the room's level, tick rate, and the `InputCommand` schema hash. Any mismatch is rejected with a clear error.
2. **Room start**: the server broadcasts the start tick, the initial RNG seed and player slots.
3. **Running**: the input and confirm loop, checksums, and resync on demand.
4. **End**: the server can store a **replay** (initial snapshot plus confirmed inputs) that any headless build can
   re-simulate.

### The server

([DESIGN.md §15.7](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#157-server-architecture-dg-server),
[§16.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#161-headless-build-mode-per-level))

* One process hosts many rooms, each on a job-system worker with its own fixed tick timer.
* An HTTP/JSON management API (plugin-replaceable): create, list and close rooms, health, Prometheus metrics, replay
  download.
* Matchmaking and lobbies are a plugin interface (`ILobbyService`) with a simple built-in room list for development.
* Transport is behind `ITransport`: **ENet** by default (decision 3), GameNetworkingSockets as a plugin for encryption,
  Steam and relays, and a WebSocket fallback later. Inputs use an unreliable-sequenced channel; commands, snapshots and
  control use reliable-ordered.
* Headless builds (`DG_HEADLESS=1`) link only the sim side and are **level-scoped**: the cook keeps only the
  sim content of the listed levels (see [Assets, cooking and packaging](assets-and-packaging.md)).

## How it will be used

Gameplay code is identical online, offline and in replays. A sim system only sees `world:input(playerSlot)` for the
current tick; prediction, rollback and resync are invisible to it
([DESIGN.md §15.8](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#158-client-api-surface-c-and-luau)).

Connecting is a view-side concern. The planned Luau API:

```lua
-- view-side Luau (planned API)
local session = dg.net.connect { address = "eu.example.com:7777", room = "Arena-42" }
session.on_state_changed = function(s) ui.status:set_text(s) end   -- Connecting/Syncing/Running/Resyncing
```

Presentation code has to be rollback-aware:

* Sim events raised on predicted ticks reach the presentation layer with `predicted=true` and are later confirmed or
  cancelled, so a sound or VFX plays once and is not duplicated on resimulation.
* `IsPredicted(entity)` and a per-view-component choice between verified and predicted interpolation.
* Anything that must survive rollback or resync is sim state (determinism rule 6).

Running a server, as designed. The `--levels`, `--port`, `--mode`, `--max-rooms`, `--replay` and `--verify` flags do
not exist yet; `--tick-rate` does:

```bash
dg-server --levels Arena,Duel --port 7777 --mode verify --max-rooms 64 --tick-rate 30
dg-server --replay match_0193.dgreplay --verify      # exits non-zero on a hash mismatch
```

## Integration with existing code

* **The server app** exists: [apps/server/Main.cpp](apps/server/Main.cpp) calls `dg::runtime::runMain` with
  `.appName = "dg-server"` and `.headless = true`, and no flag turns presentation on. It accepts the shared runtime
  flags from [Application.cpp](engine/runtime/src/Application.cpp) today: `--headless`, `--fast`, `--ticks <n>`,
  `--tick-rate <hz>` (1 to 1000, default 30), `--players <n>` (default 2), `--seed <n>`, `--level <name>` (accepted
  but ignored with a warning), `--version` and `--help`. Try it:

  ```bash
  ./dg run dev server -- --fast --ticks 300
  ```

  The planned `--port`, `--mode`, `--max-rooms`, `--levels`, `--replay` and `--verify` flags will extend this parser.
* **Inputs**: `dg::sim::InputCommand` ([InputCommand.h](engine/sim/include/dg/sim/InputCommand.h)) is a placeholder
  (`moveX`, `moveY`, `buttons`). It becomes a reflected `DG_REFLECT(Input)` schema; any change to its layout then
  changes the network protocol version (CLAUDE.md, *Multiplayer & headless*). `scriptedInput(tick, slot)` stays useful
  for bots and smoke tests.
* **State hash**: `dg::sim::Simulation::stateHash()` already hashes tick, RNG state and bodies from exact bit patterns,
  and the runtime prints a machine-readable `state-hash=` line that `dg determinism` compares between Debug and
  Release. The netcode reuses the same idea across machines.
* **Headless profile**: `DG_HEADLESS` in [DgOptions.cmake](cmake/DgOptions.cmake) turns the editor off; `DG_BUILD_SERVER`
  is on for every non-mobile host. `dg::build::kHeadless` ([BuildInfo.h](engine/core/include/dg/core/BuildInfo.h)) reports it in the `--version` output.
* **Tests**: the `server.smoke` CTest runs `dg-server --fast --ticks 120` (see [Testing](../tooling/testing.md)).
* **When you change this design or start the module**: read DESIGN.md §15 and §16 first. Gameplay code never
  touches networking; sim code sees only `world:input(slot)`. Once the netcode exists, any change to the
  `InputCommand` schema, the hash composition or the snapshot format changes the network protocol: bump the protocol
  version, mention it in the commit message, and run the determinism replay tests on x64 and ARM64. New server flags
  go into the runtime parser and into this page. Update DESIGN.md in the same change.

## Roadmap

Step 14 in
[docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md):
ENet transport, the confirmed-input protocol, predict/rollback, hashing, resync, `dg-server` relay then verify mode,
and the headless level-scoped cook. The exit criterion is that two clients and a server stay in sync and a forced
desync is resynced. It needs the ECS snapshots (step 5), the sim world with replays and input commands (step 6), Luau
(step 8) and physics (step 9). In
[DESIGN.md §17](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#17-roadmap-phased)
this is phase 5, which also adds the network debugger panel in the editor.

## Open risks

* **Resync storms**: a deterministic bug makes a client desync again at the same tick. Mitigated by `ResyncPolicy`
  rate limits, bug reports with hash-tree diffs, and determinism CI that includes the headless server build.
* **Server cost in `verify` mode**: `relay` mode for casual rooms, stripped headless builds, many rooms per process.
* **Rollback cost**: heavy physics makes resimulation expensive; a configurable rollback window and a lockstep mode
  limit it (see [Physics](physics.md)).
* **Protocol churn**: changes to the `InputCommand` schema, the hash composition or the snapshot format are protocol
  changes. Each needs a protocol version bump mentioned in the commit message.
