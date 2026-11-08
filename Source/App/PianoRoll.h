#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
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
            for (int s = 0; s < boombap::BoomBapEngine::kSteps; ++s) setStepAt (s, 0);
            repaint();
        };
        addAndMakeVisible (clearButton);

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
        if (bound >= boombap::BoomBapEngine::Sample1)
            engine.setMelodySource (bound - boombap::BoomBapEngine::Sample1 + 1);
        else if (bound == boombap::BoomBapEngine::Kick)
            engine.setMelodySource (0);
        refreshTitle();
        repaint();
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
            g.setFont (juce::Font (juce::jmin (15.0f, geo.cellH * 0.72f),
                                   semitone == 0 ? juce::Font::bold : juce::Font::plain));
            g.drawText (semitone == 0 ? juce::String ("root")
                                      : (semitone > 0 ? "+" : "") + juce::String (semitone),
                        2, (int) y, geo.labelW - 6, (int) geo.cellH, juce::Justification::centredRight);
        }

        // beat separators + playhead
        for (int s = 0; s <= kCols; s += 4)
        {
            const float x = geo.x + s * (geo.cellW + geo.gap) - geo.gap;
            g.setColour (border().withAlpha (0.8f));
            g.fillRect (x, geo.y, 1.0f, geo.h);
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

    void resized() override
    {
        titleLabel.setBounds (16, 12, 420, 26);
        clearButton.setBounds (getWidth() - 136, 12, 120, 26);
        hint.setBounds (16, getHeight() - 30, getWidth() - 32, 22);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto cell = cellAt (e.x, e.y);
        if (cell.x < 0) return;
        const int value = (12 - cell.y) + 25;
        erasing = stepAt (cell.x) == value;
        applyAt (cell.x, cell.y);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto cell = cellAt (e.x, e.y);
        if (cell.x < 0) return;
        applyAt (cell.x, cell.y);
    }

private:
    static constexpr int kCols = 16, kRows = 25;

    struct Geom
    {
        float x = 0, y = 0, w = 0, h = 0, cellW = 0, cellH = 0, gap = 2.0f;
        int labelW = 46;
    };

    // The grid fills whatever the window gives it (item 30).
    Geom gridGeom() const
    {
        Geom geo;
        geo.labelW = 46;
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
        else if (bound >= boombap::BoomBapEngine::Sample1)
            lane = juce::String (boombap::BoomBapEngine::trackName (bound)) + " melody - pitched pad";
        else
            lane = juce::String (boombap::BoomBapEngine::trackName (bound)) + " - the kick carries the pitched melody";
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
    juce::Label titleLabel, hint;
    juce::TextButton clearButton { "Clear notes" };
    int bound = 0;
    bool erasing = false;
    int lastPlayStep = -2;
};

// The piano roll lives in its own window; closing it just hides it.
class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (560, 420, 10000, 10000);
    }

    void closeButtonPressed() override { setVisible (false); }
};
