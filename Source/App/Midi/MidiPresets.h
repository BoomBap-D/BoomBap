#pragma once
#include "MidiLearn.h"

// =====================================================================
// The factory controller presets (MIDI-CONTROLLERS-REFERENCE
// Part 5). HONESTY RULE, kept from the reference: only the MPK Mini
// mk1's Program 1 is a true factory layout (decoded from Akai's
// preset files). Akai and M-Audio publish NO factory CC tables for
// the MPK49 or the Trigger Finger Pro - those two maps are a
// BOOMBAP-ASSIGNED scheme the user loads into the unit with its
// editor, and the UI says so. Playing needs none of this: pads
// 36-51 and melodic notes are fixed routing (MidiRouter).
// =====================================================================

namespace boombap
{

struct MidiPreset
{
    juce::String name;        // shown in the MIDI window's preset menu
    juce::String setupNote;   // one honest line under the menu
    std::vector<MidiBinding> bindings;
};

inline MidiBinding ccBind (int channel, int cc, const juce::String& target,
                           MidiBinding::Takeover take = MidiBinding::Absolute)
{
    MidiBinding b;
    b.sourceType = MidiBinding::CCSource;
    b.channel = channel;
    b.number = cc;
    b.targetId = target;
    b.takeover = take;
    return b;
}

// ---------------------------------------------------------------------
// Akai MPK Mini mk1 - factory Program 1 (pads on ch 10, knobs on
// ch 1). Knob order is Akai's own: K1-K8 = CC 17,18,19,20,13,14,15,16.
// The pad CC-mode numbers come from the same factory dump; Bank 2's
// odd jump (31 -> 35) is real, not a typo.
// ---------------------------------------------------------------------
inline MidiPreset mpkMiniPreset()
{
    MidiPreset p;
    p.name = "Akai MPK Mini mk1 - factory Program 1";
    p.setupNote = "Ships on the unit: pads already play BoomBap's pads "
                  "(ch 10, notes 36-51). Knobs/pad-CC mode as dumped from "
                  "Akai's factory preset.";
    const auto P = MidiBinding::Pickup;
    // Knobs K1-K8 (ch 1), Pickup per the reference (knobs are endless-
    // feeling pots on a bankless unit - Pickup stops value jumps).
    p.bindings.push_back (ccBind (1, 17, "chanGain:0", P));   // K1 Kick
    p.bindings.push_back (ccBind (1, 18, "chanGain:1", P));   // K2 Snare
    p.bindings.push_back (ccBind (1, 19, "chanGain:2", P));   // K3 Closed Hat
    p.bindings.push_back (ccBind (1, 20, "chanGain:34", P));  // K4 Pad Bus
    p.bindings.push_back (ccBind (1, 13, "glueThresh", P));   // K5 Glue
    p.bindings.push_back (ccBind (1, 14, "dustRate", P));     // K6 Dust
    p.bindings.push_back (ccBind (1, 15, "swing", P));        // K7 Swing
    p.bindings.push_back (ccBind (1, 16, "selPan", P));       // K8 Sel Pan
    // Pad CC mode, Bank 1 (ch 10).
    p.bindings.push_back (ccBind (10, 20, "actPlay"));
    p.bindings.push_back (ccBind (10, 21, "actStop"));
    p.bindings.push_back (ccBind (10, 22, "recArmMidi"));
    p.bindings.push_back (ccBind (10, 23, "songMode"));
    p.bindings.push_back (ccBind (10, 24, "metroOn"));
    p.bindings.push_back (ccBind (10, 25, "actPatternPrev"));
    p.bindings.push_back (ccBind (10, 26, "actPatternNext"));
    // (CC 27 was "open the editor" in the reference - BoomBap has no
    // separate editor window yet, so it stays unbound.)
    // Pad CC mode, Bank 2 (ch 10): patterns A-H.
    const int bank2[8] = { 28, 29, 30, 31, 35, 36, 37, 38 };
    for (int i = 0; i < 8; ++i)
        p.bindings.push_back (ccBind (10, bank2[i],
                                      "actPattern:" + juce::String (i)));
    return p;
}

// ---------------------------------------------------------------------
// Akai MPK Mini MK2 - factory Program 1 (item 147 fix). This is D's
// test unit. Program 1's pads send the same chromatic 36-51 the mk1
// sends (Bank A 36-43, Bank B 44-51), so they play BoomBap's pads
// through the router's fixed map with zero setup - the preset needs
// no pad bindings, and it carries NO knob bindings either: Akai
// publishes no factory knob-CC table for the MK2 (assignments live
// in the unit's Editor), so nothing is invented here. The knobs are
// bound per unit with right-click MIDI Learn - which is exactly the
// flow item 147 exists for. The preset's job is the correct NAME
// and the honest note, plus the MIDI window's Bank A/B hint.
// ---------------------------------------------------------------------
inline MidiPreset mpkMiniMk2Preset()
{
    MidiPreset p;
    p.name = juce::String::fromUTF8 ("Akai MPK Mini MK2 \xe2\x80\x94 factory Program 1");
    p.setupNote = "Ships on the unit: pads already play BoomBap's pads "
                  "(notes 36-51). Akai publishes no factory knob table "
                  "for the MK2 - right-click any control and choose "
                  "MIDI Learn to bind the knobs.";
    return p;
}

// ---------------------------------------------------------------------
// Akai MPK49 (original) - BOOMBAP-ASSIGNED scheme. Akai publishes no
// factory CC table; the user builds this once in Vyzex / on the unit
// (preset name "BoomBap", Global channel 1, CTRL bank for transport).
// Three control banks A/B/C as in the reference's table.
// ---------------------------------------------------------------------
inline MidiPreset mpk49Preset()
{
    MidiPreset p;
    p.name = "Akai MPK49 - BoomBap-assigned (load the BoomBap preset)";
    p.setupNote = "BoomBap-assigned CC scheme - Akai publishes no factory "
                  "table. Build it once in the unit's editor (Global ch 1, "
                  "transport on the CTRL bank); pads play with zero setup.";
    // Bank A knobs K1-K8: the selected channel's EQ (Station bound ch).
    p.bindings.push_back (ccBind (1, 16, "selHpfHz"));        // band 0 freq
    p.bindings.push_back (ccBind (1, 17, "selEqGain:1"));
    p.bindings.push_back (ccBind (1, 18, "selEqGain:2"));
    p.bindings.push_back (ccBind (1, 19, "selEqGain:3"));
    p.bindings.push_back (ccBind (1, 20, "selEqGain:4"));
    p.bindings.push_back (ccBind (1, 21, "selEqFreq:5"));     // band 5 freq
    p.bindings.push_back (ccBind (1, 22, "selEqActiveFreq"));
    p.bindings.push_back (ccBind (1, 23, "selEqActiveQ"));
    // Bank A faders F1-F8: drum rows 1-8 levels.
    for (int i = 0; i < 8; ++i)
        p.bindings.push_back (ccBind (1, 24 + i, "chanGain:" + juce::String (i)));
    // Bank A switches S1-S8.
    for (int i = 0; i < 4; ++i)
        p.bindings.push_back (ccBind (1, 32 + i, "actEqBand:" + juce::String (i)));
    p.bindings.push_back (ccBind (1, 36, "selMute"));
    p.bindings.push_back (ccBind (1, 37, "selSolo"));
    p.bindings.push_back (ccBind (1, 38, "actRec"));
    // Bank B knobs: dynamics + dust + selection.
    p.bindings.push_back (ccBind (1, 40, "selCompThresh"));
    p.bindings.push_back (ccBind (1, 41, "selCompAttack"));
    p.bindings.push_back (ccBind (1, 42, "selCompRelease"));
    p.bindings.push_back (ccBind (1, 43, "selPreampDb"));   // makeup slot: BoomBap's strip comp has no makeup knob - preamp trim is the gain-staging control
    p.bindings.push_back (ccBind (1, 44, "dustRate"));
    p.bindings.push_back (ccBind (1, 45, "selDeess"));      // dust-character slot on the reference table
    p.bindings.push_back (ccBind (1, 46, "selPan"));
    p.bindings.push_back (ccBind (1, 47, "selGain"));
    // Bank B faders: the 8 audio tracks.
    for (int i = 0; i < 8; ++i)
        p.bindings.push_back (ccBind (1, 48 + i, "chanGain:" + juce::String (35 + i)));
    // Bank B switches.
    p.bindings.push_back (ccBind (1, 56, "glueOn"));
    p.bindings.push_back (ccBind (1, 57, "dustOn"));
    p.bindings.push_back (ccBind (1, 58, "actCompRatioCycle"));
    p.bindings.push_back (ccBind (1, 59, "vocalMonitor"));
    p.bindings.push_back (ccBind (1, 60, "metroOn"));
    // (CC 61 was count-in - BoomBap has no count-in yet.)
    p.bindings.push_back (ccBind (1, 62, "actFlipStation"));
    p.bindings.push_back (ccBind (1, 63, "songMode"));
    // Bank C knobs: synth/piano/editor trims.
    p.bindings.push_back (ccBind (1, 64, "synthCutoff"));
    p.bindings.push_back (ccBind (1, 65, "synthReso"));
    p.bindings.push_back (ccBind (1, 66, "synthAttack"));
    p.bindings.push_back (ccBind (1, 67, "synthRelease"));
    p.bindings.push_back (ccBind (1, 68, "pianoBright"));
    p.bindings.push_back (ccBind (1, 69, "pianoDecay"));
    p.bindings.push_back (ccBind (1, 70, "selPadStart"));
    p.bindings.push_back (ccBind (1, 71, "selPadEnd"));
    // Bank C faders: master / selected / pad bus (cue+send levels
    // don't exist in BoomBap yet - CC 75-79 stay free).
    p.bindings.push_back (ccBind (1, 72, "chanGain:44"));
    p.bindings.push_back (ccBind (1, 73, "selGain"));
    p.bindings.push_back (ccBind (1, 74, "chanGain:34"));
    // Bank C switches.
    p.bindings.push_back (ccBind (1, 80, "recArmMidi"));
    p.bindings.push_back (ccBind (1, 82, "actFlipStation")); // "open editor" on the table - the Station is BoomBap's editor surface
    p.bindings.push_back (ccBind (1, 84, "actPatternPrev"));
    p.bindings.push_back (ccBind (1, 85, "actPatternNext"));
    p.bindings.push_back (ccBind (1, 86, "actUndo"));
    p.bindings.push_back (ccBind (1, 87, "actRedo"));
    // Transport, CTRL bank format (CCs, not MMC).
    p.bindings.push_back (ccBind (1, 88, "actRew"));
    p.bindings.push_back (ccBind (1, 89, "actFF"));
    p.bindings.push_back (ccBind (1, 90, "actStop"));
    p.bindings.push_back (ccBind (1, 91, "actPlay"));
    p.bindings.push_back (ccBind (1, 92, "actRec"));
    return p;
}

// ---------------------------------------------------------------------
// M-Audio Trigger Finger Pro - BOOMBAP-ASSIGNED scheme. M-Audio
// publishes NO factory CC table either (the reference is explicit);
// the user assigns these once in Arsenal. The unit's transport
// speaks MMC/real-time, which needs no binding (fixed inputs - a
// later pass; BoomBap's transport follows its own keys for now).
// ---------------------------------------------------------------------
inline MidiPreset triggerFingerProPreset()
{
    MidiPreset p;
    p.name = "Trigger Finger Pro - BoomBap-assigned (assign in Arsenal)";
    p.setupNote = "BoomBap-assigned CC scheme - M-Audio publishes no factory "
                  "table. Assign these CCs once in Arsenal (Global ch 1); "
                  "pads play with zero setup.";
    // Bank A: drum rows 1-4 - pans / levels / mutes.
    for (int i = 0; i < 4; ++i)
    {
        p.bindings.push_back (ccBind (1, 16 + i, "chanPan:" + juce::String (i)));
        p.bindings.push_back (ccBind (1, 20 + i, "chanGain:" + juce::String (i)));
        p.bindings.push_back (ccBind (1, 24 + i, "chanMute:" + juce::String (i)));
    }
    // Bank B: pad bus / synth / piano / vocal.
    const int bCh[4] = { 34, 32, 33, 43 };
    for (int i = 0; i < 4; ++i)
    {
        p.bindings.push_back (ccBind (1, 28 + i, "chanPan:" + juce::String (bCh[i])));
        p.bindings.push_back (ccBind (1, 32 + i, "chanGain:" + juce::String (bCh[i])));
        p.bindings.push_back (ccBind (1, 36 + i, "chanMute:" + juce::String (bCh[i])));
    }
    // Bank C knobs: glue / dust / swing (the reference's Makeup slot
    // has no BoomBap equivalent - CC 41 stays free).
    p.bindings.push_back (ccBind (1, 40, "glueThresh"));
    p.bindings.push_back (ccBind (1, 42, "dustRate"));
    p.bindings.push_back (ccBind (1, 43, "swing"));
    // Bank C faders: master / selected / pad bus.
    p.bindings.push_back (ccBind (1, 44, "chanGain:44"));
    p.bindings.push_back (ccBind (1, 45, "selGain"));
    p.bindings.push_back (ccBind (1, 46, "chanGain:34"));
    // Bank C buttons.
    p.bindings.push_back (ccBind (1, 48, "glueOn"));
    p.bindings.push_back (ccBind (1, 49, "dustOn"));
    p.bindings.push_back (ccBind (1, 50, "metroOn"));
    p.bindings.push_back (ccBind (1, 51, "actFlipStation"));
    // Bank D knobs: the Station's active EQ band + HPF.
    p.bindings.push_back (ccBind (1, 52, "selEqActiveFreq"));
    p.bindings.push_back (ccBind (1, 53, "selEqActiveGain"));
    p.bindings.push_back (ccBind (1, 54, "selEqActiveQ"));
    p.bindings.push_back (ccBind (1, 55, "selHpfHz"));
    // Bank D faders: the selected pad's trim + level.
    p.bindings.push_back (ccBind (1, 56, "selPadStart"));
    p.bindings.push_back (ccBind (1, 57, "selPadEnd"));
    p.bindings.push_back (ccBind (1, 58, "selPadGain"));
    // Bank D buttons.
    p.bindings.push_back (ccBind (1, 60, "actEqBandNext"));
    p.bindings.push_back (ccBind (1, 61, "selEqActiveOn"));
    return p;
}

inline std::vector<MidiPreset> allMidiPresets()
{
    // Order is the preset menu's order - and saved preset indices
    // point into it, so new presets join at the END of their family
    // only with care. The MK2 sits beside the mk1 it supersedes on
    // D's desk (noted in MIDI-LEARN-FIX-NOTES: indices 3/4 shifted).
    return { mpkMiniPreset(), mpkMiniMk2Preset(), mpk49Preset(), triggerFingerProPreset() };
}

} // namespace boombap
