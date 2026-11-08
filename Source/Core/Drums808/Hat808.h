#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace boombap
{
// TR-808-style hi-hat: six square oscillators at the 808's inharmonic
// frequencies, summed, band-limited, and shaped by an amp envelope.
// One voice serves both hats, so a closed hit chokes an open one,
// just like the hardware. Squares use PolyBLEP to keep aliasing down.
// No JUCE dependency.
class Hat808
{
public:
    struct Params
    {
        float hpHz           = 7000.0f; // brightness: high-pass cutoff
        float lpHz           = 14000.0f;// top-end roll-off
        float closedDecayMs  = 60.0f;   // time to fall 60 dB
        float openDecayMs    = 380.0f;  // time to fall 60 dB
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
        const float nyq = 0.45f * (float) sr;
        hpA = 1.0f - std::exp (-6.28318530718f * std::min (p.hpHz, nyq) / (float) sr);
        lpA = 1.0f - std::exp (-6.28318530718f * std::min (p.lpHz, nyq) / (float) sr);
        closedCoef = coefFor (p.closedDecayMs);
        openCoef   = coefFor (p.openDecayMs);
    }

    void triggerClosed (float velocity = 1.0f) { trigger (velocity, closedCoef); }
    void triggerOpen   (float velocity = 1.0f) { trigger (velocity, openCoef); }

    bool isActive() const { return active; }

    // Fix pass 2 item 45: hard silence on demand (the global stop).
    void stop() { active = false; ampEnv = 0.0f; }

    float process()
    {
        if (! active)
            return 0.0f;

        float sum = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            const float dt = kFreqs[i] / (float) sr;
            phase[i] += dt;
            phase[i] -= std::floor (phase[i]);
            sum += square (phase[i], dt);
        }
        sum *= (1.0f / 6.0f);

        // two cascaded one-pole high-passes, then a one-pole low-pass
        hp1 += hpA * (sum - hp1);
        const float h1 = sum - hp1;
        hp2 += hpA * (h1 - hp2);
        const float h2 = h1 - hp2;
        lp += lpA * (h2 - lp);

        const float out = std::clamp (lp * 7.5f * ampEnv * vel * params.level, -1.0f, 1.0f);

        ampEnv *= curCoef;
        if (ampEnv < 1.0e-4f)
            active = false;

        return out;
    }

    void processBlock (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] += process();
    }

private:
    static constexpr float kFreqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };

    float coefFor (float ms) const
    {
        return std::pow (0.001f, 1.0f / (std::max (5.0f, ms) * 0.001f * (float) sr));
    }

    void trigger (float velocity, float coef)
    {
        vel = std::clamp (velocity, 0.0f, 1.0f);
        curCoef = coef;
        ampEnv = 1.0f;   // retrigger replaces the envelope = choke
        active = true;
    }

    static float polyBlep (float t, float dt)
    {
        if (t < dt)       { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt){ t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    static float square (float p, float dt)
    {
        float v = p < 0.5f ? 1.0f : -1.0f;
        v += polyBlep (p, dt);
        float p2 = p + 0.5f; p2 -= std::floor (p2);
        v -= polyBlep (p2, dt);
        return v;
    }

    Params params;
    double sr = 44100.0;
    float phase[6] = { 0, 0, 0, 0, 0, 0 };
    float ampEnv = 0, vel = 1, curCoef = 0, closedCoef = 0, openCoef = 0;
    float hpA = 0, lpA = 0, hp1 = 0, hp2 = 0, lp = 0;
    bool active = false;
};
} // namespace boombap
