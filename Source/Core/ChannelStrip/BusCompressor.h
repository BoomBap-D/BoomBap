#pragma once
#include "SslCompressor.h"

namespace boombap
{
// "Glue" stereo bus compressor with the familiar stepped controls:
// ratio 2/4/10, attack 0.1-30 ms, release 0.1-1.2 s or auto, linked stereo
// detection, and a detector high-pass so the kick doesn't pump the mix.
class BusCompressor
{
public:
    struct Params
    {
        float thresholdDb = -12.0f;
        int   ratioIndex   = 1;   // 0:2:1  1:4:1  2:10:1
        int   attackIndex  = 4;   // 0:0.1  1:0.3  2:1  3:3  4:10  5:30 ms
        int   releaseIndex = 4;   // 0:0.1  1:0.3  2:0.6  3:1.2 s  4:auto
        float makeupDb    = 0.0f;
        float mix         = 1.0f;
    };

    void prepare (double sampleRate) { comp.prepare (sampleRate); setParams (params); }

    void setParams (const Params& p)
    {
        params = p;
        static const float ratios[]   = { 2.0f, 4.0f, 10.0f };
        static const float attacks[]  = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
        static const float releases[] = { 100.0f, 300.0f, 600.0f, 1200.0f };

        SslCompressor::Params c;
        c.thresholdDb    = p.thresholdDb;
        c.ratio          = ratios[std::clamp (p.ratioIndex, 0, 2)];
        c.attackMs       = attacks[std::clamp (p.attackIndex, 0, 5)];
        c.autoRelease    = p.releaseIndex >= 4;
        c.releaseMs      = releases[std::clamp (p.releaseIndex, 0, 3)];
        c.kneeDb         = 3.0f;
        c.sidechainHpfHz = 90.0f;
        c.makeupDb       = p.makeupDb;
        c.mix            = p.mix;
        comp.setParams (c);
    }

    void process (float& l, float& r) { comp.processStereo (l, r); }
    float getGainReductionDb() const { return comp.getGainReductionDb(); }

private:
    Params params;
    SslCompressor comp;
};
} // namespace boombap
