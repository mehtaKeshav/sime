// SequencerEngine.cpp
#include "MathUtils.h"        // Vec3i — must precede BlockEntry.h
#include "SequencerEngine.h"

namespace
{
    /// Compute the per-block playback rate for the current mode.  Uses the
    /// sample's natural length (in seconds) vs the region duration.
    float computeBlockRate(BlockPlaybackMode mode,
                           double regionDurationSec,
                           double sampleNaturalSec)
    {
        if (regionDurationSec <= 0.001 || sampleNaturalSec <= 0.001)
            return 1.0f;

        const double ratio = sampleNaturalSec / regionDurationSec;

        switch (mode)
        {
            case BlockPlaybackMode::Stretch:
                // Want sound to take `regionDurationSec` -> rate < 1.0 when
                // sound is shorter (slow it down) and > 1 when longer (compress).
                return juce::jlimit(0.1f, 8.0f, static_cast<float>(ratio));

            case BlockPlaybackMode::Speed:
                // Force sound to finish inside the region (only meaningful when
                // sound is longer than region).  Same formula, different intent.
                return juce::jlimit(1.0f, 8.0f, static_cast<float>(ratio));

            case BlockPlaybackMode::Natural:
            case BlockPlaybackMode::Loop:
            default:
                return 1.0f;
        }
    }
}

namespace
{
    /// Velocity between two recorded keyframes, scaled from the recorded path
    /// timeline onto the user-chosen movement playback duration.  Returns
    /// (0,0,0) when the inputs are degenerate.
    void computeKeyframeVelocity(const BlockEntry& block,
                                 size_t i,
                                 float& vx, float& vy, float& vz)
    {
        vx = vy = vz = 0.0f;
        const auto& path = block.recordedMovement;
        if (path.size() < 2 || i == 0 || i >= path.size())
            return;

        const auto& a = path[i - 1];
        const auto& b = path[i];
        const double dt = b.timeSec - a.timeSec;
        if (dt <= 1e-4)
            return;

        const double recordedSpan = path.back().timeSec;
        const double targetSpan   = block.effectiveMovementDuration();
        const double playbackDt   = (targetSpan > 0.001 && recordedSpan > 0.001)
            ? (dt * targetSpan / recordedSpan)
            : dt;
        if (playbackDt <= 1e-4)
            return;

        vx = static_cast<float>((b.position.x - a.position.x) / playbackDt);
        vy = static_cast<float>((b.position.y - a.position.y) / playbackDt);
        vz = static_cast<float>((b.position.z - a.position.z) / playbackDt);
    }
}

/// Build a Start event reflecting the block's *current* runtime state.  Used
/// for both the natural region-entry Start and for "resume after mute window"
/// re-starts.
static SequencerEvent buildStartEvent(const BlockEntry& block,
                                      double now,
                                      bool loopBuf,
                                      float blockRate)
{
    SequencerEvent ev;
    ev.type                 = SequencerEventType::Start;
    ev.blockSerial          = block.serial;
    ev.soundId              = block.soundId;
    ev.triggerTimeSec       = now;
    ev.blockX               = static_cast<float>(block.pos.x);
    ev.blockY               = static_cast<float>(block.pos.y);
    ev.blockZ               = static_cast<float>(block.pos.z);
    ev.loopBuffer           = loopBuf;
    ev.playbackRateOverride = blockRate;
    ev.loopBufferSec        = static_cast<float>(std::max(0.0, block.loopBufferSec));

    if (block.hasRecordedMovement
        && block.movementEnabled
        && block.recordedMovement.size() >= 2)
    {
        computeKeyframeVelocity(block, 1,
                                ev.velocityX, ev.velocityY, ev.velocityZ);
        ev.hasVelocity = true;
    }
    return ev;
}

static void processOccurrence(BlockEntry& block,
                              double startTime,
                              double duration,
                              bool& hasStarted,
                              bool& hasFinished,
                              bool& isPlaying,
                              int& loopIterationsFired,
                              std::vector<bool>& triggeredKeyframes,
                              size_t currentKeyframeIndex,
                              double now,
                              double sampleNaturalSec,
                              bool   isMutedNow,
                              std::vector<SequencerEvent>& eventBuffer)
{
    // Resolve effective playback mode.  Old .sime files used the legacy
    // `isLooping` flag; treat that as the new Loop mode for back-compat.
    BlockPlaybackMode mode = block.playbackMode;
    if (mode == BlockPlaybackMode::Natural && block.isLooping)
        mode = BlockPlaybackMode::Loop;

    const float blockRate = computeBlockRate(mode, duration, sampleNaturalSec);
    const bool  loopBuf   = (mode == BlockPlaybackMode::Loop);

    // ── Loop duration: when isLooping/Loop mode is on, allow the user to
    //    stop the loop early via b.loopDurationSec (anything > 0 and < the
    //    region duration shortens the audible window; 0 or >= duration =
    //    fill the whole region, which is the default).  Movement keeps
    //    running through the *region* duration regardless.
    double audioStopAt = startTime + duration;
    if (loopBuf
        && block.loopDurationSec > 0.001
        && block.loopDurationSec < duration)
    {
        audioStopAt = startTime + block.loopDurationSec;
    }
    const double endTime = startTime + duration;

    // START — runs the block's state machine even when muted, so movement
    // can keep animating; we just suppress the audio Start event.
    if (!hasStarted && now >= startTime)
    {
        hasStarted = true;
        isPlaying  = true;
        loopIterationsFired = 1;

        if (!isMutedNow)
            eventBuffer.push_back(buildStartEvent(block, now, loopBuf, blockRate));
    }

    // The new Loop mode handles all looping inside the audio thread (sample
    // buffer wraps).  The legacy sequencer-retrigger path is intentionally
    // gone — it had gaps and assumed durationSec equalled the sound length.

    // MOVEMENT KEYFRAMES
    if (block.hasRecordedMovement &&
        block.movementEnabled &&
        hasStarted &&
        !hasFinished)
    {
        // Map transport time onto the recorded movement timeline.  When the
        // user gives the path a different `movementDurationSec`, we scale.
        const double recordedSpan = block.recordedMovement.empty()
            ? 0.0
            : block.recordedMovement.back().timeSec;
        const double targetSpan   = block.effectiveMovementDuration();

        const double rawRelative  = now - startTime;
        const double playbackTime = (targetSpan > 0.001 && recordedSpan > 0.001)
            ? (rawRelative * recordedSpan / targetSpan)
            : rawRelative;

        if (triggeredKeyframes.size() != block.recordedMovement.size())
            triggeredKeyframes.resize(block.recordedMovement.size(), false);

        for (size_t i = static_cast<size_t>(currentKeyframeIndex);
             i < block.recordedMovement.size();
             ++i)
        {
            const auto& kf = block.recordedMovement[i];

            if (playbackTime >= kf.timeSec &&
                !triggeredKeyframes[i])
            {
                currentKeyframeIndex = static_cast<int>(i);
                triggeredKeyframes[i] = true;

                SequencerEvent ev;
                ev.type           = SequencerEventType::Movement;
                ev.blockSerial    = block.serial;
                ev.soundId        = block.soundId;
                ev.triggerTimeSec = now;
                ev.blockX         = static_cast<float>(kf.position.x);
                ev.blockY         = static_cast<float>(kf.position.y + block.movementYOffset);
                ev.blockZ         = static_cast<float>(kf.position.z);

                computeKeyframeVelocity(block, i,
                                        ev.velocityX, ev.velocityY, ev.velocityZ);
                ev.hasVelocity = true;

                eventBuffer.push_back(ev);
            }
        }
    }

    // EARLY AUDIO STOP — for loops cut off before the region ends.
    // Hidden behind hasStarted so we never preempt the natural Start.
    if (loopBuf && hasStarted && !hasFinished && now >= audioStopAt && audioStopAt < endTime)
    {
        if (!isMutedNow)
        {
            SequencerEvent ev;
            ev.type           = SequencerEventType::Stop;
            ev.blockSerial    = block.serial;
            ev.soundId        = block.soundId;
            ev.triggerTimeSec = now;
            eventBuffer.push_back(ev);
        }
        // NOTE: don't flip hasFinished here — the region still owns Movement
        // events until endTime.  We just kill the voice early.
    }

    // FINAL STOP
    if (hasStarted &&
        !hasFinished &&
        now >= endTime)
    {
        hasFinished = true;
        isPlaying = false;

        if (!isMutedNow)
        {
            SequencerEvent ev;
            ev.type           = SequencerEventType::Stop;
            ev.blockSerial    = block.serial;
            ev.soundId        = block.soundId;
            ev.triggerTimeSec = now;
            eventBuffer.push_back(ev);
        }
    }
}


std::vector<SequencerEvent> SequencerEngine::update(const TransportClock& clock,
                                                    std::vector<BlockEntry>& blocks)
{
    eventBuffer_.clear();

    if (!clock.isPlaying())
        return eventBuffer_;

    const double now = clock.currentTimeSec();

    for (auto& block : blocks)
    {
        if (block.soundId < 0)
            continue;

        // ── Compute the block's current mute state ─────────────────────────
        //   * effectiveMuted = per-block isMuted + per-type indefinite mute
        //     (set by ViewPortComponent before calling update()).
        //   * Time-window mute kicks in when the playhead is inside
        //     [muteStartSec, muteEndSec).
        const bool windowMute = (block.muteEndSec > block.muteStartSec
                                 && now >= block.muteStartSec
                                 && now <  block.muteEndSec);
        const bool isMutedNow = block.effectiveMuted || windowMute;

        // Natural sample length — looked up by the engine, but we don't have
        // access to the sample library from here.  The caller (the GL render
        // path) is responsible for setting `sampleNaturalDurationSec` on the
        // block when the sample is loaded.  0 means "unknown" -> rate = 1.
        const double sampleSec = block.sampleNaturalDurationSec;

        // ── Mute-state transitions:  cut or resume the live voice ─────────
        // Only meaningful while the region is in flight (started, not done).
        if (isMutedNow != block.wasMutedLastTick)
        {
            const bool regionActive = block.hasStarted && !block.hasFinished;
            if (isMutedNow && regionActive)
            {
                SequencerEvent ev;
                ev.type           = SequencerEventType::Stop;
                ev.blockSerial    = block.serial;
                ev.soundId        = block.soundId;
                ev.triggerTimeSec = now;
                eventBuffer_.push_back(ev);
            }
            else if (!isMutedNow && regionActive)
            {
                BlockPlaybackMode mode = block.playbackMode;
                if (mode == BlockPlaybackMode::Natural && block.isLooping)
                    mode = BlockPlaybackMode::Loop;

                const float rate = computeBlockRate(mode, block.durationSec, sampleSec);
                const bool  loopBuf = (mode == BlockPlaybackMode::Loop);
                eventBuffer_.push_back(buildStartEvent(block, now, loopBuf, rate));
            }
            block.wasMutedLastTick = isMutedNow;
        }

        // Original/default region
        processOccurrence(block,
                          block.startTimeSec,
                          block.durationSec,
                          block.hasStarted,
                          block.hasFinished,
                          block.isPlaying,
                          block.loopIterationsFired,
                          block.triggeredKeyframes,
                          block.currentKeyframeIndex,
                          now,
                          sampleSec,
                          isMutedNow,
                          eventBuffer_);

        // Copied/pasted regions
        for (auto& t : block.timesList)
        {
            processOccurrence(block,
                              t.startTimeSec,
                              t.durationSec,
                              t.hasStarted,
                              t.hasFinished,
                              t.isPlaying,
                              t.loopIterationsFired,
                              t.triggeredKeyframes,
                              t.currentKeyframeIndex,
                              now,
                              sampleSec,
                              isMutedNow,
                              eventBuffer_);
        }
    }

    return eventBuffer_;
}


void SequencerEngine::updateBlockMovement(std::vector<BlockEntry>& blocks,
                                          double currentTime)
{
    for (auto& block : blocks)
    {
        if (!block.hasRecordedMovement || !block.movementEnabled
            || block.recordedMovement.empty())
            continue;

        if (!block.hasStarted || block.hasFinished)
            continue;

        // Scale time onto the recorded path span when the user has set a
        // custom movement duration (Phase 1 movement feature).
        const double recordedSpan = block.recordedMovement.back().timeSec;
        const double targetSpan   = block.effectiveMovementDuration();
        const double rawRel       = currentTime - block.startTimeSec;
        const double playbackTime = (targetSpan > 0.001 && recordedSpan > 0.001)
            ? (rawRel * recordedSpan / targetSpan)
            : rawRel;

        for (size_t i = 0; i < block.recordedMovement.size(); ++i)
        {
            const auto& keyframe = block.recordedMovement[i];

            if (playbackTime >= keyframe.timeSec)
            {
                if (block.currentKeyframeIndex < i)
                {
                    block.currentKeyframeIndex = i;
                    block.pos = {
                        keyframe.position.x,
                        keyframe.position.y + block.movementYOffset,
                        keyframe.position.z
                    };
                }
            }
        }
    }
}

void SequencerEngine::resetAllBlocks(std::vector<BlockEntry>& blocks) noexcept
{
    for (auto& block : blocks)
    {
        block.resetPlaybackState();

        for (auto& t : block.timesList)
            t.resetPlaybackState();
    }
}

bool SequencerEngine::snapBlockPositionsToTime(std::vector<BlockEntry>& blocks,
                                               double timeSec) noexcept
{
    bool anyChanged = false;

    for (auto& b : blocks)
    {
        if (!b.hasRecordedMovement
            || !b.movementEnabled
            || b.recordedMovement.size() < 2)
            continue;

        // ── Determine which occurrence (main region or any timesList entry)
        // ── the scrub time falls inside.  When the playhead is outside every
        // ── occurrence we snap to whichever endpoint makes intuitive sense:
        // ──   * before any occurrence → first keyframe (block's start pos)
        // ──   * after every occurrence → last keyframe
        auto pickEndpoint = [&](Vec3i& out)
        {
            double minStart = b.startTimeSec;
            double maxEnd   = b.endTimeSec();
            for (const auto& t : b.timesList)
            {
                minStart = std::min(minStart, t.startTimeSec);
                maxEnd   = std::max(maxEnd,   t.endTimeSec());
            }
            const auto& first = b.recordedMovement.front().position;
            const auto& last  = b.recordedMovement.back().position;
            out = (timeSec >= maxEnd) ? last : first;
        };

        double regionStart  = 0.0;
        double regionDur    = 0.0;
        bool   inside       = false;

        if (timeSec >= b.startTimeSec && timeSec < b.startTimeSec + b.durationSec)
        {
            regionStart = b.startTimeSec;
            regionDur   = b.durationSec;
            inside      = true;
        }
        else
        {
            for (const auto& t : b.timesList)
            {
                if (timeSec >= t.startTimeSec && timeSec < t.startTimeSec + t.durationSec)
                {
                    regionStart = t.startTimeSec;
                    regionDur   = t.durationSec;
                    inside      = true;
                    break;
                }
            }
        }

        Vec3i target;
        if (!inside)
        {
            pickEndpoint(target);
        }
        else
        {
            // Map transport time onto the recorded keyframe timeline, honouring
            // the user's movement-duration override (Phase 1 feature).
            const double recordedSpan = b.recordedMovement.back().timeSec;
            const double targetSpan   = (b.movementDurationSec > 0.001)
                                          ? b.movementDurationSec
                                          : regionDur;

            const double rawRel = timeSec - regionStart;
            const double playbackTime = (targetSpan > 0.001 && recordedSpan > 0.001)
                ? (rawRel * recordedSpan / targetSpan)
                : rawRel;

            // Snap to the LATEST keyframe whose time has elapsed.  This is the
            // best discrete approximation given that keyframes carry integer
            // positions (no fractional grid coordinates).
            Vec3i hit = b.recordedMovement.front().position;
            for (const auto& kf : b.recordedMovement)
            {
                if (playbackTime + 1e-4 >= kf.timeSec)
                    hit = kf.position;
                else
                    break;
            }
            target = hit;
        }

        target.y += b.movementYOffset;

        if (target != b.pos)
        {
            b.pos = target;
            anyChanged = true;
        }
    }

    return anyChanged;
}
