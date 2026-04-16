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

#include "BlockType.h"
#include <string>

struct BlockEntry
{
    // ── Identity ──────────────────────────────────────────────────────────────
    int       serial    = 0;
    BlockType blockType = BlockType::Violin;
    Vec3i     pos;               ///< Requires Vec3i from MathUtils.h

    // ── Audio mapping ─────────────────────────────────────────────────────────
    int         soundId        = -1;   ///< -1 = silent / unassigned
    std::string customFilePath;        ///< Non-empty for Custom blocks with user WAV

    // ── Timing (seconds relative to transport origin) ─────────────────────────
    double startTimeSec = 0.0;
    double durationSec  = 1.0;

    // ── Playback state (written by SequencerEngine each frame) ────────────────
    bool hasStarted  = false;
    bool hasFinished = false;
    bool isPlaying   = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    double endTimeSec() const noexcept { return startTimeSec + durationSec; }

    void resetPlaybackState() noexcept
    {
        hasStarted  = false;
        hasFinished = false;
        isPlaying   = false;
    }
};