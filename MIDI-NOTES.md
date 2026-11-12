# MIDI — what this build adds (Nov 12, 2026)

D pulled MIDI + MIDI Learn to the front of the queue ("pertinent…
ASAP"), so this is the MIDI core of BUILD-PLAN Pass 5, built on top
of Rounds 1–6 + Pass 2 + Fix Passes 1–4. FEEDBACK item 25 is the
home item; the design follows MIDI-CONTROLLERS-REFERENCE Parts 3–6.

## The MIDI window

A **MIDI...** button in the project bar (right end, beside the
window controls row) opens the MIDI window: input device picker +
"All inputs", the controller-preset menu, REC INPUT and STEP-RECORD
arms, the LEARN toggle, a live input monitor with an activity lamp,
and the full mapping list (all three layers, per-binding delete,
clear-global / clear-project). In the plugin the host owns MIDI, so
the device row is replaced by a note saying so — everything else is
the same.

## Playing (zero setup)

- **Notes 36–51 on ANY channel play pads 1–16.** That is the MPK
  Mini mk1's factory Program 1 layout, so the Mini works the moment
  it is plugged in; the other two controllers' pads land there too
  once their BoomBap presets are loaded.
- **Every other note plays the selected melodic sound** — whatever
  the main window's selection points at (a drum row plays its ghost
  lane, Synth and Piano play theirs), at the incoming velocity.
  Note-offs are tracked per note, so changing the selection
  mid-hold never strands a ringing note.

## Recording from the controller

- **REC INPUT** (arm it in the MIDI window): while the transport
  plays, pad hits write steps into the pattern that is sounding,
  and melodic notes write into the selected lane at the current
  step. Velocities map onto the pattern's own tiers — hard hit =
  accent (127), medium = normal (90), a soft hit keeps its raw
  value as a ghost note.
- **Timing is pinned to the sequencer's own clock.** The engine
  publishes the step sounding at each audio block; an event's step
  is resolved from that clock plus the event's timestamp inside
  the block (its MidiBuffer position), rounded to the nearest
  step. When a hit was *delivered* to the app is never consulted —
  that is the latency trap D flagged in other apps, designed out.
- **STEP-RECORD**: with the transport stopped, each hit/note
  writes at the step cursor and advances it one step (the cursor
  position shows in the MIDI window).

## MIDI Learn

Flip **LEARN** on in the MIDI window and every bindable control in
the main window lights up amber with its name — transport keys,
pattern buttons A–H, the mixer rows' level/mute/solo, the master
FX, drum/instrument/vocal faces, and the Station's rail when it is
open. Click a control, then wiggle a knob, fader, button or pad on
the controller: bound. A notice in the MIDI window confirms the
bind — including when the wiggle **stole** a source that was
already driving something else (one source, one control).

- CCs drive continuous controls across their full range.
  **Pickup** takeover is available per binding (the factory preset
  uses it on the Mini's knobs): the control holds still until the
  physical knob crosses its current value, then tracks — no jumps.
- Notes and buttons act as momentary/toggle as the control fits:
  Play/Stop/patterns fire, mutes/solos/metronome flip.
- **Storage is layered** (reference Part 3): the chosen factory
  preset at the bottom, your **global map** above it (app settings
  — follows you between projects), and **this project's** bindings
  on top (saved inside the .boombap file as an additive `midiLearn`
  key — older builds still open new files, and old files load with
  an empty project layer). The MIDI window's "New binds go to"
  switch picks which layer a click binds into.

## The three controller presets — read the labels

- **Akai MPK Mini mk1 — factory Program 1.** A real factory
  layout (decoded from Akai's preset files): pads ch 10 notes
  36–51, knobs K1–K8 = CC 17, 18, 19, 20, 13, 14, 15, 16 (that
  order is Akai's). K1–K4 are Kick/Snare/Hats/Pad-Bus levels
  (Pickup on), K5 Glue, K6 Dust, K7 Swing, K8 selected pan; the
  pad CC-mode banks carry Play/Stop/REC arm/Song/Metronome/
  Pattern −/+ and Patterns A–H.
- **Akai MPK49 — BoomBap-ASSIGNED.** Akai publishes **no** factory
  CC table for the MPK49, so these numbers are a scheme to load
  into the unit once (Vyzex / the unit's editor, preset name
  "BoomBap", Global ch 1, transport on the CTRL bank): Bank A =
  selected-channel EQ + drum levels 1–8, Bank B = strip dynamics
  + audio-track levels, Bank C = synth/piano + selected-pad trims
  + master/selected/pad-bus faders, transport CC 88–92.
- **Trigger Finger Pro — BoomBap-ASSIGNED.** M-Audio publishes no
  factory table either; assign the CCs once in Arsenal (Global
  ch 1): Bank A = drum rows 1–4 pan/level/mute, Bank B = pad
  bus/synth/piano/vocal, Bank C = glue/dust/swing + master faders
  + toggles, Bank D = the Station's active EQ band + pad trims.

Where the reference table names a control BoomBap does not have
yet (cue levels, count-in, a separate sample editor, glue makeup),
the CC is left **unbound** and the reason is written in the preset
code — nothing is silently mapped to a near-miss. Two documented
exceptions: the MPK49's makeup slot drives the strip **Preamp**
(gain staging is its job in this strip), and its "open editor"
switch opens the **Station**.

## Proof

New suite `Tests/MidiTest.cpp` (ctest name `midi`): routing
(pads/melodic), live record landing on the correct steps with
correct tiers at three offsets against a running 120 BPM clock,
step-record write+advance, Learn bind/drive/Pickup/steal, the JSON
round-trips (global settings + project file key), presets, and the
full stack through the real MainComponent + MIDI window (learn
wiggle by injected MidiBuffer, then the bound CC driving the Glue
slider end to end). Full ctest run: all suites green.

**Honest limit:** the build machine has no MIDI hardware, so the
device list is empty there and enumeration is exercised only as
far as "no devices" — every behaviour above is proven with
injected messages, and the real-world proof is D's three
controllers on his PC. The Trigger Finger Pro's MMC transport is
not wired yet (its CC banks are); that is a fast follow, not a
silent gap.
