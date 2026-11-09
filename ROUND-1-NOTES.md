# BoomBap — Round 1 Notes (Breakdown #1, items 85, 102, 87, 86, 89)

Round 1 of Breakdown #1 — the quick-relief round, in the order the
owner blessed on Nov 9, 2026. Five items, all UI/window/selection
behaviour; no DSP changes. Built on Fix Pass 4 (ctest 12/12); this
round lands at **ctest 13/13, full build clean**.

## Item 85 — frame buttons visible + pop-outs don't blend in — DONE

- The title-bar buttons are BoomBap's own now (`BoomBapWindowButton`
  via `BoomBapLookAndFeel::createDocumentWindowButton`): same shapes
  as the stock JUCE buttons, but the glyphs are the app's bright
  amber (`accentHi`) drawn bolder (0.24 stroke vs stock 0.15). The
  stock V4 colours — dark red close, dark mustard minimise — were
  the root cause: near-invisible on the near-black bar. Hover fills
  the button amber with a dark glyph, so the button you are about
  to press is obvious, including the close.
- An auxiliary window's title bar is now a step lighter than the
  main window's bar (`frameBar()` = panelAlt brightened) with a
  thin amber trace along its top edge and the existing border
  divider below — a popped window reads as its own object with a
  grabbable bar. The main window keeps the original dark `panel()`
  bar: separation, not a redesign. The painter branches on a
  component property set by the one shared frame call
  (`applyAppWindowFrame`), so every current and future auxiliary
  window gets the treatment automatically.
- Proven headless in the `window_frames` suite as painted pixels:
  aux bars sample exactly `frameBar()`, the main bar still samples
  `panel()`, an amber trace pixel run is found along every aux
  bar's top edge, and bright-amber pixels are found inside every
  aux window's close-button bounds.

## Item 102 — one taskbar window + minimise coupling — DONE (mechanism), owner-screen proof outstanding

**Taskbar.** Each auxiliary window class (Station, the three
production pop-outs, Pad Bank, Piano Roll, Playlist, audio setup)
now drops `ComponentPeer::windowAppearsOnTaskbar` from its desktop
window style flags (a one-line `getDesktopWindowStyleFlags()`
override per class). This is JUCE's own switch, not a native hack:
in JUCE 7.0.12's Windows peer, a window whose flags LACK that bit
is built with `WS_EX_TOOLWINDOW` (no taskbar slot); with the bit it
gets `WS_EX_APPWINDOW`. The main window is untouched and keeps its
slot. Nothing platform-specific is hand-rolled, so the Linux bench
build is clean and the same code compiles on macOS.

**Minimise coupling.** `MainComponent` watches the main window's
minimise state on its 30 Hz timer (`pollMinimiseCoupling`, reading
the top-level peer). On minimise, every open auxiliary window is
hidden and remembered; on restore, exactly the remembered windows
come back. A window the owner closed himself is never held, and
one destroyed while held (e.g. a docked production window) simply
drops out via SafePointer. In the plugin the top-level peer is the
host's window and reports not-minimised, so the coupling is a
no-op there — item 102 is a standalone concern.

**What is proven headless vs what is not — stated plainly:**
- Proven: the style-flag state of every window class (aux = no
  taskbar bit, main = bit set), asserted in `window_frames`; and
  the coupling state machine driven through the real MainComponent
  (minimise holds all five open aux windows down; a second
  minimise tick is idempotent; restore brings back exactly those;
  an owner-closed window is not resurrected).
- NOT provable on this bench: the real Windows 8.1 taskbar showing
  exactly one BoomBap entry, and a real minimise gesture taking
  the windows down — this VM has no Windows peer and no window
  manager. Those two are the owner's screen's verdict, same as
  item 78's stacking was.

## Item 87 — obvious track delete in the sequencer — DONE

- Every sequencer row now carries a delete **X** button in the row
  itself (between the level slider and the steps — the level slider
  narrowed to make room; step geometry untouched), matching the
  audio lanes' X. It SHOWS only on user-added rows
  (`>= kFirstOwnRow`); protected rows (kit rows 0–4, pad rows
  5–20) carry no affordance at all rather than a dead button, per
  item 69's protections.
- The X runs the identical guarded, undoable `deleteSequencerRow`
  as the row menu — one action, two doors. The menu path stays.
- Proven in the new `round1` suite: X presence/absence per row
  class at the component level, and through the real app — X click
  deletes the row, one undo restores it.

## Item 86 — name click = select, synced everywhere — DONE

- Sequencer name clicks split by button: LEFT click selects the
  track and nothing else; the row menu (kit menu / row menu) moved
  to RIGHT click, and the row still selects first so the menu opens
  on the track you're looking at. LOAD remains its own button.
- The selected row's name lights with the standout treatment —
  solid accent fill, dark text (item 68's SELECTED language).
- Selection is one state: `MainComponent::selectChannel` now also
  mirrors into `SequencerPanel::setSelectedRow`, so the sequencer
  row light, the Audio Multitrack's top-strip chip, and the
  Station's bound channel are always the same selection, whichever
  view set it. A channel with no sequencer row (synth, piano, pad
  bus, vocal, master) lights no row — the app opens on the synth
  channel, so no row is lit at launch, consistently.
- Proven in `round1`: callback-level (left = select only, right =
  select + menu, pad-row names have no menu) and app-level through
  the real components in both directions (name click → multitrack
  channel + Station binding follow; `selectChannel` → row lights
  back). The popup menu's on-screen appearance is not observable
  headless; its wiring is asserted as callback counts.

## Item 89 — waveform panel + amber traces — DONE

- With a sample loaded, clicking the waveform AUDITIONS it (click
  again to stop; the global stop still silences it). An empty
  panel keeps item 74's click-to-load. The panel's PLAY button is
  now **LOAD** (label LOAD, opens the chooser) — the timer's old
  PLAY/STOP text-flip is removed, and the waveform caption reads
  "click to play - LOAD loads another".
- The thin amber outline for action buttons is a shared helper —
  `boombap::ui::applyActionTrace` (a button property the
  LookAndFeel's button painter reads) — applied to: POP OUT,
  ADD TRACK, the waveform LOAD, STATION, PAD BANK, PIANO ROLL,
  PLAYLIST and AUDIO SETUP openers. Passive readouts and tabs keep
  the grey outline. Later rounds reuse the same helper.
- Proven in `round1`: LOAD label + trace presence (and a passive
  tab's absence of it); empty click does not audition; after a real
  load, click → the audition sounds through the real engine render,
  click again → it stops, button still reads LOAD.
- One behaviour worth knowing (found by the test, not a bug):
  loading a sample leaves a "stop audition" request that the next
  rendered audio block consumes — in the app the audio thread does
  that within a block of the load, so a click can never eat it.
  The test renders a few blocks after loading to stand in for the
  running audio thread.

## Tests

- **ctest 13/13 green** (was 12/12): new suite `round1`
  (Tests/Round1Test.cpp, 41 checks) + `window_frames` expanded
  with the item 85 pixel assertions and the item 102 flag +
  coupling battery.
- Item 66's dock battery and item 68's chip states re-run inside
  `window_frames` / `micro_fix` — still green, no regressions.

## Screenshots (in ~/workspace/boombap-shots/)

- `round1-main.png` — main window: amber traces on the opener
  buttons + LOAD, "Open Hat" row lit, loaded waveform.
- `round1-sequencer.png` — sequencer scrolled to the user rows:
  "Sample 17" lit AND its X visible; X on the other user row;
  protected rows carry none.
- `round1-popout.png` — Pad Bank pop-out: separated bar, bright
  amber close glyph.

## Delivery

- `BoomBap-GitHub-Ready.zip` — 112 entries / 97 files (project
  tree minus `build/`, plus `.github/`, `AUTO-BUILD.md`,
  `.gitignore`).
- `BoomBap-FINISHED-Source.zip` — 107 entries / 94 files (same
  tree, no extras).
- Both refreshed on Drive in place; this notes file added.
