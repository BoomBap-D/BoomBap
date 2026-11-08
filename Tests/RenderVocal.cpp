// Vocal section tests + demos:
//   vocal_ab.wav      raw synthetic vocal, then the same vocal through the modern-rap chain
//   mic_shootout.wav  the same vocal through each mic model (loudness matched)
// The "vocal" is a synthesized stand-in (formant-filtered saw + noise "s" sounds)
// so the demo works without any recording.
// Build: g++ -std=c++17 -O2 -I../Source/Core/VocalChain -I../Source/Core/ChannelStrip -I../Source/Core/Common RenderVocal.cpp -o rendervocal
#include "VocalChain.h"
#include <cstdint>
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

static std::vector<float> sine (float hz, float amp, int sr, float seconds)
{
    std::vector<float> v ((size_t) (sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * std::sin (6.283185307f * hz * (float) i / sr);
    return v;
}

// amplitude of one frequency between sample indices [from, to)
static float toneAmp (const std::vector<float>& x, float hz, int sr, int from, int to)
{
    std::complex<double> acc = 0.0;
    for (int i = from; i < to; ++i)
        acc += (double) x[(size_t) i] * std::polar (1.0, -6.283185307179586 * hz * i / sr);
    return (float) (2.0 * std::abs (acc) / (to - from));
}

static float rms (const std::vector<float>& x)
{
    double acc = 0.0;
    for (float v : x) acc += (double) v * v;
    return (float) std::sqrt (acc / (double) x.size());
}

static float dB (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

static std::vector<float> runMic (MicType t, float cm, std::vector<float> x, int sr)
{
    MicModel m; MicModel::Params p; p.type = t; p.distanceCm = cm;
    m.setParams (p); m.prepare (sr);
    for (float& v : x) v = m.process (v);
    return x;
}

// ---- synthetic vocal: five "sah" syllables with vibrato and a hissy "s" before each
static std::vector<float> synthVocal (int sr)
{
    const float notes[5] = { 175.0f, 196.0f, 220.0f, 196.0f, 165.0f };
    Biquad f1, f2, f3, sHp, sPk;
    f1.makeBandpass (sr, 700.0, 6.0);  f2.makeBandpass (sr, 1100.0, 8.0);  f3.makeBandpass (sr, 2600.0, 10.0);
    sHp.makeHighpass (sr, 5000.0, 0.7071);  sPk.makePeaking (sr, 7000.0, 6.0, 1.0);

    std::vector<float> out;
    uint32_t rng = 4242u;
    float phase = 0.0f, t = 0.0f;
    for (int n = 0; n < 5; ++n)
    {
        const int sLen = (int) (0.12f * sr), vLen = (int) (0.30f * sr), gap = (int) (0.05f * sr);
        for (int i = 0; i < sLen; ++i)                       // "s": shaped noise burst
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float nz = (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
            const float env = std::min (1.0f, i / (0.01f * sr)) * std::min (1.0f, (sLen - i) / (0.02f * sr));
            out.push_back (sPk.process (sHp.process (nz)) * env * 0.5f);
        }
        for (int i = 0; i < vLen; ++i)                       // "ah": formant-filtered sawtooth
        {
            t += 1.0f / sr;
            const float f0 = notes[n] * (1.0f + 0.006f * std::sin (6.2832f * 5.5f * t));
            phase += f0 / sr; phase -= std::floor (phase);
            const float saw = 2.0f * phase - 1.0f;
            const float v = f1.process (saw) + 0.6f * f2.process (saw) + 0.3f * f3.process (saw);
            const float env = std::min (1.0f, i / (0.02f * sr)) * std::min (1.0f, (vLen - i) / (0.06f * sr));
            out.push_back (v * env * 1.2f);
        }
        for (int i = 0; i < gap; ++i) out.push_back (0.0f);
    }
    float peak = 0.0f;
    for (float x : out) peak = std::max (peak, std::abs (x));
    for (float& x : out) x *= 0.5f / peak;                   // about -6 dBFS peak, like a real take
    return out;
}

int main()
{
    const int sr = 44100;

    // 1) proximity effect: a 100 Hz tone gets much louder at 3 cm than at 40 cm
    {
        auto in = sine (100.0f, 0.2f, sr, 1.0f);
        const float near = rms (runMic (MicType::DynamicStage, 3.0f, in, sr));
        const float far  = rms (runMic (MicType::DynamicStage, 40.0f, in, sr));
        std::printf ("Proximity (stage dynamic, 100 Hz): +%.1f dB closer\n", dB (near / far));
        assert (dB (near / far) > 8.0f);
    }

    // 2) voicing: the stage dynamic has a presence peak (5.5 kHz vs 1 kHz) at a far distance
    {
        const float a1 = rms (runMic (MicType::DynamicStage, 40.0f, sine (1000.0f, 0.2f, sr, 1.0f), sr));
        const float a5 = rms (runMic (MicType::DynamicStage, 40.0f, sine (5500.0f, 0.2f, sr, 1.0f), sr));
        std::printf ("Stage dynamic presence: %+.1f dB at 5.5 kHz vs 1 kHz\n", dB (a5 / a1));
        assert (dB (a5 / a1) > 3.0f);
    }

    // 3) saturator: adds even + odd harmonics, stays DC-free
    {
        Saturator s; Saturator::Params p; p.drive = 0.6f; p.bias = 0.2f;
        s.setParams (p); s.prepare (sr);
        auto x = sine (1000.0f, 0.4f, sr, 1.0f);
        double mean = 0.0;
        for (float& v : x) v = s.process (v);
        for (size_t i = (size_t) sr / 2; i < x.size(); ++i) mean += x[i];
        mean /= (double) (x.size() - (size_t) sr / 2);
        const float h1 = toneAmp (x, 1000.0f, sr, sr / 2, sr);
        const float h2 = toneAmp (x, 2000.0f, sr, sr / 2, sr);
        const float h3 = toneAmp (x, 3000.0f, sr, sr / 2, sr);
        std::printf ("Saturator: H2 %.1f dB, H3 %.1f dB below fundamental, DC %.5f\n",
                     dB (h1 / h2), dB (h1 / h3), mean);
        assert (h2 > 0.001f && h3 > 0.001f && std::abs (mean) < 0.002);
    }

    // 4) de-esser: pulls down a 6.5 kHz burst, leaves a 200 Hz tone alone
    {
        auto low  = sine (200.0f, 0.3f, sr, 1.0f);
        auto hiss = sine (6500.0f, 0.3f, sr, 1.0f);
        for (size_t i = 0; i < hiss.size(); ++i)
            if (i < (size_t) (0.4f * sr) || i > (size_t) (0.8f * sr)) hiss[i] = 0.0f;
        std::vector<float> in (low.size());
        for (size_t i = 0; i < in.size(); ++i) in[i] = low[i] + hiss[i];

        DeEsser d; d.prepare (sr);
        auto out = in;
        for (float& v : out) v = d.process (v);

        const int a = (int) (0.5f * sr), b = (int) (0.7f * sr);
        const float cut = dB (toneAmp (in, 6500.0f, sr, a, b) / toneAmp (out, 6500.0f, sr, a, b));
        const float lowChange = dB (toneAmp (out, 200.0f, sr, (int) (0.1f * sr), (int) (0.3f * sr))
                                  / toneAmp (in,  200.0f, sr, (int) (0.1f * sr), (int) (0.3f * sr)));
        std::printf ("De-esser: 6.5 kHz reduced %.1f dB, 200 Hz changed %.2f dB\n", cut, lowChange);
        assert (cut > 5.0f && cut < 11.0f && std::abs (lowChange) < 0.5f);
    }

    // 5) demos
    auto vocal = synthVocal (sr);

    // A/B through the full chain, loudness matched
    {
        VocalChain chain; chain.setParams (VocalChain::presetModernRap()); chain.prepare (sr);
        auto proc = vocal;
        float maxComp = 0.0f, maxEss = 0.0f;
        for (float& v : proc)
        {
            v = chain.process (v);
            maxComp = std::min (maxComp, chain.getCompressorReductionDb());
            maxEss  = std::max (maxEss,  chain.getDeEsserReductionDb());
        }
        std::printf ("Chain: compressor up to %.1f dB, de-esser up to %.1f dB\n", maxComp, maxEss);
        for (float x : proc) assert (std::isfinite (x));

        const float g = rms (vocal) / rms (proc);
        for (float& v : proc) v *= g;
        float peak = 0.0f;
        for (float v : proc) peak = std::max (peak, std::abs (v));
        const float safe = peak > 0.9f ? 0.9f / peak : 1.0f;

        std::vector<float> out;
        for (float v : vocal) out.push_back (v * safe);
        out.insert (out.end(), (size_t) (sr / 2), 0.0f);
        for (float v : proc) out.push_back (v * safe);
        writeWav ("vocal_ab.wav", out, sr);
    }

    // mic shootout: raw, then each mic at 15 cm, all matched to the raw RMS
    {
        const MicType types[5] = { MicType::CondenserClean, MicType::LargeDiaphragmWarm, MicType::TubeCondenser,
                                   MicType::DynamicBroadcast, MicType::DynamicStage };
        std::vector<std::vector<float>> segs { vocal };
        for (MicType t : types)
        {
            auto s = runMic (t, 15.0f, vocal, sr);
            const float g = rms (vocal) / rms (s);
            for (float& v : s) v *= g;
            segs.push_back (s);
        }
        std::vector<float> out;
        for (auto& s : segs)
        {
            out.insert (out.end(), s.begin(), s.end());
            out.insert (out.end(), (size_t) (sr * 0.4f), 0.0f);
        }
        float peak = 0.0f;
        for (float v : out) peak = std::max (peak, std::abs (v));
        if (peak > 0.9f) for (float& v : out) v *= 0.9f / peak;
        writeWav ("mic_shootout.wav", out, sr);
    }

    std::printf ("checks passed\n");
}
