#pragma once
// The standalone's audio engine. Pure C++ (no JUCE), so it can be tested
// headless. UI-thread setters write atomics; the audio thread picks them up
// at the start of each block. No allocation or locking on the audio thread
// except a try-lock around sample pads.

#include "../../Core/Drums808/Kick808.h"
#include "../../Core/Drums808/Snare808.h"
#include "../../Core/Drums808/Hat808.h"
#include "../../Core/Drums808/Clap808.h"
#include "../../Core/PadsMPC/MpcPads.h"
#include "../../Core/PadsMPC/MpcSwing.h"
#include "../../Core/Sampler950/S950Crusher.h"
#include "../../Core/ChannelStrip/BusCompressor.h"
#include "../../Core/ChannelStrip/StripChain.h"
#include "../../Core/Common/Limiter.h"
#include "../../Core/VocalChain/VocalChain.h"
#include "../../Core/Instruments/Synth.h"
#include "../../Core/Instruments/Piano.h"
#include "ProjectSettings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cmath>

namespace boombap
{
class BoomBapEngine
{
public:
    // Fix pass 2: kTracks is now the CAPACITY of the track list (item 39 -
    // the rows in use are numTracks, and the list scrolls); a pattern is two
    // bars (item 48 - 32 steps). Rows 5..20 are the pad bank's pads 1..16
    // (Sample N plays pad N); rows 21+ hold their own sample in the engine.
    static constexpr int kTracks = 32;
    static constexpr int kSteps  = 32;
    static constexpr int kPads   = MpcPads::kNumPads;
    static constexpr int kPatterns  = ProjectSettings::kPatterns;
    static constexpr int kSongSlots = ProjectSettings::kSongSlots;
    static constexpr int kSongLanes = ProjectSettings::kSongLanes;       // Round 4 (item 99c)
    static constexpr int kMaxSongBlocks = ProjectSettings::kMaxSongBlocks;
    static constexpr int kMaxTakeSeconds = 120;
    static constexpr int kAudioTracks   = ProjectSettings::kAudioTracks;
    static constexpr int kMaxAudioClips = ProjectSettings::kMaxAudioClips;
    enum Track { Kick, Snare, ClosedHat, OpenHat, Clap, Sample1, Sample2, Sample3 };
    static constexpr int kFirstOwnRow = Sample1 + kPads; // 21: first row with its own sample slot

    static std::string trackName (int t)
    {
        static const char* names[5] = { "Kick", "Snare", "Closed Hat", "Open Hat", "Clap" };
        if (t >= 0 && t < 5) return names[t];
        // Round 3 (item 88): rows are TRACKS until content names them -
        // "Sample N" is gone, because a track's content is not always a
        // sample (a VST instrument will live here too, item 104). The
        // kit rows keep their names: the built-in kit IS their content.
        if (t >= 0 && t < kTracks) return "TRACK " + std::to_string (t + 1);
        return "Track " + std::to_string (t + 1);
    }

    // Round 3 (item 88): a row's CONTENT name - the file name of the
    // sample bound to it (later, an instrument's name). The app sets it
    // wherever a file binds or unbinds; the name lives in the row's
    // FIRST COLUMN and nowhere else (LOAD always says LOAD). Message
    // thread only - the audio thread never reads names. Empty = the
    // row still goes by its plain TRACK n name.
    void setTrackContentName (int t, const std::string& name)
    {
        if (t >= 0 && t < kTracks) trackContent[(size_t) t] = name;
    }
    std::string trackContentName (int t) const
    {
        return (t >= 0 && t < kTracks) ? trackContent[(size_t) t] : std::string();
    }
    // Round 5 (item 107): the owner's own name for a track, set by
    // right-click rename. It lives beside the content name and WINS
    // over it everywhere (displayTrackName below, and every surface
    // reads names through channelDisplayName). An empty string means
    // no override - the track falls back to its derived name. Like
    // the content names this is message-thread state (the audio path
    // never reads names), persisted through ProjectSettings.
    void setTrackCustomName (int t, const std::string& name)
    {
        if (t >= 0 && t < kTracks) trackCustom[(size_t) t] = name.substr (0, 48);
    }
    std::string trackCustomName (int t) const
    {
        return (t >= 0 && t < kTracks) ? trackCustom[(size_t) t] : std::string();
    }
    std::string displayTrackName (int t) const
    {
        const std::string custom = trackCustomName (t);
        if (! custom.empty()) return custom;
        const std::string c = trackContentName (t);
        return c.empty() ? trackName (t) : c;
    }
    // channelName with the content names folded in - the one name every
    // surface (sequencer column, multitrack chip, Station drop-down and
    // board) reads, so a track is called the same thing everywhere.
    std::string channelDisplayName (int ch) const
    {
        switch (channelKind (ch))
        {
            case KindRow:    return displayTrackName (ch);
            case KindSynth:  return "Synth";
            case KindPiano:  return "Piano";
            case KindPadBus: return "Pad Bus";
            case KindAudio:  return "Audio " + std::to_string (ch - kAudioChannelBase + 1);
            case KindVocal:  return "Vocal";
            default:         return "Master";
        }
    }

    // ---- Pass 2: the Station's channels (items 7/19/20) ----
    // Every element is its own channel: rows 0..31, the two instruments,
    // the pad bus the pad rows feed, the audio tracks, the vocal channel.
    // kMasterChannel is a UI/analyser id only - the master bus is not a
    // strip channel (it carries the Glue + its limiter instead).
    static constexpr int kNumChannels    = ProjectSettings::kNumChannels;   // 44
    static constexpr int kSynthChannel   = ProjectSettings::kSynthChannel;  // 32
    static constexpr int kPianoChannel   = ProjectSettings::kPianoChannel;  // 33
    static constexpr int kPadBusChannel  = ProjectSettings::kPadBusChannel; // 34
    static constexpr int kAudioChannelBase = ProjectSettings::kAudioChannelBase; // 35
    static constexpr int kVocalChannel   = ProjectSettings::kVocalChannel;  // 43
    static constexpr int kMasterChannel  = kNumChannels;                    // 44

    enum ChannelKind { KindRow = 0, KindSynth, KindPiano, KindPadBus, KindAudio, KindVocal, KindMaster };
    static int channelKind (int ch)
    {
        if (ch >= 0 && ch < kTracks) return KindRow;
        if (ch == kSynthChannel) return KindSynth;
        if (ch == kPianoChannel) return KindPiano;
        if (ch == kPadBusChannel) return KindPadBus;
        if (ch >= kAudioChannelBase && ch < kAudioChannelBase + kAudioTracks) return KindAudio;
        if (ch == kVocalChannel) return KindVocal;
        return KindMaster;
    }
    // Item 26: the preamp head and the limiter tail live on the channels
    // that carry recorded audio - the audio tracks and the vocal channel.
    static bool channelHasPreamp (int ch) { const int k = channelKind (ch); return k == KindAudio || k == KindVocal; }
    static bool channelHasLimiter (int ch) { return channelHasPreamp (ch); }
    static bool isPadRow (int track) { return track >= Sample1 && track < Sample1 + kPads; }

    static std::string channelName (int ch, int numTracks, int numAudioTracks)
    {
        switch (channelKind (ch))
        {
            case KindRow:    return trackName (ch);
            case KindSynth:  return "Synth";
            case KindPiano:  return "Piano";
            case KindPadBus: return "Pad Bus";
            case KindAudio:  return "Audio " + std::to_string (ch - kAudioChannelBase + 1);
            case KindVocal:  return "Vocal";
            default:         return "Master";
        }
        (void) numTracks; (void) numAudioTracks;
    }

    BoomBapEngine()
    {
        for (auto& c : patGrid) c.store (0);
        for (auto& g : trackGain) g.store (1.0f); // item 113: every fader starts FLAT
        for (auto& m : trackMute) m.store (false);
        for (auto& s : trackSolo) s.store (false);
        for (auto& p : trackPeak) p.store (0.0f);
        for (auto& h : padHit) h.store (0);
        for (auto& n : patMelody) n.store (0);
        for (auto& n : patMelodySource) n.store (0);
        for (auto& n : patSynth) n.store (0);
        for (auto& n : patPiano) n.store (0);
        for (auto& n : songChainA) n.store (0);
        for (auto& a : blkActiveA) a.store (false);
        for (auto& n : blkPatternA) n.store (0);
        for (auto& n : blkLaneA) n.store (0);
        for (auto& n : blkStartA) n.store (0);
        for (auto& n : blkLenA) n.store (2);
        for (auto& b : kitBoundA) b.store (false);
        for (auto& b : ownBoundA) b.store (false);
        for (auto& g : audioGainA) g.store (1.0f); // item 113: flat
        for (auto& m : audioMuteA) m.store (false);
        for (auto& s : audioSoloA) s.store (false);
        loadDefaultPattern();
    }

    // ---------------- setup (message thread, before audio starts) ----------------
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        kick.prepare (sr); snare.prepare (sr); hat.prepare (sr); clap.prepare (sr);
        pads.prepare (sr);
        rowPads.prepare (sr); // Round 6 (item 109): the sequencer rows' own store
        dust.prepare (sr);
        dustR.prepare (sr);
        bus.prepare (sr);
        vocal.prepare (sr);
        takeVocal.prepare (sr);
        for (auto& c : clipChains) c.prepare (sr);
        synth.prepare (sr);
        piano.prepare (sr);
        for (auto& s : stripDsp) s.prepare (sr);
        masterLimiter.prepare (sr);
        stationDirty.store (true);
        applyParams();
        gainSmInit = false; // item 95: smoothing states re-seat on the targets
        tick = 0; samplesToNextTick = 0.0; wasPlaying = false;
        audioPos = 0; wasAudioPlaying = false;
        panicActive = false; panicVoicesPending = false;
        auditionVoice.active = false;
    }

    double getSampleRate() const  { return sr; }
    float  getBpm() const         { return bpm.load(); }

    // Copies pattern, mixer, sound and effect settings plus loaded samples from another
    // engine (not its playing voices, and not the vocal channel). Used by WAV export so the
    // offline render never touches the live engine.
    void copySettingsFrom (const BoomBapEngine& o)
    {
        for (int i = 0; i < (int) patGrid.size(); ++i) patGrid[(size_t) i].store (o.patGrid[(size_t) i].load());
        for (int t = 0; t < kTracks; ++t)
        {
            trackGain[t].store (o.trackGain[t].load());
            trackMute[t].store (o.trackMute[t].load());
            trackSolo[t].store (o.trackSolo[t].load());
            trackCustom[(size_t) t] = o.trackCustom[(size_t) t];
        }
        bpm.store (o.bpm.load());           swing.store (o.swing.load());
        masterDb.store (o.masterDb.load());
        for (int i = 0; i < (int) patMelody.size(); ++i) patMelody[(size_t) i].store (o.patMelody[(size_t) i].load());
        for (int i = 0; i < kPatterns; ++i) patMelodySource[i].store (o.patMelodySource[i].load());
        for (int i = 0; i < (int) patSynth.size(); ++i) patSynth[(size_t) i].store (o.patSynth[(size_t) i].load());
        for (int i = 0; i < (int) patPiano.size(); ++i) patPiano[(size_t) i].store (o.patPiano[(size_t) i].load());
        synthWave.store (o.synthWave.load()); synthCutoff.store (o.synthCutoff.load());
        synthReso.store (o.synthReso.load()); synthAttack.store (o.synthAttack.load());
        synthRelease.store (o.synthRelease.load()); synthLevel.store (o.synthLevel.load());
        pianoBright.store (o.pianoBright.load()); pianoDecay.store (o.pianoDecay.load());
        pianoLevel.store (o.pianoLevel.load());
        editPattern.store (o.editPattern.load());
        for (int i = 0; i < kSongSlots; ++i) songChainA[i].store (o.songChainA[i].load());
        songLength.store (o.songLength.load());
        songMode.store (o.songMode.load());
        for (int i = 0; i < kMaxSongBlocks; ++i) // item 99c: the export clone plays the arrangement too
        {
            blkPatternA[i].store (o.blkPatternA[i].load());
            blkLaneA[i].store (o.blkLaneA[i].load());
            blkStartA[i].store (o.blkStartA[i].load());
            blkLenA[i].store (o.blkLenA[i].load());
            blkActiveA[i].store (o.blkActiveA[i].load());
        }
        refreshSongTotalBars();
        blocksRevisionA.fetch_add (1);
        kickTune.store (o.kickTune.load()); kickDecay.store (o.kickDecay.load());
        snareSnappy.store (o.snareSnappy.load());
        openHatDecay.store (o.openHatDecay.load()); clapTail.store (o.clapTail.load());
        dustOn.store (o.dustOn.load());     dustRate.store (o.dustRate.load());
        glueOn.store (o.glueOn.load());     glueThreshold.store (o.glueThreshold.load());
        // Fix pass: the export clone must sound like the session - the vocal
        // settings (item 36: they treat the take at playback), pad capture,
        // the audio track's mixer state and its clips all ride along.
        vocalMic.store (o.vocalMic.load()); vocalDistance.store (o.vocalDistance.load());
        vocalInputDb.store (o.vocalInputDb.load());
        padCaptureOn.store (o.padCaptureOn.load());
        for (int i = 0; i < kAudioTracks; ++i)
        {
            audioGainA[(size_t) i].store (o.audioGainA[(size_t) i].load());
            audioMuteA[(size_t) i].store (o.audioMuteA[(size_t) i].load());
            audioSoloA[(size_t) i].store (o.audioSoloA[(size_t) i].load());
        }
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            clipsA[(size_t) i].active.store (o.clipsA[(size_t) i].active.load());
            clipsA[(size_t) i].start.store (o.clipsA[(size_t) i].start.load());
            clipsA[(size_t) i].srcStart.store (o.clipsA[(size_t) i].srcStart.load());
            clipsA[(size_t) i].length.store (o.clipsA[(size_t) i].length.load());
            clipsA[(size_t) i].track.store (o.clipsA[(size_t) i].track.load());
        }
        numTracksA.store (o.numTracksA.load());
        numAudioTracksA.store (o.numAudioTracksA.load());
        {
            std::lock_guard<std::mutex> lock (o.sampleMutex);
            pads = o.pads;
            takeData = o.takeData;
            clipData = o.clipData;
            kitData = o.kitData;
            kitSrcSr = o.kitSrcSr;
            ownData = o.ownData;
            ownSrcSr = o.ownSrcSr;
            takeLenAtomic.store (o.takeLenAtomic.load());
            takeStartPosA.store (o.takeStartPosA.load());
            takeOn.store (o.takeOn.load());
        }
        for (int t = 0; t <= Clap; ++t) kitBoundA[(size_t) t].store (o.kitBoundA[(size_t) t].load());
        for (int t = kFirstOwnRow; t < kTracks; ++t)
            ownBoundA[(size_t) (t - kFirstOwnRow)].store (o.ownBoundA[(size_t) (t - kFirstOwnRow)].load());
        {
            std::scoped_lock lock (o.stationMutex, stationMutex);
            station = o.station;
        }
        stationDirty.store (true);
        clipRevision.fetch_add (1);
        pads.stopAll();
        rowPads.stopAll();
        synth.stopAll();
        piano.stopAll();
        dirty.store (true);
    }

    // ---------------- project settings (save / load) ----------------
    ProjectSettings getSettings() const
    {
        ProjectSettings p;
        p.numTracks = numTracksA.load();
        p.bpm = bpm.load();  p.swing = swing.load();  p.masterDb = masterDb.load();
        for (int t = 0; t < kTracks; ++t)
        {
            p.trackGain[t] = trackGain[t].load();
            p.trackMute[t] = trackMute[t].load();
            p.trackSolo[t] = trackSolo[t].load();
            p.trackCustomName[t] = trackCustom[t]; // item 107
        }
        for (int pt = 0; pt < kPatterns; ++pt)
        {
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kSteps; ++s) p.patData[pt][t][s] = patGrid[(size_t) gIdx (pt, t, s)].load();
            for (int s = 0; s < kSteps; ++s) p.patMelody[pt][s] = patMelody[(size_t) pt * kSteps + s].load();
            p.patMelodySource[pt] = patMelodySource[pt].load();
            for (int s = 0; s < kSteps; ++s) p.patSynth[pt][s] = patSynth[(size_t) pt * kSteps + s].load();
            for (int s = 0; s < kSteps; ++s) p.patPiano[pt][s] = patPiano[(size_t) pt * kSteps + s].load();
        }
        p.synthWave = synthWave.load();       p.synthCutoffHz = synthCutoff.load();
        p.synthReso = synthReso.load();       p.synthAttackMs = synthAttack.load();
        p.synthReleaseMs = synthRelease.load(); p.synthLevel = synthLevel.load();
        p.pianoBright = pianoBright.load();   p.pianoDecayMs = pianoDecay.load();
        p.pianoLevel = pianoLevel.load();
        p.currentPattern = editPattern.load();
        for (int i = 0; i < kSongSlots; ++i) p.songChain[i] = songChainA[i].load();
        p.songLength = songLength.load();
        // Round 4 (items 99b/99c): the mode and the arrangement ride the
        // snapshot too; the chain fields are re-derived from the blocks
        // so a saved file's two song forms can never disagree.
        p.songMode = songMode.load();
        for (int i = 0; i < kMaxSongBlocks; ++i) p.songBlocks[i] = getSongBlock (i);
        p.refreshChainMirror();
        p.kickTuneHz = kickTune.load();       p.kickDecayMs = kickDecay.load();
        p.snareSnappy = snareSnappy.load();
        p.openHatDecayMs = openHatDecay.load(); p.clapTailMs = clapTail.load();
        p.dustOn = dustOn.load();             p.dustRateHz = dustRate.load();
        p.glueOn = glueOn.load();             p.glueThresholdDb = glueThreshold.load();
        p.vocalMic = vocalMic.load();         p.vocalDistanceCm = vocalDistance.load();
        p.vocalInputDb = vocalInputDb.load();
        {
            std::lock_guard<std::mutex> lock (sampleMutex);
            for (int i = 0; i < kPads; ++i)
            {
                const auto pp = pads.getPadParams (i);
                p.padStart[i] = pp.startFrac;    p.padEnd[i] = pp.endFrac;
                p.padTune[i] = pp.tuneSemitones; p.padGain[i] = pp.gain;
                p.padVelSens[i] = pp.velSens;    p.padReverse[i] = pp.reverse;
                p.padMuteGroup[i] = pp.muteGroup;
                // item 101: the envelope points ride the same snapshot
                const int ec = std::min ((int) pp.envPoints.size(), ProjectSettings::kPadEnvMax);
                p.padEnvCount[i] = pp.envPoints.size() >= 2 ? ec : 0;
                for (int k = 0; k < ec; ++k)
                {
                    p.padEnvTime[i][k]  = pp.envPoints[(size_t) k].first;
                    p.padEnvLevel[i][k] = pp.envPoints[(size_t) k].second;
                }
            }
        }
        p.numAudioTracks = numAudioTracksA.load();
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            const auto& c = clipsA[(size_t) i];
            p.audioClips[i].active = c.active.load();
            p.audioClips[i].startSamples = c.start.load();
            p.audioClips[i].sourceStart = c.srcStart.load();
            p.audioClips[i].lengthSamples = c.length.load();
            p.audioClips[i].track = c.track.load();
        }
        for (int i = 0; i < kAudioTracks; ++i)
        {
            p.audioGain[i] = audioGainA[(size_t) i].load();
            p.audioMute[i] = audioMuteA[(size_t) i].load();
            p.audioSolo[i] = audioSoloA[(size_t) i].load();
        }
        {
            std::lock_guard<std::mutex> lock (stationMutex);
            for (int ch = 0; ch < kNumChannels; ++ch) p.strips[ch] = station.strips[(size_t) ch];
            for (int t = 0; t < kTracks; ++t) p.trackPan[t] = station.pan[(size_t) t];
            for (int i = 0; i < kAudioTracks; ++i) p.audioPan[i] = station.pan[(size_t) (kAudioChannelBase + i)];
            p.synthPan = station.pan[kSynthChannel];
            p.pianoPan = station.pan[kPianoChannel];
            p.padBusGain = station.padBusGain; p.padBusPan = station.pan[kPadBusChannel];
            p.padBusMute = station.padBusMute;
            p.vocalGain = station.vocalGain;   p.vocalPan = station.pan[kVocalChannel];
            p.vocalMute = station.vocalMute;   p.vocalSolo = station.vocalSolo;
            p.synthMute = station.synthMute;   p.synthSolo = station.synthSolo;
            p.pianoMute = station.pianoMute;   p.pianoSolo = station.pianoSolo;
            p.masterLimiterOn = station.masterLimiterOn;
            p.masterLimiterCeilingDb = station.masterLimiterCeilingDb;
        }
        return p;
    }

    // Applies a whole project at once. Values are clamped to safe ranges. The vocal
    // monitor on/off switch is not part of a project, so opening a file never turns a
    // microphone on.
    void applySettings (ProjectSettings p)
    {
        p.sanitize();
        // Round 4 (item 99c): a chain-only project becomes blocks here -
        // the backwards-compatibility lift. The mirror keeps the legacy
        // fields honest for the file writer and older readers.
        p.convertChainToBlocks();
        p.refreshChainMirror();
        numTracksA.store (p.numTracks);
        bpm.store (p.bpm);  swing.store (p.swing);  masterDb.store (p.masterDb);
        for (int t = 0; t < kTracks; ++t)
        {
            trackGain[t].store (p.trackGain[t]);
            trackMute[t].store (p.trackMute[t]);
            trackSolo[t].store (p.trackSolo[t]);
            trackCustom[(size_t) t] = p.trackCustomName[t]; // item 107
        }
        for (int pt = 0; pt < kPatterns; ++pt)
        {
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kSteps; ++s) patGrid[(size_t) gIdx (pt, t, s)].store ((uint8_t) p.patData[pt][t][s]);
            for (int s = 0; s < kSteps; ++s) patMelody[(size_t) pt * kSteps + s].store (p.patMelody[pt][s]);
            patMelodySource[pt].store (p.patMelodySource[pt]);
            for (int s = 0; s < kSteps; ++s) patSynth[(size_t) pt * kSteps + s].store (p.patSynth[pt][s]);
            for (int s = 0; s < kSteps; ++s) patPiano[(size_t) pt * kSteps + s].store (p.patPiano[pt][s]);
        }
        synthWave.store (p.synthWave);       synthCutoff.store (p.synthCutoffHz);
        synthReso.store (p.synthReso);       synthAttack.store (p.synthAttackMs);
        synthRelease.store (p.synthReleaseMs); synthLevel.store (p.synthLevel);
        pianoBright.store (p.pianoBright);   pianoDecay.store (p.pianoDecayMs);
        pianoLevel.store (p.pianoLevel);
        editPattern.store (p.currentPattern);
        for (int i = 0; i < kSongSlots; ++i) songChainA[i].store (p.songChain[i]);
        songLength.store (p.songLength);
        songMode.store (p.songMode); // item 99b: the mode is project state now
        for (int i = 0; i < kMaxSongBlocks; ++i)
        {
            const auto& b = p.songBlocks[i];
            blkPatternA[(size_t) i].store (b.pattern);
            blkLaneA[(size_t) i].store (b.lane);
            blkStartA[(size_t) i].store (b.startBar);
            blkLenA[(size_t) i].store (b.lengthBars);
            blkActiveA[(size_t) i].store (b.active);
        }
        refreshSongTotalBars();
        blocksRevisionA.fetch_add (1);
        kickTune.store (p.kickTuneHz);        kickDecay.store (p.kickDecayMs);
        snareSnappy.store (p.snareSnappy);
        openHatDecay.store (p.openHatDecayMs); clapTail.store (p.clapTailMs);
        dustOn.store (p.dustOn);              dustRate.store (p.dustRateHz);
        glueOn.store (p.glueOn);              glueThreshold.store (p.glueThresholdDb);
        vocalMic.store (p.vocalMic);          vocalDistance.store (p.vocalDistanceCm);
        vocalInputDb.store (p.vocalInputDb);
        {
            std::lock_guard<std::mutex> lock (sampleMutex);
            for (int i = 0; i < kPads; ++i)
            {
                MpcPads::PadParams pp;
                pp.startFrac = p.padStart[i];    pp.endFrac = p.padEnd[i];
                pp.tuneSemitones = p.padTune[i]; pp.gain = p.padGain[i];
                pp.velSens = p.padVelSens[i];    pp.reverse = p.padReverse[i];
                pp.muteGroup = p.padMuteGroup[i];
                // item 101: rebuild the point list from the flat arrays
                pp.envPoints.clear();
                for (int k = 0; k < p.padEnvCount[i]; ++k)
                    pp.envPoints.push_back ({ p.padEnvTime[i][k], p.padEnvLevel[i][k] });
                if (pp.envPoints.size() < 2) pp.envPoints.clear(); // 0/1 points = no envelope
                pads.setPadParams (i, pp);
            }
        }
        numAudioTracksA.store (p.numAudioTracks);
        if (recTrackA.load() >= p.numAudioTracks) recTrackA.store (0);
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            clipsA[(size_t) i].active.store (p.audioClips[i].active);
            clipsA[(size_t) i].start.store (p.audioClips[i].startSamples);
            clipsA[(size_t) i].srcStart.store (p.audioClips[i].sourceStart);
            clipsA[(size_t) i].length.store (p.audioClips[i].lengthSamples);
            clipsA[(size_t) i].track.store (p.audioClips[i].track);
        }
        // Item 58: heal item 56's residue on load. Two active clips with
        // identical geometry ON THE SAME TRACK are the double-placement
        // bug's stacks - the same span of the same take, indistinguishable.
        // Every edit on them LOOKED broken in a state-dependent way: split
        // one twin and the other still spans the cut; delete one and the
        // other still shows; click a stack and you may hit either. No
        // gesture in the app creates such a pair on purpose, so a load
        // keeps one copy. (The same span on a DIFFERENT track is a
        // legitimate double - it stays.)
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            if (! clipsA[(size_t) i].active.load()) continue;
            for (int j = i + 1; j < kMaxAudioClips; ++j)
            {
                if (! clipsA[(size_t) j].active.load()) continue;
                if (clipsA[(size_t) j].track.load() == clipsA[(size_t) i].track.load()
                    && clipsA[(size_t) j].start.load() == clipsA[(size_t) i].start.load()
                    && clipsA[(size_t) j].srcStart.load() == clipsA[(size_t) i].srcStart.load()
                    && clipsA[(size_t) j].length.load() == clipsA[(size_t) i].length.load())
                    clipsA[(size_t) j].active.store (false);
            }
        }
        for (int i = 0; i < kAudioTracks; ++i)
        {
            audioGainA[(size_t) i].store (p.audioGain[i]);
            audioMuteA[(size_t) i].store (p.audioMute[i]);
            audioSoloA[(size_t) i].store (p.audioSolo[i]);
        }
        {
            // clip buffers never travel in settings - clear them so a stale
            // buffer can never sit under a freshly applied region
            std::lock_guard<std::mutex> lock (sampleMutex);
            for (auto& d : clipData) d.clear();
        }
        {
            std::lock_guard<std::mutex> lock (stationMutex);
            for (int ch = 0; ch < kNumChannels; ++ch) station.strips[(size_t) ch] = p.strips[ch];
            for (int t = 0; t < kTracks; ++t) station.pan[(size_t) t] = p.trackPan[t];
            for (int i = 0; i < kAudioTracks; ++i) station.pan[(size_t) (kAudioChannelBase + i)] = p.audioPan[i];
            station.pan[kSynthChannel] = p.synthPan;
            station.pan[kPianoChannel] = p.pianoPan;
            station.pan[kPadBusChannel] = p.padBusPan;
            station.pan[kVocalChannel] = p.vocalPan;
            station.padBusGain = p.padBusGain; station.padBusMute = p.padBusMute;
            station.vocalGain = p.vocalGain;   station.vocalMute = p.vocalMute; station.vocalSolo = p.vocalSolo;
            station.synthMute = p.synthMute;   station.synthSolo = p.synthSolo;
            station.pianoMute = p.pianoMute;   station.pianoSolo = p.pianoSolo;
            station.masterLimiterOn = p.masterLimiterOn;
            station.masterLimiterCeilingDb = p.masterLimiterCeilingDb;
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
        clipRevision.fetch_add (1);
        dirty.store (true);
    }

    // After a project load: every clip on the audio track was cut from the
    // take, so each active clip's buffer is the take's audio.
    void restoreClipBuffersFromTake()
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        bool anyActive = false;
        for (int i = 0; i < kMaxAudioClips; ++i)
            if (clipsA[(size_t) i].active.load())
            {
                clipData[(size_t) i] = takeData;
                anyActive = true;
            }
        // Item 61: if the restored project already homes this take in
        // clips, the take counts as placed and the raw take loop stands
        // down - the clips are its voice. A take restored with NO clips
        // stays unplaced, and the UI homes it (no invisible audio).
        if (anyActive && takeSerial >= 0)
        {
            placedSerial = takeSerial;
            takeOn.store (false);
        }
        clipRevision.fetch_add (1);
    }

    void clearSample (int track)
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (track >= Sample1 && track < Sample1 + kPads)
            rowPads.clearSample (track - Sample1); // item 109: the ROW's own store
        else if (track >= kFirstOwnRow && track < kTracks)
        {
            ownData[(size_t) (track - kFirstOwnRow)].reset();
            ownBoundA[(size_t) (track - kFirstOwnRow)].store (false);
        }
    }

    // ---------------- track list (Fix pass 2 item 39) ----------------
    // The list grows one row at a time and scrolls in the UI; it never
    // shrinks below the rows a project already has.
    int  getNumTracks() const { return numTracksA.load(); }
    void setNumTracks (int n) { numTracksA.store (std::clamp (n, 1, kTracks)); }
    int  addTrack()
    {
        const int n = numTracksA.load();
        if (n >= kTracks) return -1;
        numTracksA.store (n + 1);
        dirty.store (true);
        return n; // the new row's index
    }

    // ---------------- kit rows carry a sample (Fix pass 2 item 40) --------
    // A built-in drum row can be bound to a sample file: the row then plays
    // the sample (one-shot, through the row's own channel) instead of the
    // synthesized drum. Clearing the binding brings the built-in sound back.
    void setKitSample (int track, std::vector<float> data, double sourceSampleRate)
    {
        if (track < 0 || track > Clap) return;
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (data.size() >= 2)
        {
            kitData[(size_t) track] = std::make_shared<const std::vector<float>> (std::move (data));
            kitSrcSr[(size_t) track] = sourceSampleRate;
            kitBoundA[(size_t) track].store (true);
        }
        else
        {
            kitData[(size_t) track].reset();
            kitBoundA[(size_t) track].store (false);
        }
        dirty.store (true);
    }

    void clearKitSample (int track) { setKitSample (track, {}, 44100.0); }

    bool kitHasSample (int track) const
    {
        return track >= 0 && track <= Clap && kitBoundA[(size_t) track].load();
    }

    // A sample row past the pad bank (rows 21+) holds its sample here.
    bool rowHasOwnSample (int track) const
    {
        return track >= kFirstOwnRow && track < kTracks
               && ownBoundA[(size_t) (track - kFirstOwnRow)].load();
    }

    // ---------------- transport ----------------
    void setPlaying (bool p)      { playing.store (p); if (! p) pausedA.store (false); }
    bool isPlaying() const        { return playing.load(); }

    // ---- Round 6 (item 115): PAUSE. Freezes the playhead where it
    // stands - the transport stops advancing but stays engaged, and
    // PLAY from pause resumes at the frozen position (the audio
    // thread re-enters the grid there, exactly like a live seek).
    // STOP clears it (stopAllSound); a pause requested while stopped
    // is dropped, so the flag can never linger into the next start.
    void setPaused (bool p)       { pausedA.store (p && playing.load()); }
    bool isPaused() const         { return pausedA.load() && playing.load(); }

    // ---- Fix pass 2 item 45: STOP takes priority over EVERYTHING. ----
    // Every sounding thing - pad one-shots (run-on included), synth/piano
    // voices, kit/row samples, the waveform audition, audio clips, the raw
    // take - is silenced by the audio thread at the next block, behind a
    // ~5 ms fade so the cut itself doesn't click. Both transports stop too.
    // (Ending a recording is the caller's job first, so the take lands.)
    void stopAllSound()
    {
        playing.store (false);
        pausedA.store (false); // Round 6 (item 115): STOP clears a pause too
        audioPlaying.store (false);
        stopAllRequested.store (true);
        // Round 2 (item 93): STOP also returns the transport park to
        // zero - stop, then play, is from the top (the MPC grammar the
        // app already had), and the clock reads 0:00 again.
        transportStartPosA.store (0);
        transportSeekA.store (0);
    }

    // ---- the audio multitrack's own transport (items 47/48/50) ----
    // The audio view plays the audio tracks ALONE: its clock runs from its
    // own playhead and is not fenced by the pattern loop, so a take of any
    // length plays end to end. While the main transport plays, the audio
    // clock follows it (clips stay in sync with the beat).
    void setAudioPlaying (bool on) { audioPlaying.store (on); }
    bool isAudioPlaying() const    { return audioPlaying.load(); }
    void setAudioPlayhead (long long samples)
    {
        audioStartPosA.store (std::max (0LL, samples));
        audioSeekPending.store (true);
        audioPosAtomic.store (std::max (0LL, samples));
    }
    long long audioPlayhead() const  { return audioStartPosA.load(); }
    long long audioPosition() const  { return audioPosAtomic.load(); }
    // Bit i is set while clip slot i is sounding (item 54: find it by eye).
    unsigned int soundingClipMask() const { return soundingClipMaskA.load(); }
    int  currentStep() const      { return playStep.load(); }  // -1 when stopped
    void setBpm (float v)         { bpm.store (std::clamp (v, 40.0f, 220.0f)); }
    void setSwing (float percent) { swing.store (std::clamp (percent, 50.0f, 75.0f)); }

    // ---------------- pattern (0 = off, 90 = normal, 127 = accent) ----------------
    void setStep (int track, int step, int velocity) { patGrid[(size_t) gIdx (editPattern.load(), track, step)].store ((uint8_t) velocity); }
    int  getStep (int track, int step) const         { return patGrid[(size_t) gIdx (editPattern.load(), track, step)].load(); }

    void clearPattern()
    {
        const int p = editPattern.load();
        for (int t = 0; t < kTracks; ++t)
            for (int s = 0; s < kSteps; ++s) patGrid[(size_t) gIdx (p, t, s)].store (0);
        for (int s = 0; s < kSteps; ++s) patMelody[(size_t) p * kSteps + s].store (0);
        for (int s = 0; s < kSteps; ++s) patSynth[(size_t) p * kSteps + s].store (0);
        for (int s = 0; s < kSteps; ++s) patPiano[(size_t) p * kSteps + s].store (0);
    }

    // ---------------- patterns + playlist ----------------
    void setEditPattern (int p) { editPattern.store (std::clamp (p, 0, kPatterns - 1)); }
    int  getEditPattern() const  { return editPattern.load(); }

    void setSongSlot (int slot, int pattern)
    {
        if (slot >= 0 && slot < kSongSlots)
            songChainA[(size_t) slot].store (std::clamp (pattern, 0, kPatterns - 1));
    }
    int  getSongSlot (int slot) const
    {
        return (slot >= 0 && slot < kSongSlots) ? songChainA[(size_t) slot].load() : 0;
    }
    void setSongLength (int len) { songLength.store (std::clamp (len, 0, kSongSlots)); }
    int  getSongLength() const   { return songLength.load(); }
    void setSongMode (bool on)   { songMode.store (on); }
    bool isSongMode() const      { return songMode.load(); }
    int  getPlayPattern() const  { return playPattern.load(); }  // the pattern sounding right now
    int  getPlaySongPos() const  { return playSongPos.load(); }  // chain position, -1 in pattern mode

    // ---------------- Round 4 (item 99c): the arrangement ----------------
    // Pattern blocks on lanes over a bar timeline - the playlist's real
    // model. When any block exists it, not the legacy chain, is what
    // song mode plays (applySettings converts a chain-only project into
    // blocks, so the two never fight). Blocks live in atomics: the UI
    // writes them, the audio thread reads them through an RT snapshot.
    using SongBlock = ProjectSettings::SongBlock;

    int songBlockCount() const
    {
        int n = 0;
        for (const auto& a : blkActiveA) if (a.load()) ++n;
        return n;
    }
    SongBlock getSongBlock (int i) const
    {
        SongBlock b;
        if (i < 0 || i >= kMaxSongBlocks) return b;
        b.active     = blkActiveA[(size_t) i].load();
        b.pattern    = blkPatternA[(size_t) i].load();
        b.lane       = blkLaneA[(size_t) i].load();
        b.startBar   = blkStartA[(size_t) i].load();
        b.lengthBars = blkLenA[(size_t) i].load();
        return b;
    }
    void setSongBlock (int i, const SongBlock& b)
    {
        if (i < 0 || i >= kMaxSongBlocks) return;
        blkPatternA[(size_t) i].store (std::clamp (b.pattern, 0, kPatterns - 1));
        blkLaneA[(size_t) i].store (std::clamp (b.lane, 0, kSongLanes - 1));
        blkStartA[(size_t) i].store (std::max (0, b.startBar));
        blkLenA[(size_t) i].store (std::max (1, b.lengthBars));
        blkActiveA[(size_t) i].store (b.active);
        refreshSongTotalBars();
        blocksRevisionA.fetch_add (1);
    }
    // First free slot, or -1 when the arrangement is full.
    int addSongBlock (const SongBlock& b)
    {
        for (int i = 0; i < kMaxSongBlocks; ++i)
            if (! blkActiveA[(size_t) i].load()) { setSongBlock (i, b); return i; }
        return -1;
    }
    void removeSongBlock (int i)
    {
        if (i < 0 || i >= kMaxSongBlocks) return;
        blkActiveA[(size_t) i].store (false);
        refreshSongTotalBars();
        blocksRevisionA.fetch_add (1);
    }
    void clearSongBlocks()
    {
        for (auto& a : blkActiveA) a.store (false);
        refreshSongTotalBars();
        blocksRevisionA.fetch_add (1);
    }
    // The arrangement's length in bars (0 = no arrangement).
    int  songTotalBars() const   { return songTotalBarsA.load(); }
    bool hasArrangement() const  { return songTotalBarsA.load() > 0; }
    // The song bar under the playhead in arrangement playback (-1 else).
    int  getPlaySongBar() const  { return playSongBarA.load(); }

    // One block sounding at one song step: its pattern, the step inside
    // the pattern (the pattern loops inside a longer block), its lane,
    // and its block index.
    struct BlockHit { int pattern = 0, localStep = 0, lane = 0, block = -1; };

    // The arrangement resolution, pure so the tests can pin it: every
    // block covering the absolute song step, ordered by lane (array
    // order breaks a same-lane tie, which the playlist's no-overlap
    // rule already prevents). Playback merge rule (item 99c): drum and
    // sample rows take the MAX velocity across hits and trigger once -
    // the same pattern on two lanes never double-triggers a row; the
    // monophonic melody/synth/piano lanes take the lowest lane's note.
    static int resolveSongStep (const SongBlock* blocks, int numBlocks,
                                long long songStep, BlockHit* out, int maxOut)
    {
        int n = 0;
        for (int i = 0; i < numBlocks && n < maxOut; ++i)
        {
            const SongBlock& b = blocks[i];
            if (! b.active) continue;
            const long long start = (long long) b.startBar * 16;
            const long long len = (long long) b.lengthBars * 16;
            if (songStep < start || songStep >= start + len) continue;
            out[n].pattern = b.pattern;
            out[n].localStep = (int) ((songStep - start) % kSteps);
            out[n].lane = b.lane;
            out[n].block = i;
            ++n;
        }
        std::sort (out, out + n, [] (const BlockHit& a, const BlockHit& b)
        {
            return a.lane != b.lane ? a.lane < b.lane : a.block < b.block;
        });
        return n;
    }

    // Can `candidate` sit in the arrangement without overlapping a
    // block on its own lane? (Other lanes may overlap freely - they
    // sound together.) `ignore` is the block being moved, or -1.
    bool arrangementCanPlace (const SongBlock& candidate, int ignore) const
    {
        if (! candidate.active) return true;
        const long long cs = candidate.startBar, ce = cs + candidate.lengthBars;
        for (int i = 0; i < kMaxSongBlocks; ++i)
        {
            if (i == ignore) continue;
            const auto b = getSongBlock (i);
            if (! b.active || b.lane != candidate.lane) continue;
            if (cs < (long long) b.startBar + b.lengthBars && (long long) b.startBar < ce)
                return false;
        }
        return true;
    }

    // ---------------- Round 4 (item 99a): the ghost roll's data ----------------
    // The pitched-lane note a sequencer row owns at a step of the EDIT
    // pattern (0 = none, else the lane value, semitone offset + 25).
    // The melody lane belongs to whichever source the pattern chose:
    // the kick owns it when the source is 0, pad row Sample1+i owns it
    // when the source is i+1. Rows with no pitched lane read 0.
    int ghostNoteValue (int track, int step) const
    {
        if (step < 0 || step >= kSteps) return 0;
        int src = -1;
        if (track == Kick) src = 0;
        else if (track >= Sample1 && track < Sample1 + kPads) src = track - Sample1 + 1;
        else return 0;
        const int pat = editPattern.load();
        if (patMelodySource[(size_t) pat].load() != src) return 0;
        return patMelody[(size_t) pat * kSteps + (size_t) step].load();
    }
    bool trackHasGhostLane (int track) const
    {
        return track == Kick || (track >= Sample1 && track < Sample1 + kPads);
    }
    // Round 6 (item 125): the ghost roll is EARNED - a track carries
    // it only when the lane it owns actually holds notes in the edit
    // pattern. Owning the lane TYPE (trackHasGhostLane) is not
    // enough; an empty lane raises no strip.
    bool trackHasGhostContent (int track) const
    {
        for (int s = 0; s < kSteps; ++s)
            if (ghostNoteValue (track, s) != 0) return true;
        return false;
    }

    // ---------------- piano roll (one monophonic melody lane) ----------------
    // Note values match ProjectSettings: 0 = off, else semitone offset + 25.
    void setMelodyStep (int step, int value) { if (step >= 0 && step < kSteps) patMelody[(size_t) editPattern.load() * kSteps + step].store (value); }
    int  getMelodyStep (int step) const      { return (step >= 0 && step < kSteps) ? patMelody[(size_t) editPattern.load() * kSteps + step].load() : 0; }
    void setMelodySource (int source)        { patMelodySource[(size_t) editPattern.load()].store (std::clamp (source, 0, 16)); }
    int  getMelodySource() const             { return patMelodySource[(size_t) editPattern.load()].load(); }

    // ---------------- instruments (synth + piano lanes, same note encoding) ----------------
    void setSynthStep (int step, int value) { if (step >= 0 && step < kSteps) patSynth[(size_t) editPattern.load() * kSteps + step].store (value); }
    int  getSynthStep (int step) const      { return (step >= 0 && step < kSteps) ? patSynth[(size_t) editPattern.load() * kSteps + step].load() : 0; }
    void setPianoStep (int step, int value) { if (step >= 0 && step < kSteps) patPiano[(size_t) editPattern.load() * kSteps + step].store (value); }
    int  getPianoStep (int step) const      { return (step >= 0 && step < kSteps) ? patPiano[(size_t) editPattern.load() * kSteps + step].load() : 0; }

    void setSynthWave (int w)        { synthWave.store (std::clamp (w, 0, 3)); dirty.store (true); }
    void setSynthCutoff (float hz)   { synthCutoff.store (hz);   dirty.store (true); }
    void setSynthReso (float v)      { synthReso.store (v);      dirty.store (true); }
    void setSynthAttack (float ms)   { synthAttack.store (ms);   dirty.store (true); }
    void setSynthRelease (float ms)  { synthRelease.store (ms);  dirty.store (true); }
    void setSynthLevel (float v)     { synthLevel.store (v);     dirty.store (true); }
    void setPianoBright (float v)    { pianoBright.store (v);    dirty.store (true); }
    void setPianoDecay (float ms)    { pianoDecay.store (ms);    dirty.store (true); }
    void setPianoLevel (float v)     { pianoLevel.store (v);     dirty.store (true); }

    void loadDefaultPattern()
    {
        const ProjectSettings d;
        for (int t = 0; t < kTracks; ++t)
            for (int s = 0; s < kSteps; ++s)
                setStep (t, s, d.patData[0][t][s]);
    }

    // ---------------- mixer ----------------
    void setTrackGain (int t, float linear) { trackGain[t].store (linear); }
    float getTrackGain (int t) const        { return (t >= 0 && t < kTracks) ? trackGain[(size_t) t].load() : 0.0f; }
    bool getTrackMute (int t) const         { return t >= 0 && t < kTracks && trackMute[(size_t) t].load(); }
    void setTrackMute (int t, bool m)       { trackMute[t].store (m); }
    void setMasterDb (float db)             { masterDb.store (db); }

    // ---- solo (Pass 1 item 6): non-exclusive; while any row is soloed, only
    // soloed rows sound. Applies to the 8 step rows, exactly like mute. ----
    void setTrackSolo (int t, bool s)       { if (t >= 0 && t < kTracks) trackSolo[(size_t) t].store (s); }
    bool getTrackSolo (int t) const         { return t >= 0 && t < kTracks && trackSolo[(size_t) t].load(); }
    bool anySoloActive() const              { for (const auto& s : trackSolo) if (s.load()) return true; return false; }

    // ---- metronome (Pass 1 item 5): click on every beat (steps 0/4/8/12),
    // accented on the bar line, in time with the sequencer clock. ----
    void  setMetronomeOn (bool on)          { metronomeOn.store (on); }
    bool  isMetronomeOn() const             { return metronomeOn.load(); }
    void  setMetronomeLevel (float level01) { metronomeLevel.store (std::clamp (level01, 0.0f, 1.0f)); }
    float getMetronomeLevel() const         { return metronomeLevel.load(); }

    // ---- pad activity (Pass 1 item 4b): peak level of the loudest pad voice
    // since the last call (consumed by reading), and which pad produced it.
    float consumePadActivityPeak()          { return padActivityPeak.exchange (0.0f); }
    int   lastTriggeredPad() const          { return lastPadIndex.load(); }

    // ---------------- drum sound controls ----------------
    void setKickTune (float hz)       { kickTune.store (hz);        dirty.store (true); }
    void setKickDecay (float ms)      { kickDecay.store (ms);       dirty.store (true); }
    void setSnareSnappy (float v)     { snareSnappy.store (v);      dirty.store (true); }
    void setOpenHatDecay (float ms)   { openHatDecay.store (ms);    dirty.store (true); }
    void setClapTail (float ms)       { clapTail.store (ms);        dirty.store (true); }

    // ---------------- master: S950 "dust" and bus glue ----------------
    void setDustEnabled (bool e)      { dustOn.store (e); }
    void setDustRate (float hz)       { dustRate.store (hz);        dirty.store (true); }
    void setGlueEnabled (bool e)      { glueOn.store (e); }
    void setGlueThresholdDb (float d) { glueThreshold.store (d);    dirty.store (true); }
    float getGlueReductionDb() const  { return glueGr.load(); }

    // Per-track peak since the last call (post track gain, pre master FX).
    // Reading consumes it; the UI polls this for the track meters.
    float consumeTrackPeak (int t)    { return trackPeak[t].exchange (0.0f); }

    // ---------------- vocal channel ----------------
    void setVocalEnabled (bool e)     { vocalOn.store (e); }
    void setVocalMic (int type)       { vocalMic.store (type);      dirty.store (true); }
    void setVocalDistance (float cm)  { vocalDistance.store (cm);   dirty.store (true); }
    void setVocalInputDb (float db)   { vocalInputDb.store (db);    dirty.store (true); }

    // ---------------- vocal take ----------------
    // Recording captures the CLEAN input (input-gain staging only) - the mic
    // emulation you monitor through is never burned in (item 36); it is
    // applied to the take at playback, and can be switched at any time.
    // Pressing record while the beat plays starts the take on the next downbeat; the
    // finished take is padded to whole bars and loops against the pattern. The live
    // mic is never part of an export, but a recorded take is.
    void startRecording()
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        captureGen.fetch_add (1); // a new capture: finishTake may build again
        recData.assign ((size_t) (sr * kMaxTakeSeconds), 0.0f);
        recPublished.store (0);
        if (playing.load()) recPending.store (true);
        else { takeStartPosA.store (0); recording.store (true); }
    }

    bool isRecording() const { return recording.load() || recPending.load(); }
    double recordedSeconds() const { return sr > 0.0 ? recPublished.load() / sr : 0.0; }

    // Fix pass 2 item 53: the take is visible WHILE it records. The
    // multitrack view asks where the take started on the timeline and pulls
    // the peaks captured so far, drawing the clip as it grows.
    bool isCapturing() const { return recording.load(); }
    long long recordStartPosition() const { return takeStartPosA.load(); }
    int copyRecordingPeaks (float* mins, float* maxs, int maxPeaks, long long samplesPerPeak) const
    {
        if (mins == nullptr || maxs == nullptr || maxPeaks <= 0 || samplesPerPeak <= 0)
            return 0;
        std::lock_guard<std::mutex> lock (sampleMutex);
        const int n = recPublished.load();
        if (n <= 0 || recData.empty()) return 0;
        const int peaks = (int) std::min<long long> (maxPeaks, (n + samplesPerPeak - 1) / samplesPerPeak);
        for (int p = 0; p < peaks; ++p)
        {
            const long long begin = (long long) p * samplesPerPeak;
            const long long end = std::min<long long> (n, begin + samplesPerPeak);
            // sample up to 48 points per bucket: a faithful shape at a
            // bounded cost, so the audio thread is never held up for long
            const int probes = (int) std::min<long long> (48, end - begin);
            float lo = 0.0f, hi = 0.0f;
            for (int k = 0; k < probes; ++k)
            {
                const long long idx = probes <= 1 ? begin
                                                  : begin + (end - begin - 1) * k / (probes - 1);
                const float v = recData[(size_t) idx];
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
            mins[p] = lo;
            maxs[p] = hi;
        }
        return peaks;
    }

    void stopRecording()
    {
        recording.store (false);
        recPending.store (false);
        finishTake();
    }

    // Builds the finished take from the capture buffer (the UI also calls this when a
    // recording hits the length cap and stops itself). Keeps any previous take when
    // nothing was captured.
    void finishTake()
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        const int n = recPublished.load();
        if (n <= 0) return;
        // Item 56: build at most once per capture. stopRecording calls
        // this, and so does the UI when a recording stops itself - the
        // second call for the same capture must not rebuild (and must
        // never re-arm the raw take loop behind the clip's back).
        if (builtForGen == captureGen.load() && ! takeData.empty()) return;
        const double barSamples = sr * 240.0 / (double) bpm.load();
        const int bars = std::max (1, (int) std::ceil (n / barSamples));
        const int quantized = (int) std::llround (bars * barSamples);
        takeData.assign (recData.begin(), recData.begin() + n);
        takeData.resize ((size_t) quantized, 0.0f);
        takeLenAtomic.store (quantized);
        takeStartPosA.store (takeStartPosA.load() % std::max (1LL, std::llround (barSamples)));
        builtForGen = captureGen.load();
        takeSerial = ++takeSerialSource;
        // The raw take does NOT start looping on its own (item 56): it
        // lands as a clip, and "Loop take" is the user's explicit switch.
        takeOn.store (false);
    }

    bool hasTake() const { return takeLenAtomic.load() > 0; }
    double takeSeconds() const { return sr > 0.0 ? takeLenAtomic.load() / sr : 0.0; }
    void setTakeEnabled (bool e) { takeOn.store (e); }
    bool takeEnabled() const { return takeOn.load(); }

    void setVocalTake (std::vector<float> data)
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        takeData = std::move (data);
        takeLenAtomic.store ((int) takeData.size());
        takeStartPosA.store (0);
        takeSerial = ++takeSerialSource; // a (re)loaded take may be homed once
        takeOn.store (true);
    }

    void clearVocalTake()
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        takeData.clear();
        takeLenAtomic.store (0);
        takeSerial = -1;
    }

    std::vector<float> getTakeData() const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        return takeData;
    }

    // ---------------- samples (message thread) ----------------
    // Round 6 (item 109 - D's ruling): sequencer rows hold their OWN
    // sounds and the pad bank holds its own; the two stores are
    // separate now. Loading a row never touches a pad and loading a
    // pad never touches a row - a pad changes only by an explicit pad
    // action (a drag onto it, its own chooser, a kit/chop load).
    // Rows 21+ hold their own sample too (item 39's added rows).
    void loadSample (int track, std::vector<float> data, double sourceSampleRate)
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (track >= Sample1 && track < Sample1 + kPads)
            rowPads.loadSample (track - Sample1, std::move (data), sourceSampleRate);
        else if (track >= kFirstOwnRow && track < kTracks)
        {
            ownData[(size_t) (track - kFirstOwnRow)] =
                std::make_shared<const std::vector<float>> (std::move (data));
            ownSrcSr[(size_t) (track - kFirstOwnRow)] = sourceSampleRate;
            ownBoundA[(size_t) (track - kFirstOwnRow)].store (true);
        }
    }

    // ---------------- waveform audition (Fix pass 2 item 46) -------------
    // The sample waveform panel's own play button: plays the shown sample
    // once, straight through the master chain. The global stop (item 45)
    // silences it like everything else.
    void startAudition (std::vector<float> data, double sourceSampleRate)
    {
        {
            std::lock_guard<std::mutex> lock (sampleMutex);
            auditionData = std::make_shared<const std::vector<float>> (std::move (data));
            auditionSrcSr = sourceSampleRate;
        }
        auditionStartRequested.store (true);
    }
    void stopAudition() { auditionStopRequested.store (true); }
    bool isAuditionPlaying() const { return auditionSounding.load(); }

    // ---------------- pad bank (all 16 pads, message thread) ----------------
    void loadPadSample (int pad, std::vector<float> data, double sourceSampleRate)
    {
        if (pad < 0 || pad >= kPads) return;
        std::lock_guard<std::mutex> lock (sampleMutex);
        pads.loadSample (pad, std::move (data), sourceSampleRate);
    }

    void clearPad (int pad)
    {
        if (pad < 0 || pad >= kPads) return;
        std::lock_guard<std::mutex> lock (sampleMutex);
        pads.clearSample (pad);
    }

    // Queues a live pad hit; the audio thread fires it at the next block,
    // whether or not the transport is running.
    void triggerPad (int pad, float velocity)
    {
        if (pad < 0 || pad >= kPads) return;
        padHit[pad].store ((int) std::lround (std::clamp (velocity, 0.0f, 1.0f) * 127.0f));
    }

    // =================================================================
    // Round 5 (item 105): THE LIVE-NOTE FUNNEL.
    //
    // Every played note - the on-screen keyboard's mouse, the QWERTY
    // rows, and (Pass 5) MIDI controllers - enters through liveNoteOn
    // / liveNoteOff and nowhere else. Producers (any thread) push a
    // small event into a lock-free ring; the audio thread drains it
    // in process(), beside the pad hits, under the same sample lock.
    //
    // Routing (the selected channel is whatever the caller passes -
    // the UI reads the one global selection at press time):
    //   rows 0..31   the row's sound pitched around C4 (note 60):
    //                pad rows go through the MPC pad voice path, so
    //                trims, the item-101 envelope and tune apply
    //                exactly as a pad hit hears them; a kit-bound
    //                drum row sounds its kit voice pitched; the
    //                unbound kick is pitch-tracked; the unbound
    //                snare / hats / clap have no pitched voice and
    //                simply sound at their root; rows past the pad
    //                bank (their own samples, item 39) pitch their
    //                own voice.
    //   Synth (32)   synth voices at pitch (root C3); note-off
    //                releases the voice for real.
    //   Piano (33)   piano voices at pitch (root C4); note-off is
    //                the damper - the ring collapses fast.
    //   anything else (pad bus, audio tracks, vocal, master) is not
    //   a note channel and stays silent.
    //
    // Note-off is a NO-OP for the one-shot voices (rows): classic
    // sampler behaviour - a drum hit rings out; only the sustained
    // instruments (synth, piano damper) answer an off. Live notes
    // never touch recording (item 56: they are not takes) and never
    // reach the item-31 pad capture (a separate, deliberate door);
    // they die with the global stop like every voice (item 45).
    // =================================================================
    static bool channelIsPlayable (int channel)
    {
        return (channel >= 0 && channel < kTracks)
            || channel == kSynthChannel || channel == kPianoChannel;
    }

    void liveNoteOn (int channel, int midiNote, float velocity)
    {
        if (! channelIsPlayable (channel)) return;
        pushLiveNote ({ channel, midiNote, std::clamp (velocity, 0.0f, 1.0f), true });
    }
    void liveNoteOff (int channel, int midiNote)
    {
        if (! channelIsPlayable (channel)) return;
        pushLiveNote ({ channel, midiNote, 0.0f, false });
    }
    // Release whatever a channel is holding (the UI's focus-loss and
    // "no stuck notes, ever" backstop).
    void liveAllNotesOff (int channel)
    {
        if (channel == kSynthChannel)
            for (int n = 0; n < 128; ++n) liveNoteOff (channel, n);
    }

    // Round 5 (item 105d): STEP-WRITE. While the transport plays, a
    // played note lands - quantized to the step sounding right now -
    // in the PLAYING pattern, in the channel's Round 4 lane:
    //   Synth / Piano   their pitched lanes, value = offset + 25
    //                   (the roll's own encoding, roots C3 / C4).
    //   Kick / pad rows a note AT the root (C4) writes the row's STEP
    //                   - Round 4 shows a row's steps as notes on its
    //                   root row, so writing the root writes the step;
    //                   a note OFF the root writes the row's melody
    //                   lane at that pitch and claims the lane's
    //                   source for the row (the same claim opening
    //                   the roll on the row would make).
    //   Snare / hats / clap / own-sample rows have no pitched lane:
    //                   any note writes the row's step.
    // Returns true when a note landed. Message thread; undo grouping
    // is the caller's (one gesture per pass).
    bool stepWriteNote (int channel, int midiNote, float velocity)
    {
        if (! playing.load() || pausedA.load()) return false; // item 115: a frozen playhead writes nothing
        const int step = currentStep();
        if (step < 0 || step >= kSteps) return false;
        const int pat = playPattern.load();
        const auto laneValue = [] (int offset)
        {
            return std::clamp (offset + 25, 1, 49);
        };
        if (channel == kSynthChannel)
        {
            patSynth[(size_t) pat * kSteps + step].store (laneValue (midiNote - 48));
            dirty.store (true);
            return true;
        }
        if (channel == kPianoChannel)
        {
            patPiano[(size_t) pat * kSteps + step].store (laneValue (midiNote - 60));
            dirty.store (true);
            return true;
        }
        if (channel < 0 || channel >= kTracks || channel >= numTracksA.load()) return false;
        const int vel127 = std::clamp ((int) std::lround (velocity * 127.0f), 1, 127);
        const int offset = midiNote - 60;
        if (trackHasGhostLane (channel) && offset != 0)
        {
            patMelody[(size_t) pat * kSteps + step].store (laneValue (offset));
            patMelodySource[(size_t) pat].store (channel == Kick ? 0 : channel - Sample1 + 1);
            dirty.store (true);
            return true;
        }
        patGrid[(size_t) gIdx (pat, channel, step)].store ((uint8_t) vel127);
        dirty.store (true);
        return true;
    }

    void setPadParams (int pad, const MpcPads::PadParams& p)
    {
        if (pad < 0 || pad >= kPads) return;
        std::lock_guard<std::mutex> lock (sampleMutex);
        pads.setPadParams (pad, p);
    }

    MpcPads::PadParams getPadParams (int pad) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        return pads.getPadParams (pad);
    }

    bool padHasSample (int pad) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        return pads.hasSample (pad);
    }

    // A copy of a pad's sample for display (the MPC panel's waveform).
    std::vector<float> getPadSampleData (int pad) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (pad < 0 || pad >= kPads) return {};
        return pads.getSampleData (pad);
    }

    // Round 6 (item 109): the sequencer sample ROWS' own store - the
    // row-side twins of the pad accessors above (index 0 = row
    // Sample1). The pad bank's editor never reads these; the rows
    // sound them through trigger(), the melody lane and the live-note
    // funnel.
    bool rowHasSample (int row) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (row < 0 || row >= kPads) return false;
        return rowPads.hasSample (row);
    }

    std::vector<float> getRowSampleData (int row) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (row < 0 || row >= kPads) return {};
        return rowPads.getSampleData (row);
    }

    // ---------------- pad capture (Fix pass item 31) ----------------
    // While the transport plays, a live hit on pads 1-3 (the sequencer's
    // sample rows) also writes that row's step at the playhead, with the
    // hit's velocity - the pad bank performs INTO the pattern. Pads 4-16
    // have no row and stay live-only.
    void setPadCaptureEnabled (bool on) { padCaptureOn.store (on); }
    bool isPadCaptureEnabled() const    { return padCaptureOn.load(); }

    // UI feedback: the pad index of the most recent captured hit, or -1 once
    // consumed. Lets the pad bank flash and the grid refresh on a capture.
    int consumeCapturedPad() { return capturedPad.exchange (-1); }

    // ---------------- audio track + clips (Fix pass items 32/35) ----------------
    // The recorded take becomes a clip on the audio track: visible waveform,
    // a home on the timeline, trim/cut/move editing. The clip's region is
    // plain data; the recorded audio (the take buffer) is shared, never cut.
    int placeTakeAsClip()
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (takeData.empty()) return -1;
        // Item 56: ONE take lands as ONE clip. Several UI paths ask for
        // the placement (the record button, the transport stop, the UI
        // timer spotting the stop, the multitrack's add button) - the
        // take's serial makes every ask after the first a no-op, so a
        // take can never stack copies of itself on the track.
        if (takeSerial < 0 || placedSerial == takeSerial) return -1;
        for (int i = 0; i < kMaxAudioClips; ++i)
            if (! clipsA[(size_t) i].active.load())
            {
                clipData[(size_t) i] = takeData;
                clipsA[(size_t) i].start.store (takeStartPosA.load());
                clipsA[(size_t) i].srcStart.store (0);
                clipsA[(size_t) i].length.store ((long long) takeData.size());
                clipsA[(size_t) i].track.store (std::clamp (recTrackA.load(), 0, numAudioTracksA.load() - 1));
                clipsA[(size_t) i].active.store (true);
                clipRevision.fetch_add (1);
                placedSerial = takeSerial;
                placedSlot = i;
                // the clip is the take's home now - the raw take loop stands
                // down so the same audio never sounds twice
                takeOn.store (false);
                return i;
            }
        return -1;
    }

    // Round 6 (item 114): a file dropped from the waveform panel onto
    // the multitrack becomes a clip in its own right - the data is the
    // clip's own buffer (already resampled to the engine rate by the
    // caller), placed on the dropped track at the dropped position.
    // Unlike the take path there is no serial: every drop is a new clip.
    int placeDataAsClip (std::vector<float> data, int track, long long startSamples)
    {
        if (data.empty()) return -1;
        std::lock_guard<std::mutex> lock (sampleMutex);
        for (int i = 0; i < kMaxAudioClips; ++i)
            if (! clipsA[(size_t) i].active.load())
            {
                const long long len = (long long) data.size();
                clipData[(size_t) i] = std::move (data);
                clipsA[(size_t) i].start.store (std::max (0LL, startSamples));
                clipsA[(size_t) i].srcStart.store (0);
                clipsA[(size_t) i].length.store (len);
                clipsA[(size_t) i].track.store (std::clamp (track, 0, std::max (0, numAudioTracksA.load() - 1)));
                clipsA[(size_t) i].active.store (true);
                clipRevision.fetch_add (1);
                return i;
            }
        return -1;
    }

    // Item 56: has the current take already been homed as a clip?
    bool takeIsPlaced() const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        return takeSerial >= 0 && placedSerial == takeSerial;
    }

    // The slot the current take was homed in (or -1), so the UI can say
    // which audio track it lives on.
    int takeClipSlot() const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        return (takeSerial >= 0 && placedSerial == takeSerial) ? placedSlot : -1;
    }

    int audioClipCount() const
    {
        int n = 0;
        for (const auto& c : clipsA) if (c.active.load()) ++n;
        return n;
    }

    AudioClip getAudioClip (int slot) const
    {
        AudioClip c;
        if (slot < 0 || slot >= kMaxAudioClips) return c;
        const auto& s = clipsA[(size_t) slot];
        c.active = s.active.load();
        c.startSamples = s.start.load();
        c.sourceStart = s.srcStart.load();
        c.lengthSamples = s.length.load();
        c.track = s.track.load();
        return c;
    }

    std::vector<float> getAudioClipData (int slot) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (slot < 0 || slot >= kMaxAudioClips) return {};
        return clipData[(size_t) slot];
    }

    long long getAudioClipDataLength (int slot) const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (slot < 0 || slot >= kMaxAudioClips) return 0;
        return (long long) clipData[(size_t) slot].size();
    }

    void clearAudioClip (int slot)
    {
        if (slot < 0 || slot >= kMaxAudioClips) return;
        {
            std::lock_guard<std::mutex> lock (sampleMutex);
            clipData[(size_t) slot].clear();
        }
        clipsA[(size_t) slot].active.store (false);
        clipsA[(size_t) slot].length.store (0);
        clipRevision.fetch_add (1);
    }

    void clearAllAudioClips()
    {
        for (int i = 0; i < kMaxAudioClips; ++i) clearAudioClip (i);
    }

    void moveAudioClip (int slot, long long newStartSamples)
    {
        if (slot < 0 || slot >= kMaxAudioClips || ! clipsA[(size_t) slot].active.load()) return;
        clipsA[(size_t) slot].start.store (std::max (0LL, newStartSamples));
        clipRevision.fetch_add (1);
    }

    // Trim: move any edge. newLength is clamped so the region never leaves
    // the recorded audio and never shrinks below a sliver.
    void trimAudioClip (int slot, long long newStart, long long newSourceStart, long long newLength)
    {
        if (slot < 0 || slot >= kMaxAudioClips || ! clipsA[(size_t) slot].active.load()) return;
        const long long dataLen = getAudioClipDataLength (slot);
        newSourceStart = std::clamp (newSourceStart, 0LL, std::max (0LL, dataLen - 512));
        newLength = std::clamp (newLength, 512LL, std::max (512LL, dataLen - newSourceStart));
        clipsA[(size_t) slot].start.store (std::max (0LL, newStart));
        clipsA[(size_t) slot].srcStart.store (newSourceStart);
        clipsA[(size_t) slot].length.store (newLength);
        clipRevision.fetch_add (1);
    }

    // Cut: split the clip at a timeline position; the right half lands in a
    // free slot, pointing further into the same recorded audio.
    bool splitAudioClip (int slot, long long atTimelineSamples)
    {
        if (slot < 0 || slot >= kMaxAudioClips) return false;
        const AudioClip c = getAudioClip (slot);
        if (! c.active) return false;
        const long long rel = atTimelineSamples - c.startSamples;
        if (rel < 512 || rel > c.lengthSamples - 512) return false;
        int freeSlot = -1;
        for (int i = 0; i < kMaxAudioClips; ++i)
            if (! clipsA[(size_t) i].active.load()) { freeSlot = i; break; }
        if (freeSlot < 0) return false;
        {
            std::lock_guard<std::mutex> lock (sampleMutex);
            clipData[(size_t) freeSlot] = clipData[(size_t) slot];
        }
        clipsA[(size_t) freeSlot].start.store (atTimelineSamples);
        clipsA[(size_t) freeSlot].srcStart.store (c.sourceStart + rel);
        clipsA[(size_t) freeSlot].length.store (c.lengthSamples - rel);
        clipsA[(size_t) freeSlot].track.store (c.track);
        clipsA[(size_t) freeSlot].active.store (true);
        clipsA[(size_t) slot].length.store (rel);
        clipRevision.fetch_add (1);
        return true;
    }

    int clipRevisionCount() const { return clipRevision.load(); }

    // The audio track's own mixer channel (item 35: mixer order = track
    // order, so this channel follows the 8 instrument rows).
    void  setAudioTrackGain (int t, float g) { if (t >= 0 && t < kAudioTracks) audioGainA[(size_t) t].store (std::clamp (g, 0.0f, 1.2f)); }
    float getAudioTrackGain (int t) const    { return t >= 0 && t < kAudioTracks ? audioGainA[(size_t) t].load() : 0.0f; }
    void  setAudioTrackMute (int t, bool m)  { if (t >= 0 && t < kAudioTracks) audioMuteA[(size_t) t].store (m); }
    bool  getAudioTrackMute (int t) const    { return t >= 0 && t < kAudioTracks && audioMuteA[(size_t) t].load(); }
    void  setAudioTrackSolo (int t, bool s)  { if (t >= 0 && t < kAudioTracks) audioSoloA[(size_t) t].store (s); }
    bool  getAudioTrackSolo (int t) const    { return t >= 0 && t < kAudioTracks && audioSoloA[(size_t) t].load(); }

    // Item 60: audio tracks are added like sequencer tracks - each one its
    // own channel. Recording targets the armed track; a take lands there.
    int  getNumAudioTracks() const { return numAudioTracksA.load(); }
    int  addAudioTrack()
    {
        const int n = numAudioTracksA.load();
        if (n >= kAudioTracks) return n;
        numAudioTracksA.store (n + 1);
        dirty.store (true);
        return n + 1;
    }
    //======================================================================
    // FIX PASS 3, item 69: whole-track deletion. Only rows the USER added
    // can be deleted. Kit rows 0-4 are protected - they are the starter
    // kit's skeleton (their samples stay replaceable per item 40 and
    // Reset restores the built-ins, but the rows themselves stay), and
    // pad rows 5-20 are bound to the sixteen physical pads: deleting one
    // would orphan a pad. Everything a deleted row owns moves with it:
    // its steps in every pattern, its mixer atomics, its Station strip
    // and pan, its own sample slot. Rows above shift down one. Wrapped
    // in the caller's snapshot undo gesture this is ONE undoable action,
    // and redo re-applies the post-delete snapshot, so nothing the row
    // held can return as a ghost.
    bool removeTrack (int track)
    {
        const int n = numTracksA.load();
        if (track < kFirstOwnRow || track >= n) return false;

        for (int pt = 0; pt < kPatterns; ++pt)
        {
            for (int t = track; t < n - 1; ++t)
                for (int s = 0; s < kSteps; ++s)
                    patGrid[(size_t) gIdx (pt, t, s)].store (patGrid[(size_t) gIdx (pt, t + 1, s)].load());
            for (int s = 0; s < kSteps; ++s)
                patGrid[(size_t) gIdx (pt, n - 1, s)].store (0);
        }
        for (int t = track; t < n - 1; ++t)
        {
            trackGain[(size_t) t].store (trackGain[(size_t) (t + 1)].load());
            trackMute[(size_t) t].store (trackMute[(size_t) (t + 1)].load());
            trackSolo[(size_t) t].store (trackSolo[(size_t) (t + 1)].load());
        }
        // item 88: a row's content name belongs to the row - it shifts
        // with everything else the row owns, and the vacated last row
        // goes back to its plain TRACK n name.
        for (int t = track; t < n - 1; ++t)
            trackContent[(size_t) t] = trackContent[(size_t) (t + 1)];
        trackContent[(size_t) (n - 1)].clear();
        trackGain[(size_t) (n - 1)].store (1.0f); // item 113: a fresh row starts flat too
        trackMute[(size_t) (n - 1)].store (false);
        trackSolo[(size_t) (n - 1)].store (false);
        {
            std::lock_guard<std::mutex> lock (sampleMutex);
            for (int t = track; t < n - 1; ++t)
            {
                ownData[(size_t) (t - kFirstOwnRow)]   = ownData[(size_t) (t + 1 - kFirstOwnRow)];
                ownSrcSr[(size_t) (t - kFirstOwnRow)]  = ownSrcSr[(size_t) (t + 1 - kFirstOwnRow)];
                ownBoundA[(size_t) (t - kFirstOwnRow)].store (ownBoundA[(size_t) (t + 1 - kFirstOwnRow)].load());
            }
            ownData[(size_t) (n - 1 - kFirstOwnRow)].reset();
            ownSrcSr[(size_t) (n - 1 - kFirstOwnRow)] = 44100.0;
            ownBoundA[(size_t) (n - 1 - kFirstOwnRow)].store (false);
        }
        {
            std::lock_guard<std::mutex> lock (stationMutex);
            for (int ch = track; ch < n - 1; ++ch)
            {
                station.strips[(size_t) ch] = station.strips[(size_t) (ch + 1)];
                station.pan[(size_t) ch]    = station.pan[(size_t) (ch + 1)];
            }
            station.strips[(size_t) (n - 1)] = {};
            station.pan[(size_t) (n - 1)] = 0.0f;
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
        numTracksA.store (n - 1);
        dirty.store (true);
        return true;
    }

    // FIX PASS 3, item 69: delete an added audio lane whole - the lane,
    // every clip on it, and its Station channel. Lane 0 is protected:
    // it is the permanent home every recorded take lands on. Clips on
    // lanes above shift down one lane; the take's homing follows its
    // clip, and if the take's own clip died with the lane the take is
    // un-homed (its audio untouched) so the established re-homing paths
    // can place it again - no invisible audio either way (item 61).
    bool removeAudioTrack (int track)
    {
        const int n = numAudioTracksA.load();
        if (track < 1 || track >= n) return false;

        const bool takeDies = takeIsPlaced() && takeClipSlot() >= 0
                              && clipsA[(size_t) takeClipSlot()].track.load() == track;
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            if (! clipsA[(size_t) i].active.load()) continue;
            const int ct = clipsA[(size_t) i].track.load();
            if (ct == track) clearAudioClip (i);
            else if (ct > track) clipsA[(size_t) i].track.store (ct - 1);
        }
        if (takeDies) placedSerial = -2; // un-homed; its audio is untouched

        for (int t = track; t < n - 1; ++t)
        {
            audioGainA[(size_t) t].store (audioGainA[(size_t) (t + 1)].load());
            audioMuteA[(size_t) t].store (audioMuteA[(size_t) (t + 1)].load());
            audioSoloA[(size_t) t].store (audioSoloA[(size_t) (t + 1)].load());
        }
        audioGainA[(size_t) (n - 1)].store (1.0f); // item 113: flat
        audioMuteA[(size_t) (n - 1)].store (false);
        audioSoloA[(size_t) (n - 1)].store (false);
        {
            std::lock_guard<std::mutex> lock (stationMutex);
            for (int ch = kAudioChannelBase + track; ch < kAudioChannelBase + n - 1; ++ch)
            {
                station.strips[(size_t) ch] = station.strips[(size_t) (ch + 1)];
                station.pan[(size_t) ch]    = station.pan[(size_t) (ch + 1)];
            }
            station.strips[(size_t) (kAudioChannelBase + n - 1)] = {};
            station.pan[(size_t) (kAudioChannelBase + n - 1)] = 0.0f;
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
        const int rt = recTrackA.load();
        if (rt == track) recTrackA.store (0);
        else if (rt > track) recTrackA.store (rt - 1);
        numAudioTracksA.store (n - 1);
        clipRevision.fetch_add (1);
        dirty.store (true);
        return true;
    }

    void setRecTargetTrack (int t) { recTrackA.store (std::clamp (t, 0, kAudioTracks - 1)); }
    int  getRecTargetTrack() const { return recTrackA.load(); }
    void setAudioClipTrack (int slot, int t)
    {
        if (slot < 0 || slot >= kMaxAudioClips || ! clipsA[(size_t) slot].active.load()) return;
        clipsA[(size_t) slot].track.store (std::clamp (t, 0, kAudioTracks - 1));
        clipRevision.fetch_add (1);
    }

    // Timeline helpers for the multitrack view.
    long long transportPosition() const { return transportPosAtomic.load(); }
    double barSamples() const          { return sr * 240.0 / (double) bpm.load(); }
    int    songBars() const            { return songLength.load(); }  // 0 = pattern mode

    // ---- Round 2 (item 93): the transport can be PARKED and SEEKED. ----
    // REW / FF move a start position (one step, or one bar on a double
    // press - the press logic lives in the UI's TransportNav); PLAY
    // starts where the transport is parked; while playing, a seek lands
    // live. STOP returns the park to zero (stopAllSound, below). A seek
    // never lands while a take is recording - moving the timeline under
    // a take would misalign it, so the request is dropped, not queued.
    double stepSamples() const
    {
        const double rate = sr > 0.0 ? sr : 44100.0;
        return rate * 60.0 / ((double) bpm.load() * kPPQN) * kTicksPer16th;
    }
    long long transportLengthSamples() const
    {
        const long long pattern = (long long) std::llround (stepSamples() * (double) kSteps);
        if (songMode.load())
        {
            // Round 4 (item 99c): the arrangement sets the song's length
            // when it exists; the legacy chain otherwise.
            if (songTotalBarsA.load() > 0)
                return (long long) std::llround (stepSamples() * 16.0 * (double) songTotalBarsA.load());
            if (songLength.load() > 0)
                return pattern * (long long) songLength.load();
        }
        return pattern;
    }
    // The skip arithmetic, pure so the tests can pin it: move `steps`
    // steps (negative = back) from `current`, clamped to the timeline.
    static long long skipTarget (long long current, int steps, double samplesPerStep,
                                 long long lengthSamples)
    {
        const long long target = current + (long long) std::llround ((double) steps * samplesPerStep);
        return std::clamp (target, 0LL, std::max (0LL, lengthSamples));
    }
    long long transportStartPosition() const { return transportStartPosA.load(); }
    void seekTransport (long long pos)
    {
        if (recording.load()) return; // a take in progress owns the timeline
        const long long len = transportLengthSamples();
        const long long target = len > 0 ? std::clamp (pos, 0LL, len) : std::max (0LL, pos);
        setAudioPlayhead (target); // the audio timeline follows the park
        if (playing.load())
            transportSeekA.store (target); // the audio thread lands it next block
        else
        {
            transportStartPosA.store (target);
            transportPosAtomic.store (target);
        }
    }

    // Round 2 (item 93): place the whole transport grid at an absolute
    // sample position - the tick phase, the song-chain position and the
    // play step all derive from it, so a parked start and a live seek
    // land exactly on the step grid. Audio thread only.
    void placeTransportAt (long long pos, double samplesPerTick)
    {
        transportSamples = pos;
        audioPos = pos; // the audio clock shadows the transport while it plays
        const long long ticksPerPattern = (long long) kSteps * kTicksPer16th;
        const double posInTicks = samplesPerTick > 0.0 ? (double) pos / samplesPerTick : 0.0;
        long long wholeTicks = (long long) posInTicks;
        double fracTick = posInTicks - (double) wholeTicks;
        if (fracTick > 0.9999) { ++wholeTicks; fracTick = 0.0; } // float dust at a boundary
        // The tick CONTAINING the position fires at the next sample (the
        // loop fires whenever the countdown reaches zero), so the
        // countdown starts at minus the fraction already inside the
        // tick - at an exact boundary that is 0, which is precisely the
        // old play-from-the-top behaviour (tick 0 sounds at sample 0),
        // and the phase against the sample-counted audio clips is exact.
        if (songMode.load() && songTotalBarsA.load() > 0)
        {
            // Round 4 (item 99c): park inside the ARRANGEMENT. The song
            // position is an absolute step; the window and tick fall out
            // of it, and the sounding pattern is whichever block covers
            // the parked step (lowest lane first).
            refreshRtBlocksIfNeeded();
            const long long totalSteps = (long long) songTotalBarsA.load() * 16;
            const long long stepPos = wholeTicks / kTicksPer16th;
            if (stepPos >= totalSteps)
            {
                // parked at the very end: sit on the song's final tick,
                // which fires at once and wraps the arrangement
                const long long lastStep = totalSteps - 1;
                songWindow = (int) (lastStep / kSteps);
                tick = (int) ((lastStep % kSteps) * kTicksPer16th + (kTicksPer16th - 1));
                samplesToNextTick = 0.0;
            }
            else
            {
                songWindow = (int) (stepPos / kSteps);
                tick = (int) (wholeTicks % ticksPerPattern);
                samplesToNextTick = -fracTick * samplesPerTick;
            }
            barsDone = songWindow;
            songPos = 0;
            placedFresh = true;
            const long long curStep = (long long) songWindow * kSteps + tick / kTicksPer16th;
            BlockHit hits[16];
            const int nh = resolveSongStep (rtBlocks, kMaxSongBlocks, curStep, hits, 16);
            activePattern = nh > 0 ? hits[0].pattern : editPattern.load();
            playSongPos.store (nh > 0 ? hits[0].block : -1);
            playSongBarA.store ((int) (curStep / 16));
        }
        else if (songMode.load() && songLength.load() > 0)
        {
            long long patterns = wholeTicks / ticksPerPattern;
            if (patterns >= (long long) songLength.load())
            {
                // parked at the very end of the chain: sit on its final
                // tick, which fires at once and wraps the chain
                patterns = (long long) songLength.load() - 1;
                tick = (int) (ticksPerPattern - 1);
                samplesToNextTick = 0.0;
            }
            else
            {
                tick = (int) (wholeTicks % ticksPerPattern);
                samplesToNextTick = -fracTick * samplesPerTick;
            }
            songPos = (int) patterns;
            barsDone = (int) patterns;
            activePattern = chainAt (songPos);
            playSongPos.store (songPos);
            playSongBarA.store (-1); // the bar readout belongs to the arrangement
        }
        else
        {
            tick = (int) (wholeTicks % ticksPerPattern);
            samplesToNextTick = -fracTick * samplesPerTick;
            barsDone = (int) (wholeTicks / ticksPerPattern);
            songPos = 0;
            songWindow = 0;
            activePattern = editPattern.load();
            playSongPos.store (-1);
            playSongBarA.store (-1);
        }
        playPattern.store (activePattern);
        // While stopped the play step stays -1 (no playhead); the grid
        // placement still counts for the moment PLAY is pressed.
        if (playing.load())
            playStep.store (tick / kTicksPer16th);
    }

    // ---------------- audio thread ----------------
    // vocalIn may be nullptr (no input). outL/outR are overwritten.
    void process (const float* vocalIn, float* outL, float* outR, int numSamples)
    {
        if (dirty.exchange (false))
            applyParams();

        if (stationDirty.load())
            applyStationLocked();

        std::unique_lock<std::mutex> lock (sampleMutex, std::try_to_lock);
        const bool samplesOk = lock.owns_lock();

        if (samplesOk)
            for (int p = 0; p < kPads; ++p)
            {
                const int v = padHit[p].exchange (0);
                if (v > 0) pads.trigger (p, v / 127.0f);
                // Fix pass item 31: a hit on a sequencer pad while the
                // transport plays is captured into the pattern at the
                // playhead step, with the hit's velocity. Fix pass 2: every
                // pad is a sequencer row now (rows 5..20 = pads 1..16), so
                // any pad whose row exists captures.
                const int capStep = playStep.load();
                if (v > 0 && playing.load() && ! pausedA.load() && padCaptureOn.load() && capStep >= 0
                    && Sample1 + p < numTracksA.load())
                {
                    patGrid[(size_t) gIdx (activePattern, Sample1 + p, capStep)].store ((uint8_t) v);
                    capturedPad.store (p);
                }
            }

        // Round 5 (item 105): the live-note funnel drains here, under
        // the same lock as the pad hits - the voices it fires read the
        // same sample state. (Events wait in the ring while a sample
        // swap holds the lock, exactly like a pad hit does.)
        if (samplesOk)
            drainLiveNotes();

        const double samplesPerTick = sr * 60.0 / ((double) bpm.load() * kPPQN);
        // Round 6 (item 115): PAUSE holds the transport engaged but
        // frozen - for advancement, step firing and clip playback a
        // paused transport is not playing; the playhead (playStep and
        // transportSamples) simply stops where it stands.
        const bool wantPlay = playing.load();
        const bool pausedNow = wantPlay && pausedA.load();
        const bool isPlayingNow = wantPlay && ! pausedNow;
        if (isPlayingNow && ! wasPlaying)
        {
            // Round 2 (item 93): PLAY starts where the transport is
            // PARKED (REW / FF move the park; STOP returns it to zero).
            // A RESUME from pause re-enters the grid at the frozen
            // position instead - the park belongs to fresh starts.
            placeTransportAt (wasPaused ? transportSamples : transportStartPosA.load(),
                              samplesPerTick);
            panicActive = false; panicVoicesPending = false; // a fresh start is never faded
        }
        if (! isPlayingNow && wasPlaying && ! pausedNow) playStep.store (-1);
        wasPlaying = isPlayingNow;
        wasPaused = pausedNow;

        // Round 2 (item 93): a seek lands here - live while playing,
        // and while stopped it is how STOP's return-to-zero reaches
        // the grid. The take-recording path never seeks (seekTransport
        // drops requests while recording).
        if (const long long seek = transportSeekA.exchange (-1); seek >= 0)
            placeTransportAt (seek, samplesPerTick);

        // the audio view's own transport (item 47): starting it away from
        // the main transport begins at the audio playhead (item 50's seek)
        const bool audioNow = audioPlaying.load();
        if (audioNow && ! wasAudioPlaying && ! isPlayingNow)
            audioPos = audioStartPosA.load();
        if (audioSeekPending.exchange (false) && ! isPlayingNow)
            audioPos = audioStartPosA.load();
        wasAudioPlaying = audioNow;

        // item 45: the global stop lands here - kill every voice behind a
        // short fade (started below), so the cut itself stays click-free
        if (stopAllRequested.exchange (false))
        {
            panicFadeTotal = panicFadeLeft = std::max (1, (int) (0.005 * sr));
            panicActive = true;
            silenceAllVoices (samplesOk);
        }
        if (panicVoicesPending && samplesOk)
        {
            pads.stopAll();
            rowPads.stopAll();
            panicVoicesPending = false;
        }

        // waveform audition (item 46): start/stop requests land here
        if (samplesOk && auditionStartRequested.exchange (false))
        {
            auditionVoice.data = auditionData;
            auditionVoice.rate = auditionSrcSr / sr;
            auditionVoice.pos = 0.0;
            auditionVoice.age = 0;
            auditionVoice.active = auditionVoice.data != nullptr && auditionVoice.data->size() >= 2;
        }
        if (auditionStopRequested.exchange (false))
            auditionVoice.active = false;

        const int activeTracks = numTracksA.load();

        const float swingNow = swing.load();
        const bool useDust = dustOn.load(), useGlue = glueOn.load(), useVocal = vocalOn.load() && vocalIn != nullptr;
        const bool wantVocal = vocalIn != nullptr && (useVocal || recording.load() || recPending.load());
        bool cap = recording.load();
        const float master = std::pow (10.0f, masterDb.load() / 20.0f);

        // Pass 2: the Station's UNIFIED solo (item 20's rule) - a solo on
        // ANY channel (a row, an instrument, an audio track, the vocal
        // channel) silences every channel that is not itself soloed.
        // Before this pass the rows and the audio tracks soloed in two
        // separate worlds; one mixer means one solo logic.
        bool anyAudioSoloNow = false;
        for (int t = 0; t < kAudioTracks; ++t) anyAudioSoloNow = anyAudioSoloNow || audioSoloA[(size_t) t].load();
        const bool soloActive = anySoloActive() || anyAudioSoloNow || synthSoloRt || pianoSoloRt || vocalSoloRt;
        // Round 2 (item 95): the GATE (mute/solo) and the LEVEL (fader)
        // are separate factors. The gate lands instantly, as it always
        // has - a mute is a deliberate cut, and Fix Pass 2's item-60
        // measurement pins that. The LEVEL is what smooths (below):
        // fader drags used to land as block-rate steps, the "jittery"
        // sound the owner reported, and a drag is a stream of level
        // changes - never a gate flip.
        float gains[kTracks];  // the gate: 0 or 1
        float levels[kTracks]; // the fader level (the smoothing target)
        for (int t = 0; t < kTracks; ++t)
        {
            gains[t] = (trackMute[t].load() || (soloActive && ! trackSolo[t].load())) ? 0.0f : 1.0f;
            levels[t] = trackGain[t].load();
        }
        const float clickLvl = metronomeLevel.load();
        if (! isPlayingNow) clickSamplesLeft = 0;

        // Audio tracks (Fix pass item 35; item 60 made them many). Clips
        // live on the audio timeline, which is not fenced by the pattern.
        float aGain[kAudioTracks];  // the gate
        float aLevel[kAudioTracks]; // the level
        for (int t = 0; t < kAudioTracks; ++t)
        {
            aGain[t] = (audioMuteA[(size_t) t].load()
                        || (soloActive && ! audioSoloA[(size_t) t].load())) ? 0.0f : 1.0f;
            aLevel[t] = audioGainA[(size_t) t].load();
        }
        // The group + instrument channel gates for this block. The synth
        // and piano levels live inside their voices (their channel faders
        // ARE those levels), so here they only get the mute/solo gate.
        bool anyPadRowSolo = false;
        for (int t = Sample1; t < Sample1 + kPads; ++t) anyPadRowSolo = anyPadRowSolo || trackSolo[t].load();
        const float synthGate = (! synthMuteRt && (! soloActive || synthSoloRt)) ? 1.0f : 0.0f;
        const float pianoGate = (! pianoMuteRt && (! soloActive || pianoSoloRt)) ? 1.0f : 0.0f;
        const float vocalGate = (! vocalMuteRt && (! soloActive || vocalSoloRt)) ? 1.0f : 0.0f;
        const float padBusGate = (! padBusMuteRt && (! soloActive || anyPadRowSolo)) ? 1.0f : 0.0f;
        const int analysedCh = analysedChannel.load();
        int anaW = analyserWrite.load();
        float chanPkL[kNumChannels] = {}, chanPkR[kNumChannels] = {};
        float masterPkL = 0.0f, masterPkR = 0.0f;
        const bool useClips = samplesOk && audioClipCount() > 0;
        unsigned int clipMask = 0; // clips sounding this block (item 54)
        // Item 36: the take records the CLEAN input - input-gain staging only.
        // The mic emulation is a playback treatment (see takeVocal below).
        const float inGainLin = std::pow (10.0f, vocalInputDb.load() / 20.0f);

        // Round 2 (item 95): channel LEVELS are SMOOTHED on the audio
        // thread (a ~15 ms one-pole) - a fader drag used to land as
        // block-rate steps, the "jittery" sound the owner reported on
        // the board. The states initialise to the targets on the first
        // block after prepare(), so steady renders (the plugin
        // measurement path included) stay bit-identical; only a level
        // in motion ramps. The mute/solo GATE (gains[]/aGain[] above)
        // is not smoothed - it cuts instantly, as it always has.
        if (! gainSmInit)
        {
            for (int t = 0; t < kTracks; ++t) rowGainSm[(size_t) t] = levels[t];
            for (int t = 0; t < kAudioTracks; ++t) audioGainSm[(size_t) t] = aLevel[t];
            vocalGainSm = vocalGainRt;
            padBusGainSm = padBusGainRt;
            masterGainSm = master;
            gainSmInit = true;
        }
        const float gainSmCoef = 1.0f - std::exp (-1.0f / (0.015f * (float) (sr > 0.0 ? sr : 44100.0)));

        float pk[kTracks] = {};
        float padPk = 0.0f;
        int padPkIdx = -1;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int t = 0; t < kTracks; ++t)
                rowGainSm[(size_t) t] += (levels[t] - rowGainSm[(size_t) t]) * gainSmCoef;
            for (int t = 0; t < kAudioTracks; ++t)
                audioGainSm[(size_t) t] += (aLevel[t] - audioGainSm[(size_t) t]) * gainSmCoef;
            vocalGainSm += (vocalGainRt - vocalGainSm) * gainSmCoef;
            padBusGainSm += (padBusGainRt - padBusGainSm) * gainSmCoef;
            masterGainSm += (master - masterGainSm) * gainSmCoef;

            if (isPlayingNow)
            {
                if (samplesToNextTick <= 0.0)
                {
                    if (tick == 0)
                    {
                        // bar line: advance the arrangement window, the
                        // song chain, or follow the edited pattern
                        if (songMode.load() && songTotalBarsA.load() > 0)
                        {
                            // Round 4 (item 99c): the transport counts
                            // 32-step windows across the arrangement;
                            // fireTick resolves the blocks per step.
                            refreshRtBlocksIfNeeded();
                            const int totalWindows = (songTotalBarsA.load() * 16 + kSteps - 1) / kSteps;
                            if (barsDone > 0 && ! placedFresh && totalWindows > 1)
                                songWindow = (songWindow + 1) % totalWindows;
                            BlockHit hits[16];
                            const int nh = resolveSongStep (rtBlocks, kMaxSongBlocks,
                                                            (long long) songWindow * kSteps, hits, 16);
                            activePattern = nh > 0 ? hits[0].pattern : editPattern.load();
                            playSongPos.store (nh > 0 ? hits[0].block : -1);
                        }
                        else if (songMode.load() && songLength.load() > 0)
                        {
                            if (barsDone > 0 && songLength.load() > 1)
                                songPos = (songPos + 1) % songLength.load();
                            activePattern = chainAt (songPos);
                            playSongPos.store (songPos);
                        }
                        else
                        {
                            activePattern = editPattern.load();
                            playSongPos.store (-1);
                        }
                        playPattern.store (activePattern);
                        ++barsDone;
                    }
                    if (tick == 0 && recPending.exchange (false))
                    {
                        recording.store (true);
                        cap = true;
                        recLen = 0;
                        takeStartPosA.store (transportSamples);
                    }
                    fireTick (tick, swingNow, samplesOk, samplesPerTick);
                    ++tick;
                    if (songMode.load() && songTotalBarsA.load() > 0)
                    {
                        // Round 4 (item 99c): the song wraps at its TRUE
                        // end, which can fall mid-window (a 5-bar song
                        // ends at step 80, inside window 3) - the next
                        // tick becomes the wrap branch's tick 0.
                        const long long nextStep = (long long) songWindow * kSteps
                                                 + tick / kTicksPer16th;
                        if (tick >= kSteps * kTicksPer16th
                            || nextStep >= (long long) songTotalBarsA.load() * 16)
                            tick = 0;
                    }
                    else tick %= (kSteps * kTicksPer16th);
                    samplesToNextTick += samplesPerTick;
                }
                samplesToNextTick -= 1.0;
            }

            // ---- sources, one per channel (Pass 2 routing) ----
            // Drums: a row bound to a sample (item 40) sounds its one-shot
            // voice instead of the synthesized drum (which then never
            // triggers, so its process() just returns silence).
            float rowSig[kTracks] = {};
            float kv = kick.process(), snv = snare.process(), cv = clap.process();
            const float hv = hat.process();
            if (kitVoices[Kick].active)  kv  = renderOneShot (kitVoices[Kick]);
            if (kitVoices[Snare].active) snv = renderOneShot (kitVoices[Snare]);
            if (kitVoices[Clap].active)  cv  = renderOneShot (kitVoices[Clap]);
            rowSig[Kick] += kv;
            rowSig[Snare] += snv;
            rowSig[Clap] += cv;
            if (kitVoices[ClosedHat].active || kitVoices[OpenHat].active)
            {
                for (int ht : { ClosedHat, OpenHat })
                    if (kitVoices[(size_t) ht].active)
                        rowSig[ht] += renderOneShot (kitVoices[(size_t) ht]);
            }
            else rowSig[hatTrack] += hv;
            const float synthSig = synth.process();
            const float pianoSig = piano.process();
            float auditionSig = 0.0f;
            float loosePadSig = 0.0f;

            if (samplesOk)
            {
                // Round 6 (item 109): the ROW store sounds through the
                // rows' own channels; the PAD bank is its own world -
                // every pad voice sums onto the pad bus, and the peak
                // tracker follows real pad sound only.
                for (int p = 0; p < kPads; ++p)
                {
                    const int row = Sample1 + p;
                    if (row < activeTracks) rowSig[row] += rowPads.processPad (p);
                }
                for (int p = 0; p < kPads; ++p)
                {
                    const float pv = pads.processPad (p);
                    loosePadSig += pv;
                    if (std::abs (pv) > padPk) { padPk = std::abs (pv); padPkIdx = p; }
                }
                // rows past the pad bank carry their own sample (item 39)
                for (int t = kFirstOwnRow; t < activeTracks; ++t)
                {
                    auto& v = ownVoices[(size_t) (t - kFirstOwnRow)];
                    if (v.active) rowSig[t] += renderOneShot (v);
                }
                // the waveform panel's audition (item 46)
                if (auditionVoice.active)
                    auditionSig = renderOneShot (auditionVoice);
            }

            // ---- channel stage: strip -> fader -> pan -> bus ----
            float beatL = 0.0f, beatR = 0.0f, padBusMono = 0.0f;
            for (int t = 0; t < activeTracks; ++t)
            {
                float s = rowSig[t];
                if (stripActiveRt[(size_t) t]) s = stripDsp[(size_t) t].process (s);
                s *= rowGainSm[(size_t) t] * gains[t]; // item 95: level smoothed, gate instant
                pk[t] = std::max (pk[t], std::abs (s));
                float gl, gr; panLaw (chanPanRt[(size_t) t], gl, gr);
                chanPkL[t] = std::max (chanPkL[t], std::abs (s * gl));
                chanPkR[t] = std::max (chanPkR[t], std::abs (s * gr));
                if (t == analysedCh) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = s;
                if (isPadRow (t))
                {
                    padBusMono += s; // pad rows feed the PAD BUS (item 7+20)
                    if (t >= Sample1 && t < Sample1 + kPads && std::abs (s) > padPk)
                    { padPk = std::abs (s); padPkIdx = t - Sample1; }
                }
                else { beatL += s * gl; beatR += s * gr; }
            }
            {
                // the pad bus: the pad rows' group channel (its strip,
                // its fader, its pan) - one fader for the whole pad set.
                // Round 6 fix: the LOOSE pads (item 109's detached pad
                // store) join the bus here too - the first cut summed
                // them straight into the beat, bypassing this strip,
                // its fader and its peak (the pad bank's meter, item
                // 112b, reads that peak and heard nothing).
                float s = padBusMono + loosePadSig;
                if (stripActiveRt[kPadBusChannel]) s = stripDsp[kPadBusChannel].process (s);
                s *= padBusGainSm * padBusGate; // item 95: level smoothed, gate instant
                float gl, gr; panLaw (chanPanRt[kPadBusChannel], gl, gr);
                beatL += s * gl; beatR += s * gr;
                chanPkL[kPadBusChannel] = std::max (chanPkL[kPadBusChannel], std::abs (s * gl));
                chanPkR[kPadBusChannel] = std::max (chanPkR[kPadBusChannel], std::abs (s * gr));
                if (analysedCh == kPadBusChannel) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = s;
            }
            {
                float s = synthSig;
                if (stripActiveRt[kSynthChannel]) s = stripDsp[kSynthChannel].process (s);
                s *= synthGate;
                float gl, gr; panLaw (chanPanRt[kSynthChannel], gl, gr);
                beatL += s * gl; beatR += s * gr;
                chanPkL[kSynthChannel] = std::max (chanPkL[kSynthChannel], std::abs (s * gl));
                chanPkR[kSynthChannel] = std::max (chanPkR[kSynthChannel], std::abs (s * gr));
                if (analysedCh == kSynthChannel) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = s;
            }
            {
                float s = pianoSig;
                if (stripActiveRt[kPianoChannel]) s = stripDsp[kPianoChannel].process (s);
                s *= pianoGate;
                float gl, gr; panLaw (chanPanRt[kPianoChannel], gl, gr);
                beatL += s * gl; beatR += s * gr;
                chanPkL[kPianoChannel] = std::max (chanPkL[kPianoChannel], std::abs (s * gl));
                chanPkR[kPianoChannel] = std::max (chanPkR[kPianoChannel], std::abs (s * gr));
                if (analysedCh == kPianoChannel) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = s;
            }
            beatL += auditionSig;
            beatR += auditionSig;

            // Dust colours the whole beat bus, as it always has - now in
            // stereo, one crusher per side, same settings.
            if (useDust) { beatL = dust.process (beatL); beatR = dustR.process (beatR); }

            float vocalSample = 0.0f;
            if (wantVocal) vocalSample = vocal.process (vocalIn[i]);

            if (cap && samplesOk && recLen < (int) recData.size())
            {
                // Item 36: store the clean input (gain staging only) - never
                // the monitored, mic-emulated signal.
                recData[(size_t) recLen++] = vocalIn != nullptr ? vocalIn[i] * inGainLin : 0.0f;
                if (recLen >= (int) recData.size()) recording.store (false); // length cap
            }

            float masterL = beatL, masterR = beatR;
            {
                // The VOCAL channel (items 26/32/36): the live monitor and
                // the stored take's raw loop sum here, run through the
                // take's playback treatment (item 36: the mic emulation is
                // a stored playback setting), then this channel's strip.
                float s = 0.0f;
                if (useVocal) s += vocalSample;
                if (samplesOk && takeOn.load() && takeLenAtomic.load() > 0 && isPlayingNow)
                {
                    const long long rel = transportSamples - takeStartPosA.load();
                    if (rel >= 0) s += takeVocal.process (takeData[(size_t) (rel % takeLenAtomic.load())]);
                }
                if (stripActiveRt[kVocalChannel]) s = stripDsp[kVocalChannel].process (s);
                s *= vocalGainSm * vocalGate; // item 95: level smoothed, gate instant
                float gl, gr; panLaw (chanPanRt[kVocalChannel], gl, gr);
                masterL += s * gl; masterR += s * gr;
                chanPkL[kVocalChannel] = std::max (chanPkL[kVocalChannel], std::abs (s * gl));
                chanPkR[kVocalChannel] = std::max (chanPkR[kVocalChannel], std::abs (s * gr));
                if (analysedCh == kVocalChannel) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = s;
            }

            // Audio tracks: clips sum per track (item 60), run through
            // that track's own mic treatment + strip, then its fader/pan.
            // Fix pass 2 (item 48): the audio timeline is not fenced by
            // the pattern loop - it runs while either transport runs.
            if (useClips && (isPlayingNow || audioNow))
            {
                float clipSig[kAudioTracks] = {};
                bool clipHit[kAudioTracks] = {};
                for (int ci = 0; ci < kMaxAudioClips; ++ci)
                {
                    const auto& c = clipsA[(size_t) ci];
                    if (! c.active.load()) continue;
                    const int tr = std::clamp (c.track.load(), 0, kAudioTracks - 1);
                    if (aGain[(size_t) tr] <= 0.0f) continue; // the gate: a muted track's clips do not render
                    const long long pos = audioPos - c.start.load();
                    if (pos < 0 || pos >= c.length.load()) continue;
                    const auto& data = clipData[(size_t) ci];
                    const long long src = c.srcStart.load() + pos;
                    if (src >= 0 && src < (long long) data.size())
                    {
                        clipSig[tr] += data[(size_t) src];
                        clipHit[tr] = true;
                        clipMask |= (1u << ci);
                    }
                }
                for (int t = 0; t < kAudioTracks; ++t)
                {
                    if (! clipHit[t]) continue;
                    const int ch = kAudioChannelBase + t;
                    float s = clipChains[(size_t) t].process (clipSig[t]);
                    if (stripActiveRt[(size_t) ch]) s = stripDsp[(size_t) ch].process (s);
                    s *= audioGainSm[(size_t) t] * aGain[(size_t) t]; // item 95: level smoothed, gate instant
                    float gl, gr; panLaw (chanPanRt[(size_t) ch], gl, gr);
                    masterL += s * gl; masterR += s * gr;
                    chanPkL[ch] = std::max (chanPkL[ch], std::abs (s * gl));
                    chanPkR[ch] = std::max (chanPkR[ch], std::abs (s * gr));
                    if (analysedCh == ch) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = s;
                }
            }

            if (isPlayingNow) { ++transportSamples; audioPos = transportSamples; }
            else if (audioNow) ++audioPos;

            float l = masterL, r = masterR;
            if (useGlue) bus.process (l, r);
            // Pass 2: the master limiter sits after the Glue - the last
            // stage before the master fader's tanh (its distinct mastering
            // LOOK is Pass 6; the DSP lands now).
            if (masterLimiterOnRt) masterLimiter.processStereo (l, r);
            masterPkL = std::max (masterPkL, std::abs (l));
            masterPkR = std::max (masterPkR, std::abs (r));
            if (analysedCh == kMasterChannel) analyserRing[(size_t) (anaW++ & (kAnalyserSize - 1))] = (l + r) * 0.5f;

            if (clickSamplesLeft > 0) // metronome click burst (post-glue, pre-master)
            {
                const double dur = 0.03 * sr;
                const float env = (float) (clickSamplesLeft / dur);
                const float c = (float) std::sin (clickPhase) * env * env * 0.6f * clickLvl;
                clickPhase += 6.283185307179586 * clickFreq / sr;
                if (clickPhase > 6.283185307179586) clickPhase -= 6.283185307179586;
                l += c; r += c;
                --clickSamplesLeft;
            }

            // item 45: the panic fade - output ramps to silence over ~5 ms
            // from the moment the stop lands, and holds at zero while any
            // voice kill is still waiting on the sample lock
            float outGain = 1.0f;
            if (panicActive)
            {
                if (panicFadeLeft > 0)
                {
                    outGain = (float) panicFadeLeft / (float) panicFadeTotal;
                    --panicFadeLeft;
                }
                else if (panicVoicesPending) outGain = 0.0f;
                else panicActive = false;
            }
            outL[i] = std::tanh (l * masterGainSm) * outGain; // item 95: smoothed
            outR[i] = std::tanh (r * masterGainSm) * outGain;
        }
        for (int t = 0; t < kTracks; ++t)
            if (pk[t] > trackPeak[t].load()) trackPeak[t].store (pk[t]);

        if (padPk > padActivityPeak.load())
        {
            padActivityPeak.store (padPk);
            lastPadIndex.store (padPkIdx);
        }

        if (cap) recPublished.store (recLen);

        transportPosAtomic.store (transportSamples);
        audioPosAtomic.store (audioPos);
        soundingClipMaskA.store (clipMask);
        auditionSounding.store (auditionVoice.active);
        // Peaks decay in the engine (per block) so EVERY reader can peek:
        // the Station VU and the mixing board watch the same channels and
        // must never starve each other the way consume-on-read would.
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            chanPeakL[(size_t) ch].store (std::max (chanPkL[ch], chanPeakL[(size_t) ch].load() * 0.96f));
            chanPeakR[(size_t) ch].store (std::max (chanPkR[ch], chanPeakR[(size_t) ch].load() * 0.96f));
            chanCompGr[(size_t) ch].store (stripActiveRt[(size_t) ch] ? stripDsp[(size_t) ch].getCompGrDb() : 0.0f);
            chanDeessGr[(size_t) ch].store (stripActiveRt[(size_t) ch] ? stripDsp[(size_t) ch].getDeessGrDb() : 0.0f);
            chanLimGr[(size_t) ch].store (stripActiveRt[(size_t) ch] ? stripDsp[(size_t) ch].getLimiterGrDb() : 0.0f);
        }
        masterPeakL.store (std::max (masterPkL, masterPeakL.load() * 0.96f));
        masterPeakR.store (std::max (masterPkR, masterPeakR.load() * 0.96f));
        masterLimGr.store (masterLimiterOnRt ? masterLimiter.getGainReductionDb() : 0.0f);
        analyserWrite.store (anaW);
        glueGr.store (useGlue ? bus.getGainReductionDb() : 0.0f);
    }

    // ---- Pass 2: the Station API ----
    // The settings-side station state (strips, pans, group channels, the
    // master limiter) lives under stationMutex; the audio thread picks it
    // up through stationDirty. These accessors are the UI's only door.
    StripSettings getStripSettings (int ch) const
    {
        std::lock_guard<std::mutex> lock (stationMutex);
        return (ch >= 0 && ch < kNumChannels) ? station.strips[(size_t) ch] : StripSettings();
    }
    void setStripSettings (int ch, const StripSettings& s)
    {
        if (ch < 0 || ch >= kNumChannels) return;
        {
            std::lock_guard<std::mutex> lock (stationMutex);
            station.strips[(size_t) ch] = s;
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
    }
    float getChannelPan (int ch) const
    {
        if (ch < 0 || ch >= kNumChannels) return 0.0f;
        std::lock_guard<std::mutex> lock (stationMutex);
        return station.pan[(size_t) ch];
    }
    void setChannelPan (int ch, float pan)
    {
        if (ch < 0 || ch >= kNumChannels) return;
        {
            std::lock_guard<std::mutex> lock (stationMutex);
            station.pan[(size_t) ch] = std::clamp (pan, -1.0f, 1.0f);
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
    }

    // One gain/mute/solo surface for every channel kind - the board and
    // the Station read and write through these, never around them.
    float getChannelGain (int ch) const
    {
        switch (channelKind (ch))
        {
            case KindRow:    return getTrackGain (ch);
            case KindSynth:  return synthLevel.load();
            case KindPiano:  return pianoLevel.load();
            case KindPadBus: { std::lock_guard<std::mutex> lock (stationMutex); return station.padBusGain; }
            case KindAudio:  return getAudioTrackGain (ch - kAudioChannelBase);
            case KindVocal:  { std::lock_guard<std::mutex> lock (stationMutex); return station.vocalGain; }
            default:         return dbToGain (masterDb.load());
        }
    }
    void setChannelGain (int ch, float g)
    {
        switch (channelKind (ch))
        {
            case KindRow:    setTrackGain (ch, g); break;
            case KindSynth:  setSynthLevel (g); break;
            case KindPiano:  setPianoLevel (g); break;
            case KindPadBus: { std::lock_guard<std::mutex> lock (stationMutex);
                               station.padBusGain = std::clamp (g, 0.0f, 1.2f); }
                             stationDirty.store (true); stationRevision.fetch_add (1); break;
            case KindAudio:  setAudioTrackGain (ch - kAudioChannelBase, g); break;
            case KindVocal:  { std::lock_guard<std::mutex> lock (stationMutex);
                               station.vocalGain = std::clamp (g, 0.0f, 1.2f); }
                             stationDirty.store (true); stationRevision.fetch_add (1); break;
            default:         setMasterDb (std::max (-60.0f, 20.0f * std::log10 (std::max (g, 0.0001f)))); break;
        }
    }
    bool getChannelMute (int ch) const
    {
        switch (channelKind (ch))
        {
            case KindRow:    return getTrackMute (ch);
            case KindSynth:  { std::lock_guard<std::mutex> lock (stationMutex); return station.synthMute; }
            case KindPiano:  { std::lock_guard<std::mutex> lock (stationMutex); return station.pianoMute; }
            case KindPadBus: { std::lock_guard<std::mutex> lock (stationMutex); return station.padBusMute; }
            case KindAudio:  return getAudioTrackMute (ch - kAudioChannelBase);
            case KindVocal:  { std::lock_guard<std::mutex> lock (stationMutex); return station.vocalMute; }
            default:         return false;
        }
    }
    void setChannelMute (int ch, bool m)
    {
        switch (channelKind (ch))
        {
            case KindRow:    setTrackMute (ch, m); return;
            case KindAudio:  setAudioTrackMute (ch - kAudioChannelBase, m); return;
            case KindSynth:  { std::lock_guard<std::mutex> lock (stationMutex); station.synthMute = m; } break;
            case KindPiano:  { std::lock_guard<std::mutex> lock (stationMutex); station.pianoMute = m; } break;
            case KindPadBus: { std::lock_guard<std::mutex> lock (stationMutex); station.padBusMute = m; } break;
            case KindVocal:  { std::lock_guard<std::mutex> lock (stationMutex); station.vocalMute = m; } break;
            default: return;
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
    }
    bool getChannelSolo (int ch) const
    {
        switch (channelKind (ch))
        {
            case KindRow:    return getTrackSolo (ch);
            case KindSynth:  { std::lock_guard<std::mutex> lock (stationMutex); return station.synthSolo; }
            case KindPiano:  { std::lock_guard<std::mutex> lock (stationMutex); return station.pianoSolo; }
            case KindAudio:  return getAudioTrackSolo (ch - kAudioChannelBase);
            case KindVocal:  { std::lock_guard<std::mutex> lock (stationMutex); return station.vocalSolo; }
            default:         return false; // the pad bus and master carry no solo
        }
    }
    void setChannelSolo (int ch, bool s)
    {
        switch (channelKind (ch))
        {
            case KindRow:    setTrackSolo (ch, s); return;
            case KindAudio:  setAudioTrackSolo (ch - kAudioChannelBase, s); return;
            case KindSynth:  { std::lock_guard<std::mutex> lock (stationMutex); station.synthSolo = s; } break;
            case KindPiano:  { std::lock_guard<std::mutex> lock (stationMutex); station.pianoSolo = s; } break;
            case KindVocal:  { std::lock_guard<std::mutex> lock (stationMutex); station.vocalSolo = s; } break;
            default: return;
        }
        stationDirty.store (true);
        stationRevision.fetch_add (1);
    }

    bool getMasterLimiterOn() const { std::lock_guard<std::mutex> lock (stationMutex); return station.masterLimiterOn; }
    void setMasterLimiterOn (bool on)
    {
        { std::lock_guard<std::mutex> lock (stationMutex); station.masterLimiterOn = on; }
        stationDirty.store (true); stationRevision.fetch_add (1);
    }
    float getMasterLimiterCeilingDb() const { std::lock_guard<std::mutex> lock (stationMutex); return station.masterLimiterCeilingDb; }
    void setMasterLimiterCeilingDb (float db)
    {
        { std::lock_guard<std::mutex> lock (stationMutex); station.masterLimiterCeilingDb = std::clamp (db, -12.0f, 0.0f); }
        stationDirty.store (true); stationRevision.fetch_add (1);
    }
    int stationRevisionCount() const { return stationRevision.load(); }

    // Per-channel meters (post-fader peaks + the strip's gain reduction).
    float consumeChannelPeak (int ch, bool right)
    {
        if (ch < 0 || ch > kMasterChannel) return 0.0f;
        auto& a = (ch == kMasterChannel) ? (right ? masterPeakR : masterPeakL)
                                         : (right ? chanPeakR[(size_t) ch] : chanPeakL[(size_t) ch]);
        return a.exchange (0.0f);
    }
    float peekChannelPeak (int ch, bool right) const
    {
        if (ch < 0 || ch > kMasterChannel) return 0.0f;
        if (ch == kMasterChannel) return right ? masterPeakR.load() : masterPeakL.load();
        return right ? chanPeakR[(size_t) ch].load() : chanPeakL[(size_t) ch].load();
    }
    float getChannelCompGrDb (int ch) const   { return (ch >= 0 && ch < kNumChannels) ? chanCompGr[(size_t) ch].load() : 0.0f; }
    float getChannelDeessGrDb (int ch) const  { return (ch >= 0 && ch < kNumChannels) ? chanDeessGr[(size_t) ch].load() : 0.0f; }
    float getChannelLimGrDb (int ch) const    { return (ch >= 0 && ch < kNumChannels) ? chanLimGr[(size_t) ch].load() : 0.0f; }
    float getGlueGrDb() const                 { return glueGr.load(); }
    float getMasterLimiterGrDb() const        { return masterLimGr.load(); }

    // The graphic EQ's analyser: the selected channel's post-fader mono.
    void setAnalysedChannel (int ch) { analysedChannel.store (std::clamp (ch, 0, kMasterChannel)); }
    int analysedChannelId() const { return analysedChannel.load(); }
    int copyAnalyserSamples (float* out, int maxN) const
    {
        const int n = std::min (maxN, kAnalyserSize);
        const int w = analyserWrite.load();
        for (int i = 0; i < n; ++i)
            out[i] = analyserRing[(size_t) ((w - n + i) & (kAnalyserSize - 1))];
        return n;
    }

private:
    static float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    // Constant-power pan with sqrt(2) centre compensation: centre keeps
    // unity gain (existing mixes don't move level), a hard pan puts
    // sqrt(2) on one side - the standard constant-power trade.
    static void panLaw (float pan, float& gl, float& gr)
    {
        const float a = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.78539816339f; // 0..pi/2
        gl = std::cos (a) * 1.41421356f;
        gr = std::sin (a) * 1.41421356f;
    }

    static StripChain::Params toChainParams (const StripSettings& s)
    {
        StripChain::Params cp;
        cp.preampOn = s.preampOn; cp.preampDb = s.preampDb;
        cp.hpfOn = s.hpfOn; cp.hpfHz = s.hpfHz;
        cp.deessOn = s.deessOn; cp.deessAmount = s.deessAmount;
        cp.compOn = s.compOn; cp.compThresholdDb = s.compThresholdDb;
        cp.compRatio = s.compRatio; cp.compAttackMs = s.compAttackMs; cp.compReleaseMs = s.compReleaseMs;
        cp.satOn = s.satOn; cp.satDrive = s.satDrive;
        cp.eq.numBands = s.eqNumBands;
        for (int i = 0; i < StripSettings::kEqBands; ++i)
        {
            cp.eq.bands[i].on = s.eq[i].on;
            cp.eq.bands[i].type = (GraphicEq::Type) std::clamp (s.eq[i].type, 0, 4);
            cp.eq.bands[i].freqHz = s.eq[i].freqHz;
            cp.eq.bands[i].gainDb = s.eq[i].gainDb;
            cp.eq.bands[i].q = s.eq[i].q;
        }
        cp.limiterOn = s.limiterOn; cp.limiterCeilingDb = s.limiterCeilingDb;
        return cp;
    }

    // Audio thread, from process(): picks up settings-side station edits.
    // try_lock like the sample path - a missed block just retries.
    void applyStationLocked()
    {
        std::unique_lock<std::mutex> lock (stationMutex, std::try_to_lock);
        if (! lock.owns_lock()) return;
        stationDirty.store (false);
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            stripDsp[(size_t) ch].setParams (toChainParams (station.strips[(size_t) ch]));
            stripActiveRt[(size_t) ch] = stripDsp[(size_t) ch].isActive();
            chanPanRt[(size_t) ch] = station.pan[(size_t) ch];
        }
        padBusGainRt = station.padBusGain; padBusMuteRt = station.padBusMute;
        vocalGainRt = station.vocalGain;   vocalMuteRt = station.vocalMute; vocalSoloRt = station.vocalSolo;
        synthMuteRt = station.synthMute;   synthSoloRt = station.synthSolo;
        pianoMuteRt = station.pianoMute;   pianoSoloRt = station.pianoSolo;
        masterLimiterOnRt = station.masterLimiterOn;
        {
            Limiter::Params lp;
            lp.ceilingDb = station.masterLimiterCeilingDb;
            masterLimiter.setParams (lp);
        }
    }

    void fireTick (int t, float swingPercent, bool samplesOk, double samplesPerTick)
    {
        placedFresh = false; // consumed: the park's own tick-0 has passed
        playStep.store (t / kTicksPer16th);
        // Round 4 (item 99c): in song mode with an arrangement, each step
        // resolves the blocks covering it instead of following the one
        // active pattern. The RT block snapshot refreshes here, once
        // per tick, only when the UI changed something.
        const bool arrange = songMode.load() && songTotalBarsA.load() > 0;
        if (arrange)
        {
            refreshRtBlocksIfNeeded();
            playSongBarA.store ((songWindow * kSteps + t / kTicksPer16th) / 16);
        }
        for (int s = 0; s < kSteps; ++s)
        {
            const int base = s * kTicksPer16th;
            if (base + swingOffsetTicks (base, kTicksPer16th, swingPercent) != t)
                continue;
            if (metronomeOn.load() && (s % 4) == 0) // beat step: fire the click
            {
                clickFreq = (s == 0) ? 2000.0 : 1500.0;
                clickPhase = 0.0;
                clickSamplesLeft = (int) (0.03 * sr);
            }
            const int activeTracks = numTracksA.load();
            if (arrange)
            {
                const long long songStep = (long long) songWindow * kSteps + s;
                if (songStep >= (long long) songTotalBarsA.load() * 16) continue;
                BlockHit hits[16];
                const int nh = resolveSongStep (rtBlocks, kMaxSongBlocks, songStep, hits, 16);
                if (nh == 0) continue;
                // rows: the max velocity across hits, one trigger per row
                for (int tr = 0; tr < activeTracks; ++tr)
                {
                    int v = 0;
                    for (int h = 0; h < nh; ++h)
                        v = std::max (v, (int) patGrid[(size_t) gIdx (hits[h].pattern, tr, hits[h].localStep)].load());
                    if (v > 0) trigger (tr, v / 127.0f, samplesOk);
                }
                // the monophonic lanes: the lowest lane with a note wins
                for (int h = 0; h < nh; ++h)
                {
                    const int mv = patMelody[(size_t) hits[h].pattern * kSteps + (size_t) hits[h].localStep].load();
                    if (mv == 0) continue;
                    const float semitones = (float) (mv - 25);
                    const int src = patMelodySource[(size_t) hits[h].pattern].load();
                    if (src == 0) kick.triggerPitched (0.9f, semitones);
                    else if (samplesOk) rowPads.triggerWithTune (src - 1, 0.9f, semitones);
                    break;
                }
                for (int h = 0; h < nh; ++h)
                {
                    const int sv = patSynth[(size_t) hits[h].pattern * kSteps + (size_t) hits[h].localStep].load();
                    if (sv == 0) continue;
                    synth.noteOn ((float) (sv - 25), 0.9f,
                                  (int) (samplesPerTick * kTicksPer16th * 0.9));
                    break;
                }
                for (int h = 0; h < nh; ++h)
                {
                    const int pv = patPiano[(size_t) hits[h].pattern * kSteps + (size_t) hits[h].localStep].load();
                    if (pv == 0) continue;
                    piano.noteOn ((float) (pv - 25), 0.9f);
                    break;
                }
                continue;
            }
            for (int tr = 0; tr < activeTracks; ++tr)
            {
                const int v = patGrid[(size_t) gIdx (activePattern, tr, s)].load();
                if (v > 0) trigger (tr, v / 127.0f, samplesOk);
            }

            const int mv = patMelody[(size_t) activePattern * kSteps + s].load();
            if (mv != 0)
            {
                const float semitones = (float) (mv - 25);
                const int src = patMelodySource[activePattern].load();
                if (src == 0) kick.triggerPitched (0.9f, semitones);
                else if (samplesOk) rowPads.triggerWithTune (src - 1, 0.9f, semitones);
            }

            const int sv = patSynth[(size_t) activePattern * kSteps + s].load();
            if (sv != 0)
                synth.noteOn ((float) (sv - 25), 0.9f,
                              (int) (samplesPerTick * kTicksPer16th * 0.9)); // gate: 90% of the step

            const int pv = patPiano[(size_t) activePattern * kSteps + s].load();
            if (pv != 0)
                piano.noteOn ((float) (pv - 25), 0.9f);
        }
    }

    // One one-shot sample voice (kit rows, own-buffer rows, the audition):
    // a shared buffer played once with the pads' click-free edge fades.
    struct OneShotVoice
    {
        std::shared_ptr<const std::vector<float>> data;
        double pos = 0.0, rate = 1.0;
        float vel = 1.0f;
        int age = 0;
        bool active = false;
    };

    float renderOneShot (OneShotVoice& v)
    {
        if (! v.active || v.data == nullptr) return 0.0f;
        const auto& d = *v.data;
        const double remaining = (double) d.size() - v.pos;
        if (remaining <= 0.0) { v.active = false; return 0.0f; }
        const int i0 = std::clamp ((int) v.pos, 0, (int) d.size() - 1);
        const int i1 = std::min (i0 + 1, (int) d.size() - 1);
        const float frac = (float) (v.pos - (double) i0);
        const float s = d[(size_t) i0] + (d[(size_t) i1] - d[(size_t) i0]) * frac;
        const float fadeIn  = std::min (1.0f, (float) v.age / 64.0f);
        const float fadeOut = (float) std::min (1.0, remaining / (64.0 * std::max (0.05, v.rate)));
        const float out = s * v.vel * fadeIn * fadeOut;
        v.pos += v.rate;
        ++v.age;
        return out;
    }

    // Starts a kit row's sample voice; true when the row is bound to a
    // sample (the synthesized drum then stays silent for this hit).
    bool tryKitVoice (int track, float vel, bool samplesOk)
    {
        if (! kitBoundA[(size_t) track].load()) return false;
        if (samplesOk && kitData[(size_t) track] != nullptr)
        {
            auto& v = kitVoices[(size_t) track];
            v.data = kitData[(size_t) track];
            v.rate = kitSrcSr[(size_t) track] / sr;
            v.pos = 0.0;
            v.age = 0;
            v.vel = std::clamp (vel, 0.0f, 1.0f);
            v.active = true;
        }
        return true;
    }

    // Item 45's voice kill, run on the audio thread when a stop lands.
    void silenceAllVoices (bool samplesOk)
    {
        kick.stop(); snare.stop(); hat.stop(); clap.stop();
        synth.stopAll(); piano.stopAll();
        for (auto& v : kitVoices) v.active = false;
        for (auto& v : ownVoices) v.active = false;
        auditionVoice.active = false;
        clickSamplesLeft = 0;
        if (samplesOk) { pads.stopAll(); rowPads.stopAll(); }
        else panicVoicesPending = true; // the fade holds silence until this lands
    }

    void trigger (int track, float vel, bool samplesOk)
    {
        switch (track)
        {
            case Kick:      if (! tryKitVoice (track, vel, samplesOk)) kick.trigger (vel); break;
            case Snare:     if (! tryKitVoice (track, vel, samplesOk)) snare.trigger (vel); break;
            case ClosedHat: if (! tryKitVoice (track, vel, samplesOk)) { hat.triggerClosed (vel); hatTrack = ClosedHat; } break; // chokes an open hat
            case OpenHat:   if (! tryKitVoice (track, vel, samplesOk)) { hat.triggerOpen (vel);   hatTrack = OpenHat; } break;
            case Clap:      if (! tryKitVoice (track, vel, samplesOk)) clap.trigger (vel); break;
            default:
                if (track >= kFirstOwnRow)
                {
                    const size_t oi = (size_t) (track - kFirstOwnRow);
                    if (samplesOk && ownBoundA[oi].load() && ownData[oi] != nullptr)
                    {
                        auto& v = ownVoices[oi];
                        v.data = ownData[oi];
                        v.rate = ownSrcSr[oi] / sr;
                        v.pos = 0.0;
                        v.age = 0;
                        v.vel = std::clamp (vel, 0.0f, 1.0f);
                        v.active = true;
                    }
                }
                else if (samplesOk) { rowPads.trigger (track - Sample1, vel); }
                break;
        }
    }

    void applyParams()
    {
        Kick808::Params kp;
        kp.tuneHz = kickTune.load(); kp.ampDecayMs = kickDecay.load();
        kick.setParams (kp);

        Snare808::Params sp;
        sp.snappy = snareSnappy.load();
        snare.setParams (sp);

        Hat808::Params hp;
        hp.openDecayMs = openHatDecay.load();
        hat.setParams (hp);

        Clap808::Params cp;
        cp.tailDecayMs = clapTail.load();
        clap.setParams (cp);

        S950Crusher::Params dp;
        dp.sampleRateHz = dustRate.load(); dp.filterQ = 1.1f;
        dust.setParams (dp);
        dustR.setParams (dp); // Pass 2: the right side of the stereo beat bus

        BusCompressor::Params bp;
        bp.thresholdDb = glueThreshold.load(); bp.ratioIndex = 0; bp.attackIndex = 4;
        bp.releaseIndex = 4; bp.makeupDb = 2.0f;
        bus.setParams (bp);

        Synth::Params yp;
        yp.wave = synthWave.load();         yp.cutoffHz = synthCutoff.load();
        yp.reso = synthReso.load();         yp.attackMs = synthAttack.load();
        yp.releaseMs = synthRelease.load(); yp.level = synthLevel.load();
        synth.setParams (yp);

        Piano::Params pp2;
        pp2.bright = pianoBright.load(); pp2.decayMs = pianoDecay.load(); pp2.level = pianoLevel.load();
        piano.setParams (pp2);

        VocalChain::Params vp = VocalChain::presetForMic ((MicType) std::clamp (vocalMic.load(), 0, 7));
        vp.mic.distanceCm = vocalDistance.load();
        vp.inputDb = vocalInputDb.load();
        vocal.setParams (vp);
        takeVocal.setParams (vp); // item 36: the take's playback treatment tracks the same settings
        // Pass 2: each audio track's clips run through that track's own
        // copy of the same mic treatment, so a clip sounds the way it did
        // summed through the one chain - per channel now, not per bus.
        for (auto& c : clipChains) c.setParams (vp);
    }

    double sr = 44100.0;

    // sound sources
    Kick808 kick; Snare808 snare; Hat808 hat; Clap808 clap;
    MpcPads pads;
    // Round 6 (item 109): the sequencer sample rows' OWN store - same
    // voice design as the pad bank, fully separate contents. Rows
    // trigger it from their steps, the melody lane and the keyboard
    // funnel; nothing in the pad bank ever reads or writes it.
    MpcPads rowPads;
    Synth synth;
    Piano piano;
    S950Crusher dust;
    BusCompressor bus;
    VocalChain vocal;
    VocalChain takeVocal; // item 36: treats the clean recorded take at playback
    S950Crusher dustR;    // Pass 2: the beat bus is stereo - one crusher per side
    std::array<VocalChain, kAudioTracks> clipChains; // Pass 2: per-track clip treatment
    std::array<StripChain, kNumChannels> stripDsp;   // Pass 2: the Station strips
    Limiter masterLimiter;                           // Pass 2: after the Glue

    // pattern + mixer state (shared with the UI thread)
    std::array<std::atomic<uint8_t>, kPatterns * kTracks * kSteps> patGrid;
    std::atomic<int> editPattern { 0 }, songLength { 0 };
    std::atomic<bool> songMode { false };
    std::atomic<int> playPattern { 0 }, playSongPos { -1 };
    int activePattern = 0, songPos = 0, barsDone = 0; // audio thread only
    static int gIdx (int p, int tr, int s) { return (p * kTracks + tr) * kSteps + s; }
    int chainAt (int i) const
    {
        return std::clamp (songChainA[(size_t) std::clamp (i, 0, kSongSlots - 1)].load(), 0, kPatterns - 1);
    }
    // Round 3 (item 88): per-row content names (see setTrackContentName).
    // Plain strings: written and read on the message thread only.
    std::array<std::string, kTracks> trackContent;
    std::array<std::string, kTracks> trackCustom;   // Round 5 (item 107): rename overrides

    std::array<std::atomic<float>, kTracks> trackGain;
    std::array<std::atomic<bool>, kTracks> trackMute;
    std::array<std::atomic<bool>, kTracks> trackSolo;
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeLevel { 0.8f };
    std::atomic<float> padActivityPeak { 0.0f };
    std::atomic<int> lastPadIndex { -1 };
    int clickSamplesLeft = 0;              // audio thread only
    double clickPhase = 0.0, clickFreq = 1500.0; // audio thread only
    std::array<std::atomic<float>, kTracks> trackPeak;
    std::array<std::atomic<int>, MpcPads::kNumPads> padHit;

    // Round 5 (item 105): the live-note funnel's ring (see the API
    // above). Single-producer (the message thread in practice) /
    // single-consumer (the audio thread) with atomic indices; 128
    // events is far past any chord a hand can play in one block.
    struct LiveNoteEvent { int channel = 0, note = 60; float velocity = 0.0f; bool on = false; };
    static constexpr int kLiveRingSize = 128;
    LiveNoteEvent liveRing[kLiveRingSize];
    std::atomic<int> liveWrite { 0 }, liveRead { 0 };
    void pushLiveNote (LiveNoteEvent e)
    {
        const int w = liveWrite.load();
        if (w - liveRead.load() >= kLiveRingSize) return; // full: drop, never block a UI
        liveRing[(size_t) (w & (kLiveRingSize - 1))] = e;
        liveWrite.store (w + 1);
    }
    void drainLiveNotes()
    {
        for (;;)
        {
            const int r = liveRead.load();
            if (r >= liveWrite.load()) break;
            const auto e = liveRing[(size_t) (r & (kLiveRingSize - 1))];
            liveRead.store (r + 1);
            fireLiveNote (e);
        }
    }
    // Audio thread, under the sample lock. Fires the channel's own
    // voice - the same voices the sequencer's trigger() fires - so a
    // keyboard note hears the row's trims, envelopes and kit exactly
    // as a step does.
    void fireLiveNote (const LiveNoteEvent& e)
    {
        const int offset = e.note - 60; // semitones from the C4 root
        if (! e.on)
        {
            if (e.channel == kSynthChannel) synth.noteOff ((float) (e.note - 48));
            else if (e.channel == kPianoChannel) piano.noteOff ((float) offset);
            return; // one-shots ignore offs - classic sampler behaviour
        }
        const float vel = std::clamp (e.velocity, 0.0f, 1.0f);
        if (e.channel == kSynthChannel)
        {
            synth.noteOn ((float) (e.note - 48), vel, (int) (sr * 60.0)); // held until the off
            return;
        }
        if (e.channel == kPianoChannel)
        {
            piano.noteOn ((float) offset, vel);
            return;
        }
        if (e.channel < 0 || e.channel >= kTracks || e.channel >= numTracksA.load()) return;
        const int t = e.channel;
        if (isPadRow (t))
        {
            // item 109: a row's live note sounds the ROW's own sample
            // (through the row voice path - its trims/envelope/tune),
            // never the pad bank's.
            rowPads.triggerWithTune (t - Sample1, vel, offset);
            return;
        }
        // A bound sample voice started at a pitch: tryKitVoice's
        // start, with the playback rate bent by the note's offset.
        auto startPitched = [this] (OneShotVoice& v,
                                    const std::shared_ptr<const std::vector<float>>& data,
                                    double srcSr, float v0, int off)
        {
            if (data == nullptr) return;
            v.data = data;
            v.rate = (srcSr / sr) * std::pow (2.0, off / 12.0);
            v.pos = 0.0;
            v.age = 0;
            v.vel = std::clamp (v0, 0.0f, 1.0f);
            v.active = true;
        };
        if (t >= kFirstOwnRow)
        {
            const size_t oi = (size_t) (t - kFirstOwnRow);
            if (ownBoundA[oi].load())
                startPitched (ownVoices[oi], ownData[oi], ownSrcSr[oi], vel, offset);
            return;
        }
        switch (t)
        {
            case Kick:
                if (kitBoundA[(size_t) Kick].load())
                    startPitched (kitVoices[(size_t) Kick], kitData[(size_t) Kick],
                                  kitSrcSr[(size_t) Kick], vel, offset);
                else
                    kick.triggerPitched (vel, offset);
                break;
            case Snare:
                if (kitBoundA[(size_t) Snare].load())
                    startPitched (kitVoices[(size_t) Snare], kitData[(size_t) Snare],
                                  kitSrcSr[(size_t) Snare], vel, offset);
                else snare.trigger (vel);
                break;
            case ClosedHat:
                if (kitBoundA[(size_t) ClosedHat].load())
                    startPitched (kitVoices[(size_t) ClosedHat], kitData[(size_t) ClosedHat],
                                  kitSrcSr[(size_t) ClosedHat], vel, offset);
                else { hat.triggerClosed (vel); hatTrack = ClosedHat; } // chokes an open hat
                break;
            case OpenHat:
                if (kitBoundA[(size_t) OpenHat].load())
                    startPitched (kitVoices[(size_t) OpenHat], kitData[(size_t) OpenHat],
                                  kitSrcSr[(size_t) OpenHat], vel, offset);
                else { hat.triggerOpen (vel); hatTrack = OpenHat; }
                break;
            case Clap:
                if (kitBoundA[(size_t) Clap].load())
                    startPitched (kitVoices[(size_t) Clap], kitData[(size_t) Clap],
                                  kitSrcSr[(size_t) Clap], vel, offset);
                else clap.trigger (vel);
                break;
            default: break;
        }
    }
    std::array<std::atomic<int>, kPatterns * kSteps> patMelody;
    std::array<std::atomic<int>, kPatterns> patMelodySource;
    std::array<std::atomic<int>, kPatterns * kSteps> patSynth, patPiano;
    std::array<std::atomic<int>, kSongSlots> songChainA;

    // Round 4 (item 99c): the arrangement's atomics (see the API above).
    std::array<std::atomic<bool>, kMaxSongBlocks> blkActiveA;
    std::array<std::atomic<int>, kMaxSongBlocks> blkPatternA, blkLaneA, blkStartA, blkLenA;
    std::atomic<int> songTotalBarsA { 0 };
    std::atomic<int> blocksRevisionA { 0 };
    std::atomic<int> playSongBarA { -1 };
    // Audio thread only: the RT snapshot of the blocks + the current
    // 32-step window of the song (arrangement playback counts windows,
    // because blocks align to bars, not to pattern boundaries).
    SongBlock rtBlocks[kMaxSongBlocks];
    int rtBlocksRevision = -1;
    int songWindow = 0;
    // Set by placeTransportAt: the tick-0 that fires AT a parked window
    // boundary is the park itself, not a completed window - the
    // arrangement's advance must not skip the parked window for it.
    // fireTick clears it (the advance branch runs before fireTick in
    // the same pass, so the flag survives exactly one tick-0).
    bool placedFresh = false;

    void refreshSongTotalBars()
    {
        int bars = 0;
        for (int i = 0; i < kMaxSongBlocks; ++i)
            if (blkActiveA[(size_t) i].load())
                bars = std::max (bars, blkStartA[(size_t) i].load() + blkLenA[(size_t) i].load());
        songTotalBarsA.store (bars);
    }
    void refreshRtBlocksIfNeeded()
    {
        const int rev = blocksRevisionA.load();
        if (rev == rtBlocksRevision) return;
        rtBlocksRevision = rev;
        for (int i = 0; i < kMaxSongBlocks; ++i) rtBlocks[i] = getSongBlock (i);
    }

    // pad capture (item 31)
    std::atomic<bool> padCaptureOn { true };
    std::atomic<int> capturedPad { -1 };

    // track list size (item 39) and the item 45/46/47 state (fix pass 2)
    std::atomic<int> numTracksA { 8 };
    std::atomic<bool> stopAllRequested { false };
    std::atomic<bool> audioPlaying { false };
    std::atomic<long long> audioStartPosA { 0 }, audioPosAtomic { 0 };
    std::atomic<bool> audioSeekPending { false };
    std::atomic<unsigned int> soundingClipMaskA { 0 };
    long long audioPos = 0;              // audio thread: the audio timeline's clock
    bool wasAudioPlaying = false;        // audio thread
    bool panicActive = false, panicVoicesPending = false; // audio thread (item 45)
    int panicFadeLeft = 0, panicFadeTotal = 1;            // audio thread (item 45)

    // kit-row sample bindings (item 40) and own-buffer rows (item 39):
    // buffers are shared_ptrs swapped under sampleMutex; the bound flags
    // are atomics so the audio thread can decide without the lock.
    std::array<std::shared_ptr<const std::vector<float>>, 5> kitData;
    std::array<double, 5> kitSrcSr {};
    std::array<std::atomic<bool>, 5> kitBoundA;
    std::array<std::shared_ptr<const std::vector<float>>, kTracks - kFirstOwnRow> ownData;
    std::array<double, kTracks - kFirstOwnRow> ownSrcSr {};
    std::array<std::atomic<bool>, kTracks - kFirstOwnRow> ownBoundA;
    std::array<OneShotVoice, 5> kitVoices;                        // audio thread
    std::array<OneShotVoice, kTracks - kFirstOwnRow> ownVoices;   // audio thread

    // waveform audition (item 46)
    std::shared_ptr<const std::vector<float>> auditionData;
    double auditionSrcSr = 44100.0;
    std::atomic<bool> auditionStartRequested { false }, auditionStopRequested { false },
                      auditionSounding { false };
    OneShotVoice auditionVoice; // audio thread

    // audio track + clips (items 32/35): regions are atomics; the audio
    // buffers sit beside takeData under sampleMutex.
    struct ClipState
    {
        std::atomic<bool> active { false };
        std::atomic<long long> start { 0 }, srcStart { 0 }, length { 0 };
        std::atomic<int> track { 0 }; // item 60: the lane this clip lives on
    };
    std::array<ClipState, kMaxAudioClips> clipsA;
    std::array<std::vector<float>, kMaxAudioClips> clipData;
    std::array<std::atomic<float>, kAudioTracks> audioGainA;
    std::array<std::atomic<bool>, kAudioTracks> audioMuteA, audioSoloA;
    std::atomic<int> clipRevision { 0 };
    std::atomic<int> numAudioTracksA { 1 }; // item 60: lanes in use
    std::atomic<int> recTrackA { 0 };       // item 60: where the next take lands
    std::atomic<long long> transportPosAtomic { 0 };

    // vocal take: capture buffer + finished take (swapped only under sampleMutex)
    std::vector<float> takeData, recData;
    // takeOn defaults OFF (item 56): the raw take never loops with the
    // beat unless the user switches "Loop take" on - a finished take's
    // home is its clip, and the two never sound together by accident.
    std::atomic<bool> recording { false }, recPending { false }, takeOn { false };
    std::atomic<int> recPublished { 0 }, takeLenAtomic { 0 };
    // Item 56 bookkeeping (serials under sampleMutex): every recording
    // bumps captureGen; finishTake builds at most once per capture and
    // stamps the take with a fresh serial; placeTakeAsClip homes a take
    // serial exactly once, so the several UI paths that ask for the clip
    // (record button, transport stop, timer, "add as clip") can never
    // stack copies of the same take.
    std::atomic<int> captureGen { 0 };
    int builtForGen = -1, takeSerial = -1, placedSerial = -2, takeSerialSource = 0;
    int placedSlot = -1; // the clip slot the current take was homed in
    std::atomic<long long> takeStartPosA { 0 };
    int recLen = 0;                // audio thread only
    long long transportSamples = 0; // audio thread only

    // Round 2 (item 93): the transport park + pending live seek.
    std::atomic<long long> transportSeekA { -1 };
    std::atomic<long long> transportStartPosA { 0 };
    // Round 2 (item 95): audio-thread gain-smoothing states (one-pole,
    // ~15 ms) - see process(). Initialised to the targets on the first
    // block after prepare(), never on a value change.
    bool gainSmInit = false;
    float rowGainSm[kTracks] = {};
    float audioGainSm[kAudioTracks] = {};
    float vocalGainSm = 0.0f, padBusGainSm = 0.0f, masterGainSm = 1.0f;

    std::atomic<bool> playing { false }, dirty { true };
    std::atomic<bool> pausedA { false }; // Round 6 (item 115): PAUSE - engaged but frozen
    std::atomic<int>  playStep { -1 };
    std::atomic<float> bpm { 90.0f }, swing { 58.0f }, masterDb { 0.0f }, glueGr { 0.0f };

    std::atomic<float> kickTune { 50.0f }, kickDecay { 600.0f }, snareSnappy { 0.6f },
                       openHatDecay { 380.0f }, clapTail { 200.0f }, dustRate { 26000.0f },
                       glueThreshold { -12.0f }, vocalDistance { 15.0f }, vocalInputDb { 0.0f };
    std::atomic<bool> dustOn { false }, glueOn { true }, vocalOn { false };
    std::atomic<int>  vocalMic { 2 };
    std::atomic<int>  synthWave { 0 };
    std::atomic<float> synthCutoff { 4500.0f }, synthReso { 0.25f }, synthAttack { 8.0f },
                       synthRelease { 240.0f }, synthLevel { 1.0f },
                       pianoBright { 0.6f }, pianoDecay { 2200.0f }, pianoLevel { 1.0f };

    // audio-thread-only state
    int tick = 0;
    double samplesToNextTick = 0.0;
    bool wasPlaying = false;
    bool wasPaused = false; // Round 6 (item 115): the resume edge re-enters at the frozen position
    int hatTrack = ClosedHat;

    mutable std::mutex sampleMutex;

    // ---- Pass 2: the Station state ----
    struct StationState
    {
        std::array<StripSettings, kNumChannels> strips;
        std::array<float, kNumChannels> pan {};
        float padBusGain = 1.0f; bool padBusMute = false;
        float vocalGain = 1.0f; bool vocalMute = false, vocalSolo = false; // item 113: flat
        bool synthMute = false, synthSolo = false, pianoMute = false, pianoSolo = false;
        bool masterLimiterOn = false; float masterLimiterCeilingDb = -1.0f;
    };
    StationState station;
    mutable std::mutex stationMutex;
    std::atomic<bool> stationDirty { true };
    std::atomic<int> stationRevision { 0 };
    // audio-thread copies (written by applyStationLocked)
    std::array<bool, kNumChannels> stripActiveRt {};
    std::array<float, kNumChannels> chanPanRt {};
    float padBusGainRt = 1.0f; bool padBusMuteRt = false;
    float vocalGainRt = 1.0f; bool vocalMuteRt = false, vocalSoloRt = false;
    bool synthMuteRt = false, synthSoloRt = false, pianoMuteRt = false, pianoSoloRt = false;
    bool masterLimiterOnRt = false;
    // per-channel meters + the EQ analyser ring
    std::array<std::atomic<float>, kNumChannels> chanPeakL, chanPeakR, chanCompGr, chanDeessGr, chanLimGr;
    std::atomic<float> masterPeakL { 0.0f }, masterPeakR { 0.0f }, masterLimGr { 0.0f };
    static constexpr int kAnalyserSize = 4096;
    std::array<float, kAnalyserSize> analyserRing {};
    std::atomic<int> analyserWrite { 0 };
    std::atomic<int> analysedChannel { 0 };
};
} // namespace boombap
