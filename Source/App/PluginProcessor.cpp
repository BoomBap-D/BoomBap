#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DataHome.h"

BoomBapProcessor::BoomBapProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();
    // MIDI build (item 25): the host owns the devices; the service
    // only routes what the host delivers. Load the saved Learn map.
    midiService.manageDevices = false;
    midiService.loadState();
}

void BoomBapProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

bool BoomBapProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::disabled();
}

void BoomBapProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI build (item 25): route the host's MIDI before the block
    // renders - the buffer's timestamps are block offsets, and the
    // engine clock the router reads is the one published last block.
    midiService.processHostBuffer (midiMessages);

    // The sequencer follows the host transport: play/stop and tempo.
    bool hostPlaying = false;
    double hostBpm = 0.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            hostPlaying = position->getIsPlaying();
            if (auto bpm = position->getBpm()) hostBpm = *bpm;
        }
    }
    engine.setPlaying (hostPlaying);
    if (hostBpm > 0.0 && std::abs (hostBpm - lastHostBpm) > 0.01)
    {
        engine.setBpm ((float) hostBpm);
        lastHostBpm = hostBpm;
    }

    const int numSamples = buffer.getNumSamples();
    const float* input = getTotalNumInputChannels() > 0 ? buffer.getReadPointer (0) : nullptr;
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    engine.process (input, outL, outR, numSamples);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

juce::AudioProcessorEditor* BoomBapProcessor::createEditor()
{
    return new BoomBapEditor (*this);
}

//==============================================================================
void BoomBapProcessor::setMediaData (const boombap::ProjectData& d)
{
    std::lock_guard<std::mutex> lock (mediaMutex);
    mediaData.samplePaths = d.samplePaths;
    mediaData.padPaths = d.padPaths;
    mediaData.vocalTakePath = d.vocalTakePath;
}

boombap::ProjectData BoomBapProcessor::captureStateData()
{
    boombap::ProjectData d;
    d.settings = engine.getSettings();
    std::lock_guard<std::mutex> lock (mediaMutex);
    d.samplePaths = mediaData.samplePaths;
    d.padPaths = mediaData.padPaths;
    d.vocalTakePath = mediaData.vocalTakePath;
    return d;
}

void BoomBapProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto d = captureStateData();
    if (engine.hasTake())
    {
        // Park the take in a temp file so the saved state can point at it,
        // the same sidecar-WAV arrangement as a saved project.
        juce::File takeFile;
        if (writeTakeTempFile (takeFile))
            d.vocalTakePath = takeFile.getFullPathName();
    }
    const juce::String json = boombap::projectToJson (d, juce::File());
    destData.append (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BoomBapProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const juce::String json = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
    boombap::ProjectData d;
    juce::String error;
    if (! boombap::projectFromJson (json, juce::File(), d, error))
        return;
    applyState (d);
    if (onStateRestored)
        onStateRestored (d);
}

void BoomBapProcessor::applyState (const boombap::ProjectData& d)
{
    engine.applySettings (d.settings);
    setMediaData (d);

    std::vector<float> mono;
    double fileSr = 44100.0;
    for (int i = 0; i < 3; ++i)
    {
        const int track = boombap::BoomBapEngine::Sample1 + i;
        engine.clearSample (track);
        const juce::File f (d.samplePaths[(size_t) i]);
        if (f.existsAsFile() && decodeToMono (f, mono, fileSr))
            engine.loadSample (track, mono, fileSr);
    }
    for (int i = 3; i < 16; ++i)
    {
        engine.clearPad (i);
        const juce::File f (d.padPaths[(size_t) i]);
        if (f.existsAsFile() && decodeToMono (f, mono, fileSr))
            engine.loadPadSample (i, mono, fileSr);
    }
    engine.clearVocalTake();
    const juce::File takeFile (d.vocalTakePath);
    if (takeFile.existsAsFile() && decodeToMono (takeFile, mono, fileSr))
        engine.setVocalTake (mono);
}

bool BoomBapProcessor::decodeToMono (const juce::File& f, std::vector<float>& out, double& sampleRateOut)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples <= 0)
        return false;

    sampleRateOut = reader->sampleRate;
    const int length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    if (! reader->read (&buffer, 0, length, 0, true, reader->numChannels > 1))
        return false;

    out.assign ((size_t) length, 0.0f);
    for (int i = 0; i < length; ++i)
    {
        float v = buffer.getSample (0, i);
        if (reader->numChannels > 1)
            v = 0.5f * (v + buffer.getSample (1, i));
        out[(size_t) i] = v;
    }
    return true;
}

bool BoomBapProcessor::writeTakeTempFile (juce::File& outFile)
{
    const auto data = engine.getTakeData();
    if (data.empty()) return false;

    // Item 62: a plugin take is a real WAV in the BoomBap home's Takes
    // folder - findable by the user, never parked in a temp directory.
    boombap::ensureDataHome();
    outFile = boombap::takesDir().getChildFile ("BoomBap plugin take.wav");
    outFile.deleteFile();

    juce::AudioBuffer<float> buffer (1, (int) data.size());
    buffer.copyFrom (0, 0, data.data(), (int) data.size());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (new juce::FileOutputStream (outFile),
                             engine.getSampleRate(), 1, 24, {}, 0));
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buffer, 0, (int) data.size());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoomBapProcessor();
}
