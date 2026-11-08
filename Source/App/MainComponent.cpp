#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "Multitrack.h"
#include "AppSettings.h"

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
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (480, 400, 1200, 900);
    }

    void closeButtonPressed() override
    {
        if (closeCallback) closeCallback();
        setVisible (false);
    }

private:
    std::function<void()> closeCallback;
};
} // namespace

//==============================================================================
MainComponent::MainComponent (boombap::BoomBapEngine& eng, bool useOwnAudioDevice)
    : engine (eng), ownAudioDevice (useOwnAudioDevice)
{
    formats.registerBasicFormats();
    setLookAndFeel (&lookAndFeel);
    setSize (1560, 900); // Pass 1 item 1: landscape design size; the layout scales to fit smaller windows
    setWantsKeyboardFocus (true);

    // ---- project bar ----
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &recentButton })
        addAndMakeVisible (b);
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

    // ---- transport (Pass 1 item 2: big labelled MPC-style buttons) ----
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (resetButton);
    playButton.setColour (TextButton::buttonColourId, boombap::ui::accent());
    playButton.setColour (TextButton::textColourOffId, Colour (0xff14100a));
    playButton.onClick  = [this] { startTransport(); };
    stopButton.onClick  = [this] { stopTransport(); };
    clearButton.onClick = [this] { engine.clearPattern(); refreshGrid(); };
    resetButton.onClick = [this] { engine.loadDefaultPattern(); refreshGrid(); };

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
    dustToggle.onClick = [this] { engine.setDustEnabled (dustToggle.getToggleState()); };
    glueToggle.onClick = [this] { engine.setGlueEnabled (glueToggle.getToggleState()); };
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
    // Fix pass item 33: clicking the waveform panel loads a sample into the
    // row it is showing.
    waveView.onClick = [this] { openSampleChooser (boombap::BoomBapEngine::Sample1 + waveTrack); };
    showWaveform (0);

    // ---- pad bank (own window, built on first open - a DocumentWindow shows itself when created, so it must not be built at launch) ----
    addAndMakeVisible (padBankButton);
    padBankButton.onClick = [this] { openPadBank(); };

    // ---- piano roll (own window, same arrangement) ----
    addAndMakeVisible (pianoButton);
    pianoButton.onClick = [this] { openPianoRoll(); };

    // ---- pattern selector (lives in the grid header strip) ----
    patternLabel.setText ("PATTERN", dontSendNotification);
    patternLabel.setColour (Label::textColourId, boombap::ui::muted());
    patternLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    addAndMakeVisible (patternLabel);
    for (int i = 0; i < 8; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        b.setButtonText (String::charToString ((juce_wchar) ('A' + i)));
        b.onClick = [this, i]
        {
            engine.setEditPattern (i);
            refreshGrid();
            refreshPatternUi();
            if (pianoRoll != nullptr) pianoRoll->refreshFromEngine();
        };
        addAndMakeVisible (b);
    }
    refreshPatternUi();

    // ---- playlist (own window) ----
    addAndMakeVisible (playlistButton);
    playlistButton.onClick = [this] { openPlaylist(); };

    // ---- work-area view switch (Fix pass item 35) + the multitrack ----
    addAndMakeVisible (viewButton);
    viewButton.onClick = [this] { setAudioView (! audioView); };
    multitrack = std::make_unique<MultitrackComponent> (engine, [] {});
    multitrack->setLookAndFeel (&lookAndFeel);
    multitrack->setVisible (false);
    addAndMakeVisible (multitrack.get());

    addAndMakeVisible (dustRate.slider);
    addAndMakeVisible (glueThreshold.slider);
    setupSlider (dustRate, "Rate", 7500.0, 39375.0, 26000.0, 100.0, " Hz",
                 [this] { engine.setDustRate ((float) dustRate.slider.getValue()); });
    setupSlider (glueThreshold, "Thresh", -30.0, 0.0, -12.0, 0.5, " dB",
                 [this] { engine.setGlueThresholdDb ((float) glueThreshold.slider.getValue()); });

    // ---- grid rows ----
    for (int t = 0; t < kTracks; ++t)
    {
        Row& row = rows[t];
        addAndMakeVisible (row.name);
        row.name.setText (boombap::BoomBapEngine::trackName (t), dontSendNotification);
        row.name.setColour (Label::textColourId, Colours::white);

        if (t >= boombap::BoomBapEngine::Sample1)
        {
            addAndMakeVisible (row.load);
            row.load.onClick = [this, t] { openSampleChooser (t); };
        }

        // Fix pass item 28: pitched rows summon the piano roll bound to
        // themselves - the kick (the pitched melody sound) and the sample
        // rows (their own pad, pitched).
        if (t == boombap::BoomBapEngine::Kick || t >= boombap::BoomBapEngine::Sample1)
        {
            addAndMakeVisible (row.roll);
            row.roll.onClick = [this, t] { openPianoRollFor (t); };
        }

        addAndMakeVisible (row.mute);
        row.mute.setClickingTogglesState (true);
        row.mute.setColour (TextButton::buttonOnColourId, Colours::indianred);
        row.mute.onClick = [this, t] { engine.setTrackMute (t, rows[t].mute.getToggleState()); };

        addAndMakeVisible (row.solo);
        row.solo.setClickingTogglesState (true);
        row.solo.setColour (TextButton::buttonOnColourId, Colour (0xffa8871a));
        row.solo.onClick = [this, t] { engine.setTrackSolo (t, rows[t].solo.getToggleState()); };

        addAndMakeVisible (row.level);
        addAndMakeVisible (row.meter);
        row.level.setSliderStyle (Slider::LinearHorizontal);
        row.level.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        row.level.setRange (0.0, 1.2, 0.01);
        row.level.setValue (0.8, dontSendNotification);
        row.level.setDoubleClickReturnValue (true, 0.8); // Pass 1: double-click reset
        row.level.onValueChange = [this, t] { engine.setTrackGain (t, (float) rows[t].level.getValue()); };

        for (int s = 0; s < kSteps; ++s)
        {
            StepButton& sb = row.steps[s];
            addAndMakeVisible (sb);
            sb.setBeatShade ((s / 4) % 2 == 0);
            sb.onChange = [this, t, s] (int v) { engine.setStep (t, s, v); };
        }
    }
    refreshGrid();

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
    micBox.onChange = [this] { engine.setVocalMic (micBox.getSelectedId() - 1); };

    vocalToggle.onClick = [this]
    {
        const bool on = vocalToggle.getToggleState();
        engine.setVocalEnabled (on);
        if (ownAudioDevice)
        {
            auto state = deviceManager.createStateXml(); // keep the chosen device / rate / buffer
            setAudioChannels (on ? 1 : 0, 2, state.get()); // only asks for the microphone once you switch this on
        }
        // in a plugin the host owns the audio device; the toggle just arms the vocal channel
    };

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
    takeToggle.setToggleState (true, dontSendNotification);
    takeToggle.onClick = [this] { engine.setTakeEnabled (takeToggle.getToggleState()); refreshTakeUi(); };
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
    synthWaveBox.onChange = [this] { engine.setSynthWave (synthWaveBox.getSelectedId() - 1); };

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
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < kSteps; ++s)
            rows[t].steps[s].setVelocity (engine.getStep (t, s));
}

// Fix pass item 29: one instrument at a time. The switcher shows the chosen
// instrument's controls; if the piano roll is open it rebinds to that
// instrument's lane, so controls and lane are never a hunt.
void MainComponent::selectInstrument (int which)
{
    shownInstrument = which;
    const bool synth = which == 0;

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

// Fix pass item 35: one button swaps the top work area between the step
// sequencer and the audio multitrack (D's Reaper grammar).
void MainComponent::setAudioView (bool audio)
{
    audioView = audio;
    viewButton.setButtonText (audio ? "STEPS" : "AUDIO");

    for (int t = 0; t < kTracks; ++t)
    {
        Row& row = rows[t];
        row.name.setVisible (! audio);
        row.load.setVisible (! audio && t >= boombap::BoomBapEngine::Sample1);
        row.roll.setVisible (! audio && (t == boombap::BoomBapEngine::Kick
                                         || t >= boombap::BoomBapEngine::Sample1));
        row.mute.setVisible (! audio);
        row.solo.setVisible (! audio);
        row.level.setVisible (! audio);
        row.meter.setVisible (! audio);
        for (int s = 0; s < kSteps; ++s)
            row.steps[s].setVisible (! audio);
    }
    patternLabel.setVisible (! audio);
    for (auto& b : patternButtons)
        b.setVisible (! audio);

    if (multitrack != nullptr)
    {
        multitrack->setVisible (audio);
        if (audio)
            multitrack->refreshFromEngine();
    }
    resized();
    repaint();
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
    // MPC-style: pressing Play while already playing restarts from the top.
    if (engine.isPlaying())
        engine.setPlaying (false);
    engine.setPlaying (true);
}

void MainComponent::stopTransport()
{
    engine.setPlaying (false);
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::spaceKey) { togglePlay(); return true; }

    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const int c = (int) CharacterFunctions::toLowerCase ((juce_wchar) key.getKeyCode());
        if (c == 's') { if (mods.isShiftDown()) saveProjectAs ({}); else saveProject ({}); return true; }
        if (c == 'o') { confirmDiscardThen ([this] { openProjectChooser(); }); return true; }
        if (c == 'n') { confirmDiscardThen ([this] { newProject(); }); return true; }
    }
    return false;
}

void MainComponent::timerCallback()
{
    const int s = engine.currentStep();
    if (s != lastPlayStep)
    {
        lastPlayStep = s;
        for (int t = 0; t < kTracks; ++t)
            for (int st = 0; st < kSteps; ++st)
                rows[t].steps[st].setPlaying (st == s);
    }

    const bool playingNow = engine.isPlaying();
    if (playingNow != lastPlayingState)
    {
        lastPlayingState = playingNow;
        playButton.setColour (TextButton::buttonColourId,
                              playingNow ? boombap::ui::accentHi() : boombap::ui::accent());
    }

    if (++dirtyTick >= 8) { dirtyTick = 0; updateProjectLabel(); }

    for (int t = 0; t < kTracks; ++t)
    {
        const float peak = engine.consumeTrackPeak (t);
        meterShown[t] = std::max (peak, meterShown[t] * 0.82f);
        rows[t].meter.setLevel (meterShown[t]);
    }

    // pad activity (Pass 1 item 4b): light + pad-set meter here, flash in the pad bank
    const float padPeak = engine.consumePadActivityPeak();
    padShown = std::max (padPeak, padShown * 0.82f);
    padMeter.setLevel (padShown);
    padLight.setLevel (padShown);
    if (padBank != nullptr && padPeak > 0.02f)
        padBank->showActivity (engine.lastTriggeredPad());

    // pad capture (Fix pass item 31): a captured hit flashes its pad and the
    // new step shows in the grid straight away
    const int captured = engine.consumeCapturedPad();
    if (captured >= 0)
    {
        if (padBank != nullptr) padBank->showActivity (captured);
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
    drawPanel (8, 172, 1544, 384);   // step grid
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
    if (! audioView)
    {
        g.drawText ("Click a step: normal, accent, off.  Right-click clears.  Space = play/stop.  Ctrl/Cmd+S = save.",
                    RB (440, 182, 1105, 24), Justification::left);

        g.setFont (12.0f);
        for (int s = 0; s < kSteps; s += 4)
            g.drawText (String (s / 4 + 1), RB (398 + s * 66 + (s / 4) * 8, 212, 40, 14), Justification::centredLeft);
    }
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
    projectName.setBounds (RB (500, 6, 430, 28));
    pianoButton.setBounds (RB (1150, 6, 96, 28));
    playlistButton.setBounds (RB (1254, 6, 88, 28));
    padBankButton.setBounds (RB (1350, 6, 100, 28));

    // transport row A: big play/stop, pattern edit helpers, tempo, swing, master, pad activity
    playButton.setBounds (RB (20, 54, 150, 50));
    stopButton.setBounds (RB (178, 54, 150, 50));
    clearButton.setBounds (RB (336, 54, 84, 50));
    resetButton.setBounds (RB (428, 54, 84, 50));
    bpm.slider.setBounds (RB (578, 54, 230, 50));
    swing.slider.setBounds (RB (888, 54, 190, 50));
    master.slider.setBounds (RB (1152, 54, 200, 50));
    padLight.setBounds (RB (1372, 61, 36, 36));
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
    viewButton.setBounds (RB (1456, 114, 88, 38));

    // pattern selector strip (in the grid panel header)
    patternLabel.setBounds (RB (20, 182, 70, 24));
    for (int i = 0; i < 8; ++i)
        patternButtons[(size_t) i].setBounds (RB (96 + i * 40, 182, 36, 24));

    // grid
    for (int t = 0; t < kTracks; ++t)
    {
        Row& row = rows[t];
        const int y = 232 + t * 40;
        row.name.setBounds (RB (20, y, 92, 36));
        row.load.setBounds (RB (116, y + 5, 54, 26));
        row.roll.setBounds (RB (174, y + 5, 46, 26));
        row.mute.setBounds (RB (224, y + 5, 28, 26));
        row.solo.setBounds (RB (256, y + 5, 28, 26));
        row.level.setBounds (RB (292, y, 94, 36));
        for (int s = 0; s < kSteps; ++s)
            row.steps[s].setBounds (RB (398 + s * 66 + (s / 4) * 8, y, 60, 36));
        row.meter.setBounds (RB (1480, y + 14, 60, 8));
    }

    // multitrack (audio view) fills the grid panel
    if (multitrack != nullptr)
        multitrack->setBounds (RB (16, 180, 1528, 368));

    // waveform panel
    for (int i = 0; i < 3; ++i)
        waveSel[i].setBounds (RB (226 + i * 44, 574, 38, 22));
    waveName.setBounds (RB (368, 574, 380, 22));
    waveView.setBounds (RB (16, 602, 748, 92));

    // vocal panel
    vocalToggle.setBounds (RB (792, 600, 150, 26));
    micBox.setBounds (RB (950, 600, 220, 26));
    vocalHint.setBounds (RB (1178, 600, 362, 26));
    vocalDistance.slider.setBounds (RB (880, 632, 260, 26));
    vocalInput.slider.setBounds (RB (1228, 632, 260, 26));
    recordButton.setBounds (RB (792, 666, 92, 26));
    takeToggle.setBounds (RB (892, 666, 76, 26));
    takeLabel.setBounds (RB (976, 666, 560, 26));

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
