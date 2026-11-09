# ROUND 2 — THE STATION REWORK (items 81, 93–97)

Round 1 baseline: ctest 13/13. Round 2 result: **full build clean, ctest 14/14**
(the 13 baseline suites plus the new `round2` suite). Backup taken before any
code moved: `project-pre-round2-2026-11-09`.

The verdict that ordered this round (item 81): the Pass 2 Station "mostly
doesn't work" in the owner's hands. The rework below is presentation and
control plumbing — **the sound did not change** (proof at the bottom).

## Item 95 — the board works now (the "doesn't work" core)

- **The vanishing thumb is killed by construction.** The old board faders were
  stock vertical sliders painted by a LookAndFeel that assumed horizontal
  geometry — grab one and the thumb painted off-strip. The board and the
  Station rail now use a purpose-built `ConsoleFader` component (not a
  Slider): the cap is painted from the same value the drag math uses, so it
  cannot vanish, jump, or disagree with the number.
- **The approved fader**: long throw, printed dB scale on the LEFT, a
  segmented LED ladder riding on the RIGHT (the strip's own meter — the old
  painted side bars are gone), amber cap with a white centre line. Taper is
  console-like (value = position^1.5; unity sits ≈76% up the travel).
  Craft call: the printed numbers are the HONEST marks for BoomBap's
  0–1.2 gain domain (+1.6, 0, −6, −12, −18, −24, −inf) — the mockup's
  +10/+5 figures were illustrative; shipped numbers tell the truth.
  Press-on-track jumps the cap under the finger; Shift-drag is fine;
  wheel steps; **double-click resets to default** (item 6 survives).
- **No more jitter.** Channel LEVELS are smoothed on the audio thread
  (~15 ms one-pole): sequencer rows, pad bus, audio tracks, vocal, master.
  A fader drag used to land as block-rate steps; now it ramps.
- **Mute/solo is ONE state, owned by the engine.** The board bug was an
  ordering bug: a strip click selected the channel first, the Station
  re-read the engine, and the refresh reset the button to the OLD value
  before the click wrote it back — the toggle cancelled itself. The order
  is now: write the engine first, select last. Sequencer rows, the Station
  strip, and the board all render the same engine state, and the audio
  path obeys it (proven by render checks: muted channel's tail is silent,
  soloing a silent channel silences a sounding one).
  Deliberate split, found by measurement: the mute/solo GATE still cuts
  instantly (a mute is a deliberate cut — Fix Pass 2's item-60 test pins
  that behaviour), while the LEVEL is what ramps. Zipper noise came from
  drags, not from gates.
- **Clicking ANYWHERE on a board channel selects it** — name included
  (the name label no longer swallows clicks). The selected channel's name
  lights in a filled box, on top of the whole-strip trace.

## Item 93 — transport, big clock, channel drop-down

- The full transport now lives on the main screen AND in the Station:
  **[⏮ TO START] [REW] [PLAY] [STOP] [FF] [⏭ TO END]**. REW/FF step one
  step; a second press within 350 ms goes a full bar; the double-press
  consumes the pair (a third press steps again). Navigation is a
  park/seek model: PLAY starts where the transport is parked (PLAY while
  playing still restarts from the top, exactly as item 2 demands); STOP
  returns the park — and the clock — to zero. Seeks clamp to the pattern
  (or the song, in song mode), and a seek requested while recording is
  dropped: a take is never moved mid-record.
- **The big clock** sits in the Station header (amber, monospaced, big).
  Click it to switch format: minutes:seconds.millis / samples /
  bars.beats.ticks. The choice persists in the app settings. Craft call:
  the main window gets a compact twin of the same clock in the project
  bar rather than crowding the transport row — same state, both faces.
- **The Station's channel name is a DROP-DOWN** listing every active
  channel in board order, at headline size. Picking one binds the strip
  without leaving the Station; selecting a channel anywhere else moves
  the drop-down too.

## Item 94 — the full chain on every channel + EQ knobs

- **Every channel shows the FULL tool set** — preamp, high-pass, de-esser,
  compressor, saturation/character, graphic EQ, limiter. No per-channel
  subsets. (The strip DSP was already uniform underneath; only the UI was
  hiding stages. Showing them changes no sound.) Tools still toggle by
  pressing their name, lit when on.
- **The graphic EQ gained its knobs**: for the selected band, Gain / Freq /
  Q knobs in the approved knob family, **colour-matched to the band's node**
  (amber peak, tinted shelves, blue HP/LP). Dragging a node moves the
  knobs; turning a knob moves the node — two-way, through the one code
  path the display itself uses. Node dragging is untouched.
- **GRAPHIC EQ on/off is an honest bypass**: switching it off stores the
  bands' on-states and flattens the strip the DSP reads; switching on
  restores them. Turning a band on by hand un-bypasses. (No new settings
  field — project files are unchanged.)
- The EQ display's double-click-adds-a-band exception (item 6) survives.

## Item 97 — rack outlines

Each tool in the chain sits in its own thin-outlined unit with a subtle
per-tool tint inside the grey/black family (saturation carries a warm
hint, the limiter a green one; the master's Glue and limiter units match).
The chain reads as separate machines in a rack, not one solid panel.

## Item 96 — presets, numbers, master VU, editor doorway

- **(a) Factory presets** ship through the existing preset mechanism:
  **Factory Vocal, Factory Drum, Factory Snare** — written into
  `Documents\BoomBap\Presets` the first time the Station opens, only if
  absent (your own edits are never overwritten). HONEST NOTE: the values
  are my craft starting points, not owner-approved settings — tweak and
  re-save under your own names; the factory files stay as a reset point.
- **(b) Numbers everywhere**: every Station control shows its numeric
  readout, thresholds and gain reduction included. The top GR readout bar
  belongs to the Station view only — **the board no longer carries it**.
- **(c) Master VU**: the master channel's Station view gains a VU meter
  (needle, VU ballistics — instant rise, ~300 ms fall, −20…+3 scale, red
  past 0, labelled VU) above its fader, in addition to the long meters.
- **(d) EDIT SAMPLE** sits beside the channel drop-down: on a drum/sample
  row it opens the pad bank with that pad selected; on Synth/Piano it
  opens the piano roll; where a channel has no editor (kit rows, audio,
  vocal, pad bus, master) the button is disabled. This is the doorway —
  the full Editor is Pass 3, as planned.

## The approved skin (item 79), as shared drawing code

- **Knob** — British Class A × Modern Arc: charcoal knurled body, amber
  value arc sweeping behind (dark-grey track), amber pointer; skirt
  numerals 0–10 appear at full size (the chain knobs are big enough) and
  drop away on small knobs (board pans, band knobs), arc + pointer stay.
  Implemented in the shared LookAndFeel, so the Station, the board, and
  later rounds inherit it — the stock JUCE rotary look is gone everywhere.
- **Meters** — segmented ("chopped") LED blocks are the house language in
  both orientations, per the meter ruling: the compressor's gain-reduction
  meter is now a HORIZONTAL segmented row under the comp (restyled from
  the smooth bar), the master Glue GR meter matches, and the fader
  ladders + long meters are segmented as approved.

## Item 81 — the verdict, answered

"Nothing works in it, a few things work but most of it doesn't." The three
concrete failures under that verdict — faders that vanished when grabbed,
mute/solo buttons that cancelled themselves, drags that sounded jittery —
are each killed at the root (above), and the rest of the round is the
Station finished to the approved picture: full chain everywhere, working
transport, the clock, the drop-down, presets, the VU. The mastering
channel's distinct LOOK stays deferred to Pass 6, as ruled — it gets the
same working fader/knobs as every channel, nothing more, this round.

## No DSP regressions — measured, not asserted

Through the plugin render path, against the Pass 2 numbers:

| measurement | Pass 2 | Round 2 |
|---|---|---|
| mic off / Condenser Clean / Vintage Tube 47 / profiles differ | 0.0000 / 0.1046 / 0.1037 / 0.0321 | identical |
| Glue off → on (GR) | 0.4423 → 0.1530 (−12.1 dB) | identical |
| Dust clean → on, difference | 0.3333 → 0.2930, diff 0.2065 | identical |
| EQ +12 dB @ 300 Hz | 0.1100 → 0.4027 (×3.66) | identical |
| Comp (thr −20, 10:1) | 0.1522 → 0.0794, GR −5.6 dB | identical |
| De-esser (1.0 @ 7 kHz) | 0.1061 → 0.0407 (×0.38) | identical |
| Saturation (drive 0.9), 3rd harmonic | 0.034 → 0.135 | identical |
| Limiter (ceiling −18 dB) | peak 0.1696 → 0.1134 | identical |

The gain smoothing initialises to the target on the first block after
prepare, so steady renders are bit-identical; only a value in motion ramps.

## Tests

New `round2` suite (41+ checks): transport skip logic (step/bar,
double-press window, pair consumption, end clamps), park/seek behaviour,
clock format conversions + click-cycle + persistence, drop-down binding
both ways, uniform chain on four channel kinds, EQ knob↔node two-way sync
+ knob colour match, mute/solo single-state across board/rail/engine +
audio-path effect, board fader drag (value agreement, cap-matches-taper,
double-click reset), board-mode GR bar hidden, factory presets exist +
load through the real path, exactly one VU and only on master. Item 66's
dock battery, items 68/86 selection states, and all Round 1 suites stay
green. **Full build clean; ctest 14/14.**

Two test-side lessons from this round, for the record (both were the
test's fault, not the app's): JUCE's `sendNotification` delivers value
changes on a later message-loop pass — programmatic knob sets in tests
now use `sendNotificationSync`; and a clock assertion must let the 30 Hz
timer actually tick before reading the readout.

## Honest limits

- **Feel is yours to judge.** The fader taper, the cap's grab behaviour,
  knob drag weight — the tests prove values, positions, and sync; only
  your hands can verdict the feel. If the taper wants reshaping, it's a
  one-line change.
- Synth/Piano channel LEVELS live inside their voices and did not get the
  new level smoothing (their mute/solo gates were already instant and
  stay so). If their faders ever feel steppy, that's the first place to
  look — say the word.
- The factory preset VALUES are starting points from my side of the desk.
- Real Windows behaviour (taskbar, window feel) is not provable on this
  Linux bench — same caveat as every round; your screen is the verdict.
- The VM was replaced mid-round and wiped the system packages again;
  they were restored and the baseline re-verified before building on it.
