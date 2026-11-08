#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include <functional>

// The AUDIO MULTITRACK view (Fix pass items 32/35): the top work area in its
// audio mode. A recorded vocal take lands here as a CLIP on the audio track -
// visible waveform, a home on the timeline, and immediate editing: drag the
// body to move it, drag an edge to trim it, Ctrl/Cmd-click (or right-click)
// to cut it at that point. Below the header, the channel order strip shows
// the mixer running in track order: the 8 instrument rows, then the audio
// track (item 35: mixer order = track order).
class MultitrackComponent : public juce::Component, private juce::Timer
{
public:
    MultitrackComponent (boombap::BoomBapEngine& eng, std::function<void()> changeCb)
        : engine (eng), onChange (std::move (changeCb))
    {
        titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, boombap::ui::text());
        titleLabel.setText ("AUDIO TRACK 1 - VOCAL", juce::dontSendNotification);
        addAndMakeVisible (titleLabel);

        addClipButton.onClick = [this]
        {
            if (engine.placeTakeAsClip() >= 0)
            {
                rebuildPeaks();
                repaint();
                if (onChange) onChange();
            }
        };
        addAndMakeVisible (addClipButton);

        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        levelSlider.setRange (0.0, 1.2, 0.01);
        levelSlider.setValue (engine.getAudioTrackGain (0), juce::dontSendNotification);
        levelSlider.setDoubleClickReturnValue (true, 0.9);
        levelSlider.onValueChange = [this] { engine.setAudioTrackGain (0, (float) levelSlider.getValue()); };
        addAndMakeVisible (levelSlider);

        levelLabel.setText ("Level", juce::dontSendNotification);
        levelLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        addAndMakeVisible (levelLabel);

        muteButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::indianred);
        muteButton.setToggleState (engine.getAudioTrackMute (0), juce::dontSendNotification);
        muteButton.onClick = [this] { engine.setAudioTrackMute (0, muteButton.getToggleState()); };
        addAndMakeVisible (muteButton);

        soloButton.setClickingTogglesState (true);
        soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
        soloButton.setToggleState (engine.getAudioTrackSolo (0), juce::dontSendNotification);
        soloButton.onClick = [this] { engine.setAudioTrackSolo (0, soloButton.getToggleState()); };
        addAndMakeVisible (soloButton);

        hintLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        hintLabel.setText ("Record a vocal take, and it lands here as a clip. Drag a clip to move it - "
                           "drag an edge to trim - Ctrl/Cmd-click or right-click a clip to cut it there.",
                           juce::dontSendNotification);
        addAndMakeVisible (hintLabel);

        rebuildPeaks();
        startTimerHz (30);
    }

    ~MultitrackComponent() override { stopTimer(); }

    void refreshFromEngine()
    {
        levelSlider.setValue (engine.getAudioTrackGain (0), juce::dontSendNotification);
        muteButton.setToggleState (engine.getAudioTrackMute (0), juce::dontSendNotification);
        soloButton.setToggleState (engine.getAudioTrackSolo (0), juce::dontSendNotification);
        rebuildPeaks();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (panel());

        const auto lane = laneRect();

        // ---- the mixer-order strip: channels run in track order ----
        const float chipY = 44.0f, chipH = 26.0f;
        const float chipAreaW = (float) getWidth() - 32.0f;
        const int numChips = boombap::BoomBapEngine::kTracks + 1;
        const float chipW = (chipAreaW - (numChips - 1) * 6.0f) / (float) numChips;
        g.setColour (muted());
        g.setFont (10.5f);
        g.drawText ("TRACK ORDER = MIXER ORDER", 16, (int) chipY - 14, 300, 12, juce::Justification::left);
        for (int i = 0; i < numChips; ++i)
        {
            const bool isAudio = i == boombap::BoomBapEngine::kTracks;
            juce::Rectangle<float> chip (16.0f + i * (chipW + 6.0f), chipY, chipW, chipH);
            g.setColour (isAudio ? accent().withAlpha (0.22f) : panelAlt());
            g.fillRoundedRectangle (chip, 6.0f);
            g.setColour (isAudio ? accent() : border().withAlpha (0.7f));
            g.drawRoundedRectangle (chip, 6.0f, 1.0f);
            g.setColour (isAudio ? text() : muted());
            g.setFont (juce::Font (juce::jmin (12.5f, chipW * 0.16f), isAudio ? juce::Font::bold : juce::Font::plain));
            g.drawText (isAudio ? juce::String ("VOCAL (AUDIO)")
                                : juce::String (boombap::BoomBapEngine::trackName (i)).toUpperCase(),
                        chip, juce::Justification::centred);
        }

        // ---- the lane ----
        g.setColour (panelAlt().darker (0.3f));
        g.fillRoundedRectangle (lane, 8.0f);
        g.setColour (border().withAlpha (0.6f));
        g.drawRoundedRectangle (lane, 8.0f, 1.0f);

        const double barSamples = engine.barSamples();
        const int barsVisible = 16;
        // bar grid + numbers
        g.setFont (11.0f);
        for (int b = 0; b <= barsVisible; ++b)
        {
            const float x = lane.getX() + (float) b * pxPerBar();
            g.setColour (border().withAlpha (b % 4 == 0 ? 0.55f : 0.28f));
            g.drawLine (x, lane.getY(), x, lane.getBottom(), 1.0f);
            if (b < barsVisible)
            {
                g.setColour (muted());
                g.drawText (juce::String (b + 1), (int) x + 4, (int) lane.getY() + 3, 30, 12,
                            juce::Justification::left);
            }
        }

        // ---- clips ----
        bool anyClip = false;
        for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
        {
            const auto clip = engine.getAudioClip (i);
            if (! clip.active) continue;
            anyClip = true;
            const auto r = clipRect (clip);
            if (r.getWidth() < 1.0f) continue;
            const bool dragging = dragSlot == i;

            g.setColour (dragging ? accent().withAlpha (0.34f) : accent().withAlpha (0.22f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (accent());
            g.drawRoundedRectangle (r, 6.0f, 1.4f);

            // waveform: only the trimmed region of the take shows (peaks are
            // computed over the full audio, so window into them)
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
            g.drawText ("VOCAL TAKE", r.getX() + 8.0f, r.getY() + 4.0f, r.getWidth() - 16.0f, 14.0f,
                        juce::Justification::left);
            // trim handles
            g.setColour (accentHi());
            g.fillRect (r.getX(), r.getY(), 3.0f, r.getHeight());
            g.fillRect (r.getRight() - 3.0f, r.getY(), 3.0f, r.getHeight());
        }

        if (! anyClip)
        {
            g.setColour (muted());
            g.setFont (13.0f);
            g.drawText ("No clips yet - record a vocal take and it lands on this track",
                        lane, juce::Justification::centred);
        }

        // ---- playhead ----
        if (engine.isPlaying() && barSamples > 0.0)
        {
            const int bars = engine.songBars() > 0 ? engine.songBars() : 1;
            const long long loopLen = (long long) std::llround (bars * barSamples);
            if (loopLen > 0)
            {
                const long long pos = engine.transportPosition() % loopLen;
                const float x = lane.getX() + (float) ((double) pos / barSamples * pxPerBar());
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawLine (x, lane.getY(), x, lane.getBottom(), 1.6f);
            }
        }
    }

    void resized() override
    {
        titleLabel.setBounds (16, 10, 300, 24);
        addClipButton.setBounds (330, 10, 170, 24);
        levelLabel.setBounds (516, 10, 44, 24);
        levelSlider.setBounds (562, 10, 170, 24);
        muteButton.setBounds (748, 10, 34, 24);
        soloButton.setBounds (788, 10, 34, 24);
        hintLabel.setBounds (16, getHeight() - 28, getWidth() - 32, 20);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int slot = clipAt (e.x, e.y);
        if (slot < 0) return;
        const auto clip = engine.getAudioClip (slot);
        const auto r = clipRect (clip);
        const long long at = xToSamples ((float) e.x);

        if (e.mods.isRightButtonDown() || e.mods.isCommandDown())
        {
            if (engine.splitAudioClip (slot, at))
            {
                rebuildPeaks();
                repaint();
                if (onChange) onChange();
            }
            return;
        }

        dragSlot = slot;
        dragStartX = e.x;
        dragOrig = clip;
        dragMode = e.x < (int) r.getX() + 7 ? 1 : (e.x > (int) r.getRight() - 7 ? 2 : 0);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragSlot < 0) return;
        const double barSamples = engine.barSamples();
        const long long delta = (long long) std::llround ((e.x - dragStartX) / pxPerBar() * barSamples);
        if (dragMode == 0)
            engine.moveAudioClip (dragSlot, dragOrig.startSamples + delta);
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
        if (dragSlot >= 0)
        {
            dragSlot = -1;
            dragMode = 0;
            rebuildPeaks();
            repaint();
            if (onChange) onChange();
        }
    }

private:
    static constexpr float kLaneY = 84.0f;

    juce::Rectangle<float> laneRect() const
    {
        return { 16.0f, kLaneY, (float) getWidth() - 32.0f, (float) juce::jmax (80, getHeight() - (int) kLaneY - 40) };
    }

    float pxPerBar() const
    {
        return laneRect().getWidth() / 16.0f;
    }

    long long xToSamples (float x) const
    {
        const double barSamples = engine.barSamples();
        return (long long) std::llround (((double) x - laneRect().getX()) / pxPerBar() * barSamples);
    }

    // During a drag the engine already holds the dragged region (live
    // preview), so the rect follows the engine state directly.
    juce::Rectangle<float> clipRect (const boombap::AudioClip& clip) const
    {
        const auto lane = laneRect();
        const float ppb = pxPerBar();
        const double barSamples = engine.barSamples();
        if (barSamples <= 0.0) return {};
        const float x = lane.getX() + (float) (clip.startSamples / barSamples * ppb);
        const float w = (float) (clip.lengthSamples / barSamples * ppb);
        return { x, lane.getY() + 22.0f, juce::jmax (4.0f, w), lane.getHeight() - 44.0f };
    }

    int clipAt (int x, int y) const
    {
        for (int i = boombap::BoomBapEngine::kMaxAudioClips - 1; i >= 0; --i)
        {
            const auto clip = engine.getAudioClip (i);
            if (clip.active && clipRect (clip).contains ((float) x, (float) y))
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
        // keep the waveform slice under the trim region current while dragging
        bool needRepaint = engine.isPlaying() || dragSlot >= 0;
        for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
        {
            const auto clip = engine.getAudioClip (i);
            if (clip.active && (clip.sourceStart != peakRegions[(size_t) i].sourceStart
                                || clip.lengthSamples != peakRegions[(size_t) i].lengthSamples
                                || clip.startSamples != peakRegions[(size_t) i].startSamples))
            {
                peakRegions[(size_t) i] = clip;
                needRepaint = true;
            }
        }
        if (needRepaint) repaint();
    }

    boombap::BoomBapEngine& engine;
    std::function<void()> onChange;

    juce::Label titleLabel, levelLabel, hintLabel;
    juce::TextButton addClipButton { "Add take as clip" };
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Slider levelSlider;

    std::array<std::vector<std::pair<float, float>>, boombap::BoomBapEngine::kMaxAudioClips> peaks;
    std::array<long long, boombap::BoomBapEngine::kMaxAudioClips> peakDataLen {};
    std::array<boombap::AudioClip, boombap::BoomBapEngine::kMaxAudioClips> peakRegions;
    int lastRevision = -1;

    int dragSlot = -1, dragMode = 0, dragStartX = 0;
    boombap::AudioClip dragOrig;
};
