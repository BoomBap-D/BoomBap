#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"

// The playlist, Round 4 (item 99c): a TRACK-VIEW ARRANGEMENT. The old
// A/B pattern chain is gone - patterns are now BLOCKS of real length
// laid on lanes over a bar ruler, with a playhead tracking across in
// song mode. A block sounds its pattern from its start bar for its
// length in bars (the pattern loops inside a longer block, trims in a
// shorter one). Blocks on one lane never overlap; lanes sound together
// (the engine's merge rules live in BoomBapEngine::resolveSongStep).
//
// Gestures are the clip grammar the owner approved (item 58): click
// selects, drag moves (bar-quantized, across lanes too), edge-drag
// trims, right-click deletes. One drag = one undo gesture. The header
// palette adds a 2-bar block of a pattern at the song's end; the ruler
// seeks the transport park when clicked (item 50's rule).
//
// NAMED SEAM: the lanes hold PATTERNS only. Audio lives in the
// multitrack timeline, as it did - a 20-bar guitar block is a
// multitrack clip today, not a playlist block. Unifying the two
// timelines is a named next step, not something this window pretends.
class PlaylistComponent : public juce::Component, private juce::Timer
{
public:
    explicit PlaylistComponent (boombap::BoomBapEngine& eng) : engine (eng)
    {
        addLabel.setText ("ADD PATTERN:", juce::dontSendNotification);
        addLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (addLabel);
        for (int i = 0; i < boombap::BoomBapEngine::kPatterns; ++i)
        {
            auto& b = addButtons[(size_t) i];
            b.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
            b.onClick = [this, i]
            {
                if (onBeforeChange) onBeforeChange();
                boombap::BoomBapEngine::SongBlock nb;
                nb.active = true;
                nb.pattern = i;
                nb.lane = 0;
                nb.startBar = engine.songTotalBars();
                nb.lengthBars = 2;
                engine.addSongBlock (nb);
                if (onAfterChange) onAfterChange();
                refreshFromEngine();
            };
            addAndMakeVisible (b);
        }
        clearButton.onClick = [this]
        {
            if (onBeforeChange) onBeforeChange();
            engine.clearSongBlocks();
            if (onAfterChange) onAfterChange();
            refreshFromEngine();
        };
        addAndMakeVisible (clearButton);

        status.setColour (juce::Label::textColourId, boombap::ui::text());
        addAndMakeVisible (status);

        hint.setText ("Click a block to select it. Drag moves it (bars snap, lanes are free). "
                      "Drag an edge to trim. Right-click deletes. Click the ruler to park the playhead there.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (hint);

        content = std::make_unique<ArrContent> (*this, engine);
        viewport.setViewedComponent (content.get(), false);
        viewport.setScrollBarsShown (true, true);
        viewport.setScrollBarThickness (12);
        addAndMakeVisible (viewport);

        refreshFromEngine();
        startTimerHz (20);
    }

    ~PlaylistComponent() override { stopTimer(); }

    void refreshFromEngine()
    {
        refreshStatus();
        if (content != nullptr)
        {
            content->syncSize();
            content->repaint();
        }
        repaint();
    }

    // Pass 2 (item 38): undo gesture hooks, wired by the owner.
    std::function<void()> onBeforeChange, onAfterChange;

    // Test seams: the arrangement canvas and its geometry.
    juce::Component& contentForTest() { return *content; }
    juce::Rectangle<float> blockRectForTest (int blockIndex) const
    {
        return content != nullptr ? content->blockRect (blockIndex) : juce::Rectangle<float>();
    }
    int selectedBlockForTest() const { return content != nullptr ? content->selectedBlock() : -1; }
    void contentPressForTest (juce::Point<float> p, bool rightButton = false)
    {
        if (content != nullptr) content->pressAt (p, rightButton);
    }
    void contentDragForTest (juce::Point<float> p)
    {
        if (content != nullptr) content->dragTo (p);
    }
    void contentReleaseForTest()
    {
        if (content != nullptr) content->releaseDrag();
    }

private:
    class ArrContent : public juce::Component
    {
    public:
        ArrContent (PlaylistComponent& o, boombap::BoomBapEngine& eng) : owner (o), engine (eng) {}

        static constexpr float kGutter = 64.0f, kRulerH = 22.0f;
        static constexpr float kBarW = 54.0f, kLaneH = 46.0f, kLaneGap = 6.0f;

        float laneY (int lane) const { return kRulerH + 8.0f + (float) lane * (kLaneH + kLaneGap); }
        int totalBarsShown() const { return juce::jmax (16, engine.songTotalBars()); }

        void syncSize()
        {
            const int w = (int) (kGutter + (float) totalBarsShown() * kBarW) + 24;
            const int h = (int) (kRulerH + 8.0f + boombap::BoomBapEngine::kSongLanes * (kLaneH + kLaneGap)) + 10;
            if (getWidth() != w || getHeight() != h) setSize (w, h);
        }

        juce::Rectangle<float> blockRect (int blockIndex) const
        {
            const auto b = displayedBlock (blockIndex);
            if (! b.active) return {};
            return { kGutter + (float) b.startBar * kBarW + 2.0f,
                     laneY (b.lane) + 3.0f,
                     (float) b.lengthBars * kBarW - 4.0f,
                     kLaneH - 6.0f };
        }
        int selectedBlock() const { return selected; }

        // During a drag the dragged block shows its candidate, not the
        // engine's (still-unchanged) block - the engine is written once,
        // on mouse-up, so the whole drag is one undo gesture.
        boombap::BoomBapEngine::SongBlock displayedBlock (int i) const
        {
            if (i == dragBlock && dragging) return dragCandidate;
            return engine.getSongBlock (i);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace boombap::ui;
            g.fillAll (bg());
            const int bars = totalBarsShown();

            // ruler: bar numbers + beat ticks
            g.setColour (panelAlt());
            g.fillRect (0.0f, 0.0f, (float) getWidth(), kRulerH);
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            for (int bar = 0; bar < bars; ++bar)
            {
                const float x = kGutter + (float) bar * kBarW;
                g.setColour (bar % 4 == 0 ? text() : muted());
                g.drawText (juce::String (bar + 1), (int) x + 4, 3, 40, 16, juce::Justification::left);
                g.setColour (border().withAlpha (0.5f));
                g.fillRect (x, kRulerH - 6.0f, 1.0f, 6.0f);
            }

            // lanes
            for (int lane = 0; lane < boombap::BoomBapEngine::kSongLanes; ++lane)
            {
                const float y = laneY (lane);
                g.setColour (panel().darker (0.25f));
                g.fillRect (kGutter, y, (float) bars * kBarW, kLaneH);
                g.setColour (muted());
                g.setFont (juce::Font (11.0f, juce::Font::bold));
                g.drawText ("LANE " + juce::String (lane + 1), 6, (int) y, (int) kGutter - 10,
                            (int) kLaneH, juce::Justification::centredLeft);
                g.setColour (border().withAlpha (0.35f));
                for (int bar = 0; bar <= bars; ++bar)
                    g.fillRect (kGutter + (float) bar * kBarW, y, 1.0f, kLaneH);
            }

            // blocks
            const int playBar = engine.isSongMode() ? engine.getPlaySongBar() : -1;
            for (int i = 0; i < boombap::BoomBapEngine::kMaxSongBlocks; ++i)
            {
                const auto b = displayedBlock (i);
                if (! b.active) continue;
                const auto r = blockRect (i);
                const bool sounding = playBar >= b.startBar && playBar < b.startBar + b.lengthBars
                                   && engine.isPlaying();
                g.setColour (sounding ? accentHi() : accent().withAlpha (i == selected ? 0.95f : 0.62f));
                g.fillRoundedRectangle (r, 5.0f);
                if (i == selected)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.9f));
                    g.drawRoundedRectangle (r, 5.0f, 1.6f);
                }
                g.setColour (juce::Colour (0xff14100a));
                g.setFont (juce::Font (17.0f, juce::Font::bold));
                g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + b.pattern)),
                            r.withHeight (24.0f).translated (6.0f, 2.0f), juce::Justification::left);
                g.setFont (juce::Font (10.5f, juce::Font::bold));
                g.drawText (juce::String (b.lengthBars) + (b.lengthBars == 1 ? " bar" : " bars"),
                            r.withHeight (16.0f).translated (6.0f, r.getHeight() - 18.0f),
                            juce::Justification::left);
                // trim handles
                g.setColour (juce::Colour (0xff14100a).withAlpha (0.5f));
                g.fillRect (r.getX() + 2.0f, r.getCentreY() - 7.0f, 2.5f, 14.0f);
                g.fillRect (r.getRight() - 4.5f, r.getCentreY() - 7.0f, 2.5f, 14.0f);
            }

            if (engine.songBlockCount() == 0)
            {
                g.setColour (muted());
                g.setFont (juce::Font (14.0f));
                g.drawText ("No blocks yet - add a pattern above and the song starts here.",
                            (int) kGutter + 16, (int) laneY (0), 560, (int) kLaneH,
                            juce::Justification::centredLeft);
            }

            // the playhead (song mode): the park when stopped, the live
            // bar while playing - one position, both faces.
            if (engine.isSongMode() && engine.songTotalBars() > 0)
            {
                const double barPos = engine.transportPosition() / juce::jmax (1.0, engine.barSamples());
                const float x = kGutter + (float) barPos * kBarW;
                g.setColour (juce::Colours::white.withAlpha (engine.isPlaying() ? 0.85f : 0.4f));
                g.fillRect (x, 0.0f, 1.6f, (float) getHeight());
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            pressAt (e.position, e.mods.isRightButtonDown());
        }

        // The gesture behaviour, shared by the mouse and the test seams.
        void pressAt (juce::Point<float> pos, bool rightButton)
        {
            const int hit = blockAt (pos);
            if (rightButton)
            {
                if (hit >= 0)
                {
                    selected = hit;
                    if (owner.onBeforeChange) owner.onBeforeChange();
                    engine.removeSongBlock (hit);
                    selected = -1;
                    if (owner.onAfterChange) owner.onAfterChange();
                    owner.refreshFromEngine();
                }
                return;
            }
            if (pos.y < kRulerH && pos.x >= kGutter)
            {
                // item 50's rule: click the ruler, park the playhead there
                const int bar = (int) ((pos.x - kGutter) / kBarW);
                engine.seekTransport ((long long) std::llround (bar * engine.barSamples()));
                repaint();
                return;
            }
            if (hit < 0) { selected = -1; repaint(); return; }

            selected = hit;
            const auto b = engine.getSongBlock (hit);
            const auto r = blockRect (hit);
            dragBlock = hit;
            dragOriginal = b;
            dragCandidate = b;
            dragging = true;
            dragStart = pos;
            dragMode = pos.x < r.getX() + 8.0f ? 1 : (pos.x > r.getRight() - 8.0f ? 2 : 0);
            if (owner.onBeforeChange) owner.onBeforeChange(); // the gesture opens; mouseUp commits + closes
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            dragTo (e.position);
        }

        void dragTo (juce::Point<float> pos)
        {
            if (! dragging || dragBlock < 0) return;
            const float dx = pos.x - dragStart.x;
            const float dy = pos.y - dragStart.y;
            auto cand = dragOriginal;
            if (dragMode == 0) // move
            {
                cand.startBar = juce::jmax (0, dragOriginal.startBar + (int) std::lround (dx / kBarW));
                cand.lane = juce::jlimit (0, boombap::BoomBapEngine::kSongLanes - 1,
                                          dragOriginal.lane + (int) std::lround (dy / (kLaneH + kLaneGap)));
            }
            else if (dragMode == 2) // trim the end
            {
                cand.lengthBars = juce::jmax (1, dragOriginal.lengthBars + (int) std::lround (dx / kBarW));
            }
            else // trim the start (the end stays put)
            {
                const int shift = (int) std::lround (dx / kBarW);
                cand.startBar = juce::jmax (0, dragOriginal.startBar + shift);
                cand.lengthBars = juce::jmax (1, dragOriginal.lengthBars - (cand.startBar - dragOriginal.startBar));
            }
            if (engine.arrangementCanPlace (cand, dragBlock))
                dragCandidate = cand; // a placement the lane forbids simply doesn't follow the mouse
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            releaseDrag();
        }

        void releaseDrag()
        {
            if (! dragging) return;
            dragging = false;
            const int blk = dragBlock;
            dragBlock = -1;
            if (blk >= 0 && (dragCandidate.startBar != dragOriginal.startBar
                             || dragCandidate.lane != dragOriginal.lane
                             || dragCandidate.lengthBars != dragOriginal.lengthBars))
            {
                engine.setSongBlock (blk, dragCandidate);
                owner.refreshFromEngine();
            }
            if (owner.onAfterChange) owner.onAfterChange();
            repaint();
        }

    private:
        int blockAt (juce::Point<float> p) const
        {
            // topmost = lowest lane drawn last; scan in reverse draw order
            for (int i = boombap::BoomBapEngine::kMaxSongBlocks - 1; i >= 0; --i)
            {
                const auto b = displayedBlock (i);
                if (b.active && blockRect (i).contains (p)) return i;
            }
            return -1;
        }

        PlaylistComponent& owner;
        boombap::BoomBapEngine& engine;
        int selected = -1;
        bool dragging = false;
        int dragBlock = -1, dragMode = 0; // 0 move, 1 trim start, 2 trim end
        juce::Point<float> dragStart;
        boombap::BoomBapEngine::SongBlock dragOriginal, dragCandidate;
    };

    void refreshStatus()
    {
        juce::String text;
        const int blocks = engine.songBlockCount();
        const int bars = engine.songTotalBars();
        if (blocks == 0)
            text = "Song: empty - no blocks on the lanes.";
        else
            text = "Song: " + juce::String (bars) + (bars == 1 ? " bar, " : " bars, ")
                 + juce::String (blocks) + (blocks == 1 ? " block" : " blocks") + ".";
        text += engine.isSongMode()
              ? "  SONG mode is ON (main page) - playback walks this arrangement."
              : "  PATTERN mode is on - switch to SONG on the main page to hear it.";
        if (status.getText() != text) status.setText (text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        // the playhead + the sounding-block light follow the transport
        const int bar = engine.isPlaying() ? engine.getPlaySongBar() : -2;
        const long long pos = engine.transportPosition();
        if (bar != lastPlayBar || pos != lastPos)
        {
            lastPlayBar = bar;
            lastPos = pos;
            refreshStatus();
            if (content != nullptr) content->repaint();
        }
    }

    void resized() override
    {
        status.setBounds (16, 8, getWidth() - 32, 22);
        addLabel.setBounds (16, 38, 104, 24);
        for (int i = 0; i < boombap::BoomBapEngine::kPatterns; ++i)
            addButtons[(size_t) i].setBounds (124 + i * 38, 38, 32, 24);
        clearButton.setBounds (getWidth() - 126, 38, 110, 24);
        viewport.setBounds (8, 70, getWidth() - 16, juce::jmax (80, getHeight() - 70 - 34));
        hint.setBounds (16, getHeight() - 28, getWidth() - 32, 22);
    }

    boombap::BoomBapEngine& engine;
    juce::Label status, hint, addLabel;
    std::array<juce::TextButton, boombap::BoomBapEngine::kPatterns> addButtons;
    juce::TextButton clearButton { "Clear song" };
    std::unique_ptr<ArrContent> content;
    juce::Viewport viewport;
    int lastPlayBar = -3;
    long long lastPos = -1;
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
        setResizeLimits (820, 430, 10000, 10000);
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
