#include "MainComponent.h"
#include "PadBank.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "WavWrite.h"

using namespace juce;

//==============================================================================
boombap::ProjectData MainComponent::makeProjectData() const
{
    boombap::ProjectData d;
    d.settings = engine.getSettings();
    for (size_t i = 0; i < samplePaths.size(); ++i)
        d.samplePaths[i] = samplePaths[i] == File() ? String() : samplePaths[i].getFullPathName();
    for (size_t i = 3; i < padFiles.size(); ++i)
        d.padPaths[i] = padFiles[i] == File() ? String() : padFiles[i].getFullPathName();
    d.vocalTakePath = takePath;
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

    for (int t = 0; t < kTracks; ++t)
    {
        rows[t].mute.setToggleState (s.trackMute[t], dontSendNotification);
        rows[t].solo.setToggleState (s.trackSolo[t], dontSendNotification);
        rows[t].level.setValue (s.trackGain[t], dontSendNotification);
    }

    exportBars.setSelectedId (d.exportBars, dontSendNotification);
    normalizeToggle.setToggleState (d.exportNormalize, dontSendNotification);
    if (pianoRoll != nullptr) pianoRoll->refreshFromEngine();
    if (playlist != nullptr) playlist->refreshFromEngine();
    refreshPatternUi();
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
        rows[boombap::BoomBapEngine::Sample1 + i].load.setButtonText ("Load...");
    }
    for (int i = 3; i < 16; ++i)
    {
        engine.clearPad (i);
        padFiles[(size_t) i] = File();
    }
    if (padBank != nullptr) padBank->refreshAll();
    engine.clearVocalTake();
    takePath.clear();
    takeDirty = false;
    refreshTakeUi();
    showWaveform (waveTrack);
    applyProjectToUi (fresh);
    currentFile = File();
    savedJson = currentJson();
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
    updateProjectLabel();
    clearRecovery(); // a saved project on disk now covers the work
    recentProjects.add (file);
}

void MainComponent::loadProjectData (const boombap::ProjectData& d, bool reportMissing, bool preserveEngineTake)
{
    loadingProject = true;
    engine.applySettings (d.settings);

    StringArray missing;
    for (int i = 0; i < 3; ++i)
    {
        const int track = boombap::BoomBapEngine::Sample1 + i;
        const String path = d.samplePaths[(size_t) i];
        engine.clearSample (track);
        samplePaths[(size_t) i] = File();
        rows[track].load.setButtonText ("Load...");

        if (path.isEmpty())
            continue;

        const File f (path);
        if (f.existsAsFile())
        {
            loadSampleFile (track, f);
        }
        else
        {
            samplePaths[(size_t) i] = f;                // keep the reference so a save doesn't lose it
            rows[track].load.setButtonText ("Missing");
            missing.add (f.getFileName());
        }
    }

    for (int i = 3; i < 16; ++i)
    {
        const String path = d.padPaths[(size_t) i];
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
        const File takeFile = file.getParentDirectory()
                                  .getChildFile (file.getFileNameWithoutExtension() + " - vocal.wav");
        if (takeDirty || ! takeFile.existsAsFile())
        {
            if (! writeTakeWav (takeFile))
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save vocal take",
                                                  "BoomBap couldn't write " + takeFile.getFileName());
                return false;
            }
            takeDirty = false;
        }
        takePath = takeFile.getFullPathName();
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
    const bool songExport = engine.isSongMode() && engine.getSongLength() > 0;
    const int bars = songExport ? engine.getSongLength() : exportBars.getSelectedId();
    const bool normalize = normalizeToggle.getToggleState();
    const double sampleRate = engine.getSampleRate();

    // clone the current pattern, mixer, sound settings and samples; the live engine is untouched
    auto offline = std::make_shared<boombap::BoomBapEngine>();
    offline->copySettingsFrom (engine);

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
