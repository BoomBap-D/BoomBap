// Record-fix test (FEEDBACK item 185): the record cluster D hit
// hands-on, Nov 14, 2026 - (a) a finished take ZIPPED TO BAR 1 on
// stop, in pattern and song mode alike; (b) nudging track 2's clip
// rewrote track 1's clip with track 2's audio (the undo system's
// snapshot re-apply refilled every clip buffer from the one shared
// take buffer); (c) the on-screen keyboard could not record - the
// transport REC (the MIDI router's recArm) and the step recorder
// only ever listened to controllers; (d) clip audio died after a
// settings re-apply during playback (the same shared-buffer path:
// buffers cleared, then "restored" from an empty take).
//
// The invariants pinned here:
//  - a take STAYS where it was recorded (stop = stay, item 48);
//  - a clip OWNS its audio: no settings apply, undo cycle, delete or
//    nudge may rewrite another clip's buffer, and undoing a delete
//    brings the clip's own audio back;
//  - a settings re-apply during playback never silences a clip;
//  - with the transport REC armed, the on-screen keys write into
//    the pattern at the engine clock, exactly like a controller;
//    with step-record on and the transport stopped, they write at
//    the step recorder's cursor.
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/Midi/MidiRouter.h"
#include "../Source/App/Midi/MidiService.h"
#include <cmath>
#if JUCE_LINUX
 #include <X11/Xlib.h> // AFTER the JUCE headers: Xlib's KeyPress macro eats juce::KeyPress otherwise
#endif

using namespace boombap;

static void renderIn (BoomBapEngine& e, int numSamples, float inValue)
{
    std::vector<float> in (512, inValue), l (512), r (512);
    for (int pos = 0; pos < numSamples; pos += 512)
    {
        const int n = std::min (512, numSamples - pos);
        e.process (in.data(), l.data(), r.data(), n);
    }
}

static void render (BoomBapEngine& e, int numSamples)
{
    std::vector<float> l (512), r (512);
    for (int pos = 0; pos < numSamples; pos += 512)
    {
        const int n = std::min (512, numSamples - pos);
        e.process (nullptr, l.data(), r.data(), n);
    }
}

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

// Presses record (the transport is already playing) and runs the
// engine until the capture is actually rolling.
static bool startCapture (BoomBapEngine& e)
{
    e.startRecording();
    for (int i = 0; i < 700 && ! e.isCapturing(); ++i)
        renderIn (e, 512, 0.0f);
    return e.isCapturing();
}

static float peakOf (const std::vector<float>& v)
{
    float p = 0.0f;
    for (float s : v) p = std::max (p, std::abs (s));
    return p;
}

struct RecordFixTestSuite : public juce::UnitTest
{
    RecordFixTestSuite() : juce::UnitTest ("RecordFix", "boombap") {}

    void runTest() override
    {
        // ---------------------------------------------------------
        beginTest ("185a: a take stays where it was recorded (pattern mode)");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            const long long bar = (long long) std::llround (e.barSamples());
            e.setPlaying (true);
            renderIn (e, (int) (11 * bar), 0.0f);   // play deep into the timeline
            expect (startCapture (e), "the capture starts");
            const long long startDuring = e.recordStartPosition();
            expect (startDuring >= 11 * bar && startDuring <= 13 * bar,
                    "while recording, the take anchors at the late bar");
            renderIn (e, (int) (2 * bar), 0.3f);    // perform two bars
            e.stopRecording();
            expect (e.recordStartPosition() == startDuring,
                    "stop must not move the take (it zipped to bar 1)");
            const int slot = e.placeTakeAsClip();
            expect (slot >= 0, "the take homes as a clip");
            const auto clip = e.getAudioClip (slot);
            expect (clip.startSamples == startDuring,
                    "the clip sits where the take was recorded");
            expect (clip.lengthSamples >= 2 * bar - 512 && clip.lengthSamples <= 3 * bar,
                    "the clip holds the performed bars");
            expect (peakOf (e.getAudioClipData (slot)) > 0.25f,
                    "the clip's audio is the performed signal");
        }

        // ---------------------------------------------------------
        beginTest ("185a: a take stays where it was recorded (song mode)");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            ProjectSettings s;
            s.songMode = true;
            s.songBlocks[0].active = true;
            s.songBlocks[0].pattern = 0;
            s.songBlocks[0].lane = 0;
            s.songBlocks[0].startBar = 0;
            s.songBlocks[0].lengthBars = 16;
            e.applySettings (s);
            e.setSongMode (true);
            const long long bar = (long long) std::llround (e.barSamples());
            e.setPlaying (true);
            renderIn (e, (int) (11 * bar), 0.0f);
            expect (e.songTotalBars() == 16, "the arrangement is 16 bars");
            expect (startCapture (e), "the capture starts");
            const long long startDuring = e.recordStartPosition();
            expect (startDuring >= 11 * bar && startDuring <= 13 * bar,
                    "while recording, the take anchors at the late bar");
            renderIn (e, (int) (2 * bar), 0.4f);
            e.stopRecording();
            expect (e.recordStartPosition() == startDuring,
                    "stop must not move the take in song mode either");
            const int slot = e.placeTakeAsClip();
            expect (slot >= 0);
            expect (e.getAudioClip (slot).startSamples == startDuring,
                    "the clip sits where the take was recorded (song mode)");
        }

        // ---------------------------------------------------------
        beginTest ("185b: a nudge never rewrites another lane's audio");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            const long long bar = (long long) std::llround (e.barSamples());
            e.addAudioTrack();
            e.setPlaying (true);
            renderIn (e, (int) (bar / 2), 0.0f);

            // Take 1 on track 1, take 2 on track 2 - two distinct
            // performances, told apart by level.
            e.setRecTargetTrack (0);
            expect (startCapture (e));
            renderIn (e, (int) bar, 0.3f);
            e.stopRecording();
            const int slotA = e.placeTakeAsClip();
            expect (slotA >= 0);
            const auto dataA = e.getAudioClipData (slotA);
            expect (peakOf (dataA) > 0.25f && peakOf (dataA) < 0.35f,
                    "clip A holds take 1");

            e.setRecTargetTrack (1);
            expect (startCapture (e));
            renderIn (e, (int) bar, 0.7f);
            e.stopRecording();
            const int slotB = e.placeTakeAsClip();
            expect (slotB >= 0 && slotB != slotA);
            expect (e.getAudioClip (slotB).track == 1, "take 2 lands on track 2");
            const auto dataB = e.getAudioClipData (slotB);
            expect (peakOf (dataB) > 0.65f, "clip B holds take 2");

            // The nudge - and the snapshot re-apply every undoable
            // gesture runs on mouse-up (the pair loadProjectData
            // performs: applySettings + restoreClipBuffersFromTake).
            const long long nudged = e.getAudioClip (slotB).startSamples + 1000;
            e.moveAudioClip (slotB, nudged);
            {
                const auto s = e.getSettings();
                e.applySettings (s);
                e.restoreClipBuffersFromTake();
            }
            expect (e.getAudioClipData (slotA) == dataA,
                    "track 1's clip still holds take 1 after the nudge");
            expect (e.getAudioClipData (slotB) == dataB,
                    "track 2's clip still holds take 2 after the nudge");
            expect (e.getAudioClip (slotB).startSamples == nudged,
                    "the nudge itself landed");
            expect (e.getAudioClip (slotA).track == 0 && e.getAudioClip (slotB).track == 1,
                    "the lanes stay put");
        }

        // ---------------------------------------------------------
        beginTest ("185b: undoing a clip delete restores the clip's own audio");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            e.addAudioTrack();
            const std::vector<float> bufX (44100, 0.22f), bufY (44100, 0.66f);
            const int sx = e.placeDataAsClip (bufX, 0, 0);
            const int sy = e.placeDataAsClip (bufY, 1, 22050);
            expect (sx >= 0 && sy >= 0 && sx != sy);
            const auto before = e.getSettings();   // the snapshot an undo returns to
            e.clearAudioClip (sx);                  // the delete
            expect (! e.getAudioClip (sx).active);
            e.applySettings (before);               // the undo apply
            e.restoreClipBuffersFromTake();
            expect (e.getAudioClip (sx).active, "the delete is undone");
            expect (e.getAudioClipData (sx) == bufX,
                    "the restored clip sounds its OWN audio again");
            expect (e.getAudioClipData (sy) == bufY,
                    "the neighbouring clip is untouched");
        }

        // ---------------------------------------------------------
        beginTest ("185d: a settings re-apply during playback never silences a clip");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            e.addAudioTrack();
            std::vector<float> sine ((size_t) (2 * 44100));
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = 0.5f * std::sin (6.283185307179586 * 220.0 * (double) i / 44100.0);
            const int sa = e.placeDataAsClip (sine, 0, 0);
            const int sb = e.placeDataAsClip (sine, 1, 0);
            expect (sa >= 0 && sb >= 0);
            e.setAudioPlayhead (0);
            e.setAudioPlaying (true);
            auto measurePeak = [&e] (int samples)
            {
                std::vector<float> l (512), r (512);
                float p = 0.0f;
                for (int pos = 0; pos < samples; pos += 512)
                {
                    e.process (nullptr, l.data(), r.data(), std::min (512, samples - pos));
                    for (float v : l) p = std::max (p, std::abs (v));
                }
                return p;
            };
            expect (measurePeak (22050) > 0.05f, "the clips sound before the gesture");
            {
                const auto s = e.getSettings();   // a gesture ends mid-playback:
                e.applySettings (s);              // the snapshot re-apply runs
                e.restoreClipBuffersFromTake();
            }
            expect (measurePeak (22050) > 0.05f,
                    "the clips still sound after the re-apply (audio died here)");
            expect (e.getAudioClipData (sa) == sine && e.getAudioClipData (sb) == sine,
                    "both buffers survive the gesture intact");
        }

        // ---------------------------------------------------------
        beginTest ("185c: on-screen keys record while the transport REC is armed");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            MainComponent main (proc.engine, false);
            main.setExternalMidiService (&proc.midiService);
            pump (60);
            auto& engine = proc.engine;
            auto& router = proc.midiService.router;

            router.recArm.store (true);
            engine.setPlaying (true);
            render (engine, 20000);   // let the engine clock publish a step
            const int step = MidiRouter::resolveStep (engine.getMidiClock(), 0);
            expect (step >= 0, "the transport is on a step");
            main.driveKeyboardNoteOn (BoomBapEngine::kSynthChannel, 60, 1.0f);
            expect (engine.getSettings().patSynth[0][(size_t) step] == 37,
                    "the key lands in the pattern at the engine clock (C4 on the synth lane)");

            // REC off: the same key is an audition, never a write.
            router.recArm.store (false);
            render (engine, 9000);
            const int step2 = MidiRouter::resolveStep (engine.getMidiClock(), 0);
            expect (step2 >= 0 && step2 != step);
            main.driveKeyboardNoteOn (BoomBapEngine::kSynthChannel, 64, 1.0f);
            expect (engine.getSettings().patSynth[0][(size_t) step2] == 0,
                    "with REC off, keys play but do not record");
            engine.setPlaying (false);
        }

        // ---------------------------------------------------------
        beginTest ("185c: on-screen keys feed the step recorder while stopped");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            MainComponent main (proc.engine, false);
            main.setExternalMidiService (&proc.midiService);
            pump (60);
            auto& engine = proc.engine;
            auto& router = proc.midiService.router;

            expect (! engine.isPlaying());
            router.stepRecord.store (true);
            router.setCursorStep (3);
            main.driveKeyboardNoteOn (BoomBapEngine::kSynthChannel, 62, 1.0f);
            expect (engine.getSettings().patSynth[0][3] == 39,
                    "the key writes at the step recorder's cursor (D4 on the synth lane)");
            expect (router.cursorStep() == 4, "the cursor advances after the write");
        }
    }
};

static RecordFixTestSuite recordFixTestSuite;

int main()
{
   #if JUCE_LINUX
    // The window-manager stand-in (the DockBackTest / WindowFramesTest
    // pattern): this suite creates real top-level windows.
    Display* wmStandInDisplay = XOpenDisplay (nullptr);
    if (wmStandInDisplay != nullptr)
    {
        for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW",
                            "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                            "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
                            "_NET_WM_STATE_HIDDEN", "_MOTIF_WM_HINTS" })
            XInternAtom (wmStandInDisplay, name, False);
    }
   #endif

    juce::ScopedJuceInitialiser_GUI init;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("boombap");
    int fails = 0, total = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* r = runner.getResult (i);
        total += r->passes + r->failures;
        fails += r->failures;
    }
    std::printf ("RecordFixTest: %d checks, %d failures\n", total, fails);
    return fails == 0 ? 0 : 1;
}
