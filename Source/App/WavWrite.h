#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <vector>

// 24-bit stereo WAV writer shared by the WAV export and the vocal
// take sidecar file. Header-inline so every MainComponent
// translation unit can use it. Moved out of MainComponent.cpp in
// the file split; behaviour unchanged.

inline bool writeWavFile (const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    file.deleteFile(); // overwrite cleanly
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns the stream now

    const float* data[2] = { channels[0].data(), channels[1].data() };
    return writer->writeFromFloatArrays (data, 2, (int) channels[0].size());
}
