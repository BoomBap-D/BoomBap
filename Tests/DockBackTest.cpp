// Dock-back regression test (FEEDBACK-V2 item 66).
//
// D's report: pop a Production Area view out into its own window, then
// DOCK IT BACK - the app crashed. (His session survived only because the
// Pass 1 crash recovery restored it.) The audit found the mechanism in
// MainComponent::popOutTab/dockTab: the pop-out window was given the tab
// content with setContentOwned(), so dockTab's clearContentComponent()
// DELETED the panel - which MainComponent also owns by unique_ptr - and
// dockTab then re-parented and dereferenced the corpse, with a second
// delete waiting in MainComponent's destructor. The X-button path added
// a second defect: dockTab destroyed the pop-out window synchronously
// from inside that window's own closeButtonPressed().
//
// This test drives the REAL tab code paths (the same functions the tab
// bar, the Pop out / Dock back button and the window X button call)
// through the plugin editor, which hosts the very same MainComponent as
// the standalone app - the tab system lives entirely in MainComponent.
// For each of the three views it runs repeated pop-out -> dock-back
// cycles, alternating the two dock paths, and asserts after every dock
// that the panel is the same object, re-parented back under
// MainComponent, alive and laid out. It ends by destroying the editor
// with a view still popped out - the shutdown case of the same bug.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
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

    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "66: the editor hosts the main view");
        if (main == nullptr) return 1;

        constexpr int kCycles = 10;
        for (int view = 0; view < 3; ++view)
        {
            auto* content = main->tabContentComponent (view);
            check (content != nullptr, "66: tab content exists");
            if (content == nullptr) continue;

            for (int cycle = 0; cycle < kCycles; ++cycle)
            {
                main->driveSelectTab (view);
                main->drivePopOutTab (view);
                pump (30);
                check (main->tabIsPoppedOut (view), "66: view reports popped out");
                check (main->tabContentComponent (view) == content,
                       "66: pop-out keeps the same panel object");
                check (content->getParentComponent() != nullptr
                         && content->getParentComponent() != static_cast<juce::Component*> (main),
                       "66: popped-out panel lives in its window, not in the tab bar");

                if (cycle % 2 == 0)
                {
                    // Path A: the "Dock back" button (togglePopOut -> dockTab).
                    main->driveDockTab (view);
                }
                else
                {
                    // Path B: the pop-out window's X button
                    // (closeButtonPressed -> dock). The dock is deferred one
                    // message turn so the window never dies inside its own
                    // close handler; pump until it lands.
                    main->driveClosePopOutWindow (view);
                    pump (400);
                }

                check (! main->tabIsPoppedOut (view), "66: view reports docked");
                check (main->tabContentComponent (view) == content,
                       "66: dock-back keeps the same panel object (nothing deleted or rebuilt)");
                check (content->getParentComponent() == static_cast<juce::Component*> (main),
                       "66: docked panel is a child of the main view again");
                // Poke the paths that dereferenced the corpse in the buggy
                // build: layout walks every child, the audio view refreshes.
                main->resized();
                content->repaint();
            }
            std::printf ("view %d: %d pop-out/dock-back cycles survived\n", view, kCycles);
        }

        // All three out at once, closed in reverse via the X button.
        for (int view = 0; view < 3; ++view) { main->driveSelectTab (view); main->drivePopOutTab (view); }
        pump (30);
        check (main->tabIsPoppedOut (0) && main->tabIsPoppedOut (1) && main->tabIsPoppedOut (2),
               "66: all three views can be popped out at once");
        for (int view = 2; view >= 0; --view) { main->driveClosePopOutWindow (view); pump (400); }
        check (! main->tabIsPoppedOut (0) && ! main->tabIsPoppedOut (1) && ! main->tabIsPoppedOut (2),
               "66: all three dock back from their X buttons");

        // Item 59 invariant still holds after the storm: one visible view.
        main->driveSelectTab (0);
        const int visible = (main->tabViewVisible (0) ? 1 : 0)
                          + (main->tabViewVisible (1) ? 1 : 0)
                          + (main->tabViewVisible (2) ? 1 : 0);
        check (visible == 1 && main->tabViewVisible (0),
               "66: exactly one production view visible after the cycles");

        // Shutdown with a view still popped out: the editor (and with it
        // MainComponent and its unique_ptrs) is destroyed while the MPC
        // panel sits in its window. The window must not delete it a
        // second time on the way down.
        main->driveSelectTab (1);
        main->drivePopOutTab (1);
        pump (30);
        check (main->tabIsPoppedOut (1), "66: view popped out for the shutdown case");
        std::printf ("destroying editor with a view still popped out...\n");
        ed.reset();
        std::printf ("editor destroyed cleanly with a view popped out\n");
    }
    std::printf ("processor destroyed cleanly\n");
    std::printf ("DOCKBACK-TEST-DONE failures=%d\n", failures);

   #if JUCE_LINUX
    if (wmStandInDisplay != nullptr)
        XCloseDisplay (wmStandInDisplay);
   #endif
    return failures == 0 ? 0 : 1;
}
