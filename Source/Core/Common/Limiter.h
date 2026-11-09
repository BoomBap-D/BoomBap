#pragma once
#include <cmath>
#include <algorithm>

namespace boombap
{
// Brick-wall-style peak limiter: a fast peak envelope runs ahead of a
// gain computer that never lets the output past the ceiling. Attack is
// effectively instant (a short smoothing keeps it click-free); release
// is programme-friendly. Mono; link two instances by sharing a gain if
// ever needed - BoomBap's channels are mono, and the master instance
// processes both sides with one gain (see processStereo). No JUCE.
class Limiter
{
public:
    struct Params
    {
        float ceilingDb = -1.0f;   // the wall: output peaks stop here
        float releaseMs = 120.0f;
    };

    void prepare (double sampleRate) { sr = sampleRate; setParams (params); reset(); }

    void reset() { env = 0.0f; gain = 1.0f; grDb = 0.0f; }

    void setParams (const Params& p)
    {
        params = p;
        ceiling = std::pow (10.0f, p.ceilingDb / 20.0f);
        relCoef = std::exp (-1.0f / (std::max (1.0f, p.releaseMs) * 0.001f * (float) sr));
        // ~0.05 ms attack smoothing on the gain itself: fast enough to
        // catch a peak within a sample or two, smooth enough not to zip.
        atkCoef = std::exp (-1.0f / (0.05f * 0.001f * (float) sr));
    }

    float process (float x)
    {
        const float target = gainFor (std::abs (x));
        gain = smoothGain (target);
        grDb = 20.0f * std::log10 (std::max (gain, 1.0e-6f));
        return x * gain;
    }

    void processStereo (float& l, float& r)
    {
        const float target = gainFor (std::max (std::abs (l), std::abs (r)));
        gain = smoothGain (target);
        grDb = 20.0f * std::log10 (std::max (gain, 1.0e-6f));
        l *= gain;
        r *= gain;
    }

    float getGainReductionDb() const { return grDb; } // 0 or negative

private:
    float gainFor (float peak)
    {
        // peak envelope: instant rise, release decay - the gain is taken
        // from the envelope, so a peak holds the gain down briefly.
        env = std::max (peak, env * relCoef);
        return env > ceiling ? ceiling / env : 1.0f;
    }

    float smoothGain (float target)
    {
        if (target < gain) // attack: move fast
            return atkCoef * gain + (1.0f - atkCoef) * target;
        return gain + (target - gain) * 0.02f; // release rides the envelope
    }

    Params params;
    double sr = 44100.0;
    float ceiling = 0.89f, relCoef = 0.999f, atkCoef = 0.1f;
    float env = 0.0f, gain = 1.0f, grDb = 0.0f;
};
} // namespace boombap
