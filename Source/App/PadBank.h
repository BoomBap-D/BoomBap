#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include "UiComponents.h" // SampleDragGhost (item 100 drags)
#include <functional>

// ============================================================================
//  The selected pad's own waveform (item 44): the whole sample, the
//  [start, end] window the pad plays shaded amber, the rest dimmed.
//  Drag the window's edges here to trim in place - the same Start/End
//  sliders below follow - or click inside the window to audition.
// ============================================================================
class PadWaveView : public juce::Component
{
public:
    std::function<void (float, float)> onTrim; // (startFrac, endFrac), on release
    std::function<void()> onAudition;
    std::function<void()> onLoadRequest; // Fix Pass 3 (item 74): clicking the EMPTY waveform loads

    void setData (std::vector<float> mono, float startFrac, float endFrac)
    {
        data = std::move (mono);
        start = juce::jlimit (0.0f, 1.0f, startFrac);
        end = juce::jlimit (0.0f, 1.0f, endFrac);
        if (end <= start + 0.002f) end = juce::jmin (1.0f, start + 0.002f);
        rebuildPeaks();
        repaint();
    }

    // During a trim drag the sample does not change - window only.
    void setWindow (float startFrac, float endFrac)
    {
        start = juce::jlimit (0.0f, 1.0f, startFrac);
        end = juce::jlimit (0.0f, 1.0f, endFrac);
        repaint();
    }

    void setActive (bool isPlaying) // the sounding pad lights up (item 54)
    {
        if (active != isPlaying) { active = isPlaying; repaint(); }
    }

    float windowStart() const { return start; } // the trim window, for tests + sync checks
    float windowEnd() const   { return end; }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.setColour (panelAlt().darker (0.25f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (active ? accentHi() : border().withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, active ? 2.0f : 1.0f);

        if (peaks.empty())
        {
            // Fix Pass 3 (item 74): the empty waveform IS the load
            // surface - it says so, at a size you can read.
            g.setColour (accent());
            g.setFont (juce::Font (15.0f, juce::Font::bold));
            g.drawText ("CLICK TO LOAD A SAMPLE", getLocalBounds().reduced (8, 0),
                        juce::Justification::centred);
            return;
        }

        const auto b = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
        const float mid = b.getCentreY();
        const float h = b.getHeight() * 0.46f;
        const float x0 = b.getX() + start * b.getWidth();
        const float x1 = b.getX() + end * b.getWidth();

        const int cols = juce::jmax (1, (int) b.getWidth());
        for (int x = 0; x < cols; ++x)
        {
            const int idx = juce::jmin ((int) peaks.size() - 1,
                                        (int) ((double) x / (double) cols * (double) peaks.size()));
            const float px = b.getX() + (float) x;
            const bool inWindow = px >= x0 && px <= x1;
            g.setColour (inWindow ? accentHi() : muted().withAlpha (0.35f));
            g.drawLine (px, mid - peaks[(size_t) idx].second * h,
                        px, mid - peaks[(size_t) idx].first * h, 1.0f);
        }

        // Item 63: the parts of the sample that are NOT used are SHADED
        // OUT - the waveform itself shows the trim decision.
        g.setColour (juce::Colours::black.withAlpha (0.48f));
        if (x0 > b.getX())
            g.fillRect (juce::Rectangle<float> (b.getX(), b.getY(), x0 - b.getX(), b.getHeight()));
        if (x1 < b.getRight())
            g.fillRect (juce::Rectangle<float> (x1, b.getY(), b.getRight() - x1, b.getHeight()));

        // Pull bars ON the waveform at the trim points: grab and drag.
        for (const float bx : { x0, x1 })
        {
            const juce::Rectangle<float> bar (bx - 5.0f, b.getY(), 10.0f, b.getHeight());
            g.setColour (accent());
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (juce::Colour (0xff14100a).withAlpha (0.75f));
            g.drawLine (bx - 1.8f, b.getY() + 6.0f, bx - 1.8f, b.getBottom() - 6.0f, 1.4f);
            g.drawLine (bx + 1.8f, b.getY() + 6.0f, bx + 1.8f, b.getBottom() - 6.0f, 1.4f);
        }

        g.setColour (text());
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText ("PAD WAVEFORM - drag the pull bars to trim, click inside to hear it",
                    10, 2, getWidth() - 20, 13, juce::Justification::left);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (peaks.empty())
        {
            // Fix Pass 3 (item 74): the empty waveform opens the chooser.
            if (onLoadRequest) onLoadRequest();
            return;
        }
        // the pull bars are the grab targets (item 63): a generous pixel
        // zone around each bar, not a fraction of the sample
        const auto b = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
        const float x0 = b.getX() + start * b.getWidth();
        const float x1 = b.getX() + end * b.getWidth();
        dragEdge = 0;
        if (std::abs ((float) e.x - x0) <= 8.0f) dragEdge = 1;
        else if (std::abs ((float) e.x - x1) <= 8.0f) dragEdge = 2;
        if (dragEdge != 0) setWindowFromDrag (fracAt ((float) e.x));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragEdge != 0) setWindowFromDrag (fracAt ((float) e.x));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (peaks.empty()) return; // the click already asked for the chooser
        if (dragEdge != 0)
        {
            dragEdge = 0;
            if (onTrim) onTrim (start, end);
        }
        else if (onAudition) onAudition(); // a plain click inside the window
    }

private:
    float fracAt (float x) const
    {
        const auto b = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
        return juce::jlimit (0.0f, 1.0f, (x - b.getX()) / juce::jmax (1.0f, b.getWidth()));
    }

    void setWindowFromDrag (float fx)
    {
        if (dragEdge == 1) start = juce::jmin (fx, end - 0.002f);
        else if (dragEdge == 2) end = juce::jmax (fx, start + 0.002f);
        repaint();
    }

    void rebuildPeaks()
    {
        peaks.clear();
        if (data.empty()) return;
        const int numPeaks = 1024;
        peaks.resize ((size_t) numPeaks);
        const double per = (double) data.size() / (double) numPeaks;
        for (int p = 0; p < numPeaks; ++p)
        {
            const int s0 = (int) (p * per);
            const int s1 = juce::jmin ((int) data.size(), juce::jmax (s0 + 1, (int) ((p + 1) * per)));
            float lo = 0.0f, hi = 0.0f;
            for (int j = s0; j < s1; ++j)
            {
                lo = juce::jmin (lo, data[(size_t) j]);
                hi = juce::jmax (hi, data[(size_t) j]);
            }
            peaks[(size_t) p] = { lo, hi };
        }
    }

    std::vector<float> data;
    std::vector<std::pair<float, float>> peaks;
    float start = 0.0f, end = 1.0f;
    int dragEdge = 0;
    bool active = false;
};

// One MPC-style pad: number top-left, sample name along the bottom,
// amber outline when selected, a flash of amber when it sounds.
class PadButton : public juce::Component
{
public:
    std::function<void (int)> onPress;
    // Round 6 (item 110): a right-click asks for the pad's menu (LOAD /
    // CLEAR PAD) instead of playing the pad.
    std::function<void (int)> onContextMenu;
    // Round 3 (item 100): drag hooks - (pad, screen position). The
    // press itself still plays the pad (that is how you pick a sound
    // UP); a drag only begins once the pointer has travelled, so
    // click-to-play keeps the plain click exactly as it was.
    std::function<void (int, juce::Point<int>)> onDragBegin, onDragMove, onDragEnd;

    void setPad (int index, bool loaded, const juce::String& name)
    {
        padIndex = index;
        hasSample = loaded;
        label = name;
        repaint();
    }

    void setSelected (bool s)
    {
        if (selected != s) { selected = s; repaint(); }
    }

    // A sample drag is hovering this pad: it would land here.
    void setDragTarget (bool t)
    {
        if (dragTarget != t) { dragTarget = t; repaint(); }
    }
    bool isDragTarget() const { return dragTarget; }

    bool isFlashing() const { return flash > 0.05f; }
    float flashLevel() const { return flash; }
    const juce::String& captionText() const { return label; }

    void triggerFlash() { flash = 1.0f; repaint(); }

    void decayFlash()
    {
        if (flash > 0.0f) { flash = std::max (0.0f, flash - 0.09f); repaint(); }
    }

    bool dragHasStarted() const { return dragStarted; } // item 110: the threshold, testable

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu) onContextMenu (padIndex);
            return;
        }
        if (onPress) onPress (padIndex);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Round 6 (item 110): the travel threshold is 16 px now, not 8 -
        // at 8 an ordinary emphatic click wobbled into a copy and pads
        // filled with duplicates. A deliberate drag travels further.
        if (! dragStarted && e.getDistanceFromDragStart() > 16)
        {
            dragStarted = true;
            if (onDragBegin) onDragBegin (padIndex, localPointToGlobal (e.position).roundToInt());
        }
        if (dragStarted && onDragMove) onDragMove (padIndex, localPointToGlobal (e.position).roundToInt());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragStarted)
        {
            dragStarted = false;
            if (onDragEnd) onDragEnd (padIndex, localPointToGlobal (e.position).roundToInt());
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        // Pass 1 item 4: the pads take the step sequencer's lighter charcoal
        // gray (0xff262b33 family), with a slight top-to-bottom gradient.
        const juce::Colour top = hasSample ? juce::Colour (0xff31363f) : juce::Colour (0xff272b32);
        const juce::Colour bot = hasSample ? juce::Colour (0xff22252c) : juce::Colour (0xff1b1e24);
        g.setGradientFill (juce::ColourGradient (top, b.getX(), b.getY(),
                                                 bot, b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 10.0f);
        if (flash > 0.0f)
        {
            g.setColour (boombap::ui::accent().withAlpha (flash * 0.35f));
            g.fillRoundedRectangle (b, 10.0f);
        }
        if (dragTarget)
        {
            g.setColour (boombap::ui::accentHi().withAlpha (0.30f));
            g.fillRoundedRectangle (b, 10.0f);
        }
        g.setColour (dragTarget ? boombap::ui::accentHi()
                                : selected ? boombap::ui::accent() : boombap::ui::border().withAlpha (0.7f));
        g.drawRoundedRectangle (b, 10.0f, dragTarget ? 2.2f : selected ? 1.6f : 1.0f);

        g.setColour (selected ? boombap::ui::accentHi() : boombap::ui::muted());
        g.setFont (juce::Font (15.0f, juce::Font::bold));
        g.drawText (juce::String (padIndex + 1), 10, 7, 34, 18, juce::Justification::left);

        // Round 3 (item 100): the sample NAME is back on the pad - "that's
        // the kick right there", without looking around. This deliberately
        // reverses the caption half of item 71; item 71's other wins
        // (numbered pads, the tall waveform, click-the-box-to-load) stand.
        if (hasSample && label.isNotEmpty())
        {
            g.setColour (selected ? boombap::ui::text() : boombap::ui::text().withAlpha (0.85f));
            g.setFont (juce::Font (10.5f, juce::Font::bold));
            g.drawText (label, 6, getHeight() - 19, getWidth() - 12, 13,
                        juce::Justification::centred);
        }
    }

private:
    int padIndex = 0;
    bool hasSample = false, selected = false, dragTarget = false, dragStarted = false;
    float flash = 0.0f;
    juce::String label;
};

// ============================================================================
//  Round 3 (item 88, second half): the visual-mix strip over the pad
//  bank - the Audio Multitrack's top name strip, replicated for the 16
//  pads. One cell per pad, in pad order: a cell lights with the pad's
//  activity and wears the selection, in item 68's two treatments
//  (PLAYING = bright outline over a tint; SELECTED = solid fill), so
//  you can see what is sounding and what is selected while you work
//  anywhere else in the bank. Clicking a cell selects its pad.
// ============================================================================
class PadNameStrip : public juce::Component
{
public:
    std::function<juce::String (int)> nameFor;
    std::function<void (int)> onSelect;

    void setSelected (int pad)
    {
        if (selected != pad) { selected = pad; repaint(); }
    }
    int selectedPad() const { return selected; }

    void flash (int pad)
    {
        if (pad >= 0 && pad < 16) { flashLevel[pad] = 1.0f; repaint(); }
    }
    float cellFlash (int pad) const { return (pad >= 0 && pad < 16) ? flashLevel[pad] : 0.0f; }

    void decayFlashes()
    {
        bool any = false;
        for (auto& f : flashLevel)
            if (f > 0.0f) { f = std::max (0.0f, f - 0.09f); any = true; }
        if (any) repaint();
    }

    juce::Rectangle<int> cellBounds (int pad) const
    {
        const float gap = 4.0f;
        const float cellW = ((float) getWidth() - 15.0f * gap) / 16.0f;
        return { (int) (pad * (cellW + gap)), 0, (int) cellW, getHeight() };
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 16; ++i)
            if (cellBounds (i).contains (e.x, e.y))
            {
                if (onSelect) onSelect (i);
                return;
            }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        for (int i = 0; i < 16; ++i)
        {
            const auto cell = cellBounds (i).toFloat();
            const bool isSel = i == selected;
            const float fl = flashLevel[i];
            const bool playing = fl > 0.05f;
            g.setColour (isSel ? accent()
                               : playing ? accent().withAlpha (0.20f + fl * 0.25f)
                                         : panelAlt());
            g.fillRoundedRectangle (cell, 6.0f);
            if (playing)
            {
                g.setColour (accentHi().withAlpha (fl));
                g.drawRoundedRectangle (cell, 6.0f, 2.2f);
            }
            else
            {
                g.setColour (isSel ? accent().darker (0.45f) : border().withAlpha (0.7f));
                g.drawRoundedRectangle (cell, 6.0f, isSel ? 1.4f : 1.0f);
            }
            const juce::String name = nameFor ? nameFor (i) : juce::String();
            const juce::String text = (name.isEmpty() ? "PAD " + juce::String (i + 1) : name).toUpperCase();
            g.setColour (isSel ? juce::Colour (0xff14100a) : playing ? boombap::ui::text() : muted());
            g.setFont (juce::Font (juce::jmin (12.0f, juce::jmax (9.0f, cell.getWidth() * 0.20f)),
                                   (isSel || playing) ? juce::Font::bold : juce::Font::plain));
            g.drawText (text, cell, juce::Justification::centred);
        }
    }

private:
    int selected = 0;
    float flashLevel[16] = {};
};

// ============================================================================
//  Round 3 (item 101): the pad editor's multi-point ENVELOPE lane.
//  Points are (time = fraction of the trimmed window, level 0..1),
//  kept sorted by time. Gestures: drag a point to move it; DOUBLE-
//  CLICK empty lane to add a point (on the curve, where you clicked);
//  RIGHT-CLICK a point to remove it (two points always remain - an
//  envelope needs at least a start and an end). FL's channel-settings
//  envelope is the owner's reference: free shapes, shaped by hand.
//  An untouched pad has NO envelope in the engine; the lane then shows
//  the default four flat points as an implicit starting shape, and
//  they become real points with the first edit (pointsToStore).
// ============================================================================
class PadEnvelopeView : public juce::Component
{
public:
    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void()> onChange; // points changed (read pointsToStore())

    static std::vector<std::pair<float, float>> defaultPoints()
    {
        return { { 0.0f, 1.0f }, { 1.0f / 3.0f, 1.0f }, { 2.0f / 3.0f, 1.0f }, { 1.0f, 1.0f } };
    }

    void setPoints (const std::vector<std::pair<float, float>>& real)
    {
        implicit = real.size() < 2;
        pts = implicit ? defaultPoints() : real;
        dragIndex = -1;
        repaint();
    }

    // Patterns build (item 201, carrying item 151c): the envelope
    // rides OVER the selected pad's waveform as line-work - no panel
    // of its own, no shaded area, no labels. In overlay mode the view
    // is transparent and only the curve's neighbourhood takes the
    // mouse (see hitTest), so the waveform's own gestures - the trim
    // pull bars, the click to audition - keep working everywhere else.
    void setOverlayMode (bool o) { overlayMode = o; repaint(); }
    bool isOverlayMode() const { return overlayMode; }

    bool hitTest (int x, int y) override
    {
        if (! overlayMode) return true;
        const auto p = juce::Point<float> ((float) x, (float) y);
        for (int i = 0; i < (int) pts.size(); ++i)
            if (pointComponentPos (i).getDistanceFrom (p) <= 11.0f) return true;
        const float w = juce::jmax (1.0f, (float) getWidth());
        const float curveY = (1.0f - levelAt (juce::jlimit (0.0f, 1.0f, (float) x / w)))
                           * juce::jmax (1.0f, (float) getHeight());
        return std::abs ((float) y - curveY) <= 9.0f;
    }

    // What the engine should hold: nothing while the shape is still
    // the untouched default, the real points once it has been shaped.
    std::vector<std::pair<float, float>> pointsToStore() const
    {
        return implicit ? std::vector<std::pair<float, float>>() : pts;
    }

    int pointCount() const { return (int) pts.size(); }
    std::pair<float, float> pointAt (int i) const { return pts[(size_t) juce::jlimit (0, (int) pts.size() - 1, i)]; }
    bool isImplicit() const { return implicit; }

    juce::Point<float> pointComponentPos (int i) const
    {
        const auto p = pointAt (i);
        return { p.first * (float) getWidth(), (1.0f - p.second) * (float) getHeight() };
    }

    float levelAt (float frac) const { return boombap::MpcPads::envLevelAt (pts, frac); }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        auto b = getLocalBounds().toFloat();
        if (! overlayMode)
        {
            g.setColour (panelAlt().darker (0.25f));
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (border().withAlpha (0.6f));
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

            g.setColour (border().withAlpha (0.30f));
            for (float f : { 0.25f, 0.5f, 0.75f })
            {
                const float y = b.getY() + f * b.getHeight();
                g.drawLine (b.getX() + 4.0f, y, b.getRight() - 4.0f, y, 1.0f);
            }
        }

        if (pts.size() >= 2)
        {
            juce::Path curve, fill;
            for (int x = 0; x <= (int) b.getWidth(); ++x)
            {
                const float frac = (float) x / juce::jmax (1.0f, b.getWidth());
                const float y = b.getY() + (1.0f - levelAt (frac)) * b.getHeight();
                if (x == 0) { curve.startNewSubPath (b.getX(), y); fill.startNewSubPath (b.getX(), b.getBottom()); fill.lineTo (b.getX(), y); }
                else { curve.lineTo (b.getX() + (float) x, y); fill.lineTo (b.getX() + (float) x, y); }
            }
            if (! overlayMode)
            {
                fill.lineTo (b.getRight(), b.getBottom());
                fill.closeSubPath();
                g.setColour (accent().withAlpha (implicit ? 0.08f : 0.18f));
                g.fillPath (fill);
            }
            g.setColour (accent().withAlpha (implicit ? 0.55f : 1.0f));
            g.strokePath (curve, juce::PathStrokeType (1.8f));

            for (int i = 0; i < (int) pts.size(); ++i)
            {
                const auto p = pointComponentPos (i);
                const float r = i == dragIndex ? 6.5f : 5.0f;
                g.setColour (juce::Colour (0xff14100a));
                g.fillEllipse (p.x - r - 1.0f, p.y - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
                g.setColour (accentHi());
                g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
            }
        }

        if (! overlayMode)
        {
            g.setColour (muted());
            g.setFont (juce::Font (10.5f, juce::Font::bold));
            g.drawText ("ENVELOPE", 8, 3, 90, 12, juce::Justification::left);
            g.setFont (juce::Font (9.5f));
            g.drawText ("drag points - double-click adds - right-click a point removes",
                        (int) b.getRight() - 308, 3, 300, 12, juce::Justification::right);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = hitPoint (e.position);
        if (e.mods.isRightButtonDown())
        {
            if (hit >= 0 && pts.size() > 2)
            {
                if (onGestureStart) onGestureStart();
                pts.erase (pts.begin() + hit);
                implicit = false;
                repaint();
                if (onChange) onChange();
                if (onGestureEnd) onGestureEnd();
            }
            return;
        }
        if (hit >= 0)
        {
            dragIndex = hit;
            if (onGestureStart) onGestureStart();
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex < 0 || dragIndex >= (int) pts.size()) return;
        const float w = juce::jmax (1.0f, (float) getWidth());
        const float h = juce::jmax (1.0f, (float) getHeight());
        const float lo = dragIndex > 0 ? pts[(size_t) dragIndex - 1].first + 0.01f : 0.0f;
        const float hi = dragIndex < (int) pts.size() - 1 ? pts[(size_t) dragIndex + 1].first - 0.01f : 1.0f;
        pts[(size_t) dragIndex].first  = juce::jlimit (lo, hi, e.position.x / w);
        pts[(size_t) dragIndex].second = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / h);
        implicit = false;
        repaint();
        if (onChange) onChange();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex >= 0)
        {
            dragIndex = -1;
            repaint();
            if (onGestureEnd) onGestureEnd();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if ((int) pts.size() >= boombap::ProjectSettings::kPadEnvMax) return;
        const float t = juce::jlimit (0.0f, 1.0f, e.position.x / juce::jmax (1.0f, (float) getWidth()));
        for (const auto& p : pts)
            if (std::abs (p.first - t) < 0.01f) return; // a point already lives there
        if (onGestureStart) onGestureStart();
        const std::pair<float, float> np { t, levelAt (t) };
        auto it = std::find_if (pts.begin(), pts.end(),
                                [&] (const std::pair<float, float>& p) { return p.first > t; });
        pts.insert (it, np);
        implicit = false;
        repaint();
        if (onChange) onChange();
        if (onGestureEnd) onGestureEnd();
    }

private:
    int hitPoint (juce::Point<float> pos) const
    {
        for (int i = 0; i < (int) pts.size(); ++i)
            if (pointComponentPos (i).getDistanceFrom (pos) <= 9.0f) return i;
        return -1;
    }

    std::vector<std::pair<float, float>> pts = defaultPoints();
    bool implicit = true;
    bool overlayMode = false; // item 201: line-work over the waveform
    int dragIndex = -1;
};

// The pad bank: a 4x4 pad grid plus an editor for the selected pad
// (load, chop start/end, tune, gain, velocity sensitivity, reverse, mute group).
// Round 6 (item 109): the pads are their own store - no pad IS a
// sequencer row any more; rows hold their own sounds in the engine.
class PadBankComponent : public juce::Component, private juce::Timer
{
public:
    using LoadCallback = std::function<void (int)>;
    using NameCallback = std::function<juce::String (int)>;

    PadBankComponent (boombap::BoomBapEngine& eng, LoadCallback loadCb, NameCallback nameCb)
        : engine (eng), onLoad (std::move (loadCb)), nameFor (std::move (nameCb))
    {
        for (int i = 0; i < 16; ++i)
        {
            pads[i].onPress = [this] (int p) { selectPad (p, true); };
            // Round 6 (item 110): right-click a pad -> its menu.
            pads[i].onContextMenu = [this] (int p) { showPadMenu (p); };
            // item 100: pad -> pad drags run through the bank (the
            // ghost, the hover light, and the copy request live here)
            pads[i].onDragBegin = [this] (int p, juce::Point<int> pos) { beginPadDrag (p, pos); };
            pads[i].onDragMove  = [this] (int, juce::Point<int> pos)   { movePadDrag (pos); };
            pads[i].onDragEnd   = [this] (int, juce::Point<int> pos)   { endPadDrag (pos); };
            addAndMakeVisible (pads[i]);
        }

        // Round 6 (item 112b): the bank's own segmented meter, running
        // the whole right side of the bank.
        addAndMakeVisible (bankMeter);

        // Item 88: the visual-mix strip over the bank - the multitrack
        // strip's pad twin (names across the top, activity + selection
        // lit). A cell click selects the pad, like a chip click there.
        nameStrip.nameFor = [this] (int p) { return displayName (p); };
        nameStrip.onSelect = [this] (int p) { selectPad (p, false); };
        addAndMakeVisible (nameStrip);

        // Fix Pass 3 (items 71/74): the editor header is ONE compact
        // label now. The old stack (title + file line + LOAD SAMPLE
        // button) is gone - the waveform below is the load surface, and
        // the space the stack took goes to the waveform.
        title.setFont (juce::Font (15.0f, juce::Font::bold));
        addAndMakeVisible (title);

        // Round 6 (item 111): the old info strip is GONE - it repeated
        // the header's "PAD n - file" line at the bottom of the editor,
        // which is the duplicated caption the item calls out. The
        // header at the top is the ONE letterbox now; the guidance
        // paragraph below the knobs stays (it teaches the gestures,
        // it does not repeat the caption).

        // item 44: the selected pad's waveform, trimmable in place
        waveView.onTrim = [this] (float s, float e)
        {
            if (onBeforeChange) onBeforeChange();
            startRow.slider.setValue (s * 100.0, juce::dontSendNotification);
            endRow.slider.setValue (e * 100.0, juce::dontSendNotification);
            pushEditor();
            if (onAfterChange) onAfterChange();
        };
        waveView.onAudition = [this]
        {
            engine.triggerPad (selected, 1.0f);
            pads[selected].triggerFlash();
        };
        // Fix Pass 3 (item 74): clicking the empty waveform loads.
        waveView.onLoadRequest = [this] { if (onLoad) onLoad (selected); };
        addAndMakeVisible (waveView);

        // Item 101: the envelope lane under the waveform - it edits
        // the selected pad's points live, inside one undo gesture.
        envView.onGestureStart = [this] { if (onBeforeChange) onBeforeChange(); };
        envView.onGestureEnd   = [this] { if (onAfterChange) onAfterChange(); };
        envView.onChange       = [this] { pushEditor(); };
        envView.setOverlayMode (true); // item 201: lines over the wave (item 151c)
        addAndMakeVisible (envView);

        // Items 80/101 (restating item 72's exception): Start/End are
        // SLIDERS - they pair with the waveform's trim endpoints, and
        // length means something there. Tune/Gain/Vel Sens stay rotary
        // knobs, drawn by the approved Round 2 knob family in the
        // shared LookAndFeel (British Class A x Modern Arc).
        setupSliderRow (startRow, "Start %", 0.0, 97.0, 0.5, 0.0);
        setupSliderRow (endRow, "End %", 3.0, 100.0, 0.5, 100.0);
        setupKnobRow (tuneRow, "Tune st", -24.0, 24.0, 0.1, 0.0);
        setupKnobRow (gainRow, "Gain", 0.0, 2.0, 0.01, 1.0);
        setupKnobRow (velRow, "Vel sens", 0.0, 1.0, 0.01, 1.0);

        reverseToggle.onClick = [this]
        {
            if (onBeforeChange) onBeforeChange();
            pushEditor();
            if (onAfterChange) onAfterChange();
        };
        addAndMakeVisible (reverseToggle);

        muteLabel.setText ("Mute group", juce::dontSendNotification);
        addAndMakeVisible (muteLabel);
        muteGroupBox.addItem ("None", 1);
        for (int gnum = 1; gnum <= 4; ++gnum) muteGroupBox.addItem (juce::String (gnum), gnum + 1);
        muteGroupBox.onChange = [this]
        {
            if (onBeforeChange) onBeforeChange();
            pushEditor();
            if (onAfterChange) onAfterChange();
        };
        addAndMakeVisible (muteGroupBox);

        // Bank build (items 158/179): the factory-kit selector on the
        // editor's top line - one choice fills all sixteen pads.
        kitBox.setTextWhenNothingSelected ("Factory kit...");
        kitBox.onChange = [this]
        {
            const int id = kitBox.getSelectedId();
            if (id > 0 && onFactoryKitChosen) onFactoryKitChosen (id - 1);
        };
        addAndMakeVisible (kitBox);

        // Item 100: the wording that used to sit UNDER THE PADS (where
        // it cost the pads their size, item 71) lives under the knobs
        // now. (Round 6 item 111 rewrote the words for the decoupled
        // pads and dropped the identity strip that shared this block.)
        // A read-only TextEditor, not a Label: a Label clips this
        // paragraph to one line instead of wrapping it (seen in the
        // Round 3 screenshots). The editor wraps at any column width;
        // it takes no mouse, shows no caret, and is sized in resized()
        // from an exact TextLayout measure of the same words.
        guidance.setReadOnly (true);
        guidance.setMultiLine (true, true);
        guidance.setScrollbarsShown (false);
        guidance.setCaretVisible (false);
        guidance.setPopupMenuEnabled (false);
        guidance.setInterceptsMouseClicks (false, false);
        guidance.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        guidance.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        guidance.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        guidance.setColour (juce::TextEditor::textColourId, boombap::ui::muted());
        guidance.setFont (juce::Font (11.0f));
        guidance.setText (guidanceText, juce::dontSendNotification);
        guidance.applyFontToAllText (juce::Font (11.0f));
        addAndMakeVisible (guidance);

        refreshAll();
        selectPad (0, false);
        startTimerHz (30);
    }

    ~PadBankComponent() override { stopTimer(); }

    void refreshPad (int pad)
    {
        pads[pad].setPad (pad, engine.padHasSample (pad), displayName (pad));
        nameStrip.repaint(); // the strip cell carries the same name
        if (pad == selected) pullEditor();
    }

    void refreshAll()
    {
        for (int i = 0; i < 16; ++i) refreshPad (i);
        pullEditor();
    }

    // Pass 1 item 4b: a pad that sounds (sequencer or live) flashes here too.
    void showActivity (int pad)
    {
        if (pad >= 0 && pad < 16)
        {
            pads[pad].triggerFlash();
            nameStrip.flash (pad); // item 88: the strip lights with it
        }
    }

    void selectPad (int pad, bool trigger)
    {
        selected = pad;
        for (int i = 0; i < 16; ++i) pads[i].setSelected (i == pad);
        nameStrip.setSelected (pad);
        pullEditor();
        // Pass 2: the Station follows the pad to its row channel (the hit
        // itself is performance - never an undoable edit)
        if (onSelectPad) onSelectPad (boombap::BoomBapEngine::Sample1 + pad);
        if (trigger)
        {
            engine.triggerPad (pad, 1.0f);
            pads[pad].triggerFlash();
        }
    }

    // Pass 2 (item 38): undo gesture hooks + Station channel selection,
    // wired by the owner after construction.
    std::function<void()> onBeforeChange, onAfterChange;
    std::function<void (int)> onSelectPad;
    // Round 3 (item 100): a pad -> pad drag landed. The OWNER performs
    // the copy (it owns the pads' files): the target pad receives the
    // source's sample plus an independent snapshot of its settings.
    std::function<void (int, int)> onPadCopyRequest;
    // Round 6 (item 110): the pad menu's CLEAR PAD - the owner empties
    // the pad as one undoable gesture.
    std::function<void (int)> onPadClearRequest;
    // MIDI Learn fix (item 147): the pad menu's MIDI LEARN - the owner
    // arms the one-shot learn capture for this pad ("pad:N"), so the
    // next pad hit on the controller becomes this pad. Pads are
    // learnable targets: per-pad learn is how a 16-pad map gets built
    // on hardware whose factory layout does not match.
    std::function<void (int)> onPadMidiLearn;
    // Bank build (items 158/179): the top line's kit selector - the
    // owner loads the chosen factory kit into the bank as one
    // undoable action (the sounds live in the binaries, FactoryBank).
    std::function<void (int)> onFactoryKitChosen;

    // Bank build: the owner hands over the factory kit names once;
    // the selector then offers them on the bank's top line.
    void setFactoryKitNames (const juce::StringArray& names)
    {
        kitBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            kitBox.addItem (names[i], i + 1);
    }
    // Show a kit as the selector's current choice WITHOUT firing the
    // load (the two bank instances and the keyboard's selector keep
    // each other honest through the owner).
    void showFactoryKit (int kit)
    {
        kitBox.setSelectedId (kit + 1, juce::dontSendNotification);
    }

    // Round 6 (item 110): the pad's right-click menu.
    // MIDI Learn fix (item 147): the menu carries MIDI Learn.
    void showPadMenu (int pad)
    {
        selectPad (pad, false);
        juce::PopupMenu m;
        m.addItem (1, "Load sample...");
        m.addItem (2, "Clear pad", engine.padHasSample (pad));
        m.addItem (3, "MIDI Learn", onPadMidiLearn != nullptr);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pads[pad]),
                         [this, pad] (int choice)
                         {
                             if (choice == 1 && onLoad) onLoad (pad);
                             else if (choice == 2 && onPadClearRequest) onPadClearRequest (pad);
                             else if (choice == 3 && onPadMidiLearn) onPadMidiLearn (pad);
                         });
    }

    // Round 6 (item 111a): the DOCKED bank (the main window's MPC tab)
    // spends its height on the pads themselves - a squarer aspect
    // makes them bigger than the pop-out's letterbox-friendly grid.
    void setDockedMode (bool d) { if (dockedMode != d) { dockedMode = d; resized(); repaint(); } }
    bool isDockedMode() const { return dockedMode; }
    SegmentedLevelMeter& bankMeterComponent() { return bankMeter; }
    juce::ComboBox& kitBoxComponent()              { return kitBox; }

    // ---- Round 3: drag + inspection doors (the same objects the mouse
    // drives; the waveform panel's drag and the regression tests both
    // come through here, no test-only back doors) ----
    int padAtScreenPosition (juce::Point<int> screenPos) const
    {
        for (int i = 0; i < 16; ++i)
            if (pads[i].getScreenBounds().contains (screenPos)) return i;
        return -1;
    }
    void setDragHighlightPad (int pad)
    {
        for (int i = 0; i < 16; ++i) pads[i].setDragTarget (i == pad);
    }
    juce::Component& padComponent (int pad)        { return pads[pad]; }
    juce::Rectangle<int> padBounds (int pad) const { return pads[pad].getBounds(); }
    juce::String padCaption (int pad) const        { return pads[pad].captionText(); }
    int selectedPadIndex() const                   { return selected; }
    PadNameStrip& nameStripComponent()             { return nameStrip; }
    PadWaveView& waveComponent()                   { return waveView; }
    PadEnvelopeView& envelopeComponent()           { return envView; }
    juce::Slider& startSlider()                    { return startRow.slider; }
    juce::Slider& endSlider()                      { return endRow.slider; }
    juce::Slider& tuneKnob()                       { return tuneRow.slider; }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());
        auto panel = [&] (int x, int y, int w, int h)
        {
            juce::Rectangle<float> r ((float) x, (float) y, (float) w, (float) h);
            g.setColour (boombap::ui::panel());
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (border().withAlpha (0.55f));
            g.drawRoundedRectangle (r, 12.0f, 1.0f);
        };
        const auto lay = layout();
        // Item 201: three panels under the name strip and the top
        // line - the pad grid (at least half the window wide), the
        // waveform column, and the controls' side strip. All stop
        // short of the bank meter's strip on the right edge.
        panel (8, 58, lay.gridRight - 6, getHeight() - 66);                    // pad grid
        const int waveBottom = lay.gridY + lay.waveH
                             + (guidance.isVisible() ? guidance.getHeight() + 8 : 0);
        panel (lay.waveX - 6, 58, lay.waveW + 6, waveBottom - 58 + 8);         // waveform
        panel (lay.stripX - 6, 58, lay.stripW + 12, getHeight() - 66);         // side strip
    }

    void resized() override
    {
        // Item 201 (carrying items 151/166): the grid takes at least
        // half the window's width and the pads fill their cells -
        // elongated is fine, size first (item 151a). The waveform
        // gets its own column at real height with the envelope as
        // lines over it; every other pad control lives in the side
        // strip. The name strip stays on top; the kit selector stays
        // on the top line.
        const auto lay = layout();
        const int W = getWidth(), H = getHeight();

        // Item 88: the visual-mix strip spans the whole top (stopping
        // short of the bank meter's strip on the right).
        nameStrip.setBounds (16, 8, W - 62, 22);

        // Item 112b: the bank meter runs the whole right side.
        bankMeter.setBounds (W - 24, 44, 16, H - 52);

        // The top line: the selected pad's letterbox over the grid,
        // the kit selector at the right (bank build, item 158).
        title.setBounds (16, 34, lay.gridW, 20);
        kitBox.setBounds (W - 246, 32, 200, 24);

        const int gridY = lay.gridY;
        for (int i = 0; i < 16; ++i)
            pads[i].setBounds (16 + (i % 4) * (lay.padW + 8), gridY + (i / 4) * (lay.padH + 8),
                               lay.padW, lay.padH);

        // The waveform column: the tall wave, the envelope view laid
        // over it at the same bounds (overlay mode - line-work only).
        waveView.setBounds (lay.waveX, gridY, lay.waveW, lay.waveH);
        envView.setBounds (lay.waveX, gridY, lay.waveW, lay.waveH);

        // Item 100: the guidance editor WRAPS - measure the paragraph
        // at the wave column's width; it shows under the wave when
        // the bank is tall enough (item 111's rule).
        int guidanceH = 0;
        if (H >= 400)
        {
            juce::AttributedString measured;
            measured.setText (guidanceText);
            measured.setFont (juce::Font (11.0f));
            juce::TextLayout tl;
            tl.createLayout (measured, (float) juce::jmax (120, lay.waveW - 8));
            guidanceH = juce::jlimit (28, 120, (int) std::ceil (tl.getHeight()) + 6);
        }
        guidance.setVisible (H >= 400);
        guidance.setBounds (lay.waveX, gridY + lay.waveH + 8, lay.waveW, juce::jmax (0, guidanceH));

        // The side strip, top to bottom: Start / End SLIDERS (item
        // 80), the three knobs, then the footer - Reverse and Mute
        // Group below the knobs, never stacked in a corner (item
        // 111b's law stands).
        const int sx = lay.stripX, sw = lay.stripW;
        int y = gridY;
        startRow.label.setBounds (sx, y, sw, 13);
        startRow.slider.setBounds (sx, y + 15, sw, 24);
        y += 46;
        endRow.label.setBounds (sx, y, sw, 13);
        endRow.slider.setBounds (sx, y + 15, sw, 24);
        y += 46;
        Row* knobs[] = { &tuneRow, &gainRow, &velRow };
        const int cellW = sw / 3;
        const int knobSize = juce::jlimit (44, 64, cellW - 22);
        for (int i = 0; i < 3; ++i)
        {
            const int cellX = sx + i * cellW;
            knobs[i]->label.setBounds (cellX, y, cellW, 14);
            knobs[i]->slider.setBounds (cellX + (cellW - knobSize) / 2, y + 16,
                                        knobSize, knobSize + 16);
        }
        y += 16 + knobSize + 16 + 8;
        reverseToggle.setBounds (sx, y, 110, 24);
        muteLabel.setBounds (sx + 118, y + 2, 84, 20);
        y += 30;
        muteGroupBox.setBounds (sx, y, 130, 24);
    }

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
    };

    struct Layout
    {
        int padW = 104, padH = 74;
        int gridY = 66, gridW = 456, gridRight = 472;
        int waveX = 488, waveW = 300, waveH = 110;
        int stripX = 800, stripW = 252;
    };

    Layout layout() const
    {
        Layout lay;
        // Item 201 (the pad-enlargement rider, carrying items
        // 151/166): the GRID owns at least half the window - 54% of
        // the bank's width - and the pads fill their cells outright
        // (item 151a: elongated pads are fine; size first). This
        // supersedes item 111a's squarer-docked geometry: at the
        // tab's fixed height, half the screen and square cannot both
        // be true, and D's Nov 14 ruling chose the half screen.
        // The waveform keeps a working width even in a small pop-out:
        // the grid's share yields before the wave starves.
        const int W = getWidth(), H = getHeight();
        lay.gridY = 66;
        const int zoneH = juce::jmax (120, H - lay.gridY - 6);
        lay.stripW = W >= 1300 ? 252 : 208;
        int gridW = (W * 54) / 100;
        gridW = juce::jmin (gridW, W - 16 - 16 - 150 - 12 - lay.stripW - 32);
        lay.gridW = juce::jmax (280, gridW);
        lay.padW = (lay.gridW - 24) / 4;
        lay.padH = (zoneH - 24) / 4;
        lay.gridRight = 16 + lay.gridW;
        lay.waveX = lay.gridRight + 16;
        lay.stripX = W - 32 - lay.stripW;
        lay.waveW = juce::jmax (80, lay.stripX - 12 - lay.waveX);
        // The wave at real height: roughly double the old editor's
        // squeezed strip (~50 px at the tab's height), capped so a
        // tall pop-out still leaves the strip its room.
        lay.waveH = juce::jlimit (88, 160, (zoneH * 45) / 100);
        return lay;
    }

    void setupKnobRow (Row& row, const char* name, double lo, double hi, double step, double def)
    {
        row.label.setText (name, juce::dontSendNotification);
        row.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (row.label);
        // Fix Pass 3 (item 72): rotary KNOBS, in the Station's exact
        // grammar (RotaryHorizontalVerticalDrag + the value box below) -
        // drawn since Round 2 by the approved knob family in the shared
        // LookAndFeel, like every rotary in the program.
        // They are still juce::Sliders underneath, so the double-click
        // reset and the waveform pull-bar sync (items 44/63) keep the
        // same wiring.
        row.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
        row.slider.setRange (lo, hi, step);
        row.slider.setDoubleClickReturnValue (true, def); // Pass 1: double-click reset
        row.slider.onDragStart = [this] { if (onBeforeChange) onBeforeChange(); };
        row.slider.onDragEnd = [this] { if (onAfterChange) onAfterChange(); };
        row.slider.onValueChange = [this] { pushEditor(); };
        addAndMakeVisible (row.slider);
    }

    void setupSliderRow (Row& row, const char* name, double lo, double hi, double step, double def)
    {
        row.label.setText (name, juce::dontSendNotification);
        row.label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (row.label);
        // Items 80/101: Start/End are SLIDERS - the exception class
        // item 72 itself named (length means something: the slider's
        // travel IS the sample's length, endpoint to endpoint), and
        // they stay two-way synced with the waveform's pull bars:
        // drag either, both move (the pull bars write these sliders in
        // waveView.onTrim; these sliders write the window in
        // pushEditor).
        row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        row.slider.setRange (lo, hi, step);
        row.slider.setDoubleClickReturnValue (true, def); // Pass 1: double-click reset
        row.slider.onDragStart = [this] { if (onBeforeChange) onBeforeChange(); };
        row.slider.onDragEnd = [this] { if (onAfterChange) onAfterChange(); };
        row.slider.onValueChange = [this] { pushEditor(); };
        addAndMakeVisible (row.slider);
    }

    juce::String displayName (int pad) const
    {
        const juce::String n = nameFor ? nameFor (pad) : juce::String();
        return n.isEmpty() ? n : juce::File (n).getFileNameWithoutExtension();
    }

    void pullEditor()
    {
        const auto pp = engine.getPadParams (selected);
        startRow.slider.setValue (pp.startFrac * 100.0, juce::dontSendNotification);
        endRow.slider.setValue (pp.endFrac * 100.0, juce::dontSendNotification);
        tuneRow.slider.setValue (pp.tuneSemitones, juce::dontSendNotification);
        gainRow.slider.setValue (pp.gain, juce::dontSendNotification);
        velRow.slider.setValue (pp.velSens, juce::dontSendNotification);
        reverseToggle.setToggleState (pp.reverse, juce::dontSendNotification);
        muteGroupBox.setSelectedId (pp.muteGroup + 1, juce::dontSendNotification);
        const juce::String n = nameFor ? nameFor (selected) : juce::String();
        const juce::String fileName = n.isEmpty() ? juce::String() : juce::File (n).getFileName();
        // Items 71/74 + Round 6 (item 111): the compact header label is
        // the ONE place the selected pad's identity is written.
        title.setText ("PAD " + juce::String (selected + 1)
                       + (fileName.isEmpty() ? juce::String (" - no sample") : " - " + fileName),
                       juce::dontSendNotification);
        // item 44: the waveform follows the selected pad
        waveView.setData (engine.getPadSampleData (selected), pp.startFrac, pp.endFrac);
        // item 101: and the envelope lane shows the pad's points (or
        // the implicit flat default when the pad has none yet)
        envView.setPoints (pp.envPoints);
    }

    void pushEditor()
    {
        boombap::MpcPads::PadParams pp;
        pp.startFrac = (float) startRow.slider.getValue() / 100.0f;
        pp.endFrac = (float) endRow.slider.getValue() / 100.0f;
        pp.tuneSemitones = (float) tuneRow.slider.getValue();
        pp.gain = (float) gainRow.slider.getValue();
        pp.velSens = (float) velRow.slider.getValue();
        pp.reverse = reverseToggle.getToggleState();
        pp.muteGroup = std::max (0, muteGroupBox.getSelectedId() - 1);
        pp.envPoints = envView.pointsToStore(); // item 101
        engine.setPadParams (selected, pp);
        waveView.setWindow (pp.startFrac, pp.endFrac); // the trim follows the sliders
    }

    // ---- item 100: pad -> pad drags ----
    void beginPadDrag (int pad, juce::Point<int> screenPos)
    {
        if (! engine.padHasSample (pad)) { dragSourcePad = -1; return; }
        dragSourcePad = pad;
        dragGhost.setText (displayName (pad));
        dragGhost.showAt (screenPos);
        movePadDrag (screenPos);
    }

    void movePadDrag (juce::Point<int> screenPos)
    {
        if (dragSourcePad < 0) return;
        dragGhost.moveTo (screenPos);
        const int hov = padAtScreenPosition (screenPos);
        if (hov != dragHoverPad)
        {
            dragHoverPad = hov;
            for (int i = 0; i < 16; ++i)
                pads[i].setDragTarget (i == dragHoverPad && dragHoverPad != dragSourcePad);
        }
    }

    void endPadDrag (juce::Point<int> screenPos)
    {
        if (dragSourcePad >= 0)
        {
            const int target = padAtScreenPosition (screenPos);
            if (target >= 0 && target != dragSourcePad && onPadCopyRequest)
                onPadCopyRequest (dragSourcePad, target);
        }
        dragSourcePad = -1;
        dragHoverPad = -1;
        dragGhost.hideGhost();
        setDragHighlightPad (-1);
    }

    void timerCallback() override
    {
        for (auto& p : pads) p.decayFlash();
        nameStrip.decayFlashes();
        waveView.setActive (pads[selected].isFlashing());
        // Item 112b: the bank meter watches the pad bus - after item
        // 109 that bus carries pad sound only, so the meter is honest.
        bankMeter.setLevel (juce::jmax (engine.peekChannelPeak (boombap::BoomBapEngine::kPadBusChannel, false),
                                        engine.peekChannelPeak (boombap::BoomBapEngine::kPadBusChannel, true)));
    }

    boombap::BoomBapEngine& engine;
    LoadCallback onLoad;
    NameCallback nameFor;

    PadButton pads[16];
    int selected = 0;

    PadNameStrip nameStrip;
    SegmentedLevelMeter bankMeter; // item 112b
    bool dockedMode = false;       // item 111a
    juce::Label title, muteLabel;
    juce::TextEditor guidance;
    // The item-100 guidance paragraph (measured in resized() to size
    // its wrapping editor; the same words the constructor sets).
    inline static const juce::String guidanceText =
        "Click a pad to play it. The pads hold their OWN sounds - the sequencer's rows "
        "hold theirs (Round 6, item 109); load a pad here and it changes nothing else. "
        "While the beat plays, hitting a pad also writes its step into the pattern. "
        "Drag a pad onto another pad - or drag from the sample waveform - to copy a "
        "sample across. Right-click a pad to clear it.";
    PadWaveView waveView;
    PadEnvelopeView envView;
    Row startRow, endRow, tuneRow, gainRow, velRow;
    juce::ToggleButton reverseToggle { "Reverse" };
    juce::ComboBox muteGroupBox;
    juce::ComboBox kitBox; // bank build: the factory-kit selector

    // item 100 drag state (pad -> pad; the waveform panel's drag is
    // the owner's, and lands through padAtScreenPosition just the same)
    SampleDragGhost dragGhost;
    int dragSourcePad = -1, dragHoverPad = -1;
};

// The pad bank lives in its own window; closing it just hides it.
class PadWindow : public juce::DocumentWindow
{
public:
    PadWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (700, 420, 10000, 10000);
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
