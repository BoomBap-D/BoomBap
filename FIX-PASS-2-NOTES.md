# BoomBap — Fix Pass 2 Notes

Built from D's hands-on session with the Fix Pass 1 build (FEEDBACK-V2
items 37–58, including the follow-ups that arrived while the pass was
being built). Everything below was built in the working copy, verified
locally (full build + ctest 5/5 + an extended engine test with 45 hard
checks), and is additive to the project format — projects saved by
earlier builds still load, and 16-step projects open extended to the
new 32 steps with the added steps empty.

## The headline fixes

**45 — STOP means ALL SOUND OFF.** The transport Stop now silences
everything at once: pad one-shots (including run-on mode), synth and
piano voices, the sample-waveform audition, audio-clip playback and
vocal playback. The silence lands behind a ~5 ms fade at the output so
there is no click, but perceptibly it is instant. Pressing Stop while a
vocal take is recording ends the take cleanly first — what was recorded
lands as a clip — and then everything goes quiet. This exists because a
long sample once left D no way out but killing the app, twice. Engine
tests render a 30-second one-shot and a playing clip, call stop, and
assert the output reaches silence within a small sample budget.

**56 — One press of Record = ONE take = ONE clip.** The audit found two
stacked causes for the double recording D heard. First, four different
code paths could each place the finished take as a clip, with no guard —
so one take could be placed more than once, as identical clips stacked
exactly on top of each other. Second, when a take finished, the raw-take
loop stayed armed behind the clip, so the take sounded twice. Both are
fixed: the engine now stamps every capture and every take, a take can be
placed as a clip exactly once, the raw-take loop is OFF by default after
recording, and the panel toggle is relabelled **"Loop take"** — it is an
explicit opt-in that plays the raw take with the beat, and the status
line says plainly where the take lives ("it lives on AUDIO TRACK 1 as
one clip"). A test records across the loop boundary (7 s over a 5.33 s
loop), asks for placement three times, and asserts exactly one clip,
holding the whole take, with the raw loop silent until opted in.

**58 — One clip gesture grammar, in every state.** D found split firing
once and then never again. Two causes, both fixed. The invisible one:
item 56's stacked twin clips meant a split (or delete) executed on the
top clip while its identical twin underneath still spanned the same
ground — the gesture worked and looked like nothing happened, and every
extra cut made it worse. Loading a project now heals this: active clips
with identical start, source offset and length collapse into one. The
state one: split is now checked first in the mouse handler, before any
selection or drag state exists, for both right-click and Ctrl/Cmd+click,
so no prior state can eat it. The final grammar, identical in every
state:

| Gesture | Result |
|---|---|
| Click a clip | Selects it |
| Drag the clip body | Moves the whole clip — dragging never splits or fragments it |
| Drag either edge | Trims that edge |
| Ctrl+click (either button) or right-click | Splits the clip at that point |
| Delete / Backspace key, or the DELETE CLIP button | Removes the selected clip |

## The audio view is self-sufficient (items 46–48, 50, 51, 53, 54)

**47 — Its own transport.** AUDIO MULTITRACK has its own PLAY/STOP. Its
PLAY runs the audio tracks alone — no sequencer, no beat — from its own
playhead. Its STOP is the same all-sound-off stop as item 45.

**48 — 32 steps, and audio is not loop-fenced.** Patterns are now 32
steps (2 bars) everywhere: the step grid, the per-track piano roll, and
the playlist (a playlist slot is now a 2-bar pattern, and whole-song
export follows). Old 16-step projects load with the pattern mirrored
into bar 2 so they sound as they did, and can then be edited across
both bars. Audio ignores the pattern fence entirely: playback and
recording run past the pattern length, and a take longer than the
pattern lands whole on the track and plays end to end.

**50 — Ruler seek.** Clicking the bar numbers at the top of the audio
lane parks the playhead there; playback starts from that point.

**51 — Clip delete, three ways.** The selected clip can be removed with
the Delete/Backspace key, with a visible DELETE CLIP button beside the
lane, and the same key route works while the multitrack is docked in
the main window. (This was escalated mid-pass: a track full of
unremovable fragments was blocking D's session.)

**53 — The take draws while it records.** During recording a red ghost
clip grows on the lane in real time, anchored where recording started,
fed by peaks captured from the input as it records.

**54 — Drag moves whole clips; sounding clips light up.** The drag path
was rebuilt around a selection model: a drag moves the whole clip (edge
drags trim, and only edges trim), and the clip currently sounding
renders brighter while it plays — the clip equivalent of the pad flash.

**46 — Waveform panel audition.** The main-window sample waveform panel
has its own PLAY/STOP button: PLAY auditions the loaded sample, STOP
silences it — and the global stop governs it too.

## Tabs, not a corner toggle (items 37/42, 44)

The AUDIO/STEPS corner switch is gone. The production area is a tab
bar — **STEP SEQUENCER · MPC PANEL · AUDIO MULTITRACK** — labelled,
outlined tabs, and each view can **pop out** into its own resizable
window and dock back, Reaper-style. The MPC PANEL tab holds the 16
pads plus, per item 44, the **selected pad's own waveform** with its
trim window shaded: drag the window edges (or the Start/End sliders) to
trim, click the waveform to hear the pad. The standalone Pad Bank
window shows the same content.

## Tracks and rows (items 39, 40, 41, 52)

**39 — The track wall is moved.** This was D's #1 holdback ("the thing
that's holding stuff back is the limited step tracks"). The sequencer
now holds up to 32 rows — kit rows 1–5, pad rows for all 16 pads, and
dedicated sample rows beyond them — in a vertically scrolling list,
with a **+ TRACK** button that adds a row ready for a sample. Rows,
their samples and the track count persist in the project. "Unlimited"
is implemented as a 32-row capacity with scrolling rather than a
literal infinity: it removes the wall D hit while keeping every row a
real, mixable channel.

**40 — Kit rows are changeable.** Clicking a built-in drum row's name
offers **Load sample for this row…** (the row then plays that sample
with the row's steps) and **Reset to the built-in sound**. The binding
is saved in the project; unbound rows play the synthesized kit exactly
as before.

**41 — Piano roll on every channel.** Every sequencer row has ROLL
access bound to that track. Pitched tracks behave as before; drum and
sample rows show their steps on a single-pitch lane where steps can be
added and removed, with a source selector naming the track versus its
pad-bank pad. The roll's labels were also rebuilt against the
content-scaling system — the squashed-label problem is fixed with
legible minimum label sizes.

**52 — Minimum control sizes.** MUTE/SOLO in the audio view are
full-word buttons (56×26), row controls keep minimum hit sizes, and
roll labels have a legibility floor.

## Item 57 — what was verified in the plugin build, honestly

The VST3 and the standalone share one engine, and the engine tests
verify engine behaviour headless — they are shell-agnostic, so the
behaviours above (stop-silence, one-take-one-clip, unfenced audio,
kit binding) hold in the code both shells run. What is verified for
the plugin specifically on this machine: the VST3 builds, it loads
and renders under a fake host transport, its state survives a
save/restore round trip byte for byte, and its editor opens on
screen and closes cleanly (plugin_load + editor_lifecycle tests) —
plus the dedicated plugin render test below (items 59/61/64). What
is **not** verified: behaviour inside a real DAW on real hardware.
This machine has no audio device and no DAW, so D's report that
audio is "really wonky" in hosts could not be reproduced here, and
**nothing in this pass claims to have fixed it** — it stays open,
flagged for a session on real hardware with a host.

## The plugin follow-ups (items 59–64, from D's continued testing)

**59 — The plugin opens on ONE view.** On first open in a host, the
step page and the audio page drew on top of each other until a toggle
click settled them. The editor's initial view state is now settled by
construction: the hidden tabs are added as child components (never
via addAndMakeVisible, which un-hides), one function owns tab
visibility, docking a popped-out view no longer force-shows it, and
the plugin editor explicitly settles on the step sequencer — the
standalone's default — before first paint. A headless test creates
the editor fresh and asserts exactly one production view is visible
(and it is the sequencer); the first-open screenshot shows the same.

**60 — Multiple audio tracks.** + AUDIO TRACK in the audio view adds
a lane, up to 8, each its own channel with its own Level / MUTE /
SOLO in the lane header, in mixer order (the strip above the ruler
shows AUDIO 1, 2, 3…). Clips live on any track; dragging a clip's
body to another lane moves it there; splits stay on their track. A
track header click ARMS it — the next recorded take lands on the
armed track. Every clip's track is stored in the project (older
files load as one track, clips on track 0), rendering gates per
track (mute/solo/gain are that channel's own), and the item 58
duplicate-heal now compares tracks too, so the same span on two
different tracks is a legitimate double and survives. Lanes scroll
when they outgrow the view, like the sequencer's rows. Engine tests
cover placement on the armed track, per-track mute gating, settings
persistence, and the cross-track heal case.

**61 — No ghost audio.** In Reaper, a plugin take could sound with
no visible home. The mechanism: a take restored from host state (or
an old project) was loaded with its raw loop armed and no clip
anywhere — it could play from behind the interface, unexplained.
Now any take that arrives via a project load or a host state
restore is homed on the spot: if it has no clip, it is placed as
one (item 56's exactly-once placement), and placement stands the
raw loop down; a take restored WITH its clips counts as placed for
the same reason. The rule the code now keeps everywhere: if it can
sound, it can be seen. Verified headless: a state carrying a take
WAV and no clips, opened in the plugin editor, yields exactly one
visible clip and a silent raw loop.

**62 — A known file home.** BoomBap's data home is the user's
Documents\BoomBap, with Projects / Takes / Exports inside, created
on first run in the standalone AND the plugin. A recorded take is
written as a real WAV into Takes as soon as it lands (both shells);
the .boombap project references that file. Saving a project whose
take predates the home (the old sidecar WAV beside the project)
copies the WAV into Takes and references the copy. Plugin state
parking uses the same Takes folder (never a temp directory). The
audio view's footer prints the home path: "BoomBap home:
…\Documents\BoomBap — takes land in its Takes folder".

**63 — The pad waveform shows the trim.** The pad bank's waveform
now SHADES OUT the regions outside the start/end points (the unused
parts of the sample are dimmed under a dark shade; the used region
stays bright), and the trim points carry draggable PULL BARS on the
waveform itself — amber handles with grips. The bottom Start/End
sliders remain and stay in sync (they drive the same values).

**64 — The signature functions in the plugin: MEASURED VERDICT.**
D's vision: somebody may load BoomBap "just for the Glue, just for
the Dust". So each function was measured through the plugin's own
processBlock — a known signal in, the difference out — in a new
headless test (plugin_render). All three process audio in the
plugin, for real:

| Function | Measurement | Verdict |
|---|---|---|
| Mic emulation | Monitored vocal-like input: channel off = RMS 0.0000; "Condenser Clean" RMS 0.1160; "Vintage Tube 47" RMS 0.1150; the two profiles differ by RMS 0.0356 (~31% of signal level) | **WORKS** |
| Glue | Hot repeating pad hit: off RMS 0.4423, gain reduction 0.0 dB; on (threshold −30 dB) RMS 0.1530, gain reduction −12.1 dB | **WORKS** |
| Dust | Clean 1 kHz sine: off RMS 0.3333; on (7.5 kHz) RMS 0.2930, output differs from clean by RMS 0.2065 | **WORKS** |

The routing explains the result: Glue, Dust and the mic chain all
live in the one engine render path that the plugin calls with the
host's buffers — there is no separate, bypassable plugin path. What
this does NOT settle is item 57's "wonky audio" in real hosts,
which is about host interaction on real hardware and remains
unverified here (no DAW, no audio device on this machine).

## Honest limits

- **No audio device exists on the build machine.** Live microphone
  recording, the feel of pad capture and live recording, and all
  real-host behaviour are proven at engine level (rendered buffers,
  state assertions) — never implied as live-verified. The live
  recording waveform (53) is fed by a tested peak-capture path; its
  on-screen smoothness is only provable on D's machine.
- **macOS still fails at Configure** in GitHub Actions (separate
  thread, unchanged by this pass): the error text lives in the
  Configure step's log, which only D's signed-in GitHub session can
  read. Windows and Linux are the verified builds.
- The "TRACK ORDER = MIXER ORDER" caption in the multitrack is now
  drawn right-aligned against the component's actual width (it is
  placed from the width, not a fixed coordinate); in current
  screenshots it sits at the right of the strip as intended.
- In the audio view, a lane's Level/MUTE/SOLO controls appear when
  the lane is fully scrolled into view (a half-visible lane shows
  its clips but not its buttons until it scrolls in) — deliberate,
  so controls never bleed over the footer.

## Verification summary

- ctest: **6/6** (engine, project file, plugin load, editor lifecycle,
  plugin render, recent files).
- EngineTest: **50 hard checks, 0 failures** — including stop-silence
  budgets for a one-shot and a clip, audio-beyond-loop rendering,
  ruler seek, live peak growth, kit-row binding, 32-step extension of
  a hand-written 16-step project file, the item 56 single-clip
  assertions, the item 58 duplicate-collapse heal, and the item 60
  multi-track checks (armed-track placement, per-track mute gating,
  cross-track heal).
- PluginRenderTest: **14 checks, 0 failures** — item 59 first-open
  view state, item 61 restored-take homing, and the item 64
  measurements tabled above.
- Artefacts: standalone 5,970,672 bytes; VST3 6,834,016 bytes,
  dlopen-verified.
- Screenshots: fix2-tabs-sequencer.png (tab bar, 32 steps, + TRACK),
  fix2-sequencer-scrolled.png (13 rows, scrolled), fix2-mpc-tab.png
  (pads + pad waveform with shaded-out trim regions and pull bars,
  item 63), fix2-audio-tab.png (three audio tracks, armed track 2,
  per-lane Level/MUTE/SOLO, + AUDIO TRACK, the BoomBap home printed
  in the footer), fix2-kit-menu.png (kit-row menu),
  fix2-plugin-first-open.png (the plugin editor's first open: the
  step sequencer only, item 59).
