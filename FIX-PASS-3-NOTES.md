# BoomBap — Fix Pass 3 Notes

Built from FEEDBACK-V2 items **69, 70, 71, 72, 74, 75, 76** — the
pad-bank workstation pass, ordered by the owner's hands-on session.
Everything below was built in the working copy, verified locally (full
build clean; **ctest 11/11** — the nine suites that existed before this
pass, plus two new ones: `pad_load` and `fix_pass3`, 248 automated
checks between them), and is additive to the project format — projects
saved by earlier builds load unchanged. Four screenshots were staged
and read back: `fix3-sequencer.png`, `fix3-padbank-loaded.png`,
`fix3-padbank-empty.png`, `fix3-audio-marquee.png`.

## Item 75 — the pad bank could not load a WAV (BUG, built first)

**Root cause, stated concretely.** The pad/sample loader had exactly
one decode route, and it trusted JUCE's `WavAudioFormat` reader
completely — both its willingness to open a file and the sample length
it reports. A probe test generated 26 WAV files across the format's
whole real-world range and pushed every one through the *actual* load
path (`decodeSampleFile` → the engine's pad store, both the pad-bank
route and the sequencer-row route). Every ordinary variant passed —
16/24/32-bit PCM, 32-bit float, 8-bit, mono, 6-channel, 192 kHz,
WAVE_FORMAT_EXTENSIBLE (PCM 16/24 and float), BWF `bext`, odd JUNK
chunks before `fmt `, LIST/INFO chunks, `smpl` chunks, 14-byte and
18-byte `fmt ` chunks, an uppercase `.WAV` name. Four real header
shapes failed, all silently:

1. **Data chunk declares size 0** (unfinalised recorder files): the
   reader reports **0 frames**, the loader "succeeds" with an empty
   sample, and the pad stays empty — no error, nothing to hear.
2. **Data chunk declares size 0xFFFFFFFF** (the other unfinalised
   placeholder): the reader reports a near-billion frames; the loader
   reads its full 20-second cap, of which everything past the real
   audio is silence — the pad "loads" a 20 s mostly-silent sample.
3. **RF64 shells** (RIFF size −1 with a `ds64` chunk, data size −1):
   the same garbage-length behaviour as (2).
4. **`data` chunk placed before `fmt `**: the reader reports the
   correct sample rate and **0 frames** — the pad silently loads
   nothing again, exactly like (1).

**The fix** hardens the one shared decode function
(`MainComponent::decodeSampleFile`, used by the pads and the sequencer
rows alike), in two parts:

- The JUCE reader is now only trusted when its reported length is
  *physically possible*: a frame can never occupy less than one byte,
  so a reader claiming more frames than the file has bytes is lying,
  and a reader claiming zero frames for a file full of audio has given
  up. Both cases fall through.
- A hand-rolled fallback RIFF decoder walks the file itself — chunks
  in any order, `ds64` honoured, a data chunk declared 0 or −1
  measured from the bytes actually present — and decodes PCM
  8/16/24/32 and float 32/64 (including the extensible SubFormat)
  directly to mono, under the same 20-second pad cap. If it also
  cannot read the file, the load fails *loudly* with the existing
  "Couldn't load sample" alert. **No silent path remains.**

Named limit: audio *compressed inside a WAV shell* (MP3-in-WAV and
kin) is genuinely unsupported — the fallback refuses it and the alert
shows. That is the only WAV family this build cannot open.

**Proof:** `Tests/PadLoadTest.cpp` (suite `pad_load`, 193 checks) —
all 26 generated variants decode at full length with their level
intact and land on pads through both load routes. Before the fix the
same test failed exactly the four classes above (0 frames; 882000
frames of capped padding).

## Item 70 — "MPC PANEL" is now "PAD BANK"

The production tab reads **PAD BANK**, and the pop-out window title is
"BoomBap - Pad Bank". Those were the only two user-visible occurrences
of the borrowed name; the internal code names (the `MpcPads` sample
engine, the `mpcPanel` member) are not shipped UI and were left alone.
Visible in both pad-bank screenshots.

## Item 69 — track deletion + marquee clip delete

**Sequencer rows.** A user-added row's name now opens a menu — *Load
sample for this row…* and **Delete track**. Deleting removes the row
whole: its steps in every pattern, its level/mute/solo, its Station
strip and pan, and its own sample slot; rows below shift up one. It is
ONE undoable action (the Pass 2 snapshot history), and redo re-applies
the post-delete snapshot, so nothing the row held can return as a
ghost.

**The documented protection choice:** kit rows 0–4 and pad rows 5–20
refuse deletion. The kit rows are the starter kit's skeleton (their
sounds stay replaceable per item 40, and Reset restores the built-ins,
but the rows themselves stay), and each pad row belongs to one of the
sixteen physical pads — deleting one would orphan the pad.

**Audio lanes.** Every added lane (tracks 2+) carries an **X** at the
top-right of its header; clicking it deletes the lane whole — the
lane, every clip on it, its mixer settings and its Station channel —
as one undoable action. **Lane 1 is protected** (no X; the engine
refuses too): it is the permanent home every take lands on. Clips on
lanes below shift up one. If the lane that dies was holding the
current take's clip, the take is *un-homed* — its audio untouched — so
the established re-homing paths ("Add take as clip", load-time
homing) can place it again. No invisible audio in either direction
(the item 61 rule, applied to deletion).

**Marquee.** Dragging across empty lane space draws a rubber band;
every clip it covers lights with the selection outline, and DELETE
(the button or the key) removes the whole selection as one undoable
action. A plain click on empty space still just clears the selection,
and the rest of the item-58 gesture grammar is untouched. The audio
view's hint line documents both additions.

**Proof:** `Tests/FixPass3Test.cpp` (suite `fix_pass3`) drives the
engine directly (protections refuse in-range; the row below a deleted
row moves down whole with its steps and gain; the take un-homes and
re-homes), the real `MainComponent` (the menu action deletes; ONE undo
restores the row and both neighbours' steps exactly; undo/redo of a
lane deletion restores and re-deletes with clip counts exact — no
ghosts), and the real multitrack component with fabricated mouse
events (the band selects exactly the covered clip; the delete is one
begin/end gesture pair at the undo boundary). Screenshot
`fix3-audio-marquee.png` shows lane 2's X, lane 1 without one, and the
marquee selection lit with DELETE CLIP armed.

## Items 71 + 74 — the pad bank diet, and the waveform is the loader

- The sixteen tiny per-pad captions are **gone**. A pad now carries
  only its number, drawn bigger; loaded pads still read lighter than
  empty ones. The grid's reserved height shrank, so the pads
  themselves are bigger.
- The selected pad's identity lives in **one info strip** under the
  effects/gain controls, at a readable 13.5 pt bold: `PAD 1 -
  kick_deep.wav - loaded, ready to play`, or `PAD 7 - empty - click
  the waveform above to load a sample`.
- The editor's header block (PAD n / file line / LOAD SAMPLE button
  stack) is **one compact label** now: `PAD 1 - kick_deep.wav`.
- **The waveform box is the load surface**: with no sample loaded it
  reads **CLICK TO LOAD A SAMPLE** and clicking it opens the file
  chooser. With a sample loaded, clicking keeps the item 44/63
  behaviour exactly — audition on click, trim by the pull bars.
- The vertical space the header stack and the sliders gave up went to
  the waveform: it is up to ~380 px tall in a tall window (was capped
  at 130), which is the room the coming chop work (item 3) needs.

**Proof:** the fix_pass3 component checks (an empty waveform click
fires the load callback exactly once; a loaded waveform click does
not) and both pad-bank screenshots, read back and verified.

## Item 72 — knobs, not long sliders, in the pad editor

Start %, End %, Tune, Gain and Vel sens are **rotary knobs** in the
Station's exact grammar (`RotaryHorizontalVerticalDrag` with the value
box below, 54×14) — one row of five under the waveform. They are the
same `juce::Slider` objects underneath, so everything they were wired
to still holds: **double-click still resets** each to its default, and
the Start/End knobs stay in sync with the waveform pull bars both ways
(a pull-bar drag moves the knobs; a knob turn moves the trim shading) —
items 44/63 behaviour unchanged. Mixer and master **faders are
untouched**: length means something there.

**Proof:** the fix_pass3 component check counts exactly five rotary
sliders in the pad editor, and `fix3-padbank-loaded.png` shows the
five knobs sitting at their staged values (Start 18.0, End 82.0,
Tune 3.5, Gain 1.35, Vel 0.60).

## Item 76 — step sequencer swipe

Dragging across steps now paints: **left-drag draws steps ON** with
the value the press produced (a press on an empty step paints normal
velocity; a press that cycled a step to accent paints accent), and
**right-drag erases** every step it crosses — D's FL grammar. The
grammar is written into the sequencer's hint line.

The classic paint-drag bug is designed out, not patched over: cells
are **set, never toggled**. The press opens ONE undo gesture and holds
it; each crossed cell is assigned the paint value (dragging back over
the starting cell leaves it ON — it cannot flip back), fast drags fill
every cell between the last and current positions, and the release
closes the gesture — a whole swipe is one undoable action. The swipe
stays on the row the press started on. Single clicks behave exactly
as before (cycle normal → accent → off; right-click clears), and the
step path the pad-capture feature listens to is the same `onChange`
as before.

**Proof:** the fix_pass3 swipe section drives real `StepButton`s with
fabricated mouse events: press + drag paints steps 0–6 and stops at 6;
dragging back over the start cell leaves it on; the gesture counters
show exactly one begin/end pair per swipe and per plain click; a
right-drag erases 4–6 and leaves 0–3; plain clicks still cycle
90 → 127.

## Honest limits

- **Real-host behaviour stays unverifiable from this machine** (no
  DAW here): the plug-in's audio behaviour in FL Studio / Reaper —
  including item 57's open report — can only be judged in the owner's
  hands. Nothing in this pass changes the plug-in's audio path; the
  item 75 fix runs on the message thread at load time.
- **The item 75 diagnosis is proven for the decode layer**, which is
  where four real header families demonstrably died (the probe's
  pre-fix failures are the evidence). If a WAV still refuses on the
  owner's machine after this build, it is not one of those four
  families — the load now fails *loudly* (the alert names the file),
  so the next report will carry information this one couldn't.
- The marquee, swipe and pad-editor interactions are proven headless
  with fabricated events on the real components, and read back in
  screenshots; their feel under a real mouse at 60 Hz is the owner's
  hands-on call, as with every pass.
- The macOS configure failure in CI is untouched by this pass and
  still awaits the Configure step's log text (see
  MACOS-CONFIGURE-NOTES.md).

## Verification summary

- Full build (standalone + VST3 + all tests): clean.
- ctest: **11/11 suites green** — engine, project_file, plugin_load,
  editor_lifecycle, plugin_render, undo, recent_files, dock_back,
  micro_fix, **pad_load (new, 193 checks)**, **fix_pass3 (new,
  55 checks)**.
- Screenshots staged, rendered and read back:
  `fix3-sequencer.png`, `fix3-padbank-loaded.png`,
  `fix3-padbank-empty.png`, `fix3-audio-marquee.png`.
