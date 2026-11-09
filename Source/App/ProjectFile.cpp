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
long long getL (const var& obj, const char* key, long long fallback) { return (long long) std::llround (numberOr (obj.getProperty (key, var()), (double) fallback)); }
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

    // Fix pass item 35: the audio track (mixer state in track order, after
    // the instrument rows) and its clips (regions only - the audio itself is
    // the take WAV). Older files simply have neither key.
    Array<var> audioTracksArr, audioClipsArr;
    for (int i = 0; i < ProjectSettings::kAudioTracks; ++i)
        audioTracksArr.add (object ({ { "gain", (double) s.audioGain[i] },
                                      { "mute", s.audioMute[i] }, { "solo", s.audioSolo[i] } }));
    for (int i = 0; i < ProjectSettings::kMaxAudioClips; ++i)
    {
        if (! s.audioClips[i].active) continue;
        audioClipsArr.add (object ({ { "slot", i },
                                      { "start", (double) s.audioClips[i].startSamples },
                                      { "sourceStart", (double) s.audioClips[i].sourceStart },
                                      { "length", (double) s.audioClips[i].lengthSamples },
                                      { "track", s.audioClips[i].track } }));
    }

    // Fix pass 2: rows bound directly to a file (kit replacements + the
    // own-buffer rows), and the track-list size. Older files have neither.
    Array<var> rowSamples;
    for (int t = 0; t < ProjectSettings::kTracks; ++t)
    {
        if (d.rowSamplePaths[(size_t) t].isEmpty()) continue;
        const bool direct = (t <= 4) || (t >= 21);
        if (! direct) continue;
        const File f (d.rowSamplePaths[(size_t) t]);
        const String rel = projectFile == File() ? f.getFullPathName()
                                                 : f.getRelativePathFrom (projectFile.getParentDirectory());
        auto* o = new DynamicObject();
        o->setProperty ("track", t);
        o->setProperty ("path", f.getFullPathName());
        if (! File::isAbsolutePath (rel)) o->setProperty ("relativePath", rel);
        rowSamples.add (var (o));
    }

    // Pass 2: the Station - per-channel strips (only channels whose strip
    // differs from default are written), the pans, the group channels and
    // the master limiter. Older files have no "station" key at all.
    Array<var> stripArr;
    for (int ch = 0; ch < ProjectSettings::kNumChannels; ++ch)
    {
        const auto& st = s.strips[ch];
        if (st.isDefault()) continue;
        Array<var> eqArr;
        for (int b = 0; b < st.eqNumBands; ++b)
        {
            Array<var> band;
            band.add (st.eq[b].on ? 1 : 0);
            band.add (st.eq[b].type);
            band.add ((double) st.eq[b].freqHz);
            band.add ((double) st.eq[b].gainDb);
            band.add ((double) st.eq[b].q);
            eqArr.add (var (band));
        }
        stripArr.add (object ({
            { "ch", ch },
            { "preampOn", st.preampOn }, { "preampDb", (double) st.preampDb },
            { "hpfOn", st.hpfOn }, { "hpfHz", (double) st.hpfHz },
            { "deessOn", st.deessOn }, { "deessAmount", (double) st.deessAmount },
            { "compOn", st.compOn }, { "compThresholdDb", (double) st.compThresholdDb },
            { "compRatio", (double) st.compRatio }, { "compAttackMs", (double) st.compAttackMs },
            { "compReleaseMs", (double) st.compReleaseMs },
            { "satOn", st.satOn }, { "satDrive", (double) st.satDrive },
            { "limiterOn", st.limiterOn }, { "limiterCeilingDb", (double) st.limiterCeilingDb },
            { "eq", var (eqArr) } }));
    }
    Array<var> trackPans, audioPans;
    for (int t = 0; t < ProjectSettings::kTracks; ++t) trackPans.add ((double) s.trackPan[t]);
    for (int i = 0; i < ProjectSettings::kAudioTracks; ++i) audioPans.add ((double) s.audioPan[i]);
    var stationObj = object ({
        { "strips", var (stripArr) },
        { "trackPans", var (trackPans) },
        { "audioPans", var (audioPans) },
        { "synth", object ({ { "pan", (double) s.synthPan }, { "mute", s.synthMute }, { "solo", s.synthSolo } }) },
        { "piano", object ({ { "pan", (double) s.pianoPan }, { "mute", s.pianoMute }, { "solo", s.pianoSolo } }) },
        { "padBus", object ({ { "gain", (double) s.padBusGain }, { "pan", (double) s.padBusPan },
                              { "mute", s.padBusMute } }) },
        { "vocal", object ({ { "gain", (double) s.vocalGain }, { "pan", (double) s.vocalPan },
                             { "mute", s.vocalMute }, { "solo", s.vocalSolo } }) },
        { "masterLimiter", object ({ { "on", s.masterLimiterOn },
                                     { "ceilingDb", (double) s.masterLimiterCeilingDb } }) } });

    var root = object ({
        { "format", "boombap-project" },
        { "version", kProjectFormatVersion },
        { "numTracks", s.numTracks },
        { "numAudioTracks", s.numAudioTracks },
        { "rowSamples", var (rowSamples) },
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
        { "station", stationObj },
        { "samples", var (samples) },
        { "pads", var (padArr) },
        { "audioTracks", var (audioTracksArr) },
        { "audioClips", var (audioClipsArr) },
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
    s.numTracks = getI (root, "numTracks", s.numTracks); // fix pass 2 (older files: the default 8)
    s.numAudioTracks = getI (root, "numAudioTracks", s.numAudioTracks); // item 60 (older files: 1)

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

    if (auto* rs = root.getProperty ("rowSamples", var()).getArray())
    {
        for (const var& item : *rs)
        {
            const int track = getI (item, "track", -1);
            if (track < 0 || track >= ProjectSettings::kTracks) continue;
            if (! (track <= 4 || track >= 21)) continue;
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
            d.rowSamplePaths[(size_t) track] = resolved.getFullPathName();
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

    if (auto* at = root.getProperty ("audioTracks", var()).getArray())
        for (int i = 0; i < ProjectSettings::kAudioTracks && i < at->size(); ++i)
        {
            s.audioGain[i] = getF ((*at)[i], "gain", s.audioGain[i]);
            s.audioMute[i] = getB ((*at)[i], "mute", s.audioMute[i]);
            s.audioSolo[i] = getB ((*at)[i], "solo", s.audioSolo[i]);
        }

    if (auto* ac = root.getProperty ("audioClips", var()).getArray())
        for (const var& item : *ac)
        {
            const int slot = getI (item, "slot", -1);
            if (slot < 0 || slot >= ProjectSettings::kMaxAudioClips) continue;
            s.audioClips[slot].startSamples  = getL (item, "start", 0);
            s.audioClips[slot].sourceStart   = getL (item, "sourceStart", 0);
            s.audioClips[slot].lengthSamples = getL (item, "length", 0);
            s.audioClips[slot].track         = getI (item, "track", 0); // item 60 (older files: track 0)
            s.audioClips[slot].active        = s.audioClips[slot].lengthSamples > 0;
        }

    // Pass 2: the Station (older files have no key; defaults stand).
    {
        const var station = root.getProperty ("station", var());
        if (station.isObject())
        {
            if (auto* tp = station.getProperty ("trackPans", var()).getArray())
                for (int t = 0; t < ProjectSettings::kTracks && t < tp->size(); ++t)
                    s.trackPan[t] = (float) numberOr ((*tp)[t], s.trackPan[t]);
            if (auto* ap = station.getProperty ("audioPans", var()).getArray())
                for (int i = 0; i < ProjectSettings::kAudioTracks && i < ap->size(); ++i)
                    s.audioPan[i] = (float) numberOr ((*ap)[i], s.audioPan[i]);
            const var syn2 = station.getProperty ("synth", var()), pia2 = station.getProperty ("piano", var());
            s.synthPan = getF (syn2, "pan", s.synthPan);
            s.synthMute = getB (syn2, "mute", s.synthMute);
            s.synthSolo = getB (syn2, "solo", s.synthSolo);
            s.pianoPan = getF (pia2, "pan", s.pianoPan);
            s.pianoMute = getB (pia2, "mute", s.pianoMute);
            s.pianoSolo = getB (pia2, "solo", s.pianoSolo);
            const var pb = station.getProperty ("padBus", var());
            s.padBusGain = getF (pb, "gain", s.padBusGain);
            s.padBusPan = getF (pb, "pan", s.padBusPan);
            s.padBusMute = getB (pb, "mute", s.padBusMute);
            const var vc = station.getProperty ("vocal", var());
            s.vocalGain = getF (vc, "gain", s.vocalGain);
            s.vocalPan = getF (vc, "pan", s.vocalPan);
            s.vocalMute = getB (vc, "mute", s.vocalMute);
            s.vocalSolo = getB (vc, "solo", s.vocalSolo);
            const var ml = station.getProperty ("masterLimiter", var());
            s.masterLimiterOn = getB (ml, "on", s.masterLimiterOn);
            s.masterLimiterCeilingDb = getF (ml, "ceilingDb", s.masterLimiterCeilingDb);

            if (auto* stripsIn = station.getProperty ("strips", var()).getArray())
                for (const var& item : *stripsIn)
                {
                    const int ch = getI (item, "ch", -1);
                    if (ch < 0 || ch >= ProjectSettings::kNumChannels) continue;
                    auto& st = s.strips[ch];
                    st.preampOn = getB (item, "preampOn", st.preampOn);
                    st.preampDb = getF (item, "preampDb", st.preampDb);
                    st.hpfOn = getB (item, "hpfOn", st.hpfOn);
                    st.hpfHz = getF (item, "hpfHz", st.hpfHz);
                    st.deessOn = getB (item, "deessOn", st.deessOn);
                    st.deessAmount = getF (item, "deessAmount", st.deessAmount);
                    st.compOn = getB (item, "compOn", st.compOn);
                    st.compThresholdDb = getF (item, "compThresholdDb", st.compThresholdDb);
                    st.compRatio = getF (item, "compRatio", st.compRatio);
                    st.compAttackMs = getF (item, "compAttackMs", st.compAttackMs);
                    st.compReleaseMs = getF (item, "compReleaseMs", st.compReleaseMs);
                    st.satOn = getB (item, "satOn", st.satOn);
                    st.satDrive = getF (item, "satDrive", st.satDrive);
                    st.limiterOn = getB (item, "limiterOn", st.limiterOn);
                    st.limiterCeilingDb = getF (item, "limiterCeilingDb", st.limiterCeilingDb);
                    if (auto* eqIn = item.getProperty ("eq", var()).getArray())
                    {
                        st.eqNumBands = std::min (StripSettings::kEqBands, eqIn->size());
                        for (int b = 0; b < st.eqNumBands; ++b)
                        {
                            auto* band = (*eqIn)[b].getArray();
                            if (band == nullptr || band->size() < 5) continue;
                            st.eq[b].on     = numberOr ((*band)[0], 0.0) > 0.5;
                            st.eq[b].type   = (int) std::lround (numberOr ((*band)[1], 0.0));
                            st.eq[b].freqHz = (float) numberOr ((*band)[2], 1000.0);
                            st.eq[b].gainDb = (float) numberOr ((*band)[3], 0.0);
                            st.eq[b].q      = (float) numberOr ((*band)[4], 1.0);
                        }
                    }
                }
        }
    }
    s.sanitize();

    const var ex = root.getProperty ("export", var());
    d.exportBars = getI (ex, "bars", d.exportBars);
    // 100 = "Whole song" (Fix pass item 34): render the full playlist length.
    if (d.exportBars != 1 && d.exportBars != 2 && d.exportBars != 4 && d.exportBars != 8
        && d.exportBars != 100) d.exportBars = 4;
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
