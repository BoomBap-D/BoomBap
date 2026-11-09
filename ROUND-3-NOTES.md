# ROUND 3 — PAD BANK & NAMING (Breakdown #1 items 88, 100, 80, 101)

Built on the Round 2 tree (ctest 14/14 baseline, re-verified before any
code changed; backup: project-pre-round3-2026-11-09). New suite
`round3` (67 checks). One older assertion was amended, on purpose and
documented in its test: Fix Pass 3's item-72 check ("all five pad-editor
controls are rotary knobs") now asserts item 80's newer law — Start/End
are sliders again, Tune/Gain/Vel Sens keep the knobs. Full suite after
the round: ctest 15/15.

## Item 88 — TRACK naming, completed: DONE

- Rows are **TRACK 1, TRACK 2, …** by default. The old "Sample N"
  label is gone everywhere.
- The naming model lives in the ENGINE now: each row carries a content
  name beside its sound. Load a sample and the track takes the file's
  name (without extension) — load a kick and the track is that kick's
  name. Kit rows 0–4 and the pad rows keep their existing identity
  rules; Synth/Piano/bus/audio/vocal/master channels are untouched.
- **The name lives in the row's FIRST COLUMN only.** The sequencer
  column, the Station drop-down, the full-board strips, the multitrack
  chips and the piano roll all read the one engine name — there is no
  second copy to drift. (Proven: the Station drop-down reads the
  loaded name in the app-level test.)
- **LOAD always says LOAD.** The row loader used to rename itself to
  the file's short name; that code path is deleted, not just unused —
  no button in the app can wear a file name. The waveform panel's LOAD
  (Round 1) and every row's "Load..." hold their text through loads,
  auditions and project reloads (all asserted).
- Deleting a row shifts its name down with the row (the same guarded,
  undoable delete as item 69/87). Unbinding a row's content returns it
  to TRACK n.
- Save/load: the name is the sample file's name, re-derived from the
  project's file references on load — the round trip is tested through
  the app's own snapshot→load loop. Consequence, stated plainly: if a
  sample file is renamed on disk, its track follows the file on next
  load. There is no separate free-text track rename in this round —
  it wasn't in the items.

## Item 88 (second half) — the name strip over the PAD BANK: DONE

- A strip of 16 name cells now runs across the top of the pad bank,
  replicating the Audio Multitrack's top strip: the pad's sample name
  (or PAD n), **PLAYING = tint + bright outline, SELECTED = solid
  fill**, in the same visual language as the multitrack chips
  (item 68). A sounding pad lights its cell in step with the pad's own
  flash (the hit-trace lighting is unchanged) and decays with it.
- A cell click selects that pad (no trigger — it's a name, like the
  multitrack chips). Long names truncate in narrow cells, same as the
  multitrack chips; the full name is on the pad caption and in the
  editor header.

## Item 100 — pad names back, bigger pads, dragging: DONE

- **Captions are back on the pads**: each loaded pad carries its
  sample's name, small, along the pad's bottom (this deliberately
  reverses the caption half of item 71; item 71's wins stand —
  numbered pads, click-the-waveform-box-to-load, the tall waveform).
- **Pads are bigger** in the same window: the grid's share grew
  (54% → 55%), the pads are squarer (aspect 1.4 → 1.25, more
  MPC-like), and the height the old wording paragraph taxed is the
  pads' now. At 1000×620 the pads measure 125×100 against the old
  formula's 123×87; at docked size 79×63 against 73×52. The test
  asserts new > old at the live window size.
- **The wording under the pads is gone.** Its content moved into the
  INFO PANEL under the editor's knobs: the selected pad's info line,
  then the guidance paragraph. The paragraph is a wrapping read-only
  text view sized by measuring the words at the live column width
  (a plain label clipped it to one line — caught in this round's own
  screenshots and fixed). At short (docked) heights the guidance hides
  and the info line remains.
- **Dragging** — gesture choice: PLAIN drag, no modifier. Click-to-
  play lives on the press; a drag is a press that travels more than
  8 px, so the two never fight, and a modifier would only hide the
  feature. The press still SOUNDS the pad — you hear the sound you're
  picking up. A ghost chip with the sample's name follows the cursor;
  the pad under it lights as the target; dropping on the source pad
  itself is a no-op; dropping elsewhere copies.
  - Pad → pad: the sample copies with an **independent settings
    snapshot** — trim, tune, envelope, everything. Two pads, same
    sample, manipulated differently is the point, and it is proven
    both ways in the tests (mutate the copy, the source stands;
    mutate the source, the copy stands). One copy = one undo gesture.
  - Main waveform → pad: click still auditions (item 89); drag the
    waveform onto any pad in either bank and the sample lands there,
    through the same loader the chooser uses (item 75's decoder
    included).

## Item 80 — Start/End back to SLIDERS: DONE

- Start % and End % are horizontal sliders with value boxes again,
  **two-way synced with the waveform's pull bars**: drag either, both
  move, and the engine's trim follows (tested in both directions).
- Tune / Gain / Vel Sens are knobs in the approved Round 2 family —
  the shared LookAndFeel draws every rotary in that family (British
  Class A × Modern Arc: skirted numbered knob, amber value arc), so
  the pad knobs carry it too; the suite now pins the membership
  (rotary style + slider form) so it can't drift back to stock.

## Item 101 — the multi-point ENVELOPE: DONE

- A new ENVELOPE lane sits directly under the pad editor's waveform,
  drawn over the trimmed region. An untouched pad shows **four flat
  default points** — and stores NOTHING: the envelope only exists
  once you touch it (no phantom shaping on pads you never edited).
- Gestures — drag a point to shape it (time clamps between its
  neighbours, so order can't tangle); **double-click adds** a point
  on the curve (up to 16); **right-click removes** one (never below
  2). Drag-off-to-remove was rejected: the lane is short, and a
  drag-off threshold would fight fine level adjustments near the top
  and bottom edges.
- It **applies to playback** as an amplitude envelope over the pad's
  trimmed window, evaluated in playback direction (a reversed pad
  hears the shape as it plays). Measured, not asserted: a pad with a
  dip to 0.1 at the window's centre renders its mid-window RMS below
  45% of the same pad flat, while the region outside the dip matches
  the flat pad within ±10%.
- Per-pad, saved with the project (an "env" point list per pad,
  written only when the pad has one), carried by undo snapshots, and
  snapshot-independent across item 100's drag copies. Amplitude only
  — loop/ping-pong semantics are out of scope, as briefed.

## Regression discipline

- Item 69 protections, item 75's WAV decoder, item 76 swipe, the
  item 66 dock battery, item 68 chip states, and the round1/round2
  suites all stay green — full ctest 15/15 (14 before + round3).
- One Fix Pass 3 assertion amended (item 72 → item 80's law), with
  the reason written into the test itself. Nothing else was relaxed.

## Screenshots (read back before this report)

- round3-padbank.png — the pop-out: strip lit (two pads sounding,
  one selected), captions on the pads, the shaped envelope, Start/
  End sliders at 8/92, the family knobs, the info panel.
- round3-editor.png — the editor close-up.
- round3-bank-docked.png — the docked bank at its shipping strip
  height: strip, captions, envelope, sliders and knobs all fit, no
  overflow.
(In ~/workspace/boombap-shots/. The JUCE splash badge overlays the
bottom-right corner in these stills — it's the transient dev-build
watermark, not layout; the guidance paragraph's last words pass
under it in the still and are fully readable in the running app.)

## Honest limits

- Drag FEEL (ghost behaviour under the Windows compositor, the real
  taskbar/window interplay) is not provable on the Linux bench —
  same caveat class as items 78/102. The owner's hands are the
  verdict; the mechanics (hit-testing, copy semantics, target
  lighting) are test-proven here.
- The strip uses the pads' own flash for activity; it does not add a
  second activity system for pads 1–3's sequencer steps beyond what
  the pads already show — a pad sounding from the sequence lights
  exactly as it did before (item 4's hit trace), now mirrored in
  its cell.

## Packages

- BoomBap-GitHub-Ready.zip — 116 files (Round 2's set + these notes
  + Tests/Round3Test.cpp), with the .github trial workflow, .gitignore
  and AUTO-BUILD.md carried over exactly as Round 2 left them.
- BoomBap-FINISHED-Source.zip — 111 files (the same tree without the
  GitHub extras).
- Both uploaded to the Drive folder 'BoomBap — Complete Project
  (Original + Finished)' with sizes verified byte-for-byte, these
  notes alongside.
