// Round 5 test (Breakdown #1 items 105 + 107): the KEYBOARD tab and
// track rename.
//
// Item 105 is proven at three levels: the engine's live-note FUNNEL
// (every note source's single door) with measured renders per track
// kind; the KeyboardPanel itself (geometry, the two-row QWERTY map,
// the funnel convergence of mouse and QWERTY, focus rules); and the
// real MainComponent (the tab joins the production family, step-write
// lands in the playing pattern with ONE undo gesture per pass).
// Item 107 is proven at the engine (the override wins over Round 3's
// derived names and persists) and through the real inline editor.
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/KeyboardPanel.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/ProjectFile.h"
#include <cmath>
#include <map>

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

static float rmsIn (const std::vector<float>& x, int from, int to)
{
    from = juce::jmax (0, from); to = juce::jmin ((int) x.size(), to);
    if (to <= from) return 0.0f;
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return (float) std::sqrt (s / (double) (to - from));
}

// Zero-crossing rate -> estimated frequency of a steady tone.
static double zcrFreq (const std::vector<float>& x, int from, int to, double sr)
{
    from = juce::jmax (0, from); to = juce::jmin ((int) x.size(), to);
    if (to - from < 16) return 0.0;
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if ((x[(size_t) (i - 1)] < 0.0f) != (x[(size_t) i] < 0.0f)) ++crossings;
    return (double) crossings * sr / (2.0 * (double) (to - from));
}

static std::vector<float> sineBurst (double freq, double seconds, double sr)
{
    std::vector<float> d ((size_t) (seconds * sr));
    for (size_t i = 0; i < d.size(); ++i)
        d[i] = 0.7f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / sr);
    return d;
}

class Round5Test : public juce::UnitTest
{
public:
    Round5Test() : juce::UnitTest ("Round5", "boombap") {}

    void runTest() override
    {
        const int sr = 44100;

        // ============ the funnel: pad rows pitch their sample =======
        beginTest ("item 105b: a pad row plays its sample pitched around C4 (measured)");
        {
            BoomBapEngine e;
            e.prepare (sr);
            e.loadSample (BoomBapEngine::Sample1, sineBurst (220.0, 0.6, sr), sr);
            e.liveNoteOn (BoomBapEngine::Sample1, 60, 0.9f);
            const auto root = render (e, sr / 2);
            expect (peakIn (root, 1000, 20000) > 0.1f, "the root note sounds the sample");
            const double fRoot = zcrFreq (root, 3000, 11000, sr);
            expect (fRoot > 190.0 && fRoot < 250.0, "root renders at the sample's pitch");

            BoomBapEngine e2;
            e2.prepare (sr);
            e2.loadSample (BoomBapEngine::Sample1, sineBurst (220.0, 0.6, sr), sr);
            e2.liveNoteOn (BoomBapEngine::Sample1, 72, 0.9f);
            const auto up = render (e2, sr / 2);
            const double fUp = zcrFreq (up, 3000, 11000, sr);
            expect (fUp > fRoot * 1.8 && fUp < fRoot * 2.2, "+12 semitones doubles the pitch");

            BoomBapEngine e3;
            e3.prepare (sr);
            e3.loadSample (BoomBapEngine::Sample1, sineBurst (220.0, 0.6, sr), sr);
            e3.liveNoteOn (BoomBapEngine::Sample1, 48, 0.9f);
            const auto dn = render (e3, sr / 2);
            const double fDn = zcrFreq (dn, 3000, 11000, sr);
            expect (fDn > fRoot * 0.42 && fDn < fRoot * 0.58, "-12 semitones halves the pitch");
        }

        // ============ the funnel: drum rows =========================
        beginTest ("item 105b: drum rows - kit sample pitches, unbound kick pitches, snare sounds");
        {
            // kit-bound kick: the row's SAMPLE, pitched (classic sampler)
            BoomBapEngine e;
            e.prepare (sr);
            e.setKitSample (BoomBapEngine::Kick, sineBurst (180.0, 0.6, sr), sr);
            e.liveNoteOn (BoomBapEngine::Kick, 60, 0.9f);
            const auto root = render (e, sr / 2);
            const double fRoot = zcrFreq (root, 3000, 11000, sr);
            expect (fRoot > 150.0 && fRoot < 215.0, "kit kick sounds at its sample pitch at C4");
            BoomBapEngine e2;
            e2.prepare (sr);
            e2.setKitSample (BoomBapEngine::Kick, sineBurst (180.0, 0.6, sr), sr);
            e2.liveNoteOn (BoomBapEngine::Kick, 72, 0.9f);
            const auto up = render (e2, sr / 2);
            const double fUp = zcrFreq (up, 3000, 11000, sr);
            expect (fUp > fRoot * 1.75 && fUp < fRoot * 2.25, "kit kick at +12 is an octave up");

            // unbound kick: the synthesized 808, pitch-tracked
            BoomBapEngine k1; k1.prepare (sr);
            k1.liveNoteOn (BoomBapEngine::Kick, 60, 0.9f);
            const auto kr = render (k1, sr / 4);
            expect (peakIn (kr, 0, 8000) > 0.1f, "the unbound kick sounds");
            BoomBapEngine k2; k2.prepare (sr);
            k2.liveNoteOn (BoomBapEngine::Kick, 72, 0.9f);
            const auto ku = render (k2, sr / 4);
            const double fk1 = zcrFreq (kr, 200, 3000, sr);
            const double fk2 = zcrFreq (ku, 200, 3000, sr);
            expect (fk2 > fk1 * 1.5, "the unbound kick tracks pitch upward");

            // unbound snare has no pitched voice: it sounds at its root
            BoomBapEngine s1; s1.prepare (sr);
            s1.liveNoteOn (BoomBapEngine::Snare, 67, 0.9f);
            const auto sn = render (s1, sr / 4);
            expect (peakIn (sn, 0, 8000) > 0.08f, "the snare sounds from the keyboard");
            s1.liveNoteOff (BoomBapEngine::Snare, 67); // a no-op for one-shots: must not crash or cut
        }

        // ============ the funnel: synth + piano note-off ============
        beginTest ("item 105b: synth holds until the off; piano damps on the off");
        {
            BoomBapEngine e;
            e.prepare (sr);
            e.liveNoteOn (BoomBapEngine::kSynthChannel, 60, 0.9f);
            const auto held = render (e, sr); // a full second, gate is long
            expect (rmsIn (held, sr - 6000, sr - 1000) > 0.02f,
                    "the synth is still sounding a second in (held, not gated)");
            e.liveNoteOff (BoomBapEngine::kSynthChannel, 60);
            const auto tail = render (e, sr / 2);
            expect (rmsIn (tail, 16000, 21000) < 0.004f,
                    "after the off + release, the synth is silent");

            BoomBapEngine p;
            p.prepare (sr);
            p.liveNoteOn (BoomBapEngine::kPianoChannel, 60, 0.9f);
            const auto ring = render (p, sr / 4);
            expect (rmsIn (ring, 2000, 8000) > 0.01f, "the piano sounds");
            p.liveNoteOff (BoomBapEngine::kPianoChannel, 60);
            const auto damped = render (p, sr / 2);
            BoomBapEngine p2;
            p2.prepare (sr);
            p2.liveNoteOn (BoomBapEngine::kPianoChannel, 60, 0.9f);
            render (p2, sr / 4);
            const auto undamped = render (p2, sr / 2);
            expect (rmsIn (damped, 12000, 20000) < rmsIn (undamped, 12000, 20000) * 0.25f,
                    "the note-off damps the piano's ring");
        }

        // ============ the funnel: who is NOT a note channel =========
        beginTest ("item 105b: pad bus / audio / vocal / master take no notes; own rows pitch");
        {
            BoomBapEngine e;
            e.prepare (sr);
            for (int ch : { BoomBapEngine::kPadBusChannel, BoomBapEngine::kAudioChannelBase,
                            BoomBapEngine::kVocalChannel, BoomBapEngine::kMasterChannel })
            {
                expect (! BoomBapEngine::channelIsPlayable (ch), "not a note channel");
                e.liveNoteOn (ch, 60, 1.0f);
            }
            const auto out = render (e, sr / 4);
            expect (peakIn (out, 0, sr / 4) < 0.001f, "non-note channels stay silent");
            expect (BoomBapEngine::channelIsPlayable (0), "rows are playable");
            expect (BoomBapEngine::channelIsPlayable (BoomBapEngine::kSynthChannel), "synth is playable");
            expect (BoomBapEngine::channelIsPlayable (BoomBapEngine::kPianoChannel), "piano is playable");

            // an own-sample row (past the pad bank) pitches its sample
            BoomBapEngine o;
            o.prepare (sr);
            while (o.getNumTracks() <= BoomBapEngine::kFirstOwnRow) o.addTrack();
            o.loadSample (BoomBapEngine::kFirstOwnRow, sineBurst (220.0, 0.6, sr), sr);
            o.liveNoteOn (BoomBapEngine::kFirstOwnRow, 72, 0.9f);
            const auto oo = render (o, sr / 2);
            const double f = zcrFreq (oo, 3000, 11000, sr);
            expect (f > 380.0 && f < 500.0, "the own row's sample sounds an octave up");
        }

        // ============ item 45: the global stop ======================
        beginTest ("item 45 regression: the global stop silences a held keyboard note");
        {
            BoomBapEngine e;
            e.prepare (sr);
            e.liveNoteOn (BoomBapEngine::kSynthChannel, 64, 0.9f);
            const auto on = render (e, sr / 4);
            expect (rmsIn (on, 4000, 9000) > 0.02f, "the note is sounding");
            e.stopAllSound();
            const auto off = render (e, sr / 4);
            expect (peakIn (off, 3000, sr / 4) < 0.002f, "stop = all sound off, keyboard notes included");
        }

        // ============ item 107: the rename override =================
        beginTest ("item 107: custom name overrides, clears, persists (settings + JSON)");
        {
            BoomBapEngine e;
            e.prepare (sr);
            e.setTrackContentName (7, "break_loop.wav");
            expectEquals (juce::String (e.displayTrackName (7)), juce::String ("break_loop.wav"),
                          "the derived name is the file name (Round 3)");
            e.setTrackCustomName (7, "John");
            expectEquals (juce::String (e.displayTrackName (7)), juce::String ("John"),
                          "the custom name overrides the file name");
            expectEquals (juce::String (e.channelDisplayName (7)), juce::String ("John"),
                          "every surface reads the override through the one name");
            // a new file binding must NOT wash the owner's name away
            e.setTrackContentName (7, "other.wav");
            expectEquals (juce::String (e.displayTrackName (7)), juce::String ("John"),
                          "rebinding a file keeps the override");
            e.setTrackCustomName (7, "");
            expectEquals (juce::String (e.displayTrackName (7)), juce::String ("other.wav"),
                          "clearing the override returns the derived name");
            expectEquals (juce::String (e.displayTrackName (3)), juce::String ("Open Hat"),
                          "an untouched kit row keeps its kit name");

            // persistence: settings snapshot -> fresh engine
            e.setTrackCustomName (7, "John");
            e.setTrackCustomName (0, "Kicks");
            const auto snap = e.getSettings();
            BoomBapEngine e2;
            e2.prepare (sr);
            e2.applySettings (snap);
            expectEquals (juce::String (e2.displayTrackName (7)), juce::String ("John"),
                          "the override survives a settings round-trip");
            expectEquals (juce::String (e2.displayTrackName (0)), juce::String ("Kicks"),
                          "kit rows rename too");

            // persistence: the project FILE (JSON) itself
            ProjectData d;
            d.settings = snap;
            const auto json = projectToJson (d, juce::File());
            expect (json.contains ("\"name\": \"John\""), "the file carries the override");
            ProjectData back;
            juce::String err;
            expect (projectFromJson (json, juce::File(), back, err), "the file parses back");
            expectEquals (juce::String (back.settings.trackCustomName[7]), juce::String ("John"),
                          "the override survives the file round-trip");
            expect (back.settings.trackCustomName[3].empty(), "unrenamed rows write no name key");
        }

        // ============ step-write at engine level ====================
        beginTest ("item 105d: step-write lands in the playing pattern's lanes");
        {
            BoomBapEngine e;
            e.prepare (sr);
            expect (! e.stepWriteNote (BoomBapEngine::kSynthChannel, 65, 0.9f),
                    "no write while the transport is stopped");
            e.setPlaying (true);
            auto waitForStep = [&] (int target)
            {
                for (int i = 0; i < 400 && e.currentStep() != target; ++i) render (e, 256);
                return e.currentStep() == target;
            };
            expect (waitForStep (2), "the playhead reaches step 2");
            expect (e.stepWriteNote (BoomBapEngine::kSynthChannel, 65, 0.9f), "synth write lands");
            expect (e.stepWriteNote (BoomBapEngine::Kick, 60, 0.9f), "kick root write lands");
            expect (e.stepWriteNote (BoomBapEngine::Snare, 71, 1.0f), "snare write lands");
            expect (waitForStep (6), "the playhead reaches step 6");
            expect (e.stepWriteNote (BoomBapEngine::Kick, 67, 0.9f), "kick pitched write lands");
            expect (e.stepWriteNote (BoomBapEngine::kPianoChannel, 72, 0.9f), "piano write lands");
            // the melody lane is MONOPHONIC (Round 4): a second pitched
            // drum write goes to its own step, not on top of the kick's
            expect (waitForStep (9), "the playhead reaches step 9");
            expect (e.stepWriteNote (BoomBapEngine::Sample1 + 2, 62, 0.9f), "pad pitched write lands");
            e.setPlaying (false);
            const auto s = e.getSettings();
            expectEquals (s.patSynth[0][2], (65 - 48) + 25, "synth lane: offset from C3, +25 encoding");
            expectEquals (s.patData[0][BoomBapEngine::Kick][2], 114, "kick at the root writes the STEP");
            expectEquals (s.patData[0][BoomBapEngine::Snare][2], 127, "the snare's lane is its steps");
            expectEquals (s.patMelody[0][6], (67 - 60) + 25, "kick off-root writes the melody lane");
            expectEquals (s.patMelody[0][9], (62 - 60) + 25, "the pad row's pitched write shares the same lane");
            expectEquals (s.patMelodySource[0], 3, "the last pitched writer (pad row 3) owns the lane source");
            expectEquals (s.patPiano[0][6], (72 - 60) + 25, "piano lane: offset from C4, +25 encoding");
            // the kick's step 2 is untouched by its pitched write at 6
            expectEquals (s.patData[0][BoomBapEngine::Kick][6], 0, "a pitched write never invents a step");
        }

        // ============ the panel: geometry + velocity ================
        beginTest ("item 105a: key geometry, centre C4, velocity from press height");
        {
            BoomBapEngine e;
            e.prepare (sr);
            KeyboardPanel panel (e);
            panel.setSize (1528, 336);
            expectEquals (panel.lowestNote(), 48, "the board starts at C3");
            expectEquals (panel.highestNote(), 84, "three octaves up to C6");
            expectEquals (panel.whiteKeyCount(), 22, "22 white keys over 3 octaves + top C");
            float frac = 0.0f;
            for (int n = 48; n <= 84; ++n)
            {
                const auto r = panel.keyRect (n);
                expect (panel.noteAt (r.getCentre().toInt(), frac) == n,
                        "every key's centre hits its own note");
            }
            expect (panel.noteAt ({ 4, 4 }, frac) == -1, "the header is not a key");
            expect (KeyboardPanel::velocityForY (0.0f) == 0.25f, "top of the key = soft (0.25)");
            expect (KeyboardPanel::velocityForY (1.0f) == 1.0f, "bottom of the key = hard (1.0)");
            expect (KeyboardPanel::velocityForY (0.5f) > 0.6f && KeyboardPanel::velocityForY (0.5f) < 0.65f,
                    "the mapping is linear between them");
            panel.setOctaveShift (1);
            expectEquals (panel.lowestNote(), 60, "octave +1 moves the board a full octave");
            panel.setOctaveShift (-2);
            expectEquals (panel.lowestNote(), 24, "octave -2 is the floor");
            panel.setOctaveShift (0);
        }

        // ============ the QWERTY map ================================
        beginTest ("item 105c/27: the two-row QWERTY map, at rest and shifted");
        {
            // the full table at shift 0, lower row from C3, upper from C4
            const juce::String lower = "zsxdcvgbhnjm,l.;/";
            const int lowerOff[17] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
            for (int i = 0; i < 17; ++i)
                expectEquals (qwertyNoteForChar (lower[i], 0), 48 + lowerOff[i],
                              "Z row maps from C3");
            const juce::String upper = "q2w3er5t6y7ui9o0p";
            for (int i = 0; i < 17; ++i)
                expectEquals (qwertyNoteForChar (upper[i], 0), 60 + lowerOff[i],
                              "Q row maps from C4 (the centre)");
            expectEquals ((int) qwertyCharForNote (60, 0), (int) 'q', "C4 wears the Q");
            expectEquals ((int) qwertyCharForNote (48, 0), (int) 'z', "C3 wears the Z");
            expectEquals (qwertyNoteForChar ('a', 0), -1, "unmapped keys stay silent");
            expectEquals (qwertyNoteForChar ('z', 1), 60, "shift +1: Z row starts at C4");
            expectEquals (qwertyNoteForChar ('q', 1), 72, "shift +1: Q row starts at C5");
            expectEquals (qwertyNoteForChar ('q', -2), 36, "shift -2: Q row starts at C2");
        }

        // ============ funnel convergence (panel level) ==============
        beginTest ("item 105e: mouse and QWERTY converge on the same note door");
        {
            BoomBapEngine e;
            e.prepare (sr);
            KeyboardPanel panel (e);
            panel.setSize (1528, 336);
            int channel = BoomBapEngine::Sample1 + 1;
            panel.getSelectedChannel = [&] { return channel; };
            std::vector<std::pair<int, int>> ons, offs;
            std::vector<float> vels;
            panel.onNoteOn = [&] (int ch, int note, float v) { ons.push_back ({ ch, note }); vels.push_back (v); };
            panel.onNoteOff = [&] (int ch, int note) { offs.push_back ({ ch, note }); };

            // mouse: a press at the centre of key F4 (65), low on the key
            const auto r65 = panel.keyRect (65);
            panel.pressAt ({ (int) r65.getCentreX(), (int) (r65.getBottom() - 4.0f) });
            expectEquals ((int) ons.size(), 1, "the mouse press fired one note-on");
            expect (ons[0].first == channel && ons[0].second == 65, "the mouse played F4 on the selected track");
            expect (vels[0] > 0.9f, "a press low on the key is a hard hit");
            expect (panel.isNoteHeld (65), "the key lights while held");
            panel.releaseMouseForTest();
            expectEquals ((int) offs.size(), 1, "the release fired the note-off");

            // QWERTY: 'r' is the same F4 on the Q row
            ons.clear(); offs.clear(); vels.clear();
            expect (panel.qwertyDownForTest ('r'), "the QWERTY press is consumed");
            expectEquals ((int) ons.size(), 1, "QWERTY fired one note-on");
            expect (ons[0].first == channel && ons[0].second == 65,
                    "QWERTY 'r' and the mouse reach the SAME (channel, note)");
            expect (std::abs (vels[0] - 0.9f) < 0.001f, "QWERTY plays at the lanes' 0.9 velocity");
            panel.qwertyDownForTest ('r'); // auto-repeat
            expectEquals ((int) ons.size(), 1, "auto-repeat never re-triggers");
            panel.qwertyUpForTest ('r');
            expectEquals ((int) offs.size(), 1, "the QWERTY release fired the off");

            // glissando: dragging across keys ends each note as the next begins
            ons.clear(); offs.clear();
            const auto r60 = panel.keyRect (60);
            const auto r62 = panel.keyRect (62);
            panel.pressAt (r60.getCentre().toInt());
            panel.pressAt (r62.getCentre().toInt());
            expectEquals ((int) ons.size(), 2, "the drag sounded both keys");
            expectEquals ((int) offs.size(), 1, "the first key ended as the drag left it");
            panel.releaseMouseForTest();

            // a held note remembers its channel: change the selection
            // mid-hold and the off still lands on the starting channel
            ons.clear(); offs.clear();
            channel = BoomBapEngine::Kick;
            panel.qwertyDownForTest ('q'); // C4 on Kick
            channel = BoomBapEngine::Snare;
            panel.qwertyUpForTest ('q');
            expectEquals ((int) offs.size(), 1);
            expect (offs[0].first == BoomBapEngine::Kick,
                    "the off went to the channel the note STARTED on");
        }

        // ============ focus rules ===================================
        beginTest ("item 105c: focus loss releases every held note");
        {
            BoomBapEngine e;
            e.prepare (sr);
            KeyboardPanel panel (e);
            panel.setSize (1528, 336);
            panel.getSelectedChannel = [] { return BoomBapEngine::kSynthChannel; };
            int offCount = 0;
            panel.onNoteOn = [&] (int, int, float) {};
            panel.onNoteOff = [&] (int, int) { ++offCount; };
            panel.qwertyDownForTest ('q');
            panel.qwertyDownForTest ('e');
            expectEquals (panel.heldNoteCount(), 2, "two notes held");
            panel.focusLost (juce::Component::focusChangedByMouseClick);
            expectEquals (panel.heldNoteCount(), 0, "focus loss released them all");
            expectEquals (offCount, 2, "both offs reached the door (no stuck notes)");
        }

        // ============ app level: the tab joins the family ===========
        beginTest ("item 105a: KEYBOARD is a production tab (item 59 discipline, pop-out)");
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
                bool sawKeyboardButton = false;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (auto* b = dynamic_cast<juce::TextButton*> (child))
                            if (b->getButtonText() == "KEYBOARD") sawKeyboardButton = true;
                        walk (*child);
                    }
                };
                walk (*mc);
                expect (sawKeyboardButton, "the tab bar carries a KEYBOARD button");
                expect (mc->keyboardPanelComponent() != nullptr, "the keyboard panel exists");
                mc->driveSelectTab (3); pump();
                expectEquals (mc->activeTabIndex(), 3);
                expect (mc->tabViewVisible (3), "the keyboard is the one visible view");
                expect (! mc->tabViewVisible (0) && ! mc->tabViewVisible (1) && ! mc->tabViewVisible (2),
                        "exactly one production view is visible (item 59)");
                // pop-out and dock back through the item-66 paths
                mc->drivePopOutTab (3); pump();
                expect (mc->tabIsPoppedOut (3), "the keyboard pops out like its siblings");
                mc->driveClosePopOutWindow (3); pump();
                expect (! mc->tabIsPoppedOut (3), "closing the pop-out docks it back");
                expect (mc->tabViewVisible (3), "and it is visible again, docked");
                mc->driveSelectTab (0); pump();
            }
            ed.reset();
            proc.releaseResources();
        }

        // ============ app level: rename through the real editor =====
        beginTest ("item 107: rename through the sequencer's inline editor");
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
                auto& engine = proc.engine;
                auto* seq = mc->sequencerPanelComponent();
                expect (seq != nullptr);
                if (seq != nullptr)
                {
                    const int row = 7; // a pad row: no menu at all before this round
                    expectEquals (seq->rowNameLabel (row).getText(), juce::String ("TRACK 8"),
                                  "the pad row starts at its derived name");
                    seq->showRenameEditor (row);
                    expect (seq->renameEditorOpen(), "the inline editor opens on the name");
                    auto* editor = seq->renameEditorForTest();
                    expect (editor != nullptr);
                    if (editor != nullptr) editor->setText ("John");
                    seq->commitRenameForTest(); pump();
                    expectEquals (juce::String (engine.displayTrackName (row)), juce::String ("John"),
                                  "the engine carries the override");
                    expectEquals (seq->rowNameLabel (row).getText(), juce::String ("John"),
                                  "the sequencer's first column shows it");
                    expectEquals (juce::String (engine.channelDisplayName (row)), juce::String ("John"),
                                  "the one name every surface reads agrees");

                    // undo / redo carry the rename like any project edit
                    mc->undoProject(); pump();
                    expectEquals (juce::String (engine.displayTrackName (row)), juce::String ("TRACK 8"),
                                  "undo restores the derived name");
                    expectEquals (seq->rowNameLabel (row).getText(), juce::String ("TRACK 8"),
                                  "and the label follows the undo");
                    mc->redoProject(); pump();
                    expectEquals (juce::String (engine.displayTrackName (row)), juce::String ("John"),
                                  "redo brings the name back");

                    // Esc cancels: an edit that never lands
                    seq->showRenameEditor (row);
                    if (auto* ed2 = seq->renameEditorForTest()) ed2->setText ("Nobody");
                    seq->cancelRenameForTest(); pump();
                    expectEquals (juce::String (engine.displayTrackName (row)), juce::String ("John"),
                                  "Esc leaves the name alone");

                    // an empty commit clears the override
                    seq->showRenameEditor (row);
                    if (auto* ed3 = seq->renameEditorForTest()) ed3->setText ("");
                    seq->commitRenameForTest(); pump();
                    expectEquals (juce::String (engine.displayTrackName (row)), juce::String ("TRACK 8"),
                                  "an empty commit clears back to the derived name");
                }
            }
            ed.reset();
            proc.releaseResources();
        }

        // ============ app level: step-write through the real tab ====
        beginTest ("item 105d: step-write through the tab - quantized, one undo per pass");
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
                pump (400);
                auto& engine = proc.engine;
                auto* panel = mc->keyboardPanelComponent();
                expect (panel != nullptr);
                if (panel != nullptr)
                {
                    mc->driveSelectTab (3); pump();
                    mc->selectChannel (BoomBapEngine::kSynthChannel);
                    panel->writeButtonForTest().triggerClick(); pump();
                    expect (panel->isWriteArmed(), "WRITE is armed");

                    engine.setPlaying (true);
                    auto renderUntilStep = [&] (int target)
                    {
                        for (int i = 0; i < 500 && engine.currentStep() != target; ++i)
                            render (engine, 256);
                        return engine.currentStep() == target;
                    };
                    pump (600); // let any stray gesture settle before counting
                    const int depthBefore = mc->undoDepth();
                    expect (renderUntilStep (2), "the playhead reaches step 2");
                    panel->pressKeyForTest (65, 1.0f); // F4 on the synth
                    panel->releaseMouseForTest();
                    expect (renderUntilStep (5), "the playhead reaches step 5");
                    panel->pressKeyForTest (69, 1.0f); // A4
                    panel->releaseMouseForTest();
                    engine.setPlaying (false);
                    pump (700); // the pass ends: stop closes the gesture
                    const auto s = engine.getSettings();
                    expectEquals (s.patSynth[0][2], (65 - 48) + 25, "the first note sits at step 2, quantized");
                    expectEquals (s.patSynth[0][5], (69 - 48) + 25, "the second note sits at step 5");
                    expectEquals (mc->undoDepth(), depthBefore + 1,
                                  "the whole pass is ONE undo step, not one per note");
                    mc->undoProject(); pump();
                    const auto s2 = engine.getSettings();
                    expectEquals (s2.patSynth[0][2], 0, "one undo takes the whole pass back");
                    expectEquals (s2.patSynth[0][5], 0, "both notes, one gesture");
                    mc->redoProject(); pump();

                    // WRITE off = audition only: a played note lands nowhere
                    panel->writeButtonForTest().triggerClick(); pump();
                    expect (! panel->isWriteArmed(), "WRITE disarmed");
                    engine.setPlaying (true);
                    expect (renderUntilStep (9), "the playhead reaches step 9");
                    panel->pressKeyForTest (72, 1.0f);
                    panel->releaseMouseForTest();
                    engine.setPlaying (false);
                    pump (300);
                    const auto s3 = engine.getSettings();
                    expectEquals (s3.patSynth[0][9], 0, "with WRITE off the keyboard only auditions");
                }
            }
            ed.reset();
            proc.releaseResources();
        }

        // ============ app level: focus on a real desktop ============
        beginTest ("item 105c: on a real desktop, focus decides whether QWERTY can play");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            auto* mc = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
            expect (mc != nullptr);
            if (mc != nullptr)
            {
                ed->setSize (1500, 950);
                ed->addToDesktop (0);
                ed->setVisible (true);
                auto pump = [] (int ms = 120) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
                pump (300);
                auto* panel = mc->keyboardPanelComponent();
                auto* seq = mc->sequencerPanelComponent();
                expect (panel != nullptr && seq != nullptr);
                if (panel != nullptr && seq != nullptr)
                {
                    mc->driveSelectTab (3); pump (200);
                    panel->grabKeyboardFocus(); pump (200);
                    expect (juce::Component::getCurrentlyFocusedComponent() == panel,
                            "the keyboard tab holds focus while it is the active tab");
                    panel->qwertyDownForTest ('q');
                    expectEquals (panel->heldNoteCount(), 1, "a QWERTY note sounds while focused");

                    // opening the rename editor moves focus to the text
                    // field: the keys now belong to typing, and the held
                    // note is released by the focus loss itself.
                    mc->driveSelectTab (0); pump (200);
                    expect (juce::Component::getCurrentlyFocusedComponent() != panel,
                            "switching tabs takes the focus (and the keys) away");
                    expectEquals (panel->heldNoteCount(), 0,
                                 "leaving the tab released the held note");
                    seq->showRenameEditor (7); pump (200);
                    auto* focused = juce::Component::getCurrentlyFocusedComponent();
                    expect (focused != nullptr && dynamic_cast<juce::TextEditor*> (focused) != nullptr,
                            "while renaming, the TEXT EDITOR owns the keyboard");
                    expect (focused != static_cast<juce::Component*> (panel),
                            "so a typed name can never fire a note");
                    seq->cancelRenameForTest();
                }
                ed->removeFromDesktop();
            }
            ed.reset();
            proc.releaseResources();
        }
    }
};

static Round5Test test;

#if JUCE_LINUX
 #include <X11/Xlib.h>
#endif

int main()
{
   #if JUCE_LINUX
    // The window-manager stand-in (the DockBackTest / WindowFramesTest
    // pattern): this suite creates real top-level windows (pop-outs,
    // a desktop editor). JUCE's Linux peer looks several WM atoms up
    // with onlyIfExists=True; under a bare Xvfb with no window
    // manager they do not exist, the lookup returns None (0), and
    // the first XChangeProperty dies with BadAtom. Interning them
    // server-side first (atoms are global to the X server) stands in
    // for the WM every real desktop has.
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
    std::printf ("Round5Test: %d checks, %d failures\n", total, fails);
    return fails == 0 ? 0 : 1;
}
