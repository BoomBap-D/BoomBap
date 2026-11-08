#pragma once
#include "SslEq.h"
#include "SslCompressor.h"

namespace boombap
{
// One mono channel: EQ + compressor + output level, with the console's
// "dynamics pre/post EQ" switch. No JUCE dependency.
class ChannelStrip
{
public:
    struct Params
    {
        SslEq::Params         eq;
        SslCompressor::Params comp;
        bool  compEnabled  = true;
        bool  compBeforeEq = false;  // true = dynamics ahead of the EQ
        float outputDb     = 0.0f;
    };

    void prepare (double sampleRate)
    {
        eq.prepare (sampleRate);
        comp.prepare (sampleRate);
        setParams (params);
    }

    void setParams (const Params& p)
    {
        params = p;
        eq.setParams (p.eq);
        comp.setParams (p.comp);
        outGain = std::pow (10.0f, p.outputDb / 20.0f);
    }

    float process (float x)
    {
        if (params.compEnabled && params.compBeforeEq) x = comp.process (x);
        x = eq.process (x);
        if (params.compEnabled && ! params.compBeforeEq) x = comp.process (x);
        return x * outGain;
    }

    void processBlock (float* data, int n) { for (int i = 0; i < n; ++i) data[i] = process (data[i]); }

    float getGainReductionDb() const { return comp.getGainReductionDb(); }

private:
    Params params;
    SslEq eq;
    SslCompressor comp;
    float outGain = 1.0f;
};
} // namespace boombap
