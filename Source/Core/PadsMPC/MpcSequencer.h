#pragma once
#include "MpcPads.h"
#include "MpcSwing.h"
#include <vector>

namespace boombap
{
// Minimal MPC-style looping sequencer at 96 PPQN (4/4) with swing.
// Events live on the straight grid; swing is applied when they fire,
// so you can change the swing amount live without re-recording anything.
class MpcSequencer
{
public:
    struct Event { int tick; int pad; float velocity; };

    void prepare (double sampleRate, MpcPads* padEngine)
    {
        sr = sampleRate;
        pads = padEngine;
        updateTempo();
    }

    void setBpm (float bpm)           { this->bpm = std::max (20.0f, bpm); updateTempo(); }
    void setSwing (float percent)     { swing = std::clamp (percent, 50.0f, 75.0f); }
    void setSwingGrid (int gridTicks) { gridTicks_ = gridTicks; } // 24 = 16ths, 48 = 8ths
    void setLoopBars (int bars)       { loopTicks = std::max (1, bars) * kPPQN * 4; }

    void clear() { events.clear(); }
    void addEvent (int tick, int pad, float velocity = 1.0f) { events.push_back ({ tick, pad, velocity }); }

    void start() { playing = true; currentTick = 0; samplesToNextTick = 0.0; }
    void stop()  { playing = false; }
    bool isPlaying() const { return playing; }

    // advance one sample; fires pad triggers on tick boundaries
    void advance()
    {
        if (! playing) return;
        if (samplesToNextTick <= 0.0)
        {
            fireTick (currentTick);
            currentTick = (currentTick + 1) % loopTicks;
            samplesToNextTick += samplesPerTick;
        }
        samplesToNextTick -= 1.0;
    }

    // sequencer + pads together
    void processBlock (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            advance();
            out[i] += pads->process();
        }
    }

private:
    void updateTempo() { samplesPerTick = sr * 60.0 / ((double) bpm * kPPQN); }

    void fireTick (int tick)
    {
        for (const auto& e : events)
        {
            const int swung = (e.tick + swingOffsetTicks (e.tick, gridTicks_, swing)) % loopTicks;
            if (swung == tick)
                pads->trigger (e.pad, e.velocity);
        }
    }

    MpcPads* pads = nullptr;
    std::vector<Event> events;
    double sr = 44100.0, samplesPerTick = 100.0, samplesToNextTick = 0.0;
    float bpm = 90.0f, swing = 50.0f;
    int gridTicks_ = kTicksPer16th, loopTicks = kPPQN * 4, currentTick = 0;
    bool playing = false;
};
} // namespace boombap
