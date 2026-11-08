#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
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

    const int waveIdx = track - boombap::BoomBapEngine::Sample1;
    waveData[(size_t) waveIdx] = mono;
    engine.loadSample (track, std::move (mono), fileSr);
    samplePaths[(size_t) waveIdx] = file;
    rows[track].load.setButtonText (file.getFileNameWithoutExtension().substring (0, 8));
    showWaveform (waveIdx);
    if (padBank != nullptr) padBank->refreshPad (waveIdx);
    fireMediaChanged();
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

    engine.loadPadSample (pad, std::move (mono), fileSr);
    padFiles[(size_t) pad] = file;
    if (padBank != nullptr) padBank->refreshPad (pad);
    fireMediaChanged();
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
    if (pianoWindow == nullptr)
    {
        pianoRoll = std::make_unique<PianoRollComponent> (engine);
        pianoRoll->setLookAndFeel (&lookAndFeel);
        pianoWindow = std::make_unique<PianoRollWindow> ("BoomBap Piano Roll", boombap::ui::bg());
        pianoWindow->setLookAndFeel (&lookAndFeel);
        pianoWindow->setContentNonOwned (pianoRoll.get(), false); // the unique_ptr owns it - the window must not delete it too
        pianoWindow->centreWithSize (760, 600);
    }
    pianoRoll->refreshFromEngine();
    pianoWindow->setVisible (true);
    pianoWindow->toFront (true);
}

void MainComponent::openPlaylist()
{
    if (playlistWindow == nullptr)
    {
        playlist = std::make_unique<PlaylistComponent> (engine);
        playlist->setLookAndFeel (&lookAndFeel);
        playlistWindow = std::make_unique<PlaylistWindow> ("BoomBap Playlist", boombap::ui::bg());
        playlistWindow->setLookAndFeel (&lookAndFeel);
        playlistWindow->setContentNonOwned (playlist.get(), false); // the unique_ptr owns it - the window must not delete it too
        playlistWindow->centreWithSize (700, 320);
    }
    playlist->refreshFromEngine();
    playlistWindow->setVisible (true);
    playlistWindow->toFront (true);
}

void MainComponent::refreshPatternUi()
{
    const int cur = engine.getEditPattern();
    for (int i = 0; i < 8; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        const bool sel = i == cur;
        b.setColour (TextButton::buttonColourId, sel ? boombap::ui::accent() : boombap::ui::panelAlt());
        b.setColour (TextButton::textColourOffId, sel ? Colour (0xff14100a) : boombap::ui::text());
    }
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
    {
        engine.stopRecording();
        takeDirty = true;
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

    String text;
    if (rec)
        text = "Recording... " + String (engine.recordedSeconds(), 1) + " s";
    else if (engine.hasTake())
        text = "Take: " + String (engine.takeSeconds(), 1) + " s, loops with the beat";
    else
        text = "No take yet - press Record and perform over the beat";

    if (takeLabel.getText() != text)
        takeLabel.setText (text, dontSendNotification);
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
    waveView.setData (waveData[(size_t) index]);
    for (int i = 0; i < 3; ++i)
        waveSel[i].setToggleState (i == index, dontSendNotification);
    const File f = samplePaths[(size_t) index];
    waveName.setText (f == File() ? "S" + String (index + 1) + " - no sample loaded"
                                  : f.getFileName(), dontSendNotification);
}
