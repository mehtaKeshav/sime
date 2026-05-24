#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BlockEntry.h
//
// IMPORTANT: MathUtils.h must be included before this file wherever BlockEntry
// is used.  Vec3i is defined there.  This header deliberately does NOT include
// MathUtils.h itself to avoid double-inclusion ordering issues in translation
// units that already pull in the full JUCE chain.
//
// Safe include order (in every .cpp and .h that needs BlockEntry):
//
//   #include "MathUtils.h"
//   #include "BlockEntry.h"
//
// ViewPortComponent.h already does this, so any .cpp that includes
// ViewPortComponent.h gets both automatically.
// ─────────────────────────────────────────────────────────────────────────────

#include "MathUtils.h"
#include "BlockType.h"
#include <string>
#include <vector>

struct MovementKeyFrame
{
    double timeSec;   // Time relative to block start
    Vec3i  position;  // Absolute world position at this keyframe
};

/// A single scheduled mute window for a block.  When the playhead is inside
/// [startSec, startSec + durationSec), the block is silenced (movement and
/// other state still play).  Blocks may carry any number of these — see
/// `BlockEntry::muteWindows` and the Mute Schedule popup.
struct MuteWindow
{
    double startSec    = 0.0;
    double durationSec = 0.0;

    bool isActive() const noexcept { return durationSec > 0.0; }
    double endSec()  const noexcept { return startSec + durationSec; }
    bool contains(double t) const noexcept
    {
        return isActive() && t >= startSec && t < endSec();
    }
};

/// Per-block playback behaviour for the WAV vs the block's region duration.
///
/// Natural  – sound plays once; if shorter than the region, the rest is silent.
/// Loop     – sound buffer loops continuously inside the audio thread until the
///            region's stop event fires.  Closes the "2 seconds won't play"
///            gap that the old sequencer-retrigger loop had.
/// Stretch  – sound is slowed (rate < 1) so it fills the whole region.  Pitch
///            drops as a side effect — true pitch-preserving time-stretch
///            (WSOLA / phase vocoder) is a future feature.
/// Speed    – sound is sped up (rate > 1) to finish inside the region.  Pitch
///            rises as a side effect, same caveat as Stretch.
enum class BlockPlaybackMode : uint8_t
{
    Natural = 0,
    Loop    = 1,
    Stretch = 2,
    Speed   = 3
};

inline const char* blockPlaybackModeName(BlockPlaybackMode m) noexcept
{
    switch (m)
    {
        case BlockPlaybackMode::Natural: return "Natural";
        case BlockPlaybackMode::Loop:    return "Loop";
        case BlockPlaybackMode::Stretch: return "Stretch (slow)";
        case BlockPlaybackMode::Speed:   return "Speed (fast)";
    }
    return "Natural";
}

struct TimeRange
{
    double startTimeSec = 0.0;
    double durationSec  = 1.0;

    bool hasStarted  = false;
    bool hasFinished = false;
    bool isPlaying   = false;

    int currentKeyframeIndex = 0;
    int loopIterationsFired  = 0;

    std::vector<bool> triggeredKeyframes;

    double endTimeSec() const
    {
        return startTimeSec + durationSec;
    }

    void resetPlaybackState()
    {
        hasStarted = false;
        hasFinished = false;
        isPlaying = false;
        currentKeyframeIndex = 0;
        loopIterationsFired = 0;
        triggeredKeyframes.clear();
    }
};

struct BlockEntry
{
    // ── Identity ──────────────────────────────────────────────────────────────
    int       serial    = 0;
    BlockType blockType = BlockType::Violin;
    Vec3i     pos;               ///< Requires Vec3i from MathUtils.h
    Vec3f     colour;

    // ── Audio mapping ─────────────────────────────────────────────────────────
    int         soundId        = -1;   ///< -1 = silent / unassigned
    std::string customFilePath;        ///< Non-empty for Custom blocks with user WAV

    // ── Timing (seconds relative to transport origin) ─────────────────────────
    double startTimeSec = 0.0;
    double durationSec  = 1.0;

    std::vector<TimeRange> timesList;

    // ── Loop ──────────────────────────────────────────────────────────────────
    // LEGACY (kept for old .sime file back-compat).  New code should use
    // playbackMode == BlockPlaybackMode::Loop instead.  Load reconstructs the
    // mode from these fields; save writes both so older builds still parse.
    bool   isLooping           = false;
    double loopDurationSec     = 4.0;
    int    loopIterationsFired = 0;      ///< Runtime: legacy retrigger counter

    // ── Playback behaviour (Phase 1 movement work) ───────────────────────────
    BlockPlaybackMode playbackMode      = BlockPlaybackMode::Natural;

    /// Movement playback length, in seconds.  0 = use the block's own region
    /// duration (durationSec).  Lets the user prolong the motion path without
    /// changing the audio region width.
    double            movementDurationSec = 0.0;

    /// World-space Y offset applied to every recorded keyframe when playing
    /// back movement.  Lets the user lift / lower the whole recorded path
    /// after the fact, without re-recording (initial motion capture is still
    /// XZ + Shift+scroll).
    int               movementYOffset     = 0;

    /// Returns the effective movement duration in seconds — `movementDurationSec`
    /// if > 0, otherwise the block's region `durationSec`.
    double effectiveMovementDuration() const noexcept
    {
        return movementDurationSec > 0.001 ? movementDurationSec : durationSec;
    }

    /// Natural length of the WAV sample assigned to this block, in seconds.
    /// Cached by the GL render path each time the sample library changes; the
    /// SequencerEngine reads it to compute Stretch / Speed rates.  0 means
    /// "unknown" (rate falls back to 1.0).
    double sampleNaturalDurationSec = 0.0;

    // Recording state
    bool isRecordingMovement = false;
    double recordingStartTime = 0.0;
    Vec3i recordingStartPos;

    // recorded movement data
    bool hasRecordedMovement = false; ///< Block has a saved movement path (keyframes on disk)
    bool movementEnabled     = true;  ///< When false, path is kept but not played during transport
    std::vector<MovementKeyFrame> recordedMovement; ///< Optional per-block movement path for sequenced motion
    bool durationLocked = false;

    // ── Per-block UI / playback flags (v7) ───────────────────────────────────
    /// When true the sequencer skips Start / Stop events for this block —
    /// the path still animates but no audio is emitted.
    bool isMuted = false;

    /// When true the renderer skips drawing this block (and its highlight /
    /// arrows).  Selection / sequencing still work — purely a viewport-clean
    /// helper for composers focusing on a subset of the scene.
    bool isHidden = false;

    /// When > 0 and the block is in Loop mode, the audio thread inserts this
    /// many seconds of silence between successive plays of the sample.
    /// 0 = tight loop (the existing behaviour).
    double loopBufferSec = 0.0;

    /// Legacy single-window mute fields (v8 scene format).
    /// Kept here only so old `.sime` files still round-trip cleanly — the
    /// engine now reads `muteWindows` and load() converts these into the
    /// first entry on import.  Save no longer writes them.
    double muteStartSec = 0.0;
    double muteEndSec   = 0.0;

    /// User-defined mute schedule.  Each entry silences this block while the
    /// playhead is inside [startSec, startSec + durationSec).  Empty list =
    /// no scheduled mutes (the "Mute (forever)" toggle is independent).
    std::vector<MuteWindow> muteWindows;

    /// Returns true if `t` falls inside any of the active mute windows.
    bool isInsideAnyMuteWindow(double t) const noexcept
    {
        for (const auto& w : muteWindows)
            if (w.contains(t))
                return true;
        return false;
    }

    // ── Runtime-only flags (NOT persisted) ───────────────────────────────────
    /// Re-computed every sequencer tick from
    ///   isMuted || (per-type indefinite mute toggled by the toolbar).
    /// SequencerEngine uses this in place of isMuted for the "indefinite"
    /// silence test; it stays out of SceneFile so the toolbar's transient
    /// view-state never leaks into saved scenes.
    bool effectiveMuted = false;

    /// Tracks whether the previous sequencer tick saw this block as muted
    /// (either indefinite or window).  Used to detect transitions so we can
    /// cut / resume the live voice mid-region.  Reset in resetPlaybackState().
    bool wasMutedLastTick = false;

    /// Reset playback-mode fields to factory defaults (does not clear movement
    /// keyframes or position / timing).
    void resetPlaybackDefaults() noexcept
    {
        playbackMode        = BlockPlaybackMode::Natural;
        movementDurationSec = 0.0;
        movementYOffset     = 0;
        isLooping           = false;
        loopDurationSec     = 4.0;
    }

    // Playback state for movement
    size_t currentKeyframeIndex = 0;
    std::vector<bool> triggeredKeyframes;

    // ── Playback state (written by SequencerEngine each frame) ────────────────
    bool hasStarted  = false;
    bool hasFinished = false;
    bool isPlaying   = false;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// End time used for sequencer / transport bookkeeping.
    /// Loop blocks span their full loopDurationSec; non-loop blocks use durationSec.
    double endTimeSec() const noexcept
    {
        return startTimeSec + (isLooping ? loopDurationSec : durationSec);
    }
    bool overlaps(double aStart, double aDuration, double bStart, double bDuration)
    {
        const double aEnd = aStart + aDuration;
        const double bEnd = bStart + bDuration;

        return aStart < bEnd && aEnd > bStart;
    }
    bool addTimeRange(double start, double duration)
    {
        if (start < 0.0 || duration <= 0.05)
            return false;


        if (overlaps(start, duration, startTimeSec, durationSec))
            return false;
        for (const auto& t : timesList)
        {
            if (overlaps(start, duration, t.startTimeSec, t.durationSec))
                return false;
        }

        timesList.push_back({ start, duration });
        return true;
    }

    void resetPlaybackState()
    {
        hasStarted  = false;
        hasFinished = false;
        isPlaying   = false;
        currentKeyframeIndex = 0;
        loopIterationsFired  = 0;
        wasMutedLastTick     = false;

        triggeredKeyframes.clear();
        if (hasRecordedMovement && !recordedMovement.empty())
        {
            triggeredKeyframes.resize(recordedMovement.size(), false);
        }
    }

    /// User-facing display name like "Violin 1", "Piano 3".
    ///
    /// The number is the 1-based position of this block in the list of all
    /// blocks of the *same type* (preserving insertion order).  This is what
    /// the user sees in the sidebar / timeline / info panel; the internal
    /// `serial` field is still the stable unique ID used by the sequencer.
    ///
    /// Deletion automatically renumbers, because we recompute from the
    /// current vector every time the list is rebuilt.
    static juce::String displayName(const BlockEntry& block,
                                    const std::vector<BlockEntry>& allBlocks)
    {
        int ordinal = 0;
        for (const auto& b : allBlocks)
        {
            if (b.blockType == block.blockType)
            {
                ++ordinal;
                if (b.serial == block.serial)
                    break;
            }
        }
        if (ordinal == 0) ordinal = 1;   // fallback if not found

        return juce::String(blockTypeDisplayName(block.blockType))
             + " " + juce::String(ordinal);
    }

    static Vec3f getBlockColor(BlockType type, int soundId)
    {
        // Custom blocks vary by soundId so different user WAVs look distinct.
        if (type == BlockType::Custom)
        {
            static const Vec3f kPalette[] = {
                { 0.92f, 0.92f, 0.92f },   // white
                { 0.95f, 0.85f, 0.20f },   // yellow
                { 0.20f, 0.85f, 0.85f },   // cyan
                { 0.85f, 0.38f, 0.85f },   // magenta
                { 0.95f, 0.55f, 0.18f },   // orange
                { 0.65f, 0.48f, 0.90f },   // purple
            };
            constexpr int kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);
            int idx = ((soundId % kPaletteSize) + kPaletteSize) % kPaletteSize;
            return kPalette[idx];
        }

        // Every other type: delegate to the canonical color helper in BlockType.h.
        auto c = blockTypeColor(type);
        return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() };
    }
};
