// Tests for the .boombap project file format. Needs only juce_core.
// Build (from the repo root, with JUCE checked out at ./JUCE or fetched by CMake):
//   cmake -B build && cmake --build build --target ProjectFileTest
#include "../Source/App/ProjectFile.h"
#include <cstdio>

using namespace juce;
using namespace boombap;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}

static bool sameSettings (const ProjectSettings& a, const ProjectSettings& b)
{
    auto near = [] (float x, float y) { return std::abs (x - y) < 1.0e-3f; };
    bool same = near (a.bpm, b.bpm) && near (a.swing, b.swing) && near (a.masterDb, b.masterDb)
             && near (a.kickTuneHz, b.kickTuneHz) && near (a.kickDecayMs, b.kickDecayMs)
             && near (a.snareSnappy, b.snareSnappy) && near (a.openHatDecayMs, b.openHatDecayMs)
             && near (a.clapTailMs, b.clapTailMs) && a.dustOn == b.dustOn && near (a.dustRateHz, b.dustRateHz)
             && a.glueOn == b.glueOn && near (a.glueThresholdDb, b.glueThresholdDb)
             && a.vocalMic == b.vocalMic && near (a.vocalDistanceCm, b.vocalDistanceCm)
             && near (a.vocalInputDb, b.vocalInputDb)
             && a.synthWave == b.synthWave && near (a.synthCutoffHz, b.synthCutoffHz)
             && near (a.synthReso, b.synthReso) && near (a.synthAttackMs, b.synthAttackMs)
             && near (a.synthReleaseMs, b.synthReleaseMs) && near (a.synthLevel, b.synthLevel)
             && near (a.pianoBright, b.pianoBright) && near (a.pianoDecayMs, b.pianoDecayMs)
             && near (a.pianoLevel, b.pianoLevel);
    for (int t = 0; t < ProjectSettings::kTracks; ++t)
    {
        same = same && near (a.trackGain[t], b.trackGain[t]) && a.trackMute[t] == b.trackMute[t]
                    && a.trackSolo[t] == b.trackSolo[t];
        for (int p = 0; p < ProjectSettings::kPatterns; ++p)
            for (int s = 0; s < ProjectSettings::kSteps; ++s) same = same && a.patData[p][t][s] == b.patData[p][t][s];
    }
    for (int p = 0; p < ProjectSettings::kPads; ++p)
        same = same && near (a.padStart[p], b.padStart[p]) && near (a.padEnd[p], b.padEnd[p])
                    && near (a.padTune[p], b.padTune[p]) && near (a.padGain[p], b.padGain[p])
                    && near (a.padVelSens[p], b.padVelSens[p]) && a.padReverse[p] == b.padReverse[p]
                    && a.padMuteGroup[p] == b.padMuteGroup[p];
    same = same && a.currentPattern == b.currentPattern && a.songLength == b.songLength;
    for (int p = 0; p < ProjectSettings::kPatterns; ++p)
    {
        same = same && a.patMelodySource[p] == b.patMelodySource[p];
        for (int s = 0; s < ProjectSettings::kSteps; ++s)
            same = same && a.patMelody[p][s] == b.patMelody[p][s]
                        && a.patSynth[p][s] == b.patSynth[p][s]
                        && a.patPiano[p][s] == b.patPiano[p][s];
    }
    for (int i = 0; i < ProjectSettings::kSongSlots; ++i) same = same && a.songChain[i] == b.songChain[i];
    return same;
}

int main()
{
    const File root = File::getSpecialLocation (File::tempDirectory)
                          .getChildFile ("boombap_test_" + String (Random::getSystemRandom().nextInt (1000000)));
    const File dirA = root.getChildFile ("A"), dirB = root.getChildFile ("B");
    dirA.getChildFile ("samples").createDirectory();
    const File kick = dirA.getChildFile ("samples").getChildFile ("kick.wav");
    kick.replaceWithText ("not really audio, only the path matters here");

    // a project with non-default values everywhere
    ProjectData d;
    d.settings.bpm = 87.0f;  d.settings.swing = 63.0f;  d.settings.masterDb = -3.5f;
    d.settings.patData[0][0][3] = 127;  d.settings.patData[0][5][9] = 90;  d.settings.patData[0][2][0] = 0;
    d.settings.patData[2][1][5] = 90;   d.settings.patMelody[2][3] = 30;  d.settings.patMelodySource[2] = 5;
    d.settings.songChain[0] = 2; d.settings.songChain[1] = 0; d.settings.songLength = 2; d.settings.currentPattern = 2;
    d.settings.trackGain[1] = 0.55f;  d.settings.trackMute[3] = true;
    d.settings.trackSolo[2] = true;   d.settings.trackSolo[5] = true;
    d.settings.kickTuneHz = 46.5f;  d.settings.kickDecayMs = 820.0f;  d.settings.snareSnappy = 0.25f;
    d.settings.openHatDecayMs = 500.0f;  d.settings.clapTailMs = 120.0f;
    d.settings.dustOn = true;  d.settings.dustRateHz = 15000.0f;
    d.settings.glueOn = false;  d.settings.glueThresholdDb = -18.5f;
    d.settings.vocalMic = 4;  d.settings.vocalDistanceCm = 7.5f;  d.settings.vocalInputDb = 6.0f;
    d.settings.padStart[3] = 0.25f; d.settings.padEnd[3] = 0.8f; d.settings.padTune[3] = -3.0f;
    d.settings.padGain[3] = 1.4f;   d.settings.padVelSens[3] = 0.5f; d.settings.padReverse[3] = true;
    d.settings.padMuteGroup[3] = 2; d.settings.padTune[12] = 7.0f;
    d.padPaths[5] = kick.getFullPathName();
    d.vocalTakePath = kick.getFullPathName();
    d.settings.patMelodySource[0] = 3;
    d.settings.patMelody[0][0] = 25; d.settings.patMelody[0][4] = 32; d.settings.patMelody[0][7] = 13;
    d.settings.synthWave = 2;  d.settings.synthCutoffHz = 2600.0f; d.settings.synthReso = 0.66f;
    d.settings.synthAttackMs = 45.0f; d.settings.synthReleaseMs = 800.0f; d.settings.synthLevel = 0.5f;
    d.settings.pianoBright = 0.15f; d.settings.pianoDecayMs = 950.0f; d.settings.pianoLevel = 0.35f;
    d.settings.patSynth[0][0] = 25; d.settings.patSynth[0][8] = 32; d.settings.patSynth[5][3] = 20;
    d.settings.patPiano[0][4] = 37; d.settings.patPiano[7][15] = 13;
    d.samplePaths[0] = kick.getFullPathName();
    d.samplePaths[2] = "/definitely/not/here/snare.wav";
    d.exportBars = 8;  d.exportNormalize = false;

    std::printf ("Round trip\n");
    const File projectA = dirA.getChildFile ("beat.boombap");
    String err;
    check (saveProjectFile (projectA, d, err), "saves");
    ProjectData back;
    check (loadProjectFile (projectA, back, err), "loads");
    check (sameSettings (d.settings, back.settings), "every setting survives");
    check (back.samplePaths[0] == d.samplePaths[0], "sample path survives");
    check (back.samplePaths[1].isEmpty(), "unused sample slot stays empty");
    check (back.samplePaths[2] == d.samplePaths[2], "missing sample keeps its path");
    check (back.exportBars == 8 && ! back.exportNormalize, "export options survive");
    check (back.padPaths[5] == d.padPaths[5], "pad sample path survives");
    check (back.vocalTakePath == d.vocalTakePath, "vocal take path survives");

    // files from before patterns existed still load: pattern + melody land in pattern A
    const File legacyFile = dirA.getChildFile ("legacy.boombap");
    legacyFile.replaceWithText ("{ \"format\": \"boombap-project\", \"version\": 1, "
                                "\"pattern\": [ [127,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0] ], "
                                "\"melody\": { \"source\": 2, \"notes\": [25,0,0,0] } }");
    ProjectData legacy; String legacyErr;
    check (loadProjectFile (legacyFile, legacy, legacyErr), "legacy single-pattern file loads");
    check (legacy.settings.patData[0][0][0] == 127, "legacy steps land in pattern A");
    check (legacy.settings.patMelody[0][0] == 25 && legacy.settings.patMelodySource[0] == 2,
           "legacy melody lands in pattern A");
    check (legacy.settings.synthWave == 0 && legacy.settings.patSynth[0][0] == 0
           && legacy.settings.patPiano[0][0] == 0 && std::abs (legacy.settings.pianoDecayMs - 2200.0f) < 1.0f,
           "files without instruments keep the defaults");
    check (back.padPaths[0].isEmpty() && back.padPaths[3].isEmpty(), "pads without files stay empty");

    const String json = projectToJson (d, projectA);
    check (json.contains ("\"relativePath\": \"samples/kick.wav\""), "in-folder sample is stored relative");
    check (json.contains ("boombap-project"), "file is labelled");

    // Pass 1 item 7: the crash-recovery file is an ordinary project file at a
    // fixed location - the autosave writes it and the restore reads it back.
    std::printf ("Recovery file\n");
    const File recovery = root.getChildFile ("settings").getChildFile ("BoomBap Recovery.boombap");
    recovery.getParentDirectory().createDirectory();
    check (saveProjectFile (recovery, d, err), "recovery autosave writes");
    ProjectData recovered;
    check (loadProjectFile (recovery, recovered, err), "recovery file loads");
    check (sameSettings (d.settings, recovered.settings), "recovered settings match the autosaved ones");
    check (recovery.deleteFile(), "recovery file clears");

    std::printf ("Moving the project folder\n");
    dirA.copyDirectoryTo (dirB);
    dirA.deleteRecursively();
    ProjectData moved;
    check (loadProjectFile (dirB.getChildFile ("beat.boombap"), moved, err), "loads from the new folder");
    check (File (moved.samplePaths[0]) == dirB.getChildFile ("samples").getChildFile ("kick.wav"), "sample found via relative path");
    check (File (moved.samplePaths[0]).existsAsFile(), "resolved sample exists");

    std::printf ("Bad input\n");
    ProjectData tmp;
    check (! projectFromJson ("this is { not json", projectA, tmp, err) && err.isNotEmpty(), "garbage is rejected with a message");
    check (! projectFromJson ("{\"format\":\"something-else\",\"version\":1}", projectA, tmp, err), "wrong format is rejected");
    check (! projectFromJson ("{\"format\":\"boombap-project\",\"version\":99}", projectA, tmp, err)
           && err.contains ("newer"), "newer version is rejected clearly");
    check (! loadProjectFile (root.getChildFile ("nope.boombap"), tmp, err), "missing file is reported");

    std::printf ("Forgiving input\n");
    ProjectData minimal;
    check (projectFromJson ("{\"format\":\"boombap-project\",\"version\":1}", projectA, minimal, err), "minimal file loads");
    check (sameSettings (minimal.settings, ProjectSettings()), "missing keys fall back to defaults");

    ProjectData wild;
    check (projectFromJson ("{\"format\":\"boombap-project\",\"version\":1,\"tempo\":{\"bpm\":9999,\"swing\":1},"
                            "\"pattern\":[[500,-4]],\"vocal\":{\"mic\":42},\"export\":{\"bars\":3}}",
                            projectA, wild, err), "out-of-range file loads");
    check (wild.settings.bpm == 220.0f && wild.settings.swing == 50.0f, "tempo is clamped");
    check (wild.settings.patData[0][0][0] == 127 && wild.settings.patData[0][0][1] == 0, "steps are clamped");
    check (wild.settings.vocalMic == 7, "mic index is clamped");
    check (wild.exportBars == 4, "odd export length falls back to 4 bars");

    root.deleteRecursively();
    std::printf (failures == 0 ? "project file checks passed\n" : "%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
