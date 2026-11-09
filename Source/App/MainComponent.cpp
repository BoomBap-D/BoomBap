#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "Multitrack.h"
#include "SequencerPanel.h"
#include "StationPanel.h"
#include "AppSettings.h"
#include "DataHome.h"

using namespace juce;

namespace
{
// The audio-device setup window (Pass 1 item 8): hosts JUCE's device
// selector; closing it saves the device state so the choice persists.
class AudioSettingsWindow : public juce::DocumentWindow
{
public:
    AudioSettingsWindow (const String& name, std::function<void()> onClose)
        : DocumentWindow (name, boombap::ui::bg(), juce::DocumentWindow::closeButton),
          closeCallback (std::move (onClose))
    {
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (480, 400, 1200, 900);
    }

    // Round 1 (item 102): only the main window appears in the Windows
    // taskbar. Without windowAppearsOnTaskbar the Windows peer builds
    // this window with WS_EX_TOOLWINDOW - no taskbar slot of its own.
    int getDesktopWindowStyleFlags() const override
    {
        return juce::DocumentWindow::getDesktopWindowStyleFlags()
                 & ~juce::ComponentPeer::windowAppearsOnTaskbar;
    }

    void closeButtonPressed() override
    {
        if (closeCallback) closeCallback();
        setVisible (false);
    }

private:
    std::function<void()> closeCallback;
};

// A Production Area view popped out into its own window (items 37/42):
// closing the window docks the view back onto its tab.
class PopOutWindow : public juce::DocumentWindow
{
public:
    PopOutWindow (const String& name, std::function<void()> onClose)
        : DocumentWindow (name, boombap::ui::bg(), juce::DocumentWindow::closeButton),
          closeCallback (std::move (onClose))
    {
        boombap::ui::applyAppWindowFrame (*this, true); // items 77/78: own dark frame, floats over main
        setResizable (true, true);
        setResizeLimits (560, 320, 4000, 3000);
    }

    // Round 1 (item 102): no taskbar slot of its own (see
    // AudioSettingsWindow's note - the same one-line rule everywhere).
    int getDesktopWindowStyleFlags() const override
    {
        return juce::DocumentWindow::getDesktopWindowStyleFlags()
                 & ~juce::ComponentPeer::windowAppearsOnTaskbar;
    }

    // Item 66: the component whose lifetime the deferred dock depends on
    // (the MainComponent). If it is gone when the deferred call lands,
    // the call must not happen.
    void setDockGuard (juce::Component* ownerComponent) { dockGuard = ownerComponent; }

    void closeButtonPressed() override
    {
        // Item 66: the dock callback DESTROYS this window (dockTab resets
        // productionWindows[index]). Running it synchronously here meant
        // the window was deleted inside its own close handler, leaving
        // the close machinery executing on a freed object - one half of
        // the dock-back crash. Hide now (what the user sees is the window
        // closing) and dock on the next message turn, from a clean stack.
        setVisible (false);
        auto cb = closeCallback;
        auto guard = dockGuard;
        juce::MessageManager::callAsync ([cb, guard] { if (guard != nullptr && cb) cb(); });
    }

private:
    std::function<void()> closeCallback;
    juce::Component::SafePointer<juce::Component> dockGuard;
};
} // namespace

//==============================================================================
MainComponent::MainComponent (boombap::BoomBapEngine& eng, bool useOwnAudioDevice)
    : engine (eng), ownAudioDevice (useOwnAudioDevice)
{
    formats.registerBasicFormats();
    boombap::ensureDataHome(); // item 62: Documents/BoomBap exists from first run, both shells
    // Pass 2 (item 38): the undo history snapshots the whole project JSON;
    // applying one goes back through the normal project-load path.
    history.setCallbacks ([this] { return currentJson(); },
                          [this] (const juce::String& json) { applyHistoryJson (json); });
    setLookAndFeel (&lookAndFeel);
    setSize (1560, 900); // Pass 1 item 1: landscape design size; the layout scales to fit smaller windows
    setWantsKeyboardFocus (true);

    // ---- project bar ----
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &recentButton })
        addAndMakeVisible (b);
    boombap::ui::applyActionTrace (audioButton); // item 89: window openers are actions - amber trace
    addAndMakeVisible (audioButton);
    audioButton.setVisible (ownAudioDevice); // in a plugin the host owns the device
    audioButton.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible (projectName);
    projectName.setColour (Label::textColourId, Colours::white);
    projectName.setFont (juce::Font (15.0f));
    newButton.onClick    = [this] { confirmDiscardThen ([this] { newProject(); }); };
    openButton.onClick   = [this] { confirmDiscardThen ([this] { openProjectChooser(); }); };
    saveButton.onClick   = [this] { saveProject ({}); };
    saveAsButton.onClick = [this] { saveProjectAs ({}); };
    recentButton.onClick = [this] { showRecentMenu(); };

    // Pass 2: the Station opens from the project bar (item 19)
    boombap::ui::applyActionTrace (stationButton);
    addAndMakeVisible (stationButton);
    stationButton.onClick = [this] { openStation(); };

    // ---- transport (Pass 1 item 2: big labelled MPC-style buttons;
    // Round 2 item 93 completes the row: TO START / REW / PLAY / STOP /
    // FF / TO END, with REW/FF stepping one step, or one bar on a
    // double press) ----
    {
        auto props = boombap::appPropertiesFile();
        clockFormat = juce::jlimit (0, 2, props->getIntValue ("clockFormat", 0));
    }
    addAndMakeVisible (toStartButton);
    addAndMakeVisible (rewButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (ffButton);
    addAndMakeVisible (toEndButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (resetButton);
    playButton.setColour (TextButton::buttonColourId, boombap::ui::accent());
    playButton.setColour (TextButton::textColourOffId, Colour (0xff14100a));
    toStartButton.onClick = [this] { navToStart(); };
    rewButton.onClick     = [this] { navRewind(); };
    playButton.onClick    = [this] { startTransport(); };
    stopButton.onClick    = [this] { stopTransport(); };
    ffButton.onClick      = [this] { navForward(); };
    toEndButton.onClick   = [this] { navToEnd(); };
    // the project bar's compact clock - the Station's big clock's twin
    mainClock.onCycleFormat = [this] { setClockFormat ((clockFormat + 1) % 3); };
    addAndMakeVisible (mainClock);
    clearButton.onClick = [this] { runUndoable ([this] { engine.clearPattern(); refreshGrid(); }); };
    resetButton.onClick = [this] { runUndoable ([this] { engine.loadDefaultPattern(); refreshGrid(); }); };

    // ---- metronome (Pass 1 item 5) + pad activity (item 4b) ----
    addAndMakeVisible (metroButton);
    metroButton.setClickingTogglesState (true);
    metroButton.onClick = [this] { engine.setMetronomeOn (metroButton.getToggleState()); };
    addAndMakeVisible (metroVol.slider);
    setupSlider (metroVol, "Click", 0.0, 1.0, 0.8, 0.01, "",
                 [this] { engine.setMetronomeLevel ((float) metroVol.slider.getValue()); });
    addAndMakeVisible (padLight);
    addAndMakeVisible (padMeter);

    addAndMakeVisible (bpm.slider);
    addAndMakeVisible (swing.slider);
    addAndMakeVisible (master.slider);
    setupSlider (bpm,    "BPM",    40.0, 220.0, 90.0, 1.0, "",    [this] { engine.setBpm ((float) bpm.slider.getValue()); });
    setupSlider (swing,  "Swing",  50.0, 75.0,  58.0, 1.0, " %",  [this] { engine.setSwing ((float) swing.slider.getValue()); });
    setupSlider (master, "Master", -24.0, 6.0,  0.0,  0.1, " dB", [this] { engine.setMasterDb ((float) master.slider.getValue()); });

    // ---- master effects ----
    addAndMakeVisible (dustToggle);
    addAndMakeVisible (glueToggle);
    addAndMakeVisible (glueMeter);
    addAndMakeVisible (glueBar);
    glueToggle.setToggleState (true, dontSendNotification);
    dustToggle.onClick = [this] { runUndoable ([this] { engine.setDustEnabled (dustToggle.getToggleState()); }); };
    glueToggle.onClick = [this] { runUndoable ([this] { engine.setGlueEnabled (glueToggle.getToggleState()); }); };
    glueMeter.setJustificationType (Justification::centredLeft);
    glueMeter.setColour (Label::textColourId, Colours::lightgrey);

    // ---- waveform panel ----
    for (int i = 0; i < 3; ++i)
    {
        waveSel[i].setButtonText ("S" + String (i + 1));
        waveSel[i].setClickingTogglesState (true);
        waveSel[i].setRadioGroupId (77);
        waveSel[i].onClick = [this, i] { showWaveform (i); };
        addAndMakeVisible (waveSel[i]);
    }
    waveName.setColour (Label::textColourId, boombap::ui::muted());
    addAndMakeVisible (waveName);
    addAndMakeVisible (waveView);
    // Round 1 (item 89): with a sample loaded, clicking the waveform
    // AUDITIONS it (item 46's behaviour, moved onto the wave itself -
    // click again to stop, and the global stop silences it like
    // everything else). An EMPTY panel keeps item 74's click-to-load:
    // there is nothing to hear, so the click loads.
    waveView.onClick = [this]
    {
        if (waveData[waveTrack].empty())
        {
            openSampleChooser (boombap::BoomBapEngine::Sample1 + waveTrack);
            return;
        }
        if (engine.isAuditionPlaying()) { engine.stopAudition(); return; }
        engine.startAudition (waveData[waveTrack],
                              waveRates[waveTrack] > 0.0 ? waveRates[waveTrack] : engine.getSampleRate());
    };
    // Round 3 (item 100): drag the shown sample onto a pad - the pad
    // loads it through the ordinary pad-load path (same decode, same
    // undo gesture as picking it from the chooser). The ghost names
    // the sample while it travels; the pad under the pointer lights.
    waveView.onDragStart = [this] (juce::Point<int> pos)
    {
        const File f = samplePaths[(size_t) waveTrack];
        if (f == File()) return;
        waveDragGhost.setText (f.getFileNameWithoutExtension());
        waveDragGhost.showAt (pos);
        moveWaveDrag (pos);
    };
    waveView.onDragMove = [this] (juce::Point<int> pos) { moveWaveDrag (pos); };
    waveView.onDragEnd = [this] (juce::Point<int> pos)
    {
        const int pad = waveDragPadAt (pos);
        waveDragGhost.hideGhost();
        if (padBank != nullptr) padBank->setDragHighlightPad (-1);
        if (mpcPanel != nullptr) mpcPanel->setDragHighlightPad (-1);
        const File f = samplePaths[(size_t) waveTrack];
        if (pad >= 0 && f != File()) loadPadFile (pad, f);
    };
    showWaveform (0);

    // Round 1 (item 89): the panel's button is LOAD now - the
    // dedicated load path (the audition lives on the wave, above).
    addAndMakeVisible (wavePlayButton);
    boombap::ui::applyActionTrace (wavePlayButton);
    wavePlayButton.onClick = [this]
    {
        openSampleChooser (boombap::BoomBapEngine::Sample1 + waveTrack);
    };

    // ---- pad bank (own window, built on first open - a DocumentWindow shows itself when created, so it must not be built at launch) ----
    boombap::ui::applyActionTrace (padBankButton);
    addAndMakeVisible (padBankButton);
    padBankButton.onClick = [this] { openPadBank(); };

    // ---- piano roll (own window, same arrangement) ----
    boombap::ui::applyActionTrace (pianoButton);
    addAndMakeVisible (pianoButton);
    pianoButton.onClick = [this] { openPianoRoll(); };

    // ---- the Production Area (fix pass 2, items 37/42): a labelled,
    // outlined TAB BAR - STEP SEQUENCER / MPC PANEL / AUDIO MULTITRACK -
    // replacing the old corner toggle D could not find. Every tab can
    // pop out into its own window and dock back (his Reaper grammar).
    {
        const char* names[3] = { "STEP SEQUENCER", "PAD BANK", "AUDIO MULTITRACK" };
        for (int i = 0; i < 3; ++i)
        {
            tabButtons[i].setButtonText (names[i]);
            tabButtons[i].onClick = [this, i] { selectTab (i); };
            addAndMakeVisible (tabButtons[i]);
        }
    }
    popOutButton.onClick = [this] { togglePopOut(); };
    boombap::ui::applyActionTrace (popOutButton); // item 89: POP OUT named in the item - amber trace
    addAndMakeVisible (popOutButton);
    tabPoppedLabel.setText ("This view is open in its own window - close that window, or press Pop out again, to dock it back here.",
                            dontSendNotification);
    tabPoppedLabel.setColour (Label::textColourId, boombap::ui::muted());
    tabPoppedLabel.setJustificationType (Justification::centred);
    tabPoppedLabel.setVisible (false);
    addAndMakeVisible (tabPoppedLabel);

    {
        SequencerPanel::Callbacks scb;
        scb.onLoadSample = [this] (int t) { openSampleChooser (t); };
        scb.onRoll = [this] (int t) { openPianoRollFor (t); };
        scb.onNameMenu = [this] (int t)
        {
            // kit rows get the item 40 menu; user-added rows get the
            // item 69 menu (Load sample / Delete track)
            if (t >= boombap::BoomBapEngine::kFirstOwnRow) showRowMenu (t);
            else showKitMenu (t);
        };
        // item 87: the row's visible X runs the same guarded, undoable
        // delete as the menu (item 69) - one action, two doors.
        scb.onDeleteRow = [this] (int t) { deleteSequencerRow (t); };
        scb.onPatternChanged = [this] { if (pianoRoll != nullptr) pianoRoll->refreshFromEngine(); };
        scb.onBeforeChange = [this] { beginUndoGesture(); };
        scb.onAfterChange = [this] { endUndoGesture(); };
        scb.onSelectRow = [this] (int t) { selectChannel (t); };
        sequencerPanel = std::make_unique<SequencerPanel> (engine, scb);
    }
    sequencerPanel->setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (sequencerPanel.get());
    sequencerPanel->setSelectedRow (selectedChannel); // item 86: the initial light (channel 0)

    // The MPC PANEL tab: its own pad bank instance (pads + the selected
    // pad's waveform and trim, item 44) - same engine, same samples as
    // the standalone Pad Bank window; both stay in step through it.
    mpcPanel = std::make_unique<PadBankComponent> (engine,
        [this] (int pad) { openPadChooser (pad); },
        [this] (int pad) { return padDisplayName (pad); });
    mpcPanel->setLookAndFeel (&lookAndFeel);
    mpcPanel->onBeforeChange = [this] { beginUndoGesture(); };
    mpcPanel->onAfterChange = [this] { endUndoGesture(); };
    mpcPanel->onSelectPad = [this] (int ch) { selectChannel (ch); };
    mpcPanel->onPadCopyRequest = [this] (int from, int to) { copyPadToPad (from, to); };
    // Item 59: hidden tabs are added as CHILD components, never via
    // addAndMakeVisible (which un-hides them) - updateTabChrome below is
    // the single authority on which production view is visible, so the
    // editor can never open with two views drawn on top of each other.
    addChildComponent (mpcPanel.get());
    mpcPanel->setVisible (false);

    multitrack = std::make_unique<MultitrackComponent> (engine, [] {},
        [this] { startAudioOnlyPlayback(); },
        [this] { stopTransport(); });
    multitrack->setLookAndFeel (&lookAndFeel);
    multitrack->onBeforeChange = [this] { beginUndoGesture(); };
    multitrack->onAfterChange = [this] { endUndoGesture(); };
    multitrack->onSelectChannel = [this] (int ch) { selectChannel (ch); };
    addChildComponent (multitrack.get());
    multitrack->setVisible (false);
    multitrack->setHomePathText ("BoomBap home: " + boombap::dataHome().getFullPathName()
                                 + "  -  takes land in its Takes folder");
    multitrack->setSelectedChannel (selectedChannel); // item 68: the strip's initial light
    updateTabChrome();
    refreshGrid();

    // ---- playlist (own window) ----
    boombap::ui::applyActionTrace (playlistButton);
    addAndMakeVisible (playlistButton);
    playlistButton.onClick = [this] { openPlaylist(); };

    addAndMakeVisible (dustRate.slider);
    addAndMakeVisible (glueThreshold.slider);
    setupSlider (dustRate, "Rate", 7500.0, 39375.0, 26000.0, 100.0, " Hz",
                 [this] { engine.setDustRate ((float) dustRate.slider.getValue()); });
    setupSlider (glueThreshold, "Thresh", -30.0, 0.0, -12.0, 0.5, " dB",
                 [this] { engine.setGlueThresholdDb ((float) glueThreshold.slider.getValue()); });

    // ---- drum sound ----
    for (auto* s : { &kickTune, &kickDecay, &snareSnappy, &openHatDecay, &clapTail })
        addAndMakeVisible (s->slider);

    setupSlider (kickTune,     "Kick tune",   40.0, 70.0,   50.0,  0.5,  " Hz", [this] { engine.setKickTune ((float) kickTune.slider.getValue()); });
    setupSlider (kickDecay,    "Kick decay",  200.0, 1200.0, 600.0, 10.0, " ms", [this] { engine.setKickDecay ((float) kickDecay.slider.getValue()); });
    setupSlider (snareSnappy,  "Snare snap",  0.0, 1.0,     0.6,   0.01, "",    [this] { engine.setSnareSnappy ((float) snareSnappy.slider.getValue()); });
    setupSlider (openHatDecay, "Open hat",    150.0, 800.0, 380.0, 10.0, " ms", [this] { engine.setOpenHatDecay ((float) openHatDecay.slider.getValue()); });
    setupSlider (clapTail,     "Clap tail",   80.0, 400.0,  200.0, 5.0,  " ms", [this] { engine.setClapTail ((float) clapTail.slider.getValue()); });

    // ---- vocal ----
    addAndMakeVisible (vocalToggle);
    addAndMakeVisible (micBox);
    addAndMakeVisible (vocalHint);
    addAndMakeVisible (vocalDistance.slider);
    addAndMakeVisible (vocalInput.slider);

    micBox.addItem ("Condenser Clean", 1);
    micBox.addItem ("Berlin FET 87", 2);
    micBox.addItem ("Vintage Tube 47", 3);
    micBox.addItem ("Broadcast Dynamic 7", 4);
    micBox.addItem ("Stage Dynamic 58", 5);
    micBox.addItem ("Tokyo Tube 800", 6);
    micBox.addItem ("Vienna Tube 251", 7);
    micBox.addItem ("Reference Studio", 8);
    micBox.setSelectedId (3, dontSendNotification);
    micBox.onChange = [this]
    {
        selectChannel (boombap::BoomBapEngine::kVocalChannel);
        runUndoable ([this] { engine.setVocalMic (micBox.getSelectedId() - 1); });
    };

    vocalToggle.onClick = [this]
    {
        selectChannel (boombap::BoomBapEngine::kVocalChannel);
        const bool on = vocalToggle.getToggleState();
        engine.setVocalEnabled (on);
        if (ownAudioDevice)
        {
            auto state = deviceManager.createStateXml(); // keep the chosen device / rate / buffer
            setAudioChannels (on ? 1 : 0, 2, state.get()); // only asks for the microphone once you switch this on
        }
        // in a plugin the host owns the audio device; the toggle just arms the vocal channel
    };

    // NOTE: no selectChannel in these slider lambdas - setupSlider fires
    // its apply lambda once at construction (a programmatic set, not a
    // user gesture), which would otherwise yank the Station's channel to
    // VOCAL before the user touches anything. The vocal channel is
    // selected by the MONITOR toggle's click and by the take lane.
    setupSlider (vocalDistance, "Distance", 3.0, 40.0, 15.0, 0.5, " cm", [this] { engine.setVocalDistance ((float) vocalDistance.slider.getValue()); });
    setupSlider (vocalInput,    "Input",   -12.0, 24.0, 0.0, 0.5, " dB", [this] { engine.setVocalInputDb ((float) vocalInput.slider.getValue()); });

    vocalHint.setText ("Use headphones: monitoring through speakers will feed back.", dontSendNotification);
    vocalHint.setColour (Label::textColourId, Colours::orange);
    vocalHint.setFont (juce::Font (11.5f));

    // ---- vocal take ----
    addAndMakeVisible (recordButton);
    addAndMakeVisible (takeToggle);
    addAndMakeVisible (takeLabel);
    recordButton.onClick = [this] { toggleRecording(); };
    takeToggle.setToggleState (false, dontSendNotification); // item 56: off until chosen
    takeToggle.onClick = [this] { runUndoable ([this] { engine.setTakeEnabled (takeToggle.getToggleState()); refreshTakeUi(); }); };
    takeLabel.setColour (Label::textColourId, boombap::ui::muted());
    refreshTakeUi();

    // ---- export ----
    addAndMakeVisible (exportBars);
    addAndMakeVisible (normalizeToggle);
    addAndMakeVisible (exportButton);
    addAndMakeVisible (exportStatus);
    exportBars.addItem ("1 bar", 1);
    exportBars.addItem ("2 bars", 2);
    exportBars.addItem ("4 bars", 4);
    exportBars.addItem ("8 bars", 8);
    exportBars.addItem ("Whole song", 100); // Fix pass item 34
    exportBars.setSelectedId (4, dontSendNotification);
    normalizeToggle.setToggleState (true, dontSendNotification);
    exportButton.onClick = [this] { startExport(); };
    exportStatus.setColour (Label::textColourId, Colours::lightgrey);
    exportStatus.setJustificationType (Justification::centredLeft);

    // ---- instruments ----
    addAndMakeVisible (synthWaveBox);
    for (auto* s : { &synthCutoff, &synthReso, &synthAttack, &synthRelease, &synthLevel,
                     &pianoBright, &pianoDecay, &pianoLevel })
        addAndMakeVisible (s->slider);

    synthWaveBox.addItem ("Saw", 1);
    synthWaveBox.addItem ("Square", 2);
    synthWaveBox.addItem ("Triangle", 3);
    synthWaveBox.addItem ("Sine", 4);
    synthWaveBox.setSelectedId (1, dontSendNotification);
    synthWaveBox.onChange = [this] { runUndoable ([this] { engine.setSynthWave (synthWaveBox.getSelectedId() - 1); }); };

    setupSlider (synthCutoff,  "Cutoff",  300.0, 12000.0, 4500.0, 50.0, " Hz", [this] { engine.setSynthCutoff ((float) synthCutoff.slider.getValue()); });
    setupSlider (synthReso,    "Reso",    0.0,   1.0,     0.25,   0.01, "",    [this] { engine.setSynthReso ((float) synthReso.slider.getValue()); });
    setupSlider (synthAttack,  "Attack",  2.0,   500.0,   8.0,    1.0,  " ms", [this] { engine.setSynthAttack ((float) synthAttack.slider.getValue()); });
    setupSlider (synthRelease, "Release", 30.0,  2000.0,  240.0,  5.0,  " ms", [this] { engine.setSynthRelease ((float) synthRelease.slider.getValue()); });
    setupSlider (synthLevel,   "Level",   0.0,   1.2,     0.8,    0.01, "",    [this] { engine.setSynthLevel ((float) synthLevel.slider.getValue()); });
    setupSlider (pianoBright,  "Bright",  0.0,   1.0,     0.6,    0.01, "",    [this] { engine.setPianoBright ((float) pianoBright.slider.getValue()); });
    setupSlider (pianoDecay,   "Decay",   400.0, 6000.0,  2200.0, 50.0, " ms", [this] { engine.setPianoDecay ((float) pianoDecay.slider.getValue()); });
    setupSlider (pianoLevel,   "Level",   0.0,   1.2,     0.85,   0.01, "",    [this] { engine.setPianoLevel ((float) pianoLevel.slider.getValue()); });

    // ---- instrument switcher (Fix pass item 29) ----
    addAndMakeVisible (synthTab);
    addAndMakeVisible (pianoTab);
    addAndMakeVisible (instrumentRollButton);
    synthTab.onClick = [this] { selectInstrument (0); };
    pianoTab.onClick = [this] { selectInstrument (1); };
    instrumentRollButton.onClick = [this]
    {
        openPianoRollFor (shownInstrument == 0 ? PianoRollComponent::kBindSynth
                                               : PianoRollComponent::kBindPiano);
    };
    selectInstrument (0);

    // keep the space bar for play/stop
    for (auto* c : getChildren())
        if (dynamic_cast<Button*> (c) != nullptr)
            c->setWantsKeyboardFocus (false);
    exportBars.setWantsKeyboardFocus (false);
    micBox.setWantsKeyboardFocus (false);
    synthWaveBox.setWantsKeyboardFocus (false);

    pushAllParams();
    savedJson = currentJson();
    updateProjectLabel();
    if (ownAudioDevice)
    {
        auto state = loadDeviceStateXml(); // Pass 1 item 8: restore the device setup from last time
        setAudioChannels (0, 2, state.get());
    }
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    setLookAndFeel (nullptr);
    stopTimer();
    saveDeviceState();
    clearRecovery(); // a clean exit means the recovery file has done its job
    shutdownAudio();
}

void MainComponent::setupSlider (LabeledSlider& s, const String& name, double min, double max, double init,
                                 double step, const String& suffix, std::function<void()> onChange)
{
    s.slider.setSliderStyle (Slider::LinearHorizontal);
    s.slider.setTextBoxStyle (Slider::TextBoxRight, false, 72, 20);
    s.slider.setRange (min, max, step);
    s.slider.setValue (init, dontSendNotification);
    s.slider.setDoubleClickReturnValue (true, init); // Pass 1 item 4: double-click resets, app-wide
    s.slider.setTextValueSuffix (suffix);
    s.slider.onDragStart = [this] { beginUndoGesture(); }; // Pass 2 item 38: a drag is one undoable action
    s.slider.onDragEnd = [this] { endUndoGesture(); };
    s.slider.onValueChange = std::move (onChange);

    s.label.setText (name, dontSendNotification);
    s.label.setJustificationType (Justification::centredRight);
    s.label.setColour (Label::textColourId, Colours::lightgrey);
    s.label.attachToComponent (&s.slider, true);
}

void MainComponent::pushAllParams()
{
    for (auto* s : { &bpm, &swing, &master, &dustRate, &glueThreshold, &kickTune, &kickDecay,
                     &snareSnappy, &openHatDecay, &clapTail, &vocalDistance, &vocalInput,
                     &synthCutoff, &synthReso, &synthAttack, &synthRelease, &synthLevel,
                     &pianoBright, &pianoDecay, &pianoLevel, &metroVol })
        if (s->slider.onValueChange) s->slider.onValueChange();

    engine.setMetronomeOn (metroButton.getToggleState());
    engine.setGlueEnabled (glueToggle.getToggleState());
    engine.setDustEnabled (dustToggle.getToggleState());
    engine.setVocalMic (micBox.getSelectedId() - 1);
    engine.setSynthWave (synthWaveBox.getSelectedId() - 1);
}

void MainComponent::refreshGrid()
{
    if (sequencerPanel != nullptr) sequencerPanel->refreshGrid();
}

// Fix pass item 29: one instrument at a time. The switcher shows the chosen
// instrument's controls; if the piano roll is open it rebinds to that
// instrument's lane, so controls and lane are never a hunt.
void MainComponent::selectInstrument (int which)
{
    shownInstrument = which;
    const bool synth = which == 0;
    selectChannel (synth ? boombap::BoomBapEngine::kSynthChannel : boombap::BoomBapEngine::kPianoChannel);

    synthWaveBox.setVisible (synth);
    for (auto* s : { &synthCutoff, &synthReso, &synthAttack, &synthRelease, &synthLevel })
        s->slider.setVisible (synth);
    for (auto* s : { &pianoBright, &pianoDecay, &pianoLevel })
        s->slider.setVisible (! synth);

    synthTab.setColour (TextButton::buttonColourId, synth ? boombap::ui::accent() : boombap::ui::panelAlt());
    synthTab.setColour (TextButton::textColourOffId, synth ? Colour (0xff14100a) : boombap::ui::text());
    pianoTab.setColour (TextButton::buttonColourId, ! synth ? boombap::ui::accent() : boombap::ui::panelAlt());
    pianoTab.setColour (TextButton::textColourOffId, ! synth ? Colour (0xff14100a) : boombap::ui::text());

    if (pianoRoll != nullptr && pianoWindow != nullptr && pianoWindow->isVisible())
        openPianoRollFor (synth ? PianoRollComponent::kBindSynth : PianoRollComponent::kBindPiano);
    repaint();
}

// Fix pass 2, items 37/42: the Production Area tabs. A tab's content is
// either docked here or living in its own pop-out window - never both,
// never lost.
juce::Component* MainComponent::tabContent (int index)
{
    if (index == 0) return sequencerPanel.get();
    if (index == 1) return mpcPanel.get();
    return multitrack.get();
}

void MainComponent::selectTab (int index)
{
    activeTab = index;
    updateTabChrome();
    if (index == 2 && multitrack != nullptr && ! tabPoppedOut[2])
        multitrack->refreshFromEngine();
    resized();
    repaint();
}

void MainComponent::togglePopOut()
{
    if (tabPoppedOut[activeTab]) dockTab (activeTab);
    else popOutTab (activeTab);
}

// Fix Pass 4 (items 77/78): window accessors for the frame test - the
// window classes are complete in this TU (the header only forward-
// declares them), so the DocumentWindow conversions live here.
juce::DocumentWindow* MainComponent::padWindowComponent()      { return padWindow.get(); }
juce::DocumentWindow* MainComponent::pianoWindowComponent()    { return pianoWindow.get(); }
juce::DocumentWindow* MainComponent::playlistWindowComponent() { return playlistWindow.get(); }

void MainComponent::popOutTab (int index)
{
    auto* content = tabContent (index);
    if (content == nullptr || tabPoppedOut[index]) return;
    static const char* titles[3] = { "BoomBap - Step Sequencer", "BoomBap - Pad Bank", "BoomBap - Audio Multitrack" };
    auto win = std::make_unique<PopOutWindow> (titles[index], [this, index] { dockTab (index); });
    win->setLookAndFeel (&lookAndFeel); // like every other window (items 77/78: its title bar is BoomBap-drawn)
    win->setDockGuard (this);
    // Item 66: NON-owned - the panels live in MainComponent's unique_ptrs
    // (sequencerPanel / mpcPanel / multitrack), exactly like the pad bank,
    // piano roll, playlist and Station windows. setContentOwned here made
    // dockTab's clearContentComponent() DELETE the panel; dockTab then
    // re-parented the corpse and MainComponent's destructor deleted it a
    // second time. That was the dock-back crash.
    win->setContentNonOwned (content, false);
    win->setSize (juce::jmax (760, content->getWidth()), juce::jmax (380, content->getHeight() + 40));
    win->centreWithSize (win->getWidth(), win->getHeight());
    productionWindows[index] = std::move (win);
    productionWindows[index]->setVisible (true);
    tabPoppedOut[index] = true;
    updateTabChrome();
    resized();
    repaint();
}

void MainComponent::dockTab (int index)
{
    auto* content = tabContent (index);
    if (content == nullptr) { tabPoppedOut[index] = false; return; }
    if (! tabPoppedOut[index] && productionWindows[index] == nullptr) return; // already docked
    if (productionWindows[index] != nullptr)
    {
        productionWindows[index]->clearContentComponent(); // non-owned: detaches the panel, never deletes it
        productionWindows[index].reset();
    }
    tabPoppedOut[index] = false;
    addChildComponent (content); // item 59: updateTabChrome decides visibility
    if (index == 2) multitrack->refreshFromEngine();
    updateTabChrome();
    resized();
    repaint();
}

void MainComponent::updateTabChrome()
{
    for (int i = 0; i < 3; ++i)
    {
        const bool sel = i == activeTab;
        tabButtons[i].setColour (TextButton::buttonColourId, sel ? boombap::ui::accent() : boombap::ui::panelAlt());
        tabButtons[i].setColour (TextButton::textColourOffId, sel ? Colour (0xff14100a) : boombap::ui::text());
        auto* c = tabContent (i);
        if (c != nullptr && ! tabPoppedOut[i])
            c->setVisible (sel);
    }
    popOutButton.setButtonText (tabPoppedOut[activeTab] ? "Dock back" : "Pop out");
    tabPoppedLabel.setVisible (tabPoppedOut[activeTab]);
}

// Item 40: clicking a kit row's name offers to change that row's sound.
void MainComponent::showKitMenu (int track)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Load sample for this row...");
    menu.addItem (2, "Reset to the built-in sound", engine.kitHasSample (track));
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, track] (int choice)
    {
        if (choice == 1) openSampleChooser (track);
        else if (choice == 2)
        {
            runUndoable ([this, track]
            {
                engine.clearKitSample (track);
                rowFiles[track] = juce::File();
                syncTrackName (track); // item 88: unbound, the row is its kit name again
                fireMediaChanged();
            });
        }
    });
}

// Fix Pass 3 (item 69): a user-added row's name menu - load its sample,
// or delete the row entirely.
void MainComponent::showRowMenu (int track)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Load sample for this row...");
    menu.addItem (2, "Delete track");
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, track] (int choice)
    {
        if (choice == 1) openSampleChooser (track);
        else if (choice == 2) deleteSequencerRow (track);
    });
}

// Fix Pass 3 (item 69): delete a user-added sequencer row whole - its
// steps in every pattern, its Station channel, its sample - as ONE
// undoable action. Kit rows (0-4) and pad rows (5-20) are protected in
// the engine: the kit rows are the starter kit's skeleton, and the pad
// rows belong to the sixteen physical pads.
void MainComponent::deleteSequencerRow (int track)
{
    const int before = engine.getNumTracks();
    if (track < boombap::BoomBapEngine::kFirstOwnRow || track >= before) return;
    runUndoable ([this, track, before]
    {
        if (! engine.removeTrack (track)) return;
        for (int t = track; t < before - 1; ++t)
            rowFiles[(size_t) t] = rowFiles[(size_t) (t + 1)];
        rowFiles[(size_t) (before - 1)] = juce::File();
        syncAllTrackNames(); // item 88: names follow their rows down
        // the Station selection follows the rows it points at
        if (selectedChannel == track) selectChannel (0);
        else if (selectedChannel > track && selectedChannel < before)
            selectChannel (selectedChannel - 1);
        fireMediaChanged();
    });
}

// Fix pass item 32: when a take finishes, it lands on the audio track as a
// clip - visible, homed, editable in the multitrack view.
void MainComponent::placeTakeAsClip()
{
    if (engine.placeTakeAsClip() >= 0 && multitrack != nullptr)
        multitrack->refreshFromEngine();
}

void MainComponent::togglePlay()
{
    engine.setPlaying (! engine.isPlaying());
}

void MainComponent::startTransport()
{
    // MPC-style: pressing Play while already playing restarts from the
    // top (Pass 1 item 2) - now a live seek to zero, which lands
    // exactly instead of racing the playing flag. From a stop, PLAY
    // starts where the transport is PARKED (Round 2 item 93: REW/FF
    // move the park; STOP returns it to zero).
    if (engine.isPlaying())
        engine.seekTransport (0);
    else
        engine.setPlaying (true);
}

void MainComponent::setClockFormat (int fmt)
{
    clockFormat = juce::jlimit (0, 2, fmt);
    auto props = boombap::appPropertiesFile();
    props->setValue ("clockFormat", clockFormat);
    props->saveIfNeeded();
}

// Round 2 (item 96d): the Station's EDIT SAMPLE doorway - a sample
// lane opens the pad bank on that pad (the pad/sample editor), an
// instrument lane opens its piano roll. Pure audio lanes, the buses
// and the master have no sample editor; their button stays disabled
// (channelHasEditor is the predicate the Station asks).
bool MainComponent::channelHasEditor (int ch) const
{
    using E = boombap::BoomBapEngine;
    if (ch == E::kSynthChannel || ch == E::kPianoChannel) return true;
    return ch >= E::Sample1 && ch < E::Sample1 + E::kPads && ch < engine.getNumTracks();
}

void MainComponent::editChannel (int ch)
{
    using E = boombap::BoomBapEngine;
    if (ch == E::kSynthChannel) { openPianoRollFor (PianoRollComponent::kBindSynth); return; }
    if (ch == E::kPianoChannel) { openPianoRollFor (PianoRollComponent::kBindPiano); return; }
    if (ch >= E::Sample1 && ch < E::Sample1 + E::kPads && ch < engine.getNumTracks())
    {
        openPadBank();
        const int pad = ch - E::Sample1;
        if (padBank != nullptr) padBank->selectPad (pad, false);
        if (mpcPanel != nullptr) mpcPanel->selectPad (pad, false);
    }
}

// Item 45: STOP MEANS ALL SOUND OFF. One press ends a take in progress
// (cleanly - what was recorded lands as a clip), stops both transports
// and kills every voice in the engine behind a few-ms click fade. There
// is no sound this button cannot stop; D killed the app twice learning
// the old one's limits.
void MainComponent::stopTransport()
{
    if (engine.isRecording())
    {
        engine.stopRecording();
        engine.finishTake();
        takeDirty = true;
        placeTakeAsClip();
        uiRecording = false;
        refreshTakeUi();
    }
    engine.stopAllSound();
}

// Item 47: the audio view's own PLAY - the audio tracks alone, from the
// audio playhead, no beat behind them.
void MainComponent::startAudioOnlyPlayback()
{
    engine.setAudioPlaying (true);
}

//==============================================================================
// Pass 2: the Station + global undo (items 19/38).
void MainComponent::openStation()
{
    if (stationWindow == nullptr)
    {
        boombap::StationPanel::Callbacks cb;
        cb.onSelectChannel = [this] (int ch) { selectChannel (ch); };
        cb.onPlay = [this] { startTransport(); };
        cb.onStopAll = [this] { stopTransport(); }; // item 45's stop, same button by another face
        cb.onRewind = [this] { rewindTransport(); };
        cb.onFastForward = [this] { fastForwardTransport(); };
        cb.onToStart = [this] { navToStart(); };
        cb.onToEnd = [this] { navToEnd(); };
        cb.getClockFormat = [this] { return clockFormat; };
        cb.setClockFormat = [this] (int fmt) { setClockFormat (fmt); };
        cb.onEditChannel = [this] (int ch) { editChannel (ch); };
        cb.channelHasEditor = [this] (int ch) { return channelHasEditor (ch); };
        cb.onUndo = [this] { undoProject(); };
        cb.onRedo = [this] { redoProject(); };
        cb.canUndo = [this] { return canUndoProject(); };
        cb.canRedo = [this] { return canRedoProject(); };
        cb.onBeforeChange = [this] { beginUndoGesture(); };
        cb.onAfterChange = [this] { endUndoGesture(); };
        station = std::make_unique<boombap::StationPanel> (engine, cb);
        station->setLookAndFeel (&lookAndFeel);
        stationWindow = std::make_unique<boombap::StationWindow> ("BoomBap - The Station", boombap::ui::bg());
        stationWindow->setLookAndFeel (&lookAndFeel);
        stationWindow->setContentNonOwned (station.get(), false); // the unique_ptr owns it
        stationWindow->centreWithSize (1240, 660);
    }
    station->bindToChannel (selectedChannel);
    stationWindow->setVisible (true);
    stationWindow->toFront (true);
}

void MainComponent::selectChannel (int ch)
{
    // Selection is a user gesture: a project load (including undo/redo
    // applies) must never move it - control refreshes during a load fire
    // change callbacks (the mic profile box among them) that would
    // otherwise yank the Station to the vocal channel on every undo.
    if (loadingProject) return;
    selectedChannel = juce::jlimit (0, boombap::BoomBapEngine::kMasterChannel, ch);
    engine.setAnalysedChannel (selectedChannel);
    if (station != nullptr)
        station->bindToChannel (selectedChannel);
    // Item 68: the multitrack's mixer-order strip lights the selected
    // channel too - selection is mirrored, never re-derived there.
    if (multitrack != nullptr)
        multitrack->setSelectedChannel (selectedChannel);
    // Round 1 (item 86): the sequencer's selected-row light is the
    // third mirror of the one selection state - now a track picked in
    // ANY view (sequencer name click, multitrack chip, Station board)
    // lights the same row here.
    if (sequencerPanel != nullptr)
        sequencerPanel->setSelectedRow (selectedChannel);
}

// =====================================================================
// Round 1 (item 102) - minimise coupling. The main window is the only
// taskbar window; when it is minimised, every open auxiliary window
// goes down with it, and restoring the main window brings back
// exactly the windows the coupling took down. The state lives in
// hiddenByMinimise so a window the owner closed himself (or one
// destroyed while held, e.g. a docked production window) is never
// resurrected.
// =====================================================================
void MainComponent::pollMinimiseCoupling()
{
    // In the plugin the top-level peer is the HOST's window - its
    // minimise state is not BoomBap's to follow, and isMinimised()
    // stays false for the hosted editor, so this is a no-op there.
    auto* top = getTopLevelComponent();
    if (top == nullptr) return;
    auto* peer = top->getPeer();
    updateMinimiseCoupling (peer != nullptr && peer->isMinimised());
}

void MainComponent::updateMinimiseCoupling (bool minimised)
{
    juce::DocumentWindow* aux[] = { stationWindow.get(),
                                    productionWindows[0].get(), productionWindows[1].get(),
                                    productionWindows[2].get(),
                                    padWindow.get(), pianoWindow.get(), playlistWindow.get(),
                                    audioWindow.get() };
    if (minimised)
    {
        if (! hiddenByMinimise.empty()) return; // already coupled down
        for (auto* w : aux)
            if (w != nullptr && w->isVisible())
            {
                hiddenByMinimise.emplace_back (w);
                w->setVisible (false);
            }
    }
    else
    {
        for (auto& held : hiddenByMinimise)
            if (auto* w = held.getComponent())
                w->setVisible (true);
        hiddenByMinimise.clear();
    }
}

// Round 2 (item 93): the transport navigation. REW / FF move one
// step; a second press inside the double-press window moves one bar
// (the shared TransportNav reads the presses for both the main row
// and the Station's). While playing, the seek lands live; while
// stopped it PARKS the start position - PLAY then starts from the
// park. TO START / TO END go to the ends of the pattern / song chain.
// (The old REWIND restarted from the top and the old FF stepped the
// audio playhead a bar; both are superseded by this one navigation,
// and the engine moves the audio playhead with the park.)
void MainComponent::seekBySteps (int steps)
{
    const long long current = engine.isPlaying() ? engine.transportPosition()
                                                 : engine.transportStartPosition();
    engine.seekTransport (boombap::BoomBapEngine::skipTarget (
        current, steps, engine.stepSamples(), engine.transportLengthSamples()));
}

void MainComponent::navRewind()  { seekBySteps (transportNav.pressRewind()); }
void MainComponent::navForward() { seekBySteps (transportNav.pressForward()); }
void MainComponent::navToStart() { engine.seekTransport (0); }
void MainComponent::navToEnd()   { engine.seekTransport (engine.transportLengthSamples()); }

// The Station's REW / FF buttons run the same navigation.
void MainComponent::rewindTransport()    { navRewind(); }
void MainComponent::fastForwardTransport() { navForward(); }

void MainComponent::beginUndoGesture()
{
    if (loadingProject || history.isApplying())
        return;
    history.beginGesture();
    if (history.gestureIsOpen())
    {
        gestureSnapshot = currentJson();
        gestureIdleTicks = 0;
    }
}

void MainComponent::endUndoGesture()
{
    if (! history.gestureIsOpen())
        return;
    history.endGesture();
    refreshViewsAfterEdit();
}

void MainComponent::runUndoable (std::function<void()> fn)
{
    if (loadingProject || history.isApplying())
    {
        fn();
        return;
    }
    history.perform (fn);
    refreshViewsAfterEdit();
}

void MainComponent::undoProject() { history.undo(); }
void MainComponent::redoProject() { history.redo(); }

void MainComponent::applyHistoryJson (const juce::String& json)
{
    boombap::ProjectData d;
    juce::String error;
    if (! boombap::projectFromJson (json, currentFile, d, error))
        return;
    // A history snapshot carries its clips explicitly - item 61's
    // auto-homing is for files and host restores, and must not invent a
    // clip here (undoing a clip delete depends on that).
    suppressAutoHome = true;
    loadProjectData (d, false, true);
    suppressAutoHome = false;
}

void MainComponent::refreshViewsAfterEdit()
{
    const auto s = engine.getSettings();
    if (sequencerPanel != nullptr) sequencerPanel->refreshMixerFromSettings (s);
    if (multitrack != nullptr) multitrack->refreshFromEngine();
    if (station != nullptr)
    {
        station->refreshFromEngine();
        station->refreshChannelNames(); // item 88: renames ride every edit refresh
    }
    refreshGrid();
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::spaceKey)
    {
        // Stop gets the spacebar too when anything is sounding: the same
        // all-sound-off, never a polite pause that leaves voices ringing.
        if (engine.isPlaying() || engine.isAudioPlaying() || engine.isRecording())
            stopTransport();
        else
            startTransport();
        return true;
    }
    if ((key == KeyPress::deleteKey || key == KeyPress::backspaceKey)
        && activeTab == 2 && multitrack != nullptr && ! tabPoppedOut[2])
    {
        multitrack->deleteSelectedClip();
        return true;
    }

    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const int c = (int) CharacterFunctions::toLowerCase ((juce_wchar) key.getKeyCode());
        if (c == 'z') { if (mods.isShiftDown()) redoProject(); else undoProject(); return true; } // Pass 2 item 38
        if (c == 'y') { redoProject(); return true; }
        if (c == 's') { if (mods.isShiftDown()) saveProjectAs ({}); else saveProject ({}); return true; }
        if (c == 'o') { confirmDiscardThen ([this] { openProjectChooser(); }); return true; }
        if (c == 'n') { confirmDiscardThen ([this] { newProject(); }); return true; }
    }
    return false;
}

void MainComponent::timerCallback()
{
    pollMinimiseCoupling(); // item 102: follow the main window's minimise state
    mainClock.setPosition (engine.transportPosition(), engine.getSampleRate(),
                           (double) engine.getBpm(), clockFormat); // item 93
    if (sequencerPanel != nullptr) sequencerPanel->tick();

    // Pass 2 (item 38): an undo gesture left open by a continuous control
    // closes itself once its edits settle (~0.4 s of no change).
    if (history.gestureIsOpen())
    {
        if (currentJson() == gestureSnapshot)
        {
            if (++gestureIdleTicks >= 12) endUndoGesture();
        }
        else
        {
            gestureSnapshot = currentJson();
            gestureIdleTicks = 0;
        }
    }

    const bool playingNow = engine.isPlaying();
    if (playingNow != lastPlayingState)
    {
        lastPlayingState = playingNow;
        playButton.setColour (TextButton::buttonColourId,
                              playingNow ? boombap::ui::accentHi() : boombap::ui::accent());
    }

    if (++dirtyTick >= 8) { dirtyTick = 0; updateProjectLabel(); }

    // (Round 1, item 89: the waveform panel's button used to flip
    // PLAY/STOP with the audition - that state machine is gone; the
    // button is LOAD permanently now and the audition lives on the
    // waveform itself.)

    // pad activity (Pass 1 item 4b): light + pad-set meter here, flash in the pad bank
    const float padPeak = engine.consumePadActivityPeak();
    padShown = std::max (padPeak, padShown * 0.82f);
    padMeter.setLevel (padShown);
    padLight.setLevel (padShown);
    const int lastPad = engine.lastTriggeredPad();
    if (padPeak > 0.02f)
    {
        if (padBank != nullptr) padBank->showActivity (lastPad);
        if (mpcPanel != nullptr) mpcPanel->showActivity (lastPad);
    }

    // pad capture (Fix pass item 31): a captured hit flashes its pad and the
    // new step shows in the grid straight away
    const int captured = engine.consumeCapturedPad();
    if (captured >= 0)
    {
        if (padBank != nullptr) padBank->showActivity (captured);
        if (mpcPanel != nullptr) mpcPanel->showActivity (captured);
        refreshGrid();
    }

    // autosave (Pass 1 item 7): every ~60 s while there are unsaved changes
    if (ownAudioDevice && ++autosaveTicks >= 30 * 60)
    {
        autosaveTicks = 0;
        if (isDirty())
            writeRecovery();
    }

    const bool recNow = engine.isRecording();
    if (uiRecording && ! recNow) { engine.finishTake(); takeDirty = true; placeTakeAsClip(); }
    if (takeDirty && engine.hasTake()) ensureTakeFileInHome(); // item 62: the take gets its WAV home promptly
    if (recNow != uiRecording || recNow) refreshTakeUi();
    uiRecording = recNow;

    const float gr = engine.getGlueReductionDb();
    const bool glueOn = glueToggle.getToggleState();
    grShown = std::max (glueOn ? -gr : 0.0f, grShown * 0.85f);
    glueBar.setReduction (grShown);
    glueMeter.setText (glueOn ? "GR " + String (gr, 1) + " dB" : "", dontSendNotification);
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    juce::ignoreUnused (samplesPerBlockExpected);
    engine.prepare (sampleRate);
    inputScratch.assign (8192, 0.0f);
    rightScratch.assign (8192, 0.0f);

    auto* device = deviceManager.getCurrentAudioDevice();
    haveInput.store (device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0);
}

void MainComponent::getNextAudioBlock (const AudioSourceChannelInfo& info)
{
    auto* buffer = info.buffer;
    const int outChannels = buffer->getNumChannels();
    const bool useInput = haveInput.load();

    int done = 0;
    while (done < info.numSamples)
    {
        const int chunk = jmin (info.numSamples - done, (int) inputScratch.size());
        const int pos = info.startSample + done;

        const float* in = nullptr;
        if (useInput)
        {
            // copy the mic first: output channel 0 shares memory with input channel 0
            const float* src = buffer->getReadPointer (0, pos);
            std::copy (src, src + chunk, inputScratch.begin());
            in = inputScratch.data();
        }

        float* left  = buffer->getWritePointer (0, pos);
        float* right = outChannels > 1 ? buffer->getWritePointer (1, pos) : rightScratch.data();
        engine.process (in, left, right, chunk);

        for (int ch = 2; ch < outChannels; ++ch)
            FloatVectorOperations::clear (buffer->getWritePointer (ch, pos), chunk);

        done += chunk;
    }
}

//==============================================================================
// Pass 1 item 8: the audio-device setup surface. JUCE's selector drives the
// real AudioDeviceManager (device type where the OS offers one, input and
// output device, sample rate, buffer size); the state is saved when the
// window closes and restored at launch.
void MainComponent::showAudioSettings()
{
    if (! ownAudioDevice)
        return;

    if (audioWindow == nullptr)
    {
        auto* selector = new AudioDeviceSelectorComponent (deviceManager, 0, 2, 0, 2,
                                                           false, false, true, false);
        selector->setSize (540, 430);
        audioWindow = std::make_unique<AudioSettingsWindow> ("Audio Device Setup",
                                                             [this] { saveDeviceState(); });
        audioWindow->setLookAndFeel (&lookAndFeel);
        audioWindow->setContentOwned (selector, true);
        audioWindow->centreWithSize (560, 470);
    }
    audioWindow->setVisible (true);
    audioWindow->toFront (true);
}

void MainComponent::saveDeviceState()
{
    if (! ownAudioDevice)
        return;
    auto state = deviceManager.createStateXml();
    if (state == nullptr)
        return;
    auto props = boombap::appPropertiesFile();
    props->setValue ("audioDeviceState", state.get());
    props->saveIfNeeded();
}

std::unique_ptr<XmlElement> MainComponent::loadDeviceStateXml()
{
    auto props = boombap::appPropertiesFile();
    return props->getXmlValue ("audioDeviceState");
}

//==============================================================================
// Pass 1 item 7: autosave + crash recovery. The recovery file is written by
// the timer while the project is dirty, and deleted by a manual save, by
// opening / starting another project, and by a clean exit - so if it is
// still there at launch, the last session ended early.
void MainComponent::writeRecovery()
{
    auto data = makeProjectData();
    if (engine.hasTake() && takeDirty)
    {
        const File takeFile = boombap::recoveryTakeFile();
        takeFile.getParentDirectory().createDirectory();
        if (writeTakeWav (takeFile))
            data.vocalTakePath = takeFile.getFullPathName();
    }
    const File recovery = boombap::recoveryFile();
    recovery.getParentDirectory().createDirectory();
    String error;
    boombap::saveProjectFile (recovery, data, error);
}

void MainComponent::clearRecovery()
{
    if (! ownAudioDevice)
        return;
    boombap::recoveryFile().deleteFile();
    boombap::recoveryTakeFile().deleteFile();
}

void MainComponent::checkForRecovery()
{
    if (! ownAudioDevice)
        return;
    const File recovery = boombap::recoveryFile();
    if (! recovery.existsAsFile())
        return;

    Component::SafePointer<MainComponent> safe (this);
    AlertWindow::showAsync (MessageBoxOptions::makeOptionsYesNo (MessageBoxIconType::InfoIcon,
                                                                 "Recover your last session?",
                                                                 "BoomBap closed with unsaved work last time. "
                                                                 "Restore the autosaved project?",
                                                                 "Restore", "Discard", this),
                            [safe, recovery] (int result)
                            {
                                if (safe == nullptr)
                                    return;
                                if (result == 1)
                                {
                                    boombap::ProjectData d;
                                    String error;
                                    if (boombap::loadProjectFile (recovery, d, error))
                                    {
                                        safe->loadProjectData (d, false, false);
                                        safe->history.clear(); // the restored session starts a fresh history
                                        safe->savedJson.clear(); // still unsaved work - keep the * and the recovery file
                                        safe->updateProjectLabel();
                                    }
                                }
                                else
                                {
                                    recovery.deleteFile();
                                    boombap::recoveryTakeFile().deleteFile();
                                }
                            });
}

//==============================================================================
// The layout is drawn on a 1560x900 landscape canvas (Pass 1 item 1) and
// scaled proportionally to whatever size the window actually has, so the
// whole app always fits the screen - never taller than the usable area.
void MainComponent::paint (Graphics& g)
{
    using namespace boombap::ui;
    g.fillAll (bg());

    const float sx = (float) getWidth() / 1560.0f;
    const float sy = (float) getHeight() / 900.0f;
    auto RB = [&] (int x, int y, int w, int h)
    {
        return Rectangle<int> (roundToInt (x * sx), roundToInt (y * sy),
                               roundToInt (w * sx), roundToInt (h * sy));
    };
    const int oy = RB (0, 0, 0, kProjectBar).getHeight();

    // project bar strip
    g.setColour (panel());
    g.fillRect (0, 0, getWidth(), oy);
    g.setColour (border().withAlpha (0.7f));
    g.fillRect (0, oy - 1, getWidth(), 1);

    // section panels
    auto drawPanel = [&] (int x, int y, int w, int h)
    {
        Rectangle<float> r = RB (x, y, w, h).toFloat();
        g.setColour (panel());
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (border().withAlpha (0.55f));
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
    };
    drawPanel (8, 46, 1544, 118);    // transport + master FX
    drawPanel (8, 172, 1544, 384);   // production area (tabbed)
    drawPanel (8, 564, 764, 140);    // sample waveform
    drawPanel (780, 564, 772, 140);  // vocal
    drawPanel (8, 712, 420, 180);    // drum sound
    drawPanel (436, 712, 760, 180);  // instruments
    drawPanel (1204, 712, 348, 180); // export

    g.setColour (text().withAlpha (0.85f));
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText ("SAMPLE WAVEFORM", RB (20, 574, 220, 20), Justification::left);
    g.drawText ("VOCAL", RB (792, 574, 300, 20), Justification::left);
    g.drawText ("DRUM SOUND", RB (20, 722, 300, 20), Justification::left);
    g.drawText ("INSTRUMENTS", RB (448, 722, 190, 20), Justification::left);
    g.drawText ("EXPORT", RB (1216, 722, 300, 20), Justification::left);
    g.drawText (shownInstrument == 0 ? "SYNTH" : "PIANO", RB (448, 752, 100, 24), Justification::left);

    g.setColour (muted());
    g.setFont (11.0f);
    g.drawText ("One instrument at a time - its roll opens from the button above, or from a track's ROLL button.",
                RB (448, 856, 736, 20), Justification::left);
}

void MainComponent::resized()
{
    const float sx = (float) getWidth() / 1560.0f;
    const float sy = (float) getHeight() / 900.0f;
    auto RB = [&] (int x, int y, int w, int h)
    {
        return Rectangle<int> (roundToInt (x * sx), roundToInt (y * sy),
                               roundToInt (w * sx), roundToInt (h * sy));
    };

    // project bar
    newButton.setBounds (RB (12, 6, 60, 28));
    openButton.setBounds (RB (78, 6, 80, 28));
    saveButton.setBounds (RB (164, 6, 60, 28));
    saveAsButton.setBounds (RB (230, 6, 90, 28));
    recentButton.setBounds (RB (326, 6, 76, 28));
    audioButton.setBounds (RB (408, 6, 80, 28));
    projectName.setBounds (RB (500, 6, 428, 28));
    mainClock.setBounds (RB (936, 7, 108, 26)); // item 93: the compact clock
    stationButton.setBounds (RB (1058, 6, 86, 28));
    pianoButton.setBounds (RB (1150, 6, 96, 28));
    playlistButton.setBounds (RB (1254, 6, 88, 28));
    padBankButton.setBounds (RB (1350, 6, 100, 28));

    // transport row A (item 93): the full navigation, big play/stop,
    // pattern edit helpers, tempo, swing, master, pad activity
    toStartButton.setBounds (RB (20, 54, 64, 50));
    rewButton.setBounds (RB (88, 54, 56, 50));
    playButton.setBounds (RB (148, 54, 130, 50));
    stopButton.setBounds (RB (282, 54, 130, 50));
    ffButton.setBounds (RB (416, 54, 56, 50));
    toEndButton.setBounds (RB (476, 54, 64, 50));
    clearButton.setBounds (RB (544, 54, 64, 50));
    resetButton.setBounds (RB (612, 54, 64, 50));
    bpm.slider.setBounds (RB (742, 54, 180, 50));
    swing.slider.setBounds (RB (988, 54, 160, 50));
    master.slider.setBounds (RB (1214, 54, 150, 50));
    padLight.setBounds (RB (1376, 61, 32, 32));
    padMeter.setBounds (RB (1416, 65, 124, 28));

    // transport row B: metronome, dust, glue
    metroButton.setBounds (RB (20, 114, 110, 38));
    metroVol.slider.setBounds (RB (196, 114, 180, 38));
    dustToggle.setBounds (RB (420, 114, 140, 38));
    dustRate.slider.setBounds (RB (626, 114, 200, 38));
    glueToggle.setBounds (RB (860, 114, 84, 38));
    glueThreshold.slider.setBounds (RB (1010, 114, 200, 38));
    glueBar.setBounds (RB (1240, 118, 200, 12));
    glueMeter.setBounds (RB (1240, 132, 200, 18));

    // Production Area: the tab bar in the panel header, the active view
    // below it (items 37/42). A popped-out view keeps its own window's
    // size, so it is only laid out here while docked.
    tabButtons[0].setBounds (RB (20, 178, 190, 28));
    tabButtons[1].setBounds (RB (218, 178, 150, 28));
    tabButtons[2].setBounds (RB (376, 178, 210, 28));
    popOutButton.setBounds (RB (1420, 178, 112, 28));
    tabPoppedLabel.setBounds (RB (16, 212, 1528, 336));
    if (sequencerPanel != nullptr && ! tabPoppedOut[0])
        sequencerPanel->setBounds (RB (8, 210, 1544, 340));
    if (mpcPanel != nullptr && ! tabPoppedOut[1])
        mpcPanel->setBounds (RB (16, 212, 1528, 336));
    if (multitrack != nullptr && ! tabPoppedOut[2])
        multitrack->setBounds (RB (16, 212, 1528, 336));

    // waveform panel
    for (int i = 0; i < 3; ++i)
        waveSel[i].setBounds (RB (226 + i * 44, 574, 38, 22));
    waveName.setBounds (RB (368, 574, 300, 22));
    wavePlayButton.setBounds (RB (676, 574, 80, 22));
    waveView.setBounds (RB (16, 602, 748, 92));

    // vocal panel
    vocalToggle.setBounds (RB (792, 600, 150, 26));
    micBox.setBounds (RB (950, 600, 220, 26));
    vocalHint.setBounds (RB (1178, 600, 362, 26));
    vocalDistance.slider.setBounds (RB (880, 632, 260, 26));
    vocalInput.slider.setBounds (RB (1228, 632, 260, 26));
    recordButton.setBounds (RB (792, 666, 92, 26));
    takeToggle.setBounds (RB (892, 666, 110, 26));
    takeLabel.setBounds (RB (1010, 666, 526, 26));

    // drum sound panel
    LabeledSlider* drum[] = { &kickTune, &kickDecay, &snareSnappy, &openHatDecay, &clapTail };
    for (int i = 0; i < 5; ++i)
        drum[i]->slider.setBounds (RB (128, 750 + i * 27, 280, 25));

    // instruments panel: the switcher on top, then the chosen instrument's
    // controls in the one shared block (item 29)
    synthTab.setBounds (RB (570, 718, 84, 24));
    pianoTab.setBounds (RB (660, 718, 84, 24));
    instrumentRollButton.setBounds (RB (1056, 718, 128, 24));
    synthWaveBox.setBounds (RB (560, 750, 130, 24));
    synthCutoff.slider.setBounds (RB (758, 750, 190, 24));
    synthReso.slider.setBounds (RB (1012, 750, 170, 24));
    synthAttack.slider.setBounds (RB (758, 782, 190, 24));
    synthRelease.slider.setBounds (RB (1012, 782, 170, 24));
    synthLevel.slider.setBounds (RB (758, 814, 190, 24));
    pianoBright.slider.setBounds (RB (758, 750, 190, 24));
    pianoDecay.slider.setBounds (RB (1012, 750, 170, 24));
    pianoLevel.slider.setBounds (RB (758, 782, 190, 24));

    // export panel
    exportBars.setBounds (RB (1216, 750, 96, 26));
    normalizeToggle.setBounds (RB (1216, 784, 130, 26));
    exportButton.setBounds (RB (1216, 818, 160, 30));
    exportStatus.setBounds (RB (1216, 854, 320, 24));
}
