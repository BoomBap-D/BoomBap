// Round 3 regression test (FEEDBACK-V2 items 88, 100, 101, 80).
//
// Item 88 - TRACK naming, completed. Rows are TRACK n until content
// names them; a loaded sample's name lives in the row's FIRST COLUMN
// and nowhere else - above all, the LOAD button never renames itself
// (it used to wear the file's short name; the waveform panel's LOAD
// was made permanent in Round 1 and the rows now obey the same law).
// The one name is read from the engine by every surface: sequencer
// column, Station drop-down, piano roll. Kit rows keep their names,
// and a deleted row's name shifts down with the row.
//
// Item 88 (second half) - the visual-mix strip over the pad bank:
// cells keyed to the pads, lit by activity and by selection, in the
// multitrack strip's own language; a cell click selects its pad.
//
// Item 100 - pad names BACK on the pads (reversing item 71's caption
// cut on purpose); pads a touch bigger at the same window size; the
// wording under the pads moved into the info panel under the knobs;
// drags - pad -> pad through the real mouse handlers and the owner's
// copy function, with copy semantics proven both ways (mutate the
// copy, the source stands; mutate the source, the copy stands).
//
// Item 80/101 - Start/End are SLIDERS again, two-way synced with the
// waveform's pull bars (drag either, both move); the multi-point
// envelope: default four flat points shown over an untouched pad
// (implicit - the engine holds nothing), double-click adds, drag
// shapes, right-click removes, and a shaped envelope measurably
// shapes the rendered sound (a deep mid dip vs the same pad flat).
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/PadBank.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/StationPanel.h"
#include "../Source/App/UiComponents.h"
#include "../Source/App/ProjectFile.h"
#include "../Source/Core/PadsMPC/MpcPads.h"
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

static juce::MouseEvent makeEvent (juce::Component& comp, float x, float y,
                                   juce::ModifierKeys mods = juce::ModifierKeys(),
                                   float downX = -1.0f, float downY = -1.0f)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> down (downX >= 0.0f ? downX : x, downY >= 0.0f ? downY : y);
    return juce::MouseEvent (src, { x, y }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &comp, &comp, juce::Time::getCurrentTime(), down,
                             juce::Time::getCurrentTime(), 1, false);
}

// A named WAV in the temp folder - the NAME is the fixture: item 88 is
// about names, so the files arrive already christened.
static juce::File writeNamedWav (const juce::String& name, float seconds = 0.5f, float freq = 220.0f)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> out (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (out.release(), 44100.0, 1, 16, {}, 0));
    const int n = (int) (44100.0f * seconds);
    juce::AudioBuffer<float> buf (1, n);
    for (int i = 0; i < n; ++i)
        buf.setSample (0, i, 0.5f * std::sin (2.0f * 3.14159265f * freq * (float) i / 44100.0f));
    w->writeFromAudioSampleBuffer (buf, 0, n);
    w.reset();
    out.reset();
    return f;
}

static float rmsOf (const std::vector<float>& v, int from, int to)
{
    double acc = 0.0;
    for (int i = from; i < to && i < (int) v.size(); ++i) acc += (double) v[(size_t) i] * v[(size_t) i];
    return (float) std::sqrt (acc / std::max (1, to - from));
}

static const std::vector<std::pair<float, float>> kDip =
    { { 0.0f, 1.0f }, { 0.4f, 1.0f }, { 0.5f, 0.1f }, { 0.6f, 1.0f }, { 1.0f, 1.0f } };

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

    //======================================================================
    // Part 1 (item 88): the naming model, at engine level.
    {
        E e;
        e.prepare (44100.0);
        check (e.displayTrackName (0) == "Kick" && e.displayTrackName (4) == "Clap",
               "88: kit rows keep their names (the kit IS their content)");
        check (e.displayTrackName (5) == "TRACK 6" && e.displayTrackName (20) == "TRACK 21",
               "88: unloaded rows are TRACK n - 'Sample N' is gone");
        check (E::trackName (7) == "TRACK 8", "88: the base name is TRACK n");
        e.setTrackContentName (5, "MyKick");
        check (e.displayTrackName (5) == "MyKick", "88: content names the track");
        check (e.channelDisplayName (5) == "MyKick", "88: the channel name follows the content");
        check (e.channelDisplayName (E::kSynthChannel) == "Synth"
                 && e.channelDisplayName (E::kVocalChannel) == "Vocal"
                 && e.channelDisplayName (E::kMasterChannel) == "Master",
               "88: non-row channels keep their names");
        e.setTrackContentName (5, "");
        check (e.displayTrackName (5) == "TRACK 6", "88: unbinding returns the row to TRACK n");

        while (e.getNumTracks() < 23) e.addTrack();
        e.setTrackContentName (21, "First");
        e.setTrackContentName (22, "Second");
        check (e.removeTrack (21), "88: a user row deletes");
        check (e.displayTrackName (21) == "Second", "88: the name shifts down with its row");
        check (e.displayTrackName (22) == "TRACK 23", "88: the vacated row is plain TRACK n");
    }
    std::printf ("part 1 done: naming model (88)\n");

    //======================================================================
    // Part 2 (item 101): the envelope - model, settings + file round
    // trips, and the rendered sound itself.
    {
        using boombap::MpcPads;
        check (MpcPads::envLevelAt ({}, 0.5f) == 1.0f, "101: no points = flat");
        check (std::abs (MpcPads::envLevelAt (kDip, 0.5f) - 0.1f) < 0.001f,
               "101: the curve reads its dip at the centre");
        check (std::abs (MpcPads::envLevelAt (kDip, 0.45f) - 0.55f) < 0.001f,
               "101: the curve interpolates between points");
        check (MpcPads::envLevelAt (kDip, 0.0f) == 1.0f && MpcPads::envLevelAt (kDip, 1.0f) == 1.0f,
               "101: the curve holds its ends");

        auto renderPad = [] (const MpcPads::PadParams& pp)
        {
            MpcPads pads;
            pads.prepare (44100.0);
            pads.loadSample (0, std::vector<float> (44100, 0.5f), 44100.0);
            pads.setPadParams (0, pp);
            pads.trigger (0, 1.0f);
            std::vector<float> out (44100);
            for (int i = 0; i < 44100; ++i) out[(size_t) i] = pads.processPad (0);
            return out;
        };
        MpcPads::PadParams flat; // envPoints empty
        const auto flatOut = renderPad (flat);
        MpcPads::PadParams dipped;
        dipped.envPoints = kDip;
        const auto dipOut = renderPad (dipped);
        const int mid0 = 19845, mid1 = 24255; // the window's 45%..55%
        const float flatMid = rmsOf (flatOut, mid0, mid1);
        const float dipMid = rmsOf (dipOut, mid0, mid1);
        check (flatMid > 0.45f, "101: the flat pad renders at full level mid-window");
        check (dipMid < 0.45f * flatMid,
               "101: the dip MEASURABLY cuts the mid-window energy vs the same pad flat");
        const float flatHead = rmsOf (flatOut, 4410, 8820);
        const float dipHead = rmsOf (dipOut, 4410, 8820);
        check (dipHead > 0.9f * flatHead && dipHead < 1.1f * flatHead,
               "101: outside the dip the shaped pad matches the flat one");

        // settings round trip (the undo snapshot path)
        E a; a.prepare (44100.0);
        auto pp = a.getPadParams (3);
        pp.envPoints = kDip;
        a.setPadParams (3, pp);
        const auto s = a.getSettings();
        check (s.padEnvCount[3] == 5 && std::abs (s.padEnvTime[3][2] - 0.5f) < 1e-4f
                 && std::abs (s.padEnvLevel[3][2] - 0.1f) < 1e-4f,
               "101: the snapshot carries the points");
        E b; b.prepare (44100.0);
        b.applySettings (s);
        const auto pp2 = b.getPadParams (3);
        check (pp2.envPoints.size() == 5 && std::abs (pp2.envPoints[2].second - 0.1f) < 1e-4f,
               "101: applying the snapshot rebuilds the points");

        // project file round trip (the save/load path)
        boombap::ProjectData d;
        d.settings = s;
        const auto json = boombap::projectToJson (d, juce::File());
        boombap::ProjectData d2;
        juce::String err;
        check (boombap::projectFromJson (json, juce::File(), d2, err),
               "101: the project JSON parses back");
        check (d2.settings.padEnvCount[3] == 5
                 && std::abs (d2.settings.padEnvLevel[3][2] - 0.1f) < 1e-4f,
               "101: the envelope survives the file round trip");
        check (json.contains ("\"env\""), "101: the envelope is really in the file text");
    }
    std::printf ("part 2 done: envelope model + sound (101)\n");

    //======================================================================
    // Part 3 (items 88/100/80/101): the pad bank at component level,
    // in a real window (drags need real screen geometry).
    {
        E e;
        e.prepare (44100.0);
        e.loadPadSample (4, std::vector<float> (44100, 0.4f), 44100.0);
        e.loadPadSample (9, std::vector<float> (44100, 0.3f), 44100.0);

        juce::String names[16];
        names[4] = "/sounds/KickA.wav";
        names[9] = "/sounds/HatB.wav";
        std::vector<std::pair<int, int>> copies;
        PadBankComponent bank (e,
                               [] (int) {},
                               [&] (int p) { return names[p]; });
        bank.onPadCopyRequest = [&] (int from, int to) { copies.push_back ({ from, to }); };

        juce::DocumentWindow win ("Round 3 pad bank", juce::Colours::black,
                                  juce::DocumentWindow::closeButton);
        win.setContentNonOwned (&bank, false);
        win.setSize (1024, 660);
        win.setVisible (true);
        pump (80);
        bank.refreshAll();

        const int bw = bank.getWidth(), bh = bank.getHeight();
        check (bw >= 900 && bh >= 540, "r3: the bank laid out at a real size");

        // Item 100: pads a touch BIGGER than the pre-round geometry
        // (54% share, 1.4 aspect, the wording's 104 px height tax),
        // computed for this very window size.
        {
            const int oldAvailW = (bw * 54) / 100 - 24, oldAvailH = bh - 104;
            const int oldCellW = (oldAvailW - 24) / 4, oldCellH = (oldAvailH - 24) / 4;
            const int oldW = std::min (oldCellW, (int) (oldCellH * 1.4f));
            const int oldH = (int) (oldW / 1.4f);
            const auto pb = bank.padBounds (0);
            check (pb.getWidth() > oldW && pb.getHeight() > oldH,
                   "100: the pads are bigger than the old geometry at this size");
        }

        // Item 100: the name is back ON the pad.
        check (bank.padCaption (4) == "KickA", "100: the pad carries its sample's name");
        check (bank.padCaption (5).isEmpty(), "100: an empty pad carries no name");

        // Item 88: the strip - activity, selection, cell clicks.
        bank.showActivity (7);
        check (bank.nameStripComponent().cellFlash (7) > 0.9f,
               "88: the strip cell lights with the pad's activity");
        bank.selectPad (3, false);
        check (bank.nameStripComponent().selectedPad() == 3 && bank.selectedPadIndex() == 3,
               "88: the strip wears the selection");
        auto& strip = bank.nameStripComponent();
        const auto cell11 = strip.cellBounds (11);
        strip.mouseDown (makeEvent (strip, (float) cell11.getCentreX(), (float) cell11.getCentreY()));
        check (bank.selectedPadIndex() == 11, "88: a strip cell click selects its pad");

        // Item 100: hit-testing, then a REAL pad -> pad drag through
        // the pads' own mouse handlers.
        check (bank.padAtScreenPosition (bank.padComponent (9).getScreenBounds().getCentre()) == 9,
               "100: screen hit-testing finds the pad under the point");
        check (bank.padAtScreenPosition ({ -500, -500 }) == -1,
               "100: hit-testing misses cleanly off the bank");
        auto& padA = static_cast<PadButton&> (bank.padComponent (4));
        auto& padB = static_cast<PadButton&> (bank.padComponent (9));
        const auto target = padB.getScreenBounds().getCentre();
        const auto rel = padA.getLocalPoint (nullptr, target.toFloat());
        padA.mouseDown (makeEvent (padA, 10.0f, 10.0f));
        padA.mouseDrag (makeEvent (padA, rel.x, rel.y, juce::ModifierKeys(), 10.0f, 10.0f));
        check (padB.isDragTarget(), "100: the pad under the drag lights as the target");
        padA.mouseUp (makeEvent (padA, rel.x, rel.y, juce::ModifierKeys(), 10.0f, 10.0f));
        check (copies.size() == 1 && copies[0].first == 4 && copies[0].second == 9,
               "100: dropping pad 5 on pad 10 asks the owner to copy 5 -> 10");
        check (! padB.isDragTarget(), "100: the target light goes out after the drop");

        // Items 80/101: Start/End are sliders; slider -> pull bars ->
        // engine all agree; then the pull bars drive the sliders back.
        bank.selectPad (4, false);
        check (bank.startSlider().getSliderStyle() == juce::Slider::LinearHorizontal
                 && bank.endSlider().getSliderStyle() == juce::Slider::LinearHorizontal,
               "80: Start/End are horizontal sliders again");
        check (bank.tuneKnob().getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
               "80: Tune stays a rotary knob (the family the LookAndFeel draws)");
        bank.startSlider().setValue (25.0, juce::sendNotificationSync);
        check (std::abs (e.getPadParams (4).startFrac - 0.25f) < 0.01f,
               "80: the Start slider moves the engine's trim");
        check (std::abs (bank.waveComponent().windowStart() - 0.25f) < 0.01f,
               "80: the pull bars follow the slider");
        auto& wv = bank.waveComponent();
        const float wvw = (float) wv.getWidth();
        const float endBarX = 6.0f + (wvw - 12.0f) * 1.0f; // the end bar, window at [0.25, 1]
        wv.mouseDown (makeEvent (wv, endBarX, 20.0f));
        wv.mouseDrag (makeEvent (wv, 6.0f + (wvw - 12.0f) * 0.6f, 20.0f));
        wv.mouseUp (makeEvent (wv, 6.0f + (wvw - 12.0f) * 0.6f, 20.0f));
        check (std::abs (bank.endSlider().getValue() - 60.0) < 1.5,
               "80: the pull bar drives the End slider back");
        check (std::abs (e.getPadParams (4).endFrac - 0.60f) < 0.02f,
               "80: the pull bar's trim lands in the engine");

        // Item 101: the envelope lane - implicit defaults, add, shape,
        // remove, all landing in the engine.
        bank.selectPad (5, false); // pad 6: untouched
        auto& env = bank.envelopeComponent();
        check (env.pointCount() == 4 && env.isImplicit(),
               "101: an untouched pad shows the default four flat points");
        check (e.getPadParams (5).envPoints.empty(),
               "101: ...and the engine still holds NO envelope for it");
        const float ew = (float) env.getWidth(), eh = (float) env.getHeight();
        env.mouseDoubleClick (makeEvent (env, ew * 0.5f, eh * 0.5f));
        check (env.pointCount() == 5, "101: double-click adds a point");
        check (! env.isImplicit() && e.getPadParams (5).envPoints.size() == 5,
               "101: the first edit makes the points real in the engine");
        const auto midPos = env.pointComponentPos (2);
        env.mouseDown (makeEvent (env, midPos.x, midPos.y));
        env.mouseDrag (makeEvent (env, midPos.x, eh * 0.8f));
        env.mouseUp (makeEvent (env, midPos.x, eh * 0.8f));
        const auto shaped = e.getPadParams (5).envPoints;
        check (shaped.size() == 5 && shaped[2].second < 0.3f,
               "101: dragging a point shapes it (and the engine hears it)");
        const auto p2 = env.pointComponentPos (2);
        env.mouseDown (makeEvent (env, p2.x, p2.y,
                                  juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
        check (env.pointCount() == 4 && e.getPadParams (5).envPoints.size() == 4,
               "101: right-click a point removes it");

        win.setVisible (false);
    }
    std::printf ("part 3 done: pad bank components (88/100/80/101)\n");

    //======================================================================
    // Part 4 (items 88/100): the whole app - names through the real
    // load paths, the LOAD invariant, save/load, and the pad copy's
    // independence in BOTH directions.
    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "r3: the editor hosts the main view");
        if (main == nullptr) return 1;
        ed->setSize (1500, 950);
        pump (30);
        auto* seq = main->sequencerPanelComponent();
        check (seq != nullptr, "r3: the sequencer panel is reachable");
        if (seq == nullptr) return 1;

        const auto kickFile = writeNamedWav ("Round3Kick.wav");
        const auto snareFile = writeNamedWav ("Round3Snare.wav", 0.5f, 330.0f);

        // The classic sample-row load (waveform panel slot 1 = row 6).
        main->driveLoadWaveformSample (0, kickFile);
        pump (30);
        check (seq->rowNameLabel (5).getText() == "Round3Kick",
               "88: the loaded name lands in the row's FIRST COLUMN");
        check (seq->rowLoadButton (5).getButtonText() == "Load...",
               "88: LOAD always says LOAD - the button never wears the file name");
        check (proc.engine.displayTrackName (5) == "Round3Kick",
               "88: the engine's one name agrees");
        check (seq->rowNameLabel (6).getText() == "TRACK 7",
               "88: the untouched neighbour stays TRACK n");
        check (seq->rowNameLabel (0).getText() == "Kick",
               "88: kit rows keep their names in the app too");

        // The Station drop-down reads the same name.
        main->openStation();
        pump (30);
        auto* st = main->stationPanel();
        check (st != nullptr, "r3: the Station opened");
        if (st != nullptr)
        {
            auto& box = st->channelDropdown();
            juce::String item5;
            for (int i = 0; i < box.getNumItems(); ++i)
                if (box.getItemId (i) == 6) item5 = box.getItemText (i);
            check (item5 == "ROUND3KICK", "88: the Station drop-down carries the content name");
        }

        // Persistence: the app's own snapshot -> load loop (what Open
        // does after parsing) keeps the name.
        const auto snapshot = proc.captureStateData();
        check (snapshot.samplePaths[0].endsWith ("Round3Kick.wav"),
               "88: the snapshot references the sample file");
        main->loadProjectData (snapshot, false, false);
        pump (30);
        check (seq->rowNameLabel (5).getText() == "Round3Kick",
               "88: the name survives the save/load loop");
        check (seq->rowLoadButton (5).getButtonText() == "Load...",
               "88: ...and LOAD still says LOAD after the reload");

        // Item 100: the pad copy, through the owner's real function -
        // sample + settings snapshot land; then independence, both ways.
        main->driveLoadPadFile (4, snareFile); // pad 5
        pump (20);
        auto pp = proc.engine.getPadParams (4);
        pp.startFrac = 0.2f;
        pp.endFrac = 0.8f;
        pp.envPoints = kDip;
        proc.engine.setPadParams (4, pp);
        main->driveCopyPadToPad (4, 9); // pad 5 -> pad 10
        pump (20);
        check (proc.engine.padHasSample (9), "100: the copy lands its sample on the target pad");
        const auto tgt = proc.engine.getPadParams (9);
        check (std::abs (tgt.startFrac - 0.2f) < 0.001f && std::abs (tgt.endFrac - 0.8f) < 0.001f
                 && tgt.envPoints.size() == 5,
               "100: the copy carries the settings snapshot (trim + envelope)");
        check (seq->rowNameLabel (14).getText() == "Round3Snare",
               "100: the target pad's row takes the sample's name too");
        auto* tabBank = dynamic_cast<PadBankComponent*> (main->tabContentComponent (1));
        check (tabBank != nullptr && tabBank->padCaption (9) == "Round3Snare",
               "100: the target pad's caption names the copied sample");

        auto tgt2 = proc.engine.getPadParams (9);
        tgt2.startFrac = 0.5f;
        tgt2.envPoints.clear();
        proc.engine.setPadParams (9, tgt2);
        const auto srcAfter = proc.engine.getPadParams (4);
        check (std::abs (srcAfter.startFrac - 0.2f) < 0.001f && srcAfter.envPoints.size() == 5,
               "100: mutating the copy leaves the source untouched");
        auto src2 = proc.engine.getPadParams (4);
        src2.tuneSemitones = 7.0f;
        proc.engine.setPadParams (4, src2);
        check (proc.engine.getPadParams (9).tuneSemitones == 0.0f,
               "100: mutating the source leaves the copy untouched");

        ed.reset();
    }
    std::printf ("part 4 done: app level (88/100)\n");

    std::printf ("ROUND3-TEST-DONE failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
