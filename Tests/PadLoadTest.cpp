// Pad-load regression test (Fix Pass 3, item 75).
//
// D's report: "The pad bank can't load a wav, which is strange."
// The pad load route is MainComponent::loadPadFile -> decodeSampleFile
// (AudioFormatManager::createReaderFor) -> engine.loadPadSample. This
// test generates the WAV variants the culture actually runs on -
// 8/16/24/32-bit PCM, 32-bit float, WAVE_FORMAT_EXTENSIBLE (PCM and
// float subformats, the DAW-export standard), BWF with a bext chunk,
// odd-sized JUNK / LIST chunks in front of fmt, an 18-byte fmt chunk,
// an uppercase .WAV name - writes them to a temp folder, and pushes
// every one through the REAL pad path (both the pad<3 route via the
// sample rows and the pad>=3 route), asserting the pad ends up
// holding the sample at full length and level.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/PluginProcessor.h"
#include "../Source/App/MainComponent.h"
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
static int checks = 0;
static void check (bool ok, const char* what)
{
    ++checks;
    std::printf ("[%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush (stdout);
    if (! ok) ++failures;
}

// ------------------------------------------------------------ WAV writer
using Bytes = std::vector<juce::uint8>;
static void putU32 (Bytes& b, juce::uint32 v) { for (int i = 0; i < 4; ++i) b.push_back ((juce::uint8) (v >> (8 * i))); }
static void putU16 (Bytes& b, juce::uint16 v) { b.push_back ((juce::uint8) v); b.push_back ((juce::uint8) (v >> 8)); }
static void putTag (Bytes& b, const char* t) { for (int i = 0; i < 4; ++i) b.push_back ((juce::uint8) t[i]); }
static void putChunk (Bytes& out, const char* id, const Bytes& payload)
{
    putTag (out, id);
    putU32 (out, (juce::uint32) payload.size());
    out.insert (out.end(), payload.begin(), payload.end());
    if (payload.size() % 2) out.push_back (0);
}
static Bytes riffWrap (const Bytes& chunks)
{
    Bytes out;
    putTag (out, "RIFF");
    putU32 (out, (juce::uint32) (4 + chunks.size()));
    putTag (out, "WAVE");
    out.insert (out.end(), chunks.begin(), chunks.end());
    return out;
}

static constexpr int kSr = 44100;
static constexpr int kLen = 4410; // 0.1 s

static std::vector<float> tone (double freq)
{
    std::vector<float> s ((size_t) kLen);
    for (int i = 0; i < kLen; ++i)
        s[(size_t) i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / kSr));
    return s;
}

// interleaved sample data for a PCM bit depth (channels: 1 or 2 tones)
static Bytes pcmData (int bits, int ch)
{
    const auto a = tone (440.0), b = tone (554.0);
    Bytes out;
    for (int i = 0; i < kLen; ++i)
        for (int c = 0; c < ch; ++c)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, (c == 0 ? a : b)[(size_t) i]);
            if (bits == 8) out.push_back ((juce::uint8) ((v * 0.5f + 0.5f) * 255.0f));
            else if (bits == 16) { putU16 (out, (juce::uint16) (juce::int16) (v * 32767.0f)); }
            else if (bits == 24) { const juce::int32 iv = (juce::int32) (v * 8388607.0f);
                                   out.push_back ((juce::uint8) iv); out.push_back ((juce::uint8) (iv >> 8)); out.push_back ((juce::uint8) (iv >> 16)); }
            else if (bits == 32) { putU32 (out, (juce::uint32) (juce::int32) (v * 2147483647.0f)); }
        }
    return out;
}
static Bytes floatData (int ch)
{
    const auto a = tone (440.0), b = tone (554.0);
    Bytes out;
    for (int i = 0; i < kLen; ++i)
        for (int c = 0; c < ch; ++c)
        {
            const float v = (c == 0 ? a : b)[(size_t) i];
            const auto* p = reinterpret_cast<const juce::uint8*> (&v);
            out.insert (out.end(), p, p + 4);
        }
    return out;
}
static Bytes fmtPcm (int bits, int ch, bool withCbSize = false)
{
    Bytes f;
    const int block = ch * ((bits + 7) / 8);
    putU16 (f, 1); putU16 (f, (juce::uint16) ch); putU32 (f, kSr);
    putU32 (f, (juce::uint32) (kSr * block)); putU16 (f, (juce::uint16) block); putU16 (f, (juce::uint16) bits);
    if (withCbSize) putU16 (f, 0);
    return f;
}
static Bytes fmtFloat (int ch)
{
    Bytes f;
    putU16 (f, 3); putU16 (f, (juce::uint16) ch); putU32 (f, kSr);
    putU32 (f, (juce::uint32) (kSr * ch * 4)); putU16 (f, (juce::uint16) (ch * 4)); putU16 (f, 32);
    return f;
}
static Bytes fmtExt (int bits, int ch, bool floatSub)
{
    Bytes f;
    const int bytesPer = floatSub ? 4 : (bits + 7) / 8;
    const int block = ch * bytesPer;
    putU16 (f, 0xFFFE); putU16 (f, (juce::uint16) ch); putU32 (f, kSr);
    putU32 (f, (juce::uint32) (kSr * block)); putU16 (f, (juce::uint16) block); putU16 (f, (juce::uint16) bits);
    putU16 (f, 22);                      // cbSize
    putU16 (f, (juce::uint16) bits);     // valid bits
    putU32 (f, ch == 1 ? 0x4u : 0x3u);   // channel mask
    // SubFormat GUID: KSDATAFORMAT_SUBTYPE_PCM (01) / IEEE_FLOAT (03),
    // {xxxxxxxx-0000-0010-8000-00aa00389b71} little-endian fields.
    putU32 (f, floatSub ? 3u : 1u); putU16 (f, 0); putU16 (f, 0x10);
    const juce::uint8 tail[8] = { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
    f.insert (f.end(), tail, tail + 8);
    return f;
}
static Bytes joinChunks (std::initializer_list<Bytes> cs)
{
    Bytes out;
    for (auto& c : cs) out.insert (out.end(), c.begin(), c.end());
    return out;
}
static Bytes chunkOf (const char* id, const Bytes& payload)
{
    Bytes out; putChunk (out, id, payload); return out;
}

static juce::File writeWav (const juce::File& dir, const juce::String& name, const Bytes& riff)
{
    const auto f = dir.getChildFile (name);
    f.replaceWithData (riff.data(), riff.size());
    return f;
}

// ------------------------------------------------------------ the probe
int main()
{
#if JUCE_LINUX
    std::signal (SIGSEGV, crashHandler);
    std::signal (SIGABRT, crashHandler);
    Display* dpy = XOpenDisplay (nullptr);
    if (dpy != nullptr)
        for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW",
                            "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                            "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
                            "_NET_WM_STATE_HIDDEN", "_MOTIF_WM_HINTS" })
            XInternAtom (dpy, name, False);
#endif

    juce::ScopedJuceInitialiser_GUI init;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("boombap-padload");
    dir.deleteRecursively();
    dir.createDirectory();

    struct Variant { juce::String name; Bytes riff; };
    const Bytes bext = chunkOf ("bext", Bytes (352, 0));
    const Bytes junk = chunkOf ("JUNK", Bytes (7, 0x31));
    const Bytes list = chunkOf ("LIST", [] { Bytes p; putTag (p, "INFO"); const Bytes inam = chunkOf ("INAM", Bytes { 'p', 'r', 'o', 'b', 'e', 0 }); p.insert (p.end(), inam.begin(), inam.end()); return p; }());
    auto wav = [] (Bytes fmt, Bytes data) { return riffWrap (joinChunks ({ chunkOf ("fmt ", fmt), chunkOf ("data", data) })); };

    std::vector<Variant> variants = {
        { "pcm16_stereo.wav",   wav (fmtPcm (16, 2), pcmData (16, 2)) },
        { "pcm16_mono.wav",     wav (fmtPcm (16, 1), pcmData (16, 1)) },
        { "pcm24_stereo.wav",   wav (fmtPcm (24, 2), pcmData (24, 2)) },
        { "pcm32int_stereo.wav",wav (fmtPcm (32, 2), pcmData (32, 2)) },
        { "pcm8_mono.wav",      wav (fmtPcm (8, 1),  pcmData (8, 1)) },
        { "float32_stereo.wav", wav (fmtFloat (2),   floatData (2)) },
        { "ext16_stereo.wav",   wav (fmtExt (16, 2, false), pcmData (16, 2)) },
        { "ext24_stereo.wav",   wav (fmtExt (24, 2, false), pcmData (24, 2)) },
        { "extfloat_stereo.wav",wav (fmtExt (32, 2, true),  floatData (2)) },
        { "bwf16_stereo.wav",   riffWrap (joinChunks ({ bext, chunkOf ("fmt ", fmtPcm (16, 2)), chunkOf ("data", pcmData (16, 2)) })) },
        { "junk16_stereo.wav",  riffWrap (joinChunks ({ junk, chunkOf ("fmt ", fmtPcm (16, 2)), chunkOf ("data", pcmData (16, 2)) })) },
        { "list16_stereo.wav",  riffWrap (joinChunks ({ list, chunkOf ("fmt ", fmtPcm (16, 2)), chunkOf ("data", pcmData (16, 2)) })) },
        { "UPPER16.WAV",        wav (fmtPcm (16, 2), pcmData (16, 2)) },
        { "fmt18_pcm16.wav",    wav (fmtPcm (16, 2, true), pcmData (16, 2)) },
    };

    // ---- round 2: the hostile headers real libraries contain ----
    // A data chunk that declares size 0 or -1 while the audio follows
    // anyway (unfinalised recorder files), a RIFF size of -1, an RF64
    // shell (ds64), data before fmt, a 14-byte fmt (pre-WAVEFORMATEX
    // writers), a fact chunk on a float file, sampler smpl metadata,
    // 6-channel and 192 kHz files.
    auto rawChunk = [] (const char* id, juce::uint32 declaredSize, const Bytes& payload)
    {
        Bytes out; putTag (out, id); putU32 (out, declaredSize);
        out.insert (out.end(), payload.begin(), payload.end());
        if (declaredSize % 2 && declaredSize == (juce::uint32) payload.size()) out.push_back (0);
        return out;
    };
    {
        const Bytes fmt16 = chunkOf ("fmt ", fmtPcm (16, 2));
        const Bytes data16 = chunkOf ("data", pcmData (16, 2));
        Bytes ds0 = riffWrap (joinChunks ({ fmt16, rawChunk ("data", 0, pcmData (16, 2)) }));
        variants.push_back ({ "datasize0.wav", ds0 });
        Bytes dsF = riffWrap (joinChunks ({ fmt16, rawChunk ("data", 0xFFFFFFFFu, pcmData (16, 2)) }));
        variants.push_back ({ "datasize_minus1.wav", dsF });
        Bytes rsF = wav (fmtPcm (16, 2), pcmData (16, 2));
        rsF[4] = rsF[5] = rsF[6] = rsF[7] = 0xFF; // RIFF size = -1
        variants.push_back ({ "riffsize_minus1.wav", rsF });
        // RF64: RIFF size -1, ds64 chunk first, data size -1
        Bytes ds64payload;
        putU32 (ds64payload, 0xFFFFFFFFu & 0u); // riffSize low (patched below)
        // build ds64 properly: riffSize64 (8), dataSize64 (8), sampleCount64 (8), tableLength (4)
        ds64payload.clear();
        auto putU64 = [&ds64payload] (juce::uint64 v) { for (int i = 0; i < 8; ++i) ds64payload.push_back ((juce::uint8) (v >> (8 * i))); };
        const juce::uint64 dataBytes = (juce::uint64) pcmData (16, 2).size();
        putU64 (36 + 8 + 28 + 8 + dataBytes); putU64 (dataBytes); putU64 ((juce::uint64) kLen); putU32 (ds64payload, 0);
        Bytes rf64body;
        putTag (rf64body, "RF64");
        Bytes inner = joinChunks ({ chunkOf ("ds64", ds64payload), fmt16, rawChunk ("data", 0xFFFFFFFFu, pcmData (16, 2)) });
        Bytes rf64;
        putTag (rf64, "RIFF"); putU32 (rf64, 0xFFFFFFFFu); putTag (rf64, "WAVE");
        rf64.insert (rf64.end(), inner.begin(), inner.end());
        (void) rf64body;
        variants.push_back ({ "rf64_pcm16.wav", rf64 });
        Bytes dataFirst = riffWrap (joinChunks ({ data16, fmt16 }));
        variants.push_back ({ "data_before_fmt.wav", dataFirst });
        Bytes fmt14;
        { Bytes f = fmtPcm (16, 2); f.resize (14); fmt14 = chunkOf ("fmt ", f); }
        variants.push_back ({ "fmt14_pcm16.wav", riffWrap (joinChunks ({ fmt14, data16 })) });
        Bytes factPayload; putU32 (factPayload, (juce::uint32) kLen);
        const Bytes fact = chunkOf ("fact", factPayload);
        variants.push_back ({ "fact_float32.wav", riffWrap (joinChunks ({ chunkOf ("fmt ", fmtFloat (2)), fact, chunkOf ("data", floatData (2)) })) });
        Bytes smplPayload (36, 0);
        const Bytes smpl = chunkOf ("smpl", smplPayload);
        variants.push_back ({ "smpl_pcm16.wav", riffWrap (joinChunks ({ fmt16, smpl, data16 })) });
        Bytes fmt6 = fmtPcm (16, 6);
        Bytes data6;
        { // 6 channels of tone
            const auto a = tone (440.0);
            for (int i = 0; i < kLen; ++i)
                for (int c = 0; c < 6; ++c) putU16 (data6, (juce::uint16) (juce::int16) (a[(size_t) i] * 32767.0f));
        }
        variants.push_back ({ "pcm16_6ch.wav", riffWrap (joinChunks ({ chunkOf ("fmt ", fmt6), chunkOf ("data", data6) })) });
        Bytes fmt192 = fmtPcm (16, 2);
        // patch the sample-rate fields (offset 4 in fmt payload) to 192000
        fmt192[4] = 0x00; fmt192[5] = 0xEE; fmt192[6] = 0x02; fmt192[7] = 0x00; // 192000 LE
        fmt192[8] = 0x00; fmt192[9] = 0xDC; fmt192[10] = 0x0B; fmt192[11] = 0x00; // byte rate 768000
        variants.push_back ({ "pcm16_192k.wav", riffWrap (joinChunks ({ chunkOf ("fmt ", fmt192), data16 })) });
    }

    BoomBapProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* main = dynamic_cast<MainComponent*> (ed->getChildComponent (0));
    check (main != nullptr, "75: the editor hosts the main view");
    if (main == nullptr) return 1;

    auto peakOf = [] (const std::vector<float>& d)
    {
        float p = 0.0f;
        for (float v : d) p = juce::jmax (p, std::abs (v));
        return p;
    };

    for (const auto& v : variants)
    {
        const auto file = writeWav (dir, v.name, v.riff);
        const juce::String tag = "75: " + v.name;

        std::vector<float> mono;
        double sr = 0.0;
        const bool decoded = main->driveDecodeSampleFile (file, mono, sr);
        check (decoded, (tag + " decodes").toRawUTF8());
        if (decoded)
        {
            const double wantRate = v.name == "pcm16_192k.wav" ? 192000.0 : 44100.0;
            check (std::abs (sr - wantRate) < 0.5, (tag + " sample rate reads correctly").toRawUTF8());
            if ((int) mono.size() != kLen)
                std::cout << "  [info] " << tag << " decoded frames=" << mono.size() << " expected " << kLen << "\n";
            check ((int) mono.size() == kLen, (tag + " full length decoded").toRawUTF8());
            check (peakOf (mono) > 0.3f && peakOf (mono) < 0.75f, (tag + " level survives decode").toRawUTF8());
        }

        // The pad>=3 route (loadPadFile -> decode -> engine.loadPadSample).
        proc.engine.clearPad (5);
        main->driveLoadPadFile (5, file);
        check (proc.engine.padHasSample (5), (tag + " lands on pad 6").toRawUTF8());
        if (proc.engine.padHasSample (5))
        {
            const auto held = proc.engine.getPadSampleData (5);
            check ((int) held.size() == kLen, (tag + " pad holds the full sample").toRawUTF8());
            check (peakOf (held) > 0.3f, (tag + " pad sample is not silent").toRawUTF8());
        }

        // The pad<3 route (through the sequencer's sample rows).
        proc.engine.clearPad (1);
        main->driveLoadPadFile (1, file);
        check (proc.engine.padHasSample (1), (tag + " lands on pad 2 (sample-row route)").toRawUTF8());
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
