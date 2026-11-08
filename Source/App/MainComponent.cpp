#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "Multitrack.h"
#include "SequencerPanel.h"
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

// A Production Area view popped out into its own window (items 37/42):
// closing the window docks the view back onto its tab.
class PopOutWindow : public juce::DocumentWindow
{
public:
    PopOutWindow (const String& name, std::function<void()> onClose)
        : DocumentWindow (name, boombap::ui::bg(), juce::DocumentWindow::closeButton),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (560, 320, 4000, 3000);
    }

    void closeButtonPressed() override
    {
        if (closeCallback) closeCallback();
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
    boombap::ensureDataHome(); // item 62: Documents/BoomBap exists from first run, both shells
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

    // item 46: the waveform panel auditions its own sample, on its own
    // button - and the global stop silences it like everything else.
    addAndMakeVisible (wavePlayButton);
    wavePlayButton.onClick = [this]
    {
        if (engine.isAuditionPlaying()) { engine.stopAudition(); return; }
        if (! waveData[waveTrack].empty())
            engine.startAudition (waveData[waveTrack],
                                  waveRates[waveTrack] > 0.0 ? waveRates[waveTrack] : engine.getSampleRate());
    };

    // ---- pad bank (own window, built on first open - a DocumentWindow shows itself when created, so it must not be built at launch) ----
    addAndMakeVisible (padBankButton);
    padBankButton.onClick = [this] { openPadBank(); };

    // ---- piano roll (own window, same arrangement) ----
    addAndMakeVisible (pianoButton);
    pianoButton.onClick = [this] { openPianoRoll(); };

    // ---- the Production Area (fix pass 2, items 37/42): a labelled,
    // outlined TAB BAR - STEP SEQUENCER / MPC PANEL / AUDIO MULTITRACK -
    // replacing the old corner toggle D could not find. Every tab can
    // pop out into its own window and dock back (his Reaper grammar).
    {
        const char* names[3] = { "STEP SEQUENCER", "MPC PANEL", "AUDIO MULTITRACK" };
        for (int i = 0; i < 3; ++i)
        {
            tabButtons[i].setButtonText (names[i]);
            tabButtons[i].onClick = [this, i] { selectTab (i); };
            addAndMakeVisible (tabButtons[i]);
        }
    }
    popOutButton.onClick = [this] { togglePopOut(); };
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
        scb.onNameMenu = [this] (int t) { showKitMenu (t); };
        scb.onPatternChanged = [this] { if (pianoRoll != nullptr) pianoRoll->refreshFromEngine(); };
        sequencerPanel = std::make_unique<SequencerPanel> (engine, scb);
    }
    sequencerPanel->setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (sequencerPanel.get());

    // The MPC PANEL tab: its own pad bank instance (pads + the selected
    // pad's waveform and trim, item 44) - same engine, same samples as
    // the standalone Pad Bank window; both stay in step through it.
    mpcPanel = std::make_unique<PadBankComponent> (engine,
        [this] (int pad) { openPadChooser (pad); },
        [this] (int pad) { return padDisplayName (pad); });
    mpcPanel->setLookAndFeel (&lookAndFeel);
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
    addChildComponent (multitrack.get());
    multitrack->setVisible (false);
    multitrack->setHomePathText ("BoomBap home: " + boombap::dataHome().getFullPathName()
                                 + "  -  takes land in its Takes folder");
    updateTabChrome();
    refreshGrid();

    // ---- playlist (own window) ----
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
    takeToggle.setToggleState (false, dontSendNotification); // item 56: off until chosen
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
    if (sequencerPanel != nullptr) sequencerPanel->refreshGrid();
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

void MainComponent::popOutTab (int index)
{
    auto* content = tabContent (index);
    if (content == nullptr || tabPoppedOut[index]) return;
    static const char* titles[3] = { "BoomBap - Step Sequencer", "BoomBap - MPC Panel", "BoomBap - Audio Multitrack" };
    auto win = std::make_unique<PopOutWindow> (titles[index], [this, index] { dockTab (index); });
    win->setContentOwned (content, false);
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
    if (productionWindows[index] != nullptr)
    {
        productionWindows[index]->clearContentComponent();
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
            engine.clearKitSample (track);
            rowFiles[track] = juce::File();
            if (sequencerPanel != nullptr) sequencerPanel->setRowLoadText (track, "Load...");
            fireMediaChanged();
        }
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
    // MPC-style: pressing Play while already playing restarts from the top.
    if (engine.isPlaying())
        engine.setPlaying (false);
    engine.setPlaying (true);
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
        if (c == 's') { if (mods.isShiftDown()) saveProjectAs ({}); else saveProject ({}); return true; }
        if (c == 'o') { confirmDiscardThen ([this] { openProjectChooser(); }); return true; }
        if (c == 'n') { confirmDiscardThen ([this] { newProject(); }); return true; }
    }
    return false;
}

void MainComponent::timerCallback()
{
    if (sequencerPanel != nullptr) sequencerPanel->tick();

    const bool playingNow = engine.isPlaying();
    if (playingNow != lastPlayingState)
    {
        lastPlayingState = playingNow;
        playButton.setColour (TextButton::buttonColourId,
                              playingNow ? boombap::ui::accentHi() : boombap::ui::accent());
    }

    if (++dirtyTick >= 8) { dirtyTick = 0; updateProjectLabel(); }

    // the waveform panel's audition button tells the truth (item 46)
    {
        const bool aud = engine.isAuditionPlaying();
        const juce::String want = aud ? "STOP" : "PLAY";
        if (wavePlayButton.getButtonText() != want) wavePlayButton.setButtonText (want);
    }

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
