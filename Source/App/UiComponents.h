#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "BoomBapLookAndFeel.h" // for the boombap::ui colour helpers

// Small reusable building-block components for the main window:
// the step-grid cell, the per-row and glue meters, and the sample
// waveform strip. Moved out of MainComponent.h in the file split;
// behaviour unchanged.

// One cell of the step grid. Click: off -> normal -> accent -> off. Right-click clears.
class StepButton : public juce::Component
{
public:
    std::function<void (int)> onChange; // called with the new velocity (0, 90 or 127)

    // Fix Pass 3 (item 76): swipe (paint-drag) hooks. onSwipeStart hears
    // the value a press produced BEFORE onChange applies it, so the
    // owner can hold one undo gesture open for the whole drag;
    // onSwipeMove reports the pointer in the parent's coordinates during
    // the drag; onSwipeEnd closes the gesture on release.
    std::function<void (int)> onSwipeStart;
    std::function<void (juce::Point<float>)> onSwipeMove;
    std::function<void()> onSwipeEnd;

    void setVelocity (int v)  { if (velocity != v) { velocity = v; repaint(); } }
    int  getVelocity() const  { return velocity; }
    void setBeatShade (bool b) { beatShade = b; }
    void setPlaying (bool p)  { if (playing != p) { playing = p; repaint(); } }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    int velocity = 0;
    bool beatShade = false, playing = false;
};

// Slim horizontal peak meter for one track row.
// Round 6 (item 112a): EVERY meter carries a clip latch - a peak at
// full scale lights it red and it STAYS lit until clicked off.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        if (newLevel >= 0.99f && ! clipLatched) { clipLatched = true; repaint(); }
        if (std::abs (newLevel - level) > 0.002f) { level = newLevel; repaint(); }
    }

    bool clipIsLatched() const { return clipLatched; }
    void clearClipLatch() { if (clipLatched) { clipLatched = false; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (boombap::ui::border().withAlpha (0.45f));
        g.fillRoundedRectangle (b, 3.0f);
        const float frac = std::sqrt (std::clamp (level, 0.0f, 1.0f));
        if (frac > 0.01f)
        {
            g.setColour (level > 0.95f ? juce::Colour (0xffe5484d) : boombap::ui::accent());
            g.fillRoundedRectangle (b.withWidth (std::max (6.0f, b.getWidth() * frac)), 3.0f);
        }
        if (clipLatched)
        {
            g.setColour (juce::Colour (0xffe5484d));
            g.drawRoundedRectangle (b, 3.0f, 1.5f);
            g.fillRect (b.getRight() - 7.0f, b.getY() + 2.0f, 5.0f,
                        std::max (2.0f, b.getHeight() - 4.0f));
        }
    }

    void mouseDown (const juce::MouseEvent&) override { clearClipLatch(); }

private:
    float level = 0.0f;
    bool clipLatched = false;
};

// Round activity lamp for the pad set: glows while any pad is sounding
// (Pass 1 item 4b - the main window's pad-activity indicator).
class PadActivityLight : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        if (std::abs (newLevel - level) > 0.01f) { level = newLevel; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const float t = std::clamp (level, 0.0f, 1.0f);
        g.setColour (boombap::ui::accent().withAlpha (0.15f + 0.85f * t));
        g.fillEllipse (b);
        g.setColour (boombap::ui::border().withAlpha (0.7f));
        g.drawEllipse (b, 1.0f);
    }

private:
    float level = 0.0f;
};

// ============================================================================
// ROUND 2 (item 79 picks + the meter ruling): the shared control kit.
//
// Meters are SEGMENTED ("chopped up") LED blocks in both orientations -
// the house meter language. One painter serves the horizontal GR meter,
// the faders' LED ladders and any future meter, so the language cannot
// drift between views.
// ============================================================================
namespace boombap
{
namespace ui
{
inline void drawSegmentedMeter (juce::Graphics& g, juce::Rectangle<float> area,
                                float level01, bool vertical)
{
    const float level = std::clamp (level01, 0.0f, 1.0f);
    const float span = vertical ? area.getHeight() : area.getWidth();
    if (span < 8.0f) return;
    const int segs = std::max (4, std::min (28, (int) (span / 8.0f)));
    const float gap = 2.0f;
    const float segSpan = (span - gap * (float) (segs - 1)) / (float) segs;
    const int lit = (int) std::lround (level * (float) segs);
    for (int i = 0; i < segs; ++i)
    {
        // fraction along the meter: 0 at the quiet end, 1 at the hot end
        const float frac = (float) i / (float) std::max (1, segs - 1);
        juce::Colour col = frac > 0.86f ? juce::Colour (0xffe5484d)
                           : frac > 0.68f ? juce::Colour (0xfff5c542)
                                          : juce::Colour (0xffe8863a);
        const bool on = i < lit;
        g.setColour (on ? col : col.withAlpha (0.13f));
        if (vertical)
        {
            const float y = area.getBottom() - (float) (i + 1) * segSpan - (float) i * gap;
            g.fillRect (area.getX(), y, area.getWidth(), segSpan);
        }
        else
        {
            const float x = area.getX() + (float) i * (segSpan + gap);
            g.fillRect (x, area.getY(), segSpan, area.getHeight());
        }
    }
}
} // namespace ui
} // namespace boombap

// Gain-reduction meter for the Glue bus compressor (0 to 15 dB scale).
// Round 2 restyle: the segmented LED blocks of the house meter language
// replace the old smooth bar - same class, same API, new face (the
// horizontal meter under the Station's compressor is this class too).
class GrMeter : public juce::Component
{
public:
    void setReduction (float db)
    {
        // Item 112a's latch, in this meter's own terms: a compressor
        // "clips" at full-scale reduction - the needle pinned at the
        // end of its 15 dB scale latches the red outline until clicked.
        if (db >= 14.9f && ! latched) { latched = true; repaint(); }
        if (std::abs (db - reduction) > 0.05f) { reduction = db; repaint(); }
    }
    float getReduction() const { return reduction; }

    bool clipIsLatched() const { return latched; }
    void clearClipLatch() { if (latched) { latched = false; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (boombap::ui::border().withAlpha (0.45f));
        g.fillRoundedRectangle (b, 3.0f);
        const float frac = std::clamp (reduction / 15.0f, 0.0f, 1.0f);
        boombap::ui::drawSegmentedMeter (g, b.reduced (1.5f), frac, false);
        if (latched)
        {
            g.setColour (juce::Colour (0xffe5484d));
            g.drawRoundedRectangle (b, 3.0f, 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { clearClipLatch(); }

private:
    float reduction = 0.0f;
    bool latched = false;
};

// ============================================================================
// The approved console long fader (item 79 fader pick, items 95/96):
// vertical long throw, printed dB scale on the LEFT, a segmented LED
// meter riding on the RIGHT, coloured cap with a white centre line.
// A plain Component, NOT a juce::Slider: the old board fader was a
// vertical Slider drawn by a LookAndFeel that assumed horizontal
// geometry, so its thumb painted outside the component and vanished
// on grab (item 95's headline defect). This component owns its own
// geometry end to end - the cap is painted from the same value the
// drag math uses, so it cannot disappear. Value domain matches the
// engine's channel gains (0..1.2, 1.0 = unity). The throw taper is
// pos = norm^1.5, putting unity at ~76% of the travel like a console.
// ============================================================================
class ConsoleFader : public juce::Component
{
public:
    ConsoleFader() {}
    std::function<void()> onDragStart, onDragEnd;
    std::function<void (double)> onValueChange;

    void setRange (double minValue, double maxValue, double defaultValue)
    {
        minV = minValue; maxV = maxValue; defV = defaultValue;
        setValue (juce::jlimit (minV, maxV, value), false);
    }
    void setDefaultValue (double defaultValue) { defV = defaultValue; }
    double getDefaultValue() const { return defV; }

    void setValue (double newValue, bool notify)
    {
        const double v = juce::jlimit (minV, maxV, newValue);
        if (std::abs (v - value) < 1.0e-9) return;
        value = v;
        repaint();
        if (notify && onValueChange) onValueChange (value);
    }
    double getValue() const { return value; }

    void setScaleVisible (bool s) { scaleVisible = s; repaint(); }
    void setMeterColumns (int cols) { meterCols = juce::jlimit (0, 2, cols); repaint(); }

    // Peak feed for the LED ladder (called on the owner's timer; the
    // ladder holds and decays here so every view reads the same).
    // Round 6 (item 112e): the two sides are held SEPARATELY - a
    // stereo ladder paints L and R honestly instead of max twice.
    void setMeterLevels (float l, float r)
    {
        meterL = std::max (l, meterL * 0.88f);
        meterR = std::max (r, meterR * 0.88f);
        meterShown = std::max (meterL, meterR);
        if (clipEnabled && (l >= 0.99f || r >= 0.99f) && ! clipLatchedState)
            clipLatchedState = true;
        repaint();
    }
    float meterLevel() const { return meterShown; }
    float meterLevelL() const { return meterL; }
    float meterLevelR() const { return meterR; }

    // Round 6 (item 112a): the clip latch box above the ladder - red,
    // lit at full scale, stays until clicked off (the click is spent
    // on the latch; it never moves the fader).
    void setClipLatchEnabled (bool on) { clipEnabled = on; repaint(); }
    bool clipLatchEnabled() const { return clipEnabled; }
    bool clipLatched() const { return clipLatchedState; }
    void clearClipLatch() { if (clipLatchedState) { clipLatchedState = false; repaint(); } }
    juce::Rectangle<float> clipBoxBounds() const
    {
        if (! clipEnabled) return {};
        const auto geo = geometry();
        const float ledW = meterCols > 0 ? (float) meterCols * 7.0f + 3.0f : 10.0f;
        return { geo.ledX - 2.0f, 1.0f, ledW, 14.0f };
    }

    // The cap's centre in local coordinates (the drag tests aim here,
    // and the paint below uses the same numbers).
    juce::Point<float> capCentre() const
    {
        auto geo = geometry();
        return { geo.trackCx, geo.top + (1.0f - posFromValue (value)) * geo.travel };
    }
    float trackTop() const { return geometry().top; }
    float trackTravel() const { return geometry().travel; }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        const auto geo = geometry();
        const float top = geo.top, travel = geo.travel;

        // printed dB scale on the left (honest positions: the labels sit
        // at the travel position of the gain they name)
        if (scaleVisible)
        {
            g.setFont (8.5f);
            for (const auto& mark : scaleMarks())
            {
                const float y = top + (1.0f - posFromValue (mark.gain)) * travel;
                g.setColour (muted().withAlpha (0.85f));
                g.drawText (mark.text, 0, (int) (y - 6.0f), (int) geo.scaleW - 4, 11,
                            juce::Justification::right);
                g.setColour (border().withAlpha (0.8f));
                g.drawLine (geo.scaleW - 3.0f, y, geo.scaleW + 2.0f, y, 1.0f);
            }
        }

        // the slot
        g.setColour (juce::Colour (0xff0a0c0f));
        g.fillRoundedRectangle (geo.trackCx - 2.5f, top - 4.0f, 5.0f, travel + 8.0f, 2.5f);
        g.setColour (border().withAlpha (0.6f));
        g.drawRoundedRectangle (geo.trackCx - 2.5f, top - 4.0f, 5.0f, travel + 8.0f, 2.5f, 0.8f);

        // the segmented LED ladder on the right (item 112e: with two
        // columns, column 0 is L and column 1 is R - separately true)
        if (meterCols > 0)
        {
            for (int c = 0; c < meterCols; ++c)
            {
                const float lv = meterCols == 1 ? meterShown : (c == 0 ? meterL : meterR);
                const float frac = std::sqrt (std::clamp (lv, 0.0f, 1.0f));
                const float x = geo.ledX + (float) c * 7.0f;
                drawSegmentedMeter (g, { x, top - 4.0f, 5.0f, travel + 8.0f }, frac, true);
            }
        }

        // the clip latch box above the ladder (item 112a)
        if (clipEnabled)
        {
            const auto box = clipBoxBounds();
            g.setColour (clipLatchedState ? juce::Colour (0xffe5484d) : juce::Colour (0xff3a1518));
            g.fillRoundedRectangle (box, 2.0f);
            g.setColour (clipLatchedState ? juce::Colour (0xffff8a8d) : border().withAlpha (0.7f));
            g.drawRoundedRectangle (box, 2.0f, 1.0f);
        }

        // the cap: coloured body, white centre line (the pick)
        const float capW = std::min (34.0f, std::max (22.0f, geo.trackRight - geo.scaleW));
        const float capH = 13.0f;
        const float cy = top + (1.0f - posFromValue (value)) * travel;
        auto cap = juce::Rectangle<float> (geo.trackCx - capW * 0.5f, cy - capH * 0.5f, capW, capH);
        juce::ColourGradient grad (juce::Colour (0xff8a5a17), cap.getX(), cap.getY(),
                                   juce::Colour (0xff5e3d0e), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colour (0xff2a1c05));
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.fillRect (cap.getX() + 2.0f, cy - 1.0f, capW - 4.0f, 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Item 112a: a press on the clip box clears the latch and is
        // spent - it must never also grab the fader.
        if (clipEnabled && clipBoxBounds().contains (e.position))
        {
            clearClipLatch();
            return;
        }
        if (e.getNumberOfClicks() > 1)
        {
            setValue (defV, true); // item 6: double-click resets, faders too
            return;
        }
        dragging = true;
        dragStartY = e.position.y;
        dragStartPos = posFromValue (value);
        if (onDragStart) onDragStart();
        // a plain press on the track jumps the cap under the finger -
        // the grab D expects from a console fader
        if (std::abs (e.position.y - capCentre().y) > 8.0f)
        {
            const auto geo = geometry();
            const float p = 1.0f - juce::jlimit (0.0f, 1.0f, (e.position.y - geo.top) / geo.travel);
            setValue (valueFromPos (p), true);
            dragStartY = e.position.y;
            dragStartPos = posFromValue (value);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto geo = geometry();
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float p = juce::jlimit (0.0f, 1.0f,
                                      dragStartPos + (dragStartY - e.position.y) / geo.travel * fine);
        setValue (valueFromPos (p), true);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        if (onDragEnd) onDragEnd();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        setValue (valueFromPos (juce::jlimit (0.0f, 1.0f, posFromValue (value) + wheel.deltaY * 0.05f)), true);
    }

private:
    struct Geometry { float scaleW, trackCx, ledX, trackRight, top, travel; };
    Geometry geometry() const
    {
        Geometry geo;
        geo.scaleW = scaleVisible ? 26.0f : 0.0f;
        const float ledW = meterCols > 0 ? (float) meterCols * 7.0f + 3.0f : 0.0f;
        const float mid = geo.scaleW + ((float) getWidth() - geo.scaleW - ledW) * 0.5f;
        geo.trackCx = mid;
        geo.ledX = (float) getWidth() - ledW + 2.0f;
        geo.trackRight = (float) getWidth() - ledW;
        geo.top = clipEnabled ? 22.0f : 8.0f; // room for the clip box (item 112a)
        geo.travel = std::max (30.0f, (float) getHeight() - geo.top - 8.0f);
        return geo;
    }

    float posFromValue (double v) const
    {
        const double norm = (v - minV) / std::max (1.0e-9, maxV - minV);
        return (float) std::pow (std::clamp (norm, 0.0, 1.0), 1.5);
    }
    double valueFromPos (float p) const
    {
        return minV + (maxV - minV) * std::pow ((double) std::clamp (p, 0.0f, 1.0f), 2.0 / 3.0);
    }

    struct Mark { double gain; const char* text; };
    std::vector<Mark> scaleMarks() const
    {
        // the gain-domain scale for the 0..1.2 channel faders; other
        // ranges fall back to plain end labels
        if (std::abs (minV) < 1.0e-9 && std::abs (maxV - 1.2) < 1.0e-9)
            return { { 1.2, "+1.6" }, { 1.0, "0" }, { 0.5, "-6" }, { 0.25, "-12" },
                     { 0.125, "-18" }, { 0.0625, "-24" }, { 0.0, "-inf" } };
        return { { maxV, "" }, { minV, "" } };
    }

    double minV = 0.0, maxV = 1.2, defV = 1.0, value = 1.0;
    bool scaleVisible = true;
    int meterCols = 2;
    float meterShown = 0.0f;
    float meterL = 0.0f, meterR = 0.0f; // item 112e: the sides, held separately
    bool clipEnabled = false, clipLatchedState = false; // item 112a
    bool dragging = false;
    float dragStartY = 0.0f, dragStartPos = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleFader)
};

// ============================================================================
// ROUND 2 (item 93): transport navigation + the big clock.
//
// TransportNav turns REW / FF presses into step counts: one press = one
// step (a 16th), a second press inside the double-press window = one
// bar (16 steps), and the double consumes the pair so a third press
// starts fresh. The clock source is injectable so the window logic is
// testable without real time.
// ============================================================================
namespace boombap
{
struct TransportNav
{
    static constexpr long long kDoublePressMs = 350;
    static constexpr int kStepsPerBar = 16;

    std::function<long long()> nowMs = [] { return (long long) juce::Time::currentTimeMillis(); };

    int pressRewind()
    {
        const long long t = nowMs();
        const bool dbl = (t - lastRewMs) <= kDoublePressMs;
        lastRewMs = dbl ? -1000000LL : t;
        return dbl ? -kStepsPerBar : -1;
    }
    int pressForward()
    {
        const long long t = nowMs();
        const bool dbl = (t - lastFfMs) <= kDoublePressMs;
        lastFfMs = dbl ? -1000000LL : t;
        return dbl ? kStepsPerBar : 1;
    }
    void reset() { lastRewMs = lastFfMs = -1000000LL; }

    long long lastRewMs = -1000000LL, lastFfMs = -1000000LL;
};

// The clock's three faces (item 93): regular time, samples, bars.beats.ticks.
enum ClockFormat { ClockTime = 0, ClockSamples = 1, ClockBars = 2 };

inline juce::String groupedNumber (long long v)
{
    juce::String s = juce::String (v);
    juce::String out;
    int run = 0;
    for (int i = s.length() - 1; i >= 0; --i)
    {
        out = juce::String::charToString (s[i]) + out;
        if (++run == 3 && i > 0) { out = "," + out; run = 0; }
    }
    return out;
}

inline juce::String clockText (long long posSamples, double sampleRate, double bpm, int format)
{
    if (posSamples < 0) posSamples = 0;
    if (format == ClockSamples)
        return groupedNumber (posSamples);
    if (format == ClockBars)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double b = bpm > 0.0 ? bpm : 120.0;
        const double samplesPerTick = sr * 60.0 / (b * 96.0); // kPPQN = 96
        const long long totalTicks = (long long) ((double) posSamples / samplesPerTick);
        const long long steps = totalTicks / 24;              // kTicksPer16th = 24
        const long long bar = steps / 16 + 1;
        const long long beat = (steps % 16) / 4 + 1;
        const long long tick = totalTicks % 96;
        return juce::String (bar) + "." + juce::String (beat) + "."
               + juce::String ((int) tick).paddedLeft ('0', 2);
    }
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double seconds = (double) posSamples / sr;
    const long long mins = (long long) (seconds / 60.0);
    const long long secs = (long long) seconds % 60;
    const long long ms = (long long) ((seconds - (double) (long long) seconds) * 1000.0);
    return juce::String (mins) + ":" + juce::String ((int) secs).paddedLeft ('0', 2)
           + "." + juce::String ((int) ms).paddedLeft ('0', 3);
}
} // namespace boombap

// The big time clock (item 93): dark readout, monospaced digits, click
// cycles the format. The owner feeds it the position on its timer.
class TimeClock : public juce::Component
{
public:
    TimeClock() {}
    std::function<void()> onCycleFormat;

    void setPosition (long long posSamples, double sampleRate, double bpm, int format)
    {
        const juce::String t = boombap::clockText (posSamples, sampleRate, bpm, format);
        if (t != text) { text = t; repaint(); }
    }
    juce::String currentText() const { return text; }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0c0f));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (border().withAlpha (0.7f));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        g.setColour (accentHi());
        const float h = b.getHeight();
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(),
                               juce::jmin (23.0f, h * 0.58f), juce::Font::bold));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override { if (onCycleFormat) onCycleFormat(); }

private:
    juce::String text = "0:00.000";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeClock)
};

// Round 3 (item 100): the little travelling name tag that follows the
// mouse during a sample drag (waveform panel -> pad, pad -> pad). It
// lives on the desktop so it can cross between windows - the pad bank
// may be popped out while the waveform panel stays in the main window.
// Temporary-window type: it never takes focus and never appears in the
// taskbar (item 102's one-window rule is not disturbed by a drag).
class SampleDragGhost : public juce::Component
{
public:
    void setText (const juce::String& t) { text = t; repaint(); }

    void showAt (juce::Point<int> screenPos)
    {
        const int w = juce::jmax (96, (int) text.length() * 7 + 26);
        setBounds (screenPos.x + 14, screenPos.y + 12, w, 26);
        if (! isOnDesktop())
            addToDesktop (juce::ComponentPeer::windowIsTemporary);
        setVisible (true);
        toFront (false);
    }

    void moveTo (juce::Point<int> screenPos)
    {
        if (isOnDesktop()) setTopLeftPosition (screenPos.x + 14, screenPos.y + 12);
    }

    void hideGhost()
    {
        setVisible (false);
        if (isOnDesktop()) removeFromDesktop();
    }

    bool isGhostShowing() const { return isOnDesktop() && isVisible(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (boombap::ui::panel().withAlpha (0.95f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (boombap::ui::accent());
        g.drawRoundedRectangle (b, 6.0f, 1.4f);
        g.setColour (boombap::ui::accentHi());
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (text, getLocalBounds().reduced (8, 0), juce::Justification::centredLeft);
    }

private:
    juce::String text;
};

// Waveform display for one loaded sample (mono peaks, drawn per pixel column).
// Fix pass item 33: the panel is clickable - a click loads a sample into the
// row it is showing (the affordance the panel is supposed to carry; a
// display-only panel is a dead end).
class WaveformView : public juce::Component
{
public:
    std::function<void()> onClick; // load a sample into the shown row
    // Round 3 (item 100): drag the shown sample OUT of the panel (onto
    // a pad). Positions are screen positions. A plain click never
    // reaches these - the drag must travel past the threshold first,
    // so item 89's click-to-audition keeps the click to itself (the
    // audition fires on the press, like picking the sound up).
    std::function<void (juce::Point<int>)> onDragStart, onDragMove, onDragEnd;

    WaveformView() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setData (std::vector<float> samples) { data = std::move (samples); repaint(); }
    bool hasData() const { return ! data.empty(); }

    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (data.empty()) return;
        if (! dragging && e.getDistanceFromDragStart() > 8)
        {
            dragging = true;
            if (onDragStart) onDragStart (localPointToGlobal (e.position).roundToInt());
        }
        if (dragging && onDragMove) onDragMove (localPointToGlobal (e.position).roundToInt());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging)
        {
            dragging = false;
            if (onDragEnd) onDragEnd (localPointToGlobal (e.position).roundToInt());
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (boombap::ui::panelAlt().darker (0.35f));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (boombap::ui::border().withAlpha (0.5f));
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        if (data.empty())
        {
            g.setColour (boombap::ui::muted());
            g.setFont (13.0f);
            g.drawText ("Click to load a sample into this row",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }

        g.setColour (boombap::ui::muted().withAlpha (0.65f));
        g.setFont (10.5f);
        // Round 1 (item 89): the wave itself is the audition now - the
        // caption says so (the LOAD button does the loading). Round 3
        // (item 100): it is also the drag source for the pads.
        g.drawText ("click to play - LOAD loads another - drag onto a pad, a row, or the multitrack",
                    10, (int) b.getBottom() - 16, 470, 12, juce::Justification::left);

        const float mid = b.getCentreY();
        const float h = b.getHeight() * 0.44f;
        g.setColour (boombap::ui::muted().withAlpha (0.4f));
        g.drawLine (b.getX() + 6.0f, mid, b.getRight() - 6.0f, mid, 1.0f);

        g.setColour (boombap::ui::accent());
        const int cols = (int) b.getWidth() - 12;
        if (cols <= 0) return;
        const double per = (double) data.size() / (double) cols;
        for (int x = 0; x < cols; ++x)
        {
            const int start = (int) (x * per);
            const int end = std::min ((int) data.size(), std::max (start + 1, (int) ((x + 1) * per)));
            float lo = 0.0f, hi = 0.0f;
            for (int i = start; i < end; ++i)
            {
                lo = std::min (lo, data[(size_t) i]);
                hi = std::max (hi, data[(size_t) i]);
            }
            g.drawLine ((float) x + 6.0f, mid - hi * h, (float) x + 6.0f, mid - lo * h, 1.0f);
        }
    }

private:
    std::vector<float> data;
    bool dragging = false;
};

// ============================================================================
// Round 6 (item 112b): the pad bank's own meter - one segmented ladder
// running the height of the bank, with the item 112a clip latch box
// on top (red, lit at full scale, stays until clicked off).
// ============================================================================
class SegmentedLevelMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        shown = std::max (newLevel, shown * 0.90f);
        if (newLevel >= 0.99f && ! latched) latched = true;
        repaint();
    }
    float level() const { return shown; }
    bool clipIsLatched() const { return latched; }
    void clearClipLatch() { if (latched) { latched = false; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto box = b.removeFromTop (12.0f).reduced (0.5f, 0.5f);
        g.setColour (latched ? juce::Colour (0xffe5484d) : juce::Colour (0xff3a1518));
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour (latched ? juce::Colour (0xffff8a8d) : boombap::ui::border().withAlpha (0.6f));
        g.drawRoundedRectangle (box, 2.0f, 1.0f);
        b.removeFromTop (3.0f);
        boombap::ui::drawSegmentedMeter (g, b, std::sqrt (std::clamp (shown, 0.0f, 1.0f)), true);
    }

    void mouseDown (const juce::MouseEvent&) override { clearClipLatch(); }

private:
    float shown = 0.0f;
    bool latched = false;
};

// ============================================================================
// Round 6 (item 115): the transport keys - family A "Studio Console"
// (D's pick): chunky charcoal keys, amber glyphs, an amber ring on
// PLAY. One component, seven glyphs; the owner wires onClick and
// polls the lit state on its timer (PLAY lit while playing, PAUSE
// lit while paused).
// ============================================================================
class TransportButton : public juce::Component
{
public:
    enum class Glyph { ToStart, Rewind, Pause, Play, Stop, Forward, ToEnd };
    std::function<void()> onClick;

    TransportButton() = default; // the leak-detector macro's deleted copy ctor suppresses the implicit one

    void setGlyph (Glyph g) { glyph = g; repaint(); }
    Glyph getGlyph() const { return glyph; }
    void setLit (bool l) { if (lit != l) { lit = l; repaint(); } }
    bool isLit() const { return lit; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        juce::ColourGradient grad (juce::Colour (0xff2a2b30), b.getX(), b.getY(),
                                   juce::Colour (0xff17181b), b.getX(), b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (boombap::ui::border().withAlpha (0.9f));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        if (glyph == Glyph::Play)
        {
            g.setColour (boombap::ui::accent());
            g.drawRoundedRectangle (b.reduced (2.0f), 4.0f, lit ? 2.2f : 1.2f);
        }
        else if (lit)
        {
            g.setColour (boombap::ui::accent().withAlpha (0.30f));
            g.fillRoundedRectangle (b.reduced (2.0f), 4.0f);
        }
        drawGlyph (g, b);
    }

    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    void drawGlyph (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const float s = std::min (b.getWidth(), b.getHeight()) * 0.20f; // glyph half-size
        g.setColour (boombap::ui::accent());
        auto tri = [&] (float x0, bool left)
        {
            juce::Path p;
            if (left) { p.startNewSubPath (x0 + s, cy - s); p.lineTo (x0 + s, cy + s); p.lineTo (x0 - s, cy); }
            else      { p.startNewSubPath (x0 - s, cy - s); p.lineTo (x0 - s, cy + s); p.lineTo (x0 + s, cy); }
            p.closeSubPath();
            g.fillPath (p);
        };
        switch (glyph)
        {
            case Glyph::Play:    tri (cx, false); break;
            case Glyph::Stop:    g.fillRect (cx - s * 0.8f, cy - s * 0.8f, s * 1.6f, s * 1.6f); break;
            case Glyph::Pause:
                g.fillRect (cx - s * 0.85f, cy - s, s * 0.62f, s * 2.0f);
                g.fillRect (cx + s * 0.23f, cy - s, s * 0.62f, s * 2.0f);
                break;
            case Glyph::Rewind:  tri (cx - s * 0.55f, true);  tri (cx + s * 0.55f, true);  break;
            case Glyph::Forward: tri (cx - s * 0.55f, false); tri (cx + s * 0.55f, false); break;
            case Glyph::ToStart:
                g.fillRect (cx - s * 1.05f, cy - s, s * 0.34f, s * 2.0f);
                tri (cx + s * 0.25f, true);
                break;
            case Glyph::ToEnd:
                tri (cx - s * 0.25f, false);
                g.fillRect (cx + s * 0.71f, cy - s, s * 0.34f, s * 2.0f);
                break;
        }
    }

    Glyph glyph = Glyph::Play;
    bool lit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportButton)
};
