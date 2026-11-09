#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"

// Piano roll for BoomBap's melody lanes: one monophonic line over the 16 steps
// per sound. Rows are semitone offsets from the bound sound's root pitch
// (+12 at the top to -12 at the bottom, two octaves).
//
// Fix pass item 28: the roll is BOUND to a track. It is summoned from a
// track's own ROLL button (or an instrument's), and its title, its lane and
// its sound all belong to that track - there is no global sound hunt.
// Bindings: the kick row and the three sample rows edit the pitched melody
// lane (with that row's sound chosen for them); the synth and piano edit
// their own lanes.
//
// Fix pass item 30: the grid SCALES with the window - cells, row labels and
// click targets all grow, so the notes stay legible at any window size.
//
// Fix pass 2 item 41: the roll opens for EVERY channel. Pitched lanes
// (kick melody, sample rows, synth, piano) keep the note grid; drum and
// sample rows ALSO get a STEPS lane - the row's own pattern on a single
// pitch, click to cycle a step's velocity - and a source selector names
// whether you are looking at the TRACK's steps or its PAD's melody. Rows
// with no pitched voice (added tracks) show their steps only. The grid
// runs the full 32 steps / two bars now, and row labels hold a readable
// floor size instead of squashing with the window.
class PianoRollComponent : public juce::Component, private juce::Timer
{
public:
    // Binding ids: a step-sequencer track index (0..7), or one of these:
    static constexpr int kBindSynth = -2;
    static constexpr int kBindPiano = -3;

    explicit PianoRollComponent (boombap::BoomBapEngine& eng) : engine (eng)
    {
        titleLabel.setFont (juce::Font (15.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        addAndMakeVisible (titleLabel);

        clearButton.onClick = [this]
        {
            selectBoundChannel();
            if (onBeforeChange) onBeforeChange();
            if (stepsMode && bound >= 0)
            {
                for (int s = 0; s < boombap::BoomBapEngine::kSteps; ++s) engine.setStep (bound, s, 0);
            }
            else
            {
                for (int s = 0; s < boombap::BoomBapEngine::kSteps; ++s) setStepAt (s, 0);
            }
            if (onAfterChange) onAfterChange();
            repaint();
        };
        addAndMakeVisible (clearButton);

        // item 41: the source selector - this track's steps, or its pad's
        // pitched melody lane.
        modeToggle.onClick = [this]
        {
            stepsMode = ! stepsMode;
            updateModeUi();
            repaint();
        };
        addAndMakeVisible (modeToggle);
        sourceLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (sourceLabel);

        hint.setText ("Click a cell to place a note - one per step - and drag to draw. "
                      "Click a note again to remove it. Rows are semitones from the sound's own pitch.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (hint);

        bindToTrack (0); // the kick's melody lane until a track summons it
        startTimerHz (30);
    }

    ~PianoRollComponent() override { stopTimer(); }

    int boundTrack() const { return bound; }

    juce::String bindingTitle() const
    {
        if (bound == kBindSynth) return "Piano Roll - Synth";
        if (bound == kBindPiano) return "Piano Roll - Piano";
        return "Piano Roll - " + juce::String (boombap::BoomBapEngine::trackName (bound));
    }

    // Summons the roll for one track. Sample rows choose their own pad as
    // the melody lane's sound, so what opens is already the right lane.
    void bindToTrack (int track)
    {
        bound = track;
        if (bound >= boombap::BoomBapEngine::Sample1
            && bound < boombap::BoomBapEngine::kFirstOwnRow)
            engine.setMelodySource (bound - boombap::BoomBapEngine::Sample1 + 1);
        else if (bound == boombap::BoomBapEngine::Kick)
            engine.setMelodySource (0);
        // Pitched rows open on their melody (the behaviour D knows); the
        // rest open on their own steps. The selector flips either way.
        stepsMode = ! (bound < 0 || bound == boombap::BoomBapEngine::Kick
                       || (bound >= boombap::BoomBapEngine::Sample1 && bound <= boombap::BoomBapEngine::Sample3));
        updateModeUi();
        refreshTitle();
        repaint();
    }

    // Can this binding show a pitched melody lane at all?
    bool supportsMelody() const
    {
        if (bound < 0) return true;                                    // synth / piano
        if (bound == boombap::BoomBapEngine::Kick) return true;        // the pitched 808
        return bound >= boombap::BoomBapEngine::Sample1
            && bound < boombap::BoomBapEngine::kFirstOwnRow;           // any pad row
    }

    void updateModeUi()
    {
        if (! supportsMelody()) stepsMode = true;
        modeToggle.setEnabled (supportsMelody());
        modeToggle.setButtonText (stepsMode ? "SHOW: TRACK STEPS" : "SHOW: PAD MELODY");
        juce::String src;
        if (bound < 0)
            src = bound == kBindSynth ? "Source: the synth's own lane" : "Source: the piano's own lane";
        else if (stepsMode)
            src = "Source: TRACK steps - " + juce::String (boombap::BoomBapEngine::trackName (bound));
        else if (bound == boombap::BoomBapEngine::Kick)
            src = "Source: PAD melody - the kick's pitched 808 lane";
        else
            src = "Source: PAD melody - pad " + juce::String (bound - boombap::BoomBapEngine::Sample1 + 1)
                + " (row " + juce::String (boombap::BoomBapEngine::trackName (bound)) + ")";
        sourceLabel.setText (src, juce::dontSendNotification);
        hint.setText (stepsMode
            ? "The row's own pattern on one pitch. Click a step to cycle its velocity: off, on, accent. Right-click clears."
            : "Click a cell to place a note - one per step - and drag to draw. "
              "Click a note again to remove it. Rows are semitones from the sound's own pitch.",
            juce::dontSendNotification);
    }

    void refreshFromEngine()
    {
        refreshTitle();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());

        if (stepsMode && bound >= 0) { paintSteps (g); return; }

        const auto geo = gridGeom();
        const int playStep = engine.isPlaying() ? engine.currentStep() : -1;

        for (int row = 0; row < kRows; ++row)
        {
            const int semitone = 12 - row;
            const float y = geo.y + row * (geo.cellH + geo.gap);
            const int pitchClass = ((semitone % 12) + 12) % 12;
            const bool blackKey = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                               || pitchClass == 8 || pitchClass == 10;

            g.setColour (semitone == 0 ? accent().withAlpha (0.13f)
                                       : blackKey ? panel().darker (0.3f) : panelAlt());
            g.fillRect (geo.x, y, geo.w, geo.cellH);

            g.setColour (semitone == 0 ? accent() : muted().withAlpha (0.75f));
            // item 41: a floor under the label size - words stay legible
            // instead of squashing to fit a short window.
            g.setFont (juce::Font (juce::jlimit (10.0f, 15.0f, geo.cellH * 0.72f),
                                   semitone == 0 ? juce::Font::bold : juce::Font::plain));
            g.drawText (semitone == 0 ? juce::String ("root")
                                      : (semitone > 0 ? "+" : "") + juce::String (semitone),
                        2, (int) y, geo.labelW - 6, (int) geo.cellH, juce::Justification::centredRight);
        }

        // beat separators + playhead (bar 2 starts at step 17 - marked)
        for (int s = 0; s <= kCols; s += 4)
        {
            const float x = geo.x + s * (geo.cellW + geo.gap) - geo.gap;
            g.setColour (s == 16 ? accent().withAlpha (0.85f) : border().withAlpha (0.8f));
            g.fillRect (x, geo.y, s == 16 ? 2.0f : 1.0f, geo.h);
        }
        if (playStep >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (geo.x + playStep * (geo.cellW + geo.gap), geo.y, geo.cellW, geo.h);
        }

        // notes
        for (int s = 0; s < kCols; ++s)
        {
            const int v = stepAt (s);
            if (v == 0) continue;
            const int row = 12 - (v - 25);
            juce::Rectangle<float> cell (geo.x + s * (geo.cellW + geo.gap),
                                         geo.y + row * (geo.cellH + geo.gap),
                                         geo.cellW, geo.cellH);
            g.setColour (s == playStep ? accentHi() : accent());
            g.fillRoundedRectangle (cell, 3.0f);
        }
    }

    // The STEPS lane (item 41): the bound row's own pattern on one pitch,
    // shaded exactly like the sequencer's step buttons.
    void paintSteps (juce::Graphics& g)
    {
        using namespace boombap::ui;
        const auto geo = gridGeom();
        const float laneH = juce::jmin (geo.h, 84.0f);
        const float laneY = geo.y + (geo.h - laneH) * 0.5f;
        const int playStep = engine.isPlaying() ? engine.currentStep() : -1;

        g.setColour (muted());
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText ("STEPS", 2, (int) laneY, geo.labelW - 6, (int) laneH, juce::Justification::centredRight);

        for (int s = 0; s < kCols; ++s)
        {
            const float x = geo.x + s * (geo.cellW + geo.gap);
            juce::Rectangle<float> cell (x, laneY, geo.cellW, laneH);
            const int v = engine.getStep (bound, s);
            juce::Colour fill = ((s / 4) % 2 == 0) ? juce::Colour (0xff232323) : juce::Colour (0xff1d1d1d);
            if (v >= 110) fill = juce::Colour (0xffffc857);
            else if (v > 0) fill = juce::Colour (0xffe8863a);
            g.setColour (fill);
            g.fillRoundedRectangle (cell, 4.0f);
            if (s == playStep)
            {
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawRoundedRectangle (cell, 4.0f, 2.0f);
            }
            if (v > 0)
            {
                g.setColour (juce::Colour (0xff14100a));
                g.setFont (juce::Font (juce::jmin (13.0f, geo.cellW * 0.5f), juce::Font::bold));
                g.drawText (juce::String (v), cell, juce::Justification::centred);
            }
            if (s % 4 == 0)
            {
                g.setColour (s == 16 ? accent() : muted());
                g.setFont (11.0f);
                g.drawText (juce::String (s + 1), (int) x, (int) laneY - 18, 40, 14, juce::Justification::left);
            }
        }
        const float bx = geo.x + 16 * (geo.cellW + geo.gap) - geo.gap;
        g.setColour (accent().withAlpha (0.85f));
        g.fillRect (bx, laneY - 20.0f, 2.0f, laneH + 24.0f);
    }

    // Step index under a point in the STEPS lane, or -1.
    int stepLaneAt (int x, int y) const
    {
        const auto geo = gridGeom();
        const float laneH = juce::jmin (geo.h, 84.0f);
        const float laneY = geo.y + (geo.h - laneH) * 0.5f;
        if ((float) y < laneY || (float) y > laneY + laneH) return -1;
        const float fx = (float) x - geo.x;
        if (fx < 0.0f) return -1;
        const int step = (int) (fx / (geo.cellW + geo.gap));
        if (step < 0 || step >= kCols) return -1;
        if (fx - step * (geo.cellW + geo.gap) >= geo.cellW) return -1;
        return step;
    }

    void resized() override
    {
        titleLabel.setBounds (16, 12, 420, 26);
        sourceLabel.setBounds (452, 12, juce::jmax (120, getWidth() - 452 - 340), 26);
        modeToggle.setBounds (getWidth() - 336, 12, 184, 26);
        clearButton.setBounds (getWidth() - 136, 12, 120, 26);
        hint.setBounds (16, getHeight() - 30, getWidth() - 32, 22);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (stepsMode && bound >= 0)
        {
            const int s = stepLaneAt (e.x, e.y);
            if (s < 0) return;
            selectBoundChannel();
            if (onBeforeChange) onBeforeChange();
            const int v = engine.getStep (bound, s);
            const int next = e.mods.isRightButtonDown() ? 0 : (v == 0 ? 90 : (v < 110 ? 127 : 0));
            engine.setStep (bound, s, next);
            if (onAfterChange) onAfterChange();
            repaint();
            return;
        }
        const auto cell = cellAt (e.x, e.y);
        if (cell.x < 0) return;
        selectBoundChannel();
        if (onBeforeChange) onBeforeChange(); // the stroke is one gesture; mouseUp closes it
        const int value = (12 - cell.y) + 25;
        erasing = stepAt (cell.x) == value;
        applyAt (cell.x, cell.y);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (stepsMode && bound >= 0) return; // steps change per click, not per drag
        const auto cell = cellAt (e.x, e.y);
        if (cell.x < 0) return;
        applyAt (cell.x, cell.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onAfterChange) onAfterChange();
    }

    // Pass 2 (item 38): undo gesture hooks + Station channel selection,
    // wired by the owner after construction.
    std::function<void()> onBeforeChange, onAfterChange;
    std::function<void (int)> onSelectChannel;

private:
    void selectBoundChannel()
    {
        if (! onSelectChannel) return;
        if (bound >= 0) onSelectChannel (bound);
        else if (bound == kBindSynth) onSelectChannel (boombap::BoomBapEngine::kSynthChannel);
        else if (bound == kBindPiano) onSelectChannel (boombap::BoomBapEngine::kPianoChannel);
    }

private:
    static constexpr int kCols = boombap::BoomBapEngine::kSteps, kRows = 25;

    struct Geom
    {
        float x = 0, y = 0, w = 0, h = 0, cellW = 0, cellH = 0, gap = 2.0f;
        int labelW = 46;
    };

    // The grid fills whatever the window gives it (item 30).
    Geom gridGeom() const
    {
        Geom geo;
        geo.labelW = 56;
        geo.x = (float) geo.labelW;
        geo.y = 48.0f;
        geo.w = (float) juce::jmax (160, getWidth() - geo.labelW - 12);
        geo.h = (float) juce::jmax (120, getHeight() - 48 - 42);
        geo.gap = 2.0f;
        geo.cellW = (geo.w - (kCols - 1) * geo.gap) / (float) kCols;
        geo.cellH = (geo.h - (kRows - 1) * geo.gap) / (float) kRows;
        return geo;
    }

    juce::Point<int> cellAt (int x, int y) const
    {
        const auto geo = gridGeom();
        const float fx = (float) x - geo.x, fy = (float) y - geo.y;
        if (fx < 0.0f || fy < 0.0f) return { -1, -1 };
        const int step = (int) (fx / (geo.cellW + geo.gap));
        const int row = (int) (fy / (geo.cellH + geo.gap));
        if (step < 0 || step >= kCols || row < 0 || row >= kRows) return { -1, -1 };
        if (fx - step * (geo.cellW + geo.gap) >= geo.cellW
            || fy - row * (geo.cellH + geo.gap) >= geo.cellH)
            return { -1, -1 };
        return { step, row };
    }

    void applyAt (int step, int row)
    {
        const int value = (12 - row) + 25;
        setStepAt (step, erasing ? 0 : value);
        repaint();
    }

    // The lane being edited follows the binding: synth lane, piano lane, or
    // the pitched melody lane (whose sound the binding already chose).
    int stepAt (int s) const
    {
        if (bound == kBindSynth) return engine.getSynthStep (s);
        if (bound == kBindPiano) return engine.getPianoStep (s);
        return engine.getMelodyStep (s);
    }

    void setStepAt (int s, int v)
    {
        if (bound == kBindSynth) { engine.setSynthStep (s, v); return; }
        if (bound == kBindPiano) { engine.setPianoStep (s, v); return; }
        engine.setMelodyStep (s, v);
    }

    void refreshTitle()
    {
        juce::String lane;
        if (bound == kBindSynth)      lane = "Synth - root C3";
        else if (bound == kBindPiano) lane = "Piano - root C4";
        else if (bound == boombap::BoomBapEngine::Kick) lane = "Kick melody - pitched 808";
        else if (bound >= boombap::BoomBapEngine::Sample1
                 && bound < boombap::BoomBapEngine::kFirstOwnRow)
            lane = juce::String (boombap::BoomBapEngine::trackName (bound)) + " melody - pitched pad";
        else if (bound >= boombap::BoomBapEngine::kFirstOwnRow)
            lane = juce::String (boombap::BoomBapEngine::trackName (bound)) + " steps - this row's own sample";
        else
            lane = juce::String (boombap::BoomBapEngine::trackName (bound)) + " steps";
        const juce::String pat = juce::String::charToString (
            (juce::juce_wchar) ('A' + engine.getEditPattern()));
        titleLabel.setText (lane + "  -  Pattern " + pat, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const int s = engine.isPlaying() ? engine.currentStep() : -1;
        if (s != lastPlayStep) { lastPlayStep = s; repaint(); }
    }

    boombap::BoomBapEngine& engine;
    juce::Label titleLabel, hint, sourceLabel;
    juce::TextButton clearButton { "Clear notes" };
    juce::TextButton modeToggle;
    int bound = 0;
    bool erasing = false;
    bool stepsMode = false;
    int lastPlayStep = -2;
};

// The piano roll lives in its own window; closing it just hides it.
class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (560, 420, 10000, 10000);
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
