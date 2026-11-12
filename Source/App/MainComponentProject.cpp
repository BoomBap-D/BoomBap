#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "Multitrack.h"
#include "SequencerPanel.h"
#include "StationPanel.h"
#include "DataHome.h"
#include "WavWrite.h"

using namespace juce;

//==============================================================================
boombap::ProjectData MainComponent::makeProjectData() const
{
    boombap::ProjectData d;
    d.settings = engine.getSettings();
    for (size_t i = 0; i < samplePaths.size(); ++i)
        d.samplePaths[i] = samplePaths[i] == File() ? String() : samplePaths[i].getFullPathName();
    // Round 6 (item 109): pads and sequencer rows carry SEPARATE file
    // references now - all sixteen pads, and the rows' own array.
    for (size_t i = 0; i < padFiles.size(); ++i)
        d.padPaths[i] = padFiles[i] == File() ? String() : padFiles[i].getFullPathName();
    for (size_t i = 0; i < rowPadFiles.size(); ++i)
        d.rowPadPaths[i] = rowPadFiles[i] == File() ? String() : rowPadFiles[i].getFullPathName();
    d.hasRowPadPaths = true;
    for (size_t t = 0; t < rowFiles.size(); ++t)
        d.rowSamplePaths[t] = rowFiles[t] == File() ? String() : rowFiles[t].getFullPathName();
    d.vocalTakePath = takePath;
    // MIDI build (item 25): the project's own Learn layer rides the
    // file (empty when the project carries no bindings of its own).
    if (midi != nullptr)
        d.midiLearnJson = midi->projectLayerJson();
    d.exportBars = exportBars.getSelectedId();
    d.exportNormalize = normalizeToggle.getToggleState();
    return d;
}

String MainComponent::currentJson() const
{
    return boombap::projectToJson (makeProjectData(), currentFile);
}

bool MainComponent::isDirty() const
{
    return currentJson() != savedJson;
}

void MainComponent::updateProjectLabel()
{
    const String name = currentFile == File() ? String ("Untitled") : currentFile.getFileNameWithoutExtension();
    const String shown = name + (isDirty() ? " *" : "");
    if (projectName.getText() != shown)
        projectName.setText (shown, dontSendNotification);
    if (auto* top = getTopLevelComponent())
        if (top->getName() != "BoomBap - " + shown)
            top->setName ("BoomBap - " + shown);
}

void MainComponent::applyProjectToUi (const boombap::ProjectData& d)
{
    const auto& s = d.settings;
    bpm.slider.setValue (s.bpm, dontSendNotification);
    swing.slider.setValue (s.swing, dontSendNotification);
    master.slider.setValue (s.masterDb, dontSendNotification);
    dustToggle.setToggleState (s.dustOn, dontSendNotification);
    dustRate.slider.setValue (s.dustRateHz, dontSendNotification);
    glueToggle.setToggleState (s.glueOn, dontSendNotification);
    glueThreshold.slider.setValue (s.glueThresholdDb, dontSendNotification);
    kickTune.slider.setValue (s.kickTuneHz, dontSendNotification);
    kickDecay.slider.setValue (s.kickDecayMs, dontSendNotification);
    snareSnappy.slider.setValue (s.snareSnappy, dontSendNotification);
    openHatDecay.slider.setValue (s.openHatDecayMs, dontSendNotification);
    clapTail.slider.setValue (s.clapTailMs, dontSendNotification);
    micBox.setSelectedId (s.vocalMic + 1, dontSendNotification);
    vocalDistance.slider.setValue (s.vocalDistanceCm, dontSendNotification);
    vocalInput.slider.setValue (s.vocalInputDb, dontSendNotification);
    synthWaveBox.setSelectedId (s.synthWave + 1, dontSendNotification);
    synthCutoff.slider.setValue (s.synthCutoffHz, dontSendNotification);
    synthReso.slider.setValue (s.synthReso, dontSendNotification);
    synthAttack.slider.setValue (s.synthAttackMs, dontSendNotification);
    synthRelease.slider.setValue (s.synthReleaseMs, dontSendNotification);
    synthLevel.slider.setValue (s.synthLevel, dontSendNotification);
    pianoBright.slider.setValue (s.pianoBright, dontSendNotification);
    pianoDecay.slider.setValue (s.pianoDecayMs, dontSendNotification);
    pianoLevel.slider.setValue (s.pianoLevel, dontSendNotification);

    if (sequencerPanel != nullptr) sequencerPanel->refreshMixerFromSettings (s);

    exportBars.setSelectedId (d.exportBars, dontSendNotification);
    normalizeToggle.setToggleState (d.exportNormalize, dontSendNotification);
    if (pianoRoll != nullptr) pianoRoll->refreshFromEngine();
    if (playlist != nullptr) playlist->refreshFromEngine();
    if (multitrack != nullptr) multitrack->refreshFromEngine();
    if (station != nullptr) station->refreshFromEngine(); // Pass 2: strips + board follow the project
    // MIDI build (item 25): the loaded project's Learn layer takes over.
    if (midi != nullptr) midi->setProjectLayerJson (d.midiLearnJson);
    refreshGrid();
}

void MainComponent::newProject()
{
    const boombap::ProjectData fresh; // defaults: the boom-bap starter pattern, no samples
    engine.applySettings (fresh.settings);
    for (int i = 0; i < 3; ++i)
    {
        engine.clearSample (boombap::BoomBapEngine::Sample1 + i);
        samplePaths[(size_t) i] = File();
        waveData[(size_t) i].clear();
        waveRates[(size_t) i] = 0.0;
    }
    for (int i = 3; i < 16; ++i)
    {
        engine.clearPad (i);
        padFiles[(size_t) i] = File();
    }
    // fix pass 2: kit rows back to their built-in drums, added rows emptied
    for (int t = 0; t <= boombap::BoomBapEngine::Clap; ++t)
    {
        engine.clearKitSample (t);
        rowFiles[(size_t) t] = File();
    }
    for (int t = boombap::BoomBapEngine::kFirstOwnRow; t < kTracks; ++t)
    {
        engine.clearSample (t);
        rowFiles[(size_t) t] = File();
    }
    syncAllTrackNames(); // item 88: a fresh project is all TRACK n again
    if (padBank != nullptr) padBank->refreshAll();
    if (mpcPanel != nullptr) mpcPanel->refreshAll();
    engine.clearVocalTake();
    engine.clearAllAudioClips();
    if (multitrack != nullptr) multitrack->refreshFromEngine();
    takePath.clear();
    takeDirty = false;
    refreshTakeUi();
    showWaveform (waveTrack);
    applyProjectToUi (fresh);
    currentFile = File();
    savedJson = currentJson();
    history.clear(); // a new project starts a new undo history (item 38)
    updateProjectLabel();
    clearRecovery(); // the recovered session, if any, has been replaced
    fireMediaChanged();
}

void MainComponent::showRecentMenu()
{
    recentProjects.pruneMissing();

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const int num = recentProjects.getNumFiles();
    if (num == 0)
    {
        menu.addItem (1, "No recent projects", false);
    }
    else
    {
        for (int i = 0; i < num; ++i)
            menu.addItem (100 + i, recentProjects.getFile (i).getFullPathName());
        menu.addSeparator();
        menu.addItem (999, "Clear recent projects");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recentButton),
        [this] (int result)
        {
            if (result == 999)
                recentProjects.clear();
            else if (result >= 100)
            {
                const auto file = recentProjects.getFile (result - 100);
                confirmDiscardThen ([this, file] { openProject (file); });
            }
        });
}

void MainComponent::openProjectChooser()
{
    chooser = std::make_unique<FileChooser> ("Open project", File::getSpecialLocation (File::userDocumentsDirectory), "*.boombap");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this] (const FileChooser& fc)
                          {
                              const File f = fc.getResult();
                              if (f != File())
                                  openProject (f);
                          });
}

void MainComponent::openProject (const File& file)
{
    boombap::ProjectData d;
    String error;
    if (! boombap::loadProjectFile (file, d, error))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't open project", error);
        return;
    }

    loadProjectData (d, true, false);
    currentFile = file;
    savedJson = currentJson();
    history.clear(); // an opened project starts a new undo history (item 38)
    updateProjectLabel();
    clearRecovery(); // a saved project on disk now covers the work
    recentProjects.add (file);
}

void MainComponent::loadProjectData (const boombap::ProjectData& d, bool reportMissing, bool preserveEngineTake)
{
    loadingProject = true;
    engine.applySettings (d.settings);

    StringArray missing;
    // Round 6 (item 109): rows and pads load as the separate stores
    // they now are. Each side's file comes from its own array; a
    // LEGACY file (no rowPads key) shared one store, so its rows
    // fall back to the pad-side paths - the same files those
    // projects always sounded.
    auto rowPathFor = [&d] (int i) -> String
    {
        if (d.hasRowPadPaths) return d.rowPadPaths[(size_t) i];
        return i < 3 ? d.samplePaths[(size_t) i] : d.padPaths[(size_t) i];
    };
    auto padPathFor = [&d] (int i) -> String
    {
        if (d.padPaths[(size_t) i].isNotEmpty()) return d.padPaths[(size_t) i];
        // The samplePaths fallback is for LEGACY files only (no
        // rowPads key): there, pads 1-3 lived in samplePaths. In a
        // split-store project an empty pad path MEANS an empty pad -
        // applying the fallback there resurrects the item 109 mirror
        // on every project round-trip (caught by the round6 suite).
        return (! d.hasRowPadPaths && i < 3) ? d.samplePaths[(size_t) i] : String();
    };

    for (int i = 0; i < 16; ++i)
    {
        const int track = boombap::BoomBapEngine::Sample1 + i;
        const String path = rowPathFor (i);
        engine.clearSample (track);
        rowPadFiles[(size_t) i] = File();
        if (i < 3) samplePaths[(size_t) i] = File();
        if (path.isEmpty())
            continue;

        const File f (path);
        if (f.existsAsFile())
        {
            loadSampleFile (track, f);
        }
        else
        {
            rowPadFiles[(size_t) i] = f;                // keep the reference so a save doesn't lose it
            if (i < 3) samplePaths[(size_t) i] = f;
            missing.add (f.getFileName());
        }
    }

    for (int i = 0; i < 16; ++i)
    {
        const String path = padPathFor (i);
        engine.clearPad (i);
        padFiles[(size_t) i] = File();
        if (path.isEmpty())
            continue;

        const File f (path);
        if (f.existsAsFile())
            loadPadFile (i, f);
        else
        {
            padFiles[(size_t) i] = f;                // keep the reference, like the sample rows
            missing.add (f.getFileName());
        }
    }
    if (padBank != nullptr) padBank->refreshAll();
    if (mpcPanel != nullptr) mpcPanel->refreshAll();

    // fix pass 2: row-bound samples - kit rows (item 40) and added rows
    for (int t = 0; t < kTracks; ++t)
    {
        if (! (t <= boombap::BoomBapEngine::Clap || t >= boombap::BoomBapEngine::kFirstOwnRow))
            continue;
        if (t <= boombap::BoomBapEngine::Clap) engine.clearKitSample (t);
        else engine.clearSample (t);
        rowFiles[(size_t) t] = File();
        const String path = d.rowSamplePaths[(size_t) t];
        if (path.isEmpty())
            continue;
        const File f (path);
        if (f.existsAsFile())
            loadSampleFile (t, f);
        else
        {
            rowFiles[(size_t) t] = f; // keep the reference, like the sample rows
            missing.add (f.getFileName());
        }
    }

    // Item 88: whatever the loads above bound (or kept as a missing
    // reference), the rows are named after it - including rows whose
    // file is missing: the track is still THAT sample's track, and the
    // name says so while the alert below says the file is gone.
    syncAllTrackNames();

    if (preserveEngineTake && d.vocalTakePath.isEmpty())
    {
        // plugin state sync: the take lives in the engine and has no file yet - keep it
        takeDirty = false;
        refreshTakeUi();
    }
    else
    {
    engine.clearVocalTake();
    takePath.clear();
    if (d.vocalTakePath.isNotEmpty())
    {
        const File tf (d.vocalTakePath);
        if (tf.existsAsFile())
        {
            std::vector<float> mono;
            double takeSr = 44100.0;
            if (decodeSampleFile (tf, mono, takeSr))
            {
                engine.setVocalTake (std::move (mono));
                takePath = tf.getFullPathName();
            }
        }
        else
        {
            missing.add (tf.getFileName());
        }
    }
    takeDirty = false;
    refreshTakeUi();
    }

    engine.restoreClipBuffersFromTake(); // clip regions came from the file; their audio is the take

    // Item 61: NO INVISIBLE AUDIO. A take that arrives with a project or a
    // host state restore and has no clip home yet gets one here, exactly
    // like a fresh recording - placement also stands the raw take loop
    // down, so it can never sound unexplained from behind the interface.
    if (engine.hasTake() && ! engine.takeIsPlaced() && ! suppressAutoHome)
        placeTakeAsClip();

    applyProjectToUi (d);
    loadingProject = false;

    if (reportMissing && missing.size() > 0)
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Some samples are missing",
                                          "BoomBap couldn't find: " + missing.joinIntoString (", ")
                                              + ".\n\nUse Load... on those rows to pick them again.");
    fireMediaChanged();
}

bool MainComponent::writeProject (const File& file)
{
    if (engine.hasTake())
    {
        // Item 62: the take's WAV lives in the BoomBap home (Takes) and the
        // project references it - no more sidecar file beside the project.
        ensureTakeFileInHome();
        if (takePath.isEmpty() || ! File (takePath).existsAsFile())
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save vocal take",
                                              "BoomBap couldn't write the take into " + boombap::takesDir().getFullPathName());
            return false;
        }
    }
    else
    {
        takePath.clear();
    }

    String error;
    if (! boombap::saveProjectFile (file, makeProjectData(), error))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save project", error);
        return false;
    }
    currentFile = file;
    savedJson = currentJson();
    updateProjectLabel();
    clearRecovery(); // the manual save supersedes the autosave
    recentProjects.add (file);
    return true;
}

void MainComponent::saveProject (std::function<void()> then)
{
    if (currentFile == File())
    {
        saveProjectAs (std::move (then));
        return;
    }
    if (writeProject (currentFile) && then)
        then();
}

void MainComponent::saveProjectAs (std::function<void()> then)
{
    const File suggested = currentFile != File()
                               ? currentFile
                               : File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Untitled.boombap");
    chooser = std::make_unique<FileChooser> ("Save project", suggested, "*.boombap");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this, then] (const FileChooser& fc)
                          {
                              const File f = fc.getResult();
                              if (f != File() && writeProject (f.withFileExtension ("boombap")) && then)
                                  then();
                          });
}

void MainComponent::confirmDiscardThen (std::function<void()> proceed)
{
    if (! isDirty())
    {
        proceed();
        return;
    }

    const String name = currentFile == File() ? String ("Untitled") : currentFile.getFileName();
    Component::SafePointer<MainComponent> safe (this);
    AlertWindow::showAsync (MessageBoxOptions::makeOptionsYesNoCancel (MessageBoxIconType::QuestionIcon,
                                                                       "Save changes?",
                                                                       "Do you want to save your changes to " + name + "?",
                                                                       "Save", "Don't save", "Cancel", this),
                            [safe, proceed] (int result)
                            {
                                if (safe == nullptr) return;
                                if (result == 1)      safe->saveProject (proceed);   // Save, then carry on
                                else if (result == 2) proceed();                     // Don't save
                            });                                                      // Cancel: do nothing
}

//==============================================================================
void MainComponent::startExport()
{
    if (exporting.load())
        return;

    const File suggested = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("BoomBap beat.wav");
    chooser = std::make_unique<FileChooser> ("Export WAV", suggested, "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
                          {
                              const File f = fc.getResult();
                              if (f != File())
                                  exportTo (f.withFileExtension ("wav"));
                          });
}

void MainComponent::exportTo (const File& file)
{
    // In song mode the export is one full pass through the playlist.
    // Fix pass item 34: choosing "Whole song" does the same pass even when
    // song mode is off (and renders one bar of pattern if the playlist is
    // still empty).
    // Round 4 (item 99c): the song's length is the arrangement's bar
    // count now (a converted legacy chain reports the same number).
    const bool wantWholeSong = exportBars.getSelectedId() == 100 && engine.songTotalBars() > 0;
    const bool songExport = engine.isSongMode() && engine.songTotalBars() > 0;
    const int bars = (songExport || wantWholeSong) ? engine.songTotalBars() : exportBars.getSelectedId();
    const bool normalize = normalizeToggle.getToggleState();
    const double sampleRate = engine.getSampleRate();

    // clone the current pattern, mixer, sound settings and samples; the live engine is untouched
    auto offline = std::make_shared<boombap::BoomBapEngine>();
    offline->copySettingsFrom (engine);
    if (wantWholeSong) offline->setSongMode (true);

    exporting = true;
    exportButton.setEnabled (false);
    exportStatus.setText ("Exporting...", dontSendNotification);

    Component::SafePointer<MainComponent> safe (this);
    Thread::launch ([safe, offline, file, bars, normalize, sampleRate]
    {
        const auto channels = boombap::renderOffline (*offline, bars, sampleRate, 2.0f, normalize);
        const bool ok = writeWavFile (file, channels, sampleRate);

        MessageManager::callAsync ([safe, ok, file]
        {
            if (safe == nullptr)
                return;
            safe->exporting = false;
            safe->exportButton.setEnabled (true);
            safe->exportStatus.setText (ok ? "Saved " + file.getFileName() : "Export failed",
                                        dontSendNotification);
        });
    });
}
