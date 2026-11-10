// Round 2 regression test (FEEDBACK-V2 items 93/94/95/96/97 + the
// Station rework, item 81).
//
// Item 95 (the "doesn't work" core): mute/solo are ONE engine-owned
// state - toggling on the board silences the audio path, shows on the
// board button afterwards (the old click cancelled itself through a
// stale re-read), and agrees with the Station rail and the sequencer,
// which read the same engine state. A board fader drag moves the
// engine gain with the cap glued to the value (the vanishing thumb),
// and a gain change RAMPS on the audio thread instead of stepping
// (the jitter) - proven with a continuous vocal input whose first
// block after a gain change is measurably quieter than steady state.
// Clicking a board strip's NAME selects it, and its name box lights.
//
// Item 93: the transport navigation - TransportNav's double-press
// window (fake clock), the engine's park/seek (PLAY starts at the
// park, STOP returns it to zero, ends clamp), the clock's three
// formats as exact strings, format switching through the Station
// clock (persisted by the owner), and the channel DROP-DOWN binding
// both ways (select in the list -> the app follows; select in the
// app -> the list follows).
//
// Item 94: the FULL chain on channels that used to hide stages (a
// kit row now shows PREAMP and LIMITER), and the EQ band knobs'
// two-way sync with the nodes (turn a knob -> the engine's strip and
// the display's strip agree; drag a node -> the knob reads it back),
// colour-matched to the band's node.
//
// Item 96: the factory presets (VOCAL / DRUM / SNARE) exist in the
// Presets home and load through the real preset path; EDIT SAMPLE is
// enabled exactly for the channels that have an editor; the board
// does not carry the Station's top GR readout.
//
// What is NOT asserted: how the new knob/fader skin FEELS under a
// hand, and the real Windows taskbar/minimise behaviour - the
// owner's hands and screen are the verdict for those (as ever).
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/StationPanel.h"
#include "../Source/App/UiComponents.h"
#include "../Source/App/AppSettings.h"
#include "../Source/App/DataHome.h"
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

static juce::MouseEvent makeEvent (juce::Component& comp, float x, float y, int clicks = 1)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (src, { x, y }, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &comp, &comp, juce::Time::getCurrentTime(), { x, y },
                             juce::Time::getCurrentTime(), clicks, false);
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

// Render the app engine directly, 256-sample blocks like a callback.
static void renderBlocks (boombap::BoomBapEngine& e, int blocks, const float* input = nullptr)
{
    static float inSilence[256] = {};
    static float L[256], R[256];
    for (int i = 0; i < blocks; ++i)
        e.process (input != nullptr ? input : inSilence, L, R, 256);
}

static float renderPeakTail (boombap::BoomBapEngine& e, int blocks)
{
    static float inSilence[256] = {};
    float L[256], R[256];
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        e.process (inSilence, L, R, 256);
        if (i >= blocks - 40) // the last ~10k samples: after any ramp settles
            for (int s = 0; s < 256; ++s) peak = std::max (peak, std::abs (L[s]));
    }
    return peak;
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
    using E = boombap::BoomBapEngine;

    //==================================================================
    // Part 1 (item 93): the pure navigation + clock arithmetic.
    {
        long long now = 100000;
        boombap::TransportNav nav;
        nav.nowMs = [&now] { return now; };
        check (nav.pressRewind() == -1, "93: a single REW press steps back one step");
        now += 200;
        check (nav.pressRewind() == -16, "93: a second press inside the window goes back a bar");
        check (nav.pressRewind() == -1, "93: the double consumed the pair - the next press is a step again");
        now += 1000;
        check (nav.pressRewind() == -1, "93: a press after the window is a step");
        check (nav.pressForward() == 1, "93: a single FF press steps forward one step");
        now += 100;
        check (nav.pressForward() == 16, "93: a double FF press goes forward a bar");

        check (E::skipTarget (1000, -1, 100.0, 5000) == 900, "93: skip back one step");
        check (E::skipTarget (1000, -16, 100.0, 5000) == 0, "93: skip back a bar clamps at zero");
        check (E::skipTarget (4900, 16, 100.0, 5000) == 5000, "93: skip forward clamps at the end");
        check (E::skipTarget (0, -1, 100.0, 5000) == 0, "93: at the start, back stays at the start");

        // clock faces, exact strings (120 bpm, 44.1k: a step is 5512.5 samples)
        check (boombap::clockText (0, 44100.0, 120.0, boombap::ClockTime) == "0:00.000",
               "93: the time clock at zero");
        check (boombap::clockText (66150, 44100.0, 120.0, boombap::ClockTime) == "0:01.500",
               "93: the time clock reads minutes:seconds.millis");
        check (boombap::clockText (1234567, 44100.0, 120.0, boombap::ClockSamples) == "1,234,567",
               "93: the samples clock groups its digits");
        check (boombap::clockText (88200, 44100.0, 120.0, boombap::ClockBars) == "2.1.00",
               "93: the bars clock at bar 2");
        check (boombap::clockText (121275, 44100.0, 120.0, boombap::ClockBars) == "2.2.48",
               "93: the bars clock reads bars.beats.ticks");
    }
    std::printf ("part 1 done: navigation + clock arithmetic (93)\n");

    //==================================================================
    // Part 2 (items 93/95): the engine - park/seek, mute/solo in the
    // audio path, and smoothed gains.
    {
        E e;
        e.prepare (44100.0);
        e.setBpm (120.0f);
        const long long length = e.transportLengthSamples();
        check (length == 176400, "93: one pattern at 120 bpm is 176400 samples");
        e.seekTransport (88200);
        check (e.transportStartPosition() == 88200, "93: a stopped seek parks the start position");
        check (e.transportPosition() == 88200, "93: the clock position follows the park");
        e.setPlaying (true);
        renderBlocks (e, 2);
        check (e.currentStep() == 16, "93: PLAY starts at the park - step 16, not step 0");
        check (e.transportPosition() > 88200 && e.transportPosition() < 88200 + 2048,
               "93: the position advances from the park");
        e.seekTransport (0);
        renderBlocks (e, 1);
        check (e.transportPosition() < 1024, "93: a live seek lands at once while playing");
        e.stopAllSound();
        renderBlocks (e, 1);
        check (e.transportStartPosition() == 0, "93: STOP returns the park to zero");
        e.seekTransport (999999999);
        check (e.transportStartPosition() == length, "93: a seek past the end clamps to the end");
        e.seekTransport (-50);
        check (e.transportStartPosition() == 0, "93: a seek before zero clamps to zero");
    }
    {
        // mute / solo in the audio path (kicks on every step)
        E e;
        e.clearPattern();
        e.setGlueEnabled (false);
        e.prepare (44100.0);
        e.setBpm (120.0f);
        for (int s = 0; s < E::kSteps; ++s) e.setStep (E::Kick, s, 127);
        e.setPlaying (true);
        check (renderPeakTail (e, 80) > 0.1f, "95: the kick sounds unmuted");
        e.setChannelMute (E::Kick, true);
        check (renderPeakTail (e, 80) < 0.002f, "95: a muted channel is silent in the render");
        e.setChannelMute (E::Kick, false);
        e.setChannelSolo (E::Snare, true); // a silent channel, soloed
        check (renderPeakTail (e, 80) < 0.002f, "95: soloing another channel silences this one");
        e.setChannelSolo (E::Kick, true);
        check (renderPeakTail (e, 80) > 0.1f, "95: soloing the channel itself brings it back");
        e.setChannelSolo (E::Snare, false);
        e.setChannelSolo (E::Kick, false);
    }
    {
        // the gain ramp: a continuous input through the vocal channel
        E e;
        e.clearPattern();
        e.setGlueEnabled (false);
        e.prepare (44100.0);
        std::vector<float> in (256);
        for (int i = 0; i < 256; ++i) in[(size_t) i] = 0.3f * std::sin (6.2832f * 220.0f * (float) i / 44100.0f);
        e.setVocalEnabled (true);
        e.setChannelGain (E::kVocalChannel, 0.0f);
        float L[256], R[256];
        double acc = 0.0;
        for (int b = 0; b < 20; ++b)
        {
            e.process (in.data(), L, R, 256);
            if (b >= 16) for (int s = 0; s < 256; ++s) acc += (double) L[s] * L[s];
        }
        check (std::sqrt (acc / (4.0 * 256.0)) < 1.0e-4, "95: zero gain is silent before the change");
        e.setChannelGain (E::kVocalChannel, 1.0f);
        e.process (in.data(), L, R, 256); // the first block after the change
        double first = 0.0;
        for (int s = 0; s < 256; ++s) first += (double) L[s] * L[s];
        const double rmsFirst = std::sqrt (first / 256.0);
        acc = 0.0;
        for (int b = 0; b < 172; ++b) // ~1 s: the ramp is long settled
        {
            e.process (in.data(), L, R, 256);
            if (b >= 132) for (int s = 0; s < 256; ++s) acc += (double) L[s] * L[s];
        }
        const double rmsSteady = std::sqrt (acc / (40.0 * 256.0));
        std::printf ("gain ramp: first block %.4f, steady %.4f\n", rmsFirst, rmsSteady);
        check (rmsSteady > 0.05, "95: the vocal path is live at full gain");
        check (rmsFirst < 0.5 * rmsSteady,
               "95: the first block after a gain change is still ramping (no zipper step)");
    }
    std::printf ("part 2 done: engine park/seek, mute/solo, gain ramps (93/95)\n");

    //==================================================================
    // Part 3 (items 93/94/95/96, app level): the real MainComponent
    // and its Station, driven the way the owner's mouse drives them.
    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "round2: the editor hosts the main view");
        if (main == nullptr) return 1;
        ed->setSize (1500, 950);
        pump (30);
        main->openStation();
        pump (30);
        auto* st = main->stationPanel();
        check (st != nullptr, "round2: the Station opens");
        if (st == nullptr) return 1;
        st->setSize (1240, 660);
        auto& eng = proc.engine;

        // ---- item 93: the drop-down binds both ways ----------------
        check (st->dropdownChannel() == main->selectedChannelId(),
               "93: the drop-down opens on the app's selected channel");
        st->channelDropdown().setSelectedId (3 + 1, juce::sendNotification);
        pump (30);
        check (main->selectedChannelId() == 3 && st->boundChannel() == 3,
               "93: picking a channel in the drop-down binds the app + the strip");
        main->selectChannel (7);
        check (st->dropdownChannel() == 7, "93: selecting in the app moves the drop-down");

        // ---- item 94: the full chain on every channel --------------
        for (int ch : { 0, E::kSynthChannel, E::kAudioChannelBase, E::kVocalChannel })
        {
            main->selectChannel (ch);
            check (st->preampStageVisible() && st->limiterStageVisible(),
                   "94: PREAMP and LIMITER show on every channel kind (no subsets)");
        }

        // ---- item 96d: EDIT SAMPLE's enable rule -------------------
        main->selectChannel (0);
        check (! st->editButtonEnabled(), "96: a kit row has no sample editor - EDIT disabled");
        main->selectChannel (E::Sample1 + 2);
        check (st->editButtonEnabled(), "96: a pad row has the pad editor - EDIT enabled");
        main->selectChannel (E::kSynthChannel);
        check (st->editButtonEnabled(), "96: the synth has its piano roll - EDIT enabled");
        main->selectChannel (E::kVocalChannel);
        check (! st->editButtonEnabled(), "96: the vocal channel has no sample editor - EDIT disabled");
        main->selectChannel (E::kMasterChannel);
        check (! st->editButtonEnabled(), "96: the master has no sample editor - EDIT disabled");

        // ---- item 94: EQ knob <-> node, two-way --------------------
        main->selectChannel (0);
        st->refreshFromEngine();
        auto& eq = st->eqDisplayComponent();
        eq.selectBand (2); // the default set's 350 Hz peak
        st->refreshFromEngine(); // syncs the band bar + knobs to the selection
        auto s0 = eng.getStripSettings (0);
        s0.eq[2].on = true;
        eng.setStripSettings (0, s0);
        st->refreshFromEngine();
        const int knobCol = (int) st->eqBandKnob (0).getProperties()["boombapKnobColour"];
        check (knobCol == (int) boombap::ui::accent().getARGB(),
               "94: a peak band's knobs carry the node's amber");
        st->eqBandKnob (0).setValue (6.0, juce::sendNotificationSync); // sync: juce::sendNotification lands on a later loop pass
        pump (20);
        const auto afterKnob = eng.getStripSettings (0);
        check (std::abs (afterKnob.eq[2].gainDb - 6.0f) < 0.05f,
               "94: turning the Gain knob writes the band gain to the engine");
        check (std::abs (eq.getStrip().eq[2].gainDb - 6.0f) < 0.05f,
               "94: the display's strip follows the knob");
        // drag the node up: the knob must read the new gain back
        const auto node = eq.bandNodePosition (2);
        eq.mouseDown (makeEvent (eq, node.x, node.y));
        eq.mouseDrag (makeEvent (eq, node.x, node.y - 40.0f));
        eq.mouseUp (makeEvent (eq, node.x, node.y - 40.0f));
        pump (20);
        const auto afterDrag = eng.getStripSettings (0);
        check (afterDrag.eq[2].gainDb > 8.0f, "94: dragging the node raises the band gain");
        check (std::abs (st->eqBandKnobValue (0) - (double) afterDrag.eq[2].gainDb) < 0.11,
               "94: the Gain knob reads the dragged node's gain back");

        // ---- item 95: the board ------------------------------------
        st->setBoardMode (true);
        pump (20);
        auto& board = st->boardComponent();
        check (board.stripCount() == eng.getNumTracks() + eng.getNumAudioTracks() + 5,
               "95: the board carries every channel");
        // the top GR readout is Station-only (item 96b)
        std::vector<juce::Label*> labels;
        collectChildren (*st, labels);
        juce::Label* gr = nullptr;
        for (auto* l : labels) if (l->getText().startsWith ("COMP ")) gr = l;
        check (gr != nullptr && ! gr->isVisible(), "96: the board does not carry the top GR readout");

        auto* mute3 = board.stripMuteButton (3);
        check (mute3 != nullptr, "95: the board strip for channel 3 has a mute button");
        mute3->triggerClick();
        pump (30);
        check (eng.getChannelMute (3), "95: the board MUTE writes the engine state");
        check (mute3->getToggleState(), "95: the board MUTE stays lit after the click (no self-cancel)");
        main->selectChannel (3);
        st->setBoardMode (false);
        pump (20);
        check (st->railMuteState(), "95: the Station rail shows the same mute (one state, three views)");
        st->setBoardMode (true);
        pump (20);
        mute3->triggerClick();
        pump (30);
        check (! eng.getChannelMute (3) && ! mute3->getToggleState(), "95: a second click unmutes everywhere");

        // clicking the strip's NAME selects it
        auto* strip5 = board.stripComponent (5);
        check (strip5 != nullptr, "95: the board strip for channel 5 exists");
        strip5->mouseDown (makeEvent (*strip5, 46.0f, 12.0f));
        pump (20);
        check (main->selectedChannelId() == 5 && board.stripSelected (5),
               "95: clicking the strip (name included) selects the channel");

        // the fader drag: the value moves and the cap stays glued to it
        auto* fader4 = board.stripFader (4);
        check (fader4 != nullptr, "95: the board strip for channel 4 has its long fader");
        const double gainBefore = eng.getChannelGain (4);
        const auto cap = fader4->capCentre();
        fader4->mouseDown (makeEvent (*fader4, cap.x, cap.y));
        fader4->mouseDrag (makeEvent (*fader4, cap.x, cap.y - 45.0f));
        check (eng.getChannelGain (4) > (float) gainBefore + 0.02f,
               "95: dragging the board fader up raises the engine gain");
        check (std::abs (fader4->getValue() - (double) eng.getChannelGain (4)) < 0.01,
               "95: the cap's value and the engine's gain are the same number (no vanishing thumb)");
        check (std::abs (fader4->capCentre().y - (fader4->trackTop()
                        + (1.0f - (float) std::pow (fader4->getValue() / 1.2, 1.5)) * fader4->trackTravel())) < 1.5f,
               "95: the cap is painted at the value's own position");
        fader4->mouseUp (makeEvent (*fader4, cap.x, cap.y - 45.0f));
        fader4->mouseDown (makeEvent (*fader4, cap.x, cap.y, 2)); // double-click
        // AMENDED by Round 6 (item 113): the double-click target used
        // to be 0.8 - the track rows' old per-kind default. Item 113
        // makes every fader start FLAT and return to FLAT: 1.0.
        check (std::abs (fader4->getValue() - 1.0) < 0.01 && std::abs ((double) eng.getChannelGain (4) - 1.0) < 0.01,
               "95/113: double-click returns the board fader to FLAT (item 6 survives, item 113's target)");

        // ---- item 93: the Station transport + clock ----------------
        // AMENDED by Round 6 (item 115): the labelled text buttons are
        // family-A console keys now (glyphs, no text). The suite takes
        // them from the panel's transport door and presses them
        // through the keys' own mouse handler; item 93's semantics
        // (park / step / double-press bar) are asserted unchanged.
        st->setBoardMode (false);
        pump (20);
        auto* toStart = st->stationTransportButtonForTest (0);
        auto* rew     = st->stationTransportButtonForTest (1);
        auto* stop    = st->stationTransportButtonForTest (4);
        auto* toEnd   = st->stationTransportButtonForTest (6);
        check (toStart != nullptr && rew != nullptr && stop != nullptr && toEnd != nullptr
                && toStart->getGlyph() == TransportButton::Glyph::ToStart
                && rew->getGlyph() == TransportButton::Glyph::Rewind
                && stop->getGlyph() == TransportButton::Glyph::Stop
                && toEnd->getGlyph() == TransportButton::Glyph::ToEnd,
               "93/115: the Station transport carries TO START / REW / STOP / TO END (family-A keys)");
        auto pressKey = [] (TransportButton* b) { b->mouseDown (makeEvent (*b, 5.0f, 5.0f)); };
        const long long len = eng.transportLengthSamples();
        const double step = eng.stepSamples();
        pressKey (toEnd);
        pump (30);
        check (eng.transportStartPosition() == len, "93: TO END parks at the end of the pattern");
        pressKey (rew);
        pump (30);
        check (std::abs ((double) (len - eng.transportStartPosition()) - step) < 4.0,
               "93: one REW press parks one step back");
        pressKey (rew); // inside the double-press window
        pump (30);
        check (std::abs ((double) (len - eng.transportStartPosition()) - 17.0 * step) < 6.0,
               "93: a double REW press parks a bar further back");
        pressKey (rew); // the pair was consumed: a step again
        pump (30);
        check (std::abs ((double) (len - eng.transportStartPosition()) - 18.0 * step) < 6.0,
               "93: the third press is a single step again");
        pressKey (toStart);
        pump (30);
        check (eng.transportStartPosition() == 0, "93: TO START parks at zero");
        pressKey (toEnd);
        pump (30);
        pressKey (stop);
        pump (30);
        renderBlocks (eng, 2);
        check (eng.transportStartPosition() == 0 && eng.transportPosition() == 0,
               "93: STOP returns the park and the clock to zero");

        // the clock: text follows the owner's format; a click cycles + persists
        const int fmtBefore = main->clockFormatValue();
        pump (200);
        check (st->clockComponent().currentText()
                   == boombap::clockText (eng.transportPosition(), eng.getSampleRate(),
                                          (double) eng.getBpm(), fmtBefore),
               "93: the Station clock shows the position in the owner's format");
        auto& clock = st->clockComponent();
        clock.mouseDown (makeEvent (clock, 10.0f, 10.0f));
        pump (20);
        check (main->clockFormatValue() == (fmtBefore + 1) % 3, "93: clicking the clock cycles the format");
        {
            auto props = boombap::appPropertiesFile();
            check (props->getIntValue ("clockFormat", -1) == (fmtBefore + 1) % 3,
                   "93: the clock format persists in the app settings");
        }

        // ---- item 96a: the factory presets --------------------------
        const auto dir = boombap::presetsDir();
        const auto snareFile = dir.getChildFile ("Factory Snare.json");
        check (dir.getChildFile ("Factory Vocal.json").existsAsFile()
                && dir.getChildFile ("Factory Drum.json").existsAsFile()
                && snareFile.existsAsFile(),
               "96: the factory presets ship - VOCAL, DRUM, SNARE");
        main->selectChannel (1);
        st->loadPresetFile (snareFile);
        pump (20);
        const auto loaded = eng.getStripSettings (1);
        check (std::abs (loaded.compThresholdDb - -16.0f) < 0.01f
                && std::abs (loaded.hpfHz - 100.0f) < 0.01f
                && std::abs (loaded.eq[0].gainDb - 3.0f) < 0.01f
                && std::abs (loaded.eq[0].freqHz - 200.0f) < 0.5f,
               "96: the SNARE preset loads through the real preset path");

        // ---- item 96c: the master's VU ------------------------------
        std::vector<boombap::VuNeedle*> vus;
        collectChildren (*st, vus);
        check (vus.size() == 1, "96: the Station carries one VU meter");
        main->selectChannel (E::kMasterChannel);
        pump (20);
        check (! vus.empty() && vus[0]->isVisible(), "96: the VU shows on the master channel");
        main->selectChannel (0);
        pump (20);
        check (! vus.empty() && ! vus[0]->isVisible(), "96: the VU is the master's only");

        ed.reset();
    }
    std::printf ("part 3 done: app level (93/94/95/96)\n");

    std::printf ("ROUND2-TEST-DONE failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
