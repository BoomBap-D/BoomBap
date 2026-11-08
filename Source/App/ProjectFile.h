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
    std::array<juce::String, 16> padPaths;    // sample files for pads 4-16 (indices 3-15);
                                              // pads 1-3 are the sample tracks above
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
