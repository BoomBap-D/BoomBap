// Plugin render test (fix pass 2 follow-ups): three verdicts the suite
// can actually measure headless -
//   item 59: a fresh plugin editor shows exactly ONE production view
//            (the step sequencer), never two drawn over each other;
//   item 61: a take arriving by host state restore gets a visible clip
//            home in the plugin, with the raw loop stood down
//            (no ghost audio);
//   item 64: the signature functions PROCESS audio in the plugin -
//            Glue, Dust and the mic emulations, measured on vs off /
//            profile vs profile through BoomBapProcessor::processBlock,
//            the host's own path. The measured numbers print, so the
//            NOTES verdict is backed by figures, not impressions.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
#include "../Source/App/ProjectFile.h"
#include <cstdio>
#include <cmath>
#include <vector>

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
    if (! ok) ++failures;
}

static double rmsOf (const std::vector<float>& v, size_t from)
{
    if (v.size() <= from) return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < v.size(); ++i) acc += (double) v[i] * v[i];
    return std::sqrt (acc / (double) (v.size() - from));
}
static double rmsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    const size_t n = std::min (a.size(), b.size());
    if (n <= from) return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < n; ++i) { const double d = (double) a[i] - b[i]; acc += d * d; }
    return std::sqrt (acc / (double) (n - from));
}

// Runs the processor the way a host does: refill the input each block,
// take the stereo output's left channel back.
template <typename Gen>
static std::vector<float> renderBlocks (BoomBapProcessor& proc, int blocks, Gen&& gen, long long& phase)
{
    std::vector<float> out;
    out.reserve ((size_t) blocks * 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float v = gen (phase++);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0);
        out.insert (out.end(), L, L + 512);
    }
    return out;
}

int main (int argc, char** argv)
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

    // ---------------- item 59: first open shows ONE view ----------------
    {
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
        check (main != nullptr, "59: the editor hosts the main view");
        if (main != nullptr)
        {
            const int visible = (main->tabViewVisible (0) ? 1 : 0)
                              + (main->tabViewVisible (1) ? 1 : 0)
                              + (main->tabViewVisible (2) ? 1 : 0);
            check (visible == 1 && main->tabViewVisible (0) && main->activeTabIndex() == 0,
                   "59: exactly one production view visible at first open (the step sequencer)");
            if (argc > 1) // optional: snapshot path for the screenshot pass
            {
                auto img = ed->createComponentSnapshot (ed->getLocalBounds());
                juce::File f (argv[1]);
                f.deleteFile();
                juce::FileOutputStream os (f);
                juce::PNGImageFormat png;
                png.writeImageToStream (img, os);
                os.flush();
            }
        }
    }

    // ---------------- item 61: a restored take gets a visible home ----------------
    {
        // a real take WAV on disk, a host state that references it but
        // carries NO clips - the ghost-audio setup exactly
        juce::File takeFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("boombap-ghost-take.wav");
        takeFile.deleteFile();
        {
            juce::AudioBuffer<float> buf (1, 22050);
            for (int i = 0; i < 22050; ++i)
                buf.setSample (0, i, 0.4f * std::sin (6.283185307179586 * 330.0 * i / 44100.0));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (
                wav.createWriterFor (new juce::FileOutputStream (takeFile), 44100.0, 1, 24, {}, 0));
            check (w != nullptr && w->writeFromAudioSampleBuffer (buf, 0, 22050),
                   "61: test take WAV written");
        }
        boombap::ProjectData d;
        d.vocalTakePath = takeFile.getFullPathName();
        const juce::String json = boombap::projectToJson (d, juce::File());

        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.setStateInformation (json.toRawUTF8(), (int) json.getNumBytesAsUTF8());
        check (proc.engine.hasTake(), "61: the restored state carries the take");
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        check (proc.engine.audioClipCount() == 1 && proc.engine.takeIsPlaced(),
               "61: opening the plugin homes the restored take as ONE visible clip");
        check (! proc.engine.takeEnabled(),
               "61: the raw take loop stands down - no invisible audio can sound");
        takeFile.deleteFile();
    }

    // ---------------- item 64: the signature functions, measured ----------------
    const double twoPi = 6.283185307179586;
    // MIC EMULATION: the monitored input path, profile vs profile
    {
        auto vocalGen = [twoPi] (long long n)
        {
            const double t = (double) n / 44100.0;
            return (float) (0.25 * std::sin (twoPi * 220.0 * t)
                          + 0.15 * std::sin (twoPi * 440.0 * t)
                          + 0.08 * std::sin (twoPi * 1320.0 * t));
        };
        BoomBapProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.engine.setGlueEnabled (false);
        proc.engine.setDustEnabled (false);
        long long phase = 0;
        proc.engine.setVocalEnabled (false);
        const auto silent = renderBlocks (proc, 48, vocalGen, phase);
        proc.engine.setVocalEnabled (true);
        proc.engine.setVocalMic (0); // Condenser Clean
        phase = 0;
        const auto profA = renderBlocks (proc, 48, vocalGen, phase);
        proc.engine.setVocalMic (2); // Vintage Tube 47
        phase = 0;
        const auto profB = renderBlocks (proc, 48, vocalGen, phase);
        const size_t skip = 12 * 512; // let the chain settle
        const double rmsOff = rmsOf (silent, skip), rmsA = rmsOf (profA, skip), rmsB = rmsOf (profB, skip);
        const double diff = rmsDiff (profA, profB, skip);
        std::printf ("MIC: monitor-off rms=%.4f, profile0 rms=%.4f, profile2 rms=%.4f, profile diff rms=%.4f\n",
                     rmsOff, rmsA, rmsB, diff);
        check (rmsOff < 0.002, "64/mic: with the vocal channel off, the input path is silent (isolation)");
        check (rmsA > 0.02 && rmsB > 0.02, "64/mic: the monitored signal passes through the mic chain");
        check (diff > 0.10 * rmsA, "64/mic: two mic profiles measure audibly different");
    }
    // GLUE: the bus compressor on a loud repeating pad hit
    {
        std::vector<float> burst (19845, 0.0f); // 0.45 s of 100 Hz
        for (size_t i = 0; i < burst.size(); ++i)
            burst[i] = 0.7f * (float) std::sin (twoPi * 100.0 * (double) i / 44100.0);
        auto silence = [] (long long) { return 0.0f; };
        auto renderGlue = [&] (bool glue, double& minGr)
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            proc.engine.setDustEnabled (false);
            proc.engine.setVocalEnabled (false);
            proc.engine.loadPadSample (15, burst, 44100.0);
            proc.engine.setGlueEnabled (glue);
            if (glue) proc.engine.setGlueThresholdDb (-30.0f);
            long long phase = 0;
            std::vector<float> all;
            minGr = 0.0;
            for (int b = 0; b < 176; ++b)
            {
                if (b % 35 == 0) proc.engine.triggerPad (15, 1.0f);
                auto chunk = renderBlocks (proc, 1, silence, phase);
                all.insert (all.end(), chunk.begin(), chunk.end());
                minGr = std::min (minGr, (double) proc.engine.getGlueReductionDb());
            }
            return rmsOf (all, 40 * 512);
        };
        double grOff = 0.0, grOn = 0.0;
        const double rmsNoGlue = renderGlue (false, grOff);
        const double rmsGlue = renderGlue (true, grOn);
        std::printf ("GLUE: off rms=%.4f (GR %.1f dB), on rms=%.4f (GR %.1f dB)\n",
                     rmsNoGlue, grOff, rmsGlue, grOn);
        check (rmsNoGlue > 0.05, "64/glue: the test signal reaches the output");
        check (rmsGlue < rmsNoGlue * 0.85, "64/glue: Glue on measures clearly lower level on a hot signal");
        check (grOn < -1.0, "64/glue: the gain-reduction meter confirms real compression");
    }
    // DUST: the S950 character on a clean sine
    {
        std::vector<float> sine (22050, 0.0f); // 0.5 s of 1 kHz
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5f * (float) std::sin (twoPi * 1000.0 * (double) i / 44100.0);
        auto silence = [] (long long) { return 0.0f; };
        auto renderDust = [&] (bool dust)
        {
            BoomBapProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            proc.engine.setGlueEnabled (false);
            proc.engine.setVocalEnabled (false);
            proc.engine.loadPadSample (15, sine, 44100.0);
            proc.engine.setDustEnabled (dust);
            if (dust) proc.engine.setDustRate (7500.0f);
            long long phase = 0;
            proc.engine.triggerPad (15, 1.0f);
            return renderBlocks (proc, 26, silence, phase);
        };
        const auto clean = renderDust (false);
        const auto dusty = renderDust (true);
        const double diff = rmsDiff (clean, dusty, 2 * 512);
        std::printf ("DUST: clean rms=%.4f, dusty rms=%.4f, difference rms=%.4f\n",
                     rmsOf (clean, 2 * 512), rmsOf (dusty, 2 * 512), diff);
        check (rmsOf (clean, 2 * 512) > 0.05, "64/dust: the test signal reaches the output");
        check (diff > 0.01, "64/dust: Dust on measures a real difference on a clean signal");
    }

    std::printf (failures == 0 ? "plugin render checks passed\n" : "PLUGIN RENDER CHECKS FAILED: %d\n", failures);

   #if JUCE_LINUX
    if (wmStandInDisplay != nullptr)
        XCloseDisplay (wmStandInDisplay);
   #endif
    return failures == 0 ? 0 : 1;
}
