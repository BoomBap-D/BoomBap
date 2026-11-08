// Direct lifecycle test for the plugin editor, bypassing the VST3 layer:
// builds the processor and editor from the app's own sources in one process,
// then destroys them. Isolates "our editor/component teardown" from "JUCE's
// VST3 hosting teardown" when a crash shows up in the host harness.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
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

int main()
{
   #if JUCE_LINUX
    std::signal (SIGSEGV, crashHandler);
    std::signal (SIGABRT, crashHandler);
    // Window-manager atom stand-in, same as in PluginLoadTest.
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
        std::printf ("processor ready\n");

        auto* editor = proc.createEditor();
        std::printf ("editor created: %dx%d\n", editor->getWidth(), editor->getHeight());

        std::printf ("deleting editor...\n");
        delete editor;
        std::printf ("editor deleted cleanly\n");
    }
    std::printf ("processor destroyed cleanly\n");

   #if JUCE_LINUX
    if (wmStandInDisplay != nullptr)
        XCloseDisplay (wmStandInDisplay);
   #endif
    return 0;
}
