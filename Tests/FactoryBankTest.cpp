// Bank build test (items 82/140/159/179): the factory kits ship inside
// the binaries - every kit loads into an engine with all sixteen pads
// (and their sequencer rows) filled and named, straight from the
// bundled BinaryData, no files on disk involved.
#include "../Source/App/FactoryBank.h"
#include <cmath>
#include <cstdio>

using namespace boombap;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

static float peakOf (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x) p = std::max (p, std::abs (v));
    return p;
}

int main()
{
    check (factoryKitCount() == 4, "bank: exactly four factory kits");
    check (factoryKitName (0) == "Boom Bap Core",  "bank: kit 1 is Boom Bap Core");
    check (factoryKitName (1) == "Golden Dust",    "bank: kit 4 is Golden Dust");
    check (factoryKitName (2) == "Lowrider Funk",  "bank: kit 9 is Lowrider Funk");
    check (factoryKitName (3) == "Modern Trap",    "bank: kit 16 is Modern Trap");
    check (factoryPadName (0, 2) == "Fat Snare",   "bank: Boom Bap Core pad 3 is Fat Snare");
    check (factoryPadName (1, 15) == "Crackle Bed Hit", "bank: Golden Dust pad 16 is Crackle Bed Hit");

    // Project identity round-trip.
    {
        int k = -1, p = -1;
        check (parseFactoryRef (factoryRef (2, 7), k, p) && k == 2 && p == 7,
               "bank: factory refs round-trip");
        check (! parseFactoryRef ("/some/user/file.wav", k, p),
               "bank: a plain path is not a factory ref");
    }

    // Every pad of every kit decodes at the render rate, non-silent.
    for (int kit = 0; kit < factoryKitCount(); ++kit)
    {
        char msg[128];
        bool allOk = true;
        for (int pad = 0; pad < 16; ++pad)
        {
            std::vector<float> mono;
            double sr = 0.0;
            if (! decodeFactoryPad (kit, pad, mono, sr) || mono.empty()
                || sr != 44100.0 || peakOf (mono) < 0.05f)
                allOk = false;
        }
        std::snprintf (msg, sizeof msg, "bank: kit %d - all 16 pads decode at 44.1k, non-silent", kit);
        check (allOk, msg);
    }

    // The one-call load fills BOTH stores, all sixteen deep.
    {
        BoomBapEngine engine;
        check (loadFactoryKitInto (engine, 1), "bank: Golden Dust loads into an engine");
        bool padsOk = true, rowsOk = true;
        for (int pad = 0; pad < 16; ++pad)
        {
            if (! engine.padHasSample (pad) || engine.getPadSampleData (pad).empty()) padsOk = false;
            if (! engine.rowHasSample (pad) || engine.getRowSampleData (pad).empty()) rowsOk = false;
        }
        check (padsOk, "bank: all 16 pads hold samples after the kit load");
        check (rowsOk, "bank: all 16 pad rows hold samples after the kit load");

        // An out-of-range kit is refused and changes nothing.
        BoomBapEngine fresh;
        check (! loadFactoryKitInto (fresh, 99) && ! fresh.padHasSample (0),
              "bank: an invalid kit is refused cleanly");
    }

    std::printf (failures == 0 ? "FACTORY BANK TEST: ALL PASS\n" : "FACTORY BANK TEST: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
