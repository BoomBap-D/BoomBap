// Patterns build test (item 160): the preset pattern bank.
//
// Proven at two levels: the PatternBank module itself (the bundled
// data parses; the picker holds the 49 playable grooves - empty
// drum-08 is absent; note counts and velocities match the verified
// conversion in preset-patterns/INVENTORY.md; invalid picks are
// refused), and the REAL app through the REAL selectors (a groove
// pick auto-loads Kit 1 when the bank is empty, fills the current
// pattern's kick/snare/hat rows by NAME with velocities, sets the
// tempo, and is exactly ONE undoable action; note 41 lands nowhere;
// a bassline pick writes its notes into the synth lane at the lane
// encoding and sets the file tempo).
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/KeyboardPanel.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/PatternBank.h"
#include <map>

using namespace boombap;

class PatternBankTest : public juce::UnitTest
{
public:
    PatternBankTest() : juce::UnitTest ("PatternBank", "boombap") {}

    void runTest() override
    {
        const int sr = 44100;
        using E = BoomBapEngine;

        // ============ the module: the data itself ==================
        beginTest ("item 160: the picker lists - 49 playable grooves, drum-08 absent, 50 basslines");
        {
            expectEquals (drumGrooveCount(), 49, "49 playable drum grooves (50 files, one empty)");
            expectEquals (basslineCount(), 50, "50 basslines");
            expectEquals (drumGrooveNames().size(), 49, "the groove picker has 49 entries");
            expect (drumGrooveNames()[0] == "01 \xc2\xb7 92 BPM", "groove names carry number + BPM");
            bool has8 = false, has1 = false, has50 = false;
            for (int i = 0; i < drumGrooveCount(); ++i)
            {
                const auto* g = drumGrooveAt (i);
                if (g != nullptr && g->number == 8) has8 = true;
                if (g != nullptr && g->number == 1) has1 = true;
                if (g != nullptr && g->number == 50) has50 = true;
            }
            expect (! has8, "drum-08 is EMPTY in the pack and absent from the picker");
            expect (has1 && has50, "grooves 01 and 50 are present");
            expect (drumGrooveAt (-1) == nullptr && drumGrooveAt (49) == nullptr,
                    "an out-of-range groove pick is refused");
            expect (basslineAt (-1) == nullptr && basslineAt (50) == nullptr,
                    "an out-of-range bassline pick is refused");
        }

        beginTest ("item 160: groove 01's data matches the verified conversion");
        {
            const auto* g = drumGrooveAt (0);
            expect (g != nullptr);
            if (g != nullptr)
            {
                expectEquals (g->number, 1);
                expect (std::abs (g->bpm - 92.0) < 0.01, "groove 01 runs at 92 BPM");
                std::map<int, int> kick, snare, hat;
                int accents = 0;
                for (const auto& h : g->hits)
                {
                    if (h.note == 36) kick[h.step32] = h.velocity;
                    else if (h.note == 38) snare[h.step32] = h.velocity;
                    else if (h.note == 40) hat[h.step32] = h.velocity;
                    else if (h.note == 41) ++accents;
                }
                expectEquals ((int) kick.size(), 10, "10 folded kick steps");
                expectEquals ((int) snare.size(), 4, "4 folded snare steps");
                expectEquals ((int) hat.size(), 16, "16 folded hat steps (the 8th grid)");
                expectEquals (accents, 0, "groove 01 has no note-41 accents");
                expectEquals (kick[0], 127, "the kick lands on step 0 at full velocity");
                expectEquals (snare[4], 127, "the snare lands on the backbeat (step 4)");
                expectEquals (hat[0], 127, "the hat lands on step 0");
            }
            const auto* g2 = drumGrooveAt (1);
            expect (g2 != nullptr);
            if (g2 != nullptr)
            {
                int accents = 0;
                for (const auto& h : g2->hits) if (h.note == 41) ++accents;
                expectEquals (accents, 4, "groove 02 carries its 4 note-41 accents IN THE DATA (held out of playback)");
            }
        }

        beginTest ("item 160: bassline data - block 1, pitches as written, the file tempo wins");
        {
            const auto* b = basslineAt (0);
            expect (b != nullptr);
            if (b != nullptr)
            {
                expectEquals (b->number, 1);
                expect (std::abs (b->bpm - 93.0) < 0.01, "bassline 001 runs at 93 BPM");
                expect (b->keyName == "A#", "the picker label is the filename key");
                expectEquals ((int) b->notes.size(), 8, "block 1 holds 8 notes");
                if (b->notes.size() >= 2)
                {
                    expectEquals (b->notes[0].pitch, 46, "first note A#2 as written");
                    expectEquals (b->notes[0].step, 0);
                    expectEquals (b->notes[0].lengthSteps, 2, "a 2-step note");
                    expectEquals (b->notes[1].step, 4);
                }
            }
            const auto* b8 = basslineAt (7);
            expect (b8 != nullptr);
            if (b8 != nullptr)
                expect (std::abs (b8->bpm - 92.0) < 0.01,
                        "bassline 008: the FILE tempo (92) wins over the filename (95)");
        }

        // ============ the app: through the real selectors ==========
        beginTest ("item 160: a groove pick fills the pattern by name, sets tempo, one undo");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            auto* mc = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
            expect (mc != nullptr);
            if (mc != nullptr)
            {
                ed->setSize (1500, 950);
                auto pump = [] (int ms = 120) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
                pump();
                auto& eng = proc.engine;
                eng.clearPattern(); // the counts below are the pick's alone
                const float bpmBefore = eng.getBpm();
                const int depthBefore = mc->undoDepth();
                expect (! eng.rowHasSample (E::Sample1), "the pad rows start empty (no kit loaded)");

                auto& grooveBox = mc->sequencerPanelComponent()->grooveBoxComponent();
                expectEquals (grooveBox.getNumItems(), 49, "the sequencer's GROOVE selector offers 49");
                grooveBox.setSelectedId (1, juce::sendNotification);
                pump();

                expect (eng.rowHasSample (E::Sample1), "the pick auto-loaded Kit 1 for voices");
                expect (std::abs (eng.getBpm() - 92.0f) < 0.01f, "the tempo became the groove's 92");
                // Kit 1's rows by name: Deep Kick (Sample1), Fat Snare
                // (Sample1+2), Closed Hat (Sample1+4).
                expect (juce::String (eng.displayTrackName (E::Sample1)) == "Deep Kick");
                expectEquals (eng.getStep (E::Sample1, 0), 127, "kick on step 0, velocity 127");
                expectEquals (eng.getStep (E::Sample1 + 2, 4), 127, "snare on step 4, velocity 127");
                expectEquals (eng.getStep (E::Sample1 + 4, 0), 127, "closed hat on step 0");
                int kickSteps = 0, snareSteps = 0, hatSteps = 0;
                for (int s = 0; s < E::kSteps; ++s)
                {
                    if (eng.getStep (E::Sample1, s) > 0) ++kickSteps;
                    if (eng.getStep (E::Sample1 + 2, s) > 0) ++snareSteps;
                    if (eng.getStep (E::Sample1 + 4, s) > 0) ++hatSteps;
                }
                expectEquals (kickSteps, 10, "the kick row holds the folded 10");
                expectEquals (snareSteps, 4, "the snare row holds the folded 4");
                expectEquals (hatSteps, 16, "the hat row holds the folded 16");
                expectEquals (mc->undoDepth(), depthBefore + 1, "the whole pick is ONE undoable action");

                mc->undoProject();
                pump();
                expect (std::abs (eng.getBpm() - bpmBefore) < 0.01f, "undo restores the tempo");
                expect (! eng.rowHasSample (E::Sample1), "undo un-loads the auto-loaded kit too");
            }
            ed.reset();
            proc.releaseResources();
        }

        beginTest ("item 160: note 41 lands nowhere; a bassline pick writes the synth lane");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            auto* mc = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
            expect (mc != nullptr);
            if (mc != nullptr)
            {
                ed->setSize (1500, 950);
                auto pump = [] (int ms = 120) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
                pump();
                auto& eng = proc.engine;
                eng.clearPattern(); // the sweep below counts the pick's steps alone

                // Groove 02 carries 4 note-41 accents: after the pick,
                // the ONLY nonzero steps in the pattern are the three
                // mapped voices' folded cells (computed from the data).
                mc->sequencerPanelComponent()->grooveBoxComponent().setSelectedId (2, juce::sendNotification);
                pump();
                const auto* g = drumGrooveAt (1);
                int expected = 0;
                {
                    std::map<std::pair<int, int>, int> cells; // (voice, step)
                    for (const auto& h : g->hits)
                        if (h.note == 36 || h.note == 38 || h.note == 40)
                            cells[{ h.note, h.step32 }] = h.velocity;
                    expected = (int) cells.size();
                }
                int landed = 0;
                for (int t = 0; t < 32; ++t) // all engine rows, not just the 8 active ones
                    for (int s = 0; s < E::kSteps; ++s)
                        if (eng.getStep (t, s) > 0) ++landed;
                expectEquals (landed, expected, "every landed step is a mapped kick/snare/hat cell - note 41 sounds nowhere");

                // Bassline 001 onto the synth lane (the default lane).
                auto& bassBox = mc->keyboardPanelComponent()->basslineBoxComponent();
                expectEquals (bassBox.getNumItems(), 50, "the keyboard's BASSLINE selector offers 50");
                const int depthBefore = mc->undoDepth();
                bassBox.setSelectedId (1, juce::sendNotification);
                pump();
                expect (std::abs (eng.getBpm() - 93.0f) < 0.01f, "the tempo became the bassline's 93");
                expectEquals (eng.getSynthStep (0), 23, "A#2 at step 0 (lane value = pitch - C3 + 25)");
                expectEquals (eng.getSynthStep (1), 23, "the note holds across its 2 steps");
                expectEquals (eng.getSynthStep (2), 0, "and releases before the next note");
                expectEquals (eng.getSynthStep (4), 23, "the second A#2 at step 4");
                expectEquals (eng.getSynthStep (16), 20, "G2 at step 16");
                expectEquals (eng.getSynthStep (24), 18, "F2 at step 24");
                expectEquals (mc->undoDepth(), depthBefore + 1, "the bassline pick is ONE undoable action");
            }
            ed.reset();
            proc.releaseResources();
        }
    }
};

static PatternBankTest test;

#if JUCE_LINUX
 #include <X11/Xlib.h>
#endif

int main()
{
   #if JUCE_LINUX
    // The window-manager stand-in (the Round5Test pattern): see that
    // suite's main for why the WM atoms are interned first.
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
        for (auto& m : r->messages) std::printf ("%s\n", m.toRawUTF8());
    }
    std::printf ("PatternBankTest: %d checks, %d failures\n", total, fails);
    return fails == 0 ? 0 : 1;
}
