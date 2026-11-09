#pragma once
#include <juce_core/juce_core.h>

// Item 62: BoomBap has a known FILE HOME - the user's Documents\BoomBap,
// with Projects / Takes / Exports inside, created on first run in both the
// standalone and the plugin. Recorded takes are real WAV files in Takes;
// a .boombap project references them, it never hides them. Where a thing
// lives is never a mystery: the audio view shows this path.
namespace boombap
{
inline juce::File dataHome()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("BoomBap");
}
inline juce::File projectsDir() { return dataHome().getChildFile ("Projects"); }
inline juce::File takesDir()    { return dataHome().getChildFile ("Takes"); }
inline juce::File exportsDir()  { return dataHome().getChildFile ("Exports"); }
inline juce::File presetsDir()  { return dataHome().getChildFile ("Presets"); } // Pass 2: strip presets

inline void ensureDataHome()
{
    for (const auto& d : { dataHome(), projectsDir(), takesDir(), exportsDir(), presetsDir() })
        d.createDirectory();
}

// A fresh, collision-free name for a take that just landed.
inline juce::File newTakeFile()
{
    const auto now = juce::Time::getCurrentTime();
    const juce::String stamp = now.formatted ("%Y-%m-%d %H%M%S");
    auto f = takesDir().getChildFile ("Take " + stamp + ".wav");
    for (int n = 2; f.existsAsFile() && n < 100; ++n)
        f = takesDir().getChildFile ("Take " + stamp + " (" + juce::String (n) + ").wav");
    return f;
}
} // namespace boombap
