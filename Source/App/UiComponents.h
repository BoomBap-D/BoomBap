#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "BoomBapLookAndFeel.h" // for the boombap::ui colour helpers

// Small reusable building-block components for the main window:
// the step-grid cell, the per-row and glue meters, and the sample
// waveform strip. Moved out of MainComponent.h in the file split;
// behaviour unchanged.

// One cell of the step grid. Click: off -> normal -> accent -> off. Right-click clears.
class StepButton : public juce::Component
{
public:
    std::function<void (int)> onChange; // called with the new velocity (0, 90 or 127)

    void setVelocity (int v)  { if (velocity != v) { velocity = v; repaint(); } }
    void setBeatShade (bool b) { beatShade = b; }
    void setPlaying (bool p)  { if (playing != p) { playing = p; repaint(); } }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    int velocity = 0;
    bool beatShade = false, playing = false;
};

// Slim horizontal peak meter for one track row.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newLevel)
    {
        if (std::abs (newLevel - level) > 0.002f) { level = newLevel; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (boombap::ui::border().withAlpha (0.45f));
        g.fillRoundedRectangle (b, 3.0f);
        const float frac = std::sqrt (std::clamp (level, 0.0f, 1.0f));
        if (frac > 0.01f)
        {
            g.setColour (level > 0.95f ? juce::Colour (0xffe5484d) : boombap::ui::accent());
            g.fillRoundedRectangle (b.withWidth (std::max (6.0f, b.getWidth() * frac)), 3.0f);
        }
    }

private:
    float level = 0.0f;
};

// Gain-reduction meter for the Glue bus compressor (0 to 15 dB scale).
class GrMeter : public juce::Component
{
public:
    void setReduction (float db)
    {
        if (std::abs (db - reduction) > 0.05f) { reduction = db; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (boombap::ui::border().withAlpha (0.45f));
        g.fillRoundedRectangle (b, 3.0f);
        const float frac = std::clamp (reduction / 15.0f, 0.0f, 1.0f);
        if (frac > 0.01f)
        {
            g.setColour (juce::Colour (0xffe8863a));
            g.fillRoundedRectangle (b.withWidth (std::max (6.0f, b.getWidth() * frac)), 3.0f);
        }
    }

private:
    float reduction = 0.0f;
};

// Waveform display for one loaded sample (mono peaks, drawn per pixel column).
class WaveformView : public juce::Component
{
public:
    void setData (std::vector<float> samples) { data = std::move (samples); repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (boombap::ui::panelAlt().darker (0.35f));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (boombap::ui::border().withAlpha (0.5f));
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        if (data.empty())
        {
            g.setColour (boombap::ui::muted());
            g.setFont (13.0f);
            g.drawText ("Load a sample into S1, S2 or S3 to see its waveform",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }

        const float mid = b.getCentreY();
        const float h = b.getHeight() * 0.44f;
        g.setColour (boombap::ui::muted().withAlpha (0.4f));
        g.drawLine (b.getX() + 6.0f, mid, b.getRight() - 6.0f, mid, 1.0f);

        g.setColour (boombap::ui::accent());
        const int cols = (int) b.getWidth() - 12;
        if (cols <= 0) return;
        const double per = (double) data.size() / (double) cols;
        for (int x = 0; x < cols; ++x)
        {
            const int start = (int) (x * per);
            const int end = std::min ((int) data.size(), std::max (start + 1, (int) ((x + 1) * per)));
            float lo = 0.0f, hi = 0.0f;
            for (int i = start; i < end; ++i)
            {
                lo = std::min (lo, data[(size_t) i]);
                hi = std::max (hi, data[(size_t) i]);
            }
            g.drawLine ((float) x + 6.0f, mid - hi * h, (float) x + 6.0f, mid - lo * h, 1.0f);
        }
    }

private:
    std::vector<float> data;
};
