#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"

// Piano roll for BoomBap's melody lanes: one monophonic line over the 16 steps
// per sound. Rows are semitone offsets from the chosen sound's root pitch
// (+12 at the top to -12 at the bottom, two octaves); the sound is the kick,
// any of the 16 pads, the synth (root C3) or the piano (root C4). Each sound
// has its own lane per pattern, so they can all play at once. Notes swing
// with the pattern and are saved in the project file.
class PianoRollComponent : public juce::Component, private juce::Timer
{
public:
    explicit PianoRollComponent (boombap::BoomBapEngine& eng) : engine (eng)
    {
        sourceLabel.setText ("Sound", juce::dontSendNotification);
        addAndMakeVisible (sourceLabel);

        sourceBox.addItem ("Kick (808)", 1);
        for (int i = 0; i < 16; ++i)
            sourceBox.addItem ("Pad " + juce::String (i + 1), i + 2);
        sourceBox.addItem ("Synth", 18);
        sourceBox.addItem ("Piano", 19);
        view = engine.getMelodySource();
        sourceBox.setSelectedId (view + 1, juce::dontSendNotification);
        sourceBox.onChange = [this]
        {
            view = sourceBox.getSelectedId() - 1;
            if (view >= 0 && view <= 16) engine.setMelodySource (view);
            repaint();
        };
        addAndMakeVisible (sourceBox);

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

        startTimerHz (30);
    }

    ~PianoRollComponent() override { stopTimer(); }

    void refreshFromEngine()
    {
        // The synth/piano views are the player's own choice, not project data;
        // only the kick/pad lane follows the project's saved sound.
        if (view <= 16)
        {
            view = engine.getMelodySource();
            sourceBox.setSelectedId (view + 1, juce::dontSendNotification);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());

        const int playStep = engine.isPlaying() ? engine.currentStep() : -1;

        for (int row = 0; row < kRows; ++row)
        {
            const int semitone = 12 - row;
            const int y = kGridY + row * (kCellH + kGap);
            const int pitchClass = ((semitone % 12) + 12) % 12;
            const bool blackKey = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                               || pitchClass == 8 || pitchClass == 10;

            g.setColour (semitone == 0 ? accent().withAlpha (0.13f)
                                       : blackKey ? panel().darker (0.3f) : panelAlt());
            g.fillRect (kGridX, y, kCols * (kCellW + kGap) - kGap, kCellH);

            g.setColour (semitone == 0 ? accent() : muted().withAlpha (0.75f));
            g.setFont (juce::Font (9.5f, semitone == 0 ? juce::Font::bold : juce::Font::plain));
            g.drawText (semitone == 0 ? juce::String ("root")
                                      : (semitone > 0 ? "+" : "") + juce::String (semitone),
                        2, y, kGridX - 6, kCellH, juce::Justification::centredRight);
        }

        // beat separators + playhead
        for (int s = 0; s <= kCols; s += 4)
        {
            const int x = kGridX + s * (kCellW + kGap) - kGap;
            g.setColour (border().withAlpha (0.8f));
            g.fillRect (x, kGridY, 1, kRows * (kCellH + kGap) - kGap);
        }
        if (playStep >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (kGridX + playStep * (kCellW + kGap), kGridY,
                        kCellW, kRows * (kCellH + kGap) - kGap);
        }

        // notes
        for (int s = 0; s < kCols; ++s)
        {
            const int v = stepAt (s);
            if (v == 0) continue;
            const int row = 12 - (v - 25);
            juce::Rectangle<float> cell ((float) (kGridX + s * (kCellW + kGap)),
                                         (float) (kGridY + row * (kCellH + kGap)),
                                         (float) kCellW, (float) kCellH);
            g.setColour (s == playStep ? accentHi() : accent());
            g.fillRoundedRectangle (cell, 3.0f);
        }
    }

    void resized() override
    {
        sourceLabel.setBounds (16, 14, 48, 24);
        sourceBox.setBounds (68, 14, 180, 24);
        clearButton.setBounds (260, 14, 110, 24);
        hint.setBounds (16, kGridY + kRows * (kCellH + kGap) + 8, getWidth() - 32, 20);
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
    static constexpr int kCellW = 36, kCellH = 15, kGap = 2;
    static constexpr int kGridX = 46, kGridY = 52;

    juce::Point<int> cellAt (int x, int y) const
    {
        const int step = (x - kGridX) / (kCellW + kGap);
        const int row = (y - kGridY) / (kCellH + kGap);
        if (step < 0 || step >= kCols || row < 0 || row >= kRows
            || x < kGridX || y < kGridY
            || (x - kGridX) % (kCellW + kGap) >= kCellW
            || (y - kGridY) % (kCellH + kGap) >= kCellH)
            return { -1, -1 };
        return { step, row };
    }

    void applyAt (int step, int row)
    {
        const int value = (12 - row) + 25;
        setStepAt (step, erasing ? 0 : value);
        repaint();
    }

    // The lane being edited: view 0..16 is the kick/pad melody lane (and sets
    // its sound), 17 is the synth lane, 18 is the piano lane.
    int stepAt (int s) const
    {
        if (view == 17) return engine.getSynthStep (s);
        if (view == 18) return engine.getPianoStep (s);
        return engine.getMelodyStep (s);
    }

    void setStepAt (int s, int v)
    {
        if (view == 17) { engine.setSynthStep (s, v); return; }
        if (view == 18) { engine.setPianoStep (s, v); return; }
        engine.setMelodyStep (s, v);
    }

    void timerCallback() override
    {
        const int s = engine.isPlaying() ? engine.currentStep() : -1;
        if (s != lastPlayStep) { lastPlayStep = s; repaint(); }
    }

    boombap::BoomBapEngine& engine;
    juce::Label sourceLabel, hint;
    juce::ComboBox sourceBox;
    juce::TextButton clearButton { "Clear notes" };
    int view = 0;
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
        setResizeLimits (716, 566, 10000, 10000);
    }

    void closeButtonPressed() override { setVisible (false); }
};
