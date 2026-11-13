#pragma once
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cmath>
#include "Engine/BoomBapEngine.h"
#include "Engine/OfflineRender.h"
#include "ProjectFile.h"
#include "BoomBapLookAndFeel.h"
#include "RecentProjects.h"

#include "UiComponents.h"
#include "UndoHistory.h"
#include "StationPanel.h"
#include "KeyboardPanel.h"
#include "Midi/MidiService.h"
#include "Midi/MidiPanel.h"

class PadBankComponent;
class PadWindow;
class PianoRollComponent;
class PianoRollWindow;
class PlaylistComponent;
class PlaylistWindow;
class MultitrackComponent;
class SequencerPanel;

class MainComponent : public juce::AudioAppComponent, private juce::Timer
{
public:
    MainComponent (boombap::BoomBapEngine& eng, bool useOwnAudioDevice);
    ~MainComponent() override;

    // Set by the plugin editor: called with a fresh ProjectData whenever the
    // project's media (sample / pad / take files) changes, so the host-facing
    // processor can include the paths in its saved state. Never set standalone.
    std::function<void (const boombap::ProjectData&)> onMediaChanged;

    // Applies a whole project (settings + media files) to the engine and the UI.
    // Used when opening a file, and by the plugin editor to sync with host state.
    void loadProjectData (const boombap::ProjectData& d, bool reportMissing, bool preserveEngineTake);

    // Pass 1 item 7: called by the standalone app once its window is up. If a
    // crash-recovery file survived the last session, offers to restore it.
    void checkForRecovery();

    // audio
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override {}

    // ui
    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void parentHierarchyChanged() override { grabKeyboardFocus(); }

    // Runs `proceed` straight away, or after asking what to do with unsaved changes.
    void confirmDiscardThen (std::function<void()> proceed);

    // Item 59: exactly one production view is visible at any time; the
    // plugin's first-open test asserts it. 0 = sequencer, 1 = MPC, 2 = audio.
    bool tabViewVisible (int index) { auto* c = tabContent (index); return c != nullptr && c->isVisible(); }
    int activeTabIndex() const { return activeTab; }
    void showInitialTab() { selectTab (0); }

    // Item 66: the dock-back regression test drives the tab system through
    // the very same code paths as the tab bar, the Pop out / Dock back
    // button, and the pop-out window's X button - no test-only back doors.
    void driveSelectTab (int index) { selectTab (index); }
    void drivePopOutTab (int index) { popOutTab (index); }
    void driveDockTab (int index) { dockTab (index); }
    void driveClosePopOutWindow (int index)
    {
        if (productionWindows[index] != nullptr)
            productionWindows[index]->closeButtonPressed();
    }
    bool tabIsPoppedOut (int index) const { return tabPoppedOut[index]; }
    juce::Component* tabContentComponent (int index) { return tabContent (index); }

    // Fix Pass 4 (items 77/78): the window-frame test reads the REAL
    // windows the app opens - the same objects the buttons and menus
    // open, reached through the same open paths. No test-only windows.
    juce::DocumentWindow* popOutWindowComponent (int index) { return productionWindows[index].get(); }
    juce::DocumentWindow* stationWindowComponent()          { return stationWindow.get(); }
    juce::DocumentWindow* padWindowComponent();       // defined in MainComponent.cpp - the
    juce::DocumentWindow* pianoWindowComponent();     // window classes are only forward-
    juce::DocumentWindow* playlistWindowComponent();  // declared in this header
    juce::DocumentWindow* audioSettingsWindowComponent()    { return audioWindow.get(); }
    void driveOpenPadBank()       { openPadBank(); }
    void driveOpenPianoRoll()     { openPianoRoll(); }
    void driveOpenPlaylist()      { openPlaylist(); }
    void driveShowAudioSettings() { showAudioSettings(); }

    // MIDI build (item 25): the MIDI window + service, reached through
    // the same doors the "MIDI..." button uses. The plugin editor
    // hands the component the PROCESSOR's service (the host owns the
    // devices; the component owns only the targets + window).
    void showMidiSettings();
    void setExternalMidiService (boombap::MidiService* svc);
    juce::DocumentWindow* midiWindowComponent() { return midiWindow.get(); }
    void driveShowMidiSettings() { showMidiSettings(); }
    boombap::MidiService* midiService() { return midi; }
    boombap::MidiPanel* midiPanelComponent() { return midiPanel; }
    boombap::MidiTargetRegistry& midiTargetRegistry() { return midiTargets; }

    // Round 1: the selection / delete / waveform tests reach the real
    // sequencer panel and the real minimise-coupling step - the same
    // objects and functions the UI drives, no test-only back doors.
    SequencerPanel* sequencerPanelComponent() { return sequencerPanel.get(); }
    // Round 5: the keyboard tab's panel, for the suite + the shots.
    KeyboardPanel* keyboardPanelComponent() { return keyboardPanel.get(); }
    void driveMinimiseCoupling (bool minimised) { updateMinimiseCoupling (minimised); }
    int auxWindowsHiddenByMinimise() const { return (int) hiddenByMinimise.size(); }
    void driveLoadWaveformSample (int slot, const juce::File& f)
    {
        loadSampleFile (boombap::BoomBapEngine::Sample1 + slot, f);
    }

    // Round 3 (item 100): the pad -> pad copy, driven through the same
    // function a drag landing calls.
    void driveCopyPadToPad (int fromPad, int toPad) { copyPadToPad (fromPad, toPad); }

    // Round 6 doors - the same objects and functions the UI drives:
    // the row store's truth (item 109), CLEAR PAD (item 110), the
    // waveform view + multitrack for the drag targets (item 114), the
    // transport keys (item 115) and the UNDO/REDO pair (item 118).
    bool rowHasSampleForTest (int row) const { return engine.rowHasSample (row); }
    bool padHasSampleForTest (int pad) const { return engine.padHasSample (pad); }
    void driveClearPad (int pad) { clearPad (pad); }
    WaveformView* waveViewComponent() { return &waveView; }
    MultitrackComponent* multitrackComponent() { return multitrack.get(); }
    PadBankComponent* mpcPanelComponent() { return mpcPanel.get(); }
    TransportButton* transportButtonForTest (int i)
    {
        TransportButton* all[] = { &toStartButton, &rewButton, &pauseButton, &playButton,
                                   &stopButton, &ffButton, &toEndButton };
        return (i >= 0 && i < 7) ? all[i] : nullptr;
    }
    juce::TextButton& undoButtonForTest() { return undoButton; }
    juce::TextButton& redoButtonForTest() { return redoButton; }

    // Fix Pass 3 (item 75): the pad-load regression test drives the exact
    // decode + pad-file load paths the pad bank's Load button uses -
    // same private functions, no test-only back doors.
    void driveDeleteSequencerRow (int t) { deleteSequencerRow (t); } // Fix Pass 3 tests: the real menu action
    bool driveDecodeSampleFile (const juce::File& f, std::vector<float>& mono, double& sr)
    {
        return decodeSampleFile (f, mono, sr);
    }
    void driveLoadPadFile (int pad, const juce::File& f) { loadPadFile (pad, f); }

    // ---- Pass 2: the Station + global undo (items 19/38) ----
    void openStation();
    void selectChannel (int ch);
    void auditionTrack (int track); // item 124: one play of a track's sound (name-tap gesture only)
    int selectedChannelId() const { return selectedChannel; }
    void beginUndoGesture();
    void endUndoGesture();
    void runUndoable (std::function<void()> fn);
    void undoProject();
    void redoProject();
    bool canUndoProject() const { return history.canUndo(); }
    bool canRedoProject() const { return history.canRedo(); }
    int undoDepth() const { return history.totalItems(); }
    void applyHistoryJson (const juce::String& json);
    void rewindTransport();
    void fastForwardTransport();
    boombap::StationPanel* stationPanel() { return station.get(); }

    // Round 2 (item 93): the full transport navigation + the shared
    // clock format (the Station clock and the project-bar readout are
    // two faces of this one choice, persisted in the app settings).
    void navToStart();
    void navToEnd();
    void navRewind();
    void navForward();
    int clockFormatValue() const { return clockFormat; }
    void setClockFormat (int fmt);
    // Round 4 (item 99b): test seams for the mode buttons.
    juce::TextButton& patternModeButtonForTest() { return patternModeButton; }
    juce::TextButton& songModeButtonForTest()    { return songModeButton; }
    // Round 2 (item 96d): the Station's EDIT SAMPLE doorway.
    void editChannel (int ch);
    bool channelHasEditor (int ch) const;

private:
    static constexpr int kTracks = boombap::BoomBapEngine::kTracks;
    static constexpr int kSteps  = boombap::BoomBapEngine::kSteps;

    struct LabeledSlider
    {
        juce::Slider slider;
        juce::Label label;
    };

    void timerCallback() override;
    // Round 1 (item 102): minimise coupling - when the main window is
    // minimised, every open auxiliary window goes down with it, and
    // restoring the main window brings back exactly the ones the
    // coupling took down (never ones the owner closed himself).
    // pollMinimiseCoupling reads the OS state (the peer) each tick;
    // updateMinimiseCoupling is the step itself, driven directly by
    // the regression test.
    void pollMinimiseCoupling();
    void updateMinimiseCoupling (bool minimised);
    void togglePlay();
    void startTransport();
    void pauseTransport(); // Round 6 (item 115): PAUSE - freeze, don't park
    void stopTransport();
    void seekBySteps (int steps); // Round 2 (item 93): REW/FF land here
    void startAudioOnlyPlayback();              // item 47: the audio view's own PLAY
    // Production Area tab bar (items 37/42): 0 steps, 1 MPC, 2 audio
    void selectTab (int index);
    void togglePopOut();
    void popOutTab (int index);
    void dockTab (int index);
    void updateTabChrome();
    void showKitMenu (int track);               // item 40
    void showRowMenu (int track);               // Fix Pass 3 item 69: user-row name menu
    void showPadRowMenu (int track);            // Round 5 item 107: pad rows get a (rename) menu too
    void deleteSequencerRow (int track);        // Fix Pass 3 item 69: one undoable row deletion
    // Round 5: the keyboard tab's note door + step-write bookkeeping,
    // and the rename commit (items 105/107).
    void keyboardNoteOn (int channel, int note, float velocity);
    void updateStepWriteGesture();
    void commitTrackRename (int track, const juce::String& text);
    juce::Component* tabContent (int index);
    // audio device setup (Pass 1 item 8) + autosave / recovery (item 7)
    void showAudioSettings();
    // MIDI build (item 25): build the Learn target table, keep the
    // learn overlay's spots honest, and persist map changes.
    void buildMidiTargets();
    void refreshLearnOverlay();
    // The learnable spots (component -> target), shared by the LEARN
    // overlay and the right-click MIDI Learn menu (item 147 fix).
    std::vector<boombap::LearnGlowOverlay::Spot> buildLearnSpotMap();
    void handleLearnContextClick (const juce::MouseEvent& e);
    void onMidiMapChanged();
    void saveDeviceState();
    std::unique_ptr<juce::XmlElement> loadDeviceStateXml();
    void writeRecovery();
    void clearRecovery();
    void refreshGrid();
    void pushAllParams();
    void openSampleChooser (int track);
    void loadSampleFile (int track, const juce::File& file);
    void showWaveform (int index);
    void openPadChooser (int pad);
    void loadPadFile (int pad, const juce::File& file);
    void clearPad (int pad); // Round 6 (item 110): the pad menu's CLEAR PAD
    // Round 3 (item 88): track names derive from the file bound to the
    // row - these keep the engine's content names (and the sequencer's
    // first column) in step with the file arrays.
    void syncTrackName (int track);
    void syncAllTrackNames();
    // Round 3 (item 100): the pad -> pad copy (sample + settings snapshot).
    void copyPadToPad (int fromPad, int toPad);
    // Round 3 (item 100): the waveform panel's drag onto a pad.
    void moveWaveDrag (juce::Point<int> screenPos);
    int waveDragPadAt (juce::Point<int> screenPos);
    // Round 6 (item 114): the same drag's other two landings - a
    // sequencer row (loads that track) and the multitrack (a clip).
    int waveDragRowAt (juce::Point<int> screenPos);
    bool waveDragMultitrackAt (juce::Point<int> screenPos, int& trackOut, long long& startOut);
    void dropWaveformFileOnRow (int track);
    void dropWaveformFileOnMultitrack (int track, long long startSamples);
    bool decodeSampleFile (const juce::File& file, std::vector<float>& monoOut, double& sampleRateOut);
    void openPadBank();
    juce::String padDisplayName (int pad) const;
    void toggleRecording();
    void refreshTakeUi();
    bool writeTakeWav (const juce::File& file);
    void ensureTakeFileInHome();         // Item 62: the take lives as a WAV in Documents/BoomBap/Takes
    void openPianoRoll();
    void openPianoRollFor (int track);   // Fix pass item 28: the roll bound to a track
    void selectInstrument (int which);   // Fix pass item 29: 0 = synth, 1 = piano
    void placeTakeAsClip();              // Fix pass item 32: take -> clip on the audio track
    void openPlaylist();
    // project files
    static constexpr int kProjectBar = 40; // height of the project bar; everything else sits below it
    boombap::ProjectData makeProjectData() const;
    juce::String currentJson() const;
    bool isDirty() const;
    void updateProjectLabel();
    void applyProjectToUi (const boombap::ProjectData& data);
    void newProject();
    void openProjectChooser();
    void showRecentMenu();
    void openProject (const juce::File& file);
    void saveProject (std::function<void()> then);
    void saveProjectAs (std::function<void()> then);
    bool writeProject (const juce::File& file);

    void startExport();
    void exportTo (const juce::File& file);
    void refreshViewsAfterEdit(); // Pass 2: after an undoable edit, the other views re-read the engine

    void setupSlider (LabeledSlider& s, const juce::String& name, double min, double max, double init,
                      double step, const juce::String& suffix, std::function<void()> onChange);

    boombap::BoomBapEngine& engine;
    bool ownAudioDevice;
    bool loadingProject = false;
    void fireMediaChanged();
    BoomBapLookAndFeel lookAndFeel;
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<float> inputScratch, rightScratch;
    std::atomic<bool> haveInput { false };
    int lastPlayStep = -2;
    float meterShown[kTracks] = {};
    float grShown = 0.0f;

    // project
    juce::TextButton newButton { "New" }, openButton { "Open..." }, saveButton { "Save" }, saveAsButton { "Save as..." }, recentButton { "Recent" };
    juce::Label projectName;
    juce::File currentFile;
    juce::String savedJson;
    RecentProjects recentProjects;
    std::array<juce::File, 3> samplePaths;
    int dirtyTick = 0;

    // transport and master
    // Round 2 (item 93): one TransportNav reads the double-presses for
    // both this row and the Station's transport.
    // Round 6 (item 115): the row itself is family A "Studio Console"
    // keys now (chunky charcoal, amber glyphs, amber ring on PLAY) in
    // D's order - TO START / REW / PAUSE / PLAY / STOP / FF / TO END -
    // PLAY dead centre of the seven, the framed clock in the middle
    // of the row. PAUSE is new: it freezes the playhead where it
    // stands, and PLAY from pause resumes there.
    TransportButton toStartButton, rewButton, pauseButton, playButton,
                    stopButton, ffButton, toEndButton;
    juce::TextButton clearButton { "Clear" }, resetButton { "Reset" };
    // Round 6 (item 118): the main window's own UNDO / REDO buttons -
    // the snapshot history has run since Pass 2 (the Station has had
    // its pair all along); this is its face on the main screen.
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    boombap::TransportNav transportNav;
    TimeClock mainClock;
    int clockFormat = 0;
    // Round 4 (item 99b): PATTERN / SONG mode buttons on the main page -
    // PATTERN loops the edited pattern, SONG walks the playlist's
    // arrangement. The mode persists in the project (the engine holds
    // it as project state now); the timer keeps the buttons honest
    // when undo or a project load flips it behind their backs.
    juce::TextButton patternModeButton { "PATTERN" }, songModeButton { "SONG" };
    bool lastSongModeShown = false;
    void refreshModeButtons();
    juce::TextButton audioButton { "Audio..." };
    juce::TextButton midiButton { "MIDI..." };   // MIDI build (item 25)
    juce::TextButton metroButton { "METRO" };
    LabeledSlider metroVol;
    PadActivityLight padLight;
    LevelMeter padMeter;
    float padShown = 0.0f;
    bool lastPlayingState = false;
    int autosaveTicks = 0;
    std::unique_ptr<juce::DocumentWindow> audioWindow;
    // MIDI build (item 25): the service (owned standalone, borrowed
    // from the processor in the plugin), the Learn target table, the
    // MIDI window, and the LEARN overlay that outlines every bindable
    // control in the main window while Learn is armed.
    std::unique_ptr<boombap::MidiService> ownedMidi;
    boombap::MidiService* midi = nullptr;
    boombap::MidiTargetRegistry midiTargets;
    std::unique_ptr<juce::DocumentWindow> midiWindow;
    boombap::MidiPanel* midiPanel = nullptr;   // owned by midiWindow
    std::unique_ptr<boombap::LearnGlowOverlay> learnOverlay;
    bool learnOverlayOn = false;
    int lastMidiMelodic = boombap::BoomBapEngine::kSynthChannel;
    // Item 147 fix: right-clicks anywhere in the window are offered
    // to handleLearnContextClick (it only acts on learnable spots;
    // pads and keys run their own menus and are skipped there).
    struct LearnContextListener : public juce::MouseListener
    {
        MainComponent* owner = nullptr;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (owner != nullptr && e.mods.isPopupMenu())
                owner->handleLearnContextClick (e);
        }
    };
    LearnContextListener learnContextListener;
    bool visualNoteShown[128] = {};   // board lights already mirrored
    LabeledSlider bpm, swing, master, dustRate, glueThreshold;
    juce::ToggleButton dustToggle { "Dust (S950)" }, glueToggle { "Glue" };
    GrMeter glueBar;
    juce::Label glueMeter;

    // grid: the step sequencer view lives in the Production Area tabs now
    std::unique_ptr<SequencerPanel> sequencerPanel;

    // drum sound
    LabeledSlider kickTune, kickDecay, snareSnappy, openHatDecay, clapTail;

    // instruments (played from the piano roll)
    // Fix pass item 29: one instrument at a time - the switcher shows the
    // chosen instrument's controls (and its roll opens from here).
    juce::TextButton synthTab { "SYNTH" }, pianoTab { "PIANO" };
    juce::TextButton instrumentRollButton { "Piano Roll" };
    int shownInstrument = 0;
    juce::ComboBox synthWaveBox;
    LabeledSlider synthCutoff, synthReso, synthAttack, synthRelease, synthLevel;
    LabeledSlider pianoBright, pianoDecay, pianoLevel;

    // vocal
    juce::ToggleButton vocalToggle { "Vocal monitor" };
    juce::ComboBox micBox;
    LabeledSlider vocalDistance, vocalInput;
    juce::Label vocalHint;

    // export
    juce::ComboBox exportBars;
    juce::ToggleButton normalizeToggle { "Normalize" };
    juce::TextButton exportButton { "Export WAV..." };
    juce::Label exportStatus;

    WaveformView waveView;
    SampleDragGhost waveDragGhost; // item 100: follows a waveform -> pad drag
    juce::TextButton waveSel[3];
    juce::Label waveName;
    // Item 46's audition button, re-roled by item 89 (Round 1): the
    // audition itself moved onto the waveform (click the wave = hear
    // it); this button is the panel's LOAD - it opens the chooser.
    juce::TextButton wavePlayButton { "LOAD" };
    std::array<std::vector<float>, 3> waveData;
    std::array<double, 3> waveRates {};         // source rate per waveform slot
    int waveTrack = 0;

    juce::TextButton recordButton { "Record" };
    juce::ToggleButton takeToggle { "Loop take" }; // item 56: the raw take's loop is an explicit opt-in
    juce::Label takeLabel;
    juce::String takePath;
    bool takeDirty = false;
    bool uiRecording = false;

    juce::TextButton padBankButton { "Pad Bank" };
    std::array<juce::File, 16> padFiles;
    // Round 6 (item 109): the sequencer sample ROWS' own file
    // references - rows and pads are separate stores now, each with
    // its own array (index 0 = row Sample1).
    std::array<juce::File, 16> rowPadFiles;
    std::unique_ptr<PadBankComponent> padBank;
    std::unique_ptr<PadWindow> padWindow;

    juce::TextButton pianoButton { "Piano Roll" };
    std::unique_ptr<PianoRollComponent> pianoRoll;
    std::unique_ptr<PianoRollWindow> pianoWindow;
    int rollTrack = 0; // the track the roll was last summoned from (item 28)

    // Fix pass 2, items 37/42: the Production Area is a TAB BAR - STEP
    // SEQUENCER / MPC PANEL / AUDIO MULTITRACK - and every tab can pop
    // out into its own window and dock back (his Reaper grammar).
    // Round 5 (item 105): KEYBOARD joins as the fourth tab, same
    // grammar - docked in the bottom section, pop-out capable.
    juce::TextButton tabButtons[4];
    juce::TextButton popOutButton { "Pop out" };
    juce::Label tabPoppedLabel;
    int activeTab = 0;
    bool tabPoppedOut[4] = {};
    std::unique_ptr<juce::DocumentWindow> productionWindows[4];
    std::unique_ptr<PadBankComponent> mpcPanel; // the MPC PANEL tab's pad bank
    std::unique_ptr<MultitrackComponent> multitrack;
    std::unique_ptr<KeyboardPanel> keyboardPanel; // Round 5 (item 105)
    // Round 5 (item 105d): step-write undo bookkeeping - a gesture
    // opens on a pass's first written note and closes when the pass
    // ends (playhead wrap, transport stop, WRITE off, track change).
    bool stepGestureOpen = false;
    int lastStepWriteStep = -1;

    // item 40: audio files bound to rows outside the sample slots and
    // pad bank (kit rows 0-4, added rows 21+) - persisted in the project
    std::array<juce::File, kTracks> rowFiles;

    juce::TextButton playlistButton { "Playlist" };
    std::unique_ptr<PlaylistComponent> playlist;
    std::unique_ptr<PlaylistWindow> playlistWindow;
    std::atomic<bool> exporting { false };

    // Pass 2: the Station window + the global undo history (item 38)
    juce::TextButton stationButton { "STATION" };
    std::unique_ptr<boombap::StationPanel> station;
    std::unique_ptr<boombap::StationWindow> stationWindow;
    int selectedChannel = 0;
    // Round 1 (item 102): the auxiliary windows the minimise coupling
    // is currently holding down (SafePointers - a window destroyed
    // while held simply drops out of the restore).
    std::vector<juce::Component::SafePointer<juce::DocumentWindow>> hiddenByMinimise;
    boombap::UndoHistory history;
    bool suppressAutoHome = false; // history snapshots carry their clips explicitly (item 61 stays for files/host)
    int gestureIdleTicks = 0;
    juce::String gestureSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
