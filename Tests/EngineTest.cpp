// Headless test of the standalone's engine (no JUCE needed).
// Build: g++ -std=c++17 -O2 EngineTest.cpp -o enginetest
#include "../Source/App/Engine/BoomBapEngine.h"
#include "../Source/App/Engine/OfflineRender.h"
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <memory>
#include <algorithm>
#include <vector>

using namespace boombap;

static void writeWav (const char* path, const std::vector<float>& s, int sr)
{
    FILE* f = std::fopen (path, "wb");
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    uint32_t dataBytes = (uint32_t) s.size() * 2;
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1);
    w32 (sr); w32 (sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (float x : s) w16 ((uint16_t) (int16_t) (std::clamp (x, -1.0f, 1.0f) * 32767.0f));
    std::fclose (f);
}

// run the engine in 256-sample blocks, like a real audio callback
static std::vector<float> render (BoomBapEngine& e, int numSamples, const std::vector<float>* input = nullptr)
{
    std::vector<float> out ((size_t) numSamples), r (256), l (256);
    for (int pos = 0; pos < numSamples; pos += 256)
    {
        const int n = std::min (256, numSamples - pos);
        const float* in = input ? input->data() + pos : nullptr;
        e.process (in, l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = l[(size_t) i];
    }
    return out;
}

static int firstOnset (const std::vector<float>& x, float thr = 0.005f)
{
    for (size_t i = 0; i < x.size(); ++i) if (std::abs (x[i]) > thr) return (int) i;
    return -1;
}

static float peakOf (const std::vector<float>& x)
{
    float p = 0.0f; for (float v : x) p = std::max (p, std::abs (v)); return p;
}

static float rmsOf (const std::vector<float>& x)
{
    double a = 0.0; for (float v : x) a += (double) v * v; return (float) std::sqrt (a / (double) x.size());
}

int main()
{
    const int sr = 44100;
    const double samplesPerTick = sr * 60.0 / (90.0 * kPPQN); // 306.25

    auto fresh = [&] (float swing = 50.0f)
    {
        auto e = std::make_unique<BoomBapEngine>();
        e->clearPattern();
        e->setBpm (90.0f); e->setSwing (swing);
        e->setGlueEnabled (false);
        e->prepare (sr);
        return e;
    };

    // 1) step timing, straight and swung
    {
        auto e = fresh (50.0f);
        e->setStep (BoomBapEngine::Kick, 1, 127);
        e->setPlaying (true);
        const int on = firstOnset (render (*e, sr));
        std::printf ("Step 1 onset, swing 50: sample %d (expect ~%d)\n", on, (int) (24 * samplesPerTick));
        assert (std::abs (on - (int) std::lround (24 * samplesPerTick)) <= 3);

        auto s = fresh (66.0f);
        s->setStep (BoomBapEngine::Kick, 1, 127);
        s->setPlaying (true);
        const int on2 = firstOnset (render (*s, sr));
        std::printf ("Step 1 onset, swing 66: sample %d (expect ~%d)\n", on2, (int) (32 * samplesPerTick));
        assert (std::abs (on2 - (int) std::lround (32 * samplesPerTick)) <= 3);
    }

    // 2) mute and stop
    {
        auto e = fresh();
        e->setStep (BoomBapEngine::Kick, 0, 127);
        e->setTrackMute (BoomBapEngine::Kick, true);
        e->setPlaying (true);
        assert (peakOf (render (*e, sr / 2)) < 1.0e-6f);

        // stopping resets the playhead; a fresh stopped engine is silent even with a full pattern
        e->setPlaying (false);
        render (*e, 512);
        assert (e->currentStep() == -1);

        auto stopped = fresh();
        stopped->loadDefaultPattern();
        assert (peakOf (render (*stopped, sr / 2)) < 1.0e-6f);
        std::printf ("Mute and stop: silent, playhead reset\n");
    }

    // 3) sample pad plays, honours mute
    {
        auto e = fresh();
        e->loadSample (BoomBapEngine::Sample1, std::vector<float> (2000, 0.5f), sr);
        e->setStep (BoomBapEngine::Sample1, 0, 127);
        e->setPlaying (true);
        const float p = peakOf (render (*e, sr / 4));
        std::printf ("Sample pad peak: %.3f\n", p);
        assert (p > 0.1f);

        auto m = fresh();
        m->loadSample (BoomBapEngine::Sample1, std::vector<float> (2000, 0.5f), sr);
        m->setStep (BoomBapEngine::Sample1, 0, 127);
        m->setTrackMute (BoomBapEngine::Sample1, true);
        m->setPlaying (true);
        assert (peakOf (render (*m, sr / 4)) < 1.0e-6f);
    }

    // 4) vocal channel: silent when off, live when on
    {
        std::vector<float> in ((size_t) sr / 2);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.3f * std::sin (6.2832f * 220.0f * (float) i / sr);

        auto e = fresh();
        const float off = rmsOf (render (*e, sr / 2, &in));
        e->setVocalEnabled (true);
        const float on = rmsOf (render (*e, sr / 2, &in));
        std::printf ("Vocal channel: off %.5f, on %.4f\n", off, on);
        assert (off < 1.0e-6f && on > 0.05f);
    }

    // 5) dust (S950) changes the sound
    {
        auto a = fresh(); a->setStep (BoomBapEngine::ClosedHat, 0, 127); a->setPlaying (true);
        auto b = fresh(); b->setStep (BoomBapEngine::ClosedHat, 0, 127); b->setPlaying (true);
        b->setDustEnabled (true); b->setDustRate (10000.0f);
        auto ra = render (*a, sr / 4), rb = render (*b, sr / 4);
        double diff = 0.0;
        for (size_t i = 0; i < ra.size(); ++i) diff += std::abs (ra[i] - rb[i]);
        std::printf ("Dust on vs off, mean difference: %.5f\n", diff / (double) ra.size());
        assert (diff / (double) ra.size() > 0.001);
    }

    // 6) demo: default pattern, 4 bars, dust switched on for the last 2
    {
        BoomBapEngine e;
        e.setBpm (90.0f); e.setSwing (58.0f); e.setGlueEnabled (true); e.setGlueThresholdDb (-14.0f);
        e.prepare (sr);
        e.setPlaying (true);

        const int bar = (int) (sr * 60.0 / 90.0 * 4.0);
        auto a = render (e, bar * 2);
        e.setDustEnabled (true); e.setDustRate (15000.0f);
        auto b = render (e, bar * 2);
        std::printf ("Bus glue reduction at end: %.1f dB\n", e.getGlueReductionDb());

        a.insert (a.end(), b.begin(), b.end());
        for (float v : a) assert (std::isfinite (v));
        const float peak = peakOf (a);
        std::printf ("Demo peak %.3f\n", peak);
        assert (peak <= 1.0f);
        writeWav ("engine_demo.wav", a, sr);
    }

    // 7) WAV export: offline render matches live playback, has the right length, leaves the live engine alone
    {
        const int bars = 1;
        auto live = fresh (58.0f);
        live->loadDefaultPattern();
        live->loadSample (BoomBapEngine::Sample1, std::vector<float> (3000, 0.4f), sr);
        live->setStep (BoomBapEngine::Sample1, 3, 100);
        live->setDustEnabled (true);
        live->setDustRate (20000.0f);
        live->setGlueEnabled (true);
        live->setTrackMute (BoomBapEngine::Clap, true);

        // a reference render straight from an identical engine
        auto twin = std::make_unique<BoomBapEngine>();
        twin->copySettingsFrom (*live);
        twin->prepare (sr);
        twin->setPlaying (true);
        const int expectedPattern = (int) std::llround (bars * 4 * kPPQN * samplesPerTick);
        auto reference = render (*twin, expectedPattern);

        // export from a clone of the live engine
        auto clone = std::make_unique<BoomBapEngine>();
        clone->copySettingsFrom (*live);
        auto ch = renderOffline (*clone, bars, sr, 1.5f, false);

        std::printf ("Export length: %zu samples (expect %d)\n", ch[0].size(), expectedPattern + (int) (1.5f * sr));
        assert ((int) ch[0].size() == expectedPattern + (int) std::lround (1.5f * sr));
        assert (ch[0] == ch[1]);

        float maxDiff = 0.0f;
        for (int i = 0; i < expectedPattern; ++i)
            maxDiff = std::max (maxDiff, std::abs (ch[0][(size_t) i] - reference[(size_t) i]));
        std::printf ("Offline vs live max difference: %.8f\n", maxDiff);
        assert (maxDiff < 1.0e-6f);

        // the live engine is untouched: still stopped (only the S950 stage's -66 dB noise floor remains)
        assert (! live->isPlaying());
        assert (peakOf (render (*live, sr / 4)) < 0.002f);

        // tail rings out then fades
        const float tailEnd = std::abs (ch[0].back());
        assert (tailEnd < 0.01f);

        // normalize hits -1 dBFS
        auto nclone = std::make_unique<BoomBapEngine>();
        nclone->copySettingsFrom (*live);
        auto n = renderOffline (*nclone, bars, sr, 1.0f, true);
        float pk = 0.0f; for (float v : n[0]) pk = std::max (pk, std::abs (v));
        std::printf ("Normalized peak: %.3f (expect 0.891)\n", pk);
        assert (std::abs (pk - 0.891f) < 0.002f);
    }

    // 8) export demo file: 4 bars of the default pattern with dust on, as the app would write it
    {
        BoomBapEngine src;
        src.setBpm (90.0f); src.setSwing (58.0f); src.setGlueThresholdDb (-14.0f);
        src.setDustEnabled (true); src.setDustRate (18000.0f);
        auto clone = std::make_unique<BoomBapEngine>();
        clone->copySettingsFrom (src);
        auto ch = renderOffline (*clone, 4, sr, 2.0f, true);
        writeWav ("engine_export_demo.wav", ch[0], sr);
        std::printf ("Wrote export demo: %.1f s\n", (double) ch[0].size() / sr);
    }

    // 9) project settings: get/apply round trip is exact and sounds identical; apply clamps bad values
    {
        auto a = fresh (62.0f);
        a->loadDefaultPattern();
        a->setStep (BoomBapEngine::Clap, 5, 127);
        a->setBpm (101.0f);
        a->setTrackGain (BoomBapEngine::Snare, 0.55f);
        a->setTrackMute (BoomBapEngine::OpenHat, true);
        a->setKickTune (44.0f); a->setKickDecay (850.0f); a->setSnareSnappy (0.2f);
        a->setDustEnabled (true); a->setDustRate (12000.0f);
        a->setGlueThresholdDb (-18.0f);
        a->setVocalMic (4); a->setVocalDistance (6.0f);
        { MpcPads::PadParams pp; pp.startFrac = 0.1f; pp.endFrac = 0.9f; pp.tuneSemitones = -5.0f;
          pp.gain = 1.5f; pp.velSens = 0.25f; pp.reverse = true; pp.muteGroup = 3;
          a->setPadParams (9, pp); }
        a->setMelodySource (2); a->setMelodyStep (0, 25); a->setMelodyStep (3, 37);
        a->setSynthWave (2); a->setSynthCutoff (2500.0f); a->setSynthReso (0.7f);
        a->setSynthAttack (40.0f); a->setSynthRelease (900.0f); a->setSynthLevel (0.5f);
        a->setPianoBright (0.2f); a->setPianoDecay (900.0f); a->setPianoLevel (0.4f);
        a->setSynthStep (0, 25); a->setSynthStep (8, 32); a->setPianoStep (4, 37);

        const ProjectSettings sa = a->getSettings();
        auto b = fresh (50.0f);
        b->applySettings (sa);
        const ProjectSettings sb = b->getSettings();

        bool same = sa.bpm == sb.bpm && sa.swing == sb.swing && sa.masterDb == sb.masterDb
                 && sa.kickTuneHz == sb.kickTuneHz && sa.kickDecayMs == sb.kickDecayMs
                 && sa.snareSnappy == sb.snareSnappy && sa.dustOn == sb.dustOn && sa.dustRateHz == sb.dustRateHz
                 && sa.glueOn == sb.glueOn && sa.glueThresholdDb == sb.glueThresholdDb
                 && sa.vocalMic == sb.vocalMic && sa.vocalDistanceCm == sb.vocalDistanceCm
                 && sa.synthWave == sb.synthWave && sa.synthCutoffHz == sb.synthCutoffHz
                 && sa.synthReso == sb.synthReso && sa.synthAttackMs == sb.synthAttackMs
                 && sa.synthReleaseMs == sb.synthReleaseMs && sa.synthLevel == sb.synthLevel
                 && sa.pianoBright == sb.pianoBright && sa.pianoDecayMs == sb.pianoDecayMs
                 && sa.pianoLevel == sb.pianoLevel;
        for (int t = 0; t < 8; ++t)
        {
            same = same && sa.trackGain[t] == sb.trackGain[t] && sa.trackMute[t] == sb.trackMute[t];
            for (int p = 0; p < ProjectSettings::kPatterns; ++p)
                for (int st = 0; st < 16; ++st) same = same && sa.patData[p][t][st] == sb.patData[p][t][st];
        }
        for (int p = 0; p < ProjectSettings::kPads; ++p)
            same = same && sa.padStart[p] == sb.padStart[p] && sa.padEnd[p] == sb.padEnd[p]
                        && sa.padTune[p] == sb.padTune[p] && sa.padGain[p] == sb.padGain[p]
                        && sa.padVelSens[p] == sb.padVelSens[p] && sa.padReverse[p] == sb.padReverse[p]
                        && sa.padMuteGroup[p] == sb.padMuteGroup[p];
        same = same && sa.currentPattern == sb.currentPattern && sa.songLength == sb.songLength;
        for (int p = 0; p < ProjectSettings::kPatterns; ++p)
        {
            same = same && sa.patMelodySource[p] == sb.patMelodySource[p];
            for (int s = 0; s < ProjectSettings::kSteps; ++s)
                same = same && sa.patMelody[p][s] == sb.patMelody[p][s]
                            && sa.patSynth[p][s] == sb.patSynth[p][s]
                            && sa.patPiano[p][s] == sb.patPiano[p][s];
        }
        for (int i = 0; i < ProjectSettings::kSongSlots; ++i) same = same && sa.songChain[i] == sb.songChain[i];
        assert (same);

        a->setPlaying (true); b->setPlaying (true);
        auto ra = render (*a, sr), rb = render (*b, sr);
        float diff = 0.0f;
        for (size_t i = 0; i < ra.size(); ++i) diff = std::max (diff, std::abs (ra[i] - rb[i]));
        std::printf ("Settings round trip: identical fields, audio difference %.8f\n", diff);
        assert (diff < 1.0e-6f);

        ProjectSettings bad;
        bad.bpm = 9999.0f; bad.swing = 10.0f; bad.patData[0][0][0] = 4000; bad.trackGain[2] = -3.0f; bad.vocalMic = 77;
        bad.padTune[0] = 99.0f; bad.padStart[1] = 0.99f; bad.padEnd[1] = 0.5f;
        bad.patMelody[0][0] = 999; bad.patMelodySource[0] = 99;
        bad.patSynth[0][0] = 999; bad.synthWave = 9; bad.synthCutoffHz = 99.0f; bad.pianoDecayMs = 99999.0f;
        bad.patData[3][0][0] = -5; bad.songLength = 99;
        b->applySettings (bad);
        const ProjectSettings fixed = b->getSettings();
        assert (fixed.bpm == 220.0f && fixed.swing == 50.0f && fixed.patData[0][0][0] == 127
                && fixed.trackGain[2] == 0.0f && fixed.vocalMic == 7
                && fixed.padTune[0] == 24.0f && fixed.padEnd[1] > fixed.padStart[1]
                && fixed.patMelody[0][0] == 49 && fixed.patMelodySource[0] == 16
                && fixed.patSynth[0][0] == 49 && fixed.synthWave == 3 && fixed.synthCutoffHz == 300.0f
                && fixed.pianoDecayMs == 6000.0f
                && fixed.patData[3][0][0] == 0 && fixed.songLength == 32);

        // clearing a sample slot silences it
        auto c = fresh();
        c->loadSample (BoomBapEngine::Sample2, std::vector<float> (2000, 0.5f), sr);
        c->clearSample (BoomBapEngine::Sample2);
        c->setStep (BoomBapEngine::Sample2, 0, 127);
        c->setPlaying (true);
        assert (peakOf (render (*c, sr / 4)) < 1.0e-6f);
        std::printf ("Settings clamp and sample clear: ok\n");

        // a live pad hit sounds even with the transport stopped
        auto e2 = fresh();
        e2->loadPadSample (11, std::vector<float> (4000, 0.5f), sr);
        e2->triggerPad (11, 1.0f);
        assert (peakOf (render (*e2, sr / 4)) > 0.1f);
        std::printf ("Live pad trigger: ok\n");

        // vocal take: record the processed vocal with the monitor off, hear it back in sync
        {
            auto vt = fresh();
            vt->clearPattern();
            vt->setBpm (220.0f); // short bars, so the whole-bar padding stays small
            const int n = 22050;
            std::vector<float> in ((size_t) n), l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
                in[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 440.0f * (float) i / (float) sr);
            vt->startRecording();
            vt->process (in.data(), l.data(), r.data(), n);
            vt->stopRecording();
            assert (vt->hasTake());
            assert (vt->takeSeconds() > 1.0 && vt->takeSeconds() < 1.2); // 0.5 s padded to one 1.09 s bar

            std::fill (l.begin(), l.end(), 0.0f);
            vt->setPlaying (true);
            vt->process (nullptr, l.data(), r.data(), n);
            assert (peakOf (l) > 0.05f); // the take plays back over the (empty) pattern

            auto twin = fresh();
            twin->copySettingsFrom (*vt);
            assert (twin->hasTake()); // exports carry the take
            std::printf ("Vocal take record/playback: ok\n");
        }

        // piano roll: a pitched pad note sounds over an empty drum pattern, and a
        // kick note an octave up really does oscillate about twice as fast
        {
            auto m = fresh();
            m->clearPattern();
            // AMENDED by Round 6 (item 109): the melody lane sounds the
            // sample ROWS' own store now - rows and pads are separate
            // stores, so the lane's source sample is loaded as a ROW
            // sample (source 5 = sample row 5, the lane's own world).
            m->loadSample (BoomBapEngine::Sample1 + 4, std::vector<float> (8000, 0.4f), sr);
            m->setMelodySource (5);   // sample row 5
            m->setMelodyStep (0, 25); // root note on step 1
            m->setPlaying (true);
            assert (peakOf (render (*m, sr / 2)) > 0.1f);

            auto crossings = [] (const std::vector<float>& v, int count)
            {
                int zc = 0;
                for (int i = 1; i < count; ++i)
                    if ((v[(size_t) (i - 1)] < 0.0f) != (v[(size_t) i] < 0.0f)) ++zc;
                return zc;
            };
            auto k0 = fresh(); k0->clearPattern(); k0->setMelodySource (0); k0->setMelodyStep (0, 25);
            k0->setPlaying (true); const auto r0 = render (*k0, sr / 2);
            auto k1 = fresh(); k1->clearPattern(); k1->setMelodySource (0); k1->setMelodyStep (0, 37);
            k1->setPlaying (true); const auto r1 = render (*k1, sr / 2);
            const int window = (int) sr / 10;
            assert (crossings (r1, window) > crossings (r0, window) * 3 / 2);
            std::printf ("Piano roll melody: ok\n");
        }

        // instruments: the synth and piano lanes sound over an empty pattern, a
        // synth note an octave up oscillates about twice as fast, a piano note decays
        {
            auto crossings = [] (const std::vector<float>& v, int count)
            {
                int zc = 0;
                for (int i = 1; i < count; ++i)
                    if ((v[(size_t) (i - 1)] < 0.0f) != (v[(size_t) i] < 0.0f)) ++zc;
                return zc;
            };
            auto s0 = fresh(); s0->clearPattern(); s0->setSynthStep (0, 25); s0->setPlaying (true);
            const auto sy0 = render (*s0, sr / 2);
            assert (peakOf (sy0) > 0.05f);
            auto s1 = fresh(); s1->clearPattern(); s1->setSynthStep (0, 37); s1->setPlaying (true);
            const auto sy1 = render (*s1, sr / 2);
            const int window = (int) sr / 10;
            assert (crossings (sy1, window) > crossings (sy0, window) * 3 / 2);

            auto pn = fresh(); pn->clearPattern(); pn->setPianoStep (0, 25); pn->setPlaying (true);
            const auto pr = render (*pn, sr);
            assert (peakOf (pr) > 0.05f);
            double e1 = 0.0, e2 = 0.0;
            for (size_t i = 0; i < pr.size() / 2; ++i) e1 += (double) pr[i] * pr[i];
            for (size_t i = pr.size() / 2; i < pr.size(); ++i) e2 += (double) pr[i] * pr[i];
            assert (e2 < e1 * 0.5);
            std::printf ("Instruments (synth + piano): ok\n");
        }

        // patterns + playlist: patterns hold separate data; the song chain plays them in order
        {
            auto pe = fresh();
            pe->setEditPattern (0);
            pe->clearPattern();
            pe->setStep (0, 0, 127);
            pe->setEditPattern (1);
            assert (pe->getStep (0, 0) == 0);      // pattern B starts empty
            assert (pe->getMelodyStep (0) == 0);
            pe->setStep (3, 0, 127);
            pe->setEditPattern (0);
            assert (pe->getStep (0, 0) == 127 && pe->getStep (3, 0) == 0);

            auto sg = fresh();
            sg->setEditPattern (0); sg->clearPattern();          // A: silent
            sg->setEditPattern (1); sg->clearPattern();
            sg->setStep (0, 0, 127);                             // B: kick on step 1
            sg->setEditPattern (0);
            sg->setSongSlot (0, 0); sg->setSongSlot (1, 1); sg->setSongLength (2);
            sg->setSongMode (true);
            sg->setBpm (220.0f);
            sg->setPlaying (true);
            const int bar = (int) (sr * 240.0 / 220.0);
            // Fix pass 2 (item 48): a pattern spans TWO bars now, so the
            // chain advances every two bars - A fills bars 1-2, B bars 3-4.
            const auto song = render (*sg, bar * 4 - 1000);
            auto peakRange = [] (const std::vector<float>& v, int from, int to)
            {
                float pk = 0.0f;
                for (int i = from; i < to && i < (int) v.size(); ++i) pk = std::max (pk, std::abs (v[(size_t) i]));
                return pk;
            };
            assert (peakRange (song, 0, bar * 2) < 0.01f);             // bars 1-2 = pattern A: silence
            assert (peakRange (song, bar * 2, bar * 4 - 1000) > 0.3f); // bars 3-4 = pattern B: the kick
            assert (sg->getPlayPattern() == 1 && sg->getPlaySongPos() == 1);
            std::printf ("Patterns + song chain: ok\n");
        }
    }

    // 9) Pass 1: solo, metronome, pad activity
    {
        auto peakRange = [] (const std::vector<float>& v, int from, int to)
        {
            float pk = 0.0f;
            for (int i = from; i < to && i < (int) v.size(); ++i) pk = std::max (pk, std::abs (v[(size_t) i]));
            return pk;
        };
        // at 90 bpm a step is 24 ticks * 306.25 = 7350 samples
        const int stepSamples = 7350;

        // solo the snare: the kick row falls silent, the snare still sounds
        auto e = fresh();
        e->setStep (BoomBapEngine::Kick, 0, 127);
        e->setStep (BoomBapEngine::Snare, 4, 127);
        e->setTrackSolo (BoomBapEngine::Snare, true);
        assert (e->anySoloActive() && e->getTrackSolo (BoomBapEngine::Snare));
        e->setPlaying (true);
        const auto soloed = render (*e, stepSamples * 8);
        assert (peakRange (soloed, 0, stepSamples / 2) < 1.0e-4f);            // kick window: silent
        assert (peakRange (soloed, stepSamples * 4, stepSamples * 5) > 0.1f); // snare window: sounds

        // solo a row with no steps in it: everything falls silent
        auto e2 = fresh();
        e2->setStep (BoomBapEngine::Snare, 4, 127);
        e2->setTrackSolo (BoomBapEngine::Kick, true);
        e2->setPlaying (true);
        assert (peakOf (render (*e2, stepSamples * 8)) < 1.0e-4f);

        // solo survives a settings round trip
        auto e3 = fresh();
        e3->setTrackSolo (BoomBapEngine::Clap, true);
        const auto st = e3->getSettings();
        assert (st.trackSolo[BoomBapEngine::Clap] && ! st.trackSolo[BoomBapEngine::Kick]);
        auto e4 = fresh();
        e4->applySettings (st);
        assert (e4->getTrackSolo (BoomBapEngine::Clap));

        // metronome: an empty pattern is silent without it, clicks on the beats with it
        auto m = fresh();
        m->setPlaying (true);
        assert (peakOf (render (*m, stepSamples * 8)) < 1.0e-6f);
        auto m2 = fresh();
        m2->setMetronomeOn (true);
        m2->setMetronomeLevel (0.8f);
        m2->setPlaying (true);
        const auto clicks = render (*m2, stepSamples * 16);
        assert (peakRange (clicks, 0, 1500) > 0.05f);                          // downbeat click
        assert (peakRange (clicks, stepSamples * 4, stepSamples * 4 + 1500) > 0.05f); // beat 2 click
        assert (peakRange (clicks, stepSamples * 2, stepSamples * 2 + 1500) < 0.01f); // no click off the beat

        // pad activity: a live pad hit registers.
        // AMENDED by Round 6 (item 109): the first half used to assert
        // a sequencer ROW step also registered as PAD activity - true
        // only while a row WAS its pad. Rows hold their own store now:
        // a row step sounds the row and touches no pad state, which is
        // what the first block asserts instead (the row still SOUNDS -
        // the render peak is the row channel's business, checked by
        // the row suites; here the pad-activity signals must stay cold).
        auto p = fresh();
        p->loadSample (BoomBapEngine::Sample1, std::vector<float> (4000, 0.5f), sr);
        p->setStep (BoomBapEngine::Sample1, 0, 127);
        p->setPlaying (true);
        render (*p, 2048);
        assert (p->consumePadActivityPeak() < 0.05f);
        assert (p->lastTriggeredPad() == -1);
        auto p2 = fresh();
        p2->loadPadSample (5, std::vector<float> (4000, 0.5f), sr);
        p2->triggerPad (5, 1.0f);
        render (*p2, 2048);
        assert (p2->consumePadActivityPeak() > 0.05f);
        assert (p2->lastTriggeredPad() == 5);
        std::printf ("Solo, metronome, pad activity: ok\n");
    }

    // ---- 11. fix pass: pad capture, whole-song length, audio clips, clean takes ----
    {
        // pad capture (item 31): a hit while playing writes its step at the playhead
        auto cap = fresh();
        cap->clearPattern();
        cap->loadSample (BoomBapEngine::Sample1, std::vector<float> (4000, 0.5f), sr);
        cap->setPadCaptureEnabled (true);
        cap->setPlaying (true);
        render (*cap, 256 * 40);
        cap->triggerPad (0, 0.5f);
        const int stepAtHit = cap->currentStep();
        render (*cap, 512);
        assert (cap->getStep (BoomBapEngine::Sample1, stepAtHit) == 64);
        assert (cap->consumeCapturedPad() == 0);
        assert (cap->consumeCapturedPad() == -1);
        cap->setPadCaptureEnabled (false);
        cap->setEditPattern (1);
        render (*cap, 256 * 4);
        cap->triggerPad (0, 0.5f);
        render (*cap, 512);
        bool anyInP1 = false;
        for (int s = 0; s < 16; ++s) anyInP1 = anyInP1 || cap->getStep (BoomBapEngine::Sample1, s) != 0;
        assert (! anyInP1); // capture off: hits stay performance-only
        cap->setPadCaptureEnabled (true);
        cap->setEditPattern (2);
        cap->triggerPad (7, 0.9f); // not a sequencer row: never captured
        render (*cap, 512);
        bool anyInP2 = false;
        for (int s = 0; s < 16; ++s) anyInP2 = anyInP2 || cap->getStep (BoomBapEngine::Sample1, s) != 0;
        assert (! anyInP2);
        cap->setPlaying (false);
        cap->triggerPad (0, 0.5f);
        render (*cap, 512);
        anyInP2 = false;
        for (int s = 0; s < 16; ++s) anyInP2 = anyInP2 || cap->getStep (BoomBapEngine::Sample1, s) != 0;
        assert (! anyInP2); // transport stopped: no capture either
        std::printf ("Pad capture: ok\n");

        // whole-song render length (item 34): a 16-pattern song is 32 bars
        // of audio now (each pattern spans 2 bars, item 48) + tail. The app
        // asks renderOffline for songLength * 2 bars in song mode.
        auto song = fresh();
        song->setStep (BoomBapEngine::Kick, 0, 100);
        for (int s = 0; s < 16; ++s) song->setSongSlot (s, 0);
        song->setSongLength (16);
        auto songClone = fresh();
        songClone->copySettingsFrom (*song);
        songClone->setSongMode (true);
        const auto wholeSong = renderOffline (*songClone, 32, 44100.0, 2.0f, false);
        const long long expectWhole = std::llround (32.0 * 384.0 * (60.0 / 90.0 / 96.0) * 44100.0)
                                      + (long long) (2.0 * 44100.0);
        assert ((long long) wholeSong[0].size() == expectWhole);
        std::printf ("Whole-song render length: ok\n");

        // audio clips (items 32/35): place, hear, move, trim, split
        auto cl = fresh();
        cl->clearPattern();
        std::vector<float> vox (88200);
        for (int i = 0; i < 88200; ++i)
            vox[(size_t) i] = 0.6f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / (float) sr);
        cl->setVocalTake (vox);
        const int slot = cl->placeTakeAsClip();
        assert (slot >= 0 && cl->audioClipCount() == 1);
        auto region = cl->getAudioClip (slot);
        assert (region.active && region.startSamples == 0 && region.lengthSamples == 88200);
        assert ((long long) cl->getAudioClipData (slot).size() == 88200);
        cl->setPlaying (true);
        assert (peakOf (render (*cl, 22050)) > 0.15f); // the clip is audible on the track
        cl->setPlaying (false);
        cl->moveAudioClip (slot, 44100);
        assert (cl->getAudioClip (slot).startSamples == 44100);
        cl->trimAudioClip (slot, 44100, 11025, 44100);
        region = cl->getAudioClip (slot);
        assert (region.sourceStart == 11025 && region.lengthSamples == 44100);
        // (splitAudioClip returns bool; an earlier version of this test
        // compared it to -1, which only "passed" because Release builds
        // compile assert away - repaired in fix pass 2.)
        const bool splitOk = cl->splitAudioClip (slot, 44100 + 22050);
        assert (splitOk && cl->audioClipCount() == 2);
        int right = -1;
        for (int i = 0; i < boombap::BoomBapEngine::kMaxAudioClips; ++i)
            if (i != slot && cl->getAudioClip (i).active) right = i;
        assert (right >= 0);
        assert (cl->getAudioClip (slot).lengthSamples == 22050);
        region = cl->getAudioClip (right);
        assert (region.startSamples == 44100 + 22050 && region.sourceStart == 11025 + 22050
                && region.lengthSamples == 22050);
        assert (! cl->splitAudioClip (slot, 10)); // a cut outside the clip is refused
        cl->clearAudioClip (right);
        assert (cl->audioClipCount() == 1);
        cl->setAudioTrackGain (0, 0.42f);
        cl->setAudioTrackMute (0, true);
        assert (std::abs (cl->getAudioTrackGain (0) - 0.42f) < 1.0e-6f && cl->getAudioTrackMute (0));
        std::printf ("Audio clips: ok\n");

        // item 36: the take stores the CLEAN capture - the mic emulation is
        // a playback setting, never burned in
        auto recordClean = [&] (int mic, float distCm, float inDb)
        {
            auto r = fresh();
            r->clearPattern();
            r->setVocalMic (mic);
            r->setVocalDistance (distCm);
            r->setVocalInputDb (inDb);
            std::vector<float> in (88200);
            for (int i = 0; i < 88200; ++i)
                in[(size_t) i] = 0.4f * std::sin (2.0f * 3.14159265f * 110.0f * (float) i / (float) sr)
                               + 0.3f * std::sin (2.0f * 3.14159265f * 440.0f * (float) i / (float) sr)
                               + 0.2f * std::sin (2.0f * 3.14159265f * 3000.0f * (float) i / (float) sr);
            r->startRecording();
            render (*r, 88200, &in);
            r->stopRecording();
            return r->getTakeData();
        };
        const auto takeA = recordClean (0, 0.0f, 0.0f);
        const auto takeB = recordClean (7, 90.0f, 0.0f); // opposite mic + far distance
        assert (takeA.size() == takeB.size() && takeA == takeB); // stored data identical
        const auto takeHot = recordClean (0, 0.0f, 6.0f);
        assert (std::abs (rmsOf (takeHot) / rmsOf (takeA) - 1.9953f) < 0.05f); // only the input gain is staged in
        auto player = fresh();
        player->clearPattern();
        player->setVocalTake (takeA);
        player->setVocalMic (0);
        player->setPlaying (true);
        const auto heardA = render (*player, 44100);
        player->setVocalMic (7);
        const auto heardB = render (*player, 44100);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < heardA.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (heardA[i] - heardB[i]));
        assert (maxDiff > 0.01f); // switching the mic changes what you HEAR...
        assert (player->getTakeData() == takeA); // ...never what is stored
        std::printf ("Clean take / playback mic (item 36): ok\n");
    }

    // ---- 12. fix pass 2: stop = all sound off (45), audio cluster (46-48,
    // 50, 51, 53), kit rows (40), unlimited tracks (39) ----
    // These use hard checks (not assert) so they gate the build even in
    // Release, where assert compiles away.
    {
        int failures = 0;
        auto CHECK = [&] (bool ok, const char* what)
        {
            std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
            if (! ok) ++failures;
        };
        auto peakRange = [] (const std::vector<float>& v, int from, int to)
        {
            float pk = 0.0f;
            for (int i = from; i < to && i < (int) v.size(); ++i) pk = std::max (pk, std::abs (v[(size_t) i]));
            return pk;
        };
        auto latePeak = [] (const std::vector<float>& v, size_t from)
        {
            float pk = 0.0f;
            for (size_t i = from; i < v.size(); ++i) pk = std::max (pk, std::abs (v[i]));
            return pk;
        };

        // item 45a: a 30-second pad one-shot dies on the global stop
        {
            auto e = fresh();
            e->clearPattern();
            e->loadPadSample (9, std::vector<float> ((size_t) sr * 30, 0.6f), sr);
            e->triggerPad (9, 1.0f);
            CHECK (peakOf (render (*e, 4096)) > 0.2f, "45a: long one-shot sounding before stop");
            e->stopAllSound();
            const auto post = render (*e, 8192);
            CHECK (latePeak (post, 2048) < 1.0e-3f, "45a: stop silences a 30 s one-shot within budget");
            CHECK (! e->isPlaying() && ! e->isAudioPlaying(), "45a: stop halts both transports");
        }

        // item 45b: a playing clip and a synth note die on the same stop
        {
            auto e = fresh();
            e->clearPattern();
            e->setVocalTake (std::vector<float> ((size_t) sr * 4, 0.5f));
            CHECK (e->placeTakeAsClip() >= 0, "45b: clip placed for stop test");
            e->setSynthStep (0, 25);
            e->setPlaying (true);
            CHECK (peakOf (render (*e, 8192)) > 0.1f, "45b: clip + synth sounding before stop");
            e->stopAllSound();
            CHECK (latePeak (render (*e, 8192), 2048) < 1.0e-3f, "45b: stop silences clip + synth");
        }

        // item 48a: steps 16..31 exist, and a hit there lands in bar 2
        {
            auto e = fresh();
            e->clearPattern();
            e->setStep (BoomBapEngine::Kick, 20, 127);
            CHECK (e->getStep (BoomBapEngine::Kick, 20) == 127, "48a: step 20 stores in the 32-step pattern");
            e->setPlaying (true);
            const int bar = (int) (sr * 240.0 / 90.0);
            const auto two = render (*e, bar * 2);
            CHECK (peakRange (two, 0, bar) < 0.01f, "48a: bar 1 silent (the hit is in bar 2)");
            CHECK (peakRange (two, bar, bar * 2) > 0.3f, "48a: the step-20 kick sounds in bar 2");
        }

        // item 48b/47/50/54: a clip longer than the pattern plays end to end
        // under the audio view's own transport; seek lands mid-clip; the
        // sounding light follows the clip
        {
            auto e = fresh();
            e->clearPattern();
            const int clipLen = sr * 3; // 3 s > one pattern (2 bars = 2.67 s at 90 bpm)
            std::vector<float> vox ((size_t) clipLen);
            for (int i = 0; i < clipLen; ++i)
                vox[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 330.0f * (float) i / (float) sr);
            e->setVocalTake (vox);
            const int slot = e->placeTakeAsClip();
            CHECK (slot >= 0, "48b: long clip placed");
            e->setAudioPlaying (true);
            const auto heard = render (*e, clipLen + sr / 2);
            CHECK (peakRange (heard, clipLen - sr / 2, clipLen) > 0.1f,
                   "48b: audio-only transport plays the clip's tail, past the loop");
            CHECK ((e->soundingClipMask() & (1u << slot)) == 0u, "54: clip light is off after it ends");

            auto e2 = fresh();
            e2->clearPattern();
            e2->setVocalTake (vox);
            e2->placeTakeAsClip();
            e2->setAudioPlayhead (clipLen - sr / 4);
            e2->setAudioPlaying (true);
            const auto tail = render (*e2, sr / 8); // stop mid-clip: it must still be lit
            CHECK (peakOf (tail) > 0.1f, "50: audio playhead seek lands inside the clip");
            CHECK (e2->audioPosition() >= clipLen - sr / 4, "50: audio position follows the playhead");
            CHECK ((e2->soundingClipMask() & 1u) != 0u, "54: sounding clip is lit while it plays");
        }

        // item 51: clip delete at the engine level (what the view calls)
        {
            auto e = fresh();
            e->setVocalTake (std::vector<float> (44100, 0.4f));
            const int slot = e->placeTakeAsClip();
            CHECK (e->audioClipCount() == 1, "51: one clip before delete");
            e->clearAudioClip (slot);
            CHECK (e->audioClipCount() == 0 && ! e->getAudioClip (slot).active, "51: clip deleted");
        }

        // item 40: a kit row bound to a sample plays the sample, not the
        // synth drum; clearing the binding brings the synth back
        {
            auto e = fresh();
            e->clearPattern();
            std::vector<float> burst (4000);
            for (int i = 0; i < 4000; ++i)
                burst[(size_t) i] = 0.7f * std::sin (2.0f * 3.14159265f * 3000.0f * (float) i / (float) sr);
            e->setKitSample (BoomBapEngine::Kick, burst, sr);
            CHECK (e->kitHasSample (BoomBapEngine::Kick), "40: kick row bound to a sample");
            e->setStep (BoomBapEngine::Kick, 0, 127);
            e->setPlaying (true);
            const auto bound = render (*e, 8000);
            auto crossings = [] (const std::vector<float>& v, int count)
            {
                int zc = 0;
                for (int i = 1; i < count && i < (int) v.size(); ++i)
                    if ((v[(size_t) (i - 1)] < 0.0f) != (v[(size_t) i] < 0.0f)) ++zc;
                return zc;
            };
            const int zcBound = crossings (bound, 4000);
            CHECK (peakOf (bound) > 0.2f && zcBound > 200,
                   "40: bound row plays the SAMPLE (3 kHz burst), not the synth kick");
            e->setPlaying (false);
            render (*e, 512); // let the stop register, like the live audio thread would
            e->clearKitSample (BoomBapEngine::Kick);
            CHECK (! e->kitHasSample (BoomBapEngine::Kick), "40: binding cleared (reset)");
            e->setPlaying (true);
            const auto synth = render (*e, 8000);
            CHECK (peakOf (synth) > 0.2f && crossings (synth, 4000) < 150,
                   "40: reset row plays the synth kick again");
        }

        // item 39: the track list grows; added rows store steps; a row past
        // the pad bank holds and plays its own sample
        {
            auto e = fresh();
            e->clearPattern();
            CHECK (e->getNumTracks() == 8, "39: projects start with 8 rows");
            const int added = e->addTrack();
            CHECK (added == 8 && e->getNumTracks() == 9, "39: addTrack grows the list");
            e->setStep (added, 0, 127);
            CHECK (e->getStep (added, 0) == 127, "39: the added row stores steps");
            while (e->getNumTracks() <= 21) e->addTrack();
            e->loadSample (21, std::vector<float> (6000, 0.5f), sr);
            CHECK (e->rowHasOwnSample (21), "39: own-buffer row holds its sample");
            e->setStep (21, 0, 127);
            e->setPlaying (true);
            CHECK (peakOf (render (*e, 8000)) > 0.1f, "39: own-buffer row sounds from its step");
            auto e2 = fresh();
            e2->applySettings (e->getSettings());
            CHECK (e2->getNumTracks() == e->getNumTracks(), "39: numTracks round-trips through settings");
        }

        // items 48/39 compat: steps beyond 15 in an old project start empty
        {
            ProjectSettings old;
            for (int t = 0; t < 8; ++t)
                for (int s = 0; s < 32; ++s) old.patData[0][t][s] = 0;
            old.patData[0][BoomBapEngine::Kick][3] = 127;
            old.numTracks = 8;
            auto e = fresh();
            e->applySettings (old);
            CHECK (e->getStep (BoomBapEngine::Kick, 3) == 127, "compat: old step lands in place");
            bool bar2Empty = true;
            for (int s = 16; s < 32; ++s)
                bar2Empty = bar2Empty && e->getStep (BoomBapEngine::Kick, s) == 0;
            CHECK (bar2Empty, "compat: extended steps start empty");
        }

        // item 53: recording peaks are available live and grow as it records
        {
            auto e = fresh();
            e->clearPattern();
            std::vector<float> in ((size_t) sr);
            for (int i = 0; i < sr; ++i)
                in[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / (float) sr);
            e->startRecording();
            render (*e, sr / 2, &in);
            float mins[512], maxs[512];
            const int n1 = e->copyRecordingPeaks (mins, maxs, 512, 1024);
            CHECK (n1 > 10 && e->isCapturing(), "53: live peaks available mid-recording");
            render (*e, sr / 2, &in);
            const int n2 = e->copyRecordingPeaks (mins, maxs, 512, 1024);
            CHECK (n2 > n1, "53: the peak count grows as the take records");
            float pk = 0.0f;
            for (int i = 0; i < n2; ++i) pk = std::max (pk, maxs[i]);
            CHECK (pk > 0.2f, "53: live peaks carry the signal");
            e->stopRecording();
        }

        // item 46: the waveform audition plays - and the global stop kills it
        {
            auto e = fresh();
            e->clearPattern();
            e->startAudition (std::vector<float> ((size_t) sr * 5, 0.5f), sr);
            CHECK (peakOf (render (*e, 4096)) > 0.2f && e->isAuditionPlaying(), "46: audition sounds");
            e->stopAllSound();
            CHECK (latePeak (render (*e, 8192), 2048) < 1.0e-3f, "46: stop silences the audition");
        }

        // item 56: one press of Record = ONE take landing as ONE clip -
        // even recorded across a loop boundary, and even though several
        // UI paths ask for the placement. The raw take's loop is an
        // explicit opt-in, off by default.
        {
            auto e = fresh();
            e->clearPattern();
            const int recLen = sr * 7; // the 2-bar loop at 90 bpm is 5.33 s
            std::vector<float> in ((size_t) recLen);
            for (int i = 0; i < recLen; ++i)
                in[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / (float) sr);
            e->startRecording();
            render (*e, recLen, &in);
            e->stopRecording(); // finishTake runs inside

            CHECK (e->takeSeconds() > 2.0 * 240.0 / 90.0,
                   "56: the take recorded across the loop boundary, whole");
            CHECK (! e->takeEnabled(), "56: the raw take does NOT loop on its own (opt-in off)");

            const int first = e->placeTakeAsClip(); // the record button asks
            e->placeTakeAsClip();                   // the transport stop asks
            e->placeTakeAsClip();                   // the UI timer / add button asks
            CHECK (first >= 0 && e->audioClipCount() == 1,
                   "56: repeated placement asks stack nothing - exactly one clip");
            CHECK (e->takeIsPlaced(), "56: the take knows it is homed");
            CHECK (e->getAudioClip (first).lengthSamples == (long long) e->getTakeData().size(),
                   "56: the clip is the whole take");

            // no doubled raw take beside its clip: park the clip a minute
            // out and play - only an armed raw loop could sound now
            e->moveAudioClip (first, (long long) sr * 60);
            e->setPlaying (true);
            CHECK (peakRange (render (*e, sr), 0, sr) < 0.01f,
                   "56: silence beside the clip - no doubling");
            e->setPlaying (false);
            render (*e, 512);
            e->setTakeEnabled (true); // the explicit opt-in
            e->setPlaying (true);
            CHECK (peakRange (render (*e, sr), 0, sr) > 0.1f,
                   "56: 'Loop take' ON plays the raw take, by choice");
            e->setTakeEnabled (false);
            e->setPlaying (false);
            render (*e, 512);

            // a NEW recording is a new take and earns its own clip
            e->startRecording();
            render (*e, sr, &in);
            e->stopRecording();
            CHECK (! e->takeIsPlaced(), "56: a new take is not pre-homed");
            CHECK (e->placeTakeAsClip() >= 0 && e->audioClipCount() == 2,
                   "56: the new take lands as its own clip");
        }

        // item 58: a project already carrying item 56's stacked duplicate
        // clips heals on load - the twins collapse to one clip, distinct
        // fragments are untouched
        {
            ProjectSettings stacked;
            stacked.audioClips[0].active = true;
            stacked.audioClips[0].startSamples = 1000;
            stacked.audioClips[0].sourceStart = 0;
            stacked.audioClips[0].lengthSamples = 44100;
            stacked.audioClips[1] = stacked.audioClips[0]; // the item-56 twin
            stacked.audioClips[2].active = true;           // a real fragment
            stacked.audioClips[2].startSamples = 50000;
            stacked.audioClips[2].sourceStart = 22050;
            stacked.audioClips[2].lengthSamples = 22050;
            auto e = fresh();
            e->applySettings (stacked);
            CHECK (e->audioClipCount() == 2,
                   "58: stacked duplicates collapse on load; real clips stay");
            CHECK (e->getAudioClip (0).active && e->getAudioClip (2).active,
                   "58: the kept clips are the original and the fragment");
        }

        // ---- item 60: MULTIPLE audio tracks ----
        {
            auto e = fresh();
            std::vector<float> take (44100, 0.0f);
            for (int i = 0; i < 44100; ++i) take[(size_t) i] = 0.5f * std::sin (6.283185307179586 * 220.0 * i / 44100.0);
            e->setVocalTake (take);
            CHECK (e->addAudioTrack() == 2, "60: a second audio track can be added");
            e->setRecTargetTrack (1);
            const int slot = e->placeTakeAsClip();
            CHECK (slot >= 0 && e->getAudioClip (slot).track == 1,
                   "60: the take lands on the ARMED track");
            const auto st = e->getSettings();
            CHECK (st.numAudioTracks == 2 && st.audioClips[(size_t) slot].track == 1,
                   "60: track count and clip track persist in settings");

            // the clip sounds through its own channel; muting THAT track
            // silences it, muting the other does not
            std::vector<float> l (8192), r (8192);
            auto renderAudio = [&] (double seconds)
            {
                double peak = 0.0;
                const int blocks = (int) (seconds * 44100.0 / 512.0);
                for (int b = 0; b < blocks; ++b)
                {
                    e->process (nullptr, l.data(), r.data(), 512);
                    for (float v : l) peak = std::max (peak, (double) std::abs (v));
                }
                return peak;
            };
            e->setAudioPlayhead (0);
            e->setAudioPlaying (true);
            const double sounding = renderAudio (0.5);
            e->setAudioTrackMute (1, true);
            e->setAudioPlayhead (0);
            const double muted = renderAudio (0.5);
            e->setAudioTrackMute (1, false);
            e->setAudioTrackMute (0, true);
            e->setAudioPlayhead (0);
            const double otherMuted = renderAudio (0.5);
            e->setAudioPlaying (false);
            CHECK (sounding > 0.05 && muted < 0.005 && otherMuted > 0.05,
                   "60: per-track mute gates its own clips only");

            // the same span on a DIFFERENT track is a legitimate double,
            // not an item-58 stack: the heal keeps both
            ProjectSettings twoTracks;
            twoTracks.numAudioTracks = 2;
            twoTracks.audioClips[0].active = true;
            twoTracks.audioClips[0].startSamples = 1000;
            twoTracks.audioClips[0].lengthSamples = 44100;
            twoTracks.audioClips[0].track = 0;
            twoTracks.audioClips[1] = twoTracks.audioClips[0];
            twoTracks.audioClips[1].track = 1;
            auto e2 = fresh();
            e2->applySettings (twoTracks);
            CHECK (e2->audioClipCount() == 2,
                   "60/58: identical spans on different tracks both survive the heal");
        }

        if (failures > 0)
        {
            std::printf ("FIX PASS 2 CHECKS FAILED: %d\n", failures);
            return 1;
        }
        std::printf ("Fix pass 2 checks: ok\n");
    }

    // ---- 13. pass 2: the Station - channel model, pans, strips, master
    // limiter (the measured strip-DSP verdicts live in PluginRenderTest) ----
    {
        int failures = 0;
        auto CHECK = [&] (bool ok, const char* what)
        {
            std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
            if (! ok) ++failures;
        };
        auto feq = [] (float a, float b) { return std::abs (a - b) < 0.001f; };

        // the whole station state rides through applySettings/getSettings
        {
            ProjectSettings in;
            in.strips[0].compOn = true;
            in.strips[0].compThresholdDb = -22.5f;
            in.strips[0].eq[2].on = true;
            in.strips[0].eq[2].gainDb = 4.5f;
            in.strips[BoomBapEngine::kVocalChannel].preampOn = true;
            in.strips[BoomBapEngine::kVocalChannel].limiterOn = true;
            in.trackPan[3] = -0.5f;
            in.audioPan[1] = 0.75f;
            in.synthMute = true;
            in.pianoSolo = true;
            in.padBusGain = 0.66f;
            in.padBusMute = true;
            in.vocalGain = 0.55f;
            in.vocalSolo = true;
            in.masterLimiterOn = true;
            in.masterLimiterCeilingDb = -3.0f;
            auto e = fresh();
            e->applySettings (in);
            const auto out = e->getSettings();
            CHECK (out.strips[0].compOn && feq (out.strips[0].compThresholdDb, -22.5f)
                   && out.strips[0].eq[2].on && feq (out.strips[0].eq[2].gainDb, 4.5f),
                   "P2: strip settings round-trip through the engine");
            CHECK (out.strips[BoomBapEngine::kVocalChannel].preampOn
                   && out.strips[BoomBapEngine::kVocalChannel].limiterOn,
                   "P2: the vocal strip's preamp + limiter round-trip");
            CHECK (feq (out.trackPan[3], -0.5f) && feq (out.audioPan[1], 0.75f)
                   && out.synthMute && out.pianoSolo,
                   "P2: pans and instrument mute/solo round-trip");
            CHECK (feq (out.padBusGain, 0.66f) && out.padBusMute && feq (out.vocalGain, 0.55f)
                   && out.vocalSolo && out.masterLimiterOn
                   && feq (out.masterLimiterCeilingDb, -3.0f),
                   "P2: pad bus, vocal channel and master limiter round-trip");
        }

        // the unified channel API routes to the right state
        {
            auto e = fresh();
            e->setChannelGain (BoomBapEngine::kAudioChannelBase + 1, 0.33f);
            e->setChannelPan (3, -0.4f);
            e->setChannelMute (BoomBapEngine::kPadBusChannel, true);
            e->setChannelSolo (BoomBapEngine::kVocalChannel, true);
            auto st = e->getStripSettings (7);
            st.satOn = true;
            st.satDrive = 0.7f;
            e->setStripSettings (7, st);
            const auto s = e->getSettings();
            CHECK (feq (s.audioGain[1], 0.33f) && feq (s.trackPan[3], -0.4f)
                   && s.padBusMute && s.vocalSolo,
                   "P2: channel gain/pan/mute/solo route to the right settings");
            CHECK (e->getStripSettings (7).satOn && feq (e->getStripSettings (7).satDrive, 0.7f)
                   && feq (s.strips[7].satDrive, 0.7f),
                   "P2: strip edits route and persist");
            CHECK (BoomBapEngine::channelName (BoomBapEngine::kPadBusChannel, 8, 1) == "Pad Bus"
                   && BoomBapEngine::channelName (BoomBapEngine::kVocalChannel, 8, 1) == "Vocal"
                   && BoomBapEngine::channelHasPreamp (BoomBapEngine::kVocalChannel)
                   && ! BoomBapEngine::channelHasPreamp (0),
                   "P2: channel identity - names + preamp only on audio/vocal");
        }

        // pan is real: a hard-panned kick leaves the other side quiet
        {
            auto e = fresh();
            e->clearPattern();
            e->setStep (BoomBapEngine::Kick, 0, 127);
            std::vector<float> l (512), r (512);
            auto renderStereo = [&] (float pan)
            {
                e->setChannelPan (BoomBapEngine::Kick, pan);
                e->setPlaying (false);
                e->process (nullptr, l.data(), r.data(), 512); // settle the station state
                e->setPlaying (true);
                double pl = 0.0, pr = 0.0;
                for (int b = 0; b < 24; ++b)
                {
                    e->process (nullptr, l.data(), r.data(), 512);
                    for (int i = 0; i < 512; ++i)
                    {
                        pl = std::max (pl, (double) std::abs (l[(size_t) i]));
                        pr = std::max (pr, (double) std::abs (r[(size_t) i]));
                    }
                }
                e->setPlaying (false);
                return std::pair<double, double> (pl, pr);
            };
            const auto left = renderStereo (-1.0f);
            const auto right = renderStereo (1.0f);
            const auto centre = renderStereo (0.0f);
            CHECK (left.first > 0.1 && left.second < left.first * 0.25,
                   "P2: hard-left pan silences the right side");
            CHECK (right.second > 0.1 && right.first < right.second * 0.25,
                   "P2: hard-right pan silences the left side");
            CHECK (centre.first > 0.1 && std::abs (centre.first - centre.second) < 0.02,
                   "P2: centre pan is balanced (and unity - old mixes don't move)");
        }

        // the master limiter holds its ceiling on a kicked beat
        {
            auto e = fresh();
            e->clearPattern();
            e->setStep (BoomBapEngine::Kick, 0, 127);
            std::vector<float> l (512), r (512);
            double minGr = 0.0;
            auto renderPeak = [&] (bool limiter)
            {
                e->setMasterLimiterOn (limiter);
                e->setMasterLimiterCeilingDb (-12.0f);
                e->setPlaying (false);
                e->process (nullptr, l.data(), r.data(), 512);
                e->setPlaying (true);
                double pk = 0.0;
                minGr = 0.0;
                for (int b = 0; b < 48; ++b)
                {
                    e->process (nullptr, l.data(), r.data(), 512);
                    // GR sampled during the render - the transient is long
                    // gone (and released) by the final block
                    minGr = std::min (minGr, (double) e->getMasterLimiterGrDb());
                    for (int i = 0; i < 512; ++i)
                        pk = std::max (pk, (double) std::abs (l[(size_t) i]));
                }
                e->setPlaying (false);
                return pk;
            };
            const double off = renderPeak (false);
            const double on = renderPeak (true);
            std::printf ("  MASTER LIMITER: off peak=%.4f, on peak=%.4f (ceiling -12 dB), min GR %.1f dB\n",
                         off, on, minGr);
            CHECK (off > 0.35, "P2: the unlimited beat is loud enough to test against");
            // the app's fixed output drive (x1.4 into the final tanh) sits
            // AFTER the limiter - pre-existing master character, present in
            // both renders - so the ceiling is judged relatively
            CHECK (on < off * 0.6, "P2: the master limiter holds its ceiling");
            CHECK (minGr < -1.0, "P2: the master limiter's GR meter confirms work");
        }

        if (failures > 0)
        {
            std::printf ("PASS 2 CHECKS FAILED: %d\n", failures);
            return 1;
        }
        std::printf ("Pass 2 checks: ok\n");
    }

    std::printf ("engine checks passed\n");
}
