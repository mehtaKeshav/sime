# Spatial Perception & Camera-Path Overhaul — Engineering Report

**Sessions covered:** 2026-06-01 (single long day, three feedback rounds)
**Scope:** End-to-end rework of how SIME presents and bakes "where the
listener is in the scene" — from per-block default sounds, through
camera-relative spatial audio, the audio anchor, A→B distance
measurement, the camera (listener) keyframe path, scrub-time preview,
free-cam override, transport-bar typed seek, toolbar declutter, and the
matching block-movement granularity controls.  Replaces / supersedes
[`CAMERA_PATH_REPORT.md`](CAMERA_PATH_REPORT.md), which captured an
intermediate state mid-session.

This report is intentionally exhaustive — it is the single doc a future
contributor (or future me) should read before touching anything in the
listener / camera-path code paths.

---

## 1. Context and user-facing problem statement

Before this overhaul SIME had three independent gaps:

1. **No "default sound" for a freshly placed block.**  Every new voxel
   started silent until the user opened the edit popup and picked a
   sample.  Demos took forever.
2. **The "spatial mix" was origin-relative.**  Pan/gain were derived from
   `(blockX, blockY, blockZ)` against the world origin, not against the
   camera, so flying around the scene didn't change what the user
   actually heard.  The audio was technically spatial but perceptually
   static.
3. **No way to choreograph the camera/listener** — you had to manually
   fly the camera during playback, and the offline exporter always
   baked from a fixed origin pose regardless of what was on screen.

The user's verbatim ordering for the first pass was:

> Default sounds — quick win for demos
> Camera-relative audio + sensitivity tuning — fixes the heart of the problem
> Anchor button — builds on (2)
> Distance picker + dB/distance in sidebar — measurement UX on top of (2)
> [...then] the ambitous feature [camera path] last

After that came two follow-up rounds:

* **Round 1** (popup polish): "weird text in help / Clear and × don't
  work / R doesn't work / Path On is confusing / I want to type a time
  / record should work while paused".
* **Round 2** (camera scheduling): "+ Hold @ cam now should add at the
  next slot / Hold needs a duration / recording every 50 ms is way too
  many keyframes / drag the timeline should preview the camera / I want
  a Free Cam override / block movement should have the same granularity
  control".

Everything below is the technical write-up of how those three rounds
were addressed.  The work touches ~12 files and bumps the `.sime`
format twice (skipped v10 was the camera-path block, v11 added
`holdDurationSec` per keyframe).

---

## 2. Phase 1 — Default sounds on placement

### Problem
`mouseClick` → place block → block sits silent.  User has to enter edit
mode, RMB, find a sample.

### Fix
* `SoundLibrary::defaultSoundForBlockType(BlockType, BlockColor)` —
  returns the first sample whose category matches the placed block
  type.  Falls back to "no sound" if the library is empty.
* `ViewPortComponent::renderOpenGL()` calls this at placement time
  (inside the `pendingOps` drain) so a brand-new block has a sensible
  `soundId` before the next audio tick.

### Result
Placing a violin now plays a violin note.  Placing a piano plays a
piano note.  Demo lift-off without any side panel interaction.

---

## 3. Phase 2 — Camera-relative spatial mix

This is the heart of the round.  Everything else in this report stacks
on top of it.

### Problem
Pan was `blockX / sceneRadius` against the origin.  Gain was a fixed
inverse-square against the origin.  Flying around the scene didn't
change what you heard.

### Architecture change
A new **listener pose** lives on `AudioEngine`:

```
listenerX/Y/Z          (position, atomic floats)
listenerFwdX/Y/Z       (forward direction)
listenerRightX/Y/Z     (right direction)
spatialSensitivity_    (0.25 = gentle … 3.0 = aggressive)
```

`ViewPortComponent::renderOpenGL()` feeds these every frame from
either the live camera or the saved anchor pose (see §4).

### Shared static math
All spatial calculations route through one function so live mixing
**and** offline export use exactly the same numbers:

```cpp
static void AudioEngine::computeSpatialGainsStatic(
    float lx, float ly, float lz,            // listener pos
    float fwdX, float fwdY, float fwdZ,      // listener forward
    float rightX, float rightY, float rightZ,// listener right
    float sensitivity,                       // 0.25 … 3.0
    float srcX, float srcY, float srcZ,      // source pos
    float& outGain,                          // 0 … 1
    float& outPan,                           // -1 … +1
    float& outPitchRate,                     // for doppler hook
    float& outLeft, float& outRight) noexcept;
```

Output behaviour:

* **Distance gain** uses an inverse-distance model with a
  `sensitivity` exponent so the user can tune "do I hear distant
  blocks loud or do they vanish?".
* **Pan** is the source's projection onto the listener's right vector
  (so turning left/right repans the world in your headphones).
* **Front/back attenuation** — sources behind the listener get a
  ~−3 dB pad so a U-turn doesn't sound identical to facing forward.
* **Pitch hook** for the Doppler toggle.

### Sensitivity slider
A live `juce::Slider` (range 0.25 – 3.0, default 1.0) sits on the
toolbar and writes directly to the atomic.  No restart, no recompile.

### Affected files
* `Source/AudioEngine.{h,cpp}`
* `Source/ViewPortComponent.cpp` (feeds listener pose)
* `Source/MainComponent.cpp` (sensitivity slider)

---

## 4. Phase 3 — Audio anchor

### Idea
"Freeze the listener pose at the current camera, but let me keep
flying around freely to scout shots."

### Implementation
* `setAudioAnchorEnabled(bool)` — message-thread entry point.
* `pendingAnchorOp_` (atomic) → GL thread reads in `renderOpenGL`.
* When toggled ON, saves the live camera pose into
  `savedCameraPos_ / savedCameraLookAt_` and starts feeding **those**
  numbers to `AudioEngine`.  Toggling OFF goes back to live camera.
* The toolbar **Anchor** pill reflects state and tints when active.

### Affected files
* `Source/ViewPortComponent.{h,cpp}`
* `Source/MainComponent.{h,cpp}`

---

## 5. Phase 4 — A→B distance measurement (sidebar SPATIAL panel)

### Idea
A "tape measure" mode in the sidebar: click block A, click block B,
get the distance and the listener-relative dB those two blocks
differ.

### Implementation
* Sidebar SPATIAL section: two lines —
  * **From listener:** `12.40 m, -8.3 dB` — live for the selected block.
  * **A→B:** `5.12 m, dB difference 4.2` — measured.
* `SidebarComponent::onRequestDistancePick(int anchorSerial)` →
  message-thread callback wired in `MainComponent`.
* `ViewPortComponent::beginDistancePick(int)` /
  `cancelDistancePick()` / `isDistancePickActive()` — atomic flag the
  GL thread inspects on the next click.
* On the second click, GL thread computes:
  * Euclidean distance between the two voxels.
  * `AudioEngine::measureSourceAt(x, y, z)` at both positions and
    diffs them in dB.
* Result published via `onDistanceMeasured(...)` back up to the
  sidebar's `setBlockDistanceReadout()`.

### Why route through GL thread
Both the click ray-pick and the listener pose live there, and
re-computing the dB needs the same `computeSpatialGainsStatic`
inputs that the audio thread is consuming.

### Affected files
* `Source/ViewPortComponent.{h,cpp}`
* `Source/SidebarComponent.{h,cpp}`
* `Source/MainComponent.{h,cpp}`
* `Source/AudioEngine.{h,cpp}` — `SpatialReadout` + `measureSourceAt`.

---

## 6. Phase 5 — Highlight separation (orange vs green vs cyan)

### Problem
Both the selected block **and** the currently-playing block highlighted
green.  When a selected block began emitting, the user couldn't tell
which highlight was which.

### Fix
* **Primary selection:** orange (`Vec3f{ 1.00f, 0.50f, 0.10f }`),
  outline + glow.
* **Currently playing:** green pulse (unchanged).
* **Multi-selection (Ctrl+A / rubber-band):** cyan
  (`Vec3f{ 0.25f, 0.75f, 1.00f }`).

Three highlights, three colours, no overlap.  Help popup explains the
mapping.

### Affected files
* `Source/ViewPortComponent.cpp`
* `Source/HelpPopup.cpp` (legend in help)

---

## 7. Phase 6 — Export honors the listener pose

### Problem
`SceneAudioExporter` used a fixed pose (`(0, 0, 0)` looking down −Z).
Anchoring or flying the camera live had zero effect on the bounced WAV.

### Fix
* `SceneAudioExporter::ListenerPose` (new struct):
  ```
  bool   anchored;          // if false, exporter uses (x, y, z, fwd, right) as-is
  float  x, y, z;           // listener position
  float  fwdX/Y/Z, rightX/Y/Z;
  float  spatialSensitivity;
  bool   pathFollow;        // overrides the above when true
  std::vector<CameraKeyframe> cameraPath;
  ```
* `bounceToFile(...)` now takes the pose by reference and:
  * Calls `resolveListener(...)` once per audio chunk.
  * For sustained voices (loops, long releases), re-mixes the
    listener gains every chunk so a moving listener actually moves
    in the file, not just at note-start.
  * Reuses `AudioEngine::computeSpatialGainsStatic` so the offline
    and live numbers are bit-identical.

### Export dialog
`ExportAudioDialog` got a new "Listener" header that shows one of
three modes before bounce:

* **CAMERA PATH active (N keyframes, T0 → T1 s).** *(green)*
* **ANCHORED at (x, y, z).** *(blue)*
* **Anchor not set. Export will use the current camera pose…**
  *(yellow warning)*

So the user **knows** what they're baking before they hit Export.

### Affected files
* `Source/SceneAudioExporter.{h,cpp}`
* `Source/ExportAudioDialog.{h,cpp}`
* `Source/MainComponent.cpp` (assembles `ListenerPose` from the view)

---

## 8. Phase 7 — Camera path data model

The data is small but it has to round-trip cleanly through the message
thread → GL thread → audio export pipeline, so the schema matters.

### `Source/CameraPath.h`

```cpp
struct CameraKeyframe
{
    enum Mode : uint8_t { Hold = 0, Lerp = 1 };

    double  timeSec          = 0.0;        // scene time
    Vec3f   pos              { ... };      // listener position
    float   yawRad           = -2.3f;
    float   pitchRad         = -0.45f;
    uint8_t mode             = Hold;
    float   holdDurationSec  = 0.0f;       // v11 addition — see §10
};

struct CameraPose { Vec3f pos; float yawRad; float pitchRad; };

namespace CameraPathUtil
{
    void       sortByTime(std::vector<CameraKeyframe>&);
    double     endTime(const std::vector<CameraKeyframe>&);
    double     effectiveEndTime(const std::vector<CameraKeyframe>&);   // v11
    CameraPose sample(const std::vector<CameraKeyframe>&,
                       double t,
                       const CameraPose& defaultPose);
    void       poseDirs(const CameraPose&, Vec3f& outFwd, Vec3f& outRight);
}
```

### Sample math
* Empty path → `defaultPose`.
* Before first keyframe → first keyframe's pose.
* After last keyframe → last keyframe's pose (no extrapolation).
* Between two keyframes where the left one is **Hold** → return the
  Hold pose verbatim (instant cut at the next keyframe time).
* Between two **Lerp** keyframes → linear lerp on position, **shortest-
  arc** lerp on yaw (so wrapping ±π doesn't unwind through a full
  circle), linear lerp on pitch.

### Hold duration semantics
`holdDurationSec` does **not** change `sample()` behaviour — a Hold
still freezes the pose until the next keyframe time regardless of its
duration field.  Duration is purely a **scheduling** hint used by
`effectiveEndTime()`:

```cpp
inline double effectiveEndTime(const std::vector<CameraKeyframe>& p)
{
    if (p.empty()) return 0.0;
    const auto& last = p.back();
    if (last.mode == CameraKeyframe::Hold && last.holdDurationSec > 0.f)
        return last.timeSec + (double) last.holdDurationSec;
    return last.timeSec;
}
```

That's what powers "+ Hold @ cam now lands at the next free slot" —
the button uses `effectiveEndTime(draft)` for the new keyframe's time
instead of dumping at `t=0`.

---

## 9. Phase 8 — Camera-path popup (`CameraPathPopup`)

### UI layout

```
┌─────────────────────────────────────────────────────────────────┐
│ Camera Path                                                     │
│ Hold = freeze pose until next keyframe (cut). Lerp = smooth.    │
│                                                                 │
│ TIME (s)  X   Y   Z   YAW   PITCH  MODE   HOLD (s)              │
│ [____]    []  []  []  [_]   [_]    [Hold▾] [____]   [×]         │
│ [____]    []  []  []  [_]   [_]    [Lerp▾] [-  ]    [×]         │
│ …rows…                                                          │
│                                                                 │
│ Tip: press R anywhere to start/stop recording (playing or paused│
│ + Hold @ cam now uses the next free slot; HOLD (s) controls     │
│ how long a Hold owns the timeline.                              │
│                                  Capture every: [Every 1 s  ▾]  │
│ [+ Hold @ cam now] [Record (R)] [Clear All]                     │
│ [    Cancel    ]   [          Apply           ]                 │
└─────────────────────────────────────────────────────────────────┘
```

### Source-of-truth model
The popup is **authoritative** while open.  The host's path is only
re-fetched on the recording state edge `true → false` (i.e. the user
just stopped recording, and the splice happened on the GL thread).
Manual edits, deletes, Clear All, and "+ Hold @ cam now" stay put
until the user presses Apply (or Cancel to throw the draft away).

This fixed the "Clear / × don't actually clear" bug from the first
follow-up — the old version polled the host every 100 ms and
overwrote the draft any time the sizes differed.

### Callbacks (host wires these in `MainComponent::showCameraPathPopup`)

| Callback                     | Used by                              |
|------------------------------|--------------------------------------|
| `onApply(path)`              | Commit + close                        |
| `onCommitDraft(path)`        | Commit without closing (pre-record)  |
| `onDismiss()`                | Cancel/Esc                            |
| `onRecordToggle()`           | Record button → host R toggle         |
| `isRecording() → bool`       | Polled @ 10 Hz for record-button UI  |
| `fetchLivePath() → vec`      | Called once on record-end edge        |
| `getCurrentCamPose() → Pose` | "+ Hold @ cam now" — pose snapshot   |
| `getCurrentPlayheadSec()`    | "+ Hold @ cam now" — fallback time   |
| `getCaptureIntervalSec()`    | Sync dropdown to host on open         |
| `onCaptureIntervalChanged()` | Write dropdown → host atomic          |

### Per-row Hold(s) editor
The `HOLD (s)` text editor on each row is enabled / interactive only
when `mode == Hold`; switching the row's mode combo to `Lerp` greys
it out and writes `-` (display only — read as 0 on commit).  Editing
a Lerp row's HOLD field is a no-op.

### Capture-interval dropdown
Six options: `0.1 / 0.25 / 0.5 / 1 / 2 / 5 s`.  Default **1 s**.
This is the granularity at which `ViewPortComponent`'s recording
loop samples the live camera pose.  Lowering it gives smoother
recordings at the cost of more keyframes; raising it produces a
manageable list (the original hardcoded 20 Hz / 0.05 s rate
generated ~600 keyframes for a 30-second flight, which the user
fairly called "way too specific").

---

## 10. Phase 9 — Recording, R-anywhere, wall-clock timing

### Where it lives
`ViewPortComponent::renderOpenGL()` — runs on the GL thread, drains
`pendingPathOps_`, owns the recording state machine.

### State

```cpp
bool   recordingActive_;
double recordingStartSec_;        // playhead at record start
double recordingStartWallSec_;    // wall-clock at record start
double recordingLastCaptureWall_;
std::atomic<double> cameraRecordIntervalSec_{ 1.0 };
std::vector<CameraKeyframe> recordingBuffer_;
```

### Per-frame loop

```cpp
if (recordingActive_)
{
    const double wallNow      = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double captureEvery = cameraRecordIntervalSec_.load();
    if (wallNow >= recordingLastCaptureWall_ + captureEvery)
    {
        // Scene time: ride the transport when playing, else
        // (playheadAtStart + wall-clock elapsed since record start).
        const double effTime = transportClock.isPlaying()
            ? transportClock.currentTimeSec()
            : (recordingStartSec_ + (wallNow - recordingStartWallSec_));

        recordingBuffer_.push_back({
            effTime,
            camera.getPosition(),
            camera.getYaw(),
            camera.getPitch(),
            CameraKeyframe::Lerp,
            0.0f   // holdDurationSec — only meaningful for Hold rows
        });
        recordingLastCaptureWall_ = wallNow;
    }
}
```

### Why dual clock
The user explicitly asked for "record at a specific time, regardless
of whether music is playing".  The dual-clock setup lets them:

* Scrub the transport to `00:08`.
* Position the camera where they want the shot to start.
* Hit R.
* Fly the camera for 5 wall-clock seconds.
* Hit R again.

Result: a recording that spans `08.00 → 13.00 s` in the timeline,
even though the transport never advanced.  When the transport **is**
playing, captures use the transport clock so playback rate (2x, 3x)
behaves correctly.

### Splice on stop
`PendingPathOp::StopRecord` handler in `renderOpenGL`:

1. Take the recording window `[startTime, endTime]`.
2. Drop every existing keyframe whose time falls inside the window.
3. Insert `recordingBuffer_` keyframes.
4. Re-sort.
5. Atomically replace `cameraPath_` and set `cameraPathHasAny_`.

### R-anywhere routing
The R hotkey is bound at **two** levels:

* `ViewPortComponent::keyPressed` — for the common case of viewport
  focus.
* `MainComponent::keyPressed` — fallback for when the popup,
  sidebar, or any other descendant has keyboard focus.

A focused TextEditor still eats R as text (correct — that's typing).
Anywhere else, R toggles recording.

---

## 11. Phase 10 — Live preview while scrubbing

### Old behaviour
The path-follow code required `transportClock.isPlaying()`.  Drag the
timeline while paused and the camera stayed put — no idea what the
path was going to do.

### New behaviour

```cpp
if (cameraPathFollowEnabled_.load()
    && cameraPathHasAny_.load()
    && !freeCameraOverride_.load()
    && !recordingActive_)
{
    const auto p = CameraPathUtil::sample(snap,
                                          transportClock.currentTimeSec(),
                                          defaultPose);
    camera.setPosition(p.pos);
    camera.setYawPitch(p.yawRad, p.pitchRad);
}
```

The `isPlaying()` guard is gone.  Dragging the timeline → typing a
time in the transport field → cycling the playhead from the keyframe
popup all give an immediate camera preview, mirroring how the per-
block keyframe popup previews block positions while scrubbing.

---

## 12. Phase 11 — Free Cam override

### Why it exists
The user wanted to scout shots **while** a path is loaded without
having to clear or temporarily delete it.

### Implementation
* `std::atomic<bool> freeCameraOverride_{ false };` on
  `ViewPortComponent`.
* Toolbar pill **Free Cam** writes to it.
* The follow-loop above guards on `!freeCameraOverride_`.
* The path data is preserved and **still used** for the listener pose
  inside `SceneAudioExporter` — so toggling Free Cam doesn't change
  what gets baked into the WAV, only what's on the live viewport.

This is also the answer to the user's "what was Path On for again?"
question.  Free Cam replaces the old Path On toggle with a clearer
name and a more intuitive default (off = "follow", which is what most
sessions want).

---

## 13. Phase 12 — Block movement: Snap-times dropdown

### Why
Carry the camera-path granularity story over to per-block movement so
the editing workflow feels consistent.

### Implementation
`KeyframeEditorPopup` (the existing per-block keyframe editor) gets a
right-aligned `Snap times: [Off | 0.5 s | 1 s | 2 s | 5 s]` combo above
its action buttons.  On change, every row's `timeSec` rounds to the
chosen grid via `std::round(t / grid) * grid` and the rows rebuild.

### Default
**Off** — so opening an existing scene doesn't silently re-quantize
anything.  Pick `1 s` for the "show every second" view.

---

## 14. Phase 13 — Transport bar: typed time input

### Behaviour
* Click the time readout in the transport bar → it becomes editable.
* Type plain seconds (`10`), `M:SS` (`1:23`), or `H:MM:SS`
  (`0:01:23`).
* Press Enter → `onPlayheadMoved(seconds)` → `view.seekTransportClock`.
* Esc → restore.
* Focus loss commits as well (so click-elsewhere doesn't lose the
  edit).

### Why not just a slider
The user wanted both — dragging the timeline still works as before;
the typed input is for "snap me to exactly 10 s" without having to
nudge.

### Implementation note
`syncTimeDisplay()` only overwrites the editor text when the editor
**doesn't** have keyboard focus — so the running timer doesn't fight
the user mid-type.

---

## 15. Phase 14 — Toolbar declutter

### Before
11 controls left-of-centre plus a slider, plus 4 menus right-of-centre:

```
[Type pill][Type ▾] [Floor][YZ Wall][XY Wall][Arrows][Doppler][Anchor][Path On][Path…][Spatial━━]    [Mute ▾][View ▾][File ▾][Help]
```

### After
5 controls left-of-centre plus a slider, same 4 menus right:

```
[Type pill][Type ▾] [Layers ▾][Doppler][Anchor][Path…][Free Cam][Spatial━━]    [Mute ▾][View ▾][File ▾][Help]
```

* **Layers ▾** — single popup menu with `Floor` / `YZ Wall` / `XY
  Wall` / `Move arrows` as ticked items.
* **Path On removed** — the camera auto-follows when a path exists;
  Free Cam is the override.
* **Free Cam** — new pill, off by default.

The per-block-type Mute / View / File / Help menus stayed put.

---

## 16. Phase 15 — Help popup

A modal `?` popup invoked from the toolbar **Help** button.  It uses
a `juce::AttributedString` so we can colour the keyword and the
description differently.

Recently fixed:

* Title: replaced an em-dash that was rendering as `â…` with a plain
  hyphen.
* Camera control: corrected from "LMB drag" to **RMB drag** (the
  earlier draft had the wrong button — actual code in
  `ViewPortComponent::mouseDrag` rotates on `isRightButtonDown()`).
* Added the highlight-colour legend (orange / cyan / green).
* Documented all the new bits: Free Cam, typed time field, Layers
  menu, Capture every dropdown, HOLD (s) column, R-works-paused.

---

## 17. Phase 16 — Persistence (`.sime` v11)

### Format
File starts with `SIME` magic + `uint16_t version`.  Version 11 means:

* All the v1–v9 block records as before.
* Optional `CPTH` trailer:

```
'C' 'P' 'T' 'H'
uint32_t  keyframeCount
[count times]
    double  timeSec
    float   pos.x, pos.y, pos.z
    float   yawRad, pitchRad
    uint8_t mode                    ; 0 = Hold, 1 = Lerp
    float   holdDurationSec         ; v11+
```

### Backward compatibility
* **v10 file** → reader skips the missing `holdDurationSec`, defaults
  it to `0` (which means "Hold until next keyframe", matching v10
  semantics — no surprise).
* **Pre-v10 file** → no `CPTH` block at all; reader leaves the camera
  path empty, scene loads normally.
* The reader sniffs for the `CPTH` magic before consuming, so any
  pre-existing trailing bytes from older formats are safe.

### Affected files
* `Source/SceneFile.{h,cpp}` — `kVersion = 11`, new field in
  serialiser / deserialiser, version-gated.
* `Source/MainComponent.cpp` — `saveScene` / `openScene` /
  `autoSave` pass `view.getCameraPathCopy()` and call
  `view.applyCameraPath(...)` on load.

---

## 18. Bug fixes (cross-cutting)

| Bug                                            | Fix |
|-----------------------------------------------|-----|
| Em-dash in Help title rendered as `â…`         | Replaced with plain hyphen.  Same root cause as the `…` / `→` / `Δ` issue from earlier — `juce::Graphics::drawText` isn't re-encoding non-ASCII chars when the source is a `const char*` literal. |
| Camera control showed "LMB drag" in Help      | Corrected to `RMB drag` — actual binding lives in `ViewPortComponent::mouseDrag`. |
| Path popup: Clear / × didn't stick            | Killed the 10 Hz auto-poll that was overwriting the draft.  Popup is now authoritative; only re-syncs on the recording stop edge. |
| R hotkey did nothing while focus was in a popup | Bound at `MainComponent::keyPressed` in addition to `ViewPortComponent::keyPressed`.  A focused `TextEditor` still consumes R as text (correct). |
| R recording captured only while playing       | Switched recording loop to a wall-clock interval; pause-mode uses `playheadAtStart + wallElapsed` so a take spans the right scene-time window even with the transport stopped. |
| "+ Hold @ cam now" piled keyframes at t=0     | Now uses `effectiveEndTime(draft)` — successive inserts stack neatly behind the previous segment. |
| Path On toggle confused users                  | Removed.  Replaced semantically with Free Cam (default off → follow path, on → manual control). |
| Camera didn't move while scrubbing timeline   | Dropped the `isPlaying()` guard on the path-follow loop. |
| Recording produced ~600 KFs per 30-sec take   | Default capture interval is now 1 s (was 0.05).  User-tunable in the popup. |
| Help popup mentioned removed buttons          | Refreshed to describe Layers menu / Free Cam / typed time / R-anywhere / HOLD (s) column. |
| Selected vs playing both glowed green         | Selected is now orange; cyan for multi-select; green pulse reserved for "currently emitting sound". |
| Toolbar overflow on smaller screens           | 11 controls collapsed to 5 + Layers menu. |

---

## 19. Threading model

All listener/camera state lives in three threads:

| Thread          | Owns                                                           |
|-----------------|----------------------------------------------------------------|
| Message thread  | UI components, popups, callbacks, file I/O                    |
| GL render thread| `blockList`, `voxelGrid`, `cameraPath_`, `recordingBuffer_`,  `transportClock`, the live camera. |
| Audio thread    | `AudioEngine` voice list, listener atomics (write by GL, read by audio). |

### Cross-thread plumbing
* **Atomics** for single-value state (`spatialSensitivity_`,
  `freeCameraOverride_`, `cameraPathHasAny_`,
  `cameraRecordIntervalSec_`, listener pose floats…).
* **Pending-op queues** under `juce::CriticalSection` for multi-field
  edits — `pendingAnchorOp_`, `pendingPathOps_`,
  `pendingClipboardOp_`, etc.  The GL thread drains them at the top
  of `renderOpenGL()` and applies them on its own data.
* **Distance-pick result** comes the other way via the
  `onDistanceMeasured(...)` callback (message thread receives it,
  posts it into the sidebar).

This is the same pattern used elsewhere in the codebase (see the
sidebar Apply / clipboard ops in `SESSION_2026-05-23_REPORT.md`).

### Why a mutex on `cameraPath_`
Recording can splice the path mid-frame from the GL thread; the
exporter reads a snapshot of it from the message thread when the
user clicks Export.  A short-held `juce::CriticalSection
cameraPathMutex_` guards copy-out under
`getCameraPathCopy()` and replace under `applyCameraPath()`.

---

## 20. Code map

| Concern                                     | File(s)                                              |
|---------------------------------------------|------------------------------------------------------|
| Default sound on placement                  | `SoundLibrary.{h,cpp}`, `ViewPortComponent.cpp`     |
| Listener pose + spatial math                | `AudioEngine.{h,cpp}`                                |
| Listener feed (live + anchor)               | `ViewPortComponent.{h,cpp}`                         |
| Anchor toggle button                        | `MainComponent.{h,cpp}`                             |
| Distance measurement (pick + dB)            | `SidebarComponent.{h,cpp}`, `ViewPortComponent.cpp`, `AudioEngine.cpp` |
| Highlight colours                           | `ViewPortComponent.cpp` (render loop)               |
| Export listener pose                        | `SceneAudioExporter.{h,cpp}`, `ExportAudioDialog.{h,cpp}` |
| Camera-path data model                      | `CameraPath.h`                                       |
| Camera-path editor popup                    | `CameraPathPopup.{h,cpp}`                            |
| Camera-path live drive + recording          | `ViewPortComponent.{h,cpp}` (`renderOpenGL`)        |
| Free-cam override toggle                    | `ViewPortComponent.{h,cpp}`, `MainComponent.{h,cpp}` |
| Layers menu                                 | `MainComponent.{h,cpp}` (`showLayersMenu`)          |
| Block movement Snap-times                   | `KeyframeEditorPopup.{h,cpp}`                       |
| Transport typed time input                  | `TransportBarComponent.{h,cpp}`                     |
| Help popup                                  | `HelpPopup.{h,cpp}`                                  |
| Scene persistence v11                       | `SceneFile.{h,cpp}`                                  |

---

## 21. End-to-end test plan

The fastest way to confirm everything in this report still works.

### A. Default sounds + spatial mix
1. New scene.  Place a violin and a brass block on opposite sides of
   the origin.
2. Hit Play.  Both blocks emit by default — no edit popup needed.
3. Fly the camera left/right with WASD + RMB drag.  Pan in your
   headphones shifts as the blocks change relative position.
4. Scrub the **Spatial** slider from 0.25 → 3.  Hear the distant
   block fade harder at higher values.

### B. Anchor
1. Fly to a corner of the scene.  Click **Anchor** on the toolbar.
2. Toolbar pill highlights; fly elsewhere.  Sound stays "from the
   corner" — the listener didn't move.
3. Click **Anchor** again.  Pill goes off; sound jumps back to live
   camera.

### C. Distance measurement
1. Select a block (`Tab` → click).  Sidebar SPATIAL line shows
   `From listener: X m, Y dB`.
2. Click **Distance…**.  Click another block.  Sidebar shows
   `A→B: X m, dB diff Y`.
3. Fly around — the "From listener" line updates live for the
   primary selection.

### D. Highlights
1. Place a block, select it.  See orange outline + glow.
2. Hit Play.  When the block fires, see a green pulse layered on
   the orange.  Two distinct visuals.
3. `Ctrl+A` → cyan multi-select on top of everything.

### E. Camera-path basics
1. Open **Path…**.
2. Click **+ Hold @ cam now**.  Row appears with `time = 0.00 s`
   and `HOLD (s) = 2.00`.  Click it again — row #2 lands at
   `time = 2.00 s` (= row 1's `time + HOLD`, not `0` like the old
   bug).  Click a third time — row #3 lands at `4.00 s`.
   Successive inserts stack neatly behind the previous segment.
3. Edit row #3's HOLD (s) to `8`, then click **+ Hold @ cam now**
   once more.  Row #4 lands at `12.00 s` (= row #3's `4 + 8`),
   confirming the duration field flows through to the next insert.
2. Switch the bottom **Capture every** dropdown to `0.5 s`.  Hit
   **Record (R)** in the popup.  Fly the camera with WASD + RMB
   for a few seconds.  Hit R again.  Confirm new Lerp keyframes
   appear in the list with `0.5 s` spacing.
3. Delete a row with `×`.  Click **Clear All**.  Confirm the list
   stays empty — no polling-overwrite.
4. Apply → close → reopen.  The list is exactly what you applied.

### F. R from anywhere
1. Click anywhere outside the viewport (the toolbar, the sidebar
   open Info panel header — anything that's not a TextEditor).
2. Press R.  Confirm the camera-path popup's Record button lights
   up red (or, if you don't have the popup open, the next viewport
   frame starts a recording).
3. Click into the sidebar's block-name TextEditor.  Press R.
   Confirm "r" gets typed (TextEditors win — correct).

### G. R while paused
1. Make sure transport is **stopped**.  Note the playhead at
   `0:00`.
2. Scrub to `0:08` by typing `8` in the transport time field.
3. Press R.  Fly around for ~3 wall-clock seconds.  R again.
4. Open **Path…**.  Confirm the captured keyframes are timestamped
   `8.00, 8.50, 9.00, 9.50, 10.00, 10.50, 11.00 s` (or similar,
   depending on capture interval).  None of them say `0.00 s`.

### H. Scrub preview
1. Add a few Lerp / Hold keyframes that move the camera around.
2. Apply.
3. With transport stopped, drag the timeline scrubber.  The
   viewport camera teleports / lerps along the path live.

### I. Free Cam
1. With the path loaded, hit Play.  Camera follows path.
2. Click **Free Cam** ON.  Camera stays where it is; user input
   regains control.  Path data is preserved.
3. Open **Export Audio**.  Listener line still shows "CAMERA PATH
   active" — exporter ignores Free Cam.
4. Free Cam OFF.  Camera snaps back onto the path.

### J. Export bake matches live
1. Set Spatial = 1.0.  No anchor, no path.  Fly to some pose, hit
   Play, listen.
2. Click Anchor.  Open **Export Audio** — listener shows
   `ANCHORED at (…)`.  Bounce.  Open the WAV in your DAW.  Pan
   matches what you heard live.
3. Clear anchor, build a quick 2-keyframe path that flies from
   one side to the other.  Bounce again.  Open the WAV — pan now
   sweeps the way the path moves.

### K. Block movement Snap-times
1. Sidebar → Edit movement… on a block with a few keyframes.
2. Change **Snap times** to `1 s`.  Every row's time rounds to a
   whole second.  Apply.
3. Reopen → confirm the rounded times persisted.

### L. Toolbar + Help
1. Click **Help**.  Confirm title reads `SIME - Controls` (plain
   hyphen, no `â…`).  Confirm the camera row says **RMB**.
2. Click the **Layers ▾** menu.  Confirm Floor / YZ Wall / XY
   Wall / Move arrows are tickable.

### M. Persistence
1. Build a scene + camera path with mixed Hold (with various
   HOLD (s) values) and Lerp keyframes.  Save.
2. Quit, relaunch, open.  Camera path round-trips byte-perfect.
3. Open an older `.sime` file (v10).  Confirm it loads with
   `holdDurationSec = 0` on every keyframe (= "hold until next",
   matching v10 behaviour).

---

## 22. Known limitations / future work

* **Block-movement recording rate** is not currently user-tunable.
  Alt-drag emits keyframes whenever the voxel changes; the
  Snap-times control only quantises post-hoc in the editor.  A
  matching `Capture every` would be a clean follow-up.
* **Hold release semantics.**  If the user wants a Hold to "release"
  before the next keyframe (e.g. fade to free cam between two
  scheduled shots), `sample()` would need a new behaviour mode.
  Today the Hold just stays put until the next keyframe time.
* **Path scrubbing across rate changes.**  If you change playback
  rate to 2x mid-recording, the dual-clock setup keeps doing what
  the docstring says ("ride the transport when playing"), but the
  captured times will advance at 2× wall-clock, which can look
  odd.  Lock the rate before recording for predictable results.
* **No undo for camera-path edits.**  The popup's Cancel button
  discards a working draft, but once Applied there's no `Ctrl+Z`
  to roll back the path.  The block-placement undo stack doesn't
  cover camera-path mutations.

---

*End of report.*
