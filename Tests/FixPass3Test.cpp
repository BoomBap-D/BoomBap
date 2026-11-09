// Fix Pass 3 regression test (FEEDBACK-V2 items 69/71/72/74/76; the
// item 75 WAV coverage lives in PadLoadTest).
//
// Item 69 (track delete + marquee): engine-level - the protections
// (kit rows, pad rows, audio lane 0 refuse), the shift semantics (the
// row above inherits nothing, the row below moves down whole with its
// steps), clip lane shifting, and the take un-homing when its lane
// dies. App-level - deleting a sequencer row through the real menu
// action is ONE undoable action (undo restores the row and its steps
// exactly; redo re-deletes), and an audio lane deleted inside an undo
// gesture comes back on undo and stays dead on redo (no ghost clips).
// Component-level - a rubber-band drag over empty lane space selects
// exactly the clips it covers, and deleting that selection is one
// gesture (one begin/end pair at the undo boundary).
//
// Item 76 (step swipe): real fabricated mouse events on real step
// buttons - a left-drag paints the press's value across every crossed
// step as ONE undo gesture; dragging back over the starting cell does
// NOT toggle it off (cells are set, never toggled); a right-drag
// erases; a plain click still cycles 0 -> 90 -> 127 -> 0.
//
// Items 71/72/74 (pad bank): the empty waveform click asks for the file
// chooser (74); the editor's five value controls are rotary knobs (72);
// the info strip names the selected pad (71).
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/Multitrack.h"
#include "../Source/App/PadBank.h"
#include "../Source/App/UiComponents.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

#if JUCE_LINUX
 #include <X11/Xlib.h>
 #include <execinfo.h>
 #include <unistd.h>
 #include <csignal>
static void crashHandler (int sig)
{
    std::fprintf (stderr, "signal %d caught, backtrace:\n", sig);
    void* frames[32];
    const int count = backtrace (frames, 32);
    backtrace_symbols_fd (frames, count, STDERR_FILENO);
    _exit (3);
}
#endif

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("[%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush (stdout);
    if (! ok) ++failures;
}

// A fabricated mouse event on a component, at a position relative to it.
static juce::MouseEvent makeEvent (juce::Component& comp, float x, float y, bool rightButton = false)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto mods = rightButton ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)
                                  : juce::ModifierKeys();
    return juce::MouseEvent (src, { x, y }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &comp, &comp, juce::Time::getCurrentTime(), { x, y },
                             juce::Time::getCurrentTime(), 1, false);
}

template <typename T>
static void collectChildren (juce::Component& root, std::vector<T*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c)) out.push_back (t);
        collectChildren (*c, out);
    }
}

int main()
{
   #if JUCE_LINUX
    std::signal (SIGSEGV, crashHandler);
    std::signal (SIGABRT, crashHandler);
    if (Display* dpy = XOpenDisplay (nullptr))
    {
        for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW",
                            "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                            "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
                            "_NET_WM_STATE_HIDDEN", "_MOTIF_WM_HINTS" })
            XInternAtom (dpy, name, False);
    }
   #endif

    juce::ScopedJuceInitialiser_GUI init;

    //======================================================================
    // Item 69, engine level: protections + shift semantics.
    {
        boombap::BoomBapEngine e;
        e.prepare (44100.0);
        check (e.getNumTracks() == 8, "69: the starter layout is the original 8 rows");
        while (e.getNumTracks() < 23) e.addTrack(); // grow to rows 21 + 22 (user rows)
        check (e.getNumTracks() == 23, "69: the grid grew to 23 rows");
        check (! e.removeTrack (0), "69: a kit row refuses deletion");
        check (! e.removeTrack (4), "69: the last kit row refuses deletion");
        check (! e.removeTrack (5), "69: a pad row refuses deletion (it belongs to a pad)");
        check (! e.removeTrack (20), "69: the last pad row refuses deletion");
        check (! e.removeAudioTrack (0), "69: audio lane 0 (the take's home) refuses deletion");
        e.setStep (21, 3, 90);
        e.setStep (22, 5, 127);
        e.setTrackGain (22, 0.33f);
        check (e.removeTrack (21), "69: a user-added row deletes");
        check (e.getNumTracks() == 22, "69: the row count drops by one");
        check (e.getStep (21, 5) == 127, "69: the row below moves down whole (its steps come with it)");
        check (e.getStep (21, 3) == 0, "69: the deleted row's steps are gone, not inherited");
        check (std::abs (e.getTrackGain (21) - 0.33f) < 0.001f, "69: the row below keeps its own gain");

        // audio lanes: clips shift, the take un-homes when its lane dies
        e.addAudioTrack();
        e.addAudioTrack(); // 3 lanes
        e.setRecTargetTrack (2);
        e.setVocalTake (std::vector<float> ((size_t) 4410, 0.3f));
        const int slotTop = e.placeTakeAsClip();
        check (slotTop >= 0 && e.getAudioClip (slotTop).track == 2, "69: a take staged on lane 3");
        check (e.takeIsPlaced(), "69: the take is homed in its clip");
        check (e.removeAudioTrack (1), "69: an added audio lane deletes");
        check (e.getNumAudioTracks() == 2, "69: the lane count drops by one");
        check (e.getAudioClip (slotTop).active && e.getAudioClip (slotTop).track == 1,
               "69: the clip above the deleted lane shifts down one lane");
        check (e.removeAudioTrack (1), "69: the lane holding the take's clip deletes");
        check (! e.takeIsPlaced(), "69: the take un-homes when its clip dies with the lane (no invisible audio)");
        check (e.placeTakeAsClip() >= 0, "69: the un-homed take can be homed again (its audio survived)");
    }

    //======================================================================
    // Item 69, app level: the row delete is ONE undoable action; the
    // audio lane delete leaves no ghost clips across undo/redo.
    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "69: the editor hosts the main view");
        if (main == nullptr) return 1;
        auto& engine = proc.engine;

        while (engine.getNumTracks() < 23) engine.addTrack(); // rows 21, 22 are user rows
        engine.setStep (21, 3, 90);
        engine.setStep (22, 5, 127);
        main->driveDeleteSequencerRow (21);
        check (engine.getNumTracks() == 22, "69/app: the menu action deletes the row");
        check (engine.getStep (21, 5) == 127 && engine.getStep (21, 3) == 0,
               "69/app: the shift lands exactly as the engine promised");
        main->undoProject();
        check (engine.getNumTracks() == 23 && engine.getStep (21, 3) == 90 && engine.getStep (22, 5) == 127,
               "69/app: ONE undo restores the row and both rows' steps");
        main->redoProject();
        check (engine.getNumTracks() == 22 && engine.getStep (21, 5) == 127,
               "69/app: redo re-deletes the same row");
        main->driveDeleteSequencerRow (5);
        check (engine.getNumTracks() == 22, "69/app: the menu action refuses a pad row");

        // audio lane delete inside a gesture: undo restores, redo must
        // not resurrect the dead lane's clips.
        engine.addAudioTrack();
        engine.addAudioTrack(); // 3 lanes
        engine.setRecTargetTrack (1);
        engine.setVocalTake (std::vector<float> ((size_t) 4410, 0.3f));
        const int slotA = engine.placeTakeAsClip();
        engine.setRecTargetTrack (2);
        engine.setVocalTake (std::vector<float> ((size_t) 4410, 0.3f));
        const int slotB = engine.placeTakeAsClip();
        check (slotA >= 0 && slotB >= 0 && engine.audioClipCount() == 2, "69/app: two clips staged");
        main->runUndoable ([&] { engine.removeAudioTrack (1); });
        check (engine.getNumAudioTracks() == 2 && engine.audioClipCount() == 1
               && engine.getAudioClip (slotB).track == 1,
               "69/app: lane delete takes its clips and shifts the rest");
        main->undoProject();
        check (engine.getNumAudioTracks() == 3 && engine.audioClipCount() == 2
               && engine.getAudioClip (slotA).track == 1 && engine.getAudioClip (slotB).track == 2,
               "69/app: undo restores the lane and both clips exactly");
        main->redoProject();
        check (engine.getNumAudioTracks() == 2 && engine.audioClipCount() == 1
               && ! engine.getAudioClip (slotA).active,
               "69/app: redo re-deletes - the dead lane's clip does not come back as a ghost");
    }

    //======================================================================
    // Item 69, component level: marquee select + one-gesture delete,
    // and the whole-lane delete button path.
    {
        boombap::BoomBapEngine e;
        e.prepare (44100.0);
        e.addAudioTrack(); // 2 lanes
        e.setRecTargetTrack (0);
        e.setVocalTake (std::vector<float> ((size_t) 44100, 0.3f));
        const int slotA = e.placeTakeAsClip();
        e.setRecTargetTrack (1);
        e.setVocalTake (std::vector<float> ((size_t) 44100, 0.3f));
        const int slotB = e.placeTakeAsClip();
        e.moveAudioClip (slotB, (long long) (4.0 * e.barSamples()));
        check (slotA >= 0 && slotB >= 0, "69/ui: two clips staged on two lanes");

        int begins = 0, ends = 0;
        MultitrackComponent mt (e, [] {});
        mt.onBeforeChange = [&] { ++begins; };
        mt.onAfterChange = [&] { ++ends; };
        mt.addToDesktop (0);
        mt.setBounds (0, 0, 1560, 900);
        mt.setVisible (true);
        mt.refreshFromEngine();

        // rubber band from empty lane-0 space (700,120) back over the
        // lane-2 clip (its rect starts near x=545, y=187)
        mt.mouseDown (makeEvent (mt, 700.0f, 120.0f));
        mt.mouseDrag (makeEvent (mt, 560.0f, 230.0f));
        mt.mouseUp (makeEvent (mt, 560.0f, 230.0f));
        mt.deleteSelectedClip();
        check (! e.getAudioClip (slotB).active, "69/ui: the marquee-covered clip is deleted");
        check (e.getAudioClip (slotA).active, "69/ui: the clip outside the band survives");
        check (begins == 1 && ends == 1, "69/ui: the marquee delete is ONE undoable gesture");

        mt.deleteAudioTrack (0);
        check (e.getNumAudioTracks() == 2 && begins == 1, "69/ui: lane 0 has no delete path");
        mt.deleteAudioTrack (1);
        check (e.getNumAudioTracks() == 1 && begins == 2 && ends == 2,
               "69/ui: the lane delete runs as one gesture");
        check (e.getAudioClip (slotA).active, "69/ui: lane 1's clip is untouched by lane 2's deletion");
    }

    //======================================================================
    // Item 76: the step swipe, driven with real events on real buttons.
    {
        boombap::BoomBapEngine e;
        e.prepare (44100.0);
        int begins = 0, ends = 0;
        SequencerPanel::Callbacks cb;
        cb.onBeforeChange = [&] { ++begins; };
        cb.onAfterChange = [&] { ++ends; };
        SequencerPanel panel (e, cb);
        panel.setSize (1544, 380);

        std::vector<StepButton*> allSteps;
        collectChildren (panel, allSteps);
        std::vector<StepButton*> steps;
        for (auto* s : allSteps)
            if (s->isVisible()) steps.push_back (s); // rows past the track count exist but are hidden
        check ((int) steps.size() == 32 * e.getNumTracks(), "76: the grid's step buttons are reachable");
        std::sort (steps.begin(), steps.end(), [] (StepButton* a, StepButton* b)
        {
            const auto ra = a->getBoundsInParent(), rb = b->getBoundsInParent();
            return ra.getY() != rb.getY() ? ra.getY() < rb.getY() : ra.getX() < rb.getX();
        });
        if ((int) steps.size() == 32 * e.getNumTracks())
        {
            // the engine ships a demo pattern - clear row 0 and sync the
            // buttons, so the swipe starts from a known-empty row
            for (int s = 0; s < 32; ++s) e.setStep (0, s, 0);
            panel.refreshGrid();
            begins = ends = 0;
            auto* step0 = steps[0];
            const float yInButton = 10.0f;
            // press step 0, drag to step 6's centre (grid x = 382+6*33+5+16)
            step0->mouseDown (makeEvent (*step0, 5.0f, yInButton));
            check (e.getStep (0, 0) == 90, "76: the press itself lands (normal velocity)");
            check (begins == 1 && ends == 0, "76: the swipe holds ONE gesture open");
            const float targetParentX = 382.0f + 6.0f * 33.0f + 5.0f + 16.0f;
            const float dragX = targetParentX - (float) step0->getBoundsInParent().getX();
            step0->mouseDrag (makeEvent (*step0, dragX, yInButton));
            bool painted = true;
            for (int s = 0; s <= 6; ++s) painted = painted && e.getStep (0, s) == 90;
            check (painted, "76: the drag paints every step it crosses");
            check (e.getStep (0, 7) == 0, "76: the drag stops where the pointer stops");
            // drag BACK over the starting cell: it must not toggle off
            step0->mouseDrag (makeEvent (*step0, 5.0f, yInButton));
            check (e.getStep (0, 0) == 90 && e.getStep (0, 3) == 90,
                   "76: re-crossing the start cell does not toggle it (set, never toggle)");
            step0->mouseUp (makeEvent (*step0, 5.0f, yInButton));
            check (begins == 1 && ends == 1, "76: the whole swipe was one undoable action");

            // right-drag erases
            auto* step4 = steps[4];
            step4->mouseDown (makeEvent (*step4, 5.0f, yInButton, true));
            const float eraseTargetX = 382.0f + 6.0f * 33.0f + 5.0f + 16.0f;
            const float eraseDragX = eraseTargetX - (float) step4->getBoundsInParent().getX();
            step4->mouseDrag (makeEvent (*step4, eraseDragX, yInButton));
            step4->mouseUp (makeEvent (*step4, eraseDragX, yInButton));
            check (e.getStep (0, 4) == 0 && e.getStep (0, 5) == 0 && e.getStep (0, 6) == 0,
                   "76: the right-drag erases every step it crosses");
            check (e.getStep (0, 0) == 90 && e.getStep (0, 3) == 90,
                   "76: the erase leaves steps outside the drag alone");
            check (begins == 2 && ends == 2, "76: the erase is its own single gesture");

            // a plain click still cycles
            auto* step10 = steps[10];
            step10->mouseDown (makeEvent (*step10, 5.0f, yInButton));
            step10->mouseUp (makeEvent (*step10, 5.0f, yInButton));
            check (e.getStep (0, 10) == 90, "76: a plain click still sets a step");
            step10->mouseDown (makeEvent (*step10, 5.0f, yInButton));
            step10->mouseUp (makeEvent (*step10, 5.0f, yInButton));
            check (e.getStep (0, 10) == 127, "76: a second click still cycles to accent");
            check (begins == 4 && ends == 4, "76: plain clicks keep their one-gesture-each shape");
        }
    }

    //======================================================================
    // Items 71/72/74: the pad bank.
    {
        boombap::BoomBapEngine e;
        e.prepare (44100.0);
        int loadCalls = 0;
        PadBankComponent bank (e, [&] (int) { ++loadCalls; },
                               [] (int) { return juce::String(); });
        bank.setSize (1200, 620);

        std::vector<PadWaveView*> waves;
        collectChildren (bank, waves);
        check (waves.size() == 1, "74: the pad editor's waveform is reachable");
        if (! waves.empty())
        {
            waves[0]->mouseDown (makeEvent (*waves[0], 12.0f, 12.0f));
            check (loadCalls == 1, "74: clicking the EMPTY waveform asks for the file chooser");
            // load a real sample onto the selected pad: now the same
            // click must audition, not load
            std::vector<float> tone ((size_t) 4410, 0.4f);
            e.loadPadSample (0, tone, 44100.0);
            bank.refreshAll();
            waves[0]->mouseDown (makeEvent (*waves[0], 60.0f, 12.0f));
            waves[0]->mouseUp (makeEvent (*waves[0], 60.0f, 12.0f));
            check (loadCalls == 1, "74: clicking a LOADED waveform does not reopen the chooser");
        }

        std::vector<juce::Slider*> sliders;
        collectChildren (bank, sliders);
        int rotary = 0;
        for (auto* s : sliders)
            if (s->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag) ++rotary;
        check (rotary == 5, "72: the pad editor's five value controls are rotary knobs");

        bank.selectPad (3, false);
        std::vector<juce::Label*> labels;
        collectChildren (bank, labels);
        bool stripFound = false;
        for (auto* l : labels)
            if (l->getText().contains ("PAD 4")) stripFound = true;
        check (stripFound, "71: the info strip names the selected pad");
    }

    std::printf ("%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
