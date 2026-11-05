---
title: Editor, world-building tools and MCP server
section: Planned systems
order: 110
status: design
summary: The planned Unity-like ImGui editor, its built-in content tools and world-building tools, and the editor-only MCP server that lets AI agents edit projects safely.
sources: docs/DESIGN.md, apps/editor/Main.cpp, cmake/DgOptions.cmake
---

## Overview

> **Important:** The editor UI is not implemented. Today `dg-editor` ([Main.cpp](apps/editor/Main.cpp)) parses the
> shared engine command line, logs that the editor UI is not implemented yet, and runs the same fixed-tick loop as
> `dg-player`. There is no `editor/` directory, no Dear ImGui dependency, no `dg::mcp` module and no `dg mcp` command.
> This page summarizes [DESIGN.md §14](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#14-editor-unity-like).

The Detergine editor is a desktop application (Windows x64 and macOS ARM64) built with **Dear ImGui** (docking and
multi-viewport), ImGuizmo, ImPlot and imgui-node-editor, with a theme modeled on Unity's dark and light skins. The
layout and workflows are *inspired by* Unity; CLAUDE.md forbids copying Unity's icons, trademarks or assets.

Three parts are covered here:

1. the editor shell and its panels,
2. the world-building tools,
3. the **MCP server** (`dg::mcp`), which lets AI agents such as Claude Code inspect and edit a project through the same
   editor commands a human uses.

Nothing depends on the editor. Editor-only code lives in `editor/` or in `#if DG_EDITOR` blocks inside `*.editor.cpp`
files and must compile out of the player (CLAUDE.md architecture rules).

## Design

### Panels

| Panel | Function |
|---|---|
| **Hierarchy** | Scene tree, drag reparenting, search, prefab coloring and override markers, multiple scenes |
| **Scene View** | Free-fly and orbit camera, W/E/R/T tools (move, rotate, scale, rect), local/global pivot, grid/vertex/surface snapping, 2D mode, gizmos, GPU ID-buffer picking, overlays (wireframe, GI debug, colliders, nav) |
| **Game View** | Player camera, resolution and aspect presets, stats overlay, **multiple simulated clients** for local determinism and network testing |
| **Inspector** | Reflection-generated component editors, custom drawers (`IInspectorDrawer`), prefab override UI, multi-object editing, add-component search |
| **Project** | Asset browser over `Assets/` with thumbnails, search and filters, drag and drop into the scene, import settings |
| **Console** | Logs with filters, collapse and source jump; Lua errors with stack traces |
| **Profiler** | Tracy client view plus engine counters (sim tick ms, rollback count, GPU passes) |
| **Animation Viewer / Anim Graph** | See [Skeletal animation](animation.md) (DESIGN.md §14.4) |
| **UI Designer** | See [Runtime UI](ui.md) |
| **Particle Editor** | See [Particles and VFX](vfx.md) (DESIGN.md §14.2) |
| **Audio Mixer / Sound Event Editor** | See [3D audio](audio.md) (DESIGN.md §14.3) |
| **MCP** | Agent connections, activity log, permissions (below) |
| **Network Debugger** | Ping, jitter and rollback visualizer, per-tick checksum timeline, desync diff viewer |
| **Build Settings** | Scene list, platform targets, quality tiers, Build and Build & Run |

Editor infrastructure: an undo/redo command stack made of reflection-based property-diff commands, a selection service,
preferences, Unity-default keyboard shortcuts, a thumbnail cache, and an editor scripting API (`*.editor.luau`).

**Play mode** snapshots the edit world, runs, and restores the snapshot on stop, so nothing changed during play persists
by accident (DESIGN.md §12).

### World-building tools

| Tool | Design |
|---|---|
| **Terrain** | Heightmap tiles with clipmap rendering; sculpt, smooth, flatten and stamp brushes; splat-map painting; holes; a deterministic collision heightfield (Jolt `HeightFieldShape`) |
| **Foliage / detail painting** | GPU-instanced scattering with density, slope and height rules |
| **Brush modeling** | ProBuilder-like greyboxing: primitives, face/edge/vertex editing, extrude, bevel, CSG blocking, UV tools |
| **Splines** | Road, river and fence generation along splines |
| **Prefab painting / scatter** | Scatter prefabs; grid snapping and alignment tools |
| **Navigation** | Navmesh baking with Recast/Detour; pathfinding queries in the sim are deterministic |
| **Lighting tools** | Light and reflection probe placement, lightmap baking, GI debug views |

Terrain collision and navigation feed the sim, so their runtime data must follow the determinism rules. The other tools
produce presentation data or plain scene content.

### MCP server architecture

The editor embeds a **Model Context Protocol (MCP)** server
([DESIGN.md §14.5](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#145-mcp-server--ai-agent-integration-dgmcp)).
Every agent change shows up in the open editor, is undoable, and ends up as ordinary YAML and Luau files.

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

* **Protocol**: an in-house JSON-RPC 2.0 layer (yyjson), **pinned to MCP spec revision 2025-11-25**, with version
  negotiation in `initialize`. Spec differences live in a **protocol adapter**, so newer revisions don't touch tool
  code. No official C++ MCP SDK exists; community SDKs were evaluated and not adopted.
* **Transports**:
  * **stdio** (default for Claude Code): the client launches the `dg-mcp` bridge (planned `apps/mcp-bridge/`), which
    connects to the running editor over a Unix domain socket or Windows named pipe in `.dg/`. With no editor open, it
    starts a **headless editor** (`dg-editor --headless --mcp`), rendering offscreen for screenshots when a GPU exists.
  * **Streamable HTTP** (off by default, cpp-httplib): binds to 127.0.0.1 only on a random port, requires a per-session
    bearer token written to `.dg/mcp.json`, and validates the `Origin` header against DNS rebinding.
* **Threading**: requests are parsed on an IO thread and executed on the editor main thread through a command queue.
  Long operations (imports, bakes, builds, determinism checks) send progress notifications, honor cancellation, and
  are exposed as MCP tasks where the client supports them.
* **Setup**: `dg mcp install` writes a project-scoped `.mcp.json` that launches `dg mcp serve`; `dg mcp status` shows
  connections; the Launcher gets matching buttons.

### MCP tools, resources and prompts

Tool input and output JSON Schemas are **generated from `dg::refl`**, so every reflected component and property is
editable with validated types, ranges and enums, and new components appear automatically.

| Area | Tools |
|---|---|
| Levels and scenes | `level.list`, `level.open`, `level.new`, `level.save`, `level.save_as`, `level.get_hierarchy`, `level.diff` |
| Entities and components | `entity.create`, `entity.delete`, `entity.duplicate`, `entity.set_parent`, `entity.find`, `component.add`, `component.remove`, `component.get`, `component.set`, `transform.set`, `transform.align` |
| Prefabs | `prefab.create_from_entity`, `prefab.instantiate`, `prefab.list_overrides`, `prefab.apply_overrides`, `prefab.revert_overrides`, `prefab.create_variant` |
| Assets | `asset.search`, `asset.info`, `asset.import` (project files only), `material.create`, `material.set`, `asset.thumbnail` |
| Animation | `anim.list_clips`, `anim.clip_info`, `anim.graph_create`, `anim.graph_edit`, `anim.graph_validate`, `anim.assign_animator`, `anim.add_event`, `anim.preview` |
| Logic (Luau) | `script.api`, `script.create`, `script.read`, `script.edit`, `script.check` (type check plus determinism lint), `script.attach` |
| VFX and audio | `vfx.create_from_template`, `vfx.set_parameter`, `audio.create_event`, `audio.assign_emitter` |
| Play and verify | `play.start`, `play.stop`, `play.step`, `play.state_hash`, `play.set_input`, `verify.determinism`, `editor.console`, `editor.screenshot` |
| Editor | `edit.batch`, `editor.undo`, `editor.redo`, `editor.select`, `editor.focus` |
| Build (full permission) | `build.run`, `build.test` |

* **Resources**: `dg://project/manifest`, `dg://levels/{guid}`, `dg://schema/components`, `dg://api/luau`,
  `dg://docs/agent-guide`, `dg://console/recent`. Level and asset resources support subscriptions, so an agent learns
  when a human edits the same level.
* **Prompts**: `blockout_level`, `create_character`, `add_gameplay_system`, `fix_desync`.
* The `initialize` result carries server instructions with the recommended loop: read the schema and API → edit in
  batches → `script.check` / `anim.graph_validate` → `play.step` and `editor.screenshot` → `verify.determinism` →
  `level.save`.

### MCP safety model

| Guarantee | How |
|---|---|
| Nothing reaches disk unasked | Changes stay in the editor until `level.save` or an asset-creating tool runs. Output is reviewable YAML and Luau; git is the final review. |
| Every change is undoable | Each mutating call is one undo transaction labeled `MCP: <tool>`. `edit.batch` is all-or-nothing. |
| Permission levels | Per project in `.dg/mcp-policy.yaml`: `read-only`, `edit` (default; saving needs confirmation) and `full` (save, import, build, play), plus a per-tool allow/deny list |
| Confirmations | Destructive operations (deleting assets, overwriting a level, builds) ask the human through MCP elicitation or an editor modal |
| Sandboxing | File access limited to `Assets/`, `Scripts/` and `Levels/`. No shell or native-code tool. Agent-written Luau runs only in the normal script sandbox. |
| Play mode isolation | Edits during play touch the play world and are discarded on stop; results say which world they touched. MCP edits are disabled in networked sessions. |
| Never shipped | `dg::mcp` is editor-only: excluded from players, Release builds and headless servers. `--mcp` exists only on the editor binary. |

The **MCP panel** shows connected clients, a live activity log of every call (arguments, result, duration, affected
entities highlighted) with undo per call, permission and tool toggles, and a **pause agent** switch that queues
mutating calls until resumed. Plugins add tools, resources and prompts through `IMcpToolProvider` and get the same
validation, undo and permission handling.

## How it will be used

* **Humans**: `./dg run editor` builds and starts `dg-editor` (it is the default app of `./dg run` on desktop presets; on `-server` presets the default is `server`, and Android presets default to `player`).
  Today that runs the sim loop only. Once the editor exists, you will build a level in the Scene View, edit components
  in the Inspector, press Play to test, and save scenes and prefabs as YAML.
* **AI agents** (planned): run `dg mcp install` once in the project, then start Claude Code there. The agent connects
  through `dg-mcp`, reads `dg://schema/components` and `dg://api/luau`, edits in batches, verifies with `play.step`,
  `editor.screenshot` and `verify.determinism`, and saves with `level.save`. You review the result in the MCP panel and
  in git.
* **Extending**: plugins add panels (`IEditorPanel`), inspector drawers (`IInspectorDrawer`), viewer panels
  (`IAnimViewerPanel`), VFX modules (`IVfxModule`) and MCP tools (`IMcpToolProvider`).

## Integration with existing code

* **Editor app**: [Main.cpp](apps/editor/Main.cpp) builds `dg::runtime::AppConfig{.appName = "dg-editor"}`, calls
  `dg::runtime::parseCommandLine`, and then `dg::runtime::run`. The editor shell will replace that loop. See
  [Runtime and apps](../systems/runtime-and-apps.md).
* **Build option**: `DG_BUILD_EDITOR` in [DgOptions.cmake](cmake/DgOptions.cmake) defaults to `ON` and is forced off
  for Android, iOS and `DG_HEADLESS` builds, so the editor and MCP server can never end up in those binaries. In
  `apps/CMakeLists.txt` the `dg-editor` target is only created when it is on. See
  [Build system](../tooling/build-system.md).
* **Command line**: the shared parser already accepts `--headless`, which the headless MCP editor will use. It rejects
  unknown arguments, so `--mcp` and `--anim-viewer` must be added to the editor's own parsing when they are built.
* **Not yet present**: the `DG_EDITOR` preprocessor macro, the `dg mcp` subcommand of [the dg tool](../tooling/dg-tool.md),
  and launcher buttons for MCP. The [Detergine Launcher](../tooling/launcher.md) already notes that the editor is not
  built in the headless profile.
* **Prerequisites**: the editor relies on reflection (step 3), serialization (step 4), ECS snapshots for play mode
  (step 5) and the RHI with ImGui (step 10). See [ECS](ecs.md) and [Rendering and global illumination](rendering.md).

## Roadmap

| Step ([IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md)) | Scope | Exit criteria |
|---|---|---|
| 12 — Editor MVP | Docking layout, Hierarchy / Inspector / Scene / Game / Project / Console, gizmos, undo/redo, play mode, scenes and prefabs | Build a level, play it, save it, reload it |
| 13 — MCP server | `dg::mcp` and stdio bridge; scene, entity, component, prefab, script and play tools; undo per call; policy | Claude Code builds and saves a small level through MCP |
| 15+ — Content systems | World-building tools and the content editors | Per DESIGN.md phases 6–9 |

In DESIGN.md §17 terms: phase 4 (editor MVP), phase 4b (MCP server, then animation, VFX and audio tools and the HTTP
transport; each later phase adds MCP tools for its features), and phase 9 (terrain, foliage, brush modeling, splines,
navmesh).

## Open risks

| Risk | Planned mitigation |
|---|---|
| AI agents making unwanted or destructive edits | Undo transaction per call; nothing saved without `level.save`; permission levels and confirmations; project-only file access; no shell or native-code tools; activity log; pause switch |
| MCP spec churn (sessions and tasks moving to extensions in newer drafts) | Pinned revision with version negotiation; differences isolated in the protocol adapter; conformance tests against the official MCP inspector |
| Scope (an engine of Unity+UE breadth) | Strict phase gating; every module behind interfaces so features can be delayed without refactors (DESIGN.md §18). For the editor this means every panel behind `IEditorPanel` |
