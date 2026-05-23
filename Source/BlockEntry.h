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
    bool   isLooping           = false;  ///< When true, re-trigger every durationSec
    double loopDurationSec     = 4.0;    ///< Total wallclock seconds the loop runs
    int    loopIterationsFired = 0;      ///< Runtime: count of Start events fired this play

    // Recording state
    bool isRecordingMovement = false;
    double recordingStartTime = 0.0;
    Vec3i recordingStartPos;

    // recorded movement data
    bool hasRecordedMovement = false; ///< Whether this block has any recorded movement keyframes
    std::vector<MovementKeyFrame> recordedMovement; ///< Optional per-block movement path for sequenced motion
    bool durationLocked = false;

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
