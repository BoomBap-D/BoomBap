#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>

// BoomBap's recent-projects list: the last ten .boombap files opened or
// saved, most recent first, persisted in the app's settings file so the
// list survives restarts. A thin wrapper over JUCE's
// RecentlyOpenedFilesList plus a PropertiesFile; the settings file can be
// injected (the tests do) so behaviour is verifiable headlessly.
class RecentProjects
{
public:
    RecentProjects()
        : RecentProjects (defaultProperties())
    {
    }

    explicit RecentProjects (std::unique_ptr<juce::PropertiesFile> properties)
        : props (std::move (properties))
    {
        list.setMaxNumberOfItems (10);
        if (props != nullptr)
            list.restoreFromString (props->getValue ("recentProjects"));
    }

    void add (const juce::File& file)
    {
        if (file == juce::File())
            return;
        list.addFile (file); // dedupes and moves it to the front
        save();
    }

    void remove (const juce::File& file)
    {
        list.removeFile (file);
        save();
    }

    void clear()
    {
        list.clear();
        save();
    }

    // Drops entries whose files have gone (deleted, renamed, drive away).
    void pruneMissing()
    {
        const int before = list.getNumFiles();
        list.removeNonExistentFiles();
        if (list.getNumFiles() != before)
            save();
    }

    int getNumFiles() const { return list.getNumFiles(); }
    juce::File getFile (int index) const { return list.getFile (index); }

private:
    static std::unique_ptr<juce::PropertiesFile> defaultProperties()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "BoomBap";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        return std::make_unique<juce::PropertiesFile> (options);
    }

    void save()
    {
        if (props == nullptr)
            return;
        props->setValue ("recentProjects", list.toString());
        props->save(); // write now, not on a timer - a crash shouldn't lose it
    }

    std::unique_ptr<juce::PropertiesFile> props;
    juce::RecentlyOpenedFilesList list;
};
