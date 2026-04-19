// SequencerEngine.cpp
#include "MathUtils.h"        // Vec3i — must precede BlockEntry.h
#include "SequencerEngine.h"

std::vector<SequencerEvent> SequencerEngine::update(const TransportClock&    clock,
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

        // ── Start ─────────────────────────────────────────────────────────────
        if (!block.hasStarted && now >= block.startTimeSec)
        {
            block.hasStarted = true;
            block.isPlaying  = true;

            SequencerEvent ev;
            ev.type           = SequencerEventType::Start;
            ev.blockSerial    = block.serial;
            ev.soundId        = block.soundId;
            ev.triggerTimeSec = now;
            ev.blockX         = static_cast<float>(block.pos.x);
            ev.blockY         = static_cast<float>(block.pos.y);
            ev.blockZ         = static_cast<float>(block.pos.z);
            eventBuffer_.push_back(ev);
        }

        // ── Movement keyframes ────────────────────────────────────────────────
        if (block.hasRecordedMovement && block.hasStarted && !block.hasFinished)
        {
            double relativeTime = now - block.startTimeSec;

            if (block.triggeredKeyframes.size() != block.recordedMovement.size())
            {
                block.triggeredKeyframes.resize(block.recordedMovement.size(), false);
            }
            
            for (size_t i = block.currentKeyframeIndex; i < block.recordedMovement.size(); ++i)
            {
                const auto& kf = block.recordedMovement[i];
                
                if (relativeTime >= kf.timeSec && !block.triggeredKeyframes[i])
                {
                    block.currentKeyframeIndex = i;
                    block.triggeredKeyframes[i] = true;
                    
                    // Create movement event
                    SequencerEvent ev;
                    ev.type           = SequencerEventType::Movement;  // New event type!
                    ev.blockSerial    = block.serial;
                    ev.soundId        = block.soundId;
                    ev.triggerTimeSec = now;
                    ev.blockX         = static_cast<float>(kf.position.x);
                    ev.blockY         = static_cast<float>(kf.position.y);
                    ev.blockZ         = static_cast<float>(kf.position.z);
                    eventBuffer_.push_back(ev);
                }
            }
        }

        // ── Stop ──────────────────────────────────────────────────────────────
        if (block.hasStarted && !block.hasFinished && now >= block.endTimeSec())
        {
            block.hasFinished = true;
            block.isPlaying   = false;

            SequencerEvent ev;
            ev.type           = SequencerEventType::Stop;
            ev.blockSerial    = block.serial;
            ev.soundId        = block.soundId;
            ev.triggerTimeSec = now;
            eventBuffer_.push_back(ev);
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
    for (auto& b : blocks)
        b.resetPlaybackState();
}