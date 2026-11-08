#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine/BoomBapEngine.h"
#include "ProjectFile.h"
#include <functional>
#include <mutex>

// The plugin brain: owns the same engine as the standalone app. The host's
// transport drives the sequencer (play/stop and BPM follow the host), the
// input feeds the vocal channel, and the engine's stereo mix is the output.
// Host state is BoomBap's own project format: settings, patterns, song, and
// the sample/pad/take file paths, so a beat saved in a DAW reopens whole.
class BoomBapProcessor : public juce::AudioProcessor
{
public:
    BoomBapProcessor();
    ~BoomBapProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BoomBap"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    boombap::BoomBapEngine engine;

    // The editor reports media changes here so state saves can include them.
    void setMediaData (const boombap::ProjectData& d);
    // Settings (live from the engine) + media paths, as one project snapshot.
    boombap::ProjectData captureStateData();

    // Set by the editor while it exists: after a host state restore, the
    // editor resyncs its UI and file references from the restored project.
    std::function<void (const boombap::ProjectData&)> onStateRestored;

private:
    void applyState (const boombap::ProjectData& d);
    bool decodeToMono (const juce::File& f, std::vector<float>& out, double& sampleRateOut);
    bool writeTakeTempFile (juce::File& outFile);

    std::mutex mediaMutex;
    boombap::ProjectData mediaData;
    double lastHostBpm = 0.0;
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoomBapProcessor)
};
