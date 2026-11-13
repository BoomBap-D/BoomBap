# MIDI LEARN FIX — Notes (Nov 13, 2026)

Fix build for FEEDBACK-V2 item 147, after the hands-on verdict on run
43293762487: "It had no midi learn. I couldn't really play it." That
build only had the MIDI window's LEARN toggle; the right-click grammar
D dictated was not in it. This build adds it, proven from the running
app (screenshots listed at the end).

## What D asked for (item 147 + addenda)

- Right-click any PAD or KEY (and bindable controls) → "MIDI Learn"
  in the popup → the NEXT pad/note/control hit on the controller
  BINDS it.
- Pads themselves are learnable targets (per-pad learn is how the
  MPK Mini MK2's pads get mapped one by one).
- Incoming controller hits LIGHT the corresponding on-screen pad/key,
  always (correspondence).
- A preset entry named for his actual unit: Akai MPK Mini MK2.
- The Bank A/B hint in the MIDI window.
- The router range was NOT re-aimed: notes 36–51 on any channel
  already play BoomBap's pads, and the MK2's factory Program 1 pads
  are that same range (Bank A 36–43 = pads 1–8, Bank B 44–51 = 9–16).

## What changed

- **Pad menu:** "MIDI Learn" added to every pad's right-click menu
  (both the docked pad bank and the pop-out pad window).
- **Key menu:** right-clicking a piano key now opens a small menu
  with "MIDI Learn" for that exact key (right-click never presses
  the note).
- **Control menu:** right-clicking any bindable control in the main
  window (sliders, knobs, buttons — anything the LEARN glow covers)
  offers "MIDI Learn - <control name>".
- **One-shot arm:** choosing MIDI Learn from any of these menus arms
  learn for that one target; the next controller hit binds it and
  learn switches itself off. The MIDI window's LEARN toggle keeps
  its old stay-on behaviour for binding many controls in a row.
- **Learned notes play through the router:** a note learned onto a
  pad or key is resolved in the MIDI router's snapshot, so it plays,
  flashes, and step-records with the same engine-clock timing as the
  fixed pad notes — it does not take the slower control path. A
  control binding on the same note still wins (controls keep their
  path), matching the map's own precedence.
- **Correspondence lighting:** any incoming note now lights its
  on-screen key (fixed-range, learned, or plain melodic) for as long
  as it is held; pads flash through the engine's existing pad-flash
  polling, learned pads included.
- **New preset:** "Akai MPK Mini MK2 — factory Program 1". It ships
  with NO knob bindings, on purpose: Akai publishes no factory
  knob-CC table for the MK2 (assignments live in the Akai editor),
  so none were invented. Its setup note says so and points at
  right-click MIDI Learn; the pads need no binding at all.
- **Bank hint:** with a Mini preset selected, the MIDI window shows
  "8 physical pads - Bank A = BoomBap pads 1-8, Bank B = pads 9-16".
- **LEARN toggle text** now reads: "LEARN - click a control (or
  right-click a pad / key / control) then wiggle the controller".

## One visible side effect

The MK2 preset was inserted next to the mk1, so the preset menu is
now: 1 No preset, 2 MPK Mini mk1, 3 MPK Mini MK2, 4 Akai MPK49,
5 Trigger Finger Pro. A saved preset choice of 4 or 5 from the old
build will point one preset later than it did (the map itself is
untouched; only the factory-preset pointer shifted).

## Proof

- Full test suite: 19/19 suites green (run twice back to back). The
  midi suite is 100 checks, 0 failures, including new tests:
  learned notes route through the snapshot (per-pad trigger,
  step-record, per-key synth write, correspondence state, control
  shadowing), the right-click arm → wiggle → bound → auto-disarm
  flow end to end, the MK2 preset's exact name, and the panel's
  Bank A/B hint showing for the MK2 and hiding for the MPK49.
- Artefacts (Linux): standalone 6,937,728 bytes; VST3
  BoomBap.so 7,812,384 bytes, dlopen-verified (GetPluginFactory
  present).
- Screenshots from the running build (Xvfb harness, Shots14):
  - midilearn-midi-window.png — MIDI window: MK2 preset selected,
    setup note, Bank A/B hint, LEARN armed, input monitor live.
  - midilearn-main-armed.png — main window with LEARN armed (the
    amber glow outlines every bindable control).
  - midilearn-pad-menu.png — pad 5's right-click menu open over the
    pad bank: Load sample... / Clear pad / MIDI Learn.
  - midilearn-pad-menu-alone.png — the same menu, isolated.

## Honest limits

- No MIDI hardware exists in the build environment: everything above
  was proven with injected MIDI through the real input path and the
  running GUI. What a physical MK2 does — its actual Program/CC-mode
  state, pad bank switch, knob CCs — is still D's hands-on test.
- Two defects were caught by the screenshot proof and fixed before
  delivery: the MK2 preset name's em dash was stored mojibake (a
  JUCE const-char* decode), fixed with an explicit UTF-8 build and a
  test that now compares against one; and a test-only step-record
  failure that was the test's own track-count setup, not the engine.
- The local verification build ran on a replacement VM whose system
  packages were still restoring; the X11/GL headers came from the
  distro's own cached packages extracted into a local sysroot. The
  delivered zips are pure source, exactly as before — CI builds are
  unaffected.
