#include "ProjectFile.h"

using namespace juce;

namespace boombap
{
namespace
{
double numberOr (const var& v, double fallback)
{
    return (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()) ? (double) v : fallback;
}

float  getF (const var& obj, const char* key, float fallback)  { return (float) numberOr (obj.getProperty (key, var()), fallback); }
int    getI (const var& obj, const char* key, int fallback)    { return (int) std::lround (numberOr (obj.getProperty (key, var()), fallback)); }
bool   getB (const var& obj, const char* key, bool fallback)
{
    const var v = obj.getProperty (key, var());
    return v.isBool() ? (bool) v : fallback;
}

var object (std::initializer_list<std::pair<const char*, var>> items)
{
    auto* o = new DynamicObject();
    for (auto& kv : items) o->setProperty (kv.first, kv.second);
    return var (o);
}

Array<var> laneNotes (const int* lane)
{
    Array<var> notes;
    for (int i = 0; i < ProjectSettings::kSteps; ++i) notes.add (lane[i]);
    return notes;
}

Array<var> melodyNotes (const ProjectSettings& s, int pattern)
{
    return laneNotes (s.patMelody[pattern]);
}
} // namespace

String projectToJson (const ProjectData& d, const File& projectFile)
{
    const ProjectSettings& s = d.settings;

    Array<var> patterns, tracks, samples;
    for (int p = 0; p < ProjectSettings::kPatterns; ++p)
    {
        Array<var> rows;
        for (int t = 0; t < ProjectSettings::kTracks; ++t)
        {
            Array<var> row;
            for (int st = 0; st < ProjectSettings::kSteps; ++st) row.add (s.patData[p][t][st]);
            rows.add (var (row));
        }
        patterns.add (object ({ { "steps", var (rows) },
                                { "melody", object ({ { "source", s.patMelodySource[p] },
                                                      { "notes", var (melodyNotes (s, p)) } }) },
                                { "synth", var (laneNotes (s.patSynth[p])) },
                                { "piano", var (laneNotes (s.patPiano[p])) } }));
    }
    for (int t = 0; t < ProjectSettings::kTracks; ++t)
        tracks.add (object ({ { "gain", (double) s.trackGain[t] }, { "mute", s.trackMute[t] }, { "solo", s.trackSolo[t] } }));

    Array<var> chain;
    for (int i = 0; i < ProjectSettings::kSongSlots; ++i) chain.add (s.songChain[i]);

    for (size_t i = 0; i < d.samplePaths.size(); ++i)
    {
        if (d.samplePaths[i].isEmpty()) continue;
        const File f (d.samplePaths[i]);
        const String rel = projectFile == File() ? f.getFullPathName()
                                                 : f.getRelativePathFrom (projectFile.getParentDirectory());
        auto* o = new DynamicObject();
        o->setProperty ("track", 5 + (int) i);
        o->setProperty ("path", f.getFullPathName());
        if (! File::isAbsolutePath (rel)) o->setProperty ("relativePath", rel);
        samples.add (var (o));
    }

    Array<var> padArr;
    for (int i = 0; i < ProjectSettings::kPads; ++i)
    {
        auto* o = new DynamicObject();
        o->setProperty ("pad", i);
        o->setProperty ("start", (double) s.padStart[i]);
        o->setProperty ("end", (double) s.padEnd[i]);
        o->setProperty ("tune", (double) s.padTune[i]);
        o->setProperty ("gain", (double) s.padGain[i]);
        o->setProperty ("velSens", (double) s.padVelSens[i]);
        o->setProperty ("reverse", s.padReverse[i]);
        o->setProperty ("muteGroup", s.padMuteGroup[i]);
        if (i >= 3 && d.padPaths[(size_t) i].isNotEmpty())
        {
            const File f (d.padPaths[(size_t) i]);
            const String rel = projectFile == File() ? f.getFullPathName()
                                                     : f.getRelativePathFrom (projectFile.getParentDirectory());
            o->setProperty ("path", f.getFullPathName());
            if (! File::isAbsolutePath (rel)) o->setProperty ("relativePath", rel);
        }
        padArr.add (var (o));
    }

    var root = object ({
        { "format", "boombap-project" },
        { "version", kProjectFormatVersion },
        { "tempo", object ({ { "bpm", (double) s.bpm }, { "swing", (double) s.swing } }) },
        { "master", object ({ { "gainDb", (double) s.masterDb } }) },
        { "patterns", var (patterns) },
        { "currentPattern", s.currentPattern },
        { "song", object ({ { "length", s.songLength }, { "chain", var (chain) } }) },
        { "tracks", var (tracks) },
        { "drums", object ({ { "kickTuneHz", (double) s.kickTuneHz }, { "kickDecayMs", (double) s.kickDecayMs },
                             { "snareSnappy", (double) s.snareSnappy }, { "openHatDecayMs", (double) s.openHatDecayMs },
                             { "clapTailMs", (double) s.clapTailMs } }) },
        { "dust", object ({ { "on", s.dustOn }, { "rateHz", (double) s.dustRateHz } }) },
        { "glue", object ({ { "on", s.glueOn }, { "thresholdDb", (double) s.glueThresholdDb } }) },
        { "vocal", object ({ { "mic", s.vocalMic }, { "distanceCm", (double) s.vocalDistanceCm },
                             { "inputDb", (double) s.vocalInputDb } }) },
        { "instruments", object ({
            { "synth", object ({ { "wave", s.synthWave }, { "cutoffHz", (double) s.synthCutoffHz },
                                 { "reso", (double) s.synthReso }, { "attackMs", (double) s.synthAttackMs },
                                 { "releaseMs", (double) s.synthReleaseMs }, { "level", (double) s.synthLevel } }) },
            { "piano", object ({ { "bright", (double) s.pianoBright }, { "decayMs", (double) s.pianoDecayMs },
                                 { "level", (double) s.pianoLevel } }) } }) },
        { "samples", var (samples) },
        { "pads", var (padArr) },
        { "export", object ({ { "bars", d.exportBars }, { "normalize", d.exportNormalize } }) }
    });

    if (d.vocalTakePath.isNotEmpty())
    {
        const File f (d.vocalTakePath);
        const String rel = projectFile == File() ? f.getFullPathName()
                                                 : f.getRelativePathFrom (projectFile.getParentDirectory());
        auto* o = new DynamicObject();
        o->setProperty ("path", f.getFullPathName());
        if (! File::isAbsolutePath (rel)) o->setProperty ("relativePath", rel);
        root.getDynamicObject()->setProperty ("vocalTake", var (o));
    }

    return JSON::toString (root, false, 4);
}

bool projectFromJson (const String& text, const File& projectFile, ProjectData& out, String& error)
{
    var root;
    const Result parsed = JSON::parse (text, root);
    if (parsed.failed() || ! root.isObject())
    {
        error = "This isn't a valid BoomBap project file.";
        return false;
    }
    if (root.getProperty ("format", var()).toString() != "boombap-project")
    {
        error = "This file isn't a BoomBap project.";
        return false;
    }
    const int version = getI (root, "version", 0);
    if (version < 1 || version > kProjectFormatVersion)
    {
        error = version > kProjectFormatVersion ? "This project was saved by a newer version of BoomBap."
                                                : "This project file has no valid version.";
        return false;
    }

    ProjectData d; // starts at defaults, so any missing key keeps its default
    ProjectSettings& s = d.settings;

    const var tempo = root.getProperty ("tempo", var());
    s.bpm = getF (tempo, "bpm", s.bpm);
    s.swing = getF (tempo, "swing", s.swing);
    s.masterDb = getF (root.getProperty ("master", var()), "gainDb", s.masterDb);

    if (auto* pats = root.getProperty ("patterns", var()).getArray())
    {
        for (int p = 0; p < ProjectSettings::kPatterns && p < pats->size(); ++p)
        {
            const var& item = (*pats)[p];
            if (auto* rows = item.getProperty ("steps", var()).getArray())
                for (int t = 0; t < ProjectSettings::kTracks && t < rows->size(); ++t)
                {
                    auto* row = (*rows)[t].getArray();
                    if (row == nullptr) continue;
                    for (int st = 0; st < ProjectSettings::kSteps; ++st)
                        s.patData[p][t][st] = st < row->size() ? (int) std::lround (numberOr ((*row)[st], 0.0)) : 0;
                }
            const var mel = item.getProperty ("melody", var());
            s.patMelodySource[p] = getI (mel, "source", s.patMelodySource[p]);
            if (auto* notes = mel.getProperty ("notes", var()).getArray())
                for (int i = 0; i < ProjectSettings::kSteps && i < notes->size(); ++i)
                    s.patMelody[p][i] = (int) std::lround (numberOr ((*notes)[i], 0.0));
            if (auto* syn = item.getProperty ("synth", var()).getArray())
                for (int i = 0; i < ProjectSettings::kSteps && i < syn->size(); ++i)
                    s.patSynth[p][i] = (int) std::lround (numberOr ((*syn)[i], 0.0));
            if (auto* pia = item.getProperty ("piano", var()).getArray())
                for (int i = 0; i < ProjectSettings::kSteps && i < pia->size(); ++i)
                    s.patPiano[p][i] = (int) std::lround (numberOr ((*pia)[i], 0.0));
        }
        s.currentPattern = getI (root, "currentPattern", s.currentPattern);
        const var song = root.getProperty ("song", var());
        s.songLength = getI (song, "length", s.songLength);
        if (auto* chainIn = song.getProperty ("chain", var()).getArray())
            for (int i = 0; i < ProjectSettings::kSongSlots && i < chainIn->size(); ++i)
                s.songChain[i] = (int) std::lround (numberOr ((*chainIn)[i], 0.0));
    }
    else if (auto* rows = root.getProperty ("pattern", var()).getArray())
    {
        // Files from before patterns existed: the single pattern lands in pattern A.
        for (int t = 0; t < ProjectSettings::kTracks && t < rows->size(); ++t)
        {
            auto* row = (*rows)[t].getArray();
            if (row == nullptr) continue;
            for (int st = 0; st < ProjectSettings::kSteps; ++st)
                s.patData[0][t][st] = st < row->size() ? (int) std::lround (numberOr ((*row)[st], 0.0)) : 0;
        }
        const var melody = root.getProperty ("melody", var());
        s.patMelodySource[0] = getI (melody, "source", s.patMelodySource[0]);
        if (auto* notes = melody.getProperty ("notes", var()).getArray())
            for (int i = 0; i < ProjectSettings::kSteps && i < notes->size(); ++i)
                s.patMelody[0][i] = (int) std::lround (numberOr ((*notes)[i], 0.0));
    }

    if (auto* tr = root.getProperty ("tracks", var()).getArray())
        for (int t = 0; t < ProjectSettings::kTracks && t < tr->size(); ++t)
        {
            s.trackGain[t] = getF ((*tr)[t], "gain", s.trackGain[t]);
            s.trackMute[t] = getB ((*tr)[t], "mute", s.trackMute[t]);
            s.trackSolo[t] = getB ((*tr)[t], "solo", s.trackSolo[t]);
        }

    const var drums = root.getProperty ("drums", var());
    s.kickTuneHz = getF (drums, "kickTuneHz", s.kickTuneHz);
    s.kickDecayMs = getF (drums, "kickDecayMs", s.kickDecayMs);
    s.snareSnappy = getF (drums, "snareSnappy", s.snareSnappy);
    s.openHatDecayMs = getF (drums, "openHatDecayMs", s.openHatDecayMs);
    s.clapTailMs = getF (drums, "clapTailMs", s.clapTailMs);

    const var dust = root.getProperty ("dust", var()), glue = root.getProperty ("glue", var()),
              vocal = root.getProperty ("vocal", var());
    s.dustOn = getB (dust, "on", s.dustOn);
    s.dustRateHz = getF (dust, "rateHz", s.dustRateHz);
    s.glueOn = getB (glue, "on", s.glueOn);
    s.glueThresholdDb = getF (glue, "thresholdDb", s.glueThresholdDb);
    s.vocalMic = getI (vocal, "mic", s.vocalMic);
    s.vocalDistanceCm = getF (vocal, "distanceCm", s.vocalDistanceCm);
    s.vocalInputDb = getF (vocal, "inputDb", s.vocalInputDb);

    const var inst = root.getProperty ("instruments", var());
    const var syn = inst.getProperty ("synth", var()), pia = inst.getProperty ("piano", var());
    s.synthWave = getI (syn, "wave", s.synthWave);
    s.synthCutoffHz = getF (syn, "cutoffHz", s.synthCutoffHz);
    s.synthReso = getF (syn, "reso", s.synthReso);
    s.synthAttackMs = getF (syn, "attackMs", s.synthAttackMs);
    s.synthReleaseMs = getF (syn, "releaseMs", s.synthReleaseMs);
    s.synthLevel = getF (syn, "level", s.synthLevel);
    s.pianoBright = getF (pia, "bright", s.pianoBright);
    s.pianoDecayMs = getF (pia, "decayMs", s.pianoDecayMs);
    s.pianoLevel = getF (pia, "level", s.pianoLevel);

    if (auto* padArrIn = root.getProperty ("pads", var()).getArray())
    {
        for (const var& item : *padArrIn)
        {
            const int idx = getI (item, "pad", -1);
            if (idx < 0 || idx >= ProjectSettings::kPads) continue;
            s.padStart[idx] = getF (item, "start", s.padStart[idx]);
            s.padEnd[idx] = getF (item, "end", s.padEnd[idx]);
            s.padTune[idx] = getF (item, "tune", s.padTune[idx]);
            s.padGain[idx] = getF (item, "gain", s.padGain[idx]);
            s.padVelSens[idx] = getF (item, "velSens", s.padVelSens[idx]);
            s.padReverse[idx] = getB (item, "reverse", s.padReverse[idx]);
            s.padMuteGroup[idx] = getI (item, "muteGroup", s.padMuteGroup[idx]);
        }
    }

    s.sanitize();

    if (auto* samples = root.getProperty ("samples", var()).getArray())
    {
        for (const var& item : *samples)
        {
            const int slot = getI (item, "track", -1) - 5;
            if (slot < 0 || slot >= (int) d.samplePaths.size()) continue;

            const String rel = item.getProperty ("relativePath", var()).toString();
            const String abs = item.getProperty ("path", var()).toString();

            File resolved;
            if (rel.isNotEmpty())
            {
                const File viaRelative = projectFile.getParentDirectory().getChildFile (rel);
                if (viaRelative.existsAsFile()) resolved = viaRelative;
            }
            if (resolved == File() && abs.isNotEmpty())
                resolved = File (abs);
            d.samplePaths[(size_t) slot] = resolved.getFullPathName();
        }
    }

    if (auto* padArrIn = root.getProperty ("pads", var()).getArray())
    {
        for (const var& item : *padArrIn)
        {
            const int idx = getI (item, "pad", -1);
            if (idx < 3 || idx >= ProjectSettings::kPads) continue;
            const String rel = item.getProperty ("relativePath", var()).toString();
            const String abs = item.getProperty ("path", var()).toString();
            if (rel.isEmpty() && abs.isEmpty()) continue;

            File resolved;
            if (rel.isNotEmpty())
            {
                const File viaRelative = projectFile.getParentDirectory().getChildFile (rel);
                if (viaRelative.existsAsFile()) resolved = viaRelative;
            }
            if (resolved == File() && abs.isNotEmpty())
                resolved = File (abs);
            d.padPaths[(size_t) idx] = resolved.getFullPathName();
        }
    }

    {
        const var vt = root.getProperty ("vocalTake", var());
        if (vt.isObject())
        {
            const String rel = vt.getProperty ("relativePath", var()).toString();
            const String abs = vt.getProperty ("path", var()).toString();
            File resolved;
            if (rel.isNotEmpty())
            {
                const File viaRelative = projectFile.getParentDirectory().getChildFile (rel);
                if (viaRelative.existsAsFile()) resolved = viaRelative;
            }
            if (resolved == File() && abs.isNotEmpty())
                resolved = File (abs);
            d.vocalTakePath = resolved.getFullPathName();
        }
    }

    const var ex = root.getProperty ("export", var());
    d.exportBars = getI (ex, "bars", d.exportBars);
    if (d.exportBars != 1 && d.exportBars != 2 && d.exportBars != 4 && d.exportBars != 8) d.exportBars = 4;
    d.exportNormalize = getB (ex, "normalize", d.exportNormalize);

    out = d;
    return true;
}

bool saveProjectFile (const File& file, const ProjectData& data, String& error)
{
    if (! file.getParentDirectory().exists())
    {
        error = "The folder doesn't exist.";
        return false;
    }
    // replaceWithText writes to a temporary file first, so a failed save can't corrupt the old one
    if (! file.replaceWithText (projectToJson (data, file), false, false, "\n"))
    {
        error = "Couldn't write " + file.getFileName() + ". Check the folder's permissions.";
        return false;
    }
    return true;
}

bool loadProjectFile (const File& file, ProjectData& out, String& error)
{
    if (! file.existsAsFile())
    {
        error = "Couldn't find " + file.getFileName() + ".";
        return false;
    }
    return projectFromJson (file.loadFileAsString(), file, out, error);
}
} // namespace boombap
