# SIME Demo Video Script (~3 min)

---

## SECTION 1 — Intro (30 sec)

> "This is SIME — Spatially-Interpreted Music Engine.
>
> It's a 3D spatial audio sequencer where you place blocks in a 3D environment
> and each block's position in space directly affects how it sounds.
>
> Move a block left or right, the audio pans. Place it higher, the pitch goes up.
> Push it further away, it gets quieter.
>
> The idea is to make music composition spatial and visual
> instead of the traditional flat timeline."

---

## SECTION 2 — Technical Approach (45 sec)

> "The app is built in C++ using JUCE for real-time audio and OpenGL 3.3 for rendering.
>
> There are two main threads: the GL render thread handles the 3D viewport,
> block placement, and runs the sequencer each frame.
> The audio thread mixes voices through a lock-free FIFO — no locks, no allocations
> in the audio callback.
>
> Each block has a position, a start time, a duration, and a sound ID.
> When you press Play, the transport clock advances, the sequencer scans all blocks,
> and fires start/stop events to the audio engine.
>
> The spatial mapping is simple: X controls stereo pan using equal-power panning,
> Y controls pitch through playback-rate scaling — one grid unit per semitone —
> and Z controls volume through inverse-distance falloff."

---

## SECTION 3 — Live Demo (1 min 30 sec)

**[Screen recording of the running app]**

### 3a — Navigation (~15 sec)

> "Here's the app running. I can look around with right-click drag,
> move with WASD, and go up and down with Space and Ctrl."

**Show:** Fly camera around the grid, demonstrate smooth movement.

### 3b — Placing Blocks (~20 sec)

> "Left click places a block on the grid.
> I can also hold Shift to place blocks in mid-air
> and use the scroll wheel while holding Shift to raise or lower the placement plane."

**Show:** Place 3–4 blocks at different positions. Place one high up with Shift.

### 3c — Edit Mode + Timing (~20 sec)

> "Pressing E enters edit mode. Now I can right-click any block
> to set its start time, duration, and which sound it plays."

**Show:** Press E, right-click a block, change start time to something like 0.5,
set duration, click Apply. Do this for 2–3 blocks with staggered start times.

### 3d — Playback + Spatial Audio (~25 sec)

> "Now when I press Play, the transport runs and you can see blocks highlight
> as they trigger. Listen to the difference —
> this block on the left is panned left,
> this one up high is pitched up,
> and this one further out on Z is quieter."

**Show:** Hit Play in the transport bar. Point out highlights on blocks.
Stop, move a block to a different position, play again to show the change.

### 3e — Wrap-up (~10 sec)

> "That's SIME so far — a working spatial audio sequencer
> with real-time 3D rendering, transport playback,
> and position-based sound mapping. Thanks for watching."

---

## Recording Tips

- Record at 1920x1080, landscape, export as MP4
- Use OBS or Windows Game Bar (Win+G) for screen capture
- Keep the app window maximized so UI is readable
- Place blocks at clearly different X/Y/Z positions so the spatial differences are obvious
- If audio is hard to hear, consider wearing headphones while recording so pan is clear to you, and mention in the video that headphones help
- Upload to Google Drive, set sharing to "Anyone with the link can view"
