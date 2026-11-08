#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace boombap
{
// TR-808-style snare: two detuned sine "shell" tones with a tiny pitch snap,
// plus high-passed noise for the snares. "snappy" balances noise vs. tone.
// No JUCE dependency.
class Snare808
{
public:
    struct Params
    {
        float tuneHz       = 180.0f;  // low shell tone; the upper tone sits at ~1.83x
        float tone         = 0.6f;    // 0..1 shell level
        float snappy       = 0.6f;    // 0..1 noise level
        float toneDecayMs  = 90.0f;   // time to fall 60 dB
        float noiseDecayMs = 200.0f;  // time to fall 60 dB
        float noiseHpHz    = 1800.0f; // noise high-pass cutoff
        float drive        = 0.15f;   // 0..1 soft-clip saturation
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        setParams (params);
    }

    void setParams (const Params& p)
    {
        params = p;
        toneCoef  = std::pow (0.001f, 1.0f / (std::max (5.0f, p.toneDecayMs)  * 0.001f * (float) sr));
        noiseCoef = std::pow (0.001f, 1.0f / (std::max (5.0f, p.noiseDecayMs) * 0.001f * (float) sr));
        pitchCoef = std::exp (-1.0f / (0.010f * (float) sr)); // 10 ms pitch snap
        hpA       = 1.0f - std::exp (-6.28318530718f * p.noiseHpHz / (float) sr);
        driveGain = 1.0f + p.drive * 6.0f;
        driveNorm = 1.0f / std::tanh (driveGain);
    }

    void trigger (float velocity = 1.0f)
    {
        vel = std::clamp (velocity, 0.0f, 1.0f);
        phase1 = phase2 = 0.0f;
        toneEnv = noiseEnv = pitchEnv = 1.0f;
        lp1 = lp2 = 0.0f;
        active = true;
    }

    bool isActive() const { return active; }

    // Fix pass 2 item 45: hard silence on demand (the global stop).
    void stop() { active = false; toneEnv = noiseEnv = 0.0f; }

    float process()
    {
        if (! active)
            return 0.0f;

        const float snap = 1.0f + 0.25f * pitchEnv;
        phase1 += params.tuneHz * snap / (float) sr;
        phase2 += params.tuneHz * 1.83f * snap / (float) sr;
        phase1 -= std::floor (phase1);
        phase2 -= std::floor (phase2);

        const float shell = (std::sin (6.28318530718f * phase1)
                           + 0.6f * std::sin (6.28318530718f * phase2)) * 0.6f
                           * toneEnv * params.tone;

        // two cascaded one-pole high-passes on white noise
        const float n = nextNoise();
        lp1 += hpA * (n - lp1);
        const float h1 = n - lp1;
        lp2 += hpA * (h1 - lp2);
        const float hp = h1 - lp2;
        const float snares = hp * noiseEnv * params.snappy;

        float out = std::clamp (std::tanh ((shell + snares) * driveGain) * driveNorm,
                                -1.0f, 1.0f) * vel;

        toneEnv  *= toneCoef;
        noiseEnv *= noiseCoef;
        pitchEnv *= pitchCoef;

        if (toneEnv < 1.0e-4f && noiseEnv < 1.0e-4f)
            active = false;

        return out;
    }

    void processBlock (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] += process();
    }

private:
    float nextNoise()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
    }

    Params params;
    double sr = 44100.0;
    float phase1 = 0, phase2 = 0, toneEnv = 0, noiseEnv = 0, pitchEnv = 0, vel = 1;
    float toneCoef = 0, noiseCoef = 0, pitchCoef = 0, hpA = 0, lp1 = 0, lp2 = 0;
    float driveGain = 1, driveNorm = 1;
    uint32_t rng = 87654321u;
    bool active = false;
};
} // namespace boombap
