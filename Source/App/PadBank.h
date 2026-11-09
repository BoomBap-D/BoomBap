#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
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

    bool isFlashing() const { return flash > 0.05f; }

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

        // Fix Pass 3 (item 71): the tiny per-pad captions are GONE - a
        // pad carries only its number, bigger. The selected pad's name
        // and state live in the editor's one readable info strip.
        g.setColour (selected ? boombap::ui::accentHi() : boombap::ui::muted());
        g.setFont (juce::Font (15.0f, juce::Font::bold));
        g.drawText (juce::String (padIndex + 1), 10, 7, 34, 18, juce::Justification::left);
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

        // Fix Pass 3 (items 71/74): the editor header is ONE compact
        // label now. The old stack (title + file line + LOAD SAMPLE
        // button) is gone - the waveform below is the load surface, and
        // the space the stack took goes to the waveform.
        title.setFont (juce::Font (15.0f, juce::Font::bold));
        addAndMakeVisible (title);

        // Item 71: the ONE readable info strip for the selected pad,
        // under the effects/gain controls.
        infoStrip.setFont (juce::Font (13.5f, juce::Font::bold));
        infoStrip.setColour (juce::Label::textColourId, boombap::ui::text());
        addAndMakeVisible (infoStrip);

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

        setupRow (startRow, "Start %", 0.0, 97.0, 0.5, 0.0);
        setupRow (endRow, "End %", 3.0, 100.0, 0.5, 100.0);
        setupRow (tuneRow, "Tune st", -24.0, 24.0, 0.1, 0.0);
        setupRow (gainRow, "Gain", 0.0, 2.0, 0.01, 1.0);
        setupRow (velRow, "Vel sens", 0.0, 1.0, 0.01, 1.0);

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

        hint.setText ("Click a pad to play it. Pads 1-3 are the sequencer's sample rows - same samples, "
                      "same settings; while the beat plays, hitting one also writes its step into the "
                      "pattern. Pads 4-16 are live pads, played by clicking.",
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
        panel (8, 8, lay.gridRight + 8, getHeight() - 16);                    // pad grid + hint
        panel (lay.editorX - 12, 8, getWidth() - lay.editorX + 4, getHeight() - 16); // editor
    }

    void resized() override
    {
        // Item 30: the grid grows with the window (pads keep their
        // proportions); the editor's sliders/faders take the rest of the
        // width, so every control scales with the window too.
        const auto lay = layout();
        for (int i = 0; i < 16; ++i)
            pads[i].setBounds (16 + (i % 4) * (lay.padW + 8), 16 + (i / 4) * (lay.padH + 8),
                               lay.padW, lay.padH);
        hint.setBounds (16, 16 + 4 * (lay.padH + 8) + 6, juce::jmax (200, lay.gridRight - 16), 44);

        const int ex = lay.editorX;
        const int colW = juce::jmax (200, getWidth() - ex - 16);
        title.setBounds (ex, 12, colW, 22);

        // Fix Pass 3 (items 71/74): the header stack's height and the
        // five long sliders' height both went to the waveform - the
        // waveform is the pad workstation now, not a strip under a form.
        const int waveH = juce::jlimit (64, 380, getHeight() - 252);
        waveView.setBounds (ex, 40, colW, waveH);

        // Item 72: one row of five rotary knobs under the waveform.
        const int knobRowY = 40 + waveH + 10;
        const int cellW = colW / 5;
        const int knobSize = juce::jlimit (54, 84, cellW - 14);
        Row* rowsArr[] = { &startRow, &endRow, &tuneRow, &gainRow, &velRow };
        for (int i = 0; i < 5; ++i)
        {
            const int cellX = ex + i * cellW;
            rowsArr[i]->label.setBounds (cellX, knobRowY, cellW, 14);
            rowsArr[i]->slider.setBounds (cellX + (cellW - knobSize) / 2, knobRowY + 16,
                                          knobSize, knobSize + 16);
        }
        const int footY = knobRowY + 16 + knobSize + 16 + 10;
        reverseToggle.setBounds (ex, footY, 130, 26);
        muteLabel.setBounds (ex + 142, footY, 84, 24);
        muteGroupBox.setBounds (ex + 230, footY, 110, 24);
        // Item 71: the one info strip, under the effects/gain area.
        infoStrip.setBounds (ex, footY + 36, colW, 24);
    }

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
    };

    struct Layout
    {
        int padW = 104, padH = 74, gridRight = 456, editorX = 484;
    };

    Layout layout() const
    {
        Layout lay;
        const int availW = juce::jmax (320, (getWidth() * 54) / 100 - 24);
        const int availH = juce::jmax (240, getHeight() - 104); // item 71: the pads take the freed room
        const int cellW = (availW - 24) / 4, cellH = (availH - 24) / 4;
        lay.padW = juce::jmin (cellW, (int) (cellH * 1.4f));
        lay.padH = (int) (lay.padW / 1.4f);
        lay.gridRight = 16 + 4 * lay.padW + 3 * 8;
        lay.editorX = lay.gridRight + 28;
        return lay;
    }

    void setupRow (Row& row, const char* name, double lo, double hi, double step, double def)
    {
        row.label.setText (name, juce::dontSendNotification);
        row.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (row.label);
        // Fix Pass 3 (item 72): rotary KNOBS, in the Station's exact
        // grammar (RotaryHorizontalVerticalDrag + the value box below) -
        // the long horizontal sliders wasted the editor's real estate.
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
        // Items 71/74: the compact header label and the ONE info strip
        // carry the selected pad's identity - the pads themselves don't.
        title.setText ("PAD " + juce::String (selected + 1)
                       + (fileName.isEmpty() ? juce::String (" - no sample") : " - " + fileName),
                       juce::dontSendNotification);
        infoStrip.setText ("PAD " + juce::String (selected + 1) + " - "
                           + (fileName.isEmpty()
                                ? juce::String ("empty - click the waveform above to load a sample")
                                : fileName + " - loaded, ready to play"),
                           juce::dontSendNotification);
        // item 44: the waveform follows the selected pad
        waveView.setData (engine.getPadSampleData (selected), pp.startFrac, pp.endFrac);
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
        waveView.setWindow (pp.startFrac, pp.endFrac); // the trim follows the sliders
    }

    void timerCallback() override
    {
        for (auto& p : pads) p.decayFlash();
        waveView.setActive (pads[selected].isFlashing());
    }

    boombap::BoomBapEngine& engine;
    LoadCallback onLoad;
    NameCallback nameFor;

    PadButton pads[16];
    int selected = 0;

    juce::Label title, infoStrip, muteLabel, hint;
    PadWaveView waveView;
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
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (700, 420, 10000, 10000);
    }

    void closeButtonPressed() override { setVisible (false); }
};
