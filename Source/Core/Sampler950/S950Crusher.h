#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace boombap
{
// Akai S950-style sample character: hot input stage, low sample rate with
// sample-and-hold (no interpolation, so aliasing is part of the sound),
// 12-bit quantization with a noise floor, and an analog-style low-pass whose
// cutoff tracks the sample rate. No JUCE dependency.
//
// Use it two ways:
//   - live, as an effect:           crusher.process (x)
//   - baked into a pad's sample:    S950Crusher::bake (data, sr, params)
class S950Crusher
{
public:
    struct Params
    {
        float sampleRateHz = 26000.0f; // target rate (the S950 ran roughly 7.5 to 39 kHz)
        int   bits         = 12;       // quantizer resolution
        float noiseDb      = -66.0f;   // converter noise floor (-120 = off)
        float filterHz     = 0.0f;     // 0 = auto: tracks the sample rate
        float filterQ      = 1.0f;     // >0.7 adds the gritty resonant bump
        float drive        = 0.2f;     // 0..1 hot-input soft clip (0 = off)
        float mix          = 1.0f;     // dry/wet
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        setParams (params);
        reset();
    }

    void reset()
    {
        phase = 1.0f; // capture the very first sample
        held = z1 = z2 = 0.0f;
    }

    void setParams (const Params& p)
    {
        params = p;
        step = std::min (1.0f, p.sampleRateHz / (float) sr);
        driveGain = 1.0f + p.drive * 4.0f;
        driveNorm = 1.0f / driveGain; // unity gain for small signals, soft ceiling for peaks
        noiseLin  = std::pow (10.0f, p.noiseDb / 20.0f);

        const float cutoff = (p.filterHz > 0.0f ? p.filterHz : 0.42f * p.sampleRateHz);
        const float w0 = 6.28318530718f * std::min (cutoff, 0.45f * (float) sr) / (float) sr;
        const float alpha = std::sin (w0) / (2.0f * std::max (0.2f, p.filterQ));
        const float c = std::cos (w0);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - c) * 0.5f / a0;
        b1 = (1.0f - c) / a0;
        b2 = b0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
    }

    static float quantize (float x, int bits)
    {
        const float levels = (float) (1 << (std::clamp (bits, 2, 16) - 1));
        x = std::clamp (x, -1.0f, 1.0f - 1.0f / levels);
        return std::round (x * levels) / levels;
    }

    float process (float in)
    {
        float x = in;
        if (params.drive > 0.0f)
            x = std::tanh (x * driveGain) * driveNorm;

        phase += step;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            held = quantize (x + nextNoise() * noiseLin, params.bits);
        }

        // analog-style low-pass (transposed direct form II)
        const float y = b0 * held + z1;
        z1 = b1 * held - a1 * y + z2;
        z2 = b2 * held - a2 * y;

        return in + (y - in) * params.mix;
    }

    void processBlock (float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = process (data[i]);
    }

    // Returns a crushed copy of a sample. Handy for baking a pad's sample once
    // instead of running the crusher live on every voice.
    static std::vector<float> bake (const std::vector<float>& in, double sampleRate, const Params& p)
    {
        S950Crusher c;
        c.params = p;
        c.prepare (sampleRate);
        std::vector<float> out (in);
        c.processBlock (out.data(), (int) out.size());
        return out;
    }

private:
    float nextNoise()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
    }

    Params params;
    double sr = 44100.0;
    float step = 1.0f, phase = 1.0f, held = 0.0f;
    float driveGain = 1.0f, driveNorm = 1.0f, noiseLin = 0.0f;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    uint32_t rng = 97531u;
};
} // namespace boombap
