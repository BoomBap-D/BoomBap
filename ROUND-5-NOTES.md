# ROUND 5 — KEYBOARD TAB (item 105) + TRACK RENAME (item 107)

Breakdown #1 continues in the owner's order. Round 4 left the app at
ctest 16/16; this round adds the Round5Test suite (**201 checks,
0 failures**) and lands at **ctest 17/17** — see "Proof" below.
One prior assertion was amended on purpose, with the reason written
in the test: Round 1's "a pad-row name has no menu" — true when it
was written, superseded by this round's item 107 (pad rows have the
rename menu now); the select-first half of the old rule stands and
is still asserted.

## Item 105 — the KEYBOARD tab

His words: *"Let's put the keyboard in as one of the tabs, just next
to that in the same section. The keyboard is one of the most
important things right now for me, to test certain functions, even
if we don't have MIDI yet."*

A fourth production tab, **KEYBOARD**, in the same tab bar as STEP
SEQUENCER / PAD BANK / AUDIO MULTITRACK, in the same grammar: docked
in the bottom section, pop-out capable through the same machinery
(same window title-bar treatment, same dock-back path, same
minimise coupling — the Round 1 coupling list now includes the
fourth production window).

### (a) The keys

- Three octaves on screen, **C3–C6** at octave shift 0, 22 white
  keys. The middle C of the board is **C4** — item 27's centre note,
  and the note every row treats as its root.
- **OCT − / OCT +** shift the whole board (readout shows the shift
  and the centre C, e.g. `OCT +1 C5`), range −2…+2. The QWERTY rows
  and the painted letters shift with it.
- Mouse: press sounds the note, hold sustains it, release ends it;
  dragging across keys glissandos (each key ends as the next
  begins). Keys light in the amber hit language while sounding.
- **Velocity comes from where on the key you press**: the top of
  the key is soft (**0.25**), the bottom is hard (**1.0**), linear
  in between. (QWERTY has no height, so it plays at **0.9** — the
  same velocity the patterns' pitched lanes use.)

### (b) Routing — the selected track, pitched

Notes play the **selected track** — the one global selection from
Rounds 1–4; the tab's header names the track it will play
("Playing: …"), and it follows selection changes live.

- **Sample/drum rows** sound their sample **pitched around C4**
  (C4 = the sound at rest, ±12 = an octave), classic sampler
  behaviour. Pad rows go through the **MPC pad voice path**, so the
  pad's trims, item-101 envelope and tune shape the note exactly as
  a pad hit hears them. A kit-bound drum row pitches its kit
  sample; the unbound kick is pitch-tracked (its `triggerPitched`);
  the unbound snare / hats / clap have no pitched voice in the app
  and simply sound at their root — no fake pitch. Rows past the pad
  bank pitch their own samples the same way.
- **Synth** plays its voices at pitch (root C3); **Piano** its
  voices at pitch (root C4).
- **Note-off is real for the sustained instruments**: the synth
  voice releases when the key comes up (a held note otherwise holds
  — the gate is a 60-second backstop, not the performance), and the
  piano's off is the **damper** — its ring collapses in ~60 ms
  instead of decaying for seconds. For the one-shot voices (drums,
  samples) note-off is a no-op: a hit rings out, as samplers have
  always behaved.
- Channels that are not note instruments (Pad Bus, the audio
  tracks, Vocal, Master) take no notes and stay silent.

### (c) QWERTY — the two rows

Item 27's layout (his Reaper virtual-keyboard grammar, and FL's):
the **Z row is the lower octave** (from the board's lowest C) and
the **Q row the octave above it** — at shift 0, Z = C3 and Q = C4,
the centre. Sharps sit on the rows above each (S D · G H J · L ; and
2 3 · 5 6 7). Every playable key wears its letter on the board, and
lights when the computer key is held. Left/Right arrows shift the
octave, under the same focus rule.

**Capture rule, structural rather than policed**: key events only
reach the panel while it holds keyboard focus. Selecting the
KEYBOARD tab hands it focus; a text editor (a rename in progress,
any field) takes focus for itself and the panel never sees the
keystrokes — **typing a track name cannot fire a note**. Leaving
the tab, hiding it, or the app losing focus releases every held
note (a focus-loss handler, a visibility handler, and a 10 Hz
reconcile against physically-held keys, so a swallowed release
still ends the note within a tenth of a second). Auto-repeat never
re-triggers. A held note remembers the channel it started on, so
changing the selection mid-hold still ends it on the right channel.
**No stuck notes** — item 45's spirit, and the global STOP silences
keyboard-held voices like everything else (proven in the suite).

### (d) Step-write

A **WRITE** toggle on the tab. While it is ON and the transport
plays (pattern or song), every note played also lands — quantized
to the step sounding at that moment — in the **playing pattern**,
in the selected track's lane, using Round 4's lane model and its
own encodings (nothing new invented):

- Synth → its lane, offset from C3; Piano → its lane, offset from
  C4 (values are the lanes' semitone + 25).
- Kick / pad rows: a note **at the root (C4)** writes the row's
  **step** (Round 4 draws a row's steps as notes on its root row —
  writing the root writes the step, with the press velocity). A
  note **off the root** writes the row's **melody lane** at that
  pitch and claims the lane's source for that row — exactly the
  claim opening the piano roll on the row makes; the last pitched
  writer owns the source.
- Snare / hats / clap / own-sample rows have no pitched lane: any
  note writes the row's step.
- WRITE off, or the transport stopped: the keyboard is
  **audition-only** — it sounds and writes nothing.

**Undo grouping — one gesture per pass.** A pass opens on its first
written note and closes when the playhead wraps into the next pass,
the transport stops, WRITE is disarmed, or the track changes. A
phrase played across one pattern pass is one Ctrl-Z, however many
notes it holds. (The history's existing idle auto-close — a gesture
whose project JSON stops changing for ~0.4 s — can close a pass
early if the playing pauses mid-pass; it can never split one
pass's notes across two undo steps in a way that loses the
grouping's point, and the suite proves the continuous case.)

Step-write is its own deliberate door: live notes never start a
recording (item 56 — they are not takes), and they never pass
through the item-31 pad capture (that captures pad hits, this is a
keyboard).

### (e) No MIDI — one funnel, ready for it

There is **no MIDI in this round** (controllers are Pass 5). But
every note — mouse, QWERTY, and later MIDI — enters through a
**single funnel**: `BoomBapEngine::liveNoteOn / liveNoteOff`.
Producers push a small event into a lock-free ring; the audio
thread drains it in `process()` beside the pad hits, under the same
sample lock, firing the same voices the sequencer fires. When MIDI
arrives it will call the same two functions and inherit routing,
step-write and the stop behaviour for free.

## Item 107 — rename tracks

His words: *"on the sequencer page where all the tracks are listed,
if I right-click it, I should be able to rename that track whatever
I want, to make it more personal for the person."*

- **Right-click any track's name → Rename track...** The kit rows'
  menu and the user rows' menu both carry it now, and the pad rows
  (5–20) — which had **no name menu at all** before this round —
  get one with Rename on it.
- An **inline editor** opens on the name: **Enter commits, Esc
  cancels**, clicking away commits (the standard editor contract).
  The box is prefilled with the current custom name; the derived
  name shows as the placeholder, so clearing the box reads as what
  it does.
- The custom name lives **in the engine**, beside Round 3's derived
  names, and **overrides the file name everywhere the name is
  read** — sequencer first column, Station drop-down and board,
  multitrack chips, the roll's drop-down, the keyboard tab's
  "Playing:" readout — because every surface reads the one
  `channelDisplayName`, and the override sits inside it. Loading a
  different file onto a renamed row does **not** wash the owner's
  name away.
- It **persists in the project file** (a `"name"` key on the track
  object, written only when a name is set — unrenamed projects stay
  byte-compatible), rides the undo history like any project edit,
  and **committing an empty name clears the override** — the track
  returns to its file-derived name.
- Renaming is message-thread state; the audio path never reads
  names, so renaming mid-playback cannot disturb a sounding note.
- **Pads keep their file-name captions** (this item is tracks): the
  pad strip's names come from the pad files, not the row names, so
  nothing about the pads changes — rows 1–3 are the drum rows, not
  pads, and rename like any row.

## Regression discipline

- Item 45: STOP silences held keyboard notes (suite-measured).
- Item 56: keyboard playing never starts a recording — notes are
  not takes, and the funnel has no path to the recorder.
- Item 86: name click still only selects; the rename lives on the
  right-click menu, and the editor's commit path never moves the
  selection.
- Round 3 invariants stand: LOAD always says LOAD; derived names
  appear whenever no override is set; the file name shows only in
  the first column.
- Round 4: the roll and the keyboard agree on lanes, roots and the
  +25 encoding — step-write writes exactly what the roll draws.
- All prior suites stay green (17/17 with this round's added).

## Proof

- New suite `round5` (Round5Test): the funnel measured per track
  kind (a pad sample's pitch doubling/halving by zero-crossing rate
  at ±12; kit kick pitched; synth sustained-then-released by RMS;
  piano damper vs natural ring), non-note channels silent, the
  stop-silences-held-note regression, the rename override + clear +
  settings and project-file round-trips, engine step-write per lane
  kind, key geometry for all 37 keys, the full QWERTY table at
  three octave shifts, mouse/QWERTY convergence on one door,
  focus-loss release, and the app-level flows through the real
  MainComponent (tab discipline + pop-out/dock, rename through the
  real inline editor with undo/redo/Esc/clear, step-write quantized
  at the playhead with one undo per pass, and the desktop focus
  rules: tab holds focus, leaving the tab releases the held note,
  the rename editor owns the keyboard while typing).
- Screenshots: `round5-keyboard.png` (the tab: three octaves,
  letters on the keys, octave readout, F4 held and lit),
  `round5-renamed.png` (the sequencer with track 8 renamed "John",
  lit and selected).

## Honest limits

- Key FEEL — the press heights, the glissando, whether the velocity
  curve (0.25 at the top of the key to 1.0 at the bottom) feels
  right under a finger — is the owner's hands, not a measurement.
  The mapping is one line to retune.
- The keyboard's audio latency in the plugin is the host's buffer,
  as for the pads; nothing in this round changes that path.
- QWERTY on this build is proven under the Linux harness's real
  desktop focus; the Windows focus behaviour is the same JUCE
  machinery the app's other keyboard shortcuts already ride.
- Note-off for one-shots is intentionally a no-op (classic sampler
  behaviour) — if the owner wants key-up to choke a long sample,
  that is a per-pad choke option for a later pass, not a silent
  behaviour change here.
