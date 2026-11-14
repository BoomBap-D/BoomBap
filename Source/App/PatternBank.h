#pragma once
// ============================================================================
// THE PATTERN BANK (item 160, patterns build, Nov 14, 2026).
//
// D's own MIDI packs - the DopeVST hip-hop drum patterns and Bass Engine
// basslines he uploaded Nov 13 - ship INSIDE the binaries as preset
// patterns, the way item 160 asked: "I give it a generic drum set and
// people can cycle through step sequence presets and already have drum
// patterns or whatever basslines right in the presets." The converted
// JSONs (~/workspace/boombap/preset-patterns, INVENTORY.md there) are
// concatenated into PatternData.bin and bundled via juce_add_binary_data
// (CMakeLists: BoomBapBinaryData), mirroring FactoryBank exactly.
//
// THE DATA, as verified in INVENTORY.md (read it before touching this):
//  - Drum note map, verified across all 50 files: 36 = kick, 38 = snare,
//    40 = closed hat. NOT GM. Hits fold to the 32-step pattern via the
//    JSON's step32, velocity carried.
//  - Note 41 is a sparse accent whose role is UNVERIFIED (71 hits in 19
//    files). It is carried in this data untouched - D's ear decides what
//    it is later. The playback mapping in MainComponent deliberately
//    does NOT map it to any sound yet.
//  - drum-08 is EMPTY (tempo meta only, zero hits). It is excluded from
//    the picker: 49 playable drum grooves ship, not 50.
//  - Drum filename BPM == file BPM in all 50 files.
//  - Basslines: pitches as written (pack range C1-A3). Two files have
//    filename/file tempo mismatches (008, 026) - the FILE tempo wins.
//    Three filename keys disagree with the content (002, 024, 031) - the
//    picker DISPLAYS the filename key (it is the pack's own label) but
//    the notes land exactly as written. Only block 1 (the first 32
//    steps) is imported: these are loop seeds; later blocks are phrase
//    development (38 of 50 are not fold-lossless).
//  - v1 playback is step-quantized: the JSON's humanized offsets stay in
//    the bundled data, unplayed - the global Swing control is the feel.
// ============================================================================
#include <juce_core/juce_core.h>
#include <vector>

namespace boombap
{
struct DrumHitData
{
    int note = 0;      // the pack's note number (36/38/40 mapped; 41 held out)
    int step32 = 0;    // folded position in the 32-step pattern
    int velocity = 0;  // 1..127, as recorded
};

struct DrumGrooveData
{
    int number = 0;                 // pack number, 1..50 (8 is empty, absent)
    double bpm = 0.0;               // file tempo (== filename for all drums)
    juce::String displayName;       // "01 · 92 BPM"
    std::vector<DrumHitData> hits;  // every hit, note 41 included (held out)
};

struct BassNoteData
{
    int pitch = 0;        // MIDI note number, as written
    int step = 0;         // absolute step inside block 1 (0..31)
    int lengthSteps = 1;  // the note's length in steps, >= 1, clipped to 32
    int velocity = 0;     // as recorded (the lanes play at their own level)
};

struct BasslineData
{
    int number = 0;               // pack number, 1..50
    double bpm = 0.0;             // the FILE tempo (it wins over the filename)
    juce::String keyName;         // the FILENAME key - a label, not gospel
    juce::String displayName;     // "001 · A# · 93 BPM"
    std::vector<BassNoteData> notes; // block 1 only
};

// The drum picker list: the 49 playable grooves (drum-08 excluded).
int drumGrooveCount();
juce::StringArray drumGrooveNames();
const DrumGrooveData* drumGrooveAt (int pickerIndex); // nullptr = refused

// The bassline picker list: all 50.
int basslineCount();
juce::StringArray basslineNames();
const BasslineData* basslineAt (int index);           // nullptr = refused
} // namespace boombap
