#include "UiComponents.h"

using namespace juce;

//==============================================================================
void StepButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    Colour c = velocity == 0 ? Colour (beatShade ? 0xff262b33 : 0xff1b1f26)
             : velocity < 110 ? Colour (0xffe8863a)
                              : Colour (0xffffc857);
    g.setColour (c);
    g.fillRoundedRectangle (b, 6.0f);

    if (playing)
    {
        g.setColour (Colours::white.withAlpha (0.20f));
        g.fillRoundedRectangle (b, 6.0f);
    }
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);
}

void StepButton::mouseDown (const MouseEvent& e)
{
    int v = 0;
    if (! e.mods.isRightButtonDown())
        v = velocity == 0 ? 90 : (velocity < 110 ? 127 : 0);

    setVelocity (v);
    if (onChange) onChange (v);
}

//==============================================================================
// 24-bit stereo WAV
