# SIME — Session Report, 2026-05-23

> A single-day push covering audio export, transport / playback UX, persistence
> bug-hunting, the entire Block Movement v1 feature, 3D gizmos & grid planes,
> audio analysis (frequency / oscilloscope), Doppler effect, sidebar scrolling,
> immediate pause/stop semantics, seek-to-time visual sync, automatic block
> duration adjustment, per-block & per-type mute, hide, time-window mute, loop
> length & loop gap, and a `.sime` file format bumped from **v5 → v6 → v7 → v8**.

This document is the canonical reference for the architecture changes made in
this session.  Teammates picking up adjacent features (e.g. **multi-select
mute / hide via click-drag**, **block-type changes mid-movement**, **multiple
position keyframes**, **MP3 / AAC export back-ends**) should read this before
opening a PR.

---

## Contents

1. [Top-level summary](#1-top-level-summary)
2. [File-by-file changes](#2-file-by-file-changes)
3. [Data model changes](#3-data-model-changes)
4. [Threading touchpoints](#4-threading-touchpoints)
5. [Persistence: v5 → v6 → v7 → v8](#5-persistence-v5--v6--v7--v8)
6. [Feature deep-dives](#6-feature-deep-dives)
   - 6.1 [Audio export](#61-audio-export)
   - 6.2 [Playback speed dropdown (0.25× – 3×)](#62-playback-speed-dropdown-025--3)
   - 6.3 [HUD overlay rendering bug](#63-hud-overlay-rendering-bug)
   - 6.4 [Persistence bugs (block labelling, scene reset)](#64-persistence-bugs-block-labelling-scene-reset)
   - 6.5 [Hover / select interaction](#65-hover--select-interaction)
   - 6.6 [Block movement v1 (Phase 1)](#66-block-movement-v1-phase-1)
   - 6.7 [3D arrow gizmos & grid plane toggles (Phase 2)](#67-3d-arrow-gizmos--grid-plane-toggles-phase-2)
   - 6.8 [Audio analysis: frequency & oscilloscope (Phase 3)](#68-audio-analysis-frequency--oscilloscope-phase-3)
   - 6.9 [Doppler effect (Phase 4)](#69-doppler-effect-phase-4)
   - 6.10 [Sidebar info-panel scrolling](#610-sidebar-info-panel-scrolling)
   - 6.11 [Pause / Stop immediacy & seek-to-time visual sync](#611-pause--stop-immediacy--seek-to-time-visual-sync)
   - 6.12 [Auto duration on sound assignment](#612-auto-duration-on-sound-assignment)
   - 6.13 [Per-block mute, hide; per-type View / Mute toolbar filters](#613-per-block-mute-hide-per-type-view--mute-toolbar-filters)
   - 6.14 [Loop overhaul (toggle move, length enforcement, gap)](#614-loop-overhaul-toggle-move-length-enforcement-gap)
   - 6.15 [Time-window mute](#615-time-window-mute)
7. [Bug log](#7-bug-log)
8. [What's still queued](#8-whats-still-queued)
9. [How to extend](#9-how-to-extend)

---

## 1. Top-level summary

| Category | What changed |
|----------|--------------|
| **New systems** | Offline audio bounce (`SceneAudioExporter`); audio analysis (`AudioAnalysis`); 3D arrow gizmos; grid plane toggles; Doppler model; per-type view / mute filters; per-block mute, hide, mute-window; loop length + loop gap; sidebar scrolling. |
| **Refactored systems** | Looping moved from sequencer retriggers to audio-thread sample wrap; pause / stop now atomic at the audio engine; seek now syncs visual block positions; loop UI consolidated in the Info panel. |
| **Removed** | Silent `autosave.sime` autoload (caused "old scene appears after New Scene"); Loop toggle + Loop (s) editor from `BlockEditPopup`. |
| **Persistence** | `.sime` schema bumped from v5 → v6 (`movementEnabled`) → v7 (`isMuted`, `isHidden`, `loopBufferSec`) → v8 (`muteStartSec`, `muteEndSec`).  Older files load with sensible defaults. |
| **Threading** | New atomic flags on `AudioEngine` (`audioPaused_`, `killAllVoices_`, `listenerX/Y/Z_`, `dopplerEnabled_`, `speedOfSound_`); new transient per-block flag `effectiveMuted` set by the GL thread before each sequencer tick. |
| **UI** | DAW-style toolbar: `Mute ▾ · View ▾ · File ▾` on the right; new toggle pills for `Floor / YZ Wall / XY Wall / Arrows / Doppler`; speed dropdown on the transport bar; expanded Block Info panel (mode, movement duration, path Y lift, loop toggle, loop length + match-block-duration button, loop gap, mute, hide, mute from / to, match-duration-to-sound). |

---

## 2. File-by-file changes

| File | Touched in this session for… |
|------|---------------------------|
| `Source/AudioAnalysis.h` (new) | Pitch + waveform-envelope analysis API |
| `Source/AudioAnalysis.cpp` (new) | Autocorrelation F0 estimate + min/max envelope |
| `Source/AudioEngine.h` | `audioPaused_`, `killAllVoices_`, listener pos, Doppler, `loopGapSamples`/`loopGapRemaining` on voices, `setAudioPaused/killAllVoices/setListenerPosition/setDopplerEnabled/setSpeedOfSound` |
| `Source/AudioEngine.cpp` | Pause / stop semantics, voice loop-gap silence countdown, Doppler rate compute, voice pos/vel fields |
| `Source/BlockEntry.h` | `playbackMode`, `movementDurationSec`, `movementYOffset`, `movementEnabled`, `sampleNaturalDurationSec`, `isMuted`, `isHidden`, `loopBufferSec`, `muteStartSec`, `muteEndSec`, transient `effectiveMuted` & `wasMutedLastTick`, `effectiveMovementDuration()` |
| `Source/BlockEditPopup.h` / `.cpp` | Removed Loop toggle + Loop (s) editor; "Browse File" button rename + widening; pass-through of `isLooping`/`loopDurationSec` on commit |
| `Source/Renderer.h` / `.cpp` | `renderPlaneXZ` / `renderPlaneXY` / `renderPlaneYZ` (separate VAOs); `renderArrow` for gizmos; `buildPlaneMeshes` / `buildArrowMeshes` |
| `Source/Main.cpp` | Removed `autosave.sime` silent autoload |
| `Source/MainComponent.h` / `.cpp` | `File ▾ / View ▾ / Mute ▾` menus; toggle pills (Floor / YZ Wall / XY Wall / Arrows / Doppler); speed dropdown wiring; widened `onApplyBlockInfo` callback; `onMatchDurationToSound` |
| `Source/SceneFile.cpp` | v6 (`movementEnabled`), v7 (`isMuted`, `isHidden`, `loopBufferSec`), v8 (`muteStartSec`, `muteEndSec`) — additive, backwards compatible |
| `Source/SceneAudioExporter.cpp` | New offline mixer mirroring `AudioEngine`; per-voice Doppler + loop-gap mirrored; per-block `isMuted` honoured (per-type mute deliberately skipped) |
| `Source/SequencerEvent.h` | `loopBuffer`, `playbackRateOverride`, `loopBufferSec`, velocity (`velocityX/Y/Z`, `hasVelocity`) |
| `Source/SequencerEngine.h` / `.cpp` | Mode resolution helper, loop-length enforcement, movement velocity, `snapBlockPositionsToTime`, mute transition events, `effectiveMuted` consumption |
| `Source/SidebarComponent.h` / `.cpp` | Info-panel scrolling, mode combo, movement duration / Y lift editors, loop toggle + length + `Loop = Block Dur.` button + loop-gap editor, mute toggle, hide toggle, mute-from / mute-to editors, `Match Duration to Sound`, audio-analysis line + oscilloscope graph |
| `Source/TransportBarComponent.h` / `.cpp` | Speed dropdown (0.25× / 0.5× / 0.75× / 1× / 2× / 3×) replacing the previous cycle button |
| `Source/TransportClock.cpp` / `.h` | `playbackRate`, applied in `update(dt)` so the whole pipeline accelerates uniformly |
| `Source/ViewPortComponent.h` / `.cpp` | Gizmo state, plane / arrow toggles, `analyzeBlockAudio`, immediate transport semantics, `seekTransportClock` block-position sync, auto-duration on sound assign, `setBlockTypeVisible` / `isBlockTypeVisible` / `setBlockTypeMuted` / `isBlockTypeMuted`, per-frame `effectiveMuted` composition, `matchBlockDurationToSound`, widened `applySidebarBlockInfo` |
| `CMakeLists.txt` | Added `Source/AudioAnalysis.cpp` |

---

## 3. Data model changes

`BlockEntry` accumulated a fair number of new fields.  Grouped by purpose:

```cpp
// ── Phase 1 movement controls ──────────────────────────────────────────────
enum class BlockPlaybackMode : uint8_t { Natural, Loop, Stretch, Speed };
BlockPlaybackMode playbackMode        = BlockPlaybackMode::Natural;
double            movementDurationSec = 0.0;   // 0 = use durationSec
int               movementYOffset     = 0;     // global Y lift for the recorded path
bool              movementEnabled     = true;  // keep keyframes but disable playback
double            sampleNaturalDurationSec = 0.0; // refreshed every frame from the library

// effectiveMovementDuration() = movementDurationSec > 0 ? movementDurationSec : durationSec

// ── v7 per-block flags ─────────────────────────────────────────────────────
bool   isMuted        = false;   // forever-mute audio (movement still plays)
bool   isHidden       = false;   // hide block from the viewport (renderer skip)
double loopBufferSec  = 0.0;     // silence (seconds) inserted between loop iterations

// ── v8 time-window mute ────────────────────────────────────────────────────
double muteStartSec   = 0.0;     // window mute applies while now ∈ [start, end)
double muteEndSec     = 0.0;     // 0/0 → no window

// ── Transient runtime flags (NOT persisted) ────────────────────────────────
bool effectiveMuted     = false; // = isMuted || isBlockTypeMuted(blockType) — set by GL thread
bool wasMutedLastTick   = false; // mute-state edge detection for cut/resume events
```

`SequencerEvent` grew with:

```cpp
bool   loopBuffer           = false;   // audio-thread should wrap sample at end
float  playbackRateOverride = 1.0f;    // Stretch/Speed-mode rate baked at Start
float  loopBufferSec        = 0.0f;    // mirrors BlockEntry::loopBufferSec
float  velocityX, velocityY, velocityZ = 0.0f;  // Doppler input
bool   hasVelocity          = false;
```

`AudioEngine::ActiveVoice` grew with:

```cpp
float  blockRate        = 1.0f;
float  dopplerRate      = 1.0f;
bool   loopBuffer       = false;
float  loopGapSamples   = 0.0f;
float  loopGapRemaining = 0.0f;
float  posX, posY, posZ = 0.0f;   // Doppler source pos (live)
float  velX, velY, velZ = 0.0f;   // Doppler source velocity
```

All transient runtime members reset cleanly via `BlockEntry::resetPlaybackState()`.

---

## 4. Threading touchpoints

| Thread | Owns | New responsibilities this session |
|--------|------|-----------------------------------|
| **GL render thread** | `blockList`, `SequencerEngine`, `TransportClock`, raycasting, voxelGrid | Drains `pendingBlockEdit_` and `pendingSidebarEdit_`; refreshes `sampleNaturalDurationSec` and `effectiveMuted` for every block before each sequencer tick; feeds the camera position to `AudioEngine::setListenerPosition` each frame; emits synthetic `Stop` events when loop-related fields change so live voices rebake; runs `snapBlockPositionsToTime` on Stop / scrub. |
| **Audio thread** | `activeVoices_`, mixing | Reads `audioPaused_` / `killAllVoices_` atomics each block; per-sample state machine for loop-gap silence; per-sample step now multiplies `pitchRate × blockRate × dopplerRate`. |
| **Message thread** | UI, callbacks | Speed dropdown / toolbar toggle pills / `View ▾` / `Mute ▾` menus all flip atomics on the viewport or audio engine; sidebar `onApplyBlockInfo` posts to `pendingSidebarEdit_`. |

The only cross-thread structures touched this session are:

* `pendingBlockEdit_` + `pendingSidebarEdit_` (mutex-guarded; drained on GL).
* Lock-free `juce::AbstractFifo` of `SequencerEvent` (already existed).
* Atomic flags on `AudioEngine` and `ViewPortComponent`.

There are still some inherited races (T1 / T2 / T3 / T4 in the README), but
this session did **not** add new ones.

---

## 5. Persistence: v5 → v6 → v7 → v8

`SceneFile::kVersion` was bumped three times in this session.  All bumps are
**strictly additive trailing fields** — readers fall back to defaults when
loading an older version.

```text
v5 → v6  (block movement Phase 1 follow-up)
   + uint8_t movementEnabled

v7 → v7  (per-block UX flags)
   + uint8_t isMuted
   + uint8_t isHidden
   + double  loopBufferSec

v7 → v8  (time-window mute)
   + double  muteStartSec
   + double  muteEndSec
```

Bumping bypass for older saves:

```cpp
if (version >= 6) read movementEnabled;
else              b.movementEnabled = b.hasRecordedMovement;

if (version >= 7) read isMuted, isHidden, loopBufferSec;
// else all default to false / 0.0

if (version >= 8) read muteStartSec, muteEndSec;
// else default 0.0 / 0.0 → no time-window mute
```

`SceneFile::kMagic` is unchanged.  Anything written by the current code is
**not** readable by older binaries (the version check fails fast), but all
v5+ files are still readable by the current code.

---

## 6. Feature deep-dives

### 6.1 Audio export

**Files:** `Source/SceneAudioExporter.h/.cpp`, `Source/ExportAudioDialog.h/.cpp`,
`Source/MainComponent.cpp` (File menu), `CMakeLists.txt`.

The exporter is a deterministic, offline mirror of the live audio path: a
private `TransportClock` ticks forward in fixed `kChunkSamples` chunks, the
real `SequencerEngine::update` runs against a *copy* of the block list, and a
local `MixerVoice` struct (mirroring `AudioEngine::ActiveVoice`) renders into
a `juce::AudioBuffer<float>`.

Key design decisions:

* **Sample rate:** matches the live audio device rate by default; if the chosen
  file format (e.g. FLAC) only supports specific rates, `pickWriterSampleRate`
  walks `juce::AudioFormat::getPossibleSampleRates()` and picks the closest.
* **Length:** whatever the timeline says — `max(b.endTimeSec())` over every
  block + every `timesList` entry.  Capped at `kMaxDurationSec` (20 min) as a
  safety net.
* **Formats:** WAV, AIFF (16-bit PCM, lossless), FLAC (lossless), Ogg Vorbis
  (lossy).  MP3 / AAC deferred — JUCE doesn't ship encoders for them on all
  platforms.
* **Doppler:** the exporter calls `dispatchEvents(..., dopplerEnabled=false)`
  to mirror the live default-off behaviour.  Flip to `true` if you ever want
  exports to bake the effect in.
* **Per-type mute:** intentionally **ignored** during export; only `isMuted`
  on individual blocks is honoured.  Type filters are a view convenience and
  shouldn't silently change what gets rendered to disk.

UI: a new `ExportAudioDialog` shows the format picker + a `FileChooser` for
the destination path.  Wired into the `File ▾` menu's "Export Audio…" entry.

### 6.2 Playback speed dropdown (0.25× – 3×)

**Files:** `Source/TransportClock.cpp/h`, `Source/AudioEngine.cpp/h`,
`Source/TransportBarComponent.cpp/h`.

The clock now carries a `playbackRate` member; `update(dt)` multiplies its
delta by that rate before advancing.  Because **all** sequencing is driven by
the clock — and because audio voices step at `voice.pitchRate * blockRate *
dopplerRate * playbackRate` — speeding up the clock speeds up the whole
composition consistently.

The transport bar replaced the original "1× / 2× / 3× cycle button" with a
proper `juce::PopupMenu` (0.25×, 0.5×, 0.75×, 1×, 2×, 3×).  Rate can be
selected mid-playback and takes effect on the next clock tick.  The button
label shows the current rate.

### 6.3 HUD overlay rendering bug

**Files:** `Source/ViewPortComponent.cpp`.

Symptom: the "Controls" pill at the bottom of the viewport was rendered with
its right-hand half "washed off" — visible only on the left of the pill.  The
root cause was an OpenGL state leak from `renderOpenGL()` confusing the JUCE
2D compositor's alpha-blending state when it overlaid the pill in `paint()`.

Fix: at the end of `renderOpenGL()` we now perform a comprehensive GL state
reset (blend func, depth test, depth mask, cull face, line width, viewport,
vertex array, program) so JUCE sees a clean slate.

### 6.4 Persistence bugs (block labelling, scene reset)

Three related symptoms:

1. **"Block 251" after New Scene.**  Root cause: `nextSerial` was a free
   counter never reset on scene clear.  Fix: `ViewPortComponent::clearScene()`
   resets `nextSerial = 1`.
2. **New Scene loads the previous scene.**  Root cause: `Main.cpp` silently
   loaded `autosave.sime` on startup; if the user pressed "New Scene" before
   the autoload completed, the autoload won the race.  Fix: removed the
   silent autoload entirely.  `clearScene()` now also explicitly cancels any
   pending load.
3. **Block Info panel still shows a deleted block.**  Root cause: deleting a
   block did not notify the sidebar.  Fix: new
   `SidebarComponent::clearSelectedBlock()` and
   `clearSelectedBlockIfSerial(int)`; the deletion path in `ViewPortComponent`
   calls the targeted variant, and `MainComponent::newScene` /
   `loadSceneFromFile` call the unconditional clear.

Naming: the previous `Block N` label is now `Violin N`, `Piano N`, etc., via
`displayNameForSerial()`.

### 6.5 Hover / select interaction

Two sub-bugs, both in `ViewPortComponent`:

* `doRaycast()` did not set `hoveredBlockSerial_`, so hovering over an
  existing block never produced a green highlight.  Fixed.
* LMB click in normal mode always placed a new block, even when hovering over
  an existing block.  Now, in normal mode, LMB on a hovered block selects it
  (opens Block Info) instead of placing on top of it.

### 6.6 Block movement v1 (Phase 1)

The biggest single feature of the day.  Original ask was a 7-point list; what
shipped:

* `BlockPlaybackMode { Natural, Loop, Stretch, Speed }`.
  * **Natural** — sample plays once, stops at sample end.
  * **Loop** — sample wraps inside the audio thread for as long as the region
    is active.  No sequencer retriggers (gapless).
  * **Stretch** — sample's playback rate scaled so it spans the region; pitch
    follows.
  * **Speed** — like Stretch but only speeds up (rate ≥ 1.0).
* `movementDurationSec` — independent control over how long the recorded
  movement path takes to play out.  0 means "use the region duration"
  (the legacy behaviour).
* `movementYOffset` — integer Y offset applied globally to every keyframe at
  playback time.  Lets you lift / drop a recorded path without re-recording.
* `movementEnabled` — keep the keyframes on disk but disable playback.  The
  Info panel toggle now actually works (was a no-op before).
* 3D recording — keyframes carry `Vec3i` positions, so Y is captured.
* "Reset to Default" button in the Info panel resets all four movement
  params (`playbackMode`, `movementDurationSec`, `movementYOffset`,
  `isLooping`, `loopDurationSec`) to factory defaults via
  `BlockEntry::resetPlaybackDefaults()`.
* Sidebar refreshes immediately after Apply / Confirm via an "optimistic
  refresh" pattern in `MainComponent` (merges the form values into the
  display snapshot rather than waiting for the next GL-thread roundtrip),
  plus a new `onBlockPropertiesChanged` callback fired from the GL thread
  drain.

### 6.7 3D arrow gizmos & grid plane toggles (Phase 2)

**Files:** `Source/Renderer.h/.cpp`, `Source/ViewPortComponent.h/.cpp`,
`Source/MainComponent.h/.cpp`.

Blender-style red/green/blue arrows extending from the selected block.  Click
+ drag along an axis moves the block in integer steps.

Implementation:

* `Renderer::buildArrowMeshes()` generates a cylinder shaft + cone tip mesh
  once at init.  `renderArrow(axis, origin)` colors and rotates it.
* `ViewPortComponent` adds `gizmoHoveredAxis_`, `gizmoActiveAxis_`,
  `gizmoDragSerial_`, `gizmoDragOrigPos_`, `gizmoDragStartCoord_`, and a
  message-thread `PendingGizmoDrag` request that the GL thread drains.
* `pickGizmoAxis()` runs a skew-line solve (mouse ray vs three axis lines).
* `projectRayOntoAxis()` projects the live drag onto the active axis to
  derive a delta in grid units.

Companion grid-plane toggles let you show / hide individual XZ (Floor), YZ
(Wall X), and XY (Wall Z) planes.  Internally there are now three separate
mesh VAOs and three `renderPlaneXZ/YZ/XY` calls.

UI: five toggle pills sit between the type toolbar and the View menu:
`Floor` (on by default), `YZ Wall`, `XY Wall`, `Arrows`, `Doppler` (all
off by default).

### 6.8 Audio analysis: frequency & oscilloscope (Phase 3)

**Files (new):** `Source/AudioAnalysis.h`, `Source/AudioAnalysis.cpp`.
**Files (modified):** `Source/SidebarComponent.h/.cpp`,
`Source/ViewPortComponent.cpp` (analyze hook).

`AudioAnalysis::analyze(const juce::AudioBuffer<float>&, double sampleRate)`
returns:

```cpp
struct AudioAnalysisResult {
    bool   valid;
    float  fundamentalHz;      // 0 if unknown / percussive
    bool   pitchReliable;
    juce::String noteName;     // e.g. "A4"
    juce::String pitchLabel;   // human-readable line
    double durationSec, sampleRateHz;
    std::vector<float> waveformMin, waveformMax;  // 128-column envelope
};
```

The estimator mixes the buffer to mono, normalises, and runs autocorrelation
within a musically reasonable lag window (≈ 60–1500 Hz).  `pitchReliable` is
false for noise-like / very short clips so we don't flash misleading numbers
for percussion.

The Info panel renders a "Pitch / note / duration / period" text line
followed by a filled blue oscilloscope graph drawn from the min/max envelope.
`SidebarComponent::audioWaveformGraph()` does the painting.

Hooked up via `MainComponent`'s `sidebar.setAudioAnalyzer(view.analyzeBlockAudio)`
so the sidebar can lazily request analysis without owning a reference to the
sample library.

### 6.9 Doppler effect (Phase 4)

**Files:** `Source/SequencerEvent.h`, `Source/SequencerEngine.cpp`,
`Source/AudioEngine.h/.cpp`, `Source/SceneAudioExporter.cpp`,
`Source/MainComponent.h/.cpp`, `Source/ViewPortComponent.h/.cpp`.

Standard source-motion Doppler.  The implementation pipeline:

1. **SequencerEngine** computes per-keyframe velocity
   (`computeKeyframeVelocity()` — segment vector / playback Δt) and stamps
   `velocityX/Y/Z` + `hasVelocity` on `Start` and `Movement` events.
2. **AudioEngine** carries `posX/Y/Z` + `velX/Y/Z` on each `ActiveVoice` and
   reads `listenerX/Y/Z_` + `dopplerEnabled_` atomics each event.
3. `computeDopplerRate` returns
   `c / (c - v_dot_r̂)`
   where `c` is `speedOfSound_` (default 343 m/s) and `r̂` is the unit vector
   from source to listener.  Clamped to `[0.25, 4.0]` so weird velocities
   don't blow speakers.
4. The audio mix loop's per-sample step multiplies in `voice.dopplerRate`
   alongside `pitchRate` and `blockRate`.
5. **ViewPortComponent::renderOpenGL** feeds the camera position to
   `audioEngine.setListenerPosition(...)` every frame.
6. **Toolbar** has a `Doppler` toggle (off by default — the user wants more
   tuning before relying on it).
7. **Export** mirrors the same calculation but uses listener at origin; the
   exporter passes `dopplerEnabled=false` by default so files match the live
   no-Doppler experience.

### 6.10 Sidebar info-panel scrolling

**Files:** `Source/SidebarComponent.h/.cpp`.

The Info panel grew enough rows that movement / waveform graphs were
off-screen.  Added an `infoScrollY_` offset, an `infoContentBottomY_` height
metric, and a `mouseWheelMove` handler.  All `resized()` editor placements
and `paint()` label / graph draws apply the offset; off-screen editors get
`setBounds(0,0,0,0)` so they don't intercept clicks.  A 6-pixel-wide
right-edge scrollbar shows scroll progress.

`showBlockInfo` and `clearSelectedBlock` reset `infoScrollY_` to 0 so opening
a different block always starts at the top.

### 6.11 Pause / Stop immediacy & seek-to-time visual sync

**Files:** `Source/AudioEngine.h/.cpp`, `Source/SequencerEngine.h/.cpp`,
`Source/ViewPortComponent.h/.cpp`.

Two atomics on `AudioEngine`:

* `audioPaused_` — `getNextAudioBlock` outputs silence without advancing any
  voice state.  Resume picks up at the exact sample positions.
* `killAllVoices_` — `getNextAudioBlock` clears `activeVoices_` once then
  resets the flag.

`ViewPortComponent::transportPause()` sets `audioPaused_`,
`transportPlay()` clears it, `transportStop()` sets `killAllVoices_` + clears
`audioPaused_`.

`SequencerEngine::snapBlockPositionsToTime(blocks, t)` is a new static helper
that walks each block's recorded path (honouring `movementDurationSec` and
`movementYOffset`) and snaps `b.pos` to the latest keyframe ≤ t.  Called by:

* `ViewPortComponent::seekTransportClock` whenever the timeline is scrubbed,
* `ViewPortComponent::transportStop` for the Stop-button reset.

Both call sites also clear / rebuild `voxelGrid` so movement snapping doesn't
desync collision queries.

### 6.12 Auto duration on sound assignment

**File:** `Source/ViewPortComponent.cpp` (`pendingBlockEdit_` drain).

When the user assigns a new sound to a block:

* If `b.durationLocked` (typically set after movement confirm) → leave
  duration alone.
* Else, resolve the sample's natural length from the audio engine and set
  `b.durationSec = max(natDur, movDur)`.  When the block has a movement path
  with `movementDurationSec == 0` (defaulting), the previous span is pinned
  into `movementDurationSec` first so growing the region doesn't stretch the
  recorded path.

Effect: assigning a 25-second sound to a block with a 15-second movement
results in a 25-second region; the path still plays in 15 s, and the audio
continues for the remaining 10 s at the final keyframe — without the user
ever pressing a "play past movement" button.

A `Match Duration to Sound` button in the Info panel triggers the same logic
on demand for blocks whose duration the user changed manually.

### 6.13 Per-block mute, hide; per-type View / Mute toolbar filters

**Files:** `Source/BlockEntry.h`, `Source/SequencerEngine.cpp`,
`Source/ViewPortComponent.h/.cpp`, `Source/SidebarComponent.h/.cpp`,
`Source/MainComponent.h/.cpp`, `Source/SceneFile.cpp`,
`Source/SceneAudioExporter.cpp`.

#### Per-block toggles (Info panel)

* `isMuted` — sequencer skips emitting `Start` / `Stop` for this block.  Note
  that the **block's state machine still ticks** (so `hasStarted`, the
  keyframe iterator, and movement events all fire normally).  This is what
  makes muted blocks still animate — see `processOccurrence(..., isMutedNow,
  ...)` and the `update()` loop in `SequencerEngine.cpp`.
* `isHidden` — renderer skip in `ViewPortComponent::renderOpenGL` (both the
  solid block draw and every highlight pass).  Block is still selectable in
  raycasting only if visible.

#### Per-type filters (toolbar)

* `View ▾` menu — toggles `blockTypeVisible_[t]` for each type.  Grouped by
  category (Synth, Strings, Woodwinds, Brass, Percussion, Special), with
  "Show All / Hide All" shortcuts.  Visibility is consulted by every
  highlight / mesh draw in `renderOpenGL`.
* `Mute ▾` menu — toggles `blockTypeMuted_[t]` for each type.  Each GL frame,
  the viewport composes `b.effectiveMuted = b.isMuted ||
  isBlockTypeMuted(b.blockType)` for every block.  The sequencer reads
  `effectiveMuted` instead of `isMuted` for its indefinite-mute test.

Both filter arrays are `std::array<std::atomic<bool>, BlockType::_Count>` on
the viewport.  They are **transient** — never persisted to `.sime` and
explicitly skipped by the exporter.

#### Live transitions

When the per-block `isMuted` toggle flips on for a currently-playing block,
the GL drain emits a synchronous `Stop` event so the user hears the change
immediately.

### 6.14 Loop overhaul (toggle move, length enforcement, gap)

**Files:** `Source/BlockEditPopup.h/.cpp`, `Source/SidebarComponent.h/.cpp`,
`Source/SequencerEngine.cpp`, `Source/AudioEngine.cpp`,
`Source/SceneAudioExporter.cpp`, `Source/ViewPortComponent.cpp`.

Three problems were tangled together:

1. **Toggle desync.**  `BlockEditPopup` had its own `Loop` checkbox.  The
   sidebar Info panel had a `Mode` combo (Natural / Loop / Stretch / Speed).
   They wrote to two different fields (`isLooping` vs `playbackMode`) and
   the legacy fallback `if (mode == Natural && isLooping) → Loop` masked the
   inconsistency.  The user saw the popup loop toggle "turn off
   inconsistently" because the sidebar combo would later force
   `mode = Natural` and clear `isLooping` to keep them in sync.
2. **Loop length wasn't enforced.**  `loopDurationSec` was a popup field that
   nothing in the audio path read; loop always filled the region.
3. **Loop gap didn't change live.**  `loopGapSamples` is baked at `Start`;
   editing the field mid-playback had no effect on the already-running voice.

Fixes:

* Removed the Loop UI from `BlockEditPopup` entirely (its `commit()` now
  passes through the incoming `isLooping` / `loopDurationSec` unchanged so
  saving doesn't clobber them).
* Added a dedicated `Loop sound` toggle to the sidebar Info panel above the
  Loop gap row.  Toggle is the single source of truth — the GL-drain forces
  `playbackMode = Loop` / `Natural` and the legacy `isLooping` flag to match.
* Added a `Loop length (s)` editor and a `Loop = Block Dur.` button that
  one-clicks the current block duration into the editor.
* `SequencerEngine::processOccurrence` now respects loop length: when
  `loopDurationSec > 0` and `< region duration`, the audio gets a `Stop`
  event at `startTime + loopDurationSec`.  Movement keeps running through
  the region's natural end.
* GL drain detects any change to loop-related fields (`isLooping`,
  `loopDurationSec`, `loopBufferSec`) and synchronously fires a `Stop` event
  so the next sequencer tick re-fires `Start` with the new params.  The
  user hears the new gap on the very next iteration.
* `SceneAudioExporter` mirrors the same loop-gap silence logic.

### 6.15 Time-window mute

**Files:** `Source/BlockEntry.h`, `Source/SequencerEngine.cpp`,
`Source/SidebarComponent.h/.cpp`, `Source/SceneFile.cpp`.

Per-block `muteStartSec` / `muteEndSec` fields silence the block while the
playhead is inside `[muteStartSec, muteEndSec)`.  Both 0 ⇒ window disabled.

`SequencerEngine::update` computes a `windowMute` boolean each tick.  The
combined `isMutedNow = block.effectiveMuted || windowMute`.  When
`isMutedNow != block.wasMutedLastTick`:

* If the transition is **off → on** while the region is active, emit a
  synthetic `Stop` event (cut the voice).
* If the transition is **on → off** while the region is active, emit a
  synthetic `Start` event (resume — voice restarts from the sample's
  beginning at the block's current position).

Inside `processOccurrence`, audio events are gated by the same
`isMutedNow` flag — natural `Start` / `Stop` pushes are suppressed while
muted, but the block's state machine (hasStarted / hasFinished /
keyframe iterator) keeps progressing.

Persisted in `.sime` v8.

---

## 7. Bug log

| # | File | Symptom | Fix |
|---|------|---------|-----|
| B1 | `MainComponent.cpp` | `warnAboutOverwritingExistingFiles` undeclared | Use `juce::FileBrowserComponent::warnAboutOverwriting`. |
| B2 | build | `C1041` PDB lock during parallel Debug | Use `--parallel 1` for Debug. |
| B3 | `SceneAudioExporter.cpp` | `getPossibleSampleRates` is non-const | Drop the `const` qualifier on the `juce::AudioFormat*`. |
| B4 | `ViewPortComponent.cpp` | HUD pill rendered with right-half "washed off" | Full GL state reset at end of `renderOpenGL`. |
| B5 | `ViewPortComponent.cpp` | "Block 251" after New Scene | Reset `nextSerial = 1` in `clearScene()`. |
| B6 | `Main.cpp` | New Scene loaded the previous scene | Removed silent `autosave.sime` autoload. |
| B7 | `SidebarComponent.cpp` | Info panel persisted after block deletion / New Scene | `clearSelectedBlock` + `clearSelectedBlockIfSerial`. |
| B8 | `ViewPortComponent.cpp` | No green hover; LMB always placed | `doRaycast` sets `hoveredBlockSerial_`; LMB on hovered = select. |
| B9 | `ViewPortComponent.cpp` | Enable Recorded Movement toggle had no effect | New `movementEnabled` flag honoured by sequencer. |
| B10 | `MainComponent.cpp` | Sidebar Info panel didn't refresh after Apply / Confirm | Optimistic merge + `onBlockPropertiesChanged`. |
| B11 | `BlockEditPopup.h` | "Browse…" ellipsis truncated to "Browse F…" on HiDPI | Renamed to "Browse File", widened to 100 px. |
| B12 | build | `LNK1104` cannot open `SIME.exe` | Kill the running process before rebuilding. |
| B13 | `ViewPortComponent.h` | `std::array<std::atomic<bool>, N>` can't be copied | Default-init the array; populate in ctor body. |
| B14 | `ViewPortComponent.h` | `BlockType::Custom + 1` was the wrong size | Use `BlockType::_Count`. |
| B15 | `SequencerEngine.cpp` | Muted blocks didn't animate (`continue` in `update()`) | Removed early continue; gate event pushes inside `processOccurrence` via `isMutedNow`. |
| B16 | `SidebarComponent.cpp` | Loop toggle state would "turn off" after subsequent edits | Removed popup loop toggle; Info panel toggle is single source of truth; GL drain forces `playbackMode` + `isLooping` to agree. |
| B17 | `AudioEngine.cpp` | Loop gap edits didn't apply to a playing voice | GL drain emits synthetic `Stop` on loop-related field change so next tick re-bakes voice params. |

---

## 8. What's still queued

The user explicitly deferred these, in roughly this priority order:

1. **Click-and-drag highlight** for multi-block selection + bulk
   mute / hide.  All the per-block primitives now exist; this is a
   rectangle-select layer over the existing raycast logic.
2. **User-adjustable frequency** for blocks (currently the analyzer is
   display-only).
3. **Different sounds at different times** on the same block (would change
   the block-as-a-single-sample data model).
4. **Position keyframes** as an alternative to recorded movement (assign
   precise position at time t).
5. **Block-type change mid-movement** (e.g. violin → piano halfway through
   the path).

---

## 9. How to extend

### Add a new audio format to export

1. Extend `SceneAudioExporter::Format` enum.
2. Add a case to `SceneAudioExporter::createWriter` for the new
   `juce::AudioFormat` subclass.
3. Add an entry to `ExportAudioDialog`'s format combo.
4. Update `MainComponent::showExportAudioDialog` if the format requires
   additional UI (bitrate, etc.).
5. If the format only supports certain sample rates, `pickWriterSampleRate`
   already handles the fallback — verify behaviour by exporting at 44100 and
   48000.

### Add a new per-block UX flag

1. Add the field to `BlockEntry`.  If transient, do **not** persist it (mark
   "runtime-only" in the comment).
2. If persisted, bump `SceneFile::kVersion` and add additive trailing
   read/write.  Always read with a version gate and fall back to a default.
3. Add the editor / toggle to `SidebarComponent` (`resized()` + `paint()`
   layout, plus reset in `clearSelectedBlock` and hydrate in
   `showBlockInfo`).
4. Widen `SidebarComponent::onApplyBlockInfo` signature.
5. Update `MainComponent::onApplyBlockInfo` wiring.
6. Widen `ViewPortComponent::PendingSidebarEdit` + `applySidebarBlockInfo`.
7. Apply the field in the GL drain block (search for
   `pendingSidebarEdit_.active`).
8. If the flag affects audio, plumb it through `SequencerEvent` →
   `AudioEngine::handleStartEvent` → `ActiveVoice`.  Mirror in
   `SceneAudioExporter` so exports stay consistent.

### Add a new per-type filter (alongside View / Mute)

1. Add `std::array<std::atomic<bool>, kNumBlockTypes>` to
   `ViewPortComponent` and initialise in the ctor body.
2. Add `setBlockTypeXxx` / `isBlockTypeXxx` accessors.
3. Add a `Xxx ▾` `TextButton` to `MainComponent`, follow the existing
   `showViewMenu()` / `showMuteMenu()` pattern.
4. Consume the filter where appropriate:
   - render-side → check in `renderOpenGL`.
   - sequencer-side → fold into `effectiveMuted` (or add a new transient
     flag) and consume in `SequencerEngine::update`.
   - persistence → leave transient (filters are view conveniences, not
     musical intent).

### Add another playback mode (e.g. PitchedLoop)

1. Extend `BlockPlaybackMode` enum.
2. Add a switch case in `computeBlockRate()` in `SequencerEngine.cpp`.
3. Decide whether the audio thread needs new behaviour (it already handles
   loop wrap + gap; if you need pitch-preserving stretch, that's a DSP
   change in `AudioEngine`).
4. Add an entry to the Info panel `modeCombo_`.
5. Bump persistence if you change `BlockPlaybackMode`'s underlying integers
   (don't reorder existing values).

---

*— Generated for the 2026-05-23 session.  Owner: Claude.*
