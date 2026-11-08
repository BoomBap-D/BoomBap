#include <juce_audio_utils/juce_audio_utils.h>
#include "MainComponent.h"

class BoomBapApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "BoomBap"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override         { mainWindow = std::make_unique<MainWindow> (getApplicationName()); }
    void shutdown() override                               { mainWindow = nullptr; }
    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr) mainWindow->closeButtonPressed();
        else quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, true), true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
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
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (BoomBapApplication)
