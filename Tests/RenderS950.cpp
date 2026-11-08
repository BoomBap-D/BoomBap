// A/B demo: a 2-bar loop clean, then the same loop through the S950 crusher.
// Also checks that quantization and aliasing behave as expected.
// Build: g++ -std=c++17 -O2 -I../Source/Core/Drums808 -I../Source/Core/PadsMPC -I../Source/Core/Sampler950 RenderS950.cpp -o renders950
#include "Kick808.h"
#include "Snare808.h"
#include "Hat808.h"
#include "MpcSequencer.h"
#include "S950Crusher.h"
#include <cstdio>
#include <cassert>
#include <complex>
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

// amplitude of one frequency in a signal (single-bin DFT)
static float toneAmp (const std::vector<float>& x, float hz, int sr, int from)
{
    std::complex<double> acc = 0.0;
    const int n = (int) x.size() - from;
    for (int i = 0; i < n; ++i)
        acc += (double) x[(size_t) (from + i)] * std::polar (1.0, -6.283185307179586 * hz * i / sr);
    return (float) (2.0 * std::abs (acc) / n);
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
    for (float& x : out) x *= 0.7f;
    return out;
}

int main()
{
    const int sr = 44100;

    // 1) quantizer lands exactly on the 12-bit grid (2048 steps per side)
    const float q = S950Crusher::quantize (0.3337f, 12);
    assert (std::abs (q * 2048.0f - std::round (q * 2048.0f)) < 1.0e-4f);
    assert (S950Crusher::quantize (0.3337f, 4) != q);

    // 2) aliasing: a 9 kHz sine through a 15 kHz sample rate must fold down to 6 kHz
    {
        S950Crusher c;
        S950Crusher::Params p; p.sampleRateHz = 15000.0f; p.bits = 16; p.noiseDb = -120.0f; p.drive = 0.0f;
        c.setParams (p);
        c.prepare (sr);
        std::vector<float> sine (sr / 2);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5f * std::sin (6.283185307f * 9000.0f * (float) i / sr);
        c.processBlock (sine.data(), (int) sine.size());
        const float a9 = toneAmp (sine, 9000.0f, sr, 2000);
        const float a6 = toneAmp (sine, 6000.0f, sr, 2000);
        std::printf ("9 kHz in -> 9 kHz: %.4f, aliased 6 kHz: %.4f\n", a9, a6); std::fflush (stdout);
        assert (a6 > 2.0f * a9); // the folded-down alias is the loudest component
    }

    // 3) A/B loop: clean, half a second of silence, then crushed
    Kick808 k; Snare808 s; Hat808 h;
    auto kick  = renderOneShot (k, [] (Kick808&  d) { d.trigger(); },       sr, 0.8f);
    auto snare = renderOneShot (s, [] (Snare808& d) { d.trigger(); },       sr, 0.5f);
    auto hat   = renderOneShot (h, [] (Hat808&   d) { d.triggerClosed(); }, sr, 0.25f);

    S950Crusher::Params dusty;           // 15 kHz / 12-bit: dusty, lo-fi
    dusty.sampleRateHz = 15000.0f;
    dusty.filterQ = 1.1f;

    auto clean   = renderLoop (kick, snare, hat, sr);
    auto crushed = renderLoop (S950Crusher::bake (kick,  sr, dusty),
                               S950Crusher::bake (snare, sr, dusty),
                               S950Crusher::bake (hat,   sr, dusty), sr);

    std::vector<float> out (clean);
    out.insert (out.end(), (size_t) (sr / 2), 0.0f);
    out.insert (out.end(), crushed.begin(), crushed.end());

    float peak = 0.0f;
    for (float x : out) peak = std::max (peak, std::abs (x));
    std::printf ("checks passed, peak %.3f\n", peak);
    writeWav ("s950_ab.wav", out, sr);
}
