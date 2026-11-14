// ============================================================================
// THE FACTORY BANK - implementation. See FactoryBank.h for the design.
// The WAV bytes come from BinaryData (FactoryKitData.bin, bundled by
// juce_add_binary_data); the offsets/names table is FactoryKitData.h.
// ============================================================================
#include "FactoryBank.h"
#include "FactoryKitData.h"
#include "BinaryData.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace boombap
{
int factoryKitCount()
{
    return factorydata::numKits;
}

juce::String factoryKitName (int kit)
{
    if (kit < 0 || kit >= factorydata::numKits) return {};
    return factorydata::kits[kit].name;
}

juce::String factoryPadName (int kit, int pad)
{
    if (kit < 0 || kit >= factorydata::numKits || pad < 0 || pad >= 16) return {};
    return factorydata::kits[kit].pads[pad].name;
}

bool factoryPadBytes (int kit, int pad, const void*& data, std::size_t& size)
{
    data = nullptr;
    size = 0;
    if (kit < 0 || kit >= factorydata::numKits || pad < 0 || pad >= 16) return false;
    if (BinaryData::FactoryKitData_bin == nullptr
        || BinaryData::FactoryKitData_binSize != (int) factorydata::blobSize)
        return false;
    const auto& e = factorydata::kits[kit].pads[pad];
    if (e.size == 0 || (std::size_t) e.offset + e.size > factorydata::blobSize)
        return false;
    data = BinaryData::FactoryKitData_bin + e.offset;
    size = e.size;
    return true;
}

bool decodeFactoryPad (int kit, int pad,
                       std::vector<float>& monoOut, double& sampleRateOut)
{
    monoOut.clear();
    sampleRateOut = 44100.0;

    const void* data = nullptr;
    std::size_t size = 0;
    if (! factoryPadBytes (kit, pad, data, size)) return false;

    // The pad load path's decode policy (MainComponent::decodeSampleFile),
    // minus the hand-rolled fallback: factory WAVs are canonical PCM from
    // our own render scripts, and the plausibility checks below still
    // refuse a lying reader rather than landing garbage on a pad.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto stream = std::make_unique<juce::MemoryInputStream> (data, size, false);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::move (stream)));
    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->lengthInSamples > (juce::int64) size
        || reader->sampleRate <= 0.0)
        return false;

    const int maxSamples = (int) (reader->sampleRate * 20.0); // pads stay <= 20 s, as in the file path
    const int n = (int) juce::jmin ((juce::int64) maxSamples, reader->lengthInSamples);
    const int channels = (int) juce::jmax (1u, reader->numChannels);

    juce::AudioBuffer<float> buffer (channels, n);
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
    return ! monoOut.empty();
}

bool loadFactoryKitInto (BoomBapEngine& engine, int kit)
{
    if (kit < 0 || kit >= factorydata::numKits) return false;

    std::vector<float> monos[16];
    double rates[16] = {};
    for (int pad = 0; pad < 16; ++pad)
        if (! decodeFactoryPad (kit, pad, monos[pad], rates[pad]))
            return false; // all-or-nothing: no half-filled bank

    for (int pad = 0; pad < 16; ++pad)
    {
        engine.loadPadSample (pad, monos[pad], rates[pad]);
        engine.loadSample (BoomBapEngine::Sample1 + pad, std::move (monos[pad]), rates[pad]);
    }
    return true;
}

juce::String factoryRef (int kit, int pad)
{
    return "factory:" + juce::String (kit) + ":" + juce::String (pad);
}

bool parseFactoryRef (const juce::String& path, int& kit, int& pad)
{
    kit = -1;
    pad = -1;
    if (! path.startsWith ("factory:")) return false;
    const auto rest = path.substring (8);
    const int colon = rest.indexOfChar (':');
    if (colon <= 0) return false;
    kit = rest.substring (0, colon).getIntValue();
    pad = rest.substring (colon + 1).getIntValue();
    return kit >= 0 && kit < factorydata::numKits && pad >= 0 && pad < 16;
}
} // namespace boombap
