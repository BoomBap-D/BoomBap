#include <juce_audio_utils/juce_audio_utils.h>
#include "MainComponent.h"

class BoomBapApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "BoomBap"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        // Fix Pass 4 (item 77): the app's own look and feel is the desktop
        // default, so the main window's self-drawn title bar is painted in
        // the BoomBap palette like every other window's.
        juce::Desktop::getInstance().setDefaultLookAndFeel (&appLookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
    }
    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr) mainWindow->closeButtonPressed();
        else quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, boombap::ui::bg(), DocumentWindow::allButtons)
        {
            // Fix Pass 4 (items 77/78): BoomBap draws this window's own
            // dark title bar instead of borrowing the OS one - and the
            // main window does NOT float: it is the background every
            // other window floats above.
            boombap::ui::applyAppWindowFrame (*this, false);
            auto* content = new MainComponent (engine, true);
            setContentOwned (content, true);
            // Pass 1 item 1: resizable, and never bigger than the display's
            // usable area (screen minus taskbar) - the old fixed 1100x1082
            // window ran off the top and bottom of a 1080p screen.
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto area = display != nullptr ? display->userArea
                                                 : juce::Rectangle<int> (0, 0, 1920, 1080);
            setResizeLimits (1000, 620, area.getWidth(), area.getHeight());
            setResizable (true, true);
            const int w = juce::jmin (1560, area.getWidth());
            const int h = juce::jmin (900, area.getHeight());
            setBounds (area.withSizeKeepingCentre (w, h));
            setVisible (true);
            content->checkForRecovery();
        }

        void closeButtonPressed() override
        {
            // offers to save unsaved changes first
            if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
                main->confirmDiscardThen ([] { juce::JUCEApplication::getInstance()->quit(); });
            else
                juce::JUCEApplication::getInstance()->quit();
        }

    private:
        boombap::BoomBapEngine engine; // outlives the component, which borrows it
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    BoomBapLookAndFeel appLookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (BoomBapApplication)
