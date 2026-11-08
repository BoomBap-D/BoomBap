#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include <functional>

// One MPC-style pad: number top-left, sample name along the bottom,
// amber outline when selected, a flash of amber when it sounds.
class PadButton : public juce::Component
{
public:
    std::function<void (int)> onPress;

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

    void triggerFlash() { flash = 1.0f; repaint(); }

    void decayFlash()
    {
        if (flash > 0.0f) { flash = std::max (0.0f, flash - 0.09f); repaint(); }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onPress) onPress (padIndex);
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
        g.setColour (selected ? boombap::ui::accent() : boombap::ui::border().withAlpha (0.7f));
        g.drawRoundedRectangle (b, 10.0f, selected ? 1.6f : 1.0f);

        g.setColour (boombap::ui::muted());
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (juce::String (padIndex + 1), 9, 6, 30, 14, juce::Justification::left);

        g.setColour (hasSample ? boombap::ui::text().withAlpha (0.85f)
                               : boombap::ui::muted().withAlpha (0.55f));
        g.setFont (11.0f);
        g.drawText (hasSample ? label : juce::String ("empty"),
                    9, getHeight() - 21, getWidth() - 18, 14, juce::Justification::left);
    }

private:
    int padIndex = 0;
    bool hasSample = false, selected = false;
    float flash = 0.0f;
    juce::String label;
};

// The pad bank: a 4x4 pad grid plus an editor for the selected pad
// (load, chop start/end, tune, gain, velocity sensitivity, reverse, mute group).
// Pads 1-3 are the step sequencer's sample rows; pads 4-16 are live pads.
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
            addAndMakeVisible (pads[i]);
        }

        title.setFont (juce::Font (16.0f, juce::Font::bold));
        addAndMakeVisible (title);
        fileLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (fileLabel);

        loadButton.onClick = [this] { if (onLoad) onLoad (selected); };
        addAndMakeVisible (loadButton);

        setupRow (startRow, "Start %", 0.0, 97.0, 0.5, 0.0);
        setupRow (endRow, "End %", 3.0, 100.0, 0.5, 100.0);
        setupRow (tuneRow, "Tune st", -24.0, 24.0, 0.1, 0.0);
        setupRow (gainRow, "Gain", 0.0, 2.0, 0.01, 1.0);
        setupRow (velRow, "Vel sens", 0.0, 1.0, 0.01, 1.0);

        reverseToggle.onClick = [this] { pushEditor(); };
        addAndMakeVisible (reverseToggle);

        muteLabel.setText ("Mute group", juce::dontSendNotification);
        addAndMakeVisible (muteLabel);
        muteGroupBox.addItem ("None", 1);
        for (int gnum = 1; gnum <= 4; ++gnum) muteGroupBox.addItem (juce::String (gnum), gnum + 1);
        muteGroupBox.onChange = [this] { pushEditor(); };
        addAndMakeVisible (muteGroupBox);

        hint.setText ("Click a pad to play it. Pads 1-3 are the sequencer's sample rows - same samples, "
                      "same settings. Pads 4-16 are live pads, played by clicking.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (hint);

        refreshAll();
        selectPad (0, false);
        startTimerHz (30);
    }

    ~PadBankComponent() override { stopTimer(); }

    void refreshPad (int pad)
    {
        pads[pad].setPad (pad, engine.padHasSample (pad), displayName (pad));
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
            pads[pad].triggerFlash();
    }

    void selectPad (int pad, bool trigger)
    {
        selected = pad;
        for (int i = 0; i < 16; ++i) pads[i].setSelected (i == pad);
        pullEditor();
        if (trigger)
        {
            engine.triggerPad (pad, 1.0f);
            pads[pad].triggerFlash();
        }
    }

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
        panel (8, 8, 456, 396);                       // pad grid + hint
        panel (472, 8, getWidth() - 480, 352);        // editor
    }

    void resized() override
    {
        for (int i = 0; i < 16; ++i)
            pads[i].setBounds (16 + (i % 4) * 112, 16 + (i / 4) * 82, 104, 74);
        hint.setBounds (16, 352, 440, 44);

        const int ex = 484;
        title.setBounds (ex, 16, 264, 24);
        fileLabel.setBounds (ex, 42, 264, 20);
        loadButton.setBounds (ex, 66, 150, 26);

        Row* rowsArr[] = { &startRow, &endRow, &tuneRow, &gainRow, &velRow };
        for (int i = 0; i < 5; ++i)
        {
            rowsArr[i]->label.setBounds (ex, 108 + i * 36, 64, 24);
            rowsArr[i]->slider.setBounds (ex + 66, 108 + i * 36, juce::jmax (120, getWidth() - ex - 84), 24);
        }
        reverseToggle.setBounds (ex, 292, 130, 26);
        muteLabel.setBounds (ex, 326, 84, 24);
        muteGroupBox.setBounds (ex + 88, 326, 110, 24);
    }

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
    };

    void setupRow (Row& row, const char* name, double lo, double hi, double step, double def)
    {
        row.label.setText (name, juce::dontSendNotification);
        addAndMakeVisible (row.label);
        row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        row.slider.setRange (lo, hi, step);
        row.slider.setDoubleClickReturnValue (true, def); // Pass 1: double-click reset
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
        title.setText ("PAD " + juce::String (selected + 1), juce::dontSendNotification);
        const juce::String n = nameFor ? nameFor (selected) : juce::String();
        fileLabel.setText (n.isEmpty() ? "No sample loaded" : n, juce::dontSendNotification);
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
        engine.setPadParams (selected, pp);
    }

    void timerCallback() override
    {
        for (auto& p : pads) p.decayFlash();
    }

    boombap::BoomBapEngine& engine;
    LoadCallback onLoad;
    NameCallback nameFor;

    PadButton pads[16];
    int selected = 0;

    juce::Label title, fileLabel, muteLabel, hint;
    juce::TextButton loadButton { "Load sample..." };
    Row startRow, endRow, tuneRow, gainRow, velRow;
    juce::ToggleButton reverseToggle { "Reverse" };
    juce::ComboBox muteGroupBox;
};

// The pad bank lives in its own window; closing it just hides it.
class PadWindow : public juce::DocumentWindow
{
public:
    PadWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (700, 420, 10000, 10000);
    }

    void closeButtonPressed() override { setVisible (false); }
};
