#pragma once
// Offline (faster-than-realtime) render of the pattern, for WAV export.
// Pure C++ (no JUCE). Uses the same engine as live playback, so an export
// sounds exactly like what you hear: swing, dust, glue, mutes, sample pads.
// The live vocal channel is never part of an export; a recorded vocal take is,
// whenever the take is switched on.

#include "BoomBapEngine.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace boombap
{
// Returns { left, right }. The pattern plays for `bars` bars, then the engine keeps
// running for tailSeconds so the last hits ring out. normalize = true scales the
// peak to -1 dBFS.
inline std::vector<std::vector<float>> renderOffline (BoomBapEngine& e, int bars, double sampleRate,
                                                      float tailSeconds, bool normalize)
{
    e.prepare (sampleRate);
    e.setVocalEnabled (false);

    const double samplesPerTick = sampleRate * 60.0 / ((double) e.getBpm() * kPPQN);
    const int patternSamples = (int) std::llround ((double) std::max (1, bars) * 4 * kPPQN * samplesPerTick);
    const int tailSamples    = (int) std::llround ((double) std::max (0.0f, tailSeconds) * sampleRate);
    const int total          = patternSamples + tailSamples;

    std::vector<float> left ((size_t) total), right ((size_t) total);

    constexpr int kBlock = 512;
    auto run = [&] (int from, int to)
    {
        for (int pos = from; pos < to; pos += kBlock)
            e.process (nullptr, left.data() + pos, right.data() + pos, std::min (kBlock, to - pos));
    };

    e.setPlaying (true);
    run (0, patternSamples);              // the pattern, stopping exactly at the bar line
    e.setPlaying (false);
    run (patternSamples, total);          // tail: voices, dust and glue ring out

    if (normalize)
    {
        float peak = 0.0f;
        for (size_t i = 0; i < left.size(); ++i)
            peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
        if (peak > 1.0e-6f)
        {
            const float g = 0.891f / peak; // -1 dBFS
            for (size_t i = 0; i < left.size(); ++i) { left[i] *= g; right[i] *= g; }
        }
    }
    return { std::move (left), std::move (right) };
}
} // namespace boombap
