#pragma once
#include "Biquad.h"
#include <cmath>

namespace boombap
{
// Preamp/tube-style saturation: asymmetric tanh (bias adds even harmonics),
// 2x oversampled to tame aliasing, DC-blocked. Unity gain for small signals,
// soft ceiling for peaks. drive = 0 bypasses. No JUCE dependency.
class Saturator
{
public:
    struct Params
    {
        float drive = 0.0f; // 0..1
        float bias  = 0.0f; // 0..0.4, asymmetry = even-harmonic warmth
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        up1.makeLowpass (2.0 * sr, 0.45 * sr, 0.5412);
        up2.makeLowpass (2.0 * sr, 0.45 * sr, 1.3066);
        dn1 = up1; dn2 = up2;
        dcA = 1.0f - std::exp (-6.28318530718f * 10.0f / (float) sr);
        setParams (params);
        reset();
    }

    void reset() { up1.reset(); up2.reset(); dn1.reset(); dn2.reset(); dc = 0.0f; }

    void setParams (const Params& p)
    {
        params = p;
        g  = 1.0f + p.drive * 8.0f;
        b  = std::clamp (p.bias, 0.0f, 0.4f);
        tb = std::tanh (b);
        comp = 1.0f / (g * (1.0f - tb * tb)); // small-signal gain of the curve at the bias point
    }

    float process (float x)
    {
        if (params.drive <= 0.0f)
            return x;

        const float u0 = up2.process (up1.process (2.0f * x)); // zero-stuff upsample
        const float u1 = up2.process (up1.process (0.0f));
        const float y0 = curve (u0), y1 = curve (u1);
        const float d  = dn2.process (dn1.process (y0));
        dn2.process (dn1.process (y1));                        // keep filter state, drop the sample

        dc += dcA * (d - dc);
        return d - dc;
    }

private:
    float curve (float x) const { return (std::tanh (g * x + b) - tb) * comp; }

    Params params;
    double sr = 44100.0;
    Biquad up1, up2, dn1, dn2;
    float g = 1, b = 0, tb = 0, comp = 1, dc = 0, dcA = 0;
};
} // namespace boombap
