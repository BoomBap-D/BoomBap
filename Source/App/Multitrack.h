#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include <cstdint>
#include <functional>
#include <set>

// The AUDIO MULTITRACK view (items 32/35, made self-sufficient in fix
// pass 2, multi-track in item 60): the top work area in its audio mode.
// - Its OWN transport (item 47): PLAY/STOP here run the audio tracks
//   alone, from this view's own playhead - no beat behind them.
// - The timeline is NOT fenced by the pattern loop (item 48): a take of
//   any length plays end to end; the view widens to fit the audio.
// - Click the bar numbers to place the playhead (item 50).
// - Audio tracks are added like sequencer tracks (item 60): + AUDIO
//   TRACK gives another lane, each its own channel with its own Level /
//   MUTE / SOLO. Clips live on any track; dragging a clip to another
//   lane moves it there. Click a track's header to ARM it - the next
//   recorded take lands on the armed track.
// - A recorded take lands as a CLIP: drag the body to move it (whole -
//   dragging never splits it, item 54), drag an edge to trim it,
//   Ctrl/Cmd-click or right-click to cut it there, select + DELETE
//   button or Delete key to remove it (item 51).
// - While a take records, its waveform DRAWS LIVE on the armed track,
//   anchored where it started (item 53). A clip that is sounding lights
//   up (item 54), so a stray sound can be found by eye.
// - MUTE/SOLO are full words at a size you can hit (item 52).
// - The footer shows the BoomBap file home (item 62): where takes live
//   is printed, never a mystery.
// The strip above the ruler shows the mixer running in track order: the
// instrument rows, then the audio tracks (item 35). Its chips carry the
// state lights (item 68): a channel that is PLAYING wears a bright
// outline box (the sounding-clip light of item 54, up top), and the
// SELECTED channel wears a solid light of its own - the two never read
// as the same state, and a chip that is both shows both.
// The docked view scrolls its lanes in place (item 67): the lane
// scrollbar's total range is the full lane-stack height.
class MultitrackComponent : public juce::Component, private juce::Timer,
                            private juce::ScrollBar::Listener
{
public:
    MultitrackComponent (boombap::BoomBapEngine& eng, std::function<void()> changeCb,
                         std::function<void()> audioPlayCb = {},
                         std::function<void()> stopAllCb = {})
        : engine (eng), onChange (std::move (changeCb)),
          onAudioPlay (std::move (audioPlayCb)), onStopAll (std::move (stopAllCb))
    {
        setWantsKeyboardFocus (true);

        titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        titleLabel.setText ("AUDIO MULTITRACK", juce::dontSendNotification);
        addAndMakeVisible (titleLabel);

        playButton.setColour (juce::TextButton::buttonColourId, boombap::ui::accent());
        playButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff14100a));
        playButton.onClick = [this] { if (onAudioPlay) onAudioPlay(); };
        addAndMakeVisible (playButton);

        stopButton.onClick = [this] { if (onStopAll) onStopAll(); };
        addAndMakeVisible (stopButton);

        addClipButton.onClick = [this]
        {
            if (onBeforeChange) onBeforeChange();
            if (engine.placeTakeAsClip() >= 0)
            {
                rebuildPeaks();
                repaint();
                if (onChange) onChange();
            }
            if (onAfterChange) onAfterChange();
        };
        addAndMakeVisible (addClipButton);

        addTrackButton.onClick = [this]
        {
            if (onBeforeChange) onBeforeChange();
            engine.addAudioTrack();
            layoutLaneControls();
            resized();
            repaint();
            if (onChange) onChange();
            if (onAfterChange) onAfterChange();
        };
        addAndMakeVisible (addTrackButton);

        deleteButton.setEnabled (false);
        deleteButton.onClick = [this] { deleteSelectedClip(); };
        addAndMakeVisible (deleteButton);

        // per-track channel controls (item 60): one Level + MUTE + SOLO
        // per audio track, in the lane headers
        for (int t = 0; t < boombap::BoomBapEngine::kAudioTracks; ++t)
        {
            auto& lvl = laneLevel[(size_t) t];
            lvl.setSliderStyle (juce::Slider::LinearHorizontal);
            lvl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            lvl.setRange (0.0, 1.2, 0.01);
            lvl.setValue (engine.getAudioTrackGain (t), juce::dontSendNotification);
            lvl.setDoubleClickReturnValue (true, 0.9);
            lvl.onDragStart = [this, t]
            {
                if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kAudioChannelBase + t);
                if (onBeforeChange) onBeforeChange();
            };
            lvl.onDragEnd = [this] { if (onAfterChange) onAfterChange(); };
            lvl.onValueChange = [this, t] { engine.setAudioTrackGain (t, (float) laneLevel[(size_t) t].getValue()); };
            addChildComponent (lvl);

            auto& m = laneMute[(size_t) t];
            m.setButtonText ("MUTE");
            m.setClickingTogglesState (true);
            m.setColour (juce::TextButton::buttonOnColourId, juce::Colours::indianred);
            m.setToggleState (engine.getAudioTrackMute (t), juce::dontSendNotification);
            m.onClick = [this, t]
            {
                if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kAudioChannelBase + t);
                if (onBeforeChange) onBeforeChange();
                engine.setAudioTrackMute (t, laneMute[(size_t) t].getToggleState());
                if (onAfterChange) onAfterChange();
            };
            addChildComponent (m);

            auto& s = laneSolo[(size_t) t];
            s.setButtonText ("SOLO");
            s.setClickingTogglesState (true);
            s.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
            s.setToggleState (engine.getAudioTrackSolo (t), juce::dontSendNotification);
            s.onClick = [this, t]
            {
                if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kAudioChannelBase + t);
                if (onBeforeChange) onBeforeChange();
                engine.setAudioTrackSolo (t, laneSolo[(size_t) t].getToggleState());
                if (onAfterChange) onAfterChange();
            };
            addChildComponent (s);

            // Fix Pass 3 (item 69): whole-lane delete. Lane 0 never gets
            // one - it is the permanent home every take lands on.
            auto& del = laneDelete[(size_t) t];
            del.setButtonText ("X");
            del.onClick = [this, t] { deleteAudioTrack (t); };
            addChildComponent (del);
        }

        laneScroll.addListener (this);
        addChildComponent (laneScroll);

        hintLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        hintLabel.setText ("Clip gestures, the same in every state: click selects - drag moves whole (across lanes too) - "
                           "edge-drag trims - Ctrl+click or right-click cuts - DELETE key or button removes the selected clip. "
                           "Drag across empty lane space to select a group of clips at once. The X on a lane (tracks 2+) deletes the whole lane. "
                           "Click a track header to arm it for recording.",
                           juce::dontSendNotification);
        addAndMakeVisible (hintLabel);

        homeLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        homeLabel.setFont (juce::Font (11.0f));
        addAndMakeVisible (homeLabel);

        rebuildPeaks();
        layoutLaneControls();
        startTimerHz (30);
    }

    ~MultitrackComponent() override { stopTimer(); }

    // Item 62: the file home, printed in the footer.
    void setHomePathText (const juce::String& text) { homeLabel.setText (text, juce::dontSendNotification); }

    // Item 68: the selected channel, mirrored here so the mixer-order
    // strip can light it. The owner pushes it (MainComponent::
    // selectChannel is the one funnel every selection flows through).
    void setSelectedChannel (int ch)
    {
        if (ch != selectedChannel) { selectedChannel = ch; repaint(); }
    }
    int getSelectedChannel() const { return selectedChannel; }

    void refreshFromEngine()
    {
        for (int t = 0; t < boombap::BoomBapEngine::kAudioTracks; ++t)
        {
            laneLevel[(size_t) t].setValue (engine.getAudioTrackGain (t), juce::dontSendNotification);
            laneMute[(size_t) t].setToggleState (engine.getAudioTrackMute (t), juce::dontSendNotification);
            laneSolo[(size_t) t].setToggleState (engine.getAudioTrackSolo (t), juce::dontSendNotification);
        }
        lastNumAudioTracks = engine.getNumAudioTracks();
        layoutLaneControls();
        rebuildPeaks();
        repaint();
    }

    // Round 6 (item 114): a waveform drag asks where it would land -
    // the audio lane under the point and the timeline position there,
    // in the component's own mapping (the same xToSamples the ruler's
    // seek uses). False when the point is not over a live lane.
    bool dropPoint (juce::Point<int> screenPos, int& trackOut, long long& startOut) const
    {
        const auto p = getLocalPoint (nullptr, screenPos);
        if (! getLocalBounds().contains (p)) return false;
        const int t = laneAtY ((float) p.getY());
        if (t < 0) return false;
        trackOut = t;
        startOut = juce::jmax (0LL, xToSamples ((float) p.getX()));
        return true;
    }

    // Fix Pass 3 (item 69): delete an added audio lane whole - the lane,
    // every clip on it, its mixer settings and its Station channel - as
    // one undoable action.
    void deleteAudioTrack (int t)
    {
        if (t < 1) return; // lane 0 is protected in the engine too
        if (onBeforeChange) onBeforeChange();
        if (engine.removeAudioTrack (t))
        {
            if (selectedSlot >= 0 && ! engine.getAudioClip (selectedSlot).active)
            {
                selectedSlot = -1;
                deleteButton.setEnabled (false);
            }
            marqueeSlots.clear();
            dragSlot = -1;
            dragMode = 0;
            refreshFromEngine();
            if (onChange) onChange();
        }
        if (onAfterChange) onAfterChange();
        repaint();
    }

    // Item 51: remove the selected clip (the DELETE button and key).
    // Fix Pass 3 (item 69): with a marquee selection live, the whole
    // selection goes together, as one undoable action.
    void deleteSelectedClip()
    {
        if (! marqueeSlots.empty())
        {
            if (onBeforeChange) onBeforeChange();
            for (int s : marqueeSlots)
                engine.clearAudioClip (s);
            marqueeSlots.clear();
            selectedSlot = -1;
            deleteButton.setEnabled (false);
            dragSlot = -1;
            dragMode = 0;
            rebuildPeaks();
            repaint();
            if (onChange) onChange();
            if (onAfterChange) onAfterChange();
            return;
        }
        if (selectedSlot < 0) return;
        if (! engine.getAudioClip (selectedSlot).active) { selectedSlot = -1; return; }
        if (onBeforeChange) onBeforeChange();
        engine.clearAudioClip (selectedSlot);
        selectedSlot = -1;
        deleteButton.setEnabled (false);
        if (dragSlot >= 0) { dragSlot = -1; dragMode = 0; }
        rebuildPeaks();
        repaint();
        if (onChange) onChange();
        if (onAfterChange) onAfterChange();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelectedClip();
            return true;
        }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (panel());

        const int barsVisible = barsVisibleCount();
        const unsigned int mask = engine.soundingClipMask();
        const bool capturing = engine.isCapturing();
        const int numAudio = engine.getNumAudioTracks();
        const double barSamples = engine.barSamples();

        // ---- the mixer-order strip: channels run in track order ----
        const float chipY = 44.0f, chipH = 26.0f;
        const float chipAreaW = (float) getWidth() - 32.0f;
        const int numChips = engine.getNumTracks() + numAudio;
        const float chipW = (chipAreaW - (numChips - 1) * 6.0f) / (float) numChips;
        g.setColour (muted());
        g.setFont (10.5f);
        g.drawText ("TRACK ORDER = MIXER ORDER", getWidth() - 316, (int) chipY - 14, 300, 12,
                    juce::Justification::right);
        for (int i = 0; i < numChips; ++i)
        {
            const bool isAudio = i >= engine.getNumTracks();
            const int audioIdx = i - engine.getNumTracks();
            const int chipChannel = isAudio ? boombap::BoomBapEngine::kAudioChannelBase + audioIdx : i;
            // Item 68: PLAYING wears an outline box (the sounding-clip
            // light of item 54, up top); SELECTED wears a solid light of
            // its own. Different treatments on purpose - the two states
            // must never read as the same, and a chip that is both shows
            // both: the solid fill carries the selection, the bright box
            // drawn over it carries the playing.
            const bool chipSelected = chipChannel == selectedChannel;
            const bool chipPlaying = chipChannelPlaying (chipChannel, isAudio, audioIdx, mask);
            juce::Rectangle<float> chip (16.0f + i * (chipW + 6.0f), chipY, chipW, chipH);
            g.setColour (chipSelected ? accent()
                                      : chipPlaying ? accent().withAlpha (0.35f)
                                                    : isAudio ? accent().withAlpha (0.22f) : panelAlt());
            g.fillRoundedRectangle (chip, 6.0f);
            if (chipPlaying)
            {
                g.setColour (accentHi());
                g.drawRoundedRectangle (chip, 6.0f, 2.2f);
            }
            else
            {
                g.setColour (chipSelected ? accent().darker (0.45f)
                                          : isAudio ? accent() : border().withAlpha (0.7f));
                g.drawRoundedRectangle (chip, 6.0f, chipSelected ? 1.4f : 1.0f);
            }
            g.setColour (chipSelected ? juce::Colour (0xff14100a)
                                      : (isAudio || chipPlaying) ? text() : muted());
            g.setFont (juce::Font (juce::jmin (12.5f, chipW * 0.16f),
                                   (isAudio || chipSelected || chipPlaying) ? juce::Font::bold : juce::Font::plain));
            g.drawText (isAudio ? juce::String ("AUDIO ") + juce::String (audioIdx + 1)
                                : juce::String (engine.displayTrackName (i)).toUpperCase(),
                        chip, juce::Justification::centred);
        }

        // ---- the ruler: bar numbers are the seek control (item 50) ----
        const auto ruler = rulerRect();
        g.setColour (panelAlt().darker (0.3f));
        g.fillRoundedRectangle (ruler, 6.0f);
        g.setFont (11.0f);
        for (int b = 0; b <= barsVisible; ++b)
        {
            const float x = ruler.getX() + (float) b * pxPerBar();
            g.setColour (border().withAlpha (b % 4 == 0 ? 0.55f : 0.28f));
            g.drawLine (x, ruler.getY(), x, ruler.getBottom(), 1.0f);
            if (b < barsVisible)
            {
                g.setColour (muted());
                g.drawText (juce::String (b + 1), (int) x + 4, (int) ruler.getY() + 3, 30, 12,
                            juce::Justification::left);
            }
        }

        // ---- the lanes (scrolled) ----
        const auto view = lanesViewRect();
        {
            juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (view.toNearestInt());
            g.addTransform (juce::AffineTransform::translation (0.0f, -scrollOffset));

            for (int t = 0; t < numAudio; ++t)
            {
                const auto lane = laneRect (t);
                const bool armed = engine.getRecTargetTrack() == t;

                // lane body
                g.setColour (panelAlt().darker (0.3f));
                g.fillRoundedRectangle (lane, 8.0f);
                g.setColour (armed ? accent().withAlpha (0.8f) : border().withAlpha (0.6f));
                g.drawRoundedRectangle (lane, 8.0f, armed ? 1.6f : 1.0f);

                // header gutter: name + arm state (the MUTE/SOLO/Level
                // controls are real components laid over this area)
                g.setColour (armed ? accent() : muted());
                g.fillRect (lane.getX() + 10.0f, lane.getCentreY() - 5.0f, 10.0f, 10.0f);
                g.setColour (text());
                g.setFont (juce::Font (12.5f, juce::Font::bold));
                g.drawText ("AUDIO TRACK " + juce::String (t + 1), (int) lane.getX() + 26, (int) lane.getY() + 4, 150, 16,
                            juce::Justification::left);
                g.setColour (muted());
                g.setFont (10.5f);
                g.drawText (armed ? "armed - takes land here" : "click header to arm",
                            (int) lane.getX() + 26, (int) lane.getY() + 20, 160, 12,
                            juce::Justification::left);

                // bar grid on the clip area
                for (int b = 0; b <= barsVisible; ++b)
                {
                    const float x = lane.getX() + kGutterW + (float) b * pxPerBar();
                    g.setColour (border().withAlpha (b % 4 == 0 ? 0.4f : 0.2f));
                    g.drawLine (x, lane.getY() + 4.0f, x, lane.getBottom() - 4.0f, 1.0f);
                }
            }

            // ---- the take being recorded draws itself live (item 53),
            // on the ARMED track ----
            if (capturing && barSamples > 0.0)
            {
                const int t = juce::jlimit (0, numAudio - 1, engine.getRecTargetTrack());
                const auto lane = laneRect (t);
                const long long start = engine.recordStartPosition();
                const long long captured = (long long) std::llround (engine.recordedSeconds() * engine.getSampleRate());
                if (captured > 0)
                {
                    juce::Rectangle<float> r (lane.getX() + kGutterW + (float) (start / barSamples * pxPerBar()),
                                              lane.getY() + 6.0f,
                                              juce::jmax (4.0f, (float) (captured / barSamples * pxPerBar())),
                                              lane.getHeight() - 12.0f);
                    const juce::Colour rec (0xffc0392b);
                    g.setColour (rec.withAlpha (0.25f));
                    g.fillRoundedRectangle (r, 6.0f);
                    g.setColour (rec);
                    g.drawRoundedRectangle (r, 6.0f, 1.4f);

                    const long long spp = juce::jmax (1LL, captured / 1024);
                    const int n = engine.copyRecordingPeaks (recMins.data(), recMaxs.data(), 1024, spp);
                    g.setColour (rec.brighter (0.6f).withAlpha (0.95f));
                    const float mid = r.getCentreY();
                    const float h = r.getHeight() * 0.40f;
                    const int cols = juce::jmax (1, (int) r.getWidth());
                    for (int x = 0; x < cols && n > 0; ++x)
                    {
                        const int idx = juce::jmin (n - 1, (int) ((double) x / (double) cols * n));
                        g.drawLine (r.getX() + (float) x, mid - recMaxs[(size_t) idx] * h,
                                    r.getX() + (float) x, mid - recMins[(size_t) idx] * h, 1.0f);
                    }
                    g.setColour (text());
                    g.setFont (juce::Font (11.0f, juce::Font::bold));
                    g.drawText ("RECORDING...", r.getX() + 8.0f, r.getY() + 4.0f, r.getWidth() - 16.0f, 14.0f,
                                juce::Justification::left);
                }
            }

            // ---- clips, each on its own track's lane ----
            bool anyClip = false;
            for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
            {
                const auto clip = engine.getAudioClip (i);
                if (! clip.active) continue;
                anyClip = true;
                const auto r = clipRect (clip);
                if (r.getWidth() < 1.0f) continue;
                const bool dragging = dragSlot == i;
                const bool selected = selectedSlot == i;
                const bool inMarquee = marqueeSlots.find (i) != marqueeSlots.end();
                const bool sounding = (mask & (1u << i)) != 0u;

                g.setColour (sounding ? accent().withAlpha (0.55f)
                                      : accent().withAlpha (dragging ? 0.85f : 0.22f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (sounding || selected || inMarquee ? juce::Colours::white : accent());
                g.drawRoundedRectangle (r, 6.0f, (sounding || selected || inMarquee) ? 2.2f : 1.4f);

                const auto& pk = peaks[(size_t) i];
                const long long dataLen = peakDataLen[(size_t) i];
                if (! pk.empty() && dataLen > 0)
                {
                    const auto cur = engine.getAudioClip (i);
                    const int p0 = (int) ((double) cur.sourceStart / (double) dataLen * (double) pk.size());
                    const int p1 = juce::jmax (p0 + 1, (int) ((double) (cur.sourceStart + cur.lengthSamples)
                                                               / (double) dataLen * (double) pk.size()));
                    g.setColour (accentHi().withAlpha (0.9f));
                    const float mid = r.getCentreY();
                    const float h = r.getHeight() * 0.40f;
                    const int cols = juce::jmax (1, (int) r.getWidth());
                    const double per = (double) (p1 - p0) / (double) cols;
                    for (int x = 0; x < cols; ++x)
                    {
                        const int start = p0 + (int) (x * per);
                        const int end = juce::jmin (p1, juce::jmax (start + 1, p0 + (int) ((x + 1) * per)));
                        float lo = 0.0f, hi = 0.0f;
                        for (int j = start; j < end && j < (int) pk.size(); ++j)
                        {
                            lo = juce::jmin (lo, pk[(size_t) j].first);
                            hi = juce::jmax (hi, pk[(size_t) j].second);
                        }
                        g.drawLine (r.getX() + (float) x, mid - hi * h, r.getX() + (float) x, mid - lo * h, 1.0f);
                    }
                }

                g.setColour (text());
                g.setFont (juce::Font (11.0f, juce::Font::bold));
                g.drawText (sounding ? "VOCAL TAKE - PLAYING" : "VOCAL TAKE",
                            r.getX() + 8.0f, r.getY() + 4.0f, r.getWidth() - 16.0f, 14.0f,
                            juce::Justification::left);
                g.setColour (accentHi());
                g.fillRect (r.getX(), r.getY(), 3.0f, r.getHeight());
                g.fillRect (r.getRight() - 3.0f, r.getY(), 3.0f, r.getHeight());
            }

            // Fix Pass 3 (item 69): the rubber band while it is drawn.
            if (marqueeActive && marqueeRectContent.getWidth() > 2.0f
                && marqueeRectContent.getHeight() > 2.0f)
            {
                g.setColour (accent().withAlpha (0.16f));
                g.fillRect (marqueeRectContent);
                g.setColour (accent());
                g.drawRect (marqueeRectContent, 1.2f);
            }

            if (! anyClip && ! capturing && numAudio > 0)
            {
                const auto lane = laneRect (0);
                g.setColour (muted());
                g.setFont (13.0f);
                g.drawText ("No clips yet - record a vocal take and it lands on the armed track",
                            (int) lane.getX() + (int) kGutterW + 12, (int) lane.getY(), (int) lane.getWidth() - (int) kGutterW - 24,
                            (int) lane.getHeight(), juce::Justification::centredLeft);
            }
        }

        // ---- playhead: the AUDIO clock (item 48 - it runs past the loop),
        // drawn over the ruler and every lane ----
        if (barSamples > 0.0)
        {
            const long long pos = engine.audioPosition();
            const float x = rulerRect().getX() + (float) ((double) pos / barSamples * pxPerBar());
            const bool running = engine.isPlaying() || engine.isAudioPlaying();
            g.setColour (juce::Colours::white.withAlpha (running ? 0.85f : 0.4f));
            g.drawLine (x, rulerRect().getY(), x, lanesViewRect().getBottom(), 1.6f);
            juce::Path tri;
            tri.addTriangle (x - 5.0f, rulerRect().getY(), x + 5.0f, rulerRect().getY(), x, rulerRect().getY() + 7.0f);
            g.fillPath (tri);
        }
    }

    void resized() override
    {
        titleLabel.setBounds (16, 10, 190, 26);
        playButton.setBounds (214, 10, 64, 26);
        stopButton.setBounds (284, 10, 64, 26);
        addClipButton.setBounds (356, 10, 150, 26);
        addTrackButton.setBounds (514, 10, 132, 26);
        deleteButton.setBounds (getWidth() - 142, 10, 126, 26);
        hintLabel.setBounds (16, getHeight() - 50, getWidth() - 32, 18);
        homeLabel.setBounds (16, getHeight() - 30, getWidth() - 32, 16);

        const auto view = lanesViewRect();
        const float contentH = (float) engine.getNumAudioTracks() * kLaneH;
        // Item 67: the scrollbar's TOTAL range is the full content
        // height and its visible range is the viewport's height inside
        // it - the canonical ScrollBar model. The old code set the
        // limits to (contentH - viewH), the maximum OFFSET, while the
        // visible range's size was the full viewport height; JUCE clamps
        // a visible range longer than the total range to the whole
        // total range (Range::constrainRange), so the thumb filled the
        // track, the start stayed pinned at 0, and neither a thumb drag
        // nor the wheel could ever move it - docked, lanes past the
        // fold were unreachable. (Popped out, the window is tall enough
        // to show every lane at once, which is why only the docked
        // view failed.) The wheel handler's clamp (contentH - viewH)
        // is the maximum START under this model, unchanged.
        scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, contentH - view.getHeight()), scrollOffset);
        laneScroll.setBounds ((int) view.getRight() - 14, (int) view.getY(), 14, (int) view.getHeight());
        laneScroll.setRangeLimits (0.0, juce::jmax (0.0f, contentH));
        laneScroll.setCurrentRange (scrollOffset, view.getHeight());
        laneScroll.setVisible (contentH > view.getHeight() + 1.0f);
        layoutLaneControls();
    }

    void scrollBarMoved (juce::ScrollBar* sb, double newRangeStart) override
    {
        if (sb == &laneScroll)
        {
            scrollOffset = (float) newRangeStart;
            layoutLaneControls();
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (laneScroll.isVisible())
        {
            const float maxScroll = juce::jmax (0.0f, (float) engine.getNumAudioTracks() * kLaneH
                                                      - lanesViewRect().getHeight());
            laneScroll.setCurrentRangeStart (
                juce::jlimit (0.0f, maxScroll, scrollOffset - wheel.deltaY * 40.0f),
                juce::sendNotification);
        }
        juce::Component::mouseWheelMove (e, wheel);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        // the ruler: clicking the bar numbers places the playhead (item 50)
        const auto ruler = rulerRect();
        if (e.x >= (int) ruler.getX() && e.y >= (int) ruler.getY() && e.y < (int) ruler.getBottom())
        {
            engine.setAudioPlayhead (xToSamples ((float) e.x));
            repaint();
            return;
        }

        const float contentY = (float) e.y + scrollOffset;

        // a lane header click arms that track for recording (item 60);
        // the header's own controls (slider/buttons) take their own clicks
        const int laneIdx = laneAtY (contentY);
        if (laneIdx >= 0 && e.x < (int) (16.0f + kGutterW))
        {
            engine.setRecTargetTrack (laneIdx);
            if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kAudioChannelBase + laneIdx);
            repaint();
            return;
        }

        const int slot = clipAt (e.x, (int) contentY);
        if (slot < 0)
        {
            // Fix Pass 3 (item 69): pressing empty lane space starts a
            // rubber-band selection; a plain click (no drag) just clears
            // the current selection, exactly as it always did.
            if (selectedSlot >= 0 || ! marqueeSlots.empty())
            {
                selectedSlot = -1;
                marqueeSlots.clear();
                deleteButton.setEnabled (false);
            }
            if (contentY >= kLanesY)
            {
                marqueeActive = true;
                marqueeStartContent = { (float) e.x, contentY };
                marqueeRectContent = { (float) e.x, contentY, 0.0f, 0.0f };
            }
            repaint();
            return;
        }
        marqueeSlots.clear(); // a clip press replaces any marquee selection
        selectedSlot = slot;
        deleteButton.setEnabled (true);
        const auto clip = engine.getAudioClip (slot);
        const auto r = clipRect (clip);
        const long long at = xToSamples ((float) e.x);
        if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kAudioChannelBase + clip.track);
        // Pass 2 (item 38): the whole clip gesture - split, move, trim -
        // is ONE undoable action; the before-state is captured now, the
        // gesture closes on mouseUp.
        if (onBeforeChange) onBeforeChange();

        // Item 58: split is checked FIRST, before any selection or drag
        // state exists - the same gesture fires in every state.
        if (e.mods.isRightButtonDown() || e.mods.isCommandDown())
        {
            if (engine.splitAudioClip (slot, at))
            {
                rebuildPeaks();
                if (onChange) onChange();
            }
            if (onAfterChange) onAfterChange();
            repaint();
            return;
        }

        dragSlot = slot;
        dragStartX = e.x;
        dragStartContentY = contentY;
        dragOrig = clip;
        // only the painted edge handles trim; the body always moves the
        // whole clip (item 54 - a drag can never fragment it)
        dragMode = e.x < (int) r.getX() + 7 ? 1 : (e.x > (int) r.getRight() - 7 ? 2 : 0);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (marqueeActive)
        {
            const float cx = (float) e.x, cy = (float) e.y + scrollOffset;
            marqueeRectContent = { juce::jmin (marqueeStartContent.x, cx),
                                   juce::jmin (marqueeStartContent.y, cy),
                                   std::abs (cx - marqueeStartContent.x),
                                   std::abs (cy - marqueeStartContent.y) };
            repaint();
            return;
        }
        if (dragSlot < 0) return;
        const double barSamples = engine.barSamples();
        const long long delta = (long long) std::llround ((e.x - dragStartX) / pxPerBar() * barSamples);
        if (dragMode == 0)
        {
            engine.moveAudioClip (dragSlot, dragOrig.startSamples + delta);
            // dragging the body to another lane moves the clip there
            const int laneIdx = laneAtY ((float) e.y + scrollOffset);
            if (laneIdx >= 0 && laneIdx != engine.getAudioClip (dragSlot).track)
                engine.setAudioClipTrack (dragSlot, laneIdx);
        }
        else if (dragMode == 1)
            engine.trimAudioClip (dragSlot, dragOrig.startSamples + delta,
                                  dragOrig.sourceStart + delta, dragOrig.lengthSamples - delta);
        else
            engine.trimAudioClip (dragSlot, dragOrig.startSamples, dragOrig.sourceStart,
                                  dragOrig.lengthSamples + delta);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (marqueeActive)
        {
            marqueeActive = false;
            if (marqueeRectContent.getWidth() > 4.0f && marqueeRectContent.getHeight() > 4.0f)
                for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
                {
                    const auto clip = engine.getAudioClip (i);
                    if (clip.active && clipRect (clip).intersects (marqueeRectContent))
                        marqueeSlots.insert (i);
                }
            deleteButton.setEnabled (! marqueeSlots.empty());
            repaint();
            return;
        }
        if (dragSlot >= 0)
        {
            dragSlot = -1;
            dragMode = 0;
            rebuildPeaks();
            repaint();
            if (onChange) onChange();
            if (onAfterChange) onAfterChange();
        }
    }

    // Pass 2 (item 38): undo gesture hooks + Station channel selection,
    // wired by the owner after construction.
    std::function<void()> onBeforeChange, onAfterChange;
    std::function<void (int)> onSelectChannel;

private:
    static constexpr float kRulerY = 78.0f;
    static constexpr float kLanesY = 102.0f;
    static constexpr float kLaneH = 76.0f;
    static constexpr float kGutterW = 196.0f; // per-lane header (name + controls)

    juce::Rectangle<float> rulerRect() const
    {
        return { 16.0f + kGutterW, kRulerY, (float) getWidth() - 32.0f - kGutterW, 20.0f };
    }

    juce::Rectangle<float> lanesViewRect() const
    {
        return { 16.0f, kLanesY, (float) getWidth() - 32.0f,
                 (float) juce::jmax (60, getHeight() - (int) kLanesY - 58) };
    }

    // A lane's rect in CONTENT coordinates (before the scroll translation).
    juce::Rectangle<float> laneRect (int track) const
    {
        return { 16.0f, kLanesY + (float) track * kLaneH + 3.0f,
                 (float) getWidth() - 32.0f, kLaneH - 6.0f };
    }

    int laneAtY (float contentY) const
    {
        const int t = (int) ((contentY - kLanesY) / kLaneH);
        return (t >= 0 && t < engine.getNumAudioTracks()) ? t : -1;
    }

    // Item 68: is this chip's channel making sound right now? The
    // engine's channel peaks decay per block in the engine itself, so
    // every reader may peek without starving another (the same values
    // the Station's meters watch). An audio chip is also live while
    // any clip on its track is sounding (item 54's mask).
    static constexpr float kChipLiveFloor = 0.01f; // -40 dB peak
    bool chipChannelPlaying (int channel, bool isAudio, int audioTrack, unsigned int clipMask) const
    {
        if (engine.peekChannelPeak (channel, false) > kChipLiveFloor
            || engine.peekChannelPeak (channel, true) > kChipLiveFloor)
            return true;
        if (isAudio)
            for (int s = 0; s < boombap::BoomBapEngine::kMaxAudioClips; ++s)
                if ((clipMask & (1u << s)) != 0u && engine.getAudioClip (s).track == audioTrack)
                    return true;
        return false;
    }

    // The live-chip set as a bitmask over channel ids (item 68): the
    // timer repaints when it changes, and keeps repainting while any
    // chip is lit so the peak decay tail is seen through.
    std::uint64_t chipLiveMask (unsigned int clipMask) const
    {
        std::uint64_t live = 0;
        const int numTracks = engine.getNumTracks();
        const int numChips = numTracks + engine.getNumAudioTracks();
        for (int i = 0; i < numChips; ++i)
        {
            const bool isAudio = i >= numTracks;
            const int audioIdx = i - numTracks;
            const int ch = isAudio ? boombap::BoomBapEngine::kAudioChannelBase + audioIdx : i;
            if (ch >= 0 && ch < 64 && chipChannelPlaying (ch, isAudio, audioIdx, clipMask))
                live |= 1ull << ch;
        }
        return live;
    }

    void layoutLaneControls()
    {
        const int numAudio = engine.getNumAudioTracks();
        const auto view = lanesViewRect();
        for (int t = 0; t < boombap::BoomBapEngine::kAudioTracks; ++t)
        {
            const bool show = t < numAudio;
            auto lane = laneRect (t);
            lane.setY (lane.getY() - scrollOffset);
            // controls belong to a lane only while the lane is FULLY in
            // view - a half-scrolled lane must not bleed its buttons over
            // the footer (they return as it scrolls in)
            const bool inView = show && lane.getY() >= view.getY() - 1.0f
                                && lane.getBottom() <= view.getBottom() + 1.0f;
            laneLevel[(size_t) t].setBounds ((int) lane.getX() + 26, (int) lane.getBottom() - 24, 76, 18);
            laneMute[(size_t) t].setBounds ((int) lane.getX() + 108, (int) lane.getBottom() - 25, 46, 20);
            laneSolo[(size_t) t].setBounds ((int) lane.getX() + 158, (int) lane.getBottom() - 25, 46, 20);
            laneDelete[(size_t) t].setBounds ((int) lane.getX() + 172, (int) lane.getY() + 3, 24, 16);
            laneLevel[(size_t) t].setVisible (inView);
            laneMute[(size_t) t].setVisible (inView);
            laneSolo[(size_t) t].setVisible (inView);
            laneDelete[(size_t) t].setVisible (inView && t >= 1); // lane 0 is the take's home: no X
        }
    }

    // The view widens to fit the audio (item 48): at least 16 bars, more
    // when clips, the playhead or a recording run past them.
    int barsVisibleCount() const
    {
        const double barSamples = engine.barSamples();
        if (barSamples <= 0.0) return 16;
        long long end = engine.audioPosition();
        for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
        {
            const auto c = engine.getAudioClip (i);
            if (c.active) end = juce::jmax (end, c.startSamples + c.lengthSamples);
        }
        if (engine.isCapturing())
            end = juce::jmax (end, engine.recordStartPosition()
                                       + (long long) std::llround (engine.recordedSeconds() * engine.getSampleRate()));
        const int bars = (int) std::ceil ((double) end / barSamples) + 1;
        return juce::jlimit (16, 96, bars);
    }

    float pxPerBar() const
    {
        return rulerRect().getWidth() / (float) barsVisibleCount();
    }

    long long xToSamples (float x) const
    {
        const double barSamples = engine.barSamples();
        return (long long) std::llround (((double) x - rulerRect().getX()) / pxPerBar() * barSamples);
    }

    // Clip rects live in CONTENT coordinates (the paint translates by the
    // scroll offset; mouse handlers add it back).
    juce::Rectangle<float> clipRect (const boombap::AudioClip& clip) const
    {
        const auto lane = laneRect (juce::jlimit (0, boombap::BoomBapEngine::kAudioTracks - 1, clip.track));
        const float ppb = pxPerBar();
        const double barSamples = engine.barSamples();
        if (barSamples <= 0.0) return {};
        const float x = lane.getX() + kGutterW + (float) (clip.startSamples / barSamples * ppb);
        const float w = (float) (clip.lengthSamples / barSamples * ppb);
        return { x, lane.getY() + 5.0f, juce::jmax (4.0f, w), lane.getHeight() - 10.0f };
    }

    int clipAt (int x, int contentY) const
    {
        for (int i = boombap::BoomBapEngine::kMaxAudioClips - 1; i >= 0; --i)
        {
            const auto clip = engine.getAudioClip (i);
            if (clip.active && clipRect (clip).contains ((float) x, (float) contentY))
                return i;
        }
        return -1;
    }

    // Peak pairs (min,max) per clip, resampled from the clip's full audio;
    // paint() picks the slice the trim region shows.
    void rebuildPeaks()
    {
        lastRevision = engine.clipRevisionCount();
        for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
        {
            peaks[(size_t) i].clear();
            peakDataLen[(size_t) i] = 0;
            const auto clip = engine.getAudioClip (i);
            if (! clip.active) continue;
            const auto data = engine.getAudioClipData (i);
            if (data.empty()) continue;
            peakDataLen[(size_t) i] = (long long) data.size();
            const int numPeaks = 2048;
            peaks[(size_t) i].resize ((size_t) numPeaks);
            const double per = (double) data.size() / (double) numPeaks;
            for (int p = 0; p < numPeaks; ++p)
            {
                const int start = (int) (p * per);
                const int end = juce::jmin ((int) data.size(), juce::jmax (start + 1, (int) ((p + 1) * per)));
                float lo = 0.0f, hi = 0.0f;
                for (int j = start; j < end; ++j)
                {
                    lo = juce::jmin (lo, data[(size_t) j]);
                    hi = juce::jmax (hi, data[(size_t) j]);
                }
                peaks[(size_t) i][(size_t) p] = { lo, hi };
            }
            peakRegions[(size_t) i] = clip;
        }
    }

    void timerCallback() override
    {
        if (engine.clipRevisionCount() != lastRevision)
        {
            rebuildPeaks();
            repaint();
            return;
        }
        if (engine.getNumAudioTracks() != lastNumAudioTracks)
        {
            lastNumAudioTracks = engine.getNumAudioTracks();
            layoutLaneControls();
            resized();
            repaint();
            return;
        }
        bool needRepaint = engine.isPlaying() || engine.isAudioPlaying() || engine.isCapturing()
                           || dragSlot >= 0;
        if (engine.soundingClipMask() != lastMask) { lastMask = engine.soundingClipMask(); needRepaint = true; }
        // Item 68: the chip strip's PLAYING lights track the channel
        // peaks even when neither transport is running (a run-on pad or
        // a ringing tail still shows up top while it sounds).
        const auto liveChips = chipLiveMask (lastMask);
        if (liveChips != lastChipLive) { lastChipLive = liveChips; needRepaint = true; }
        if (liveChips != 0) needRepaint = true;
        if (engine.audioPosition() != lastAudioPos) { lastAudioPos = engine.audioPosition(); needRepaint = true; }
        if (engine.getRecTargetTrack() != lastRecTrack) { lastRecTrack = engine.getRecTargetTrack(); needRepaint = true; }
        // keep the waveform slice under the trim region current while dragging
        for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
        {
            const auto clip = engine.getAudioClip (i);
            if (clip.active && (clip.sourceStart != peakRegions[(size_t) i].sourceStart
                                || clip.lengthSamples != peakRegions[(size_t) i].lengthSamples
                                || clip.startSamples != peakRegions[(size_t) i].startSamples
                                || clip.track != peakRegions[(size_t) i].track))
            {
                peakRegions[(size_t) i] = clip;
                needRepaint = true;
            }
        }
        // the play button shows the audio transport's state
        const bool ap = engine.isAudioPlaying();
        if (ap != lastAudioPlaying)
        {
            lastAudioPlaying = ap;
            playButton.setButtonText (ap ? "PLAYING" : "PLAY");
        }
        if (needRepaint) repaint();
    }

    boombap::BoomBapEngine& engine;
    std::function<void()> onChange, onAudioPlay, onStopAll;

    juce::Label titleLabel, hintLabel, homeLabel;
    juce::TextButton playButton { "PLAY" }, stopButton { "STOP" };
    juce::TextButton addClipButton { "Add take as clip" };
    juce::TextButton addTrackButton { "+ AUDIO TRACK" };
    juce::TextButton deleteButton { "DELETE CLIP" };
    std::array<juce::Slider, boombap::BoomBapEngine::kAudioTracks> laneLevel;
    std::array<juce::TextButton, boombap::BoomBapEngine::kAudioTracks> laneMute, laneSolo;
    std::array<juce::TextButton, boombap::BoomBapEngine::kAudioTracks> laneDelete; // item 69: whole-lane delete (lanes 2+)
    juce::ScrollBar laneScroll { true };

    std::array<std::vector<std::pair<float, float>>, boombap::BoomBapEngine::kMaxAudioClips> peaks;
    std::array<long long, boombap::BoomBapEngine::kMaxAudioClips> peakDataLen {};
    std::array<boombap::AudioClip, boombap::BoomBapEngine::kMaxAudioClips> peakRegions;
    int lastRevision = -1;

    std::array<float, 1024> recMins {}, recMaxs {};
    unsigned int lastMask = 0;
    std::uint64_t lastChipLive = 0;
    int selectedChannel = -1; // item 68: mirrored by the owner (see setSelectedChannel)
    long long lastAudioPos = -1;
    bool lastAudioPlaying = false;
    int lastNumAudioTracks = 1;
    int lastRecTrack = 0;
    float scrollOffset = 0.0f;

    int selectedSlot = -1;
    // Fix Pass 3 (item 69): rubber-band multi-selection over clips.
    std::set<int> marqueeSlots;
    bool marqueeActive = false;
    juce::Point<float> marqueeStartContent;
    juce::Rectangle<float> marqueeRectContent;
    int dragSlot = -1, dragMode = 0, dragStartX = 0;
    float dragStartContentY = 0.0f;
    boombap::AudioClip dragOrig;
};
