#pragma once
// Round 4 (item 98e): the piano roll's chord + progression library.
// Pure data and pure functions - the roll stamps through them, the
// tests pin them. Notes are semitone offsets from a root, matching the
// lanes' encoding (a stamped value is root + interval, stored + 25).
//
// The lanes are MONOPHONIC - one note per step - so a chord cannot
// stack. A stamp therefore ARPEGGIATES: the chord's tones cycle
// up-and-down across the stamp's steps (root, third, fifth, seventh,
// fifth, third...), the way a boom-bap Rhodes line actually plays a
// chord. The pitch SET stamped is exactly the chord; every stamped
// note is an ordinary lane note afterwards.

#include <algorithm>
#include <cstring>
#include <vector>

namespace boombap
{
struct ChordType
{
    const char* name;
    std::vector<int> intervals; // semitones from the chord root
};

inline const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> types = {
        { "Major",      { 0, 4, 7 } },
        { "Minor",      { 0, 3, 7 } },
        { "Maj7",       { 0, 4, 7, 11 } },
        { "Min7",       { 0, 3, 7, 10 } },
        { "Dom7",       { 0, 4, 7, 10 } },
        { "Dim",        { 0, 3, 6 } },
        { "Aug",        { 0, 4, 8 } },
        { "Sus2",       { 0, 2, 7 } },
        { "Sus4",       { 0, 5, 7 } },
        { "Min9",       { 0, 3, 7, 10, 14 } },
        { "Maj9",       { 0, 4, 7, 11, 14 } },
        { "MinMaj7",    { 0, 3, 7, 11 } },
        { "Dim7",       { 0, 3, 6, 9 } },
        { "HalfDim7",   { 0, 3, 6, 10 } },
        { "Add9",       { 0, 4, 7, 14 } },
        { "6",          { 0, 4, 7, 9 } },
        { "Min6",       { 0, 3, 7, 9 } },
    };
    return types;
}

inline int chordTypeIndex (const char* name)
{
    const auto& types = chordTypes();
    for (int i = 0; i < (int) types.size(); ++i)
        if (std::strcmp (types[(size_t) i].name, name) == 0) return i;
    return -1;
}

// One chord of a progression: its root as a semitone offset from the
// KEY root, and the chord type that sounds there. Progressions are
// written for a minor key (the boom-bap home key); roots and qualities
// are the named staples themselves, resolved against the key the
// owner picks in the roll.
struct ProgressionChord { int rootOffset; int chordType; };
struct Progression
{
    const char* name;
    std::vector<ProgressionChord> chords;
};

inline const std::vector<Progression>& progressions()
{
    const int maj = chordTypeIndex ("Major"), min = chordTypeIndex ("Minor"),
              min7 = chordTypeIndex ("Min7"), dom7 = chordTypeIndex ("Dom7"),
              halfDim7 = chordTypeIndex ("HalfDim7");
    static const std::vector<Progression> progs = {
        { "i - VII",            { { 0, min }, { 10, maj } } },
        { "i - iv",             { { 0, min }, { 5, min } } },
        { "i - VI - III - VII", { { 0, min }, { 8, maj }, { 3, maj }, { 10, maj } } },
        { "Andalusian i - VII - VI - V",
                                { { 0, min }, { 10, maj }, { 8, maj }, { 7, maj } } },
        { "ii - V - i (minor)", { { 2, halfDim7 }, { 7, dom7 }, { 0, min7 } } },
    };
    return progs;
}

// Folds a semitone offset into the lanes' writable range (-24..+24)
// by octave, so a high key + a 9th never falls off the roll.
inline int foldToLaneRange (int semitone)
{
    while (semitone > 24) semitone -= 12;
    while (semitone < -24) semitone += 12;
    return semitone;
}

// The arpeggio order over a chord's tones: up, then back down, the
// peak and root not repeated at the turns (0 1 2 3 2 1 0 1 2 3...).
inline int arpeggioTone (const ChordType& chord, int stepInChord)
{
    const int n = (int) chord.intervals.size();
    if (n <= 1) return chord.intervals.empty() ? 0 : chord.intervals[0];
    const int period = 2 * n - 2;
    int k = stepInChord % period;
    if (k >= n) k = period - k;
    return chord.intervals[(size_t) k];
}

// The note value (semitone offset, NOT yet +25) a chord stamp writes
// at one step: the key root + the chord root + the arpeggiated tone,
// folded into the lane range.
inline int chordStampNote (int keyRoot, int chordRootOffset, const ChordType& chord, int stepInChord)
{
    return foldToLaneRange (keyRoot + chordRootOffset + arpeggioTone (chord, stepInChord));
}
} // namespace boombap
