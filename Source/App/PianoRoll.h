#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include "ChordLibrary.h"

// Piano roll for BoomBap's melody lanes: one monophonic line over the 32
// steps. Rows are semitone offsets from the bound sound's root pitch.
//
// Fix pass item 28: the roll is BOUND to a track, summoned from the
// track's own ROLL button; fix pass 2 item 41 widened it to every
// channel, with a STEPS lane for rows (the row's own pattern on one
// pitch) beside the pitched melody lane.
//
// Round 4 (item 98) - the roll becomes a real editor for any track:
//  (a) the bound track's sequencer steps show IN the grid as hollow
//      markers on the root row, under its pitched notes;
//  (b) an OCTAVE offset shifts the visible/editable window ±12;
//  (c) a TRACK DROP-DOWN inside the roll rebinds it (and the global
//      selection) without leaving;
//  (d) ZOOM: 25/50/100% presets + mouse-wheel scaling - the grid is a
//      fixed-size canvas in a viewport now, text grows with the zoom;
//  (e) CHORD + PROGRESSION presets stamp editable notes (arpeggiated -
//      the lanes are monophonic; see ChordLibrary.h), one undo gesture.
class PianoRollComponent : public juce::Component, private juce::Timer
{
public:
    // Binding ids: a step-sequencer track index (0..31), or one of these:
    static constexpr int kBindSynth = -2;
    static constexpr int kBindPiano = -3;

    explicit PianoRollComponent (boombap::BoomBapEngine& eng);
    ~PianoRollComponent() override;

    int boundTrack() const { return bound; }

    juce::String bindingTitle() const
    {
        if (bound == kBindSynth) return "Piano Roll - Synth";
        if (bound == kBindPiano) return "Piano Roll - Piano";
        return "Piano Roll - " + juce::String (engine.displayTrackName (bound));
    }

    // Summons the roll for one track. Sample rows choose their own pad as
    // the melody lane's sound, so what opens is already the right lane.
    void bindToTrack (int track);
    bool supportsMelody() const;

    // Round 4 (item 98b): the octave offset of the visible window.
    int  octaveShift() const { return octShift; }
    void setOctaveShift (int semitones)
    {
        octShift = juce::jlimit (-12, 12, semitones - (semitones % 12));
        refreshOctaveUi();
        repaint();
    }

    // Round 4 (item 98d): zoom of the grid canvas (1.0 = the 100% preset).
    float zoomFactor() const { return zoom; }
    void setZoomFactor (float z)
    {
        zoom = juce::jlimit (0.25f, 2.0f, z);
        layoutGrid();
        repaint();
    }

    // Round 4 (item 98e): the stamps. Both write through the roll's own
    // lane, wrapped in ONE undo gesture, and return the notes written.
    // A stamp on a steps-only row does nothing (no pitched lane).
    int stampChord (int chordTypeIdx, int rootOffset, int startStep, int spanSteps = 16);
    int stampProgression (int progressionIdx, int keyRootOffset, int startStep);

    void refreshFromEngine();
    void updateModeUi();

    // Pass 2 (item 38): undo gesture hooks + Station channel selection,
    // wired by the owner after construction.
    std::function<void()> onBeforeChange, onAfterChange;
    std::function<void (int)> onSelectChannel;

    // Test seams (the component suites drive the real controls).
    juce::ComboBox& trackBoxForTest()  { return trackBox; }
    juce::ComboBox& chordBoxForTest()  { return chordTypeBox; }
    juce::ComboBox& progBoxForTest()   { return progBox; }
    juce::TextButton& stampChordButtonForTest() { return stampChordButton; }
    juce::TextButton& stampProgButtonForTest()  { return stampProgButton; }
    bool stepsModeForTest() const { return stepsMode; }
    // The grid-touching seams are defined after PianoRollGrid below
    // (the nested class is incomplete inside this class body).
    juce::Rectangle<int> gridCanvasSizeForTest() const;
    void gridPressForTest (int x, int y, bool rightButton = false);
    void gridReleaseForTest()
    {
        if (grid != nullptr && onAfterChange) onAfterChange();
    }
    juce::Point<int> gridCellCentreForTest (int step, int row) const;
    juce::Point<int> gridStepCentreForTest (int step) const;

private:
    friend class PianoRollGrid;
    class PianoRollGrid;

    void selectBoundChannel();
    void refreshTitle();
    void refreshOctaveUi();
    void rebuildTrackBox();
    void layoutGrid();
    void zoomAt (float newZoom, juce::Point<int> gridPoint);
    int  stampStartStep() const;
    int  stepAt (int s) const;
    void setStepAt (int s, int v);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    boombap::BoomBapEngine& engine;
    std::unique_ptr<PianoRollGrid> grid;
    juce::Viewport viewport;

    juce::Label titleLabel, hint, sourceLabel;
    juce::TextButton clearButton { "Clear notes" };
    juce::TextButton modeToggle;
    juce::ComboBox trackBox;
    juce::TextButton octDownButton { "OCT -" }, octUpButton { "OCT +" };
    juce::Label octLabel;
    juce::TextButton zoom25 { "25%" }, zoom50 { "50%" }, zoom100 { "100%" };
    juce::Label chordLabel, progLabel;
    juce::ComboBox chordRootBox, chordTypeBox, progBox;
    juce::TextButton stampChordButton { "STAMP" }, stampProgButton { "STAMP" };

    int bound = 0;
    bool stepsMode = false;
    int octShift = 0;
    float zoom = 1.0f;
    int lastPlayStep = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};

// The scrollable note canvas. All of the roll's painting and note
// editing lives here, at the owner's zoom; the owner owns the header.
class PianoRollComponent::PianoRollGrid : public juce::Component
{
public:
    PianoRollGrid (PianoRollComponent& o, boombap::BoomBapEngine& eng)
        : owner (o), engine (eng) {}

    static constexpr int kCols = boombap::BoomBapEngine::kSteps, kRows = 25;

    // The canvas size at the owner's zoom: the 100% cell is 30x22 -
    // comfortably readable, per item 98's tiny-lettering complaint.
    juce::Rectangle<int> canvasSize() const
    {
        const float z = owner.zoom;
        const int labelW = labelWidth();
        const int w = labelW + (int) std::ceil (kCols * (30.0f * z + 2.0f)) + 10;
        const int h = 8 + (int) std::ceil (kRows * (22.0f * z + 2.0f)) + 8;
        return { 0, 0, w, h };
    }
    int labelWidth() const { return juce::jlimit (30, 64, (int) (56.0f * owner.zoom)); }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.fillAll (bg());
        if (owner.stepsMode && owner.bound >= 0) { paintSteps (g); return; }

        const auto geo = gridGeom();
        const int playStep = engine.isPlaying() ? engine.currentStep() : -1;

        for (int row = 0; row < kRows; ++row)
        {
            const int semitone = owner.octShift + 12 - row;
            const float y = geo.y + row * (geo.cellH + geo.gap);
            const int pitchClass = ((semitone % 12) + 12) % 12;
            const bool blackKey = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                               || pitchClass == 8 || pitchClass == 10;

            g.setColour (semitone == 0 ? accent().withAlpha (0.13f)
                                       : blackKey ? panel().darker (0.3f) : panelAlt());
            g.fillRect (geo.x, y, geo.w, geo.cellH);

            g.setColour (semitone == 0 ? accent() : muted().withAlpha (0.75f));
            g.setFont (juce::Font (juce::jlimit (9.0f, 16.0f, geo.cellH * 0.72f),
                                   semitone == 0 ? juce::Font::bold : juce::Font::plain));
            g.drawText (semitone == 0 ? juce::String ("root")
                                      : (semitone > 0 ? "+" : "") + juce::String (semitone),
                        2, (int) y, geo.labelW - 6, (int) geo.cellH, juce::Justification::centredRight);
        }

        // Item 98a: the bound row's steps, hollow markers on the root
        // row - over the row shading, under any pitched note there.
        if (owner.bound >= 0)
        {
            const int rootRow = owner.octShift + 12;
            if (rootRow >= 0 && rootRow < kRows)
            {
                const float y = geo.y + rootRow * (geo.cellH + geo.gap);
                for (int s = 0; s < kCols; ++s)
                {
                    const int v = engine.getStep (owner.bound, s);
                    if (v == 0) continue;
                    juce::Rectangle<float> cell (geo.x + s * (geo.cellW + geo.gap), y, geo.cellW, geo.cellH);
                    g.setColour (accent().withAlpha (0.10f + 0.25f * (float) v / 127.0f));
                    g.fillRoundedRectangle (cell, 3.0f);
                    g.setColour (accent().withAlpha (0.55f));
                    g.drawRoundedRectangle (cell, 3.0f, 1.2f);
                }
            }
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
            const int v = owner.stepAt (s);
            if (v == 0) continue;
            const int row = owner.octShift + 12 - (v - 25);
            if (row < 0 || row >= kRows) continue; // outside the shifted window
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
        const float laneH = juce::jmin (geo.h - 16.0f, juce::jmax (44.0f, 84.0f * owner.zoom));
        const float laneY = geo.y + (geo.h - laneH) * 0.5f;
        const int playStep = engine.isPlaying() ? engine.currentStep() : -1;

        g.setColour (muted());
        g.setFont (juce::Font (juce::jmax (11.0f, 12.0f * owner.zoom), juce::Font::bold));
        g.drawText ("STEPS", 2, (int) laneY, geo.labelW - 6, (int) laneH, juce::Justification::centredRight);

        for (int s = 0; s < kCols; ++s)
        {
            const float x = geo.x + s * (geo.cellW + geo.gap);
            juce::Rectangle<float> cell (x, laneY, geo.cellW, laneH);
            const int v = engine.getStep (owner.bound, s);
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
                g.setFont (juce::Font (juce::jmin (15.0f, geo.cellW * 0.5f), juce::Font::bold));
                g.drawText (juce::String (v), cell, juce::Justification::centred);
            }
            if (s % 4 == 0)
            {
                g.setColour (s == 16 ? accent() : muted());
                g.setFont (juce::jmax (10.0f, 11.0f * owner.zoom));
                g.drawText (juce::String (s + 1), (int) x, (int) laneY - 18, 40, 14, juce::Justification::left);
            }
        }
        const float bx = geo.x + 16 * (geo.cellW + geo.gap) - geo.gap;
        g.setColour (accent().withAlpha (0.85f));
        g.fillRect (bx, laneY - 20.0f, 2.0f, laneH + 24.0f);
    }

    int stepLaneAt (int x, int y) const
    {
        const auto geo = gridGeom();
        const float laneH = juce::jmin (geo.h - 16.0f, juce::jmax (44.0f, 84.0f * owner.zoom));
        const float laneY = geo.y + (geo.h - laneH) * 0.5f;
        if ((float) y < laneY || (float) y > laneY + laneH) return -1;
        const float fx = (float) x - geo.x;
        if (fx < 0.0f) return -1;
        const int step = (int) (fx / (geo.cellW + geo.gap));
        if (step < 0 || step >= kCols) return -1;
        if (fx - step * (geo.cellW + geo.gap) >= geo.cellW) return -1;
        return step;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressAt (e.x, e.y, e.mods);
    }

    // The press behaviour, shared by the mouse and the test seam.
    void pressAt (int px, int py, juce::ModifierKeys mods)
    {
        if (owner.stepsMode && owner.bound >= 0)
        {
            const int s = stepLaneAt (px, py);
            if (s < 0) return;
            owner.selectBoundChannel();
            if (owner.onBeforeChange) owner.onBeforeChange();
            const int v = engine.getStep (owner.bound, s);
            const int next = mods.isRightButtonDown() ? 0 : (v == 0 ? 90 : (v < 110 ? 127 : 0));
            engine.setStep (owner.bound, s, next);
            if (owner.onAfterChange) owner.onAfterChange();
            repaint();
            return;
        }
        const auto cell = cellAt (px, py);
        if (cell.x < 0) return;
        owner.selectBoundChannel();
        if (owner.onBeforeChange) owner.onBeforeChange(); // the stroke is one gesture; mouseUp closes it
        const int semitone = owner.octShift + 12 - cell.y;
        const int value = semitone + 25;
        stepToggled = false;
        if (owner.stepAt (cell.x) == value)
        {
            erasing = true;
            applyAt (cell.x, cell.y);
        }
        else if (semitone == 0 && owner.bound >= 0 && engine.getStep (owner.bound, cell.x) > 0)
        {
            // Item 98a: clicking a step's root marker toggles the STEP
            // itself - the marker is the track's step, at its root.
            engine.setStep (owner.bound, cell.x, 0);
            stepToggled = true;
            repaint();
        }
        else
        {
            erasing = false;
            applyAt (cell.x, cell.y);
        }
    }

    // Test seams: pixel centres the tests can press through pressAt.
    juce::Point<int> cellCentreForTest (int step, int row) const
    {
        const auto geo = gridGeom();
        return { (int) (geo.x + step * (geo.cellW + geo.gap) + geo.cellW * 0.5f),
                 (int) (geo.y + row * (geo.cellH + geo.gap) + geo.cellH * 0.5f) };
    }
    juce::Point<int> stepCentreForTest (int step) const
    {
        const auto geo = gridGeom();
        const float laneH = juce::jmin (geo.h - 16.0f, juce::jmax (44.0f, 84.0f * owner.zoom));
        const float laneY = geo.y + (geo.h - laneH) * 0.5f;
        return { (int) (geo.x + step * (geo.cellW + geo.gap) + geo.cellW * 0.5f),
                 (int) (laneY + laneH * 0.5f) };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (owner.stepsMode && owner.bound >= 0) return; // steps change per click, not per drag
        if (stepToggled) return;
        const auto cell = cellAt (e.x, e.y);
        if (cell.x < 0) return;
        applyAt (cell.x, cell.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (owner.onAfterChange) owner.onAfterChange();
    }

    // Item 98d: the wheel zooms, centred on the step under the cursor.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f) return;
        const float f = wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f;
        owner.zoomAt (owner.zoom * f, e.getPosition());
    }

private:
    struct Geom
    {
        float x = 0, y = 0, w = 0, h = 0, cellW = 0, cellH = 0, gap = 2.0f;
        int labelW = 46;
    };

    Geom gridGeom() const
    {
        Geom geo;
        geo.labelW = labelWidth();
        geo.x = (float) geo.labelW;
        geo.y = 8.0f;
        geo.w = (float) juce::jmax (160, getWidth() - geo.labelW - 10);
        geo.h = (float) juce::jmax (120, getHeight() - 16);
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
        const int value = (owner.octShift + 12 - row) + 25;
        owner.setStepAt (step, erasing ? 0 : value);
        repaint();
    }

    PianoRollComponent& owner;
    boombap::BoomBapEngine& engine;
    bool erasing = false;
    bool stepToggled = false;
};

// ---- PianoRollComponent implementation ----

inline juce::Rectangle<int> PianoRollComponent::gridCanvasSizeForTest() const
{
    return grid != nullptr ? grid->canvasSize() : juce::Rectangle<int>();
}

inline void PianoRollComponent::gridPressForTest (int x, int y, bool rightButton)
{
    if (grid != nullptr)
        grid->pressAt (x, y, rightButton ? juce::ModifierKeys::rightButtonModifier
                                         : juce::ModifierKeys());
}

inline juce::Point<int> PianoRollComponent::gridCellCentreForTest (int step, int row) const
{
    return grid != nullptr ? grid->cellCentreForTest (step, row) : juce::Point<int>();
}

inline juce::Point<int> PianoRollComponent::gridStepCentreForTest (int step) const
{
    return grid != nullptr ? grid->stepCentreForTest (step) : juce::Point<int>();
}

inline PianoRollComponent::PianoRollComponent (boombap::BoomBapEngine& eng) : engine (eng)
{
    titleLabel.setFont (juce::Font (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, boombap::ui::text());
    addAndMakeVisible (titleLabel);

    // Item 98c: the track drop-down - the Station's channel list,
    // restricted to what a roll can bind (rows + the two instruments).
    trackBox.setTooltip ("The track this roll edits");
    trackBox.onChange = [this]
    {
        const int id = trackBox.getSelectedId();
        if (id == 1001)      { bindToTrack (kBindSynth); if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kSynthChannel); }
        else if (id == 1002) { bindToTrack (kBindPiano); if (onSelectChannel) onSelectChannel (boombap::BoomBapEngine::kPianoChannel); }
        else if (id >= 1)    { bindToTrack (id - 1);     if (onSelectChannel) onSelectChannel (id - 1); }
    };
    addAndMakeVisible (trackBox);

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

    // Item 98b: octave shift of the visible window.
    octDownButton.onClick = [this] { setOctaveShift (octShift - 12); };
    octUpButton.onClick   = [this] { setOctaveShift (octShift + 12); };
    addAndMakeVisible (octDownButton);
    addAndMakeVisible (octUpButton);
    octLabel.setJustificationType (juce::Justification::centred);
    octLabel.setColour (juce::Label::textColourId, boombap::ui::text());
    addAndMakeVisible (octLabel);

    // Item 98d: zoom presets (the wheel zooms freely between them).
    zoom25.onClick  = [this] { setZoomFactor (0.25f); };
    zoom50.onClick  = [this] { setZoomFactor (0.5f); };
    zoom100.onClick = [this] { setZoomFactor (1.0f); };
    addAndMakeVisible (zoom25);
    addAndMakeVisible (zoom50);
    addAndMakeVisible (zoom100);

    // Item 98e: chord + progression stamps.
    chordLabel.setText ("CHORD", juce::dontSendNotification);
    chordLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
    addAndMakeVisible (chordLabel);
    static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i) chordRootBox.addItem (noteNames[i], i + 1);
    chordRootBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (chordRootBox);
    {
        const auto& types = boombap::chordTypes();
        for (int i = 0; i < (int) types.size(); ++i) chordTypeBox.addItem (types[(size_t) i].name, i + 1);
        chordTypeBox.setSelectedId (2, juce::dontSendNotification); // Minor - the home chord
    }
    addAndMakeVisible (chordTypeBox);
    stampChordButton.onClick = [this]
    {
        stampChord (chordTypeBox.getSelectedId() - 1, chordRootBox.getSelectedId() - 1, stampStartStep());
    };
    addAndMakeVisible (stampChordButton);

    progLabel.setText ("PROGRESSION", juce::dontSendNotification);
    progLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
    addAndMakeVisible (progLabel);
    {
        const auto& progs = boombap::progressions();
        for (int i = 0; i < (int) progs.size(); ++i) progBox.addItem (progs[(size_t) i].name, i + 1);
        progBox.setSelectedId (3, juce::dontSendNotification); // i - VI - III - VII
    }
    addAndMakeVisible (progBox);
    stampProgButton.onClick = [this]
    {
        stampProgression (progBox.getSelectedId() - 1, chordRootBox.getSelectedId() - 1, stampStartStep());
    };
    addAndMakeVisible (stampProgButton);

    hint.setText ("Click a cell to place a note - one per step - and drag to draw. "
                  "Hollow cells on the root row are the track's own steps; click one to clear it. "
                  "Mouse wheel zooms the grid.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, boombap::ui::muted());
    addAndMakeVisible (hint);

    grid = std::make_unique<PianoRollGrid> (*this, engine);
    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (12);
    addAndMakeVisible (viewport);

    refreshOctaveUi();
    bindToTrack (0); // the kick's melody lane until a track summons it
    startTimerHz (30);
}

inline PianoRollComponent::~PianoRollComponent() { stopTimer(); }

inline void PianoRollComponent::selectBoundChannel()
{
    if (! onSelectChannel) return;
    if (bound >= 0) onSelectChannel (bound);
    else if (bound == kBindSynth) onSelectChannel (boombap::BoomBapEngine::kSynthChannel);
    else if (bound == kBindPiano) onSelectChannel (boombap::BoomBapEngine::kPianoChannel);
}

inline bool PianoRollComponent::supportsMelody() const
{
    if (bound < 0) return true;                                    // synth / piano
    if (bound == boombap::BoomBapEngine::Kick) return true;        // the pitched 808
    return bound >= boombap::BoomBapEngine::Sample1
        && bound < boombap::BoomBapEngine::kFirstOwnRow;           // any pad row
}

inline void PianoRollComponent::bindToTrack (int track)
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
    rebuildTrackBox();
    updateModeUi();
    refreshTitle();
    repaint();
}

inline void PianoRollComponent::rebuildTrackBox()
{
    trackBox.clear (juce::dontSendNotification);
    const int n = engine.getNumTracks();
    for (int t = 0; t < n; ++t)
        trackBox.addItem (juce::String (engine.displayTrackName (t)), t + 1);
    trackBox.addSeparator();
    trackBox.addItem ("Synth", 1001);
    trackBox.addItem ("Piano", 1002);
    const int id = bound == kBindSynth ? 1001 : bound == kBindPiano ? 1002 : bound + 1;
    trackBox.setSelectedId (id, juce::dontSendNotification);
}

inline void PianoRollComponent::updateModeUi()
{
    if (! supportsMelody()) stepsMode = true;
    modeToggle.setEnabled (supportsMelody());
    modeToggle.setButtonText (stepsMode ? "SHOW: TRACK STEPS" : "SHOW: PAD MELODY");
    const bool canStamp = supportsMelody();
    stampChordButton.setEnabled (canStamp);
    stampProgButton.setEnabled (canStamp);
    juce::String src;
    if (bound < 0)
        src = bound == kBindSynth ? "Source: the synth's own lane" : "Source: the piano's own lane";
    else if (stepsMode)
        src = "Source: TRACK steps - " + juce::String (engine.displayTrackName (bound));
    else if (bound == boombap::BoomBapEngine::Kick)
        src = "Source: PAD melody - the kick's pitched 808 lane";
    else
        src = "Source: PAD melody - pad " + juce::String (bound - boombap::BoomBapEngine::Sample1 + 1)
            + " (row " + juce::String (engine.displayTrackName (bound)) + ")";
    sourceLabel.setText (src, juce::dontSendNotification);
    hint.setText (stepsMode
        ? "The row's own pattern on one pitch. Click a step to cycle its velocity: off, on, accent. Right-click clears."
        : "Click a cell to place a note - one per step - and drag to draw. "
          "Hollow cells on the root row are the track's own steps; click one to clear it. "
          "Mouse wheel zooms the grid.",
        juce::dontSendNotification);
    if (grid != nullptr) grid->repaint();
}

inline void PianoRollComponent::refreshFromEngine()
{
    rebuildTrackBox();
    refreshTitle();
    repaint();
    if (grid != nullptr) grid->repaint();
}

inline int PianoRollComponent::stepAt (int s) const
{
    if (bound == kBindSynth) return engine.getSynthStep (s);
    if (bound == kBindPiano) return engine.getPianoStep (s);
    return engine.getMelodyStep (s);
}

inline void PianoRollComponent::setStepAt (int s, int v)
{
    if (bound == kBindSynth) { engine.setSynthStep (s, v); return; }
    if (bound == kBindPiano) { engine.setPianoStep (s, v); return; }
    engine.setMelodyStep (s, v);
}

inline int PianoRollComponent::stampStartStep() const
{
    const int s = engine.currentStep();
    return (engine.isPlaying() && s >= 0) ? s : 0;
}

inline int PianoRollComponent::stampChord (int chordTypeIdx, int rootOffset, int startStep, int spanSteps)
{
    const auto& types = boombap::chordTypes();
    if (chordTypeIdx < 0 || chordTypeIdx >= (int) types.size()) return 0;
    if (! supportsMelody()) return 0;
    if (stepsMode) { stepsMode = false; updateModeUi(); }
    startStep = juce::jlimit (0, boombap::BoomBapEngine::kSteps - 1, startStep);
    const int span = juce::jlimit (1, boombap::BoomBapEngine::kSteps - startStep, spanSteps);
    if (onBeforeChange) onBeforeChange();
    for (int k = 0; k < span; ++k)
        setStepAt (startStep + k, boombap::chordStampNote (rootOffset, 0, types[(size_t) chordTypeIdx], k) + 25);
    if (onAfterChange) onAfterChange();
    if (grid != nullptr) grid->repaint();
    return span;
}

inline int PianoRollComponent::stampProgression (int progressionIdx, int keyRootOffset, int startStep)
{
    const auto& progs = boombap::progressions();
    const auto& types = boombap::chordTypes();
    if (progressionIdx < 0 || progressionIdx >= (int) progs.size()) return 0;
    if (! supportsMelody()) return 0;
    if (stepsMode) { stepsMode = false; updateModeUi(); }
    startStep = juce::jlimit (0, boombap::BoomBapEngine::kSteps - 1, startStep);
    const auto& prog = progs[(size_t) progressionIdx];
    const int nChords = (int) prog.chords.size();
    const int total = boombap::BoomBapEngine::kSteps - startStep;
    const int share = juce::jmax (1, total / nChords);
    if (onBeforeChange) onBeforeChange();
    int written = 0;
    for (int c = 0; c < nChords; ++c)
    {
        const int from = startStep + c * share;
        const int to = (c == nChords - 1) ? boombap::BoomBapEngine::kSteps : juce::jmin (boombap::BoomBapEngine::kSteps, from + share);
        const auto& chord = prog.chords[(size_t) c];
        for (int s = from; s < to; ++s)
        {
            setStepAt (s, boombap::chordStampNote (keyRootOffset, chord.rootOffset,
                                                   types[(size_t) chord.chordType], s - from) + 25);
            ++written;
        }
    }
    if (onAfterChange) onAfterChange();
    if (grid != nullptr) grid->repaint();
    return written;
}

inline void PianoRollComponent::refreshOctaveUi()
{
    octLabel.setText (octShift == 0 ? "OCT 0" : (octShift > 0 ? "OCT +1" : "OCT -1"),
                      juce::dontSendNotification);
    octDownButton.setEnabled (octShift > -12);
    octUpButton.setEnabled (octShift < 12);
}

inline void PianoRollComponent::refreshTitle()
{
    juce::String lane;
    if (bound == kBindSynth)      lane = "Synth - root C3";
    else if (bound == kBindPiano) lane = "Piano - root C4";
    else if (bound == boombap::BoomBapEngine::Kick) lane = "Kick melody - pitched 808";
    else if (bound >= boombap::BoomBapEngine::Sample1
             && bound < boombap::BoomBapEngine::kFirstOwnRow)
        lane = juce::String (engine.displayTrackName (bound)) + " melody - pitched pad";
    else if (bound >= boombap::BoomBapEngine::kFirstOwnRow)
        lane = juce::String (engine.displayTrackName (bound)) + " steps - this row's own sample";
    else
        lane = juce::String (engine.displayTrackName (bound)) + " steps";
    const juce::String pat = juce::String::charToString (
        (juce::juce_wchar) ('A' + engine.getEditPattern()));
    titleLabel.setText (lane + "  -  Pattern " + pat, juce::dontSendNotification);
}

inline void PianoRollComponent::layoutGrid()
{
    if (grid == nullptr) return;
    const auto size = grid->canvasSize();
    grid->setSize (size.getWidth(), size.getHeight());
    // the zoom buttons wear the current preset (free wheel zooms clear all)
    auto mark = [] (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     on ? boombap::ui::accent() : boombap::ui::panelAlt());
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colour (0xff14100a) : boombap::ui::text());
    };
    mark (zoom25, juce::approximatelyEqual (zoom, 0.25f));
    mark (zoom50, juce::approximatelyEqual (zoom, 0.5f));
    mark (zoom100, juce::approximatelyEqual (zoom, 1.0f));
    grid->repaint();
}

inline void PianoRollComponent::zoomAt (float newZoom, juce::Point<int> gridPoint)
{
    if (grid == nullptr) return;
    const auto oldSize = grid->getBounds();
    const auto viewPos = viewport.getViewPosition();
    const float fracX = oldSize.getWidth() > 0 ? (float) gridPoint.x / (float) oldSize.getWidth() : 0.0f;
    const float fracY = oldSize.getHeight() > 0 ? (float) gridPoint.y / (float) oldSize.getHeight() : 0.0f;
    const int relX = gridPoint.x - viewPos.x, relY = gridPoint.y - viewPos.y;
    setZoomFactor (newZoom);
    const auto newSize = grid->getBounds();
    viewport.setViewPosition (juce::roundToInt (fracX * (float) newSize.getWidth()) - relX,
                              juce::roundToInt (fracY * (float) newSize.getHeight()) - relY);
}

inline void PianoRollComponent::paint (juce::Graphics& g)
{
    g.fillAll (boombap::ui::bg());
}

inline void PianoRollComponent::resized()
{
    titleLabel.setBounds (16, 10, 264, 26);
    trackBox.setBounds (288, 10, 250, 26);
    modeToggle.setBounds (getWidth() - 336, 10, 184, 26);
    clearButton.setBounds (getWidth() - 136, 10, 120, 26);

    sourceLabel.setBounds (16, 42, 330, 24);
    octDownButton.setBounds (354, 42, 56, 24);
    octLabel.setBounds (414, 42, 56, 24);
    octUpButton.setBounds (474, 42, 56, 24);
    zoom25.setBounds (546, 42, 48, 24);
    zoom50.setBounds (598, 42, 48, 24);
    zoom100.setBounds (650, 42, 52, 24);

    chordLabel.setBounds (16, 74, 52, 24);
    chordRootBox.setBounds (72, 74, 62, 24);
    chordTypeBox.setBounds (138, 74, 108, 24);
    stampChordButton.setBounds (250, 74, 76, 24);
    progLabel.setBounds (342, 74, 96, 24);
    progBox.setBounds (442, 74, 196, 24);
    stampProgButton.setBounds (642, 74, 76, 24);

    viewport.setBounds (8, 106, getWidth() - 16, juce::jmax (80, getHeight() - 106 - 32));
    hint.setBounds (16, getHeight() - 28, getWidth() - 32, 22);
    layoutGrid();
}

inline void PianoRollComponent::timerCallback()
{
    const int s = engine.isPlaying() ? engine.currentStep() : -1;
    if (s != lastPlayStep)
    {
        lastPlayStep = s;
        if (grid != nullptr) grid->repaint();
    }
}

// The piano roll lives in its own window; closing it just hides it.
class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow (const juce::String& name, juce::Colour background)
        : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton)
    {
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (780, 540, 10000, 10000);
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
