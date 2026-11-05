---
title: Assets, cooking and packaging
section: Planned systems
order: 120
status: design
summary: The planned asset pipeline (dg::asset) with GUID .meta files, pluggable importers, a derived data cache, the dg-cook cooker, .dgpak archives, level-scoped headless cooks and the dg-package packager.
sources: docs/DESIGN.md, docs/IMPLEMENTATION_PLAN.md, engine/core/include/dg/core/Handle.h, engine/core/include/dg/core/Hash.h, engine/runtime/, cmake/DgOptions.cmake, vcpkg.json
---

## Overview

The asset pipeline turns source files in a project's `Assets/` folder into cooked, per-platform data that the player
and the headless server load at runtime. It is modeled on Unity's workflow: every source file has a sidecar `.meta`
file with a stable GUID, and serialized data refers to assets by GUID, never by path (CLAUDE.md, *Architecture
rules*). Cooking and packaging produce `.dgpak` archives and ready-to-ship builds for Windows, macOS and Android.

> **Note:** None of this exists yet. There is no `engine/asset` module, no `dg-cook` and no `dg-package`; the
> `tools/` folder holds only `dg.py`, `dg_launcher.py` and the manual builder. The design is in
> [DESIGN.md §11](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#11-asset-pipeline)
> and [§16](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#16-build-configurations--packaging).

## Design

### Asset database

* Every source file in `Assets/` has a YAML **`.meta`** sidecar holding a stable **GUID**, the importer type and
  version, and the import settings. Moving or renaming a file keeps its GUID.
* Serialized objects (scenes, prefabs, components) reference assets by GUID through `AssetHandle<T>`
  ([DESIGN.md §4.3](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#43-serialization-dgser)).

### Importers

Importers implement the pluggable `IAssetImporter` interface. The planned set
([DESIGN.md §2.6](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#26-assets--content)):

| Source | Library | Output |
|---|---|---|
| glTF / GLB | fastgltf | Meshes, materials, skeletons, clips |
| FBX | ufbx | Meshes, skeletons, clips |
| OBJ, DAE and others | Assimp (optional importer plugin) | Meshes |
| Textures | stb_image, tinyexr | KTX2 with Basis Universal, transcoded to BC7 (desktop) or ASTC (mobile) |
| Audio | miniaudio decoding, libopus, stb_vorbis | Opus by default |
| Fonts | FreeType, HarfBuzz, msdf-atlas-gen | MSDF glyph atlases |
| Lua scripts | Luau compiler | Luau bytecode |
| Shaders | Slang | SPIR-V |
| Prefabs and scenes | rapidyaml | YAML in the editor, binary when cooked |

Mesh processing uses meshoptimizer (vertex cache and fetch optimization, LOD simplification, meshlets).

### Derived data cache

The import output is keyed by `hash(source content, import settings, importer version, platform)` and cached locally,
optionally shared on the network. Changing any of the four inputs re-imports; nothing else does.

### Cooking and `.dgpak`

`dg-cook` converts assets per platform (BC7 or ASTC, mesh quantization, shader variants), computes the dependency
closure from the build's scene list, and writes **`.dgpak`** archives: zstd-compressed chunks, a table of contents by
GUID, and an mmap-friendly layout.

### Runtime loading

`AssetHandle<T>` is reference counted. Loading streams asynchronously through an IO thread plus job-system decoding,
with LRU eviction under memory budgets. Texture mip streaming and mesh LOD streaming come later.

### Sim-relevant assets

Collision meshes, gameplay data tables and sim scripts are flagged **sim-relevant**. Their content hashes form part
of the session's **determinism manifest**, so peers with different sim content cannot join the same session
([Multiplayer networking and headless servers](networking.md)).

### Headless, level-scoped cooks

([DESIGN.md §16.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#161-headless-build-mode-per-level))

A headless cook computes the dependency closure of only the listed levels and strips presentation data: render
meshes, textures, materials, shaders, audio, UI, VFX (`*.dgvfx`) and view-only components on prefabs. It keeps
collision shapes, navmesh, data tables, sim scripts (Luau bytecode), sim components and prefabs. The output is a
small `server.dgpak` plus a **sim-asset manifest hash per level**, which is the hash clients send in the handshake.

### Packaging

`dg-package` (also driven from the editor's Build Settings window) cooks for a target platform, builds the player with
static plugins, and assembles the output
([DESIGN.md §16](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#16-build-configurations--packaging),
[§2.7](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#27-packaging--distribution)):

| Target | Output |
|---|---|
| Windows | `Game.exe`, `*.dgpak` and redistributables; optional MSIX or installer |
| macOS | `Game.app` (ARM64) with MoltenVK in `Contents/Frameworks`, `Info.plist` and entitlements, then `codesign`, `notarytool`, `stapler`; `.dmg` |
| Android | A Gradle project generated from `/platform/android` (SDL3 activity), paks in `assets/` or an install-time asset pack, signed `.aab` / `.apk` (ARM64) |
| Server | The `dg-server` binary, `server.dgpak` and `server.yaml` (rooms, ports, tick rate, mode, resync policy); an optional Docker image on Linux |

Shipping builds (`Release`) use static plugins, stripped symbols and cooked paks only.

## How it will be used

For a game developer, as designed:

1. Drop source files into `Assets/`. The editor imports them, writes `.meta` files, and shows them in the Project
   panel with their import settings in the Inspector. Commit the `.meta` files with the sources.
2. Reference assets from components, scenes and prefabs by dragging them in the editor; the serialized YAML stores
   GUIDs.
3. Mark the scenes that ship in Build Settings, then build. Or cook and package from the command line:

```bash
# Planned commands (DESIGN.md §16.1); these tools do not exist yet.
dg-cook --profile headless --levels Levels/Arena.dgscene,Levels/Duel.dgscene
dg-package --target server --platform linux-x64 --levels Levels/Arena.dgscene --config Shipping
dg-package --target server --platform windows-x64 --levels Levels/Arena.dgscene,Levels/Duel.dgscene
```

Engine contributors add a new asset type by implementing `IAssetImporter` in a module or plugin, versioning the
importer (a version bump invalidates its DDC entries), and declaring whether its output is sim-relevant.

## Integration with existing code

* **GUIDs and handles**: `AssetHandle<T>` follows the handles-over-pointers rule that `dg::Handle` and
  `dg::HandlePool` ([Handle.h](engine/core/include/dg/core/Handle.h)) implement today. See
  [Generational handles](../systems/handles.md).
* **Hashing**: asset IDs and DDC keys use XXH3. Today the engine only has `dg::Fnv1a64`
  ([Hash.h](engine/core/include/dg/core/Hash.h)); xxHash arrives in step 2.
* **Levels**: the runtime already accepts `--level <name>` and stores it in `dg::runtime::AppConfig::level`
  ([Application.h](engine/runtime/include/dg/runtime/Application.h)), but logs a warning that level loading is not
  implemented and ignores it.
* **Build configurations**: `Debug`, `Development` and `Release` exist in the build system today, and `DG_HEADLESS`
  is the headless profile option ([DgOptions.cmake](cmake/DgOptions.cmake)). The `Profile` configuration is planned.
  See [Build system](../tooling/build-system.md).
* **Tooling**: `dg-cook` and `dg-package` are planned as `tools/dg-cook/` and `tools/dg-package/`
  ([DESIGN.md §3.1](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#31-repository-layout-target)).
  The [dg developer tool](../tooling/dg-tool.md) is the natural front end for them.
* **Dependencies**: fastgltf, ufbx, KTX-Software, Basis Universal, meshoptimizer, zstd, LZ4 and rapidyaml are all
  listed in DESIGN.md §2 but not yet in [vcpkg.json](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/vcpkg.json), which contains only `gtest` and `spdlog`.
* **When you change this design or start the module**: read DESIGN.md §11 first. Serialized data references assets by
  GUID, never by path. Changing which assets are sim-relevant, or how their content hashes enter the determinism
  manifest, changes what peers accept at the handshake. New importers go behind `IAssetImporter`, and every new
  third-party library needs a permissive license, a vcpkg port or vendored copy, and a DESIGN.md §2 line. Update this
  page and DESIGN.md in the same change.

## Roadmap

From [docs/IMPLEMENTATION_PLAN.md](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md):

| Step | Asset work |
|---|---|
| 4 | Serialization: the YAML and binary archives that `.meta` files, scenes and cooked data use |
| 11 | Renderer MVP: glTF import |
| 12 | Editor MVP: Project panel, scenes and prefabs |
| 14 | Netcode: the headless level-scoped cook |
| 15+ | Packaging: `dg-cook`, `.dgpak`, `dg-package` |

In [DESIGN.md §17](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#17-roadmap-phased)
packaging is phase 7 (`dg-cook`, `.dgpak`, `dg-package` for Windows, macOS and Android, Build Settings window), FBX
import is phase 6, and the headless cook is phase 5.

## Open risks

* **Sim-asset hashing must be canonical.** If the cooked bytes of a sim-relevant asset differ between hosts (float
  formatting, ordering in YAML), the determinism manifest rejects valid peers. Cooked sim data has to be produced
  deterministically, or hashed in a canonical form.
* **Stripping too much or too little.** The headless cook must drop view-only components from prefabs without dropping
  anything a sim system reads; a mistake shows up only when a server runs the level.
* **Platform signing and store tooling** (notarization, Android signing) change outside the engine's control.
* **Scope.** The importer list is long; each importer is a separate plugin behind `IAssetImporter`, so formats can be
  added late without refactors.
