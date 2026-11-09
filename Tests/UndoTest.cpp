// Undo test (Pass 2, item 38): GLOBAL undo/redo over project-changing
// actions, driven through MainComponent's public history API - the same
// entry points the panels, the Station and Ctrl+Z use. Representative
// actions from the brief: a step toggle, a fader move, a clip delete and
// a strip edit must each undo and redo to the EXACT prior state; and
// transport/performance must never enter the history.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include <cstdio>
#include <vector>

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
    if (! ok) ++failures;
}

int main()
{
   #if JUCE_LINUX
    std::signal (SIGSEGV, crashHandler);
    std::signal (SIGABRT, crashHandler);
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

    BoomBapProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
    check (main != nullptr, "38: the editor hosts the main view");
    if (main == nullptr) return 1;
    auto& engine = proc.engine;

    check (! main->canUndoProject() && ! main->canRedoProject(),
           "38: a fresh project starts with an empty history");

    // ---- a step toggle ----
    {
        const int v0 = engine.getStep (0, 0);
        const int v1 = v0 == 0 ? 127 : 0;
        main->runUndoable ([&] { engine.setStep (0, 0, v1); });
        check (engine.getStep (0, 0) == v1, "38/step: the edit lands");
        check (main->canUndoProject(), "38/step: the edit is undoable");
        main->undoProject();
        check (engine.getStep (0, 0) == v0, "38/step: undo restores the exact prior step");
        check (main->canRedoProject(), "38/step: redo is armed");
        main->redoProject();
        check (engine.getStep (0, 0) == v1, "38/step: redo reapplies the edit exactly");
        main->undoProject();
    }

    // ---- a fader move ----
    {
        const float g0 = engine.getTrackGain (0);
        main->runUndoable ([&] { engine.setTrackGain (0, 0.25f); });
        check (engine.getTrackGain (0) == 0.25f, "38/fader: the move lands");
        main->undoProject();
        check (engine.getTrackGain (0) == g0, "38/fader: undo restores the exact prior gain");
        main->redoProject();
        check (engine.getTrackGain (0) == 0.25f, "38/fader: redo reapplies the exact gain");
        main->undoProject();
    }

    // ---- a clip delete (geometry must come back identical) ----
    {
        engine.setVocalTake (std::vector<float> ((size_t) 44100, 0.3f));
        const int slot = engine.placeTakeAsClip();
        check (slot >= 0 && engine.audioClipCount() == 1, "38/clip: the take is placed as one clip");
        const auto before = engine.getAudioClip (slot);
        main->runUndoable ([&] { engine.clearAudioClip (slot); });
        check (engine.audioClipCount() == 0, "38/clip: the delete lands");
        main->undoProject();
        check (engine.audioClipCount() == 1, "38/clip: undo brings the clip back");
        const auto restored = engine.getAudioClip (slot);
        check (restored.active && restored.startSamples == before.startSamples
               && restored.sourceStart == before.sourceStart
               && restored.lengthSamples == before.lengthSamples
               && restored.track == before.track,
               "38/clip: the restored clip's geometry is identical");
        main->redoProject();
        check (engine.audioClipCount() == 0,
               "38/clip: redo deletes it again - and does NOT invent a replacement clip");
        main->undoProject();
    }

    // ---- a strip edit (mixer + strip settings are project state) ----
    {
        auto st = engine.getStripSettings (0);
        const float thr0 = st.compThresholdDb;
        const bool on0 = st.compOn;
        st.compOn = true;
        st.compThresholdDb = -30.0f;
        st.eq[2].on = true;
        st.eq[2].gainDb = 6.0f;
        main->runUndoable ([&] { engine.setStripSettings (0, st); });
        check (engine.getStripSettings (0).compOn
               && engine.getStripSettings (0).compThresholdDb == -30.0f,
               "38/strip: the strip edit lands");
        main->undoProject();
        const auto back = engine.getStripSettings (0);
        check (back.compOn == on0 && back.compThresholdDb == thr0 && ! back.eq[2].on,
               "38/strip: undo restores the exact prior strip");
        main->redoProject();
        check (engine.getStripSettings (0).eq[2].gainDb == 6.0f,
               "38/strip: redo reapplies the strip exactly");
        main->undoProject();
    }

    // ---- a gesture (begin/end) is ONE action, however many edits it holds ----
    {
        const int depthBefore = main->undoDepth();
        main->beginUndoGesture();
        engine.setStep (1, 0, 127);
        engine.setStep (1, 1, 127);
        engine.setStep (1, 2, 127);
        main->endUndoGesture();
        check (main->undoDepth() == depthBefore + 1, "38/gesture: a multi-edit gesture is one undo step");
        main->undoProject();
        check (engine.getStep (1, 0) == 0 && engine.getStep (1, 1) == 0 && engine.getStep (1, 2) == 0,
               "38/gesture: one undo reverts the whole gesture");
    }

    // ---- transport + performance are NOT undoable ----
    {
        const int depthBefore = main->undoDepth();
        engine.setPlaying (true);
        engine.setPlaying (false);
        engine.triggerPad (3, 1.0f);
        engine.setAudioPlaying (true);
        engine.setAudioPlaying (false);
        check (main->undoDepth() == depthBefore,
               "38/transport: play/stop/pad hits never enter the history");
    }

    std::printf (failures == 0 ? "undo checks passed\n" : "UNDO CHECKS FAILED: %d\n", failures);

   #if JUCE_LINUX
    if (wmStandInDisplay != nullptr)
        XCloseDisplay (wmStandInDisplay);
   #endif
    return failures == 0 ? 0 : 1;
}
