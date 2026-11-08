#pragma once
#include "../Common/Biquad.h"
#include "../Common/Saturator.h"
#include <algorithm>

namespace boombap
{
// Character models of classic vocal microphone types: frequency voicing,
// proximity effect (bass boost as you move closer), and a touch of
// transformer/tube colour. These are original approximations of each type's
// general tonal character, NOT measured responses of any specific product.
// Swap in your own measured impulse responses later if you own the mics.
enum class MicType
{
    CondenserClean,      // bright, open small-diaphragm style   (NT1-style character)
    LargeDiaphragmWarm,  // smooth presence, full lows           (U87-style character)
    TubeCondenser,       // thick lows, silky top, tube colour   (U47-style character)
    DynamicBroadcast,    // tight, mid-forward, rolled-off top   (SM7B-style character)
    DynamicStage,        // aggressive presence peak, big proximity (SM58-style character)
    TokyoTube,           // bright, airy, expensive sheen        (C800G-style character)
    VintageTube251,      // silky top, detailed and smooth       (ELA M 251-style character)
    ReferenceStudio      // open, hi-fi, gentle tube warmth      (Manley Reference-style character)
};

class MicModel
{
public:
    struct Params
    {
        MicType type = MicType::LargeDiaphragmWarm;
        float distanceCm = 15.0f; // 3 = lips on the grille, 40+ = no proximity effect
    };

    void prepare (double sampleRate) { sr = sampleRate; setParams (params); reset(); }

    void reset() { for (auto& f : bands) f.reset(); hpf.reset(); prox.reset(); sat.reset(); }

    void setParams (const Params& p)
    {
        params = p;
        const Voicing v = voicingFor (p.type);

        numBands = v.count;
        for (int i = 0; i < v.count; ++i)
        {
            const Filt& f = v.f[i];
            if (f.kind == Peak)           bands[i].makePeaking   (sr, f.hz, f.gainDb, f.q);
            else if (f.kind == LowShelf)  bands[i].makeLowShelf  (sr, f.hz, f.gainDb, f.q);
            else                          bands[i].makeHighShelf (sr, f.hz, f.gainDb, f.q);
        }

        useHpf = v.hpfHz > 0.0f;
        if (useHpf) hpf.makeHighpass (sr, v.hpfHz, 0.7071);

        // proximity effect: low-shelf boost that grows as distance shrinks
        const float t = std::clamp ((40.0f - p.distanceCm) / 37.0f, 0.0f, 1.0f);
        proxDb = v.proximity * 14.0f * std::pow (t, 2.5f);
        useProx = proxDb > 0.05f;
        if (useProx) prox.makeLowShelf (sr, 150.0, proxDb, 0.7);

        Saturator::Params sp; sp.drive = v.satDrive; sp.bias = v.satBias;
        sat.setParams (sp);
        sat.prepare (sr);
    }

    float process (float x)
    {
        if (useHpf)  x = hpf.process (x);
        if (useProx) x = prox.process (x);
        for (int i = 0; i < numBands; ++i) x = bands[i].process (x);
        return sat.process (x);
    }

    float getProximityDb() const { return proxDb; }

private:
    enum Kind { Peak, LowShelf, HighShelf };
    struct Filt { Kind kind; float hz, gainDb, q; };
    struct Voicing
    {
        Filt f[4]; int count;
        float hpfHz, proximity, satDrive, satBias;
    };

    static Voicing voicingFor (MicType t)
    {
        switch (t)
        {
            case MicType::CondenserClean:
                return { { { HighShelf, 9000, 3.0f, 0.7f }, { Peak, 4500, 1.5f, 1.0f }, {}, {} }, 2,
                         0.0f, 0.15f, 0.0f, 0.0f };
            case MicType::LargeDiaphragmWarm:
                return { { { LowShelf, 150, 1.5f, 0.7f }, { Peak, 700, -1.0f, 1.0f },
                           { Peak, 5000, 2.5f, 1.0f }, { HighShelf, 12000, 1.5f, 0.7f } }, 4,
                         0.0f, 0.35f, 0.10f, 0.05f };
            case MicType::TubeCondenser:
                return { { { LowShelf, 200, 2.0f, 0.7f }, { Peak, 3500, 2.0f, 0.9f },
                           { HighShelf, 14000, -2.0f, 0.7f }, {} }, 3,
                         0.0f, 0.30f, 0.30f, 0.20f };
            case MicType::DynamicBroadcast:
                return { { { LowShelf, 120, -1.0f, 0.7f }, { Peak, 5000, 3.0f, 1.0f },
                           { HighShelf, 10000, -4.0f, 0.7f }, {} }, 3,
                         50.0f, 0.70f, 0.05f, 0.0f };
            case MicType::TokyoTube:
                return { { { LowShelf, 120, -1.0f, 0.7f }, { Peak, 6000, 2.0f, 0.9f },
                           { HighShelf, 12000, 4.5f, 0.7f }, {} }, 3,
                         0.0f, 0.30f, 0.22f, 0.15f };
            case MicType::VintageTube251:
                return { { { LowShelf, 180, 1.0f, 0.7f }, { Peak, 5000, 1.0f, 1.0f },
                           { HighShelf, 14000, 3.0f, 0.7f }, {} }, 3,
                         0.0f, 0.32f, 0.18f, 0.12f };
            case MicType::ReferenceStudio:
                return { { { LowShelf, 100, 0.5f, 0.7f }, { Peak, 3500, 1.5f, 0.9f },
                           { Peak, 8000, 1.0f, 1.0f }, { HighShelf, 11000, 2.5f, 0.7f } }, 4,
                         0.0f, 0.28f, 0.12f, 0.08f };
            case MicType::DynamicStage:
            default:
                return { { { LowShelf, 100, -3.0f, 0.7f }, { Peak, 400, -1.5f, 0.8f },
                           { Peak, 5500, 5.0f, 1.2f }, { HighShelf, 11000, -5.0f, 0.7f } }, 4,
                         90.0f, 1.00f, 0.05f, 0.0f };
        }
    }

    Params params;
    double sr = 44100.0;
    Biquad bands[4], hpf, prox;
    Saturator sat;
    int numBands = 0;
    float proxDb = 0.0f;
    bool useHpf = false, useProx = false;
};
} // namespace boombap
