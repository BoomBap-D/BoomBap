#pragma once
// BoomBap project files (.boombap): readable JSON. Depends only on juce_core.
//
// Samples are stored twice, as an absolute path and (when possible) a path relative to
// the project file, so a project folder can be moved or shared and still find its samples.

#include <juce_core/juce_core.h>
#include <array>
#include "Engine/ProjectSettings.h"

namespace boombap
{
constexpr int kProjectFormatVersion = 1;

struct ProjectData
{
    ProjectSettings settings;
    std::array<juce::String, 3> samplePaths;  // absolute path per sample track; empty = unused
    std::array<juce::String, 16> padPaths;    // sample files for the pads (all 16 since
                                              // Round 6 item 109; older files carry pads
                                              // 4-16 here and pads 1-3 in samplePaths)
    // Round 6 (item 109): the sequencer sample ROWS' own files (row
    // index 0 = track Sample1). Rows and pads are separate stores now,
    // so each carries its own path. Files written before the split
    // have no "rowPads" key at all (hasRowPadPaths stays false) and
    // their rows fall back to the pad files - which is exactly what
    // those projects sounded like, one shared store.
    std::array<juce::String, 16> rowPadPaths;
    bool hasRowPadPaths = false;
    // Fix pass 2: files bound directly to a row - the kit rows' replacement
    // samples (item 40, tracks 0-4) and the own-buffer rows past the pad
    // bank (item 39, tracks 21+). Indexed by track; empty = unbound.
    std::array<juce::String, ProjectSettings::kTracks> rowSamplePaths;
    int  exportBars = 4;
    bool exportNormalize = true;
    juce::String vocalTakePath;   // the recorded vocal take's WAV file; empty = no take
};

// projectFile is where the JSON will live (used to write relative sample paths).
juce::String projectToJson (const ProjectData& data, const juce::File& projectFile);

bool projectFromJson (const juce::String& text, const juce::File& projectFile,
                      ProjectData& out, juce::String& error);

bool saveProjectFile (const juce::File& file, const ProjectData& data, juce::String& error);
bool loadProjectFile (const juce::File& file, ProjectData& out, juce::String& error);
} // namespace boombap
