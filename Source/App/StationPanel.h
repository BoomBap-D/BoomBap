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
            name.setText (juce::String (BoomBapEngine::channelName (ch, engine.getNumTracks(),
                                                                   engine.getNumAudioTracks())),
                          juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, boombap::ui::text());
            name.setFont (juce::Font (11.5f, juce::Font::bold));
            name.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (name);

            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            fader.setRange (0.0, 1.2, 0.01);
            fader.setDoubleClickReturnValue (true, defaultGain());
            fader.onDragStart = [this]
            {
                select();
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
            };
            fader.onDragEnd = [this] { if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange(); };
            fader.onValueChange = [this] { engine.setChannelGain (channel, (float) fader.getValue()); };
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
                select();
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
                engine.setChannelMute (channel, mute.getToggleState());
                if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange();
            };
            addAndMakeVisible (mute);
            mute.setVisible (channel != BoomBapEngine::kMasterChannel);

            solo.setButtonText ("S");
            solo.setClickingTogglesState (true);
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
            solo.onClick = [this]
            {
                select();
                if (owner.callbacks.onBeforeChange) owner.callbacks.onBeforeChange();
                engine.setChannelSolo (channel, solo.getToggleState());
                if (owner.callbacks.onAfterChange) owner.callbacks.onAfterChange();
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
            fader.setValue (engine.getChannelGain (channel), juce::dontSendNotification);
            pan.setValue (channel == BoomBapEngine::kMasterChannel ? 0.0 : engine.getChannelPan (channel),
                          juce::dontSendNotification);
            mute.setToggleState (engine.getChannelMute (channel), juce::dontSendNotification);
            solo.setToggleState (engine.getChannelSolo (channel), juce::dontSendNotification);
        }

        void setMeter (float l, float r)
        {
            meterL = std::max (l, meterL * 0.85f);
            meterR = std::max (r, meterR * 0.85f);
            repaint();
        }
        void setSelected (bool s) { if (selectedFlag != s) { selectedFlag = s; repaint(); } }

        void mouseDown (const juce::MouseEvent&) override { select(); }

        void paint (juce::Graphics& g) override
        {
            using namespace boombap::ui;
            auto b = getLocalBounds().toFloat();
            g.setColour (selectedFlag ? panelAlt() : panel());
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (selectedFlag ? accent() : border().withAlpha (0.5f));
            g.drawRoundedRectangle (b, 8.0f, selectedFlag ? 1.8f : 1.0f);
            // the meter: one slim stereo pair down the strip's right edge
            const float mx = b.getRight() - 13.0f, mw = 4.0f;
            const float top = 34.0f, h = b.getHeight() - 150.0f;
            auto bar = [&] (float x, float level)
            {
                g.setColour (panelAlt().darker (0.4f));
                g.fillRect (x, top, mw, h);
                const float frac = std::sqrt (juce::jlimit (0.0f, 1.0f, level));
                if (frac > 0.01f)
                {
                    g.setColour (level > 0.95f ? juce::Colour (0xffe5484d) : accent());
                    g.fillRect (x, top + h * (1.0f - frac), mw, h * frac);
                }
            };
            bar (mx, meterL);
            bar (mx + 6.0f, meterR);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            name.setBounds (2, 4, b.getWidth() - 4, 26);
            fader.setBounds (8, 34, 44, juce::jmax (60, b.getHeight() - 150));
            pan.setBounds (b.getWidth() / 2 - 17, b.getHeight() - 108, 34, 34);
            mute.setBounds (8, b.getHeight() - 66, 34, 26);
            solo.setBounds (50, b.getHeight() - 66, 34, 26);
            juce::ignoreUnused (b);
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
        juce::Slider fader, pan;
        juce::TextButton mute, solo;
        float meterL = 0.0f, meterR = 0.0f;
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
        channelLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        channelLabel.setFont (juce::Font (24.0f, juce::Font::bold));
        addAndMakeVisible (channelLabel);

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

        // the transport, inside the Station (item 19)
        rewButton.setButtonText ("|<< REW");
        rewButton.onClick = [this] { if (callbacks.onRewind) callbacks.onRewind(); };
        playButton.setButtonText ("PLAY");
        playButton.setColour (juce::TextButton::buttonColourId, boombap::ui::accent());
        playButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff14100a));
        playButton.onClick = [this] { if (callbacks.onPlay) callbacks.onPlay(); };
        stopButton.setButtonText ("STOP");
        stopButton.onClick = [this] { if (callbacks.onStopAll) callbacks.onStopAll(); };
        ffButton.setButtonText ("FF >>");
        ffButton.onClick = [this] { if (callbacks.onFastForward) callbacks.onFastForward(); };
        for (auto* b : { &rewButton, &playButton, &stopButton, &ffButton }) addAndMakeVisible (b);

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

        // the rail: meters, fader, pan, mute/solo
        addAndMakeVisible (vuMeter);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 1.2, 0.01);
        fader.onDragStart = [this] { gestureBegin(); };
        fader.onDragEnd = [this] { gestureEnd(); };
        fader.onValueChange = [this]
        {
            if (! refreshing) engine.setChannelGain (channel, (float) fader.getValue());
            updateFaderLabel();
        };
        addAndMakeVisible (fader);
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
        const bool master = channel == BoomBapEngine::kMasterChannel;
        channelLabel.setText (master ? "MASTER BUS"
                                     : juce::String (BoomBapEngine::channelName (channel, engine.getNumTracks(),
                                                                                 engine.getNumAudioTracks())).toUpperCase(),
                              juce::dontSendNotification);
        updateVisibility();
        refreshFromEngine();
        resized();
        repaint();
    }

    int boundChannel() const { return channel; }
    bool isBoardMode() const { return boardMode; }

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
        fader.setValue (engine.getChannelGain (channel), juce::dontSendNotification);
        fader.setDoubleClickReturnValue (true, defaultGainFor (channel));
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
        const float g = engine.getChannelGain (channel);
        faderLabel.setText (g <= 0.001f ? "-inf dB" : stationui::gainDbText (20.0f * std::log10 (g)),
                            juce::dontSendNotification);
    }

    void updateVisibility()
    {
        const bool master = channel == BoomBapEngine::kMasterChannel;
        const bool chan = ! master && ! boardMode;
        const bool preamp = chan && BoomBapEngine::channelHasPreamp (channel);
        const bool lim = chan && BoomBapEngine::channelHasLimiter (channel);

        board.setVisible (boardMode);
        eqDisplay.setVisible (chan);
        bandLabel.setVisible (chan);
        for (auto& b : bandTypeButtons) b.setVisible (chan);
        bandOnToggle.setVisible (chan);
        bandAddButton.setVisible (chan);
        bandDelButton.setVisible (chan);

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
        fader.setVisible (rail);
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
        const int sel = eqDisplay.getSelectedBand();
        if (sel < 0 || sel >= cur.eqNumBands)
        {
            bandLabel.setText ("No EQ bands - double-click the display to add one", juce::dontSendNotification);
            bandOnToggle.setToggleState (false, juce::dontSendNotification);
            return;
        }
        const auto& b = cur.eq[(size_t) sel];
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
        w->addTextEditor ("name", juce::String (BoomBapEngine::channelName (channel, engine.getNumTracks(),
                                                                            engine.getNumAudioTracks())) + " strip");
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

    void writePresetFile (const juce::String& name)
    {
        juce::Array<juce::var> eqArr;
        for (int i = 0; i < cur.eqNumBands; ++i)
        {
            juce::Array<juce::var> band;
            band.add (cur.eq[i].on ? 1 : 0);
            band.add (cur.eq[i].type);
            band.add ((double) cur.eq[i].freqHz);
            band.add ((double) cur.eq[i].gainDb);
            band.add ((double) cur.eq[i].q);
            eqArr.add (juce::var (band));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("format", "boombap-strip-preset");
        o->setProperty ("name", name);
        o->setProperty ("pan", (double) engine.getChannelPan (channel));
        o->setProperty ("preampOn", cur.preampOn); o->setProperty ("preampDb", (double) cur.preampDb);
        o->setProperty ("hpfOn", cur.hpfOn);       o->setProperty ("hpfHz", (double) cur.hpfHz);
        o->setProperty ("deessOn", cur.deessOn);   o->setProperty ("deessAmount", (double) cur.deessAmount);
        o->setProperty ("compOn", cur.compOn);
        o->setProperty ("compThresholdDb", (double) cur.compThresholdDb);
        o->setProperty ("compRatio", (double) cur.compRatio);
        o->setProperty ("compAttackMs", (double) cur.compAttackMs);
        o->setProperty ("compReleaseMs", (double) cur.compReleaseMs);
        o->setProperty ("satOn", cur.satOn);       o->setProperty ("satDrive", (double) cur.satDrive);
        o->setProperty ("limiterOn", cur.limiterOn);
        o->setProperty ("limiterCeilingDb", (double) cur.limiterCeilingDb);
        o->setProperty ("eq", juce::var (eqArr));
        ensureDataHome();
        const auto file = presetsDir().getChildFile (juce::File::createLegalFileName (name) + ".json");
        file.replaceWithText (juce::JSON::toString (juce::var (o), false, 2));
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
        auto panelRect = [&] (juce::Rectangle<int> r)
        {
            g.setColour (panel());
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
            g.setColour (border().withAlpha (0.5f));
            g.drawRoundedRectangle (r.toFloat(), 10.0f, 1.0f);
        };
        if (! boardMode)
        {
            if (channel != BoomBapEngine::kMasterChannel)
                for (auto& r : stageRects) if (! r.isEmpty()) panelRect (r);
            else
            {
                panelRect (glueRect);
                panelRect (masterLimRect);
            }
            panelRect (railRect);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        titleLabel.setBounds (14, 8, 220, 16);
        channelLabel.setBounds (14, 22, 420, 32);
        boardButton.setBounds (b.getWidth() - 128, 12, 116, 30);
        presetLoadButton.setBounds (b.getWidth() - 248, 12, 110, 30);
        presetSaveButton.setBounds (b.getWidth() - 368, 12, 110, 30);
        abButton.setBounds (b.getWidth() - 440, 12, 62, 30);

        rewButton.setBounds (14, 56, 74, 30);
        playButton.setBounds (94, 56, 84, 30);
        stopButton.setBounds (184, 56, 84, 30);
        ffButton.setBounds (274, 56, 74, 30);
        undoButton.setBounds (366, 56, 70, 30);
        redoButton.setBounds (442, 56, 70, 30);
        grLabel.setBounds (530, 56, b.getWidth() - 544, 30);

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
        const int bandH = 38;
        const int eqH = juce::jmax (150, contentH - chainH - bandH - 18);
        const bool master = channel == BoomBapEngine::kMasterChannel;
        if (! master)
        {
            eqDisplay.setBounds (10, contentY, mainW, eqH);
            const int bandY = contentY + eqH + 6;
            bandLabel.setBounds (14, bandY, 430, 30);
            int bx = 452;
            for (auto& btn : bandTypeButtons) { btn.setBounds (bx, bandY + 2, 40, 26); bx += 44; }
            bandOnToggle.setBounds (bx + 4, bandY + 2, 52, 26);
            bandAddButton.setBounds (bx + 62, bandY + 2, 74, 26);
            bandDelButton.setBounds (bx + 142, bandY + 2, 74, 26);

            const int chainY = bandY + bandH + 6;
            const bool preamp = BoomBapEngine::channelHasPreamp (channel);
            const bool lim = BoomBapEngine::channelHasLimiter (channel);
            int fixed = 0;
            fixed += preamp ? 108 : 0;
            fixed += 108 + 108;             // HPF + DE-ESS
            fixed += 108;                   // SAT
            fixed += lim ? 118 : 0;
            const int gaps = (3 + (preamp ? 1 : 0) + (lim ? 1 : 0)) * 8;
            const int compW = juce::jmax (220, mainW - fixed - gaps);
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
                    int kx = x + 8;
                    for (auto* k : knobs)
                    {
                        k->slider.setBounds (kx + kw / 2 - 30, chainY + 42, 60, 60);
                        k->label.setBounds (kx, chainY + 116, kw, 14);
                        kx += kw;
                    }
                    if (extra != nullptr) extra->setBounds (x + 10, chainY + 140, w - 20, 14);
                    x += w + 8;
                }
                ++idx;
            };
            placeStage (preamp, 108, preampToggle, { &preampKnob }, nullptr);
            placeStage (true, 108, hpfToggle, { &hpfKnob }, nullptr);
            placeStage (true, 108, deessToggle, { &deessKnob }, nullptr);
            placeStage (true, compW, compToggle, { &compThrKnob, &compRatioKnob, &compAtkKnob, &compRelKnob }, &compGr);
            placeStage (true, 108, satToggle, { &satKnob }, nullptr);
            placeStage (lim, 118, limiterToggle, { &limiterKnob }, &limiterGrLabel);
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

        // rail (laid out to fit: meter, fader + value, pan knob + label,
        // then the MUTE/SOLO row pinned to the bottom - no overlaps)
        const int rx = railRect.getX();
        const int meterH = juce::jmax (110, contentH - 380);
        vuMeter.setBounds (rx + 14, contentY + 10, railRect.getWidth() - 28, meterH);
        const int faderY = contentY + meterH + 18;
        fader.setBounds (rx + railRect.getWidth() / 2 - 22, faderY, 44, 140);
        faderLabel.setBounds (rx + 8, faderY + 142, railRect.getWidth() - 16, 16);
        panKnob.slider.setBounds (rx + railRect.getWidth() / 2 - 28, faderY + 164, 56, 56);
        panKnob.label.setBounds (rx + 8, faderY + 238, railRect.getWidth() - 16, 14);
        muteButton.setBounds (rx + 12, contentY + contentH - 40, 72, 28);
        soloButton.setBounds (rx + 92, contentY + contentH - 40, 72, 28);
    }

    void timerCallback() override
    {
        vuMeter.setLevels (engine.peekChannelPeak (channel, false),
                           engine.peekChannelPeak (channel, true));
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

    juce::Label titleLabel, channelLabel, grLabel, bandLabel, faderLabel, limiterGrLabel, masterNote;
    juce::TextButton abButton, presetSaveButton, presetLoadButton, boardButton;
    juce::TextButton rewButton, playButton, stopButton, ffButton, undoButton, redoButton;

    juce::TextButton preampToggle, hpfToggle, deessToggle, compToggle, satToggle, limiterToggle;
    Knob preampKnob, hpfKnob, deessKnob, compThrKnob, compRatioKnob, compAtkKnob, compRelKnob,
         satKnob, limiterKnob;
    GrMeter compGr;
    juce::TextButton glueToggle, masterLimToggle;
    Knob glueThrKnob, masterLimKnob;
    GrMeter glueGr;

    EqDisplay eqDisplay;
    juce::TextButton bandTypeButtons[5], bandOnToggle, bandAddButton, bandDelButton;

    VuMeter vuMeter;
    juce::Slider fader;
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
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (900, 520, 4000, 3000);
    }

    void closeButtonPressed() override { setVisible (false); }
};
} // namespace boombap
