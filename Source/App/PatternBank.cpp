// ============================================================================
// THE PATTERN BANK - implementation. See PatternBank.h for the design and
// the verified data facts. The JSON bytes come from BinaryData
// (PatternData.bin, bundled by juce_add_binary_data); the offsets table
// is PatternData.h. Parsing is lazy and one-time: the first picker or
// apply call reads all 100 files (about 1.2 MB of JSON) into plain
// structs; everything after that is table lookup.
// ============================================================================
#include "PatternBank.h"
#include "PatternData.h"
#include "BinaryData.h"

#include <cmath>

namespace boombap
{
namespace
{
struct Bank
{
    std::vector<DrumGrooveData> grooves;   // playable only (49)
    std::vector<BasslineData>   basslines; // all 50
    bool loaded = false;
};

juce::String bpmText (double bpm)
{
    const double r = std::round (bpm);
    if (std::abs (bpm - r) < 0.05) return juce::String ((int) r);
    return juce::String (bpm, 1);
}

juce::var parseEntry (const patterndata::PatternEntry& e)
{
    if (BinaryData::PatternData_bin == nullptr
        || BinaryData::PatternData_binSize != (int) patterndata::blobSize)
        return {};
    if (e.size == 0 || (std::size_t) e.offset + e.size > patterndata::blobSize)
        return {};
    const juce::String text (BinaryData::PatternData_bin + e.offset, (size_t) e.size);
    return juce::JSON::parse (text);
}

int propInt (const juce::var& v, const char* name, int dflt = 0)
{
    return (int) v.getProperty (juce::Identifier (name), dflt);
}
double propDouble (const juce::var& v, const char* name, double dflt = 0.0)
{
    return (double) v.getProperty (juce::Identifier (name), dflt);
}

void loadBank (Bank& bank)
{
    bank.loaded = true; // one attempt; a corrupt entry reads as empty

    for (int i = 0; i < 50; ++i)
    {
        const auto root = parseEntry (patterndata::drums[i]);
        if (! root.isObject()) continue;

        DrumGrooveData g;
        g.number = propInt (root, "number", i + 1);
        g.bpm = propDouble (root, "bpm_file", propDouble (root, "bpm_filename"));
        const auto hitsVar = root.getProperty ("hits", {});
        if (auto* hits = hitsVar.getArray())
            for (const auto& h : *hits)
            {
                DrumHitData d;
                d.note = propInt (h, "note");
                d.step32 = juce::jlimit (0, 31, propInt (h, "step32"));
                d.velocity = juce::jlimit (1, 127, propInt (h, "velocity", 100));
                g.hits.push_back (d);
            }
        // drum-08 (and any other empty file) never reaches the picker.
        if (g.hits.empty()) continue;
        g.displayName = juce::String::formatted ("%02d", g.number) + " \xc2\xb7 "
                      + bpmText (g.bpm) + " BPM";
        bank.grooves.push_back (std::move (g));
    }

    for (int i = 0; i < 50; ++i)
    {
        const auto root = parseEntry (patterndata::basslines[i]);
        if (! root.isObject()) continue;

        BasslineData b;
        b.number = propInt (root, "number", i + 1);
        b.bpm = propDouble (root, "bpm_file", propDouble (root, "bpm_filename"));
        b.keyName = root.getProperty ("key_filename", {}).toString();
        const auto notesVar = root.getProperty ("notes", {});
        if (auto* notes = notesVar.getArray())
            for (const auto& n : *notes)
            {
                // Block 1 only: the first 32 steps are the loop seed;
                // later blocks are phrase development (INVENTORY.md).
                const int step = propInt (n, "step");
                if (step < 0 || step >= 32) continue;
                BassNoteData bn;
                bn.pitch = propInt (n, "note");
                bn.step = step;
                const int len = (int) std::lround (propDouble (n, "length_steps", 1.0));
                bn.lengthSteps = juce::jlimit (1, 32 - step, len);
                bn.velocity = juce::jlimit (1, 127, propInt (n, "velocity", 100));
                b.notes.push_back (bn);
            }
        b.displayName = juce::String::formatted ("%03d", b.number) + " \xc2\xb7 "
                      + b.keyName + " \xc2\xb7 " + bpmText (b.bpm) + " BPM";
        bank.basslines.push_back (std::move (b));
    }
}

Bank& bank()
{
    static Bank b;
    if (! b.loaded) loadBank (b);
    return b;
}
} // namespace

int drumGrooveCount() { return (int) bank().grooves.size(); }

juce::StringArray drumGrooveNames()
{
    juce::StringArray names;
    for (const auto& g : bank().grooves) names.add (g.displayName);
    return names;
}

const DrumGrooveData* drumGrooveAt (int pickerIndex)
{
    const auto& gs = bank().grooves;
    if (pickerIndex < 0 || pickerIndex >= (int) gs.size()) return nullptr;
    return &gs[(size_t) pickerIndex];
}

int basslineCount() { return (int) bank().basslines.size(); }

juce::StringArray basslineNames()
{
    juce::StringArray names;
    for (const auto& b : bank().basslines) names.add (b.displayName);
    return names;
}

const BasslineData* basslineAt (int index)
{
    const auto& bs = bank().basslines;
    if (index < 0 || index >= (int) bs.size()) return nullptr;
    return &bs[(size_t) index];
}
} // namespace boombap
