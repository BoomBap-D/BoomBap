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
            m->loadPadSample (4, std::vector<float> (8000, 0.4f), sr);
            m->setMelodySource (5);   // pad 5
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
            const auto song = render (*sg, bar * 2 - 1000);
            auto peakRange = [] (const std::vector<float>& v, int from, int to)
            {
                float pk = 0.0f;
                for (int i = from; i < to && i < (int) v.size(); ++i) pk = std::max (pk, std::abs (v[(size_t) i]));
                return pk;
            };
            assert (peakRange (song, 0, bar) < 0.01f);             // bar 1 = pattern A: silence
            assert (peakRange (song, bar, bar * 2 - 1000) > 0.3f); // bar 2 = pattern B: the kick
            assert (sg->getPlayPattern() == 1 && sg->getPlaySongPos() == 1);
            std::printf ("Patterns + song chain: ok\n");
        }
    }

    std::printf ("engine checks passed\n");
}
