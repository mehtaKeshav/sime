#include "AudioAnalysis.h"

#include <cmath>
#include <algorithm>

namespace
{
    constexpr float kMinPitchHz = 55.f;
    constexpr float kMaxPitchHz = 2200.f;

    float mixToMono(const juce::AudioBuffer<float>& buffer, int index)
    {
        float sum = 0.f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            sum += buffer.getSample(ch, index);
        return sum / static_cast<float>(juce::jmax(1, buffer.getNumChannels()));
    }

    float rmsInRange(const std::vector<float>& mono, int start, int end)
    {
        if (end <= start) return 0.f;
        double acc = 0.0;
        for (int i = start; i < end; ++i)
            acc += static_cast<double>(mono[static_cast<size_t>(i)])
                 * static_cast<double>(mono[static_cast<size_t>(i)]);
        return static_cast<float>(std::sqrt(acc / static_cast<double>(end - start)));
    }

    float estimateFundamental(const std::vector<float>& mono, double sampleRateHz)
    {
        const int n = static_cast<int>(mono.size());
        if (n < 256 || sampleRateHz <= 0.0)
            return 0.f;

        // Use the loudest 40% window (skip long silence at start/end).
        const int win = juce::jmin(n / 4, 8192);
        int bestStart = 0;
        float bestRms = 0.f;
        for (int s = 0; s + win < n; s += win / 4)
        {
            const float r = rmsInRange(mono, s, s + win);
            if (r > bestRms) { bestRms = r; bestStart = s; }
        }

        if (bestRms < 1e-4f)
            return 0.f;

        const int start = bestStart;
        const int len   = juce::jmin(win, n - start);

        const int minLag = juce::jmax(2, static_cast<int>(sampleRateHz / kMaxPitchHz));
        const int maxLag = juce::jmin(len / 2,
                                       static_cast<int>(sampleRateHz / kMinPitchHz));

        float bestCorr = 0.f;
        int   bestLag  = 0;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double corr   = 0.0;
            double energy = 0.0;
            for (int i = 0; i < len - lag; ++i)
            {
                const double a = mono[static_cast<size_t>(start + i)];
                const double b = mono[static_cast<size_t>(start + i + lag)];
                corr   += a * b;
                energy += a * a;
            }
            if (energy < 1e-8)
                continue;

            const float ncorr = static_cast<float>(corr / energy);
            if (ncorr > bestCorr)
            {
                bestCorr = ncorr;
                bestLag  = lag;
            }
        }

        if (bestLag <= 0 || bestCorr < 0.35f)
            return 0.f;

        return static_cast<float>(sampleRateHz / static_cast<double>(bestLag));
    }

    void buildWaveformEnvelope(const std::vector<float>& mono,
                               AudioAnalysisResult& out)
    {
        const int cols = AudioAnalysisResult::kWaveformColumns;
        out.waveformMin.resize(static_cast<size_t>(cols));
        out.waveformMax.resize(static_cast<size_t>(cols));

        const int n = static_cast<int>(mono.size());
        if (n <= 0)
            return;

        for (int c = 0; c < cols; ++c)
        {
            const int i0 = (c * n) / cols;
            const int i1 = ((c + 1) * n) / cols;
            float mn = 1.f, mx = -1.f;
            for (int i = i0; i < i1; ++i)
            {
                const float s = mono[static_cast<size_t>(i)];
                mn = std::min(mn, s);
                mx = std::max(mx, s);
            }
            if (i1 <= i0)
            {
                mn = 0.f;
                mx = 0.f;
            }
            out.waveformMin[static_cast<size_t>(c)] = mn;
            out.waveformMax[static_cast<size_t>(c)] = mx;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

juce::String AudioAnalysis::frequencyToNoteName(float hz) noexcept
{
    if (hz < 20.f)
        return {};

    const float midi = 69.f + 12.f * std::log2(hz / 440.f);
    const int rounded = static_cast<int>(std::lround(midi));
    const int noteIdx = ((rounded % 12) + 12) % 12;
    const int octave  = rounded / 12 - 1;

    static const char* kNames[] =
    {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    return juce::String(kNames[noteIdx]) + juce::String(octave);
}

AudioAnalysisResult AudioAnalysis::analyze(const juce::AudioBuffer<float>& buffer,
                                           double sampleRateHz)
{
    AudioAnalysisResult out;
    out.sampleRateHz = sampleRateHz;

    const int n = buffer.getNumSamples();
    if (n <= 0 || sampleRateHz <= 0.0)
        return out;

    out.durationSec = static_cast<double>(n) / sampleRateHz;
    out.valid       = true;

    std::vector<float> mono(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        mono[static_cast<size_t>(i)] = mixToMono(buffer, i);

    buildWaveformEnvelope(mono, out);

    const float f0 = estimateFundamental(mono, sampleRateHz);
    if (f0 >= kMinPitchHz && f0 <= kMaxPitchHz)
    {
        out.fundamentalHz = f0;
        out.pitchReliable = true;
        out.noteName      = frequencyToNoteName(f0);

        out.pitchLabel = juce::String(f0, 1) + " Hz"
                       + (out.noteName.isNotEmpty()
                              ? juce::String("  (") + out.noteName + ")"
                              : juce::String());
    }
    else
    {
        out.pitchReliable = false;
        out.pitchLabel    = "No clear pitch (percussive or noisy)";
    }

    return out;
}
