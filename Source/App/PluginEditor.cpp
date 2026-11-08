#include "PluginEditor.h"

BoomBapEditor::BoomBapEditor (BoomBapProcessor& p)
    : AudioProcessorEditor (p), proc (p), main (p.engine, false)
{
    main.onMediaChanged = [this] (const boombap::ProjectData& d) { proc.setMediaData (d); };
    proc.onStateRestored = [this] (const boombap::ProjectData& d) { main.loadProjectData (d, false, true); };
    addAndMakeVisible (main);

    // The host may have restored state before the editor was opened; bring
    // the UI in line with the project the processor is already holding.
    main.loadProjectData (proc.captureStateData(), false, true);

    // Item 59: the editor opens on exactly ONE view - the step sequencer,
    // the standalone's default - settled explicitly before first paint.
    main.showInitialTab();

    // Pass 1 item 1: same landscape fit as the standalone - clamped to the
    // display's usable area, and resizable in the host.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = display != nullptr ? display->userArea
                                         : juce::Rectangle<int> (0, 0, 1920, 1080);
    setResizable (true, true);
    setResizeLimits (1000, 620, area.getWidth(), area.getHeight());
    setSize (juce::jmin (1560, area.getWidth()), juce::jmin (900, area.getHeight()));
}

BoomBapEditor::~BoomBapEditor()
{
    proc.onStateRestored = nullptr;
}

void BoomBapEditor::resized()
{
    main.setBounds (getLocalBounds());
}
