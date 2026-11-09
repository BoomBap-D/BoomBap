// Round 1 regression test (FEEDBACK-V2 Breakdown #1, items 86/87/89;
// items 85/102 live in WindowFramesTest).
//
// Item 86 (name click = select): at the component level, a LEFT click
// on a sequencer row's name fires selection and ONLY selection - the
// row menu callback stays silent - while a RIGHT click selects AND
// opens the menu (the menu's new home). The selected row's name
// carries the standout treatment (solid accent fill, dark text - the
// item 68 SELECTED language). At the app level, through the real
// MainComponent, the same click moves the ONE selection state: the
// sequencer row light, the Audio Multitrack's selected channel and
// the Station's bound channel all read the clicked track - and a
// selection made through selectChannel (the funnel the multitrack
// chips and the Station board use) lights the sequencer row back.
// What is NOT asserted: the popup menu's on-screen appearance - a
// PopupMenu is not observable headless; the menu wiring is asserted
// as callback counts here, and in the app the menu call itself is the
// same showRowMenu/showKitMenu the pre-round left click ran.
//
// Item 87 (visible track delete): the delete X exists on the row
// itself and SHOWS only on user-added rows (item 69's protections:
// kit rows 0-4 and pad rows 5-20 carry no affordance). Clicking it
// through the real MainComponent runs the same guarded, undoable
// delete as the menu - one action, undo restores the row.
//
// Item 89 (waveform panel + amber traces): the panel's button reads
// LOAD and carries the amber action trace (as do POP OUT and STATION;
// a passive tab button does not). With no sample loaded, clicking the
// waveform does not audition (it asks for the chooser - the chooser
// itself is an async OS dialog and is not asserted). With a sample
// loaded through the real load path, clicking the waveform starts
// the audition (proven by rendering the real engine until the
// audition voice sounds) and clicking again stops it; the button
// still reads LOAD throughout - the old PLAY/STOP flip is gone.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/Multitrack.h"
#include "../Source/App/UiComponents.h"
#include "../Source/App/BoomBapLookAndFeel.h"
#include <cstdio>
#include <cmath>

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

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
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

// A plain half-second sine WAV, written with JUCE's own writer.
static juce::File writeTestWav()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("boombap-round1-audition.wav");
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> out (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (out.release(), 44100.0, 1, 16, {}, 0));
    juce::AudioBuffer<float> buf (1, 22050);
    for (int i = 0; i < 22050; ++i)
        buf.setSample (0, i, 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / 44100.0f));
    w->writeFromAudioSampleBuffer (buf, 0, 22050);
    w.reset();
    out.reset();
    return f;
}

// Render the engine directly so request flags (audition start/stop)
// land in the voice state the way the audio thread would land them.
static void renderBlocks (boombap::BoomBapEngine& e, int blocks)
{
    static float in[512] = {};
    static float L[512], R[512];
    for (int i = 0; i < blocks; ++i)
        e.process (in, L, R, 512);
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
    // Part 1 (items 86/87, component level): a standalone sequencer
    // panel with recording callbacks.
    {
        boombap::BoomBapEngine e;
        e.prepare (44100.0);
        while (e.getNumTracks() < 23) e.addTrack(); // rows 21/22: user-added

        int selects = 0, lastSelect = -1, menus = 0, lastMenu = -1;
        int deletes = 0, lastDelete = -1;
        SequencerPanel::Callbacks cb;
        cb.onSelectRow = [&] (int t) { ++selects; lastSelect = t; };
        cb.onNameMenu  = [&] (int t) { ++menus;   lastMenu = t; };
        cb.onDeleteRow = [&] (int t) { ++deletes; lastDelete = t; };
        SequencerPanel panel (e, cb);
        panel.setSize (1544, 900);
        panel.resized();

        // Item 87: the X shows on user rows only.
        check (panel.rowDeleteButton (21).isVisible(), "87: the delete X shows on a user-added row");
        check (panel.rowDeleteButton (22).isVisible(), "87: the delete X shows on the second user row");
        check (! panel.rowDeleteButton (0).isVisible(), "87: no delete X on a kit row (protected)");
        check (! panel.rowDeleteButton (4).isVisible(), "87: no delete X on the last kit row (protected)");
        check (! panel.rowDeleteButton (5).isVisible(), "87: no delete X on a pad row (protected)");
        check (! panel.rowDeleteButton (20).isVisible(), "87: no delete X on the last pad row (protected)");

        // Item 87: the X fires the delete callback for its own row.
        // (triggerClick posts an async command message - pump so it lands.)
        panel.rowDeleteButton (21).triggerClick();
        pump (50);
        check (deletes == 1 && lastDelete == 21, "87: the X asks to delete its own row");

        // Item 86: LEFT click on a name selects - and does NOT menu.
        auto& name3 = panel.rowNameLabel (3);
        panel.mouseDown (makeEvent (name3, 5.0f, 5.0f));
        check (selects == 1 && lastSelect == 3, "86: a left name click selects the track");
        check (menus == 0, "86: a left name click does NOT open the row menu");

        // Item 86: RIGHT click selects AND opens the menu.
        panel.mouseDown (makeEvent (name3, 5.0f, 5.0f, true));
        check (selects == 2 && lastSelect == 3, "86: a right name click still selects first");
        check (menus == 1 && lastMenu == 3, "86: a right name click opens the row menu");

        // A pad row's name has no menu on either button.
        auto& name10 = panel.rowNameLabel (10);
        panel.mouseDown (makeEvent (name10, 5.0f, 5.0f, true));
        check (lastSelect == 10 && menus == 1, "86: a pad-row name selects; it has no menu to open");

        // Item 86: the selected row's name carries the standout light.
        panel.setSelectedRow (3);
        check (panel.getSelectedRow() == 3, "86: the panel reports its selected row");
        check (panel.rowNameLabel (3).findColour (juce::Label::backgroundColourId) == boombap::ui::accent(),
               "86: the selected name is filled with the accent");
        check (panel.rowNameLabel (3).findColour (juce::Label::textColourId) == juce::Colour (0xff14100a),
               "86: the selected name's text is dark on the accent");
        check (panel.rowNameLabel (0).findColour (juce::Label::backgroundColourId) == juce::Colours::transparentBlack,
               "86: unselected names stay unlit");
        panel.setSelectedRow (40); // a channel with no sequencer row
        check (panel.getSelectedRow() == -1
                 && panel.rowNameLabel (3).findColour (juce::Label::backgroundColourId) == juce::Colours::transparentBlack,
               "86: a row-less channel (synth/master) lights no row");
    }
    std::printf ("part 1 done: sequencer component level (86/87)\n");

    //======================================================================
    // Part 2 (items 86/87/89, app level): the real MainComponent in
    // the plugin editor, exactly as the app hosts it.
    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "round1: the editor hosts the main view");
        if (main == nullptr) return 1;
        ed->setSize (1500, 950); // lay the views out at a real size
        pump (30);
        auto* seq = main->sequencerPanelComponent();
        check (seq != nullptr, "round1: the sequencer panel is reachable");
        if (seq == nullptr) return 1;

        // ---- item 89: the LOAD button + amber traces ----------------
        std::vector<juce::TextButton*> buttons;
        collectChildren (*main, buttons);
        juce::TextButton* loadBtn = nullptr;
        juce::TextButton* popOutBtn = nullptr;
        juce::TextButton* stationBtn = nullptr;
        juce::TextButton* tabBtn = nullptr;
        for (auto* b : buttons)
        {
            if (b->getButtonText() == "LOAD") loadBtn = b;
            if (b->getButtonText() == "Pop out") popOutBtn = b;
            if (b->getButtonText() == "STATION") stationBtn = b;
            if (b->getButtonText() == "STEP SEQUENCER") tabBtn = b;
        }
        check (loadBtn != nullptr, "89: the waveform panel's button reads LOAD");
        check (loadBtn != nullptr && boombap::ui::hasActionTrace (*loadBtn),
               "89: LOAD carries the amber action trace");
        check (popOutBtn != nullptr && boombap::ui::hasActionTrace (*popOutBtn),
               "89: POP OUT carries the amber action trace");
        check (stationBtn != nullptr && boombap::ui::hasActionTrace (*stationBtn),
               "89: STATION carries the amber action trace");
        check (tabBtn != nullptr && ! boombap::ui::hasActionTrace (*tabBtn),
               "89: a passive tab button carries no trace (actions only)");

        // The main window's own waveform view is a DIRECT child (the
        // pad bank's MPC panel carries another, deeper in the tree).
        WaveformView* wave = nullptr;
        for (auto* c : main->getChildren())
            if (auto* wv = dynamic_cast<WaveformView*> (c)) wave = wv;
        check (wave != nullptr, "89: the waveform view is reachable");
        if (wave == nullptr) return 1;

        // Empty panel: a click asks for the chooser - it must NOT
        // start an audition (there is nothing to hear).
        wave->mouseDown (makeEvent (*wave, 8.0f, 8.0f));
        pump (60);
        renderBlocks (proc.engine, 3);
        check (! proc.engine.isAuditionPlaying(), "89: an empty-panel click does not audition");

        // Loaded through the real load path: a click auditions, a
        // second click stops - through the real engine render.
        const auto wavFile = writeTestWav();
        main->driveLoadWaveformSample (0, wavFile);
        pump (30);
        // Loading a sample asks the engine to stop any audition (the
        // audition belongs to the sample it started on); that stop
        // request is consumed by the next rendered block. A running
        // audio thread does that within a block of the load - render
        // a few here so the click starts from the same clean state
        // the owner's app is in when he clicks.
        renderBlocks (proc.engine, 3);
        wave->mouseDown (makeEvent (*wave, 8.0f, 8.0f));
        renderBlocks (proc.engine, 4);
        check (proc.engine.isAuditionPlaying(), "89: clicking the loaded waveform auditions it");
        wave->mouseDown (makeEvent (*wave, 8.0f, 8.0f));
        renderBlocks (proc.engine, 4);
        check (! proc.engine.isAuditionPlaying(), "89: clicking again stops the audition");
        check (loadBtn != nullptr && loadBtn->getButtonText() == "LOAD",
               "89: the button still reads LOAD after auditioning (no PLAY/STOP flip)");

        // ---- item 86: one selection state, three mirrors ------------
        // The app opens on the SYNTH instrument (the constructor's
        // selectInstrument (0)) - a channel with no sequencer row, so
        // no row is lit, and the state is consistent from the start.
        check (main->selectedChannelId() == boombap::BoomBapEngine::kSynthChannel
                 && seq->getSelectedRow() == -1,
               "86: the app opens on the synth channel - no row lit, state consistent");
        auto& name3 = seq->rowNameLabel (3);
        seq->mouseDown (makeEvent (name3, 5.0f, 5.0f));
        check (main->selectedChannelId() == 3, "86: the name click selects channel 3");
        check (seq->getSelectedRow() == 3, "86: the sequencer row lights for the click");
        check (seq->rowNameLabel (3).findColour (juce::Label::backgroundColourId) == boombap::ui::accent(),
               "86: the clicked row's name is lit in the app too");

        std::vector<MultitrackComponent*> mts;
        collectChildren (*main, mts);
        check (mts.size() == 1 && mts[0]->getSelectedChannel() == 3,
               "86: the multitrack's selected channel follows the click");

        main->openStation();
        pump (30);
        check (main->stationPanel() != nullptr && main->stationPanel()->boundChannel() == 3,
               "86: the Station is bound to the clicked channel");

        // The other direction: a selection made through the funnel
        // (the multitrack chips / Station board call exactly this)
        // lights the sequencer row.
        main->selectChannel (5);
        check (seq->getSelectedRow() == 5, "86: selectChannel lights the sequencer row back");
        check (mts.size() == 1 && mts[0]->getSelectedChannel() == 5,
               "86: the multitrack follows selectChannel too");

        // ---- item 87: the X deletes through the app, undoably ------
        while (proc.engine.getNumTracks() < 23) proc.engine.addTrack();
        seq->resized(); // re-lay the rows out against the grown engine
        check (seq->rowDeleteButton (21).isVisible(), "87: the X shows on a user row in the app");
        check (! seq->rowDeleteButton (0).isVisible(), "87: no X on a kit row in the app");
        seq->rowDeleteButton (21).triggerClick();
        pump (30);
        check (proc.engine.getNumTracks() == 22, "87: the X deletes the row (item 69's action)");
        main->undoProject();
        pump (30);
        check (proc.engine.getNumTracks() == 23, "87: undo restores the deleted row (one action)");

        ed.reset();
    }
    std::printf ("part 2 done: app level (86/87/89)\n");

    std::printf ("ROUND1-TEST-DONE failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
