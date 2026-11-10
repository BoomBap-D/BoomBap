#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace boombap
{
// A synthesized piano for BoomBap's piano roll: five harmonics with
// per-harmonic decay (the high ones die first, like a real string), a
// short hammer transient, and one overall decay. Percussive by nature -
// a note rings for its decay and stops, no gate needed. Root note is C4.
class Piano
{
public:
    struct Params
    {
        float bright  = 0.6f;     // 0..1: harmonic richness / hammer amount
        float decayMs = 2200.0f;  // time for the fundamental to fall 60 dB
        float level   = 0.85f;    // 0..1.2
    };

    static constexpr float kRootHz = 261.6256f; // C4
    static constexpr int kHarmonics = 5;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        setParams (params);
        stopAll();
    }

    void setParams (const Params& p) { params = p; }

    // semitones is the piano roll offset from the root (C4).
    void noteOn (float semitones, float velocity)
    {
        Voice* v = nullptr;
        for (auto& cand : voices) if (! cand.active) { v = &cand; break; }
        if (v == nullptr) // steal the quietest voice
        {
            v = &voices[0];
            for (auto& cand : voices) if (cand.env[0] < v->env[0]) v = &cand;
        }
        const float bright = std::clamp (params.bright, 0.0f, 1.0f);
        v->freq = (double) (kRootHz * std::pow (2.0f, semitones / 12.0f));
        v->vel = std::clamp (velocity, 0.0f, 1.0f);
        const double decaySamples = std::max (20.0, (double) params.decayMs * 0.001 * sr);
        for (int k = 0; k < kHarmonics; ++k)
        {
            v->phase[k] = 0.0;
            v->env[k] = 1.0f;
            // higher harmonics start quieter (less so when bright) and decay faster
            v->gain[k] = kBase[k] * std::pow (0.22f + bright * 1.05f, (float) k * 0.62f);
            const double t = decaySamples / (1.0 + 0.85 * k);
            v->coef[k] = (float) std::pow (0.001, 1.0 / t);
        }
        v->hammerEnv = 1.0f;
        v->hammerAmt = 0.05f + bright * 0.16f;
        v->active = true;
    }

    // Round 5 (item 105): the damper. A piano note is percussive and
    // rings to its natural decay while HELD; a note-off drops the
    // pedal - the voice's decay collapses to a fast ~60 ms damp
    // instead of the multi-second ring. One voice per call.
    void noteOff (float semitones)
    {
        const double target = (double) (kRootHz * std::pow (2.0f, semitones / 12.0f));
        const double dampSamples = std::max (20.0, 0.06 * sr);
        for (auto& v : voices)
            if (v.active && std::abs (v.freq - target) < 1.0e-6)
            {
                for (int k = 0; k < kHarmonics; ++k)
                    v.coef[k] = (float) std::pow (0.001, 1.0 / dampSamples);
                return;
            }
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

            float note = 0.0f;
            for (int k = 0; k < kHarmonics; ++k)
            {
                v.phase[k] += v.freq * (k + 1) / sr;
                v.phase[k] -= std::floor (v.phase[k]);
                note += std::sin (6.28318530718f * (float) v.phase[k]) * v.env[k] * v.gain[k];
                v.env[k] *= v.coef[k];
            }
            if (v.hammerEnv > 0.001f)
            {
                note += nextNoise() * v.hammerEnv * v.hammerAmt;
                v.hammerEnv *= 0.90f; // ~0.5 ms per halving at 44.1k - a short tick
            }
            sum += note * v.vel;
            if (v.env[0] < 1.0e-4f) v.active = false;
        }
        return sum * params.level * 0.5f;
    }

private:
    struct Voice
    {
        bool active = false;
        double freq = 0.0, phase[kHarmonics] = {};
        float env[kHarmonics] = {}, gain[kHarmonics] = {}, coef[kHarmonics] = {};
        float vel = 1.0f, hammerEnv = 0.0f, hammerAmt = 0.0f;
    };

    static constexpr float kBase[kHarmonics] = { 1.0f, 0.55f, 0.32f, 0.18f, 0.10f };

    float nextNoise() // xorshift white noise, -1..1
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
    }

    Params params;
    double sr = 44100.0;
    uint32_t rng = 987654u;
    std::array<Voice, 8> voices;
};
} // namespace boombap
