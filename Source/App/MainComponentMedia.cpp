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

namespace
{
juce::uint16 rdU16le (const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
juce::uint32 rdU32le (const juce::uint8* p)
{
    return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
         | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
}
juce::uint64 rdU64le (const juce::uint8* p)
{
    return (juce::uint64) rdU32le (p) | ((juce::uint64) rdU32le (p + 4) << 32);
}

//==============================================================================
// Fallback WAV decoder (Fix Pass 3, item 75). JUCE's WavAudioFormat
// reader trusts the header completely, and real files in the wild break
// that trust: unfinalised recorder files and RF64 shells declare their
// data chunk as 0 or 0xFFFFFFFF bytes, and some writers place data
// before fmt. For those, the stock reader reports ZERO frames (the pad
// silently loads nothing at all) or a garbage near-billion frames (the
// pad loads the 20 second cap of mostly silence). This decoder walks
// the RIFF itself, measures the audio from the bytes actually present
// in the file, and converts PCM 8/16/24/32 and float 32/64 data to
// mono. Anything compressed inside a WAV shell (MP3-in-WAV and kin) is
// refused here - the caller then shows its "couldn't read" alert.
bool decodeWavByHand (const juce::File& file, std::vector<float>& monoOut, double& sampleRateOut)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return false;
    const auto total = in.getTotalLength();
    if (total < 44 || total > (juce::int64) 1024 * 1024 * 1024) return false;
    std::vector<juce::uint8> bytes ((size_t) total);
    if (in.read (bytes.data(), (int) total) != (int) total) return false;

    auto tagIs = [&bytes] (size_t off, const char* t)
    {
        return off + 4 <= bytes.size() && std::memcmp (bytes.data() + off, t, 4) == 0;
    };
    if (! (tagIs (0, "RIFF") || tagIs (0, "RF64")) || ! tagIs (8, "WAVE")) return false;

    size_t fmtOff = 0, dataOff = 0;
    juce::uint32 fmtSize = 0;
    juce::uint64 dataBytes = 0, ds64DataBytes = 0;
    bool haveFmt = false, haveData = false;

    for (size_t pos = 12; pos + 8 <= bytes.size();)
    {
        const juce::uint32 sz = rdU32le (bytes.data() + pos + 4);
        const size_t payload = pos + 8;
        if (tagIs (pos, "ds64") && sz >= 28)
            ds64DataBytes = rdU64le (bytes.data() + payload + 8);
        else if (tagIs (pos, "fmt ") && ! haveFmt)
        {
            fmtOff = payload; fmtSize = sz; haveFmt = true;
        }
        else if (tagIs (pos, "data") && ! haveData)
        {
            dataOff = payload;
            // A declared size of 0 or -1 means "the writer never
            // finalised this header": the audio runs to the end of the
            // file (or to the ds64 byte count when one is present).
            dataBytes = (sz == 0xFFFFFFFFu)
                            ? (ds64DataBytes > 0 ? ds64DataBytes : (juce::uint64) (bytes.size() - payload))
                        : (sz == 0 ? (juce::uint64) (bytes.size() - payload) : sz);
            haveData = true;
        }
        if (sz == 0xFFFFFFFFu) break; // a -1-sized chunk cannot be walked past
        pos = payload + sz + (sz & 1u);
    }
    if (! haveFmt || ! haveData || fmtSize < 14 || dataOff >= bytes.size()) return false;

    const juce::uint8* fmt = bytes.data() + fmtOff;
    juce::uint16 tag = rdU16le (fmt);
    const juce::uint16 chans = rdU16le (fmt + 2);
    const juce::uint32 rate = rdU32le (fmt + 4);
    const juce::uint16 blockAlign = rdU16le (fmt + 12);
    juce::uint16 bits = fmtSize >= 16 ? rdU16le (fmt + 14) : (juce::uint16) 0;
    if (tag == 0xFFFE && fmtSize >= 40)
        tag = rdU16le (fmt + 24); // the SubFormat GUID opens with the real tag
    if (bits == 0 && chans > 0 && blockAlign >= chans)
        bits = (juce::uint16) (blockAlign / chans * 8); // pre-WAVEFORMATEX fmt chunks carry no bits field
    if (chans < 1 || chans > 64 || rate < 100 || rate > 768000) return false;
    const int bytesPerSample = (bits + 7) / 8;
    if (bytesPerSample < 1 || bytesPerSample > 8) return false;
    const juce::uint64 frameBytes = (juce::uint64) bytesPerSample * chans;
    if (frameBytes == 0) return false;

    const juce::uint64 avail = (juce::uint64) (bytes.size() - dataOff);
    const juce::uint64 framesAvail = (dataBytes < avail ? dataBytes : avail) / frameBytes;
    const juce::uint64 frames = juce::jmin (framesAvail, (juce::uint64) (rate * 20.0)); // same 20 s pad cap
    if (frames == 0) return false;

    monoOut.assign ((size_t) frames, 0.0f);
    const juce::uint8* d = bytes.data() + dataOff;
    for (juce::uint64 i = 0; i < frames; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < (int) chans; ++c)
        {
            const juce::uint8* s = d + i * frameBytes + (juce::uint64) c * (juce::uint64) bytesPerSample;
            double v = 0.0;
            if (tag == 3) // IEEE float
            {
                if (bits == 32)      { float f;  std::memcpy (&f, s, 4); v = f; }
                else if (bits == 64) { double x; std::memcpy (&x, s, 8); v = x; }
                else return false;
            }
            else if (tag == 1) // PCM
            {
                if (bits == 8)       v = ((int) s[0] - 128) / 128.0;
                else if (bits == 16) v = (juce::int16) rdU16le (s) / 32768.0;
                else if (bits == 24)
                {
                    juce::int32 x = (juce::int32) (s[0] | (s[1] << 8) | (s[2] << 16));
                    if (x & 0x800000) x -= 0x1000000;
                    v = x / 8388608.0;
                }
                else if (bits == 32) v = (juce::int32) rdU32le (s) / 2147483648.0;
                else return false;
            }
            else return false; // compressed in a WAV shell: genuinely unsupported
            acc += v;
        }
        monoOut[(size_t) i] = (float) (acc / chans);
    }
    sampleRateOut = (double) rate;
    return true;
}
} // namespace

bool MainComponent::decodeSampleFile (const File& file, std::vector<float>& monoOut, double& sampleRateOut)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));

    // Item 75: a frame can never occupy less than one byte, so a reader
    // claiming more frames than the file has bytes is lying (the
    // 0xFFFFFFFF data-size class), and a reader claiming zero frames
    // for a file full of audio has given up on a header it cannot
    // parse. Both fall through to the hand decoder below.
    if (reader != nullptr && reader->lengthInSamples > 0
        && reader->lengthInSamples <= file.getSize())
    {
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

    if (decodeWavByHand (file, monoOut, sampleRateOut))
        return true;

    monoOut.clear();
    return false;
}

// One loader for every row kind (fix pass 2):
// - kit rows 0-4 (item 40): the file BINDS to the row - the row plays the
//   sample with its steps until Reset brings the built-in drum back.
// - sample rows 5-7: the classic path (waveform panel + the row's own
//   store) - Round 6 item 109: NOT the pad bank any more.
// - rows 8-20: the row's own sample slot in the engine's row store
//   (item 109 - the row is no longer the pad; rows and pads hold
//   separate sounds and load through separate doors).
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
    if (track <= boombap::BoomBapEngine::Clap)
    {
        engine.setKitSample (track, mono, fileSr);
        rowFiles[(size_t) track] = file;
    }
    else if (track < boombap::BoomBapEngine::kFirstOwnRow)
    {
        const int row = track - boombap::BoomBapEngine::Sample1; // rows 5..20
        // Round 6 (item 109): the row's file reference is its OWN -
        // the pad bank is never touched from the row side, in either
        // direction. Pads change only by explicit pad action.
        rowPadFiles[(size_t) row] = file;
        if (row < 3)
        {
            waveData[(size_t) row] = mono;
            waveRates[(size_t) row] = fileSr;
            samplePaths[(size_t) row] = file;
            showWaveform (row);
        }
        engine.loadSample (track, std::move (mono), fileSr);
    }
    else
    {
        engine.loadSample (track, std::move (mono), fileSr);
        rowFiles[(size_t) track] = file;
    }
    // Item 88: the sample's name goes in the row's FIRST COLUMN - the
    // LOAD button never wears it (it used to be renamed to the file's
    // short name right here; that is exactly what the item forbids).
    syncTrackName (track);
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
    std::vector<float> mono;
    double fileSr = 44100.0;
    if (! decodeSampleFile (file, mono, fileSr))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't load sample",
                                          "BoomBap couldn't read " + file.getFileName());
        return;
    }

    beginUndoGesture(); // Pass 2 item 38
    // Round 6 (item 109): a pad load touches the PAD only, all sixteen
    // of them the same way - it never writes a sequencer row, the
    // waveform panel's record, or a track name.
    engine.loadPadSample (pad, std::move (mono), fileSr);
    padFiles[(size_t) pad] = file;
    if (padBank != nullptr) padBank->refreshPad (pad);
    if (mpcPanel != nullptr) mpcPanel->refreshPad (pad);
    fireMediaChanged();
    endUndoGesture();
}

// Round 6 (item 110): right-click a pad -> CLEAR PAD. One undoable
// gesture: the pad's sample and file reference go; its settings
// survive (this clears a SOUND, not a configuration - the same law
// as a row's clear). Only this pad: the sequencer row with the same
// number is a separate store now (item 109) and keeps its sound.
void MainComponent::clearPad (int pad)
{
    if (pad < 0 || pad >= 16) return;
    if (! engine.padHasSample (pad) && padFiles[(size_t) pad] == File()) return;
    beginUndoGesture();
    engine.clearPad (pad);
    padFiles[(size_t) pad] = File();
    if (padBank != nullptr) padBank->refreshPad (pad);
    if (mpcPanel != nullptr) mpcPanel->refreshPad (pad);
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
    const File f = padFiles[(size_t) pad]; // item 109: every pad has its own file
    return f == File() ? String() : f.getFileName();
}

//==============================================================================
// Round 3 (item 88): a track's name is its CONTENT's name. The app's
// file arrays are the source of truth; the engine holds the derived
// name; every surface (sequencer column, multitrack chip, Station
// drop-down and board, piano roll) reads the engine, so the track is
// called the same thing everywhere. An unbound row derives "" and
// falls back to its plain TRACK n (or kit) name in the engine.
void MainComponent::syncTrackName (int track)
{
    using E = boombap::BoomBapEngine;
    if (track < 0 || track >= kTracks) return;
    File f;
    if (track <= E::Clap) f = rowFiles[(size_t) track];
    else if (track < E::kFirstOwnRow)
        f = rowPadFiles[(size_t) (track - E::Sample1)]; // item 109: the ROW's file, never the pad's
    else f = rowFiles[(size_t) track];
    engine.setTrackContentName (track, f == File() ? std::string()
                                                  : f.getFileNameWithoutExtension().toStdString());
    if (sequencerPanel != nullptr) sequencerPanel->refreshRowName (track);
}

void MainComponent::syncAllTrackNames()
{
    for (int t = 0; t < kTracks; ++t) syncTrackName (t);
    if (station != nullptr) station->refreshChannelNames();
}

// Round 3 (item 100): a pad -> pad drag lands here (and the waveform
// panel's drag lands in loadPadFile, its sibling). The target pad
// receives the source's sample AND a snapshot of its settings - trim,
// tune, envelope, all of it - taken before the load. From that moment
// the two pads are fully independent: each holds its own params, so
// the same sample on two pads can be manipulated differently, which
// is the entire point of the gesture. One undoable action.
void MainComponent::copyPadToPad (int fromPad, int toPad)
{
    using E = boombap::BoomBapEngine;
    if (fromPad == toPad || fromPad < 0 || fromPad >= 16 || toPad < 0 || toPad >= 16) return;
    const File file = padFiles[(size_t) fromPad]; // item 109: every pad has its own file
    if (file == File() || ! file.existsAsFile()) return;
    if (! engine.padHasSample (fromPad)) return;

    const auto snapshot = engine.getPadParams (fromPad); // the copy's settings, frozen first

    std::vector<float> mono;
    double fileSr = 44100.0;
    if (! decodeSampleFile (file, mono, fileSr)) return;

    beginUndoGesture();
    // Round 6 (item 109): the copy lands on the pad only - no row,
    // waveform record or track name is written from the pad side.
    engine.loadPadSample (toPad, std::move (mono), fileSr);
    padFiles[(size_t) toPad] = file;
    engine.setPadParams (toPad, snapshot);
    if (padBank != nullptr) padBank->refreshPad (toPad);
    if (mpcPanel != nullptr) mpcPanel->refreshPad (toPad);
    fireMediaChanged();
    endUndoGesture();
}

// Round 3 (item 100): which pad, if any, sits under this screen point -
// asked of both pad banks (the standalone window and the tab, docked
// or popped). As a side effect the bank under the point lights the pad
// the drop would land on; the other bank's light goes out.
int MainComponent::waveDragPadAt (juce::Point<int> screenPos)
{
    PadBankComponent* banks[] = { padBank.get(), mpcPanel.get() };
    int found = -1;
    for (auto* b : banks)
    {
        if (b == nullptr) continue;
        int p = -1;
        if (found < 0 && b->isShowing()) p = b->padAtScreenPosition (screenPos);
        if (p >= 0) found = p;
        b->setDragHighlightPad (p);
    }
    return found;
}

void MainComponent::moveWaveDrag (juce::Point<int> screenPos)
{
    if (! waveDragGhost.isGhostShowing()) return;
    waveDragGhost.moveTo (screenPos);
    waveDragPadAt (screenPos);
}

// Round 6 (item 114): the waveform drag's other landing places - a
// sequencer row (that track takes the file) and the multitrack (the
// file becomes a clip at the drop point). The pad hit test above
// runs first in the drag-end handler; these ask the panels directly,
// in LOCAL coordinates converted from the screen point.
int MainComponent::waveDragRowAt (juce::Point<int> screenPos)
{
    if (sequencerPanel == nullptr || ! sequencerPanel->isShowing()) return -1;
    return sequencerPanel->trackAtScreenPosition (screenPos);
}

bool MainComponent::waveDragMultitrackAt (juce::Point<int> screenPos, int& trackOut, long long& startOut)
{
    if (multitrack == nullptr || ! multitrack->isShowing()) return false;
    return multitrack->dropPoint (screenPos, trackOut, startOut);
}

void MainComponent::dropWaveformFileOnRow (int track)
{
    const File f = samplePaths[(size_t) waveTrack];
    if (f == File() || ! f.existsAsFile()) return;
    loadSampleFile (track, f); // the row's own load path (item 109 keeps it off the pads)
}

void MainComponent::dropWaveformFileOnMultitrack (int track, long long startSamples)
{
    const File f = samplePaths[(size_t) waveTrack];
    if (f == File() || ! f.existsAsFile()) return;
    std::vector<float> mono;
    double fileSr = 44100.0;
    if (! decodeSampleFile (f, mono, fileSr)) return;
    // Clips render at the engine's rate - resample on the way in
    // (linear; the clip is a placement, not a mastering pass).
    const double sr = engine.getSampleRate();
    if (sr > 0.0 && std::abs (fileSr - sr) > 0.5)
    {
        const double ratio = sr / fileSr;
        std::vector<float> rs ((size_t) std::max (1.0, (double) mono.size() * ratio));
        for (size_t i = 0; i < rs.size(); ++i)
        {
            const double pos = (double) i / ratio;
            const size_t i0 = std::min ((size_t) pos, mono.size() - 1);
            const float frac = (float) (pos - (double) i0);
            const float a = mono[i0];
            const float b = mono[std::min (i0 + 1, mono.size() - 1)];
            rs[i] = a + (b - a) * frac;
        }
        mono = std::move (rs);
    }
    beginUndoGesture();
    if (engine.placeDataAsClip (std::move (mono), track, startSamples) >= 0)
    {
        if (multitrack != nullptr) multitrack->refreshFromEngine();
        fireMediaChanged();
    }
    endUndoGesture();
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
        padBank->onPadCopyRequest = [this] (int from, int to) { copyPadToPad (from, to); };
        padBank->onPadClearRequest = [this] (int pad) { clearPad (pad); }; // item 110
        padWindow = std::make_unique<PadWindow> ("BoomBap Pad Bank", boombap::ui::bg());
        padWindow->setLookAndFeel (&lookAndFeel);
        padWindow->setContentNonOwned (padBank.get(), false); // the unique_ptr owns it - the window must not delete it too
        padWindow->centreWithSize (980, 560); // item 111b: the rebalanced editor opens with room to be one
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
        playlistWindow->centreWithSize (980, 480); // item 99c: the arrangement needs room the chain never did
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
