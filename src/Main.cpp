#include <JuceHeader.h>
#include "MainComponent.h"
#include "CrashLog.h"
#include "SplashComponent.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name)
        : DocumentWindow(name,
                          juce::Colours::black,
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(false);
        setResizable(true, true);

        // Show splash first, then load main content
        splash = std::make_unique<SplashComponent>();
        splash->onFinished = [this] {
            // Defer the swap so we're not destroying ourselves mid-callback
            juce::MessageManager::callAsync([this] {
                auto savedBounds = getBounds();
                splash = nullptr;
                setContentOwned(new MainComponent(), true);
                setBounds(savedBounds);
            });
        };

        setContentNonOwned(splash.get(), false);
        centreWithSize(1280, 800);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    std::unique_ptr<SplashComponent> splash;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class SequencerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Legion Stage"; }
    const juce::String getApplicationVersion() override { return "1.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        CrashLog::install();

        juce::File oldExe("C:\\Program Files\\Legion Stage\\Legion Stage.exe.old");
        if (oldExe.existsAsFile())
            oldExe.deleteFile();

        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SequencerApplication)
