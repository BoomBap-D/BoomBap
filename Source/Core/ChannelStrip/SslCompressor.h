#pragma once
#include <cmath>
#include <algorithm>

namespace boombap
{
// VCA-style feed-forward compressor in the SSL mould: fast peak detector,
// soft-knee gain computer in dB, attack/release smoothing of the gain
// reduction, optional program-dependent release, detector high-pass, and a
// dry/wet mix for parallel compression. Mono, or stereo-linked. No JUCE dependency.
class SslCompressor
{
public:
    struct Params
    {
        float thresholdDb     = -18.0f;
        float ratio           = 4.0f;
        float attackMs        = 10.0f;
        float releaseMs       = 100.0f;
        bool  autoRelease     = false;  // longer release when reducing harder
        float kneeDb          = 2.0f;
        float makeupDb        = 0.0f;
        float sidechainHpfHz  = 0.0f;   // 0 = off; keeps bass from over-triggering
        float mix             = 1.0f;   // 1 = fully compressed
    };

    void prepare (double sampleRate) { sr = sampleRate; setParams (params); reset(); }

    void reset() { env = 0.0f; grDb = 0.0f; hpL = hpR = 0.0f; }

    void setParams (const Params& p)
    {
        params = p;
        attackCoef  = coefFor (p.attackMs);
        releaseCoef = coefFor (p.releaseMs);
        detDecay    = std::exp (-1.0f / (0.015f * (float) sr)); // 15 ms peak hold-off
        hpA         = p.sidechainHpfHz > 0.0f
                        ? 1.0f - std::exp (-6.28318530718f * p.sidechainHpfHz / (float) sr) : 0.0f;
        makeup      = std::pow (10.0f, p.makeupDb / 20.0f);
    }

    float process (float x)
    {
        const float g = updateGain (std::abs (sidechain (x, hpL)));
        return x * (1.0f + (g - 1.0f) * params.mix) * makeup;
    }

    void processStereo (float& l, float& r)
    {
        const float det = std::max (std::abs (sidechain (l, hpL)), std::abs (sidechain (r, hpR)));
        const float g = updateGain (det);
        const float k = (1.0f + (g - 1.0f) * params.mix) * makeup;
        l *= k; r *= k;
    }

    float getGainReductionDb() const { return grDb; } // 0 or negative

private:
    float sidechain (float x, float& hp) const
    {
        if (hpA <= 0.0f) return x;
        hp += hpA * (x - hp);
        return x - hp;
    }

    float coefFor (float ms) const
    {
        return std::exp (-1.0f / (std::max (0.05f, ms) * 0.001f * (float) sr));
    }

    float updateGain (float level)
    {
        env = std::max (level, env * detDecay);
        const float inDb = 20.0f * std::log10 (std::max (env, 1.0e-6f));

        // soft-knee gain computer
        const float over = inDb - params.thresholdDb;
        const float w = std::max (0.0f, params.kneeDb);
        float outDb;
        if (2.0f * over < -w)            outDb = inDb;
        else if (2.0f * std::abs (over) <= w)
        {
            const float t = over + w * 0.5f;
            outDb = inDb + (1.0f / params.ratio - 1.0f) * t * t / (2.0f * w);
        }
        else                             outDb = params.thresholdDb + over / params.ratio;

        const float target = outDb - inDb; // <= 0

        float coef;
        if (target < grDb)
            coef = attackCoef;
        else
            coef = params.autoRelease
                     ? coefFor (100.0f + 1100.0f * std::min (1.0f, -grDb / 12.0f))
                     : releaseCoef;
        grDb = coef * grDb + (1.0f - coef) * target;

        return std::pow (10.0f, grDb / 20.0f);
    }

    Params params;
    double sr = 44100.0;
    float env = 0, grDb = 0, hpL = 0, hpR = 0, hpA = 0, makeup = 1;
    float attackCoef = 0, releaseCoef = 0, detDecay = 0;
};
} // namespace boombap
