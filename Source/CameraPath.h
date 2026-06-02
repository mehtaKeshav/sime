#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CameraPath.h
//
// Per-scene, time-keyed camera (listener) path.  A scene has zero or more
// CameraKeyframes; live playback and the offline exporter sample the path at
// the current transport time to compute the listener pose.
//
// Two keyframe modes — same idea as Blender's "constant" vs "linear"
// interpolation:
//
//   * Hold : pose is held verbatim until the next keyframe's time, then snaps
//            instantly to the next keyframe.  Lets the composer cut between
//            anchored viewpoints (e.g. orchestra front, then teleport behind
//            the cellos).
//   * Lerp : pose lerps smoothly toward the next keyframe.  R-recording emits
//            a stream of Lerp keyframes at the polling rate so the camera
//            replays the user's hand-flown motion.
//
// Yaw is lerped along the shortest arc so the camera does not unwind through
// a full circle when wrapping ±π.  Pitch and position are straight linear.
// Sample(t) returns the FIRST keyframe pose for t < first.timeSec and the
// LAST keyframe pose for t >= last.timeSec — i.e. the path holds at both
// ends, so callers do not need to worry about clamping the time themselves.
// ─────────────────────────────────────────────────────────────────────────────

#include "MathUtils.h"
#include <JuceHeader.h>
#include <vector>
#include <cstdint>

struct CameraKeyframe
{
    enum Mode : uint8_t { Hold = 0, Lerp = 1 };

    double  timeSec          = 0.0;
    Vec3f   pos              { 6.f, 6.f, -4.f };
    float   yawRad           = -2.3f;
    float   pitchRad         = -0.45f;
    uint8_t mode             = Hold;

    /// Only meaningful when mode == Hold.  Defines how long this static pose
    /// occupies the timeline.  Used by CameraPathUtil::effectiveEndTime() so
    /// the "+ Hold @ cam now" button can place new keyframes after the last
    /// segment ends (rather than overlapping it).  When 0 the hold runs
    /// straight up to the next keyframe (or forever if it's the last one).
    float   holdDurationSec  = 0.0f;
};

struct CameraPose
{
    Vec3f pos;
    float yawRad   = 0.f;
    float pitchRad = 0.f;
};

namespace CameraPathUtil
{
    inline CameraPose toPose(const CameraKeyframe& k)
    {
        return { k.pos, k.yawRad, k.pitchRad };
    }

    /// Sort keyframes by time (in place).
    inline void sortByTime(std::vector<CameraKeyframe>& path)
    {
        std::sort(path.begin(), path.end(),
                  [](const CameraKeyframe& a, const CameraKeyframe& b)
                  { return a.timeSec < b.timeSec; });
    }

    /// Last keyframe time, or 0.0 for empty paths.
    inline double endTime(const std::vector<CameraKeyframe>& path)
    {
        return path.empty() ? 0.0 : path.back().timeSec;
    }

    /// Time the path's last segment effectively ends — i.e. the earliest
    /// time a brand-new keyframe can be added without overlapping the
    /// existing material.  For a Hold tail with a positive holdDurationSec,
    /// that's `timeSec + holdDurationSec`; otherwise it's just `timeSec`.
    inline double effectiveEndTime(const std::vector<CameraKeyframe>& path)
    {
        if (path.empty()) return 0.0;
        const auto& last = path.back();
        if (last.mode == CameraKeyframe::Hold && last.holdDurationSec > 0.f)
            return last.timeSec + (double) last.holdDurationSec;
        return last.timeSec;
    }

    /// Sample the path at @p t.  Empty paths return @p defaultPose.  Yaw is
    /// shortest-arc lerped; positions and pitch are linear; Hold keyframes
    /// freeze their pose until the next keyframe time.
    inline CameraPose sample(const std::vector<CameraKeyframe>& path,
                              double                              t,
                              const CameraPose&                   defaultPose)
    {
        if (path.empty())            return defaultPose;
        if (t <= path.front().timeSec) return toPose(path.front());
        if (t >= path.back().timeSec)  return toPose(path.back());

        for (size_t i = 0; i + 1 < path.size(); ++i)
        {
            const auto& a = path[i];
            const auto& b = path[i + 1];
            if (t >= a.timeSec && t < b.timeSec)
            {
                if (a.mode == CameraKeyframe::Hold)
                    return toPose(a);

                const double dur = b.timeSec - a.timeSec;
                const float  u   = dur > 1e-6 ? (float) ((t - a.timeSec) / dur) : 0.f;

                float dy = b.yawRad - a.yawRad;
                while (dy >  juce::MathConstants<float>::pi)
                    dy -= 2.f * juce::MathConstants<float>::pi;
                while (dy < -juce::MathConstants<float>::pi)
                    dy += 2.f * juce::MathConstants<float>::pi;

                CameraPose p;
                p.pos.x    = a.pos.x + (b.pos.x - a.pos.x) * u;
                p.pos.y    = a.pos.y + (b.pos.y - a.pos.y) * u;
                p.pos.z    = a.pos.z + (b.pos.z - a.pos.z) * u;
                p.yawRad   = a.yawRad + dy * u;
                p.pitchRad = a.pitchRad + (b.pitchRad - a.pitchRad) * u;
                return p;
            }
        }
        return toPose(path.back());
    }

    /// Convert a CameraPose into normalised forward & right vectors using the
    /// same yaw / pitch convention as Camera::getForward / getRight.  Yaw 0
    /// looks down -Z; positive yaw rotates toward +X.
    inline void poseDirs(const CameraPose& p,
                         Vec3f&             outForward,
                         Vec3f&             outRight)
    {
        const float cp = std::cos(p.pitchRad);
        const float sp = std::sin(p.pitchRad);
        const float cy = std::cos(p.yawRad);
        const float sy = std::sin(p.yawRad);

        outForward = Vec3f{ cp * sy, sp, -cp * cy };
        outRight   = Vec3f{ cy,      0.f,  sy     };
    }
}
