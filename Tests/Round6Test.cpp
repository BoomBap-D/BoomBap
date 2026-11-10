// Round 6 test (Breakdown #1 items 108-118): the Station preset-save
// crash, the pad/row decoupling (109), CLEAR PAD + the drag guard
// (110), the pad bank layout (111), the meter laws (112), flat faders
// (113), waveform drag targets (114), the family-A transport + PAUSE
// (115), the B1 VU face (116), the centred EQ buttons + RESET (117),
// and the main window's UNDO/REDO face + coverage chain (118).
//
// Proven at the levels the items live at: the engine (pause
// semantics, flat defaults, the decoupled stores measured in renders),
// the components (latches, faces, thresholds), and the real
// MainComponent (crash path through the actual button + modal dialog,
// drag landings, the mixed-action undo chain in exact reverse).
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/StationPanel.h"
#include "../Source/App/PadBank.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/Multitrack.h"
#include "../Source/App/KeyboardPanel.h"
#include "../Source/App/ProjectFile.h"
#include "../Source/App/DataHome.h"
#include <cmath>

using namespace boombap;

static std::vector<float> render (BoomBapEngine& e, int numSamples)
{
    std::vector<float> out ((size_t) numSamples), r (256), l (256);
    for (int pos = 0; pos < numSamples; pos += 256)
    {
        const int n = std::min (256, numSamples - pos);
        e.process (nullptr, l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = l[(size_t) i];
    }
    return out;
}

static float peakIn (const std::vector<float>& x, int from, int to)
{
    float p = 0.0f;
    for (int i = juce::jmax (0, from); i < juce::jmin ((int) x.size(), to); ++i)
        p = std::max (p, std::abs (x[(size_t) i]));
    return p;
}

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

static juce::MouseEvent makeEvent (juce::Component& comp, float x, float y,
                                   juce::ModifierKeys mods = juce::ModifierKeys(),
                                   int clicks = 1, float downX = -999.0f, float downY = -999.0f)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> down (downX < -100.0f ? x : downX, downY < -100.0f ? y : downY);
    return juce::MouseEvent (src, { x, y }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &comp, &comp, juce::Time::getCurrentTime(), down,
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

static juce::File writeNamedWav (const juce::String& name, float seconds = 0.5f,
                                 float freq = 220.0f, float amp = 0.5f)
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
        buf.setSample (0, i, amp * std::sin (2.0f * 3.14159265f * freq * (float) i / 44100.0f));
    w->writeFromAudioSampleBuffer (buf, 0, n);
    w.reset();
    out.reset();
    return f;
}

class Round6Test : public juce::UnitTest
{
public:
    Round6Test() : juce::UnitTest ("Round6", "boombap") {}

    void runTest() override
    {
        const int sr = 44100;
        using E = BoomBapEngine;

        // ============ item 115: PAUSE semantics (engine) ============
        beginTest ("item 115: pause freezes the playhead, resume continues, stop clears");
        {
            E e;
            e.prepare (sr);
            e.clearPattern();
            e.setStep (E::Kick, 0, 127);
            e.setPlaying (true);
            render (e, 4096);
            const long long pos1 = e.transportPosition();
            const int step1 = e.currentStep();
            expect (pos1 > 0, "the transport advanced before the pause");
            e.setPaused (true);
            expect (e.isPaused() && e.isPlaying(), "paused is engaged-but-frozen, not stopped");
            render (e, 16384);
            expectEquals (e.transportPosition(), pos1, "the playhead does not move while paused");
            expectEquals (e.currentStep(), step1, "the step holds while paused");
            e.setPaused (false);
            render (e, 4096);
            expect (e.transportPosition() > pos1, "resume advances from the frozen position");
            e.setPaused (true);
            e.stopAllSound();
            expect (! e.isPaused() && ! e.isPlaying(), "STOP clears the pause (and everything)");
            render (e, 512); // the stop edge parks the transport on the next block
            expectEquals (e.transportPosition(), 0LL, "STOP still parks at zero (item 93)");
            e.setPaused (true);
            expect (! e.isPaused(), "a pause requested while stopped is dropped");
        }

        // ============ item 113: flat defaults (engine) ==============
        beginTest ("item 113: every channel gain starts FLAT (unity)");
        {
            E e;
            e.prepare (sr);
            bool allFlat = true;
            for (int ch = 0; ch <= E::kMasterChannel; ++ch)
                allFlat = allFlat && std::abs (e.getChannelGain (ch) - 1.0f) < 0.001f;
            expect (allFlat, "all channels - rows, audio, synth, piano, pad bus, vocal, master - read 1.0 fresh");
            const auto s = e.getSettings();
            expect (std::abs (s.synthLevel - 1.0f) < 0.001f && std::abs (s.pianoLevel - 1.0f) < 0.001f
                    && std::abs (s.vocalGain - 1.0f) < 0.001f && std::abs (s.audioGain[0] - 1.0f) < 0.001f
                    && std::abs (s.trackGain[3] - 1.0f) < 0.001f,
                    "the settings defaults agree (no 0.8 / 0.85 / 0.9 stragglers)");
        }

        // ============ item 113: fader double-click (component) ======
        beginTest ("item 113: double-click returns a console fader to flat");
        {
            ConsoleFader f;
            f.setSize (90, 220);
            f.setRange (0.0, 1.2, 1.0);
            f.setValue (0.4, false);
            expect (std::abs (f.getValue() - 0.4) < 0.001, "the fader starts where it was put");
            f.mouseDown (makeEvent (f, 45.0f, 100.0f, juce::ModifierKeys(), 2));
            expect (std::abs (f.getValue() - 1.0) < 0.001, "double-click lands exactly on unity");
        }

        // ============ item 109: the decoupled stores (engine) =======
        beginTest ("item 109: rows and pads are separate stores (measured renders)");
        {
            const std::vector<float> tone (8000, 0.5f);
            E e;
            e.prepare (sr);
            e.clearPattern();
            e.loadSample (E::Sample1, tone, sr); // the ROW's load
            expect (e.rowHasSample (0), "the row holds its sample");
            expect (! e.padHasSample (0), "the pad was NOT loaded by the row's load");
            e.setStep (E::Sample1, 0, 127);
            e.setPlaying (true);
            const auto rowRender = render (e, 8192);
            expect (peakIn (rowRender, 0, 8192) > 0.1f, "the row's step sounds the row's sample");
            e.setPlaying (false);

            E e2;
            e2.prepare (sr);
            e2.clearPattern();
            e2.loadPadSample (0, tone, sr); // the PAD's load
            expect (e2.padHasSample (0) && ! e2.rowHasSample (0),
                    "the pad's load leaves the row empty");
            e2.setStep (E::Sample1, 0, 127);
            e2.setPlaying (true);
            const auto padOnlyRender = render (e2, 8192);
            expect (peakIn (padOnlyRender, 0, 8192) < 0.01f,
                    "a step on the row stays silent when only the pad holds a sound");
            e2.setPlaying (false);
            e2.triggerPad (0, 1.0f);
            const auto padRender = render (e2, 8192);
            expect (peakIn (padRender, 0, 8192) > 0.1f, "the pad itself sounds on the pad bus");
            e2.clearSample (E::Sample1); // row clear
            expect (e2.padHasSample (0), "clearing the ROW leaves the pad's sound alone");
            e2.clearPad (0);
            expect (! e2.padHasSample (0), "the pad clears through its own door");
        }

        // ============ item 109: the file format carries both ========
        beginTest ("item 109: project files carry row files and pad files separately");
        {
            const auto kick = writeNamedWav ("Round6Row.wav", 0.5f, 220.0f);
            const auto snare = writeNamedWav ("Round6Pad.wav", 0.25f, 330.0f);
            ProjectData d;
            d.hasRowPadPaths = true;
            d.rowPadPaths[0] = kick.getFullPathName();
            d.padPaths[0] = snare.getFullPathName();
            ProjectData back;
            juce::String err;
            expect (projectFromJson (projectToJson (d, {}), {}, back, err), "the split-store file parses");
            expect (back.hasRowPadPaths, "the rowPads key marks a split-store file");
            expectEquals (back.rowPadPaths[0], kick.getFullPathName(), "the row's file survives");
            expectEquals (back.padPaths[0], snare.getFullPathName(), "the pad's file survives (pads 1-3 carry paths now)");
            ProjectData legacy;
            expect (projectFromJson ("{\"format\":\"boombap-project\",\"version\":1}", {}, legacy, err),
                    "a legacy file parses");
            expect (! legacy.hasRowPadPaths, "a file with no rowPads key reads as legacy (rows fall back to pad files)");
        }

        // ============ items 112/116: the meter laws (components) ====
        beginTest ("items 112/116: clip latches latch and clear; the B1 face reads and holds");
        {
            LevelMeter lm;
            lm.setSize (100, 10);
            lm.setLevel (1.0f);
            expect (lm.clipIsLatched(), "112a: a full-scale peak latches the row meter");
            lm.mouseDown (makeEvent (lm, 5.0f, 5.0f));
            expect (! lm.clipIsLatched(), "112a: a click clears it");

            GrMeter gr;
            gr.setSize (100, 12);
            gr.setReduction (15.0f);
            expect (gr.clipIsLatched(), "112a: full-scale reduction latches the GR meter");
            gr.mouseDown (makeEvent (gr, 5.0f, 5.0f));
            expect (! gr.clipIsLatched(), "112a: its click clears too");

            SegmentedLevelMeter bank;
            bank.setSize (16, 300);
            bank.setLevel (1.0f);
            expect (bank.clipIsLatched(), "112a/b: the pad bank meter latches at full scale");
            bank.clearClipLatch();
            expect (! bank.clipIsLatched(), "112b: and clears");

            ConsoleFader f;
            f.setSize (90, 220);
            f.setRange (0.0, 1.2, 1.0);
            f.setClipLatchEnabled (true);
            f.setValue (0.7, false);
            f.setMeterLevels (1.0f, 0.0f);
            expect (f.clipLatched(), "112a: the fader's clip box latches");
            expect (f.meterLevelL() > 0.9f && f.meterLevelR() < 0.05f,
                    "112e: L and R are held separately (no max-twice)");
            f.setMeterLevels (0.0f, 0.8f);
            f.setMeterLevels (0.0f, 0.8f); // a second feed lets the left decay clear
            expect (f.meterLevelR() > 0.7f && f.meterLevelL() < f.meterLevelR(),
                    "112e: the right side moves on its own");
            const auto box = f.clipBoxBounds();
            f.mouseDown (makeEvent (f, box.getCentreX(), box.getCentreY()));
            expect (! f.clipLatched(), "112a: clicking the box clears the latch");
            expect (std::abs (f.getValue() - 0.7) < 0.001,
                    "112a: the clearing click never moves the fader");

            VuFaceB1 vu;
            vu.setSize (160, 190);
            vu.setLevel (0.5f);
            expect (vu.currentText() == "-6.0", "116: the CURRENT box reads the level in dB");
            expect (! vu.clipLatched(), "116: no latch under 0 VU");
            vu.setLevel (1.2f);
            expect (vu.clipLatched(), "116: past 0 VU the peak latch takes");
            expect (vu.peakValueDb() > 0.0f, "116: the peak holds above zero");
            const float heldPeak = vu.peakValueDb();
            for (int i = 0; i < 20; ++i) vu.setLevel (0.0f);
            expect (vu.peakValueDb() == heldPeak, "116: the peak box keeps the peak (D's reason for B1)");
            expect (vu.currentValueDb() < -20.0f, "116: the needle falls with VU ballistics");
            vu.clearLatch();
            expect (! vu.clipLatched() && vu.peakValueDb() == vu.currentValueDb(),
                    "116: clearing drops the peak to the current reading");
        }

        // ============ item 112d: the compressor line =================
        beginTest ("item 112d: the EQ display's compressor line tracks the reduction");
        {
            E e;
            e.prepare (sr);
            EqDisplay eq (e);
            eq.setSize (500, 220);
            eq.setCompOverlay (true, 3.0f);
            const float y3 = eq.compLineY();
            expect (y3 > 0.0f, "the line shows when the compressor works");
            eq.setCompOverlay (true, 9.0f);
            expect (eq.compLineY() > y3, "deeper reduction draws the line lower (more cut)");
            eq.setCompOverlay (false, 9.0f);
            expect (eq.compLineY() < 0.0f, "compressor off, line gone");
        }

        // ============ item 110: the drag guard (component) ==========
        beginTest ("item 110: a wobble is not a drag any more (16 px threshold)");
        {
            PadButton pad;
            pad.setSize (90, 70);
            pad.setPad (0, true, "Kick");
            int begins = 0;
            pad.onDragBegin = [&] (int, juce::Point<int>) { ++begins; };
            pad.mouseDown (makeEvent (pad, 10.0f, 10.0f));
            pad.mouseDrag (makeEvent (pad, 20.0f, 10.0f, juce::ModifierKeys(), 1, 10.0f, 10.0f));
            expect (! pad.dragHasStarted() && begins == 0,
                    "a 10 px wobble does not begin a copy drag");
            pad.mouseDrag (makeEvent (pad, 30.0f, 10.0f, juce::ModifierKeys(), 1, 10.0f, 10.0f));
            expect (pad.dragHasStarted() && begins == 1,
                    "a deliberate 20 px travel begins the drag");
            pad.mouseUp (makeEvent (pad, 30.0f, 10.0f));

            int presses = 0, menus = 0;
            pad.onPress = [&] (int) { ++presses; };
            pad.onContextMenu = [&] (int) { ++menus; };
            pad.mouseDown (makeEvent (pad, 10.0f, 10.0f,
                                      juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
            expect (menus == 1 && presses == 0, "a right-click asks for the menu instead of playing the pad");
        }

        // ============ item 111: the pad bank layout ==================
        beginTest ("item 111: docked pads bigger; the editor serves waveform and envelope; one caption");
        {
            E e;
            e.prepare (sr);
            PadBankComponent bank (e, [] (int) {}, [] (int) { return juce::String(); });
            bank.setDockedMode (true);
            bank.setSize (1528, 336);
            const auto dockedPad = bank.padBounds (0);
            expect (dockedPad.getHeight() >= 60,
                    "111a: the docked pads stand tall on a 17-inch screen (" + juce::String (dockedPad.getHeight()) + " px)");
            expect (dockedPad.getWidth() * 100 < dockedPad.getHeight() * 125,
                    "111a: the docked pads are squarer (more MPC) than the pop-out shape");
            expect (bank.bankMeterComponent().getHeight() > 240,
                    "112b: the segmented meter runs the bank's whole right side");

            PadBankComponent pop (e, [] (int) {}, [] (int) { return juce::String(); });
            pop.setSize (1000, 620);
            expect (pop.waveComponent().getHeight() >= 100,
                    "111b: the waveform earns real space (" + juce::String (pop.waveComponent().getHeight()) + " px)");
            expect (pop.envelopeComponent().getHeight() >= 100,
                    "111b: the envelope earns real space (" + juce::String (pop.envelopeComponent().getHeight()) + " px)");
            std::vector<juce::Label*> labels;
            collectChildren (pop, labels);
            int padCaptions = 0;
            for (auto* l : labels)
                if (l->getText().startsWith ("PAD ")) ++padCaptions;
            expect (padCaptions == 1, "111b: the 'PAD 1' caption appears exactly once (the letterbox)");
            std::vector<juce::ToggleButton*> toggles;
            collectChildren (pop, toggles);
            juce::ToggleButton* reverse = nullptr;
            for (auto* t : toggles)
                if (t->getButtonText() == "Reverse") reverse = t;
            expect (reverse != nullptr, "111b: Reverse still exists");
            if (reverse != nullptr)
                expect (reverse->getY() > pop.tuneKnob().getBottom() - 4,
                        "111b: Reverse lives in the footer row below the knobs, not stacked in a corner");
        }

        // ============ item 125: the ghost is earned per track =======
        beginTest ("item 125: the ghost roll belongs to tracks that carry roll content");
        {
            E e;
            e.prepare (sr);
            e.setEditPattern (0);
            e.setMelodySource (1); // Sample1's row owns the lane
            SequencerPanel::Callbacks cb;
            SequencerPanel panel (e, cb);
            panel.setSize (1580, 360);
            panel.setGhostEnabled (true);
            panel.setSelectedRow (E::Sample1);
            expect (! panel.ghostStripShowing(),
                    "125: selected and lane-owning, but the lane is EMPTY - no ghost");
            e.setMelodyStep (7, 28);
            panel.refreshGrid();
            expect (panel.ghostStripShowing(), "125: a note in the lane earns the strip");
            panel.setSelectedRow (E::Sample2);
            expect (! panel.ghostStripShowing(), "125: selecting another track never raises it");
            panel.setSelectedRow (E::Sample1);
            expect (panel.ghostStripShowing(), "125: back on the earning track, it returns");
            e.setMelodyStep (7, 0);
            panel.refreshGrid();
            expect (! panel.ghostStripShowing(), "125: clearing the lane's last note retires the strip");
            expect (! e.trackHasGhostContent (E::Sample1) && e.trackHasGhostLane (E::Sample1),
                    "125: the lane TYPE remains - content is what is earned and lost");

            // The rider: the strip is a DOOR - one click anywhere on
            // it asks for that track's piano roll. (The panel copies
            // its Callbacks at construction, so the door wiring must
            // be in place before the panel exists - the click half of
            // this block therefore builds its own panel on the same
            // engine.)
            int doorTrack = -1;
            SequencerPanel::Callbacks cb2;
            cb2.onGhostRoll = [&doorTrack] (int t) { doorTrack = t; };
            SequencerPanel panel2 (e, cb2);
            panel2.setSize (1580, 360);
            panel2.setGhostEnabled (true);
            panel2.setSelectedRow (E::Sample1);
            e.setMelodyStep (3, 28);
            panel2.refreshGrid();
            expect (panel2.ghostStripShowing(), "125 rider: the strip is up for the click");
            auto& strip = panel2.ghostStripForTest();
            strip.mouseDown (makeEvent (strip, 12.0f, 10.0f));
            expectEquals (doorTrack, (int) E::Sample1,
                          "125 rider: one click on the strip summons that track's roll");
        }

        // ============ pad bank meter fed by the pad bus ==============
        beginTest ("item 112b: the bank meter hears the detached pad bus");
        {
            E e;
            e.prepare (sr);
            const std::vector<float> square (8000, 1.0f);
            e.loadPadSample (0, square, sr);
            PadBankComponent bank (e, [] (int) {}, [] (int) { return juce::String(); });
            bank.setSize (1000, 620);
            e.triggerPad (0, 1.0f);
            render (e, 4096);
            pump (150);
            expect (bank.bankMeterComponent().level() > 0.3f,
                    "the meter rides the pad bus (rows feeding it would prove nothing)");
        }

        // ============ MainComponent: crash, transport, drags, undo ===
        beginTest ("items 108/114/115/117/118: the real main window");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (sr, 512);
            auto ed = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
            auto* mc = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
            expect (mc != nullptr, "the editor's content is the main window");
            if (mc == nullptr) { ed.reset(); proc.releaseResources(); return; }
            ed->addToDesktop (0);
            ed->setVisible (true);
            ed->setSize (1500, 950);
            pump (400); // let the initial load / stray gestures settle
            auto& engine = proc.engine;

            const auto kickFile = writeNamedWav ("Round6Kick.wav", 0.5f, 220.0f);
            const auto snareFile = writeNamedWav ("Round6Snare.wav", 0.25f, 330.0f);

            // ---- item 108: the preset save path, hero AND board ----
            mc->openStation();
            pump (100);
            auto* st = mc->stationPanel();
            expect (st != nullptr, "the Station is open");
            auto saveThroughDialog = [&] (const juce::String& name, bool cancel)
            {
                st->presetSaveButtonForTest().triggerClick();
                pump (120);
                auto* modal = juce::ModalComponentManager::getInstance()->getModalComponent (0);
                auto* aw = dynamic_cast<juce::AlertWindow*> (modal);
                expect (aw != nullptr, "108: the save dialog is up (mode: " + juce::String (cancel ? "cancel" : "save") + ")");
                if (aw == nullptr) return;
                if (auto* te = aw->getTextEditor ("name")) te->setText (name);
                aw->exitModalState (cancel ? 0 : 1);
                pump (120);
            };
            const auto presetA = boombap::presetsDir().getChildFile ("Round6 Hero Save.json");
            presetA.deleteFile();
            saveThroughDialog ("Round6 Hero Save", false);
            expect (presetA.existsAsFile(), "108: the hero-mode save lands its file (pre-fix this crashed the app)");
            st->setBoardMode (true);
            pump (60);
            const auto presetB = boombap::presetsDir().getChildFile ("Round6 Board Save.json");
            presetB.deleteFile();
            saveThroughDialog ("Round6 Board Save", false);
            expect (presetB.existsAsFile(), "108: the FULL BOARD save lands its file (D reproduced the crash here)");
            saveThroughDialog ("Round6 Never Saved", true);
            expect (mc->stationPanel() != nullptr, "108: cancelling the dialog leaves the app standing");
            st->setBoardMode (false);
            pump (60);

            // ---- item 116: the B1 face is the rail's VU now ---------
            expect (st->vuFaceComponent().isVisible(),
                    "116: the Station rail wears the B1 VU face");

            // ---- item 117: RESET + the centred buttons --------------
            {
                mc->selectChannel (0); // bind the Station to the channel under test
                pump (40);
                auto strip = engine.getStripSettings (0);
                strip.eq[0].gainDb = 5.0f;
                strip.eq[2].gainDb = -4.0f;
                engine.setStripSettings (0, strip);
                st->refreshFromEngine();
                pump (40);
                const int depthBefore = mc->undoDepth();
                st->bandResetButtonForTest().triggerClick();
                pump (40);
                const auto flat = engine.getStripSettings (0);
                expect (std::abs (flat.eq[0].gainDb) < 0.001f && std::abs (flat.eq[2].gainDb) < 0.001f,
                        "117: RESET puts every band back on the flat line");
                expect (flat.eq[0].freqHz == strip.eq[0].freqHz && flat.eq[0].type == strip.eq[0].type,
                        "117: RESET touches gains only - bands, types, frequencies stand");
                expectEquals (mc->undoDepth(), depthBefore + 1, "117: the RESET is ONE undoable gesture");
                mc->undoProject();
                pump (40);
                expect (std::abs (engine.getStripSettings (0).eq[0].gainDb - 5.0f) < 0.001f,
                        "117: one undo brings the wild EQ back");

                std::vector<juce::TextButton*> btns;
                collectChildren (*st, btns);
                juce::Rectangle<int> group;
                bool haveGroup = false;
                for (auto* b : btns)
                {
                    const auto t = b->getButtonText();
                    if ((t == "ON" || t == "+ BAND" || t == "DELETE" || t == "RESET") && b->isVisible())
                    {
                        group = haveGroup ? group.getUnion (b->getBoundsInParent()) : b->getBoundsInParent();
                        haveGroup = true;
                    }
                }
                expect (haveGroup, "117: the EQ action buttons exist");
                if (haveGroup)
                {
                    const auto eqB = st->eqDisplayComponent().getBoundsInParent();
                    expect (std::abs (group.getCentreX() - eqB.getCentreX()) <= 30,
                            "117: the button group sits centred under the EQ display");
                }
            }

            // ---- items 115: glyph order, both transports ------------
            {
                const TransportButton::Glyph want[7] = {
                    TransportButton::Glyph::ToStart, TransportButton::Glyph::Rewind,
                    TransportButton::Glyph::Pause, TransportButton::Glyph::Play,
                    TransportButton::Glyph::Stop, TransportButton::Glyph::Forward,
                    TransportButton::Glyph::ToEnd };
                bool mainOrder = true, stationOrder = true;
                for (int i = 0; i < 7; ++i)
                {
                    auto* mb = mc->transportButtonForTest (i);
                    mainOrder = mainOrder && mb != nullptr && mb->getGlyph() == want[i];
                    auto* sb = st->stationTransportButtonForTest (i);
                    stationOrder = stationOrder && sb != nullptr && sb->getGlyph() == want[i];
                }
                expect (mainOrder, "115: the main row reads TO-START REW PAUSE PLAY STOP FF TO-END");
                expect (stationOrder, "115: the Station wears the same family in the same order");
                // the Station's PAUSE key drives the real pause
                engine.setPlaying (true);
                pump (30);
                st->stationTransportButtonForTest (2)->mouseDown (makeEvent (*st->stationTransportButtonForTest (2), 5.0f, 5.0f));
                expect (engine.isPaused(), "115: the Station's PAUSE key pauses the engine");
                st->stationTransportButtonForTest (2)->mouseUp (makeEvent (*st->stationTransportButtonForTest (2), 5.0f, 5.0f));
                engine.stopAllSound();
                pump (30);
            }

            // ---- item 114: waveform drags to row and multitrack -----
            {
                const bool pad0Before = mc->padHasSampleForTest (0);
                const bool pad1Before = mc->padHasSampleForTest (1);
                mc->driveLoadWaveformSample (0, kickFile);
                pump (80);
                expect (mc->rowHasSampleForTest (0), "109: the waveform load fills the row");
                expect (mc->padHasSampleForTest (0) == pad0Before,
                        "109: and the load does not touch pad 1 (whatever the project started with)");
                auto* seq = mc->sequencerPanelComponent();
                std::vector<StepButton*> allSteps;
                collectChildren (*seq, allSteps);
                std::vector<StepButton*> steps;
                for (auto* s : allSteps)
                    if (s->isVisible()) steps.push_back (s); // rows past the track count exist but are hidden
                std::sort (steps.begin(), steps.end(), [] (StepButton* a, StepButton* b)
                {
                    const auto ra = a->getBoundsInParent(), rb = b->getBoundsInParent();
                    return ra.getY() != rb.getY() ? ra.getY() < rb.getY() : ra.getX() < rb.getX();
                });
                expect ((int) steps.size() == 32 * engine.getNumTracks(), "the step grid is collected");
                auto* wave = mc->waveViewComponent();
                if ((int) steps.size() == 32 * engine.getNumTracks() && wave != nullptr && wave->onDragEnd)
                {
                    const auto cellCentre = steps[(size_t) (6 * 32)]->getScreenBounds().getCentre().toFloat();
                    wave->onDragEnd (cellCentre.toInt());
                    pump (60);
                    expect (mc->rowHasSampleForTest (1), "114: the waveform drag loads the sequencer row it lands on");
                    expect (mc->padHasSampleForTest (1) == pad1Before, "114: the row drag still leaves the pads alone");
                }
                mc->driveSelectTab (2);
                pump (60);
                auto* mt = mc->multitrackComponent();
                int dropTrack = -1;
                long long dropStart = -1;
                juce::Point<int> dropPoint;
                bool foundLane = false;
                if (mt != nullptr)
                    for (int y = 40; y < mt->getHeight() && ! foundLane; y += 8)
                    {
                        const auto sp = mt->localPointToGlobal (juce::Point<int> (120, y));
                        if (mt->dropPoint (sp, dropTrack, dropStart)) { dropPoint = sp; foundLane = true; }
                    }
                expect (foundLane, "114: a multitrack lane accepts the drop point");
                if (foundLane && wave != nullptr && wave->onDragEnd)
                {
                    wave->onDragEnd (dropPoint);
                    pump (60);
                    int activeClips = 0, clipTrack = -1;
                    for (int i = 0; i < 16; ++i)
                    {
                        const auto c = engine.getAudioClip (i);
                        if (c.active) { ++activeClips; clipTrack = c.track; }
                    }
                    expect (activeClips == 1 && clipTrack == dropTrack,
                            "114: the drag lands exactly one clip on the dropped lane");
                }
                mc->driveSelectTab (0);
                pump (40);
            }

            // ---- item 124: the name tap auditions; nothing else does -
            {
                auto* seq = mc->sequencerPanelComponent();
                const auto tapFile = writeNamedWav ("Round6Tap.wav", 0.1f, 220.0f);
                mc->driveLoadWaveformSample (0, tapFile); // Sample1 holds a short tone
                pump (60);
                engine.clearPattern();
                auto silenceThenRender = [&] (int n)
                {
                    engine.stopAllSound();
                    render (engine, 1024); // the stop lands; voices die
                    return render (engine, n);
                };
                // (a) a programmatic selection change is silent
                mc->selectChannel (5);
                expect (peakIn (silenceThenRender (8192), 0, 8192) < 0.01f,
                        "124: a programmatic selection change does NOT audition");
                // (b) the name tap sounds the row's sample
                auto& nameLabel = seq->rowNameComponent (5);
                seq->mouseDown (makeEvent (nameLabel, 5.0f, 5.0f));
                expect (peakIn (render (engine, 8192), 0, 8192) > 0.1f,
                        "124: tapping the row's name auditions its sound");
                // (c) an empty row's tap stays silent
                auto& emptyName = seq->rowNameComponent (9);
                engine.stopAllSound();
                render (engine, 1024);
                seq->mouseDown (makeEvent (emptyName, 5.0f, 5.0f));
                expect (peakIn (render (engine, 8192), 0, 8192) < 0.01f,
                        "124: an empty row has nothing to audition");
                // (d) a kit row's tap sounds its built-in voice
                auto& kickName = seq->rowNameComponent (0);
                engine.stopAllSound();
                render (engine, 1024);
                seq->mouseDown (makeEvent (kickName, 5.0f, 5.0f));
                expect (peakIn (render (engine, 8192), 0, 8192) > 0.05f,
                        "124: a kit row auditions its built-in sound");
                // (e) the right-click selects (the menu's gesture) but
                // does not audition
                engine.stopAllSound();
                render (engine, 1024);
                seq->mouseDown (makeEvent (nameLabel, 5.0f, 5.0f,
                                           juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
                expect (peakIn (render (engine, 8192), 0, 8192) < 0.01f,
                        "124: the right-click (menu gesture) does not audition");
                juce::PopupMenu::dismissAllActiveMenus();
                pump (30);
            }

            // ---- item 110: CLEAR PAD at app level -------------------
            {
                mc->driveLoadPadFile (2, snareFile);
                pump (60);
                expect (mc->padHasSampleForTest (2), "110 setup: the pad is loaded");
                mc->driveClearPad (2);
                pump (60);
                expect (! mc->padHasSampleForTest (2), "110: CLEAR PAD empties the pad");
                mc->undoProject();
                pump (60);
                expect (mc->padHasSampleForTest (2), "110: the clear is undoable");
            }

            // ---- item 118: the mixed-action undo chain --------------
            {
                // a flusher action first: it truncates any redo tail
                // left by item 117's undo, so the depth math below is
                // exact (each chain action adds exactly one entry)
                mc->driveLoadWaveformSample (3, snareFile);
                pump (60);
                const int depth0 = mc->undoDepth();
                // (a) load (a fresh row + a different file, so the
                // snapshot really changes)
                mc->driveLoadWaveformSample (2, snareFile);
                pump (60);
                // (b) step edit through the grid's real buttons (the
                // pattern is cleared directly first - the demo pattern
                // may already hold this step)
                engine.clearPattern();
                auto* seq = mc->sequencerPanelComponent();
                seq->refreshGrid();
                pump (30);
                std::vector<StepButton*> allSteps2;
                collectChildren (*seq, allSteps2);
                std::vector<StepButton*> steps;
                for (auto* s : allSteps2)
                    if (s->isVisible()) steps.push_back (s);
                std::sort (steps.begin(), steps.end(), [] (StepButton* a, StepButton* b)
                {
                    const auto ra = a->getBoundsInParent(), rb = b->getBoundsInParent();
                    return ra.getY() != rb.getY() ? ra.getY() < rb.getY() : ra.getX() < rb.getX();
                });
                if ((int) steps.size() == 32 * engine.getNumTracks())
                {
                    auto* cell = steps[3];
                    cell->mouseDown (makeEvent (*cell, 5.0f, 5.0f));
                    cell->mouseUp (makeEvent (*cell, 5.0f, 5.0f));
                }
                pump (60);
                expect (engine.getStep (0, 3) == 90, "118 setup: the step edit landed");
                // (c) rename
                seq->showRenameEditor (7);
                if (auto* te = seq->renameEditorForTest()) te->setText ("Chain Row");
                seq->commitRenameForTest();
                pump (60);
                expectEquals (juce::String (engine.displayTrackName (7)), juce::String ("Chain Row"), "118 setup: the rename landed");
                // (d) pad load + pad copy
                mc->driveLoadPadFile (4, snareFile);
                pump (60);
                mc->driveCopyPadToPad (4, 9);
                pump (60);
                expect (mc->padHasSampleForTest (9), "118 setup: the pad copy landed");
                // (e) a board fader move
                st->setBoardMode (true);
                pump (60);
                auto* fader = st->boardComponent().stripFader (4);
                const auto cap = fader->capCentre();
                fader->mouseDown (makeEvent (*fader, cap.x, cap.y));
                fader->mouseDrag (makeEvent (*fader, cap.x, cap.y - 45.0f));
                fader->mouseUp (makeEvent (*fader, cap.x, cap.y - 45.0f));
                pump (60);
                expect (engine.getChannelGain (4) > 1.05f, "118 setup: the fader move landed");
                expectEquals (mc->undoDepth(), depth0 + 6, "118: six actions, six history entries");
                pump (200);
                expect (mc->undoButtonForTest().isEnabled(), "118: the UNDO face lights up with history behind it");

                auto pressUndo = [&] { mc->undoButtonForTest().triggerClick(); pump (60); };
                pressUndo();
                expect (std::abs (engine.getChannelGain (4) - 1.0f) < 0.001f,
                        "118: undo 1 - the fader comes back flat");
                expect (mc->padHasSampleForTest (9), "118: undo 1 leaves the earlier actions standing");
                pressUndo();
                expect (! mc->padHasSampleForTest (9) && mc->padHasSampleForTest (4),
                        "118: undo 2 - the pad copy goes, the pad load stands");
                pressUndo();
                expect (! mc->padHasSampleForTest (4), "118: undo 3 - the pad load goes");
                pressUndo();
                expectEquals (juce::String (engine.displayTrackName (7)), juce::String ("Round6Snare"),
                              "118: undo 4 - the rename reverts to the file-derived name (action (a)'s snare, still standing)");
                pressUndo();
                expect (engine.getStep (0, 3) == 0, "118: undo 5 - the step clears");
                pressUndo();
                expect (! mc->rowHasSampleForTest (2), "118: undo 6 - the load goes; the chain's start state is restored");
                expect (mc->rowHasSampleForTest (3),
                        "118: the pre-chain action still stands (the chain unwound exactly its own six)");
            }

            ed->removeFromDesktop();
            ed.reset();
            proc.releaseResources();
        }

        // ============ item 118: the face + the focus clause ==========
        beginTest ("item 118: UNDO starts disabled, lights with history, and fires from any focus");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (sr, 512);
            auto ed = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
            auto* mc = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
            expect (mc != nullptr, "the editor's content is the main window");
            if (mc == nullptr) { ed.reset(); proc.releaseResources(); return; }
            ed->addToDesktop (0);
            ed->setVisible (true);
            ed->setSize (1500, 950);
            pump (400);
            expect (! mc->undoButtonForTest().isEnabled() && ! mc->redoButtonForTest().isEnabled(),
                    "118: on a clean history both faces start disabled");
            const auto kickFile = writeNamedWav ("Round6Kick.wav", 0.5f, 220.0f);
            mc->driveLoadWaveformSample (0, kickFile);
            pump (200);
            expect (mc->undoButtonForTest().isEnabled(), "118: one action lights the UNDO face");

            mc->driveSelectTab (3);
            pump (100);
            auto* panel = mc->keyboardPanelComponent();
            panel->grabKeyboardFocus();
            pump (40);
            const juce::KeyPress ctrlZ ((int) 'z', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier),
                                        (juce::juce_wchar) 'z');
            expect (! panel->keyPressed (ctrlZ),
                    "118: with the KEYBOARD tab focused, Ctrl+Z is not swallowed as a note");
            expect (mc->keyPressed (ctrlZ), "118: the main window's shortcut takes the key");
            pump (60);
            expect (! mc->rowHasSampleForTest (0), "118: and the undo actually fired");
            expect (mc->redoButtonForTest().isEnabled(), "118: REDO lights after an undo");
            const juce::KeyPress plainZ ((int) 'z', juce::ModifierKeys(), (juce::juce_wchar) 'z');
            expect (panel->keyPressed (plainZ), "118: a plain z still plays its note (Round 5 untouched)");
            panel->releaseAllNotes();

            ed->removeFromDesktop();
            ed.reset();
            proc.releaseResources();
        }
    }
};

static Round6Test test;

#if JUCE_LINUX
 #include <X11/Xlib.h>
#endif

int main()
{
   #if JUCE_LINUX
    // The window-manager stand-in (the DockBackTest / WindowFramesTest
    // pattern): this suite creates real top-level windows. JUCE's Linux
    // peer looks several WM atoms up with onlyIfExists=True; under a
    // bare Xvfb with no window manager they do not exist, the lookup
    // returns None (0), and the first XChangeProperty dies with
    // BadAtom. Interning them server-side first (atoms are global to
    // the X server) stands in for the WM every real desktop has.
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
    std::printf ("Round6Test: %d checks, %d failures\n", total, fails);
    return fails == 0 ? 0 : 1;
}
