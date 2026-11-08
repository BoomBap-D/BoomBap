// SSL-style channel strips on the drums + bus compressor glue, A/B against the raw loop.
// Also verifies EQ gain, high-pass slope, and static compression math.
// Build: g++ -std=c++17 -O2 -I../Source/Core/Drums808 -I../Source/Core/PadsMPC -I../Source/Core/ChannelStrip RenderStrip.cpp -o renderstrip
#include "Kick808.h"
#include "Snare808.h"
#include "Hat808.h"
#include "MpcSequencer.h"
#include "ChannelStrip.h"
#include "BusCompressor.h"
#include <cstdio>
#include <cassert>
#include <vector>

using namespace boombap;

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

static std::vector<float> renderLoop (const std::vector<float>& kick, const std::vector<float>& snare,
                                      const std::vector<float>& hat, int sr)
{
    MpcPads pads;
    pads.prepare (sr);
    pads.loadSample (0, kick, sr);
    pads.loadSample (1, snare, sr);
    pads.loadSample (2, hat, sr);
    MpcPads::PadParams hp; hp.gain = 0.7f;
    pads.setPadParams (2, hp);

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
        seq.addEvent (step (bar, 4), 1, 1.0f);
        seq.addEvent (step (bar, 12), 1, 1.0f);
        for (int i = 0; i < 16; ++i)
            seq.addEvent (step (bar, i), 2, i % 4 == 0 ? 1.0f : (i % 2 == 0 ? 0.7f : 0.4f));
    }

    const int loopSamples = (int) (sr * 60.0 / 90.0 * 8.0);
    std::vector<float> out (loopSamples + sr, 0.0f);
    seq.start();
    seq.processBlock (out.data(), loopSamples);
    seq.stop();
    pads.processBlock (out.data() + loopSamples, sr);
    return out;
}


static float rms (const std::vector<float>& x, size_t from)
{
    double acc = 0.0;
    for (size_t i = from; i < x.size(); ++i) acc += (double) x[i] * x[i];
    return (float) std::sqrt (acc / (double) (x.size() - from));
}

static std::vector<float> sine (float hz, float amp, int sr, float seconds)
{
    std::vector<float> v ((size_t) (sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * std::sin (6.283185307f * hz * (float) i / sr);
    return v;
}

static std::vector<float> through (ChannelStrip::Params p, std::vector<float> x, int sr)
{
    ChannelStrip s; s.setParams (p); s.prepare (sr);
    s.processBlock (x.data(), (int) x.size());
    return x;
}

static std::vector<float> bakeStrip (const ChannelStrip::Params& p, const std::vector<float>& in, int sr)
{
    return through (p, in, sr);
}

static void normalize (std::vector<float>& v, float target)
{
    float peak = 0.0f;
    for (float x : v) peak = std::max (peak, std::abs (x));
    for (float& x : v) x *= target / peak;
}

int main()
{
    const int sr = 44100;

    // 1) +6 dB bell at 1 kHz doubles a 1 kHz sine
    {
        ChannelStrip::Params p; p.compEnabled = false;
        p.eq.lmf = { 1000.0f, 6.0f, 1.0f };
        auto in = sine (1000.0f, 0.25f, sr, 1.0f);
        auto out = through (p, in, sr);
        const float ratio = rms (out, sr / 2) / rms (in, sr / 2);
        std::printf ("EQ +6 dB at 1 kHz -> gain x%.3f (expect ~2.0)\n", ratio);
        assert (std::abs (ratio - 2.0f) < 0.1f);
    }

    // 2) 100 Hz high-pass: 50 Hz is cut hard, 1 kHz passes
    {
        ChannelStrip::Params p; p.compEnabled = false; p.eq.hpfHz = 100.0f;
        auto lo = sine (50.0f, 0.5f, sr, 1.0f), hi = sine (1000.0f, 0.5f, sr, 1.0f);
        const float rLo = rms (through (p, lo, sr), sr / 2) / rms (lo, sr / 2);
        const float rHi = rms (through (p, hi, sr), sr / 2) / rms (hi, sr / 2);
        std::printf ("HPF 100 Hz: 50 Hz x%.3f (expect ~0.12), 1 kHz x%.3f\n", rLo, rHi);
        assert (rLo < 0.2f && rHi > 0.98f);
    }

    // 3) static compression: -6 dBFS peak, thr -18, 4:1 -> about -9 dB of gain reduction
    {
        ChannelStrip::Params p;
        p.comp.thresholdDb = -18.0f; p.comp.ratio = 4.0f; p.comp.kneeDb = 0.0f;
        p.comp.attackMs = 1.0f; p.comp.releaseMs = 100.0f;
        auto in = sine (1000.0f, 0.5f, sr, 2.0f);
        auto out = through (p, in, sr);
        const float db = 20.0f * std::log10 (rms (out, sr) / rms (in, sr));
        std::printf ("Compressor static gain: %.2f dB (expect about -9)\n", db);
        assert (std::abs (db + 9.0f) < 1.5f);
    }

    // 4) A/B: raw loop vs. channel strips + bus compressor
    Kick808 k; Snare808 s; Hat808 h;
    auto kick  = renderOneShot (k, [] (Kick808&  d) { d.trigger(); },       sr, 0.8f);
    auto snare = renderOneShot (s, [] (Snare808& d) { d.trigger(); },       sr, 0.5f);
    auto hat   = renderOneShot (h, [] (Hat808&   d) { d.triggerClosed(); }, sr, 0.25f);

    ChannelStrip::Params kp;                       // kick: weight, less mud, let the thump through
    kp.eq.hpfHz = 30.0f; kp.eq.lf = { 60.0f, 4.0f, 0.7f }; kp.eq.lmf = { 300.0f, -3.0f, 1.0f };
    kp.comp.thresholdDb = -14.0f; kp.comp.attackMs = 30.0f; kp.comp.releaseMs = 150.0f;

    ChannelStrip::Params sp;                       // snare: bite and control
    sp.eq.hmf = { 4000.0f, 3.0f, 1.0f };
    sp.comp.thresholdDb = -18.0f; sp.comp.attackMs = 3.0f; sp.comp.releaseMs = 120.0f;

    ChannelStrip::Params hp;                       // hats: less low end, more air
    hp.eq.hpfHz = 400.0f; hp.eq.hf = { 10000.0f, 2.0f, 0.7f };
    hp.compEnabled = false;

    auto raw  = renderLoop (kick, snare, hat, sr);
    auto mix  = renderLoop (bakeStrip (kp, kick, sr), bakeStrip (sp, snare, sr), bakeStrip (hp, hat, sr), sr);

    BusCompressor bus;
    BusCompressor::Params bp; bp.thresholdDb = -12.0f; bp.ratioIndex = 1; bp.attackIndex = 4;
    bp.releaseIndex = 4; bp.makeupDb = 2.0f;
    bus.setParams (bp); bus.prepare (sr);

    float maxGr = 0.0f;
    for (float& x : mix)
    {
        float l = x, r = x;
        bus.process (l, r);
        x = l;
        maxGr = std::min (maxGr, bus.getGainReductionDb());
    }
    std::printf ("Bus compressor max gain reduction: %.1f dB\n", maxGr);
    assert (maxGr < -1.0f);

    normalize (raw, 0.89f);
    normalize (mix, 0.89f);

    std::vector<float> out (raw);
    out.insert (out.end(), (size_t) (sr / 2), 0.0f);
    out.insert (out.end(), mix.begin(), mix.end());
    for (float x : out) assert (std::isfinite (x));

    std::printf ("checks passed\n");
    writeWav ("ssl_ab.wav", out, sr);
}
