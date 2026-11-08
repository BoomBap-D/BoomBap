// Tests for BoomBap's recent-projects list: ordering, dedupe, persistence
// across a restart (a fresh RecentProjects over the same settings file),
// removal, pruning of missing files, and clearing.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/App/RecentProjects.h"
#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("BoomBapRecentTest");
    dir.deleteRecursively();
    dir.createDirectory();

    auto makeFile = [&dir] (const char* name)
    {
        auto f = dir.getChildFile (name);
        f.create();
        return f;
    };
    const auto a = makeFile ("Alpha.boombap");
    const auto b = makeFile ("Beta.boombap");
    const auto c = makeFile ("Gamma.boombap");

    const auto settingsFile = dir.getChildFile ("test.settings");
    auto makeProps = [&settingsFile]
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "BoomBapRecentTest";
        options.filenameSuffix = ".settings";
        return std::make_unique<juce::PropertiesFile> (settingsFile, options);
    };

    {
        RecentProjects recent (makeProps());
        check (recent.getNumFiles() == 0, "starts empty");

        recent.add (a);
        recent.add (b);
        recent.add (c);
        check (recent.getNumFiles() == 3, "three projects stored");
        check (recent.getFile (0) == c && recent.getFile (1) == b && recent.getFile (2) == a,
               "most recent first");

        recent.add (a);
        check (recent.getNumFiles() == 3 && recent.getFile (0) == a,
               "re-adding moves to the front without duplicating");
    }

    {
        RecentProjects recent (makeProps());
        check (recent.getNumFiles() == 3 && recent.getFile (0) == a && recent.getFile (2) == b,
               "list survives a restart (reloaded from the settings file)");

        recent.remove (a);
        check (recent.getNumFiles() == 2 && recent.getFile (0) == c, "remove works");

        b.deleteFile();
        recent.pruneMissing();
        check (recent.getNumFiles() == 1 && recent.getFile (0) == c,
               "missing files are pruned");

        recent.clear();
        check (recent.getNumFiles() == 0, "clear empties the list");
    }

    {
        RecentProjects recent (makeProps());
        check (recent.getNumFiles() == 0, "clear survives a restart too");
    }

    dir.deleteRecursively();
    std::printf (failures == 0 ? "recent projects checks passed\n" : "recent projects checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
