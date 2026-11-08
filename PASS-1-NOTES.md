# BoomBap — Pass 1 "The Room Fits" (Nov 8, 2026)

Built and verified on Linux (ctest 5/5, same count as before — the new
checks live inside the existing engine / project-file / plugin tests).
One commit with this package carries Pass 1 AND the macOS build trial.

## What changed

1. **The window fits.** The app is landscape now (1560×900 design), opens
   clamped to your screen's usable area — never under the taskbar or off
   the top — and every window (main, Pad Bank, Piano Roll, Playlist, and
   the plugin editor in FL Studio) is resizable. Shrink it and the whole
   layout scales down to fit.
2. **Big transport.** PLAY and STOP are now large separate labelled
   buttons. Pressing PLAY while playing restarts from the top, MPC-style.
   Space still toggles play/stop.
3. **Pads.** Pad-bank cells wear the step sequencer's lighter charcoal
   gray with a slight gradient, and a pad flashes while its sound plays —
   from the sequencer or a click. The main window gained a pad-activity
   light and a pad-set meter next to Master.
4. **Double-click reset.** Double-click any slider (main window, row
   levels, pad editor) and it snaps back to its default.
5. **Metronome.** METRO button in the transport row + a Click volume
   slider. Clicks land on the beat, accented on the bar line, in time
   with the sequencer.
6. **Solo.** An S button beside every M (mute). Non-exclusive — latch
   several. Solo is saved in the project file.
7. **Autosave + crash recovery.** While you have unsaved changes, the
   project autosaves every 60 seconds to a recovery file beside the app
   settings. Save, open, or quit cleanly and it clears itself. If the app
   dies with work unsaved, the next launch offers to restore it.
   (Verified live: edit → autosave → forced crash → relaunch → Restore
   brought the edit back.)
8. **Audio device setup.** The new "Audio..." button in the top bar opens
   the device surface: device type (where the OS offers one), input and
   output device, sample rate, buffer size — wired to the real audio
   engine, and your choices are remembered across launches.

## The macOS trial (same commit)

The workflow's macOS job now pins CMake 3.31 (the macos-15 image ships a
newer one), prints the toolchain versions, and uploads the configure log
if it fails — so this run either builds the Mac version or tells us
exactly why not. Windows and Linux jobs are untouched.

## Your one action

Upload this package's contents to the GitHub repo (replacing the files)
and commit — or use the paste version of the workflow if you prefer.
Then download the Windows artifact and verdict it.
