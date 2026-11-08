#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace boombap
{
// TR-808-style clap: band-passed noise shaped by a few quick re-triggered
// bursts (the "group of hands"), followed by a longer decaying tail.
// No JUCE dependency.
class Clap808
{
public:
    struct Params
    {
        float centerHz       = 1100.0f; // band-pass centre
        float q              = 1.4f;    // band-pass resonance
        int   numBursts      = 3;       // quick bursts before the tail
        float burstSpacingMs = 10.0f;   // gap between bursts
        float burstDecayMs   = 14.0f;   // time to fall 60 dB per burst
        float tailDecayMs    = 200.0f;  // time to fall 60 dB for the tail
        float level          = 1.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        setParams (params);
    }

    void setParams (const Params& p)
    {
        params = p;
        burstCoef = coefFor (p.burstDecayMs);
        tailCoef  = coefFor (p.tailDecayMs);
        spacing   = std::max (1, (int) (p.burstSpacingMs * 0.001f * (float) sr));

        // RBJ band-pass, constant 0 dB peak gain
        const float w0 = 6.28318530718f * std::min (p.centerHz, 0.45f * (float) sr) / (float) sr;
        const float alpha = std::sin (w0) / (2.0f * std::max (0.1f, p.q));
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0f * std::cos (w0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void trigger (float velocity = 1.0f)
    {
        vel = std::clamp (velocity, 0.0f, 1.0f);
        counter = 0;
        burstEnv = tailEnv = 0.0f;
        z1 = z2 = 0.0f;
        tailStarted = false;
        active = true;
    }

    bool isActive() const { return active; }

    float process()
    {
        if (! active)
            return 0.0f;

        // schedule bursts, then the tail
        if (counter % spacing == 0)
        {
            const int k = counter / spacing;
            if (k < params.numBursts)
                burstEnv = 1.0f;
            else if (k == params.numBursts)
            {
                tailEnv = 1.0f;
                burstEnv = 0.0f;
                tailStarted = true;
            }
        }
        ++counter;

        const float n = nextNoise();
        const float y = b0 * n + z1;           // transposed direct form II
        z1 = z2 - a1 * y;
        z2 = b2 * n - a2 * y;

        const float env = tailStarted ? tailEnv : burstEnv;
        const float out = std::clamp (y * 3.0f * env * vel * params.level, -1.0f, 1.0f);

        burstEnv *= burstCoef;
        if (tailStarted)
        {
            tailEnv *= tailCoef;
            if (tailEnv < 1.0e-4f)
                active = false;
        }

        return out;
    }

    void processBlock (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] += process();
    }

private:
    float coefFor (float ms) const
    {
        return std::pow (0.001f, 1.0f / (std::max (2.0f, ms) * 0.001f * (float) sr));
    }

    float nextNoise()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
    }

    Params params;
    double sr = 44100.0;
    int counter = 0, spacing = 441;
    float burstEnv = 0, tailEnv = 0, burstCoef = 0, tailCoef = 0, vel = 1;
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    uint32_t rng = 13579246u;
    bool tailStarted = false, active = false;
};
} // namespace boombap
