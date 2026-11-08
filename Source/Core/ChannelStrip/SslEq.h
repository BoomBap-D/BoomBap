#pragma once
#include "../Common/Biquad.h"
#include <cmath>

namespace boombap
{
// SSL-console-style EQ: 3rd-order high-pass (18 dB/oct), 2nd-order low-pass,
// and four bands: LF (shelf or bell), LMF bell, HMF bell, HF (shelf or bell).
// Bands at 0 dB are skipped entirely. No JUCE dependency.
class SslEq
{
public:
    struct Band { float freqHz; float gainDb; float q; };

    struct Params
    {
        float hpfHz = 0.0f;                  // 0 = off (16-350 Hz on the console)
        float lpfHz = 0.0f;                  // 0 = off (3-22 kHz on the console)
        Band  lf  { 100.0f,   0.0f, 0.7f };  bool lfBell = false;
        Band  lmf { 600.0f,   0.0f, 1.0f };
        Band  hmf { 3000.0f,  0.0f, 1.0f };
        Band  hf  { 10000.0f, 0.0f, 0.7f };  bool hfBell = false;
    };

    void prepare (double sampleRate) { sr = sampleRate; setParams (params); reset(); }

    void reset() { hpf2.reset(); lpf.reset(); lfB.reset(); lmfB.reset(); hmfB.reset(); hfB.reset(); hp1 = 0.0f; }

    void setParams (const Params& p)
    {
        params = p;
        useHpf = p.hpfHz > 0.0f;
        useLpf = p.lpfHz > 0.0f;
        if (useHpf)
        {
            // 3rd order = Butterworth biquad (Q 0.707) + one-pole
            hpf2.makeHighpass (sr, p.hpfHz, 0.7071);
            hp1A = 1.0f - std::exp (-6.28318530718f * p.hpfHz / (float) sr);
        }
        if (useLpf) lpf.makeLowpass (sr, p.lpfHz, 0.7071);

        useLf  = std::abs (p.lf.gainDb)  > 0.01f;
        useLmf = std::abs (p.lmf.gainDb) > 0.01f;
        useHmf = std::abs (p.hmf.gainDb) > 0.01f;
        useHf  = std::abs (p.hf.gainDb)  > 0.01f;
        if (useLf)  { if (p.lfBell) lfB.makePeaking (sr, p.lf.freqHz, p.lf.gainDb, p.lf.q);
                      else          lfB.makeLowShelf (sr, p.lf.freqHz, p.lf.gainDb, p.lf.q); }
        if (useLmf) lmfB.makePeaking (sr, p.lmf.freqHz, p.lmf.gainDb, p.lmf.q);
        if (useHmf) hmfB.makePeaking (sr, p.hmf.freqHz, p.hmf.gainDb, p.hmf.q);
        if (useHf)  { if (p.hfBell) hfB.makePeaking (sr, p.hf.freqHz, p.hf.gainDb, p.hf.q);
                      else          hfB.makeHighShelf (sr, p.hf.freqHz, p.hf.gainDb, p.hf.q); }
    }

    float process (float x)
    {
        if (useHpf)
        {
            x = hpf2.process (x);
            hp1 += hp1A * (x - hp1);
            x -= hp1;
        }
        if (useLf)  x = lfB.process (x);
        if (useLmf) x = lmfB.process (x);
        if (useHmf) x = hmfB.process (x);
        if (useHf)  x = hfB.process (x);
        if (useLpf) x = lpf.process (x);
        return x;
    }

private:
    Params params;
    double sr = 44100.0;
    Biquad hpf2, lpf, lfB, lmfB, hmfB, hfB;
    float hp1 = 0.0f, hp1A = 0.0f;
    bool useHpf = false, useLpf = false, useLf = false, useLmf = false, useHmf = false, useHf = false;
};
} // namespace boombap
