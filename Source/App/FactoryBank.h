#pragma once
// ============================================================================
// THE FACTORY BANK (bank build, Nov 14, 2026 - items 82/140/159/179).
//
// The four finished factory kits - Kit 1 "Boom Bap Core" and the Wave 1
// three (Golden Dust, Lowrider Funk, Modern Trap), 16 synthesized pads
// each - ship INSIDE the binaries. FactoryKitData.bin (the 64 WAVs
// concatenated, see FactoryKitData.h for the table) is bundled via
// juce_add_binary_data (CMakeLists: BoomBapBinaryData), so a fresh
// install can play before the user owns a single sample file - the
// bank D called "the one thing we really really need" (item 179).
//
// This header is the whole surface: kit/pad names for the selectors,
// the raw WAV bytes, a JUCE decode of one pad, and the one-call kit
// load into an engine. A kit load fills BOTH sample stores - the pad
// bank (loadPadSample) and the sequencer's pad rows (loadSample on
// rows Sample1..Sample1+15) - because a kit is one instrument with
// two doors: the rows are what the step sequencer triggers and what
// the keyboard pitch-plays through the live-note funnel. (Item 109's
// decoupling governs FILE loads leaking between the stores; a kit
// load is the explicit whole-bank action the engine header itself
// names: "a pad changes only by an explicit pad action - a drag
// onto it, its own chooser, a kit/chop load.")
//
// Project files reference factory pads by URI - "factory:<kit>:<pad>"
// in the pad/row path slots - so a saved project (and every undo
// snapshot, which is project JSON) re-loads its factory sounds from
// the bank instead of pointing at files that never existed.
// ============================================================================
#include "Engine/BoomBapEngine.h"
#include <juce_core/juce_core.h>
#include <cstddef>
#include <vector>

namespace boombap
{
    int         factoryKitCount();
    juce::String factoryKitName (int kit);
    juce::String factoryPadName (int kit, int pad);

    // The pad's WAV bytes inside the bundled blob. False when the
    // kit/pad is out of range.
    bool factoryPadBytes (int kit, int pad, const void*& data, std::size_t& size);

    // Decode one factory pad to mono (the pad load path's policy:
    // channels averaged, 20-second cap, plausibility-checked length).
    bool decodeFactoryPad (int kit, int pad,
                           std::vector<float>& monoOut, double& sampleRateOut);

    // Fill an engine's pads AND pad rows with a kit. All-or-nothing:
    // every pad decodes before the first one lands. False (and the
    // engine untouched) when any pad fails.
    bool loadFactoryKitInto (BoomBapEngine& engine, int kit);

    // Project-file identity for a factory pad: "factory:<kit>:<pad>".
    juce::String factoryRef (int kit, int pad);
    bool parseFactoryRef (const juce::String& path, int& kit, int& pad);
}
