#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace boombap
{
// TR-808-style kick: sine oscillator + fast pitch envelope + exponential
// amp decay, with an optional click transient and soft-clip drive.
// No JUCE dependency, so it works in the app and in every plugin wrapper.
class Kick808
{
public:
    struct Params
    {
        float tuneHz       = 50.0f;   // resting pitch of the body (40-70 Hz typical)
        float sweepHz      = 200.0f;  // extra Hz added at the start of the hit
        float pitchDecayMs = 40.0f;   // how fast the pitch drops to tuneHz
        float ampDecayMs   = 600.0f;  // time to fall 60 dB (long = booming)
        float click        = 0.2f;    // 0..1 transient amount
        float drive        = 0.2f;    // 0..1 soft-clip saturation
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        setParams (params);
    }

    void setParams (const Params& p)
    {
        params = p;
        pitchCoef = std::exp (-1.0f / (std::max (1.0f, p.pitchDecayMs) * 0.001f * (float) sr));
        // 60 dB decay: envelope reaches 0.001 after ampDecayMs
        ampCoef   = std::pow (0.001f, 1.0f / (std::max (5.0f, p.ampDecayMs) * 0.001f * (float) sr));
        clickCoef = std::exp (-1.0f / (0.002f * (float) sr)); // ~2 ms click
        driveGain = 1.0f + p.drive * 8.0f;
        driveNorm = 1.0f / std::tanh (driveGain);
    }

    void trigger (float velocity = 1.0f)
    {
        vel      = std::clamp (velocity, 0.0f, 1.0f);
        phase    = 0.0f;          // start at a zero crossing: no pop
        pitchEnv = 1.0f;
        ampEnv   = 1.0f;
        clickEnv = 1.0f;
        freqMul  = 1.0f;
        active   = true;
    }

    // A hit pitched away from the resting tune (the piano roll's kick notes).
    // The sweep scales with it, so the 808 character stays proportional.
    void triggerPitched (float velocity, float semitones)
    {
        trigger (velocity);
        freqMul = std::pow (2.0f, semitones / 12.0f);
    }

    bool isActive() const { return active; }

    float process()
    {
        if (! active)
            return 0.0f;

        const float freq = (params.tuneHz + params.sweepHz * pitchEnv) * freqMul;
        phase += freq / (float) sr;
        phase -= std::floor (phase);

        float body = std::sin (6.28318530718f * phase) * ampEnv;
        float tick = nextNoise() * clickEnv * params.click * 0.5f;

        float out = std::clamp (std::tanh ((body + tick) * driveGain) * driveNorm, -1.0f, 1.0f) * vel;

        pitchEnv *= pitchCoef;
        ampEnv   *= ampCoef;
        clickEnv *= clickCoef;

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
    float nextNoise() // xorshift white noise, -1..1
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
    }

    Params params;
    double sr = 44100.0;
    float phase = 0.0f, pitchEnv = 0.0f, ampEnv = 0.0f, clickEnv = 0.0f, vel = 1.0f, freqMul = 1.0f;
    float pitchCoef = 0.0f, ampCoef = 0.0f, clickCoef = 0.0f;
    float driveGain = 1.0f, driveNorm = 1.0f;
    uint32_t rng = 22222u;
    bool active = false;
};
} // namespace boombap
