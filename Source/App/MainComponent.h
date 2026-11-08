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

private:
    static constexpr int kTracks = boombap::BoomBapEngine::kTracks;
    static constexpr int kSteps  = boombap::BoomBapEngine::kSteps;

    struct LabeledSlider
    {
        juce::Slider slider;
        juce::Label label;
    };

    void timerCallback() override;
    void togglePlay();
    void startTransport();
    void stopTransport();
    void startAudioOnlyPlayback();              // item 47: the audio view's own PLAY
    // Production Area tab bar (items 37/42): 0 steps, 1 MPC, 2 audio
    void selectTab (int index);
    void togglePopOut();
    void popOutTab (int index);
    void dockTab (int index);
    void updateTabChrome();
    void showKitMenu (int track);               // item 40
    juce::Component* tabContent (int index);
    // audio device setup (Pass 1 item 8) + autosave / recovery (item 7)
    void showAudioSettings();
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
    juce::TextButton playButton { "PLAY" },
                     stopButton { "STOP" },
                     clearButton { "Clear" }, resetButton { "Reset" };
    juce::TextButton audioButton { "Audio..." };
    juce::TextButton metroButton { "METRO" };
    LabeledSlider metroVol;
    PadActivityLight padLight;
    LevelMeter padMeter;
    float padShown = 0.0f;
    bool lastPlayingState = false;
    int autosaveTicks = 0;
    std::unique_ptr<juce::DocumentWindow> audioWindow;
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
    juce::TextButton waveSel[3];
    juce::Label waveName;
    juce::TextButton wavePlayButton { "PLAY" }; // item 46: audition this panel's sample
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
    std::unique_ptr<PadBankComponent> padBank;
    std::unique_ptr<PadWindow> padWindow;

    juce::TextButton pianoButton { "Piano Roll" };
    std::unique_ptr<PianoRollComponent> pianoRoll;
    std::unique_ptr<PianoRollWindow> pianoWindow;
    int rollTrack = 0; // the track the roll was last summoned from (item 28)

    // Fix pass 2, items 37/42: the Production Area is a TAB BAR - STEP
    // SEQUENCER / MPC PANEL / AUDIO MULTITRACK - and every tab can pop
    // out into its own window and dock back (his Reaper grammar).
    juce::TextButton tabButtons[3];
    juce::TextButton popOutButton { "Pop out" };
    juce::Label tabPoppedLabel;
    int activeTab = 0;
    bool tabPoppedOut[3] = {};
    std::unique_ptr<juce::DocumentWindow> productionWindows[3];
    std::unique_ptr<PadBankComponent> mpcPanel; // the MPC PANEL tab's pad bank
    std::unique_ptr<MultitrackComponent> multitrack;

    // item 40: audio files bound to rows outside the sample slots and
    // pad bank (kit rows 0-4, added rows 21+) - persisted in the project
    std::array<juce::File, kTracks> rowFiles;

    juce::TextButton playlistButton { "Playlist" };
    std::unique_ptr<PlaylistComponent> playlist;
    std::unique_ptr<PlaylistWindow> playlistWindow;
    std::atomic<bool> exporting { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
