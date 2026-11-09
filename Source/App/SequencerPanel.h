#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include "UiComponents.h"

// The STEP SEQUENCER view of the Production Area (items 37/39/41/52):
// the pattern strip (A-H + add-track) over a vertically scrolling list of
// track rows. Every row carries Load (a sample for the row - on a kit row
// it replaces the built-in drum, item 40), ROLL (the piano roll bound to
// that row, item 41), MUTE/SOLO at a size you can actually hit (item 52),
// a level slider, a meter, and 32 step buttons (the pattern is two bars,
// item 48). The row list grows with engine.addTrack() and scrolls - the
// original 8 rows are just where it starts (item 39).
class SequencerPanel : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (int)> onLoadSample; // a row's Load... button
        std::function<void (int)> onRoll;       // a row's ROLL button
        std::function<void (int)> onNameMenu;   // a kit row's name was clicked (item 40)
        std::function<void()> onPatternChanged; // pattern A-H switched (the roll follows)
        std::function<void()> onChange;         // a row was added
        // Pass 2: undo gestures (item 38) + channel selection (the Station follows)
        std::function<void()> onBeforeChange;
        std::function<void()> onAfterChange;
        std::function<void (int)> onSelectRow;
    };

    SequencerPanel (boombap::BoomBapEngine& eng, Callbacks cb)
        : engine (eng), callbacks (std::move (cb))
    {
        patternLabel.setText ("PATTERN", juce::dontSendNotification);
        patternLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        patternLabel.setFont (juce::Font (11.0f, juce::Font::bold));
        addAndMakeVisible (patternLabel);
        for (int i = 0; i < 8; ++i)
        {
            auto& b = patternButtons[(size_t) i];
            b.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
            b.onClick = [this, i]
            {
                engine.setEditPattern (i);
                refreshGrid();
                refreshPatternUi();
                if (callbacks.onPatternChanged) callbacks.onPatternChanged();
            };
            addAndMakeVisible (b);
        }
        addTrackButton.onClick = [this]
        {
            if (callbacks.onBeforeChange) callbacks.onBeforeChange();
            if (engine.addTrack() >= 0)
            {
                layoutRows();
                refreshGrid();
                if (callbacks.onChange) callbacks.onChange();
            }
            if (callbacks.onAfterChange) callbacks.onAfterChange();
        };
        addAndMakeVisible (addTrackButton);

        viewport.setViewedComponent (&gridContent, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (12);
        addAndMakeVisible (viewport);

        for (int t = 0; t < kTracks; ++t)
        {
            Row& row = rows[t];
            gridContent.addAndMakeVisible (row.name);
            row.name.setText (boombap::BoomBapEngine::trackName (t), juce::dontSendNotification);
            row.name.setColour (juce::Label::textColourId, juce::Colours::white);
            row.name.setFont (juce::Font (13.0f, juce::Font::bold));
            // Pass 2: any row interaction selects the row's channel (the
            // Station follows); kit rows also keep their name menu (item 40)
            row.name.addMouseListener (this, false);

            gridContent.addAndMakeVisible (row.load);
            row.load.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onLoadSample) callbacks.onLoadSample (t);
            };

            gridContent.addAndMakeVisible (row.roll);
            row.roll.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onRoll) callbacks.onRoll (t);
            };

            gridContent.addAndMakeVisible (row.mute);
            row.mute.setClickingTogglesState (true);
            row.mute.setColour (juce::TextButton::buttonOnColourId, juce::Colours::indianred);
            row.mute.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onBeforeChange) callbacks.onBeforeChange();
                engine.setTrackMute (t, rows[t].mute.getToggleState());
                if (callbacks.onAfterChange) callbacks.onAfterChange();
            };

            gridContent.addAndMakeVisible (row.solo);
            row.solo.setClickingTogglesState (true);
            row.solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
            row.solo.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onBeforeChange) callbacks.onBeforeChange();
                engine.setTrackSolo (t, rows[t].solo.getToggleState());
                if (callbacks.onAfterChange) callbacks.onAfterChange();
            };

            gridContent.addAndMakeVisible (row.level);
            gridContent.addAndMakeVisible (row.meter);
            row.level.setSliderStyle (juce::Slider::LinearHorizontal);
            row.level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row.level.setRange (0.0, 1.2, 0.01);
            row.level.setValue (0.8, juce::dontSendNotification);
            row.level.setDoubleClickReturnValue (true, 0.8); // double-click reset, app-wide
            row.level.onDragStart = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onBeforeChange) callbacks.onBeforeChange();
            };
            row.level.onDragEnd = [this] { if (callbacks.onAfterChange) callbacks.onAfterChange(); };
            row.level.onValueChange = [this, t] { engine.setTrackGain (t, (float) rows[t].level.getValue()); };

            for (int s = 0; s < kSteps; ++s)
            {
                auto& sb = row.steps[s];
                gridContent.addAndMakeVisible (sb);
                sb.setBeatShade ((s / 4) % 2 == 0);
                sb.onChange = [this, t, s] (int v)
                {
                    if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                    // Fix Pass 3 (item 76): while a swipe is held, the
                    // gesture the press opened stays open - the press's
                    // own begin/end step aside until the release.
                    if (! swipeHeld && callbacks.onBeforeChange) callbacks.onBeforeChange();
                    engine.setStep (t, s, v);
                    if (! swipeHeld && callbacks.onAfterChange) callbacks.onAfterChange();
                };
                // Item 76: left-drag paints steps ON (with the value the
                // press produced: normal, or accent if the press cycled
                // to it); right-drag erases. FL grammar, D's hands.
                sb.onSwipeStart = [this, t, s] (int v)
                {
                    swipeHeld = true;
                    swipeTrack = t;
                    swipeValue = v;
                    lastSwipeStep = s;
                    if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                    if (callbacks.onBeforeChange) callbacks.onBeforeChange();
                };
                sb.onSwipeMove = [this] (juce::Point<float> p) { applySwipe (p); };
                sb.onSwipeEnd = [this]
                {
                    if (! swipeHeld) return;
                    swipeHeld = false;
                    if (callbacks.onAfterChange) callbacks.onAfterChange();
                };
            }
        }
        refreshPatternUi();
        refreshGrid();
    }

    ~SequencerPanel() override
    {
        for (int t = 0; t < kTracks; ++t)
            rows[t].name.removeMouseListener (this);
    }

    void refreshGrid()
    {
        const int n = engine.getNumTracks();
        for (int t = 0; t < n; ++t)
            for (int s = 0; s < kSteps; ++s)
                rows[t].steps[s].setVelocity (engine.getStep (t, s));
    }

    void refreshPatternUi()
    {
        const int cur = engine.getEditPattern();
        for (int i = 0; i < 8; ++i)
        {
            auto& b = patternButtons[(size_t) i];
            const bool sel = i == cur;
            b.setColour (juce::TextButton::buttonColourId, sel ? boombap::ui::accent() : boombap::ui::panelAlt());
            b.setColour (juce::TextButton::textColourOffId, sel ? juce::Colour (0xff14100a) : boombap::ui::text());
        }
    }

    void refreshMixerFromSettings (const boombap::ProjectSettings& s)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            rows[t].mute.setToggleState (s.trackMute[t], juce::dontSendNotification);
            rows[t].solo.setToggleState (s.trackSolo[t], juce::dontSendNotification);
            rows[t].level.setValue (s.trackGain[t], juce::dontSendNotification);
        }
        layoutRows();
        refreshPatternUi();
        refreshGrid();
    }

    void setRowLoadText (int track, const juce::String& text)
    {
        if (track >= 0 && track < kTracks)
            rows[track].load.setButtonText (text);
    }

    // Called by the owner's 30 Hz timer: playhead column + row meters.
    void tick()
    {
        const int n = engine.getNumTracks();
        const int s = engine.currentStep();
        if (s != lastPlayStep)
        {
            lastPlayStep = s;
            for (int t = 0; t < n; ++t)
                for (int st = 0; st < kSteps; ++st)
                    rows[t].steps[st].setPlaying (st == s);
        }
        for (int t = 0; t < n; ++t)
        {
            const float peak = engine.consumeTrackPeak (t);
            meterShown[t] = std::max (peak, meterShown[t] * 0.82f);
            rows[t].meter.setLevel (meterShown[t]);
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.setColour (muted());
        g.setFont (11.0f);
        g.drawText ("Click a step: normal, accent, off.  Drag paints steps on - right-drag erases.  Steps 1-16 = bar 1, 17-32 = bar 2.",
                    getWidth() / 3, 6, getWidth() / 3 + 130, 22, juce::Justification::centredRight);

        // bar/beat numbers over the step columns, aligned with the grid
        g.setFont (12.0f);
        const float cx = contentScaleX;
        for (int s = 0; s < kSteps; s += 4)
        {
            const float x = (float) viewport.getX() + stepX (s) * cx;
            g.setColour (s == 16 ? accent() : muted());
            g.drawText (juce::String (s / 4 + 1) + (s == 16 ? " (bar 2)" : ""),
                        (int) x, 34, 70, 14, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        const float sx = (float) getWidth() / 1544.0f;
        const float sy = (float) getHeight() / 340.0f;
        auto RB = [&] (int x, int y, int w, int h)
        {
            return juce::Rectangle<int> (juce::roundToInt (x * sx), juce::roundToInt (y * sy),
                                         juce::roundToInt (w * sx), juce::roundToInt (h * sy));
        };
        scaleY = sy;
        patternLabel.setBounds (RB (12, 4, 70, 24));
        for (int i = 0; i < 8; ++i)
            patternButtons[(size_t) i].setBounds (RB (88 + i * 40, 4, 36, 24));
        addTrackButton.setBounds (RB (1396, 4, 132, 24));
        viewport.setBounds (RB (8, 50, 1528, 282));
        layoutRows();
    }

    // (Component is a MouseListener; row name clicks arrive here.)
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int t = 0; t < kTracks; ++t)
            if (e.eventComponent == &rows[t].name)
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                // Item 40's kit menu, and (Fix Pass 3, item 69) the
                // user-added rows' menu with Delete track.
                if ((t <= boombap::BoomBapEngine::Clap
                     || t >= boombap::BoomBapEngine::kFirstOwnRow)
                    && callbacks.onNameMenu)
                    callbacks.onNameMenu (t);
                return;
            }
    }

private:
    static constexpr int kTracks = boombap::BoomBapEngine::kTracks;
    static constexpr int kSteps  = boombap::BoomBapEngine::kSteps;

    struct Row
    {
        juce::Label name;
        juce::TextButton load { "Load..." };
        juce::TextButton roll { "ROLL" };
        juce::TextButton mute { "M" };
        juce::TextButton solo { "S" };
        juce::Slider level;
        LevelMeter meter;
        StepButton steps[kSteps];
    };

    // Step column geometry in the content's virtual space (1528 wide):
    // 32 steps of 32 px with a small gap, a wider gap every beat, the
    // meter at the right edge.
    static float stepX (int s) { return 382.0f + (float) s * 33.0f + (float) (s / 4) * 5.0f; }

    // Fix Pass 3 (item 76): paint the swipe value across every step the
    // drag crosses, on the row the press started on. Cells are SET,
    // never toggled - re-entering a cell (above all the starting cell)
    // cannot flip it back, which is the classic paint-drag bug - and
    // the walk from the last cell fills gaps on fast drags.
    void applySwipe (juce::Point<float> p)
    {
        if (! swipeHeld || swipeTrack < 0 || lastRowH <= 0) return;
        const int rowY = swipeTrack * lastRowH;
        if (p.y < (float) rowY || p.y >= (float) (rowY + lastRowH)) return; // the press's own row only
        const float vx = p.x / contentScaleX;
        int s = -1;
        for (int i = 0; i < kSteps; ++i)
            if (vx >= stepX (i) && vx < stepX (i) + 32.0f) { s = i; break; }
        if (s < 0) return;
        const int from = lastSwipeStep;
        const int dir = s >= from ? 1 : -1;
        for (int i = from;; i += dir)
        {
            if (engine.getStep (swipeTrack, i) != swipeValue)
            {
                engine.setStep (swipeTrack, i, swipeValue);
                rows[swipeTrack].steps[i].setVelocity (swipeValue);
            }
            if (i == s) break;
        }
        lastSwipeStep = s;
    }

    void layoutRows()
    {
        const int n = engine.getNumTracks();
        const int rowH = juce::jmax (28, juce::roundToInt (35.0f * scaleY));
        lastRowH = rowH;
        gridContent.setSize (juce::jmax (200, viewport.getWidth()),
                             juce::jmax (viewport.getHeight(), n * rowH));
        contentScaleX = (float) gridContent.getWidth() / 1528.0f;
        const float cx = contentScaleX;
        auto CB = [&] (float x, int y, float w, int h)
        {
            return juce::Rectangle<int> (juce::roundToInt (x * cx), y,
                                         juce::roundToInt (w * cx), h);
        };
        for (int t = 0; t < kTracks; ++t)
        {
            Row& row = rows[t];
            const bool vis = t < n;
            row.name.setVisible (vis);
            row.load.setVisible (vis);
            row.roll.setVisible (vis);
            row.mute.setVisible (vis);
            row.solo.setVisible (vis);
            row.level.setVisible (vis);
            row.meter.setVisible (vis);
            for (int s = 0; s < kSteps; ++s) row.steps[s].setVisible (vis);
            if (! vis) continue;
            const int y = t * rowH;
            const int h = rowH - 5;
            row.name.setBounds (CB (12, y + 2, 88, h));
            row.load.setBounds (CB (104, y + 2, 54, h));
            row.roll.setBounds (CB (162, y + 2, 44, h));
            row.mute.setBounds (CB (210, y + 2, 36, h));
            row.solo.setBounds (CB (250, y + 2, 36, h));
            row.level.setBounds (CB (292, y + 2, 84, h));
            for (int s = 0; s < kSteps; ++s)
                row.steps[s].setBounds (CB (stepX (s), y + 2, 32, h));
            row.meter.setBounds (CB (1476, y + rowH / 2 - 4, 44, 8));
        }
        repaint();
    }

    boombap::BoomBapEngine& engine;
    Callbacks callbacks;

    juce::Label patternLabel;
    std::array<juce::TextButton, 8> patternButtons;
    juce::TextButton addTrackButton { "+ TRACK" };
    juce::Viewport viewport;
    juce::Component gridContent;
    Row rows[kTracks];

    float scaleY = 1.0f, contentScaleX = 1.0f;
    float meterShown[kTracks] = {};
    int lastPlayStep = -2;
    // Item 76 swipe state (see applySwipe).
    bool swipeHeld = false;
    int swipeTrack = -1, swipeValue = 0, lastSwipeStep = -1, lastRowH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerPanel)
};
