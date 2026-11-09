// Micro-fix regression test (FEEDBACK-V2 items 67-68).
//
// Item 67: D's report - in the DOCKED audio multitrack, lanes past ~3
// were unreachable; he had to pop the view out and stretch it to see
// his own tracks. The cause sits in MultitrackComponent::resized():
// the lane scrollbar's range limits were set to (contentH - viewH) -
// the maximum scroll OFFSET - while its visible range was given the
// full viewport height as its size. JUCE clamps a visible range
// longer than the total range to the whole total range
// (Range::constrainRange), so the thumb filled the track and the
// start stayed pinned at 0: neither a thumb drag nor the wheel (both
// end in setCurrentRangeStart) could ever move it. This test builds
// the component at the exact docked bounds (1528x336 at design
// scale), adds six audio lanes, and drives the REAL scrollbar: it
// asserts the bar is visible, that requesting a huge start lands
// clamped at (contentH - viewH) instead of staying pinned at 0, and
// that the lane controls for the bottom lanes become visible at the
// exact positions the scroll predicts while the top lanes' controls
// scroll away. (Pre-fix, the start assertion fails: it reads 0.)
//
// Item 68: the mixer-order chips up top must show PLAYING as an
// outline box and SELECTED as a different, solid light. The test
// renders the component with a kick actually sounding through the
// engine (peaks staged by processing real blocks) and the snare
// channel selected, then samples the rendered pixels: the kick chip's
// top border is the bright accentHi box, the snare chip's centre is
// the solid accent fill, and a silent unselected chip keeps the idle
// panel colour.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/Multitrack.h"
#include <cstdio>
#include <cmath>

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

static bool near (juce::Colour a, juce::Colour b, int tol = 8)
{
    return std::abs ((int) a.getRed() - (int) b.getRed()) <= tol
        && std::abs ((int) a.getGreen() - (int) b.getGreen()) <= tol
        && std::abs ((int) a.getBlue() - (int) b.getBlue()) <= tol;
}

// Y positions of the visible per-lane MUTE buttons, sorted.
static std::vector<int> visibleMuteYs (juce::Component& root)
{
    std::vector<int> ys;
    for (auto* child : root.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child))
            if (b->getButtonText() == "MUTE" && b->isVisible())
                ys.push_back (b->getY());
    std::sort (ys.begin(), ys.end());
    return ys;
}

int main()
{
   #if JUCE_LINUX
    std::signal (SIGSEGV, crashHandler);
    std::signal (SIGABRT, crashHandler);
    if (Display* dpy = XOpenDisplay (nullptr))
    {
        for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW",
                            "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                            "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
                            "_NET_WM_STATE_HIDDEN", "_MOTIF_WM_HINTS" })
            XInternAtom (dpy, name, False);
    }
   #endif

    juce::ScopedJuceInitialiser_GUI init;

    boombap::BoomBapEngine engine;
    engine.prepare (44100.0);
    for (int i = 0; i < 5; ++i) engine.addAudioTrack();
    check (engine.getNumAudioTracks() == 6, "67: six audio lanes staged");

    MultitrackComponent mt (engine, [] {});
    mt.addToDesktop (0);
    mt.setBounds (0, 0, 1528, 336); // the docked bounds at design scale
    mt.setVisible (true);
    mt.refreshFromEngine();

    // ---- item 67: the docked lane scrollbar must actually scroll ----
    juce::ScrollBar* bar = nullptr;
    for (auto* child : mt.getChildren())
        if (auto* sb = dynamic_cast<juce::ScrollBar*> (child)) bar = sb;
    check (bar != nullptr, "67: the lane scrollbar exists");
    check (bar != nullptr && bar->isVisible(),
           "67: scrollbar visible when 6 lanes overflow the docked view");
    check (bar != nullptr && bar->getCurrentRangeStart() == 0.0,
           "67: scroll starts at the top");

    // lanes are 76px tall from y=102 in a 176px viewport: lanes 1-2
    // (index 0-1) are fully in view, so only their MUTE buttons show,
    // at the layout's own y positions.
    check (visibleMuteYs (mt) == std::vector<int> ({ 150, 226 }),
           "67: before scrolling, lanes 1-2 controls are the visible ones");

    if (bar != nullptr)
        bar->setCurrentRangeStart (100000.0, juce::sendNotificationSync);

    // content 456 - viewport 176 = a 280px maximum start; pre-fix the
    // clamp bug pinned this at 0 no matter what was requested.
    check (bar != nullptr && bar->getCurrentRangeStart() > 200.0,
           "67: scrollbar start moves to the clamped maximum (280)");
    // lanes 5-6 (index 4-5) are now fully in view; lanes 1-2 are gone.
    check (visibleMuteYs (mt) == std::vector<int> ({ 174, 250 }),
           "67: after scrolling, lanes 5-6 controls are the visible ones");

    if (bar != nullptr)
        bar->setCurrentRangeStart (0.0, juce::sendNotificationSync);
    check (visibleMuteYs (mt) == std::vector<int> ({ 150, 226 }),
           "67: scrolling back to the top restores lanes 1-2");

    // ---- item 68: PLAYING gets a box, SELECTED gets its own light ----
    engine.clearPattern();
    engine.setStep (boombap::BoomBapEngine::Kick, 0, 127);
    engine.setPlaying (true);
    std::vector<float> l (512), r (512);
    for (int n = 0; n < 400 && engine.peekChannelPeak (0, false) < 0.3f; ++n)
        engine.process (nullptr, l.data(), r.data(), 512);
    check (engine.peekChannelPeak (0, false) > 0.3f,
           "68: the kick channel is genuinely sounding");
    check (engine.peekChannelPeak (1, false) < 0.01f,
           "68: the snare channel is silent");
    mt.setSelectedChannel (boombap::BoomBapEngine::Snare);
    check (mt.getSelectedChannel() == boombap::BoomBapEngine::Snare,
           "68: the selection mirror holds the snare channel");

    auto img = mt.createComponentSnapshot (mt.getLocalBounds());
    const int numChips = engine.getNumTracks() + engine.getNumAudioTracks();
    const float chipW = (1496.0f - (float) (numChips - 1) * 6.0f) / (float) numChips;
    auto chipCentreX = [&] (int i) { return (int) (16.0f + i * (chipW + 6.0f) + chipW * 0.5f); };

    // chip 0 = KICK (playing): its top border is the bright outline box
    // (the 2.2px stroke is centred on the chip's top edge, y=44)
    check (near (img.getPixelAt (chipCentreX (0), 44), juce::Colour (0xffffc857), 24),
           "68: the playing chip wears the bright outline box");
    // Fill samples are taken at y=47 - inside the chip, above the
    // centred name text (sampling the centre would hit glyphs, not
    // fill) and below the top border stroke.
    // chip 1 = SNARE (selected, silent): its fill is the solid light
    check (near (img.getPixelAt (chipCentreX (1), 47), juce::Colour (0xfff5a524), 24),
           "68: the selected chip wears the solid accent light");
    // chip 2 = CLOSED HAT (idle): plain panel, neither state
    check (near (img.getPixelAt (chipCentreX (2), 47), juce::Colour (0xff1e232c), 24),
           "68: an idle chip keeps the plain panel colour");
    // the two states are visibly different treatments, not one light:
    // the playing chip's fill stays a translucent tint under its box,
    // the selected chip's fill is the solid accent
    check (! near (img.getPixelAt (chipCentreX (0), 47), img.getPixelAt (chipCentreX (1), 47), 24),
           "68: playing and selected do not render as the same state");

    engine.setPlaying (false);
    std::printf (failures == 0 ? "MICROFIX TEST PASS\n" : "MICROFIX TEST FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
