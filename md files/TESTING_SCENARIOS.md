# Block Behaviour Testing Scenarios

A playbook of user stories for stress-testing how **duration**, **sound assignment**, **block movement**, **playback modes**, **looping**, and **muting** interact with each other in SIME.  Use these the next time you change anything in `BlockEntry`, `SequencerEngine`, `AudioEngine`, or the block info panel — they're meant to catch the weird edge cases that surface from combining features rather than testing each in isolation.

Each scenario lists:
- **Setup** — what to build in the scene before playing.
- **Steps** — what to do.
- **Expected** — what *should* happen given the current design.
- **Watch for** — the failure mode that would tell you the feature regressed.

> Tip: keep a scratch scene called `tests.sime` with one block of every type pre-staggered along the timeline.  Loading it is the fastest way to reproduce most of these.

---

## Table of Contents

1. [Quick Concept Recap](#1-quick-concept-recap)
2. [Duration ↔ Sound Assignment](#2-duration--sound-assignment)
3. [Block Movement](#3-block-movement)
4. [Playback Modes (Natural / Loop / Stretch / Speed)](#4-playback-modes-natural--loop--stretch--speed)
5. [Looping](#5-looping)
6. [Muting (Forever, Per-Type, Scheduled Windows)](#6-muting-forever-per-type-scheduled-windows)
7. [Combined / Cross-Feature](#7-combined--cross-feature)
8. [Transport / Seek / Loop-Wrap](#8-transport--seek--loop-wrap)
9. [Persistence Round-Trips](#9-persistence-round-trips)
10. [Export Bounces](#10-export-bounces)
11. [UX Improvement Suggestions](#11-ux-improvement-suggestions)
12. [Bug-Hunt Checklist](#12-bug-hunt-checklist)

---

## 1. Quick Concept Recap

Three different "durations" live on every block — keeping them straight is the key to reading the test results below:

| Name | Stored as | Means |
|---|---|---|
| **Region duration** | `BlockEntry::durationSec` | How long the block "owns" on the timeline.  Sequencer fires *Start* at `startTimeSec` and *Stop* at `startTimeSec + durationSec` (or `loopDurationSec` for loop mode). |
| **Sound natural duration** | `sampleNaturalDurationSec` (cached from the loaded WAV) | The raw playback length of the assigned sample at its native sample rate. |
| **Movement duration** | `movementDurationSec` (0 ⇒ "use region") | How long the recorded keyframe path takes to traverse from first to last keyframe. |

The playback mode (`Natural / Loop / Stretch / Speed`) is the bridge between region and sound length.  The mute schedule (`muteWindows`) layers on top: it can silence audio without touching movement.

---

## 2. Duration ↔ Sound Assignment

### 2.1 Duration set first, sound assigned after
- **Setup:** place one Violin block at `(0,0,0)`.
- **Steps:**
  1. In the Block Info panel, set `Duration: 5.00`.  Apply.
  2. Right-click the block in edit mode → open the edit popup → pick a sound that's clearly *shorter* than 5 s (any percussive sample).
- **Expected (current behaviour):** the region duration is **rebound to the sample's natural length** (so 5 s becomes something like 0.5 s).  This is intentional — it keeps the visible region honest about how much audio actually plays in Natural mode — but it is *surprising* if you intentionally set 5 s first.
- **Watch for:** sound stops at 0.5 s but timeline still shows 5 s (= the auto-resize didn't fire); or the auto-resize ran but didn't refresh the sidebar text.

### 2.2 Duration set first, sound assigned, you *want* the long region back
- **Setup:** same as 2.1.
- **Steps:** after sound is assigned, click `Match Duration to Sound`, then manually edit `Duration` back to 5.0 and Apply.
- **Expected:** region is 5 s, sample plays for 0.5 s and then there is silence for 4.5 s (Natural mode).  The two extremes are: sample loops (Loop mode) or sample stretches to fill (Stretch mode) — both controlled by the **Mode** combo.
- **Watch for:** Apply silently snaps the field back to the sample length (would indicate a sticky "lock duration to sound" path).

### 2.3 Sound assigned first, duration changed after
- **Setup:** new block, assign a 2 s sample.
- **Steps:** change `Duration` to 1.0 (shorter than the sample) and Apply.
- **Expected:** Natural mode → sample is cut off at 1 s.  Loop mode → sample restarts; you hear ~½ a loop.
- **Watch for:** sample keeps playing past the region end (Stop event lost).

### 2.4 Sample reassigned mid-scene
- **Setup:** block with a 2 s sample, duration 2 s, scene playing.
- **Steps:** while playback is running, open the edit popup and pick a 10 s sample.
- **Expected:** voice currently playing is replaced cleanly; duration gets auto-resized to the new sample (per §2.1), region grows to 10 s, timeline reflows.
- **Watch for:** old voice keeps ringing alongside the new one; or the region grows but the in-flight voice was never restarted with the new sample.

### 2.5 Duration locked
- **Setup:** any block.
- **Steps:** flip `durationLocked` to true (the lock state isn't currently exposed in UI — set it once via save-edit-load, or add a temporary checkbox to test).
- **Expected:** sound assignment **does not** rewrite duration.
- **Watch for:** duration changes anyway → the lock isn't honored on this code path.

---

## 3. Block Movement

### 3.1 Record movement, then change region duration
- **Setup:** any block; Alt+LMB drag to record 4 keyframes over ~3 s.
- **Steps:** set `Duration: 1.0` and Apply.  Set `Movement Duration: 0` (which means "use region").
- **Expected:** path now plays back in 1 s (3× faster than recorded).  Doppler-enabled scenes will show a noticeable pitch shift while the block moves.
- **Watch for:** path still takes 3 s and the region ends before the path completes.

### 3.2 Record movement, set explicit movement duration
- **Setup:** same recording as 3.1.
- **Steps:** set `Move dur (s): 6` and `Duration: 1.0` and Apply.
- **Expected:** sound plays for 1 s only; movement keeps playing for 6 s on its own.  The region's Stop event silences audio at t = 1 s.
- **Watch for:** Stop event also kills movement; or movement re-syncs to region duration anyway.

### 3.3 Path Y-lift offset
- **Steps:** record a flat XZ path; set `Path Y lift: 5`, Apply.
- **Expected:** the block plays the path 5 cells higher than recorded, raising pitch by 5 semitones.
- **Watch for:** offset applied visually but not to pitch (pitch uses pre-offset Y) or vice versa.

### 3.4 Movement disabled
- **Steps:** record a path; uncheck `Enable Recorded Movement`; Apply.
- **Expected:** block stays at its current position during playback; no Movement events fired; path is preserved on disk.
- **Watch for:** block still moves (toggle not respected) or path got wiped.

### 3.5 Manual gizmo drag overrides start position
- **Steps:** record a path starting at `(0,0,0)`; drag the block via the gizmo to `(5,0,0)`; replay.
- **Expected:** path replays relative to the *new* starting position (recorded keyframes are world-absolute, so this depends on current implementation — verify which one we're using).
- **Watch for:** mismatch between visual block position before play vs. position once the first Movement event fires.

---

## 4. Playback Modes (Natural / Loop / Stretch / Speed)

These are the cells of the truth table you should walk through manually:

| Sample length | Region length | Mode | Expected audio |
|---|---|---|---|
| 1 s | 5 s | Natural | 1 s of sound, 4 s silence |
| 1 s | 5 s | Loop | 5 ish play-throughs back-to-back (or with the configured loop gap) |
| 1 s | 5 s | Stretch | 1 sample stretched to 5 s — much lower pitch / slower |
| 1 s | 5 s | Speed | 1 sample at rate 0.2 — even slower than Stretch (Speed isn't semantically meaningful when sample < region) |
| 5 s | 1 s | Natural | first 1 s of the sample, rest cut |
| 5 s | 1 s | Loop | first ~1/5 of one iteration |
| 5 s | 1 s | Stretch | rate 5 — much higher pitch / faster (Stretch isn't semantically meaningful when sample > region) |
| 5 s | 1 s | Speed | sample crushed into 1 s at rate 5 — higher pitch |

**Conclusion you should be able to teach to a new user:**
- Sample shorter than region → use **Loop** or **Stretch**.
- Sample longer than region → use **Speed** (or accept the cut-off in Natural mode).
- Don't use Stretch / Speed in the wrong direction; the maths still works but the result is rarely musical.

> Suggestion (§11): the UI could auto-pick the sensible mode given the ratio, or grey out the non-sensible one.

---

## 5. Looping

### 5.1 Loop length shorter than region
- **Setup:** sample 0.5 s, region 4 s, Loop on, `Loop length: 0.5`.
- **Expected:** ~8 plays of the sample inside the region.

### 5.2 Loop length equals region (`= Block` button)
- **Steps:** click `= Block` next to the loop length field.
- **Expected:** field updates to the current `Duration` value.  In playback, you hear exactly one play-through (loop wraps right at the Stop event).

### 5.3 Loop gap
- **Setup:** sample 0.5 s, `Loop length: 0.5`, `Loop gap (s): 1`.
- **Expected:** play, 1 s silence, play, 1 s silence, … inside the region.
- **Watch for:** gap counted in seconds but the engine uses samples (or vice versa) → ends up dramatically too long or zero.

### 5.4 Loop + mode combo set to Natural
- **Steps:** flip `Loop sound` on; leave `Mode = Natural`; Apply.
- **Expected:** the Loop toggle is the source of truth and the mode is force-promoted to `Loop` internally (see `ViewPortComponent::applySidebarBlockInfo`).  Saving and reloading should round-trip as Loop.

### 5.5 Loop mid-region duration change
- **Steps:** while a loop block is playing, edit `Duration` from 4 s to 8 s and Apply.
- **Expected:** the in-flight voice keeps looping; sequencer's Stop event is rescheduled for the new region end.
- **Watch for:** voice stops early at the old region end.

---

## 6. Muting (Forever, Per-Type, Scheduled Windows)

### 6.1 Mute (forever) toggle
- **Steps:** check `Mute (no audio, forever)`, Apply.
- **Expected:** zero audio for this block; visuals and movement keep playing.

### 6.2 Per-type mute (toolbar `Mute ▾`)
- **Steps:** open `Mute ▾`, tick "Violin".
- **Expected:** every Violin block goes silent until you untick.  The per-block `isMuted` field is *not* touched; mute is transient and not saved into `.sime`.

### 6.3 Single scheduled mute window
- **Setup:** 10 s region, sample loops.
- **Steps:** open `Mute Schedule...` → add one row with `Start: 3`, `Duration: 2`.  Apply.
- **Expected:** audio plays t = 0–3, silent 3–5, plays 5–10.  Movement and visuals never pause.

### 6.4 Multiple overlapping windows
- **Steps:** add windows `(2, 3)` and `(4, 2)` → effective mute is `[2, 6)`.
- **Expected:** the union of the two windows is muted; no flickering at the boundary.

### 6.5 Window outside region
- **Steps:** 5 s region; window `(10, 5)`.
- **Expected:** window has no audible effect (region ends before playhead enters window).
- **Watch for:** the window somehow extends the region end on the timeline.

### 6.6 Live mute toggle mid-region (popup applied while playing)
- **Steps:** with playback running, open the popup, add a window covering "now", Apply.
- **Expected:** voice is killed immediately on the next sequencer tick; popup closes.
- **Watch for:** voice keeps ringing until the end of the window starts.

### 6.7 Stack: per-type mute + window
- **Steps:** per-type mute Violin **and** schedule a window on a Violin block.
- **Expected:** block is silent for the union of `(effectiveMuted OR windowMute)`.  Untick the per-type mute → block goes silent only inside the scheduled window.

### 6.8 Hide is purely visual
- **Steps:** check `Hide block in viewport`, leave everything else at defaults, play.
- **Expected:** block isn't drawn but you still hear the audio.  Selection still works through the sidebar list.

---

## 7. Combined / Cross-Feature

These are the ones that catch most regressions.

### 7.1 Movement + Loop
- **Setup:** record a path, Loop on, `Loop length: 1.0`, region 6 s.
- **Expected:** sound restarts every 1 s.  Each restart inherits the block's *current* spatial position → pan / pitch jump on every loop boundary.

### 7.2 Movement + Stretch
- **Setup:** record a path, Mode = Stretch, sample 1 s, region 6 s.
- **Expected:** one play-through that lasts 6 s; movement plays in lockstep with the region.

### 7.3 Movement + Mute window
- **Setup:** record a path; window `(2, 2)` over a 6 s region.
- **Expected:** block visibly moves across the entire 6 s; audio cuts out from 2–4 s only.

### 7.4 Loop + Mute window
- **Setup:** Loop on, `Loop length: 1`, region 6 s, window `(2.5, 1)`.
- **Expected:** loop iterations at t=0, 1, 2 fire; the 3rd loop is killed at t=2.5; resumes a fresh loop at t=3.5; final loops at 3.5, 4.5, 5.5.

### 7.5 Doppler + Movement + Loop
- **Setup:** record a fast path through the listener; Loop sound; Doppler enabled in the toolbar.
- **Expected:** clear Doppler pitch sweep, and the loop boundary doesn't reset the Doppler state.

### 7.6 Multiple blocks of the same type, layered
- **Setup:** three Violin blocks at slightly different start times, all with Loop on.
- **Expected:** all three voices coexist without dropouts.  Watch the simultaneous-voice count — if you push past 32, look for `activeVoices_.push_back` allocations on the audio thread (logged in Debug builds).

### 7.7 Per-type Mute while Type Filter view is on
- **Setup:** per-type Mute = Violin, `View ▾` hide Strings.
- **Expected:** Violins are silent **and** hidden; other types unaffected.

---

## 8. Transport / Seek / Loop-Wrap

### 8.1 Seek into a mute window
- **Steps:** scrub the timeline so the playhead lands inside a window.
- **Expected:** block stays silent until the window ends, then a *fresh* Start event fires.

### 8.2 Seek out of a mute window
- **Steps:** scrub from inside a window to after it.
- **Expected:** audio resumes from the appropriate position within the region (not from t=0 unless the seek crossed the region's start boundary).

### 8.3 Transport loop wraps inside a mute window
- **Setup:** transport loop region overlaps a mute window.
- **Expected:** every loop iteration honours the window without doubling up or silently failing.

### 8.4 Transport speed change with mute window
- **Steps:** scene with a window at 2–3 s; play at 0.5x then bump to 2x.
- **Expected:** the window stays at *transport seconds* 2–3, so at 2x speed it triggers half a second earlier in wall-clock time.  Mute starts and ends correctly.

---

## 9. Persistence Round-Trips

### 9.1 Save & reload a fully-decorated block
- **Steps:** one block with: custom WAV, recorded movement, movement duration override, Y-lift, Loop on, loop gap > 0, per-block mute, 3 mute windows, hide on.  Save.  New scene.  Open the saved file.
- **Expected:** every field comes back identically; mute windows in their original order.

### 9.2 v8 → v9 migration
- **Steps:** open a `.sime` file saved before the multi-window feature (v8).  That file had a single `muteStartSec` / `muteEndSec` pair.
- **Expected:** the loader auto-promotes that pair into the first entry of `muteWindows`; the Block Info panel shows `Mute Schedule (1)...` and the popup contains the migrated entry.

### 9.3 Forwards compat guard
- **Steps:** hand-edit a `.sime` file to claim version 99.
- **Expected:** loader refuses cleanly with a polite error, doesn't crash.

---

## 10. Export Bounces

### 10.1 Mute windows are baked into the export
- **Setup:** scene with a mute window.
- **Steps:** `File → Export Audio…` → WAV.
- **Expected:** silence in the WAV exactly during the window.  The per-type Mute (toolbar) is **not** baked in (it's a transient view filter; see `SceneAudioExporter.cpp` comments).

### 10.2 Loop blocks export to their full region length
- **Setup:** Loop block, region 8 s, loop length 1 s.
- **Expected:** 8 s of audio in the bounce, looped 8 times.

### 10.3 Hidden block still audible in export
- **Setup:** `Hide block in viewport` on but not muted.
- **Expected:** block IS audible in the export — hide is visual-only.

### 10.4 Stretch / Speed are time-accurate in the export
- **Setup:** Stretch block with rate 0.5 over a 4 s region.
- **Expected:** WAV is exactly 4 s long (offline mixer matches live rate).

---

## 11. UX Improvement Suggestions

The user story from the chat — *"I set duration to 5 s, assigned a sound, and the duration was overwritten — should it have been?"* — points to a real ambiguity: SIME has three "durations" that the user thinks of as one.  Some directions that would help, ranked by impact:

### 11.1 Show the three durations at the same time
In the Block Info panel, add a tiny read-only summary row:

```
Region 5.00 s   Sample 2.30 s   Movement 3.40 s
```

Or, in Natural mode specifically, show "*2.30 s sound + 2.70 s silence*" — that one sentence prevents most of the "why is my block silent" support questions.

### 11.2 Add a `Lock region duration` toggle
Right next to the Duration editor.  When on, assigning a sound never rewrites the region.  When off (default = current behaviour), it does.  Either way the user opts in explicitly instead of being surprised.

### 11.3 Auto-pick the playback mode
When the user assigns a sound, suggest:
- Sample > region by >5% → "Speed"
- Sample < region by >5% → "Loop" (most common intent) or "Stretch" (preserves the sound)
- Sample ≈ region (±5%) → "Natural"

A tiny `Suggest mode` button next to the Mode combo would offer this without forcing it.

### 11.4 Single-block preview
A small `▶` button on each block info panel that plays *only this block* in isolation from t = 0 of the block, so the user can audition a setting without scrubbing the global transport.

### 11.5 Timeline overlays for mute schedule
Draw the scheduled mute windows on the block's timeline strip as a darker / hatched overlay.  Makes it instantly visible whether a block has a long, fragmented mute schedule.

### 11.6 Mute schedule "add from selection"
In the popup: `Add window starting at playhead, duration = …` and `Add window covering current loop region`.  Saves a lot of typing for users who scrub-then-mute.

### 11.7 Drag-resize the block on the timeline
Today, duration is editable only in the sidebar.  Adding two drag handles on each timeline strip (left edge = start, right edge = end) would make timing edits much faster.  Snap to seconds or to other blocks' edges with Shift.

### 11.8 Better error messages for "sound assigned but inaudible"
If the block has `isMuted = true` OR a scheduled window covering the entire region, paint the block's timeline strip in grey + show a small (muted) badge.  Today the user has to deduce silence from the lack of voice activity.

### 11.9 Tooltip glossary
Hover the playback mode names and get a one-sentence explanation (`Stretch — slows the sample so it fills the whole region; pitch drops as a side effect.`).  Same for Loop length / Loop gap / Path Y lift / Mute Schedule.

### 11.10 "Reset mute schedule" / per-row "duplicate"
Two quality-of-life buttons inside the popup that mirror what people do in DAWs — duplicate the previous window with one click, or reset to the engine default (single window covering the whole region) for quick experimentation.

---

## 12. Bug-Hunt Checklist

Things to specifically look for while running through the scenarios above:

- [ ] The Block Info panel's `Mute Schedule (N)` count stays in sync with the actual popup contents after Apply / Cancel / Reset to Default.
- [ ] After `Reset to Default`, the popup (if open) shows zero windows.
- [ ] Reordering scheduled windows via delete-and-readd doesn't fire stale "delete" indices (popup `rebuildRows()` captures index by value, so this should be safe — verify).
- [ ] The transport `Stop` button stops the looping audio cleanly even when a mute window is currently active.
- [ ] Save / load round-trips preserve mute windows in their original order (vector order is significant for any future "first window wins" optimisation).
- [ ] The sidebar's scroll never lets the loop-length editor / `= Block` button float over the section headers or the bottom Apply / Reset strip.
- [ ] Multi-line section headers (MOVEMENT / LOOP / MUTE / HIDE) stay aligned with their first row at every scroll position.
- [ ] The audio analyser stats line shows `·` separators correctly (regression test for the Windows source-charset bug — needs `/utf-8` MSVC flag).
- [ ] The Mute Schedule popup title shows the block name as a subtitle, never as `Mute Schedule \xe2\x80\x94 Block 12` style garbage.
- [ ] The sidebar collapse button shows `☰` when collapsed and `✕` when expanded (regression test for the icon-swap bug).
