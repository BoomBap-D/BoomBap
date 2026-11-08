#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include "../Common/Biquad.h"

namespace boombap
{
// A small subtractive synth for BoomBap's piano roll: one oscillator per
// voice into a resonant lowpass, with an attack / gate / release envelope.
// Notes are one-shot from the sequencer's point of view - noteOn is given
// the step length and the voice gates itself off, then releases. Eight
// voices, so release tails can overlap the next note. Root note is C3.
class Synth
{
public:
    struct Params
    {
        int   wave       = 0;        // 0 saw, 1 square, 2 triangle, 3 sine
        float cutoffHz   = 4500.0f;  // filter cutoff
        float reso       = 0.25f;    // 0..1 -> Q 0.7..8
        float attackMs   = 8.0f;     // envelope attack
        float releaseMs  = 240.0f;   // envelope release after the gate
        float level      = 0.8f;     // 0..1.2
    };

    static constexpr float kRootHz = 130.8128f; // C3

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        setParams (params);
        stopAll();
    }

    void setParams (const Params& p)
    {
        params = p;
        attackStep  = 1.0f / std::max (1.0f, p.attackMs * 0.001f * (float) sr);
        releaseCoef = std::pow (0.001f, 1.0f / (std::max (5.0f, p.releaseMs) * 0.001f * (float) sr));
        filterQ     = 0.7 + (double) std::clamp (p.reso, 0.0f, 1.0f) * 7.3;
    }

    // semitones is the piano roll offset from the root (C3).
    void noteOn (float semitones, float velocity, int gateSamples)
    {
        Voice* v = nullptr;
        for (auto& cand : voices) if (! cand.active) { v = &cand; break; }
        if (v == nullptr) // steal the quietest voice
        {
            v = &voices[0];
            for (auto& cand : voices) if (cand.env < v->env) v = &cand;
        }
        v->freq = (double) (kRootHz * std::pow (2.0f, semitones / 12.0f));
        v->phase = 0.0;
        v->vel = std::clamp (velocity, 0.0f, 1.0f);
        v->env = 0.0f;
        v->gateLeft = std::max (1, gateSamples);
        v->releasing = false;
        v->wave = params.wave;
        v->filter.makeLowpass (sr, params.cutoffHz, filterQ);
        v->filter.reset();
        v->active = true;
    }

    void stopAll()
    {
        for (auto& v : voices) v.active = false;
    }

    float process()
    {
        float sum = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;

            v.phase += v.freq / sr;
            v.phase -= std::floor (v.phase);
            const double ph = v.phase;
            float osc = 0.0f;
            switch (v.wave)
            {
                case 1:  osc = ph < 0.5 ? 1.0f : -1.0f; break;                       // square
                case 2:  osc = (float) (1.0 - 4.0 * std::abs (std::round (ph - 0.25) - (ph - 0.25))); break; // triangle
                case 3:  osc = std::sin (6.28318530718f * (float) ph); break;        // sine
                default: osc = (float) (2.0 * ph - 1.0); break;                      // saw
            }

            if (! v.releasing)
            {
                if (v.env < 1.0f) v.env = std::min (1.0f, v.env + attackStep);
                if (--v.gateLeft <= 0) v.releasing = true;
            }
            else
            {
                v.env *= releaseCoef;
                if (v.env < 1.0e-4f) { v.active = false; continue; }
            }

            sum += v.filter.process (osc) * v.env * v.vel;
        }
        return sum * params.level * 0.45f;
    }

private:
    struct Voice
    {
        bool active = false, releasing = false;
        double phase = 0.0, freq = 0.0;
        float env = 0.0f, vel = 1.0f;
        int gateLeft = 0, wave = 0;
        Biquad filter;
    };

    Params params;
    double sr = 44100.0;
    float attackStep = 0.01f, releaseCoef = 0.999f;
    double filterQ = 2.5;
    std::array<Voice, 8> voices;
};
} // namespace boombap
