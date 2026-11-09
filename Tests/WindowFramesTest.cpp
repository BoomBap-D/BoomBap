// Window-frame test (FEEDBACK-V2 items 77/78, Fix Pass 4).
//
// Item 77: every BoomBap window used to borrow the OS title bar - on the
// owner's Windows 8.1 that bar paints beige, whatever the app looks
// like. Now every window draws its own bar, painted by
// BoomBapLookAndFeel in the app's dark palette, via the one shared call
// (boombap::ui::applyAppWindowFrame) in every window's constructor.
// Item 78: the main window is always the background; every auxiliary
// window floats above it for as long as it is open (JUCE always-on-top,
// scoped to the auxiliary windows only - see the helper's note for why
// that is the mechanism JUCE/Windows offers), so opening one view can
// never bury another behind the program.
//
// What this suite proves, headlessly: for EVERY window type the app
// creates - the production-area pop-outs, the Station, the pad bank,
// the piano roll, the playlist, the audio-device setup, and a main
// window built exactly the way Main.cpp builds it - the native title
// bar is off, the app-drawn bar is in place (non-zero title-bar height,
// close button present, title text colour = the app palette colour,
// the look and feel attached is BoomBapLookAndFeel, and the bar's
// painted pixels ARE the palette's panel colour with its divider), and
// the float flag is set on the auxiliary windows and NOT set on the
// main window. It then re-runs the item-66 pop-out/dock-back battery
// (both dock paths, all three views out at once, teardown while
// popped) under the new frame path, asserting the frame state of every
// popped window on every cycle - the dock semantics must not regress.
//
// What it cannot prove: real on-screen stacking. This VM has no window
// manager and no display the owner uses; always-on-top is asserted at
// the JUCE level (the flag the peers are built from - on Windows that
// flag is what puts a window in the HWND_TOPMOST band). Whether the
// frames look right and the stacking feels right on Windows 8.1 is the
// owner's screen's verdict, not this suite's.
//
// Round 1 (items 85/102) extends this suite:
//  - Item 85: auxiliary title bars paint the LIGHTER frameBar tone with
//    an amber trace along the top edge (the main window keeps panel()),
//    and the title-bar buttons' glyphs are the bright amber accentHi -
//    asserted as painted pixels inside the close/minimise button
//    bounds, not just as colour constants.
//  - Item 102: every auxiliary window class drops
//    ComponentPeer::windowAppearsOnTaskbar from its desktop style
//    flags (the main window keeps it) - on Windows that flag is
//    exactly what decides WS_EX_APPWINDOW vs WS_EX_TOOLWINDOW, i.e.
//    whether the window takes a taskbar slot; and the minimise
//    coupling state machine is driven through the real MainComponent:
//    minimise hides every open auxiliary window, restore brings back
//    exactly those, and a window the owner closed in between is never
//    resurrected. What remains the owner's-screen-only proof: the
//    real Windows 8.1 taskbar showing one entry, and a real minimise
//    gesture - this bench has no Windows peer and no window manager.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/PadBank.h"
#include "../Source/App/PianoRoll.h"
#include "../Source/App/Playlist.h"
#include "../Source/App/StationPanel.h"
#include "../Source/App/BoomBapLookAndFeel.h"
#include <cstdio>

#if JUCE_LINUX
 #include <X11/Xlib.h>
 #include <execinfo.h>
 #include <unistd.h>
 #include <csignal>
static void crashHandler (int sig)
{
    std::fprintf (stderr, "signal %d caught, backtrace:\n", sig);
    void* frames[32];
    const int count = backtrace (frames, 32);
    backtrace_symbols_fd (frames, count, STDERR_FILENO);
    _exit (3);
}
#endif

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("[%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush (stdout);
    if (! ok) ++failures;
}

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

// The frame contract for one window. expectFloat: auxiliary windows
// float above the main window (item 78); the main window does not.
static void checkFramed (juce::DocumentWindow* w, bool expectFloat, const char* what)
{
    const juce::String prefix = juce::String (what) + ": ";
    check (w != nullptr, (prefix + "window exists").toRawUTF8());
    if (w == nullptr) return;

    check (! w->isUsingNativeTitleBar(), (prefix + "native title bar is OFF (item 77)").toRawUTF8());
    check (w->getTitleBarHeight() > 0, (prefix + "app-drawn title bar is in place (height > 0)").toRawUTF8());
    check (w->isAlwaysOnTop() == expectFloat,
           (prefix + (expectFloat ? "floats above the main window (item 78)"
                                  : "does NOT float - it is the background (item 78)")).toRawUTF8());
    check (w->getCloseButton() != nullptr, (prefix + "close button present").toRawUTF8());
    check (w->findColour (juce::DocumentWindow::textColourId) == boombap::ui::text(),
           (prefix + "title text colour is the app palette colour").toRawUTF8());
    check (dynamic_cast<BoomBapLookAndFeel*> (&w->getLookAndFeel()) != nullptr,
           (prefix + "the bar is painted by BoomBapLookAndFeel").toRawUTF8());

    // The painted bar itself: charcoal from the palette with a
    // border() divider along its bottom edge - sampled down a column
    // away from the centred title (the bar sits inside the window's
    // resize border, so scan the column rather than assume its exact
    // row). Round 1 (item 85): an AUXILIARY window's bar is the
    // lighter frameBar tone; the main window keeps panel().
    if (w->getWidth() > 40 && w->getTitleBarHeight() > 4)
    {
        auto img = w->createComponentSnapshot (w->getLocalBounds());
        const int barH = w->getTitleBarHeight();
        const auto expectedBar = expectFloat ? boombap::ui::frameBar() : boombap::ui::panel();
        check (img.getPixelAt (10, barH / 2) == expectedBar,
               (prefix + (expectFloat ? "painted title bar is the lighter frameBar tone (item 85)"
                                      : "painted title bar is the palette's panel colour")).toRawUTF8());
        bool dividerFound = false;
        for (int y = 0; y < barH + 16 && y < img.getHeight(); ++y)
            if (img.getPixelAt (10, y) == boombap::ui::border())
                dividerFound = true;
        check (dividerFound,
               (prefix + "title bar divider is the palette's border colour").toRawUTF8());

        if (expectFloat)
        {
            // Item 85, the amber trace: a bright warm pixel run along
            // the bar's top edge (accent at 0.55 alpha over frameBar
            // reads roughly R150 G108 B41).
            bool traceFound = false;
            for (int y = 0; y < 10 && y < img.getHeight() && ! traceFound; ++y)
                for (int x = 4; x < 60 && x < img.getWidth(); ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getRed() > 110 && (int) p.getRed() > (int) p.getBlue() + 40)
                        traceFound = true;
                }
            check (traceFound, (prefix + "amber trace along the bar's top edge (item 85)").toRawUTF8());

            // Item 85, the buttons: the close glyph must be painted in
            // bright amber INSIDE the close button's own bounds (the
            // stock V4 glyph was dark red - invisible on this bar).
            if (auto* close = w->getCloseButton())
            {
                const auto cb = close->getBounds().reduced (close->getWidth() / 4, close->getHeight() / 4);
                int amber = 0;
                for (int y = cb.getY(); y < cb.getBottom() && y < img.getHeight(); ++y)
                    for (int x = cb.getX(); x < cb.getRight() && x < img.getWidth(); ++x)
                    {
                        const auto p = img.getPixelAt (x, y);
                        if (p.getRed() > 200 && p.getGreen() > 140 && p.getBlue() < 130)
                            ++amber;
                    }
                check (amber > 0, (prefix + "close glyph is painted bright amber (item 85)").toRawUTF8());
            }
        }
    }
}

// Round 1 (item 102): a window takes a Windows taskbar slot iff its
// desktop style flags carry windowAppearsOnTaskbar - the peer builds
// WS_EX_APPWINDOW from it, WS_EX_TOOLWINDOW without it. Assert the
// flag state per window class; the taskbar itself is Windows-only.
static void checkTaskbarSlot (juce::DocumentWindow* w, bool expectSlot, const char* what)
{
    const juce::String prefix = juce::String (what) + ": ";
    check (w != nullptr, (prefix + "window exists").toRawUTF8());
    if (w == nullptr) return;
    const bool hasSlot = (w->getDesktopWindowStyleFlags()
                            & juce::ComponentPeer::windowAppearsOnTaskbar) != 0;
    check (hasSlot == expectSlot,
           (prefix + (expectSlot ? "keeps its Windows taskbar slot (item 102)"
                                 : "claims NO Windows taskbar slot (item 102)")).toRawUTF8());
}

int main()
{
   #if JUCE_LINUX
    std::signal (SIGSEGV, crashHandler);
    std::signal (SIGABRT, crashHandler);
    Display* wmStandInDisplay = XOpenDisplay (nullptr);
    if (wmStandInDisplay != nullptr)
    {
        for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW",
                            "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                            "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
                            "_NET_WM_STATE_HIDDEN", "_MOTIF_WM_HINTS" })
            XInternAtom (wmStandInDisplay, name, False);
    }
   #endif

    juce::ScopedJuceInitialiser_GUI init;
    BoomBapLookAndFeel testLnf; // the creation sites attach MainComponent's; same class

    //==================================================================
    // Part 1: every window class, constructed the way the app
    // constructs it (name + app background, the app look and feel
    // attached by the creation site, then shown).
    {
        PadWindow pad ("BoomBap Pad Bank", boombap::ui::bg());
        pad.setLookAndFeel (&testLnf);
        pad.centreWithSize (860, 500);
        pad.setVisible (true);
        pump (60);
        checkFramed (&pad, true, "77/78 pad bank window");
        checkTaskbarSlot (&pad, false, "102 pad bank window");

        PianoRollWindow roll ("BoomBap Piano Roll", boombap::ui::bg());
        roll.setLookAndFeel (&testLnf);
        roll.centreWithSize (900, 620);
        roll.setVisible (true);
        pump (60);
        checkFramed (&roll, true, "77/78 piano roll window");
        checkTaskbarSlot (&roll, false, "102 piano roll window");

        PlaylistWindow playlist ("BoomBap Playlist", boombap::ui::bg());
        playlist.setLookAndFeel (&testLnf);
        playlist.centreWithSize (700, 320);
        playlist.setVisible (true);
        pump (60);
        checkFramed (&playlist, true, "77/78 playlist window");
        checkTaskbarSlot (&playlist, false, "102 playlist window");

        boombap::StationWindow station ("BoomBap - The Station", boombap::ui::bg());
        station.setLookAndFeel (&testLnf);
        station.centreWithSize (1240, 660);
        station.setVisible (true);
        pump (60);
        checkFramed (&station, true, "77/78 station window");
        checkTaskbarSlot (&station, false, "102 station window");
    }
    std::printf ("part 1 done: the four editor/station window classes\n");

    //==================================================================
    // Part 2: the main window, mirrored step for step from Main.cpp's
    // MainWindow (same background, allButtons, the same frame call with
    // float=false, the app look and feel - there supplied by the
    // desktop default the application installs). MainWindow itself
    // lives in Main.cpp beside START_JUCE_APPLICATION and cannot be
    // linked into a test; the shipped window is proven by the
    // full-app screenshot taken this pass.
    {
        juce::DocumentWindow mainWin ("BoomBap", boombap::ui::bg(),
                                      juce::DocumentWindow::allButtons);
        mainWin.setLookAndFeel (&testLnf);
        boombap::ui::applyAppWindowFrame (mainWin, false);
        mainWin.setResizable (true, true);
        mainWin.centreWithSize (1200, 800);
        mainWin.setVisible (true);
        pump (60);
        checkFramed (&mainWin, false, "77/78 main window (Main.cpp mirror)");
        check (mainWin.getMinimiseButton() != nullptr, "77: main window minimise button present");
        check (mainWin.getMaximiseButton() != nullptr, "77: main window maximise button present");
        checkTaskbarSlot (&mainWin, true, "102 main window (Main.cpp mirror)");
    }
    std::printf ("part 2 done: main-window mirror\n");

    //==================================================================
    // Part 3: the real windows, opened through the real app paths in
    // the plugin editor (which hosts the very same MainComponent as
    // the standalone), plus the item-66 dock battery under the new
    // frame path.
    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "frames: the editor hosts the main view");
        if (main == nullptr) return 1;

        check (! main->isAlwaysOnTop(), "78: the main view itself never floats");

        // The Station, the pad bank, the piano roll, the playlist -
        // opened exactly as their buttons open them.
        main->openStation();
        pump (60);
        checkFramed (main->stationWindowComponent(), true, "77/78 station (app path)");

        main->driveOpenPadBank();
        pump (60);
        checkFramed (main->padWindowComponent(), true, "77/78 pad bank (app path)");

        main->driveOpenPianoRoll();
        pump (60);
        checkFramed (main->pianoWindowComponent(), true, "77/78 piano roll (app path)");

        main->driveOpenPlaylist();
        pump (60);
        checkFramed (main->playlistWindowComponent(), true, "77/78 playlist (app path)");

        // All four open at once, all floating - the owner's case:
        // opening one must not bury another.
        check (main->stationWindowComponent()->isVisible()
                 && main->padWindowComponent()->isVisible()
                 && main->pianoWindowComponent()->isVisible()
                 && main->playlistWindowComponent()->isVisible(),
               "78: station + pad bank + piano roll + playlist all open at once");

        // Round 1 (item 102): none of the app-path auxiliary windows
        // claims a taskbar slot either.
        checkTaskbarSlot (main->stationWindowComponent(), false, "102 station (app path)");
        checkTaskbarSlot (main->padWindowComponent(), false, "102 pad bank (app path)");
        checkTaskbarSlot (main->pianoWindowComponent(), false, "102 piano roll (app path)");
        checkTaskbarSlot (main->playlistWindowComponent(), false, "102 playlist (app path)");

        // Round 1 (item 102): the minimise coupling, driven at the
        // logic level through the real MainComponent (the OS-level
        // minimise gesture needs a real Windows main window - see the
        // header note). A production pop-out joins the four windows.
        main->driveSelectTab (0);
        main->drivePopOutTab (0);
        pump (30);
        checkTaskbarSlot (main->popOutWindowComponent (0), false, "102 production pop-out (app path)");
        check (main->popOutWindowComponent (0)->isVisible(), "102: pop-out open for the coupling battery");

        main->driveMinimiseCoupling (true);
        check (main->auxWindowsHiddenByMinimise() == 5,
               "102: minimising holds down all five open auxiliary windows");
        check (! main->stationWindowComponent()->isVisible()
                 && ! main->padWindowComponent()->isVisible()
                 && ! main->pianoWindowComponent()->isVisible()
                 && ! main->playlistWindowComponent()->isVisible()
                 && ! main->popOutWindowComponent (0)->isVisible(),
               "102: every auxiliary window is down with the main window");
        main->driveMinimiseCoupling (true);
        check (main->auxWindowsHiddenByMinimise() == 5,
               "102: a second minimise tick changes nothing (idempotent)");

        main->driveMinimiseCoupling (false);
        check (main->auxWindowsHiddenByMinimise() == 0
                 && main->stationWindowComponent()->isVisible()
                 && main->padWindowComponent()->isVisible()
                 && main->pianoWindowComponent()->isVisible()
                 && main->playlistWindowComponent()->isVisible()
                 && main->popOutWindowComponent (0)->isVisible(),
               "102: restoring the main window brings all five back");

        // The owner closes the pad bank himself, then minimises:
        // the coupling must not resurrect it on restore.
        main->padWindowComponent()->setVisible (false);
        main->driveMinimiseCoupling (true);
        check (main->auxWindowsHiddenByMinimise() == 4,
               "102: an owner-closed window is not held by the coupling");
        main->driveMinimiseCoupling (false);
        check (! main->padWindowComponent()->isVisible()
                 && main->stationWindowComponent()->isVisible(),
               "102: restore does not resurrect the owner-closed window");
        main->driveOpenPadBank(); // reopen for the rest of the suite
        pump (30);

        // Dock the pop-out back before the item-66 battery.
        main->driveDockTab (0);
        pump (30);
        check (! main->tabIsPoppedOut (0), "102: pop-out docked back after the coupling battery");

        // The item-66 battery under the new frame path: repeated
        // pop-out -> dock-back cycles per view, alternating the two
        // dock paths, the frame state asserted on every pop-out.
        constexpr int kCycles = 6;
        for (int view = 0; view < 3; ++view)
        {
            auto* content = main->tabContentComponent (view);
            check (content != nullptr, "66/77: tab content exists");
            if (content == nullptr) continue;

            for (int cycle = 0; cycle < kCycles; ++cycle)
            {
                main->driveSelectTab (view);
                main->drivePopOutTab (view);
                pump (30);
                check (main->tabIsPoppedOut (view), "66/77: view reports popped out");
                check (main->tabContentComponent (view) == content,
                       "66/77: pop-out keeps the same panel object");
                checkFramed (main->popOutWindowComponent (view), true,
                             "77/78 production pop-out (app path)");

                if (cycle % 2 == 0)
                {
                    main->driveDockTab (view); // path A: the Dock back button
                }
                else
                {
                    main->driveClosePopOutWindow (view); // path B: the X button (deferred dock)
                    pump (400);
                }

                check (! main->tabIsPoppedOut (view), "66/77: view reports docked");
                check (main->tabContentComponent (view) == content,
                       "66/77: dock-back keeps the same panel object (nothing deleted or rebuilt)");
                check (content->getParentComponent() == static_cast<juce::Component*> (main),
                       "66/77: docked panel is a child of the main view again");
                main->resized();
                content->repaint();
            }
            std::printf ("view %d: %d framed pop-out/dock-back cycles survived\n", view, kCycles);
        }

        // All three out at once under the new frames, closed in
        // reverse via the X buttons.
        for (int view = 0; view < 3; ++view) { main->driveSelectTab (view); main->drivePopOutTab (view); }
        pump (30);
        check (main->tabIsPoppedOut (0) && main->tabIsPoppedOut (1) && main->tabIsPoppedOut (2),
               "66/77: all three views can be popped out at once");
        check (main->popOutWindowComponent (0)->isAlwaysOnTop()
                 && main->popOutWindowComponent (1)->isAlwaysOnTop()
                 && main->popOutWindowComponent (2)->isAlwaysOnTop(),
               "78: all three pop-outs float at once - none buried behind another");
        for (int view = 2; view >= 0; --view) { main->driveClosePopOutWindow (view); pump (400); }
        check (! main->tabIsPoppedOut (0) && ! main->tabIsPoppedOut (1) && ! main->tabIsPoppedOut (2),
               "66/77: all three dock back from their X buttons");

        // Item 59 invariant still holds: one visible view.
        main->driveSelectTab (0);
        const int visible = (main->tabViewVisible (0) ? 1 : 0)
                          + (main->tabViewVisible (1) ? 1 : 0)
                          + (main->tabViewVisible (2) ? 1 : 0);
        check (visible == 1 && main->tabViewVisible (0),
               "66/77: exactly one production view visible after the cycles");

        // Shutdown with a view still popped out - the teardown case.
        main->driveSelectTab (1);
        main->drivePopOutTab (1);
        pump (30);
        check (main->tabIsPoppedOut (1), "66/77: view popped out for the shutdown case");
        std::printf ("destroying editor with a framed pop-out still open...\n");
        ed.reset();
        std::printf ("editor destroyed cleanly with a framed pop-out open\n");
    }
    std::printf ("part 3 done: app paths + dock battery under the new frames\n");

    //==================================================================
    // Part 4: the audio-device setup window - only reachable through a
    // standalone MainComponent (in a plugin the host owns the device),
    // constructed exactly as Main.cpp constructs it.
    {
        boombap::BoomBapEngine engine;
        engine.prepare (44100.0);
        MainComponent standalone (engine, true);
        standalone.driveShowAudioSettings();
        pump (120);
        checkFramed (standalone.audioSettingsWindowComponent(), true,
                     "77/78 audio device setup (app path)");
        checkTaskbarSlot (standalone.audioSettingsWindowComponent(), false,
                          "102 audio device setup (app path)");
        if (auto* w = standalone.audioSettingsWindowComponent())
        {
            w->closeButtonPressed();
            pump (60);
            check (! w->isVisible(), "77: audio setup X button still just hides the window");
        }
    }
    std::printf ("part 4 done: audio device setup window\n");

    std::printf ("WINDOWFRAMES-TEST-DONE failures=%d\n", failures);

   #if JUCE_LINUX
    if (wmStandInDisplay != nullptr)
        XCloseDisplay (wmStandInDisplay);
   #endif
    return failures == 0 ? 0 : 1;
}
