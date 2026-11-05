---
title: 3D audio
section: Planned systems
order: 80
status: design
summary: The planned dg::audio engine: event-based sound design, HRTF spatialization, acoustics, a bus mixer, virtual voices, and rollback-safe playback.
sources: docs/DESIGN.md, engine/core/include/dg/core/Hash.h, cmake/DgOptions.cmake
---

## Overview

> **Important:** `dg::audio` is not implemented. There is no `engine/audio/` module and no miniaudio or Steam Audio
> dependency yet. This page summarizes [DESIGN.md §9](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/DESIGN.md#9-3d-audio-system-dgaudio)
> and the audio tools in §14.3.

`dg::audio` is a built-in 3D audio engine of the class usually bought as middleware: game code plays **sound events**,
designers build those events from clips and containers, and the engine handles spatialization, acoustics and mixing.
Like VFX it is **presentation-only**: it never affects the sim, is excluded from state hashes, and headless builds link a
Null audio device. FMOD and Wwise can be added as optional, user-licensed plugins behind the same interfaces; they are
never required.

## Design

### Layers and threading

```
 Game / Luau view scripts ── dg.audio.play(event, …) ──┐
 ECS presentation components (Listener, Emitter, Zone) ─┤
                                                        ▼
 ┌──────────────────────── Audio API (game thread) ─────────────────────────┐
 │ Event instances · parameters · 3D attributes · snapshots · voice requests │
 └───────────────────────── lock-free command queue ─────────────────────────┘
                                                        ▼
 ┌──────────────────────── Audio engine (audio thread) ──────────────────────┐
 │ Event runtime (containers, randomization) → Voice manager (virtualization) │
 │ → Spatializer (panning / HRTF, attenuation, Doppler, occlusion, reverb)    │
 │ → Mixer bus graph (effects, sends, ducking, snapshots) → Master limiter    │
 └────────────────────────────────────────────────────────────────────────────┘
                                                        ▼
 Device backend: miniaudio (WASAPI · CoreAudio · AAudio/OpenSL ES) │ Null device (headless)
```

* The game thread only pushes commands (play, stop, set parameter, set 3D attributes) into a lock-free SPSC queue.
* The mixer runs on its own high-priority audio thread; the device callback only copies finished buffers. Neither the
  audio thread nor the callback allocates, locks, does file IO, or touches the ECS.
* Streaming IO runs on the engine IO thread into lock-free ring buffers.
* The device backend sits behind `IAudioBackend`. miniaudio is the default (device IO plus WAV/FLAC/MP3 decoding).

### Assets

All audio assets are reflected, YAML-serialized and referenced by GUID.

| Asset | Contents |
|---|---|
| **AudioClip** | Imported from `.wav/.flac/.ogg/.mp3`; cooked per platform to Opus (default), Vorbis, ADPCM or PCM. Load mode: decompress on load, compressed in memory, or streamed. LUFS loudness analysis, loop points and markers. |
| **SoundEvent** (`.dgsound`) | The unit game code plays: a tree of **containers** (Single, Random, Sequence, Switch, Blend, Layer), randomization, fades, instance limits with a steal policy, cooldown, priority, output bus, attenuation preset, and RTPC-like **parameters** mapped through curves |
| **AttenuationPreset** | Distance curve, min/max distance, spread, air-absorption low-pass, reverb send, and cone |
| **MixerGraph** (`.dgmix`) | Bus hierarchy, effect chains, sends, snapshots |
| **ReverbPreset / Impulse Response** | Algorithmic reverb parameters or a convolution IR |
| **AcousticMaterial** | Absorption, scattering and transmission, assigned to geometry |
| **MusicTrack / MusicSet** | Interactive music: tempo grid, segments, stems, transition rules, stingers |

### Spatialization and acoustics

* One or more `AudioListener` components (split-screen supported). The listener usually follows the camera.
* Emitters have position, velocity (for Doppler), orientation (for cones) and spread. Shape emitters (sphere, box,
  line, spline) use the closest point, for rivers or crowds.
* Panning tiers: VBAP-style speaker panning for stereo, 5.1 and 7.1; **HRTF** binaural rendering through **Steam Audio**
  on headphones, with stereo plus ITD/ILD as the low-tier fallback.
* Occlusion: on the low tier, a few rays per emitter against a dedicated **acoustic proxy BVH** built at cook time (the
  audio side never queries the sim physics world). On the high tier, Steam Audio occlusion and transmission through
  acoustic materials. Rays are amortized by priority and smoothed.
* Reverb: `AudioZone` volumes select a reverb preset and send levels and crossfade where they overlap. The high tier adds
  Steam Audio real-time or baked reflections and baked pathing.
* Ambience: 2D beds plus 3D one-shots scattered around the listener, driven by parameters such as weather.

### Mixer and voices

* Default bus graph: `Master → {Music, SFX → {Weapons, Footsteps, Impacts, …}, Voice, Ambience, UI}`, each bus with
  volume, pitch, mute/solo, an effect chain and aux sends.
* Built-in effects are `IAudioEffect` plugins processed in SIMD blocks: EQ, filters, compressor, limiter (always on
  Master), sidechain ducker, delay, algorithmic and convolution reverb, chorus, distortion.
* **Mixer snapshots** (for example *Paused* or *Underwater*) blend over time with priorities.
* **Virtual voices**: every playing instance exists logically, but only the top N by audibility × priority get a real
  voice (for example 128 on desktop, 32 on mobile). Virtual voices keep advancing their playback position.
* 32-bit float mixing in fixed blocks (for example 256 or 512 frames), SSE/AVX2 on x64 and NEON on ARM64, live handling
  of device changes, and AAudio low-latency mode on Android.

### Relationship to the simulation and networking

* **Rollback-safe playback**: a sound triggered by a *predicted* sim event starts immediately and is tagged with the
  event ID. If a rollback cancels the event, the sound fades out quickly (about 30–80 ms, configurable). If the event is
  confirmed, nothing happens. If re-simulation raises the same event again, it is deduplicated.
* **Same variation on every client**: random container choices and pitch/volume randomization are seeded from
  `hash(sim entity, tick, event id)` (best effort).
* Gameplay must never depend on audio. "Wait until the sound finishes" must be a sim timer.

## How it will be used

You attach presentation components (`AudioListener`, `AudioEmitter`, `AudioZone`, `AcousticGeometry`, `MusicPlayer`)
or play events from view scripts. Animation notifies and VFX events can trigger sound events directly. The design's
example (not runnable yet):

```lua
--!strict
-- footsteps.view.luau
local Footstep = dg.audio.event("SFX/Footstep")

return dg.view_system {
    on_sim_event = { Landed = function(view, e)
        dg.audio.play(Footstep, {
            position = view:position(e.entity),
            params = { Surface = e.surface, Speed = e.speed },
            source_event = e,          -- ties playback to the sim event for rollback cancel/dedupe
        })
    end },
}
```

Exposed parameters can be bound to reflected component fields (for example engine RPM to an `Rpm` parameter) and are
updated once per frame. Sound designers work in the planned Sound Event Editor, Audio Mixer, Attenuation Editor, Music
Editor and Audio Debugger (DESIGN.md §14.3; see [Editor, world-building tools and MCP server](editor-and-mcp.md)).

## Integration with existing code

* **Headless**: the CMake option `DG_HEADLESS` (in [DgOptions.cmake](cmake/DgOptions.cmake)) and
  `dg::build::kHeadless` already exist. `dg-server` always runs with `AppConfig::headless = true`. The audio module must
  stay out of headless builds; the cook will strip audio assets.
* **Seeding**: the per-instance seed is a hash of entity, tick and event ID. Today the engine's hash is `dg::Fnv1a64` in
  [Hash.h](engine/core/include/dg/core/Hash.h), a stand-in until xxHash lands (implementation plan step 2). Its
  `updateValue` hashes exact scalar bit patterns. Audio must **not** use `dg::sim::Rng`: that RNG is sim state.
* **Sim events**: rollback-safe playback needs sim events with stable IDs and the predicted/confirmed flag from the
  networking layer. Neither exists yet (see [ECS](ecs.md) and [Multiplayer networking](networking.md)).
* **Platform code**: device IO goes through miniaudio behind `IAudioBackend`; any platform-specific code belongs in
  `engine/platform/` or a clearly marked backend file, per CLAUDE.md.

## Roadmap

Audio is in step 15+ of the
[implementation plan](https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/docs/IMPLEMENTATION_PLAN.md),
DESIGN.md phase **6a**: audio thread and miniaudio backend, clip cooking (Opus), SoundEvents and containers,
attenuation, the bus mixer and snapshots, virtual voices, rollback-safe playback; then Steam Audio HRTF and occlusion,
reverb zones, baked acoustics, the music system, and the editor tools. It depends on the ECS, sim events, the asset
pipeline and (for rollback behaviour) netcode.

## Open risks

| Risk | Planned mitigation |
|---|---|
| Glitches from priority inversion or allocation on the audio thread | Real-time rules (no locks, allocations or IO on the audio thread), lock-free queues, an underrun counter in CI soak tests, low-latency testing on real Android devices |
| Duplicate or orphaned sounds after rollback | Event-ID tagging, fast fade on cancel, deduplication on re-raise |
| Steam Audio cost on mobile | Tiered panning and occlusion with separate HRTF and ray budgets |
