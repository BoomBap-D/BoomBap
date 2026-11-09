#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "Multitrack.h"
#include "SequencerPanel.h"
#include "DataHome.h"
#include "WavWrite.h"

using namespace juce;

//==============================================================================
void MainComponent::openSampleChooser (int track)
{
    chooser = std::make_unique<FileChooser> ("Choose a sample", File(), "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this, track] (const FileChooser& fc)
                          {
                              const File f = fc.getResult();
                              if (f != File())
                                  loadSampleFile (track, f);
                          });
}

bool MainComponent::decodeSampleFile (const File& file, std::vector<float>& monoOut, double& sampleRateOut)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) return false;

    const int maxSamples = (int) (reader->sampleRate * 20.0); // keep pads to 20 seconds
    const int n = (int) jmin ((int64) maxSamples, reader->lengthInSamples);
    const int channels = (int) jmax (1u, reader->numChannels);

    AudioBuffer<float> buffer (channels, n);
    reader->read (&buffer, 0, n, 0, true, true);

    std::vector<float> mono ((size_t) n, 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += src[i] / (float) channels;
    }

    monoOut = std::move (mono);
    sampleRateOut = reader->sampleRate;
    return true;
}

// One loader for every row kind (fix pass 2):
// - kit rows 0-4 (item 40): the file BINDS to the row - the row plays the
//   sample with its steps until Reset brings the built-in drum back.
// - sample rows 5-7: the classic path (waveform panel + pad 1-3).
// - pad rows 8-20: the row IS pad (track - 4) - same sample store.
// - added rows 21+: the row's own sample slot (item 39).
void MainComponent::loadSampleFile (int track, const File& file)
{
    std::vector<float> mono;
    double fileSr = 44100.0;
    if (! decodeSampleFile (file, mono, fileSr))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't load sample",
                                          "BoomBap couldn't read " + file.getFileName());
        return;
    }

    beginUndoGesture(); // Pass 2 item 38: a sample load is one undoable action (a no-op while a project loads)
    const String shortName = file.getFileNameWithoutExtension().substring (0, 8);
    if (track <= boombap::BoomBapEngine::Clap)
    {
        engine.setKitSample (track, mono, fileSr);
        rowFiles[(size_t) track] = file;
    }
    else if (track < boombap::BoomBapEngine::kFirstOwnRow)
    {
        const int pad = track - boombap::BoomBapEngine::Sample1; // rows 5..20 -> pads 1..16
        if (pad < 3)
        {
            waveData[(size_t) pad] = mono;
            waveRates[(size_t) pad] = fileSr;
            samplePaths[(size_t) pad] = file;
            showWaveform (pad);
        }
        else
        {
            padFiles[(size_t) pad] = file;
        }
        engine.loadSample (track, std::move (mono), fileSr);
        if (padBank != nullptr) padBank->refreshPad (pad);
        if (mpcPanel != nullptr) mpcPanel->refreshPad (pad);
    }
    else
    {
        engine.loadSample (track, std::move (mono), fileSr);
        rowFiles[(size_t) track] = file;
    }
    if (sequencerPanel != nullptr) sequencerPanel->setRowLoadText (track, shortName);
    fireMediaChanged();
    endUndoGesture();
}

void MainComponent::fireMediaChanged()
{
    if (onMediaChanged && ! loadingProject)
        onMediaChanged (makeProjectData());
}

void MainComponent::loadPadFile (int pad, const File& file)
{
    if (pad < 3)
    {
        loadSampleFile (boombap::BoomBapEngine::Sample1 + pad, file);
        return;
    }

    std::vector<float> mono;
    double fileSr = 44100.0;
    if (! decodeSampleFile (file, mono, fileSr))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't load sample",
                                          "BoomBap couldn't read " + file.getFileName());
        return;
    }

    beginUndoGesture(); // Pass 2 item 38
    engine.loadPadSample (pad, std::move (mono), fileSr);
    padFiles[(size_t) pad] = file;
    if (padBank != nullptr) padBank->refreshPad (pad);
    if (mpcPanel != nullptr) mpcPanel->refreshPad (pad);
    if (sequencerPanel != nullptr && pad >= 3)
        sequencerPanel->setRowLoadText (boombap::BoomBapEngine::Sample1 + pad,
                                        file.getFileNameWithoutExtension().substring (0, 8));
    fireMediaChanged();
    endUndoGesture();
}

void MainComponent::openPadChooser (int pad)
{
    chooser = std::make_unique<FileChooser> ("Choose a sample", File(), "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this, pad] (const FileChooser& fc)
                          {
                              const File f = fc.getResult();
                              if (f != File())
                                  loadPadFile (pad, f);
                          });
}

String MainComponent::padDisplayName (int pad) const
{
    const File f = pad < 3 ? samplePaths[(size_t) pad] : padFiles[(size_t) pad];
    return f == File() ? String() : f.getFileName();
}

void MainComponent::openPadBank()
{
    if (padWindow == nullptr)
    {
        // Built on first open: a DocumentWindow shows itself the moment it
        // is created, so building it in the constructor pops it up at launch.
        padBank = std::make_unique<PadBankComponent> (engine,
            [this] (int pad) { openPadChooser (pad); },
            [this] (int pad) { return padDisplayName (pad); });
        padBank->setLookAndFeel (&lookAndFeel);
        padBank->onBeforeChange = [this] { beginUndoGesture(); };
        padBank->onAfterChange = [this] { endUndoGesture(); };
        padBank->onSelectPad = [this] (int ch) { selectChannel (ch); };
        padWindow = std::make_unique<PadWindow> ("BoomBap Pad Bank", boombap::ui::bg());
        padWindow->setLookAndFeel (&lookAndFeel);
        padWindow->setContentNonOwned (padBank.get(), false); // the unique_ptr owns it - the window must not delete it too
        padWindow->centreWithSize (860, 500);
    }
    padBank->refreshAll();
    padWindow->setVisible (true);
    padWindow->toFront (true);
}

void MainComponent::openPianoRoll()
{
    openPianoRollFor (rollTrack);
}

// Fix pass item 28: the roll is summoned FROM a track and bound to it - the
// window title, the lane and the sound all belong to that track.
void MainComponent::openPianoRollFor (int track)
{
    rollTrack = track;
    if (pianoWindow == nullptr)
    {
        pianoRoll = std::make_unique<PianoRollComponent> (engine);
        pianoRoll->setLookAndFeel (&lookAndFeel);
        pianoRoll->onBeforeChange = [this] { beginUndoGesture(); };
        pianoRoll->onAfterChange = [this] { endUndoGesture(); };
        pianoRoll->onSelectChannel = [this] (int ch) { selectChannel (ch); };
        pianoWindow = std::make_unique<PianoRollWindow> ("BoomBap Piano Roll", boombap::ui::bg());
        pianoWindow->setLookAndFeel (&lookAndFeel);
        pianoWindow->setContentNonOwned (pianoRoll.get(), false); // the unique_ptr owns it - the window must not delete it too
        pianoWindow->centreWithSize (900, 620);
    }
    pianoRoll->bindToTrack (track);
    pianoWindow->setName (pianoRoll->bindingTitle());
    pianoWindow->setVisible (true);
    pianoWindow->toFront (true);
}

void MainComponent::openPlaylist()
{
    if (playlistWindow == nullptr)
    {
        playlist = std::make_unique<PlaylistComponent> (engine);
        playlist->setLookAndFeel (&lookAndFeel);
        playlist->onBeforeChange = [this] { beginUndoGesture(); };
        playlist->onAfterChange = [this] { endUndoGesture(); };
        playlistWindow = std::make_unique<PlaylistWindow> ("BoomBap Playlist", boombap::ui::bg());
        playlistWindow->setLookAndFeel (&lookAndFeel);
        playlistWindow->setContentNonOwned (playlist.get(), false); // the unique_ptr owns it - the window must not delete it too
        playlistWindow->centreWithSize (700, 320);
    }
    playlist->refreshFromEngine();
    playlistWindow->setVisible (true);
    playlistWindow->toFront (true);
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
    {
        engine.stopRecording();
        takeDirty = true;
        placeTakeAsClip();
    }
    else
    {
        if (! vocalToggle.getToggleState())
            vocalToggle.setToggleState (true, sendNotification); // opens the mic input too
        engine.startRecording();
    }
    refreshTakeUi();
}

void MainComponent::refreshTakeUi()
{
    const bool rec = engine.isRecording();
    recordButton.setButtonText (rec ? "Stop" : "Record");
    recordButton.setColour (TextButton::buttonColourId,
                            rec ? Colour (0xffc0392b) : boombap::ui::panelAlt());
    recordButton.setColour (TextButton::textColourOffId, Colours::white);
    // item 56: the switch shows the engine's truth, never a stale default
    takeToggle.setToggleState (engine.takeEnabled(), dontSendNotification);

    String text;
    if (rec)
        text = "Recording... " + String (engine.recordedSeconds(), 1) + " s";
    else if (engine.hasTake() && engine.takeIsPlaced())
    {
        const int slot = engine.takeClipSlot();
        const int trk = slot >= 0 ? engine.getAudioClip (slot).track + 1 : 1;
        text = "Take: " + String (engine.takeSeconds(), 1) + " s - it lives on AUDIO TRACK "
             + String (trk) + " as one clip";
    }
    else if (engine.hasTake())
        text = "Take: " + String (engine.takeSeconds(), 1) + " s - 'Loop take' plays it raw with the beat";
    else
        text = "No take yet - press Record and perform over the beat";

    if (takeLabel.getText() != text)
        takeLabel.setText (text, dontSendNotification);
}

// Item 62: a recorded take is a real WAV in Documents/BoomBap/Takes - in
// the standalone AND the plugin (this component serves both). The project
// references that file; an older sidecar WAV from beside a project file
// is copied home the first time the project is saved again.
void MainComponent::ensureTakeFileInHome()
{
    if (! engine.hasTake()) return;
    boombap::ensureDataHome();
    const File current (takePath);
    const auto takes = boombap::takesDir();

    if (! takeDirty && current.existsAsFile() && current.getParentDirectory() == takes)
        return; // already homed and up to date

    File dest;
    if (! takeDirty && current.existsAsFile())
    {
        dest = takes.getChildFile (current.getFileName());
        if (dest.existsAsFile() && dest.getSize() == current.getSize())
        {
            takePath = dest.getFullPathName(); // the home copy is already there
            fireMediaChanged();
            return;
        }
        if (dest.existsAsFile())
            dest = boombap::newTakeFile();
        if (! current.copyFileTo (dest)) return;
    }
    else
    {
        dest = boombap::newTakeFile();
        if (! writeTakeWav (dest)) return;
    }
    takePath = dest.getFullPathName();
    takeDirty = false;
    fireMediaChanged();
}

bool MainComponent::writeTakeWav (const File& file)
{
    const auto data = engine.getTakeData();
    if (data.empty()) return false;

    AudioBuffer<float> buffer (1, (int) data.size());
    buffer.copyFrom (0, 0, data.data(), (int) data.size());

    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> writer (wav.createWriterFor (new FileOutputStream (file),
                                                                      engine.getSampleRate(), 1, 24, {}, 0));
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buffer, 0, (int) data.size());
}

void MainComponent::showWaveform (int index)
{
    waveTrack = index;
    engine.stopAudition(); // the audition belongs to the sample it started on
    waveView.setData (waveData[(size_t) index]);
    for (int i = 0; i < 3; ++i)
        waveSel[i].setToggleState (i == index, dontSendNotification);
    const File f = samplePaths[(size_t) index];
    waveName.setText (f == File() ? "S" + String (index + 1) + " - no sample loaded"
                                  : f.getFileName(), dontSendNotification);
}
