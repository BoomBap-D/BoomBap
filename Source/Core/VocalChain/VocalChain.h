#pragma once
#include "MicModel.h"
#include "DeEsser.h"
#include "../Common/Saturator.h"
#include "../ChannelStrip/SslCompressor.h"
#include "../ChannelStrip/SslEq.h"

namespace boombap
{
// Complete mono vocal path:
//   input gain -> mic model -> high-pass -> preamp saturation -> de-esser
//   -> compressor -> EQ (presence/air) -> output level
// No JUCE dependency.
class VocalChain
{
public:
    struct Params
    {
        float inputDb = 0.0f;
        MicModel::Params mic;
        float hpfHz = 80.0f;
        Saturator::Params preamp { 0.2f, 0.1f };
        bool  deEssEnabled = true;
        DeEsser::Params deess;
        bool  compEnabled = true;
        SslCompressor::Params comp;
        SslEq::Params eq;
        float outputDb = 0.0f;
    };

    // A ready-made starting point for rap/R&B vocals.
    static Params presetModernRap()
    {
        Params p;
        p.mic.type = MicType::TubeCondenser;
        p.mic.distanceCm = 12.0f;
        p.hpfHz = 80.0f;
        p.preamp = { 0.25f, 0.12f };
        p.comp.thresholdDb = -20.0f; p.comp.ratio = 3.5f; p.comp.attackMs = 8.0f;
        p.comp.releaseMs = 120.0f;   p.comp.kneeDb = 4.0f; p.comp.makeupDb = 4.0f;
        p.eq.lmf = { 350.0f, -2.0f, 1.0f };
        p.eq.hf  = { 12000.0f, 3.0f, 0.7f };
        return p;
    }

    // Chain pairing tuned per mic profile: the classic sound is mic + chain,
    // so each locker mic gets the compressor/EQ/preamp treatment that suits it.
    static Params presetForMic (MicType type)
    {
        Params p = presetModernRap();
        p.mic.type = type;
        switch (type)
        {
            case MicType::TokyoTube:        // already bright: less extra air, firmer control
                p.comp.thresholdDb = -22.0f; p.comp.ratio = 3.0f; p.comp.makeupDb = 3.0f;
                p.preamp = { 0.18f, 0.10f };
                p.eq.hf = { 13000.0f, 2.0f, 0.7f };
                break;
            case MicType::VintageTube251:   // smooth and silky: a lighter touch all round
                p.comp.thresholdDb = -20.0f; p.comp.ratio = 2.5f; p.comp.makeupDb = 3.0f;
                p.preamp = { 0.22f, 0.14f };
                p.eq.hf = { 12000.0f, 2.5f, 0.7f };
                break;
            case MicType::ReferenceStudio:  // open hi-fi: keep the chain out of the way
                p.comp.thresholdDb = -21.0f; p.comp.ratio = 3.0f; p.comp.makeupDb = 3.5f;
                p.preamp = { 0.15f, 0.08f };
                p.eq.hf = { 12000.0f, 2.0f, 0.7f };
                break;
            case MicType::DynamicBroadcast: // low-output dynamic: more drive, firmer compression
                p.comp.thresholdDb = -18.0f; p.comp.ratio = 4.0f; p.comp.makeupDb = 5.0f;
                p.preamp = { 0.30f, 0.10f };
                p.eq.lmf = { 300.0f, -1.5f, 1.0f };
                break;
            default:
                break;
        }
        return p;
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        mic.prepare (sr); pre.prepare (sr); deess.prepare (sr); comp.prepare (sr); eq.prepare (sr);
        setParams (params);
        hpf.reset();
    }

    void setParams (const Params& p)
    {
        params = p;
        inGain  = std::pow (10.0f, p.inputDb  / 20.0f);
        outGain = std::pow (10.0f, p.outputDb / 20.0f);
        mic.setParams (p.mic);
        useHpf = p.hpfHz > 0.0f;
        if (useHpf) hpf.makeHighpass (sr, p.hpfHz, 0.7071);
        pre.setParams (p.preamp);
        deess.setParams (p.deess);
        comp.setParams (p.comp);
        eq.setParams (p.eq);
    }

    float process (float x)
    {
        x *= inGain;
        x = mic.process (x);
        if (useHpf) x = hpf.process (x);
        x = pre.process (x);
        if (params.deEssEnabled) x = deess.process (x);
        if (params.compEnabled)  x = comp.process (x);
        x = eq.process (x);
        return x * outGain;
    }

    void processBlock (float* data, int n) { for (int i = 0; i < n; ++i) data[i] = process (data[i]); }

    float getCompressorReductionDb() const { return comp.getGainReductionDb(); }
    float getDeEsserReductionDb()    const { return deess.getReductionDb(); }

private:
    Params params;
    double sr = 44100.0;
    MicModel mic;
    Biquad hpf;
    Saturator pre;
    DeEsser deess;
    SslCompressor comp;
    SslEq eq;
    float inGain = 1.0f, outGain = 1.0f;
    bool useHpf = true;
};
} // namespace boombap
