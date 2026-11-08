# BoomBap — Fix Pass Notes

Built from D's verdict on Pass 1 (FEEDBACK-V2 items 28–35) plus item 36,
which arrived while the pass was being built. Everything below was built in
the working copy, verified locally (full build + ctest), and is additive to
the project format — projects saved by earlier builds still load.

## What changed

**28 — The piano roll belongs to a track now.** Pitched rows (Kick and the
three sample rows) carry a ROLL button. Pressing it opens the piano roll
bound to that track: the window is titled for the track ("Piano Roll -
Sample 1 (pad 1)"), the lane shown is that track's own lane, and there is no
sound menu to hunt through. The project-bar Piano Roll button reopens the
roll for whichever track you summoned it from last.

**29 — One instrument at a time.** The INSTRUMENTS panel has a clear
SYNTH / PIANO switcher. Picking one shows that instrument's controls (the
other instrument's sliders leave the panel), and if the piano roll is open
it rebinds to that instrument's lane. A Piano Roll button sits right beside
the switcher, and each instrument's roll also opens from the switcher —
never via the playlist.

**30 — Content scales with the window.** Piano Roll and Playlist windows
now lay their grids out from the window's actual size: enlarge the window
and the cells, the pattern letters and the click targets grow with it.
The Pad Bank's controls and faders scale with its window too (the pads
keep their proportions).

**31 — Pad hits land in the sequencer.** While the beat plays, hitting
pad 1, 2 or 3 writes that hit — with its velocity — into the pattern at
the step the playhead is on. The step appears in the grid straight away
and the pad flashes. This is how the pad bank performs into the pattern
now. (Capture applies to the three sequencer pads; pads 4–16 stay live
performance pads.)

**32 + 35 — The vocal take gets a home: an audio track.** The top work
area now has an AUDIO / STEPS switch (top right, beside Glue). In AUDIO
view you get the multitrack: AUDIO TRACK 1 — VOCAL. When a vocal take
finishes recording it lands there automatically as a clip — waveform
visible, sitting at the bar where it was recorded — and the "Add take as
clip" button places the current take manually any time. Clips can be:

- **moved** — drag the body along the track;
- **trimmed** — drag either edge (the waveform shows the trimmed region);
- **cut** — Ctrl/Cmd-click or right-click a clip to split it at that point.

The track has its own Level slider and Mute / Solo buttons. A strip above
the lane shows the mixer running in track order — the eight instrument
rows, then the audio track — because mixer order is track order. Once a
take is placed as a clip, the clip is what plays (the loose take loop
stands down, so the audio never doubles).

**33 — The waveform panel loads again.** The sample waveform panel is
clickable: click it and the file chooser opens for the row it is showing;
its empty state says so. (Note: we checked every preserved earlier tree,
including the pre-Pass-1 backup — the panel was display-only in all of
them, so this restores the behaviour as specified rather than recovering
old code.)

**34 — Whole-song export.** The export length menu gains "Whole song":
it renders the full playlist length (a 16-bar song renders 16 bars) plus
the usual tail, even if song mode is off. 1/2/4/8-bar renders are
unchanged, and song mode still exports the whole song as before.

**36 — The mic emulation is never burned in.** A recorded take now stores
the CLEAN input — only the input-gain staging is part of the recording.
The mic-locker profile (mic type + distance character) is a playback
setting: the take, and every clip cut from it, play through whichever mic
is chosen at playback time, and switching mics after recording changes
what you hear without touching the stored audio. Live monitoring while
recording still runs through the chosen emulation, so you hear the mic
while you perform; the file just doesn't keep it. This applies to clips
too — take and clips share one playback chain.

## Project files

Additive keys only ("audioTracks", "audioClips", and the whole-song
export choice riding the existing export setting). Older .boombap files
load with the audio track at its defaults and no clips.

## Verification (local, this machine)

- Full build from the updated tree; ctest 5/5 green (engine tests now
  cover pad capture on/off/stopped/non-sequencer-pad, whole-song render
  length, clip place/move/trim/split, and the clean-take rule: identical
  stored data across opposite mic profiles, playback that changes with
  the mic while the stored take does not; project-file tests cover the
  clip round-trip and pre-fix-pass files loading with defaults).
- Xvfb launch smoke test + screenshots of each fix. One limit, stated
  plainly: this build machine has no audio device (the app's Audio
  settings show << none >>), so the live transport cannot advance here
  and pad capture is proven by the engine tests rather than by a
  screenshot of steps landing; on a machine with audio hardware the
  captured step appears in the grid as described in item 31.
