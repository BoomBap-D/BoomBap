# BoomBap — Fix Pass 4 Notes (window frames)

Built from FEEDBACK-V2 items **77 and 78** — the two window-system
items the owner raised in his hands-on session with the Station build;
they missed Fix Pass 3's package seal and led this round. Everything
below was built in the working copy, verified locally (full build
clean; **ctest 12/12** — the eleven suites that existed before this
pass, plus a new one: `window_frames`, 356 automated checks), and
changes no project data — projects saved by earlier builds load
unchanged. Four screenshots were staged and read back:
`frames-main.png`, `frames-padbank-popout.png`, `frames-station.png`,
`frames-padwindow.png` (in `~/workspace/boombap-shots/`).

## Item 77 — dark app-drawn window frames

**The defect, in the owner's words:** the frame around each pop-out
window was beige on his machine, and he wants the whole app dark. The
beige was never BoomBap's: every window borrowed the OS native title
bar (`setUsingNativeTitleBar (true)` in all seven window classes), so
Windows 8.1 painted the frames in its own theme colour.

**The fix.** Every window now draws its own title bar:

- One shared call — `boombap::ui::applyAppWindowFrame` in
  `BoomBapLookAndFeel.h` — is made in every window's constructor: the
  main window (`Main.cpp`), the Station, the three Production Area
  pop-outs, the Pad Bank, the Piano Roll, the Playlist, and the
  audio-device setup window. It switches the native title bar off and
  sets the title text colour from the app palette.
- `BoomBapLookAndFeel` gained a `drawDocumentWindowTitleBar` override
  that paints the bar in the app's existing palette — flat `panel()`
  charcoal, a 1 px `border()` divider along the bottom, the title in
  `text()` (dimmed to `muted()` when the window is inactive). The
  layout follows the stock JUCE bar (icon, centred title, room for
  the buttons), so only the colours change hands. The look and feel
  also registers `DocumentWindow::textColourId` = the palette text
  colour in its constructor.
- The standalone application now installs `BoomBapLookAndFeel` as the
  desktop default look and feel, which is what paints the main
  window's bar (its content already carried its own instance). The
  Production Area pop-out windows also now receive MainComponent's
  look and feel at creation, like every other window already did —
  they were the one window type missing it.
- Minimise / maximise / close buttons are JUCE's own title-bar
  buttons, created and wired exactly as before; drag-to-move, edge
  resize and the close behaviours (dock-back for pop-outs, hide for
  the editor windows, save-then-quit for the main window) are
  untouched.

**Item 66 did not regress.** No component ownership changed anywhere
(the pop-outs still use `setContentNonOwned`, for the documented
reason). The existing `dock_back` suite stays green, and the new
suite re-runs the whole battery under the new frame path: 6
pop-out/dock-back cycles per view alternating both dock paths, all
three views out at once closed in reverse from their X buttons, the
one-visible-view invariant, and teardown with a view still popped —
with the frame state of every popped window asserted on every cycle.

## Item 78 — pop-outs float above the main window

**The owner's rule:** the main program window is always the
background. Every pop-out stays on top of it; opening another view
must never bury an already-open pop-out behind the program; pop-outs
order among themselves by last touch.

**Mechanism chosen: JUCE always-on-top, scoped to the auxiliary
windows only.** The same shared call sets `setAlwaysOnTop (true)` on
the Station, the three production pop-outs, the Pad Bank, the Piano
Roll, the Playlist and the audio-device setup window — and
explicitly **not** on the main window, which is the background they
float over.

Why not an owned-window relationship, which was preferred: JUCE's
public API offers no way to give an app window a Win32 owner HWND.
A desktop component has no component parent (`addToDesktop` removes
a component from its parent), and the only parent-HWND path in the
peer API is for embedding into a foreign window (the plugin-in-host
case), not for app windows. So the owned relationship is not
available to build on. Always-on-top delivers the owner's rule on
Windows exactly: it places the pop-outs in the `HWND_TOPMOST` band —
above every non-topmost window, the main window included, for as
long as they are open — and windows inside the band still order
among themselves by activation, i.e. by last touch. One honest side
effect of the band: a BoomBap pop-out also floats above *other
applications'* windows while it is open. That is the price of the
only mechanism JUCE/Windows offers for this rule; closing or docking
the pop-out ends it.

In the plugin shell the same rule applies with the host's editor
window as the background: the pop-outs float above it rather than
being buried behind it.

## What the headless tests can and cannot prove

The new `window_frames` suite (356 checks) proves, for **every**
window type — each constructed or opened through the real app paths,
including the pad bank / piano roll / playlist / Station via their
real open functions in the hosted MainComponent, the audio setup
window via a standalone MainComponent exactly as `Main.cpp` builds
it, and a main window mirrored step-for-step from `Main.cpp` (which
cannot be linked into a test — it sits beside
`START_JUCE_APPLICATION`; the shipped main window itself is proven
by the `frames-main.png` screenshot, a root-window capture of the
actual standalone binary):

- the native title bar is off and the app-drawn bar is in place
  (title-bar height > 0, close button present; minimise/maximise
  additionally asserted on the main window);
- the attached look and feel is `BoomBapLookAndFeel`, the title text
  colour is the palette colour, and the *painted* bar's pixels are
  the palette's panel colour with its border-colour divider
  (component snapshots sampled per window);
- the float flag is set on every auxiliary window and not set on
  the main window or the main view;
- the item-66 dock battery passes under the new path (above).

It cannot prove real on-screen stacking: this VM's X server has no
window manager, so z-order is asserted at the JUCE level — the flag
the native peers are built from (on Windows that flag is precisely
what puts a window in the topmost band). Whether the frames look
right and the stacking feels right on the owner's Windows 8.1
screen is his verdict, as with every pass.

## Evidence

- Baseline before touching code: tree backed up to
  `~/workspace/boombap/project-pre-frames-2026-11-08`; baseline full
  build clean, ctest 11/11.
- After: full build clean (BUILD-EXIT=0); **ctest 12/12**.
- `window_frames` alone: **356 checks, 0 failures.**
- Screenshots read back by the builder: `frames-main.png` (the real
  standalone app — dark app-drawn bar, "BoomBap - Untitled", working
  min/max/close glyphs), `frames-padbank-popout.png` (the Pad Bank
  production view popped out, dark bar + close button),
  `frames-station.png` (the Station, dark bar), `frames-padwindow.png`
  (the Pad Bank editor window, dark bar). The harness log confirms
  `nativeTitleBar=0, titleBarHeight=26, alwaysOnTop=1` for each
  popped window at capture time.
- One environmental note: JUCE's `centreWithSize` dereferences the
  primary display, and this VM's Xvfb dies between sessions — test
  runs need a live X server on :99 or every window test (the
  pre-existing `dock_back` included) segfaults in JUCE display code.
  Nothing in the app changed for this; it is the known stale-X
  behaviour of this VM.
