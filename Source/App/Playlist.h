#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"

// The playlist: a chain of patterns (up to 32 bars) played in order when song
// mode is on. Sounds are global - the chain only chooses which pattern plays.
// Click a slot to cycle its pattern, click the + slot to append the pattern
// being edited in the main window, right-click a slot to remove it.
// Fix pass item 30: the slots SCALE with the window - letters and click
// targets grow, so the chain stays legible at any size.
class PlaylistComponent : public juce::Component, private juce::Timer
{
public:
    explicit PlaylistComponent (boombap::BoomBapEngine& eng) : engine (eng)
    {
        modeToggle.setToggleState (engine.isSongMode(), juce::dontSendNotification);
        modeToggle.onClick = [this]
        {
            if (onBeforeChange) onBeforeChange();
            engine.setSongMode (modeToggle.getToggleState());
            if (onAfterChange) onAfterChange();
            refreshStatus(); repaint();
        };
        addAndMakeVisible (modeToggle);

        clearButton.onClick = [this]
        {
            if (onBeforeChange) onBeforeChange();
            engine.setSongLength (0);
            if (onAfterChange) onAfterChange();
            refreshStatus(); repaint();
        };
        addAndMakeVisible (clearButton);

        status.setColour (juce::Label::textColourId, boombap::ui::text());
        addAndMakeVisible (status);

        hint.setText ("Click a slot to change its pattern. Click + to add the pattern you are editing. "
                      "Right-click a slot to remove it. In song mode the export is one full pass.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (hint);

        refreshStatus();
        startTimerHz (15);
    }

    ~PlaylistComponent() override { stopTimer(); }

    void refreshFromEngine()
    {
        modeToggle.setToggleState (engine.isSongMode(), juce::dontSendNotification);
        refreshStatus();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());

        const int len = engine.getSongLength();
        const int playPos = (engine.isSongMode() && engine.isPlaying()) ? engine.getPlaySongPos() : -1;

        const float letterSize = juce::jlimit (13.0f, 34.0f, slotRect (0).getHeight() * 0.52f);
        for (int i = 0; i < kSlots; ++i)
        {
            const auto r = slotRect (i);
            if (i < len)
            {
                g.setColour (i == playPos ? accent() : panelAlt());
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (i == playPos ? juce::Colour (0xff14100a) : text());
                g.setFont (juce::Font (letterSize, juce::Font::bold));
                g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + engine.getSongSlot (i))),
                            r, juce::Justification::centred);
            }
            else if (i == len)
            {
                g.setColour (border());
                g.drawRoundedRectangle (r, 5.0f, 1.2f);
                g.setColour (muted());
                g.setFont (juce::Font (letterSize));
                g.drawText ("+", r, juce::Justification::centred);
            }
            else
            {
                g.setColour (muted().withAlpha (0.35f));
                g.fillEllipse (r.getCentreX() - 2.0f, r.getCentreY() - 2.0f, 4.0f, 4.0f);
            }
        }
    }

    void resized() override
    {
        modeToggle.setBounds (16, 12, 120, 26);
        clearButton.setBounds (148, 12, 110, 26);
        status.setBounds (270, 12, juce::jmax (120, getWidth() - 286), 26);
        hint.setBounds (16, getHeight() - 42, getWidth() - 32, 34);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int slot = slotAt (e.x, e.y);
        if (slot < 0) return;
        const int len = engine.getSongLength();
        if (onBeforeChange) onBeforeChange();

        if (e.mods.isRightButtonDown())
        {
            if (slot < len)
            {
                for (int i = slot; i < len - 1; ++i) engine.setSongSlot (i, engine.getSongSlot (i + 1));
                engine.setSongLength (len - 1);
            }
        }
        else if (slot < len)
        {
            engine.setSongSlot (slot, (engine.getSongSlot (slot) + 1) % boombap::BoomBapEngine::kPatterns);
        }
        else if (slot == len)
        {
            engine.setSongSlot (slot, engine.getEditPattern());
            engine.setSongLength (len + 1);
        }
        if (onAfterChange) onAfterChange();
        refreshStatus();
        repaint();
    }

    // Pass 2 (item 38): undo gesture hooks, wired by the owner.
    std::function<void()> onBeforeChange, onAfterChange;

private:
    static constexpr int kSlots = boombap::BoomBapEngine::kSongSlots;

    // Slots tile the area between the header and the hint, scaling with the
    // window (item 30): 16 columns x 2 rows of equal cells with a small gap.
    juce::Rectangle<float> slotRect (int i) const
    {
        const int col = i % 16, row = i / 16;
        const float areaX = 16.0f, areaY = 50.0f;
        const float areaW = (float) juce::jmax (160, getWidth() - 32);
        const float areaH = (float) juce::jmax (72, getHeight() - 50 - 52);
        const float gap = 6.0f;
        const float w = (areaW - 15.0f * gap) / 16.0f;
        const float h = (areaH - gap) / 2.0f;
        return { areaX + col * (w + gap), areaY + row * (h + gap), w, h };
    }

    int slotAt (int x, int y) const
    {
        for (int i = 0; i < kSlots; ++i)
            if (slotRect (i).contains ((float) x, (float) y)) return i;
        return -1;
    }

    void refreshStatus()
    {
        juce::String text;
        const int len = engine.getSongLength();
        if (! engine.isSongMode())
            text = "Pattern mode: the selected pattern loops.";
        else if (len == 0)
            text = "Song mode is on, but the chain is empty - add patterns below.";
        else
        {
            text = "Song: " + juce::String (len) + (len == 1 ? " pattern" : " patterns")
                 + " (" + juce::String (len * 2) + " bars) - ";
            for (int i = 0; i < len; ++i)
            {
                if (i > 0) text += " ";
                text += juce::String::charToString ((juce::juce_wchar) ('A' + engine.getSongSlot (i)));
            }
        }
        if (status.getText() != text) status.setText (text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const int pos = (engine.isSongMode() && engine.isPlaying()) ? engine.getPlaySongPos() : -1;
        if (pos != lastPlayPos) { lastPlayPos = pos; repaint(); }
    }

    boombap::BoomBapEngine& engine;
    juce::ToggleButton modeToggle { "Song mode" };
    juce::TextButton clearButton { "Clear song" };
    juce::Label status, hint;
    int lastPlayPos = -2;
};

// The playlist lives in its own window; closing it just hides it.
class PlaylistWindow : public juce::DocumentWindow
{
public:
    PlaylistWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (520, 240, 10000, 10000);
    }

    void closeButtonPressed() override { setVisible (false); }
};
