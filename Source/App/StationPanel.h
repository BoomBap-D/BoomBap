#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <complex>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "Engine/ProjectSettings.h"
#include "../Core/ChannelStrip/GraphicEq.h"
#include "BoomBapLookAndFeel.h"
#include "UiComponents.h"
#include "DataHome.h"

// ============================================================================
// PASS 2 - THE STATION (items 7, 9, 19, 20, 26, 38, 65)
//
// The Master Station: a hero channel strip bound to the SELECTED channel,
// in BoomBap's own near-black + amber style with the classic channel-strip
// lineage (SSL 4000/9000, Neve 1073, Avalon VT-737SP) as design input only -
// original approximations, never clones. The chain is item 65's:
// PREAMP (audio/vocal) -> HIGH-PASS -> DE-ESSER -> COMPRESSOR ->
// SATURATION/CHARACTER -> GRAPHIC EQ -> LIMITER (audio/vocal) -> fader+pan.
// One button flips the Station to the FULL MIXING BOARD (every channel a
// compact strip); selecting a board strip rebinds the Station to it.
// The transport lives here too (item 19): PLAY / STOP / REWIND / FAST
// FORWARD + UNDO / REDO (item 38). The master bus shows the Glue and the
// master limiter - the mastering channel's own LOOK is Pass 6.
// ============================================================================
namespace boombap
{
namespace stationui
{
inline juce::String freqText (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String ((int) std::lround (hz)) + " Hz";
}
inline juce::String gainDbText (float g) { return (g >= 0.0f ? "+" : "") + juce::String (g, 1) + " dB"; }
} // namespace stationui

// ----------------------------------------------------------------------------
// The graphic EQ display (item 9): spectrum analyser behind the true
// response curve, draggable band nodes. Double-click empty space adds a
// peak band there; double-click a node toggles it on/off; the mouse wheel
// over a node trims its Q. (The EQ display keeps double-click-to-create;
// D's double-click-reset rule governs knobs and sliders, which all carry
// setDoubleClickReturnValue.)
// ----------------------------------------------------------------------------
class EqDisplay : public juce::Component
{
public:
    std::function<void (const StripSettings&)> onStripEdited;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<void (int)> onBandSelected;

    EqDisplay (BoomBapEngine& eng) : engine (eng) {}

    void setStrip (const StripSettings& s)
    {
        strip = s;
        if (selectedBand >= strip.eqNumBands) selectedBand = juce::jmax (0, strip.eqNumBands - 1);
        rebuildCurve();
        repaint();
    }
    const StripSettings& getStrip() const { return strip; }
    int getSelectedBand() const { return selectedBand; }
    // Round 2: a band node's centre in local coordinates - the same
    // numbers paint() and the mouse handlers use (the knob-sync test
    // aims its fabricated drags here).
    juce::Point<float> bandNodePosition (int i) const { return nodeCentre (i); }
    void selectBand (int b)
    {
        selectedBand = juce::jlimit (0, StripSettings::kEqBands - 1, b);
        repaint();
    }

    // Called by the owner's timer: refresh the analyser spectrum.
    void tick()
    {
        computeSpectrum();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        auto b = getLocalBounds().toFloat();
        g.setColour (panelAlt().darker (0.45f));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (border().withAlpha (0.5f));
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        // grid: frequency lines + labels, dB lines
        g.setFont (9.5f);
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = freqToX (f);
            g.setColour (border().withAlpha (0.25f));
            g.drawLine (x, b.getY() + 4.0f, x, b.getBottom() - 16.0f, 1.0f);
            g.setColour (muted().withAlpha (0.8f));
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                        (int) x - 12, (int) b.getBottom() - 15, 30, 11, juce::Justification::centred);
        }
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const float y = gainToY (db);
            g.setColour (db == 0.0f ? border().withAlpha (0.55f) : border().withAlpha (0.22f));
            g.drawLine (b.getX() + 4.0f, y, b.getRight() - 4.0f, y, 1.0f);
            g.setColour (muted().withAlpha (0.8f));
            g.drawText ((db >= 0.0f ? "+" : "") + juce::String ((int) db), (int) b.getX() + 5, (int) y - 6, 26, 11,
                        juce::Justification::left);
        }

        // the spectrum, behind everything (item 9)
        if (! spectrum.empty() && getWidth() > 8)
        {
            juce::Path p;
            const float base = b.getBottom() - 16.0f;
            p.startNewSubPath (b.getX() + 2.0f, base);
            for (int x = 0; x < (int) spectrum.size(); ++x)
            {
                const float px = b.getX() + 2.0f + (b.getWidth() - 4.0f) * (float) x / (float) (spectrum.size() - 1);
                const float frac = juce::jlimit (0.0f, 1.0f, (spectrum[(size_t) x] + 84.0f) / 66.0f); // -84..-18 dB window
                p.lineTo (px, base - frac * (b.getHeight() - 30.0f));
            }
            p.lineTo (b.getRight() - 2.0f, base);
            p.closeSubPath();
            g.setColour (juce::Colour (0xff9aa3b2).withAlpha (0.30f)); // grey analyzer (approved mockup)
            g.fillPath (p);
        }

        // the true response curve
        {
            juce::Path p;
            const int w = (int) b.getWidth();
            for (int x = 0; x < w; x += 3)
            {
                const float db = curveEq.responseDb (xToFreq ((float) x));
                const float y = gainToY (juce::jlimit (-20.0f, 20.0f, db));
                if (x == 0) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y);
            }
            g.setColour (accentHi());
            g.strokePath (p, juce::PathStrokeType (2.2f));
        }

        // band nodes
        for (int i = 0; i < strip.eqNumBands; ++i)
        {
            const auto& band = strip.eq[(size_t) i];
            const auto c = nodeCentre (i);
            const float r = i == selectedBand ? 11.0f : 9.0f;
            juce::Colour col = band.type == GraphicEq::HighPass || band.type == GraphicEq::LowPass
                                   ? juce::Colour (0xff7fb2d9)
                                   : (band.type == GraphicEq::Peak ? accent() : accentHi());
            if (! band.on) col = col.withAlpha (0.30f);
            g.setColour (col);
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
            if (i == selectedBand)
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (c.x - r - 2.0f, c.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f, 1.6f);
            }
            g.setColour (band.on ? juce::Colour (0xff14100a) : muted());
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), (int) (c.x - r), (int) (c.y - 7.0f), (int) (r * 2.0f), 14,
                        juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = bandAt (e.position);
        if (hit >= 0)
        {
            selectedBand = hit;
            if (onBandSelected) onBandSelected (hit);
            dragBand = hit;
            if (onGestureBegin) onGestureBegin();
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0 || dragBand >= strip.eqNumBands) return;
        auto& band = strip.eq[(size_t) dragBand];
        band.freqHz = xToFreq (e.position.x);
        if (band.type == GraphicEq::HighPass || band.type == GraphicEq::LowPass)
            band.q = yToQ (e.position.y);
        else
            band.gainDb = yToGain (e.position.y);
        edited();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
        {
            dragBand = -1;
            if (onGestureEnd) onGestureEnd();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = bandAt (e.position);
        if (onGestureBegin) onGestureBegin();
        if (hit >= 0)
        {
            auto& band = strip.eq[(size_t) hit];
            band.on = ! band.on; // double-click a node toggles it
            selectedBand = hit;
            if (onBandSelected) onBandSelected (hit);
        }
        else if (strip.eqNumBands < StripSettings::kEqBands)
        {
            const int idx = strip.eqNumBands++;
            auto& band = strip.eq[(size_t) idx];
            band.on = true;
            band.type = GraphicEq::Peak;
            band.freqHz = xToFreq (e.position.x);
            band.gainDb = yToGain (e.position.y);
            band.q = 1.0f;
            selectedBand = idx;
            if (onBandSelected) onBandSelected (idx);
        }
        edited();
        if (onGestureEnd) onGestureEnd();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int hit = bandAt (e.position);
        if (hit < 0) return;
        if (onGestureBegin) onGestureBegin();
        auto& band = strip.eq[(size_t) hit];
        band.q = juce::jlimit (0.1f, 18.0f, band.q * (wheel.deltaY > 0.0f ? 1.18f : 1.0f / 1.18f));
        selectedBand = hit;
        if (onBandSelected) onBandSelected (hit);
        edited();
        if (onGestureEnd) onGestureEnd();
    }

private:
    static constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f, kRangeDb = 18.0f;

    float freqToX (float f) const
    {
        const float t = (std::log (juce::jlimit (kMinFreq, kMaxFreq, f)) - std::log (kMinFreq))
                        / (std::log (kMaxFreq) - std::log (kMinFreq));
        return 4.0f + t * (float) juce::jmax (8, getWidth() - 8);
    }
    float xToFreq (float x) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - 4.0f) / (float) juce::jmax (8, getWidth() - 8));
        return std::exp (std::log (kMinFreq) + t * (std::log (kMaxFreq) - std::log (kMinFreq)));
    }
    float gainToY (float db) const
    {
        const float h = (float) juce::jmax (8, getHeight() - 20);
        return 4.0f + (1.0f - (juce::jlimit (-kRangeDb, kRangeDb, db) + kRangeDb) / (2.0f * kRangeDb)) * h;
    }
    float yToGain (float y) const
    {
        const float h = (float) juce::jmax (8, getHeight() - 20);
        const float t = juce::jlimit (0.0f, 1.0f, (y - 4.0f) / h);
        return (1.0f - t) * 2.0f * kRangeDb - kRangeDb;
    }
    float yToQ (float y) const
    {
        const float h = (float) juce::jmax (8, getHeight() - 20);
        const float t = juce::jlimit (0.0f, 1.0f, (y - 4.0f) / h); // top = narrow
        return std::exp (std::log (10.0f) + t * (std::log (0.4f) - std::log (10.0f)));
    }

    juce::Point<float> nodeCentre (int i) const
    {
        const auto& band = strip.eq[(size_t) i];
        const float x = freqToX (band.freqHz);
        const float y = (band.type == GraphicEq::HighPass || band.type == GraphicEq::LowPass)
                            ? gainToY (0.0f) + 14.0f // filters park just under the 0 dB line
                            : gainToY (band.gainDb);
        return { x, y };
    }

    int bandAt (juce::Point<float> pos) const
    {
        int best = -1;
        float bestD = 15.0f;
        for (int i = 0; i < strip.eqNumBands; ++i)
        {
            const float d = pos.getDistanceFrom (nodeCentre (i));
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void edited()
    {
        rebuildCurve();
        if (onStripEdited) onStripEdited (strip);
        repaint();
    }

    void rebuildCurve()
    {
        const double srNow = engine.getSampleRate();
        if (! curveReady || std::abs (srNow - curveSr) > 1.0)
        {
            curveEq.prepare (srNow > 0.0 ? srNow : 44100.0);
            curveSr = srNow;
            curveReady = true;
        }
        GraphicEq::Params p;
        p.numBands = strip.eqNumBands;
        for (int i = 0; i < GraphicEq::kMaxBands; ++i)
        {
            p.bands[(size_t) i].on = strip.eq[i].on;
            p.bands[(size_t) i].type = strip.eq[i].type;
            p.bands[(size_t) i].freqHz = strip.eq[i].freqHz;
            p.bands[(size_t) i].gainDb = strip.eq[i].gainDb;
            p.bands[(size_t) i].q = strip.eq[i].q;
        }
        curveEq.setParams (p);
    }

    void computeSpectrum()
    {
        constexpr int N = 2048;
        float buf[4096];
        engine.copyAnalyserSamples (buf, 4096);
        std::vector<std::complex<float>> fft ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * (float) i / (float) (N - 1));
            fft[(size_t) i] = buf[4096 - N + i] * w;
        }
        // iterative radix-2 FFT
        for (int i = 1, j = 0; i < N; ++i)
        {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (fft[(size_t) i], fft[(size_t) j]);
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const float ang = -2.0f * 3.14159265f / (float) len;
            const std::complex<float> wl (std::cos (ang), std::sin (ang));
            for (int i = 0; i < N; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (int j = 0; j < len / 2; ++j)
                {
                    const auto u = fft[(size_t) (i + j)];
                    const auto v = fft[(size_t) (i + j + len / 2)] * w;
                    fft[(size_t) (i + j)] = u + v;
                    fft[(size_t) (i + j + len / 2)] = u - v;
                    w *= wl;
                }
            }
        }
        const double srNow = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
        constexpr int kPoints = 128;
        if ((int) spectrum.size() != kPoints) spectrum.assign (kPoints, -90.0f);
        for (int i = 0; i < kPoints; ++i)
        {
            const float t = (float) i / (float) (kPoints - 1);
            const float f = std::exp (std::log (kMinFreq) + t * (std::log (kMaxFreq) - std::log (kMinFreq)));
            const int bin = juce::jlimit (1, N / 2 - 1, (int) std::lround (f / srNow * (double) N));
            const float mag = std::abs (fft[(size_t) bin]) / (float) N;
            const float db = 20.0f * std::log10 (std::max (1.0e-6f, mag)) + 12.0f;
            // fast attack, slow fall - in the dB domain the fall must be
            // SUBTRACTIVE (multiplying a negative dB value walks it UP
            // toward 0 dB, which filled the whole plot with phantom level)
            spectrum[(size_t) i] = std::max (db, spectrum[(size_t) i] - 1.5f);
        }
    }

    BoomBapEngine& engine;
    StripSettings strip;
    GraphicEq curveEq;
    bool curveReady = false;
    double curveSr = 0.0;
    int selectedBand = 2;
    int dragBand = -1;
    std::vector<float> spectrum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};

// ----------------------------------------------------------------------------
// Long L/R VU-style meters for the Station rail (item 19): tall bars with
// peak hold and dB marks - the analog-console read the strip deserves.
// ----------------------------------------------------------------------------
class VuMeter : public juce::Component
{
public:
    void setLevels (float l, float r)
    {
        levelL = std::max (l, levelL * 0.86f);
        levelR = std::max (r, levelR * 0.86f);
        peakL = std::max (l, peakL * 0.995f);
        peakR = std::max (r, peakR * 0.995f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        auto b = getLocalBounds().toFloat();
        g.setColour (muted());
        g.setFont (9.0f);
        g.drawText ("L", (int) b.getX(), (int) b.getBottom() - 12, 20, 10, juce::Justification::centred);
        g.drawText ("R", (int) b.getRight() - 20, (int) b.getBottom() - 12, 20, 10, juce::Justification::centred);
        const float top = b.getY() + 4.0f, h = b.getHeight() - 20.0f;
        const float barW = std::min (16.0f, b.getWidth() * 0.26f);
        const float lx = b.getCentreX() - barW - 5.0f, rx = b.getCentreX() + 5.0f;
        auto dbToY = [&] (float db)
        {
            const float t = (juce::jlimit (-48.0f, 3.0f, db) + 48.0f) / 51.0f; // -48..+3 dB -> 0..1
            return top + (1.0f - t) * h;
        };
        auto drawBar = [&] (float x, float level, float peak)
        {
            g.setColour (juce::Colour (0xff262c38)); // empty track, visible on the rail
            g.fillRoundedRectangle (x, top, barW, h, 3.0f);
            const float db = level > 0.0004f ? 20.0f * std::log10 (level) : -60.0f;
            const float y = dbToY (db);
            if (db > -48.0f)
            {
                juce::ColourGradient grad (juce::Colour (0xffe5484d), x, top,
                                           accent(), x, top + h, false);
                grad.addColour (0.72, juce::Colour (0xfff5c542));
                g.setGradientFill (grad);
                g.fillRoundedRectangle (x, y, barW, top + h - y, 3.0f);
            }
            const float pdb = peak > 0.0004f ? 20.0f * std::log10 (peak) : -60.0f;
            if (pdb > -48.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (x, dbToY (pdb), barW, 2.0f);
            }
        };
        drawBar (lx, levelL, peakL);
        drawBar (rx, levelR, peakR);
        g.setColour (muted().withAlpha (0.75f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f })
        {
            const float y = dbToY (db);
            g.drawLine (b.getX() + 2.0f, y, b.getRight() - 2.0f, y, 0.6f);
            g.drawText (db == 0.0f ? "0" : juce::String ((int) db), (int) b.getX(), (int) y - 5, 18, 9,
                        juce::Justification::left);
        }
    }

private:
    float levelL = 0.0f, levelR = 0.0f, peakL = 0.0f, peakR = 0.0f;
};

// ----------------------------------------------------------------------------
// Round 2 (item 96c): the master's VU meter - a needle with VU
// ballistics (fast rise, ~300 ms fall) over the classic -20..+3 VU
// scale, sitting above the master fader in the rail, IN ADDITION to
// the long L/R meters. BoomBap dress: near-black face, amber needle,
// red zone past 0 VU.
// ----------------------------------------------------------------------------
class VuNeedle : public juce::Component
{
public:
    VuNeedle() {}
    void setLevel (float linear)
    {
        const float db = linear > 0.0004f ? 20.0f * std::log10 (linear) : -60.0f;
        if (db > shownDb) shownDb = db;                          // rise at once
        else shownDb = std::max (db, shownDb - 1.1f);            // per 30 Hz tick ≈ VU fall
        repaint();
    }
    float shownLevelDb() const { return shownDb; }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff101216));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (border().withAlpha (0.6f));
        g.drawRoundedRectangle (b, 6.0f, 1.0f);

        const juce::Point<float> pivot (b.getCentreX(), b.getBottom() - 10.0f);
        const float len = b.getHeight() * 0.82f;
        auto angleFor = [] (float vu)
        {
            const float t = (juce::jlimit (-20.0f, 3.5f, vu) + 20.0f) / 23.5f;
            return (-38.0f + t * 76.0f) * 0.01745329f; // radians from vertical
        };
        auto tip = [&] (float vu, float scale)
        {
            const float a = angleFor (vu);
            return juce::Point<float> (pivot.x + std::sin (a) * len * scale,
                                       pivot.y - std::cos (a) * len * scale);
        };
        // red zone arc (0..+3 VU)
        {
            juce::Path p;
            for (int i = 0; i <= 10; ++i)
            {
                const auto pt = tip ((float) i / 10.0f * 3.0f, 0.97f);
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (juce::Colour (0xffe5484d).withAlpha (0.85f));
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }
        g.setFont (7.5f);
        for (float vu : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -1.0f, 0.0f, 3.0f })
        {
            const auto p1 = tip (vu, 0.88f), p2 = tip (vu, 0.97f);
            g.setColour (vu >= 0.0f ? juce::Colour (0xffe5484d) : muted());
            g.drawLine (p1.x, p1.y, p2.x, p2.y, vu == 0.0f ? 1.6f : 1.0f);
            if (vu == -20.0f || vu == -10.0f || vu == -5.0f || vu == 0.0f || vu == 3.0f)
            {
                const auto tp = tip (vu, 0.72f);
                g.drawText (vu == 3.0f ? "+3" : juce::String ((int) vu),
                            (int) tp.x - 10, (int) tp.y - 5, 20, 9, juce::Justification::centred);
            }
        }
        // the needle
        const auto n = tip (shownDb, 1.0f);
        g.setColour (accentHi());
        g.drawLine (pivot.x, pivot.y, n.x, n.y, 2.2f);
        g.setColour (border());
        g.fillEllipse (pivot.x - 3.5f, pivot.y - 3.5f, 7.0f, 7.0f);
        g.setColour (muted());
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText ("VU", (int) b.getX(), (int) b.getBottom() - 13, (int) b.getWidth(), 10,
                    juce::Justification::centred);
    }

private:
    float shownDb = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuNeedle)
};

// ----------------------------------------------------------------------------
// The FULL MIXING BOARD (scope item 5): every channel as a compact strip -
// name, fader, pan knob, MUTE, SOLO, meter - in track order (item 35),
// scrollable. Clicking a strip selects its channel; the Station binds to
// it when you flip back.
// ----------------------------------------------------------------------------
class MixingBoard : public juce::Component, private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void (int)> onSelectChannel;
        std::function<void()> onBeforeChange, onAfterChange;
    };

    MixingBoard (BoomBapEngine& eng, Callbacks cb) : engine (eng), callbacks (std::move (cb))
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (12);
        addAndMakeVisible (viewport);
        rebuild();
        startTimerHz (30);
    }

    ~MixingBoard() override { stopTimer(); }

    void rebuild()
    {
        strips.clear();
        channels.clear();
        const int n = engine.getNumTracks();
        for (int t = 0; t < n; ++t) channels.push_back (t);
        channels.push_back (BoomBapEngine::kSynthChannel);
        channels.push_back (BoomBapEngine::kPianoChannel);
        channels.push_back (BoomBapEngine::kPadBusChannel);
        for (int i = 0; i < engine.getNumAudioTracks(); ++i)
            channels.push_back (BoomBapEngine::kAudioChannelBase + i);
        channels.push_back (BoomBapEngine::kVocalChannel);
        channels.push_back (BoomBapEngine::kMasterChannel);
        for (int ch : channels)
        {
            auto s = std::make_unique<Strip> (*this, ch);
            content.addAndMakeVisible (*s);
            strips.push_back (std::move (s));
        }
        lastNumTracks = engine.getNumTracks();
        lastNumAudio = engine.getNumAudioTracks();
        resized();
    }

    void refreshValues()
    {
        for (auto& s : strips) s->pullFromEngine();
    }

    void setSelectedChannel (int ch)
    {
        selected = ch;
        for (auto& s : strips) s->setSelected (s->channelId() == ch);
    }

    // Round 2: the board's pieces, reachable for the regression tests -
    // the same objects the mouse drives, no test-only back doors.
    int stripCount() const { return (int) strips.size(); }
    juce::Component* stripComponent (int ch)
    {
        for (auto& s : strips) if (s->channelId() == ch) return s.get();
        return nullptr;
    }
    juce::TextButton* stripMuteButton (int ch)
    {
        for (auto& s : strips) if (s->channelId() == ch) return &s->muteButton();
        return nullptr;
    }
    juce::TextButton* stripSoloButton (int ch)
    {
        for (auto& s : strips) if (s->channelId() == ch) return &s->soloButton();
        return nullptr;
    }
    ConsoleFader* stripFader (int ch)
    {
        for (auto& s : strips) if (s->channelId() == ch) return &s->faderControl();
        return nullptr;
    }
    bool stripSelected (int ch)
    {
        for (auto& s : strips) if (s->channelId() == ch) return s->isSelected();
        return false;
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int stripW = 92, gap = 6;
        const int totalW = 12 + (int) strips.size() * (stripW + gap);
        content.setSize (std::max (totalW, getWidth()), std::max (getHeight() - 14, 200));
        int x = 8;
        for (auto& s : strips)
        {
            s->setBounds (x, 8, stripW, content.getHeight() - 16);
            x += stripW + gap;
        }
    }

private:
    class Strip : public juce::Component
    {
    public:
        Strip (MixingBoard& board, int ch) : owner (board), engine (board.engine), channel (ch)
        {
            name.setText (juce::String (engine.channelDisplayName (ch)),
                          juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, boombap::ui::text());
            name.setFont (juce::Font (11.5f, juce::Font::bold));
            name.setJustificationType (juce::Justification::centred);
            // Round 2 (item 95): the name does not swallow clicks - a
            // click on the NAME selects the channel like a click
            // anywhere else on the strip (it lands on the Strip's own
            // mouseDown below).
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);

            // Round 2 (item 95): the approved console long fader (item
            // 79's pick) replaces the side slider whose thumb vanished
            // on grab - the ConsoleFader paints its own cap from its
            // own value, and the audio side ramps the gain (no zipper).
            fader.setRange (0.0, 1.2, defaultGain());
            fader.setMeterColumns (1);
            fader.onDragStart = [this]
            {
                select();
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
            };
            fader.onDragEnd = [this] { if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange(); };
            fader.onValueChange = [this] (double v) { engine.setChannelGain (channel, (float) v); };
            addAndMakeVisible (fader);

            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.onDragStart = [this]
            {
                select();
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
            };
            pan.onDragEnd = [this] { if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange(); };
            pan.onValueChange = [this] { engine.setChannelPan (channel, (float) pan.getValue()); };
            addAndMakeVisible (pan);
            pan.setVisible (channel != BoomBapEngine::kMasterChannel);

            mute.setButtonText ("M");
            mute.setClickingTogglesState (true);
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colours::indianred);
            mute.onClick = [this]
            {
                // Round 2 (item 95 root cause): the engine write comes
                // FIRST and the select LAST. The old order selected
                // first; the rebind's refresh then read the engine's
                // PRE-click state back over the button, and the click
                // wrote that stale state to the engine - the board's
                // MUTE/SOLO quite literally cancelled themselves out.
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
                engine.setChannelMute (channel, mute.getToggleState());
                if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange();
                select();
            };
            addAndMakeVisible (mute);
            mute.setVisible (channel != BoomBapEngine::kMasterChannel);

            solo.setButtonText ("S");
            solo.setClickingTogglesState (true);
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
            solo.onClick = [this]
            {
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
                engine.setChannelSolo (channel, solo.getToggleState());
                if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange();
                select();
            };
            addAndMakeVisible (solo);
            solo.setVisible (channel != BoomBapEngine::kMasterChannel
                             && channel != BoomBapEngine::kPadBusChannel);

            pullFromEngine();
        }

        int channelId() const { return channel; }

        double defaultGain() const
        {
            switch (BoomBapEngine::channelKind (channel))
            {
                case BoomBapEngine::KindAudio: return 0.9;
                case BoomBapEngine::KindVocal: return 0.9;
                case BoomBapEngine::KindSynth: return 0.8;
                case BoomBapEngine::KindPiano: return 0.85;
                case BoomBapEngine::KindPadBus: return 1.0;
                case BoomBapEngine::KindMaster: return 1.0;
                default: return 0.8;
            }
        }

        void pullFromEngine()
        {
            // Round 3 (item 88): the strip's name follows the track's
            // content name, like everywhere else (set only on change -
            // this runs on every board refresh).
            const juce::String n (engine.channelDisplayName (channel));
            if (name.getText() != n) name.setText (n, juce::dontSendNotification);
            fader.setValue (engine.getChannelGain (channel), false);
            fader.setDefaultValue (defaultGain());
            pan.setValue (channel == BoomBapEngine::kMasterChannel ? 0.0 : engine.getChannelPan (channel),
                          juce::dontSendNotification);
            mute.setToggleState (engine.getChannelMute (channel), juce::dontSendNotification);
            solo.setToggleState (engine.getChannelSolo (channel), juce::dontSendNotification);
        }

        // the fader's own LED ladder is the strip meter now (item 95)
        void setMeter (float l, float r) { fader.setMeterLevels (l, r); }
        void setSelected (bool s)
        {
            if (selectedFlag == s) return;
            selectedFlag = s;
            // Round 2 (item 95): the selected channel's NAME lights in
            // its box - dark text on the amber fill, the item 68
            // SELECTED language, on top of the whole-strip trace.
            name.setColour (juce::Label::textColourId,
                            s ? juce::Colour (0xff14100a) : boombap::ui::text());
            repaint();
        }
        bool isSelected() const { return selectedFlag; }
        juce::TextButton& muteButton() { return mute; }
        juce::TextButton& soloButton() { return solo; }
        ConsoleFader& faderControl() { return fader; }

        void mouseDown (const juce::MouseEvent&) override { select(); }

        void paint (juce::Graphics& g) override
        {
            using namespace boombap::ui;
            auto b = getLocalBounds().toFloat();
            g.setColour (selectedFlag ? panelAlt() : panel());
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (selectedFlag ? accent() : border().withAlpha (0.5f));
            g.drawRoundedRectangle (b, 8.0f, selectedFlag ? 1.8f : 1.0f);
            // the name box on top of the strip trace (item 95): a lit
            // box when this channel is the selected one
            auto nameBox = juce::Rectangle<float> (4.0f, 4.0f, b.getWidth() - 8.0f, 24.0f);
            g.setColour (selectedFlag ? accent() : panelAlt().darker (0.25f));
            g.fillRoundedRectangle (nameBox, 5.0f);
            g.setColour (selectedFlag ? accentHi() : border().withAlpha (0.6f));
            g.drawRoundedRectangle (nameBox, 5.0f, selectedFlag ? 1.6f : 1.0f);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            name.setBounds (4, 4, b.getWidth() - 8, 24);
            fader.setBounds (12, 34, b.getWidth() - 20, juce::jmax (70, b.getHeight() - 34 - 92));
            pan.setBounds (b.getWidth() / 2 - 17, b.getHeight() - 88, 34, 34);
            mute.setBounds (8, b.getHeight() - 44, 34, 26);
            solo.setBounds (50, b.getHeight() - 44, 34, 26);
        }

    private:
        void select()
        {
            if (owner.callbacks.onSelectChannel) owner.callbacks.onSelectChannel (channel);
        }

        MixingBoard& owner;
        BoomBapEngine& engine;
        int channel;
        juce::Label name;
        ConsoleFader fader;
        juce::Slider pan;
        juce::TextButton mute, solo;
        bool selectedFlag = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Strip)
    };

    void timerCallback() override
    {
        if (engine.getNumTracks() != lastNumTracks || engine.getNumAudioTracks() != lastNumAudio)
        {
            rebuild();
            setSelectedChannel (selected);
            return;
        }
        for (auto& s : strips)
            s->setMeter (engine.peekChannelPeak (s->channelId(), false),
                         engine.peekChannelPeak (s->channelId(), true));
    }

    BoomBapEngine& engine;
    Callbacks callbacks;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Strip>> strips;
    std::vector<int> channels;
    int selected = 0;
    int lastNumTracks = -1, lastNumAudio = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixingBoard)
};

// ----------------------------------------------------------------------------
// The Station itself: header (channel + presets + A/B + board flip),
// transport + undo/redo, the chain, the EQ, and the meter/fader rail.
// ----------------------------------------------------------------------------
class StationPanel : public juce::Component, private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void (int)> onSelectChannel;
        std::function<void()> onPlay, onStopAll, onRewind, onFastForward;
        // Round 2 (item 93): the full transport - TO START / TO END
        // flank the step skips; the clock format lives with the owner
        // so the Station clock and the main window's readout agree.
        std::function<void()> onToStart, onToEnd;
        std::function<int()> getClockFormat;
        std::function<void (int)> setClockFormat;
        // Round 2 (item 96d): the EDIT SAMPLE doorway beside the
        // channel drop-down - the owner opens the channel's editor
        // (pad bank / piano roll); channels without one disable it.
        std::function<void (int)> onEditChannel;
        std::function<bool (int)> channelHasEditor;
        std::function<void()> onUndo, onRedo;
        std::function<bool()> canUndo, canRedo;
        std::function<void()> onBeforeChange, onAfterChange;
    };

    StationPanel (BoomBapEngine& eng, Callbacks cb)
        : engine (eng), callbacks (std::move (cb)), eqDisplay (eng),
          board (eng, MixingBoard::Callbacks {
              [this] (int ch) { if (callbacks.onSelectChannel) callbacks.onSelectChannel (ch); },
              [this] { gestureBegin(); },
              [this] { gestureEnd(); } })
    {
        titleLabel.setText ("THE MASTER STATION", juce::dontSendNotification);
        titleLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        titleLabel.setFont (juce::Font (11.0f, juce::Font::bold));
        addAndMakeVisible (titleLabel);
        // Round 2 (item 93): the channel name IS a drop-down of every
        // active track/channel - picking one binds the strip to it
        // without leaving the Station.
        boombap::ui::markSelectorBox (channelBox);
        channelBox.setTextWhenNothingSelected ("—");
        channelBox.onChange = [this]
        {
            if (refreshing) return;
            const int id = channelBox.getSelectedId();
            if (id > 0 && callbacks.onSelectChannel) callbacks.onSelectChannel (id - 1);
        };
        addAndMakeVisible (channelBox);
        // Round 2 (item 96d): EDIT SAMPLE - the doorway to the
        // channel's own editor (the pad/sample editor for sample
        // lanes, the piano roll for instruments). Disabled where the
        // channel has no editor (pure audio lanes, buses, master).
        editButton.setButtonText ("EDIT SAMPLE");
        boombap::ui::applyActionTrace (editButton);
        editButton.onClick = [this] { if (callbacks.onEditChannel) callbacks.onEditChannel (channel); };
        addAndMakeVisible (editButton);
        refreshChannelBox();

        abButton.setButtonText ("A/B");
        abButton.onClick = [this] { toggleAB(); };
        addAndMakeVisible (abButton);
        presetSaveButton.setButtonText ("Save preset");
        presetSaveButton.onClick = [this] { savePreset(); };
        addAndMakeVisible (presetSaveButton);
        presetLoadButton.setButtonText ("Load preset");
        presetLoadButton.onClick = [this] { loadPresetMenu(); };
        addAndMakeVisible (presetLoadButton);
        boardButton.setButtonText ("FULL BOARD");
        boardButton.onClick = [this] { setBoardMode (! boardMode); };
        addAndMakeVisible (boardButton);

        // the transport, inside the Station (item 19; item 93's full
        // spec in Round 2): TO START, REW (step; double-press = bar),
        // PLAY, STOP, FF (step; double-press = bar), TO END - the
        // double-press reading lives in the owner's TransportNav.
        toStartButton.setButtonText ("|<< START");
        toStartButton.onClick = [this] { if (callbacks.onToStart) callbacks.onToStart(); };
        rewButton.setButtonText ("REW");
        rewButton.onClick = [this] { if (callbacks.onRewind) callbacks.onRewind(); };
        playButton.setButtonText ("PLAY");
        playButton.setColour (juce::TextButton::buttonColourId, boombap::ui::accent());
        playButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff14100a));
        playButton.onClick = [this] { if (callbacks.onPlay) callbacks.onPlay(); };
        stopButton.setButtonText ("STOP");
        stopButton.onClick = [this] { if (callbacks.onStopAll) callbacks.onStopAll(); };
        ffButton.setButtonText ("FF");
        ffButton.onClick = [this] { if (callbacks.onFastForward) callbacks.onFastForward(); };
        toEndButton.setButtonText ("END >>|");
        toEndButton.onClick = [this] { if (callbacks.onToEnd) callbacks.onToEnd(); };
        for (auto* b : { &toStartButton, &rewButton, &playButton, &stopButton, &ffButton, &toEndButton })
            addAndMakeVisible (b);

        // Round 2 (item 93): the BIG time clock - click cycles
        // time / samples / bars.beats.ticks; the choice is the owner's
        // (persisted), so this clock and the main window's agree.
        timeClock.onCycleFormat = [this]
        {
            const int next = (currentClockFormat() + 1) % 3;
            if (callbacks.setClockFormat) callbacks.setClockFormat (next);
            else localClockFormat = next;
        };
        addAndMakeVisible (timeClock);

        undoButton.setButtonText ("UNDO");
        undoButton.onClick = [this] { if (callbacks.onUndo) callbacks.onUndo(); };
        redoButton.setButtonText ("REDO");
        redoButton.onClick = [this] { if (callbacks.onRedo) callbacks.onRedo(); };
        addAndMakeVisible (undoButton);
        addAndMakeVisible (redoButton);
        grLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        grLabel.setFont (juce::Font (11.5f));
        grLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (grLabel);

        // the chain (item 65's order)
        setupStageToggle (preampToggle, "PREAMP", [this] (bool on) { cur.preampOn = on; pushStrip(); });
        setupKnob (preampKnob, "Drive", -12.0, 24.0, 0.5, 0.0, " dB",
                   [this] (double v) { cur.preampDb = (float) v; pushStrip(); });
        setupStageToggle (hpfToggle, "HIGH-PASS", [this] (bool on) { cur.hpfOn = on; pushStrip(); });
        setupKnob (hpfKnob, "Freq", 10.0, 400.0, 1.0, 80.0, " Hz",
                   [this] (double v) { cur.hpfHz = (float) v; pushStrip(); });
        hpfKnob.slider.setSkewFactorFromMidPoint (80.0);
        setupStageToggle (deessToggle, "DE-ESSER", [this] (bool on) { cur.deessOn = on; pushStrip(); });
        setupKnob (deessKnob, "Amount", 0.0, 1.0, 0.01, 0.4, "",
                   [this] (double v) { cur.deessAmount = (float) v; pushStrip(); });
        setupStageToggle (compToggle, "COMPRESSOR", [this] (bool on) { cur.compOn = on; pushStrip(); });
        setupKnob (compThrKnob, "Thresh", -48.0, 0.0, 0.5, -18.0, " dB",
                   [this] (double v) { cur.compThresholdDb = (float) v; pushStrip(); });
        setupKnob (compRatioKnob, "Ratio", 1.0, 20.0, 0.1, 3.0, ":1",
                   [this] (double v) { cur.compRatio = (float) v; pushStrip(); });
        setupKnob (compAtkKnob, "Attack", 0.1, 300.0, 0.1, 10.0, " ms",
                   [this] (double v) { cur.compAttackMs = (float) v; pushStrip(); });
        compAtkKnob.slider.setSkewFactorFromMidPoint (10.0);
        setupKnob (compRelKnob, "Release", 10.0, 1000.0, 1.0, 120.0, " ms",
                   [this] (double v) { cur.compReleaseMs = (float) v; pushStrip(); });
        compRelKnob.slider.setSkewFactorFromMidPoint (120.0);
        addAndMakeVisible (compGr);
        setupStageToggle (satToggle, "SATURATION", [this] (bool on) { cur.satOn = on; pushStrip(); });
        setupKnob (satKnob, "Drive", 0.0, 1.0, 0.01, 0.3, "",
                   [this] (double v) { cur.satDrive = (float) v; pushStrip(); });
        setupStageToggle (limiterToggle, "LIMITER", [this] (bool on) { cur.limiterOn = on; pushStrip(); });
        setupKnob (limiterKnob, "Ceiling", -12.0, 0.0, 0.1, -1.0, " dB",
                   [this] (double v) { cur.limiterCeilingDb = (float) v; pushStrip(); });
        limiterGrLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        limiterGrLabel.setFont (juce::Font (10.5f));
        limiterGrLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (limiterGrLabel);

        // the master bus face: Glue + the master limiter (its own
        // mastering LOOK is Pass 6 - the note on the panel says so)
        setupStageToggle (glueToggle, "GLUE (BUS COMP)", [this] (bool on) { engine.setGlueEnabled (on); });
        setupKnob (glueThrKnob, "Thresh", -30.0, 0.0, 0.5, -12.0, " dB",
                   [this] (double v) { engine.setGlueThresholdDb ((float) v); });
        addAndMakeVisible (glueGr);
        setupStageToggle (masterLimToggle, "MASTER LIMITER", [this] (bool on) { engine.setMasterLimiterOn (on); });
        setupKnob (masterLimKnob, "Ceiling", -12.0, 0.0, 0.1, -1.0, " dB",
                   [this] (double v) { engine.setMasterLimiterCeilingDb ((float) v); });
        masterNote.setText ("The master bus: Glue plus the final limiter. "
                            "The mastering channel's own look arrives in Pass 6.",
                            juce::dontSendNotification);
        masterNote.setColour (juce::Label::textColourId, boombap::ui::muted());
        masterNote.setFont (juce::Font (12.0f));
        addAndMakeVisible (masterNote);

        // the EQ + its band editor bar
        eqDisplay.onStripEdited = [this] (const StripSettings& s)
        {
            cur = s;
            engine.setStripSettings (channel, cur);
            updateBandBar();
        };
        eqDisplay.onGestureBegin = [this] { gestureBegin(); };
        eqDisplay.onGestureEnd = [this] { gestureEnd(); };
        eqDisplay.onBandSelected = [this] (int) { updateBandBar(); };
        addAndMakeVisible (eqDisplay);

        bandLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        bandLabel.setFont (juce::Font (12.0f));
        addAndMakeVisible (bandLabel);
        const char* typeNames[5] = { "HP", "LS", "PK", "HS", "LP" };
        const int typeIds[5] = { GraphicEq::HighPass, GraphicEq::LowShelf, GraphicEq::Peak,
                                 GraphicEq::HighShelf, GraphicEq::LowPass };
        for (int i = 0; i < 5; ++i)
        {
            bandTypeButtons[i].setButtonText (typeNames[i]);
            const int typeId = typeIds[i];
            bandTypeButtons[i].onClick = [this, typeId]
            {
                const int sel = eqDisplay.getSelectedBand();
                if (sel < 0 || sel >= cur.eqNumBands) return;
                gestureBegin();
                cur.eq[(size_t) sel].type = typeId;
                pushStrip();
                updateBandBar();
                gestureEnd();
            };
            addAndMakeVisible (bandTypeButtons[i]);
        }
        bandOnToggle.setButtonText ("ON");
        bandOnToggle.setClickingTogglesState (true);
        bandOnToggle.onClick = [this]
        {
            const int sel = eqDisplay.getSelectedBand();
            if (sel < 0 || sel >= cur.eqNumBands || refreshing) return;
            gestureBegin();
            cur.eq[(size_t) sel].on = bandOnToggle.getToggleState();
            pushStrip();
            gestureEnd();
        };
        addAndMakeVisible (bandOnToggle);
        bandAddButton.setButtonText ("+ BAND");
        bandAddButton.onClick = [this]
        {
            if (cur.eqNumBands >= StripSettings::kEqBands) return;
            gestureBegin();
            const int idx = cur.eqNumBands++;
            cur.eq[(size_t) idx] = EqBandSettings { true, GraphicEq::Peak, 1000.0f, 0.0f, 1.0f };
            pushStrip();
            eqDisplay.selectBand (idx);
            updateBandBar();
            gestureEnd();
        };
        addAndMakeVisible (bandAddButton);
        bandDelButton.setButtonText ("DELETE");
        bandDelButton.onClick = [this]
        {
            const int sel = eqDisplay.getSelectedBand();
            if (sel < 0 || sel >= cur.eqNumBands) return;
            gestureBegin();
            for (int i = sel; i < cur.eqNumBands - 1; ++i) cur.eq[(size_t) i] = cur.eq[(size_t) i + 1];
            --cur.eqNumBands;
            pushStrip();
            eqDisplay.selectBand (juce::jmax (0, juce::jmin (sel, cur.eqNumBands - 1)));
            updateBandBar();
            gestureEnd();
        };
        addAndMakeVisible (bandDelButton);

        // Round 2 (item 94): the EQ's master name-toggle + the
        // selected band's CORRESPONDING KNOBS. Pressing GRAPHIC EQ
        // bypasses the whole EQ honestly: the bands' on-states are
        // stored and every band goes off in the strip the DSP reads
        // (flat = bypassed); pressing again restores them. The knobs
        // (Gain / Freq / Q, the approved knob family) are colour-matched
        // to the selected band's node and stay two-way synced with the
        // nodes: drag a node and updateBandBar() re-reads the knobs;
        // turn a knob and it writes the same strip the nodes draw.
        setupStageToggle (eqToggle, "GRAPHIC EQ", [this] (bool on) { setEqBypassed (! on); });
        setupKnob (eqGainKnob, "Gain", -18.0, 18.0, 0.1, 0.0, " dB",
                   [this] (double v)
                   {
                       const int sel = eqDisplay.getSelectedBand();
                       if (sel < 0 || sel >= cur.eqNumBands) return;
                       cur.eq[(size_t) sel].gainDb = (float) v;
                       pushStrip();
                       updateBandBar();
                   });
        setupKnob (eqFreqKnob, "Freq", 20.0, 20000.0, 1.0, 1000.0, "",
                   [this] (double v)
                   {
                       const int sel = eqDisplay.getSelectedBand();
                       if (sel < 0 || sel >= cur.eqNumBands) return;
                       cur.eq[(size_t) sel].freqHz = (float) v;
                       pushStrip();
                       updateBandBar();
                   });
        eqFreqKnob.slider.setSkewFactorFromMidPoint (1000.0);
        eqFreqKnob.slider.textFromValueFunction = [] (double v) { return stationui::freqText ((float) v); };
        setupKnob (eqQKnob, "Q", 0.1, 18.0, 0.01, 1.0, "",
                   [this] (double v)
                   {
                       const int sel = eqDisplay.getSelectedBand();
                       if (sel < 0 || sel >= cur.eqNumBands) return;
                       cur.eq[(size_t) sel].q = (float) v;
                       pushStrip();
                       updateBandBar();
                   });

        // the rail: meters, fader, pan, mute/solo
        addAndMakeVisible (vuMeter);
        addAndMakeVisible (vuNeedle); // item 96c: the master's VU (visibility follows the channel)
        // Round 2 (items 95/96): the rail fader is the same approved
        // console long fader as the board's - dB scale left, segmented
        // LED ladder right, cap that cannot vanish.
        railFader.setRange (0.0, 1.2, 1.0);
        railFader.setMeterColumns (2);
        railFader.onDragStart = [this] { gestureBegin(); };
        railFader.onDragEnd = [this] { gestureEnd(); };
        railFader.onValueChange = [this] (double v)
        {
            if (! refreshing) engine.setChannelGain (channel, (float) v);
            updateFaderLabel();
        };
        addAndMakeVisible (railFader);
        faderLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        faderLabel.setFont (juce::Font (11.0f));
        faderLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (faderLabel);
        panKnob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        panKnob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 14);
        panKnob.slider.setRange (-1.0, 1.0, 0.01);
        panKnob.slider.setDoubleClickReturnValue (true, 0.0);
        panKnob.slider.onDragStart = [this] { gestureBegin(); };
        panKnob.slider.onDragEnd = [this] { gestureEnd(); };
        panKnob.slider.onValueChange = [this]
        {
            if (! refreshing) engine.setChannelPan (channel, (float) panKnob.slider.getValue());
        };
        panKnob.label.setText ("Pan", juce::dontSendNotification);
        panKnob.label.setJustificationType (juce::Justification::centred);
        panKnob.label.setColour (juce::Label::textColourId, boombap::ui::muted());
        panKnob.label.setFont (juce::Font (10.5f));
        addAndMakeVisible (panKnob.slider);
        addAndMakeVisible (panKnob.label);
        muteButton.setButtonText ("MUTE");
        muteButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::indianred);
        muteButton.onClick = [this]
        {
            if (refreshing) return;
            gestureBegin();
            engine.setChannelMute (channel, muteButton.getToggleState());
            gestureEnd();
        };
        addAndMakeVisible (muteButton);
        soloButton.setButtonText ("SOLO");
        soloButton.setClickingTogglesState (true);
        soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
        soloButton.onClick = [this]
        {
            if (refreshing) return;
            gestureBegin();
            engine.setChannelSolo (channel, soloButton.getToggleState());
            gestureEnd();
        };
        addAndMakeVisible (soloButton);

        addChildComponent (board); // item 59 discipline: visibility is set explicitly below
        board.setVisible (false);

        ensureFactoryPresets(); // item 96a: VOCAL / DRUM / SNARE ship with the app
        bindToChannel (0);
        startTimerHz (30);
    }

    ~StationPanel() override { stopTimer(); }

    void bindToChannel (int ch)
    {
        channel = juce::jlimit (0, BoomBapEngine::kMasterChannel, ch);
        abValid = false;
        abShowingStored = false;
        updateABButton();
        engine.setAnalysedChannel (channel);
        board.setSelectedChannel (channel);
        // Round 2 (item 93): the drop-down IS the channel name now -
        // the binding moves the selector, never the other way round.
        channelBox.setSelectedId (channel + 1, juce::dontSendNotification);
        updateEditButton();
        updateVisibility();
        refreshFromEngine();
        resized();
        repaint();
    }

    int boundChannel() const { return channel; }
    bool isBoardMode() const { return boardMode; }

    // Round 3 (item 88): after a load renames a track, the drop-down's
    // items and the board strips' names re-read the engine's content
    // names. (The timer only rebuilds the box when track COUNTS move;
    // a rename changes no count.)
    void refreshChannelNames()
    {
        refreshChannelBox();
        board.refreshValues();
        repaint();
    }

    // Round 2: public doors for the regression tests - the same
    // objects and paths the mouse drives, no test-only back doors.
    void loadPresetFile (const juce::File& f) { applyPresetFile (f); }
    MixingBoard& boardComponent() { return board; }
    EqDisplay& eqDisplayComponent() { return eqDisplay; }
    TimeClock& clockComponent() { return timeClock; }
    juce::ComboBox& channelDropdown() { return channelBox; }
    ConsoleFader& railFaderComponent() { return railFader; }
    int dropdownChannel() const { return channelBox.getSelectedId() - 1; }
    bool editButtonEnabled() const { return editButton.isEnabled(); }
    bool preampStageVisible() const { return preampToggle.isVisible(); }
    bool limiterStageVisible() const { return limiterToggle.isVisible(); }
    bool railMuteState() const { return muteButton.getToggleState(); }
    bool railSoloState() const { return soloButton.getToggleState(); }
    int currentClockFormat() const
    {
        return callbacks.getClockFormat ? callbacks.getClockFormat() : localClockFormat;
    }
    double eqBandKnobValue (int which) const
    {
        if (which == 0) return eqGainKnob.slider.getValue();
        if (which == 1) return eqFreqKnob.slider.getValue();
        return eqQKnob.slider.getValue();
    }
    juce::Slider& eqBandKnob (int which)
    {
        if (which == 0) return eqGainKnob.slider;
        if (which == 1) return eqFreqKnob.slider;
        return eqQKnob.slider;
    }

    void setBoardMode (bool b)
    {
        boardMode = b;
        boardButton.setButtonText (b ? "STATION" : "FULL BOARD");
        if (b) board.refreshValues();
        updateVisibility();
        resized();
        repaint();
    }

    void refreshFromEngine()
    {
        refreshing = true;
        if (channel != BoomBapEngine::kMasterChannel)
        {
            cur = engine.getStripSettings (channel);
            eqBypassed = false;   // a freshly read strip starts un-bypassed;
            eqStoredValid = false; // the stored band states belong to the old strip
            eqDisplay.setStrip (cur);
            preampToggle.setToggleState (cur.preampOn, juce::dontSendNotification);
            preampKnob.slider.setValue (cur.preampDb, juce::dontSendNotification);
            hpfToggle.setToggleState (cur.hpfOn, juce::dontSendNotification);
            hpfKnob.slider.setValue (cur.hpfHz, juce::dontSendNotification);
            deessToggle.setToggleState (cur.deessOn, juce::dontSendNotification);
            deessKnob.slider.setValue (cur.deessAmount, juce::dontSendNotification);
            compToggle.setToggleState (cur.compOn, juce::dontSendNotification);
            compThrKnob.slider.setValue (cur.compThresholdDb, juce::dontSendNotification);
            compRatioKnob.slider.setValue (cur.compRatio, juce::dontSendNotification);
            compAtkKnob.slider.setValue (cur.compAttackMs, juce::dontSendNotification);
            compRelKnob.slider.setValue (cur.compReleaseMs, juce::dontSendNotification);
            satToggle.setToggleState (cur.satOn, juce::dontSendNotification);
            satKnob.slider.setValue (cur.satDrive, juce::dontSendNotification);
            limiterToggle.setToggleState (cur.limiterOn, juce::dontSendNotification);
            limiterKnob.slider.setValue (cur.limiterCeilingDb, juce::dontSendNotification);
        }
        else
        {
            const auto s = engine.getSettings();
            glueToggle.setToggleState (s.glueOn, juce::dontSendNotification);
            glueThrKnob.slider.setValue (s.glueThresholdDb, juce::dontSendNotification);
            masterLimToggle.setToggleState (s.masterLimiterOn, juce::dontSendNotification);
            masterLimKnob.slider.setValue (s.masterLimiterCeilingDb, juce::dontSendNotification);
        }
        railFader.setValue (engine.getChannelGain (channel), false);
        railFader.setDefaultValue (defaultGainFor (channel));
        panKnob.slider.setValue (channel == BoomBapEngine::kMasterChannel ? 0.0 : engine.getChannelPan (channel),
                                 juce::dontSendNotification);
        muteButton.setToggleState (engine.getChannelMute (channel), juce::dontSendNotification);
        soloButton.setToggleState (engine.getChannelSolo (channel), juce::dontSendNotification);
        updateFaderLabel();
        updateBandBar();
        board.refreshValues();
        refreshing = false;
        repaint();
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
    };

    void gestureBegin()
    {
        ++gestureDepth;
        if (callbacks.onBeforeChange) callbacks.onBeforeChange();
    }
    void gestureEnd()
    {
        if (--gestureDepth <= 0)
        {
            gestureDepth = 0;
            if (callbacks.onAfterChange) callbacks.onAfterChange();
        }
    }

    void setupKnob (Knob& k, const juce::String& name, double min, double max, double step,
                    double def, const juce::String& suffix, std::function<void (double)> apply)
    {
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
        k.slider.setRange (min, max, step);
        k.slider.setValue (def, juce::dontSendNotification);
        k.slider.setDoubleClickReturnValue (true, def); // D's reset rule: knobs reset on double-click
        k.slider.setTextValueSuffix (suffix);
        k.slider.onDragStart = [this] { gestureBegin(); };
        k.slider.onDragEnd = [this] { gestureEnd(); };
        k.slider.onValueChange = [this, &k, apply]
        {
            if (! refreshing && apply) apply (k.slider.getValue());
        };
        addAndMakeVisible (k.slider);
        k.label.setText (name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, boombap::ui::muted());
        k.label.setFont (juce::Font (10.5f));
        addAndMakeVisible (k.label);
    }

    void setupStageToggle (juce::TextButton& b, const juce::String& text, std::function<void (bool)> apply)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, boombap::ui::accent());
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff14100a));
        b.onClick = [this, &b, apply]
        {
            if (refreshing) return;
            gestureBegin();
            if (apply) apply (b.getToggleState());
            gestureEnd();
        };
        addAndMakeVisible (b);
    }

    void pushStrip()
    {
        if (channel == BoomBapEngine::kMasterChannel) return;
        engine.setStripSettings (channel, cur);
        eqDisplay.setStrip (cur);
    }

    static double defaultGainFor (int ch)
    {
        switch (BoomBapEngine::channelKind (ch))
        {
            case BoomBapEngine::KindAudio: return 0.9;
            case BoomBapEngine::KindVocal: return 0.9;
            case BoomBapEngine::KindSynth: return 0.8;
            case BoomBapEngine::KindPiano: return 0.85;
            case BoomBapEngine::KindPadBus: return 1.0;
            case BoomBapEngine::KindMaster: return 1.0;
            default: return 0.8;
        }
    }

    void updateFaderLabel()
    {
        const float g = (float) railFader.getValue();
        faderLabel.setText (g <= 0.001f ? "-inf dB" : stationui::gainDbText (20.0f * std::log10 (g)),
                            juce::dontSendNotification);
    }

    // Round 2 (item 93): the drop-down lists every active channel in
    // board order; rebuilt when the track counts move.
    void refreshChannelBox()
    {
        channelBox.clear (juce::dontSendNotification);
        auto addCh = [this] (int ch)
        {
            channelBox.addItem (juce::String (engine.channelDisplayName (ch)).toUpperCase(),
                                ch + 1);
        };
        for (int t = 0; t < engine.getNumTracks(); ++t) addCh (t);
        addCh (BoomBapEngine::kSynthChannel);
        addCh (BoomBapEngine::kPianoChannel);
        addCh (BoomBapEngine::kPadBusChannel);
        for (int i = 0; i < engine.getNumAudioTracks(); ++i)
            addCh (BoomBapEngine::kAudioChannelBase + i);
        addCh (BoomBapEngine::kVocalChannel);
        channelBox.addItem ("MASTER BUS", BoomBapEngine::kMasterChannel + 1);
        channelBox.setSelectedId (channel + 1, juce::dontSendNotification);
        lastBoxTracks = engine.getNumTracks();
        lastBoxAudio = engine.getNumAudioTracks();
    }

    void updateEditButton()
    {
        editButton.setEnabled (callbacks.channelHasEditor && callbacks.channelHasEditor (channel));
    }

    // Round 2 (item 94): the GRAPHIC EQ name-toggle's bypass - real,
    // not cosmetic: bypassing stores the bands' on-states and turns
    // every band off in the strip the engine reads (a flat EQ IS a
    // bypassed EQ in the DSP); restoring brings the states back.
    void setEqBypassed (bool bypassed)
    {
        if (bypassed)
        {
            for (int i = 0; i < StripSettings::kEqBands; ++i)
                eqStoredOn[(size_t) i] = cur.eq[i].on;
            eqStoredValid = true;
            for (int i = 0; i < StripSettings::kEqBands; ++i)
                cur.eq[i].on = false;
            eqBypassed = true;
        }
        else
        {
            if (eqStoredValid)
                for (int i = 0; i < StripSettings::kEqBands; ++i)
                    cur.eq[i].on = eqStoredOn[(size_t) i];
            eqBypassed = false;
        }
        pushStrip();
        updateBandBar();
    }

    void updateVisibility()
    {
        const bool master = channel == BoomBapEngine::kMasterChannel;
        const bool chan = ! master && ! boardMode;
        // Round 2 (item 94): the FULL tool set on EVERY channel - the
        // preamp and limiter used to appear only on audio/vocal
        // channels (the DSP processed them everywhere all along; only
        // the face hid them). No per-channel subsets any more.
        const bool preamp = chan;
        const bool lim = chan;

        board.setVisible (boardMode);
        eqDisplay.setVisible (chan);
        bandLabel.setVisible (chan);
        for (auto& b : bandTypeButtons) b.setVisible (chan);
        bandOnToggle.setVisible (chan);
        bandAddButton.setVisible (chan);
        bandDelButton.setVisible (chan);
        eqToggle.setVisible (chan);
        for (auto* k : { &eqGainKnob, &eqFreqKnob, &eqQKnob })
        { k->slider.setVisible (chan); k->label.setVisible (chan); }
        // Round 2 (item 96b): the top GR readout belongs to the Station
        // strip view only - the board does not carry it.
        grLabel.setVisible (! boardMode);

        preampToggle.setVisible (preamp);
        preampKnob.slider.setVisible (preamp); preampKnob.label.setVisible (preamp);
        hpfToggle.setVisible (chan);
        hpfKnob.slider.setVisible (chan); hpfKnob.label.setVisible (chan);
        deessToggle.setVisible (chan);
        deessKnob.slider.setVisible (chan); deessKnob.label.setVisible (chan);
        compToggle.setVisible (chan);
        for (auto* k : { &compThrKnob, &compRatioKnob, &compAtkKnob, &compRelKnob })
        { k->slider.setVisible (chan); k->label.setVisible (chan); }
        compGr.setVisible (chan);
        satToggle.setVisible (chan);
        satKnob.slider.setVisible (chan); satKnob.label.setVisible (chan);
        limiterToggle.setVisible (lim);
        limiterKnob.slider.setVisible (lim); limiterKnob.label.setVisible (lim);
        limiterGrLabel.setVisible (lim);

        const bool masterFace = master && ! boardMode;
        glueToggle.setVisible (masterFace);
        glueThrKnob.slider.setVisible (masterFace); glueThrKnob.label.setVisible (masterFace);
        glueGr.setVisible (masterFace);
        masterLimToggle.setVisible (masterFace);
        masterLimKnob.slider.setVisible (masterFace); masterLimKnob.label.setVisible (masterFace);
        masterNote.setVisible (masterFace);

        const bool rail = ! boardMode;
        vuMeter.setVisible (rail);
        vuNeedle.setVisible (rail && master); // item 96c: the VU is the master's
        railFader.setVisible (rail);
        faderLabel.setVisible (rail);
        const bool hasPan = rail && ! master;
        panKnob.slider.setVisible (hasPan); panKnob.label.setVisible (hasPan);
        muteButton.setVisible (rail && ! master);
        soloButton.setVisible (rail && ! master && channel != BoomBapEngine::kPadBusChannel);
        abButton.setVisible (! master);
        presetSaveButton.setVisible (! master);
        presetLoadButton.setVisible (! master);
    }

    void updateBandBar()
    {
        if (channel == BoomBapEngine::kMasterChannel) return;
        // Round 2 (item 94): the EQ master toggle + display dim follow
        // the bypass state; a band switched on by hand un-bypasses.
        {
            bool anyOn = false;
            for (int i = 0; i < cur.eqNumBands; ++i) anyOn = anyOn || cur.eq[(size_t) i].on;
            if (anyOn && eqBypassed) { eqBypassed = false; eqStoredValid = false; }
            eqToggle.setToggleState (! eqBypassed, juce::dontSendNotification);
            eqDisplay.setAlpha (eqBypassed ? 0.45f : 1.0f);
        }
        const int sel = eqDisplay.getSelectedBand();
        if (sel < 0 || sel >= cur.eqNumBands)
        {
            bandLabel.setText ("No EQ bands - double-click the display to add one", juce::dontSendNotification);
            bandOnToggle.setToggleState (false, juce::dontSendNotification);
            return;
        }
        const auto& b = cur.eq[(size_t) sel];
        // Round 2 (item 94): the band knobs mirror the selected band -
        // values, enabled state (a filter band has no gain to turn) and
        // the colour of its node on the display.
        {
            juce::Colour col = (b.type == GraphicEq::HighPass || b.type == GraphicEq::LowPass)
                                   ? juce::Colour (0xff7fb2d9)
                                   : (b.type == GraphicEq::Peak ? boombap::ui::accent()
                                                                 : boombap::ui::accentHi());
            if (! b.on) col = col.withAlpha (0.45f);
            for (auto* k : { &eqGainKnob, &eqFreqKnob, &eqQKnob })
                boombap::ui::setKnobColour (k->slider, col);
            eqGainKnob.slider.setValue (b.gainDb, juce::dontSendNotification);
            eqFreqKnob.slider.setValue (b.freqHz, juce::dontSendNotification);
            eqQKnob.slider.setValue (b.q, juce::dontSendNotification);
            const bool isFilter = b.type == GraphicEq::HighPass || b.type == GraphicEq::LowPass;
            eqGainKnob.slider.setEnabled (! isFilter);
            eqGainKnob.label.setEnabled (! isFilter);
        }
        static const char* typeNames[5] = { "Peak", "Low shelf", "High shelf", "High-pass", "Low-pass" };
        juce::String text = "Band " + juce::String (sel + 1) + " - " + typeNames[juce::jlimit (0, 4, b.type)]
                            + " - " + stationui::freqText (b.freqHz);
        if (b.type != GraphicEq::HighPass && b.type != GraphicEq::LowPass)
            text += " - " + stationui::gainDbText (b.gainDb);
        text += " - Q " + juce::String (b.q, 2) + (b.on ? "" : " - OFF");
        bandLabel.setText (text, juce::dontSendNotification);
        bandOnToggle.setToggleState (b.on, juce::dontSendNotification);
        static const int typeIds[5] = { GraphicEq::HighPass, GraphicEq::LowShelf, GraphicEq::Peak,
                                        GraphicEq::HighShelf, GraphicEq::LowPass };
        for (int i = 0; i < 5; ++i)
        {
            const int typeId = typeIds[i];
            const bool active = b.type == typeId;
            bandTypeButtons[i].setColour (juce::TextButton::buttonColourId,
                                          active ? boombap::ui::accent() : boombap::ui::panelAlt());
            bandTypeButtons[i].setColour (juce::TextButton::textColourOffId,
                                          active ? juce::Colour (0xff14100a) : boombap::ui::text());
        }
    }

    // ---- A/B compare: one stored snapshot; flipping swaps it with the
    // live strip. A compare is not an edit, so it stays out of the undo
    // history (the resulting state still shows as a change, honestly). ----
    void toggleAB()
    {
        if (channel == BoomBapEngine::kMasterChannel) return;
        if (! abValid)
        {
            abStored = cur;
            abValid = true;
            abShowingStored = false; // the live side is now "B" - tweak freely
        }
        else
        {
            const StripSettings was = cur;
            cur = abStored;
            abStored = was;
            abShowingStored = ! abShowingStored;
            engine.setStripSettings (channel, cur);
        }
        updateABButton();
        refreshFromEngine();
    }

    void updateABButton()
    {
        abButton.setButtonText (! abValid ? "A/B" : (abShowingStored ? "A: stored" : "B: live"));
    }

    // ---- strip presets (Documents\BoomBap\Presets, item 62's home) ----
    void savePreset()
    {
        if (channel == BoomBapEngine::kMasterChannel) return;
        auto w = std::make_shared<juce::AlertWindow> ("Save strip preset",
                                                      "Name this strip setting - it lands in Documents\\BoomBap\\Presets.",
                                                      juce::AlertWindow::NoIcon);
        w->addTextEditor ("name", juce::String (engine.channelDisplayName (channel)) + " strip");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w] (int result)
            {
                if (result != 1) return;
                const juce::String name = w->getTextEditorContents ("name").trim();
                if (name.isEmpty()) return;
                writePresetFile (name);
            }), true);
    }

    juce::String presetJsonString (const juce::String& name, const StripSettings& s, float pan)
    {
        juce::Array<juce::var> eqArr;
        for (int i = 0; i < s.eqNumBands; ++i)
        {
            juce::Array<juce::var> band;
            band.add (s.eq[i].on ? 1 : 0);
            band.add (s.eq[i].type);
            band.add ((double) s.eq[i].freqHz);
            band.add ((double) s.eq[i].gainDb);
            band.add ((double) s.eq[i].q);
            eqArr.add (juce::var (band));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("format", "boombap-strip-preset");
        o->setProperty ("name", name);
        o->setProperty ("pan", (double) pan);
        o->setProperty ("preampOn", s.preampOn); o->setProperty ("preampDb", (double) s.preampDb);
        o->setProperty ("hpfOn", s.hpfOn);       o->setProperty ("hpfHz", (double) s.hpfHz);
        o->setProperty ("deessOn", s.deessOn);   o->setProperty ("deessAmount", (double) s.deessAmount);
        o->setProperty ("compOn", s.compOn);
        o->setProperty ("compThresholdDb", (double) s.compThresholdDb);
        o->setProperty ("compRatio", (double) s.compRatio);
        o->setProperty ("compAttackMs", (double) s.compAttackMs);
        o->setProperty ("compReleaseMs", (double) s.compReleaseMs);
        o->setProperty ("satOn", s.satOn);       o->setProperty ("satDrive", (double) s.satDrive);
        o->setProperty ("limiterOn", s.limiterOn);
        o->setProperty ("limiterCeilingDb", (double) s.limiterCeilingDb);
        o->setProperty ("eq", juce::var (eqArr));
        return juce::JSON::toString (juce::var (o), false, 2);
    }

    void writePresetFile (const juce::String& name)
    {
        ensureDataHome();
        const auto file = presetsDir().getChildFile (juce::File::createLegalFileName (name) + ".json");
        file.replaceWithText (presetJsonString (name, cur, engine.getChannelPan (channel)));
    }

    // Round 2 (item 96a): the factory starter set, written into the
    // same Presets home the owner's own presets use (only when absent -
    // an owner's edited copy is never overwritten). Starting points in
    // the classic shapes: a vocal chain, a drum-bus chain, a snare
    // chain - EQ included.
    void ensureFactoryPresets()
    {
        ensureDataHome();
        auto ship = [this] (const char* name, const StripSettings& s)
        {
            const auto f = presetsDir().getChildFile (juce::File::createLegalFileName (name) + ".json");
            if (! f.existsAsFile())
                f.replaceWithText (presetJsonString (name, s, 0.0f));
        };
        {
            StripSettings s;
            s.preampOn = true;  s.preampDb = 4.0f;
            s.hpfOn = true;     s.hpfHz = 80.0f;
            s.deessOn = true;   s.deessAmount = 0.45f;
            s.compOn = true;    s.compThresholdDb = -18.0f; s.compRatio = 3.0f;
            s.compAttackMs = 10.0f; s.compReleaseMs = 120.0f;
            s.satOn = true;     s.satDrive = 0.25f;
            s.limiterOn = true; s.limiterCeilingDb = -1.0f;
            s.eqNumBands = 3;
            s.eq[0] = { true, GraphicEq::Peak,      300.0f,   2.0f, 1.0f };
            s.eq[1] = { true, GraphicEq::Peak,      3000.0f,  1.5f, 1.2f };
            s.eq[2] = { true, GraphicEq::HighShelf, 10000.0f, 2.0f, 0.7f };
            ship ("Factory Vocal", s);
        }
        {
            StripSettings s;
            s.preampOn = true;  s.preampDb = 3.0f;
            s.hpfOn = true;     s.hpfHz = 35.0f;
            s.deessOn = false;  s.deessAmount = 0.3f;
            s.compOn = true;    s.compThresholdDb = -14.0f; s.compRatio = 4.0f;
            s.compAttackMs = 3.0f;  s.compReleaseMs = 90.0f;
            s.satOn = true;     s.satDrive = 0.4f;
            s.limiterOn = true; s.limiterCeilingDb = -0.5f;
            s.eqNumBands = 3;
            s.eq[0] = { true, GraphicEq::Peak, 60.0f,   3.0f,  1.0f };
            s.eq[1] = { true, GraphicEq::Peak, 400.0f, -2.0f,  1.2f };
            s.eq[2] = { true, GraphicEq::Peak, 5000.0f, 2.0f,  1.4f };
            ship ("Factory Drum", s);
        }
        {
            StripSettings s;
            s.preampOn = true;  s.preampDb = 2.0f;
            s.hpfOn = true;     s.hpfHz = 100.0f;
            s.deessOn = false;  s.deessAmount = 0.3f;
            s.compOn = true;    s.compThresholdDb = -16.0f; s.compRatio = 4.0f;
            s.compAttackMs = 1.0f;  s.compReleaseMs = 80.0f;
            s.satOn = true;     s.satDrive = 0.3f;
            s.limiterOn = true; s.limiterCeilingDb = -1.0f;
            s.eqNumBands = 3;
            s.eq[0] = { true, GraphicEq::Peak,      200.0f,   3.0f, 1.0f };
            s.eq[1] = { true, GraphicEq::Peak,      5000.0f,  3.0f, 1.2f };
            s.eq[2] = { true, GraphicEq::HighShelf, 12000.0f, 1.5f, 0.7f };
            ship ("Factory Snare", s);
        }
    }

    void loadPresetMenu()
    {
        if (channel == BoomBapEngine::kMasterChannel) return;
        ensureDataHome();
        const auto files = presetsDir().findChildFiles (juce::File::findFiles, false, "*.json");
        juce::PopupMenu menu;
        if (files.isEmpty())
            menu.addItem (1, "No presets saved yet", false);
        else
            for (int i = 0; i < files.size(); ++i)
                menu.addItem (100 + i, files[i].getFileNameWithoutExtension());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetLoadButton),
            [this, files] (int result)
            {
                if (result < 100 || result - 100 >= files.size()) return;
                applyPresetFile (files[result - 100]);
            });
    }

    void applyPresetFile (const juce::File& file)
    {
        juce::var root;
        if (! juce::JSON::parse (file.loadFileAsString(), root).wasOk() || ! root.isObject()) return;
        auto getF = [&root] (const char* key, float fb)
        {
            const auto v = root.getProperty (key, juce::var());
            return (v.isInt() || v.isInt64() || v.isDouble()) ? (float) (double) v : fb;
        };
        auto getB = [&root] (const char* key, bool fb)
        {
            const auto v = root.getProperty (key, juce::var());
            return v.isBool() ? (bool) v : fb;
        };
        StripSettings st = cur;
        st.preampOn = getB ("preampOn", st.preampOn);   st.preampDb = getF ("preampDb", st.preampDb);
        st.hpfOn = getB ("hpfOn", st.hpfOn);             st.hpfHz = getF ("hpfHz", st.hpfHz);
        st.deessOn = getB ("deessOn", st.deessOn);       st.deessAmount = getF ("deessAmount", st.deessAmount);
        st.compOn = getB ("compOn", st.compOn);
        st.compThresholdDb = getF ("compThresholdDb", st.compThresholdDb);
        st.compRatio = getF ("compRatio", st.compRatio);
        st.compAttackMs = getF ("compAttackMs", st.compAttackMs);
        st.compReleaseMs = getF ("compReleaseMs", st.compReleaseMs);
        st.satOn = getB ("satOn", st.satOn);             st.satDrive = getF ("satDrive", st.satDrive);
        st.limiterOn = getB ("limiterOn", st.limiterOn);
        st.limiterCeilingDb = getF ("limiterCeilingDb", st.limiterCeilingDb);
        if (auto* eqIn = root.getProperty ("eq", juce::var()).getArray())
        {
            st.eqNumBands = juce::jmin (StripSettings::kEqBands, eqIn->size());
            for (int i = 0; i < st.eqNumBands; ++i)
            {
                auto* band = (*eqIn)[i].getArray();
                if (band == nullptr || band->size() < 5) continue;
                st.eq[i].on     = (double) (*band)[0] > 0.5;
                st.eq[i].type   = (int) std::lround ((double) (*band)[1]);
                st.eq[i].freqHz = (float) (double) (*band)[2];
                st.eq[i].gainDb = (float) (double) (*band)[3];
                st.eq[i].q      = (float) (double) (*band)[4];
            }
        }
        const float pan = getF ("pan", engine.getChannelPan (channel));
        gestureBegin(); // a preset load is ONE undoable action
        cur = st;
        engine.setStripSettings (channel, cur);
        engine.setChannelPan (channel, pan);
        gestureEnd();
        refreshFromEngine();
    }

    // ---- layout + paint ----
    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());
        auto panelRect = [&] (juce::Rectangle<int> r, juce::Colour fill)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
            g.setColour (border().withAlpha (0.5f));
            g.drawRoundedRectangle (r.toFloat(), 10.0f, 1.0f);
        };
        if (! boardMode)
        {
            if (channel != BoomBapEngine::kMasterChannel)
            {
                // Round 2 (item 97): each tool sits in its own thin
                // outline with a subtle tint of its own (all inside
                // the grey/black family) - the chain reads as separate
                // machines in a rack, not one solid form.
                static const juce::Colour stageTints[6] = {
                    juce::Colour (0xff1a1d24), // preamp
                    juce::Colour (0xff181c22), // high-pass
                    juce::Colour (0xff1c1a21), // de-esser
                    juce::Colour (0xff1a2027), // compressor
                    juce::Colour (0xff201c17), // saturation (a warm hint)
                    juce::Colour (0xff18201c), // limiter (a green hint)
                };
                for (int i = 0; i < 6; ++i)
                    if (! stageRects[(size_t) i].isEmpty())
                        panelRect (stageRects[(size_t) i], stageTints[i]);
            }
            else
            {
                panelRect (glueRect, juce::Colour (0xff1a2027));
                panelRect (masterLimRect, juce::Colour (0xff18201c));
            }
            panelRect (railRect, panel());
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        titleLabel.setBounds (14, 8, 220, 16);
        channelBox.setBounds (14, 24, 300, 32);
        editButton.setBounds (322, 24, 118, 32);
        boardButton.setBounds (b.getWidth() - 128, 12, 116, 30);
        presetLoadButton.setBounds (b.getWidth() - 248, 12, 110, 30);
        presetSaveButton.setBounds (b.getWidth() - 368, 12, 110, 30);
        abButton.setBounds (b.getWidth() - 440, 12, 62, 30);

        toStartButton.setBounds (14, 56, 82, 30);
        rewButton.setBounds (102, 56, 64, 30);
        playButton.setBounds (172, 56, 84, 30);
        stopButton.setBounds (262, 56, 84, 30);
        ffButton.setBounds (352, 56, 64, 30);
        toEndButton.setBounds (422, 56, 82, 30);
        undoButton.setBounds (516, 56, 64, 30);
        redoButton.setBounds (586, 56, 64, 30);
        timeClock.setBounds (662, 54, 160, 34);
        grLabel.setBounds (830, 56, juce::jmax (40, b.getWidth() - 840), 30);

        const int contentY = 96;
        const int contentH = b.getHeight() - contentY - 10;
        if (boardMode)
        {
            board.setBounds (10, contentY, b.getWidth() - 20, contentH);
            return;
        }

        railRect = { b.getWidth() - 186, contentY, 176, contentH };
        const int mainW = railRect.getX() - 20;
        const int chainH = 178;
        const int bandH = 118; // two rows: band controls, then the band knobs (item 94)
        const int eqH = juce::jmax (140, contentH - chainH - bandH - 18);
        const bool master = channel == BoomBapEngine::kMasterChannel;
        if (! master)
        {
            eqDisplay.setBounds (10, contentY, mainW, eqH);
            const int bandY = contentY + eqH + 6;
            eqToggle.setBounds (14, bandY + 2, 104, 26);
            bandLabel.setBounds (126, bandY, 320, 30);
            int bx = 454;
            for (auto& btn : bandTypeButtons) { btn.setBounds (bx, bandY + 2, 40, 26); bx += 44; }
            bandOnToggle.setBounds (bx + 4, bandY + 2, 52, 26);
            bandAddButton.setBounds (bx + 62, bandY + 2, 74, 26);
            bandDelButton.setBounds (bx + 142, bandY + 2, 74, 26);
            // the band knobs row: name above, knob + value below
            const int knobY = bandY + 38;
            int kx = 14;
            for (auto* k : { &eqGainKnob, &eqFreqKnob, &eqQKnob })
            {
                k->label.setBounds (kx, knobY, 72, 12);
                k->slider.setBounds (kx, knobY + 13, 72, 66);
                kx += 84;
            }

            const int chainY = bandY + bandH + 6;
            // Round 2 (item 94): all six stages, always - the widths
            // simply share the row (the compressor keeps the remainder).
            const int stageW = mainW >= 800 ? 108 : 92;
            const int limW = mainW >= 800 ? 118 : 102;
            const int compW = juce::jmax (170, mainW - stageW * 4 - limW - 40);
            int x = 10, idx = 0;
            auto placeStage = [&] (bool show, int w, juce::TextButton& toggle,
                                   std::initializer_list<Knob*> knobs, juce::Component* extra)
            {
                stageRects[(size_t) idx] = show ? juce::Rectangle<int> (x, chainY, w, chainH)
                                               : juce::Rectangle<int>();
                if (show)
                {
                    toggle.setBounds (x + 8, chainY + 8, w - 16, 24);
                    const int kw = (w - 16) / juce::jmax (1, (int) knobs.size());
                    const int knobW = juce::jlimit (34, 78, kw - 2);
                    int kx = x + 8;
                    for (auto* k : knobs)
                    {
                        k->slider.setBounds (kx + (kw - knobW) / 2, chainY + 40, knobW, knobW);
                        k->label.setBounds (kx, chainY + 44 + knobW, kw, 14);
                        kx += kw;
                    }
                    if (extra != nullptr) extra->setBounds (x + 10, chainY + 150, w - 20, 14);
                    x += w + 8;
                }
                ++idx;
            };
            placeStage (true, stageW, preampToggle, { &preampKnob }, nullptr);
            placeStage (true, stageW, hpfToggle, { &hpfKnob }, nullptr);
            placeStage (true, stageW, deessToggle, { &deessKnob }, nullptr);
            placeStage (true, compW, compToggle, { &compThrKnob, &compRatioKnob, &compAtkKnob, &compRelKnob }, &compGr);
            placeStage (true, stageW, satToggle, { &satKnob }, nullptr);
            placeStage (true, limW, limiterToggle, { &limiterKnob }, &limiterGrLabel);
        }
        else
        {
            const int rowY = contentY + 10;
            glueRect = { 10, rowY, 330, 190 };
            masterLimRect = { 352, rowY, 250, 190 };
            glueToggle.setBounds (20, rowY + 10, 310, 26);
            glueThrKnob.slider.setBounds (36, rowY + 48, 70, 70);
            glueThrKnob.label.setBounds (20, rowY + 130, 102, 14);
            glueGr.setBounds (150, rowY + 62, 170, 14);
            masterLimToggle.setBounds (362, rowY + 10, 230, 26);
            masterLimKnob.slider.setBounds (382, rowY + 48, 70, 70);
            masterLimKnob.label.setBounds (366, rowY + 130, 102, 14);
            masterNote.setBounds (620, rowY + 16, juce::jmax (120, mainW - 630), 120);
        }

        // rail (laid out to fit: meters, fader + value, pan knob + label,
        // then the MUTE/SOLO row pinned to the bottom - no overlaps).
        // Round 2 (item 96c): on the master, the VU needle sits above
        // the fader and the long meters give up its height.
        const int rx = railRect.getX();
        const int meterH = juce::jmax (110, contentH - 380);
        const bool masterRail = channel == BoomBapEngine::kMasterChannel;
        vuMeter.setBounds (rx + 14, contentY + 10, railRect.getWidth() - 28,
                           masterRail ? juce::jmax (60, meterH - 88) : meterH);
        vuNeedle.setBounds (rx + 14, contentY + 10 + meterH - 80, railRect.getWidth() - 28, 72);
        const int faderY = contentY + meterH + 18;
        railFader.setBounds (rx + 30, faderY, 116, 146);
        faderLabel.setBounds (rx + 8, faderY + 148, railRect.getWidth() - 16, 16);
        panKnob.slider.setBounds (rx + railRect.getWidth() / 2 - 28, faderY + 170, 56, 56);
        panKnob.label.setBounds (rx + 8, faderY + 240, railRect.getWidth() - 16, 14);
        muteButton.setBounds (rx + 12, contentY + contentH - 40, 72, 28);
        soloButton.setBounds (rx + 92, contentY + contentH - 40, 72, 28);
    }

    void timerCallback() override
    {
        // Round 2 (item 93): the clock + the drop-down's freshness
        timeClock.setPosition (engine.transportPosition(), engine.getSampleRate(),
                               (double) engine.getBpm(), currentClockFormat());
        if (engine.getNumTracks() != lastBoxTracks || engine.getNumAudioTracks() != lastBoxAudio)
            refreshChannelBox();

        vuMeter.setLevels (engine.peekChannelPeak (channel, false),
                           engine.peekChannelPeak (channel, true));
        railFader.setMeterLevels (engine.peekChannelPeak (channel, false),
                                  engine.peekChannelPeak (channel, true));
        if (channel == BoomBapEngine::kMasterChannel)
            vuNeedle.setLevel (std::max (engine.peekChannelPeak (channel, false),
                                         engine.peekChannelPeak (channel, true)));
        if (channel != BoomBapEngine::kMasterChannel)
        {
            compGr.setReduction (-engine.getChannelCompGrDb (channel));
            limiterGrLabel.setText ("GR " + juce::String (engine.getChannelLimGrDb (channel), 1) + " dB",
                                    juce::dontSendNotification);
            grLabel.setText ("COMP " + juce::String (engine.getChannelCompGrDb (channel), 1)
                                 + " dB - DE-ESS " + juce::String (engine.getChannelDeessGrDb (channel), 1)
                                 + " dB - LIM " + juce::String (engine.getChannelLimGrDb (channel), 1) + " dB",
                             juce::dontSendNotification);
        }
        else
        {
            glueGr.setReduction (-engine.getGlueGrDb());
            grLabel.setText ("GLUE GR " + juce::String (engine.getGlueGrDb(), 1)
                                 + " dB - MASTER LIM " + juce::String (engine.getMasterLimiterGrDb(), 1) + " dB",
                             juce::dontSendNotification);
        }
        undoButton.setEnabled (callbacks.canUndo && callbacks.canUndo());
        redoButton.setEnabled (callbacks.canRedo && callbacks.canRedo());
        if (! boardMode && channel != BoomBapEngine::kMasterChannel)
            eqDisplay.tick();
        const int rev = engine.stationRevisionCount();
        if (rev != lastRevision)
        {
            lastRevision = rev;
            if (gestureDepth == 0 && ! refreshing)
                refreshFromEngine();
        }
    }

    BoomBapEngine& engine;
    Callbacks callbacks;
    int channel = 0;
    StripSettings cur;
    bool boardMode = false;
    bool refreshing = false;
    int gestureDepth = 0;
    int lastRevision = -1;

    juce::Label titleLabel, grLabel, bandLabel, faderLabel, limiterGrLabel, masterNote;
    juce::ComboBox channelBox;
    juce::TextButton editButton;
    juce::TextButton abButton, presetSaveButton, presetLoadButton, boardButton;
    juce::TextButton toStartButton, rewButton, playButton, stopButton, ffButton, toEndButton;
    juce::TextButton undoButton, redoButton;
    TimeClock timeClock;
    int localClockFormat = 0;
    int lastBoxTracks = -1, lastBoxAudio = -1;

    juce::TextButton preampToggle, hpfToggle, deessToggle, compToggle, satToggle, limiterToggle;
    Knob preampKnob, hpfKnob, deessKnob, compThrKnob, compRatioKnob, compAtkKnob, compRelKnob,
         satKnob, limiterKnob;
    GrMeter compGr;
    juce::TextButton glueToggle, masterLimToggle;
    Knob glueThrKnob, masterLimKnob;
    GrMeter glueGr;

    EqDisplay eqDisplay;
    juce::TextButton bandTypeButtons[5], bandOnToggle, bandAddButton, bandDelButton;
    juce::TextButton eqToggle;
    Knob eqGainKnob, eqFreqKnob, eqQKnob;
    bool eqBypassed = false, eqStoredValid = false;
    bool eqStoredOn[StripSettings::kEqBands] = {};

    VuMeter vuMeter;
    VuNeedle vuNeedle;
    ConsoleFader railFader;
    Knob panKnob;
    juce::TextButton muteButton, soloButton;

    MixingBoard board;

    StripSettings abStored;
    bool abValid = false, abShowingStored = false;

    juce::Rectangle<int> stageRects[6];
    juce::Rectangle<int> glueRect, masterLimRect, railRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StationPanel)
};

// The Station lives in its own window (item 27's grammar: movable, docked
// right by default next to the main window); closing it just hides it.
class StationWindow : public juce::DocumentWindow
{
public:
    StationWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (900, 520, 4000, 3000);
    }

    // Round 1 (item 102): no taskbar slot of its own - the main window
    // is BoomBap's only Windows taskbar entry.
    int getDesktopWindowStyleFlags() const override
    {
        return juce::DocumentWindow::getDesktopWindowStyleFlags()
                 & ~juce::ComponentPeer::windowAppearsOnTaskbar;
    }

    void closeButtonPressed() override { setVisible (false); }
};
} // namespace boombap
