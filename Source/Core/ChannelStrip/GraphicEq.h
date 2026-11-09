#pragma once
#include "../Common/Biquad.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace boombap
{
// The Station's graphic EQ (Pass 2, item 9 - the Pro-Q grammar): up to
// eight bands, each a biquad the user can see and grab - high-pass,
// low shelf, peaking, high shelf, low-pass. The DSP is a plain series
// of biquads; the same class also evaluates its own magnitude response
// so the display draws the TRUE curve, not an approximation of it.
// No JUCE dependency.
class GraphicEq
{
public:
    enum Type { Peak = 0, LowShelf = 1, HighShelf = 2, HighPass = 3, LowPass = 4 };

    struct Band
    {
        bool  on     = false;
        int   type   = Peak;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q      = 1.0f;
    };

    static constexpr int kMaxBands = 8;

    struct Params
    {
        std::array<Band, kMaxBands> bands {};
        int numBands = 0; // bands[0..numBands) exist; the rest are spares
    };

    void prepare (double sampleRate) { sr = sampleRate; setParams (params); reset(); }

    void reset() { for (auto& b : filters) b.reset(); }

    void setParams (const Params& p)
    {
        params = p;
        for (int i = 0; i < kMaxBands; ++i)
        {
            const auto& band = params.bands[(size_t) i];
            active[(size_t) i] = band.on && i < params.numBands;
            if (! active[(size_t) i]) continue;
            auto& f = filters[(size_t) i];
            const double q = std::clamp ((double) band.q, 0.1, 18.0);
            switch (band.type)
            {
                case LowShelf:  f.makeLowShelf  (sr, band.freqHz, band.gainDb, q); break;
                case HighShelf: f.makeHighShelf (sr, band.freqHz, band.gainDb, q); break;
                case HighPass:  f.makeHighpass  (sr, band.freqHz, q); break;
                case LowPass:   f.makeLowpass   (sr, band.freqHz, q); break;
                case Peak:
                default:        f.makePeaking   (sr, band.freqHz, band.gainDb, q); break;
            }
        }
    }

    float process (float x)
    {
        for (int i = 0; i < kMaxBands; ++i)
            if (active[(size_t) i]) x = filters[(size_t) i].process (x);
        return x;
    }

    bool anyOn() const
    {
        for (int i = 0; i < kMaxBands && i < params.numBands; ++i)
            if (params.bands[(size_t) i].on) return true;
        return false;
    }

    // The total magnitude response in dB at a frequency, summed over the
    // live bands - what the display draws.
    float responseDb (float freqHz) const
    {
        const double w = 2.0 * kPi * std::clamp ((double) freqHz, 1.0, 0.499 * sr) / sr;
        const double cw = std::cos (w), cw2 = std::cos (2.0 * w);
        const double sw = std::sin (w), sw2 = std::sin (2.0 * w);
        double totalDb = 0.0;
        for (int i = 0; i < kMaxBands; ++i)
        {
            if (! active[(size_t) i]) continue;
            const auto& f = filters[(size_t) i];
            const double nre = (double) f.b0 + (double) f.b1 * cw + (double) f.b2 * cw2;
            const double nim = -((double) f.b1 * sw + (double) f.b2 * sw2);
            const double dre = 1.0 + (double) f.a1 * cw + (double) f.a2 * cw2;
            const double dim = -((double) f.a1 * sw + (double) f.a2 * sw2);
            const double mag2 = (nre * nre + nim * nim) / std::max (1.0e-12, dre * dre + dim * dim);
            totalDb += 10.0 * std::log10 (std::max (1.0e-9, mag2));
        }
        return (float) totalDb;
    }

private:
    Params params;
    double sr = 44100.0;
    std::array<Biquad, kMaxBands> filters {};
    std::array<bool, kMaxBands> active {};
};
} // namespace boombap
