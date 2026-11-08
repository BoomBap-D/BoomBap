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

    setSize (1100, 1082);
}

BoomBapEditor::~BoomBapEditor()
{
    proc.onStateRestored = nullptr;
}

void BoomBapEditor::resized()
{
    main.setBounds (getLocalBounds());
}
