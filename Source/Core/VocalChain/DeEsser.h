#pragma once
#include "../Common/Biquad.h"
#include <cmath>
#include <algorithm>

namespace boombap
{
// Split-band de-esser: a band-pass picks out the sibilance region, and when
// that band gets too loud it is subtracted back out of the signal
// (out = in - (1 - g) * band), which acts like a fast dynamic notch.
// No JUCE dependency.
class DeEsser
{
public:
    struct Params
    {
        float freqHz          = 6500.0f;
        float q               = 2.0f;     // width of the sibilance band
        float thresholdDb     = -32.0f;   // band level where reduction starts
        float ratio           = 4.0f;
        float maxReductionDb  = 10.0f;
        float attackMs        = 0.5f;
        float releaseMs       = 30.0f;
    };

    void prepare (double sampleRate) { sr = sampleRate; setParams (params); reset(); }

    void reset() { bp.reset(); env = 0.0f; reductionDb = 0.0f; }

    void setParams (const Params& p)
    {
        params = p;
        bp.makeBandpass (sr, p.freqHz, p.q);
        atk = std::exp (-1.0f / (std::max (0.05f, p.attackMs)  * 0.001f * (float) sr));
        rel = std::exp (-1.0f / (std::max (1.0f,  p.releaseMs) * 0.001f * (float) sr));
    }

    float process (float x)
    {
        const float band = bp.process (x);
        const float a = std::abs (band);
        const float c = a > env ? atk : rel;
        env = c * env + (1.0f - c) * a;

        const float levelDb = 20.0f * std::log10 (std::max (env, 1.0e-6f));
        reductionDb = std::clamp ((levelDb - params.thresholdDb) * (1.0f - 1.0f / params.ratio),
                                  0.0f, params.maxReductionDb);
        const float g = std::pow (10.0f, -reductionDb / 20.0f);
        return x - (1.0f - g) * band;
    }

    float getReductionDb() const { return reductionDb; }

private:
    Params params;
    double sr = 44100.0;
    Biquad bp;
    float env = 0, atk = 0, rel = 0, reductionDb = 0;
};
} // namespace boombap
