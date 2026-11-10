#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Engine/BoomBapEngine.h"
#include "BoomBapLookAndFeel.h"
#include "UiComponents.h"

// The STEP SEQUENCER view of the Production Area (items 37/39/41/52):
// the pattern strip (A-H + add-track) over a vertically scrolling list of
// track rows. Every row carries Load (a sample for the row - on a kit row
// it replaces the built-in drum, item 40), ROLL (the piano roll bound to
// that row, item 41), MUTE/SOLO at a size you can actually hit (item 52),
// a level slider, a meter, and 32 step buttons (the pattern is two bars,
// item 48). The row list grows with engine.addTrack() and scrolls - the
// original 8 rows are just where it starts (item 39).
class SequencerPanel : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (int)> onLoadSample; // a row's Load... button
        std::function<void (int)> onRoll;       // a row's ROLL button
        // Round 6 (item 125 rider): the ghost strip is a DOOR - a
        // click anywhere on it asks for that track's piano roll,
        // the same summon the row's ROLL button makes.
        std::function<void (int)> onGhostRoll;
        std::function<void (int)> onNameMenu;   // a row's name was RIGHT-clicked (items 40/69; the menu moved off the left click, item 86)
        std::function<void (int)> onDeleteRow;  // a user row's visible X (item 87; the action is item 69's)
        std::function<void()> onPatternChanged; // pattern A-H switched (the roll follows)
        std::function<void()> onChange;         // a row was added
        std::function<void (bool)> onGhostToggled; // Round 4 (item 99a): persist the ghost strip's switch
        // Pass 2: undo gestures (item 38) + channel selection (the Station follows)
        std::function<void()> onBeforeChange;
        std::function<void()> onAfterChange;
        std::function<void (int)> onSelectRow;
        // Round 6 (item 124): the NAME click-to-select gesture (left
        // button only) also asks for an audition of the track's
        // sound. Separate from onSelectRow on purpose: that callback
        // fires for LOAD / ROLL / MUTE / SOLO clicks too, and those
        // gestures must stay silent - only the name tap auditions.
        std::function<void (int)> onSelectAudition;
        // Round 5 (item 107): a rename committed in the inline editor
        // (already trimmed; empty = clear the override).
        std::function<void (int, const juce::String&)> onRenameTrack;
    };

    SequencerPanel (boombap::BoomBapEngine& eng, Callbacks cb)
        : engine (eng), callbacks (std::move (cb))
    {
        patternLabel.setText ("PATTERN", juce::dontSendNotification);
        patternLabel.setColour (juce::Label::textColourId, boombap::ui::muted());
        patternLabel.setFont (juce::Font (11.0f, juce::Font::bold));
        addAndMakeVisible (patternLabel);
        for (int i = 0; i < 8; ++i)
        {
            auto& b = patternButtons[(size_t) i];
            b.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
            b.onClick = [this, i]
            {
                engine.setEditPattern (i);
                refreshGrid();
                refreshPatternUi();
                if (callbacks.onPatternChanged) callbacks.onPatternChanged();
            };
            addAndMakeVisible (b);
        }
        boombap::ui::applyActionTrace (addTrackButton); // item 89: ADD TRACK is an action - amber trace
        addTrackButton.onClick = [this]
        {
            if (callbacks.onBeforeChange) callbacks.onBeforeChange();
            if (engine.addTrack() >= 0)
            {
                layoutRows();
                refreshGrid();
                if (callbacks.onChange) callbacks.onChange();
            }
            if (callbacks.onAfterChange) callbacks.onAfterChange();
        };
        addAndMakeVisible (addTrackButton);

        // Round 4 (item 99a): the ghost roll's switch. The strip itself
        // is laid out by layoutRows (it borrows height only when the
        // selected row owns a pitched lane).
        ghostToggle.setButtonText ("Ghost roll");
        ghostToggle.setToggleState (ghostEnabled, juce::dontSendNotification);
        ghostToggle.onClick = [this]
        {
            ghostEnabled = ghostToggle.getToggleState();
            layoutRows();
            if (callbacks.onGhostToggled) callbacks.onGhostToggled (ghostEnabled);
        };
        addAndMakeVisible (ghostToggle);
        gridContent.addAndMakeVisible (ghostStrip);
        // Item 125 rider: clicking the strip summons the roll of the
        // track it belongs to (the strip's track is the selected row).
        ghostStrip.onClicked = [this]
        {
            if (callbacks.onGhostRoll && ghostStrip.trackForTest() >= 0)
                callbacks.onGhostRoll (ghostStrip.trackForTest());
        };

        viewport.setViewedComponent (&gridContent, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (12);
        addAndMakeVisible (viewport);

        for (int t = 0; t < kTracks; ++t)
        {
            Row& row = rows[t];
            gridContent.addAndMakeVisible (row.name);
            row.name.setText (juce::String (engine.displayTrackName (t)), juce::dontSendNotification);
            row.name.setColour (juce::Label::textColourId, juce::Colours::white);
            row.name.setFont (juce::Font (13.0f, juce::Font::bold));
            // Pass 2: any row interaction selects the row's channel (the
            // Station follows); kit rows also keep their name menu (item 40)
            row.name.addMouseListener (this, false);

            gridContent.addAndMakeVisible (row.load);
            row.load.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onLoadSample) callbacks.onLoadSample (t);
            };

            gridContent.addAndMakeVisible (row.roll);
            row.roll.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onRoll) callbacks.onRoll (t);
            };

            gridContent.addAndMakeVisible (row.mute);
            row.mute.setClickingTogglesState (true);
            row.mute.setColour (juce::TextButton::buttonOnColourId, juce::Colours::indianred);
            row.mute.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onBeforeChange) callbacks.onBeforeChange();
                engine.setTrackMute (t, rows[t].mute.getToggleState());
                if (callbacks.onAfterChange) callbacks.onAfterChange();
            };

            gridContent.addAndMakeVisible (row.solo);
            row.solo.setClickingTogglesState (true);
            row.solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa8871a));
            row.solo.onClick = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onBeforeChange) callbacks.onBeforeChange();
                engine.setTrackSolo (t, rows[t].solo.getToggleState());
                if (callbacks.onAfterChange) callbacks.onAfterChange();
            };

            // Round 1 (item 87): a VISIBLE delete on the row itself -
            // item 69's action used to live only in the name menu and
            // the owner could not find it. The button exists on every
            // row but only SHOWS on user-added rows; item 69's
            // protections decide that in layoutRows.
            gridContent.addAndMakeVisible (row.del);
            row.del.onClick = [this, t]
            {
                if (callbacks.onDeleteRow) callbacks.onDeleteRow (t);
            };

            gridContent.addAndMakeVisible (row.level);
            gridContent.addAndMakeVisible (row.meter);
            row.level.setSliderStyle (juce::Slider::LinearHorizontal);
            row.level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row.level.setRange (0.0, 1.2, 0.01);
            row.level.setValue (0.8, juce::dontSendNotification);
            row.level.setDoubleClickReturnValue (true, 0.8); // double-click reset, app-wide
            row.level.onDragStart = [this, t]
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                if (callbacks.onBeforeChange) callbacks.onBeforeChange();
            };
            row.level.onDragEnd = [this] { if (callbacks.onAfterChange) callbacks.onAfterChange(); };
            row.level.onValueChange = [this, t] { engine.setTrackGain (t, (float) rows[t].level.getValue()); };

            for (int s = 0; s < kSteps; ++s)
            {
                auto& sb = row.steps[s];
                gridContent.addAndMakeVisible (sb);
                sb.setBeatShade ((s / 4) % 2 == 0);
                sb.onChange = [this, t, s] (int v)
                {
                    if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                    // Fix Pass 3 (item 76): while a swipe is held, the
                    // gesture the press opened stays open - the press's
                    // own begin/end step aside until the release.
                    if (! swipeHeld && callbacks.onBeforeChange) callbacks.onBeforeChange();
                    engine.setStep (t, s, v);
                    if (! swipeHeld && callbacks.onAfterChange) callbacks.onAfterChange();
                };
                // Item 76: left-drag paints steps ON (with the value the
                // press produced: normal, or accent if the press cycled
                // to it); right-drag erases. FL grammar, D's hands.
                sb.onSwipeStart = [this, t, s] (int v)
                {
                    swipeHeld = true;
                    swipeTrack = t;
                    swipeValue = v;
                    lastSwipeStep = s;
                    if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                    if (callbacks.onBeforeChange) callbacks.onBeforeChange();
                };
                sb.onSwipeMove = [this] (juce::Point<float> p) { applySwipe (p); };
                sb.onSwipeEnd = [this]
                {
                    if (! swipeHeld) return;
                    swipeHeld = false;
                    if (callbacks.onAfterChange) callbacks.onAfterChange();
                };
            }
        }
        refreshPatternUi();
        refreshGrid();
    }

    ~SequencerPanel() override
    {
        for (int t = 0; t < kTracks; ++t)
            rows[t].name.removeMouseListener (this);
    }

    void refreshGrid()
    {
        const int n = engine.getNumTracks();
        for (int t = 0; t < n; ++t)
            for (int s = 0; s < kSteps; ++s)
                rows[t].steps[s].setVelocity (engine.getStep (t, s));
        ghostStrip.repaint();
        // Round 6 (item 125): lane CONTENT is the strip's on/off law,
        // and content changes outside selection changes too (roll
        // edits, pattern switches, undo) - re-run the layout so the
        // band appears/retires with the lane it belongs to.
        layoutRows();
    }

    // Round 4 (item 99a): the ghost strip's switch, settable from the
    // owner's persisted preference as well as the header toggle.
    void setGhostEnabled (bool on)
    {
        ghostEnabled = on;
        ghostToggle.setToggleState (on, juce::dontSendNotification);
        layoutRows();
    }
    bool isGhostEnabled() const { return ghostEnabled; }
    bool ghostStripShowing() const { return ghostStrip.isVisible(); }
    juce::Component& ghostStripForTest() { return ghostStrip; }
    const Callbacks& callbacksForTest() const { return callbacks; }
    juce::Component& rowNameComponent (int t) { return rows[juce::jlimit (0, kTracks - 1, t)].name; }

    void refreshPatternUi()
    {
        const int cur = engine.getEditPattern();
        for (int i = 0; i < 8; ++i)
        {
            auto& b = patternButtons[(size_t) i];
            const bool sel = i == cur;
            b.setColour (juce::TextButton::buttonColourId, sel ? boombap::ui::accent() : boombap::ui::panelAlt());
            b.setColour (juce::TextButton::textColourOffId, sel ? juce::Colour (0xff14100a) : boombap::ui::text());
        }
    }

    void refreshMixerFromSettings (const boombap::ProjectSettings& s)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            rows[t].mute.setToggleState (s.trackMute[t], juce::dontSendNotification);
            rows[t].solo.setToggleState (s.trackSolo[t], juce::dontSendNotification);
            rows[t].level.setValue (s.trackGain[t], juce::dontSendNotification);
        }
        layoutRows();
        refreshPatternUi();
        refreshGrid();
        refreshAllRowNames(); // item 88: a project apply renames rows too
    }

    // Round 3 (item 88): the row's FIRST COLUMN carries the track's
    // name - its content's name when it has content, TRACK n until
    // then. The LOAD button is not a name tag: it says "Load..." on
    // the day the row is created and "Load..." forever after (Round 1
    // made the same rule law for the waveform panel). Names re-read
    // from the engine, the one place they live.
    void refreshRowName (int track)
    {
        if (track >= 0 && track < kTracks)
            rows[track].name.setText (juce::String (engine.displayTrackName (track)),
                                      juce::dontSendNotification);
    }

    void refreshAllRowNames()
    {
        for (int t = 0; t < kTracks; ++t) refreshRowName (t);
    }

    // Round 6 (item 114): which track's row a SCREEN point falls in -
    // the waveform panel's drag asks on drop. Rows are children of the
    // grid content, so the point is read in that space; anything
    // outside the grid is -1.
    int trackAtScreenPosition (juce::Point<int> screenPos) const
    {
        const auto p = gridContent.getLocalPoint (nullptr, screenPos);
        if (! gridContent.getLocalBounds().contains (p)) return -1;
        for (int t = 0; t < kTracks; ++t)
            if (p.getY() >= rowTopY (t) && p.getY() < rowTopY (t) + lastRowH) return t;
        return -1;
    }

    // Round 1 (item 86): the selected row's NAME lights - solid accent
    // fill, dark text (the item 68 SELECTED language) - so the track
    // you are working on is visible in the grid itself. Selection is
    // one state owned by MainComponent::selectChannel; this panel is
    // a mirror, set from there, whichever view made the selection.
    // A channel with no sequencer row (synth, master...) lights none.
    void setSelectedRow (int track)
    {
        selectedRow = (track >= 0 && track < kTracks) ? track : -1;
        for (int t = 0; t < kTracks; ++t)
        {
            const bool sel = t == selectedRow;
            rows[t].name.setColour (juce::Label::backgroundColourId,
                                    sel ? boombap::ui::accent() : juce::Colours::transparentBlack);
            rows[t].name.setColour (juce::Label::textColourId,
                                    sel ? juce::Colour (0xff14100a) : juce::Colours::white);
        }
        ghostStrip.setTrack (selectedRow);
        layoutRows(); // the ghost band borrows its height from the selected row
    }
    int getSelectedRow() const { return selectedRow; }

    // Row parts, reachable for the component tests (and any future
    // view code that needs them).
    juce::Label& rowNameLabel (int track)       { return rows[track].name; }
    juce::TextButton& rowDeleteButton (int track) { return rows[track].del; }
    juce::TextButton& rowLoadButton (int track)   { return rows[track].load; }

    // Called by the owner's 30 Hz timer: playhead column + row meters.
    void tick()
    {
        const int n = engine.getNumTracks();
        const int s = engine.currentStep();
        if (s != lastPlayStep)
        {
            lastPlayStep = s;
            for (int t = 0; t < n; ++t)
                for (int st = 0; st < kSteps; ++st)
                    rows[t].steps[st].setPlaying (st == s);
            ghostStrip.repaint();
        }
        for (int t = 0; t < n; ++t)
        {
            const float peak = engine.consumeTrackPeak (t);
            meterShown[t] = std::max (peak, meterShown[t] * 0.82f);
            rows[t].meter.setLevel (meterShown[t]);
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace boombap::ui;
        g.setColour (muted());
        g.setFont (11.0f);
        g.drawText ("Click a step: normal, accent, off.  Drag paints steps on - right-drag erases.  Steps 1-16 = bar 1, 17-32 = bar 2.",
                    getWidth() / 3, 6, getWidth() / 3 + 130, 22, juce::Justification::centredRight);

        // bar/beat numbers over the step columns, aligned with the grid
        g.setFont (12.0f);
        const float cx = contentScaleX;
        for (int s = 0; s < kSteps; s += 4)
        {
            const float x = (float) viewport.getX() + stepX (s) * cx;
            g.setColour (s == 16 ? accent() : muted());
            g.drawText (juce::String (s / 4 + 1) + (s == 16 ? " (bar 2)" : ""),
                        (int) x, 34, 70, 14, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        const float sx = (float) getWidth() / 1544.0f;
        const float sy = (float) getHeight() / 340.0f;
        auto RB = [&] (int x, int y, int w, int h)
        {
            return juce::Rectangle<int> (juce::roundToInt (x * sx), juce::roundToInt (y * sy),
                                         juce::roundToInt (w * sx), juce::roundToInt (h * sy));
        };
        scaleY = sy;
        patternLabel.setBounds (RB (12, 4, 70, 24));
        for (int i = 0; i < 8; ++i)
            patternButtons[(size_t) i].setBounds (RB (88 + i * 40, 4, 36, 24));
        addTrackButton.setBounds (RB (1396, 4, 132, 24));
        ghostToggle.setBounds (RB (1224, 4, 160, 24));
        viewport.setBounds (RB (8, 50, 1528, 282));
        layoutRows();
    }

    // (Component is a MouseListener; row name clicks arrive here.)
    // Round 1 (item 86): a LEFT click on a name SELECTS the track -
    // nothing else. It used to also pop the row's menu, which made
    // "select this track" and "load a sample" the same gesture in
    // the owner's hands. The menu (item 40's kit menu, item 69's row
    // menu) moved to RIGHT-click; the row still selects first, so
    // the menu opens on the track you are looking at.
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int t = 0; t < kTracks; ++t)
            if (e.eventComponent == &rows[t].name)
            {
                if (callbacks.onSelectRow) callbacks.onSelectRow (t);
                // Round 6 (item 124): a LEFT-button tap on the name
                // auditions the track's sound - the click-to-select
                // gesture only. The right-click's job is the menu;
                // it selects (above) but stays silent.
                if (! e.mods.isRightButtonDown() && callbacks.onSelectAudition)
                    callbacks.onSelectAudition (t);
                // Round 5 (item 107): EVERY row's right-click opens
                // its menu now - the pad rows (5..20) had none, and
                // rename belongs on all of them.
                if (e.mods.isRightButtonDown() && callbacks.onNameMenu)
                    callbacks.onNameMenu (t);
                return;
            }
    }

    // Round 5 (item 107): the inline rename editor, opened over the
    // row's name by the owner's menu. Prefilled with the CURRENT
    // custom name (the derived name shows as the placeholder, so
    // clearing the box and committing is visibly "back to the file
    // name"). Enter commits, Esc cancels, losing focus commits -
    // the JUCE editor convention; a guard keeps a commit single.
    void showRenameEditor (int track)
    {
        if (track < 0 || track >= kTracks) return;
        cancelRenameEditor();
        renameTrack = track;
        renameEditor = std::make_unique<juce::TextEditor>();
        auto& ed = *renameEditor;
        ed.setText (juce::String (engine.trackCustomName (track)), false);
        ed.setTextToShowWhenEmpty (juce::String (engine.displayTrackName (track)),
                                   boombap::ui::muted());
        ed.setSelectAllWhenFocused (true);
        ed.setFont (juce::Font (13.0f));
        ed.setColour (juce::TextEditor::backgroundColourId, boombap::ui::panelAlt());
        ed.setColour (juce::TextEditor::textColourId, boombap::ui::text());
        ed.setColour (juce::TextEditor::highlightColourId, boombap::ui::accent());
        ed.setBounds (rows[track].name.getBounds());
        ed.onReturnKey = [this] { commitRenameEditor(); };
        ed.onEscapeKey = [this] { cancelRenameEditor(); };
        ed.onFocusLost = [this] { commitRenameEditor(); };
        gridContent.addAndMakeVisible (ed);
        ed.grabKeyboardFocus();
    }
    bool renameEditorOpen() const { return renameEditor != nullptr; }
    // Test seams: the same commit/cancel the editor's keys run.
    void commitRenameForTest() { commitRenameEditor(); }
    void cancelRenameForTest() { cancelRenameEditor(); }
    juce::TextEditor* renameEditorForTest() { return renameEditor.get(); }

private:
    void commitRenameEditor()
    {
        if (renameEditor == nullptr || renameCommitting) return;
        renameCommitting = true;
        const int track = renameTrack;
        const juce::String text = renameEditor->getText().trim();
        renameEditor.reset();
        renameTrack = -1;
        renameCommitting = false;
        if (callbacks.onRenameTrack) callbacks.onRenameTrack (track, text);
    }
    void cancelRenameEditor()
    {
        if (renameEditor == nullptr) return;
        renameEditor.reset();
        renameTrack = -1;
    }
    static constexpr int kTracks = boombap::BoomBapEngine::kTracks;
    static constexpr int kSteps  = boombap::BoomBapEngine::kSteps;

    struct Row
    {
        juce::Label name;
        juce::TextButton load { "Load..." };
        juce::TextButton roll { "ROLL" };
        juce::TextButton mute { "M" };
        juce::TextButton solo { "S" };
        juce::TextButton del { "X" }; // item 87: visible on user-added rows only
        juce::Slider level;
        LevelMeter meter;
        StepButton steps[kSteps];
    };

    // Step column geometry in the content's virtual space (1528 wide):
    // 32 steps of 32 px with a small gap, a wider gap every beat, the
    // meter at the right edge.
    static float stepX (int s) { return 382.0f + (float) s * 33.0f + (float) (s / 4) * 5.0f; }

    // Fix Pass 3 (item 76): paint the swipe value across every step the
    // drag crosses, on the row the press started on. Cells are SET,
    // never toggled - re-entering a cell (above all the starting cell)
    // cannot flip it back, which is the classic paint-drag bug - and
    // the walk from the last cell fills gaps on fast drags.
    // The top of a row in content space: rows below the selected one
    // sit one ghost band lower while the strip is showing.
    int rowTopY (int t) const
    {
        return t * lastRowH + (t > selectedRow ? layoutGhostH : 0);
    }

    void applySwipe (juce::Point<float> p)
    {
        if (! swipeHeld || swipeTrack < 0 || lastRowH <= 0) return;
        const int rowY = rowTopY (swipeTrack);
        if (p.y < (float) rowY || p.y >= (float) (rowY + lastRowH)) return; // the press's own row only
        const float vx = p.x / contentScaleX;
        int s = -1;
        for (int i = 0; i < kSteps; ++i)
            if (vx >= stepX (i) && vx < stepX (i) + 32.0f) { s = i; break; }
        if (s < 0) return;
        const int from = lastSwipeStep;
        const int dir = s >= from ? 1 : -1;
        for (int i = from;; i += dir)
        {
            if (engine.getStep (swipeTrack, i) != swipeValue)
            {
                engine.setStep (swipeTrack, i, swipeValue);
                rows[swipeTrack].steps[i].setVelocity (swipeValue);
            }
            if (i == s) break;
        }
        lastSwipeStep = s;
    }

    void layoutRows()
    {
        const int n = engine.getNumTracks();
        const int rowH = juce::jmax (28, juce::roundToInt (35.0f * scaleY));
        lastRowH = rowH;
        // Round 4 (item 99a) as ruled by Round 6 (item 125): the
        // ghost roll is a property of a TRACK, earned by content -
        // the band exists only while the selected row's OWN lane
        // holds notes. Selecting a track that never used the
        // keyboard/piano roll never raises it, however the switch
        // sits; the switch remains the owner's master off.
        const bool ghostActive = ghostEnabled && selectedRow >= 0
                              && engine.trackHasGhostContent (selectedRow);
        layoutGhostH = ghostActive ? 20 : 0;
        gridContent.setSize (juce::jmax (200, viewport.getWidth()),
                             juce::jmax (viewport.getHeight(), n * rowH + layoutGhostH));
        contentScaleX = (float) gridContent.getWidth() / 1528.0f;
        const float cx = contentScaleX;
        auto CB = [&] (float x, int y, float w, int h)
        {
            return juce::Rectangle<int> (juce::roundToInt (x * cx), y,
                                         juce::roundToInt (w * cx), h);
        };
        for (int t = 0; t < kTracks; ++t)
        {
            Row& row = rows[t];
            const bool vis = t < n;
            row.name.setVisible (vis);
            row.load.setVisible (vis);
            row.roll.setVisible (vis);
            row.mute.setVisible (vis);
            row.solo.setVisible (vis);
            // item 87: the delete X shows ONLY on rows item 69 allows
            // deleting (user-added rows) - protected rows carry no
            // affordance at all, rather than a dead button.
            row.del.setVisible (vis && t >= boombap::BoomBapEngine::kFirstOwnRow);
            row.level.setVisible (vis);
            row.meter.setVisible (vis);
            for (int s = 0; s < kSteps; ++s) row.steps[s].setVisible (vis);
            if (! vis) continue;
            const int y = rowTopY (t);
            const int h = rowH - 5;
            row.name.setBounds (CB (12, y + 2, 88, h));
            row.load.setBounds (CB (104, y + 2, 54, h));
            row.roll.setBounds (CB (162, y + 2, 44, h));
            row.mute.setBounds (CB (210, y + 2, 36, h));
            row.solo.setBounds (CB (250, y + 2, 36, h));
            row.level.setBounds (CB (292, y + 2, 56, h));
            row.del.setBounds (CB (352, y + 2, 24, h));
            for (int s = 0; s < kSteps; ++s)
                row.steps[s].setBounds (CB (stepX (s), y + 2, 32, h));
            row.meter.setBounds (CB (1476, y + rowH / 2 - 4, 44, 8));
        }
        // Round 4 (item 99a): the strip sits in the band under the
        // selected row's steps, spanning exactly the step columns.
        ghostStrip.setVisible (layoutGhostH > 0);
        if (layoutGhostH > 0)
        {
            const float cx = contentScaleX;
            const int y = rowTopY (selectedRow) + rowH - 2;
            ghostStrip.setBounds (juce::roundToInt (stepX (0) * cx), y,
                                  juce::roundToInt ((stepX (kSteps - 1) + 32.0f - stepX (0)) * cx), 18);
        }
        repaint();
    }

    // Round 4 (item 99a): the GHOST STRIP - the selected row's pitched
    // lane drawn lightly under its step boxes: one mini tick per step,
    // its height inside the strip following the note's pitch. It reads
    // the engine live (ghostNoteValue is the data path), so roll edits
    // show here without a second copy of the data.
    class GhostStrip : public juce::Component
    {
    public:
        explicit GhostStrip (boombap::BoomBapEngine& eng) : engine (eng) {}
        void setTrack (int t) { track = t; repaint(); }
        int trackForTest() const { return track; }
        // Item 125 rider: the strip is clickable end to end; the
        // panel routes the click to its onGhostRoll callback.
        std::function<void()> onClicked;
        void mouseDown (const juce::MouseEvent&) override { if (onClicked) onClicked(); }
        void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            using namespace boombap::ui;
            if (track < 0) return;
            const float w = (float) getWidth(), h = (float) getHeight();
            g.setColour (border().withAlpha (0.35f));
            g.fillRect (0.0f, h - 1.0f, w, 1.0f);
            const int playStep = engine.isPlaying() ? engine.currentStep() : -1;
            // The strip draws the lane's CONTOUR: its pitch range is
            // auto-ranged to the strip's height (an absolute ±24 map
            // squeezed close intervals into a single pixel - a flat
            // line that said "notes exist" and nothing about the tune).
            int lo = 1000, hi = -1000;
            for (int s = 0; s < boombap::BoomBapEngine::kSteps; ++s)
            {
                const int v = engine.ghostNoteValue (track, s);
                if (v == 0) continue;
                lo = juce::jmin (lo, v - 25);
                hi = juce::jmax (hi, v - 25);
            }
            if (hi < lo) return; // the lane is empty under this row
            if (hi == lo) { lo -= 1; hi += 1; }
            // Ticks sit under their own step boxes: the columns are not
            // uniform (a wider gap every beat), so the strip maps the
            // panel's own stepX across its width instead of dividing it.
            const float span = stepX (boombap::BoomBapEngine::kSteps - 1) + 32.0f - stepX (0);
            for (int s = 0; s < boombap::BoomBapEngine::kSteps; ++s)
            {
                const int v = engine.ghostNoteValue (track, s);
                if (v == 0) continue;
                const float x = (stepX (s) - stepX (0)) / span * w;
                const float cellW = 32.0f / span * w;
                const float semitone = (float) (v - 25);
                const float y = (h - 4.0f) - ((semitone - (float) lo) / (float) (hi - lo)) * (h - 5.0f);
                g.setColour (accent().withAlpha (s == playStep ? 0.95f : 0.5f));
                g.fillRect (x + cellW * 0.18f, y, cellW * 0.64f, 2.2f);
            }
        }

    private:
        boombap::BoomBapEngine& engine;
        int track = -1;
    };

    boombap::BoomBapEngine& engine;
    Callbacks callbacks;

    juce::Label patternLabel;
    std::array<juce::TextButton, 8> patternButtons;
    juce::TextButton addTrackButton { "+ TRACK" };
    juce::ToggleButton ghostToggle;
    juce::Viewport viewport;
    juce::Component gridContent;
    GhostStrip ghostStrip { engine };
    Row rows[kTracks];
    bool ghostEnabled = true; // item 99a: the default is ruled by the round's screenshot verdict
    int layoutGhostH = 0;

    float scaleY = 1.0f, contentScaleX = 1.0f;
    float meterShown[kTracks] = {};
    int lastPlayStep = -2;
    int selectedRow = -1; // item 86: mirrored by the owner (see setSelectedRow)
    // Item 76 swipe state (see applySwipe).
    bool swipeHeld = false;
    int swipeTrack = -1, swipeValue = 0, lastSwipeStep = -1, lastRowH = 0;
    // Round 5 (item 107): the inline rename editor's state.
    std::unique_ptr<juce::TextEditor> renameEditor;
    int renameTrack = -1;
    bool renameCommitting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerPanel)
};
