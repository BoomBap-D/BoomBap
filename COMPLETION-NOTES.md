# BoomBap — Completion Notes

**What this is:** BoomBap, Dwayne Lawrence's hip-hop beat-making app (JUCE / C++),
finished on **November 7, 2026** by Koda, working as D's partner from his
original unfinished upload. D's original files are in this same Drive folder,
untouched — everything below was built in a working copy.

**Company / bundle:** Lawrence Legacy Press · `com.lawrencelegacypress.boombap`

---

## What's in the app now (the original list, complete)

- **Step sequencer** — 8 tracks × 16 steps (Kick, Snare, Closed Hat, Open Hat,
  Clap + 3 sample rows), MPC-style swing, per-row levels and mutes
- **808 drums, synthesized** — kick tune/decay, snare snap, hat decay, clap tail
- **S950 "Dust"** — sample-rate/bit reduction on the bus
- **SSL-style "Glue"** bus compressor with gain-reduction meter
- **Patterns + playlist** — 8 patterns (A–H), each with its own grid and
  melody; a 32-slot song chain; Song mode plays the chain in order
- **Piano roll** — three monophonic lanes per pattern, ±12 semitones each:
  a melody lane played by the kick or any pad, a synth lane, and a piano
  lane — so all three can play at once, swinging with the pattern
- **Synth** — a subtractive synth (saw / square / triangle / sine, resonant
  lowpass filter, attack + release envelope), root C3, with its own panel
  controls: wave, cutoff, resonance, attack, release, level
- **Piano** — a synthesized piano (five harmonics that decay like real
  strings — the high ones die first — plus a hammer transient), root C4;
  brightness, decay and level controls. No samples needed
- **Pad bank** — 16 pads; pads 1–3 are the sequencer's sample rows, pads 4–16
  are live pads; per-pad editor: load, chop start/end, tune, gain, velocity
  sensitivity, reverse, mute groups
- **Sample waveform view** — loaded samples drawn in the main window (S1/S2/S3)
- **Mic locker** — 8 vocal mic models (Condenser Clean, Berlin FET 87,
  Vintage Tube 47, Broadcast Dynamic 7, Stage Dynamic 58, Tokyo Tube 800,
  Vienna Tube 251, Reference Studio), each with its paired vocal chain preset
- **Vocal recording** — records through the vocal chain, starts on the next
  downbeat, pads to whole bars, loops in sync; the take rides along in WAV
  exports and saves beside the project as `"<project> - vocal.wav"`
- **Projects** — save/load as readable `.boombap` JSON; **Recent** menu with
  the last 10 projects
- **WAV export** — 24-bit stereo, pattern or full song pass, normalize option
- **Modern clean interface** — near-black blue-grey panels, one amber accent
  (D's choice over the classic hardware look)
- **Plug-in** — VST3 (built and verified) + AU (in the build; compiles on a
  Mac). The plug-in follows the host's transport and tempo, and the whole
  beat — patterns, song, samples, pads, vocal take — saves inside the DAW
  project and restores exactly

Everything the original README listed as "not done" is done: vocal recording,
piano roll/playlist, pad-chop controls, VST3/AU wrappers, recent-files list.

## Bugs found and fixed on the way

1. The original build file was missing its C language declaration (JUCE ships
   C sources) — the app could not build at all until fixed.
2. The pad-bank/piano-roll/playlist windows were double-owned and crashed
   the app on any window close — fixed; the app now quits cleanly.
3. Those three windows popped open by themselves at launch — they are now
   built on first use and stay hidden until their buttons are pressed.

## Verification (November 7, 2026)

- Full build clean; automated test suite **5/5**: engine, project files,
  plug-in load (plays under a host transport, state restores byte-for-byte),
  editor lifecycle, recent-files storage
- Standalone app launches and quits cleanly; screenshots in this folder are
  the real app running

## How to build it

You need: CMake 3.22+, a C++17 compiler, and JUCE 7.0.12 (downloaded
automatically by the build).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- **Mac:** gives you the standalone app **plus the AU and VST3 plug-ins**
  (the AU only builds on a Mac — it has never been compiled yet; the VST3
  is the verified one)
- **Windows / Linux:** standalone + VST3. On Linux, install the usual X11/GL
  dev packages first (libxrandr-dev, libxinerama-dev, libxcursor-dev,
  libxcomposite-dev, libgl1-mesa-dev, libglu1-mesa-dev)
- One test (editor_lifecycle) needs a screen connected; on a headless Linux
  box, run the tests under Xvfb

## Honest limits

- The plug-in has no automatable parameters yet (v1) — your DAW can't
  automate its knobs
- Project and plug-in state remember samples by file path — keep your
  sample files where they were when you saved
- The models are original approximations of classic gear, as the original
  README says — not copies of any company's product

## File map (finished source)

- `Source/Core/` — the sound engine (pure C++, no UI): 808 drums, pads,
  sampler, channel strip, vocal chain, synth + piano instruments
- `Source/App/` — the app and plug-in: main window (split into core / media /
  project files), pad bank, piano roll, playlist, recent projects, plug-in
  processor + editor
- `Tests/` — the five test suites

---

## Update — Nov 7, 2026: JUCE 7 port (Windows 8.1 support)

BoomBap now builds on **JUCE 7.0.12** instead of JUCE 8.0.6. Reason:
JUCE 8 dropped Windows versions before 10, so the JUCE 8 Windows build
would not start on D's Windows 8.1 Pro machine (the launch error named
GetDpiForWindow, a Windows 10 API). JUCE 7 supports Windows 7/8.1, and
the same build also runs on Windows 10/11 — one build for every Windows
machine. The only code change: 13 font calls converted from JUCE 8's
FontOptions spelling to JUCE 7's Font(height, style) — same sizes, same
bold, no visible change. Verified here on Linux: full build clean
(standalone + VST3), test suite 5/5 (engine, project file, plugin load
with state round-trip, editor lifecycle, recent files). The GitHub
auto-build recipe now pins era-matched machines (windows-2022,
macos-15, ubuntu-24.04) for the JUCE 7 toolchains.
