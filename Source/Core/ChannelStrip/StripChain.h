#pragma once
#include "../Common/Biquad.h"
#include "../Common/Saturator.h"
#include "../Common/Limiter.h"
#include "../VocalChain/DeEsser.h"
#include "SslCompressor.h"
#include "GraphicEq.h"
#include <cmath>
#include <algorithm>

namespace boombap
{
// The Master Station's channel strip (Pass 2, items 19/26/65): one mono
// chain in the order D specified - a hip-hop channel strip in the
// lineage of the SSL boards, the Neve 1073, the Avalon VT-737SP and
// Softube's British Class A, drawn in BoomBap's own style and built
// from original approximations, never a clone of any of them:
//
//   PREAMP -> HIGH-PASS -> DE-ESSER -> COMPRESSOR -> SATURATION
//          -> GRAPHIC EQ -> LIMITER -> (fader + pan live in the mixer)
//
// The preamp and limiter only exist on the channels that want them
// (vocal/audio channels carry both; the engine gates them by kind).
// Every section has its own on/off; a strip with everything off costs
// one branch. No JUCE dependency.
class StripChain
{
public:
    struct Params
    {
        bool  preampOn = false;
        float preampDb = 0.0f;        // -12..+24, gain staging with character
        bool  hpfOn = false;
        float hpfHz = 20.0f;          // 10..400
        bool  deessOn = false;
        float deessAmount = 0.0f;     // 0..1
        bool  compOn = false;
        float compThresholdDb = -18.0f;
        float compRatio = 4.0f;       // 1..20
        float compAttackMs = 10.0f;
        float compReleaseMs = 100.0f;
        bool  satOn = false;
        float satDrive = 0.0f;        // 0..1
        GraphicEq::Params eq;
        bool  limiterOn = false;
        float limiterCeilingDb = -1.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        preampSat.prepare (sr);
        deesser.prepare (sr);
        comp.prepare (sr);
        saturator.prepare (sr);
        eq.prepare (sr);
        limiter.prepare (sr);
        setParams (params);
    }

    void setParams (const Params& p)
    {
        params = p;

        preampGain = std::pow (10.0f, p.preampDb / 20.0f);
        {
            Saturator::Params sp;
            // character grows with positive drive: past unity the preamp
            // starts to lean on the signal, 1073-style
            sp.drive = p.preampDb > 0.5f ? std::clamp ((p.preampDb - 0.5f) / 24.0f, 0.0f, 1.0f) * 0.55f : 0.0f;
            sp.bias = 0.12f;
            preampSat.setParams (sp);
            preampHasCharacter = sp.drive > 0.01f;
        }

        if (p.hpfOn)
        {
            hpf.makeHighpass (sr, std::clamp ((double) p.hpfHz, 10.0, 0.45 * sr), 0.7071);
            hpf2.makeHighpass (sr, std::clamp ((double) p.hpfHz, 10.0, 0.45 * sr), 0.7071);
        }

        {
            DeEsser::Params dp;
            dp.freqHz = 6500.0f;
            dp.q = 1.3f;
            dp.thresholdDb = -16.0f - 28.0f * std::clamp (p.deessAmount, 0.0f, 1.0f);
            dp.maxReductionDb = 3.0f + 11.0f * std::clamp (p.deessAmount, 0.0f, 1.0f);
            dp.ratio = 5.0f;
            deesser.setParams (dp);
        }

        {
            SslCompressor::Params cp;
            cp.thresholdDb = p.compThresholdDb;
            cp.ratio = std::clamp (p.compRatio, 1.0f, 20.0f);
            cp.attackMs = p.compAttackMs;
            cp.releaseMs = p.compReleaseMs;
            cp.kneeDb = 3.0f;
            cp.makeupDb = 0.0f;
            cp.sidechainHpfHz = 60.0f; // keep kick energy from pumping the strip
            comp.setParams (cp);
        }

        {
            Saturator::Params sp;
            sp.drive = std::clamp (p.satDrive, 0.0f, 1.0f);
            sp.bias = 0.15f;
            saturator.setParams (sp);
        }

        eq.setParams (p.eq);

        {
            Limiter::Params lp;
            lp.ceilingDb = p.limiterCeilingDb;
            limiter.setParams (lp);
        }

        active = p.preampOn || p.hpfOn || p.deessOn || p.compOn || p.satOn
                 || eqAnyOn (p.eq) || p.limiterOn;
    }

    bool isActive() const { return active; }

    float process (float x)
    {
        const auto& p = params;
        if (p.preampOn)
        {
            x *= preampGain;
            if (preampHasCharacter) x = preampSat.process (x);
        }
        if (p.hpfOn) { x = hpf.process (x); x = hpf2.process (x); }
        if (p.deessOn) x = deesser.process (x);
        if (p.compOn) x = comp.process (x);
        if (p.satOn) x = saturator.process (x);
        if (eqAnyOn (p.eq)) x = eq.process (x);
        if (p.limiterOn) x = limiter.process (x);
        return x;
    }

    float getCompGrDb() const    { return params.compOn ? comp.getGainReductionDb() : 0.0f; }
    float getDeessGrDb() const   { return params.deessOn ? -deesser.getReductionDb() : 0.0f; }
    float getLimiterGrDb() const { return params.limiterOn ? limiter.getGainReductionDb() : 0.0f; }

private:
    static bool eqAnyOn (const GraphicEq::Params& ep)
    {
        for (int i = 0; i < GraphicEq::kMaxBands && i < ep.numBands; ++i)
            if (ep.bands[(size_t) i].on) return true;
        return false;
    }

    Params params;
    double sr = 44100.0;
    Saturator preampSat;
    Biquad hpf, hpf2;
    DeEsser deesser;
    SslCompressor comp;
    Saturator saturator;
    GraphicEq eq;
    Limiter limiter;
    float preampGain = 1.0f;
    bool preampHasCharacter = false;
    bool active = false;
};
} // namespace boombap
