#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace boombap
{
// 16 velocity-sensitive sample pads, MPC style. Each pad holds one sample
// with a chop window (start/end), tuning, reverse, and a mute group
// (e.g. closed hat chokes open hat). One voice per pad: retriggering restarts it.
// No JUCE dependency.
class MpcPads
{
public:
    static constexpr int kNumPads = 16;

    struct PadParams
    {
        float startFrac     = 0.0f; // chop start, 0..1 of the sample
        float endFrac       = 1.0f; // chop end,   0..1 of the sample
        float tuneSemitones = 0.0f; // pitch (also changes speed, like a sampler)
        float gain          = 1.0f;
        float velSens       = 1.0f; // 0 = fixed level, 1 = fully velocity-scaled
        bool  reverse       = false;
        int   muteGroup     = 0;    // 0 = none; pads in the same group choke each other
    };

    void prepare (double sampleRate) { sr = sampleRate; }

    void loadSample (int pad, std::vector<float> data, double sourceSampleRate)
    {
        if (! valid (pad)) return;
        samples[pad] = std::move (data);
        srcSr[pad] = sourceSampleRate;
    }

    void clearSample (int pad)
    {
        if (! valid (pad)) return;
        samples[pad].clear();
        voices[pad] = Voice{};
    }

    void setPadParams (int pad, const PadParams& p)
    {
        if (valid (pad)) params[pad] = p;
    }

    PadParams getPadParams (int pad) const
    {
        return valid (pad) ? params[pad] : PadParams{};
    }

    bool hasSample (int pad) const
    {
        return valid (pad) && samples[pad].size() >= 2;
    }

    void trigger (int pad, float velocity = 1.0f)
    {
        triggerWithTune (pad, velocity, 0.0f);
    }

    // A hit pitched away from the pad's own tune (the piano roll's notes add
    // their semitone offset on top of the pad's tune setting).
    void triggerWithTune (int pad, float velocity, float extraSemitones)
    {
        if (! valid (pad) || samples[pad].size() < 2) return;
        const PadParams& pp = params[pad];
        const int size = (int) samples[pad].size();

        Voice v;
        v.startIdx = std::clamp ((int) (pp.startFrac * (float) size), 0, size - 2);
        v.endIdx   = std::clamp ((int) (pp.endFrac   * (float) size), v.startIdx + 2, size);
        v.reverse  = pp.reverse;
        v.pos      = pp.reverse ? (double) (v.endIdx - 1) : (double) v.startIdx;
        v.rate     = (srcSr[pad] / sr) * std::pow (2.0, (pp.tuneSemitones + extraSemitones) / 12.0);
        const float vel = std::clamp (velocity, 0.0f, 1.0f);
        v.gain     = pp.gain * ((1.0f - pp.velSens) + pp.velSens * vel);
        v.active   = true;

        // mute group: choke every other pad in the same group
        if (pp.muteGroup != 0)
            for (int i = 0; i < kNumPads; ++i)
                if (i != pad && params[i].muteGroup == pp.muteGroup)
                    voices[i].choking = true;

        voices[pad] = v;
    }

    float process()
    {
        float sum = 0.0f;
        for (int p = 0; p < kNumPads; ++p)
            if (voices[p].active)
                sum += renderVoice (p, voices[p]);
        return sum;
    }

    // One pad's output for this sample, so a mixer can level/mute each pad.
    float processPad (int pad)
    {
        return (valid (pad) && voices[pad].active) ? renderVoice (pad, voices[pad]) : 0.0f;
    }

    void processBlock (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] += process();
    }

    // silence every voice immediately (used when cloning the engine for export)
    void stopAll() { for (auto& v : voices) v = Voice{}; }

    bool anyActive() const
    {
        for (const auto& v : voices) if (v.active) return true;
        return false;
    }

private:
    struct Voice
    {
        double pos = 0.0, rate = 1.0;
        int startIdx = 0, endIdx = 0;
        int age = 0;
        float gain = 1.0f, chokeGain = 1.0f;
        bool reverse = false, active = false, choking = false;
    };

    static bool valid (int pad) { return pad >= 0 && pad < kNumPads; }

    float renderVoice (int p, Voice& v)
    {
        const auto& d = samples[p];
        const double remaining = v.reverse ? v.pos - (double) v.startIdx
                                           : (double) v.endIdx - v.pos;
        if (remaining <= 0.0) { v.active = false; return 0.0f; }

        const int i0 = std::clamp ((int) v.pos, 0, (int) d.size() - 1);
        const int i1 = std::min (i0 + 1, (int) d.size() - 1);
        const float frac = (float) (v.pos - (double) i0);
        const float s = d[i0] + (d[i1] - d[i0]) * frac; // linear interpolation

        // short fades at the edges and on choke prevent clicks
        const float fadeIn  = std::min (1.0f, (float) v.age / 64.0f);
        const float fadeOut = (float) std::min (1.0, remaining / (64.0 * v.rate));
        if (v.choking)
        {
            v.chokeGain -= 1.0f / 128.0f;
            if (v.chokeGain <= 0.0f) { v.active = false; return 0.0f; }
        }

        const float out = s * v.gain * fadeIn * fadeOut * v.chokeGain;
        v.pos += v.reverse ? -v.rate : v.rate;
        ++v.age;
        return out;
    }

    double sr = 44100.0;
    std::array<std::vector<float>, kNumPads> samples;
    std::array<double, kNumPads> srcSr {};
    std::array<PadParams, kNumPads> params;
    std::array<Voice, kNumPads> voices;
};
} // namespace boombap
