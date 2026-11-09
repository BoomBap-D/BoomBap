// Round 4 test (Breakdown #1 items 98 + 99): the piano roll becomes a
// real editor for any track (steps shown as notes, octave shift, the
// track drop-down, zoom, chord/progression stamps) and the playlist
// becomes a track-view ARRANGEMENT (blocks on lanes over a bar ruler,
// song playback walking them, legacy chains converted on load).
// Plus the ghost roll's data path (item 99a) and the PATTERN/SONG
// buttons (item 99b).
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/PianoRoll.h"
#include "../Source/App/Playlist.h"
#include "../Source/App/SequencerPanel.h"
#include "../Source/App/ChordLibrary.h"
#include "../Source/App/ProjectFile.h"
#include <cmath>
#include <set>

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

// Fraction of the window's energy a gentle one-pole lowpass keeps:
// a low sine (the "kick") keeps nearly all of it, noise (the "snare")
// loses most of it - the character distinguisher for the song render.
static float lowRatio (const std::vector<float>& x, int from, int to)
{
    double lp = 0.0, tot = 0.0, lpE = 0.0;
    for (int i = juce::jmax (0, from); i < juce::jmin ((int) x.size(), to); ++i)
    {
        lp += 0.08 * ((double) x[(size_t) i] - lp);
        tot += (double) x[(size_t) i] * x[(size_t) i];
        lpE += lp * lp;
    }
    return tot > 0.0 ? (float) (lpE / tot) : 0.0f;
}

class Round4Test : public juce::UnitTest
{
public:
    Round4Test() : juce::UnitTest ("Round4", "boombap") {}

    void runTest() override
    {
        const int sr = 44100;

        // ============ the chord library's pitch sets (pure) ============
        beginTest ("chord library: stamp notes are the chord's pitch set, arpeggiated");
        {
            const auto& minor = chordTypes()[1]; // Minor {0,3,7}
            std::set<int> got;
            for (int k = 0; k < 16; ++k) got.insert (chordStampNote (9, 0, minor, k));
            expect (got == std::set<int> ({ 9, 12, 16 }), "A minor stamps {9,12,16}");

            const auto& dom7 = chordTypes()[4]; // Dom7 {0,4,7,10}
            got.clear();
            for (int k = 0; k < 16; ++k) got.insert (chordStampNote (0, 0, dom7, k));
            expect (got == std::set<int> ({ 0, 4, 7, 10 }), "C dom7 stamps {0,4,7,10}");

            const auto& prog = progressions()[2]; // i - VI - III - VII
            expectEquals ((int) prog.chords.size(), 4);
            expectEquals (prog.chords[0].rootOffset, 0);
            expectEquals (prog.chords[1].rootOffset, 8);
            expectEquals (prog.chords[2].rootOffset, 3);
            expectEquals (prog.chords[3].rootOffset, 10);
        }

        // ============ item 98e: stamps through the real roll ============
        beginTest ("item 98e: chord + progression stamps write editable notes, one undo gesture");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            PianoRollComponent roll (engine);
            roll.setSize (1000, 620);
            int before = 0, after = 0;
            roll.onBeforeChange = [&] { ++before; };
            roll.onAfterChange  = [&] { ++after; };

            roll.bindToTrack (PianoRollComponent::kBindSynth);
            const int written = roll.stampChord (1, 9, 0, 16); // Minor at A
            expectEquals (written, 16);
            std::set<int> got;
            for (int s = 0; s < 16; ++s) got.insert (engine.getSynthStep (s) - 25);
            expect (got == std::set<int> ({ 9, 12, 16 }), "the synth lane holds A minor");
            expectEquals (before, 1, "one undo gesture opens");
            expectEquals (after, 1, "one undo gesture closes");

            roll.bindToTrack (PianoRollComponent::kBindPiano);
            const int w2 = roll.stampProgression (2, 0, 0); // i - VI - III - VII in C
            expectEquals (w2, 32, "the progression fills the pattern");
            auto shareSet = [&] (int from)
            {
                std::set<int> s;
                for (int k = from; k < from + 8; ++k) s.insert (engine.getPianoStep (k) - 25);
                return s;
            };
            expect (shareSet (0)  == std::set<int> ({ 0, 3, 7 }),    "i = C minor");
            expect (shareSet (8)  == std::set<int> ({ 8, 12, 15 }), "VI = Ab major");
            expect (shareSet (16) == std::set<int> ({ 3, 7, 10 }),  "III = Eb major");
            expect (shareSet (24) == std::set<int> ({ 10, 14, 17 }), "VII = Bb major");
            expectEquals (before, 2);
            expectEquals (after, 2);

            // a steps-only row refuses stamps (no pitched lane)
            roll.bindToTrack (BoomBapEngine::Snare);
            expectEquals (roll.stampChord (1, 0, 0, 16), 0, "no stamp without a lane");
        }

        // ============ item 98a/c: steps in the roll + the drop-down ============
        beginTest ("item 98a: the bound track's steps show in the roll; a root-marker click clears the step");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            engine.setStep (BoomBapEngine::Kick, 4, 100);
            PianoRollComponent roll (engine);
            roll.setSize (1000, 620);
            roll.bindToTrack (BoomBapEngine::Kick);
            expect (! roll.stepsModeForTest(), "the kick opens on its melody lane");
            // the marker for step 4 sits on the root row (row 12 at OCT 0)
            const auto c = roll.gridCellCentreForTest (4, 12);
            roll.gridPressForTest (c.x, c.y);
            roll.gridReleaseForTest();
            expectEquals (engine.getStep (BoomBapEngine::Kick, 4), 0,
                          "clicking the marker toggled the step off");
        }

        beginTest ("item 41/98: the steps lane still edits a lane-less row from the roll");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            PianoRollComponent roll (engine);
            roll.setSize (1000, 620);
            roll.bindToTrack (BoomBapEngine::Snare);
            expect (roll.stepsModeForTest(), "the snare opens on its steps");
            const auto c = roll.gridStepCentreForTest (2);
            roll.gridPressForTest (c.x, c.y);
            expectEquals (engine.getStep (BoomBapEngine::Snare, 2), 90, "first click: on");
            roll.gridPressForTest (c.x, c.y);
            expectEquals (engine.getStep (BoomBapEngine::Snare, 2), 127, "second click: accent");
            roll.gridPressForTest (c.x, c.y);
            expectEquals (engine.getStep (BoomBapEngine::Snare, 2), 0, "third click: off");
        }

        beginTest ("item 98c: the roll's track drop-down rebinds the roll and the selection together");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            PianoRollComponent roll (engine);
            roll.setSize (1000, 620);
            int selected = -99;
            roll.onSelectChannel = [&] (int ch) { selected = ch; };
            roll.bindToTrack (0);
            // JUCE delivers a ComboBox's onChange through its async
            // updater - pump the loop like the running app would.
            auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (150); };
            roll.trackBoxForTest().setSelectedId (3, juce::sendNotification);
            pump();
            expectEquals (roll.boundTrack(), 2, "id 3 = the third row");
            expectEquals (selected, 2, "the selection followed the drop-down");
            roll.trackBoxForTest().setSelectedId (1001, juce::sendNotification);
            pump();
            expectEquals (roll.boundTrack(), (int) PianoRollComponent::kBindSynth);
            expectEquals (selected, (int) BoomBapEngine::kSynthChannel);
            roll.bindToTrack (5);
            expectEquals (roll.trackBoxForTest().getSelectedId(), 6,
                          "binding from outside moves the drop-down too");
        }

        // ============ item 98b/d: octave + zoom ============
        beginTest ("item 98b: octave shift moves the window - the same pitch is edited at its new row");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            PianoRollComponent roll (engine);
            roll.setSize (1000, 620);
            roll.bindToTrack (PianoRollComponent::kBindSynth);
            engine.setSynthStep (0, 25); // the root
            roll.setOctaveShift (12);
            expectEquals (roll.octaveShift(), 12);
            // at OCT +1 the root pitch lives on the bottom row (24)
            const auto c = roll.gridCellCentreForTest (0, 24);
            roll.gridPressForTest (c.x, c.y);
            roll.gridReleaseForTest();
            expectEquals (engine.getSynthStep (0), 0,
                          "pressing the root's new row erased the root note");
            roll.setOctaveShift (13);
            expectEquals (roll.octaveShift(), 12, "the shift snaps to whole octaves");
            roll.setOctaveShift (-40);
            expectEquals (roll.octaveShift(), -12, "and clamps at one octave");
        }

        beginTest ("item 98d: zoom presets change the canvas; the factor clamps");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            PianoRollComponent roll (engine);
            roll.setSize (1000, 620);
            const int w100 = roll.gridCanvasSizeForTest().getWidth();
            const int h100 = roll.gridCanvasSizeForTest().getHeight();
            roll.setZoomFactor (0.25f);
            expect (roll.gridCanvasSizeForTest().getWidth() < w100, "25% is a smaller canvas");
            expect (roll.gridCanvasSizeForTest().getHeight() < h100);
            roll.setZoomFactor (2.0f);
            expect (roll.gridCanvasSizeForTest().getWidth() > w100, "200% is a bigger canvas");
            roll.setZoomFactor (99.0f);
            expectEquals (roll.zoomFactor(), 2.0f, "zoom clamps high");
            roll.setZoomFactor (0.01f);
            expectEquals (roll.zoomFactor(), 0.25f, "zoom clamps low");
        }

        // ============ the arrangement model ============
        beginTest ("item 99c: arrangement rules - same-lane overlap forbidden, lanes free");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            BoomBapEngine::SongBlock a; a.active = true; a.pattern = 0; a.lane = 0; a.startBar = 0; a.lengthBars = 2;
            const int ia = engine.addSongBlock (a);
            expect (ia >= 0);
            expectEquals (engine.songTotalBars(), 2);
            BoomBapEngine::SongBlock overlap = a; overlap.pattern = 1; overlap.startBar = 1;
            expect (! engine.arrangementCanPlace (overlap, -1), "bar 1 on lane 1 collides");
            BoomBapEngine::SongBlock otherLane = overlap; otherLane.lane = 1;
            expect (engine.arrangementCanPlace (otherLane, -1), "another lane is free");
            BoomBapEngine::SongBlock after = a; after.pattern = 2; after.startBar = 2;
            expect (engine.arrangementCanPlace (after, -1), "touching ends is not overlap");
            expect (engine.arrangementCanPlace (a, ia), "a block may stay where it is");
            engine.removeSongBlock (ia);
            expectEquals (engine.songBlockCount(), 0);
            expectEquals (engine.songTotalBars(), 0);
        }

        beginTest ("item 99c: resolveSongStep - pattern loops inside a block; hits come in lane order");
        {
            BoomBapEngine::SongBlock blocks[3];
            blocks[0].active = true; blocks[0].pattern = 3; blocks[0].lane = 1; blocks[0].startBar = 2; blocks[0].lengthBars = 4;
            blocks[1].active = true; blocks[1].pattern = 1; blocks[1].lane = 0; blocks[1].startBar = 0; blocks[1].lengthBars = 8;
            blocks[2].active = false;
            BoomBapEngine::BlockHit hits[8];
            // song step at bar 4, step 5 = 69: block 0 is in its second
            // loop there (local step 5), block 1 covers it too.
            const int n = BoomBapEngine::resolveSongStep (blocks, 3, 69, hits, 8);
            expectEquals (n, 2);
            expectEquals (hits[0].pattern, 1, "lane 0 first");
            expectEquals (hits[0].localStep, 69 % 32);
            expectEquals (hits[1].pattern, 3);
            expectEquals (hits[1].localStep, 5, "the pattern looped inside the 4-bar block");
            expectEquals (BoomBapEngine::resolveSongStep (blocks, 3, 200, hits, 8), 0,
                          "past the end, nothing sounds");
        }

        // ============ song playback, rendered and measured ============
        beginTest ("item 99c: song playback renders each block's pattern at its bars (measured)");
        {
            auto makeEngine = [&]
            {
                auto e = std::make_unique<BoomBapEngine>();
                e->clearPattern();
                e->setBpm (120.0f);
                e->setSwing (50.0f);
                e->setGlueEnabled (false);
                e->prepare (sr);
                // the "kick": a low decaying tone; the "snare": noise
                std::vector<float> kick (sr / 4), snare (sr / 8);
                for (int i = 0; i < (int) kick.size(); ++i)
                    kick[(size_t) i] = (float) (0.9 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / sr)
                                                * std::exp (-18.0 * i / sr));
                juce::Random rng (7);
                for (int i = 0; i < (int) snare.size(); ++i)
                    snare[(size_t) i] = (float) ((rng.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-30.0 * i / sr));
                e->loadSample (BoomBapEngine::Kick, kick, sr);
                e->loadSample (BoomBapEngine::Snare, snare, sr);
                for (int p = 0; p < 2; ++p)
                {
                    e->setEditPattern (p);
                    e->clearPattern();
                    for (int s = 0; s < BoomBapEngine::kSteps; ++s)
                    {
                        e->setMelodyStep (s, 0);
                        e->setSynthStep (s, 0);
                        e->setPianoStep (s, 0);
                    }
                }
                e->setEditPattern (0);
                e->setStep (BoomBapEngine::Kick, 0, 127);
                e->setEditPattern (1);
                e->setStep (BoomBapEngine::Snare, 0, 127);
                e->setEditPattern (0);
                return e;
            };
            const int barLen = sr * 2; // 120 bpm: 2 s per bar

            // the arrangement: kick pattern on lane 1 (bars 1-2),
            // snare pattern on lane 2 (bars 3-4)
            float singleLaneKickPeak = 0.0f;
            {
                auto e = makeEngine();
                BoomBapEngine::SongBlock b1; b1.active = true; b1.pattern = 0; b1.lane = 0; b1.startBar = 0; b1.lengthBars = 2;
                BoomBapEngine::SongBlock b2; b2.active = true; b2.pattern = 1; b2.lane = 1; b2.startBar = 2; b2.lengthBars = 2;
                e->addSongBlock (b1);
                e->addSongBlock (b2);
                e->setSongMode (true);
                e->setPlaying (true);
                const auto out = render (*e, barLen * 4 + sr / 2);
                e->setPlaying (false);
                // each pattern strikes on step 0 of its two-bar loop:
                // onsets at bars 1 and 3, the patterns' empty bars
                // (2 and 4) stay silent - placement proven both ways.
                expect (peakIn (out, 0, 4000) > 0.08f, "an onset sounds at bar 1 (kick pattern)");
                expect (peakIn (out, barLen, barLen + 4000) < 0.04f, "bar 2 is the kick pattern's empty bar");
                expect (peakIn (out, barLen * 2, barLen * 2 + 4000) > 0.08f, "an onset sounds at bar 3 (snare pattern)");
                expect (peakIn (out, barLen * 3, barLen * 3 + 4000) < 0.04f, "bar 4 is the snare pattern's empty bar");
                expect (lowRatio (out, 0, barLen) > 0.7f, "bar 1 is the low tone (kick)");
                expect (lowRatio (out, barLen * 2, barLen * 3) < 0.55f, "bar 3 is noise (snare)");
                singleLaneKickPeak = peakIn (out, 0, 4000);
            }

            // the same pattern on two lanes must NOT double-trigger
            {
                auto e = makeEngine();
                BoomBapEngine::SongBlock b1; b1.active = true; b1.pattern = 0; b1.lane = 0; b1.startBar = 0; b1.lengthBars = 2;
                BoomBapEngine::SongBlock b2 = b1; b2.lane = 1;
                e->addSongBlock (b1);
                e->addSongBlock (b2);
                e->setSongMode (true);
                e->setPlaying (true);
                const auto out = render (*e, barLen * 2);
                e->setPlaying (false);
                const float p = peakIn (out, 0, 4000);
                expect (p < singleLaneKickPeak * 1.1f && p > singleLaneKickPeak * 0.9f,
                        "two lanes of one pattern = one trigger, not two");
            }

            // a 4-bar block loops its 2-bar pattern inside itself
            {
                auto e = makeEngine();
                BoomBapEngine::SongBlock b1; b1.active = true; b1.pattern = 0; b1.lane = 0; b1.startBar = 0; b1.lengthBars = 4;
                e->addSongBlock (b1);
                e->setSongMode (true);
                e->setPlaying (true);
                const auto out = render (*e, barLen * 4);
                e->setPlaying (false);
                expect (peakIn (out, 0, 4000) > 0.08f, "loop pass 1 kicks");
                expect (peakIn (out, barLen, barLen + 4000) < 0.04f, "bar 2 is the pattern's empty bar");
                expect (peakIn (out, barLen * 2, barLen * 2 + 4000) > 0.08f, "loop pass 2 kicks again");
            }
        }

        // ============ back-compat: the legacy chain ============
        beginTest ("item 99c back-compat: a chain-only project converts to lane-1 blocks on load");
        {
            ProjectSettings ps;
            for (int i = 0; i < ProjectSettings::kSongSlots; ++i) ps.songChain[i] = -1;
            ps.songLength = 2;
            ps.songChain[0] = 3;
            ps.songChain[1] = 5;
            BoomBapEngine engine;
            engine.prepare (sr);
            engine.applySettings (ps);
            expectEquals (engine.songBlockCount(), 2, "two chain slots became two blocks");
            expectEquals (engine.songTotalBars(), 4, "two patterns = four bars");
            bool saw3 = false, saw5 = false;
            for (int i = 0; i < ProjectSettings::kMaxSongBlocks; ++i)
            {
                const auto b = engine.getSongBlock (i);
                if (! b.active) continue;
                expectEquals (b.lane, 0, "converted blocks live on lane 1");
                expectEquals (b.lengthBars, 2);
                if (b.pattern == 3) { saw3 = true; expectEquals (b.startBar, 0); }
                if (b.pattern == 5) { saw5 = true; expectEquals (b.startBar, 2); }
            }
            expect (saw3 && saw5, "both chain patterns survived the conversion");
            const ProjectSettings back = engine.getSettings();
            expectEquals (back.songLength, 2, "the chain mirror round-trips");
            expectEquals (back.songChain[0], 3);
            expectEquals (back.songChain[1], 5);
        }

        beginTest ("item 99b/c: the arrangement + the mode round-trip through the project file");
        {
            ProjectData data;
            data.settings.songMode = true;
            auto& b0 = data.settings.songBlocks[0];
            b0.active = true; b0.pattern = 2; b0.lane = 1; b0.startBar = 4; b0.lengthBars = 6;
            const auto text = projectToJson (data, juce::File());
            ProjectData back;
            juce::String error;
            expect (projectFromJson (text, juce::File(), back, error), error);
            expect (back.settings.songMode, "SONG mode persisted");
            expect (back.settings.songBlocks[0].active);
            expectEquals (back.settings.songBlocks[0].pattern, 2);
            expectEquals (back.settings.songBlocks[0].lane, 1);
            expectEquals (back.settings.songBlocks[0].startBar, 4);
            expectEquals (back.settings.songBlocks[0].lengthBars, 6);
        }

        beginTest ("item 99c back-compat: a legacy JSON file (no blocks key) still loads its chain");
        {
            ProjectData data;
            for (int i = 0; i < ProjectSettings::kSongSlots; ++i) data.settings.songChain[i] = -1;
            data.settings.songLength = 1;
            data.settings.songChain[0] = 4;
            auto text = projectToJson (data, juce::File());
            // strip the Round 4 keys, leaving the file a pre-Round-4 build wrote
            auto root = juce::JSON::parse (text);
            if (auto* song = root.getProperty ("song", {}).getDynamicObject())
            {
                song->removeProperty ("blocks");
                song->removeProperty ("mode");
            }
            text = juce::JSON::toString (root);
            ProjectData back;
            juce::String error;
            expect (projectFromJson (text, juce::File(), back, error), error);
            expectEquals (back.settings.songLength, 1);
            expectEquals (back.settings.songChain[0], 4);
            expect (! back.settings.hasBlocks(), "no blocks were invented at read time");
            BoomBapEngine engine;
            engine.prepare (sr);
            engine.applySettings (back.settings);
            expectEquals (engine.songBlockCount(), 1, "the engine lifts the chain on apply");
        }

        // ============ item 99a: the ghost roll's data path ============
        beginTest ("item 99a: ghost data - a row reads its own lane's notes, and only its own");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            engine.setEditPattern (0);
            engine.setMelodySource (0); // the kick owns the lane
            engine.setMelodyStep (5, 30);
            expectEquals (engine.ghostNoteValue (BoomBapEngine::Kick, 5), 30);
            expectEquals (engine.ghostNoteValue (BoomBapEngine::Snare, 5), 0, "a lane-less row reads nothing");
            expect (engine.trackHasGhostLane (BoomBapEngine::Kick));
            expect (! engine.trackHasGhostLane (BoomBapEngine::Snare));
            engine.setMelodySource (3); // pad 3's row takes the lane
            expectEquals (engine.ghostNoteValue (BoomBapEngine::Sample3, 5), 30);
            expectEquals (engine.ghostNoteValue (BoomBapEngine::Kick, 5), 0, "the kick lost the lane");
        }

        beginTest ("item 99a: the strip shows under a lane-owning row only, and obeys its switch");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            SequencerPanel::Callbacks cb;
            SequencerPanel panel (engine, cb);
            panel.setSize (1580, 360);
            panel.setGhostEnabled (true);
            panel.setSelectedRow (BoomBapEngine::Kick);
            expect (panel.ghostStripShowing(), "strip under the kick");
            panel.setSelectedRow (BoomBapEngine::Snare);
            expect (! panel.ghostStripShowing(), "no strip under a lane-less row");
            panel.setSelectedRow (BoomBapEngine::Kick);
            panel.setGhostEnabled (false);
            expect (! panel.ghostStripShowing(), "the switch hides it");
            panel.setGhostEnabled (true);
            expect (panel.ghostStripShowing(), "and brings it back");
        }

        // ============ the playlist component's gestures ============
        beginTest ("item 99c: playlist gestures - drag moves a block one undo gesture; right-click deletes");
        {
            BoomBapEngine engine;
            engine.prepare (sr);
            BoomBapEngine::SongBlock b; b.active = true; b.pattern = 0; b.lane = 0; b.startBar = 0; b.lengthBars = 2;
            const int idx = engine.addSongBlock (b);
            PlaylistComponent playlist (engine);
            playlist.setSize (980, 480);
            int before = 0, after = 0;
            playlist.onBeforeChange = [&] { ++before; };
            playlist.onAfterChange  = [&] { ++after; };

            const auto r = playlist.blockRectForTest (idx);
            expect (! r.isEmpty(), "the block has a rect");
            const auto centre = r.getCentre();
            playlist.contentPressForTest (centre);
            expectEquals (playlist.selectedBlockForTest(), idx, "a click selects");
            playlist.contentDragForTest (centre + juce::Point<float> (108.0f, 0.0f)); // two bars right
            playlist.contentReleaseForTest();
            expectEquals (engine.getSongBlock (idx).startBar, 2, "the drag moved it two bars");
            expectEquals (before, 1, "one undo gesture opened");
            expectEquals (after, 1, "and closed");

            // trim by the right edge: +1 bar of length
            const auto r2 = playlist.blockRectForTest (idx);
            const juce::Point<float> edge (r2.getRight() - 3.0f, r2.getCentreY());
            playlist.contentPressForTest (edge);
            playlist.contentDragForTest (edge + juce::Point<float> (54.0f, 0.0f));
            playlist.contentReleaseForTest();
            expectEquals (engine.getSongBlock (idx).lengthBars, 3, "the edge drag trimmed it longer");

            // the ruler parks the transport (item 50's rule)
            engine.setSongMode (true);
            playlist.contentPressForTest (juce::Point<float> (64.0f + 2.5f * 54.0f, 10.0f));
            expectEquals ((int) engine.transportPosition(),
                          (int) std::llround (2.0 * engine.barSamples()),
                          "a ruler click parked at bar 3");

            // right-click deletes
            const auto r3 = playlist.blockRectForTest (idx);
            playlist.contentPressForTest (r3.getCentre(), true);
            expectEquals (engine.songBlockCount(), 0, "right-click removed the block");
        }

        // ============ item 99b: the main page's mode buttons ============
        beginTest ("item 99b: PATTERN/SONG buttons on the main page drive the engine's mode");
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            auto* mc = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
            expect (mc != nullptr);
            if (mc != nullptr)
            {
                auto& engine = proc.engine; // the editor's MainComponent drives this same engine
                ed->setSize (1500, 950);
                auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (150); };
                pump();
                engine.setSongMode (false);
                mc->songModeButtonForTest().triggerClick();
                pump(); // button clicks land through the message loop
                expect (engine.isSongMode(), "SONG switches the mode on");
                mc->patternModeButtonForTest().triggerClick();
                pump();
                expect (! engine.isSongMode(), "PATTERN switches it back");
            }
            ed.reset();
            proc.releaseResources();
        }
    }
};

static Round4Test test;

int main()
{
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
    std::printf ("Round4Test: %d checks, %d failures\n", total, fails);
    return fails == 0 ? 0 : 1;
}
