#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// BoomBap's modern-clean skin: near-black blue-grey panels, soft borders,
// one warm amber accent (a nod to the classic hardware), flat rounded
// controls, thin slider tracks and round thumbs. No skeuomorphism.
namespace boombap::ui
{
inline juce::Colour bg()       { return juce::Colour (0xff0f1115); }
inline juce::Colour panel()    { return juce::Colour (0xff171a20); }
inline juce::Colour panelAlt() { return juce::Colour (0xff1e232c); }
inline juce::Colour border()   { return juce::Colour (0xff2b3038); }
inline juce::Colour text()     { return juce::Colour (0xffe9ebee); }
inline juce::Colour muted()    { return juce::Colour (0xff9aa3ad); }
inline juce::Colour accent()   { return juce::Colour (0xfff5a524); }
inline juce::Colour accentHi() { return juce::Colour (0xffffc857); }
// Round 1 (item 85): an auxiliary window's title bar is a step LIGHTER
// than the main window's bar (plus an amber trace along its top edge),
// so a popped-out window reads as its own object with a grabbable bar
// instead of blending into the program behind it.
inline juce::Colour frameBar() { return panelAlt().brighter (0.05f); }

// Component-property keys the frame/skin code shares (Round 1):
//  - auxWindowProperty: set on every auxiliary DocumentWindow by
//    applyAppWindowFrame; the title-bar painter branches on it.
//  - actionButtonProperty: set on buttons that DO something (open,
//    load, add) by applyActionTrace; the button painter branches on it.
inline const char* auxWindowProperty()    { return "boombapAuxWindow"; }
inline const char* actionButtonProperty() { return "boombapAction"; }
inline bool isAuxWindow (const juce::Component& c)
{
    return c.getProperties().contains (auxWindowProperty());
}

// Round 1 (item 89): the thin amber trace that marks a button as an
// ACTION (POP OUT, ADD TRACK, LOAD, the window openers) - actionable
// controls stand out from passive readouts at a glance. One shared
// helper so later rounds reuse the same language.
inline void applyActionTrace (juce::Button& b)
{
    b.getProperties().set (actionButtonProperty(), true);
    b.repaint();
}
inline bool hasActionTrace (const juce::Button& b)
{
    return b.getProperties().contains (actionButtonProperty());
}
}

// Round 1 (item 85): BoomBap's own title-bar button. JUCE V4's stock
// button paints its glyph in dark red / dark mustard / dark green -
// on this app's near-black bar the owner could not see the minimise /
// maximise / close buttons at all ("a little brighter amber so you
// can see them"). Same shapes and behaviour as the stock button, but
// the glyphs are the app's bright amber, drawn a touch bolder, over
// the bar itself (no grey chip); hover fills the chip amber and the
// glyph goes dark, so the button you are about to press is obvious.
class BoomBapWindowButton final : public juce::Button
{
public:
    BoomBapWindowButton (const juce::String& name, juce::Colour glyph,
                         const juce::Path& normal, const juce::Path& toggled)
        : juce::Button (name), glyphColour (glyph), normalShape (normal), toggledShape (toggled)
    {
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        if (shouldDrawButtonAsDown)
        {
            g.setColour (glyphColour.darker (0.35f));
            g.fillRoundedRectangle (bounds, 5.0f);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            g.setColour (glyphColour);
            g.fillRoundedRectangle (bounds, 5.0f);
        }

        g.setColour (shouldDrawButtonAsHighlighted && ! shouldDrawButtonAsDown
                         ? juce::Colour (0xff14100a)
                         : (isEnabled() ? glyphColour : glyphColour.withAlpha (0.45f)));

        const auto& p = getToggleState() ? toggledShape : normalShape;
        auto reducedRect = juce::Justification (juce::Justification::centred)
                               .appliedToRectangle (juce::Rectangle<int> (getHeight(), getHeight()),
                                                    getLocalBounds())
                               .toFloat()
                               .reduced ((float) getHeight() * 0.27f);
        g.fillPath (p, p.getTransformToScaleToFit (reducedRect, true));
    }

private:
    juce::Colour glyphColour;
    juce::Path normalShape, toggledShape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoomBapWindowButton)
};

class BoomBapLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BoomBapLookAndFeel()
    {
        using namespace boombap::ui;
        setColour (juce::ResizableWindow::backgroundColourId, bg());
        setColour (juce::TextButton::buttonColourId, panelAlt());
        setColour (juce::TextButton::buttonOnColourId, accent());
        setColour (juce::TextButton::textColourOffId, text());
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff14100a));
        setColour (juce::ToggleButton::textColourId, text());
        setColour (juce::ToggleButton::tickColourId, accent());
        setColour (juce::ToggleButton::tickDisabledColourId, muted());
        setColour (juce::Label::textColourId, text());
        setColour (juce::Slider::backgroundColourId, panelAlt());
        setColour (juce::Slider::trackColourId, accent());
        setColour (juce::Slider::thumbColourId, text());
        setColour (juce::Slider::textBoxTextColourId, text());
        setColour (juce::Slider::textBoxBackgroundColourId, panel());
        setColour (juce::Slider::textBoxOutlineColourId, border());
        setColour (juce::ComboBox::backgroundColourId, panelAlt());
        setColour (juce::ComboBox::textColourId, text());
        setColour (juce::ComboBox::outlineColourId, border());
        setColour (juce::ComboBox::arrowColourId, muted());
        setColour (juce::ComboBox::buttonColourId, panelAlt());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::textColourId, text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent());
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff14100a));
        setColour (juce::DocumentWindow::textColourId, text());
    }

    // Round 1 (item 85): the title-bar buttons, in bright amber (see
    // BoomBapWindowButton above) instead of V4's dark stock colours.
    juce::Button* createDocumentWindowButton (int buttonType) override
    {
        using namespace boombap::ui;
        juce::Path shape;
        const auto thickness = 0.24f; // bolder than stock: findable at a glance

        if (buttonType == juce::DocumentWindow::closeButton)
        {
            shape.addLineSegment ({ 0.0f, 0.0f, 1.0f, 1.0f }, thickness);
            shape.addLineSegment ({ 1.0f, 0.0f, 0.0f, 1.0f }, thickness);
            return new BoomBapWindowButton ("close", accentHi(), shape, shape);
        }

        if (buttonType == juce::DocumentWindow::minimiseButton)
        {
            shape.addLineSegment ({ 0.0f, 0.5f, 1.0f, 0.5f }, thickness);
            return new BoomBapWindowButton ("minimise", accentHi(), shape, shape);
        }

        if (buttonType == juce::DocumentWindow::maximiseButton)
        {
            shape.addLineSegment ({ 0.5f, 0.0f, 0.5f, 1.0f }, thickness);
            shape.addLineSegment ({ 0.0f, 0.5f, 1.0f, 0.5f }, thickness);

            juce::Path fullscreenShape;
            fullscreenShape.startNewSubPath (45.0f, 100.0f);
            fullscreenShape.lineTo (0.0f, 100.0f);
            fullscreenShape.lineTo (0.0f, 0.0f);
            fullscreenShape.lineTo (100.0f, 0.0f);
            fullscreenShape.lineTo (100.0f, 45.0f);
            fullscreenShape.addRectangle (45.0f, 45.0f, 100.0f, 100.0f);
            juce::PathStrokeType (30.0f).createStrokedPath (fullscreenShape, fullscreenShape);

            return new BoomBapWindowButton ("maximise", accentHi(), shape, fullscreenShape);
        }

        jassertfalse;
        return nullptr;
    }

    // Fix Pass 4 (item 77): the title bar every BoomBap window draws for
    // itself - flat panel charcoal from the app palette, a hairline
    // divider along the bottom, the title in the app's text colour
    // (muted when the window is inactive). The layout follows the stock
    // JUCE bar (icon, centred-or-left title, room reserved for the
    // buttons) so only the colours change hands: the OS theme never
    // paints any part of this app again.
    // Round 1 (item 85): an AUXILIARY window's bar is a step lighter
    // (frameBar) with a thin amber trace along its top edge - a popped
    // window separates from the main window behind it and its bar
    // reads as a handle. The main window keeps the dark panel() bar:
    // separation, not a redesign.
    void drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                     int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override
    {
        using namespace boombap::ui;
        if (w * h == 0)
            return;

        const bool isActive = window.isActiveWindow();
        const bool aux = isAuxWindow (window);

        g.setColour (aux ? frameBar() : panel());
        g.fillAll();
        if (aux)
        {
            g.setColour (accent().withAlpha (0.55f));
            g.fillRect (0, 0, w, 2);
        }
        g.setColour (border());
        g.fillRect (0, h - 1, w, 1);

        juce::Font font ((float) h * 0.62f, juce::Font::bold);
        g.setFont (font);

        int textW = font.getStringWidth (window.getName());
        int iconW = 0;
        int iconH = 0;

        if (icon != nullptr)
        {
            iconH = (int) font.getHeight();
            iconW = icon->getWidth() * iconH / icon->getHeight() + 4;
        }

        textW = juce::jmin (titleSpaceW, textW + iconW);
        int textX = drawTitleTextOnLeft ? titleSpaceX
                                        : juce::jmax (titleSpaceX, (w - textW) / 2);

        if (textX + textW > titleSpaceX + titleSpaceW)
            textX = titleSpaceX + titleSpaceW - textW;

        if (icon != nullptr)
        {
            g.setOpacity (isActive ? 1.0f : 0.6f);
            g.drawImageWithin (*icon, textX, (h - iconH) / 2, iconW, iconH,
                               juce::RectanglePlacement::centred, false);
            textX += iconW;
            textW -= iconW;
        }

        g.setColour (isActive ? text() : muted());
        g.drawText (window.getName(), textX, 0, textW, h,
                    juce::Justification::centredLeft, true);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (13.5f);
    }

    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override
    {
        // Pass 1 item 2: the transport buttons are findable from across the room.
        const auto text = button.getButtonText();
        if (text.contains ("PLAY") || text.contains ("STOP"))
            return juce::Font (juce::jmin (24.0f, (float) buttonHeight * 0.5f), juce::Font::bold);
        return juce::Font (13.5f);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (13.5f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool drawAsHighlighted, bool drawAsDown) override
    {
        auto b = button.getLocalBounds().toFloat().reduced (0.5f);
        auto c = backgroundColour;
        if (drawAsDown) c = c.darker (0.25f);
        else if (drawAsHighlighted) c = c.brighter (0.12f);
        g.setColour (c);
        g.fillRoundedRectangle (b, 8.0f);
        // Round 1 (item 89): action buttons carry a thin amber outline
        // instead of the passive grey one (see applyActionTrace).
        if (boombap::ui::hasActionTrace (button))
        {
            g.setColour (boombap::ui::accent().withAlpha (0.9f));
            g.drawRoundedRectangle (b, 8.0f, 1.4f);
        }
        else
        {
            g.setColour (boombap::ui::border().withAlpha (0.6f));
            g.drawRoundedRectangle (b, 8.0f, 1.0f);
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool drawAsHighlighted, bool drawAsDown) override
    {
        using namespace boombap::ui;
        auto b = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();
        auto fill = on ? accent().withAlpha (0.16f) : panelAlt();
        if (drawAsDown) fill = fill.darker (0.2f);
        else if (drawAsHighlighted) fill = fill.brighter (0.1f);
        g.setColour (fill);
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (on ? accent().withAlpha (0.55f) : border().withAlpha (0.6f));
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        // status dot
        const float cy = b.getCentreY();
        g.setColour (on ? accent() : muted().withAlpha (0.5f));
        g.fillEllipse (b.getX() + 9.0f, cy - 3.0f, 6.0f, 6.0f);

        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (juce::Font (13.5f));
        g.drawText (button.getButtonText(),
                    (int) b.getX() + 22, (int) b.getY(), (int) b.getWidth() - 24, (int) b.getHeight(),
                    juce::Justification::centredLeft);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        using namespace boombap::ui;
        const float cy = (float) y + (float) height * 0.5f;
        const float trackH = 4.0f;
        juce::Rectangle<float> track ((float) x, cy - trackH * 0.5f, (float) width, trackH);
        g.setColour (border());
        g.fillRoundedRectangle (track, trackH * 0.5f);
        auto filled = track.withWidth (juce::jmax (trackH, sliderPos - (float) x));
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (filled, trackH * 0.5f);

        const float r = 7.0f;
        juce::Rectangle<float> thumb (sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (panelAlt());
        g.fillEllipse (thumb);
        g.setColour (slider.isMouseOverOrDragging() ? accent() : text().withAlpha (0.85f));
        g.drawEllipse (thumb, 1.5f);
        g.setColour (accent());
        g.fillEllipse (sliderPos - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int, int, int, int, juce::ComboBox& box) override
    {
        using namespace boombap::ui;
        juce::Rectangle<float> b (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        g.setColour (isButtonDown ? panelAlt().darker (0.2f) : box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (border().withAlpha (0.6f));
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        // chevron
        const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
        juce::Path p;
        p.startNewSubPath (cx - 4.0f, cy - 2.0f);
        p.lineTo (cx, cy + 2.5f);
        p.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (muted());
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

namespace boombap::ui
{
// Fix Pass 4 - the frame every BoomBap window wears (items 77/78).
//  - Item 77: the window stops borrowing the OS title bar (on the
//    owner's Windows 8.1 that bar paints beige, whatever the app looks
//    like) and draws its own, painted by BoomBapLookAndFeel in the
//    app's dark palette above. Drag / resize / minimise / maximise /
//    close all stay JUCE's own machinery, unchanged.
//  - Item 78: the main window is always the background. Every auxiliary
//    window floats above it for as long as it is open, so opening one
//    view can never bury another behind the program. JUCE's public API
//    offers no Win32 owner-HWND relationship for app windows (a desktop
//    component has no component parent to own it), so the mechanism is
//    JUCE always-on-top scoped to the auxiliary windows only: on
//    Windows that is the HWND_TOPMOST band - above every non-topmost
//    window, the main window included - and windows inside the band
//    still order among themselves by last touch, which is exactly the
//    owner's rule. The main window passes false: it never floats.
inline void applyAppWindowFrame (juce::DocumentWindow& window, bool floatAboveMainWindow)
{
    window.setUsingNativeTitleBar (false);
    window.setColour (juce::DocumentWindow::textColourId, text());
    window.setAlwaysOnTop (floatAboveMainWindow);
    // Round 1 (item 85): mark the auxiliary windows so the title-bar
    // painter gives them the lighter, amber-traced bar.
    if (floatAboveMainWindow)
        window.getProperties().set (auxWindowProperty(), true);
}
} // namespace boombap::ui
