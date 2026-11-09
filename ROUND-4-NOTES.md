# ROUND 4 — PIANO ROLL & PLAYLIST (Breakdown #1, items 98 + 99)

The last of the four rounds. The piano roll becomes a real editor for
any track, the playlist becomes a track-view arrangement, and PATTERN /
SONG mode moves onto the main page where it can be seen.

**Tests: ctest 16/16** (new suite `round4`: 108 checks — chord pitch
sets, stamps through the real roll, steps-as-notes editing, the
drop-down's selection sync, octave/zoom, the arrangement model,
song playback RENDERED AND MEASURED, legacy-chain conversion both at
the settings level and from a stripped legacy JSON, the ghost data
path, the playlist's real drag/trim/delete gestures, and the mode
buttons on the real main component). Artefacts verified on the bench:
standalone app 6,495,328 B; VST3 .so 7,375,312 B, dlopen OK. DSP
untouched this round — no engine audio-path changes beyond the
arrangement resolution below, and every earlier suite stays green.

## Item 98 — the Piano Roll is a real editor for any track

- **(a) Steps in the roll.** The bound track's sequencer steps draw IN
  the grid as hollow markers on the root row, under its pitched notes
  (screenshot: round4-roll.png — the two hollow cells on the root row).
  Clicking a marker clears that STEP (the note wins where a note sits
  there). Rows without a pitched lane keep the item-41 STEPS lane, now
  reachable from the same window for every row.
- **(b) Octave shift.** OCT − / OCT + move the visible/editable window
  ±12 semitones (snapped, clamped). It is a window, not a transpose:
  the same pitch is edited at its new row (proven in the suite).
- **(c) Track drop-down.** The Station's channel list lives in the
  roll's header now (rows + Synth + Piano). Switching it rebinds the
  roll AND fires the one global selection — the same single state
  Rounds 1–3 established; binding from outside moves the drop-down too.
- **(d) Zoom.** 25% / 50% / 100% presets plus mouse-wheel zoom centred
  on the cursor. The grid is a fixed canvas in a viewport; at 100% a
  cell is 30×22 px and the row lettering scales with the zoom — the
  "everything is so tiny" complaint is answered at 100% (screenshot).
- **(e) Chord + progression stamps.** 17 chord types (major, minor,
  maj7, min7, dom7, dim, aug, sus2, sus4, min9, maj9, minmaj7, dim7,
  half-dim7, add9, 6, min6) and five minor-key progressions (i–VII,
  i–iv, i–VI–III–VII, Andalusian i–VII–VI–V, iiø–V–i) in a chosen
  root. A stamp writes EDITABLE notes at the playhead (or step 0 when
  stopped) as ONE undo gesture. The lanes are monophonic, so chords
  stamp ARPEGGIATED — tones cycle up-down across the span; the pitch
  SET is exactly the chord (the suite pins the sets: A minor =
  {9,12,16}; i–VI–III–VII in C = {0,3,7} / {8,12,15} / {3,7,10} /
  {10,14,17}). Stamped notes are ordinary notes afterwards.

## Item 99a — the ghost roll: SHIPPED ON (with its toggle)

For the selected sequencer row, its pitched lane draws lightly in a
thin strip under the row's step boxes — one amber tick per step, its
height following the note. It reads the engine live, so roll edits
show in the strip without a second copy of the data.

**The verdict, with the evidence (round4-ghost-crop.png):** the first
build mapped pitch absolutely (±24 semitones squeezed into 12 px) and
the screenshot showed a FLAT line of dashes — close intervals landed
within a pixel of each other and the strip said "notes exist" and
nothing about the tune. That build was rejected on its own evidence.
The shipped strip AUTO-RANGES the lane's pitch contour to an 18 px
band: the crop now shows a C-minor arpeggio's up-down shape at a
glance — three clear levels, ticks aligned under their own step
boxes, no clutter against the rows around it. It reads clean, so it
ships **ON by default**, with the "Ghost roll" toggle in the
sequencer header (persisted in the app settings). A strip only
appears under a row that owns a pitched lane (the kick and the pad
rows); lane-less rows grow nothing.

Honest note: the contour is RELATIVE (auto-ranged), not absolute
pitch — the strip answers "what shape is the tune playing", the roll
itself answers "which notes".

## Item 99b — PATTERN / SONG on the main page

Two buttons beside the tabs: PATTERN (the current pattern loops —
today's behaviour) and SONG (playback walks the arrangement, the
audio timeline running alongside as it already does). The active one
wears the accent fill; the mode persists IN THE PROJECT (it is
project state in the engine now, round-tripped by the suite), and
undo / redo / project loads restyle the buttons from the engine via
the timer. In SONG mode the Round 2 clock shows song position, as
before — the park/seek model extends over the arrangement: a park
can sit at any song step, seeks clamp to the song's true end (even
mid-window — a 5-bar song ends at step 80, not at a window edge),
and seeks stay dropped while recording.

## Item 99c — the playlist is a track-view arrangement

The A/B chain UI is gone. In its place (screenshot:
round4-playlist.png):

- **4 lanes** over a **bar ruler**, with a **playhead** that tracks
  across in song mode (the park when stopped, the live bar while
  playing — one position, both faces).
- **Blocks** = one pattern on a lane at a bar, with a length in bars.
  A pattern is 2 bars: a longer block LOOPS the pattern inside
  itself, a shorter one trims it. Blocks MOVE by drag (bar-quantized,
  across lanes too), TRIM by edge-drag (the start trim keeps the end
  fixed), DELETE by right-click, SELECT by click — the item-58 clip
  grammar. During a drag the engine is never written; the candidate
  is painted, a placement the lane forbids simply doesn't follow the
  mouse, and mouse-up commits once = **one undo gesture per
  operation**. Blocks on one lane never overlap; lanes sound
  together. The header palette adds a 2-bar block at the song's end;
  the ruler seeks the park (item 50's rule).
- **Playback merge rule (the dedupe choice):** rows take the MAX
  velocity across the blocks sounding at a step and trigger ONCE —
  the same pattern on two lanes never double-triggers a row (the
  suite renders it: two lanes of one pattern peak within ±10% of one
  lane, not double). The monophonic melody/synth/piano lanes take
  the lowest lane's note. The render test also proves placement both
  ways: onsets at the bars a block starts its loop, silence in its
  pattern's empty bar.
- **Back-compat:** a project whose song is only the old A/B CHAIN
  converts on load into equivalent blocks on lane 1 — nothing the
  owner made is lost (chain slot i → a 2-bar block at bar 2i). The
  conversion lives in applySettings, so every load path gets it; old
  JSON files (no blocks key) read exactly as before. When an
  arrangement reduces to a plain lane-1 chain of 2-bar blocks, the
  saved file ALSO writes that chain back (a mirror), so older builds
  still read the song; anything richer writes songLength 0 rather
  than a wrong chain. The WAV export's whole-song length follows the
  arrangement's bar count now.

**NAMED SEAM — audio blocks in the playlist:** the lanes hold
PATTERNS only. A "20-bar guitar block" still lives where it lives
today — the multitrack timeline, whose clips play alongside the
arrangement in song mode as they already did. Unifying the two
timelines (audio blocks on playlist lanes) is the named next step;
this window does not pretend otherwise.

## Honest limits

- The arrangement's lanes and the multitrack timeline are two views
  of the song, not yet one (the seam above).
- The ghost strip's contour is relative by design (see 99a).
- Stamps arpeggiate because the lanes are monophonic; a polyphonic
  chord lane is a different feature, not a stamp bug.
- Feel items stay the owner's call on his machine: the roll's zoom
  ergonomics, the drag feel of blocks, and whether the ghost strip
  earns its 20 px on his screen. The toggle is one click if it
  doesn't.
- The macOS Configure failure is STILL unverified from this side —
  the workflow runs every round; the real error text still exists
  only in the auth-gated Configure-step log (one paste from D
  remains the ask, unchanged since Round 1).
