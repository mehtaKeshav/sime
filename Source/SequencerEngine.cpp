// SequencerEngine.cpp
#include "MathUtils.h"        // Vec3i — must precede BlockEntry.h
#include "SequencerEngine.h"

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
                              std::vector<SequencerEvent>& eventBuffer)
{
    const double endTime = startTime + duration;

    // START
    if (!hasStarted && now >= startTime)
    {
        hasStarted = true;
        isPlaying = true;
        loopIterationsFired = 1;

        SequencerEvent ev;
        ev.type           = SequencerEventType::Start;
        ev.blockSerial    = block.serial;
        ev.soundId        = block.soundId;
        ev.triggerTimeSec = now;
        ev.blockX         = static_cast<float>(block.pos.x);
        ev.blockY         = static_cast<float>(block.pos.y);
        ev.blockZ         = static_cast<float>(block.pos.z);

        eventBuffer.push_back(ev);
    }

    // LOOP RETRIGGER
    if (block.isLooping &&
        hasStarted &&
        !hasFinished &&
        duration > 0.001)
    {
        const double playbackEnd = startTime + block.loopDurationSec;
        const double relTime = now - startTime;

        const int expectedIterations =
            static_cast<int>(relTime / duration) + 1;

        while (loopIterationsFired < expectedIterations)
        {
            const double iterStart =
                startTime + loopIterationsFired * duration;

            if (iterStart >= playbackEnd)
                break;

            SequencerEvent stopEv;
            stopEv.type           = SequencerEventType::Stop;
            stopEv.blockSerial    = block.serial;
            stopEv.soundId        = block.soundId;
            stopEv.triggerTimeSec = iterStart;
            eventBuffer.push_back(stopEv);

            SequencerEvent startEv;
            startEv.type           = SequencerEventType::Start;
            startEv.blockSerial    = block.serial;
            startEv.soundId        = block.soundId;
            startEv.triggerTimeSec = iterStart;
            startEv.blockX         = static_cast<float>(block.pos.x);
            startEv.blockY         = static_cast<float>(block.pos.y);
            startEv.blockZ         = static_cast<float>(block.pos.z);
            eventBuffer.push_back(startEv);

            ++loopIterationsFired;
        }
    }

    // MOVEMENT KEYFRAMES
    if (block.hasRecordedMovement &&
        hasStarted &&
        !hasFinished)
    {
        const double relativeTime = now - startTime;

        if (triggeredKeyframes.size() != block.recordedMovement.size())
            triggeredKeyframes.resize(block.recordedMovement.size(), false);

        for (size_t i = static_cast<size_t>(currentKeyframeIndex);
             i < block.recordedMovement.size();
             ++i)
        {
            const auto& kf = block.recordedMovement[i];

            if (relativeTime >= kf.timeSec &&
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
                ev.blockY         = static_cast<float>(kf.position.y);
                ev.blockZ         = static_cast<float>(kf.position.z);

                eventBuffer.push_back(ev);
            }
        }
    }

    // FINAL STOP
    if (hasStarted &&
        !hasFinished &&
        now >= endTime)
    {
        hasFinished = true;
        isPlaying = false;

        SequencerEvent ev;
        ev.type           = SequencerEventType::Stop;
        ev.blockSerial    = block.serial;
        ev.soundId        = block.soundId;
        ev.triggerTimeSec = now;

        eventBuffer.push_back(ev);
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
        // Skip blocks without recorded movement
        if (!block.hasRecordedMovement || block.recordedMovement.empty())
            continue;
        
        // Skip if block hasn't started playing yet
        if (!block.hasStarted || block.hasFinished)
            continue;
        
        // Calculate time relative to block start
        double relativeTime = currentTime - block.startTimeSec;
        
        // Find the appropriate keyframe for current time
        for (size_t i = 0; i < block.recordedMovement.size(); ++i)
        {
            const auto& keyframe = block.recordedMovement[i];
            
            // Check if we've reached this keyframe's time
            if (relativeTime >= keyframe.timeSec)
            {
                // Update to this keyframe's position if we haven't already
                if (block.currentKeyframeIndex < i)
                {
                    block.currentKeyframeIndex = i;
                    block.pos = keyframe.position;
                    
                    // DBG("Block " << block.serial << " moved to keyframe " << i 
                    //     << " at position (" << keyframe.position.x << "," 
                    //     << keyframe.position.y << "," << keyframe.position.z << ")");
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
