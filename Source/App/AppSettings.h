#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>

// Shared access to BoomBap's per-user settings file (the same PropertiesFile
// the recent-projects list lives in) and to the crash-recovery file, which
// sits beside it (Pass 1 items 7 and 8).
namespace boombap
{
inline std::unique_ptr<juce::PropertiesFile> appPropertiesFile()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "BoomBap";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}

// The autosave / crash-recovery project. It only exists on disk when the
// previous session ended without a clean exit and without a manual save
// covering it - both of those delete it.
inline juce::File recoveryFile()
{
    auto props = appPropertiesFile();
    return props->getFile().getParentDirectory().getChildFile ("BoomBap Recovery.boombap");
}

// Sidecar WAV for a vocal take captured in a recovered session.
inline juce::File recoveryTakeFile()
{
    return recoveryFile().getParentDirectory().getChildFile ("BoomBap Recovery - vocal.wav");
}
} // namespace boombap
