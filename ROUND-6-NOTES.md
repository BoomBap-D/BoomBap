# ROUND 6 — CRASH FIX, PAD FREEDOM, METERS, TRANSPORT (items 108–118)

Breakdown #1 continues in the owner's order. Round 5 left the app at
ctest 17/17; this round adds the Round6Test suite (**122 checks,
0 failures**) and lands at **ctest 18/18** — see "Proof" below.

Prior assertions amended on purpose, each with the reason written in
the test itself:

- **Round3Test** — "the target pad's row takes the sample's name
  too": item 109 dissolved the pad↔row coupling that line asserted;
  a pad copy now leaves the row's name alone.
- **EngineTest** (two places) — the melody lane and the pad-activity
  checks now drive the ROW store, since rows sound their own samples
  after item 109; a row step asserts pad activity stays cold.
- **Round2Test** — the board fader double-click target 0.8 → 1.0
  (item 113's flat), and the item-93 Station transport block presses
  the new family-A keys through the panel's transport door instead
  of hunting labelled text buttons (item 115); item 93's semantics
  are asserted unchanged.
- **ProjectFileTest** — a legacy file with no gain data now defaults
  to flat 1.0 instead of 0.9 (item 113).
- **PluginRenderTest** — the limiter's absolute bound 0.125 → 0.13:
  the old number silently encoded the vocal channel's 0.85 default
  gain; at item 113's flat unity the same strip ceiling (−18 dB)
  measures ~0.126 at the output. The relative cut is unchanged.

## Item 108 — SAVE PRESET CRASH (priority 1) — FIXED

His words: the crash reproduced twice inside the Station's FULL
BOARD when saving a preset.

**Root cause.** `StationPanel::savePreset()` held the name dialog in
a `std::make_shared<juce::AlertWindow>` AND passed
`deleteWhenDismissed=true` to `enterModalState`. On dismissal JUCE
runs the callback first, then deletes the window — and then the
callback lambda's own destruction dropped the shared_ptr's last
reference and deleted the window a **second time**. A deterministic
double-delete on every dismissal, Save or Cancel. It was the only
`enterModalState`/`make_shared<AlertWindow>` site in the codebase.

**Fix.** Single ownership: the window is created raw and
`deleteWhenDismissed` owns it; the callback captures the raw pointer
(safe — JUCE deletes only after callbacks run).

**Proof.** The round6 suite drives the real dialog in the real main
window: hero-mode save, FULL BOARD save (his crash site), and a
Cancel dismissal — the preset files land and the app stands.

## Item 109 — NO AUTO-LOAD INTO PADS (his ruling) — DONE

His words: *"loading a sample into the main waveform panel must NOT
land on any drum pad. Pads change ONLY by explicit action."*

The engine now keeps **two stores**: the sequencer rows hold their
own sounds (a second `MpcPads` bank), the 16 pads hold theirs. The
old coupling (pads 1–3 mirroring the first three sample rows) is
gone from the load path, the step triggers, the pitched lanes, and
the live-note path. Project files carry both stores: a new `rowPads`
array is always written, and pads 1–3 carry their file paths now
too. **Legacy projects sound identical** — a file with no `rowPads`
key loads its rows from the pad files, which is what those files
always meant. Measured in renders: a row step sounds the row's
sample and stays silent when only the pad holds a sound; the pad
sounds on the pad bus and is untouched by row loads/clears.

**Two catches found by the new suite, both fixed in this round.**
(1) The first cut summed the loose pads straight into the beat,
bypassing the pad bus — its strip, its fader, and its peak (the pad
bank's new meter reads that peak and heard nothing). The pads now
join the pad bus like the pad rows always did. (2) The project
loader's legacy fallback (pads 1–3 reading their files from the row
paths, correct for pre-split files) fired for *current* projects
too, so every project round-trip — undo snapshots included —
quietly re-mirrored rows 1–3 onto pads 1–3. The fallback is now
gated to legacy files (no `rowPads` key); in a split-store project
an empty pad path means an empty pad.

**A seam to know about (flagged, not changed):** pad capture
(item 31) still writes steps onto a pad's namesake ROW when you play
pads with capture on. The steps land where they always did; the row
now sounds its own sample rather than the pad's. Left as-is — it's
the step-writing item working as written; say the word if capture
should follow the pad's sound instead.

## Item 110 — CLEAR PAD + the drag guard — DONE

Right-click a pad → **Clear pad** (Load sample… is in the same menu;
Clear is greyed when the pad is empty, and clearing doesn't play the
pad). The clear is one undoable gesture. The accidental-copy problem
— he ended up with seven pads of the same sound — was the drag
threshold: 8 px turned an emphatic click's wobble into a copy. The
threshold is now **16 px**: a wobble is a click, a deliberate travel
still drag-copies exactly as before.

## Item 111 — PAD BANK LAYOUT — DONE

- **Docked:** the pads use a squarer shape (aspect 1.12 vs the
  pop-out's 1.25) — at the standard dock they stand ~62 px tall
  instead of ~54, and the editor column gives up 30 px on the right
  to the new meter without shrinking the pads.
- **Pop-out:** knobs capped smaller (72 px), the envelope gets a
  real band (130 px at working size), the waveform takes what's left
  after the fixed rows (~120 px at 1000×620), and Reverse / Mute
  Group / None now live in their **own footer row** under the knobs
  instead of stacking into a corner over them. Knobs no longer sit
  on their labels.
- **The caption appears ONCE** — the top letterbox. The duplicate
  "PAD 1 — file — loaded" line at the bottom is gone (the guidance
  paragraph stays, rewritten to say pads and rows hold their own
  sounds now).
- The pop-out opens a little bigger (980×560) to hold it.

## Item 112 — METERS — DONE, with the master exception he set

(a) **Clip latch on every meter** — a red box that lights at full
scale and **stays lit until clicked off**: the row meters, the GR
meter (its latch trips at full-scale reduction — a GR meter has no
audio clip point, so full deflection is its honest analog), every
fader's ladder, the pad bank's meter, the B1 face. Clicking a
fader's clip box clears it and never moves the fader.
(b) **Pad bank meter** — a segmented ladder running the bank's whole
right side, fed from the pad bus (honest after item 109).
(c) **Station clip outline** — a clip on the bound channel lights a
red outline around the whole EQ/meter box; clicking the EQ or the
VU face clears it.
(d) **Compressor line** — a thin red line on the EQ display sits at
the reduction's depth and moves as the gain reduction moves (with a
small "GR" tag), present only while the compressor works.
(e) **Double meters** — every board/Station channel fader now meters
L and R in two columns, so a panned channel reads honestly instead
of one max-of-both ladder.

**MASTER EXCEPTION (his ruling, Nov 9):** the master's VU FACE is
held for the future master console (Pass 6) — the Round 2 needle
stands untouched this round. The master channel still gains the
clip latch and the double meter like every other channel; only its
VU face waits.

## Item 116 — THE B1 VU FACE — DONE

Where a VU appears on channels/Station, it's his B1 pick: charcoal
housing and face, speedometer numbers fanned along the arc
(−20 −10 −7 −5 −3 −2 −1 0, red +1 +2 +3, red zone past 0), a thin
red peak-hold needle, **CURRENT** box bottom-left, **PEAK** box
bottom-right — and when it peaks, the PEAK box takes a **red outline
that stays until clicked off**, with the overload (OL) dot glowing
red beside it. True VU ballistics (fast rise, ~1.1 dB/tick fall; the
peak holds ~3 s before easing down). His reason for B1 stands in the
code: when you're tightening a sound you see how loud it GOT, not
just where it is. The symbols from his reference photo's top bar
were excluded, as he ruled.

## Item 113 — FADERS FLAT — DONE

Every fader starts at unity: sequencer rows, audio tracks, synth,
piano, pad bus, vocal, master — including the settings defaults and
what a legacy file with no gain data comes up as. Double-click
returns any console fader to exactly flat.

## Item 114 — WAVEFORM DRAGS — DONE

Dragging from the waveform panel now lands on **a step sequencer
row** (loads that track) and on **the audio multitrack** (creates a
clip at the drop point, decoded and resampled to the session rate).
Waveform→pad worked before and still does. One flagged limit: the
two new targets show **no drag highlight/ghost** while you drag
(the pads highlight theirs) — the drop lands, but the row/lane
doesn't light up on the way. Say the word if it should.

## Item 115 — TRANSPORT — DONE

Family A "Studio Console" in his order on the main window:
**[TO-START][REWIND][PAUSE][PLAY dead centre][STOP][FF][TO-END]** —
chunky charcoal keys, amber glyphs, an amber ring on PLAY that
thickens while playing. The clock sits centred in the row, large,
inside its framed bezel; clicking it cycles the Round 2 formats.
**PAUSE is new**: it freezes the playhead where it stands (held
notes and steps hold their state); PLAY from pause resumes there;
STOP keeps its return-to-park behaviour; double-press REW/FF still
steps a bar. The Station wears the same family, same order,
**compact**, its clock framed too, and its PAUSE key drives the same
engine pause.

Semantics note: resuming re-enters at the frozen position and the
step under the playhead re-fires — the same law as every other seek
in this engine, applied consistently.

## Item 117 — STATION EQ: CENTRED BUTTONS + RESET — DONE

His words: *"that space kinda irks me"* — the EQ's action buttons
(ON / + BAND / DELETE / RESET) are now **centred as a group** under
the EQ display. (The five TYPE buttons stay at the left with the
band label — they describe the selected band; the centred four act
on it. Flagged as a reading of "the three EQ buttons" — strike it if
he meant all nine.) **RESET** sits beside DELETE: one press flattens
every band gain to 0 dB — "back to line level" — as **one undoable
gesture**; bands, types, frequencies and on-states are untouched.

## Item 118 — UNDO GETS A FACE — DONE

A labelled **UNDO** button in the main window's top cluster, with
**REDO** as its pair (the pairing is ours — flagged for him to
strike; the manager always supported redo). Each press undoes the
most recent action, no depth cap; the buttons enable/disable with
the history and refresh every UI tick. They work **regardless of
focus** — including while the KEYBOARD tab holds it: the audit found
the real hole there (the panel swallowed Ctrl+Z as a note key) and
it's fixed — a modified key is never a note now, so Ctrl+Z / Ctrl+Y /
Ctrl+S climb to the window as they should.

**The audit.** Every user-visible mutation on the main screen was
traced to the one history: step edits and swipes, piano-roll edits,
playlist blocks, multitrack clip edits and lane ops, pad editor /
copies / loads / clears, Station and board strips, pattern/song
mode, Clear/Reset pattern, the setup sliders, rename, keyboard
step-write, and this round's own additions. Four named exceptions,
all deliberate:

1. **Metronome on/level** — live performance settings; they live in
   engine atomics, not the project file, so the snapshot history
   cannot hold them.
2. **Vocal monitor toggle** — it arms a live input; undoing it would
   re-open the microphone. A device action, not a document edit.
3. **Recording and take placement** — capture results, as in any
   DAW; the clips they create are then fully editable and undoable.
4. **Export bars-count and normalize toggle** — render parameters
   that ride the file as convenience state; their controls
   self-mutate before notifying, so a handler-time snapshot cannot
   straddle the change.

**Proof.** The suite runs a six-action chain through the real main
window — load, step edit, rename, pad load, pad copy, board fader
move — then presses the UNDO button six times, asserting after each
press that exactly the last action reverted and the earlier ones
still stand, ending exactly at the start state.

## Added the same evening (items 123–125, from his hands-on session)

**Item 108 re-confirmed.** He hit the save-preset crash again
tonight — on the Round 5 build he still has, the fix not yet in his
hands. The round6 suite walks his exact path (Station open, FULL
BOARD up, Save preset, name, confirm) plus hero mode and the Cancel
dismissal; his re-test lands on this build.

**Item 123 — the keyboard's keys read as KEYS.** Painting only, no
behaviour, routing, layout or geometry change (keyRect is
untouched): every white key now has a real seam (the panel shows
through the gap), a per-key vertical gradient — lit at the top,
settling darker at the bottom — and a lit top edge, in the same
dark family as before; black keys get their own near-black gradient
with a lit top edge and a faintly lit front edge. The C4 key keeps
its brighter distinction inside the gradient, the held-key amber is
a gradient of the same accents, and the QWERTY letters and note
names sit exactly where they sat. Proof: `round6-keyboard123.png` /
`round6-keys123.png`, read back before claiming.

**Item 124 — selecting a track auditions its sound.** A LEFT-button
tap on a sequencer row's **name** now also plays the track's sound
once — C4, the sound at rest — through the live-note funnel: the
pad voice path for sample rows, the track's own voice for kit rows
(the same voices a step fires, with the track's trims, envelope and
channel). Boundaries held: it fires ONLY from the name-tap callback
— programmatic selection (loads, undo applies, other views,
playback) never sounds; the row's other select paths (LOAD, ROLL,
MUTE, SOLO) stay silent; the right-click selects and opens its menu
but does not audition; an empty sample row has nothing to play and
stays silent; it is never a take and never a step-write, and stop
silences it like any voice. It does NOT open any editor — that
deeper gesture stays parked, as he ruled. Suite-covered at render
level, five ways.

**Item 125 — the ghost roll is per-track, earned.** The ghost strip
no longer follows selection around: it appears only when the
selected track's OWN lane actually holds notes (a new engine door,
`trackHasGhostContent`). A lane-owning row with an empty lane
raises nothing; selecting any other track never raises it; clearing
the lane's last note retires it. The Ghost roll switch remains the
master off. One Round 4 assertion was amended with the reason in
the test (its setup now gives the lane a note, and asserts the new
empty-lane law). **The rider:** the strip is also a DOOR — one
click anywhere on it summons that track's piano roll, the same
summon the row's ROLL button makes (his either/or — click or
double-click — is satisfied by the single click, which a
double-click contains); the cursor turns to a pointing hand over
it. Suite-covered: the click lands on the strip's own track.

**The zipcheck fix (not a defect — a wrong test).** The
investigation of the Round 5 package's Linux exit-code-8 found it:
the app was never broken. `PluginLoadTest` asserted the plugin
editor opens at EXACTLY 1560×900, but the editor clamps to the
display's usable area — item 1's fit-the-screen law, working as
designed. GitHub's xvfb screen is 1280×1024, so the editor opened
1280 wide and the check failed; the same binary passed on any
screen big enough. The assertion now verifies the CLAMP CONTRACT
(editor == 1560×900 capped by the usable area of the display it
opens on), and it is verified at BOTH screen sizes: 1280×1024
(the GitHub shape) and the 1920×1200 harness. The editor's clamp
itself is untouched — the clamp is the owner's law; the test was
wrong, not the app.

**Not this round (logged, untouched):** items 126 (the universal
track strip), 127 (selected track's sample in the waveform panel),
128 (the keyboard as instrument room) — they form the next round,
as he set them.

## Proof

- ctest **18/18** green on the Linux build (was 17/17).
- Round6Test: **122 checks, 0 failures** — crash path (both modes +
  cancel), the decoupled stores measured in renders, the split-store
  file format and its legacy fallback, every latch, the B1 face's
  hold-and-clear, the compressor line, the drag threshold, the pad
  bank geometry, pause/park semantics, flat defaults, the RESET and
  its single undo step, the waveform drag landings, the six-action
  undo chain, the tap-to-audition boundaries (124), the earned ghost
  and its door (125 + rider).
- plugin_load passes at 1280×1024 AND 1920×1200 (the zipcheck fix).
- Screenshots read back before claiming: `round6-main.png`,
  `round6-transport-main.png`, `round6-transport-station.png`,
  `round6-station.png`, `round6-vuface.png`, `round6-board.png`,
  `round6-padbank.png`, `round6-keyboard123.png`,
  `round6-keys123.png` (in the shots folder).
- Item 57's real-host audio behaviour stays **unverified** — the
  FL/Reaper run on his PC is still the test, as ever.

## Packages

Refreshed with this round: `BoomBap-GitHub-Ready.zip` (plus its
PART1-APP / PART2-TESTS split for the 100-file drag cap) and
`BoomBap-FINISHED-Source.zip` — the same packaging set as Round 5,
plus these notes and the new suite. Both byte-verified on Drive
after upload; these notes also ride as their own file in the same
folder.
