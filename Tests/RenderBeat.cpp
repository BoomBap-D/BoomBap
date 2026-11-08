// Builds a 2-bar boom-bap loop from the 808 modules on MPC pads, with swing.
// Build: g++ -std=c++17 -O2 -I../Source/Core/Drums808 -I../Source/Core/PadsMPC RenderBeat.cpp -o renderbeat
#include "Kick808.h"
#include "Snare808.h"
#include "Hat808.h"
#include "MpcSequencer.h"
#include <cstdio>
#include <cassert>
#include <vector>

static void writeWav (const char* path, const std::vector<float>& s, int sr)
{
    FILE* f = std::fopen (path, "wb");
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    uint32_t dataBytes = (uint32_t) s.size() * 2;
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1);
    w32 (sr); w32 (sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (float x : s) w16 ((uint16_t) (int16_t) (std::clamp (x, -1.0f, 1.0f) * 32767.0f));
    std::fclose (f);
}

template <typename Drum, typename TriggerFn>
static std::vector<float> renderOneShot (Drum& d, TriggerFn trig, int sr, float seconds)
{
    d.prepare (sr);
    std::vector<float> buf ((size_t) (sr * seconds), 0.0f);
    trig (d);
    d.processBlock (buf.data(), (int) buf.size());
    return buf;
}

int main()
{
    using namespace boombap;
    const int sr = 44100;

    // swing math checks
    assert (swingOffsetTicks (24, 24, 50.0f) == 0);   // straight
    assert (swingOffsetTicks (24, 24, 66.0f) == 8);   // triplet-ish
    assert (swingOffsetTicks (24, 24, 75.0f) == 12);  // maximum
    assert (swingOffsetTicks (48, 24, 66.0f) == 0);   // even 16ths never move
    assert (swingOffsetTicks (30, 24, 66.0f) == 0);   // off-grid notes never move

    // build pad samples from the drum modules
    Kick808 k;  Snare808 s;  Hat808 h;
    auto kick  = renderOneShot (k, [] (Kick808&  d) { d.trigger(); },       sr, 0.8f);
    auto snare = renderOneShot (s, [] (Snare808& d) { d.trigger(); },       sr, 0.5f);
    auto hat   = renderOneShot (h, [] (Hat808&   d) { d.triggerClosed(); }, sr, 0.25f);

    MpcPads pads;
    pads.prepare (sr);
    pads.loadSample (0, kick,  sr);
    pads.loadSample (1, snare, sr);
    pads.loadSample (2, hat,   sr);
    MpcPads::PadParams hatParams; hatParams.gain = 0.7f;
    pads.setPadParams (2, hatParams);

    MpcSequencer seq;
    seq.prepare (sr, &pads);
    seq.setBpm (90.0f);
    seq.setSwing (58.0f);
    seq.setLoopBars (2);

    auto step = [] (int bar, int s16) { return bar * 384 + s16 * 24; };
    const int kickSteps[2][3] = { { 0, 7, 10 }, { 0, 3, 10 } };
    for (int bar = 0; bar < 2; ++bar)
    {
        for (int ks : kickSteps[bar]) seq.addEvent (step (bar, ks), 0, 1.0f);
        seq.addEvent (step (bar, 4),  1, 1.0f);   // snare on 2
        seq.addEvent (step (bar, 12), 1, 1.0f);   // snare on 4
        for (int i = 0; i < 16; ++i)              // 16th hats, accented on the beat
            seq.addEvent (step (bar, i), 2, i % 4 == 0 ? 1.0f : (i % 2 == 0 ? 0.7f : 0.4f));
    }

    const int loopSamples = (int) (sr * 60.0 / 90.0 * 8.0);        // 2 bars
    std::vector<float> out (loopSamples + sr, 0.0f);               // + 1 s tail

    seq.start();
    seq.processBlock (out.data(), loopSamples);
    seq.stop();
    pads.processBlock (out.data() + loopSamples, sr);              // let the last hits ring out

    for (float& x : out) x *= 0.7f;
    float peak = 0.0f;
    for (float x : out) peak = std::max (peak, std::abs (x));
    std::printf ("swing checks passed, peak %.3f, pads still active at end: %d\n",
                 peak, (int) pads.anyActive());

    writeWav ("mpc_beat.wav", out, sr);
}
