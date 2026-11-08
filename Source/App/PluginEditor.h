#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "MainComponent.h"

// The plugin editor is the standalone app's main window, minus its own audio
// device: the processor's engine is shared, and the host provides the audio.
class BoomBapEditor : public juce::AudioProcessorEditor
{
public:
    explicit BoomBapEditor (BoomBapProcessor& proc);
    ~BoomBapEditor() override;

    void resized() override;

private:
    BoomBapProcessor& proc;
    MainComponent main;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoomBapEditor)
};
