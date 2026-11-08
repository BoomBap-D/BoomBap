#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"

using namespace juce;

//==============================================================================
MainComponent::MainComponent (boombap::BoomBapEngine& eng, bool useOwnAudioDevice)
    : engine (eng), ownAudioDevice (useOwnAudioDevice)
{
    formats.registerBasicFormats();
    setLookAndFeel (&lookAndFeel);
    setSize (1100, 1082);
    setWantsKeyboardFocus (true);

    // ---- project bar ----
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &recentButton })
        addAndMakeVisible (b);
    addAndMakeVisible (projectName);
    projectName.setColour (Label::textColourId, Colours::white);
    projectName.setFont (juce::Font (15.0f));
    newButton.onClick    = [this] { confirmDiscardThen ([this] { newProject(); }); };
    openButton.onClick   = [this] { confirmDiscardThen ([this] { openProjectChooser(); }); };
    saveButton.onClick   = [this] { saveProject ({}); };
    saveAsButton.onClick = [this] { saveProjectAs ({}); };
    recentButton.onClick = [this] { showRecentMenu(); };

    // ---- transport ----
    addAndMakeVisible (playButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (resetButton);
    playButton.setColour (TextButton::buttonColourId, boombap::ui::accent());
    playButton.setColour (TextButton::textColourOffId, Colour (0xff14100a));
    playButton.onClick  = [this] { togglePlay(); };
    clearButton.onClick = [this] { engine.clearPattern(); refreshGrid(); };
    resetButton.onClick = [this] { engine.loadDefaultPattern(); refreshGrid(); };

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

        addAndMakeVisible (row.mute);
        row.mute.setClickingTogglesState (true);
        row.mute.setColour (TextButton::buttonOnColourId, Colours::indianred);
        row.mute.onClick = [this, t] { engine.setTrackMute (t, rows[t].mute.getToggleState()); };

        addAndMakeVisible (row.level);
        addAndMakeVisible (row.meter);
        row.level.setSliderStyle (Slider::LinearHorizontal);
        row.level.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        row.level.setRange (0.0, 1.2, 0.01);
        row.level.setValue (0.8, dontSendNotification);
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
            setAudioChannels (on ? 1 : 0, 2); // only asks for the microphone once you switch this on
        // in a plugin the host owns the audio device; the toggle just arms the vocal channel
    };

    setupSlider (vocalDistance, "Distance", 3.0, 40.0, 15.0, 0.5, " cm", [this] { engine.setVocalDistance ((float) vocalDistance.slider.getValue()); });
    setupSlider (vocalInput,    "Input",   -12.0, 24.0, 0.0, 0.5, " dB", [this] { engine.setVocalInputDb ((float) vocalInput.slider.getValue()); });

    vocalHint.setText ("Use headphones: monitoring through speakers will feed back.", dontSendNotification);
    vocalHint.setColour (Label::textColourId, Colours::orange);
    vocalHint.setFont (juce::Font (13.0f));

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
        setAudioChannels (0, 2);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    setLookAndFeel (nullptr);
    stopTimer();
    shutdownAudio();
}

void MainComponent::setupSlider (LabeledSlider& s, const String& name, double min, double max, double init,
                                 double step, const String& suffix, std::function<void()> onChange)
{
    s.slider.setSliderStyle (Slider::LinearHorizontal);
    s.slider.setTextBoxStyle (Slider::TextBoxRight, false, 72, 20);
    s.slider.setRange (min, max, step);
    s.slider.setValue (init, dontSendNotification);
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
                     &pianoBright, &pianoDecay, &pianoLevel })
        if (s->slider.onValueChange) s->slider.onValueChange();

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

void MainComponent::togglePlay()
{
    engine.setPlaying (! engine.isPlaying());
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

    playButton.setButtonText (engine.isPlaying() ? "Stop" : "Play");

    if (++dirtyTick >= 8) { dirtyTick = 0; updateProjectLabel(); }

    for (int t = 0; t < kTracks; ++t)
    {
        const float peak = engine.consumeTrackPeak (t);
        meterShown[t] = std::max (peak, meterShown[t] * 0.82f);
        rows[t].meter.setLevel (meterShown[t]);
    }

    const bool recNow = engine.isRecording();
    if (uiRecording && ! recNow) { engine.finishTake(); takeDirty = true; }
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
void MainComponent::paint (Graphics& g)
{
    using namespace boombap::ui;
    g.fillAll (bg());

    const int oy = kProjectBar;

    // project bar strip
    g.setColour (panel());
    g.fillRect (0, 0, getWidth(), oy);
    g.setColour (border().withAlpha (0.7f));
    g.fillRect (0, oy - 1, getWidth(), 1);

    // section panels
    auto drawPanel = [&] (int x, int y, int w, int h)
    {
        Rectangle<float> r ((float) x, (float) y, (float) w, (float) h);
        g.setColour (panel());
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (border().withAlpha (0.55f));
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
    };
    drawPanel (8, oy + 4,   getWidth() - 16, 96);    // transport + master FX
    drawPanel (8, oy + 106, getWidth() - 16, 366);   // step grid
    drawPanel (8, oy + 478, getWidth() - 16, 100);   // sample waveform
    drawPanel (8, oy + 586, 532, 200);               // drum sound
    drawPanel (548, oy + 586, getWidth() - 556, 262); // vocal + export
    drawPanel (8, oy + 856, getWidth() - 16, 190);    // instruments

    g.setColour (text().withAlpha (0.85f));
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText ("SAMPLE WAVEFORM", 20, oy + 486, 220, 20, Justification::left);
    g.drawText ("DRUM SOUND", 20, oy + 594, 300, 20, Justification::left);
    g.drawText ("VOCAL", 560, oy + 594, 300, 20, Justification::left);
    g.drawText ("EXPORT", 560, oy + 782, 300, 20, Justification::left);
    g.drawText ("INSTRUMENTS", 20, oy + 864, 300, 20, Justification::left);
    g.drawText ("SYNTH", 20, oy + 890, 60, 20, Justification::left);
    g.drawText ("PIANO", 580, oy + 890, 60, 20, Justification::left);

    g.setColour (muted());
    g.setFont (12.0f);
    g.drawText ("Synth and piano play from the Piano Roll - pick them in its Sound menu. Synth root C3, piano root C4.",
                20, oy + 1022, 1000, 18, Justification::left);

    g.setColour (muted());
    g.setFont (12.0f);
    for (int s = 0; s < kSteps; s += 4)
    {
        const int x = 330 + s * 42 + (s / 4) * 6;
        g.drawText (String (s / 4 + 1), x, oy + 106, 40, 14, Justification::centredLeft);
    }
    g.drawText ("Click a step: normal, accent, off.  Right-click clears.  Space = play/stop.  Ctrl/Cmd+S = save.",
                12, oy + 792, 540, 18, Justification::left);
}

void MainComponent::resized()
{
    const int oy = kProjectBar;

    // project bar
    newButton.setBounds (12, 6, 60, 26);
    openButton.setBounds (78, 6, 80, 26);
    saveButton.setBounds (164, 6, 60, 26);
    saveAsButton.setBounds (230, 6, 90, 26);
    recentButton.setBounds (326, 6, 76, 26);
    projectName.setBounds (410, 6, 330, 26);
    pianoButton.setBounds (748, 6, 96, 26);
    playlistButton.setBounds (852, 6, 88, 26);
    padBankButton.setBounds (948, 6, 92, 26);
    patternLabel.setBounds (12, oy + 105, 56, 18);
    for (int i = 0; i < 8; ++i)
        patternButtons[(size_t) i].setBounds (72 + i * 30, oy + 105, 26, 18);

    // top bar
    playButton.setBounds (12, oy + 12, 90, 40);
    clearButton.setBounds (110, oy + 12, 70, 40);
    resetButton.setBounds (188, oy + 12, 70, 40);
    bpm.slider.setBounds (310, oy + 12, 200, 40);
    swing.slider.setBounds (610, oy + 12, 200, 40);
    master.slider.setBounds (900, oy + 12, 190, 40);

    // master effects
    dustToggle.setBounds (12, oy + 62, 130, 30);
    dustRate.slider.setBounds (210, oy + 62, 220, 30);
    glueToggle.setBounds (480, oy + 62, 80, 30);
    glueThreshold.slider.setBounds (650, oy + 62, 220, 30);
    glueBar.setBounds (890, oy + 64, 150, 10);
    glueMeter.setBounds (890, oy + 76, 150, 16);

    // grid
    for (int t = 0; t < kTracks; ++t)
    {
        Row& row = rows[t];
        const int y = oy + 124 + t * 43;
        row.name.setBounds (12, y, 90, 38);
        row.load.setBounds (100, y + 6, 62, 26);
        row.mute.setBounds (168, y + 6, 32, 26);
        row.level.setBounds (204, y, 118, 38);
        for (int s = 0; s < kSteps; ++s)
            row.steps[s].setBounds (330 + s * 42 + (s / 4) * 6, y, 40, 38);
        row.meter.setBounds (1026, y + 15, 58, 8);
    }

    // waveform panel
    for (int i = 0; i < 3; ++i)
        waveSel[i].setBounds (226 + i * 44, oy + 485, 38, 22);
    waveName.setBounds (368, oy + 485, 624, 22);
    waveView.setBounds (16, oy + 512, getWidth() - 32, 58);

    // drum sound panel
    LabeledSlider* drum[] = { &kickTune, &kickDecay, &snareSnappy, &openHatDecay, &clapTail };
    for (int i = 0; i < 5; ++i)
        drum[i]->slider.setBounds (130, oy + 622 + i * 34, 380, 28);

    // vocal panel
    vocalToggle.setBounds (560, oy + 620, 150, 28);
    micBox.setBounds (720, oy + 620, 220, 28);
    vocalDistance.slider.setBounds (650, oy + 650, 380, 28);
    vocalInput.slider.setBounds (650, oy + 684, 380, 28);
    vocalHint.setBounds (560, oy + 714, 520, 22);
    recordButton.setBounds (560, oy + 742, 92, 26);
    takeToggle.setBounds (660, oy + 742, 76, 26);
    takeLabel.setBounds (744, oy + 742, 348, 26);

    // export
    exportBars.setBounds (560, oy + 798, 90, 28);
    normalizeToggle.setBounds (660, oy + 798, 110, 28);
    exportButton.setBounds (780, oy + 798, 130, 28);
    exportStatus.setBounds (920, oy + 798, 170, 28);

    // instruments panel
    synthWaveBox.setBounds (84, oy + 888, 130, 24);
    synthCutoff.slider.setBounds (150, oy + 918, 176, 26);
    synthAttack.slider.setBounds (150, oy + 950, 176, 26);
    synthLevel.slider.setBounds (150, oy + 982, 176, 26);
    synthReso.slider.setBounds (430, oy + 918, 176, 26);
    synthRelease.slider.setBounds (430, oy + 950, 176, 26);
    pianoBright.slider.setBounds (716, oy + 918, 344, 26);
    pianoDecay.slider.setBounds (716, oy + 950, 344, 26);
    pianoLevel.slider.setBounds (716, oy + 982, 344, 26);
}
