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
#include <mutex>
#include <vector>
#include <cmath>

namespace boombap
{
class BoomBapEngine
{
public:
    static constexpr int kTracks = 8;
    static constexpr int kSteps  = 16;
    static constexpr int kPads   = MpcPads::kNumPads;
    static constexpr int kPatterns  = ProjectSettings::kPatterns;
    static constexpr int kSongSlots = ProjectSettings::kSongSlots;
    static constexpr int kMaxTakeSeconds = 120;
    static constexpr int kAudioTracks   = ProjectSettings::kAudioTracks;
    static constexpr int kMaxAudioClips = ProjectSettings::kMaxAudioClips;
    enum Track { Kick, Snare, ClosedHat, OpenHat, Clap, Sample1, Sample2, Sample3 };

    static const char* trackName (int t)
    {
        static const char* names[kTracks] = { "Kick", "Snare", "Closed Hat", "Open Hat", "Clap",
                                              "Sample 1", "Sample 2", "Sample 3" };
        return names[t];
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
        }
        {
            std::lock_guard<std::mutex> lock (o.sampleMutex);
            pads = o.pads;
            takeData = o.takeData;
            clipData = o.clipData;
            takeLenAtomic.store (o.takeLenAtomic.load());
            takeStartPosA.store (o.takeStartPosA.load());
            takeOn.store (o.takeOn.load());
        }
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
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            const auto& c = clipsA[(size_t) i];
            p.audioClips[i].active = c.active.load();
            p.audioClips[i].startSamples = c.start.load();
            p.audioClips[i].sourceStart = c.srcStart.load();
            p.audioClips[i].lengthSamples = c.length.load();
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
        for (int i = 0; i < kMaxAudioClips; ++i)
        {
            clipsA[(size_t) i].active.store (p.audioClips[i].active);
            clipsA[(size_t) i].start.store (p.audioClips[i].startSamples);
            clipsA[(size_t) i].srcStart.store (p.audioClips[i].sourceStart);
            clipsA[(size_t) i].length.store (p.audioClips[i].lengthSamples);
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
        for (int i = 0; i < kMaxAudioClips; ++i)
            if (clipsA[(size_t) i].active.load())
                clipData[(size_t) i] = takeData;
        clipRevision.fetch_add (1);
    }

    void clearSample (int track)
    {
        if (track < Sample1 || track > Sample3) return;
        std::lock_guard<std::mutex> lock (sampleMutex);
        pads.clearSample (track - Sample1);
    }

    // ---------------- transport ----------------
    void setPlaying (bool p)      { playing.store (p); }
    bool isPlaying() const        { return playing.load(); }
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
        recData.assign ((size_t) (sr * kMaxTakeSeconds), 0.0f);
        recPublished.store (0);
        if (playing.load()) recPending.store (true);
        else { takeStartPosA.store (0); recording.store (true); }
    }

    bool isRecording() const { return recording.load() || recPending.load(); }
    double recordedSeconds() const { return sr > 0.0 ? recPublished.load() / sr : 0.0; }

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
        const double barSamples = sr * 240.0 / (double) bpm.load();
        const int bars = std::max (1, (int) std::ceil (n / barSamples));
        const int quantized = (int) std::llround (bars * barSamples);
        takeData.assign (recData.begin(), recData.begin() + n);
        takeData.resize ((size_t) quantized, 0.0f);
        takeLenAtomic.store (quantized);
        takeStartPosA.store (takeStartPosA.load() % std::max (1LL, std::llround (barSamples)));
        takeOn.store (true);
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
        takeOn.store (true);
    }

    void clearVocalTake()
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        takeData.clear();
        takeLenAtomic.store (0);
    }

    std::vector<float> getTakeData() const
    {
        std::lock_guard<std::mutex> lock (sampleMutex);
        return takeData;
    }

    // ---------------- samples (message thread) ----------------
    // track must be Sample1..Sample3
    void loadSample (int track, std::vector<float> data, double sourceSampleRate)
    {
        if (track < Sample1 || track > Sample3) return;
        std::lock_guard<std::mutex> lock (sampleMutex);
        pads.loadSample (track - Sample1, std::move (data), sourceSampleRate);
    }

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
        for (int i = 0; i < kMaxAudioClips; ++i)
            if (! clipsA[(size_t) i].active.load())
            {
                clipData[(size_t) i] = takeData;
                clipsA[(size_t) i].start.store (takeStartPosA.load());
                clipsA[(size_t) i].srcStart.store (0);
                clipsA[(size_t) i].length.store ((long long) takeData.size());
                clipsA[(size_t) i].active.store (true);
                clipRevision.fetch_add (1);
                // the clip is the take's home now - the raw take loop stands
                // down so the same audio never sounds twice
                takeOn.store (false);
                return i;
            }
        return -1;
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
                // Fix pass item 31: a hit on a sequencer pad (1-3) while the
                // transport plays is captured into the pattern at the
                // playhead step, with the hit's velocity.
                const int capStep = playStep.load();
                if (v > 0 && p < 3 && playing.load() && padCaptureOn.load() && capStep >= 0)
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
            const bool useSong = songMode.load() && songLength.load() > 0;
            activePattern = useSong ? chainAt (0) : editPattern.load();
            playPattern.store (activePattern);
            playSongPos.store (useSong ? 0 : -1);
        }
        if (! isPlayingNow && wasPlaying) playStep.store (-1);
        wasPlaying = isPlayingNow;

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

        // Audio track (Fix pass item 35): silent when muted, or when an
        // instrument solo is active and the audio track is not itself soloed.
        // Clips live on the song timeline, which loops with the song (or with
        // the single pattern in pattern mode).
        const float audioGainNow = (audioMuteA[0].load() || (soloActive && ! audioSoloA[0].load()))
                                       ? 0.0f : audioGainA[0].load();
        const bool useClips = samplesOk && audioGainNow > 0.0f && audioClipCount() > 0;
        const double barSamplesNow = sr * 240.0 / (double) bpm.load();
        const long long loopSamples = (long long) std::llround (
            (songMode.load() && songLength.load() > 0 ? (double) songLength.load() : 1.0) * barSamplesNow);
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

            const float kv = kick.process(), snv = snare.process(),
                        cv = clap.process(), hv = hat.process();
            float mix = kv * gains[Kick] + snv * gains[Snare]
                      + cv * gains[Clap]  + hv * gains[hatTrack]
                      + synth.process() + piano.process();

            pk[Kick]     = std::max (pk[Kick],     std::abs (kv * gains[Kick]));
            pk[Snare]    = std::max (pk[Snare],    std::abs (snv * gains[Snare]));
            pk[Clap]     = std::max (pk[Clap],     std::abs (cv * gains[Clap]));
            pk[hatTrack] = std::max (pk[hatTrack], std::abs (hv * gains[hatTrack]));

            if (samplesOk)
            {
                for (int p = 0; p < 3; ++p)
                {
                    const float pv = pads.processPad (p) * gains[Sample1 + p];
                    mix += pv;
                    pk[Sample1 + p] = std::max (pk[Sample1 + p], std::abs (pv));
                    if (std::abs (pv) > padPk) { padPk = std::abs (pv); padPkIdx = p; }
                }
                // pads 4-16 are live/audition pads: straight into the mix
                for (int p = 3; p < kPads; ++p)
                {
                    const float pv = pads.processPad (p);
                    mix += pv;
                    if (std::abs (pv) > padPk) { padPk = std::abs (pv); padPkIdx = p; }
                }
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
                if (useClips && isPlayingNow && loopSamples > 0)
                {
                    const long long songPos = transportSamples % loopSamples;
                    for (int ci = 0; ci < kMaxAudioClips; ++ci)
                    {
                        const auto& c = clipsA[(size_t) ci];
                        if (! c.active.load()) continue;
                        const long long pos = songPos - c.start.load();
                        if (pos < 0 || pos >= c.length.load()) continue;
                        const auto& data = clipData[(size_t) ci];
                        const long long src = c.srcStart.load() + pos;
                        if (src >= 0 && src < (long long) data.size())
                        {
                            vocalBus += data[(size_t) src] * audioGainNow;
                            vocalPresent = true;
                        }
                    }
                }
                if (vocalPresent) mix += takeVocal.process (vocalBus);
            }

            if (isPlayingNow) ++transportSamples;

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

            outL[i] = std::tanh (l * master);
            outR[i] = std::tanh (r * master);
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
            for (int tr = 0; tr < kTracks; ++tr)
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

    void trigger (int track, float vel, bool samplesOk)
    {
        switch (track)
        {
            case Kick:      kick.trigger (vel); break;
            case Snare:     snare.trigger (vel); break;
            case ClosedHat: hat.triggerClosed (vel); hatTrack = ClosedHat; break; // chokes an open hat
            case OpenHat:   hat.triggerOpen (vel);   hatTrack = OpenHat;   break;
            case Clap:      clap.trigger (vel); break;
            default:        if (samplesOk) { pads.trigger (track - Sample1, vel); lastPadIndex.store (track - Sample1); }
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

    // audio track + clips (items 32/35): regions are atomics; the audio
    // buffers sit beside takeData under sampleMutex.
    struct ClipState
    {
        std::atomic<bool> active { false };
        std::atomic<long long> start { 0 }, srcStart { 0 }, length { 0 };
    };
    std::array<ClipState, kMaxAudioClips> clipsA;
    std::array<std::vector<float>, kMaxAudioClips> clipData;
    std::array<std::atomic<float>, kAudioTracks> audioGainA;
    std::array<std::atomic<bool>, kAudioTracks> audioMuteA, audioSoloA;
    std::atomic<int> clipRevision { 0 };
    std::atomic<long long> transportPosAtomic { 0 };

    // vocal take: capture buffer + finished take (swapped only under sampleMutex)
    std::vector<float> takeData, recData;
    std::atomic<bool> recording { false }, recPending { false }, takeOn { true };
    std::atomic<int> recPublished { 0 }, takeLenAtomic { 0 };
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
