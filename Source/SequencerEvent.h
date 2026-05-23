#pragma once
 
// ---------------------------------------------------------------------------
// SequencerEvent
//
// A lightweight value type produced by SequencerEngine and consumed by
// AudioEngine.  Carries just enough info to act on a voice without coupling
// the two systems together.
// ---------------------------------------------------------------------------
 
enum class SequencerEventType
{
    Start,
    Stop,
    Movement  // ← ADD THIS
};
struct SequencerEvent
{
    SequencerEventType type    = SequencerEventType::Start;
    int blockSerial            = -1;  ///< Identifies which block triggered this event
    int soundId                = -1;  ///< Which sample to play / stop
    double triggerTimeSec      = 0.0; ///< Transport time at which the event fired
    float blockX               = 0.0f; ///< Block grid X (used for stereo pan)
    float blockY               = 0.0f; ///< Block grid Y (used for pitch)
    float blockZ               = 0.0f; ///< Block grid Z (used for proximity/gain)

    // ── Phase 1 playback mode ──────────────────────────────────────────────
    /// If true, the voice's sample buffer wraps around when it reaches the
    /// end instead of finishing.  Stops only on a matching Stop event.
    bool  loopBuffer           = false;

    /// Per-block playback rate multiplier applied on top of the spatial Y
    /// pitch and the global fast-forward speed.  1.0 = use sample rate as-is.
    float playbackRateOverride = 1.0f;

    /// Silence inserted between successive plays of the sample when looping.
    /// 0 = tight loop.  Audio engine converts to a sample-count countdown.
    float loopBufferSec        = 0.0f;

    // ── Doppler (Phase 4) ────────────────────────────────────────────────────
    /// Source velocity in world units per second (grid units / second).
    /// AudioEngine combines this with the listener position to compute the
    /// Doppler rate multiplier for the voice.
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float velocityZ = 0.0f;

    /// When false, the AudioEngine treats source velocity as zero (no
    /// Doppler shift).  Set on Start events for the initial state and on
    /// Movement events when a new velocity has been computed from the path.
    bool  hasVelocity = false;
};