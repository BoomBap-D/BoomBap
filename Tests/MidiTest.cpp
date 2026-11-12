// MIDI build test (FEEDBACK item 25): the input router (pads 36-51
// on any channel, melodic notes on the selected lane), live record
// timed by the ENGINE clock from block offsets (never arrival time),
// step-record, and MIDI Learn - the layered map, Pickup takeover,
// steal-on-conflict, and the round-trips through the global settings
// and the .boombap project layer. No MIDI hardware exists on this
// machine: every event is injected, exactly the proof the build
// brief asks for.
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/Midi/MidiRouter.h"
#include "../Source/App/Midi/MidiService.h"
#include "../Source/App/ProjectFile.h"
#include <cmath>
#if JUCE_LINUX
 #include <X11/Xlib.h> // AFTER the JUCE headers: Xlib's KeyPress macro eats juce::KeyPress otherwise
#endif

using namespace boombap;

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

static MidiRouteEvent noteOn (int ch, int note, int vel, int offset = 0)
{
    MidiRouteEvent e;
    e.type = MidiRouteEvent::NoteOn;
    e.channel = ch; e.number = note; e.value = vel; e.sampleOffset = offset;
    return e;
}
static MidiRouteEvent noteOff (int ch, int note)
{
    MidiRouteEvent e;
    e.type = MidiRouteEvent::NoteOff;
    e.channel = ch; e.number = note;
    return e;
}
static MidiRouteEvent cc (int ch, int num, int val)
{
    MidiRouteEvent e;
    e.type = MidiRouteEvent::ControlChange;
    e.channel = ch; e.number = num; e.value = val;
    return e;
}

struct MidiTestSuite : public juce::UnitTest
{
    MidiTestSuite() : juce::UnitTest ("Midi", "boombap") {}

    void runTest() override
    {
        // ---------------------------------------------------------
        beginTest ("velocity tiers + step resolution from the engine clock");
        {
            expect (MidiRouter::velocityTier (127) == 127);
            expect (MidiRouter::velocityTier (100) == 127);
            expect (MidiRouter::velocityTier (99) == 90);
            expect (MidiRouter::velocityTier (40) == 90);
            expect (MidiRouter::velocityTier (39) == 39);
            expect (MidiRouter::velocityTier (1) == 1);

            BoomBapEngine::MidiClock clk;
            clk.playing = true;
            clk.step = 5;
            clk.intoStepSamples = 0;
            clk.nextStepInSamples = 5512;
            clk.samplesPerStep = 5512.5;
            expect (MidiRouter::resolveStep (clk, 0) == 5);
            expect (MidiRouter::resolveStep (clk, 2000) == 5);   // first half of step 5
            expect (MidiRouter::resolveStep (clk, 3000) == 6);   // past the midpoint: step 6's half
            expect (MidiRouter::resolveStep (clk, 5512 + 100) == 6);
            expect (MidiRouter::resolveStep (clk, 5512 + 2756 + 100) == 7);
            clk.step = -1;
            expect (MidiRouter::resolveStep (clk, 0) == -1);      // stopped: no step
        }

        // ---------------------------------------------------------
        beginTest ("notes 36-51 play pads on any channel; other notes play the selected lane");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            MidiRouter router (e);
            // lastTriggeredPad follows the loudest sounding pad, so
            // give the three pads under test a short real sample
            // (an empty pad triggers silently by design).
            for (int p : { 0, 4, 15 })
                e.loadPadSample (p, std::vector<float> (2000, 0.5f), 44100.0);
            // (consumePadActivityPeak mirrors the UI's per-tick read:
            // the peak is a latch - reset it before each render so
            // the hit under test is the one lastTriggeredPad names.)
            router.processEvent (noteOn (10, 40, 100));
            e.consumePadActivityPeak();
            render (e, 8192);
            expect (e.lastTriggeredPad() == 4, "note 40 on ch 10 should fire pad 5");
            router.processEvent (noteOn (3, 36, 100));
            e.consumePadActivityPeak();
            render (e, 8192);
            expect (e.lastTriggeredPad() == 0, "note 36 on ch 3 should fire pad 1");
            router.processEvent (noteOn (1, 51, 100));
            e.consumePadActivityPeak();
            render (e, 8192);
            expect (e.lastTriggeredPad() == 15, "note 51 should fire pad 16");

            router.melodicChannel.store (BoomBapEngine::kSynthChannel);
            router.processEvent (noteOn (1, 60, 100));
            std::vector<float> l (512), r (512);
            float peak = 0.0f;
            for (int i = 0; i < 16; ++i)
            {
                e.process (nullptr, l.data(), r.data(), 512);
                for (float s : l) peak = std::max (peak, std::abs (s));
            }
            expect (peak > 0.03f, "the synth should sound from a melodic note");
            router.processEvent (noteOff (1, 60));
        }

        // ---------------------------------------------------------
        beginTest ("live record lands on the engine-clock steps with tiered velocities");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            e.setBpm (120.0f);
            MidiRouter router (e);
            router.recArm.store (true);
            // Clear the cells under test (edit pattern == play pattern
            // in pattern mode).
            for (int s : { 5, 6, 7 })
            {
                e.setStep (BoomBapEngine::Sample1 + 0, s, 0);
                e.setStep (BoomBapEngine::Sample1 + 1, s, 0);
                e.setStep (BoomBapEngine::Sample1 + 2, s, 0);
            }
            e.setSynthStep (5, 0);
            e.setPlaying (true);
            render (e, 512);
            auto clk = e.getMidiClock();
            for (int i = 0; i < 400 && clk.step != 5; ++i)
            {
                render (e, 512);
                clk = e.getMidiClock();
            }
            expect (clk.step == 5, "transport should reach step 5");
            expect (clk.pattern == e.getEditPattern());
            // A hit at the block's start belongs to the sounding step.
            router.processEvent (noteOn (10, 36, 120, 0));
            expect (e.getStep (BoomBapEngine::Sample1 + 0, 5) == 127,
                    "hard pad hit at offset 0 -> accent on step 5");
            // Just inside the next step: still the next step's hit.
            router.processEvent (noteOn (10, 37, 80, clk.nextStepInSamples + 10));
            expect (e.getStep (BoomBapEngine::Sample1 + 1, 6) == 90,
                    "medium hit just after the boundary -> normal on step 6");
            // Past the following midpoint: the step after.
            const int late = clk.nextStepInSamples + (int) (clk.samplesPerStep * 0.5) + 100;
            router.processEvent (noteOn (10, 38, 30, late));
            expect (e.getStep (BoomBapEngine::Sample1 + 2, 7) == 30,
                    "soft hit past the midpoint -> raw ghost value on step 7");
            // A melodic note writes the selected lane at the same step.
            router.melodicChannel.store (BoomBapEngine::kSynthChannel);
            router.processEvent (noteOn (1, 60, 100, 0));
            expect (e.getSynthStep (5) == 37,
                    "synth note 60 -> synth-lane value 37 on step 5");
            router.processEvent (noteOff (1, 60));
            e.setPlaying (false);
            render (e, 512);
        }

        // ---------------------------------------------------------
        beginTest ("step-record writes at the cursor and advances");
        {
            BoomBapEngine e;
            e.prepare (44100.0);
            MidiRouter router (e);
            router.stepRecord.store (true);
            router.setCursorStep (0);
            router.melodicChannel.store (BoomBapEngine::kSynthChannel);
            e.setStep (BoomBapEngine::Sample1 + 2, 0, 0);
            e.setStep (BoomBapEngine::Sample1 + 0, 2, 0);
            e.setSynthStep (1, 0);
            router.processEvent (noteOn (10, 38, 110));   // pad 3
            expect (e.getStep (BoomBapEngine::Sample1 + 2, 0) == 127);
            expect (router.cursorStep() == 1);
            router.processEvent (noteOn (1, 60, 100));    // synth note
            expect (e.getSynthStep (1) == 37);
            expect (router.cursorStep() == 2);
            router.processEvent (noteOff (1, 60));
            router.processEvent (noteOn (10, 36, 45));    // pad 1, medium
            expect (e.getStep (BoomBapEngine::Sample1 + 0, 2) == 90);
            expect (router.cursorStep() == 3);
        }

        // ---------------------------------------------------------
        beginTest ("learn map: bind, drive, pickup, toggle, steal");
        {
            MidiTargetRegistry reg;
            float contVal = 0.8f;
            bool togVal = false;
            int actCount = 0;
            {
                MidiTarget t; t.id = "contA"; t.name = "A"; t.kind = MidiTarget::Continuous;
                t.get01 = [&contVal] { return contVal; };
                t.set01 = [&contVal] (float v) { contVal = v; };
                reg.add (t);
            }
            {
                MidiTarget t; t.id = "togB"; t.name = "B"; t.kind = MidiTarget::Toggle;
                t.get01 = [&togVal] { return togVal ? 1.0f : 0.0f; };
                t.set01 = [&togVal] (float v) { togVal = v >= 0.5f; };
                reg.add (t);
            }
            {
                MidiTarget t; t.id = "actC"; t.name = "C"; t.kind = MidiTarget::Action;
                t.trigger = [&actCount] { ++actCount; };
                reg.add (t);
            }
            MidiMap map;
            juce::String stolen;
            MidiBinding b = ccBind (1, 20, "contA");
            expect (map.learnBind (MidiLayer::Global, b, stolen));
            expect (stolen.isEmpty());
            MidiMapEvent ev;
            ev.type = MidiMapEvent::ControlChange; ev.channel = 1; ev.number = 20; ev.value = 127;
            expect (map.applyEvent (ev, reg) == "contA");
            expect (std::abs (contVal - 1.0f) < 0.001f);

            // Steal: the same source rebinds; the old target is reported.
            b = ccBind (1, 20, "actC");
            expect (map.learnBind (MidiLayer::Global, b, stolen));
            expect (stolen == "contA", "stealing should report the old target");
            ev.value = 10;
            map.applyEvent (ev, reg);            // below the edge: no fire
            expect (actCount == 0);
            ev.value = 100;
            map.applyEvent (ev, reg);            // rising edge: fire once
            expect (actCount == 1);
            map.applyEvent (ev, reg);            // held: no repeat
            expect (actCount == 1);

            // Pickup: no jump until the physical value crosses.
            contVal = 0.8f;
            b = ccBind (1, 21, "contA", MidiBinding::Pickup);
            map.learnBind (MidiLayer::Global, b, stolen);
            ev.number = 21; ev.value = 0;
            map.applyEvent (ev, reg);
            expect (std::abs (contVal - 0.8f) < 0.001f, "pickup must hold before the cross");
            ev.value = 90;                        // 90 < 0.8*127 = 101.6
            map.applyEvent (ev, reg);
            expect (std::abs (contVal - 0.8f) < 0.001f, "still holding below the cross");
            ev.value = 110;                       // crosses
            map.applyEvent (ev, reg);
            expect (std::abs (contVal - 110.0f / 127.0f) < 0.01f, "grabbed at the cross");
            ev.value = 40;
            map.applyEvent (ev, reg);
            expect (std::abs (contVal - 40.0f / 127.0f) < 0.01f, "tracks after the grab");

            // Toggle from a note: press flips, release does not.
            MidiBinding nb;
            nb.sourceType = MidiBinding::NoteSource; nb.channel = 0; nb.number = 99;
            nb.targetId = "togB";
            map.learnBind (MidiLayer::Project, nb, stolen);
            MidiMapEvent ne;
            ne.type = MidiMapEvent::Note; ne.channel = 5; ne.number = 99;
            ne.value = 90; ne.isNoteOn = true;
            map.applyEvent (ne, reg);
            expect (togVal);
            ne.isNoteOn = false; ne.value = 0;
            map.applyEvent (ne, reg);
            expect (togVal, "note-off must not flip a toggle");
            ne.isNoteOn = true; ne.value = 90;
            map.applyEvent (ne, reg);
            expect (! togVal);

            // Layer priority: project beats global for the same source.
            b = ccBind (1, 20, "contA");
            map.learnBind (MidiLayer::Project, b, stolen);
            MidiMapEvent probe;
            probe.type = MidiMapEvent::ControlChange; probe.channel = 1; probe.number = 20;
            MidiLayer layer;
            const auto* eff = map.findEffective (probe, &layer);
            expect (eff != nullptr && eff->targetId == "contA" && layer == MidiLayer::Project);

            // JSON round-trip per layer.
            const auto json = map.layerToJson (MidiLayer::Project);
            auto back = MidiMap::layerFromJson (json);
            expect ((int) back.size() == map.layerCount (MidiLayer::Project));
            bool foundSwing = false;
            for (const auto& x : back) if (x.number == 20) foundSwing = true;
            expect (foundSwing);
        }

        // ---------------------------------------------------------
        beginTest ("presets: three controllers, honest labels");
        {
            const auto mini = mpkMiniPreset();
            expect (mini.name.contains ("factory Program 1"));
            bool k5 = false, pattern4 = false;
            for (const auto& x : mini.bindings)
            {
                if (x.number == 13 && x.channel == 1) k5 = x.targetId == "glueThresh";
                if (x.number == 35 && x.channel == 10) pattern4 = x.targetId == "actPattern:4";
            }
            expect (k5, "MPK Mini K5 is CC 13 (Akai's order) -> Glue");
            expect (pattern4, "MPK Mini bank 2 jumps 31 -> 35 for pattern E");
            const auto mpk = mpk49Preset();
            expect (mpk.setupNote.contains ("BoomBap-assigned"),
                    "the MPK49 numbers are assigned, never 'factory'");
            const auto tfp = triggerFingerProPreset();
            expect (tfp.setupNote.contains ("BoomBap-assigned")
                    && tfp.setupNote.contains ("no factory"));
            expect ((int) allMidiPresets().size() == 3);
        }

        // ---------------------------------------------------------
        beginTest ("project file: the midiLearn key is additive and round-trips");
        {
            ProjectData d;
            MidiMap m;
            juce::String stolen;
            m.learnBind (MidiLayer::Project, ccBind (1, 51, "bpm"), stolen);
            d.midiLearnJson = m.layerToJson (MidiLayer::Project);
            const auto text = projectToJson (d, juce::File());
            expect (text.contains ("midiLearn"));
            ProjectData out;
            juce::String err;
            expect (projectFromJson (text, juce::File(), out, err));
            expect (out.midiLearnJson == d.midiLearnJson);
            ProjectData plain;
            ProjectData out2;
            expect (projectFromJson (projectToJson (plain, juce::File()),
                                     juce::File(), out2, err));
            expect (out2.midiLearnJson.isEmpty(),
                    "a file without the key loads with an empty project layer");
        }

        // ---------------------------------------------------------
        beginTest ("full stack: window, preset, learn wiggle, drive, persistence");
        {
            BoomBapProcessor proc;
            auto& svc = proc.midiService;
            svc.map.clearUserLayers();
            svc.saveState();

            MainComponent main (proc.engine, false);
            main.setExternalMidiService (&svc);
            pump (60);

            main.driveShowMidiSettings();
            expect (main.midiWindowComponent() != nullptr
                    && main.midiWindowComponent()->isVisible(),
                    "the MIDI window opens");
            expect (main.midiPanelComponent() != nullptr);
            expect (main.midiPanelComponent()->deviceComboBox().getNumItems() >= 1,
                    "the device list always offers at least 'No device'");

            svc.applyPreset (1);   // MPK Mini mk1
            expect (svc.map.layerCount (MidiLayer::Factory)
                        == (int) mpkMiniPreset().bindings.size());
            MidiMapEvent probe;
            probe.type = MidiMapEvent::ControlChange; probe.channel = 1; probe.number = 17;
            const auto* eff = svc.map.findEffective (probe);
            expect (eff != nullptr && eff->targetId == "chanGain:0",
                    "the preset's K1 drives the Kick level");
            probe.channel = 10; probe.number = 35;
            eff = svc.map.findEffective (probe);
            expect (eff != nullptr && eff->targetId == "actPattern:4");

            // Learn: arm, pick the Glue Threshold, wiggle CC 44.
            svc.learnArmed.store (true);
            svc.setPendingTarget ("glueThresh");
            juce::MidiBuffer buf;
            buf.addEvent (juce::MidiMessage::controllerEvent (1, 44, 77), 0);
            svc.processHostBuffer (buf);
            pump (500);
            probe.channel = 1; probe.number = 44;
            eff = svc.map.findEffective (probe);
            expect (eff != nullptr && eff->targetId == "glueThresh",
                    "the wiggle should bind CC 44 to the Glue Threshold");
            expect (svc.learnNoticeCount() >= 1, "a learn notice is shown");

            // The binding drives the control (through the real slider).
            juce::MidiBuffer zero, full;
            zero.addEvent (juce::MidiMessage::controllerEvent (1, 44, 0), 0);
            svc.processHostBuffer (zero);
            pump (300);
            expect (std::abs (proc.engine.getSettings().glueThresholdDb - (-30.0f)) < 0.6f,
                    "CC 44 at 0 -> glue threshold -30 dB");
            full.addEvent (juce::MidiMessage::controllerEvent (1, 44, 127), 0);
            svc.processHostBuffer (full);
            pump (300);
            expect (std::abs (proc.engine.getSettings().glueThresholdDb - 0.0f) < 0.6f,
                    "CC 44 at 127 -> glue threshold 0 dB");

            // A bound NOTE toggles; the binding wiggle itself is swallowed.
            svc.setPendingTarget ("metroOn");
            juce::MidiBuffer wiggle;
            wiggle.addEvent (juce::MidiMessage::noteOn (1, 99, (juce::uint8) 90), 0);
            svc.processHostBuffer (wiggle);
            pump (500);
            expect (! proc.engine.isMetronomeOn(),
                    "the learn wiggle must not also fire the control");
            juce::MidiBuffer press;
            press.addEvent (juce::MidiMessage::noteOn (1, 99, (juce::uint8) 90), 0);
            svc.processHostBuffer (press);
            pump (300);
            expect (proc.engine.isMetronomeOn(),
                    "the bound note toggles the metronome");

            // Global layer survives a save/reload through the settings.
            svc.saveState();
            MidiService svc2 (proc.engine);
            svc2.manageDevices = false;
            svc2.loadState();
            probe.channel = 1; probe.number = 44;
            eff = svc2.map.findEffective (probe);
            expect (eff != nullptr && eff->targetId == "glueThresh",
                    "the global map reloads from the app settings");

            // Project layer: bind in the project, read it back.
            svc.learnLayer = MidiLayer::Project;
            svc.setPendingTarget ("swing");
            juce::MidiBuffer wig2;
            wig2.addEvent (juce::MidiMessage::controllerEvent (1, 46, 64), 0);
            svc.processHostBuffer (wig2);
            pump (500);
            const auto projJson = svc.projectLayerJson();
            expect (projJson.contains ("swing"), "the project layer carries the binding");
            MidiService svc3 (proc.engine);
            svc3.manageDevices = false;
            svc3.setProjectLayerJson (projJson);
            probe.channel = 1; probe.number = 46;
            eff = svc3.map.findEffective (probe);
            expect (eff != nullptr && eff->targetId == "swing",
                    "the project layer reloads onto a fresh service");
            svc.learnArmed.store (false);
        }
    }
};

static MidiTestSuite midiTestSuite;

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
    std::printf ("MidiTest: %d checks, %d failures\n", total, fails);
    return fails == 0 ? 0 : 1;
}
