# Camera Path & Spatial Listener Report (2026-06-01)

This document is the in-depth companion to the [README](../README.md) for
the spatial-perception overhaul we shipped in the **2026-06-01** session
plus the camera-path feature that closed it out.  It explains what changed,
why we changed it, the data structures and threading rules, and how to
test / extend the system.

Roughly in execution order:

1. [Default sounds on placement](#1-default-sounds-on-placement)
2. [Camera-relative spatial audio](#2-camera-relative-spatial-audio)
3. [Toolbar Anchor](#3-toolbar-anchor)
4. [Sidebar SPATIAL panel (live distance + dB, A→B picker)](#4-sidebar-spatial-panel)
5. [Export honors the listener](#5-export-honors-the-listener)
6. [Selected-block highlight swap (green → orange)](#6-selected-block-highlight-swap)
7. [Camera (listener) path](#7-camera-listener-path)
8. [Help popup](#8-help-popup)
9. [Persistence (.sime v10)](#9-persistence-sime-v10)
10. [How to test everything end-to-end](#10-how-to-test-everything-end-to-end)

---

## 1. Default sounds on placement

**Problem.** Most block types were placed silent because only
`Violin / Piano / Drum` had a `blockTypeDefaultSoundId`.  New users
would place an Oboe block, hit Play, and hear nothing.

**Fix.**
* `SoundLibrary::defaultSoundForBlockType(BlockType, AudioEngine&)`
  picks the first CSV-indexed sample for the type, preferring an A3 /
  A4 / C4 / G3 note when several exist.  Lazy-loads the WAV through
  the existing `ensureLoaded` cache.
* Falls back to `blockTypeDefaultSoundId` (`= 100`, the violin synth)
  when the CSV library failed to load — so the app still produces
  sound on a broken / missing `Sounds/` folder.

Wired in `ViewPortComponent.cpp` at the placement point:

```cpp
int sid = libraryLoaded_
        ? library_.defaultSoundForBlockType(placedType, audioEngine)
        : blockTypeDefaultSoundId(placedType);
if (sid >= 0) newBlock.soundId = sid;
```

---

## 2. Camera-relative spatial audio

**Problem.** The old `AudioEngine::applySpatialPosition` used the block's
world coordinates relative to **the origin (0, 0, 0)**, not the camera.
Walking around the scene didn't change the mix; the only way to perceive
spatial-ness was to literally move the block.  Falloff was also weak:
`refDist = 5`, rear sources only ~35% quieter.

**Fix.** Per-voice gains now come from a pure helper

```cpp
AudioEngine::computeSpatialGainsStatic(
    listenerX, listenerY, listenerZ,
    fwdX, fwdY, fwdZ,
    rightX, rightY, rightZ,
    sensitivity,
    srcX, srcY, srcZ,
    &outGain, &outPan, &outPitchRate, &outLeft, &outRight);
```

Algorithm (1 grid unit = 1 metre, documented in the header):

1. `rel = src - listener`; `dist = |rel|`.
2. `gain = refDist / (refDist + dist)` with
   `refDist = 3 m / spatialSensitivity` so the toolbar slider
   genuinely controls falloff.
3. Forward / right vectors come from the camera each frame; the
   horizontal pan angle is `atan2(dot(rel,right), dot(rel,fwd))`.
4. `rearAtten = jmap(frontNorm, -1..1, 0.35..1)` — sources behind the
   listener lose two-thirds of their level.
5. Equal-power L/R from the pan angle.

Pitch is still `2^(y/12)` from world Y (independent of the camera).

**Why it's a free function.** The offline exporter needs the same math
without any of the engine's atomic state, so the member
`AudioEngine::computeSpatialGains` just loads the atomics and calls the
static version.  Live + bake stay byte-identical.

**Listener feed.** `ViewPortComponent::renderOpenGL` pushes the camera
pose every frame:

```cpp
audioEngine.setListenerPosition(p.x, p.y, p.z);
audioEngine.setListenerOrientation(fwd.x, fwd.y, fwd.z,
                                   right.x, right.y, right.z);
```

When **Anchor** is active these come from the frozen anchor pose; when
the camera path is active and **Path On** is enabled, the camera itself
has already been driven by the path before this block runs, so the same
code path stays correct.

**Toolbar Spatial slider.** New `AudioEngine::setSpatialSensitivity()`
clamped to `[0.25, 3.0]`.  Bound to a `juce::Slider` in the toolbar.

---

## 3. Toolbar Anchor

Freezes the listener at the current camera pose so the composer can
fly around the scene without changing what the mix sounds like.

* Toggle: toolbar **Anchor** pill.
* On enable: captures `camera.position`, `camera.forward`,
  `camera.right` and the lookAt target.
* On disable: snaps the camera back to where it was when you anchored.
* Implementation: small `pendingAnchorOp_` atomic drained on the GL
  thread in `renderOpenGL` (same pattern as every other cross-thread
  edit in the codebase).
* Anchor + Path interact cleanly: Anchor wins over Path (so you can
  freeze a moment from a path), and disabling Anchor returns to
  whatever Path / camera state was active.

---

## 4. Sidebar SPATIAL panel

The Info panel grew a new **SPATIAL** section (under the block-position
fields) with two readouts and a measurement button.

* **From listener: X.XX m, Y.Y dB** — refreshed on the message-thread
  timer; computed by `AudioEngine::measureSourceAt`, which calls
  exactly the same `computeSpatialGainsStatic` as the audio thread.
* **Distance...** — start a two-block pick.  After clicking the
  button, the next block click in the viewport completes the
  measurement; the sidebar shows `A -> B: X.XX m  (d dx, dy, dz)
  level @ B: Z.Z dB` (the level is at the listener, not the
  A-to-B distance — easy follow-up if we want both).

Threading: the pick state lives in two atomics on
`ViewPortComponent` (`distancePickActive_`, `distancePickAnchorSerial_`).
A queued callback on the message thread fires when the GL thread
completes the pick, so the sidebar update happens on the right thread
without manual locking.

---

## 5. Export honors the listener

`SceneAudioExporter::ListenerPose` carries the static fields (position,
forward, right, sensitivity) plus an optional `cameraPath`:

```cpp
struct ListenerPose
{
    bool   anchored      = false;
    float  posX, posY, posZ;
    float  fwdX, fwdY, fwdZ;
    float  rightX, rightY, rightZ;
    float  sensitivity   = 1.f;

    bool                          pathFollow = false;
    std::vector<CameraKeyframe>   cameraPath;
};
```

The bouncer resolves the listener **per chunk**:

```cpp
const double chunkTime = clock.currentTimeSec();
Vec3f lpos, lfwd, lright;
resolveListener(listener, chunkTime, lpos, lfwd, lright);

// Re-mix sustained voices for the new listener pose
if (pathFollow) for (auto& v : voices) if (!v.isFinished()) applyListenerGainsAt(...);

const auto events = sequencer.update(clock, blocks);
dispatchEvents(events, ..., lpos, lfwd, lright, rate);
```

That makes a long-held note slide through pan / level smoothly as the
listener moves; without the re-mix the note would lock to whatever pose
was active at trigger time.

**Dialog (`ExportAudioDialog`).** New "Listener pose" panel shows one
of three states:

* `CAMERA PATH active (N keyframes, T0 → T1 s).` — green, path mode.
* `ANCHORED at (x, y, z) yaw …, pitch ….` — blue, anchor mode.
* `Anchor not set. Export will use the current camera pose …` — yellow,
  with a tip line nudging the user to hit Anchor first.

Doppler is still excluded from the offline render (matches the live
default-off behaviour).  Flip it on in `SceneAudioExporter::bounceToFile`
if you want a Doppler-baked bounce.

---

## 6. Selected-block highlight swap

**Problem.** Both *selected* and *currently firing* used the same
green tones, so users couldn't tell why a block was glowing.

**Fix.** In `ViewPortComponent.cpp`, the "selected block" pass now
draws **orange** (`{1.0, 0.50, 0.10}`, brighter `{1.0, 0.65, 0.20}` when
also hovered).  The "currently firing" pulse stays its original green
(`{0.0, 1.0, 0.30}`).  When a block is selected *and* firing, the green
pulse layers on top of the orange and reads clearly.

(Edit-mode's existing orange selection highlight stayed unchanged —
this just brings normal-mode selection in line.)

---

## 7. Camera (listener) path

The user's "mini drone" feature.  Two interaction modes per keyframe,
mirroring how block keyframes already work in `KeyframeEditorPopup`:

* **Hold** — pose freezes until the next keyframe time, then snaps.
* **Lerp** — pose lerps smoothly to the next keyframe.

### 7.1 Data model — `Source/CameraPath.h`

```cpp
struct CameraKeyframe
{
    enum Mode : uint8_t { Hold = 0, Lerp = 1 };
    double  timeSec   = 0.0;
    Vec3f   pos       { 6.f, 6.f, -4.f };
    float   yawRad    = -2.3f;
    float   pitchRad  = -0.45f;
    uint8_t mode      = Hold;
};

struct CameraPose { Vec3f pos; float yawRad; float pitchRad; };

namespace CameraPathUtil {
    void sortByTime(std::vector<CameraKeyframe>&);
    double endTime(const std::vector<CameraKeyframe>&);
    CameraPose sample(const std::vector<CameraKeyframe>&, double t, const CameraPose& def);
    void poseDirs(const CameraPose&, Vec3f& fwd, Vec3f& right);
}
```

`sample()` linearly interpolates position + pitch and **shortest-arc**
interpolates yaw (so we never spin the camera through a full circle
when wrapping ±π).  Before the first keyframe and after the last, the
path holds at the endpoint pose — callers don't have to clamp.

### 7.2 Live following — `ViewPortComponent`

Three atomics + a mutex-protected vector live on the viewport:

```cpp
std::vector<CameraKeyframe>   cameraPath_;        // GL-thread owned
juce::CriticalSection         cameraPathMutex_;   // snapshot for popup
std::atomic<bool>             cameraPathHasAny_       { false };
std::atomic<bool>             cameraPathFollowEnabled_{ false };
std::atomic<bool>             cameraPathRecording_    { false };
```

Edits go through the standard pending-op pattern
(`pendingPathOps_` vector of small `PendingPathOp` structs) and the GL
thread drains them at the top of `renderOpenGL()`:

```cpp
case Replace:     swap-in new path
case Clear:       wipe path
case AddHold:     capture current camera pose at op.timeSec
case StartRecord: stamp recordingStartSec_, clear recording buffer
case StopRecord:  splice recording into path (replace [t0, t1])
case FollowSet:   atomic store for cameraPathFollowEnabled_
```

Every render frame, three things happen for the path:

1. **Capture** (if recording + transport playing): if enough wall time
   has passed (`kRecCaptureIntervalSec = 50 ms`, ~20 Hz), append a
   `Lerp` keyframe at the current playhead time + live camera pose.
2. **Drive** (if `cameraPathFollowEnabled_` + path non-empty + playing +
   *not* recording): sample the path at `transportClock.currentTimeSec()`
   and call `camera.setPosition` + `camera.setYawPitch`.  Recording
   intentionally suppresses driving so the user can fly the camera mid-
   recording without fighting their own keyframes.
3. **Listener feed** runs *after* path driving, so anchored / path-driven
   / free poses all flow through one code path to the audio engine.

### 7.3 Hotkey + popup

* **R** toggles recording.  Replaced the old "reset camera to (8,8,8)"
  binding, which moved to **Home**.  The popup also has a Record button
  that mirrors the same toggle and shows recording state in red.
* **Path…** toolbar button opens `CameraPathPopup`.  Each keyframe is
  one row: time, X, Y, Z, yaw°, pitch°, mode combo, delete.  Buttons:
  * **+ Hold @ cam now** — requests the host to capture the live pose at
    the current playhead time.
  * **Record (R)** — toggles recording (host pushes a pending op).
  * **Clear All** — wipes the local draft.
  * **Apply / Cancel** — commit / discard.
* The popup polls the host every 100 ms for the live path snapshot so
  recordings appear live in the editor — but it only refreshes the row
  list when the size changes, so user-typed edits aren't blown away
  mid-edit.

### 7.4 Toolbar **Path On**

Just an atomic store with the same pending-op queue:

```cpp
pathFollowBtn_.onClick = [this] {
    const bool v = !pathFollowBtn_.getToggleState();
    view.setCameraPathFollowEnabled(v);
    pathFollowBtn_.setToggleState(v, juce::dontSendNotification);
};
```

### 7.5 Export

When a camera path exists, `ViewPortComponent::exportSceneAudioToFile`
copies it into `ListenerPose::cameraPath` and sets `pathFollow = true`.
The bouncer then animates the listener per chunk (see §5).  The path
overrides anchor / camera at export time — the assumption is "if you
made a path, you want it baked".

---

## 8. Help popup

A single-panel cheat sheet of every binding.  Lives in
`Source/HelpPopup.{h,cpp}`; opened from the toolbar **Help** button.
Bindings are listed by section (Mouse, Placing, Clipboard, Camera path,
Transport, Sidebar) so they stay scannable.  Kept in sync by hand with
the actual handlers in `ViewPortComponent::keyPressed` and
`MainComponent::keyPressed` — there's a tiny test in mind for the next
session to fail at compile time if they ever drift.

---

## 9. Persistence (.sime v10)

`.sime` files gain an optional trailing block, identified by a `CPTH`
4-byte magic so older v9 readers ignore it cleanly:

```
... existing v9 block records ...
[Camera-Path Trailer]
  4 bytes  magic   "CPTH"
  4 bytes  uint32  keyframe count
  Keyframe × count
    8 bytes  double  timeSec
    4 bytes  float   pos.x
    4 bytes  float   pos.y
    4 bytes  float   pos.z
    4 bytes  float   yawRad
    4 bytes  float   pitchRad
    1 byte   uint8   mode (0=Hold, 1=Lerp)
```

`SceneFile::save` and `SceneFile::load` got new overloads that take a
`std::vector<CameraKeyframe>`.  The old single-arg versions still exist
and stay binary-compatible (they pass an empty path).  v9 → v10 upgrade
is silent on load (old files come back with an empty path); v10 → v9 is
also silent on save (you just lose the trailer).

---

## 10. How to test everything end-to-end

1. Build: `cmake --build build --config Release`.
2. Launch `build/SIME_artefacts/Release/SIME.exe`.
3. Place a couple of violins at `(5, 0, 0)` and `(-5, 0, 0)`, give each
   a 5 s region in the **Info** panel.
4. **Defaults** — placed blocks should already have a sound (Phase 1).
5. **Camera-relative mix** — hit Play, fly with WASD/mouse.  Pan and
   level should follow your view.
6. **Anchor** — fly to where you like the mix, click **Anchor**, then
   fly elsewhere.  The mix stays anchored; toggle Anchor off to snap
   the view back.
7. **Sidebar SPATIAL** — select one block, watch the live `From
   listener` line update as you fly.  Hit **Distance…** then click the
   other block — you should see the metre + dB readout.
8. **Camera path**:
   1. Click **Path…**.
   2. Scrub the playhead to `00:00`, fly to pose A, click **+ Hold @ cam
      now**.
   3. Scrub to `00:10`, fly to pose B, click **+ Hold @ cam now**.
   4. Scrub to `00:20`, click **Apply** to dismiss.
   5. Click **Play**, then **Path On** in the toolbar.  The camera
      should hold pose A for 10 s, jump to B, then hold B.
   6. Stop, scrub back to `00:25`, press **R**, fly the camera around
      for ~5 s, press **R** again.  Re-Play with Path On.  The recorded
      segment should replay verbatim.
9. **Export** — File → Export Audio.  The dialog should say
   `CAMERA PATH active …`.  Export, listen — the bake should match what
   you heard live.
10. **Save / load** — Ctrl+S, then File → New → File → Open the saved
    `.sime`.  Re-open the Path popup; your keyframes should still be
    there.

---

## Code map

| File | Role |
|------|------|
| `Source/AudioEngine.{h,cpp}` | `computeSpatialGainsStatic`, listener atomics, sensitivity, anchor / path-friendly listener feed |
| `Source/CameraPath.h` | `CameraKeyframe`, `CameraPose`, `CameraPathUtil::{sortByTime,endTime,sample,poseDirs}` |
| `Source/CameraPathPopup.{h,cpp}` | Editor popup (rows, Hold / Lerp combo, recording state, live path polling) |
| `Source/HelpPopup.{h,cpp}` | Cheat-sheet modal |
| `Source/ViewPortComponent.{h,cpp}` | Live path drive + recording + R hotkey + listener feed + export listener info |
| `Source/MainComponent.{h,cpp}` | Toolbar Path / Help buttons, sidebar wiring, save / load with camera path |
| `Source/SceneAudioExporter.{h,cpp}` | `ListenerPose` (now path-aware), per-chunk listener resolve + voice re-mix |
| `Source/ExportAudioDialog.{h,cpp}` | Listener-pose info label (anchor / camera / path) |
| `Source/SceneFile.{h,cpp}` | `.sime` v10 with optional `CPTH` trailer |
| `Source/SidebarComponent.{h,cpp}` | SPATIAL section (live readout, Distance pick) |

---

## Future ideas (not done)

* **Path scrub overlay** — when Path is set, draw a small 2D timeline at
  the top of the viewport showing keyframe positions + current playhead;
  click a marker to scrub to that keyframe.
* **Path visualisation in 3D** — draw the position curve as a polyline
  with keyframe pip markers, coloured by mode.
* **Camera lerp easing** — currently linear-only; add Bezier or
  ease-in/ease-out per segment.
* **Mouse-grab override** — instead of requiring a manual toggle, an
  in-progress mouse drag could temporarily suspend path driving for the
  duration of the drag (with a HUD toast on release).
* **Doppler in the bounce** — currently disabled in offline render;
  trivial to enable now that the listener feeds chunk-by-chunk.
