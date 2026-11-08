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
#include "../../Core/VocalChain/VocalChain.h"
#include "../../Core/Instruments/Synth.h"
#include "../../Core/Instruments/Piano.h"
#include "ProjectSettings.h"

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
    static constexpr int kMaxTakeSeconds = 120;
    static constexpr int kAudioTracks   = ProjectSettings::kAudioTracks;
    static constexpr int kMaxAudioClips = ProjectSettings::kMaxAudioClips;
    enum Track { Kick, Snare, ClosedHat, OpenHat, Clap, Sample1, Sample2, Sample3 };
    static constexpr int kFirstOwnRow = Sample1 + kPads; // 21: first row with its own sample slot

    static std::string trackName (int t)
    {
        static const char* names[8] = { "Kick", "Snare", "Closed Hat", "Open Hat", "Clap",
                                        "Sample 1", "Sample 2", "Sample 3" };
        if (t >= 0 && t < 8) return names[t];
        if (t >= Sample1 && t < kTracks) return "Sample " + std::to_string (t - Sample1 + 1);
        return "Track " + std::to_string (t + 1);
    }

    BoomBapEngine()
    {
        for (auto& c : patGrid) c.store (0);
        for (auto& g : trackGain) g.store (0.8f);
        for (auto& m : trackMute) m.store (false);
        for (auto& s : trackSolo) s.store (false);
        for (auto& p : trackPeak) p.store (0.0f);
        for (auto& h : padHit) h.store (0);
        for (auto& n : patMelody) n.store (0);
        for (auto& n : patMelodySource) n.store (0);
        for (auto& n : patSynth) n.store (0);
        for (auto& n : patPiano) n.store (0);
        for (auto& n : songChainA) n.store (0);
        for (auto& b : kitBoundA) b.store (false);
        for (auto& b : ownBoundA) b.store (false);
        for (auto& g : audioGainA) g.store (0.9f);
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
        dust.prepare (sr);
        bus.prepare (sr);
        vocal.prepare (sr);
        takeVocal.prepare (sr);
        synth.prepare (sr);
        piano.prepare (sr);
        applyParams();
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
        clipRevision.fetch_add (1);
        pads.stopAll();
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
        return p;
    }

    // Applies a whole project at once. Values are clamped to safe ranges. The vocal
    // monitor on/off switch is not part of a project, so opening a file never turns a
    // microphone on.
    void applySettings (ProjectSettings p)
    {
        p.sanitize();
        numTracksA.store (p.numTracks);
        bpm.store (p.bpm);  swing.store (p.swing);  masterDb.store (p.masterDb);
        for (int t = 0; t < kTracks; ++t)
        {
            trackGain[t].store (p.trackGain[t]);
            trackMute[t].store (p.trackMute[t]);
            trackSolo[t].store (p.trackSolo[t]);
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
            pads.clearSample (track - Sample1);
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
    void setPlaying (bool p)      { playing.store (p); }
    bool isPlaying() const        { return playing.load(); }

    // ---- Fix pass 2 item 45: STOP takes priority over EVERYTHING. ----
    // Every sounding thing - pad one-shots (run-on included), synth/piano
    // voices, kit/row samples, the waveform audition, audio clips, the raw
    // take - is silenced by the audio thread at the next block, behind a
    // ~5 ms fade so the cut itself doesn't click. Both transports stop too.
    // (Ending a recording is the caller's job first, so the take lands.)
    void stopAllSound()
    {
        playing.store (false);
        audioPlaying.store (false);
        stopAllRequested.store (true);
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
    // Rows 5..20 play the pad bank's pads 1..16; rows 21+ hold their own
    // sample in the engine (item 39's added rows past the bank).
    void loadSample (int track, std::vector<float> data, double sourceSampleRate)
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        if (track >= Sample1 && track < Sample1 + kPads)
            pads.loadSample (track - Sample1, std::move (data), sourceSampleRate);
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

    // ---------------- audio thread ----------------
    // vocalIn may be nullptr (no input). outL/outR are overwritten.
    void process (const float* vocalIn, float* outL, float* outR, int numSamples)
    {
        if (dirty.exchange (false))
            applyParams();

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
                if (v > 0 && playing.load() && padCaptureOn.load() && capStep >= 0
                    && Sample1 + p < numTracksA.load())
                {
                    patGrid[(size_t) gIdx (activePattern, Sample1 + p, capStep)].store ((uint8_t) v);
                    capturedPad.store (p);
                }
            }

        const bool isPlayingNow = playing.load();
        if (isPlayingNow && ! wasPlaying)
        {
            tick = 0; samplesToNextTick = 0.0; transportSamples = 0;
            songPos = 0; barsDone = 0;
            audioPos = 0; // the audio clock shadows the transport while it plays
            panicActive = false; panicVoicesPending = false; // a fresh start is never faded
            const bool useSong = songMode.load() && songLength.load() > 0;
            activePattern = useSong ? chainAt (0) : editPattern.load();
            playPattern.store (activePattern);
            playSongPos.store (useSong ? 0 : -1);
        }
        if (! isPlayingNow && wasPlaying) playStep.store (-1);
        wasPlaying = isPlayingNow;

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

        const double samplesPerTick = sr * 60.0 / ((double) bpm.load() * kPPQN);
        const float swingNow = swing.load();
        const bool useDust = dustOn.load(), useGlue = glueOn.load(), useVocal = vocalOn.load() && vocalIn != nullptr;
        const bool wantVocal = vocalIn != nullptr && (useVocal || recording.load() || recPending.load());
        bool cap = recording.load();
        const float master = std::pow (10.0f, masterDb.load() / 20.0f);

        const bool soloActive = anySoloActive();
        float gains[kTracks];
        for (int t = 0; t < kTracks; ++t)
            gains[t] = (trackMute[t].load() || (soloActive && ! trackSolo[t].load())) ? 0.0f
                                                                                      : trackGain[t].load();
        const float clickLvl = metronomeLevel.load();
        if (! isPlayingNow) clickSamplesLeft = 0;

        // Audio tracks (Fix pass item 35; item 60 made them many): each is
        // silent when muted, or when a solo - instrument or audio - is
        // active somewhere and this track is not itself soloed. Clips live
        // on the audio timeline, which is not fenced by the pattern loop.
        bool anyAudioSolo = false;
        for (int t = 0; t < kAudioTracks; ++t) anyAudioSolo = anyAudioSolo || audioSoloA[(size_t) t].load();
        float aGain[kAudioTracks];
        for (int t = 0; t < kAudioTracks; ++t)
            aGain[t] = (audioMuteA[(size_t) t].load()
                        || (soloActive && ! audioSoloA[(size_t) t].load())
                        || (anyAudioSolo && ! audioSoloA[(size_t) t].load()))
                           ? 0.0f : audioGainA[(size_t) t].load();
        const bool useClips = samplesOk && audioClipCount() > 0;
        unsigned int clipMask = 0; // clips sounding this block (item 54)
        // Item 36: the take records the CLEAN input - input-gain staging only.
        // The mic emulation is a playback treatment (see takeVocal below).
        const float inGainLin = std::pow (10.0f, vocalInputDb.load() / 20.0f);

        float pk[kTracks] = {};
        float padPk = 0.0f;
        int padPkIdx = -1;
        for (int i = 0; i < numSamples; ++i)
        {
            if (isPlayingNow)
            {
                if (samplesToNextTick <= 0.0)
                {
                    if (tick == 0)
                    {
                        // bar line: advance the song chain, or follow the edited pattern
                        if (songMode.load() && songLength.load() > 0)
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
                    tick = (tick + 1) % (kSteps * kTicksPer16th);
                    samplesToNextTick += samplesPerTick;
                }
                samplesToNextTick -= 1.0;
            }

            // Drums: a row bound to a sample (item 40) sounds its one-shot
            // voice instead of the synthesized drum (which then never
            // triggers, so its process() just returns silence).
            float kv = kick.process(), snv = snare.process(), cv = clap.process();
            const float hv = hat.process();
            if (kitVoices[Kick].active)  kv  = renderOneShot (kitVoices[Kick]);
            if (kitVoices[Snare].active) snv = renderOneShot (kitVoices[Snare]);
            if (kitVoices[Clap].active)  cv  = renderOneShot (kitVoices[Clap]);
            float hatMix = hv * gains[hatTrack];
            pk[hatTrack] = std::max (pk[hatTrack], std::abs (hv * gains[hatTrack]));
            if (kitVoices[ClosedHat].active || kitVoices[OpenHat].active)
            {
                hatMix = 0.0f;
                for (int ht : { ClosedHat, OpenHat })
                    if (kitVoices[(size_t) ht].active)
                    {
                        const float pv = renderOneShot (kitVoices[(size_t) ht]) * gains[ht];
                        hatMix += pv;
                        pk[ht] = std::max (pk[ht], std::abs (pv));
                    }
            }
            float mix = kv * gains[Kick] + snv * gains[Snare]
                      + cv * gains[Clap]  + hatMix
                      + synth.process() + piano.process();

            pk[Kick]  = std::max (pk[Kick],  std::abs (kv * gains[Kick]));
            pk[Snare] = std::max (pk[Snare], std::abs (snv * gains[Snare]));
            pk[Clap]  = std::max (pk[Clap],  std::abs (cv * gains[Clap]));

            if (samplesOk)
            {
                // every pad is a sequencer row while its row exists (rows
                // 5..20); pads past the track list stay live/audition pads
                for (int p = 0; p < kPads; ++p)
                {
                    const int row = Sample1 + p;
                    if (row < activeTracks)
                    {
                        const float pv = pads.processPad (p) * gains[row];
                        mix += pv;
                        pk[row] = std::max (pk[row], std::abs (pv));
                        if (std::abs (pv) > padPk) { padPk = std::abs (pv); padPkIdx = p; }
                    }
                    else
                    {
                        const float pv = pads.processPad (p);
                        mix += pv;
                        if (std::abs (pv) > padPk) { padPk = std::abs (pv); padPkIdx = p; }
                    }
                }
                // rows past the pad bank carry their own sample (item 39)
                for (int t = kFirstOwnRow; t < activeTracks; ++t)
                {
                    auto& v = ownVoices[(size_t) (t - kFirstOwnRow)];
                    if (v.active)
                    {
                        const float pv = renderOneShot (v) * gains[t];
                        mix += pv;
                        pk[t] = std::max (pk[t], std::abs (pv));
                    }
                }
                // the waveform panel's audition (item 46)
                if (auditionVoice.active)
                    mix += renderOneShot (auditionVoice);
            }

            if (useDust) mix = dust.process (mix);

            float vocalSample = 0.0f;
            if (wantVocal) vocalSample = vocal.process (vocalIn[i]);
            if (useVocal) mix += vocalSample;

            if (cap && samplesOk && recLen < (int) recData.size())
            {
                // Item 36: store the clean input (gain staging only) - never
                // the monitored, mic-emulated signal.
                recData[(size_t) recLen++] = vocalIn != nullptr ? vocalIn[i] * inGainLin : 0.0f;
                if (recLen >= (int) recData.size()) recording.store (false); // length cap
            }

            // The vocal bus (items 32/36): the stored take and the clips
            // cut from it are the same CLEAN recording, so they sum here and
            // run through the one playback chain - the mic-locker treatment
            // is applied at playback, switchable any time, never burned in.
            {
                float vocalBus = 0.0f;
                bool vocalPresent = false;
                if (samplesOk && takeOn.load() && takeLenAtomic.load() > 0 && isPlayingNow)
                {
                    const long long rel = transportSamples - takeStartPosA.load();
                    if (rel >= 0)
                    {
                        vocalBus += takeData[(size_t) (rel % takeLenAtomic.load())];
                        vocalPresent = true;
                    }
                }
                // Fix pass 2 (item 48): clips sit on the AUDIO timeline,
                // which is not fenced by the pattern loop - it runs while
                // either transport runs, as far as the audio goes.
                if (useClips && (isPlayingNow || audioNow))
                {
                    for (int ci = 0; ci < kMaxAudioClips; ++ci)
                    {
                        const auto& c = clipsA[(size_t) ci];
                        if (! c.active.load()) continue;
                        const float g = aGain[(size_t) std::clamp (c.track.load(), 0, kAudioTracks - 1)];
                        if (g <= 0.0f) continue;
                        const long long pos = audioPos - c.start.load();
                        if (pos < 0 || pos >= c.length.load()) continue;
                        const auto& data = clipData[(size_t) ci];
                        const long long src = c.srcStart.load() + pos;
                        if (src >= 0 && src < (long long) data.size())
                        {
                            vocalBus += data[(size_t) src] * g;
                            vocalPresent = true;
                            clipMask |= (1u << ci);
                        }
                    }
                }
                if (vocalPresent) mix += takeVocal.process (vocalBus);
            }

            if (isPlayingNow) { ++transportSamples; audioPos = transportSamples; }
            else if (audioNow) ++audioPos;

            float l = mix, r = mix;
            if (useGlue) bus.process (l, r);

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
            outL[i] = std::tanh (l * master) * outGain;
            outR[i] = std::tanh (r * master) * outGain;
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
        glueGr.store (useGlue ? bus.getGainReductionDb() : 0.0f);
    }

private:
    void fireTick (int t, float swingPercent, bool samplesOk, double samplesPerTick)
    {
        playStep.store (t / kTicksPer16th);
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
                else if (samplesOk) pads.triggerWithTune (src - 1, 0.9f, semitones);
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
        if (samplesOk) pads.stopAll();
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
                else if (samplesOk) { pads.trigger (track - Sample1, vel); lastPadIndex.store (track - Sample1); }
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
    }

    double sr = 44100.0;

    // sound sources
    Kick808 kick; Snare808 snare; Hat808 hat; Clap808 clap;
    MpcPads pads;
    Synth synth;
    Piano piano;
    S950Crusher dust;
    BusCompressor bus;
    VocalChain vocal;
    VocalChain takeVocal; // item 36: treats the clean recorded take at playback

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
    std::array<std::atomic<int>, kPatterns * kSteps> patMelody;
    std::array<std::atomic<int>, kPatterns> patMelodySource;
    std::array<std::atomic<int>, kPatterns * kSteps> patSynth, patPiano;
    std::array<std::atomic<int>, kSongSlots> songChainA;

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

    std::atomic<bool> playing { false }, dirty { true };
    std::atomic<int>  playStep { -1 };
    std::atomic<float> bpm { 90.0f }, swing { 58.0f }, masterDb { 0.0f }, glueGr { 0.0f };

    std::atomic<float> kickTune { 50.0f }, kickDecay { 600.0f }, snareSnappy { 0.6f },
                       openHatDecay { 380.0f }, clapTail { 200.0f }, dustRate { 26000.0f },
                       glueThreshold { -12.0f }, vocalDistance { 15.0f }, vocalInputDb { 0.0f };
    std::atomic<bool> dustOn { false }, glueOn { true }, vocalOn { false };
    std::atomic<int>  vocalMic { 2 };
    std::atomic<int>  synthWave { 0 };
    std::atomic<float> synthCutoff { 4500.0f }, synthReso { 0.25f }, synthAttack { 8.0f },
                       synthRelease { 240.0f }, synthLevel { 0.8f },
                       pianoBright { 0.6f }, pianoDecay { 2200.0f }, pianoLevel { 0.85f };

    // audio-thread-only state
    int tick = 0;
    double samplesToNextTick = 0.0;
    bool wasPlaying = false;
    int hatTrack = ClosedHat;

    mutable std::mutex sampleMutex;
};
} // namespace boombap
