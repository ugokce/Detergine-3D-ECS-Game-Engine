---
title: Home
section: Start here
order: 10
status: implemented
summary: What Detergine is, what it guarantees, how this manual is organized, and where to start reading.
sources: README.md
---

## What Detergine is

Detergine is a **natively deterministic**, modular 3D/2D game engine. Given the same initial state and the same ordered
stream of inputs, its gameplay simulation produces **bit-identical** results on every supported platform and CPU
architecture: Windows x64, macOS ARM64, Android ARM64, and headless Linux, Windows and macOS servers. This property is
what makes lockstep and rollback multiplayer work without extra effort from game code.

The engine core, physics and rendering are written in C++23. The renderer targets Vulkan. Gameplay is written in Luau
or C++ on a deterministic ECS simulation, and the editor is an ImGui application modeled on Unity's layout.

> **Important:** Detergine is early in development. Today the repository contains the build system, the developer
> tools, the core foundation (allocators, handles, logging, hashing), deterministic math, a placeholder simulation, and
> the three app hosts. The ECS, scripting, physics, renderer, editor UI and networking are designed but not built.
> Every page shows a status badge so you always know which is which.

## Pillars

| Pillar | What it means in practice |
|---|---|
| Deterministic simulation | A fixed-tick simulation that sees only the tick number, a constant `dt`, and input commands. Bit-identical on x64 and ARM64. |
| Built-in multiplayer | Predict/rollback over server-confirmed inputs, served by the headless `dg-server`, with state hashing and automatic resync (planned). |
| Performance | Data-oriented C++23 core, archetype ECS, job system, bindless Vulkan renderer with a render graph (ECS onward planned). |
| Productivity | A Unity-like editor with prefabs and hot-reloaded Luau (planned). |
| Modularity | Every subsystem is a module behind an interface; features ship as plugins. |
| Portability | Editor and runtime on Windows x64 and macOS ARM64, runtime on Android ARM64, headless servers on Linux, Windows and macOS. |

The determinism contract is the heart of the engine. Read [Determinism model](architecture/determinism.md) before you
write any simulation code.

## How this manual is organized

The manual moves from the high-level picture to precise, low-level detail.

| Section | Contents |
|---|---|
| **Start here** | This page and [Getting started](getting-started.md): requirements, first build, running the apps. |
| **Architecture** | [Architecture overview](architecture/overview.md) (layers, modules, the two worlds), [Determinism model](architecture/determinism.md), and [Decisions and roadmap](architecture/decisions-and-roadmap.md). |
| **Systems** | One page per implemented engine system, for example [Fixed-point numbers](systems/fixed-point.md) and [Generational handles](systems/handles.md). Each page explains how to use the system, how to change it, and how it is tested. |
| **Tooling** | The [build system](tooling/build-system.md), the [dg developer tool](tooling/dg-tool.md), the [launcher](tooling/launcher.md), [testing](tooling/testing.md), and [CI](tooling/ci.md). |
| **Planned systems** | Designed systems that are not built yet, starting with the [planned systems overview](planned/overview.md). |
| **Reference** | The [glossary](reference/glossary.md), the engine's dictionary of terms. |
| **Contributing** | [Writing documentation](contributing/documentation.md): page template, conventions, and the rule that keeps the manual current. |

### Status badges

Every page carries a status badge under its title, and the sidebar shows the same status as a coloured dot.

| Status | Meaning |
|---|---|
| Implemented | Built and tested. The page describes the code as it is now. |
| Partially implemented | Partly built. The page says clearly what exists and what is missing. |
| Design | Designed in `docs/DESIGN.md`, not built yet. |
| Planned | On the roadmap, not designed in detail yet. |

### Search and the A–Z index

* Press `/` (or click the search box) to search page titles, summaries, headings and the start of each page's text.
  Use the arrow keys and Enter to open a result, and Escape to close the list.
* The [A–Z index](a-z.html) lists every page title and every glossary term alphabetically. Use it when you know a
  term, such as *golden hash* or *fixed32*, but not the page it lives on.
* Each page has a table of contents for its own headings.

The manual is generated from Markdown in `docs/manual/` by `./dg docs`. It works straight from disk, with no web server.

## Where to start

### If you are a game developer

1. Build the engine and run the player: [Getting started](getting-started.md).
2. Learn the one rule that shapes all gameplay code: [Determinism model](architecture/determinism.md).
3. See how the engine is split into a deterministic sim world and a free presentation world:
   [Architecture overview](architecture/overview.md).
4. Read the gameplay-facing systems that exist today: [Fixed-point numbers](systems/fixed-point.md),
   [Deterministic math (detmath)](systems/detmath.md), and [Simulation](systems/simulation.md).
5. Look ahead at the gameplay APIs that are coming:
   [Luau scripting and C++ game modules](planned/scripting.md) and [ECS](planned/ecs.md).

### If you are an engine contributor

1. Build and test on your machine: [Getting started](getting-started.md), then [Testing](tooling/testing.md).
2. Read the [Architecture overview](architecture/overview.md) for the layering and module rules, and the
   [Determinism model](architecture/determinism.md) for the rules every sim change must follow.
3. Check [Decisions and roadmap](architecture/decisions-and-roadmap.md) to see which implementation step is in progress.
4. Before you change a system, read its page under **Systems** or **Tooling**. Each one has a *How to edit* section
   listing the invariants you must keep and the tests and golden hashes that must change with your code.
5. Update the manual in the same commit as your change: [Writing documentation](contributing/documentation.md).
   CI enforces this with `dg docs --check --changed-since <base>`; run the same check locally with
   `./dg docs --check --changed-since origin/main`.

## Other sources of truth

* `docs/DESIGN.md` is the architecture decision record: the *why* and the target shape of every system.
* `docs/IMPLEMENTATION_PLAN.md` breaks the roadmap into verifiable steps.
* `CLAUDE.md` holds the rules every contributor, human or AI agent, must follow.

This manual describes what exists, how to use it, and how to change it. Where it summarizes a design that is not built
yet, it says so.
