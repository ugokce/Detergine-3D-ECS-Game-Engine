---
title: Runtime UI
section: Planned systems
order: 100
status: design
summary: The planned dg::ui system: retained-mode, ECS-backed UI with anchors, Yoga flexbox layout, style sheets, MSDF text, and a WYSIWYG UI Designer.
sources: docs/DESIGN.md, engine/sim/include/dg/sim/InputCommand.h
---

## Overview

> **Important:** `dg::ui` is not implemented. There is no `engine/ui/` module and no Yoga, FreeType, HarfBuzz or
> msdf-atlas-gen dependency yet. This page summarizes
> [DESIGN.md §13](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#13-runtime-ui-system-dgui--ui-designer).

`dg::ui` is the in-game UI system: menus, HUDs, inventories and world-space panels in shipped games. It is separate from
the editor's Dear ImGui interface, which is editor-only and never ships in a player. Runtime UI is presentation: it
reads sim state for display, but it never writes it. A button press reaches the sim only as an input command.

## Design

### Elements and layout

* **Retained mode, ECS-backed**: UI elements are entities. Each has a `RectTransform` (Unity-like anchors, pivot and
  offsets) and optionally a `FlexLayout` container that uses **Yoga** flexbox layout.
* **Responsive layout**: anchor presets, safe-area insets (notches), DPI scaling, a reference resolution with a
  match-width/height policy (like Unity's `CanvasScaler`), **breakpoints** (style overrides per aspect or size class),
  and 9-slice images.

### Styling

A **style sheet** asset with USS-like selectors (type, class, and states such as `:hover`, `:pressed` and `:focus`)
is applied to elements.

### Widgets

Panel, Image, Text, Button, Toggle, Slider, InputField, ScrollView, Dropdown, ListView (virtualized) and ProgressBar.

* Text uses **HarfBuzz** shaping (RTL, ligatures) and **MSDF** glyph atlases from msdf-atlas-gen, so it stays crisp at
  any scale.
* InputField gets IME support through SDL3.

### Input

Pointer, touch and gamepad input are unified. Controllers navigate with **focus graphs**.

### Rendering

UI is batched by the 2D renderer into the `UI` render-graph extension point. Canvases can be screen-space,
camera-space or world-space. See [Rendering and global illumination](rendering.md).

### Data binding

Reflected properties bind to **view-model components**, and Luau view scripts (`*.view.luau`) handle UI logic.
Bindings are generated from reflection, like every other script binding.

### UI Designer

The editor's **UI Designer** is a WYSIWYG canvas with:

* device preview presets (resolution, aspect and safe area for phones, tablets and desktops),
* drag-and-drop widgets from a palette,
* anchor and flex gizmos,
* live style editing,
* a hierarchy view.

UI layouts are saved as **prefabs**, so they use the normal prefab instance and override workflow (DESIGN.md §12).

## How it will be used

1. Open the UI Designer, pick a device preset, and drag widgets from the palette onto the canvas.
2. Set anchors on each `RectTransform`, or add a `FlexLayout` to a container for flowing layouts.
3. Style elements with a style-sheet asset instead of per-element colors.
4. Bind widget properties to fields of a view-model component, and write behaviour in a `*.view.luau` script.
5. Save the layout as a prefab and instantiate it from a scene or from a view script.

To change gameplay from the UI (for example a "Buy" button), the view script turns the click into an input for the
local player. It never writes a sim component directly, because CLAUDE.md determinism rule 9 makes `InputCommand`s the
only path into the sim. Everything named on this page is planned; none of it exists in the code yet.

## Integration with existing code

* **Input path**: the current `dg::sim::InputCommand` in
  [InputCommand.h](engine/sim/include/dg/sim/InputCommand.h) is a placeholder (`moveX`, `moveY`, `buttons`, with
  `dg::sim::buttons::kJump`) until reflected input schemas land. UI-driven gameplay actions will become fields of the
  project's input schema. Changing the `InputCommand` schema changes the network protocol, so it needs a protocol
  version bump (see [Multiplayer networking and headless servers](networking.md)).
* **ECS and reflection**: elements as entities, data binding and style assets all depend on the ECS (step 5),
  reflection (step 3) and serialization (step 4). See [ECS](ecs.md) and [Luau scripting](scripting.md).
* **Headless**: `DG_HEADLESS` builds exclude UI (the option's own description says "no renderer, audio, UI or editor").
* **Prefabs and assets**: UI prefabs and style sheets are GUID-referenced assets (see
  [Assets, cooking and packaging](assets-and-packaging.md)).

## Roadmap

Runtime UI and the UI Designer are DESIGN.md phase **6 (Content)**, step 15+ of the
[implementation plan](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md).
They need the 2D renderer (steps 10–11), the editor (step 12) for the Designer, and the Luau view-script runtime
(step 8).

## Open risks

DESIGN.md §18 lists no UI-specific risks. The main schedule risk is dependencies: runtime UI cannot start before the
2D renderer, reflection-driven bindings and the Luau view runtime exist. The architectural risk to watch in review is
UI code writing sim state; the presentation-layer rule above (UI produces input commands only) is the guard.
