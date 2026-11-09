# BoomBap — Pass 2 Notes: THE STATION

Built from FEEDBACK-V2 items 7, 9, 19, 20, 26, 38 and 65 — the mixer
pass. Everything below was built in the working copy, verified locally
(full build + ctest 7/7, the suite grew by the new undo test), and is
additive to the project format — projects saved by the Fix Pass 2 build
load unchanged, with every new mixer/strip value at its unity default.

## What the Station is

A new **STATION** button in the project bar opens **THE MASTER STATION**
as its own movable window (the app's established grammar — the Pad Bank,
Piano Roll and Playlist are windows too, and item 27 allows the Station
to be movable). Inside it: one hero channel strip bound to the SELECTED
channel, the full item-65 chain, long L/R meters, the transport, and a
**FULL BOARD** button that flips the same window to the complete mixing
board. Clicking a sequencer row, a pad, an audio lane or a board strip
selects its channel; the Station follows.

## 1 — The mixer model (items 20, 7, 35)

Every element now has its OWN channel, in track order: one per sequencer
row (32), the Synth (32) and Piano (33), a **PAD BUS** group channel
(34), one per audio track (35–42), the **VOCAL** channel (43) and the
**MASTER** bus (44). Pad rows run through their own strip, fader and pan
first, then sum into the PAD BUS (which has its own strip, fader and
pan) on the way to the master — item 20's every-element-a-channel rule
and item 7's one-fader-for-the-whole-pad-set intent both hold, and the
bus is where the set gets glued as a set. Every channel carries level,
pan (constant-power, centre = unity so existing mixes don't move),
mute and solo, and ALL of it persists in the project file, sparsely —
only channels changed from default are written. Solo is now ONE unified
logic: any solo anywhere silences every non-soloed channel (the old
two-domain row/audio split is gone). Channel peaks decay inside the
engine, so the Station meters and the board meters can both watch
without starving each other.

## 2 — The Master Station strip (items 19, 65, 26)

The hero strip runs item 65's chain in this order, in BoomBap's own
near-black/amber style with the lineage (SSL 4000/9000, Neve 1073,
Avalon VT-737SP, Softube British Class A) as design input only —
original approximations, no clones:

**HIGH-PASS FILTER → DE-ESSER → COMPRESSOR (with live GR bar) →
SATURATION/CHARACTER → GRAPHIC EQ → fader + pan**, with long L/R VU
meters (gradient bars + peak hold) on the right rail and per-stage
on/off toggles. Every stage is REAL DSP per channel and persists:
a preamp stage whose drive past +0.5 dB leans on the signal 1073-style,
a two-biquad high-pass, a split-band de-esser, an SSL-style compressor,
an oversampled saturator, the graphic EQ and a true peak-ceiling
limiter. On **audio and vocal channels** the chain head gains the
**PREAMP** drive stage and the chain end a **LIMITER** toggle
(item 26); the **MASTER** bus instead shows the existing **Glue**
(threshold + GR meter) and a final **LIMITER** — the mastering
channel's own distinct look is Pass 6 work and is NOT built here; the
Station's master view says so on its face. The recording path is
untouched: a take still captures the clean input (item 36) — the strip
treats monitoring and playback only.

## 3 — The graphic EQ (item 9)

The Pro-Q-grammar display: a grey spectrum analyzer (FFT of the
selected channel's post-fader signal, computed in the UI from the
engine's analyser ring) behind the TRUE response curve of the channel's
EQ. Up to 8 bands — high-pass, low shelf, peaks, high shelf, low-pass —
as draggable numbered nodes: drag moves frequency/gain (frequency/Q
for the filters), the mouse wheel trims Q, **double-click on empty
display adds a band**, double-click on a node toggles it, and the band
editor bar under the display reads out the selected band's exact
frequency/gain/Q with type buttons, ON, +BAND and DELETE. Per the
standing resolution, the display keeps double-click-to-create while
every knob and slider keeps D's double-click-to-reset rule. EQ state
persists per channel.

## 4 — Transport in the Station (item 19)

**REW · PLAY · STOP · FF** live in the Station header. PLAY and STOP
are the same transport the main window drives (one engine, two faces);
STOP is item 45's all-sound-off stop. REW restarts from the top and
returns the audio playhead to zero; FF jumps the playhead one bar
forward.

## 5 — The full mixing board (item 5 of the pass scope)

**FULL BOARD** flips the Station window to the board (the button then
reads STATION to flip back): every channel as a compact strip — name,
fader, pan knob, MUTE, SOLO, live meter — in track order, horizontally
scrollable, the bound channel outlined. Selecting a strip binds the
Station to it when you flip back. Board edits are undoable like
everything else.

## 6 — Global undo/redo (item 38)

**UNLIMITED** history over project-changing actions: step edits, clip
place/move/trim/split/delete, mixer + strip settings, row/track
structure, sample and kit loads, trims — captured as before/after
project snapshots (JUCE UndoManager underneath), so undo restores the
EXACT prior state. **Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y** work app-wide,
and **UNDO/REDO** buttons sit in the Station's transport row.
Continuous controls collapse into one history entry per gesture.
Transport, playback and pad hits never enter the history. Undoing a
clip delete restores the clip with identical geometry and does NOT
trigger item 61's auto-homing (a history apply carries its clips
explicitly) — a dedicated test proves exactly that.

## 7 — Strip presets + A/B

**Save preset / Load preset** on the Station header store a channel's
whole strip (chain + EQ + pan) as a named file in
Documents\BoomBap\Presets (the item 62 home gained a Presets folder).
Loading a preset is one undoable action. The **A/B** button snapshots
the shown strip so you can tweak and flip back to compare; A/B is a
compare, not an edit, so it never enters the undo history.

## 8 — Measured plugin parity (item 64's method, extended)

The strip DSP was measured THROUGH the plugin render path
(BoomBapProcessor::processBlock, vocal channel strip, on/off renders):

- **EQ:** +12 dB peak @ 300 Hz on a 300 Hz tone — RMS 0.1100 → 0.4027 (×3.66)
- **COMP:** threshold −20 dB, ratio 10 on a hot 220 Hz tone — RMS 0.1522 → 0.0794, strip GR −5.6 dB
- **DE-ESS:** amount 1.0 on a 7 kHz tone — RMS 0.1061 → 0.0407 (×0.38)
- **SAT:** drive 0.9 on a gentle 200 Hz tone — 3rd-harmonic ratio 0.034 → 0.135 (×3.95)
- **LIMITER:** ceiling −18 dB — peak 0.1696 → 0.1134 (the ceiling × the channel's 0.9 fader, exact)
- **MASTER LIMITER** (engine path): kick beat peak 0.6640 → 0.3413 at a −12 dB ceiling, GR −9.9 dB during the transient. (The app's fixed output drive sits after the limiter — pre-existing master character, present in both renders.)

Item 64's original measurements reproduce IDENTICALLY after the
rework — mic off RMS 0.0000 / "Condenser Clean" 0.1046 / "Vintage
Tube 47" 0.1037, profiles differ 0.0321; Glue off 0.4423 (GR 0.0) →
on 0.1530 (GR −12.1 dB); Dust off 0.3333 → on 0.2930, difference
0.2065. No regressions.

## Two bugs this pass caught in itself

- The first Station screenshots showed the VOCAL channel no matter
  what was selected: the vocal Distance/Input sliders' apply lambdas
  fired once at construction (a programmatic set, not a gesture) and
  selected the vocal channel. Selection now comes only from genuine
  user gestures (rows, pads, lanes, the MONITOR toggle, board strips),
  and a project load — including undo/redo applies — never moves it.
- The EQ analyzer's smoothing multiplied dB values, which walks a
  negative level UP toward 0 dB and filled the whole plot with phantom
  signal. The fall is now subtractive, and the plot shows the real
  spectrum (see the kick screenshot: all low end, as a kick should be).

## Honest limits

- **Item 57 (real-host behaviour) remains unverifiable on this build
  machine** — there is no DAW and no audio device here. The strip DSP
  is proven through the plugin's own render path in tests, but how the
  plugin behaves inside a real host on a real interface can only be
  judged on D's machine.
- The **macOS build still fails at Configure** in GitHub Actions
  (unchanged from Fix Pass 2; the CMake-pin theory was ruled out and
  the real error text lives in the auth-gated step log). Windows and
  Linux are the verified targets.
- The mastering channel's distinct look and mastering toolset are
  **Pass 6** by design; this pass gives the master bus Glue + limiter
  + meters only.
- A/B snapshots and the Station's open channel are session state, not
  saved in the project (presets are the persisted form).

## Verification summary

- ctest: **7/7** (engine, project file, plugin load, editor lifecycle,
  plugin render, **undo** — new, recent files). Items 36, 45, 56, 58,
  61, 64 checks all still green.
- EngineTest: **63 checks, 0 failures** — including the Pass 2 block:
  strip/pan/bus/master round-trips, channel routing, pan renders
  (hard-left silences right, centre balanced at unity), master
  limiter render.
- ProjectFileTest: **66 checks, 0 failures** — including the station
  round-trip (strip, 7-band EQ, pans, pad bus, vocal, master limiter,
  sparse defaults) and older files loading with unity defaults.
- PluginRenderTest: **23 checks, 0 failures** — the measured table
  above.
- UndoTest: **21 checks, 0 failures** — step toggle, fader move, clip
  delete (exact geometry back, no invented clip on redo), strip edit,
  one-gesture-one-entry, transport exclusion.
- Artefacts: standalone 6,257,192 bytes; VST3 7,143,960 bytes,
  dlopen-verified.
- Screenshots: pass2-station-drum.png (the Station on the KICK channel
  — live kick spectrum, EQ curve, compressor working at −8.5 dB GR,
  meters hot), pass2-station-vocal.png (VOCAL channel — PREAMP and
  LIMITER at the chain ends), pass2-eq.png (analyzer + nodes
  close-up), pass2-board.png (the full mixing board in track order),
  pass2-transport-undo.png (REW/PLAY/STOP/FF + UNDO/REDO in the
  Station header).

## PATCH — item 66 dock-back crash (Nov 8, after Pass 2)

D's first Fix Pass 2 session on his PC: pop a Production Area view
out, dock it back — crash. (The Pass 1 crash recovery restored his
session.) Two stacked defects, both in the tab pop-out code from
Fix Pass 2 — every other window in the program (pad bank, piano
roll, playlist, Station) already used the safe pattern; this one
didn't:

1. **The window owned what it only borrowed.** `popOutTab` handed
   the tab panel to its window with `setContentOwned()`. In JUCE
   that flag means "delete this when clearing/destroying", so
   `dockTab`'s `clearContentComponent()` **deleted the panel** —
   sequencer / MPC / multitrack — even though MainComponent owns
   it by `unique_ptr`. `dockTab` then re-parented and laid out the
   freed object, and MainComponent's destructor was left holding a
   second delete. Reproduced headless before the fix: the new
   regression test segfaulted on the FIRST dock-back, backtrace
   `Component::addChildComponent ← MainComponent::dockTab ← main`.
2. **The window died inside its own close handler.** On the
   X-button path, `dockTab` ran synchronously inside
   `PopOutWindow::closeButtonPressed()` and `reset()` the window
   whose member function was still on the stack.

**The fix** (MainComponent.cpp only; no user-visible behaviour
change): the pop-out window now takes the panel with
`setContentNonOwned()` — MainComponent's `unique_ptr` is the sole
owner, so detaching and destroying the window can never delete a
panel, in dock-back and in shutdown-while-popped alike;
`closeButtonPressed()` hides the window and defers the dock one
message turn via `MessageManager::callAsync`, guarded by a
`SafePointer` to the main view, so the window is only ever
destroyed from a clean stack; `dockTab` is idempotent against a
duplicate dock. `Tests/DockBackTest.cpp` (new ctest `dock_back`)
drives the real tab code paths through the plugin editor: 10
pop-out/dock-back cycles per view alternating the Dock-back button
and the X button, all three views out at once, and editor teardown
with a view still popped out — asserting after every dock that the
panel is the same object, re-parented under the main view.

**Verification:** the same test that segfaulted on the unfixed
tree passes on the fixed tree — **188 checks, 0 failures**. Full
suite on a fresh Xvfb: **ctest 8/8** (the suite grew by
`dock_back`; engine, project_file, plugin_load, editor_lifecycle,
plugin_render, undo, recent_files all still green — items 36, 45,
56, 58, 61, 64 and the Pass 2 undo/station checks included).
Artefacts: standalone 6,261,968 bytes; VST3 7,140,432 bytes,
dlopen-verified. Real-host behaviour on D's PC remains his verdict
to give, as with every pass.

---

## MICRO-FIX — items 67–68 (Nov 8)

Two small items from the same hands-on session, banked at D's word
("that's small stuff, it's just something I noticed").

### Item 67 — The docked audio view scrolls its lanes

**What D hit:** in the DOCKED AUDIO MULTITRACK view, lanes past ~3
were unreachable — he had to pop the view out and stretch the
window to see his own tracks. Popped out it worked; docked, no
scroll he could use.

**The cause** (`MultitrackComponent::resized()`): the lane
scrollbar's range model mixed two unit systems. Its limits were
set to `(contentH − viewH)` — the maximum scroll OFFSET — while
its visible range was given the full viewport height as its size.
JUCE clamps a visible range longer than the total range to the
whole total range (`Range::constrainRange`), so the thumb filled
the track, the start stayed pinned at 0, and neither a thumb drag
nor the wheel (both end in `setCurrentRangeStart`) could ever
move it. Popped out, the window is tall enough to show every lane
at once, which is why only the docked view failed.

**The fix:** the canonical model — the scrollbar's total range is
the full lane-stack height, the visible range is the viewport
height inside it, and `scrollOffset` is clamped to
`(contentH − viewH)` before being applied. The wheel handler's
clamp already used that maximum start; it is now correct. No
change to the popped-out behaviour.

### Item 68 — Up top: PLAYING gets a box, SELECTED gets its own light

The mixer-order chips in the multitrack now carry the state
lights. A chip whose channel is sounding wears a **bright outline
box** (`accentHi`, 2.2 px — the sounding-clip light of item 54,
up top) over a lifted amber tint; the **selected** channel's chip
wears a **solid accent fill** with dark text — a different
treatment, so the two states never read as the same, and a chip
that is both shows both (solid fill + the bright box over it).
"Sounding" reads the engine's per-channel peaks
(`peekChannelPeak` — they decay per block inside the engine
precisely so every reader may peek without starving another)
with a −40 dB floor; an audio chip is also lit while any clip on
its track is in the item-54 sounding mask. The selection is
mirrored, never re-derived: `MainComponent::selectChannel` — the
one funnel every selection flows through — now also pushes the
channel into the multitrack, once at construction and on every
change.

**Verification:** new ctest `micro_fix` (`Tests/MicroFixTest.cpp`)
builds the multitrack at the exact docked bounds (1528×336) with
six lanes and drives the real scrollbar: visible before scrolling,
the start lands clamped at 280 (content 456 − viewport 176) when
a huge start is requested — pre-fix it stays pinned at 0 — and
the bottom lanes' MUTE/SOLO/Level controls appear at the exact
positions the scroll predicts while the top lanes' scroll away,
and back. For item 68 it renders the strip with a kick genuinely
sounding through the engine and the snare selected, and samples
the pixels: kick chip border = the bright box, snare chip fill =
the solid accent, an idle chip = plain panel, and the two states
differ. Screenshots (docked top / docked scrolled to lanes 5–6 /
chip strip with kick playing + open hat selected) in
`boombap-shots/microfix-*.png`, staged through the real scrollbar
and live engine playback. Full suite on a fresh Xvfb: **ctest
9/9** — the suite grew by `micro_fix`; engine, project_file,
plugin_load, editor_lifecycle, plugin_render, undo, dock_back
and recent_files all still green (items 36, 45, 56, 58, 61, 64,
66 proofs intact). Artefacts: standalone 6,265,968 bytes; VST3
7,144,664 bytes, dlopen-verified. One environment note: this
session's first ctest run failed plugin_load and dock_back with
segfaults on a STALE X server (the pre-micro-fix binaries crashed
identically on it — the same failure the Fix Pass 2 notes
record); a fresh Xvfb restored 9/9, code untouched.
