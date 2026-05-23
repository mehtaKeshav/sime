#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AudioAnalysis.h  –  Offline analysis of loaded sample buffers for the UI.
// ─────────────────────────────────────────────────────────────────────────────

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

struct AudioAnalysisResult
{
    bool valid = false;

    float fundamentalHz   = 0.f;    ///< Estimated F0 (0 if unknown / percussive)
    bool  pitchReliable   = false;  ///< False for noise-like or very short clips
    juce::String noteName;          ///< e.g. "A4", empty when no pitch
    juce::String pitchLabel;        ///< Human-readable pitch line for the UI

    double durationSec    = 0.0;
    double sampleRateHz   = 44100.0;

    /// Oscilloscope envelope: per-column min/max in [-1, 1], same length.
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;

    static constexpr int kWaveformColumns = 128;
};

class AudioAnalysis
{
public:
    /// Analyze @p buffer (any channel count).  @p sampleRateHz is the rate the
    /// buffer was recorded at (file rate or synthesis rate).
    static AudioAnalysisResult analyze(const juce::AudioBuffer<float>& buffer,
                                       double sampleRateHz);

    static juce::String frequencyToNoteName(float hz) noexcept;
};
