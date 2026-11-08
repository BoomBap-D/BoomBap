#pragma once
// Everything about a project that is plain data (no JUCE), so the engine and the
// file format share one definition. Sample file paths live in ProjectFile.h.

#include <algorithm>

namespace boombap
{
// One audio clip on the audio track (Fix pass item 35). Positions are in SAMPLES
// on the song timeline; the recorded audio itself lives in the engine (and, on
// disk, in the take WAV). Trimming moves these numbers, never the audio.
struct AudioClip
{
    bool active = false;
    long long startSamples = 0;   // where the clip sits on the timeline
    long long sourceStart = 0;    // trim: offset into the recorded audio
    long long lengthSamples = 0;  // trim: how much of the audio plays
    int track = 0;                // which audio track it lives on (item 60;
                                  // older files have no track - they mean 0)
};

struct ProjectSettings
{
    // Fix pass 2: a pattern is TWO bars (item 48: 32 steps), and the track
    // list grows past the original 8 rows (item 39: unlimited tracks that
    // scroll). kTracks is the engine capacity; numTracks is how many rows
    // this project actually shows. Files from earlier builds carry 8 tracks
    // x 16 steps - the loader leaves the rest at defaults, so they extend
    // with empty steps and the standard 8 rows.
    static constexpr int kTracks = 32;
    static constexpr int kSteps  = 32;
    static constexpr int kPatterns = 8;   // patterns A-H
    static constexpr int kSongSlots = 32; // playlist length
    int numTracks = 8;                    // rows in use (5 kit + 3 sample to start)

    float bpm = 90.0f, swing = 58.0f, masterDb = 0.0f;

    // Note data for all 8 patterns. Sounds (drums, pads, samples, tempo) are
    // global - patterns are note data only, like a classic drum machine.
    int   patData[kPatterns][kTracks][kSteps];      // 0 = off, 90 = normal, 127 = accent
    int   currentPattern = 0;                        // the pattern being edited

    // Playlist: a chain of pattern indices played in order in song mode.
    int   songChain[kSongSlots];
    int   songLength = 0;                            // 0 = no song, pattern mode only
    float trackGain[kTracks];
    bool  trackMute[kTracks];
    bool  trackSolo[kTracks];

    float kickTuneHz = 50.0f, kickDecayMs = 600.0f, snareSnappy = 0.6f,
          openHatDecayMs = 380.0f, clapTailMs = 200.0f;

    bool  dustOn = false;
    float dustRateHz = 26000.0f;
    bool  glueOn = true;
    float glueThresholdDb = -12.0f;

    int   vocalMic = 2;                  // index into the five mic models
    float vocalDistanceCm = 15.0f, vocalInputDb = 0.0f;

    // Audio tracks (Fix pass item 35; item 60 made them many): each is its
    // own channel, after the instrument rows in mixer order, and clips live
    // on any of them. kAudioTracks is the capacity; numAudioTracks is how
    // many this project shows. Older files carry one track.
    static constexpr int kAudioTracks = 8;
    static constexpr int kMaxAudioClips = 16;
    int numAudioTracks = 1;
    AudioClip audioClips[kMaxAudioClips];
    float audioGain[kAudioTracks] = { 0.9f };
    bool  audioMute[kAudioTracks] = { false };
    bool  audioSolo[kAudioTracks] = { false };

    // MPC pad bank: per-pad chop/tune/level settings for all 16 pads.
    // (The pads' sample file paths live in ProjectFile.h.)
    static constexpr int kPads = 16;
    float padStart[kPads], padEnd[kPads], padTune[kPads], padGain[kPads], padVelSens[kPads];
    bool  padReverse[kPads];
    int   padMuteGroup[kPads];

    // Piano roll: one monophonic melody lane per pattern.
    // patMelody[p][s] is 0 = off, else a semitone offset stored as value + 25
    // (1..49 for -24..+24). patMelodySource[p]: 0 = the kick, 1..16 = pads 1..16.
    int patMelody[kPatterns][kSteps];
    int patMelodySource[kPatterns];

    // Instruments: two more monophonic lanes per pattern, same note encoding -
    // patSynth plays the synth (root C3), patPiano the piano (root C4).
    int patSynth[kPatterns][kSteps];
    int patPiano[kPatterns][kSteps];

    // Instrument sounds are global, like the drum sounds.
    int   synthWave = 0;              // 0 saw, 1 square, 2 triangle, 3 sine
    float synthCutoffHz = 4500.0f, synthReso = 0.25f,
          synthAttackMs = 8.0f, synthReleaseMs = 240.0f, synthLevel = 0.8f;
    float pianoBright = 0.6f, pianoDecayMs = 2200.0f, pianoLevel = 0.85f;

    // The default is the classic boom-bap starter pattern.
    ProjectSettings()
    {
        for (int p = 0; p < kPatterns; ++p)
        {
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kSteps; ++s) patData[p][t][s] = 0;
            for (int s = 0; s < kSteps; ++s) patMelody[p][s] = patSynth[p][s] = patPiano[p][s] = 0;
            patMelodySource[p] = 0;
        }
        for (int t = 0; t < kTracks; ++t)
        {
            trackGain[t] = 0.8f;
            trackMute[t] = false;
            trackSolo[t] = false;
        }
        for (int i = 0; i < kAudioTracks; ++i) audioGain[i] = 0.9f;
        for (int p = 0; p < kPads; ++p)
        {
            padStart[p] = 0.0f; padEnd[p] = 1.0f; padTune[p] = 0.0f;
            padGain[p] = 1.0f;  padVelSens[p] = 1.0f;
            padReverse[p] = false; padMuteGroup[p] = 0;
        }
        for (int i = 0; i < kSongSlots; ++i) songChain[i] = 0;
        // The starter pattern lives in pattern A; B-H start empty.
        for (int s : { 0, 7, 10 }) patData[0][0][s] = 127;                       // kick
        for (int s : { 4, 12 })    patData[0][1][s] = 127;                       // snare
        for (int s = 0; s < 16; ++s)                                             // closed hat
            patData[0][2][s] = s % 4 == 0 ? 110 : (s % 2 == 0 ? 80 : 45);
        patData[0][3][14] = 90;                                                  // open hat
        patData[0][4][12] = 70;                                                  // clap
        // the pattern is two bars now (item 48): the classic bar repeats into
        // bar 2 so the starter pattern loops the way it always did
        for (int t = 0; t < kTracks; ++t)
            for (int s = 0; s < 16; ++s)
                patData[0][t][s + 16] = patData[0][t][s];
    }

    // Pulls every value into a safe range (used for files written by hand or by newer builds).
    void sanitize()
    {
        bpm = std::clamp (bpm, 40.0f, 220.0f);
        swing = std::clamp (swing, 50.0f, 75.0f);
        masterDb = std::clamp (masterDb, -24.0f, 6.0f);
        for (int t = 0; t < kTracks; ++t)
            trackGain[t] = std::clamp (trackGain[t], 0.0f, 1.2f);
        for (int p = 0; p < kPatterns; ++p)
        {
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kSteps; ++s) patData[p][t][s] = std::clamp (patData[p][t][s], 0, 127);
            for (int s = 0; s < kSteps; ++s)
            {
                patMelody[p][s] = std::clamp (patMelody[p][s], 0, 49);
                patSynth[p][s]  = std::clamp (patSynth[p][s], 0, 49);
                patPiano[p][s]  = std::clamp (patPiano[p][s], 0, 49);
            }
            patMelodySource[p] = std::clamp (patMelodySource[p], 0, 16);
        }
        numTracks = std::clamp (numTracks, 1, kTracks);
        currentPattern = std::clamp (currentPattern, 0, kPatterns - 1);
        for (int i = 0; i < kSongSlots; ++i) songChain[i] = std::clamp (songChain[i], 0, kPatterns - 1);
        songLength = std::clamp (songLength, 0, kSongSlots);
        kickTuneHz = std::clamp (kickTuneHz, 40.0f, 70.0f);
        kickDecayMs = std::clamp (kickDecayMs, 200.0f, 1200.0f);
        snareSnappy = std::clamp (snareSnappy, 0.0f, 1.0f);
        openHatDecayMs = std::clamp (openHatDecayMs, 150.0f, 800.0f);
        clapTailMs = std::clamp (clapTailMs, 80.0f, 400.0f);
        dustRateHz = std::clamp (dustRateHz, 7500.0f, 39375.0f);
        glueThresholdDb = std::clamp (glueThresholdDb, -30.0f, 0.0f);
        synthWave = std::clamp (synthWave, 0, 3);
        synthCutoffHz = std::clamp (synthCutoffHz, 300.0f, 12000.0f);
        synthReso = std::clamp (synthReso, 0.0f, 1.0f);
        synthAttackMs = std::clamp (synthAttackMs, 2.0f, 500.0f);
        synthReleaseMs = std::clamp (synthReleaseMs, 30.0f, 2000.0f);
        synthLevel = std::clamp (synthLevel, 0.0f, 1.2f);
        pianoBright = std::clamp (pianoBright, 0.0f, 1.0f);
        pianoDecayMs = std::clamp (pianoDecayMs, 400.0f, 6000.0f);
        pianoLevel = std::clamp (pianoLevel, 0.0f, 1.2f);
        vocalMic = std::clamp (vocalMic, 0, 7);
        vocalDistanceCm = std::clamp (vocalDistanceCm, 3.0f, 40.0f);
        vocalInputDb = std::clamp (vocalInputDb, -12.0f, 24.0f);
        numAudioTracks = std::clamp (numAudioTracks, 1, kAudioTracks);
        for (auto& c : audioClips)
        {
            c.startSamples  = std::clamp<long long> (c.startSamples, 0, 1LL << 40);
            c.sourceStart   = std::clamp<long long> (c.sourceStart, 0, 1LL << 40);
            c.lengthSamples = std::clamp<long long> (c.lengthSamples, 0, 1LL << 40);
            c.track         = std::clamp (c.track, 0, kAudioTracks - 1);
            if (c.lengthSamples <= 0) c.active = false;
        }
        for (int i = 0; i < kAudioTracks; ++i)
        {
            audioGain[i] = std::clamp (audioGain[i], 0.0f, 1.2f);
        }
        for (int p = 0; p < kPads; ++p)
        {
            padStart[p] = std::clamp (padStart[p], 0.0f, 0.95f);
            padEnd[p] = std::clamp (padEnd[p], 0.05f, 1.0f);
            if (padEnd[p] < padStart[p] + 0.02f) padEnd[p] = std::min (1.0f, padStart[p] + 0.02f);
            padTune[p] = std::clamp (padTune[p], -24.0f, 24.0f);
            padGain[p] = std::clamp (padGain[p], 0.0f, 2.0f);
            padVelSens[p] = std::clamp (padVelSens[p], 0.0f, 1.0f);
            padMuteGroup[p] = std::clamp (padMuteGroup[p], 0, 16);
        }
    }
};
} // namespace boombap
